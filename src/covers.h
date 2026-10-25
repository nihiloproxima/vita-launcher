#pragma once

// Jaquettes téléchargées en arrière-plan, mises en cache sur la carte sous le nom de l'appli.
void covers_init(void);
int covers_cached(const char *id, char *path, int size); // 1 si la jaquette est en cache (chemin dans path)
int covers_known_missing(const char *id);                // 1 si la base n'en a pas (inutile de redemander)
void covers_request(const char *id, const char *url);
int covers_poll(char id[16]); // 1 si une jaquette vient d'arriver (son id dans id), à appeler jusqu'à 0
