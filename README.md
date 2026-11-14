# Game hub : shoot'em up sur dalles LED HUB75 (ESP32)

Le shoot'em up du mode Jeu du VU-mètre, jouable de trois façons :
- depuis une **page web** (téléphone, tablette, PC) ;
- avec une **manette ESP32**, un 2ᵉ ESP32 avec des boutons qui parle directement au jeu par radio ESP-NOW. Il n'y a besoin ni de box ni de page web ;
- avec une **manette PS4** reliée en Bluetooth directement à l'ESP32 des dalles. C'est la version `Games_PS4`, sans wifi, encore **expérimentale**.

![Le jeu sur les 3 configurations de dalles](docs/rendu.png)

*Aperçu simulé (généré par `docs/rendu.py`, mêmes sprites que le jeu). Chaque configuration en grand : [128x128](docs/rendu-128x128.png), [256x64](docs/rendu-256x64.png), [128x256](docs/rendu-128x256.png).*

## Écrans

Le menu **Réglages → Dalles** de la page web règle le montage. Le réglage est gardé en mémoire, et l'ESP32 redémarre quand on clique sur Appliquer.

- **Nombre :** 1 à 4 dalles 128x64.
- **Disposition :** l'une sous l'autre, côte à côte, ou carré 2x2 (4 dalles). La page affiche la résolution obtenue.
- **Écran retourné (180°) :** la 1ʳᵉ dalle, celle reliée à l'ESP32, est en bas ou à droite, ou bien tout l'ensemble est monté à l'envers.
- **Une dalle sur deux retournée (serpentin) :** les dalles 2 et 4 sont tournées de 180° pour garder des nappes courtes.

| Écran | Dalles | Montage (ordre de la chaîne) |
|---|---|---|
| 128x128 | 2 | l'une sous l'autre, la 1ʳᵉ en haut |
| 256x64 | 2 | côte à côte, la 1ʳᵉ à gauche |
| 128x256 | 4 | l'une sous l'autre, écran vertical façon borne d'arcade, avec 7 rangées d'ennemis |
| 256x128 | 4 | carré 2x2 : haut gauche → haut droite → bas gauche → bas droite |
| 128x64, 128x192, 384x64, 512x64 | 1, 3 ou 4 | autres combinaisons possibles |

Brochage identique à l'horloge ClockV1 et au VU-mètre.

![Branchement des dalles](docs/branchement-dalles.png)

La profondeur de couleur est choisie au démarrage : la plus haute qui laisse assez de mémoire au wifi, entre 6 et 3 bits. Avec 4 dalles, on aura probablement 3 bits. Si même 3 bits ne passe pas, l'ESP32 revient tout seul à 2 dalles.

## Jouer

1. **Wifi :** au démarrage, l'ESP32 essaie dans l'ordre :
   1. les réseaux de `games/secrets.h`, un fichier local jamais publié (il est dans .gitignore), même format que celui de l'horloge ;
   2. le dernier réseau réglé ;
   3. sinon, il crée le réseau **Games-Setup** pendant 3 min. Connecte-toi dessus et choisis ta box sur la page qui s'ouvre (sinon va sur 192.168.4.1). Pendant ce temps, le jeu et la manette ESP32 marchent déjà, et la dalle affiche `WIFI:Games-Setup`.
2. Ouvre **http://games.local**, ou l'adresse IP affichée sur la dalle pendant la démo.
3. Appuie sur **FEU** pour lancer une partie.

Commandes :
- **Tactile :** ◀ ▶ et **FEU**. Garder FEU appuyé tire en continu.
- **Clavier :** ← → ou Q / D pour bouger, espace pour tirer.
- **Manette :** une manette branchée au PC ou au téléphone est reconnue par le navigateur.

**Stages :** quand tous les ennemis sont détruits, l'annonce « STAGE n » s'affiche avec un bonus de 500 points × le numéro du stage. La vague suivante arrive alors, plus agressive. À chaque stage, ton vaisseau évolue :

![Évolution du vaisseau](docs/rendu-vaisseaux.png)

| Stage | Vaisseau |
|---|---|
| 1 | de base, un tir |
| 2 | **tir rapide** (ailes vertes) |
| 3 | **double canon**, deux tirs parallèles |
| 4 et + | **triple canon**, tirs en éventail |

Sans joueur, le jeu tourne tout seul en démo. Une partie abandonnée repasse en démo après 1 min sans appui. Le record (des joueurs seulement) est gardé en mémoire.

Réglages de la page : luminosité et dalles.

**Bouton BOOT appuyé 5 s :** oublie le wifi et redémarre sur Games-Setup.

## Manette ESP32 (sans wifi ni box)

Sketch : [manette/manette.ino](manette/manette.ino). Il marche sur n'importe quel ESP32 classique (DevKit), avec des boutons vers la masse :

| Bouton | GPIO |
|---|---|
| Gauche | 32 |
| Droite | 33 |
| Feu | 25 |
| Feu (2ᵉ bouton, ou bouton du joystick) | 26 |
| LED de la carte | 2 : clignote = recherche du jeu, allumée = reliée |

![Branchement de la manette](docs/branchement-manette.png)

En option, un joystick analogique (module KY-023) : axe X sur GPIO 34, alimenté en 3,3 V. Mets `#define JOYSTICK 1` dans manette.ino.

**Fonctionnement (ESP-NOW, la radio directe d'Espressif entre ESP32) :**
- Le jeu émet une balise 2 fois par seconde sur son canal wifi : celui de la box s'il y est connecté, sinon celui du réseau Games-Setup.
- La manette écoute les canaux 1 à 13 jusqu'à trouver cette balise. Elle envoie ensuite l'état de ses boutons à chaque appui et toutes les 100 ms, avec environ 1 ms de délai.
- Si le jeu ne répond plus pendant 3 s (jeu éteint, ou canal changé parce que la box a été réglée), la manette recommence sa recherche toute seule.
- Si la manette se tait plus de 0,5 s, le jeu relâche ses touches : le vaisseau ne reste pas bloqué.
- Plusieurs manettes et la page web peuvent être utilisées en même temps : elles pilotent le même vaisseau.

Pourquoi pas le Bluetooth : sa pile prend 50 à 100 Ko de RAM, ce qui ne tient pas avec la mémoire des dalles. ESP-NOW passe par la radio wifi déjà active et ne coûte presque rien.

## Manette PS4 en Bluetooth (expérimental)

Version à part du jeu : `Games_PS4_V<version>.bin`. Elle sort du même code, compilé avec `-DGAMES_PS4`. Cette version n'a pas de wifi, de page web ni de manette ESP-NOW : le Bluetooth et les dalles se partagent la mémoire.

> Pas encore testé sur le matériel. Au démarrage, le moniteur série (115200 bauds) affiche la mémoire libre après les dalles et après le Bluetooth (`Manette PS4 : Bluetooth pret ...`). Avec 2 dalles, ça devrait passer. Avec 4, ce n'est pas sûr.

![Connexion de la manette PS4](docs/manette-ps4.gif)

**Connexion, une seule fois.** Une manette PS4 ne se connecte qu'à l'adresse Bluetooth de « sa » console. L'ESP32 prend donc cette adresse :

1. Branche la manette en USB sur le PC et lance **SixaxisPairTool**. Il affiche l'adresse de la console que la manette connaît (« Current Master »).
2. Ouvre le moniteur série de l'ESP32 (115200 bauds) et tape `a` suivi de cette adresse, par exemple `a 1a:2b:3c:4d:5e:6f`, puis Entrée. L'ESP32 enregistre l'adresse et redémarre.
3. Appuie sur le bouton **PS** de la manette. Elle se connecte et sa barre lumineuse s'allume.

L'adresse reste en mémoire et la manette marche toujours sur la console. Autre méthode : écris dans la manette, avec SixaxisPairTool, l'adresse affichée par l'ESP32 sur la dalle. Mais il faudra alors réappairer la manette sur la console. La commande `a` seule, sans adresse, rend à l'ESP32 son adresse d'origine.

| Manette | Action |
|---|---|
| stick gauche ou croix gauche / droite | bouger |
| croix, rond, carré, triangle, R1, L1 ou R2 | tirer (et lancer une partie) |
| croix haut / bas | luminosité (enregistrée) |
| Share + Options pendant 3 s | redémarrer l'ESP32 |

- **Vibration :** la manette vibre quand ton vaisseau est touché.
- **Barre lumineuse :** bleu sombre en démo, puis la couleur de l'évolution du vaisseau.

**Moniteur série :**
- `?` affiche l'aide.
- `c NDFS` règle les dalles :
  - N : nombre de dalles, de 1 à 4 ;
  - D : disposition, 0 l'une sous l'autre, 1 côte à côte, 2 carré ;
  - F : écran retourné, 0 ou 1 ;
  - S : serpentin, 0 ou 1.

  Exemple : `c 2000`. Ce réglage est commun avec la version wifi.

## Firmware

Binaires prêts à flasher sur un ESP32 classique, à l'adresse 0x10000 :

| Fichier | Rôle | MD5 |
|---|---|---|
| [firmware/Games_V1.03.bin](firmware/Games_V1.03.bin) | le jeu (wifi, page web, manette ESP-NOW), pour l'ESP32 des dalles | `1a1638eaf125761aca8b3de7a4099cd5` |
| [firmware/Games_PS4_V1.03.bin](firmware/Games_PS4_V1.03.bin) | le jeu, version manette PS4 Bluetooth sans wifi (expérimental) | `820fd2c1c0ac652fa528103688cc507a` |
| [firmware/Manette_V1.00.bin](firmware/Manette_V1.00.bin) | la manette ESP-NOW | `598ccc714d7c450ee49a337ae10b483b` |

Le jeu publié ne contient aucun réseau wifi. Au premier démarrage, règle le wifi avec le réseau **Games-Setup**. Pour intégrer tes propres réseaux, crée `games/secrets.h` et recompile.

## Compilation

```powershell
./build.ps1           # le jeu et la manette -> Games_V<version>.bin et Manette_V<version>.bin
./build.ps1 games     # le jeu seulement
./build.ps1 games -Ps4  # le jeu en version manette PS4 -> Games_PS4_V<version>.bin
```

Avec arduino-cli, pour un ESP32 classique sur le core 2.0.17.

Plans, aperçus et GIF : `python docs/branchement.py`, `python docs/rendu.py`, `python docs/gif_manette_ps4.py` (Pillow).

Le script utilise les bibliothèques de l'horloge : ESP32-HUB75-MatrixPanel-I2S-DMA, Adafruit GFX, ESPAsyncWebServer, AsyncTCP et WiFiManager. La version PS4 utilise en plus PS4Controller 1.1.0.

> Pas encore testé sur le matériel.
