#pragma once

// Liste ordonnée d'identifiants d'applis (title ID), sauvegardée en texte, un par ligne.
#define IDLIST_MAX 64

typedef struct {
	char ids[IDLIST_MAX][16];
	int count;
} IdList;

int idlist_index(const IdList *l, const char *id); // -1 si absent
void idlist_remove(IdList *l, const char *id);
void idlist_push_front(IdList *l, const char *id); // déplace en tête si présent ; le plus ancien sort si pleine
void idlist_push_back(IdList *l, const char *id);  // ignoré si présent ou pleine
void idlist_load(IdList *l, const char *path);     // liste vide si le fichier manque
int idlist_save(const IdList *l, const char *path);
