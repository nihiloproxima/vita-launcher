#include "sheet.h"

#include "sound.h"
#include "ui.h"

#include <psp2/ctrl.h>
#include <stdio.h>

#define CARD_W 520
#define CARD_H 330
#define ICON   96
#define ROW_H  52
#define ROWS_Y 150

static App app; // copie : l'ordre des applis peut changer et une icône être libérée après une action
static int open, row, confirm;
static float t;

// Une ROM n'a pas « Désinstaller » : ce serait supprimer le fichier, et un jeu PS1 a souvent plusieurs fichiers.
static int row_count(void) { return app.path[0] ? 2 : 3; }

static float card_x(void) { return (SCREEN_W - CARD_W) / 2.0f; }
static float card_y(void) { return (SCREEN_H - CARD_H) / 2.0f + 24 * (1 - ease_out(t)); }

void sheet_open(const App *a) {
	app = *a;
	open = 1;
	row = 0;
	confirm = 0;
	sound_play(SOUND_OPEN);
}

int sheet_visible(void) {
	return open || t > 0.01f;
}

static const char *row_label(int i) {
	if (i == 0)
		return "Lancer";
	if (i == 1)
		return app.pinned ? "Retirer des épinglées" : "Épingler en tête";
	return confirm ? "Confirmer la désinstallation" : "Désinstaller";
}

static SheetAction activate(int i) {
	if (i == 2 && !confirm) {
		confirm = 1; // désinstaller est irréversible : second appui
		sound_play(SOUND_OPEN);
		return SHEET_NONE;
	}
	open = 0;
	if (i != 0)
		sound_play(SOUND_CLOSE); // lancer : son de lancement joué par l'accueil
	t = 0; // fermeture immédiate : après une désinstallation, l'icône copiée n'existe plus
	return i == 0 ? SHEET_LAUNCH : i == 1 ? SHEET_PIN : SHEET_UNINSTALL;
}

SheetAction sheet_update(const Input *in) {
	t = approach(t, open, 0.25f);
	if (!open)
		return SHEET_NONE;
	if (in->pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE)) {
		open = 0;
		sound_play(SOUND_CLOSE);
		return SHEET_NONE;
	}
	if (in->dy) {
		int old_row = row;
		row = row + in->dy < 0 ? 0 : row + in->dy >= row_count() ? row_count() - 1 : row + in->dy;
		confirm = 0;
		if (row != old_row)
			sound_play(SOUND_MOVE);
	}
	if (in->pressed & SCE_CTRL_CROSS)
		return activate(row);
	if (in->tap) {
		float x = card_x(), y = card_y();
		if (in->x < x || in->x > x + CARD_W || in->y < y || in->y > y + CARD_H) {
			open = 0;
			sound_play(SOUND_CLOSE);
			return SHEET_NONE;
		}
		int i = (int)((in->y - y - ROWS_Y) / ROW_H);
		if (in->y >= y + ROWS_Y && i >= 0 && i < row_count()) {
			if (i != row)
				confirm = 0;
			row = i;
			return activate(i);
		}
	}
	return SHEET_NONE;
}

void sheet_draw(void) {
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(160 * t)));
	float x = card_x(), y = card_y(), a = t;
	draw_round_rect(x, y, CARD_W, CARD_H, 26, RGBA8(26, 26, 32, (int)(250 * a)));

	if (app.icon) {
		float k = (float)ICON / vita2d_texture_get_width(app.icon);
		vita2d_draw_texture_tint_scale(app.icon, x + 28, y + 28, k, k, WHITE(255 * a));
	} else {
		draw_round_rect(x + 28, y + 28, ICON, ICON, ICON * 0.22f, RGBA8(app.r, app.g, app.b, (int)(255 * a)));
	}
	draw_text_any(font_title, x + 144, y + 70, WHITE(255 * a), 26, app.title);
	char buf[96];
	if (app.path[0])
		snprintf(buf, sizeof(buf), "%s", apps_kind(&app));
	else
		snprintf(buf, sizeof(buf), "%s  ·  %s%s%s", apps_kind(&app), app.id, app.version[0] ? "  ·  v" : "", app.version);
	draw_text(font_body, x + 144, y + 100, WHITE(160 * a), 17, buf);

	for (int i = 0; i < row_count(); i++) {
		float ry = y + ROWS_Y + i * ROW_H;
		int focused = i == row, danger = i == 2;
		unsigned int fg = danger ? RGBA8(255, 110, 100, (int)(255 * a)) : WHITE(230 * a);
		if (focused) {
			unsigned int bg = danger && confirm ? RGBA8(232, 72, 62, (int)(255 * a)) : WHITE(240 * a);
			fg = danger && confirm ? WHITE(255 * a) : danger ? RGBA8(200, 50, 40, (int)(255 * a)) : RGBA8(18, 18, 24, (int)(255 * a));
			draw_round_rect(x + 20, ry + 4, CARD_W - 40, ROW_H - 8, 14, bg);
		}
		draw_text(font_title, x + 40, ry + ROW_H / 2 + 7, fg, 19, row_label(i));
	}
}
