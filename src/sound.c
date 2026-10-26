#include "sound.h"

#include <math.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RATE      48000
#define GRAIN     256
#define MUTE_FLAG "ux0:data/vita-launcher/sound_off" // présent = sons coupés

// Note : sinusoïde de f0 à f1 (glissando). decay > 0 : attaque nette puis décroissance exponentielle (constante
// en secondes), sinon enveloppe en cloche sur toute la durée.
typedef struct {
	float at, length, f0, f1, amp, decay;
} Note;

static const Note tick[] = {{0, 0.06f, 900, 700, 0.22f, 0.012f}, {0, 0.04f, 1800, 1400, 0.05f, 0.006f}};
static const Note chime_up[] = {{0, 0.3f, 660, 660, 0.15f, 0.06f}, {0.06f, 0.4f, 990, 990, 0.13f, 0.08f}};
static const Note chime_down[] = {{0, 0.3f, 880, 880, 0.13f, 0.06f}, {0.06f, 0.4f, 587, 587, 0.13f, 0.08f}};
static const Note whoosh[] = {
	{0, 0.6f, 260, 1040, 0.2f, 0}, {0, 0.6f, 390, 1560, 0.07f, 0}, {0.35f, 0.6f, 1318, 1318, 0.1f, 0.12f}};

static const struct {
	const Note *notes;
	int count;
} recipes[SOUND_COUNT] = {
	[SOUND_MOVE] = {tick, 2}, [SOUND_OPEN] = {chime_up, 2}, [SOUND_CLOSE] = {chime_down, 2}, [SOUND_LAUNCH] = {whoosh, 3},
};

static int16_t *pcm[SOUND_COUNT];
static int length[SOUND_COUNT];
static int port = -1, muted;
static volatile int request, request_seq; // écrits par sound_play, lus par le thread audio
static SceUID wake;

static void render(Sound s) {
	float end = 0;
	for (int i = 0; i < recipes[s].count; i++)
		end = fmaxf(end, recipes[s].notes[i].at + recipes[s].notes[i].length);
	int len = (int)(end * RATE);
	float *mix = calloc(len, sizeof(float));
	pcm[s] = malloc(len * sizeof(int16_t));
	if (!mix || !pcm[s]) {
		free(mix);
		return;
	}
	for (int i = 0; i < recipes[s].count; i++) {
		const Note *n = &recipes[s].notes[i];
		int from = (int)(n->at * RATE), count = (int)(n->length * RATE);
		float phase = 0;
		for (int j = 0; j < count && from + j < len; j++) {
			float t = (float)j / RATE, k = (float)j / count, bell = sinf((float)M_PI * k);
			float env = n->decay > 0 ? expf(-t / n->decay) * fminf(t / 0.002f, 1) : bell * bell; // 2 ms d'attaque : pas de clic
			phase += 2 * (float)M_PI * (n->f0 + (n->f1 - n->f0) * k) / RATE;
			mix[from + j] += n->amp * env * sinf(phase);
		}
	}
	for (int j = 0; j < len; j++)
		pcm[s][j] = (int16_t)(fmaxf(-1, fminf(1, mix[j])) * 32767);
	free(mix);
	length[s] = len;
}

// Joue le dernier son demandé, une tranche à la fois (deux tampons : celui qu'on remplit, celui que la puce lit).
static int audio_thread(SceSize args, void *argp) {
	static int16_t buf[2][GRAIN];
	int seen = 0, cur = -1, pos = 0, b = 0;
	for (;;) {
		if (seen != request_seq) {
			seen = request_seq;
			cur = request;
			pos = 0;
		}
		if (cur < 0) {
			sceKernelWaitSema(wake, 1, NULL);
			continue;
		}
		int n = length[cur] - pos < GRAIN ? length[cur] - pos : GRAIN;
		memcpy(buf[b], pcm[cur] + pos, n * sizeof(int16_t));
		memset(buf[b] + n, 0, (GRAIN - n) * sizeof(int16_t));
		pos += n;
		if (pos >= length[cur])
			cur = -1;
		sceAudioOutOutput(port, buf[b]);
		b ^= 1;
	}
	return 0;
}

void sound_init(void) {
	SceIoStat st;
	muted = sceIoGetstat(MUTE_FLAG, &st) >= 0;
	for (int s = 0; s < SOUND_COUNT; s++)
		render(s);
	port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, RATE, SCE_AUDIO_OUT_MODE_MONO);
	wake = sceKernelCreateSema("sound_wake", 0, 0, 1, NULL);
	// priorité au-dessus du thread principal : un PNG décodé pendant un son ne le hache pas
	SceUID thread = port >= 0 && wake >= 0 ? sceKernelCreateThread("sound", audio_thread, 100, 0x4000, 0, 0, NULL) : -1;
	if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0)
		port = -1;
}

void sound_play(Sound s) {
	if (port < 0 || muted || !length[s])
		return;
	request = s;
	request_seq++;
	sceKernelSignalSema(wake, 1); // déjà au maximum si le thread joue : sans effet, il verra la demande
}

void sound_toggle(void) {
	muted = !muted;
	if (muted) {
		SceUID fd = sceIoOpen(MUTE_FLAG, SCE_O_WRONLY | SCE_O_CREAT, 0666);
		if (fd >= 0)
			sceIoClose(fd);
	} else {
		sceIoRemove(MUTE_FLAG);
		sound_play(SOUND_OPEN);
	}
}

int sound_enabled(void) {
	return !muted;
}
