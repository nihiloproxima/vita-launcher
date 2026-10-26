#include "input.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <stdlib.h>

#define HISTORY 16 // échantillons relus (~0,25 s) : un appui bref pendant une image lente (PNG décodé…) n'est pas perdu

enum { GO_LEFT = 1, GO_RIGHT = 2, GO_UP = 4, GO_DOWN = 8 };

static uint32_t old_buttons, old_dirs;
static uint64_t last_stamp;
static int hold_x, hold_y, motion_ready;
static float rest_x, rest_y;

void input_init(void) {
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceMotionStartSampling();
}

// Croix et joystick gauche confondus.
static uint32_t directions(const SceCtrlData *p) {
	uint32_t d = 0;
	if ((p->buttons & SCE_CTRL_LEFT) || p->lx < 40)
		d |= GO_LEFT;
	if ((p->buttons & SCE_CTRL_RIGHT) || p->lx > 215)
		d |= GO_RIGHT;
	if ((p->buttons & SCE_CTRL_UP) || p->ly < 40)
		d |= GO_UP;
	if ((p->buttons & SCE_CTRL_DOWN) || p->ly > 215)
		d |= GO_DOWN;
	return d;
}

// Chaque appui compte tout de suite, même relâché depuis ou enchaîné sans repasser par le neutre (gauche puis droite) ;
// maintenu, il se répète toutes les 5 images après ~0,3 s.
static int repeat(uint32_t held, uint32_t pressed, uint32_t neg, uint32_t pos, int *hold) {
	if (pressed & (neg | pos)) {
		*hold = 1;
		return pressed & neg ? -1 : 1;
	}
	int dir = held & neg ? -1 : held & pos ? 1 : 0;
	if (!dir) {
		*hold = 0;
		return 0;
	}
	(*hold)++;
	return *hold > 18 && *hold % 5 == 0 ? dir : 0;
}

void input_read(Input *in) {
	// tous les échantillons arrivés depuis l'image précédente, du plus ancien au plus récent (l'ordre rendu n'est pas
	// documenté : on se fie à l'horodatage) ; le plus récent toujours, pour l'état courant
	SceCtrlData pads[HISTORY];
	int n = sceCtrlPeekBufferPositive(0, pads, HISTORY);
	n = n < 1 ? 0 : n > HISTORY ? HISTORY : n;
	int newest_first = n > 1 && pads[0].timeStamp > pads[n - 1].timeStamp;
	uint32_t pressed = 0, dir_pressed = 0;
	for (int k = 0; k < n; k++) {
		const SceCtrlData *p = &pads[newest_first ? n - 1 - k : k];
		if (p->timeStamp <= last_stamp && k < n - 1)
			continue;
		uint32_t dirs = directions(p);
		pressed |= p->buttons & ~old_buttons;
		dir_pressed |= dirs & ~old_dirs;
		old_buttons = p->buttons;
		old_dirs = dirs;
		last_stamp = p->timeStamp;
	}
	in->held = old_buttons;
	in->pressed = pressed;
	in->dx = repeat(old_dirs, dir_pressed, GO_LEFT, GO_RIGHT, &hold_x);
	in->dy = repeat(old_dirs, dir_pressed, GO_UP, GO_DOWN, &hold_y);

	SceTouchData touch;
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
	in->tap = 0;
	in->swipe_x = in->swipe_y = 0;
	if (touch.reportNum > 0) {
		in->x = touch.report[0].x / 2;
		in->y = touch.report[0].y / 2;
		if (!in->touching) {
			in->x0 = in->x;
			in->y0 = in->y;
		}
		in->touching = 1;
	} else if (in->touching) {
		in->touching = 0;
		int mx = in->x - in->x0, my = in->y - in->y0;
		if (abs(mx) < 20 && abs(my) < 20) {
			in->tap = 1;
		} else {
			in->swipe_x = mx;
			in->swipe_y = my;
		}
	}

	// accéléromètre : écart à la position moyenne des ~2 dernières secondes, la console se tient à n'importe quel angle
	SceMotionState motion;
	if (sceMotionGetState(&motion) >= 0) {
		float ax = motion.acceleration.x, ay = motion.acceleration.y;
		if (!motion_ready) {
			rest_x = ax;
			rest_y = ay;
			motion_ready = 1;
		}
		rest_x += (ax - rest_x) * 0.01f;
		rest_y += (ay - rest_y) * 0.01f;
		in->tilt_x += (ax - rest_x - in->tilt_x) * 0.15f;
		in->tilt_y += (ay - rest_y - in->tilt_y) * 0.15f;
	}
}
