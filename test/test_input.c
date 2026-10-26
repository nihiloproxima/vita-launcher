// Test hôte : cc -idirafter $VITASDK/arm-vita-eabi/include test/test_input.c src/input.c -o build/test_input && build/test_input
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/input.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

// Faux système : sceCtrlPeekBufferPositive rend les échantillons préparés par le test.
static SceCtrlData samples[16];
static int sample_count;
static uint64_t now;

int sceCtrlSetSamplingMode(SceCtrlPadInputMode mode) { return 0; }
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state) { return 0; }
int sceMotionStartSampling(void) { return 0; }
int sceMotionGetState(SceMotionState *state) { return -1; }
int sceTouchPeek(SceUInt32 port, SceTouchData *data, SceUInt32 count) {
	memset(data, 0, sizeof(*data));
	return 1;
}
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pads, int count) {
	memcpy(pads, samples, sample_count * sizeof(SceCtrlData));
	return sample_count;
}

static SceCtrlData sample(uint32_t buttons) {
	return (SceCtrlData){.timeStamp = ++now, .buttons = buttons, .lx = 128, .ly = 128};
}

// Une image : les échantillons arrivés depuis la précédente, du plus ancien au plus récent.
static Input frame(Input *in, int count, const uint32_t *buttons) {
	for (int i = 0; i < count; i++)
		samples[i] = sample(buttons[i]);
	sample_count = count;
	input_read(in);
	return *in;
}

int main(void) {
	Input in = {0};
	const uint32_t neutral[] = {0}, left[] = {SCE_CTRL_LEFT}, right[] = {SCE_CTRL_RIGHT};
	input_init();

	assert(frame(&in, 1, neutral).dx == 0);

	// gauche puis droite sans repasser par le neutre : deux déplacements
	assert(frame(&in, 1, left).dx == -1);
	assert(frame(&in, 1, right).dx == 1);
	assert(frame(&in, 1, neutral).dx == 0);

	// appui bref pendant une image lente : compté, et l'état courant reste le neutre
	const uint32_t tap[] = {0, SCE_CTRL_RIGHT, SCE_CTRL_RIGHT, 0};
	assert(frame(&in, 4, tap).dx == 1);
	assert(frame(&in, 1, neutral).dx == 0);
	const uint32_t cross[] = {0, SCE_CTRL_CROSS, 0};
	assert(frame(&in, 3, cross).pressed & SCE_CTRL_CROSS);
	assert(!(in.held & SCE_CTRL_CROSS));

	// même chose rendu du plus récent (indice 0) au plus ancien (indice 2)
	samples[2] = sample(0);
	samples[1] = sample(SCE_CTRL_LEFT);
	samples[0] = sample(0);
	sample_count = 3;
	input_read(&in);
	assert(in.dx == -1);

	// aucun nouvel échantillon : l'appui déjà vu ne compte pas deux fois
	assert(frame(&in, 1, right).dx == 1);
	sample_count = 1; // samples[0] inchangé, même horodatage
	input_read(&in);
	assert(in.dx == 0);

	// maintenu : rien pendant ~0,3 s, puis une répétition toutes les 5 images
	frame(&in, 1, neutral);
	assert(frame(&in, 1, right).dx == 1);
	int fired = 0, first = -1;
	for (int f = 1; f <= 40; f++) {
		if (frame(&in, 1, right).dx == 1) {
			fired++;
			if (first < 0)
				first = f;
		}
	}
	assert(first >= 18 && first <= 20 && fired == 5);

	// joystick : même règle que la croix, y compris droite -> gauche d'un coup
	frame(&in, 1, neutral);
	samples[0] = sample(0);
	samples[0].lx = 255;
	input_read(&in);
	assert(in.dx == 1);
	samples[0] = sample(0);
	samples[0].lx = 0;
	input_read(&in);
	assert(in.dx == -1);

	puts("test_input OK");
	return 0;
}
