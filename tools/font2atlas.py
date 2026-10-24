#!/usr/bin/env python3
"""Rastérise les polices de art/fonts/ en un atlas PNG + un fichier de mesures pour le jeu.

    tools/.venv/bin/python tools/font2atlas.py

Sorties :
  assets/ui/font.png  atlas blanc (alpha = couverture) : glyphes nets + halos flous à mi-résolution
  assets/ui/font.txt  mesures lues par src/font.c

Chaque « face » (police + taille) correspond à une valeur de l'enum FontFace de src/font.h,
dans le même ordre. Pour changer une taille ou une police : modifier FACES puis relancer.

Format de font.txt :
  ATLAS <largeur> <hauteur>
  FACE <index> <hauteur des capitales> <interligne> <halo 0/1> <contour à mi-résolution 0/1> <contour 0/1>
       <creux 0/1>
  G <codepoint> <x> <y> <w> <h> <xoff> <yoff> <avance> <gx> <gy> <gw> <gh> <gxoff> <gyoff>
    <ox> <oy> <ow> <oh> <oxoff> <oyoff>          (sur la même ligne)
Trois couches par glyphe : nette, halo (g*) et contour (o*, glyphe épaissi). Faces néon : halo + contour
sombre fin entre le halo et la lettre. Faces « Cascade pop » : contour d'encre épais, sans halo. Absentes : 0.
Faces « creuses » (HOLLOW) : pas de halo, sa place (g*) accueille un anneau de 2 px à cheval sur le bord de la
lettre, à pleine résolution : titres « en contour » de la cascade, intérieur transparent.
Les décalages sont relatifs au crayon : x = position courante, y = haut des capitales.
Les coordonnées du halo sont à mi-résolution (à dessiner à 2x). Codepoints : ASCII, Latin-1,
et quelques caractères de Windows-1252 rangés sur 0x80..0x9F (’ “ ” – — … •).
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "art" / "fonts"
OUT_PNG = ROOT / "assets" / "ui" / "font.png"
OUT_TXT = ROOT / "assets" / "ui" / "font.txt"

ASCII = "".join(chr(c) for c in range(32, 127))
LATIN1 = "".join(chr(c) for c in range(0xA0, 0x100))
CP1252 = {0x80: "€", 0x85: "…", 0x91: "‘", 0x92: "’", 0x93: "“", 0x94: "”", 0x95: "•", 0x96: "–", 0x97: "—"}
FULL = ASCII + LATIN1 + "".join(CP1252.values())
DIGITS = " 0123456789SABCDGO!+x%"

# Titres en capitales seulement (le jeu passe par ui_upper) : pas de minuscules, le contour pleine résolution coûte cher
UPPER = "".join(c for c in FULL if not c.islower())
ORB = "Orbitron[wght].ttf"
ANTON = "Anton-Regular.ttf"
BRICO = "BricolageGrotesque[opsz,wdth,wght].ttf"
ROUND = "MPLUSRounded1c-ExtraBold.ttf"
# (nom dans font.h, fichier, variante, taille px, halo, caractères, contour d'encre en px)
#   variante : nom d'instance (str) ou valeurs des axes dans l'ordre de la police (liste)
#   halo : couches halo + contour sombre fin (style néon) ; contour d'encre : couche contour épaisse
#   sans halo (style « Cascade pop », charte du 15/10/2026), 0 = aucun
FACES = [
    ("FONT_SMALL",       "Rajdhani-SemiBold.ttf", None,    22, False, FULL, 0),
    ("FONT_BODY",        "Rajdhani-SemiBold.ttf", None,    28, False, FULL, 0),
    ("FONT_BODY_BOLD",   "Rajdhani-Bold.ttf",     None,    34, False, FULL, 0),
    ("FONT_TITLE_SMALL", ORB,                     "Bold",  28, True,  FULL, 0),
    ("FONT_TITLE",       ORB,                     "Black", 38, True,  FULL, 0),
    ("FONT_DISPLAY",     ORB,                     "Black", 56, True,  ASCII, 0),
    ("FONT_HUGE",        ORB,                     "Black", 128, True, DIGITS, 0),
    ("FONT_POP_XL",      ANTON,                   None,    66, False, UPPER, 6),
    ("FONT_POP",         ANTON,                   None,    38, False, FULL, 3),
    ("FONT_CARD_NAME",   BRICO,                   [28, 800, 100], 28, False, FULL, 0),
    ("FONT_CARD",        BRICO,                   [16, 600, 100], 16, False, FULL, 0),
    ("FONT_CARD_BOLD",   BRICO,                   [16, 800, 100], 15, False, FULL, 0),
    ("FONT_LABEL",       ROUND,                   None,    14, False, FULL, 0),
    ("FONT_POP_S",       ANTON,                   None,    16, False, FULL, 0),
    ("FONT_POP_HUGE",    ANTON,                   None,   148, False, " 0123456789SABCD?", 7),
]
HOLLOW = {"FONT_POP"}   # faces avec la couche creuse (rangée à la place du halo)
ATLAS_W = 2048
HALF_OUTLINE_FROM = 56  # faces néon seulement : à partir de cette taille, contour stocké à mi-résolution (VRAM).
# Les faces « Cascade pop » gardent leur contour d'encre en pleine résolution : agrandi 2x, il devenait flou (2026-10-19).
GAP = 1  # pixels vides entre cellules (évite le débordement du filtrage bilinéaire)


def load_font(file, variation, size):
    f = ImageFont.truetype(str(SRC / file), size)
    if isinstance(variation, list):
        f.set_variation_by_axes(variation)
    elif variation:
        f.set_variation_by_name(variation)
    return f


def glyph_mask(font, ch, pad, stroke=0):
    """Masque L du glyphe (épaissi de `stroke` px) + (xoff, yoff) par rapport au crayon posé sur la ligne de base."""
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    pad += stroke
    w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    img = Image.new("L", (max(w, 1), max(h, 1)), 0)
    ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls",
                             stroke_width=stroke, stroke_fill=255)
    return img, x0 - pad, y0 - pad


def shelf_pack(sizes, width):
    """Rangement en étagères, plus hautes d'abord. Retourne positions et hauteur totale."""
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    pos, x, y, shelf = [None] * len(sizes), 0, 0, 0
    for i in order:
        w, h = sizes[i]
        if x + w > width:
            x, y, shelf = 0, y + shelf + GAP, 0
        pos[i] = (x, y)
        x += w + GAP
        shelf = max(shelf, h)
    return pos, y + shelf


def main():
    items = []   # (face, codepoint, image, xoff, yoff, advance, couche : sharp / glow / outline)
    face_lines = []
    for index, (name, file, var, size, glow, chars, ink) in enumerate(FACES):
        font = load_font(file, var, size)
        notdef = glyph_mask(font, "\ue000", 0)[0].tobytes()
        cap_top = font.getbbox("H", anchor="ls")[1]      # négatif : au-dessus de la ligne de base
        cap_h = -cap_top
        ascent, descent = font.getmetrics()
        has_outline = glow or ink > 0
        hollow = name in HOLLOW and not glow
        half_outline = glow and size >= HALF_OUTLINE_FROM
        face_lines.append((index, cap_h, ascent + descent, int(glow), int(half_outline), int(has_outline), int(hollow)))
        radius = max(3.0, size * 0.10)
        gpad = int(radius * 2.5) + 2
        stroke = ink if ink > 0 else max(2, round(size * 0.05))   # épaisseur du contour
        count = 0
        for ch in dict.fromkeys(chars):
            if ch != " " and glyph_mask(font, ch, 0)[0].tobytes() == notdef:
                continue  # absent de la police : le jeu affichera '?'
            cp = ord(ch)
            if cp > 0xFF:
                cp = next(k for k, v in CP1252.items() if v == ch)
            adv = round(font.getlength(ch))
            if ch == " ":
                items.append((index, cp, None, 0, 0, adv, "sharp"))
                continue
            img, xo, yo = glyph_mask(font, ch, 1)
            items.append((index, cp, img, xo, yo - cap_top, adv, "sharp"))
            if glow:
                gimg, gxo, gyo = glyph_mask(font, ch, gpad)
                g = gimg.filter(ImageFilter.GaussianBlur(radius))
                g = g.point(lambda v: min(255, int(v * 1.8)))
                g = g.resize(((g.width + 1) // 2, (g.height + 1) // 2), Image.LANCZOS)
                items.append((index, cp, g, gxo, gyo - cap_top, adv, "glow"))
            if has_outline:
                oimg, oxo, oyo = glyph_mask(font, ch, 1, stroke)
                if half_outline:
                    oimg = oimg.resize(((oimg.width + 1) // 2, (oimg.height + 1) // 2), Image.LANCZOS)
                items.append((index, cp, oimg, oxo, oyo - cap_top, adv, "outline"))
            if hollow:   # anneau : lettre épaissie de 1 px moins lettre amincie de 1 px
                fat, rxo, ryo = glyph_mask(font, ch, 2, 1)
                thin, _, _ = glyph_mask(font, ch, 3)
                thin = thin.filter(ImageFilter.MinFilter(3))
                ring = ImageChops.multiply(fat, ImageOps.invert(thin))   # bords intérieur et extérieur lissés
                items.append((index, cp, ring, rxo, ryo - cap_top, adv, "glow"))
            count += 1
        extra = '  + halo' if glow else f'  + contour {ink}px' if ink else ''
        extra += '  + creux' if hollow else ''
        print(f"  {name:17s} {file[:22]:22s} {size:4d}px  {count} glyphes{extra}")

    sizes = [(it[2].width, it[2].height) if it[2] else (0, 0) for it in items]
    pos, height = shelf_pack(sizes, ATLAS_W)
    atlas_h = (height + 63) // 64 * 64   # pas de puissance de 2 en hauteur : économise la VRAM
    alpha = Image.new("L", (ATLAS_W, atlas_h), 0)
    for (face, cp, img, *_), p in zip(items, pos):
        if img:
            alpha.paste(img, p)
    atlas = Image.merge("RGBA", (*[Image.new("L", alpha.size, 255)] * 3, alpha))
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(OUT_PNG, optimize=True)

    # Regroupe glyphe net + halo sur une seule ligne G
    glyphs = {}
    empty = (0, 0, 0, 0, 0, 0)
    for (face, cp, img, xo, yo, adv, layer), (x, y) in zip(items, pos):
        w, h = (img.width, img.height) if img else (0, 0)
        g = glyphs.setdefault((face, cp), {"sharp": empty, "glow": empty, "outline": empty, "adv": adv})
        g[layer] = (x, y, w, h, xo, yo)
    with open(OUT_TXT, "w") as f:
        f.write(f"ATLAS {ATLAS_W} {atlas_h}\n")
        for index, cap_h, line_h, glow, half_outline, has_outline, hollow in face_lines:
            f.write(f"FACE {index} {cap_h} {line_h} {glow} {half_outline} {has_outline} {hollow}\n")
            for (face, cp), g in sorted(glyphs.items()):
                if face != index:
                    continue
                x, y, w, h, xo, yo = g["sharp"]
                gl = " ".join(map(str, g["glow"] + g["outline"]))
                f.write(f"G {cp} {x} {y} {w} {h} {xo} {yo} {g['adv']} {gl}\n")
    print(f"{OUT_PNG.relative_to(ROOT)} : {ATLAS_W}x{atlas_h} (rempli jusqu'à {height}), "
          f"{OUT_PNG.stat().st_size // 1024} Ko ; {OUT_TXT.relative_to(ROOT)} : {len(glyphs)} glyphes")


if __name__ == "__main__":
    main()
