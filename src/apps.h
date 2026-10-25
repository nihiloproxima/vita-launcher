#pragma once

#include <stdint.h>
#include <vita2d.h>

#define MAX_APPS 256

typedef enum { ICON_PENDING, ICON_READY, ICON_NONE } IconState;

typedef struct {
	char id[16];
	char title[128];
	char version[8];
	int is_game;
	int pinned;
	IconState icon_state;
	vita2d_texture *icon; // carré, coins arrondis
	vita2d_texture *blur; // icône réduite à 16x16 : agrandie avec filtrage linéaire, elle donne un fond flou
	uint8_t r, g, b;      // couleur dominante
	float focus;          // 0 = repos, 1 = sélectionnée (animé)
	float appear;         // 0 -> 1 quand l'icône arrive (fondu)
} App;

extern App apps[MAX_APPS];
extern int app_count;

void apps_scan(void);            // liste ux0:app, trie : épinglées, puis récentes, puis par nom
int apps_load_icons(int budget); // charge jusqu'à `budget` icônes et intègre les jaquettes arrivées
int apps_find(const char *id);   // index, ou -1
// Mémorise dans les récentes puis lance. Avec le plugin de démarrage, renvoie 1 : le launcher affiche
// son animation de lancement jusqu'à ce que le plugin le ferme. Sinon ne revient pas.
int apps_launch(const App *a);
int apps_boot_plugin(void); // le plugin de démarrage (plugin/boot.c) est-il dans la config taiHEN ?
int apps_reload(void); // fait relancer le launcher par le plugin (après une mise à jour par FTP) ; 0 sans plugin
void apps_toggle_pin(const char *id);
int apps_uninstall(const char *id); // < 0 en cas d'échec
