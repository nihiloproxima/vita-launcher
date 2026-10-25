#pragma once

#include <stdint.h>
#include <vita2d.h>

#define MAX_APPS 256

typedef enum { ICON_PENDING, ICON_READY, ICON_NONE } IconState;
typedef enum { KIND_APP, KIND_VITA, KIND_PSP, KIND_GBA, KIND_PS1 } Kind; // KIND_APP : onglet Applis, les autres : Jeux

typedef struct {
	char id[16]; // title ID ; pour une ROM, identifiant à nous en minuscules (aucun title ID n'en a)
	char title[128];
	char version[8];
	char path[256]; // ROM, lancée par RetroArch ; "" pour une appli installée
	Kind kind;
	int pinned;
	unsigned int played;      // temps de jeu en secondes, noté par le plugin de démarrage
	unsigned int last_played; // heure locale en secondes de la dernière partie, 0 si jamais jouée
	IconState icon_state;
	vita2d_texture *icon; // carré, coins arrondis
	vita2d_texture *blur; // icône réduite à 16x16 : agrandie avec filtrage linéaire, elle donne un fond flou
	uint8_t r, g, b;      // couleur dominante
	float focus;          // 0 = repos, 1 = sélectionnée (animé)
	float appear;         // 0 -> 1 quand l'icône arrive (fondu)
} App;

extern App apps[MAX_APPS];
extern int app_count;

static inline int app_tab(const App *a) { return a->kind == KIND_APP; } // 0 = Jeux, 1 = Applis

// Liste ux0:app et les ROMs (ux0:roms/gba, ux0:roms/ps1, si RetroArch est installé),
// trie : onglet, puis épinglées, puis récentes, puis par nom.
void apps_scan(void);
const char *apps_kind(const App *a); // « Jeu PS Vita », « Game Boy Advance »…
const char *apps_last_launched(void); // id de la dernière appli lancée, NULL si aucune
int apps_load_icons(int budget); // charge jusqu'à `budget` icônes et intègre les jaquettes arrivées
int apps_find(const char *id);   // index, ou -1
// Mémorise dans les récentes puis lance. Avec le plugin de démarrage, renvoie 1 : le launcher affiche
// son animation de lancement jusqu'à ce que le plugin le ferme. Sinon ne revient pas.
int apps_launch(const App *a);
int apps_boot_plugin(void); // le plugin de démarrage (plugin/boot.c) est-il dans la config taiHEN ?
int apps_reload(void); // fait relancer le launcher par le plugin (après une mise à jour par FTP) ; 0 sans plugin
void apps_toggle_pin(const char *id);
int apps_uninstall(const char *id); // < 0 en cas d'échec
