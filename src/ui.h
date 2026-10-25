#pragma once

#include <vita2d.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define MARGIN_X 56

#define WHITE(a) RGBA8(255, 255, 255, (int)(a))

extern vita2d_font *font_title, *font_body;

int ui_init(void); // charge les polices ; -1 si elles manquent
float approach(float cur, float target, float speed);
float ease_out(float t);

// Polygone unique (éventail de triangles) : la transparence reste uniforme, sans chevauchement.
void draw_round_rect(float x, float y, float w, float h, float r, unsigned int color);
void draw_text_right(vita2d_font *font, float right, float y, unsigned int color, unsigned int size, const char *text);
void draw_text_center(vita2d_font *font, float center, float y, unsigned int color, unsigned int size, const char *text);

// Pictogrammes : (x, y) = coin bas gauche des barres Wi-Fi (18x13) ; coin haut gauche de la batterie (26x12).
void draw_wifi(float x, float y, int connected, int percent, float alpha);
void draw_battery(float x, float y, int percent, int charging, float alpha);
