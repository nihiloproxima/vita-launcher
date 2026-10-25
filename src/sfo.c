#include "sfo.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Format PSF : en-tête (20 octets), index (16 octets par clé), table des clés, table des données.
int read_sfo_string(const char *path, const char *name, char *out, int out_size) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 20 || n > 64 * 1024) {
		fclose(f);
		return -1;
	}
	uint8_t *buf = malloc(n);
	int ok = buf && fread(buf, 1, n, f) == (size_t)n;
	fclose(f);
	int ret = -1;
	size_t name_len = strlen(name) + 1;
	if (ok && memcmp(buf, "\0PSF", 4) == 0) {
		uint32_t keys = *(uint32_t *)(buf + 8), data = *(uint32_t *)(buf + 12), count = *(uint32_t *)(buf + 16);
		for (uint32_t i = 0; i < count && 20 + (i + 1) * 16 <= (uint32_t)n; i++) {
			uint8_t *e = buf + 20 + i * 16;
			uint32_t key = keys + *(uint16_t *)e, len = *(uint32_t *)(e + 4), off = data + *(uint32_t *)(e + 12);
			if (key + name_len > (uint32_t)n || memcmp(buf + key, name, name_len) != 0 || off + len > (uint32_t)n)
				continue;
			int l = (int)len < out_size - 1 ? (int)len : out_size - 1;
			memcpy(out, buf + off, l);
			out[l] = 0;
			for (char *c = out; *c; c++)
				if (*c == '\n')
					*c = ' ';
			ret = 0;
			break;
		}
	}
	free(buf);
	return ret;
}
