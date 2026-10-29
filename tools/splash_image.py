"""Image de l'écran de démarrage (assets/ui/splash.jpg) : la scène de l'écran titre figée à l'instant 0, sans
l'invitation « PRESS ✕ TO START » (la barre de chargement prend sa place, dessinée par le jeu).

Composée à partir des calques de assets/ui/title/ (générés par tools/title_assets.py), aux positions de
src/title.c : à garder synchronisé si la mise en page du titre change. JPEG : décodage rapide au démarrage.

    tools/.venv/bin/python tools/splash_image.py
"""
from pathlib import Path
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent.parent
T = ROOT / "assets/ui/title"
W, H = 960, 544
FAR_Y, DOTS_Y, NEAR_Y = 126, 364, 259          # title.c : FAR_Y, DOTS_Y, NEAR_Y
FAR_BRIGHT, FAR_ALPHA, NEAR_BRIGHT = 225, 230, 235   # title.c à l'instant 0 (pas de battement)


def layer(name):
    return Image.open(T / name).convert("RGBA")


def tint(im, bright, alpha=255):
    """SDL_SetTextureColorMod(bright) + SDL_SetTextureAlphaMod(alpha)"""
    r, g, b, a = im.split()
    k = bright / 255.0
    r, g, b = (c.point(lambda v: int(v * k)) for c in (r, g, b))
    a = a.point(lambda v: v * alpha // 255)
    return Image.merge("RGBA", (r, g, b, a))


def strip(canvas, im, y):
    """Bande infinie de title.c (draw_strip) au décalage 0 : l'image puis son miroir, répétés"""
    x, i = 0, 0
    while x < W:
        canvas.alpha_composite(ImageOps.mirror(im) if i & 1 else im, (x, y))
        x += im.width
        i += 1


def main():
    c = Image.new("RGBA", (W, H), (14, 8, 36, 255))
    c.alpha_composite(layer("facets.png").resize((W, H)))
    beam = layer("beam.png")
    c.alpha_composite(beam, (480 - beam.width // 2, -40))       # faisceau vertical à l'instant 0
    strip(c, tint(layer("far.png"), FAR_BRIGHT, FAR_ALPHA), FAR_Y)
    c.alpha_composite(layer("dots.png"), (0, DOTS_Y))
    strip(c, tint(layer("near.png"), NEAR_BRIGHT), NEAR_Y)
    c.alpha_composite(layer("vignette.png").resize((W, H), Image.BILINEAR))
    logo = layer("logo.png")
    c.alpha_composite(logo, ((W - logo.width) // 2, (H - logo.height) // 2))
    out = ROOT / "assets/ui/splash.jpg"
    c.convert("RGB").save(out, quality=88, optimize=True)
    print(f"{out.relative_to(ROOT)} ({out.stat().st_size // 1024} Ko)")


if __name__ == "__main__":
    main()
