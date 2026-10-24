# Dancing Every Night

*[Version française](README.fr.md)*

A community rhythm game for the **PS Vita**, written in C11 + SDL2. Notes fly in towards two arcs of
button targets; hit the right button on the beat, keep your combo, chase the S rank.

The goal is an osu!-style community: the game ships with a couple of original songs, and players
create, share and download new ones.

> **Status: 0.9.0, pre-release.** The game is playable and community songs can be installed; the
> official songs are still in progress.

## For players

You need a PS Vita running custom firmware (follow https://vita.hacks.guide) with **VitaShell**.

1. Download `DancingEveryNight-<version>.vpk` from the Releases page.
2. Copy it to the Vita (VitaShell USB or FTP mode), open it in VitaShell and press Cross to install.
3. Launch *Dancing Every Night* from the home screen.

| Vita | Action |
|---|---|
| Triangle, Circle, Cross / Up, Left, Down | Hit the notes of the matching target |
| L / R | Scratch notes |
| Start | Pause (L/R in pause: audio calibration ±10 ms) |
| Select + Start | Quit |

Saves are stored in `ux0:data/dancingeverynight/`.

### Installing a song

1. Copy the song's `.zip` file into `ux0:data/dancingeverynight/import/` (VitaShell USB or FTP mode).
   The folder is created the first time you launch the game.
2. Launch the game: the zip is unpacked during the loading screen, then deleted.
3. The song shows up in the song list (and in its pack, if it has one).

Installing a song with the same ID again replaces it and keeps your best scores. To delete a song, select it in the
song list and press **Triangle**; you can also delete its folder in `ux0:data/dancingeverynight/songs/`.
A zip that cannot be installed is moved to `import/failed/`, with a `.txt` file explaining why.

**Bugs and ideas:** open an issue on this repository (there are forms for bugs, ideas and reporting a song) and mention
the version shown at the bottom right of the title screen. See [CONTRIBUTING](CONTRIBUTING.md) for the other ways to help. For a crash, attach the `.psp2dmp` file found in `ux0:data/`.

### On a computer

The Releases page also has PC versions, with nothing else to install:

- **Windows**: unzip `DancingEveryNight-<version>-windows.zip` and run `DancingEveryNight.exe`. The build is not
  signed: if SmartScreen warns, choose *More info → Run anyway*.
- **macOS** (11 or later, Intel or Apple Silicon): unzip, move *Dancing Every Night* to Applications, then the first
  time right-click it → *Open* (the app is not notarised).
- **Linux**: extract `DancingEveryNight-<version>-linux.tar.gz` and run `DancingEveryNight`.

Keyboard: arrows / WASD, Space or J (Cross), K (Circle), U (Square), I (Triangle), Q / E (scratch), Enter (Start),
Backspace (Select); a game controller works too. Songs install the same way, through an `import/` folder: in the
`save/` folder next to the game on Windows and Linux, in `~/Library/Application Support/Dancing Every Night/save/`
on macOS.

## For song creators

**Online editor: <https://dancing-every-night.pages.dev>**. Nothing to install: create a song from a music file, detect the tempo, place
the notes (or start from an automatic draft) and export the `.zip` for the console. Start with the
[creator guide](https://dancing-every-night.pages.dev/guide.html).

A song is **one folder** named after its ID, holding one `.chart` file per difficulty, the song audio
(`.ogg`) and an optional background image (`.jpg`) or video (`.mpg`, MPEG-1):

```
my_song/
  easy.chart  normal.chart  hard.chart  expert.chart
  audio.ogg  background.jpg  video.mpg
```

Each `.chart` starts with a header, then one note per line (times in beats):

```
FORMAT 1
ID my_song            # letters, digits, _ and -; the same in every difficulty
VERSION 1            # raise it when you publish an update
MAPPER your_name     # the author of the difficulties, shown to players
TITLE My Song
ARTIST Some Artist
DIFFICULTY NORMAL
ORDER 2              # optional: position among the song's difficulties (1 = first); default EASY, NORMAL, HARD, EXPERT
BPM 120
OFFSET_MS 500
AUDIO audio.ogg      # file names inside the song folder only
BACKGROUND background.jpg
END 276              # optional: when the song ends (results), in beats; default = just after the last note

4 TRIANGLE TAP
5 CIRCLE HOLD 2
6 SCRATCH CW
```

To share a song, zip its folder (or its files) and send the `.zip`. Check it first:
`make check MAP=my_song.zip` (or `./vitagame --check my_song.zip`) applies the game's own rules and lists
errors (the song will not work) and warnings (worth fixing).

Today's tools need the developer setup below; a no-install web editor is planned.

```bash
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt
brew install ffmpeg            # or your package manager
```

- `make editor` — graphical chart editor in the browser (http://localhost:8765), with audio/video conversion.
- `make editor-site` — the online version of the editor (http://localhost:8766): no server, songs are kept in the
  browser. Audio in another format (mp3, wav, m4a…) is converted to `.ogg` and videos to MPEG-1 right in the browser
  (ffmpeg.wasm, about 31 MB downloaded on first use). BPM detection and the automatic draft run the same
  `tools/mapper.py` in the browser (Pyodide, about 30 MB downloaded on first use). The editor speaks English and
  French, and comes with a creator guide (`tools/editor/guide.html`). Open a song `.zip`, edit it (including MAPPER, LICENSE, VERSION), export the `.zip` for the Vita.
- `make map SONG=/path/track.ogg NAME=my_song` — automatic first draft from an audio file.
- `make run CHART=assets/songs/my_song/normal.chart` — play it on the computer.

## For developers

| Command | What it does |
|---|---|
| `make run` | Build and run on the computer (macOS, Linux, WSL) |
| `make desktop` | Desktop build only |
| `make vita` | Development VPK + `eboot.bin` (all local assets) in `build-vita/` |
| `make release` | Release VPK with committed assets only, in `dist/` |
| `make pc` | PC package for this system (SDL2 built in, committed assets only), in `dist/` |
| `make deploy` | Send the build to a Vita running vitacompanion (`VITA_IP` in `.env`) |
| `make clean` | Remove build folders |

Requirements: CMake, a C compiler and SDL2 (`brew install cmake sdl2` or
`sudo apt install build-essential cmake libsdl2-dev`), plus [VitaSDK](https://vitasdk.org) for Vita
builds (or Docker: `scripts/build-vita.sh` falls back to the `vitasdk/vitasdk` image).

The game name, version and TITLEID are defined once, at the top of `CMakeLists.txt`.
`CLAUDE.md` documents the architecture and the PS Vita constraints.

GitHub Actions (`.github/workflows/builds.yml`, run by hand or by pushing a `v*` tag) builds the Windows, macOS,
Linux and Vita packages; a tag also prepares a draft GitHub Release with the four files.

Songs you add locally for testing (`assets/songs/`) are ignored by git and never
included in `make release`.

**Official songs and their audio.** The three official songs are in `assets/songs/` (charts, images, credits), but their
audio files are **not** in this repository: the labels' terms allow distributing them inside the game, not on their own.
They live in a private repository read by the CI. Without them the game runs normally and those songs are silent
(a metronome plays), and `make check` reports the missing `audio.ogg`. Release builds (`make release`, `make pc`)
pick the files up from `assets/songs/<ID>/audio.ogg` when you have them (`scripts/official-audio.sh`).

## Licence

- Code: [GNU GPL v3](LICENSE).
- Visual identity (logo, hostess, title screen, LiveArea): CC BY-NC-ND 4.0, see [`LICENSE-ASSETS.md`](LICENSE-ASSETS.md).
- Credits for fonts, music and third-party code: [`assets/CREDITS.md`](assets/CREDITS.md).
