# Contributing to Dancing Every Night

*[Version française plus bas](#contribuer-à-dancing-every-night)*

Thank you for your interest! There are four ways to help, from the easiest to the most involved.

## 1. Play, and tell us what you think

- **Bug?** Open a [Bug report](../../issues/new?template=bug.yml). Say which version you play (shown at the bottom
  right of the title screen) and attach the `.psp2dmp` file if the Vita crashed.
- **Idea?** Open an [Idea](../../issues/new?template=idea.yml), or start a
  [discussion](../../discussions) if it is not ready yet.
- **A song is wrong or should not be shared?** Use [Report a song](../../issues/new?template=song-report.yml).

English and French are both fine, everywhere.

## 2. Make songs

You do not need to code. The [online song editor](https://dancing-every-night.pages.dev) and its
[creator guide](https://dancing-every-night.pages.dev/guide.html) take you from a piece of music to a song you can
play on your Vita or computer in a few minutes. Check a song before sharing it:

```
make check MAP=my_song.zip
```

You stay responsible for the music you share; the `LICENSE` field is optional, fill it in if you know the rights. The song library, where you
will be able to submit songs for everyone to download, is coming; until then, share the `.zip` directly.

## 3. Improve the code

The game is C11 + SDL2 and runs on the PS Vita (VitaSDK) and on desktop. See the *For developers* section of the
[README](README.md) for the commands. In short:

- `make run` builds and runs the game on your computer; `make desktop` must compile **without warnings**.
- `make vita` builds the Vita version (needs [VitaSDK](https://vitasdk.org)); run it before opening a pull request.
- `make check MAP=...` validates a song with the game's own rules.

Rules that keep the code easy to change (details in `CLAUDE.md`):

- Game logic reads only the `Input` struct, never SDL or `sceCtrl*` directly.
- State lives in structs passed as parameters; no globals in the game logic.
- Vita-specific code stays behind `#ifdef __vita__` and stays small.
- `*_update` never draws and `*_draw` never changes state.
- Think about the Vita: a modest ARM CPU, little VRAM, 60 fps on a 960×544 screen. Load assets at start-up, not during play.

Pull requests: keep them small and focused, describe what you tested and on which platform, and add a line to
`CHANGELOG.md` when players can see the change. Open an issue first for anything big.

By contributing code you agree that it is released under the project's licence, **GNU GPL v3** (see `LICENSE`).

## 4. Visuals and translations

The game's visual identity (logo, title screen, hostess) is under a separate licence, **CC BY-NC-ND 4.0**
(see `LICENSE-ASSETS.md`), so we cannot take modified versions of it as contributions. The game itself is in English;
if you would like to translate the guide or the editor into another language, open an idea and we will talk.

---

# Contribuer à Dancing Every Night

Merci de ton intérêt ! Quatre façons d'aider, de la plus simple à la plus poussée.

## 1. Jouer et donner ton avis

- **Un bug ?** Ouvre un [Bug report](../../issues/new?template=bug.yml) en précisant ta version (en bas à droite de
  l'écran titre) et en joignant le fichier `.psp2dmp` si la Vita a planté.
- **Une idée ?** Ouvre une [Idea](../../issues/new?template=idea.yml), ou lance une
  [discussion](../../discussions) si elle n'est pas encore mûre.
- **Une chanson est fautive ou ne devrait pas être partagée ?** Utilise [Report a song](../../issues/new?template=song-report.yml).

Français et anglais sont acceptés partout.

## 2. Créer des chansons

Pas besoin de coder. L'[éditeur de chansons en ligne](https://dancing-every-night.pages.dev) et son
[guide du créateur](https://dancing-every-night.pages.dev/guide.fr.html) t'amènent d'une musique à une chanson jouable
sur ta Vita ou ton ordinateur en quelques minutes. Vérifie une chanson avant de la partager :

```
make check MAP=ma_chanson.zip
```

Tu restes responsable de la musique que tu partages ; le champ `LICENSE` est facultatif, renseigne-le si tu connais les droits. La
bibliothèque de chansons, où tu pourras proposer tes chansons à tout le monde, arrive bientôt ; en attendant, partage
directement le `.zip`.

## 3. Améliorer le code

Le jeu est en C11 + SDL2 et tourne sur PS Vita (VitaSDK) et sur ordinateur. Les commandes sont dans la section
*Pour les développeurs* du [README](README.fr.md). En bref :

- `make run` compile et lance le jeu sur ton ordinateur ; `make desktop` doit compiler **sans warning**.
- `make vita` compile la version Vita (il faut [VitaSDK](https://vitasdk.org)) ; à lancer avant une pull request.
- `make check MAP=...` valide une chanson avec les règles du jeu.

Les règles qui gardent le code facile à faire évoluer (détails dans `CLAUDE.md`) :

- La logique de jeu ne lit que la struct `Input`, jamais SDL ni `sceCtrl*` directement.
- L'état est dans des structs passées en paramètre ; pas de globales dans la logique.
- Le code propre à la Vita reste derrière `#ifdef __vita__` et reste petit.
- `*_update` ne dessine jamais et `*_draw` ne modifie jamais l'état.
- Pense à la Vita : CPU ARM modeste, peu de VRAM, 60 i/s en 960×544. Charge les assets au démarrage, pas en jeu.

Pull requests : petites et ciblées, dis ce que tu as testé et sur quelle plateforme, et ajoute une ligne au
`CHANGELOG.md` quand les joueurs voient le changement. Ouvre d'abord une issue pour tout ce qui est gros.

En contribuant du code, tu acceptes qu'il soit publié sous la licence du projet, **GNU GPL v3** (voir `LICENSE`).

## 4. Visuels et traductions

L'identité visuelle du jeu (logo, écran titre, hôtesse) est sous une licence séparée, **CC BY-NC-ND 4.0** (voir
`LICENSE-ASSETS.md`) : nous ne pouvons donc pas accepter de versions modifiées en contribution. Le jeu est en anglais ;
pour traduire le guide ou l'éditeur dans une autre langue, ouvre une idée et on en parle.
