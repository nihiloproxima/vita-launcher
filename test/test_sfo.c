// Test hôte : cc test/test_sfo.c src/sfo.c -o build/test_sfo && build/test_sfo
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/sfo.h"

int main(void) {
	char title[128];
	assert(read_sfo_title("test/fixtures/test_app.sfo", title, sizeof(title)) == 0);
	assert(strcmp(title, "Test App") == 0);
	char tiny[6];
	assert(read_sfo_title("test/fixtures/test_app.sfo", tiny, sizeof(tiny)) == 0);
	assert(strcmp(tiny, "Test ") == 0); // tronqué proprement
	assert(read_sfo_title("test/fixtures/absent.sfo", title, sizeof(title)) == -1);
	assert(read_sfo_title("CMakeLists.txt", title, sizeof(title)) == -1); // pas un PSF
	puts("test_sfo OK");
	return 0;
}
