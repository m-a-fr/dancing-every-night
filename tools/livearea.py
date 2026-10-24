"""Compose l'icône, le fond LiveArea et l'image de démarrage à partir des calques de l'écran d'accueil.
Sortie : PNG 8 bits indexés (exigence de la Vita pour sce_sys)."""
import math
import sys
from PIL import Image, ImageDraw

A, OUT = sys.argv[1], sys.argv[2]       # dossier des calques, dossier de sortie
PAL = [(59, 31, 168), (106, 63, 209), (154, 124, 240), (201, 182, 255), (69, 200, 232), (46, 143, 214), (255, 46, 138), (208, 75, 200)]


def h(a, b):
    x = math.sin(a * 127.1 + b * 311.7) * 43758.5453
    return x - math.floor(x)


def facets(w, h_, s=58):
    """Fond de facettes de vitrail, comme le canvas de l'écran d'accueil (état au repos)."""
    img = Image.new("RGB", (w, h_), (14, 8, 36))
    grad = Image.radial_gradient("L").resize((w, h_))
    base = Image.composite(Image.new("RGB", (w, h_), (14, 8, 36)), Image.new("RGB", (w, h_), (42, 22, 112)), grad)
    img.paste(base)
    over = Image.new("RGBA", (w, h_))
    d = ImageDraw.Draw(over)
    hh = s * .866
    for r in range(-1, int(h_ / hh) + 2):
        for c in range(-1, int(w / s) + 3):
            x0 = c * s + (r % 2) * s / 2
            y0 = r * hh
            for k, tri in enumerate(([(x0, y0), (x0 + s, y0), (x0 + s / 2, y0 + hh)], [(x0 + s, y0), (x0 + s * 1.5, y0 + hh), (x0 + s / 2, y0 + hh)])):
                cx = sum(p[0] for p in tri) / 3; cy = sum(p[1] for p in tri) / 3
                dist = math.hypot(cx - w / 2, (cy - h_ * .46) * 1.3) / (w * .62)
                a = max(0, min(1, .16 + (1 - dist) * .32))
                col = PAL[int(h(c * 2 + k, r) * len(PAL))]
                d.polygon(tri, fill=col + (int(a * 255),), outline=(14, 8, 36, 220), width=2)
    img = img.convert("RGBA"); img.alpha_composite(over)
    return img


def layer(name, height=None, width=None):
    im = Image.open(f"{A}/{name}").convert("RGBA")
    if height: return im.resize((round(im.width * height / im.height), height), Image.LANCZOS)
    return im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)


def scene(w=960, h_=544):
    """L'écran d'accueil au repos, 960 x 544."""
    img = facets(w, h_)
    for name, bottom, sw in (("ville_lointaine_bande.png", 70, 2000), ("ville_proche_bande.png", -6, 2600)):
        strip = Image.open(f"{A}/{name}").convert("RGBA")
        sh = round(strip.height * sw / strip.width); strip = strip.resize((sw, sh), Image.LANCZOS)
        img.alpha_composite(strip.crop((0, 0, w, sh)), (0, h_ - sh - bottom) if bottom >= 0 else (0, h_ - sh + 6))
    vig = Image.radial_gradient("L").resize((w, h_)).point(lambda v: int(max(0, v - 90) * 1.1))
    img = Image.composite(Image.new("RGBA", (w, h_), (14, 8, 36, 255)), img, vig)
    return img


def logo(img, cx, top, scale):
    ball = layer("logo_boule.png", width=round(440 * scale)); girl = layer("logo_hotesse.png", height=round(420 * scale)); title = layer("logo_titre.png", width=round(470 * scale))
    img.alpha_composite(ball, (round(cx - 200 * scale), round(top + 8 * scale)))
    img.alpha_composite(girl, (round(cx - 210 * scale), round(top + 4 * scale)))
    img.alpha_composite(title, (round(cx - 235 * scale), round(top + 232 * scale)))
    return img


def to8(im, path):
    im.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)
    print(path, Image.open(path).mode, Image.open(path).size)


def bg_A():
    """Porte au centre (style a1) : l'hôtesse et la boule à droite de la porte, le titre est dans la porte."""
    img = scene()
    s = 1.12
    ball = layer("logo_boule.png", width=round(440 * s)); girl = layer("logo_hotesse.png", height=round(420 * s))
    img.alpha_composite(ball, (640, 70)); img.alpha_composite(girl, (612, 64))
    return img.resize((882, 500), Image.LANCZOS).crop((21, 0, 861, 500))


def bg_C():
    """Porte au centre, décor dans les espaces libres : hôtesse à gauche, titre en bas, ville et étoiles à droite.
    Coordonnées du fond 840 x 500 ; la porte occupe x 277..563, y 135..295, les boutons du haut x 210..630, y 0..66."""
    img = scene().resize((882, 500), Image.LANCZOS).crop((21, 0, 861, 500))
    girl = layer("logo_hotesse.png", height=500); img.alpha_composite(girl, (-90, 0))   # main dégagée de la porte
    title = layer("logo_titre.png", width=500); img.alpha_composite(title, (340, 500 - title.height + 6))
    d = ImageDraw.Draw(img)
    for x, y, r in ((640, 120, 18), (760, 210, 12), (700, 60, 9), (800, 110, 15), (610, 260, 10)):   # étoiles dorées à droite
        pts = [(x + math.cos(i * math.pi / 4 - math.pi / 2) * (r if i % 2 == 0 else r * .3), y + math.sin(i * math.pi / 4 - math.pi / 2) * (r if i % 2 == 0 else r * .3)) for i in range(8)]
        d.polygon(pts, fill=(255, 225, 74, 255))
    return img


def startup_C():
    """Porte : la boule disco sur les facettes."""
    st = facets(280, 158, s=34)
    b = layer("logo_boule.png", height=150); st.alpha_composite(b, ((280 - b.width) // 2, 4))
    return st


def bg_D():
    """Porte au centre, entourée par la boule disco ; l'hôtesse à gauche ; le titre est dans la porte.
    La porte occupe x 277..563, y 135..295 du fond (centre 420, 215)."""
    img = scene().resize((882, 500), Image.LANCZOS).crop((21, 0, 861, 500))
    ball = layer("logo_boule.png", width=470)
    img.alpha_composite(ball, (420 - ball.width // 2 + 18, 215 - ball.height // 2 + 6))
    girl = layer("logo_hotesse.png", height=500); img.alpha_composite(girl, (-150, 0))
    d = ImageDraw.Draw(img)
    for x, y, r in ((700, 360, 16), (790, 300, 11), (760, 420, 13), (650, 450, 9), (810, 180, 12)):
        pts = [(x + math.cos(i * math.pi / 4 - math.pi / 2) * (r if i % 2 == 0 else r * .3), y + math.sin(i * math.pi / 4 - math.pi / 2) * (r if i % 2 == 0 else r * .3)) for i in range(8)]
        d.polygon(pts, fill=(255, 225, 74, 255))
    return img


def bg_B():
    """Porte à droite (style psmobile) : le logo complet à gauche."""
    img = scene()
    return logo(img, 250, 40, 1.0).resize((882, 500), Image.LANCZOS).crop((21, 0, 861, 500))


if __name__ == "__main__":
    mode = sys.argv[3] if len(sys.argv) > 3 else "D"   # D : mise en page retenue (boule autour de la porte)
    to8({"A": bg_A, "B": bg_B, "C": bg_C, "D": bg_D}[mode](), f"{OUT}/bg_{mode}.png" if len(sys.argv) > 3 else f"{OUT}/bg.png")
    if mode == "C": to8(startup_C(), f"{OUT}/startup_C.png")
    # Image de démarrage 280 x 158 : le bandeau-titre sur les facettes
    st = facets(280, 158, s=34)
    t = layer("logo_titre.png", width=250); st.alpha_composite(t, ((280 - t.width) // 2, (158 - t.height) // 2))
    to8(st, f"{OUT}/startup.png")
    # Icône 128 x 128 (affichée dans une bulle ronde) : la boule et le visage de l'hôtesse
    ic = facets(256, 256, s=40)
    ball = layer("logo_boule.png", width=300); ic.alpha_composite(ball, (-10, 10))
    girl = layer("logo_hotesse.png", height=540); ic.alpha_composite(girl, (-150, -48))   # visage centré dans la bulle ronde
    to8(ic.resize((128, 128), Image.LANCZOS), f"{OUT}/icon0.png")
