#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <vita2d.h>
#include "apps.h"
#include "covers.h"
#include "ftp.h"
#include "input.h"
#include "panel.h"
#include "sheet.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICON_BASE  104.0f
#define ICON_FOCUS 148.0f
#define ICON_GAP   20.0f
#define ROW_CY     416.0f
#define TITLE_Y    286
#define HINTS_Y    528
#define TABS_Y     112
#define TAB_COUNT  3
#define TAB_WEEK   2 // Ta semaine : pas une rangée d'applis, aucune appli n'y est triée
#define WEEK_BASE  460.0f // pied des barres de Ta semaine
#define WEEK_BAR_W 36.0f
#define WEEK_BAR_H 110.0f
#define BLUR_SIZE  16
#define ICONS_PER_FRAME 2
#define LAUNCH_TIMEOUT 300 // images (5 s) : si le plugin ne ferme pas le launcher, on se ferme nous-mêmes
#define HERO_DELAY     20  // images sur la même appli avant de charger sa grande image : pas pendant un défilement
#define HERO_MARGIN    40  // marge de la grande image hors de l'écran, décalage maximal du fond
#define PARALLAX       120 // décalage du fond (px) par g d'inclinaison
#define IDLE_FRAMES    3600 // une minute sans rien toucher : écran de veille
#define DOZE_VBLANKS   4    // veille installée : une image toutes les 4 (15 par seconde)

static vita2d_texture *shade_v, *shade_h;
static vita2d_texture *hero; // grande image de l'appli hero_id (homebrews), NULL si elle n'en a pas
static char hero_id[16];

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

// Position de l'icône index dans la rangée de l'onglet qui commence à from.
static float icon_offset(int from, int index) {
	float x = 0;
	for (int i = from; i < index; i++)
		x += icon_size(&apps[i]) + ICON_GAP;
	return x;
}

static void draw_blur(const App *a, float alpha, float dx, float dy) {
	if (!a->blur || alpha <= 0.01f)
		return;
	float scale = 1400.0f / BLUR_SIZE;
	vita2d_draw_texture_tint_scale(a->blur, -220 + dx, -760 + dy, scale, scale, RGBA8(170, 170, 170, (int)(120 * alpha * a->appear)));
}

// Couvre l'écran avec HERO_MARGIN de rab de chaque côté : un décalage jusqu'à la marge ne découvre pas de bord.
static void draw_hero(float alpha, float dx, float dy) {
	float w = vita2d_texture_get_width(hero), h = vita2d_texture_get_height(hero);
	float k = fmaxf((SCREEN_W + 2 * HERO_MARGIN) / w, (SCREEN_H + 2 * HERO_MARGIN) / h);
	vita2d_draw_texture_tint_scale(hero, (SCREEN_W - w * k) / 2 + dx, (SCREEN_H - h * k) / 2 + dy, k, k, RGBA8(210, 210, 210, alpha8(255 * alpha)));
}

// Couleur dominante, icône floue (fondu depuis la sélection précédente), grande image, puis les dégradés qui
// assombrissent le haut, le bas et la gauche pour le texte. Redessiné par-dessus l'interface, il l'efface (veille).
static void draw_backdrop(const App *cur, const App *prev, float fade, float hero_t, float dx, float dy, unsigned int bg, float alpha) {
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, (bg & 0x00FFFFFF) | (unsigned int)alpha8(255 * alpha) << 24);
	if (prev)
		draw_blur(prev, (1 - fade) * alpha, dx, dy);
	if (cur)
		draw_blur(cur, fade * alpha, dx, dy);
	if (hero && hero_t > 0.01f)
		draw_hero(hero_t * alpha, dx, dy);
	vita2d_draw_texture_tint_scale(shade_v, 0, 0, SCREEN_W, SCREEN_H / 256.0f, WHITE(255 * alpha));
	vita2d_draw_texture_tint_scale(shade_h, 0, 0, SCREEN_W * 0.7f / 256.0f, SCREEN_H, WHITE(255 * alpha));
}

// Heure de l'écran de veille : elle se promène lentement pour ne pas marquer l'écran OLED des PCH-1000.
static void draw_ambient_clock(float alpha, int frame) {
	SceDateTime now;
	sceRtcGetCurrentClockLocalTime(&now);
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
	float w = text_width(font_title, 96, buf);
	float x = MARGIN_X + (SCREEN_W - 2 * MARGIN_X - w) * (0.5f + 0.5f * sinf(frame * 0.0011f));
	float y = 160 + 300 * (0.5f + 0.5f * sinf(frame * 0.0007f + 1));
	draw_text(font_title, (int)x, (int)y, WHITE(200 * alpha), 96, buf);
}

// Barre du haut : heure à gauche ; Wi-Fi, batterie et pourcentage à droite.
static void draw_status_bar(void) {
	SceDateTime now;
	sceRtcGetCurrentClockLocalTime(&now);
	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
	draw_text(font_title, MARGIN_X, 52, WHITE(230), 22, buf);
	if (ftp_running()) {
		float x = MARGIN_X + text_width(font_title, 22, buf) + 20;
		vita2d_draw_fill_circle(x + 4, 45, 4, RGBA8(90, 210, 120, 255));
		draw_text(font_body, x + 16, 52, WHITE(190), 18, ftp_address());
	}
	snprintf(buf, sizeof(buf), "%d %%", sys.battery);
	float right = SCREEN_W - MARGIN_X, text_x = right - text_width(font_body, 20, buf);
	draw_text(font_body, text_x, 52, WHITE(210), 20, buf);
	draw_battery(text_x - 36, 38, sys.battery, sys.charging, 0.9f);
	draw_wifi(text_x - 66, 51, sys.wifi, sys.wifi_signal, 0.9f);
}

// Les applis sont triées par onglet (app_tab) : l'onglet tab occupe apps[lo .. hi[.
static void tab_range(int tab, int *lo, int *hi) {
	for (*lo = 0; *lo < app_count && app_tab(&apps[*lo]) < tab; (*lo)++)
		;
	for (*hi = *lo; *hi < app_count && app_tab(&apps[*hi]) == tab; (*hi)++)
		;
}

static const char *const TAB_NAMES[TAB_COUNT] = {"Jeux", "Applis", "Ta semaine"};

static float tab_x(int tab) {
	float x = MARGIN_X + 36; // après la pastille L
	for (int t = 0; t < tab; t++)
		x += text_width(font_title, 26, TAB_NAMES[t]) + 28;
	return x;
}

static void draw_trigger(float x, const char *label) {
	draw_round_rect(x, TABS_Y - 20, 26, 20, 6, WHITE(60));
	draw_text_center(font_title, x + 13, TABS_Y - 5, WHITE(210), 13, label);
}

// Onglets en haut à gauche, comme Jeux / Multimédia sur PS5, entre les gâchettes qui les changent.
static void draw_tabs(int tab) {
	draw_trigger(MARGIN_X, "L");
	for (int t = 0; t < TAB_COUNT; t++)
		draw_text(font_title, tab_x(t), TABS_Y, WHITE(t == tab ? 255 : 110), 26, TAB_NAMES[t]);
	draw_trigger(tab_x(TAB_COUNT) - 18, "R");
}

// « ·  12 h de jeu  ·  joué hier », vide si le plugin n'a jamais vu l'appli tourner.
static void format_playtime(const App *a, char *buf, size_t size) {
	buf[0] = 0;
	if (!a->played)
		return;
	SceDateTime dt;
	SceUInt64 now = 0;
	sceRtcGetCurrentClockLocalTime(&dt);
	sceRtcGetTime64_t(&dt, &now); // même horloge que le plugin : heure locale en secondes
	int days = (int)(now / 86400) - (int)(a->last_played / 86400);
	char when[32];
	if (days <= 0)
		snprintf(when, sizeof(when), "joué aujourd’hui");
	else if (days == 1)
		snprintf(when, sizeof(when), "joué hier");
	else if (days < 60)
		snprintf(when, sizeof(when), "joué il y a %d jours", days);
	else
		snprintf(when, sizeof(when), "joué il y a %d mois", days / 30);
	if (a->played < 3600)
		snprintf(buf, size, "  ·  %u min de jeu  ·  %s", a->played < 60 ? 1 : a->played / 60, when);
	else
		snprintf(buf, size, "  ·  %u h de jeu  ·  %s", a->played / 3600, when);
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

// « 35 min », « 4 h 05 »
static void format_duration(unsigned int secs, char *buf, size_t size) {
	if (secs < 3600)
		snprintf(buf, size, "%u min", secs < 60 ? 1 : secs / 60);
	else
		snprintf(buf, size, "%u h %02u", secs / 3600, secs / 60 % 60);
}

// Onglet Ta semaine : temps de jeu des 7 derniers jours, une barre par jour, et le jeu le plus joué.
static void draw_week(void) {
	unsigned int total = 0, max = 1;
	for (int d = 0; d < 7; d++) {
		total += apps_week[d];
		max = apps_week[d] > max ? apps_week[d] : max;
	}
	if (!total) {
		draw_text(font_title, MARGIN_X, TITLE_Y, WHITE(255), 36, "Aucune partie ces 7 derniers jours");
		draw_text(font_body, MARGIN_X, TITLE_Y + 36, WHITE(170), 20, "Le temps de jeu est noté par le plugin de démarrage");
		return;
	}
	char buf[32], title[48];
	format_duration(total, buf, sizeof(buf));
	snprintf(title, sizeof(title), "%s de jeu", buf);
	draw_text(font_title, MARGIN_X, TITLE_Y, WHITE(255), 44, title);
	draw_text(font_body, MARGIN_X, TITLE_Y + 36, WHITE(170), 20, "ces 7 derniers jours");

	for (int d = 0; d < 7; d++) {
		float h = fmaxf(4, WEEK_BAR_H * apps_week[d] / max), x = MARGIN_X + d * (WEEK_BAR_W + 20);
		draw_round_rect(x, WEEK_BASE - h, WEEK_BAR_W, h, fminf(6, h / 2), WHITE(d == 6 ? 240 : 90));
		char day[2] = {"DLMMJVS"[(apps_today + d + 1) % 7], 0}; // d = 6 : aujourd'hui
		draw_text_center(font_body, x + WEEK_BAR_W / 2, WEEK_BASE + 26, WHITE(d == 6 ? 230 : 140), 16, day);
	}

	// cherché à chaque image : épingler ou désinstaller déplace les applis dans apps[]
	const App *top = NULL;
	for (int i = 0; i < app_count; i++)
		if (apps[i].week && (!top || apps[i].week > top->week))
			top = &apps[i];
	if (top) { // NULL si le seul jeu joué a été désinstallé depuis
		float x = MARGIN_X + 7 * (WEEK_BAR_W + 20) + 40, tx = x + ICON_BASE + 22;
		draw_app_icon(top, x, ROW_CY - ICON_BASE / 2, ICON_BASE, 255);
		draw_text(font_body, tx, ROW_CY - 18, WHITE(150), 18, "Le plus joué");
		draw_text_any(font_title, tx, ROW_CY + 12, WHITE(255), 24, top->title);
		format_duration(top->week, buf, sizeof(buf));
		draw_text(font_body, tx, ROW_CY + 40, WHITE(170), 18, buf);
	}
}

int main(void) {
	vita2d_init();
	vita2d_set_vblank_wait(1);
	input_init();
	if (ui_init() < 0)
		sceKernelExitProcess(0);
	sys_init();
	sound_init();
	ftp_init();
	covers_init();
	shade_v = make_gradient(1, 50, 245);
	shade_h = make_gradient(0, 150, 0);
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

	// on revient sur l'onglet de la dernière appli lancée, en général celle qu'on vient de quitter
	const char *last = apps_last_launched();
	int last_index = last ? apps_find(last) : -1;
	int tab = last_index >= 0 ? app_tab(&apps[last_index]) : 0, lo, hi;
#ifdef PREVIEW_WEEK
	tab = TAB_WEEK; // aperçu dans Vita3K : tools/preview.sh week
#endif
	tab_range(tab, &lo, &hi);
	int sel = lo, prev_sel = lo;
	float scroll = 0, fade = 1, text_in = 1;
	float bg_r = 20, bg_g = 20, bg_b = 26, launch_t = 0;
	int launching = -1, launch_frames = 0; // appli en cours de lancement par le plugin
	int frame = 0, idle = 0, waking = 0, settle = 0, hero_tried = 0;
	float ambient = 0, hero_t = 0; // ambient : écran de veille, 0 -> 1
	char settle_id[16] = ""; // appli sélectionnée depuis settle images
	Input in = {0};

	while (1) {
		frame++;
		input_read(&in);
		// écran de veille : la première entrée, jusqu'à son relâchement, ne fait que réveiller
		int active = (in.held & 0xFFFF) || in.touching || in.dx || in.dy || in.tap || in.swipe_x || in.swipe_y;
		idle = active ? 0 : idle + 1;
		if (active && ambient > 0.05f)
			waking = 1;
		if (waking) {
			waking = active;
			in.pressed = 0;
			in.dx = in.dy = in.tap = in.swipe_x = in.swipe_y = 0;
			in.touching = 0; // et au relâchement, input_read ne verra ni toucher ni glissé
		}
		sys_poll();
		ftp_poll(sys.wifi);
		apps_load_icons(ICONS_PER_FRAME);

		// --- entrées : la fiche puis le panneau ont la main tant qu'ils sont visibles ---
		if (launching >= 0) {
			if (++launch_frames > LAUNCH_TIMEOUT)
				sceKernelExitProcess(0); // la demande reste dans ux0:data : le plugin la traitera à notre fermeture
		} else if (sheet_visible()) {
			char id[16];
			snprintf(id, sizeof(id), "%s", apps[sel].id);
			SheetAction action = sheet_update(&in);
			if (action == SHEET_LAUNCH) {
				sound_play(SOUND_LAUNCH);
				if (apps_launch(&apps[sel]))
					launching = sel;
			} else if (action == SHEET_PIN) {
				apps_toggle_pin(id);
				sel = prev_sel = apps_find(id); // l'ordre a changé : on suit l'appli
			} else if (action == SHEET_UNINSTALL && apps_uninstall(id) == 0) {
				prev_sel = sel; // ramené dans l'onglet plus bas
				fade = 0;
				text_in = 0;
			}
		} else if (panel_visible()) {
			panel_update(&in);
		} else {
			int target = sel + in.dx, launch_index = -1, new_tab = tab;
			if ((in.pressed & SCE_CTRL_LTRIGGER) && tab > 0)
				new_tab = tab - 1;
			if ((in.pressed & SCE_CTRL_RTRIGGER) && tab < TAB_COUNT - 1)
				new_tab = tab + 1;
			if (in.tap && in.y > TABS_Y - 34 && in.y < TABS_Y + 12) {
				for (int t = 0; t < TAB_COUNT; t++)
					if (in.x >= tab_x(t) && in.x <= tab_x(t + 1) - 28)
						new_tab = t;
			}
			if (new_tab != tab) {
				sound_play(SOUND_MOVE);
				tab = new_tab;
				tab_range(tab, &lo, &hi);
				sel = prev_sel = target = lo;
				scroll = 0;
				fade = 0;
				text_in = 0;
			} else if ((in.pressed & SCE_CTRL_TRIANGLE) || (in.y0 < 70 && in.swipe_y > 60 && in.swipe_y > abs(in.swipe_x))) {
				panel_open();
			} else if ((in.pressed & SCE_CTRL_SQUARE) && hi > lo) {
				sheet_open(&apps[sel]);
			} else if (abs(in.swipe_x) > 60) {
				target -= in.swipe_x / 110 ? in.swipe_x / 110 : (in.swipe_x > 0 ? 1 : -1);
			} else if (in.tap) {
				for (int i = lo; i < hi; i++) {
					float s = icon_size(&apps[i]), x = MARGIN_X + icon_offset(lo, i) - scroll;
					if (in.x >= x && in.x <= x + s && in.y >= ROW_CY - s / 2 && in.y <= ROW_CY + s / 2) {
						if (i == sel)
							launch_index = i;
						else
							target = i;
						break;
					}
				}
			}
			if (hi > lo) {
				target = target < lo ? lo : target > hi - 1 ? hi - 1 : target;
				if (target != sel) {
					sound_play(SOUND_MOVE);
					prev_sel = sel;
					sel = target;
					fade = 0;
					text_in = 0;
				}
				if (in.pressed & SCE_CTRL_CROSS)
					launch_index = sel;
				if (launch_index >= 0) {
					sound_play(SOUND_LAUNCH);
					if (apps_launch(&apps[launch_index]))
						launching = launch_index;
				}
			}
		}

		// épingler ou désinstaller réordonne les applis : l'onglet et la sélection suivent
		tab_range(tab, &lo, &hi);
		if (sel < lo || sel >= hi)
			sel = hi > lo && sel >= hi ? hi - 1 : lo;

		// --- animation ---
		for (int i = 0; i < app_count; i++) {
			apps[i].focus = approach(apps[i].focus, i == sel ? 1 : 0, 0.22f);
			if (apps[i].icon)
				apps[i].appear = approach(apps[i].appear, 1, 0.15f);
		}
		scroll = approach(scroll, icon_offset(lo, sel), 0.2f);
		fade = approach(fade, 1, 0.08f);
		text_in = approach(text_in, 1, 0.15f);
		launch_t = approach(launch_t, launching >= 0, 0.12f);
		ambient = approach(ambient, idle > IDLE_FRAMES && launching < 0, idle > IDLE_FRAMES ? 0.01f : 0.2f);
		if (hi > lo) {
			App *a = &apps[sel];
			bg_r = approach(bg_r, a->r * 0.3f, 0.08f);
			bg_g = approach(bg_g, a->g * 0.3f, 0.08f);
			bg_b = approach(bg_b, a->b * 0.3f, 0.08f);
		}

		// --- rendu ---
		// vita2d réutilise une seule réserve de sommets à chaque image : sans cette attente, on la réécrit pendant que
		// le GPU lit encore l'image précédente (triple tampon), ce qui l'a planté sur console (« render gpu crash »).
		vita2d_wait_rendering_done();

		// grande image : chargée une fois la sélection posée (un PNG plein écran se décode en plusieurs images),
		// libérée après son fondu, ici où le GPU a fini de la lire
		const char *cur_id = hi > lo ? apps[sel].id : "";
		if (strcmp(settle_id, cur_id) != 0) {
			snprintf(settle_id, sizeof(settle_id), "%s", cur_id);
			settle = hero_tried = 0;
		}
		settle++;
		int hero_wanted = hero && strcmp(hero_id, cur_id) == 0;
		hero_t = approach(hero_t, hero_wanted, hero_wanted ? 0.08f : 0.15f);
		if (hero && !hero_wanted && hero_t < 0.01f) {
			vita2d_free_texture(hero);
			hero = NULL;
		}
		if (!hero && !hero_tried && hi > lo && settle > HERO_DELAY && launching < 0) {
			hero_tried = 1;
			if ((hero = apps_load_hero(&apps[sel])))
				snprintf(hero_id, sizeof(hero_id), "%s", cur_id);
		}

		vita2d_start_drawing();
		vita2d_clear_screen();

		// le fond suit l'inclinaison de la console et dérive lentement pendant la veille
		float off_x = fmaxf(fminf(-in.tilt_x * PARALLAX + ambient * 30 * sinf(frame * 0.004f), HERO_MARGIN), -HERO_MARGIN);
		float off_y = fmaxf(fminf(in.tilt_y * PARALLAX + ambient * 18 * cosf(frame * 0.003f), HERO_MARGIN), -HERO_MARGIN);
		const App *cur = hi > lo ? &apps[sel] : NULL;
		const App *prev = cur && prev_sel != sel && prev_sel < app_count ? &apps[prev_sel] : NULL;
		unsigned int bg = RGBA8((int)bg_r, (int)bg_g, (int)bg_b, 255);
		draw_backdrop(cur, prev, fade, hero_t, off_x, off_y, bg, 1);
		draw_status_bar();
		draw_tabs(tab);

		if (tab == TAB_WEEK) {
			draw_week();
		} else if (hi == lo) {
			draw_text(font_title, MARGIN_X, TITLE_Y, WHITE(255), 36, tab == 0 ? "Aucun jeu" : "Aucune appli");
			if (tab == 0)
				draw_text(font_body, MARGIN_X, TITLE_Y + 36, WHITE(170), 20,
					"ROMs GBA et PS1 : ux0:roms/gba et ux0:roms/ps1, avec RetroArch installé");
		} else {
			App *a = &apps[sel];
			char buf[160], played[64];
			int slide = (int)((1 - text_in) * 10);
			draw_text_any(font_title, MARGIN_X, TITLE_Y + slide, WHITE(255 * text_in), 44, a->title);
			format_playtime(a, played, sizeof(played));
			// l'identifiant d'une ROM est le nôtre, sans intérêt à l'écran
			snprintf(buf, sizeof(buf), "%s%s%s%s%s", a->pinned ? "Épinglée  ·  " : "", apps_kind(a), a->path[0] ? "" : "  ·  ",
				a->path[0] ? "" : a->id, played);
			draw_text(font_body, MARGIN_X, TITLE_Y + 36 + slide, WHITE(170 * text_in), 20, buf);

			for (int i = lo; i < hi; i++) {
				App *ai = &apps[i];
				float s = icon_size(ai), x = MARGIN_X + icon_offset(lo, i) - scroll, y = ROW_CY - s / 2;
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
		if (ambient > 0.01f) {
			draw_backdrop(cur, prev, fade, hero_t, off_x, off_y, bg, ambient);
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, alpha8(90 * ambient)));
			draw_ambient_clock(ambient, frame);
		}
		if (launching >= 0) {
			// l'écran passe au noir pendant que l'icône grossit au centre, jusqu'à ce que le plugin nous ferme
			float k = ease_out(launch_t), s = ICON_FOCUS * (1 + 0.3f * k);
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(255 * k)));
			draw_app_icon(&apps[launching], (SCREEN_W - s) / 2, (SCREEN_H - s) / 2, s, (int)(255 * k));
		}

		vita2d_end_drawing();
		vita2d_swap_buffers();
		// veille installée : l'heure et le fond bougent à peine, moins d'images laissent le processeur et le GPU au repos
		if (ambient > 0.99f) {
			sceDisplayWaitVblankStartMulti(DOZE_VBLANKS);
			frame += DOZE_VBLANKS - 1; // en plus du frame++ : la dérive garde sa vitesse
		}
	}
	return 0;
}
