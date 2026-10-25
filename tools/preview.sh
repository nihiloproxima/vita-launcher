#!/bin/bash
# Compile, lance le launcher dans Vita3K avec les applis de preview/ux0, puis capture l'écran dans build-preview/shot.png.
# usage : tools/preview.sh [panel|sheet] [secondes avant capture, défaut 5]   (démarre avec le panneau rapide ou la fiche ouverte)
set -e
cd "$(dirname "$0")/.."
export VITASDK=${VITASDK:-$HOME/vitasdk} PATH=$HOME/vitasdk/bin:$PATH
DEFS=""
case "$1" in panel) DEFS="-DPREVIEW_PANEL"; shift ;; sheet) DEFS="-DPREVIEW_SHEET"; shift ;; esac
cmake -S . -B build-preview -DCMAKE_C_FLAGS="$DEFS" >/dev/null && cmake --build build-preview >/dev/null
FS="$HOME/Library/Application Support/Vita3K/Vita3K/fs/ux0"
mkdir -p "$FS" && rsync -a preview/ux0/ "$FS/"
# ne ferme que l'instance lancée par ce script (pas un Vita3K ouvert à la main)
if [ -f build-preview/vita3k.pid ]; then
  OLD=$(cat build-preview/vita3k.pid)
  kill "$OLD" 2>/dev/null || true
  for _ in $(seq 25); do kill -0 "$OLD" 2>/dev/null || break; sleep 0.2; done
fi
"$HOME/Applications/Vita3K.app/Contents/MacOS/Vita3K" --frontend sdl -w build-preview/launcher.vpk > build-preview/vita3k.log 2>&1 &
echo $! > build-preview/vita3k.pid
sleep "${1:-5}"
WID=$(swift tools/winid.swift "$(cat build-preview/vita3k.pid)")
[ -n "$WID" ] && screencapture -x -o -l "$WID" build-preview/shot.png && echo "capture : build-preview/shot.png"
