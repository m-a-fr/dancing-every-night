#!/usr/bin/env python3
"""Génère un brouillon de carte (.chart) à partir d'une piste .ogg.

    tools/.venv/bin/python tools/audio2chart.py piste.ogg --name ma_chanson [--difficulty normal]

Copie la piste dans assets/songs/<name>/audio.ogg et écrit assets/songs/<name>/<difficulté>.chart.
Le résultat est un BROUILLON : le BPM est supposé constant, et les lanes/tenues
sont des heuristiques. Relis, corrige à la main, rejoue.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import find_peaks, stft
from scipy.ndimage import uniform_filter1d

ROOT = Path(__file__).resolve().parent.parent
HOP = 512
NPERSEG = 2048

# grille (en temps), quantile d'attaques gardées, durée mini d'une tenue (en temps)
DIFFICULTY = {
    "easy":   dict(grid=1.0,  keep=0.45, hold_min=2.0, double_flux=None),
    "normal": dict(grid=0.5,  keep=0.60, hold_min=1.5, double_flux=1.6),
    "hard":   dict(grid=0.25, keep=0.75, hold_min=1.0, double_flux=1.2),
}
LEFT_LANES = ["UP", "LEFT", "DOWN"]            # du plus aigu au plus grave
RIGHT_LANES = ["TRIANGLE", "CIRCLE", "CROSS"]  # idem
MAX_HOLD_BEATS = 4.0
HOLD_COOLDOWN_BEATS = 4.0
DOUBLE_MIN_GAP_BEATS = 4.0   # écart minimal entre deux doubles


def load_mono(path):
    y, sr = sf.read(path)
    if y.ndim > 1:
        y = y.mean(axis=1)
    return y.astype(np.float32), sr


def spectral_features(y, sr):
    f, t, Z = stft(y, sr, nperseg=NPERSEG, noverlap=NPERSEG - HOP)
    mag = np.abs(Z)
    S = np.log1p(mag * 10)
    flux_bins = np.maximum(0, np.diff(S, axis=1, prepend=S[:, :1]))
    bands = {"low": (30, 250), "mid": (250, 2500), "high": (2500, 10000)}
    flux, energy = {}, {}
    for name, (lo, hi) in bands.items():
        m = (f >= lo) & (f < hi)
        fl = flux_bins[m].sum(axis=0)
        flux[name] = fl / (np.percentile(fl, 95) + 1e-9)
        energy[name] = mag[m].sum(axis=0)
    # centroïde spectral (échelle log) pour classer les attaques par "hauteur"
    lf = np.log2(np.maximum(f, 20.0))[:, None]
    centroid = (lf * mag).sum(axis=0) / (mag.sum(axis=0) + 1e-9)
    onset = flux["low"] + flux["mid"] + 0.8 * flux["high"]
    return t, onset, flux, energy, centroid


def estimate_tempo(onset, fps, lo=90.0, hi=180.0):
    o = (onset - onset.mean()) / (onset.std() + 1e-9)
    ac = np.correlate(o, o, mode="full")[len(o) - 1:]
    lags = np.arange(int(fps * 60 / 220), int(fps * 60 / 60))
    best = lags[np.argmax(ac[lags])]
    bpm = 60 * fps / best
    while bpm < lo:
        bpm *= 2
    while bpm > hi:
        bpm /= 2
    return bpm


def grid_score(o_s, fps, bpm, phase_s):
    L = 60.0 / bpm * fps
    n = int((len(o_s) - 1 - phase_s * fps) / L)
    if n < 4:
        return -1e9
    pos = phase_s * fps + np.arange(n) * L
    half = pos + L / 2
    half = half[half < len(o_s) - 1]
    return np.interp(pos, np.arange(len(o_s)), o_s).sum() + 0.5 * np.interp(half, np.arange(len(o_s)), o_s).sum()


def refine_tempo(onset, fps, bpm0):
    o_s = uniform_filter1d(onset, 3)
    best = (-1e18, bpm0, 0.0)
    for bpm in np.arange(bpm0 - 2.0, bpm0 + 2.0, 0.02):
        L = 60.0 / bpm
        for ph in np.arange(0, L, 0.01):
            sc = grid_score(o_s, fps, bpm, ph)
            if sc > best[0]:
                best = (sc, bpm, ph)
    _, bpm, ph = best
    # affinage du BPM puis de la phase
    for step in (0.005,):
        for b in np.arange(bpm - 0.1, bpm + 0.1, step):
            for p in np.arange(max(0, ph - 0.01), ph + 0.01, 0.001):
                sc = grid_score(o_s, fps, b, p)
                if sc > best[0]:
                    best = (sc, b, p)
    return best[1], best[2]


def pick_onsets(onset, fps):
    o = uniform_filter1d(onset, 3)
    thr = uniform_filter1d(o, int(fps * 0.5)) + 0.35 * o.std()
    peaks, props = find_peaks(o, height=thr[:len(o)], distance=max(1, int(fps * 0.06)))
    return peaks


def choose_lane(pitch_pct, arc, recent):
    lanes = LEFT_LANES if arc == "L" else RIGHT_LANES
    # pitch_pct : 1 = aigu -> première lane
    idx = min(2, int((1.0 - pitch_pct) * 3))
    lane = lanes[idx]
    if len(recent) >= 3 and all(r == lane for r in recent[-3:]):
        lane = lanes[(idx + 1) % 3]   # évite de marteler la même touche
    return lane


def build_notes(t, onset, flux, energy, centroid, bpm, offset_s, cfg, holds):
    fps = 1.0 / (t[1] - t[0])
    peaks = pick_onsets(onset, fps)
    strengths = onset[peaks]
    keep_thr = np.quantile(strengths, 1.0 - cfg["keep"]) if len(strengths) else 0
    beat_len = 60.0 / bpm
    grid = cfg["grid"]
    pitches = centroid[peaks]
    order = pitches.argsort().argsort() / max(1, len(peaks) - 1)   # 0 grave .. 1 aigu

    slots = {}
    for k, pk in enumerate(peaks):
        if strengths[k] < keep_thr:
            continue
        beat = (t[pk] - offset_s) / beat_len
        if beat < 0:
            continue
        slot = round(beat / grid)
        cur = slots.get(slot)
        if cur is None or strengths[k] > cur[0]:
            # flux graves / flux médiums-aigus au moment de l'attaque : les deux forts = double note
            both = min(flux["low"][pk], max(flux["mid"][pk], flux["high"][pk]))
            slots[slot] = (strengths[k], pk, order[k], both)

    notes = []                      # (beat, lane, type, dur)
    last_end = {"L": -1e9, "R": -1e9}
    last_beat = {"L": -1e9, "R": -1e9}
    recent = {"L": [], "R": []}
    hold_free_from = {"L": -1e9, "R": -1e9}
    sorted_slots = sorted(slots)
    last_double = -1e9
    n_double = 0
    for i, slot in enumerate(sorted_slots):
        strength, pk, pitch_pct, both = slots[slot]
        beat = slot * grid
        # double note : attaque forte à la fois dans les graves et dans les médiums/aigus,
        # possible seulement si les deux mains sont libres
        if (cfg["double_flux"] is not None and both >= cfg["double_flux"]
                and beat - last_double >= DOUBLE_MIN_GAP_BEATS
                and beat >= max(last_end.values())
                and all(beat - last_beat[a] >= grid * 0.99 for a in "LR")):
            # une lane de chaque arc ; sur 9 doubles consécutives, les 9 associations sortent une fois
            l_lane = LEFT_LANES[n_double % 3]
            r_lane = RIGHT_LANES[(n_double // 3 + n_double) % 3]
            notes.append((beat, r_lane, "DOUBLE", l_lane))
            recent["L"].append(l_lane)
            recent["R"].append(r_lane)
            last_beat["L"] = last_beat["R"] = beat
            last_double = beat
            n_double += 1
            continue
        # arc : moitié grave -> gauche, moitié aigüe -> droite
        arc = "L" if pitch_pct < 0.45 else "R"
        if beat < last_end[arc] or beat - last_beat[arc] < grid * 0.99:
            continue
        # pitch relatif au sein de l'arc pour choisir la lane
        rel = pitch_pct / 0.45 if arc == "L" else (pitch_pct - 0.45) / 0.55
        lane = choose_lane(min(1.0, max(0.0, rel)), arc, recent[arc])
        recent[arc].append(lane)
        dur = 0.0
        if holds:
            dur = sustain_beats(t, energy, pk, beat, arc, sorted_slots, slots, grid, cfg["hold_min"], bpm, offset_s)
            if dur and beat >= hold_free_from[arc]:
                notes.append((beat, lane, "HOLD", dur))
                last_end[arc] = beat + dur + grid
                hold_free_from[arc] = beat + dur + HOLD_COOLDOWN_BEATS
                last_beat[arc] = beat
                continue
        notes.append((beat, lane, "TAP", 0.0))
        last_beat[arc] = beat
    return sorted(notes, key=lambda n: n[0])


def sustain_beats(t, energy, pk, beat, arc, sorted_slots, slots, grid, hold_min, bpm, offset_s):
    """Durée (en temps) pendant laquelle l'énergie reste élevée après l'attaque, sinon 0."""
    env = energy["mid"] if arc == "R" else energy["low"]
    dt = t[1] - t[0]
    ref = env[pk:pk + 4].max() if pk + 4 <= len(env) else env[pk]
    if ref <= 0:
        return 0.0
    beat_len = 60.0 / bpm
    max_frames = int(MAX_HOLD_BEATS * beat_len / dt)
    seg = env[pk + 3:pk + 3 + max_frames]
    below = np.nonzero(seg < 0.6 * ref)[0]
    length = (below[0] if len(below) else len(seg)) * dt / beat_len
    length = min(MAX_HOLD_BEATS, np.floor(length / 0.5) * 0.5)
    if length < hold_min:
        return 0.0
    return float(length - 0.25)


def note_lines(notes):
    lines = []
    for beat, lane, typ, dur in notes:
        b = f"{beat:.2f}".rstrip("0").rstrip(".")
        if typ == "DOUBLE":
            lines.append(f"{b} {lane} DOUBLE {dur}")
        elif typ == "HOLD":
            d = f"{dur:.2f}".rstrip("0").rstrip(".")
            lines.append(f"{b} {lane} HOLD {d}")
        else:
            lines.append(f"{b} {lane} TAP")
    return lines


def write_chart(path, title, artist, bpm, offset_ms, notes, audio_rel, cfg_name, pack="", map_id=""):
    lines = [
        f"# Brouillon généré par tools/audio2chart.py (difficulté : {cfg_name}). À relire et corriger.",
        "FORMAT 1",
        f"ID {map_id}",
        "VERSION 1",
        f"TITLE {title}",
        *([f"ARTIST {artist}"] if artist else []),
        *([f"PACK {pack}"] if pack else []),
        f"DIFFICULTY {cfg_name.upper()}",
        f"BPM {bpm:.2f}",
        f"OFFSET_MS {offset_ms}",
        f"AUDIO {audio_rel}",
        "",
        *note_lines(notes),
    ]
    Path(path).write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("audio", help="piste .ogg")
    ap.add_argument("--name", help="nom de la carte (défaut : nom du fichier)")
    ap.add_argument("--difficulty", choices=DIFFICULTY, default="normal")
    ap.add_argument("--bpm", type=float, help="force le BPM (sinon estimé)")
    ap.add_argument("--offset-ms", type=int, help="force la position du premier temps (sinon estimée)")
    ap.add_argument("--title", help="titre affiché dans le menu (défaut : dérivé du nom)")
    ap.add_argument("--artist", help="artiste affiché dans le menu")
    ap.add_argument("--pack", default="", help="pack du morceau (optionnel : vue par pack du menu)")
    ap.add_argument("--no-holds", action="store_true", help="pas de notes tenues")
    ap.add_argument("--analyze", action="store_true",
                    help="n'écrit rien : affiche BPM et offset estimés en JSON (utilisé par l'éditeur)")
    ap.add_argument("--stdout", action="store_true",
                    help="n'écrit ni carte ni piste : affiche seulement les lignes de notes (utilisé par l'éditeur)")
    args = ap.parse_args()

    if args.analyze:
        y, sr = load_mono(args.audio)
        t, onset, *_ = spectral_features(y, sr)
        fps = 1.0 / (t[1] - t[0])
        bpm, phase = refine_tempo(onset, fps, estimate_tempo(onset, fps))
        print(json.dumps({"bpm": round(float(bpm), 2), "offset_ms": int(round(phase * 1000)),
                          "duration_s": round(len(y) / sr, 2)}))
        return

    src = Path(args.audio)
    if src.suffix.lower() != ".ogg":
        sys.exit("Fournis un .ogg (convertis d'abord un mp3 avec ffmpeg -i in.mp3 out.ogg)")
    name = args.name or "".join(c if c.isalnum() else "_" for c in src.stem.lower()).strip("_")
    cfg = DIFFICULTY[args.difficulty]

    log = (lambda *a: print(*a, file=sys.stderr)) if args.stdout else print
    y, sr = load_mono(src)
    log(f"{src.name} : {len(y)/sr:.1f} s, {sr} Hz")
    t, onset, flux, energy, centroid = spectral_features(y, sr)
    fps = 1.0 / (t[1] - t[0])

    if args.bpm:
        bpm = args.bpm
        _, phase = refine_tempo(onset, fps, bpm) if args.offset_ms is None else (bpm, args.offset_ms / 1000)
    else:
        bpm0 = estimate_tempo(onset, fps)
        bpm, phase = refine_tempo(onset, fps, bpm0)
    offset_ms = args.offset_ms if args.offset_ms is not None else int(round(phase * 1000))
    log(f"BPM {bpm:.2f}, premier temps à {offset_ms} ms  (vérifie à l'oreille ; --bpm / --offset-ms pour forcer)")

    notes = build_notes(t, onset, flux, energy, centroid, bpm, offset_ms / 1000.0, cfg, holds=not args.no_holds)
    if args.stdout:
        print("\n".join(note_lines(notes)))
        return

    folder = ROOT / "assets" / "songs" / name    # une map = un dossier (format de map 1)
    folder.mkdir(parents=True, exist_ok=True)
    dst = folder / "audio.ogg"
    if src.resolve() != dst.resolve():
        shutil.copyfile(src, dst)
    out = folder / f"{args.difficulty}.chart"
    write_chart(out, args.title or name.replace("_", " ").title(), args.artist, bpm, offset_ms, notes, "audio.ogg", args.difficulty,
                args.pack, name)

    holds = sum(1 for n in notes if n[2] == "HOLD")
    doubles = sum(1 for n in notes if n[2] == "DOUBLE")
    per_lane = {}
    for n in notes:
        per_lane[n[1]] = per_lane.get(n[1], 0) + 1
        if n[2] == "DOUBLE":
            per_lane[n[3]] = per_lane.get(n[3], 0) + 1
    print(f"{len(notes)} notes ({holds} tenues, {doubles} doubles) sur {len(y)/sr:.0f} s -> {out.relative_to(ROOT)}")
    print("répartition :", ", ".join(f"{k} {v}" for k, v in sorted(per_lane.items())))
    print(f"Jouer : make run CHART={out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
