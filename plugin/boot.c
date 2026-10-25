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

#define LAUNCHER_ID    "LNCH00001"
#define LAUNCHER_URI   "psgm:play?titleid=" LAUNCHER_ID
#define LAUNCH_REQUEST "ux0:data/vita-launcher/launch.txt"
#define SHELL_READY    "NPXS10079" // démarre avec la LiveArea : avant, un lancement fige le boot (méthode d'AutoBoot)
#define POLL_US        (100 * 1000)
#define MAX_APPS       16

static volatile int stopping;
static SceUID thread = -1;

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

// Appli demandée par le launcher : la lancer d'ici, une fois le launcher fermé, évite la confirmation
// « fermer l'appli en cours ? » qu'affiche le shell quand une appli en lance une autre.
static int take_request(char uri[64]) {
	char id[10];
	SceUID fd = sceIoOpen(LAUNCH_REQUEST, SCE_O_RDONLY, 0);
	if (fd < 0)
		return 0;
	int n = sceIoRead(fd, id, 9);
	sceIoClose(fd);
	sceIoRemove(LAUNCH_REQUEST);
	if (n != 9)
		return 0;
	for (int i = 0; i < 9; i++)
		if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9')))
			return 0;
	id[9] = 0;
	sceClibSnprintf(uri, 64, "psgm:play?titleid=%s", id);
	return 1;
}

static int watch(SceSize args, void *argp) {
	char last[32], now[32], uri[64];
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
			launch(uri);
			last[0] = 0;
			continue;
		}
		scan(now);
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
