# Dancing Every Night

*[Version française](README.fr.md)*

A community rhythm game for the **PS Vita**, written in C11 + SDL2. Notes fly in towards two arcs of
button targets; hit the right button on the beat, keep your combo, chase the S rank.

The goal is an osu!-style community: the game ships with a couple of original songs, and players
create, share and download new ones.

> **Status: 1.0.0.** The game is playable, with 3 official songs, and the in-game **song library** lets you browse and
> install community songs from the console.

**Community:** [Discord](https://discord.gg/vTJDvgJe74) · [Song library](https://dancing-every-night.pages.dev/songs/) · [Online editor](https://dancing-every-night.pages.dev/editor/)

## For players

You need a PS Vita running custom firmware (follow https://vita.hacks.guide) with **VitaShell**.

1. Download `DancingEveryNight-<version>.vpk` from the Releases page.
2. Copy it to the Vita (VitaShell USB or FTP mode), open it in VitaShell and press Cross to install.
3. Launch *Dancing Every Night* from the home screen.

| Vita | Action |
|---|---|
| Triangle, Circle, Cross / Up, Left, Down | Hit the notes of the matching target |
| L / R, or a light touch of either stick | Scratch notes (any direction) |
| Start | Pause (L/R in pause: audio calibration ±10 ms) |
| Select + Start | Quit |

Saves are stored in `ux0:data/dancingeverynight/`.

**Updates:** when the Vita is online, the game reads a small file (`latest.json`, on the project website) at start-up and, if a newer version exists, announces it in the main menu. Nothing is downloaded or installed, and nothing about you is sent beyond the request itself. Switch it off in OPTIONS > UPDATE CHECK.

### Installing a song

1. Copy the song's `.zip` file or extracted folder into `ux0:data/dancingeverynight/import/` (VitaShell USB or FTP mode).
   The folder is created the first time you launch the game.
2. Launch the game: the song is installed during the loading screen, then the source ZIP or folder is deleted.
3. The song shows up in the song list (and in its pack, if it has one).

Installing a song with the same ID again replaces it and keeps your best scores. To delete a song, select it in the
song list and press **Triangle**; you can also delete its folder in `ux0:data/dancingeverynight/songs/`.
A ZIP or folder that cannot be installed is moved to `import/failed/`, with a `.txt` file explaining why.

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

**Online editor: <https://dancing-every-night.pages.dev/editor/>**. Nothing to install: create a song from a music file, detect the tempo, place
### Replacing the hostess pictures and voices

Everything the hostess shows or says is a plain file you can swap. Put your own copy in a `custom/` folder inside
`save/` (on the Vita: `ux0:data/dancingeverynight/custom/`), with the same path it has under `assets/`; the game reads
`custom/` first, so nothing in the game itself changes and removing your file brings the original back.

| File in `custom/` | What it is |
|---|---|
| `ui/hostess/rank_s.png`, `rank_a.png`, `rank_b.png`, `rank_cd.png` | The hostess on the results screen for each rank (C and D share one) |
| `ui/hostess/loading.png`, `pause.png` | The hostess on the loading and pause screens |
| `audio/sfx/voice_*.wav` | Her voice: `voice_title`, `voice_rank_s`, `voice_rank_a`, `voice_rank_b`, `voice_rank_cd` (heard at the title and on the results) |

Pictures: PNG with a transparent background, at most 2048 px on a side; they are drawn anchored to the bottom of the
screen, so crop them there. Sounds: WAV, 16-bit, mono, 44100 Hz (`ffmpeg -i in.wav -ar 44100 -ac 1 -sample_fmt s16 out.wav`).
Every other interface sound in `audio/sfx/` can be replaced the same way.

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

## Use of AI

I want to be clear about where AI was used in this project, and what I think about it.

**What was made with AI**

| Area | What | How |
|---|---|---|
| Code | The game (C11 + SDL2), the Python tools (automatic draft, editor, library scripts), the online editor, the CI, the tests, the documentation and its translations | Written with [Claude Code](https://claude.com/claude-code) (Anthropic). I set the design and the goals, play-test on a real Vita, review and decide; the model writes most of the code. |
| Visual identity | The logo, the hostess (mascot) and the title screen layers, plus the images derived from them: boot splash, LiveArea and icon | Generated with AI image tools (ChatGPT), then selected and edited by hand (the stained-glass look is applied by a script). |
| Charts | The difficulties of the official songs | Start from an automatic draft (`tools/mapper.py`: a signal-analysis algorithm I wrote with Claude, not a generative model), then played, corrected and tuned by me. |

**What was not made with AI**: the music (the three official songs are by their artists and used under their label's
| Hostess pictures | The six poses of the hostess (results, loading, pause) | Generated with AI image tools (ChatGPT), then selected and edited by hand. |
| Hostess voice | The title and results lines | Synthesized with [Qwen3-TTS](https://github.com/QwenLM/Qwen3-TTS) (Apache-2.0): an invented voice, no real person cloned. I chose the voice and the takes by ear and trimmed them. |
terms; the menu music is by Juhani Junkala), the fonts, the button icons, and the interface sounds (synthesized by a
script; only the hostess's voice is generative audio). Community songs carry their own rights, declared by their authors.

**What I think**

- **Graphics: a stopgap.** This is a volunteer project with no budget. AI let the game have a coherent look rather
  than none, but it is not where I want it to stay. I would much rather work with artists, and I will replace these
  visuals as soon as I can collaborate with some. If you draw, animate or design interfaces and the project speaks to you,
  come and talk on [Discord](https://discord.gg/vTJDvgJe74).
- **Engineering: assumed.** AI is how I build this game today, and I do not hide it. It is what lets one person ship
  a Vita game with a song editor and an online library. It does not remove responsibility: I choose what the game
  should be, I test it on the console, and bugs are mine to fix.

## Licence

- Code: [GNU GPL v3](LICENSE).
- Visual identity (logo, hostess, title screen, LiveArea): CC BY-NC-ND 4.0, see [`LICENSE-ASSETS.md`](LICENSE-ASSETS.md).
- Credits for fonts, music and third-party code: [`assets/CREDITS.md`](assets/CREDITS.md).
