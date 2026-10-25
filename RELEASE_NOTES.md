# Dancing Every Night 1.0.0

*[Version française plus bas](#version-française)*

A community rhythm game for the **PS Vita** (and your computer). Notes fly in towards two arcs of
targets; hit the right button on the beat, keep your combo, chase the S rank.

**1.0 brings the song library into the game**: browse the online catalogue from your console and install a
song with one button. No computer, no zip, no cable.

## What's new since 0.9.0

### In-game song library
- New **LIBRARY** entry in the main menu: the whole online catalogue in a dense list, with the cover picture,
  difficulties, number of notes, length and size of the selected song.
- Each song tells you if it is already yours: a **check** when it is installed, an **arrow** when a newer version
  exists, an empty circle when you don't have it yet.
- Filter (all / not installed / installed / updates) and sort (newest / title / artist).
- **Cross installs** a song: download with a progress bar (Circle cancels), checked against the catalogue, then
  added to your song list straight away. An update replaces the old version and keeps your scores.
- Works on the Vita (Wi-Fi), Windows, macOS and Linux. It needs an internet connection; without one the game
  tells you so, and everything else keeps working.

### Playing
- **Scratch notes are clearer**: the blue ring now reads "SCRATCH" and "R OR L", and **a light touch of either
  stick, in any direction**, hits them. L and R still work.
- **Loading is honest**: the loading card appears the moment you pick a song, and its bar shows the steps really
  done (song, music, video) with the current one named.
- **Linked notes** lose their black outline: the bar between the two heads now matches the other notes.

### Everything else
- The main menu announces a newer game version when one exists (switch it off in OPTIONS > UPDATE CHECK; nothing
  is ever downloaded or installed by it).
- Songs can also be installed from an extracted folder in `import/`, not only from a `.zip`.
- A website home page; the online editor can send a finished song straight to the library.

## Download

| System | File |
|---|---|
| PS Vita | `DancingEveryNight-1.0.0.vpk` |
| Windows | `DancingEveryNight-1.0.0-windows.zip` |
| macOS (11+, Intel and Apple Silicon) | `DancingEveryNight-1.0.0-macos.zip` |
| Linux | `DancingEveryNight-1.0.0-linux.tar.gz` |

**Vita**: you need a console with custom firmware and VitaShell ([guide](https://vita.hacks.guide)). Copy the
`.vpk` to the console, open it in VitaShell and press Cross to install. Updating from 0.9.x keeps your songs
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

**La 1.0 met la bibliothèque de chansons dans le jeu** : parcours le catalogue en ligne depuis ta console et
installe une chanson d'un bouton. Sans ordinateur, sans zip, sans câble.

## Nouveautés depuis la 0.9.0

### Bibliothèque de chansons dans le jeu
- Nouvelle entrée **LIBRARY** dans le menu principal : tout le catalogue en ligne dans une liste dense, avec
  l'image, les difficultés, le nombre de notes, la durée et la taille de la chanson sélectionnée.
- Chaque chanson indique si elle est déjà à toi : une **coche** si elle est installée, une **flèche** si une version
  plus récente existe, un cercle vide si tu ne l'as pas encore.
- Filtre (toutes / non installées / installées / mises à jour) et tri (récentes / titre / artiste).
- **Croix installe** une chanson : téléchargement avec une barre de progression (Rond annule), vérifié avec le
  catalogue, puis ajouté tout de suite à ta liste. Une mise à jour remplace l'ancienne version et garde tes scores.
- Fonctionne sur la Vita (Wi-Fi), Windows, macOS et Linux. Il faut une connexion internet ; sans elle le jeu te le
  dit, et tout le reste continue de marcher.

### En jeu
- **Les scratches sont plus lisibles** : l'anneau bleu affiche « SCRATCH » et « R OR L », et **un léger toucher de
  l'un des deux sticks, dans n'importe quel sens,** les valide. L et R marchent toujours.
- **Le chargement est honnête** : la fiche apparaît dès que tu choisis une chanson, et la barre montre les étapes
  réellement terminées (chanson, musique, vidéo), l'étape en cours étant nommée.
- **Les notes liées** n'ont plus de contour noir : la barre entre les deux têtes est assortie aux autres notes.

### Le reste
- Le menu principal annonce une nouvelle version du jeu quand il y en a une (désactivable dans OPTIONS > UPDATE
  CHECK ; rien n'est jamais téléchargé ni installé par ce biais).
- Une chanson s'installe aussi depuis un dossier décompressé dans `import/`, pas seulement depuis un `.zip`.
- Une page d'accueil pour le site ; l'éditeur en ligne envoie une chanson terminée directement à la bibliothèque.

## Téléchargement

| Système | Fichier |
|---|---|
| PS Vita | `DancingEveryNight-1.0.0.vpk` |
| Windows | `DancingEveryNight-1.0.0-windows.zip` |
| macOS (11+, Intel et Apple Silicon) | `DancingEveryNight-1.0.0-macos.zip` |
| Linux | `DancingEveryNight-1.0.0-linux.tar.gz` |

**Vita** : il faut une console avec firmware personnalisé et VitaShell ([guide](https://vita.hacks.guide)). Copie le
`.vpk` sur la console, ouvre-le dans VitaShell et appuie sur Croix pour l'installer. Depuis la 0.9.x, tes chansons
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
