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
#include <psp2/rtc.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DATA_DIR    "ux0:data/vita-launcher"
#define PINNED_FILE DATA_DIR "/pinned.txt"
#define RECENT_FILE DATA_DIR "/recent.txt"
#define PLAYTIME_FILE DATA_DIR "/playtime.txt" // écrit par le plugin de démarrage : « ID heure secondes » par tranche
#define LAUNCH_REQUEST DATA_DIR "/launch.txt" // lu par le plugin de démarrage
#define TAI_CONFIG  "ur0:tai/config.txt"
#define BLUR_SIZE   16
#define ICON_MAX    256 // côté max d'une icône en mémoire : les jaquettes rétro font 512 px et plus
#define RETROARCH   "RETROVITA"

// Systèmes lancés par RetroArch : cœurs par défaut de RetroFlow, jaquettes de libretro-thumbnails.
static const struct {
	Kind kind;
	char prefix; // première lettre de l'identifiant des ROMs
	const char *dir, *exts[4], *core, *thumbs;
} systems[] = {
	{KIND_GBA, 'g', "ux0:/roms/gba", {"gba", "zip"}, "app0:/gpsp_libretro.self", "Nintendo_-_Game_Boy_Advance"},
	// shortcut: un jeu multidisque apparaît pour chaque .cue en plus de son .m3u, à filtrer si ça gêne
	{KIND_PS1, 'p', "ux0:/roms/ps1", {"chd", "cue", "pbp", "m3u"}, "app0:/pcsx_rearmed_libretro.self", "Sony_-_PlayStation"},
};
#define SYSTEM_COUNT (int)(sizeof(systems) / sizeof(systems[0]))

App apps[MAX_APPS];
int app_count;
unsigned int apps_week[7];
int apps_today;
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

// Les jaquettes ne sont pas carrées (Vita : 250x320) : on garde le carré central, ce qui retire le bandeau « PS Vita »,
// réduit à ICON_MAX au plus (moyenne des pixels couverts).
static vita2d_texture *square_crop(vita2d_texture *t) {
	unsigned w = vita2d_texture_get_width(t), h = vita2d_texture_get_height(t);
	unsigned s = w < h ? w : h, d = s < ICON_MAX ? s : ICON_MAX, ox = (w - s) / 2, oy = (h - s) / 2;
	if (w == h && d == s)
		return t;
	vita2d_texture *sq = vita2d_create_empty_texture(d, d);
	if (!sq)
		return t;
	unsigned src_stride = vita2d_texture_get_stride(t) / 4, dst_stride = vita2d_texture_get_stride(sq) / 4;
	uint32_t *src = vita2d_texture_get_datap(t), *dst = vita2d_texture_get_datap(sq);
	for (unsigned y = 0; y < d; y++) {
		for (unsigned x = 0; x < d; x++) {
			uint32_t sum[4] = {0}, n = 0;
			for (unsigned sy = y * s / d; sy < (y + 1) * s / d; sy++) {
				for (unsigned sx = x * s / d; sx < (x + 1) * s / d; sx++, n++) {
					uint32_t p = src[(sy + oy) * src_stride + sx + ox];
					for (int c = 0; c < 4; c++)
						sum[c] += (p >> (8 * c)) & 0xFF;
				}
			}
			dst[y * dst_stride + x] = RGBA8(sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n);
		}
	}
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

static int system_of(const App *a) {
	for (int s = 0; s < SYSTEM_COUNT; s++)
		if (systems[s].kind == a->kind)
			return s;
	return -1;
}

// Jaquette : base HexFlow-Covers par title ID, ou libretro-thumbnails par nom de ROM (les caractères &*/:`<>?\|
// y sont remplacés par _), encodé pour l'URL.
static void cover_url(const App *a, char *url, int size) {
	int s = system_of(a);
	if (s < 0) {
		snprintf(url, size, "https://raw.githubusercontent.com/Andiweli/HexFlow-Covers/main/Covers/PSVita/%.9s.png", a->id);
		return;
	}
	int n = snprintf(url, size, "https://raw.githubusercontent.com/libretro-thumbnails/%s/master/Named_Boxarts/", systems[s].thumbs);
	const char *name = strrchr(a->path, '/') + 1, *dot = strrchr(name, '.');
	for (const char *c = name; c < dot && n < size - 8; c++) {
		unsigned char ch = strchr("&*/:`<>?\\|", *c) ? '_' : *c;
		if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || strchr("-._~", ch))
			url[n++] = ch;
		else
			n += snprintf(url + n, size - n, "%%%02X", ch);
	}
	snprintf(url + n, size - n, ".png");
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
	if (!covers_known_missing(a->id)) {
		char url[512];
		cover_url(a, url, sizeof(url));
		covers_request(a->id, url);
	}
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

// Ordre d'affichage, par onglet : épinglées (dans l'ordre d'épinglage), récentes (la dernière lancée d'abord), puis par nom.
static int compare_apps(const void *x, const void *y) {
	const App *a = x, *b = y;
	if (app_tab(a) != app_tab(b))
		return app_tab(a) - app_tab(b);
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

// Une ligne tronquée (coupure pendant l'écriture) est ignorée, pas le reste du journal.
static void load_playtime(void) {
	SceDateTime dt;
	SceUInt64 now = 0;
	sceRtcGetCurrentClockLocalTime(&dt);
	sceRtcGetTime64_t(&dt, &now); // même horloge que le plugin : heure locale en secondes
	unsigned int today = (unsigned int)(now / 86400);
	apps_today = (today + 4) % 7; // le 1er janvier 1970 était un jeudi
	FILE *f = fopen(PLAYTIME_FILE, "r");
	if (!f)
		return;
	char line[64], id[16];
	unsigned int when, secs;
	while (fgets(line, sizeof(line), f)) {
		int i = sscanf(line, "%15s %u %u", id, &when, &secs) == 3 ? apps_find(id) : -1;
		if (i < 0)
			continue;
		apps[i].played += secs;
		if (when > apps[i].last_played)
			apps[i].last_played = when;
		// une tranche compte pour le jour où elle a été notée ; non signé : une date future est écartée aussi
		unsigned int ago = today - when / 86400;
		if (app_tab(&apps[i]) == 0 && ago < 7) {
			apps[i].week += secs;
			apps_week[6 - ago] += secs;
		}
	}
	fclose(f);
}

static int exists(const char *path) {
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

// pic0.png (960x544) puis fond de la LiveArea (840x500), lisibles dans ux0:app pour les homebrews et les bulles
// d'Adrenaline ; chiffrés pour les jeux du commerce, que vita2d refuse dès l'en-tête.
vita2d_texture *apps_load_hero(const App *a) {
	static const char *const files[] = {"pic0.png", "livearea/contents/bg.png"};
	if (a->path[0])
		return NULL;
	for (int i = 0; i < 2; i++) {
		char path[96];
		snprintf(path, sizeof(path), "ux0:app/%.9s/sce_sys/%s", a->id, files[i]);
		vita2d_texture *t = exists(path) ? vita2d_load_PNG_file(path) : NULL;
		if (t) {
			vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
			return t;
		}
	}
	return NULL;
}

// Identifiant d'une ROM : lettre du système et empreinte FNV-1a du nom de fichier, en minuscules comme aucun title ID.
static void rom_id(char id[16], char prefix, const char *name) {
	uint32_t h = 2166136261u;
	for (const unsigned char *c = (const unsigned char *)name; *c; c++)
		h = (h ^ *c) * 16777619u;
	snprintf(id, 16, "%c%08lx", prefix, (unsigned long)h);
}

static int has_ext(const char *name, const char *const exts[4]) {
	const char *dot = strrchr(name, '.');
	for (int i = 0; dot && i < 4 && exts[i]; i++)
		if (strcasecmp(dot + 1, exts[i]) == 0)
			return 1;
	return 0;
}

// shortcut: les ROMs comptent dans MAX_APPS et toutes les icônes restent en mémoire (256 Ko chacune) ;
// au-delà de quelques centaines de jeux, passer à une bibliothèque qui charge les icônes à la demande.
static void scan_roms(void) {
	sceIoMkdir("ux0:/roms", 0777); // créés d'avance : on voit où déposer les ROMs par FTP
	for (int s = 0; s < SYSTEM_COUNT; s++)
		sceIoMkdir(systems[s].dir, 0777);
	if (!exists("ux0:app/" RETROARCH))
		return;
	for (int s = 0; s < SYSTEM_COUNT; s++) {
		SceUID dir = sceIoDopen(systems[s].dir);
		if (dir < 0)
			continue;
		SceIoDirent ent;
		while (app_count < MAX_APPS && sceIoDread(dir, &ent) > 0) {
			// RetroArch coupe ses paramètres au « & » : une ROM qui en contient ne se lancerait pas
			if (SCE_S_ISDIR(ent.d_stat.st_mode) || !has_ext(ent.d_name, systems[s].exts) || strchr(ent.d_name, '&'))
				continue;
			App *a = &apps[app_count];
			memset(a, 0, sizeof(*a));
			if (snprintf(a->path, sizeof(a->path), "%s/%s", systems[s].dir, ent.d_name) >= (int)sizeof(a->path))
				continue;
			rom_id(a->id, systems[s].prefix, ent.d_name);
			// titre : le nom sans extension ni étiquettes No-Intro, « (USA, Europe) » ou « [!] »
			snprintf(a->title, sizeof(a->title), "%.*s", (int)(strrchr(ent.d_name, '.') - ent.d_name), ent.d_name);
			char *end = strpbrk(a->title, "([");
			if (end > a->title)
				*end = 0;
			for (end = a->title + strlen(a->title); end > a->title && end[-1] == ' ';)
				*--end = 0;
			a->kind = systems[s].kind;
			a->pinned = idlist_index(&pinned, a->id) >= 0;
			a->r = 60; a->g = 60; a->b = 72;
			app_count++;
		}
		sceIoDclose(dir);
	}
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
		// data/boot.bin : bulle d'Adrenaline Bubble Manager, qui ouvre un jeu PSP (ou PS1) dans Adrenaline
		// shortcut: une bulle PS1 est aussi présentée comme « Jeu PSP », lire le chemin du jeu dans boot.bin si ça gêne
		snprintf(path, sizeof(path), "ux0:app/%s/data/boot.bin", a->id);
		a->kind = exists(path) ? KIND_PSP : strncmp(a->id, "PCS", 3) == 0 ? KIND_VITA : KIND_APP;
		a->pinned = idlist_index(&pinned, a->id) >= 0;
		a->r = 60; a->g = 60; a->b = 72;
		app_count++;
	}
	sceIoDclose(dir);
	scan_roms();
	load_playtime();
	qsort(apps, app_count, sizeof(App), compare_apps);
}

const char *apps_kind(const App *a) {
	static const char *const labels[] = {
		[KIND_APP] = "Homebrew", [KIND_VITA] = "Jeu PS Vita", [KIND_PSP] = "Jeu PSP",
		[KIND_GBA] = "Game Boy Advance", [KIND_PS1] = "PlayStation",
	};
	return labels[a->kind];
}

const char *apps_last_launched(void) {
	return recent.count ? recent.ids[0] : NULL;
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
// target : « TITLEID[&param=…] » ; key : ID sous lequel le plugin note le temps de jeu, s'il diffère du title ID.
static int request_launch(const char *target, const char *key) {
	if (!apps_boot_plugin())
		return 0;
	FILE *f = fopen(LAUNCH_REQUEST, "w");
	if (!f)
		return 0;
	fputs(target, f);
	if (key)
		fprintf(f, "\n%s", key);
	fclose(f);
	return 1;
}

int apps_reload(void) {
	return request_launch(SELF_TITLEID, NULL);
}

int apps_launch(const App *a) {
	idlist_push_front(&recent, a->id);
	idlist_save(&recent, RECENT_FILE);
	// une ROM s'ouvre dans RetroArch avec son cœur (paramètres lus par RetroArch, frontend/drivers/platform_psp.c)
	char target[384];
	int s = system_of(a);
	if (s >= 0)
		snprintf(target, sizeof(target), RETROARCH "&param=%s&param2=%s", systems[s].core, a->path);
	else
		snprintf(target, sizeof(target), "%s", a->id);
	if (request_launch(target, s >= 0 ? a->id : NULL))
		return 1;
	// Sans plugin, même séquence que VitaShell : le double appel évite un échec quand le shell n'est pas prêt.
	char uri[416];
	snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", target);
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
