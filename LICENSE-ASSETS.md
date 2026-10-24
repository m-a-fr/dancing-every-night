# Asset licence

The **source code** of Dancing Every Night is licensed under the GNU General Public License v3
(see `LICENSE`). The **visual identity** of the game is licensed separately, so that the code can be
reused freely without anyone taking over the game's look and name.

## Visual identity — CC BY-NC-ND 4.0

The following files are licensed under the
[Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International](https://creativecommons.org/licenses/by-nc-nd/4.0/)
licence: you may share them unchanged, with credit, for non-commercial purposes. You may not distribute
modified versions or use them commercially.

- `art/logo/`, `art/accueil_assets/` — logo, title screen layers
- `art/charte/` — style guide
- `art/hostess/FICHE.md` — character sheet of the hostess (the game's mascot)
- `assets/ui/title/`, `assets/ui/splash.jpg` — title screen and boot splash
- `sce_sys/` images — LiveArea and icon
- `assets/songs/*/background.jpg` — the illustrations of the official songs

Forks of the code must replace these files with their own visual identity.

The logo, the hostess and the title screen layers were created with the help of AI image generation
tools, then selected and edited by hand. Depending on the country, such images may be only partly
protected by copyright; this licence applies to the extent that they are.

## Other assets

| Files | Licence |
|---|---|
| `art/fonts/*.ttf` and the bitmap font `assets/ui/font.png` derived from them | SIL Open Font License 1.1 (see `art/fonts/OFL-*.txt`) |
| `art/pad_icons/`, `assets/ui/pad_icons*.png` | GPLv3, like the code |
| `assets/audio/sfx/` (synthesized by `tools/gen_sfx.py`) | CC0 1.0 |
| `assets/audio/bgm/menu.ogg` | CC0 1.0 — see `assets/CREDITS.md` |

## Official songs

`assets/songs/` holds the game's official songs. They are **not** under the GPL nor under CC BY-NC-ND: each song
belongs to its authors and is included under the terms of its label (free rhythm game charts with the song file
included; the song files may not be redistributed on their own). Credits and links are in `assets/CREDITS.md`. The
charts (`*.chart`) are our work, under the GPL like the code, and are only meant to be used with these songs.

Community songs (charts, audio, images, videos) are not part of this repository; each song carries its
own rights, declared by its author in its `LICENSE` field.
