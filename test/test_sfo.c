// Test hôte : cc test/test_sfo.c src/sfo.c -o build/test_sfo && build/test_sfo
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/sfo.h"

#define FIXTURE "test/fixtures/test_app.sfo"

int main(void) {
	char out[128];
	assert(read_sfo_string(FIXTURE, "TITLE", out, sizeof(out)) == 0);
	assert(strcmp(out, "Test App") == 0);
	assert(read_sfo_string(FIXTURE, "TITLE_ID", out, sizeof(out)) == 0);
	assert(strcmp(out, "TEST00001") == 0); // clé dont TITLE est un préfixe : pas de confusion
	char tiny[6];
	assert(read_sfo_string(FIXTURE, "TITLE", tiny, sizeof(tiny)) == 0);
	assert(strcmp(tiny, "Test ") == 0); // tronqué proprement
	assert(read_sfo_string(FIXTURE, "NOPE", out, sizeof(out)) == -1);
	assert(read_sfo_string("test/fixtures/absent.sfo", "TITLE", out, sizeof(out)) == -1);
	assert(read_sfo_string("CMakeLists.txt", "TITLE", out, sizeof(out)) == -1); // pas un PSF
	puts("test_sfo OK");
	return 0;
}
