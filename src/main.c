#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <vita2d.h>
#include "input.h"
#include "panel.h"
#include "sfo.h"
#include "sys.h"
#include "ui.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#define MAX_APPS 256

#define ICON_BASE  104.0f
#define ICON_FOCUS 148.0f
#define ICON_GAP   20.0f
#define ROW_CY     430.0f
#define BLUR_SIZE  16

typedef struct {
	char id[16];
	char title[128];
	int is_game;
	vita2d_texture *icon;
	vita2d_texture *blur; // icône réduite à 16x16 : agrandie avec filtrage linéaire, elle donne un fond flou
	uint8_t r, g, b;
	float focus; // 0 = repos, 1 = sélectionnée (animé)
} App;

static App apps[MAX_APPS];
static int app_count;

// Arrondit les coins de l'icône (alpha anticrénelé) : icônes carrées et bulles rondes ont la même silhouette.
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
	} else {
		a->r = 60; a->g = 60; a->b = 72;
	}
}

static int compare_apps(const void *x, const void *y) {
	return strcasecmp(((const App *)x)->title, ((const App *)y)->title);
}

static void scan_apps(void) {
	SceUID dir = sceIoDopen("ux0:app");
	if (dir < 0)
		return;
	SceIoDirent ent;
	while (app_count < MAX_APPS && sceIoDread(dir, &ent) > 0) {
		if (!SCE_S_ISDIR(ent.d_stat.st_mode) || strlen(ent.d_name) != 9 || strcmp(ent.d_name, SELF_TITLEID) == 0)
			continue;
		App *a = &apps[app_count];
		memset(a, 0, sizeof(*a));
		strcpy(a->id, ent.d_name);
		char path[256];
		snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", a->id);
		if (read_sfo_title(path, a->title, sizeof(a->title)) < 0)
			continue; // dossier vide ou cassé : pas une appli lançable
		a->is_game = strncmp(a->id, "PCS", 3) == 0;
		// ux0:app d'abord : en clair pour les homebrews ; ux0:appmeta est souvent chiffré (PFS)
		snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/icon0.png", a->id);
		a->icon = vita2d_load_PNG_file(path);
		if (!a->icon) {
			snprintf(path, sizeof(path), "ux0:appmeta/%s/icon0.png", a->id);
			a->icon = vita2d_load_PNG_file(path);
		}
		if (a->icon) {
			vita2d_texture_set_filters(a->icon, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
			round_corners(a->icon);
			analyze_icon(a);
		} else {
			a->r = 60; a->g = 60; a->b = 72;
		}
		app_count++;
	}
	sceIoDclose(dir);
	qsort(apps, app_count, sizeof(App), compare_apps);
}

// Même séquence que VitaShell : le double appel évite un échec quand le shell n'est pas prêt.
static void launch(const App *a) {
	char uri[64];
	snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", a->id);
	sceKernelDelayThread(10000);
	sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	sceKernelDelayThread(10000);
	sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	sceKernelExitProcess(0);
}

// Dégradé noir de 256 pixels (vertical ou horizontal), étiré à l'écran au rendu.
static vita2d_texture *make_gradient(int vertical, int alpha_from, int alpha_to) {
	vita2d_texture *t = vertical ? vita2d_create_empty_texture(1, 256) : vita2d_create_empty_texture(256, 1);
	unsigned stride = vita2d_texture_get_stride(t) / 4;
	uint32_t *px = vita2d_texture_get_datap(t);
	for (int i = 0; i < 256; i++) {
		float k = i / 255.0f;
		px[vertical ? i * stride : i] = RGBA8(0, 0, 0, (int)(alpha_from + (alpha_to - alpha_from) * k * k));
	}
	vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return t;
}

// Ombre portée : carré de 32 px au centre d'une texture de 64 px, bords en flou gaussien.
#define SHADOW_TEX 64
#define SHADOW_BOX 32
static vita2d_texture *make_shadow(void) {
	vita2d_texture *t = vita2d_create_empty_texture(SHADOW_TEX, SHADOW_TEX);
	unsigned stride = vita2d_texture_get_stride(t) / 4;
	uint32_t *px = vita2d_texture_get_datap(t);
	float lo = (SHADOW_TEX - SHADOW_BOX) / 2.0f, hi = lo + SHADOW_BOX, sigma = 6;
	for (int y = 0; y < SHADOW_TEX; y++) {
		for (int x = 0; x < SHADOW_TEX; x++) {
			float dx = fmaxf(fmaxf(lo - x, x - hi), 0), dy = fmaxf(fmaxf(lo - y, y - hi), 0);
			px[y * stride + x] = RGBA8(0, 0, 0, (int)(255 * expf(-(dx * dx + dy * dy) / (2 * sigma * sigma))));
		}
	}
	vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return t;
}

static void draw_shadow(vita2d_texture *shadow, float x, float y, float size, int alpha) {
	float k = size / SHADOW_BOX, pad = (SHADOW_TEX - SHADOW_BOX) / 2.0f * k;
	vita2d_draw_texture_tint_scale(shadow, x - pad, y - pad + size * 0.06f, k, k, RGBA8(255, 255, 255, alpha));
}

static float icon_size(const App *a) {
	return ICON_BASE + (ICON_FOCUS - ICON_BASE) * a->focus;
}

static float icon_offset(int index) {
	float x = 0;
	for (int i = 0; i < index; i++)
		x += icon_size(&apps[i]) + ICON_GAP;
	return x;
}

static void draw_blur(const App *a, float alpha) {
	if (!a->blur || alpha <= 0.01f)
		return;
	float scale = 1400.0f / BLUR_SIZE;
	vita2d_draw_texture_tint_scale(a->blur, -220, -760, scale, scale, RGBA8(170, 170, 170, (int)(120 * alpha)));
}

// Barre du haut : heure à gauche ; Wi-Fi, batterie et pourcentage à droite.
static void draw_status_bar(void) {
	SceDateTime now;
	sceRtcGetCurrentClockLocalTime(&now);
	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
	vita2d_font_draw_text(font_title, MARGIN_X, 52, WHITE(230), 22, buf);
	snprintf(buf, sizeof(buf), "%d %%", sys.battery);
	float right = SCREEN_W - MARGIN_X, text_x = right - vita2d_font_text_width(font_body, 20, buf);
	vita2d_font_draw_text(font_body, text_x, 52, WHITE(210), 20, buf);
	draw_battery(text_x - 36, 38, sys.battery, sys.charging, 0.9f);
	draw_wifi(text_x - 66, 51, sys.wifi, sys.wifi_signal, 0.9f);
}

int main(void) {
	vita2d_init();
	vita2d_set_vblank_wait(1);
	input_init();
	if (ui_init() < 0)
		sceKernelExitProcess(0);
	sys_init();
	vita2d_texture *shade_v = make_gradient(1, 50, 245);
	vita2d_texture *shade_h = make_gradient(0, 150, 0);
	vita2d_texture *shadow = make_shadow();

	scan_apps();
#ifdef PREVIEW_PANEL
	panel_open(); // aperçu dans Vita3K : tools/preview.sh panel
#endif

	int sel = 0, prev_sel = 0;
	float scroll = 0, fade = 1, text_in = 1;
	float bg_r = 20, bg_g = 20, bg_b = 26;
	Input in = {0};

	while (1) {
		input_read(&in);
		sys_poll();

		// --- entrées : le panneau rapide a la main tant qu'il est visible ---
		if (panel_visible()) {
			panel_update(&in);
		} else {
			int target = sel + in.dx, launch_index = -1;
			if (in.pressed & SCE_CTRL_LTRIGGER)
				target -= 5;
			if (in.pressed & SCE_CTRL_RTRIGGER)
				target += 5;
			if ((in.pressed & SCE_CTRL_TRIANGLE) || (in.y0 < 70 && in.swipe_y > 60 && in.swipe_y > abs(in.swipe_x))) {
				panel_open();
			} else if (abs(in.swipe_x) > 60) {
				target -= in.swipe_x / 110 ? in.swipe_x / 110 : (in.swipe_x > 0 ? 1 : -1);
			} else if (in.tap) {
				for (int i = 0; i < app_count; i++) {
					float s = icon_size(&apps[i]), x = MARGIN_X + icon_offset(i) - scroll;
					if (in.x >= x && in.x <= x + s && in.y >= ROW_CY - s / 2 && in.y <= ROW_CY + s / 2) {
						if (i == sel)
							launch_index = i;
						else
							target = i;
						break;
					}
				}
			}
			if (app_count > 0) {
				target = target < 0 ? 0 : target > app_count - 1 ? app_count - 1 : target;
				if (target != sel) {
					prev_sel = sel;
					sel = target;
					fade = 0;
					text_in = 0;
				}
				if (in.pressed & SCE_CTRL_CROSS)
					launch_index = sel;
				if (launch_index >= 0)
					launch(&apps[launch_index]);
			}
		}

		// --- animation ---
		for (int i = 0; i < app_count; i++)
			apps[i].focus = approach(apps[i].focus, i == sel ? 1 : 0, 0.22f);
		scroll = approach(scroll, icon_offset(sel), 0.2f);
		fade = approach(fade, 1, 0.08f);
		text_in = approach(text_in, 1, 0.15f);
		if (app_count > 0) {
			App *a = &apps[sel];
			bg_r = approach(bg_r, a->r * 0.3f, 0.08f);
			bg_g = approach(bg_g, a->g * 0.3f, 0.08f);
			bg_b = approach(bg_b, a->b * 0.3f, 0.08f);
		}

		// --- rendu ---
		vita2d_set_clear_color(RGBA8((int)bg_r, (int)bg_g, (int)bg_b, 255));
		vita2d_start_drawing();
		vita2d_clear_screen();

		if (app_count > 0) {
			if (prev_sel != sel)
				draw_blur(&apps[prev_sel], 1 - fade);
			draw_blur(&apps[sel], fade);
		}
		vita2d_draw_texture_scale(shade_v, 0, 0, SCREEN_W, SCREEN_H / 256.0f);
		vita2d_draw_texture_scale(shade_h, 0, 0, SCREEN_W * 0.7f / 256.0f, SCREEN_H);
		draw_status_bar();

		if (app_count == 0) {
			vita2d_font_draw_text(font_title, MARGIN_X, 300, WHITE(255), 36, "Aucune appli trouvée");
		} else {
			App *a = &apps[sel];
			char buf[64];
			int slide = (int)((1 - text_in) * 10);
			vita2d_font_draw_text(font_title, MARGIN_X, 300 + slide, WHITE(255 * text_in), 44, a->title);
			snprintf(buf, sizeof(buf), "%s  ·  %s", a->is_game ? "Jeu PS Vita" : "Homebrew", a->id);
			vita2d_font_draw_text(font_body, MARGIN_X, 336 + slide, WHITE(170 * text_in), 20, buf);

			for (int i = 0; i < app_count; i++) {
				App *ai = &apps[i];
				float s = icon_size(ai), x = MARGIN_X + icon_offset(i) - scroll, y = ROW_CY - s / 2;
				if (x > SCREEN_W || x + s < -ICON_FOCUS)
					continue;
				int alpha = (int)(170 + 85 * ai->focus);
				draw_shadow(shadow, x, y, s, (int)(90 + 90 * ai->focus));
				if (ai->icon) {
					float k = s / vita2d_texture_get_width(ai->icon);
					vita2d_draw_texture_tint_scale(ai->icon, x, y, k, k, WHITE(alpha));
				} else {
					draw_round_rect(x, y, s, s, s * 0.22f, RGBA8(ai->r, ai->g, ai->b, alpha));
					char letter[2] = {ai->title[0], 0};
					draw_text_center(font_title, x + s / 2, y + s / 2 + 13, WHITE(alpha), 36, letter);
				}
				if (ai->focus > 0.05f) {
					float bw = s * 0.3f * ai->focus;
					draw_round_rect(x + (s - bw) / 2, y + s + 12, bw, 4, 2, WHITE(255 * ai->focus));
				}
			}
		}

		if (panel_visible())
			panel_draw();

		vita2d_end_drawing();
		vita2d_swap_buffers();
	}
	return 0;
}
