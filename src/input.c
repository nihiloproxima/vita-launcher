#include "input.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <stdlib.h>

static uint32_t old_buttons;
static int hold_x, hold_y;

void input_init(void) {
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

// Premier appui immédiat, puis répétition toutes les 5 images après ~0,3 s.
static int repeat(int dir, int *hold) {
	if (!dir) {
		*hold = 0;
		return 0;
	}
	int fire = *hold == 0 || (*hold > 18 && *hold % 5 == 0);
	(*hold)++;
	return fire ? dir : 0;
}

void input_read(Input *in) {
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	in->held = pad.buttons;
	in->pressed = pad.buttons & ~old_buttons;
	old_buttons = pad.buttons;

	int dx = (pad.buttons & SCE_CTRL_LEFT) || pad.lx < 40 ? -1 : (pad.buttons & SCE_CTRL_RIGHT) || pad.lx > 215 ? 1 : 0;
	int dy = (pad.buttons & SCE_CTRL_UP) || pad.ly < 40 ? -1 : (pad.buttons & SCE_CTRL_DOWN) || pad.ly > 215 ? 1 : 0;
	in->dx = repeat(dx, &hold_x);
	in->dy = repeat(dy, &hold_y);

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
}
