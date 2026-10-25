#include "idlist.h"

#include <stdio.h>
#include <string.h>

int idlist_index(const IdList *l, const char *id) {
	for (int i = 0; i < l->count; i++)
		if (strcmp(l->ids[i], id) == 0)
			return i;
	return -1;
}

void idlist_remove(IdList *l, const char *id) {
	int i = idlist_index(l, id);
	if (i < 0)
		return;
	memmove(l->ids[i], l->ids[i + 1], (l->count - i - 1) * sizeof(l->ids[0]));
	l->count--;
}

void idlist_push_front(IdList *l, const char *id) {
	idlist_remove(l, id);
	if (l->count == IDLIST_MAX)
		l->count--;
	memmove(l->ids[1], l->ids[0], l->count * sizeof(l->ids[0]));
	snprintf(l->ids[0], sizeof(l->ids[0]), "%s", id);
	l->count++;
}

void idlist_push_back(IdList *l, const char *id) {
	if (l->count == IDLIST_MAX || idlist_index(l, id) >= 0)
		return;
	snprintf(l->ids[l->count++], sizeof(l->ids[0]), "%s", id);
}

void idlist_load(IdList *l, const char *path) {
	l->count = 0;
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[64];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (strlen(line) == 9) // un title ID fait toujours 9 caractères
			idlist_push_back(l, line);
	}
	fclose(f);
}

int idlist_save(const IdList *l, const char *path) {
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	for (int i = 0; i < l->count; i++)
		fprintf(f, "%s\n", l->ids[i]);
	return fclose(f);
}
