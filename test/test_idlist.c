// Test hôte : cc test/test_idlist.c src/idlist.c -o build/test_idlist && build/test_idlist
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/idlist.h"

int main(void) {
	IdList l = {0};
	idlist_push_back(&l, "AAAA00001");
	idlist_push_back(&l, "BBBB00002");
	idlist_push_back(&l, "AAAA00001"); // doublon ignoré
	assert(l.count == 2);

	idlist_push_front(&l, "BBBB00002"); // déjà présent : remonte en tête
	assert(l.count == 2 && strcmp(l.ids[0], "BBBB00002") == 0 && strcmp(l.ids[1], "AAAA00001") == 0);

	idlist_remove(&l, "BBBB00002");
	assert(l.count == 1 && idlist_index(&l, "AAAA00001") == 0 && idlist_index(&l, "BBBB00002") == -1);

	IdList full = {0};
	char id[16];
	for (int i = 0; i < IDLIST_MAX + 5; i++) {
		snprintf(id, sizeof(id), "TEST%05d", i);
		idlist_push_front(&full, id);
	}
	assert(full.count == IDLIST_MAX);
	snprintf(id, sizeof(id), "TEST%05d", IDLIST_MAX + 4);
	assert(idlist_index(&full, id) == 0); // le plus récent en tête, les plus anciens sortis
	assert(idlist_index(&full, "TEST00000") == -1);

	const char *path = "build/test_idlist.txt";
	assert(idlist_save(&full, path) == 0);
	IdList back;
	idlist_load(&back, path);
	assert(back.count == full.count);
	for (int i = 0; i < full.count; i++)
		assert(strcmp(back.ids[i], full.ids[i]) == 0);

	FILE *f = fopen(path, "w");
	fputs("BAD\nCCCC00003\r\n\n", f); // ligne invalide et fin de ligne Windows
	fclose(f);
	idlist_load(&back, path);
	assert(back.count == 1 && strcmp(back.ids[0], "CCCC00003") == 0);

	idlist_load(&back, "build/absent.txt");
	assert(back.count == 0);
	puts("test_idlist OK");
	return 0;
}
