#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <vita2d.h>
#include "apps.h"
#include "covers.h"
#include "input.h"
#include "panel.h"
#include "sheet.h"
#include "sys.h"
#include "ui.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ICON_BASE  104.0f
#define ICON_FOCUS 148.0f
#define ICON_GAP   20.0f
#define ROW_CY     416.0f
#define TITLE_Y    286
#define HINTS_Y    528
#define BLUR_SIZE  16
#define ICONS_PER_FRAME 2

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
	vita2d_draw_texture_tint_scale(shadow, x - pad, y - pad + size * 0.06f, k, k, WHITE(alpha));
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
	vita2d_draw_texture_tint_scale(a->blur, -220, -760, scale, scale, RGBA8(170, 170, 170, (int)(120 * alpha * a->appear)));
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

// Première lettre ou chiffre ASCII du titre (Inter n'a pas les caractères japonais), sinon celle de l'ID.
static char initial(const App *a) {
	for (const char *c = a->title; *c; c++)
		if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))
			return *c >= 'a' && *c <= 'z' ? *c - 32 : *c;
	return a->id[0];
}

static void draw_app_icon(const App *a, float x, float y, float s, int alpha) {
	if (!a->icon || a->appear < 0.99f) { // la lettre reste visible sous l'icône qui apparaît en fondu
		draw_round_rect(x, y, s, s, s * 0.22f, RGBA8(a->r, a->g, a->b, alpha));
		char letter[2] = {initial(a), 0};
		draw_text_center(font_title, x + s / 2, y + s / 2 + s * 0.12f, WHITE(alpha * 0.8f), (unsigned)(s * 0.34f), letter);
	}
	if (a->icon) {
		float k = s / vita2d_texture_get_width(a->icon);
		vita2d_draw_texture_tint_scale(a->icon, x, y, k, k, WHITE(alpha * a->appear));
	}
}

int main(void) {
	vita2d_init();
	vita2d_set_vblank_wait(1);
	input_init();
	if (ui_init() < 0)
		sceKernelExitProcess(0);
	sys_init();
	covers_init();
	vita2d_texture *shade_v = make_gradient(1, 50, 245);
	vita2d_texture *shade_h = make_gradient(0, 150, 0);
	vita2d_texture *shadow = make_shadow();

	apps_scan();
#ifdef PREVIEW_PANEL
	panel_open(); // aperçu dans Vita3K : tools/preview.sh panel
#endif
#ifdef PREVIEW_SHEET
	apps_load_icons(MAX_APPS);
	if (app_count > 0)
		sheet_open(&apps[0]); // aperçu dans Vita3K : tools/preview.sh sheet
#endif

	int sel = 0, prev_sel = 0;
	float scroll = 0, fade = 1, text_in = 1;
	float bg_r = 20, bg_g = 20, bg_b = 26;
	Input in = {0};

	while (1) {
		input_read(&in);
		sys_poll();
		apps_load_icons(ICONS_PER_FRAME);

		// --- entrées : la fiche puis le panneau ont la main tant qu'ils sont visibles ---
		if (sheet_visible()) {
			char id[16];
			snprintf(id, sizeof(id), "%s", apps[sel].id);
			SheetAction action = sheet_update(&in);
			if (action == SHEET_LAUNCH) {
				apps_launch(&apps[sel]);
			} else if (action == SHEET_PIN) {
				apps_toggle_pin(id);
				sel = prev_sel = apps_find(id); // l'ordre a changé : on suit l'appli
			} else if (action == SHEET_UNINSTALL && apps_uninstall(id) == 0) {
				sel = prev_sel = sel < app_count ? sel : app_count - 1;
				if (sel < 0)
					sel = prev_sel = 0;
				fade = 0;
				text_in = 0;
			}
		} else if (panel_visible()) {
			panel_update(&in);
		} else {
			int target = sel + in.dx, launch_index = -1;
			if (in.pressed & SCE_CTRL_LTRIGGER)
				target -= 5;
			if (in.pressed & SCE_CTRL_RTRIGGER)
				target += 5;
			if ((in.pressed & SCE_CTRL_TRIANGLE) || (in.y0 < 70 && in.swipe_y > 60 && in.swipe_y > abs(in.swipe_x))) {
				panel_open();
			} else if ((in.pressed & SCE_CTRL_SQUARE) && app_count > 0) {
				sheet_open(&apps[sel]);
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
					apps_launch(&apps[launch_index]);
			}
		}

		// --- animation ---
		for (int i = 0; i < app_count; i++) {
			apps[i].focus = approach(apps[i].focus, i == sel ? 1 : 0, 0.22f);
			if (apps[i].icon)
				apps[i].appear = approach(apps[i].appear, 1, 0.15f);
		}
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
			if (prev_sel != sel && prev_sel < app_count)
				draw_blur(&apps[prev_sel], 1 - fade);
			draw_blur(&apps[sel], fade);
		}
		vita2d_draw_texture_scale(shade_v, 0, 0, SCREEN_W, SCREEN_H / 256.0f);
		vita2d_draw_texture_scale(shade_h, 0, 0, SCREEN_W * 0.7f / 256.0f, SCREEN_H);
		draw_status_bar();

		if (app_count == 0) {
			vita2d_font_draw_text(font_title, MARGIN_X, TITLE_Y, WHITE(255), 36, "Aucune appli trouvée");
		} else {
			App *a = &apps[sel];
			char buf[64];
			int slide = (int)((1 - text_in) * 10);
			draw_text_any(font_title, MARGIN_X, TITLE_Y + slide, WHITE(255 * text_in), 44, a->title);
			snprintf(buf, sizeof(buf), "%s%s  ·  %s", a->pinned ? "Épinglée  ·  " : "", a->is_game ? "Jeu PS Vita" : "Homebrew", a->id);
			vita2d_font_draw_text(font_body, MARGIN_X, TITLE_Y + 36 + slide, WHITE(170 * text_in), 20, buf);

			for (int i = 0; i < app_count; i++) {
				App *ai = &apps[i];
				float s = icon_size(ai), x = MARGIN_X + icon_offset(i) - scroll, y = ROW_CY - s / 2;
				if (x > SCREEN_W || x + s < -ICON_FOCUS)
					continue;
				int alpha = (int)(170 + 85 * ai->focus);
				draw_shadow(shadow, x, y, s, (int)(90 + 90 * ai->focus));
				draw_app_icon(ai, x, y, s, alpha);
				if (ai->focus > 0.05f) {
					float bw = s * 0.3f * ai->focus;
					draw_round_rect(x + (s - bw) / 2, y + s + 12, bw, 4, 2, WHITE(255 * ai->focus));
				}
			}

			float right = draw_hint(GLYPH_TRIANGLE, "Menu", SCREEN_W - MARGIN_X, HINTS_Y, 1);
			right = draw_hint(GLYPH_SQUARE, "Options", right - 14, HINTS_Y, 1);
			draw_hint(GLYPH_CROSS, "Lancer", right - 14, HINTS_Y, 1);
		}

		if (panel_visible())
			panel_draw();
		if (sheet_visible())
			sheet_draw();

		vita2d_end_drawing();
		vita2d_swap_buffers();
	}
	return 0;
}
