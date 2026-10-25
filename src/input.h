#pragma once

#include <stdint.h>

typedef struct {
	uint32_t held, pressed; // boutons maintenus / appuyés à cette image
	int dx, dy;             // -1, 0 ou 1, avec répétition auto (croix ou joystick gauche)
	int touching, x, y;     // doigt posé et position courante (écran 960x544)
	int x0, y0;             // position au moment du contact
	int tap;                // doigt relevé sans avoir bougé (position : x, y)
	int swipe_x, swipe_y;   // doigt relevé après un glissé : déplacement total, sinon 0
} Input;

void input_init(void);
void input_read(Input *in);
