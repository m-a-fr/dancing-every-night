# Changelog

All notable changes to Dancing Every Night. Versions follow `MAJOR.MINOR.PATCH`.

## [0.9.0] — 2026-10-25

First version prepared for public release.

### Gameplay
- The in-game score counter grows past 7 digits (it used to show only the first 7 above 9,999,999).
- Six lanes on two arcs (triangle, circle, cross / up, left, down), taps, holds, double notes, scratches (L/R or stick flick).
- Timing judgements, combo, score and ranks; animated results screen.
- Background image or MPEG-1 video per song, chorus preview in the menu.
- Audio offset calibration (tap-along) in the options.

### Interface
- Boot splash, animated title screen, main menu, options.
- Song select with packs, difficulties and best scores.
- Menu music and sound effects.

### Song editor
- Opening a song ZIP automatically prepares its video preview, with progress and a retry on failure.
  The gameplay preview now draws the video in sync with playback, seeking, speed and video offset.
  Replacing a song clears old previews and ignores conversions from the previous ZIP.
- Live tempo calibration during import and editing: waveform, draggable beat grid, zoom, seeking,
  continuous playback and a metronome that follows BPM and first-beat changes without restarting the song.
- New difficulties inherit the song image, video, dimming and video timing; pending video conversion also
  updates difficulties added while it runs. Video frame loading registers its events before decoding.
- ZIP exports use the song title, with an editable name for individual exports. Existing overlong media-based
  song IDs are shortened consistently on import/export to match the Vita format; missing media block export.
- Songs with the same title keep separate identities in the editor. Publication credits remain optional for
  playing locally; the preparation step is now called “Publish”. Technical field names and redundant file
  paths are replaced with instructions about editing, playback and sharing.

### Community songs
- Attachment downloads support GitHub authentication without forwarding credentials on redirects.
  Private attachments remain inaccessible with the default Actions token; a manual CC0 fixture test
  checks publication independently.
- Maintainer publication and withdrawal workflow for the community library: stable publication IDs,
  matching source checksums, R2 upload, catalogue updates and withdrawal of recorded versions.
  Deployment requires R2 configuration; original local record IDs are recorded but not migrated automatically.
- Song format 1: one folder per song (`songs/<ID>/`) with one `.chart` per difficulty and its media; new header
  lines `FORMAT`, `ID`, `VERSION`, `MAPPER`, `SOURCE`, `LICENSE`. Media paths are file names inside the folder.
- Install songs by copying a `.zip` into `ux0:data/dancingeverynight/import/`: unpacked at startup, a song with the
  same ID is replaced; rejected zips go to `import/failed/` with the reason.
- Triangle deletes the selected imported song in song select (with confirmation).
- Best scores are keyed by song ID and difficulty, survive reinstalls and deletions; older scores are converted.
- Song list cached in `index.txt`: much faster startup with many songs.
- Tools (`make song`, `make map`, chart editor) write maps in the new layout; `tools/migrate_maps.py` converts
  songs of the old layout.

### Song end
- Optional `END <beat>` line: the song ends there (results), the progress bar reaches 100 % at that point and the
  music fades out under the results. Without it, the game ends just after the last note, as before.
- Editor: the end of the audio track is shown (timeline greyed beyond it) and the song end is a draggable marker
  (dashed while automatic), plus an End field in the Tempo step.
- Song checker warns about notes after END and an END past the end of the audio track.

### Online editor: English and creator guide
- Published on Cloudflare Pages: <https://dancing-every-night.pages.dev> (rebuilt from `main` by `scripts/build-editor-site.sh`).
- The editor is available in English and French: it follows the browser's language, with an FR / EN switch in the
  header that is remembered (`tools/editor/i18n.js`).
- Creator guide in English and French (`guide.html`, `guide.fr.html`): first song step by step, placing notes,
  mapping tips per difficulty, media formats, playing and sharing, troubleshooting, file format. Linked from the
  editor's home screen and its help.
- Favicon, page description and a preview image for shared links.

### Online editor: music step and video
- New song: one drop zone for the music **or a video clip** (any common format). A clip also becomes the song's
  background: a picture taken from it (read by the browser, so AV1/VP9 clips work too) and the MPEG-1 video, converted
  after the song is created, with progress in the Background step.
- The music is only saved when the song is created: closing the wizard leaves nothing behind (tracks left over by
  earlier versions are cleaned up), and Cancel stops a running conversion.
- Progress bar during conversions; one conversion at a time.
- *Show the video* in the Background step prepares a playable copy of the game's MPEG-1 video for the session, so the
  preview and the sync test also work after reloading the page or for a song opened from a `.zip`.
- AV1 videos (that the browser's converter can't decode) get a clear message instead of "unknown format".

### PC versions
- Windows, macOS (universal .app) and Linux packages with SDL2 built in (`make pc`, `scripts/build-pc.sh`).
- The game finds its data wherever it is launched from: next to the program on Windows and Linux; on macOS, saves
  and the song `import/` folder live in `~/Library/Application Support/Dancing Every Night/`.
- GitHub Actions workflow builds the four packages (Windows, macOS, Linux, Vita) by hand or on a version tag, and
  prepares a draft release.

### Update notice
- On the Vita, the main menu announces a newer version when one exists (the game reads `latest.json` from the website at start-up). Switch it off in OPTIONS > UPDATE CHECK. Nothing is downloaded or installed yet.

### Website
- The website has a home page and a common menu: game download, song library, song editor and creator guide. The editor moved to `/editor/` (your songs are kept).

### Send a song from the editor
- The editor's Publish step can send the finished song straight to the library, with no GitHub account and no zip to download and upload again.

### Licence is optional
- The LICENSE field is no longer required to share a song (editor checklist, export, library submission). A song published without one is only noted in a private maintainer register.

### Difficulty order
- Optional `ORDER <n>` line: the position of the difficulty in its song. Without it, EASY, NORMAL, HARD, EXPERT
  come first ("EASY 2" next to EASY), then other names.
- Song select: with many difficulties, the difficulty pills scroll with the selection (arrows on each side).
- Editor: ↑/↓ buttons in a difficulty's settings change the order (written to every difficulty of the song); a name
  already used in the song becomes "EASY 2", "EASY 3"…; the difficulty bar scrolls instead of wrapping.
- Song checker warns about a duplicate ORDER and about ORDER set on only some difficulties.

### Song creation tools
- Song checker: `vitagame --check <song.zip | folder>` (`make check MAP=...`) validates a song with the game's own
  rules (zip layout, format, ID, sorted notes, Ogg Vorbis audio, JPEG/PNG image within 2048 px, MPEG-1 video,
  Vita performance hints) and returns a non-zero exit code on errors.

- Chart editor runs without the Python server (online version, `make editor-site`): songs kept in the browser
  (IndexedDB), open and export map `.zip` files, MAPPER / LICENSE / SOURCE / VERSION editable; `make editor` keeps
  the local server with media conversion.

### Media in the online editor
- Music in any common format (mp3, wav, m4a, flac, even a video file) is converted to Ogg Vorbis in the browser, and
  videos to MPEG-1 480x272 at 24 fps, with the same settings as `make song` (ffmpeg.wasm, loaded on first use from
  unpkg and cached; its driver is vendored in `tools/editor/vendor/ffmpeg/`). `.ogg` and `.mpg` files are kept as they are.
- The original video stays available during the session for the preview and the sync test.
- BPM detection and the automatic draft work online: the same `tools/mapper.py` runs in the browser with Pyodide
  (Python, numpy and scipy in WebAssembly, loaded from jsDelivr on first use, in a worker), on the track decoded by
  the browser; results match `make song`. `mapper.py` gains `analysis_summary()` / `draft_notes()` on decoded samples.

### Editor look
- The chart editor takes the game's "Cascade pop" identity: charte fonts (Anton, Bricolage Grotesque, M PLUS Rounded,
  served by the editor itself, subset to Latin by `tools/editor_fonts.py`), ink / pink / lemon / cyan colours, pink
  primary buttons with ink text, difficulty pills, SVG icons, recoloured timeline and preview.
- Readability: control borders at least 3:1, larger checkboxes, clearer placeholders.

### Vocabulary
- One vocabulary everywhere (editor, game, checker, docs): a **song** is the folder players install (the music and
  all its difficulties), a **difficulty** is each level (EASY, NORMAL…). The author of the difficulties is shown as
  "Author" (`MAPPER` in the file). Main menu: SONGS.

### Release preparation
- Game name, version, TITLEID (`DNEN00001`) and save folder defined in one place (`CMakeLists.txt`).
- Version number shown on the title screen.
- Save folder moved to `ux0:data/dancingeverynight/`; settings and scores from the previous
  development folder (`ux0:data/vibegame/`) are copied over on first launch.
- `make release` builds a VPK containing only the committed assets (`dist/DancingEveryNight-<version>.vpk`).
- Licences: code under GPLv3, visual identity under CC BY-NC-ND 4.0.
