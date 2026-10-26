#pragma once

// Sons d'interface, synthétisés au démarrage. La tuile Sons du panneau les coupe (réglage retenu).
typedef enum { SOUND_MOVE, SOUND_OPEN, SOUND_CLOSE, SOUND_LAUNCH, SOUND_COUNT } Sound;

void sound_init(void);
void sound_play(Sound s); // remplace le son en cours
void sound_toggle(void);
int sound_enabled(void);
