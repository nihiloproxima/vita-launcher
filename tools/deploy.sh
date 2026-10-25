#!/bin/bash
# Met à jour le launcher sur la console par son propre FTP (tuile FTP du panneau △) : le plugin de démarrage
# le ferme, met le nouvel eboot en place et le relance. Ne remplace que eboot.bin : un changement d'images ou de polices demande le VPK.
# usage : tools/deploy.sh <ip:port>      ex. tools/deploy.sh 192.168.1.10:1337
set -e
cd "$(dirname "$0")/.."
F="ftp://${1:?usage: tools/deploy.sh <ip:port>}"
cmake --build build
# le dossier de l'appli est verrouillé tant qu'elle tourne : le plugin met le fichier en place après l'avoir fermée
curl -sS --max-time 60 -T build/launcher.self "$F/ux0:/data/vita-launcher/eboot.part"
# RELOAD en dernier (« -» = après le listage que curl fait toujours) : le launcher se ferme juste après sa réponse
curl -sS --max-time 10 -Q "RNFR ux0:/data/vita-launcher/eboot.part" -Q "RNTO ux0:/data/vita-launcher/eboot.new" -Q -RELOAD "$F/" -o /dev/null
# le launcher redémarre : on attend le retour de son FTP, puis on vérifie que le plugin a bien mis le fichier en place
for i in $(seq 1 30); do sleep 1; curl -s --max-time 2 "$F/" >/dev/null && break; done
if curl -s --max-time 30 "$F/ux0:/app/LNCH00001/eboot.bin" | cmp -s - build/launcher.self; then
  echo "launcher mis à jour et relancé"
else
  echo "la mise à jour n'a pas été mise en place :" >&2
  curl -s --max-time 10 "$F/ux0:/data/vita-launcher/update.log" >&2
  exit 1
fi
