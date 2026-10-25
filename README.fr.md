# Dancing Every Night

*[English version](README.md)*

Un jeu de rythme communautaire pour **PS Vita**, écrit en C11 + SDL2. Les notes convergent vers deux
arcs de cibles ; il faut appuyer sur le bon bouton en rythme, garder son combo et viser le rang S.

L'objectif : une communauté à la osu!. Le jeu est livré avec une ou deux chansons originales, et les
joueurs créent, partagent et téléchargent les suivantes.

> **État : 0.9.0, première version publique.** Le jeu est jouable, avec 3 chansons officielles, et les chansons de la
> communauté s'installent. Un store de chansons dans le jeu est prévu pour la 1.0.

**Communauté :** [Discord](https://discord.gg/vTJDvgJe74) · [Bibliothèque de chansons](https://dancing-every-night.pages.dev/songs/) · [Éditeur en ligne](https://dancing-every-night.pages.dev/editor/)

## Pour les joueurs

Il faut une PS Vita avec un firmware modifié (suivre https://vita.hacks.guide) et **VitaShell**.

1. Télécharger `DancingEveryNight-<version>.vpk` depuis la page Releases.
2. Le copier sur la Vita (mode USB ou FTP de VitaShell), l'ouvrir dans VitaShell et appuyer sur Croix pour l'installer.
3. Lancer *Dancing Every Night* depuis l'écran d'accueil.

| Vita | Action |
|---|---|
| Triangle, Rond, Croix / Haut, Gauche, Bas | Valider les notes de la cible correspondante |
| L / R | Notes scratch |
| Start | Pause (L/R en pause : calibration audio ±10 ms) |
| Select + Start | Quitter |

Les sauvegardes sont dans `ux0:data/dancingeverynight/`.

**Mises à jour :** quand la Vita est en ligne, le jeu lit au démarrage un petit fichier (`latest.json`, sur le site du projet) et, s'il existe une version plus récente, l'annonce dans le menu principal. Rien n'est téléchargé ni installé, et rien sur toi n'est envoyé en dehors de la requête elle-même. Désactivable dans OPTIONS > UPDATE CHECK.

### Installer une chanson

1. Copier le `.zip` de la chanson dans `ux0:data/dancingeverynight/import/` (mode USB ou FTP de VitaShell).
   Le dossier est créé au premier lancement du jeu.
2. Lancer le jeu : le zip est décompressé pendant l'écran de chargement, puis supprimé.
3. La chanson apparaît dans la liste (et dans son pack, si elle en a un).

Réinstaller une chanson de même ID la remplace et garde tes records. Pour supprimer une chanson, la sélectionner dans
la liste des chansons et appuyer sur **Triangle** ; on peut aussi supprimer son dossier dans
`ux0:data/dancingeverynight/songs/`. Un zip impossible à installer est déplacé dans `import/failed/`, avec un
fichier `.txt` qui explique pourquoi.

**Bugs et idées :** ouvrir une issue sur ce dépôt (des formulaires existent pour un bug, une idée ou signaler une
chanson) en indiquant la version affichée en bas à droite de l'écran titre. Voir [CONTRIBUTING](CONTRIBUTING.md) pour
les autres façons d'aider. Pour un crash, joindre le fichier `.psp2dmp` présent dans `ux0:data/`.

### Sur ordinateur

La page Releases propose aussi des versions PC, sans rien d'autre à installer :

- **Windows** : décompresser `DancingEveryNight-<version>-windows.zip` et lancer `DancingEveryNight.exe`. Le jeu n'est
  pas signé : si SmartScreen avertit, choisir *Informations complémentaires → Exécuter quand même*.
- **macOS** (11 ou plus, Intel ou Apple Silicon) : décompresser, ranger *Dancing Every Night* dans Applications, puis
  la première fois clic droit → *Ouvrir* (l'app n'est pas notariée).
- **Linux** : extraire `DancingEveryNight-<version>-linux.tar.gz` et lancer `DancingEveryNight`.

Clavier : flèches / WASD, Espace ou J (Croix), K (Rond), U (Carré), I (Triangle), Q / E (scratch), Entrée (Start),
Retour arrière (Select) ; une manette marche aussi. Les chansons s'installent de la même façon, par un dossier
`import/` : dans le dossier `save/` à côté du jeu sous Windows et Linux, dans
`~/Library/Application Support/Dancing Every Night/save/` sous macOS.

## Pour créer des chansons

**Éditeur en ligne : <https://dancing-every-night.pages.dev/editor/>**. Rien à installer : crée une chanson à partir d'une musique, détecte le tempo,
pose les notes (ou pars d'un brouillon automatique) et exporte le `.zip` pour la console. Commence par le
[guide du créateur](https://dancing-every-night.pages.dev/guide.fr.html).

Une chanson, c'est **un dossier** au nom de son ID, qui contient un `.chart` par difficulté, la piste audio (`.ogg`)
et, au choix, une image (`.jpg`) ou une vidéo de fond (`.mpg`, MPEG-1) :

```
ma_chanson/
  easy.chart  normal.chart  hard.chart  expert.chart
  audio.ogg  background.jpg  video.mpg
```

Chaque `.chart` commence par un en-tête, puis une note par ligne (temps en beats) :

```
FORMAT 1
ID ma_chanson            # lettres, chiffres, _ et - ; le même dans toutes les difficultés
VERSION 1            # à augmenter quand tu publies une mise à jour
MAPPER ton_pseudo    # l'auteur des difficultés, affiché aux joueurs
TITLE Ma chanson
ARTIST Un artiste
DIFFICULTY NORMAL
ORDER 2              # optionnel : place parmi les difficultés de la chanson (1 = première) ; défaut EASY, NORMAL, HARD, EXPERT
BPM 120
OFFSET_MS 500
AUDIO audio.ogg      # uniquement des noms de fichiers du dossier de la chanson
BACKGROUND background.jpg
END 276              # optionnel : fin du morceau (résultats), en temps ; par défaut juste après la dernière note

4 TRIANGLE TAP
5 CIRCLE HOLD 2
6 SCRATCH CW
```

Pour partager une chanson, zippe son dossier (ou ses fichiers) et envoie le `.zip`. Vérifie-la d'abord :
`make check MAP=ma_chanson.zip` (ou `./vitagame --check ma_chanson.zip`) applique les règles du jeu et liste les
erreurs (la chanson ne marchera pas) et les avertissements (à corriger de préférence).

Pour l'instant, les outils demandent l'installation développeur ci-dessous ; un éditeur web sans
installation est prévu.

```bash
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt
brew install ffmpeg            # ou le gestionnaire de paquets de ton système
```

- `make editor` : éditeur graphique de cartes dans le navigateur (http://localhost:8765), avec conversion audio/vidéo.
- `make editor-site` : la version en ligne de l'éditeur (http://localhost:8766) : sans serveur, les chansons restent dans
  le navigateur. Un audio dans un autre format (mp3, wav, m4a…) est converti en `.ogg` et une vidéo en MPEG-1 directement
  dans le navigateur (ffmpeg.wasm, environ 31 Mo téléchargés au premier usage). La détection du BPM et le brouillon
  automatique exécutent le même `tools/mapper.py` dans le navigateur (Pyodide, environ 30 Mo au premier usage). L'éditeur
  parle anglais et français et a son guide du créateur (`tools/editor/guide.fr.html`). Ouvrir le `.zip` d'une chanson, l'éditer (MAPPER, LICENSE, VERSION compris), exporter le `.zip` pour la Vita.
- `make map SONG=/chemin/piste.ogg NAME=ma_chanson` : premier jet automatique à partir d'un fichier audio.
- `make run CHART=assets/songs/ma_chanson/normal.chart` : la jouer sur l'ordinateur.

## Pour les développeurs

| Commande | Effet |
|---|---|
| `make run` | Compiler et lancer sur l'ordinateur (macOS, Linux, WSL) |
| `make desktop` | Build desktop seul |
| `make vita` | VPK de dev + `eboot.bin` (tous les assets locaux) dans `build-vita/` |
| `make release` | VPK de release, assets commités uniquement, dans `dist/` |
| `make pc` | Paquet PC pour ce système (SDL2 intégrée, assets commités uniquement), dans `dist/` |
| `make deploy` | Envoyer le build sur une Vita qui fait tourner vitacompanion (`VITA_IP` dans `.env`) |
| `make clean` | Supprimer les dossiers de build |

Prérequis : CMake, un compilateur C et SDL2 (`brew install cmake sdl2` ou
`sudo apt install build-essential cmake libsdl2-dev`), et [VitaSDK](https://vitasdk.org) pour la Vita
(ou Docker : `scripts/build-vita.sh` se rabat sur l'image `vitasdk/vitasdk`).

Le nom du jeu, sa version et son TITLEID sont définis une seule fois, en haut de `CMakeLists.txt`.
`CLAUDE.md` décrit l'architecture et les contraintes de la PS Vita.

GitHub Actions (`.github/workflows/builds.yml`, lancé à la main ou en poussant un tag `v*`) construit les paquets
Windows, macOS, Linux et Vita ; un tag prépare aussi une GitHub Release en brouillon avec les quatre fichiers.

Les morceaux ajoutés en local pour tester (`assets/songs/`) sont ignorés par git et
jamais inclus dans `make release`.

**Chansons officielles et leur audio.** Les trois chansons officielles sont dans `assets/songs/` (difficultés, images,
crédits), mais leurs fichiers audio ne sont **pas** dans ce dépôt : les conditions des labels autorisent à les distribuer
dans le jeu, pas seuls. Ils vivent dans un dépôt privé lu par la CI. Sans eux le jeu tourne normalement et ces
chansons sont muettes (un métronome joue), et `make check` signale l'`audio.ogg` manquant. Les builds de release
(`make release`, `make pc`) reprennent les fichiers de `assets/songs/<ID>/audio.ogg` quand tu les as
(`scripts/official-audio.sh`).

## Licence

- Code : [GNU GPL v3](LICENSE).
- Identité visuelle (logo, hôtesse, écran titre, LiveArea) : CC BY-NC-ND 4.0, voir [`LICENSE-ASSETS.md`](LICENSE-ASSETS.md).
- Crédits des polices, de la musique et du code tiers : [`assets/CREDITS.md`](assets/CREDITS.md).
