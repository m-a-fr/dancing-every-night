#!/usr/bin/env python3
"""Prépare les images de l'écran d'accueil à partir de art/accueil_assets/ (maquette validée le 15/10/2026).

    tools/.venv/bin/python tools/title_assets.py

Sorties dans assets/ui/title/ (tailles d'affichage, 1 texel = 1 px écran) :
  facets.png  fond fixe 960x544 : dégradé + facettes de vitrail cernées d'encre (ne défile pas)
  far.png     demi-bande de la ville lointaine (dessinée normale puis retournée : raccord sans couture)
  near.png    demi-bande de la ville proche, idem
  dots.png    bande de trame rose 972x200, motif de 12 px (défile par pas de 12 px)
  vignette.png  voile radial, petite image agrandie (dégradé doux)
  ball.png / girl.png / logo.png   les trois couches du logo
  tri.png     triangle blanc (scintillement des facettes, en additif)
  beam.png    faisceau lumineux (tourne autour de son sommet)
  spark.png   étoile à quatre branches blanche (teintée en or par le jeu)
Les facettes suivent le même découpage que title.c (FACET_SIZE) pour que le scintillement tombe juste.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "art" / "accueil_assets"
OUT = ROOT / "assets" / "ui" / "title"
W, H = 960, 544
FACET = 58                      # = FACET_SIZE dans src/title.c
PAL = [(59, 31, 168), (106, 63, 209), (154, 124, 240), (201, 182, 255),
       (69, 200, 232), (46, 143, 214), (255, 46, 138), (208, 75, 200)]


def h(a, b):
    x = math.sin(a * 127.1 + b * 311.7) * 43758.5453
    return x - math.floor(x)


def facets():
    img = Image.new("RGB", (W, H), (14, 8, 36))
    g = Image.radial_gradient("L").resize((W, H))
    img = Image.composite(Image.new("RGB", (W, H), (14, 8, 36)), Image.new("RGB", (W, H), (42, 22, 112)), g)
    over = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(over)
    hh = FACET * .866
    for r in range(-1, int(H / hh) + 2):
        for c in range(-1, int(W / FACET) + 3):
            x0 = c * FACET + (r % 2) * FACET / 2
            y0 = r * hh
            for k, tri in enumerate(([(x0, y0), (x0 + FACET, y0), (x0 + FACET / 2, y0 + hh)],
                                     [(x0 + FACET, y0), (x0 + FACET * 1.5, y0 + hh), (x0 + FACET / 2, y0 + hh)])):
                cx = sum(p[0] for p in tri) / 3
                cy = sum(p[1] for p in tri) / 3
                dist = math.hypot(cx - 480, (cy - 250) * 1.3) / 600
                a = max(0.0, min(1.0, .14 + (1 - dist) * .3))
                col = PAL[int(h(c * 2 + k, r) * len(PAL))]
                d.polygon(tri, fill=col + (int(a * 255),))
                d.line(tri + [tri[0]], fill=(14, 8, 36, 220), width=2)
    img = img.convert("RGBA")
    img.alpha_composite(over)
    return img.convert("RGB")


def city(name):
    im = Image.open(SRC / name).convert("RGBA")
    return im.crop(im.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox())


def fit(im, width=None, height=None):
    if width:
        return im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    return im.resize((round(im.width * height / im.height), height), Image.LANCZOS)


def dots():
    band = Image.new("RGBA", (972, 200))
    d = ImageDraw.Draw(band)
    for y in range(6, 200, 12):
        for x in range(6, 972, 12):
            d.ellipse((x - 2, y - 2, x + 2, y + 2), fill=(255, 46, 138, 153))
    mask = Image.linear_gradient("L").resize((972, 200)).point(lambda v: min(255, int(v / .85)))
    a = band.getchannel("A").point(lambda v: v)
    band.putalpha(Image.composite(a, Image.new("L", a.size, 0), mask))
    return band


def vignette():
    g = Image.radial_gradient("L").resize((240, 136))           # 0 au centre -> 255 au bord
    a = g.point(lambda v: int(max(0, (v / 255 - .35) / .65) * .7 * 255))
    v = Image.new("RGBA", g.size, (14, 8, 36, 0))
    v.putalpha(a)
    return v


def tri():
    t = Image.new("L", (64, 56), 0)
    ImageDraw.Draw(t).polygon([(0, 0), (64, 0), (32, 56)], fill=255)
    return Image.merge("RGBA", (Image.new("L", t.size, 255),) * 3 + (t,))


def beam():
    b = Image.new("L", (360, 680), 0)
    ImageDraw.Draw(b).polygon([(150, 0), (210, 0), (360, 680), (0, 680)], fill=255)
    fade = Image.linear_gradient("L").resize((360, 680)).point(lambda v: int((255 - v) * .2))
    alpha = Image.composite(fade, Image.new("L", b.size, 0), b).filter(ImageFilter.GaussianBlur(6))
    return Image.merge("RGBA", (Image.new("L", b.size, 255),) * 3 + (alpha,))


def spark():
    s = Image.new("L", (64, 64), 0)
    pts = [(32 + math.cos(i * math.pi / 4 - math.pi / 2) * (32 if i % 2 == 0 else 7.7),
            32 + math.sin(i * math.pi / 4 - math.pi / 2) * (32 if i % 2 == 0 else 7.7)) for i in range(8)]
    ImageDraw.Draw(s).polygon(pts, fill=255)
    return Image.merge("RGBA", (Image.new("L", s.size, 255),) * 3 + (s,))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    out = {
        "facets.png": facets(),
        "far.png": fit(city("ville_lointaine_propre.webp"), width=1000),
        "near.png": fit(city("ville_proche_propre.webp"), width=1300),
        "dots.png": dots(),
        "vignette.png": vignette(),
        "ball.png": fit(Image.open(SRC / "logo_boule.png").convert("RGBA"), width=440),
        "girl.png": fit(Image.open(SRC / "logo_hotesse.png").convert("RGBA"), height=420),
        "logo.png": fit(Image.open(SRC / "logo_titre.png").convert("RGBA"), width=470),
        "tri.png": tri(),
        "beam.png": beam(),
        "spark.png": spark(),
    }
    for name, im in out.items():
        im.save(OUT / name, optimize=True)
        print(f"  {name:13s} {im.width}x{im.height}  {(OUT / name).stat().st_size // 1024} Ko")


if __name__ == "__main__":
    main()
