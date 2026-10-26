#pragma once

#include <vita2d.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define MARGIN_X 56

// Opacité 0..255 bornée : RGBA8 ne garde que l'octet de poids faible, 256 redeviendrait transparent.
static inline int alpha8(float a) {
	return a < 0 ? 0 : a > 255 ? 255 : (int)a;
}
#define WHITE(a) RGBA8(255, 255, 255, alpha8(a))

extern vita2d_font *font_title, *font_body;
extern vita2d_pgf *font_cjk; // police système (japonais, chinois, coréen), NULL si indisponible

int ui_init(void); // charge les polices ; -1 si elles manquent
float approach(float cur, float target, float speed);

// vita2d_font_draw_text et vita2d_font_text_width, par une instance de la police propre à la gamme de tailles : vita2d
// garde chaque glyphe à la taille de son premier tracé et l'étire ensuite (le « R » de 13 px flou dans un titre de 44 px).
void draw_text(vita2d_font *font, float x, float y, unsigned int color, unsigned int size, const char *text);
int text_width(vita2d_font *font, unsigned int size, const char *text);
float ease_out(float t);

// Polygone unique (éventail de triangles) : la transparence reste uniforme, sans chevauchement.
void draw_round_rect(float x, float y, float w, float h, float r, unsigned int color);
void draw_text_right(vita2d_font *font, float right, float y, unsigned int color, unsigned int size, const char *text);
void draw_text_center(vita2d_font *font, float center, float y, unsigned int color, unsigned int size, const char *text);

// Pictogrammes : (x, y) = coin bas gauche des barres Wi-Fi (18x13) ; coin haut gauche de la batterie (26x12).
void draw_wifi(float x, float y, int connected, int percent, float alpha);
void draw_battery(float x, float y, int percent, int charging, float alpha);

// Texte avec bascule automatique sur la police système si le texte contient des caractères CJK (absents d'Inter).
void draw_text_any(vita2d_font *font, float x, float y, unsigned int color, unsigned int size, const char *text);

typedef enum { GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_SQUARE, GLYPH_TRIANGLE } Glyph;
void draw_glyph(Glyph glyph, float cx, float cy, float alpha); // pastille blanche de 20 px avec le symbole
// « [glyphe] Libellé » aligné à droite sur `right` ; renvoie l'abscisse gauche, pour enchaîner les aides.
float draw_hint(Glyph glyph, const char *label, float right, float baseline, float alpha);
