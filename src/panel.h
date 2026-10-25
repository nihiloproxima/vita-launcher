#pragma once

#include "input.h"

// Panneau rapide : état de la console, volume, Paramètres et alimentation.
void panel_open(void);
int panel_visible(void);            // ouvert, ou en train de se refermer
void panel_update(const Input *in); // à appeler seulement si panel_visible()
void panel_draw(void);
