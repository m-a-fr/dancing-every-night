# Dancing Every Night 1.1.0

*[Version française plus bas](#version-française)*

A community rhythm game for the **PS Vita** (and your computer). Notes fly in towards two arcs of
targets; hit the right button on the beat, keep your combo, chase the S rank.

**1.1 changes how song videos work and brings the hostess into the menus.**

## What's new since 1.0.0

### Lighter, sharper song videos
- Songs now use **H.264** video (Baseline profile): about 20 % smaller files at the same picture quality. The Vita
  decodes them with its hardware video decoder (almost no CPU), and the computer versions with a built-in decoder.
- **Songs published before 1.1 keep working**, and the library already has songs with H.264 video: these play their
  video only from 1.1 on (on 1.0 you get the still picture).

### The hostess
- She now appears on the **results screen** (a different picture for each rank), on the **loading** screen and on the
  **pause** screen. The title screen keeps only the logo.
- She **speaks**: a line at the title screen and one for each rank.
- Replace her pictures or voices with your own: put files in a `custom/` folder next to your saves
  (`ux0:data/dancingeverynight/custom/` on the Vita), with the same path as under `assets/`.

### Songs
- Each difficulty can have its **own note speed** (travel time); songs made with the online editor keep it.
- The song generator and the editor handle both video formats; creators get a new
  [generation guide](tools/MAPPING_GUIDE.md).

## Download

| System | File |
|---|---|
| PS Vita | `DancingEveryNight-1.1.0.vpk` |
| Windows | `DancingEveryNight-1.1.0-windows.zip` |
| macOS (11+, Intel and Apple Silicon) | `DancingEveryNight-1.1.0-macos.zip` |
| Linux | `DancingEveryNight-1.1.0-linux.tar.gz` |

**Vita**: you need a console with custom firmware and VitaShell ([guide](https://vita.hacks.guide)). Copy the
`.vpk` to the console, open it in VitaShell and press Cross to install. Updating from 1.0 keeps your songs
and scores.

**Computer**: nothing to install. Windows is not signed (SmartScreen: *More info → Run anyway*); on macOS, right-click
the app → *Open* the first time. On Linux the LIBRARY needs the `libcurl` package (`libcurl4` on Debian and
Ubuntu); the game itself runs without it.

## Good to know
- Official songs ship with the game; every other song comes from the library or from a `.zip` you add yourself.
- Songs with a video are bigger (up to a few tens of MB): the download takes longer on a slow connection.
- No search box in the library yet: use the filter and sort for now.
- Your saves and songs stay in `ux0:data/dancingeverynight/` on the Vita, whatever the version.

## Songs and credits
The three official songs are charted and distributed under the free rhythm-game terms of **HARDCORE TANO\*C**:
- *Break Through Myself feat. Risa Yuzuki* — DJ Myosuke & Laur — https://youtu.be/9D8NJxSMHrk
- *Breaking Dawn feat. Itsuki Natsume* — Getty & Kobaryo — https://youtu.be/Dgs41A6VjJM
- *Make Up Your World feat. Ci-chan & Rappubito* — t+pazolite & Srav3R — https://youtu.be/oXQjnmYjFFs

The code is under the GNU GPL v3; the game's visual identity is under a separate licence
([LICENSE-ASSETS.md](LICENSE-ASSETS.md)). Community songs keep their own authors and licences, shown in the library.

## Feedback
- Bug or idea: open an issue (forms for bugs, ideas and reporting a song). Mention the version shown on the title
  screen; for a crash, attach the `.psp2dmp` file from `ux0:data/`.
- Chat and share songs: [Discord](https://discord.gg/vTJDvgJe74).
- Make your own song: [online editor](https://dancing-every-night.pages.dev/editor/) and
  [creator guide](https://dancing-every-night.pages.dev/guide.html), then send it to the
  [library](https://dancing-every-night.pages.dev/songs/).

Full list of changes: [CHANGELOG.md](CHANGELOG.md).

---

# Version française

Un jeu de rythme communautaire pour la **PS Vita** (et ton ordinateur). Les notes arrivent vers deux arcs de
cibles ; appuie sur le bon bouton en rythme, garde ton combo, vise le rang S.

**La 1.1 change la vidéo des chansons et fait entrer l'hôtesse dans les menus.**

## Nouveautés depuis la 1.0.0

### Des vidéos plus légères et plus nettes
- Les chansons utilisent désormais la vidéo **H.264** (profil Baseline) : des fichiers environ 20 % plus petits à
  qualité d'image égale. La Vita les décode avec son décodeur vidéo matériel (presque aucun CPU), les versions
  ordinateur avec un décodeur intégré.
- **Les chansons publiées avant la 1.1 continuent de marcher**, et la bibliothèque contient déjà des chansons avec
  vidéo H.264 : leur vidéo ne se lit qu'à partir de la 1.1 (en 1.0 tu vois l'image fixe).

### L'hôtesse
- Elle apparaît maintenant sur l'**écran de résultats** (une image différente par rang), sur l'écran de
  **chargement** et sur l'écran de **pause**. L'écran titre ne garde que le logo.
- Elle **parle** : une phrase à l'écran titre et une pour chaque rang.
- Remplace ses images ou ses voix par les tiennes : mets des fichiers dans un dossier `custom/` à côté de tes
  sauvegardes (`ux0:data/dancingeverynight/custom/` sur la Vita), avec le même chemin que sous `assets/`.

### Chansons
- Chaque difficulté peut avoir sa **propre vitesse de notes** (temps de trajet) ; les chansons faites avec l'éditeur
  en ligne la conservent.
- Le générateur de chansons et l'éditeur gèrent les deux formats de vidéo ; les créateurs ont un nouveau
  [guide de génération](tools/MAPPING_GUIDE.md).

## Téléchargement

| Système | Fichier |
|---|---|
| PS Vita | `DancingEveryNight-1.1.0.vpk` |
| Windows | `DancingEveryNight-1.1.0-windows.zip` |
| macOS (11+, Intel et Apple Silicon) | `DancingEveryNight-1.1.0-macos.zip` |
| Linux | `DancingEveryNight-1.1.0-linux.tar.gz` |

**Vita** : il faut une console avec firmware personnalisé et VitaShell ([guide](https://vita.hacks.guide)). Copie le
`.vpk` sur la console, ouvre-le dans VitaShell et appuie sur Croix pour l'installer. Depuis la 1.0, tes chansons
et tes scores sont conservés.

**Ordinateur** : rien à installer. Windows n'est pas signé (SmartScreen : *Informations complémentaires →
Exécuter quand même*) ; sur macOS, clic droit sur l'app → *Ouvrir* la première fois. Sous Linux, la LIBRARY demande
le paquet `libcurl` (`libcurl4` sous Debian et Ubuntu) ; le jeu lui-même démarre sans.

## Bon à savoir
- Les chansons officielles sont dans le jeu ; toutes les autres viennent de la bibliothèque ou d'un `.zip` que tu
  ajoutes toi-même.
- Une chanson avec vidéo est plus lourde (jusqu'à quelques dizaines de Mo) : le téléchargement est plus long sur une
  connexion lente.
- Pas encore de recherche par texte dans la bibliothèque : utilise le filtre et le tri.
- Tes sauvegardes et tes chansons restent dans `ux0:data/dancingeverynight/` sur la Vita, quelle que soit la version.

## Chansons et crédits
Les trois chansons officielles sont mappées et distribuées selon les conditions libres pour jeux de rythme de
**HARDCORE TANO\*C** (liens ci-dessus). Le code est sous GNU GPL v3 ; l'identité visuelle du jeu est sous une
licence séparée ([LICENSE-ASSETS.md](LICENSE-ASSETS.md)). Les chansons de la communauté gardent leurs auteurs et
leurs licences, affichés dans la bibliothèque.

## Retours
- Bug ou idée : ouvre une issue (formulaires pour un bug, une idée ou signaler une chanson). Indique la version
  affichée sur l'écran titre ; pour un plantage, joins le fichier `.psp2dmp` de `ux0:data/`.
- Discuter et partager des chansons : [Discord](https://discord.gg/vTJDvgJe74).
- Créer ta chanson : [éditeur en ligne](https://dancing-every-night.pages.dev/editor/) et
  [guide du créateur](https://dancing-every-night.pages.dev/guide.fr.html), puis envoie-la à la
  [bibliothèque](https://dancing-every-night.pages.dev/songs/).
