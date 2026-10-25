#pragma once

// Serveur FTP (libftpvita) : tourne tant que le launcher est ouvert, si l'utilisateur l'a allumé
// depuis le panneau rapide. Le choix est retenu entre deux lancements.
void ftp_init(void);
void ftp_poll(int wifi); // à chaque image : démarre le serveur dès que le Wi-Fi est là
void ftp_toggle(void);
int ftp_enabled(void);
int ftp_running(void);
const char *ftp_address(void); // « ip:port » quand le serveur tourne
