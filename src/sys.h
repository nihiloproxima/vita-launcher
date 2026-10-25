#pragma once

// État de la console, relu toutes les ~2 s par sys_poll().
typedef struct {
	int battery;      // 0..100
	int charging;
	int minutes_left; // autonomie estimée, < 0 si inconnue
	int wifi;         // 1 si connecté
	int wifi_signal;  // 0..100
	char ssid[33];
	char ip[16];
	int volume;       // 0..SYS_VOLUME_MAX, -1 si indisponible
} SysStatus;

#define SYS_VOLUME_MAX 30

extern SysStatus sys;

void sys_init(void);
void sys_poll(void);
void sys_set_volume(int volume);
void sys_suspend(void);
void sys_reboot(void);
void sys_power_off(void);
void sys_open_settings(const char *page); // "" pour l'accueil des Paramètres, "wifi" pour le Wi-Fi
