#!/usr/bin/env python3
"""Assemble les icônes SVG de art/pad_icons/ en un atlas PNG pour le jeu.

    tools/.venv/bin/python tools/svg2atlas.py

Sorties : assets/ui/pad_icons.png (cellules 128x128), pad_icons_64.png et pad_icons_32.png (mêmes icônes,
plus petites : le jeu prend la taille la plus proche de l'affichage). 6 colonnes x 2 lignes.
Ligne 0 = cibles, ligne 1 = notes, ligne 2 = symboles blancs des notes liées ; colonnes dans l'ordre des lanes du jeu
(voir sprites.c) : TRIANGLE, CIRCLE, CROSS, UP, LEFT, DOWN.
"""
import io
from pathlib import Path

import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "art" / "pad_icons"
OUT = ROOT / "assets" / "ui" / "pad_icons.png"
CELL = 128
LEVELS = [128, 64, 32]   # tailles de cellule produites (pad_icons.png, pad_icons_64.png, pad_icons_32.png)
HIRES = 512              # résolution de travail avant réduction
LANES = ["Triangle", "Rond", "Croix", "Haut", "Gauche", "Bas"]   # noms des fichiers SVG
KINDS = ["Cible", "Note", "Blanche"]   # Blanche = symboles des notes en blanc pur (notes liées), tirés des SVG « Note »
# Symboles recentrés sur la cellule : les SVG des notes flèches ne sont pas exactement centrés (ex. « Note Bas »
# une unité plus haut), ce qui se voit dans le disque des notes. Centrage « optique » : à mi-chemin entre le
# centre de la boîte englobante et le centre de masse, car une flèche centrée géométriquement paraît décalée
# vers sa pointe (plus large, donc plus lourde).
# Flèches au trait (2026-10-15) : leur masse est équilibrée, le centre de la boîte englobante suffit (le centre de
# masse les remontait). Les flèches des cibles sont centrées dans leur SVG (pointe en onglet comprise).
RECENTER = {("Note", "Haut"), ("Note", "Gauche"), ("Note", "Bas")}
OPTICAL_WEIGHT = 0.0   # 0 = boîte englobante, 1 = centre de masse


def recenter(img):
    """Décale le contenu pour que son centre optique soit au centre de l'image."""
    alpha = img.getchannel("A")
    box = alpha.getbbox()
    if not box:
        return img
    px, w, h = alpha.load(), img.width, img.height
    total = sx = sy = 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            v = px[x, y]
            total += v; sx += v * (x + 0.5); sy += v * (y + 0.5)
    bx, by = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    cx = bx + (sx / total - bx) * OPTICAL_WEIGHT
    cy = by + (sy / total - by) * OPTICAL_WEIGHT
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (round(w / 2 - cx), round(h / 2 - cy)))
    return out


def main():
    # Rendu à haute résolution (recentrage précis au sous-pixel), puis réduction de qualité vers chaque
    # taille d'atlas. Le jeu choisit la taille la plus proche de l'affichage (comme un mipmap) : une
    # icône réduite plus de 2x par le filtrage bilinéaire devient crénelée.
    cells = {}
    for row, kind in enumerate(KINDS):
        for col, lane in enumerate(LANES):
            src_kind = "Note" if kind == "Blanche" else kind
            svg = SRC / f"{src_kind} {lane}.svg"
            png = resvg_py.svg_to_bytes(svg_path=str(svg), width=HIRES, height=HIRES)
            img = Image.open(io.BytesIO(png)).convert("RGBA")
            if (src_kind, lane) in RECENTER:
                img = recenter(img)
            if kind == "Blanche":   # même forme, blanc pur : le canal alpha est conservé
                img = Image.merge("RGBA", (*[Image.new("L", img.size, 255)] * 3, img.getchannel("A")))
            cells[(row, col)] = img
    OUT.parent.mkdir(parents=True, exist_ok=True)
    for cell in LEVELS:
        atlas = Image.new("RGBA", (cell * len(LANES), cell * len(KINDS)), (0, 0, 0, 0))
        for (row, col), img in cells.items():
            atlas.paste(img.resize((cell, cell), Image.LANCZOS), (col * cell, row * cell))
        out = OUT if cell == CELL else OUT.with_name(f"{OUT.stem}_{cell}.png")
        atlas.save(out, optimize=True)
        print(f"{out.relative_to(ROOT)} : {atlas.width}x{atlas.height}, {out.stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
