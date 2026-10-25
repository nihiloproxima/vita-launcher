# Vita Launcher

Un launcher pour PS Vita modifiée, inspiré de l'écran d'accueil de la PS5.
*A PS5-style home screen homebrew for the PS Vita.*

![Accueil](docs/home.png)
![Panneau rapide](docs/panel.png)

## Fonctionnalités

- **Accueil** : rangée des applis installées. L'icône sélectionnée grossit, le fond prend sa couleur dominante et une version floue de son icône.
- **Panneau rapide** (△ ou glisser depuis le haut de l'écran) : heure et date, Wi-Fi, batterie et autonomie, volume, raccourcis vers les Paramètres et le Wi-Fi, veille, redémarrage et extinction (avec confirmation).
- **Commandes** : croix ou joystick gauche pour naviguer, L/R pour sauter de 5, ✕ pour lancer. Au tactile : toucher pour sélectionner, toucher à nouveau pour lancer, glisser pour défiler.

## État

En développement. Testé dans l'émulateur [Vita3K](https://vita3k.org), **pas encore sur une vraie console** : le lancement des applis, le volume et l'alimentation restent à valider sur le matériel.

## Prérequis

Une PS Vita sous HENkaku/Ensō avec *Enable Unsafe Homebrew* activé dans les paramètres HENkaku.

## Compiler

Avec [VitaSDK](https://vitasdk.org) installé (`VITASDK` défini) et les paquets `libvita2d` et `freetype` (`vdpm install libvita2d freetype`) :

```sh
cmake -S . -B build && cmake --build build
```

Le paquet à installer avec VitaShell est `build/launcher.vpk`.

## Aperçu sur Mac avec Vita3K

```sh
tools/pull-preview.sh 192.168.1.10:1337   # récupère nom et icône des applis de la console (FTP de VitaShell)
tools/preview.sh                          # compile, lance dans Vita3K, capture dans build-preview/shot.png
tools/preview.sh panel                    # pareil, panneau rapide ouvert
```

Vita3K doit être dans `~/Applications`. Les images de la bulle se régénèrent avec `python3 tools/make_assets.py` (pngquant requis : la Vita exige des PNG 8 bits indexés).

## Tests

```sh
cc test/test_sfo.c src/sfo.c -o build/test_sfo && build/test_sfo
```

## Suite prévue

- Jeux PSP et PS1 (via Adrenaline), jaquettes téléchargées
- Boutique : catalogue VitaDB, installation et mises à jour
- Plugin pour revenir au launcher en quittant un jeu, lancement au démarrage

## Licence

MIT, voir [LICENSE](LICENSE). Police [Inter](https://rsms.me/inter/) sous SIL Open Font License 1.1 (`fonts/OFL.txt`). Construit avec [VitaSDK](https://vitasdk.org) et [vita2d](https://github.com/xerpi/libvita2d).
