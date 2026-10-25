#include "ftp.h"

#include "apps.h"

#include <ftpvita.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <stdio.h>

#define FTP_FLAG     "ux0:data/vita-launcher/ftp_on" // présent = FTP allumé
#define RETRY_FRAMES 120
#define PAUSE_US     1500000 // au-delà de cet écart entre deux images, le launcher a été mis en pause

// Partitions utiles seulement : les partitions système (os0, vs0, sa0…) restent hors d'atteinte du réseau.
static const char *devices[] = {"ux0:", "ur0:", "uma0:", "imc0:", "grw0:", "ud0:"};
static int enabled, running, wait_frames;
static char address[24];

// RELOAD : fait fermer et relancer le launcher par le plugin, qui met en place la mise à jour déposée par
// tools/deploy.sh (ux0:data/vita-launcher/eboot.new).
static void cmd_reload(ftpvita_client_info_t *client) {
	if (!apps_boot_plugin()) {
		ftpvita_ext_client_send_ctrl_msg(client, "550 Boot plugin not installed" FTPVITA_EOL);
		return;
	}
	// réponse d'abord : le plugin ferme le launcher, et cette connexion avec, dès qu'il voit la demande
	ftpvita_ext_client_send_ctrl_msg(client, "200 Reloading" FTPVITA_EOL);
	apps_reload();
}

static void start(void) {
	char ip[16];
	unsigned short port;
	if (ftpvita_init(ip, &port) < 0)
		return;
	for (unsigned i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
		ftpvita_add_device(devices[i]);
	ftpvita_ext_add_custom_command("RELOAD", cmd_reload);
	snprintf(address, sizeof(address), "%s:%u", ip, port);
	running = 1;
}

void ftp_init(void) {
	SceIoStat st;
	enabled = sceIoGetstat(FTP_FLAG, &st) >= 0;
}

void ftp_poll(int wifi) {
	// Une mise en pause (bouton PS, veille) coupe les sockets de l'appli : le serveur s'arrête sans le dire.
	// On le relance au retour.
	static SceUInt64 last;
	SceRtcTick tick; // horloge temps réel : elle avance aussi pendant la veille
	sceRtcGetCurrentTick(&tick);
	SceUInt64 now = tick.tick;
	if (running && last && now - last > PAUSE_US) {
		ftpvita_fini();
		running = 0;
		wait_frames = 0;
	}
	last = now;
	if (enabled && !running && wifi && --wait_frames <= 0) {
		wait_frames = RETRY_FRAMES;
		start();
	}
}

void ftp_toggle(void) {
	enabled = !enabled;
	if (enabled) {
		SceUID fd = sceIoOpen(FTP_FLAG, SCE_O_WRONLY | SCE_O_CREAT, 0666);
		if (fd >= 0)
			sceIoClose(fd);
		wait_frames = 0;
	} else {
		sceIoRemove(FTP_FLAG);
		if (running)
			ftpvita_fini();
		running = 0;
	}
}

int ftp_enabled(void) {
	return enabled;
}

int ftp_running(void) {
	return running;
}

const char *ftp_address(void) {
	return address;
}
