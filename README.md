# Vita Launcher

Un launcher pour PS Vita modifiée, inspiré de l'écran d'accueil de la PS5.
*A PS5-style home screen homebrew for the PS Vita.*

![Accueil](docs/home.png)
![Panneau rapide](docs/panel.png)
![Fiche d'appli](docs/sheet.png)

## Fonctionnalités

- **Accueil** : rangée des applis installées. L'icône sélectionnée grossit, le fond prend sa couleur dominante et une version floue de son icône. Les applis épinglées passent en tête, puis les dernières lancées, puis le reste par ordre alphabétique.
- **Jaquettes** : les jeux dont l'icône est illisible (chiffrée, comme pour les jeux du commerce) reçoivent leur jaquette, téléchargée en arrière-plan depuis la base [HexFlow-Covers](https://github.com/Andiweli/HexFlow-Covers) et gardée en cache.
- **Fiche d'appli** (▢) : version et identifiant, lancer, épingler en tête, désinstaller (avec confirmation).
- **Panneau rapide** (△ ou glisser depuis le haut de l'écran) : heure et date, Wi-Fi, batterie et autonomie, volume, raccourcis vers les Paramètres et le Wi-Fi, FTP, veille, redémarrage et extinction (avec confirmation).
- **FTP intégré** (tuile du panneau) : transférer des fichiers sans passer par VitaShell, tant que le launcher est ouvert. L'adresse s'affiche à côté de l'heure. Seules les partitions utiles sont exposées (ux0, ur0, uma0, imc0, grw0, ud0), pas les partitions système. Le réglage est retenu ; le serveur repart tout seul après une mise en pause (bouton PS, veille).
- **Commandes** : croix ou joystick gauche pour naviguer, L/R pour sauter de 5, ✕ pour lancer, aides affichées en bas de l'écran. Au tactile : toucher pour sélectionner, toucher à nouveau pour lancer, glisser pour défiler.
- **Interface principale** (plugin optionnel `launcher_boot.suprx`) : le launcher démarre avec la console et revient tout seul quand on quitte un jeu ou une appli. Si on ferme le launcher lui-même, on reste sur l'accueil Sony.
- Les icônes se chargent progressivement, le démarrage reste rapide même avec beaucoup d'applis. Les titres japonais, chinois ou coréens utilisent la police système de la Vita.

Réglages enregistrés dans `ux0:data/vita-launcher/` (épinglées, récentes, cache des jaquettes).

## État

En développement. Tourne sur une vraie PS Vita (PCH-2000, 3.60 HENkaku) : affichage, icônes, jaquettes, lancement des applis et plugin de démarrage validés. Le volume, l'alimentation, la désinstallation et la police japonaise restent à valider sur le matériel.

## Prérequis

Une PS Vita sous HENkaku/Ensō avec *Enable Unsafe Homebrew* activé dans les paramètres HENkaku.

## Compiler

Avec [VitaSDK](https://vitasdk.org) installé (`VITASDK` défini) et ses paquets `libvita2d`, `freetype` et `curl-mbedtls` (`vdpm install libvita2d freetype curl-mbedtls`) :

```sh
cmake -S . -B build && cmake --build build
```

Le paquet à installer avec VitaShell est `build/launcher.vpk`.

## En faire l'interface principale

Copier `build/launcher_boot.suprx` dans `ur0:tai/`, puis ajouter cette ligne sous `*main` dans `ur0:tai/config.txt` (garder une copie de l'ancien fichier) et redémarrer :

```
*main
ur0:tai/launcher_boot.suprx
```

Le plugin attend que l'accueil soit prêt (même méthode qu'[AutoBoot](https://github.com/Rinnegatamante/AutoBoot)), lance le launcher, puis le relance chaque fois qu'une autre appli se ferme. C'est aussi lui qui lance les applis choisies dans le launcher : il ferme le launcher puis démarre l'appli, ce qui évite la confirmation « fermer l'appli en cours ? » de l'accueil Sony. L'accueil Sony reste visible environ 2 secondes entre les deux : le système ignore les lancements pendant ce temps. En cas de souci au démarrage : éteindre, rallumer en maintenant L jusqu'à l'accueil (les plugins ne se chargent pas), puis retirer la ligne.

## Mettre à jour sur la console

Avec le plugin installé et le FTP du launcher allumé :

```sh
tools/deploy.sh 192.168.1.10:1337
```

Le script compile, dépose le nouvel `eboot.bin` dans `ux0:data/vita-launcher/` et envoie la commande FTP `RELOAD`. Le dossier d'une appli étant verrouillé tant qu'elle tourne, c'est le plugin qui ferme le launcher, met le fichier en place (l'ancien est remis en cas d'échec, résultat dans `update.log`) et le relance. Le script vérifie ensuite que la console a bien la nouvelle version. Les changements d'images ou de polices demandent toujours le VPK.

## Aperçu sur Mac avec Vita3K

```sh
tools/pull-preview.sh 192.168.1.10:1337   # récupère nom et icône des applis de la console (FTP de VitaShell)
tools/preview.sh                          # compile, lance dans Vita3K, capture dans build-preview/shot.png
tools/preview.sh panel                    # pareil, panneau rapide ouvert
tools/preview.sh sheet                    # pareil, fiche de la première appli ouverte
```

Vita3K doit être dans `~/Applications`. Les images de la bulle se régénèrent avec `python3 tools/make_assets.py` (pngquant requis : la Vita exige des PNG 8 bits indexés).

## Tests

```sh
cc test/test_sfo.c src/sfo.c -o build/test_sfo && build/test_sfo
cc test/test_idlist.c src/idlist.c -o build/test_idlist && build/test_idlist
```

## Suite prévue

- Jeux PSP et PS1 (via Adrenaline)
- Boutique : catalogue VitaDB, installation et mises à jour
- Bouton PS qui ramène au launcher plutôt qu'à l'accueil Sony

## Licence

MIT, voir [LICENSE](LICENSE). Police [Inter](https://rsms.me/inter/) sous SIL Open Font License 1.1 (`fonts/OFL.txt`). Certificats racines de Mozilla (`certs/cacert.pem`, MPL 2.0, via [curl](https://curl.se/docs/caextract.html)). Jaquettes de [HexFlow-Covers](https://github.com/Andiweli/HexFlow-Covers), téléchargées à l'usage et non incluses. Serveur FTP [libftpvita](https://github.com/xerpi/libftpvita) de Sergi Granell (MIT, `third_party/ftpvita/LICENSE`, en-tête adapté au VitaSDK récent). Construit avec [VitaSDK](https://vitasdk.org), [vita2d](https://github.com/xerpi/libvita2d) et libcurl.
