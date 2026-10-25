#pragma once

#include "apps.h"
#include "input.h"

// Fiche d'appli (bouton ▢) : infos et actions. Les actions sont exécutées par l'appelant.
typedef enum { SHEET_NONE, SHEET_LAUNCH, SHEET_PIN, SHEET_UNINSTALL } SheetAction;

void sheet_open(const App *app);
int sheet_visible(void);
SheetAction sheet_update(const Input *in); // à appeler seulement si sheet_visible() ; referme la fiche après une action
void sheet_draw(void);
