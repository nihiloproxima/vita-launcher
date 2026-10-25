#include "covers.h"

#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

#define COVERS_DIR "ux0:data/vita-launcher/covers"
#define COVERS_URL "https://raw.githubusercontent.com/Andiweli/HexFlow-Covers/main/Covers/PSVita/%s.png"
#define QUEUE 128

// Deux files à un seul écrivain chacune : demandes (thread principal -> téléchargement) et arrivées (l'inverse).
static char requests[QUEUE][16], arrivals[QUEUE][16];
static int req_head, req_tail, arr_head, arr_tail;
static SceUID pending;

static void cover_file(const char *id, const char *ext, char *out, int size) {
	snprintf(out, size, COVERS_DIR "/%s.%s", id, ext);
}

static int exists(const char *path) {
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

int covers_cached(const char *id, char *path, int size) {
	cover_file(id, "png", path, size);
	return exists(path);
}

int covers_known_missing(const char *id) {
	char path[96];
	cover_file(id, "none", path, sizeof(path));
	return exists(path);
}

static int download(const char *id) {
	char url[160], path[96], part[104];
	snprintf(url, sizeof(url), COVERS_URL, id);
	cover_file(id, "png", path, sizeof(path));
	snprintf(part, sizeof(part), "%s.part", path);
	FILE *f = fopen(part, "wb");
	if (!f)
		return 0;
	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_CAINFO, "app0:certs/cacert.pem");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "vita-launcher");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(curl);
	fclose(f);
	if (res == CURLE_OK && code == 200) {
		sceIoRename(part, path); // .part évite de garder une image tronquée si la connexion coupe
		return 1;
	}
	sceIoRemove(part);
	if (res == CURLE_OK && code == 404) {
		cover_file(id, "none", path, sizeof(path));
		FILE *marker = fopen(path, "w");
		if (marker)
			fclose(marker);
	}
	return 0; // autre échec (pas de réseau…) : redemandé au prochain lancement
}

static int worker(SceSize args, void *argp) {
	curl_global_init(CURL_GLOBAL_ALL);
	while (sceKernelWaitSema(pending, 1, NULL) >= 0) {
		int head = __atomic_load_n(&req_head, __ATOMIC_RELAXED);
		char id[16];
		memcpy(id, requests[head % QUEUE], sizeof(id));
		__atomic_store_n(&req_head, head + 1, __ATOMIC_RELEASE);
		if (download(id)) {
			int tail = __atomic_load_n(&arr_tail, __ATOMIC_RELAXED);
			if (tail - __atomic_load_n(&arr_head, __ATOMIC_ACQUIRE) < QUEUE) {
				memcpy(arrivals[tail % QUEUE], id, sizeof(id));
				__atomic_store_n(&arr_tail, tail + 1, __ATOMIC_RELEASE);
			}
		}
	}
	return 0;
}

void covers_init(void) {
	sceIoMkdir("ux0:data/vita-launcher", 0777);
	sceIoMkdir(COVERS_DIR, 0777);
	pending = sceKernelCreateSema("covers_pending", 0, 0, QUEUE, NULL);
	// TLS (mbedtls) gourmand en pile : 1 Mo
	SceUID thread = sceKernelCreateThread("covers", worker, 0x10000100, 0x100000, 0, 0, NULL);
	if (pending >= 0 && thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
}

void covers_request(const char *id) {
	int tail = __atomic_load_n(&req_tail, __ATOMIC_RELAXED);
	if (pending < 0 || tail - __atomic_load_n(&req_head, __ATOMIC_ACQUIRE) >= QUEUE)
		return;
	snprintf(requests[tail % QUEUE], sizeof(requests[0]), "%s", id);
	__atomic_store_n(&req_tail, tail + 1, __ATOMIC_RELEASE);
	sceKernelSignalSema(pending, 1);
}

int covers_poll(char id[16]) {
	int head = __atomic_load_n(&arr_head, __ATOMIC_RELAXED);
	if (head == __atomic_load_n(&arr_tail, __ATOMIC_ACQUIRE))
		return 0;
	memcpy(id, arrivals[head % QUEUE], 16);
	__atomic_store_n(&arr_head, head + 1, __ATOMIC_RELEASE);
	return 1;
}
