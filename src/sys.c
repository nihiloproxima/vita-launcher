#include "sys.h"

#include <psp2/appmgr.h>
#include <psp2/avconfig.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_MEMORY (1024 * 1024)

SysStatus sys;
static int net_ready, frame;

static void refresh(void) {
	sys.battery = scePowerGetBatteryLifePercent();
	sys.charging = scePowerIsBatteryCharging();
	// valeurs hors plage rejetées : certaines fonctions renvoient n'importe quoi (ex. dans Vita3K)
	int minutes = scePowerGetBatteryLifeTime();
	sys.minutes_left = !sys.charging && minutes > 0 && minutes < 24 * 60 ? minutes : -1;

	int volume = -1;
	sys.volume = sceAVConfigGetSystemVol(&volume) >= 0 && volume >= 0 && volume <= SYS_VOLUME_MAX ? volume : -1;

	int state = 0;
	sys.wifi = net_ready && sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
	if (sys.wifi) {
		SceNetCtlInfo info;
		if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &info) >= 0)
			snprintf(sys.ssid, sizeof(sys.ssid), "%s", info.ssid);
		if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) >= 0)
			sys.wifi_signal = info.rssi_percentage;
		if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0)
			snprintf(sys.ip, sizeof(sys.ip), "%s", info.ip_address);
	}
}

void sys_init(void) {
	if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) >= 0) {
		SceNetInitParam param = {malloc(NET_MEMORY), NET_MEMORY, 0};
		sceNetInit(&param); // peut déjà être initialisé : seul sceNetCtlInit compte
		net_ready = sceNetCtlInit() >= 0;
	}
	refresh();
}

void sys_poll(void) {
	if (++frame % 120 == 0)
		refresh();
}

void sys_set_volume(int volume) {
	if (sys.volume < 0)
		return;
	volume = volume < 0 ? 0 : volume > SYS_VOLUME_MAX ? SYS_VOLUME_MAX : volume;
	if (sceAVConfigSetSystemVol(volume) >= 0)
		sys.volume = volume;
}

void sys_suspend(void) {
	scePowerRequestSuspend();
}

void sys_reboot(void) {
	scePowerRequestColdReset();
}

void sys_power_off(void) {
	scePowerRequestStandby();
}

// Les applis système ne se lancent pas par title ID, seulement par URI (cf. VitaXMB) ;
// le drapeau qui convient n'est pas documenté, d'où les deux essais.
void sys_open_settings(const char *page) {
	char uri[64];
	snprintf(uri, sizeof(uri), "settings_dlg:%s", page);
	if (sceAppMgrLaunchAppByUri(0x20000, uri) < 0)
		sceAppMgrLaunchAppByUri(0xFFFFF, uri);
}
