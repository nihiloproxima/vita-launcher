#include "apps.h"

#include "covers.h"
#include "idlist.h"
#include "sfo.h"

#include <math.h>
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_DIR    "ux0:data/vita-launcher"
#define PINNED_FILE DATA_DIR "/pinned.txt"
#define RECENT_FILE DATA_DIR "/recent.txt"
#define LAUNCH_REQUEST DATA_DIR "/launch.txt" // lu par le plugin de démarrage
#define TAI_CONFIG  "ur0:tai/config.txt"
#define BLUR_SIZE   16

App apps[MAX_APPS];
int app_count;
static IdList pinned, recent;

// Arrondit les coins (alpha anticrénelé) : icônes carrées, bulles rondes et jaquettes ont la même silhouette.
static void round_corners(vita2d_texture *t) {
	unsigned w = vita2d_texture_get_width(t), h = vita2d_texture_get_height(t);
	unsigned stride = vita2d_texture_get_stride(t) / 4;
	uint32_t *px = vita2d_texture_get_datap(t);
	float r = w * 0.22f;
	for (unsigned y = 0; y < h; y++) {
		for (unsigned x = 0; x < w; x++) {
			float fx = x + 0.5f, fy = y + 0.5f;
			float cx = fx < r ? r : (fx > w - r ? w - r : fx);
			float cy = fy < r ? r : (fy > h - r ? h - r : fy);
			float cover = r - sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) + 0.5f;
			if (cover >= 1)
				continue;
			uint32_t p = px[y * stride + x];
			px[y * stride + x] = (p & 0x00FFFFFF) | ((uint32_t)((p >> 24) * (cover > 0 ? cover : 0)) << 24);
		}
	}
}

// Couleur dominante (pondérée par la saturation pour éviter le gris) et version floue 16x16 de l'icône.
static void analyze_icon(App *a) {
	unsigned w = vita2d_texture_get_width(a->icon), h = vita2d_texture_get_height(a->icon);
	unsigned stride = vita2d_texture_get_stride(a->icon) / 4;
	uint32_t *px = vita2d_texture_get_datap(a->icon);

	a->blur = vita2d_create_empty_texture(BLUR_SIZE, BLUR_SIZE);
	unsigned bstride = vita2d_texture_get_stride(a->blur) / 4;
	uint32_t *bpx = vita2d_texture_get_datap(a->blur);
	vita2d_texture_set_filters(a->blur, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);

	uint64_t tr = 0, tg = 0, tb = 0, tw = 0;
	for (unsigned by = 0; by < BLUR_SIZE; by++) {
		for (unsigned bx = 0; bx < BLUR_SIZE; bx++) {
			uint32_t r = 0, g = 0, b = 0, c = 0;
			for (unsigned y = by * h / BLUR_SIZE; y < (by + 1) * h / BLUR_SIZE; y++) {
				for (unsigned x = bx * w / BLUR_SIZE; x < (bx + 1) * w / BLUR_SIZE; x++) {
					uint32_t p = px[y * stride + x];
					if ((p >> 24) < 128)
						continue;
					uint32_t pr = p & 0xFF, pg = (p >> 8) & 0xFF, pb = (p >> 16) & 0xFF;
					r += pr; g += pg; b += pb; c++;
					uint32_t mx = pr > pg ? (pr > pb ? pr : pb) : (pg > pb ? pg : pb);
					uint32_t mn = pr < pg ? (pr < pb ? pr : pb) : (pg < pb ? pg : pb);
					uint32_t weight = mx - mn + 8;
					tr += pr * weight; tg += pg * weight; tb += pb * weight; tw += weight;
				}
			}
			bpx[by * bstride + bx] = c ? RGBA8(r / c, g / c, b / c, 255) : RGBA8(0, 0, 0, 0);
		}
	}
	if (tw) {
		a->r = tr / tw; a->g = tg / tw; a->b = tb / tw;
	}
}

// Les jaquettes sont en portrait (250x320) : on garde le carré central, ce qui retire le bandeau « PS Vita ».
static vita2d_texture *square_crop(vita2d_texture *t) {
	unsigned w = vita2d_texture_get_width(t), h = vita2d_texture_get_height(t);
	if (w == h)
		return t;
	unsigned s = w < h ? w : h, ox = (w - s) / 2, oy = (h - s) / 2;
	vita2d_texture *sq = vita2d_create_empty_texture(s, s);
	if (!sq)
		return t;
	unsigned src_stride = vita2d_texture_get_stride(t) / 4, dst_stride = vita2d_texture_get_stride(sq) / 4;
	uint32_t *src = vita2d_texture_get_datap(t), *dst = vita2d_texture_get_datap(sq);
	for (unsigned y = 0; y < s; y++)
		memcpy(dst + y * dst_stride, src + (y + oy) * src_stride + ox, s * 4);
	vita2d_free_texture(t);
	return sq;
}

static int set_icon(App *a, vita2d_texture *t) {
	if (!t)
		return 0;
	t = square_crop(t);
	vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	round_corners(t);
	a->icon = t;
	analyze_icon(a);
	a->icon_state = ICON_READY;
	a->appear = 0;
	return 1;
}

static void load_icon(App *a) {
	char path[256];
	// ux0:app d'abord : en clair pour les homebrews ; ux0:appmeta et les jeux du commerce sont chiffrés (PFS)
	snprintf(path, sizeof(path), "ux0:app/%.9s/sce_sys/icon0.png", a->id);
	if (set_icon(a, vita2d_load_PNG_file(path)))
		return;
	snprintf(path, sizeof(path), "ux0:appmeta/%.9s/icon0.png", a->id);
	if (set_icon(a, vita2d_load_PNG_file(path)))
		return;
	if (covers_cached(a->id, path, sizeof(path)) && set_icon(a, vita2d_load_PNG_file(path)))
		return;
	a->icon_state = ICON_NONE;
	if (!covers_known_missing(a->id))
		covers_request(a->id);
}

int apps_load_icons(int budget) {
	int remaining = 0;
	for (int i = 0; i < app_count; i++) {
		if (apps[i].icon_state != ICON_PENDING)
			continue;
		if (budget-- > 0)
			load_icon(&apps[i]);
		else
			remaining++;
	}
	char id[16], path[96];
	while (covers_poll(id)) {
		int i = apps_find(id);
		if (i >= 0 && apps[i].icon_state == ICON_NONE && covers_cached(id, path, sizeof(path)))
			set_icon(&apps[i], vita2d_load_PNG_file(path));
	}
	return remaining;
}

// Insensible à la casse ASCII, octets comparés non signés : les titres non latins passent après.
static int title_cmp(const char *a, const char *b) {
	const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
	for (;; x++, y++) {
		int cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x, cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;
		if (cx != cy || !cx)
			return cx - cy;
	}
}

// Ordre d'affichage : épinglées (dans l'ordre d'épinglage), récentes (la dernière lancée d'abord), puis par nom.
static int compare_apps(const void *x, const void *y) {
	const App *a = x, *b = y;
	int pa = idlist_index(&pinned, a->id), pb = idlist_index(&pinned, b->id);
	int ra = idlist_index(&recent, a->id), rb = idlist_index(&recent, b->id);
	int group_a = pa >= 0 ? 0 : ra >= 0 ? 1 : 2, group_b = pb >= 0 ? 0 : rb >= 0 ? 1 : 2;
	if (group_a != group_b)
		return group_a - group_b;
	if (group_a == 0)
		return pa - pb;
	if (group_a == 1)
		return ra - rb;
	return title_cmp(a->title, b->title);
}

void apps_scan(void) {
	sceIoMkdir(DATA_DIR, 0777);
	idlist_load(&pinned, PINNED_FILE);
	idlist_load(&recent, RECENT_FILE);
	SceUID dir = sceIoDopen("ux0:app");
	if (dir < 0)
		return;
	SceIoDirent ent;
	while (app_count < MAX_APPS && sceIoDread(dir, &ent) > 0) {
		if (!SCE_S_ISDIR(ent.d_stat.st_mode) || strlen(ent.d_name) != 9 || strcmp(ent.d_name, SELF_TITLEID) == 0)
			continue;
		App *a = &apps[app_count];
		memset(a, 0, sizeof(*a));
		snprintf(a->id, sizeof(a->id), "%s", ent.d_name);
		char path[256];
		snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", a->id);
		if (read_sfo_string(path, "TITLE", a->title, sizeof(a->title)) < 0)
			continue; // dossier vide ou cassé : pas une appli lançable
		read_sfo_string(path, "APP_VER", a->version, sizeof(a->version));
		a->is_game = strncmp(a->id, "PCS", 3) == 0;
		a->pinned = idlist_index(&pinned, a->id) >= 0;
		a->r = 60; a->g = 60; a->b = 72;
		app_count++;
	}
	sceIoDclose(dir);
	qsort(apps, app_count, sizeof(App), compare_apps);
}

int apps_find(const char *id) {
	for (int i = 0; i < app_count; i++)
		if (strcmp(apps[i].id, id) == 0)
			return i;
	return -1;
}

// Le plugin de démarrage (plugin/boot.c) est-il dans la config taiHEN ?
// shortcut: une ligne commentée compte aussi, à affiner si ça gêne quelqu'un.
int apps_boot_plugin(void) {
	static char buf[8192];
	FILE *f = fopen(TAI_CONFIG, "r");
	if (!f)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return strstr(buf, "launcher_boot.suprx") != NULL;
}

// Avec le plugin, c'est le shell qui ferme le launcher puis lance l'appli : pas de confirmation
// « fermer l'appli en cours ? » affichée par-dessus l'accueil Sony. Renvoie 0 sans plugin.
static int request_launch(const char *id) {
	if (!apps_boot_plugin())
		return 0;
	FILE *f = fopen(LAUNCH_REQUEST, "w");
	if (!f)
		return 0;
	fputs(id, f);
	fclose(f);
	return 1;
}

int apps_reload(void) {
	return request_launch(SELF_TITLEID);
}

int apps_launch(const App *a) {
	idlist_push_front(&recent, a->id);
	idlist_save(&recent, RECENT_FILE);
	if (request_launch(a->id))
		return 1;
	// Sans plugin, même séquence que VitaShell : le double appel évite un échec quand le shell n'est pas prêt.
	char uri[64];
	snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", a->id);
	sceKernelDelayThread(10000);
	sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	sceKernelDelayThread(10000);
	sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	sceKernelExitProcess(0);
	return 0; // jamais atteint
}

void apps_toggle_pin(const char *id) {
	if (idlist_index(&pinned, id) >= 0)
		idlist_remove(&pinned, id);
	else
		idlist_push_back(&pinned, id);
	idlist_save(&pinned, PINNED_FILE);
	for (int i = 0; i < app_count; i++)
		apps[i].pinned = idlist_index(&pinned, apps[i].id) >= 0;
	qsort(apps, app_count, sizeof(App), compare_apps);
}

int apps_uninstall(const char *id) {
	int i = apps_find(id);
	if (i < 0)
		return -1;
	sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	int ret = scePromoterUtilityInit();
	if (ret >= 0) {
		ret = scePromoterUtilityDeletePkg(id);
		scePromoterUtilityExit();
	}
	if (ret < 0)
		return ret;
	vita2d_wait_rendering_done(); // le GPU ne doit plus lire les textures qu'on libère
	if (apps[i].icon)
		vita2d_free_texture(apps[i].icon);
	if (apps[i].blur)
		vita2d_free_texture(apps[i].blur);
	memmove(&apps[i], &apps[i + 1], (app_count - i - 1) * sizeof(App));
	app_count--;
	idlist_remove(&pinned, id);
	idlist_remove(&recent, id);
	idlist_save(&pinned, PINNED_FILE);
	idlist_save(&recent, RECENT_FILE);
	return 0;
}
