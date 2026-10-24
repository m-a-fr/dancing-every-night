#!/usr/bin/env python3
"""Mesure le trajet des notes dans une vidéo de gameplay du jeu de référence (étude de référence).

    tools/.venv/bin/python tools/analyze_gameplay_video.py art/refs/video.mp4 [--audio art/refs/audio.m4a]

Ne regarde que la bande horizontale du milieu de l'écran (voies Rond, à droite, et Gauche, à
gauche), où les notes se déplacent en ligne droite. Détecte les anneaux bleus des notes
(teinte ~205°), les suit d'une image à l'autre, et mesure : point d'apparition, vitesse, temps de
trajet jusqu'à la cible, et taille en fonction de la distance. Ne modifie rien dans le jeu.
Nécessite ffmpeg. Les positions de la mise en page (cibles) sont réglables en option.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage

W, H = 1280, 720            # résolution attendue de la vidéo
BAND_Y, BAND_H = 325, 70    # bande du milieu
CENTER_X = W / 2
DEFAULT_RIGHT_TARGET_X = 1190.0
DEFAULT_LEFT_TARGET_X = 85.0


def frames(path):
    cmd = ["ffmpeg", "-v", "error", "-i", str(path), "-vf", f"crop={W}:{BAND_H}:0:{BAND_Y}",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = W * BAND_H * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, np.uint8).reshape(BAND_H, W, 3)
    p.wait()


def note_mask(img):
    a = img.astype(np.float32) / 255.0
    mx, mn = a.max(2), a.min(2)
    d = mx - mn + 1e-9
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    s = d / (mx + 1e-9)
    return (h > 193) & (h < 216) & (s > 0.7) & (mx > 0.45)


def detect(mask, lo_x, hi_x):
    """Anneaux de note : composantes de taille plausible entre lo_x et hi_x."""
    m = np.zeros_like(mask)
    m[:, lo_x:hi_x] = mask[:, lo_x:hi_x]
    m = ndimage.binary_closing(m, iterations=2)
    lab, n = ndimage.label(m)
    out = []
    for i, sl in enumerate(ndimage.find_objects(lab), 1):
        h = sl[0].stop - sl[0].start
        w = sl[1].stop - sl[1].start
        if not (18 <= w <= 90 and 18 <= h <= 90) or abs(w - h) > 0.35 * max(w, h):
            continue
        fill = (lab[sl] == i).sum() / (w * h)
        if not 0.15 <= fill <= 0.85:
            continue
        out.append(((sl[1].start + sl[1].stop) / 2.0, (w + h) / 2.0))
    return out


def track(dets, sign, max_step, min_len):
    """dets : liste (frame, [(x, taille), ...]). sign = +1 (droite, x croît) ou -1 (gauche)."""
    tracks, live = [], []
    for f, items in dets:
        used = set()
        for t in list(live):
            best = None
            for k, (x, s) in enumerate(items):
                dx = (x - t["x"][-1]) * sign
                if k not in used and 0 <= dx <= max_step and f - t["f"][-1] <= 3:
                    if best is None or dx < best[0]:
                        best = (dx, k)
            if best:
                k = best[1]; used.add(k)
                t["f"].append(f); t["x"].append(items[k][0]); t["s"].append(items[k][1])
            elif f - t["f"][-1] > 3:
                live.remove(t)
                if len(t["f"]) >= min_len:
                    tracks.append(t)
        for k, (x, s) in enumerate(items):
            if k not in used:
                live.append({"f": [f], "x": [x], "s": [s]})
    tracks += [t for t in live if len(t["f"]) >= min_len]
    return tracks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("video")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--right-target", type=float, default=DEFAULT_RIGHT_TARGET_X)
    ap.add_argument("--left-target", type=float, default=DEFAULT_LEFT_TARGET_X)
    ap.add_argument("--json", help="écrit les mesures brutes dans ce fichier")
    args = ap.parse_args()

    right, left = [], []
    n = 0
    for n, img in enumerate(frames(args.video)):
        m = note_mask(img)
        right.append((n, detect(m, int(CENTER_X) + 20, int(args.right_target) - 100)))
        left.append((n, detect(m, int(args.left_target) + 100, int(CENTER_X) - 20)))
    print(f"{n + 1} images analysées ({(n + 1) / args.fps:.1f} s)")

    result = {}
    for name, dets, sign, target in (("droite (Rond)", right, +1, args.right_target),
                                     ("gauche", left, -1, args.left_target)):
        tr = track(dets, sign, max_step=30, min_len=8)
        rows = []
        for t in tr:
            f = np.array(t["f"], float); x = np.array(t["x"]); s = np.array(t["s"])
            slope = np.polyfit(f, x, 1)[0]                   # px / image
            if abs(slope) < 1.0:
                continue
            spawn_x = x[0]
            frames_to_target = (target - spawn_x) / slope    # images du 1er relevé à la cible
            rows.append(dict(xs=[float(v) for v in x], ss=[float(v) for v in s], fs=[int(v) for v in f], f0=int(f[0]), n=len(f), x0=float(x[0]), x1=float(x[-1]), speed=float(slope * args.fps),
                             t_to_target_ms=float(frames_to_target / args.fps * 1000), s0=float(s[0]), s1=float(s[-1])))
        result[name] = rows
        print(f"\n== Voie {name} : {len(rows)} trajectoires suivies")
        if not rows:
            continue
        sp = np.array([r["speed"] for r in rows]); tt = np.array([r["t_to_target_ms"] for r in rows])
        x0 = np.array([r["x0"] for r in rows])
        print(f"vitesse : médiane {np.median(sp):.0f} px/s  (écart-type {sp.std():.0f})")
        print(f"1er relevé à x = {np.median(x0):.0f} px (centre écran = {CENTER_X:.0f}), cible à x = {target:.0f}")
        print(f"temps du 1er relevé à la cible : médiane {np.median(tt):.0f} ms  (min {tt.min():.0f}, max {tt.max():.0f})")
        for r in sorted(rows, key=lambda r: r["f0"])[:6]:
            print(f"  image {r['f0']:>5}  x {r['x0']:.0f} -> {r['x1']:.0f}  ({r['n']} relevés, {r['speed']:.0f} px/s, taille {r['s0']:.0f} -> {r['s1']:.0f} px)")
    if args.json:
        Path(args.json).write_text(json.dumps(result))


if __name__ == "__main__":
    main()
