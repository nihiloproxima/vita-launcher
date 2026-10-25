#include "ui.h"

#include <math.h>

#define CORNER_SEGMENTS 6

vita2d_font *font_title, *font_body;
vita2d_pgf *font_cjk;

int ui_init(void) {
	font_title = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	font_body = vita2d_load_font_file("app0:fonts/Inter-Regular.ttf");
	font_cjk = vita2d_load_default_pgf();
	return font_title && font_body ? 0 : -1;
}

float approach(float cur, float target, float speed) {
	return cur + (target - cur) * speed;
}

float ease_out(float t) {
	float u = 1 - t;
	return 1 - u * u * u;
}

void draw_round_rect(float x, float y, float w, float h, float r, unsigned int color) {
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	int count = 4 * (CORNER_SEGMENTS + 1) + 2;
	vita2d_color_vertex *v = vita2d_pool_memalign(count * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
	int k = 0;
	v[k++] = (vita2d_color_vertex){x + w / 2, y + h / 2, 0.5f, color};
	// coins dans le sens horaire, en partant du haut droit
	for (int c = 0; c < 4; c++) {
		float cx = c < 2 ? x + w - r : x + r;
		float cy = c == 1 || c == 2 ? y + h - r : y + r;
		for (int i = 0; i <= CORNER_SEGMENTS; i++) {
			float a = (c - 1 + (float)i / CORNER_SEGMENTS) * (float)M_PI / 2;
			v[k++] = (vita2d_color_vertex){cx + cosf(a) * r, cy + sinf(a) * r, 0.5f, color};
		}
	}
	v[k++] = v[1];
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, k);
}

void draw_text_right(vita2d_font *font, float right, float y, unsigned int color, unsigned int size, const char *text) {
	vita2d_font_draw_text(font, (int)(right - vita2d_font_text_width(font, size, text)), (int)y, color, size, text);
}

void draw_text_center(vita2d_font *font, float center, float y, unsigned int color, unsigned int size, const char *text) {
	vita2d_font_draw_text(font, (int)(center - vita2d_font_text_width(font, size, text) / 2), (int)y, color, size, text);
}

void draw_wifi(float x, float y, int connected, int percent, float alpha) {
	int lit = !connected ? 0 : percent >= 75 ? 4 : percent >= 50 ? 3 : percent >= 25 ? 2 : 1;
	for (int i = 0; i < 4; i++) {
		float h = 4 + i * 3;
		vita2d_draw_rectangle(x + i * 5, y - h, 3, h, WHITE((i < lit ? 230 : 70) * alpha));
	}
}

void draw_battery(float x, float y, int percent, int charging, float alpha) {
	unsigned int frame = WHITE(200 * alpha);
	vita2d_draw_rectangle(x, y, 23, 1.5f, frame);
	vita2d_draw_rectangle(x, y + 10.5f, 23, 1.5f, frame);
	vita2d_draw_rectangle(x, y, 1.5f, 12, frame);
	vita2d_draw_rectangle(x + 21.5f, y, 1.5f, 12, frame);
	vita2d_draw_rectangle(x + 23.5f, y + 3.5f, 2, 5, frame);
	unsigned int fill = percent <= 15 ? RGBA8(255, 95, 85, (int)(255 * alpha))
		: charging ? RGBA8(110, 220, 130, (int)(255 * alpha)) : WHITE(235 * alpha);
	vita2d_draw_rectangle(x + 3, y + 3, 17 * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f, 6, fill);
}

// Octet de tête UTF-8 >= 0xE3 : caractères à partir de U+3000 (kana, idéogrammes, hangul, pleine chasse).
static int has_cjk(const char *text) {
	for (const unsigned char *c = (const unsigned char *)text; *c; c++)
		if (*c >= 0xE3 && *c <= 0xEF)
			return 1;
	return 0;
}

void draw_text_any(vita2d_font *font, float x, float y, unsigned int color, unsigned int size, const char *text) {
	if (font_cjk && has_cjk(text))
		vita2d_pgf_draw_text(font_cjk, (int)x, (int)y, color, size / 18.0f, text);
	else
		vita2d_font_draw_text(font, (int)x, (int)y, color, size, text);
}

static void draw_line(float x0, float y0, float x1, float y1, float width, unsigned int color) {
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	float nx = -dy / len * width / 2, ny = dx / len * width / 2;
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
	v[0] = (vita2d_color_vertex){x0 + nx, y0 + ny, 0.5f, color};
	v[1] = (vita2d_color_vertex){x0 - nx, y0 - ny, 0.5f, color};
	v[2] = (vita2d_color_vertex){x1 + nx, y1 + ny, 0.5f, color};
	v[3] = (vita2d_color_vertex){x1 - nx, y1 - ny, 0.5f, color};
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

void draw_glyph(Glyph glyph, float cx, float cy, float alpha) {
	unsigned int ink = RGBA8(26, 26, 32, (int)(255 * alpha));
	vita2d_draw_fill_circle(cx, cy, 10, WHITE(235 * alpha));
	switch (glyph) {
	case GLYPH_CROSS:
		draw_line(cx - 4, cy - 4, cx + 4, cy + 4, 2, ink);
		draw_line(cx - 4, cy + 4, cx + 4, cy - 4, 2, ink);
		break;
	case GLYPH_CIRCLE:
		vita2d_draw_fill_circle(cx, cy, 5, ink);
		vita2d_draw_fill_circle(cx, cy, 3.2f, WHITE(235 * alpha));
		break;
	case GLYPH_SQUARE:
		draw_line(cx - 4, cy - 4.9f, cx - 4, cy + 4.9f, 1.8f, ink);
		draw_line(cx + 4, cy - 4.9f, cx + 4, cy + 4.9f, 1.8f, ink);
		draw_line(cx - 4.9f, cy - 4, cx + 4.9f, cy - 4, 1.8f, ink);
		draw_line(cx - 4.9f, cy + 4, cx + 4.9f, cy + 4, 1.8f, ink);
		break;
	case GLYPH_TRIANGLE:
		draw_line(cx, cy - 5, cx + 5, cy + 3.8f, 1.8f, ink);
		draw_line(cx + 5, cy + 3.8f, cx - 5, cy + 3.8f, 1.8f, ink);
		draw_line(cx - 5, cy + 3.8f, cx, cy - 5, 1.8f, ink);
		break;
	}
}

float draw_hint(Glyph glyph, const char *label, float right, float baseline, float alpha) {
	float text_x = right - vita2d_font_text_width(font_body, 17, label);
	vita2d_font_draw_text(font_body, (int)text_x, (int)baseline, WHITE(210 * alpha), 17, label);
	draw_glyph(glyph, text_x - 18, baseline - 6, alpha);
	return text_x - 30;
}
