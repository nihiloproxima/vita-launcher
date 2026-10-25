#include "panel.h"

#include "ftp.h"
#include "sys.h"
#include "ui.h"

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <stdio.h>

#define PANEL_X   40
#define PANEL_W   880
#define PANEL_H   300
#define PANEL_TOP 24
#define PAD       32
#define VOLUME_CY 150
#define TILES_Y   204
#define TILE_H    64
#define TILE_GAP  12
#define TILE_COUNT 6
#define CONFIRM_FRAMES 180

enum { TILE_WIFI, TILE_SETTINGS, TILE_FTP, TILE_SUSPEND, TILE_REBOOT, TILE_POWEROFF };
static const char *tile_labels[TILE_COUNT] = {"Wi-Fi", "Paramètres", "FTP", "Veille", "Redémarrer", "Éteindre"};
static const char *days[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
static const char *months[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet",
	"août", "septembre", "octobre", "novembre", "décembre"};

static int open, row, col = TILE_SETTINGS, confirm = -1, confirm_timer;
static float t, row_focus[2];

static float panel_y(void) {
	return PANEL_TOP - (PANEL_H + PANEL_TOP + 20) * (1 - ease_out(t));
}

static float bar_x(void) { return PANEL_X + 180; }
static float bar_w(void) { return PANEL_W - 180 - 110; }
static float tile_w(void) { return (PANEL_W - 2 * PAD - (TILE_COUNT - 1) * TILE_GAP) / (float)TILE_COUNT; }
static float tile_x(int i) { return PANEL_X + PAD + i * (tile_w() + TILE_GAP); }

void panel_open(void) {
	open = 1;
	row = 1;
	confirm = -1;
}

int panel_visible(void) {
	return open || t > 0.01f;
}

static void activate(int tile) {
	switch (tile) {
	case TILE_WIFI:
		sys_open_settings("wifi");
		break;
	case TILE_SETTINGS:
		sys_open_settings("");
		break;
	case TILE_FTP:
		ftp_toggle();
		break;
	case TILE_SUSPEND:
		sys_suspend();
		break;
	case TILE_REBOOT:
	case TILE_POWEROFF:
		// actions irréversibles : un second appui confirme
		if (confirm != tile) {
			confirm = tile;
			confirm_timer = CONFIRM_FRAMES;
		} else if (tile == TILE_REBOOT) {
			sys_reboot();
		} else {
			sys_power_off();
		}
		break;
	}
}

void panel_update(const Input *in) {
	t = approach(t, open, 0.2f);
	row_focus[0] = approach(row_focus[0], row == 0, 0.25f);
	row_focus[1] = approach(row_focus[1], row == 1, 0.25f);
	if (confirm_timer > 0 && --confirm_timer == 0)
		confirm = -1;
	if (!open)
		return;

	if ((in->pressed & (SCE_CTRL_TRIANGLE | SCE_CTRL_CIRCLE)) || in->swipe_y < -60) {
		open = 0;
		return;
	}
	if (in->dy) {
		row = in->dy < 0 ? 0 : 1;
		confirm = -1;
	}
	if (in->dx) {
		if (row == 0) {
			sys_set_volume(sys.volume + in->dx);
		} else {
			col = col + in->dx < 0 ? 0 : col + in->dx >= TILE_COUNT ? TILE_COUNT - 1 : col + in->dx;
			confirm = -1;
		}
	}
	if (row == 1 && (in->pressed & SCE_CTRL_CROSS))
		activate(col);

	// tactile : glisser sur la barre de volume, toucher une tuile, toucher hors du panneau pour fermer
	float py = panel_y();
	if (in->touching && in->x0 >= bar_x() - 24 && in->x0 <= bar_x() + bar_w() + 24
		&& in->y0 >= py + VOLUME_CY - 28 && in->y0 <= py + VOLUME_CY + 28) {
		row = 0;
		float k = (in->x - bar_x()) / bar_w();
		int volume = (int)(SYS_VOLUME_MAX * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5f);
		if (volume != sys.volume)
			sys_set_volume(volume);
	}
	if (in->tap) {
		if (in->y > py + PANEL_H) {
			open = 0;
		} else if (in->y >= py + TILES_Y && in->y <= py + TILES_Y + TILE_H) {
			for (int i = 0; i < TILE_COUNT; i++) {
				if (in->x >= tile_x(i) && in->x <= tile_x(i) + tile_w()) {
					row = 1;
					col = i;
					activate(i);
				}
			}
		}
	}
}

static void draw_header(float py) {
	SceDateTime now;
	sceRtcGetCurrentClockLocalTime(&now);
	char buf[64];
	snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
	vita2d_font_draw_text(font_title, PANEL_X + PAD, py + 68, WHITE(255), 44, buf);
	int dow = sceRtcGetDayOfWeek(now.year, now.month, now.day);
	snprintf(buf, sizeof(buf), "%s %d %s", days[dow % 7], now.day, months[(now.month + 11) % 12]);
	vita2d_font_draw_text(font_body, PANEL_X + PAD, py + 100, WHITE(170), 18, buf);

	float right = PANEL_X + PANEL_W - PAD;
	const char *network = !sys.wifi ? "Wi-Fi déconnecté" : sys.ssid[0] ? sys.ssid : "Connecté";
	draw_text_right(font_body, right, py + 60, WHITE(230), 18, network);
	draw_wifi(right - vita2d_font_text_width(font_body, 18, network) - 30, py + 59, sys.wifi, sys.wifi_signal, 1);

	if (sys.charging)
		snprintf(buf, sizeof(buf), "%d %% · en charge", sys.battery);
	else if (sys.minutes_left > 0)
		snprintf(buf, sizeof(buf), "%d %% · %d h %02d restantes", sys.battery, sys.minutes_left / 60, sys.minutes_left % 60);
	else
		snprintf(buf, sizeof(buf), "%d %%", sys.battery);
	draw_text_right(font_body, right, py + 94, WHITE(230), 18, buf);
	draw_battery(right - vita2d_font_text_width(font_body, 18, buf) - 38, py + 81, sys.battery, sys.charging, 1);
}

static void draw_volume(float py) {
	float cy = py + VOLUME_CY, f = row_focus[0];
	draw_round_rect(PANEL_X + 16, cy - 28, PANEL_W - 32, 56, 16, WHITE(16 * f));
	vita2d_font_draw_text(font_title, PANEL_X + PAD, cy + 7, WHITE(170 + 85 * f), 20, "Volume");

	float k = sys.volume < 0 ? 0 : (float)sys.volume / SYS_VOLUME_MAX;
	draw_round_rect(bar_x(), cy - 3, bar_w(), 6, 3, WHITE(50));
	if (k > 0)
		draw_round_rect(bar_x(), cy - 3, bar_w() * k, 6, 3, WHITE(235));
	vita2d_draw_fill_circle(bar_x() + bar_w() * k, cy, 8 + 3 * f, WHITE(255));

	char buf[16] = "—";
	if (sys.volume >= 0)
		snprintf(buf, sizeof(buf), "%d", sys.volume);
	draw_text_right(font_title, PANEL_X + PANEL_W - PAD, cy + 7, WHITE(170 + 85 * f), 20, buf);
}

static void draw_tiles(float py) {
	for (int i = 0; i < TILE_COUNT; i++) {
		float x = tile_x(i), y = py + TILES_Y, w = tile_w();
		int focused = row == 1 && col == i;
		unsigned int bg = WHITE(22), fg = WHITE(230);
		if (confirm == i) {
			bg = RGBA8(232, 72, 62, 255);
			fg = WHITE(255);
		} else if (focused) {
			bg = WHITE(240 * row_focus[1] + 22 * (1 - row_focus[1]));
			fg = RGBA8(18, 18, 24, 255);
		}
		draw_round_rect(x, y, w, TILE_H, 16, bg);
		if (i == TILE_FTP && ftp_enabled() && confirm != i) {
			// pastille verte quand le serveur tourne, orange tant qu'il attend le Wi-Fi
			const char *label = ftp_running() ? "FTP activé" : "FTP…";
			float left = x + (w - vita2d_font_text_width(font_title, 17, label) - 14) / 2;
			vita2d_draw_fill_circle(left + 4, y + TILE_H / 2, 4, ftp_running() ? RGBA8(90, 210, 120, 255) : RGBA8(240, 170, 60, 255));
			vita2d_font_draw_text(font_title, (int)(left + 14), (int)(y + TILE_H / 2 + 6), fg, 17, label);
		} else {
			draw_text_center(font_title, x + w / 2, y + TILE_H / 2 + 6, fg, 17, confirm == i ? "Confirmer ?" : tile_labels[i]);
		}
	}
}

void panel_draw(void) {
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(150 * t)));
	float py = panel_y();
	draw_round_rect(PANEL_X, py, PANEL_W, PANEL_H, 26, RGBA8(26, 26, 32, 250));
	draw_header(py);
	draw_volume(py);
	draw_tiles(py);
}
