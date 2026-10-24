#!/usr/bin/env python3
"""Polices de l'éditeur (charte « Cascade pop », art/fonts/) réduites au latin et converties en WOFF2.

    tools/.venv/bin/python tools/editor_fonts.py

Écrit tools/editor/fonts/*.woff2 (commitées : l'éditeur en ligne les sert lui-même, sans Google Fonts).
À relancer seulement si les polices de art/fonts/ changent. Nécessite fonttools et brotli (tools/requirements.txt).
"""
from pathlib import Path

from fontTools import subset

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "tools" / "editor" / "fonts"
FONTS = {   # source -> nom publié
    "Anton-Regular.ttf": "anton.woff2",
    "BricolageGrotesque[opsz,wdth,wght].ttf": "bricolage.woff2",
    "MPLUSRounded1c-ExtraBold.ttf": "mplus-rounded-800.woff2",
}
# ASCII + Latin-1 + ponctuation française et symboles de l'interface (… « » – — ’ € ✓ etc.)
UNICODES = "U+0020-007E,U+00A0-00FF,U+0152-0153,U+0178,U+2013-2014,U+2018-201E,U+2022,U+2026,U+2039-203A,U+20AC,U+2190-2193,U+2212,U+2713"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for src, dst in FONTS.items():
        opts = subset.Options()
        opts.flavor = "woff2"
        opts.layout_features = ["*"]
        opts.name_IDs = ["*"]
        opts.notdef_outline = True
        font = subset.load_font(str(ROOT / "art" / "fonts" / src), opts)
        sub = subset.Subsetter(opts)
        sub.populate(unicodes=subset.parse_unicodes(UNICODES))
        sub.subset(font)
        subset.save_font(font, str(OUT / dst), opts)
        print(f"{dst}: {(OUT / dst).stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
