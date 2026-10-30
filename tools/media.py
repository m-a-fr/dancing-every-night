#!/usr/bin/env python3
"""Conversion des médias de fond pour le jeu (utilisé par l'éditeur de cartes).

    tools/.venv/bin/python tools/media.py image entree.png sortie.jpg
    python3 tools/media.py video entree.mp4 sortie.mpg|sortie.h264 [--fps 24] [--kbps 900] [--size 480x272]

image : 960x544 en JPEG (Pillow requis). --fill auto (défaut) : recadrage « cover » si l'image
        couvre bien l'écran, sinon effet « ambilight » : image entière au centre, bords vides
        remplis par la même image agrandie, floutée et assombrie. --fill cover / blur pour forcer.
video : ffmpeg -> MPEG-1 sans son (.mpg), décodé par le jeu avec pl_mpeg (src/video.c).
        Taille ≈ débit x durée (900 kb/s : ~20 Mo pour 3 min). Une image clé par seconde
        (le jeu s'y cale pour les sauts). ffmpeg requis (brew install ffmpeg). JSON sur stdout.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

SCREEN = (960, 544)


# Mode auto : ambilight si le recadrage « cover » devrait agrandir l'image (trop petite)
# ou couper plus de MAX_CROP de sa surface (format trop différent de l'écran, ex. portrait)
MAX_CROP = 0.15


def ambilight(im):
    """Image entière centrée (« contain »), vides remplis par un fond flou tiré de l'image."""
    from PIL import Image, ImageEnhance, ImageFilter, ImageOps
    W, H = SCREEN
    # fond : « cover » flouté ; le flou se fait en petit (rapide, très doux) puis est ré-agrandi
    small = ImageOps.fit(im, (W // 8, H // 8), method=Image.BILINEAR).filter(ImageFilter.GaussianBlur(4))
    bg = small.resize(SCREEN, Image.BICUBIC).filter(ImageFilter.GaussianBlur(6))
    bg = ImageEnhance.Color(bg).enhance(1.25)
    bg = ImageEnhance.Brightness(bg).enhance(0.7)
    fg = ImageOps.contain(im, SCREEN, method=Image.LANCZOS)
    x, y = (W - fg.width) // 2, (H - fg.height) // 2
    # ombre douce sous l'image pour la détacher du fond
    shadow = Image.new("L", SCREEN, 0)
    shadow.paste(150, (x, y, x + fg.width, y + fg.height))
    shadow = shadow.filter(ImageFilter.GaussianBlur(12))
    bg = Image.composite(Image.new("RGB", SCREEN, (0, 0, 0)), bg, shadow)
    bg.paste(fg, (x, y))
    return bg


def convert_image(src, dst, fill="auto"):
    from PIL import Image, ImageOps
    with Image.open(src) as im:
        im = ImageOps.exif_transpose(im).convert("RGB")
        w, h = im.size
        cover = max(SCREEN[0] / w, SCREEN[1] / h)
        crop = 1.0 - (SCREEN[0] * SCREEN[1]) / (w * cover * h * cover)
        if fill == "auto":
            fill = "blur" if cover > 1.0 or crop > MAX_CROP else "cover"
        out = ambilight(im) if fill == "blur" else ImageOps.fit(im, SCREEN, method=Image.LANCZOS)
        out.save(dst, "JPEG", quality=85, optimize=True)
    return {"width": SCREEN[0], "height": SCREEN[1], "bytes": Path(dst).stat().st_size,
            "fill": fill, "source": [w, h]}


DEFAULT_KBPS = 900  # débit moyen par défaut de tous les outils (import, éditeur, ligne de commande)


DEFAULT_KBPS_H264 = 720  # H.264 Baseline : même qualité que le MPEG-1 à 900 kb/s pour ~20 % de poids en moins (essais sur console et PC 2026-10-30 ; 450 était visiblement moins bon)


def h264_cmd(ffmpeg, src, dst, vf, fps, kbps):
    # Flux brut Annex B, profil BASELINE (le seul que lit le décodeur logiciel des PC, h264bsd ; la Vita le lit aussi
    # par son décodeur matériel) : niveau 3.0, sans B-frames, image clé toutes les secondes (pas de détection de scène :
    # le jeu saute d'image clé en image clé), une tranche par image, SPS/PPS répétés devant chaque image clé (src/h264idx.c).
    return [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", str(src), "-an", "-vf", vf,
            "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.0", "-preset", "veryslow",
            "-b:v", f"{kbps}k", "-maxrate", f"{kbps * 2}k", "-bufsize", "3M",
            "-x264-params", f"keyint={fps}:min-keyint={fps}:scenecut=0:bframes=0:ref=3:repeat-headers=1:slices=1",
            "-f", "h264", str(dst)]


def convert_video(src, dst, fps, size, kbps):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg introuvable : installe-le avec « brew install ffmpeg »")
    w, h = size
    vf = f"fps={fps},scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h}"
    # Débit moyen = kbps, pointes jusqu'à 2x (scènes difficiles : transitions, flashs) ; optimisation
    # débit-distorsion contre les « gros carrés » du MPEG-1. Pas de B-frames : GOP ouvert = seek corrompu
    # dans pl_mpeg, et sans eux la qualité mesurée est la même (SSIM 0,922 contre 0,921).
    rate, peak = f"{kbps}k", f"{kbps * 2}k"
    if str(dst).endswith(".h264"):
        cmd = h264_cmd(ffmpeg, src, dst, vf, fps, kbps)
    else:
        cmd = [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", str(src), "-an", "-vf", vf,
               "-c:v", "mpeg1video", "-b:v", rate, "-maxrate", peak, "-bufsize", "3M",
               "-g", str(fps), "-bf", "0", "-mbd", "rd", "-trellis", "1",
               "-cmp", "2", "-subcmp", "2", "-mbcmp", "2",
               "-muxrate", "20M", "-f", "mpeg", str(dst)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("ffmpeg a échoué : " + (r.stderr.strip().splitlines() or ["?"])[-1][:200])
    size_b = Path(dst).stat().st_size
    if size_b < 1024:
        raise RuntimeError("aucune image produite : fichier vidéo illisible ?")
    return {"fps": fps, "kbps": kbps, "width": w, "height": h, "bytes": size_b}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="kind", required=True)
    a = sub.add_parser("image"); a.add_argument("src"); a.add_argument("dst")
    a.add_argument("--fill", choices=["auto", "cover", "blur"], default="auto")
    v = sub.add_parser("video"); v.add_argument("src"); v.add_argument("dst")
    v.add_argument("--fps", type=int, default=24)
    v.add_argument("--kbps", type=int, default=None, help=f"défaut {DEFAULT_KBPS} (.mpg) ou {DEFAULT_KBPS_H264} (.h264)")
    v.add_argument("--size", default="480x272")
    args = ap.parse_args()
    try:
        if args.kind == "image":
            out = convert_image(args.src, args.dst, args.fill)
        else:
            kbps = args.kbps or (DEFAULT_KBPS_H264 if args.dst.endswith(".h264") else DEFAULT_KBPS)
            out = convert_video(args.src, args.dst, args.fps, tuple(int(x) for x in args.size.split("x")), kbps)
    except Exception as e:  # message lisible pour l'éditeur
        print(json.dumps({"error": str(e)}))
        sys.exit(1)
    print(json.dumps(out))


if __name__ == "__main__":
    main()
