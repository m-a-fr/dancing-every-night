# CLAUDE.md — Jeu homebrew PS Vita (C + SDL2 + VitaSDK)

Ce fichier est lu par Claude Code au début de chaque session. Il décrit le projet,
les contraintes matérielles et les règles de travail. Le respecter en priorité.

## Le projet

Jeu 2D homebrew pour PS Vita, écrit en **C11 + SDL2**, compilé avec **VitaSDK**.
Le même code compile aussi sur desktop (Linux/macOS/WSL) pour itérer vite.
Game design : voir `GAME_DESIGN.md`, plan de sortie : `RELEASE_PLAN.md`. Ces deux documents sont **locaux**
(ignorés par git, jamais publiés) : ils citent des jeux de référence et des décisions internes.

Identité du jeu (nom, version, TITLEID, dossier de sauvegarde) : **uniquement** en haut de `CMakeLists.txt`,
reçue dans le code par les macros `GAME_NAME`, `GAME_VERSION`, `GAME_SAVE_SLUG`. Ne jamais l'écrire ailleurs.

## Commandes

| Action | Commande |
|---|---|
| Build + lancer sur PC | `make run` |
| Build desktop seul | `make desktop` |
| Build Vita (.vpk + eboot.bin) | `make vita` |
| VPK de release (assets commités seulement) | `make release` → `dist/` |
| Paquet PC de ce système (SDL2 statique, .app sur macOS) | `make pc` → `dist/` ; CI : `.github/workflows/builds.yml` (manuel ou tag `v*`) |
| Envoyer et relancer sur la console | `make deploy` (nécessite `VITA_IP` dans `.env`) |
| Première install du .vpk | `make install-vpk` puis installer via VitaShell |
| Vérifier une map (zip ou dossier) | `make check MAP=...` |
| Éditeur en ligne (sans serveur), essai local | `make editor-site` → http://localhost:8766 (accueil ; éditeur sur /editor/, bibliothèque sur /songs/) ; publié sur https://dancing-every-night.pages.dev (Cloudflare Pages, à chaque push sur `main`) |
| Nouveau morceau (lien YouTube ou fichier) | `make song SRC=... TITLE=... ARTIST=... [PACK=...] [NOVIDEO=1 IMAGE=...]` (vidéo gardée par défaut) |
| Nettoyer | `make clean` |

Après chaque modification : **`make desktop` doit compiler sans warning**. Lancer `make vita`
avant de déclarer une fonctionnalité terminée. Ne jamais lancer `make deploy` sans que
l'utilisateur l'ait demandé (la console doit être allumée et connectée).

## Architecture

```
src/
  main.c      boucle principale, init SDL, timing (dt variable, plafonné à 1/20 s)
  config.h    résolution, chemins d'assets et de sauvegarde par plateforme
  sdl.h       include portable de SDL2 — toujours inclure "sdl.h", jamais <SDL.h>
  input.h/.c  abstraction des contrôles -> struct Input (held / pressed / sticks)
  game.h/.c   état du jeu (struct Game) + update + draw
  net.h/.c    GET HTTPS (libcurl) ; library.h/.c  écran LIBRARY : catalogue en ligne, liste, fiche, état installé
assets/       fichiers du jeu, copiés dans app0:assets/ sur Vita
sce_sys/      icône et LiveArea (PNG indexés 8 bits obligatoires)
```

Règles d'architecture :
- La logique de jeu ne lit **que** `Input`, jamais SDL directement ni `sceCtrl*`.
- Tout l'état est dans des structs passées en paramètre ; pas de globales dans la logique.
- Le code spécifique Vita est isolé derrière `#ifdef __vita__` et reste minimal.
- Nouveaux modules : un couple `.h/.c` dans `src/` (le CMake les prend automatiquement).
- Update et draw séparés : `*_update(state, input, dt)` ne dessine pas, `*_draw` ne modifie pas l'état.

## Contraintes matérielles PS Vita (non négociables)

- Écran **960×544**, 60 Hz. Toute la logique raisonne dans cet espace (`SCREEN_W/H`).
- CPU ARM Cortex-A9 quad-core à 444 MHz max : modeste. Préférer `float` à `double`,
  `sqrtf/sinf` plutôt que `sqrt/sin`, éviter les allocations dans la boucle de jeu.
- RAM : ~256 Mo utilisables par l'app, tas newlib réglé dans `main.c`
  (`_newlib_heap_size_user`). VRAM limitée : textures raisonnables (≤ 2048×2048,
  puissance de 2 de préférence), atlas plutôt que des centaines de petites images.
- Charger les assets **au démarrage ou aux changements de niveau**, jamais pendant le jeu.
- Pas de bibliothèques dynamiques, pas de `system()`. Réseau (libcurl, `src/net.c`) seulement pour : l'annonce des mises à jour (`src/update.c`, lit `latest.json` au démarrage, désactivable dans OPTIONS ; rien n'est installé) et l'écran LIBRARY (`src/library.c`, lit `catalog.json` à chaque ouverture et télécharge les zips choisis, vérifiés par SHA-256 puis installés par `mapimport_install` ; sans réseau, pas de catalogue, aucune copie gardée). Sur PC, libcurl n'est liée que si le système l'a (`GAME_NET`, coupé dans les paquets distribués).
- Budget : **60 fps stables** visés ; si impossible, 30 fps assumés et constants.
- Audio : SDL2 audio fonctionne ; pour du son/musique, SDL2_mixer est disponible dans VitaSDK
  (vérifier avant d'ajouter toute dépendance : elle doit exister dans `vdpm` / l'image Docker).

## Contrôles

| Vita | SDL GameController | Clavier (desktop) | Usage démo |
|---|---|---|---|
| Croix | A | Espace / J | dash |
| Rond | B | K | — |
| Carré | X | U | changer couleur |
| Triangle | Y | I | — |
| L / R | épaules | Q / E | scratch (valide n'importe quelle scratch) ; en pause : calibration audio ±10 ms |
| Start | START | Entrée | pause |
| Select | BACK | Retour arrière | Select+Start = quitter |
| Stick gauche / croix | axes / DPAD | flèches / WASD | déplacement |

Écrans tactiles : via les événements `SDL_FINGER*` (coordonnées normalisées 0..1,
`touchId` 0 = avant, 1 = pavé arrière). Ne jamais rendre le tactile obligatoire sans
alternative bouton, car le desktop ne l'a pas.

Convention occidentale : Croix = valider, Rond = annuler.

## Fichiers et chemins

- Lecture d'assets : macro `ASSET_PATH("fichier.png")` (→ `app0:assets/` sur Vita).
- Sauvegardes : `SAVE_DIR` (→ `ux0:data/dancingeverynight/`). Créer le dossier au besoin
  (`sceIoMkdir` sur Vita, `mkdir` sur desktop).
- `app0:` est en lecture seule.

## Débogage

- `printf` n'est pas visible sur la console. Pour des logs sur Vita, écrire dans
  `ux0:data/dancingeverynight/log.txt`, ou afficher un overlay de debug à l'écran (FPS, état).
- Crash sur Vita → un fichier `.psp2dmp` est créé dans `ux0:data/`. L'utilisateur peut
  le récupérer et le décoder avec **vita-parse-core** (avec l'ELF `build-vita/vitagame`)
  puis coller la trace ici.
- Build Vita : `vita-elf-create: Cannot allocate ... segment 1 overlaps` = le code se termine trop
  d'une frontière de page de 4 Ko (coïncidence de taille). `scripts/build-vita.sh` le contourne tout seul
  en relançant avec un bloc de remplissage (`VITA_PAD_BYTES`) ; `--gc-sections` réduit aussi le binaire.
- Vérifier un rendu sans jouer (desktop) : `cd build-desktop && VITAGAME_SHOT=/chemin/capture.bmp
  VITAGAME_SHOT_FRAME=300 ./vitagame [carte.chart]` enregistre l'image 300 puis quitte.
- Atteindre un écran / tester un parcours de menu sans manette (desktop) : `VITAGAME_KEYS="5:CROSS,10:L,30:CROSS"`
  simule des appuis (`image:BOUTON`, boutons UP DOWN LEFT RIGHT CROSS CIRCLE SQUARE TRIANGLE L R START SELECT) ;
  se combine avec `VITAGAME_SHOT`. Sans carte en argument, le jeu démarre sur l'écran titre (Croix = menu principal, Croix encore = sélection de chanson).
- Capturer l'écran de jeu ou de résultats sans jouer (desktop) : `VITAGAME_AUTOPLAY=1` joue la carte parfaitement
  (Start et les touches simulées restent actifs en pause), à combiner avec `VITAGAME_SHOT` / `VITAGAME_KEYS`.
- Essayer l'écran LIBRARY sans réseau (desktop) : `VITAGAME_LIBRARY_JSON=catalogue.json` lit un catalogue local (même format que `catalog.json`) ; l'état « installée » se lit dans `save/songs/<ID>/*.chart` (ligne `VERSION`). Se combine avec `VITAGAME_KEYS` (Croix, Bas, Croix depuis l'écran titre) et `VITAGAME_SHOT`.
- Un comportement qui marche sur desktop mais pas sur Vita vient presque toujours de :
  chemin de fichier, mémoire insuffisante, format de texture, ou alignement/endianness
  dans du code de sérialisation.

## Façon de travailler avec l'utilisateur

- L'utilisateur fait du « vibe coding » : il décrit ce qu'il veut, tu implémentes.
  Avancer par **petites étapes jouables** : chaque étape se termine par un build qui
  tourne et une phrase qui dit quoi tester et avec quels boutons.
- Avant une grosse fonctionnalité, proposer un plan court (3–6 points) et attendre l'accord.
- Ne pas réécrire des fichiers entiers pour un petit changement.
- Tenir `GAME_DESIGN.md` à jour quand une décision de gameplay est prise.
- En cas de doute sur une API VitaSDK (`psp2/*.h`), le dire plutôt que d'inventer une
  fonction : vérifier dans les headers de `$VITASDK/arm-vita-eabi/include/psp2/`.

## Vocabulaire (décidé le 2026-10-22)

Partout ce que voit l'utilisateur (éditeur, jeu, validateur, docs, futur site) : une **chanson** (EN : *song*) = le
dossier que l'on installe (la musique et toutes ses difficultés) ; une **difficulté** (*difficulty*) = chaque niveau
(EASY, NORMAL…) ; l'auteur des difficultés s'affiche « Auteur » (clé `MAPPER` dans le fichier). Ne pas utiliser
« map », « morceau » ni « carte » dans l'interface. Les noms internes du code (mapimport, `.chart`, `MAPPER`) restent.

## Dépôt public

- Format de map 1 : **une map = un dossier** `songs/<ID>/` (cartes + médias, rien ailleurs) ; voir `src/chart.h`.
  Maps officielles dans `assets/songs/`, maps importées dans `SAVE_DIR "songs/"`, zips à importer dans
  `SAVE_DIR "import/"` (décompressés au démarrage, `src/mapimport.c`). Records : clé `<ID>:<DIFFICULTÉ>`.
- Les morceaux de test (`assets/songs/`), les vidéos de `dev/videos/` et les fichiers de travail
  de l'hôtesse sont **ignorés par git** : ne jamais les forcer (`git add -f`). Une map officielle s'ajoute par
  une exception explicite dans `.gitignore`.
- `make release` n'embarque que les assets commités : c'est ce qui part dans le VPK public. **Exception : l'audio des
  chansons officielles n'est pas dans le dépôt** (dépôt privé `m-a-fr/dne-official-audio`, lu par la CI) ; les scripts de
  release le copient depuis l'arbre de travail (`scripts/official-audio.sh`). Ne jamais le commiter.
- Mettre à jour `CHANGELOG.md` à chaque fonctionnalité visible par le joueur.
