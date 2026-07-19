# Control — validation LILYGO T5 4.7 Plus V2.4

Premier jalon du contrôleur domotique autonome `Control`. Ce firmware valide
l'écran e-paper, le tactile, le Wi-Fi, la persistance NVS et le parcours UI qui
précède Matter. Il ne contient encore ni tableau de bord domotique, ni découverte
IKEA, ni commissioning Matter.

## Matériel pris en charge

- **LILYGO T5 4.7 Plus V2.4**, PCB daté **2024-12-03**, révision physique
  confirmée ;
- module **ESP32-S3-WROOM-1-N16R8**, flash 16 Mio, PSRAM OPI 8 Mio ;
- dalle ED047 960 × 540 en paysage ;
- contrôleur tactile capacitif GT911 à l'adresse I²C `0x14` ou `0x5D` ;
- USB-C relié à l'USB natif ESP32-S3, D− GPIO19 et D+ GPIO20.

La révision physique V2.4 et sa date de PCB sont désormais l'identité de
compilation, de diagnostic écran et de log.
Les broches de l'ancienne T5 à ESP32 ne sont pas compilées.

## Audit officiel V2.4

Audit réalisé contre le dépôt officiel LILYGO `LilyGo-EPD47`, branche
`esp32s3`, commit `5b1adc7100cce95529ace84278fcdc3aa0439c85` :

- `schematic/Screen-4.7-S3-V2.4 24-12-03.pdf` — schéma exact V2.4 ;
- `src/ed047tc1.h` et `src/ed047tc1.c` — bus écran et séquence 74HCT4094 ;
- `src/utilities.h` — I²C, tactile, batterie, bouton et microSD S3 ;
- `examples/touch/touch.ino` — réveil/probe GT911 et absence de reset logiciel ;
- `boards/esp32s3/lilygo_t5_epaper_s3.json` — N16R8, QIO/OPI et USB natif ;
- `README.md` officiel — tableau GPIO et procédure BOOT/RESET.

Mappage confirmé par le schéma exact :

| Fonction | GPIO / contrôle |
|---|---|
| EPD D0..D7 | 8, 1, 2, 3, 4, 5, 6, 7 |
| EPD CKV / STH / CKH | 38 / 40 / 41 |
| Registre EPD DATA / CLK / STR | 13 / 12 / 0 |
| GT911 SDA / SCL / INT | 18 / 17 / 47 |
| Batterie `BAT_ADC` | 14, pont 100 kΩ / 100 kΩ |
| Bouton utilisateur | 21 |
| USB natif D− / D+ | 19 / 20 |
| BOOT / RESET | GPIO0 vers GND / `CHIP_PU` (`EN`) vers GND |

L'alimentation de la dalle n'est pas un GPIO direct. Le 74HCT4094 expose
QP0..QP7 comme `EP_LE`, `SMPS_CTRL`, `POS_CTRL`, `NEG_CTRL`, `PWR_EN`,
`EP_MODE`, `EP_OE`, `EP_STV`; seule la séquence officielle du pilote est
utilisée. Le reset tactile `T_RST` est présent côté nappe mais non relié à un
GPIO du S3. Aucun GPIO de reset ou d'alimentation tactile n'est donc piloté.
Les liaisons série de `POS_CTRL` et `NEG_CTRL` sont marquées NC sur ce schéma ;
elles restent des sorties sans charge du registre et aucune polarité nouvelle
n'est inventée.

Le fichier central `components/control_board/include/board/board_config.h`
porte toute l'identité, les dimensions, les GPIO et les attentes mémoire. Les
modules écran et tactile ainsi que le pilote vendored l'utilisent directement.

## Pipeline tactile GT911

Le contrôleur physique est confirmé à l'adresse `0x5D`. Le pipeline natif
ESP-IDF utilise les registres GT911 suivants : Product ID `0x8140` (4 octets),
statut/compteur `0x814E`, et premier bloc de point `0x814F` (Track ID, X little
endian, Y little endian, taille). Chaque paquet avec le bit ready est acquitté
en écrivant zéro dans `0x814E`, y compris les compteurs nuls ou invalides.

Le défaut initial avait deux causes combinées : le bloc était lu à `0x8150`
mais décodé comme s'il commençait encore par le Track ID, puis le debounce
exigeait deux paquets `data-ready` consécutifs avant de produire un PRESS. Le
GT911 ne garantissant pas cette succession au rythme de polling, aucun événement
n'atteignait l'UI.

Le polling est désormais autoritaire toutes les 30 ms (33,3 Hz), sans dépendre
de GPIO47. Un paquet valide après IDLE produit PRESS, les changements suivants
produisent MOVE, et `ready + count=0` produit RELEASE. Un délai de sécurité de
90 ms évite un contact bloqué si le paquet de relâchement est perdu. Les
échantillons identiques à ±1 pixel sont dédupliqués.

GPIO47 est une entrée sans pull interne, avec le pull-up externe 4,7 kΩ du PCB,
et une interruption `ANYEDGE` utilisée uniquement comme indication. Le niveau
initial, le premier front observé et le mode IRQ lu dans `0x804D` sont
journalisés. L'absence totale de front n'empêche pas le tactile de fonctionner.

La transformation centralisée est : X natif `0..539`, Y natif `0..959`, swap
XY, inversion Y, aucune inversion X, puis clamp paysage X `0..959`, Y `0..539`.
Elle correspond à `setSwapXY(true)` et `setMirrorXY(false, true)` de l'exemple
LILYGO.

Dans le build actuel, seuls les événements PRESS sont envoyés au hit-test UI.
Un PRESS sur `Configurer le Wi-Fi` passe à `WIFI_SCANNING`, lance le scan
asynchrone et rafraîchit l'écran une seule fois. Sur l'écran de scan, `Retour`
revient à `HARDWARE_TEST`. À la fin du scan, la boucle principale consomme
l'événement Wi-Fi et affiche `WIFI_NETWORK_SELECTION` une seule fois. Une ligne
réseau ouvre l'écran de mot de passe, ou une confirmation explicite pour un
réseau ouvert. MOVE et RELEASE ne déclenchent aucune action UI.

Les logs MOVE détaillés sont contrôlés par `CONFIG_CONTROL_TOUCH_DEBUG`. Les
PRESS, RELEASE et erreurs I²C restent journalisés.

La carte utilise l'USB natif CDC/JTAG de l'ESP32-S3. Windows 10/11 récent doit
utiliser le pilote USB Serial/JTAG intégré ; aucun CP210x/CH340 n'apparaît sur
le schéma V2.4. Si aucun port n'apparaît, utiliser un câble USB-C de données,
maintenir `BOOT` (GPIO0), appuyer sur `RESET`, relâcher `RESET`, puis `BOOT`.
GPIO0 est aussi `STR_IO0` pour l'écran : ne pas manipuler BOOT pendant un
rafraîchissement. Sous WSL, attacher d'abord le périphérique avec `usbipd-win`.

## Versions épinglées

- ESP-IDF **v5.4.2** (`f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`) ;
- pilote e-paper LILYGO EPD47, branche `esp32s3`, commit
  **`5b1adc7100cce95529ace84278fcdc3aa0439c85`** (sources minimales vendored,
  licence GPL-3.0 conservée) ;
- tactile : implémentation GT911 native ESP-IDF du projet, basée sur le mapping
  et la transformation de coordonnées de l'exemple LILYGO de ce même commit ;
- prochain jalon Matter : ESP-Matter **1.5.0**, branche `release/v1.5`, commit
  **`8da2466dcab079e0721b68e3583558d9253dfcd5`**, qui recommande ESP-IDF 5.4.1
  et reste dans la même série 5.4 que ce firmware.

Le pilote LILYGO est compilé hors ligne depuis `components/lilygo_epd47`. Les
adaptations IDF 5.4 sont isolées : RMT moderne, liste zlib minimale, mappage
central V2.4 et retours d'erreur au lieu de `ESP_ERROR_CHECK` pendant
l'initialisation I80/RMT. Un échec écran est ainsi journalisé sans arrêter la
console série.

## Installation reproductible

Sous Linux ou WSL :

```bash
mkdir -p "$HOME/esp"
cd "$HOME/esp"
git clone --branch v5.4.2 --depth 1 --recursive \
  https://github.com/espressif/esp-idf.git esp-idf-v5.4.2
cd esp-idf-v5.4.2
./install.sh esp32s3
. ./export.sh
```

À chaque nouveau terminal :

```bash
. "$HOME/esp/esp-idf-v5.4.2/export.sh"
cd /chemin/vers/control
```

Le dépôt contient toutes les sources du pilote écran nécessaires ; aucun
téléchargement de dépendance non épinglée n'a lieu pendant le build.

## Configurer et compiler

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

Le build validé produit `build/control.bin`. La configuration active utilise
flash QIO 80 MHz, taille flash 16 Mio, PSRAM OPI 80 MHz, console primaire USB
CDC et console secondaire USB Serial/JTAG. La table `single_app_large` réserve
24 Kio à NVS et environ 1,5 Mio à l'application ; elle convient au jalon sans
Matter, mais devra être remplacée avant l'intégration ESP-Matter/OTA.

Avec QIO sélectionné, l'argument esptool généré reste volontairement `dio` :
le bootloader est lu initialement en DIO puis active QIO. C'est le comportement
documenté par `components/esptool_py/Kconfig.projbuild` d'ESP-IDF 5.4.2, et non
une rétrogradation du mode d'exécution.

## Flasher et surveiller

Remplacer `PORT` par `/dev/ttyACM0`, `/dev/ttyUSB0` ou `COM5` selon le système :

```bash
idf.py -p PORT flash
idf.py -p PORT monitor
```

Ou en une commande :

```bash
idf.py -p PORT flash monitor
```

Quitter le moniteur avec `Ctrl+]`.

Sous Windows PowerShell, avec ESP-IDF v5.4.2 installé dans le chemin standard :

```powershell
& "$HOME\esp\esp-idf-v5.4.2\export.ps1"
Set-Location "C:\Users\Kovac\Documents\Arduino\control"
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Remplacer uniquement `COM5` par le port affiché dans le Gestionnaire de
périphériques. Le flash n'a pas été exécuté pendant cet audit faute de carte
accessible depuis l'environnement de compilation.

Effacer toute la flash, y compris NVS :

```bash
idf.py -p PORT erase-flash
```

Effacer uniquement la partition NVS de ce partitionnement (`0x9000`, 24 Kio) :

```bash
python -m esptool --chip esp32s3 -p PORT erase_region 0x9000 0x6000
```

## Séquence attendue

1. La console affiche la version, la carte et le résultat de chaque
   initialisation sous les tags `CONTROL_*`.
2. L'écran diagnostic affiche `Control`, la carte, l'état écran/tactile/Wi-Fi,
   le firmware et le bouton `Configurer le Wi-Fi`.
3. Le firmware écrit `CONTROL_DISPLAY: first_render_done`, puis
   `CONTROL_APP: idle_after_first_render`.
4. Un toucher hors du bouton ne produit que les logs de hit-test.
5. Un PRESS sur `Configurer le Wi-Fi` affiche l'écran `Recherche Wi-Fi` et
   démarre le scan Wi-Fi asynchrone.
6. `Retour` depuis l'écran de scan revient au diagnostic, et un scan terminé
   affiche la liste des réseaux dédupliqués.
7. La sélection d'un réseau sécurisé affiche un clavier tactile. Le mot de
   passe est masqué par défaut et n'est jamais journalisé.
8. Après connexion, l'écran `Wi-Fi connecté` affiche SSID, IPv4, RSSI et MAC,
   puis propose `Rechercher IKEA`.

Les mots de passe ne sont jamais écrits dans les logs. Ils sont masqués à
l'écran par défaut, avec un contrôle explicite Afficher/Masquer.

## Architecture

- `main/main.cpp` : initialisation, premier rendu, tâche de polling tactile et dispatch PRESS minimal ;
- `components/control_board` : identité et mappage V2.4 centralisés ;
- `main/display` : framebuffer et rafraîchissement e-paper ;
- `main/touch` : GT911 natif, transformation paysage, debounce, press/release ;
- `main/ui` : rendu et hit-testing rectangulaire ;
- `main/wifi` : scan/connexion événementiels, tri, déduplication et diagnostics ;
- `main/ikea` : découverte locale conservative après connexion Wi-Fi ;
- `main/storage` : NVS réutilisable et récupération après NVS incompatible ;
- `main/matter` : interface compilable du prochain jalon, sans fausse découverte.

## Protection de pile et rendu e-paper

Le build de récupération ne crée plus de tâche UI dédiée. `app_main()` garde la
propriété des objets d'application, rend le menu Control une seule fois, puis
reste vivant dans une boucle idle qui consomme uniquement les événements tactiles
pour les logs. Le framebuffer 960x540 (259 200 octets) reste alloué directement
en PSRAM, sans zone de garde ni décalage de pointeur.

Configuration de développement :

```text
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384         # octets
CONFIG_CONTROL_TOUCH_UI_DISPATCH=y
CONFIG_CONTROL_TOUCH_TASK_STACK_SIZE=4096     # octets
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
```

Les valeurs sont des octets. Le stack main est augmenté parce que le rendu et le
rafraîchissement synchrones sont revenus dans `app_main()` afin de restaurer
l'architecture la plus proche du premier menu Control fonctionnel.

## Recovery display build

Le safe-display expérimental, les écrans checkerboard, les gardes framebuffer,
la machine d'états écran, les compteurs de refresh et les forçages GPIO ont été
retirés du chemin de récupération. Le firmware courant rend le menu Control
normal avec la séquence historique :

```text
epd_poweron()
epd_clear()
epd_draw_grayscale_image(epd_full_screen(), framebuffer)
epd_poweroff()
```

Le tactile GT911 est toujours initialisé et sondé à 30 ms. Les actions UI
actives restent PRESS-only : configuration Wi-Fi, Retour/Rafraîchir sur les
écrans Wi-Fi, sélection de ligne réseau, clavier, connexion et recherche IKEA.
Le framebuffer unique de 259200 octets est alloué directement en PSRAM.

Le code officiel audité est le commit LILYGO
`5b1adc7100cce95529ace84278fcdc3aa0439c85`, notamment `ed047tc1.c`,
`i2s_data_bus.c`, `rmt_pulse.c`, `epd_driver.c` et `examples/button/button.ino`.
Les différences locales sont limitées aux adaptations ESP-IDF 5 nécessaires
(retours `esp_err_t`, I80/RMT driver-ng). Aucune suppression manuelle de bus,
aucune réécriture GPIO et aucune modification de LUT, tension, timing ou
configuration 960x540 n'est ajoutée au chemin de récupération.

La machine d'états explicite contient `BOOT`, `HARDWARE_TEST`,
`WIFI_SCANNING`, `WIFI_NETWORK_SELECTION`, `WIFI_PASSWORD_ENTRY`,
`WIFI_CONNECTING`, `WIFI_CONNECTED`, `WIFI_CONNECTION_FAILED`,
`IKEA_DISCOVERY`, `IKEA_FOUND`, `IKEA_NOT_FOUND`, `IKEA_DISCOVERY_FAILED`,
`MATTER_INTRO` et `MATTER_NOT_IMPLEMENTED`.

## Wi-Fi, NVS et découverte IKEA

La connexion Wi-Fi est asynchrone : les callbacks ESP-IDF ne rafraîchissent pas
l'écran. Ils signalent seulement un événement consommé par la boucle principale.
Les identifiants sont enregistrés dans NVS uniquement après obtention de l'IP.
Au démarrage suivant, le firmware planifie une tentative d'auto-connexion avec
le SSID stocké après le premier rendu du menu Control.

Le clavier tactile utilise des rafraîchissements plein écran pour les touches
visibles, car le rafraîchissement partiel n'est pas encore validé. C'est lent
mais volontairement sûr pour cette étape. Les modes disponibles sont minuscules,
majuscules/chiffres et symboles courants (`! @ # $ % ^ & * - _ + = . , ? / :
; ( ) [ ] { }`). Le bouton `Retour` supprime un caractère, `Effacer` vide le
champ, `Afficher/Masquer` contrôle l'affichage du secret.

La découverte IKEA ne fait pas de commissioning Matter. Elle démarre seulement
après connexion IP et ne simule aucun résultat. Dans ce build hors ligne, aucun
composant ESP-IDF `mdns` n'est vendored ; le module journalise donc les services
DNS-SD visés comme non disponibles puis tente de résoudre les hôtes locaux
suivants via LwIP : `dirigera.local`, `hubv3.local`, `ikea-dirigera.local`.

Services DNS-SD prévus pour le prochain ajout du composant mDNS épinglé :
`_matter._tcp`, `_matterc._udp`, `_matterd._udp`, `_hap._tcp` et `_http._tcp`.
`_hap._tcp` et `_http._tcp` restent des diagnostics et ne prouvent pas IKEA.

Classification :

- `IKEA_FOUND_CONFIDENT` : un hôte/service/TXT résolu contient clairement
  `ikea`, `dirigera` ou `hubv3` ;
- `MATTER_BRIDGE_FOUND_UNCONFIRMED` : réservé au futur DNS-SD Matter sans
  identité IKEA ;
- `LOCAL_MDNS_FOUND_UNCONFIRMED` : résolution locale sans identité IKEA claire ;
- `NOT_FOUND` : aucun hôte/service probant ;
- `DISCOVERY_ERROR` : tâche impossible à créer ou découverte annulée.

## Limites connues

- aucune carte physique n'a été accessible pendant la compilation : flash,
  écran, orientation tactile et qualité des rafraîchissements restent à valider ;
- le pilote ne peut pas détecter électriquement une dalle absente après une
  initialisation I80/RMT réussie. Les erreurs de périphérique et de mémoire
  sont récupérables, mais « écran OK » signifie pilote initialisé, pas dalle
  physiquement observée ;
- `T_RST` et l'alimentation tactile n'ont pas de GPIO S3 confirmé et restent
  volontairement inutilisés ;
- la lecture batterie est documentée sur GPIO14 mais non activée. ADC2 doit
  être validé avec le Wi-Fi actif avant tout futur module batterie ;
- les changements d'écran et actions de clavier utilisent encore un plein
  rafraîchissement. L'abstraction est prête pour une politique de zones
  partielles, mais celle-ci doit être caractérisée sur la dalle réelle pour
  limiter ghosting et usure. `CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH` reste
  désactivé ; `commitRegion()` journalise et ignore la demande pour l'instant ;
- les réseaux masqués sont indiqués clairement mais la saisie manuelle de leur
  SSID est reportée ;
- les identifiants NVS ne sont pas encore chiffrés par flash encryption ;
- ESP-Matter n'est pas lié à ce binaire et DIRIGERA n'est pas commissionné ;
- le composant mDNS/DNS-SD ESP-IDF n'est pas encore vendored dans ce build, donc
  la recherche IKEA actuelle se limite aux résolutions de noms locaux ;
- pas d'OTA, de DNS-SD Matter complet, de BLE commissioning, de QR scanner ni de
  tableau de bord de contrôle dans ce jalon.

## Prochain jalon recommandé

Intégrer le `basic_commissioner`/composant `esp_matter_controller` du commit
ESP-Matter épinglé, activer le commissioner sur ESP32-S3, puis ajouter : stockage
de fabric/node dans la partition Matter, saisie du code manuel et du payload QR,
commissioning BLE-Wi-Fi et on-network, découverte DNS-SD des nœuds commissionnés,
et commissioning réel du Matter Bridge DIRIGERA après ouverture de sa fenêtre
depuis IKEA Home smart. Aucun API IKEA propriétaire n'est nécessaire.
