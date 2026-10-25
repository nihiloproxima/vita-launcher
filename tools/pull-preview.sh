#!/bin/bash
# Copie le nom et l'icône des applis de la console (FTP de VitaShell) dans preview/ux0, pour l'aperçu Vita3K.
# usage : tools/pull-preview.sh <ip:port>      ex. tools/pull-preview.sh 192.168.1.10:1337
set -e
cd "$(dirname "$0")/.."
F="ftp://${1:?usage: tools/pull-preview.sh <ip:port>}"
for id in $(curl -s --max-time 15 "$F/ux0:/app/" | awk '$1 ~ /^d/ {print $NF}'); do
  mkdir -p "preview/ux0/app/$id/sce_sys"
  for f in param.sfo icon0.png; do
    curl -sf --max-time 30 -o "preview/ux0/app/$id/sce_sys/$f" "$F/ux0:/app/$id/sce_sys/$f" || rm -f "preview/ux0/app/$id/sce_sys/$f"
  done
  echo "$id"
done
