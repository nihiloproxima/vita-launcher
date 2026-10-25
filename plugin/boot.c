// Plugin SceShell (section *main de ur0:tai/config.txt) : démarre le launcher au boot
// et le relance quand on quitte une autre appli, pour qu'il reste l'interface principale.
// Si le launcher lui-même se ferme (fermé à la main, plantage), on ne le relance pas :
// l'accueil Sony reste accessible et un plantage ne tourne pas en boucle.
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

#define LAUNCHER_ID    "LNCH00001"
#define LAUNCHER_URI   "psgm:play?titleid=" LAUNCHER_ID
#define LAUNCH_REQUEST "ux0:data/vita-launcher/launch.txt"
#define UPDATE_FILE    "ux0:data/vita-launcher/eboot.new" // déposé par tools/deploy.sh
#define LAUNCHER_EBOOT "ux0:app/" LAUNCHER_ID "/eboot.bin"
#define BACKUP_EBOOT   "ux0:app/" LAUNCHER_ID "/eboot.old"
#define UPDATE_LOG     "ux0:data/vita-launcher/update.log"
#define PLAYTIME_LOG   "ux0:data/vita-launcher/playtime.txt" // cumulé par le launcher (src/apps.c)
#define CHECKPOINT_S   300 // tranche écrite pendant la partie : une coupure de courant en perd au plus autant
#define MAX_GAP_S      5   // trou plus long entre deux scrutations : la console était en veille, pas compté
#define SHELL_READY    "NPXS10079" // démarre avec la LiveArea : avant, un lancement fige le boot (méthode d'AutoBoot)
#define POLL_US        (100 * 1000)
#define URI_MAX        512
#define MAX_APPS       16

static volatile int stopping;
static SceUID thread = -1;
static char playing[32], key[32]; // key : sous quel ID noter le temps de jeu de `playing`
static char alias_app[10], alias_key[16]; // ROM demandée par le launcher : son temps compte pour elle, pas pour RetroArch
static unsigned int played, tick;

// Copie dans app le nom de la première appli en cours hors applis système (NPXS…), "" s'il n'y en a pas.
// Renvoie 1 si le shell est prêt.
static int scan(char app[32]) {
	SceInt32 ids[MAX_APPS];
	int count = sceAppMgrGetRunningAppIdListForShell(ids, MAX_APPS), ready = 0;
	app[0] = 0;
	for (int i = 0; i < count && i < MAX_APPS; i++) {
		SceUID pid = sceAppMgrGetProcessIdByAppIdForShell(ids[i]);
		char name[32];
		name[0] = 0;
		if (pid <= 0 || sceAppMgrGetNameById(pid, name) < 0)
			continue;
		if (sceClibStrcmp(name, SHELL_READY) == 0)
			ready = 1;
		else if (!app[0] && sceClibStrncmp(name, "NPXS", 4) != 0)
			sceClibStrncpy(app, name, 32);
	}
	return ready;
}

// Juste après la fermeture d'une appli, le shell peut accepter un lancement sans l'exécuter (animation de retour
// à l'accueil) : on vérifie qu'une appli tourne bien ensuite, sinon on recommence.
static void launch(const char *uri) {
	char now[32];
	for (int attempt = 0; attempt < 10 && !stopping; attempt++) {
		sceAppMgrLaunchAppByUri(0xFFFFF, uri);
		for (int t = 0; t < 6 && !stopping; t++) {
			sceKernelDelayThread(100 * 1000);
			scan(now);
			if (now[0])
				return;
		}
	}
}

static int alnum(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Appli demandée par le launcher : la lancer d'ici, une fois le launcher fermé, évite la confirmation
// « fermer l'appli en cours ? » qu'affiche le shell quand une appli en lance une autre.
// Le fichier contient « TITLEID[&param=…] », puis sur une 2e ligne facultative l'ID sous lequel noter le temps de jeu.
static int take_request(char uri[URI_MAX]) {
	char buf[URI_MAX - 18]; // place pour le préfixe psgm:play?titleid=
	SceUID fd = sceIoOpen(LAUNCH_REQUEST, SCE_O_RDONLY, 0);
	if (fd < 0)
		return 0;
	int n = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	sceIoRemove(LAUNCH_REQUEST);
	if (n < 9)
		return 0;
	buf[n] = 0;
	char *alias = sceClibStrchr(buf, '\n');
	if (alias)
		*alias++ = 0;
	if (sceClibStrnlen(buf, 10) < 9 || (buf[9] && buf[9] != '&'))
		return 0;
	for (int i = 0; buf[i]; i++)
		if (i < 9 ? !alnum(buf[i]) : (unsigned char)buf[i] < 0x20)
			return 0;
	alias_key[0] = 0;
	if (alias) {
		int i = 0;
		while (i < (int)sizeof(alias_key) - 1 && alnum(alias[i]))
			i++;
		if (alias[i] == 0 || alias[i] == '\n' || alias[i] == '\r') {
			sceClibMemcpy(alias_key, alias, i);
			alias_key[i] = 0;
			sceClibStrncpy(alias_app, buf, 9);
			alias_app[9] = 0;
		}
	}
	sceClibSnprintf(uri, URI_MAX, "psgm:play?titleid=%s", buf);
	return 1;
}

// Le dossier du launcher est verrouillé tant qu'il tourne, et encore un instant après sa fermeture : une mise à jour
// déposée par FTP est mise en place ici, avec quelques essais. L'ancien eboot est remis si le remplacement échoue.
// Le résultat est noté dans UPDATE_LOG, que tools/deploy.sh affiche en cas d'échec.
static void apply_update(void) {
	char magic[4], line[96];
	SceUID fd = sceIoOpen(UPDATE_FILE, SCE_O_RDONLY, 0);
	if (fd < 0)
		return;
	int n = sceIoRead(fd, magic, 4), old = -1, ret = -1, tries = 0;
	sceIoClose(fd);
	if (n == 4 && sceClibMemcmp(magic, "SCE\0", 4) == 0) {
		sceIoRemove(BACKUP_EBOOT);
		for (; tries < 30 && (old = sceIoRename(LAUNCHER_EBOOT, BACKUP_EBOOT)) < 0; tries++)
			sceKernelDelayThread(100 * 1000);
		if (old >= 0) {
			ret = sceIoRename(UPDATE_FILE, LAUNCHER_EBOOT);
			if (ret < 0)
				sceIoRename(BACKUP_EBOOT, LAUNCHER_EBOOT);
			else
				sceIoRemove(BACKUP_EBOOT);
		}
	}
	sceIoRemove(UPDATE_FILE); // pas un SELF, ou échec : on ne garde pas un fichier qui serait réessayé à chaque fois
	n = sceClibSnprintf(line, sizeof(line), "magic %d, ancien eboot 0x%08x apres %d essais, nouveau 0x%08x\n", n, old, tries, ret);
	fd = sceIoOpen(UPDATE_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd >= 0) {
		sceIoWrite(fd, line, n);
		sceIoClose(fd);
	}
}

// Heure locale en secondes : le launcher compte les jours avec la même horloge (« joué hier »).
static unsigned int clock_s(void) {
	SceDateTime dt;
	SceUInt64 t = 0;
	sceRtcGetCurrentClockLocalTime(&dt);
	sceRtcGetTime64_t(&dt, &t);
	return (unsigned int)t;
}

// Ajoute au journal une ligne « ID heure secondes » pour le temps de jeu accumulé (sauf celui du launcher).
static void log_played(unsigned int now) {
	char line[64];
	if (played && sceClibStrcmp(key, LAUNCHER_ID) != 0) {
		int n = sceClibSnprintf(line, sizeof(line), "%s %u %u\n", key, now, played);
		SceUID fd = sceIoOpen(PLAYTIME_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
		if (fd >= 0) {
			sceIoWrite(fd, line, n);
			sceIoClose(fd);
		}
	}
	played = 0;
}

// Appelé à chaque scrutation avec l'appli en cours ("" s'il n'y en a pas).
// shortcut: une appli laissée en arrière-plan (bouton PS) compte comme jouée, à affiner via l'appli au premier plan.
static void track(const char *app) {
	unsigned int now = clock_s();
	if (sceClibStrcmp(app, playing) != 0) {
		log_played(now);
		sceClibStrncpy(playing, app, sizeof(playing));
		// la première appli lancée après une demande est celle demandée (ou aucune si le lancement a échoué)
		sceClibStrncpy(key, alias_key[0] && sceClibStrcmp(app, alias_app) == 0 ? alias_key : app, sizeof(key));
		alias_key[0] = 0;
	} else if (playing[0] && now - tick <= MAX_GAP_S) { // non signé : une horloge reculée n'est pas comptée non plus
		played += now - tick;
		if (played >= CHECKPOINT_S)
			log_played(now);
	}
	tick = now;
}

static int watch(SceSize args, void *argp) {
	char last[32], now[32], uri[URI_MAX];
	last[0] = 0;
	while (!stopping && !scan(now))
		sceKernelDelayThread(100 * 1000);
	if (!stopping && !now[0])
		launch(LAUNCHER_URI);
	while (!stopping) {
		sceKernelDelayThread(POLL_US);
		SceIoStat st;
		if (sceIoGetstat(LAUNCH_REQUEST, &st) >= 0 && take_request(uri)) {
			// le launcher attend sur son animation de lancement : c'est le shell qui le ferme
			sceAppMgrDestroyOtherApp();
			for (int waited = 0; waited < 3000; waited += 50) {
				scan(now);
				if (!now[0])
					break;
				sceKernelDelayThread(50 * 1000);
			}
			if (sceClibStrcmp(uri, LAUNCHER_URI) == 0)
				apply_update();
			launch(uri);
			last[0] = 0;
			continue;
		}
		scan(now);
		track(now);
		if (now[0]) {
			sceClibStrncpy(last, now, sizeof(last));
		} else if (last[0]) {
			if (sceClibStrcmp(last, LAUNCHER_ID) != 0)
				launch(LAUNCHER_URI);
			else if (take_request(uri))
				launch(uri);
			last[0] = 0;
		}
	}
	return sceKernelExitDeleteThread(0);
}

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
	thread = sceKernelCreateThread("launcher_boot", watch, 0x10000100, 0x10000, 0, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	stopping = 1;
	if (thread >= 0)
		sceKernelWaitThreadEnd(thread, NULL, NULL);
	return SCE_KERNEL_STOP_SUCCESS;
}
