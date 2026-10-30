#!/usr/bin/env python3
"""Analyse d'une piste et génération des cartes EASY / NORMAL / HARD / EXPERT.

    tools/.venv/bin/python tools/mapper.py assets/songs/x/audio.ogg --title "Titre" --artist "Artiste"
        [--bpm 152] [--offset-ms 435] [--background background.jpg] [--video video.mpg]
        [--video-offset-ms 0] [--force]

Format de map 1 : la piste est dans le dossier de la map (assets/songs/<ID>/), les cartes y sont écrites
(<difficulté>.chart) et --background / --video nomment des fichiers de ce même dossier.

Utilisé par tools/song_import.py (make song) ; peut être relancé seul pour regénérer les cartes
(par exemple avec --bpm / --offset-ms si l'analyse s'est trompée). Règles de mapping : voir
GAME_DESIGN.md (« Règles de mapping »).

Étapes :
 1. tempo (constant) et phase : grille de temps qui colle le mieux aux attaques, calée pour que
    la grosse caisse tombe sur les temps ;
 2. début de mesure : là où la musique change (couleur harmonique, énergie) ;
 3. découpage en sections (changements de texture), intensité 0-3, reprises (même musique
    qu'une section précédente -> même pattern, en miroir) ;
 4. rythme par difficulté (attaques voix + batterie, densité cible), placement main gauche /
    droite, doubles, tenues.
"""
import argparse
import math
import random
import sys
import zlib
import collections
from pathlib import Path

import numpy as np
from scipy.ndimage import maximum_filter1d, median_filter
from scipy.signal import stft

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
REPORTS = ROOT / "art" / "analysis"

LEFT = ["UP", "LEFT", "DOWN"]
RIGHT = ["TRIANGLE", "CIRCLE", "CROSS"]
MIRROR = {"UP": "TRIANGLE", "LEFT": "CIRCLE", "DOWN": "CROSS",
          "TRIANGLE": "UP", "CIRCLE": "LEFT", "CROSS": "DOWN"}

DIFF = {
    #         densité (notes/temps) par intensité 0-3, grille, écart mini, même main, bloc main,
    #         tenue mini, doubles tous les N temps (sections intenses), part des tenues gardées
    "EASY":   dict(dens=[0.16, 0.30, 0.40, 0.50], grid=1.0, gap=1.0, same=1.0, block=8, hold=2.0, dbl=16, hold_rate=0.6),
    "NORMAL": dict(dens=[0.30, 0.55, 0.75, 0.95], grid=0.5, gap=0.5, same=0.5, block=4, hold=1.5, dbl=8, hold_rate=0.55),
    "HARD":   dict(dens=[0.45, 0.85, 1.20, 1.50], grid=0.25, gap=0.25, same=0.5, block=2, hold=1.5, dbl=4, hold_rate=0.5),
    "EXPERT": dict(dens=[0.65, 1.15, 1.65, 2.20], grid=0.25, gap=0.25, same=0.5, block=1, hold=1.5, dbl=2, hold_rate=0.45),
}
ONSET_BIAS_S = 0.006  # les attaques détectées arrivent ~6 ms après l'attaque réelle
# --- Règles liées au tempo (voir GAME_DESIGN.md, « Règles de mapping »)
# Temps de trajet : jeu de référence = 1500 ms à ~119 BPM. Au-dessus, compromis entre temps de
# lecture constant (notes tassées) et défilement proportionnel au tempo (trop rapide) : 1500 x sqrt(119 / BPM),
# arrondi à 50 ms, plancher 1100 ms. Écrit dans la carte (APPROACH_MS) seulement s'il diffère du défaut du jeu.
# APPROACH_MODE : "tempo" = cette formule ; "fixed" = 1500 ms pour tous les morceaux (comme le jeu de référence, où la vitesse ne
# dépend pas de la difficulté — HARD et niveau maximal mesurés à ~294 px/s, vidéos art/refs/). Essai "fixed" du 2026-10-12.
APPROACH_MODE = "fixed"  # défaut ; un choix d'auteur par difficulté peut le remplacer
REF_BPM, REF_APPROACH_MS, MIN_APPROACH_MS, GAME_DEFAULT_APPROACH_MS = 119.0, 1500, 1100, 1500
# Même lane : une note parcourt ~300 px et mesure ~54 px près de la cible -> il faut ~18 % du trajet entre deux
# notes d'une même lane pour qu'elles ne se chevauchent pas.
SAME_LANE_FRACTION = 0.18
# Écart minimal entre deux notes consécutives (ms) : la grille de la difficulté est grossie si elle descend dessous.
MIN_INTERVAL_MS = {"EASY": 300, "NORMAL": 160, "HARD": 110, "EXPERT": 80}
# Scratchs (coup de stick, voir GAME_DESIGN.md) : le pouce quitte la croix / les boutons pour le stick, il faut
# du temps libre autour. SCRATCH_CLEAR_MS = aucune autre note dans cette marge avant et après le scratch.
SCRATCH_CLEAR_MS = {"EASY": 450, "NORMAL": 400, "HARD": 350, "EXPERT": 300}
# Scratchs dans les sections : intensité minimale de la section, et un scratch supplémentaire tous les N temps
# sur un accent (None = seulement à l'attaque de la section).
SCRATCH_RULES = {"EASY": (3, None), "NORMAL": (2, None), "HARD": (2, 16), "EXPERT": (2, 16)}
FLIP = {"CW": "CCW", "CCW": "CW"}
# EASY / NORMAL : limites absolues de jouabilité (autocritique du 2026-10-12 : NORMAL avait 22-56 % de contretemps,
# 74-90 % de changements de bouton, des rafales d'une même main à 160-190 ms ; EASY des noires en continu à 170 BPM).
#   max_nps   plafond de notes par seconde (section la plus intense ; moins dans les sections calmes)
#   gap_ms    écart minimal entre deux notes : la grille grossit jusqu'à max_grid temps pour le respecter
#   same_ms   écart minimal entre deux notes d'une même main (sinon on alterne les mains)
#   offbeat   part maximale de notes à contretemps
#   repeat    nombre de fois qu'on peut rester sur le même bouton avant de bouger d'un cran
SIMPLE = {
    "EASY":   dict(max_nps=1.0, gap_ms=450, same_ms=900, offbeat=0.0, repeat=3, max_grid=2.0, hold_rate=0.4),
    "NORMAL": dict(max_nps=1.6, gap_ms=230, same_ms=380, offbeat=0.15, repeat=2, max_grid=1.0, hold_rate=0.45),
    # HARD (autocritique du 2026-10-12 : 28-64 % de contretemps, 53-87 % de changements de bouton, 36-55 % de notes
    # d'une même main à moins de 200 ms et des rafales de 11-12 notes au-delà de 150 BPM, doubles très irrégulières).
    #   max_run / run_ms   au plus max_run notes de suite à moins de run_ms, puis une respiration
    #   hold_others        au plus N notes de l'autre main pendant une tenue, espacées d'au moins un temps
    #   dbl_every / dbl_cap  une double toutes les dbl_every mesures (sections intenses), au plus dbl_cap par 10 s
    "HARD":   dict(max_nps=2.6, gap_ms=140, same_ms=250, offbeat=0.30, repeat=2, max_grid=1.0, hold_rate=0.35,
                   off_weight=0.75, pitch_step=2, max_run=6, run_ms=220, hold_others=2, dbl_bars=2, dbl_cap=3),
    # EXPERT (autocritique du 2026-10-12 : 51-69 % de contretemps, écarts de 81-97 ms et rafales de 15-17 notes au-delà de
    # 150 BPM, 76-92 % de changements de bouton, 13-17 scratchs par minute). Jeu de référence (
    # écrans de résultats) : niveau maximal 612 notes = 3,6 notes/s, seulement +20 % par rapport à HARD (510) -> la difficulté
    # d'EXPERT doit venir du type de notes plus que de leur nombre : plafond 3,0 notes/s (~ +25 % sur notre HARD).
    #   dbl_clear_ms  au moins ce temps libre avant et après une double ; hold_spacing : écart (temps) entre les notes de
    #   l'autre main pendant une tenue
    "EXPERT": dict(max_nps=3.0, gap_ms=100, same_ms=180, offbeat=0.40, repeat=2, max_grid=1.0, hold_rate=0.4,
                   off_weight=0.85, pitch_step=1, max_run=8, run_ms=180, hold_others=3, hold_spacing=0.5,
                   dbl_bars=1, dbl_cap=4, dbl_clear_ms=250),
}
NPS_BY_INTENSITY = [0.55, 0.75, 0.9, 1.0]


def approach_ms_for(bpm):
    if APPROACH_MODE == "fixed" or bpm <= REF_BPM:
        return REF_APPROACH_MS
    return max(MIN_APPROACH_MS, int(round(REF_APPROACH_MS * math.sqrt(REF_BPM / bpm) / 50.0)) * 50)


# --------------------------------------------------------------------------- analyse audio

def load_mono(path):
    import soundfile as sf      # importé ici : la version en ligne (Pyodide) fournit déjà les échantillons
    y, sr = sf.read(str(path), dtype="float32", always_2d=True)
    return y.mean(axis=1), sr


def onset_envelope(y, sr):
    """Attaques (flux spectral) à ~6 ms de résolution : bandes grave / tout."""
    hop, n = 256, 1024
    f, t, Z = stft(y, sr, nperseg=n, noverlap=n - hop)
    S = np.log1p(np.abs(Z) * 20)
    fl = np.maximum(0, np.diff(S, axis=1, prepend=S[:, :1]))
    allb = fl.sum(0)
    low = fl[(f >= 30) & (f < 130)].sum(0)
    return allb / (np.percentile(allb, 99) + 1e-9), low / (np.percentile(low, 99) + 1e-9), sr / hop


def grid_score(on, fps, bpm, phase, t0=0.0, t1=1e9, step=0.5):
    L = 60.0 / bpm
    b = np.arange(phase, len(on) / fps, L * step)
    b = b[(b >= t0) & (b < t1)]
    idx = np.round(b * fps).astype(int)
    idx = idx[idx < len(on)]
    return float(on[idx].mean()) if len(idx) else 0.0


def estimate_tempo(on, fps):
    """BPM (entre 100 et 200) et phase de la grille des croches : balayage grossier de tous les
    tempos, puis affinage des meilleurs candidats (évite les erreurs de rapport 2/3, 3/4...)."""
    coarse = []
    for bpm in np.arange(100.0, 200.0, 0.5):
        L = 60.0 / bpm
        coarse.append((max(grid_score(on, fps, bpm, ph) for ph in np.arange(0, L / 2, 0.006)), bpm))
    coarse.sort(reverse=True)
    tops = []
    for sc, bpm in coarse:                     # 3 meilleurs candidats bien distincts
        if all(abs(bpm - t) > 2.0 for t in tops): tops.append(bpm)
        if len(tops) == 3: break
    best = (-1.0, tops[0], 0.0)
    for c in tops:
        for bpm in np.arange(c - 0.6, c + 0.6, 0.05):
            L = 60.0 / bpm
            for ph in np.arange(0, L / 2, 0.003):
                s = grid_score(on, fps, bpm, ph)
                if s > best[0]: best = (s, bpm, ph)
    _, bpm, ph = best
    for b in np.arange(bpm - 0.06, bpm + 0.06, 0.01):   # affinage
        L = 60.0 / b
        for p in np.arange(max(0, ph - 0.01), ph + 0.01, 0.001):
            s = grid_score(on, fps, b, p)
            if s > best[0]: best = (s, b, p)
    _, bpm, ph = best
    if abs(bpm - round(bpm)) < 0.06: bpm = float(round(bpm))
    return bpm, ph


def tempo_drift(on, fps, bpm, ph):
    """Écart max (s) de la phase locale (fenêtres de 15 s) : grand = tempo variable."""
    L2 = 60.0 / bpm / 2
    dur = len(on) / fps
    devs, locs = [], []
    for t0 in np.arange(0, dur - 15, 15):
        loc = max((grid_score(on, fps, bpm, p, t0, t0 + 15), p) for p in np.arange(0, L2, 0.004))
        locs.append(loc)
    ref = np.median([l[0] for l in locs]) if locs else 0
    for sc, p in locs:
        if sc < max(0.3, 0.75 * ref): continue      # passage sans rythme net (intro, pont) : pas significatif
        d = (p - ph) % L2
        devs.append(min(d, L2 - d))
    return max(devs) if devs else 0.0


def hpss(y, sr):
    hop, n = 512, 4096
    f, t, Z = stft(y, sr, nperseg=n, noverlap=n - hop)
    mag = np.abs(Z).astype(np.float32)
    H = median_filter(mag, size=(1, 17))
    P = median_filter(mag, size=(17, 1))
    Mh = H ** 2 / (H ** 2 + P ** 2 + 1e-12)
    return f, t, mag * Mh, mag * (1 - Mh), sr / hop


class Song:
    """Données par double-croche (slot) et par temps, calées sur la grille BPM/offset."""

    def __init__(self, f, t, harm, perc, fps, bpm, off):
        self.bpm, self.off, self.fps = bpm, off, fps
        L = 60.0 / bpm
        self.L = L
        # --- batterie (composante percussive)
        S = np.log1p(perc * 20)
        fl = np.maximum(0, np.diff(S, axis=1, prepend=S[:, :1]))

        def band(lo, hi): return fl[(f >= lo) & (f < hi)].sum(0)

        def peaks(v, k):
            loc = median_filter(v, size=int(fps * 1.5))
            mx = maximum_filter1d(v, size=5)
            return (v == mx) & (v > loc * k) & (v > np.percentile(v, 40))
        K = band(35, 120); Sn = band(150, 350) + 0.5 * band(1500, 5000); Hh = band(7000, 16000)
        pk = {"K": peaks(K, 1.8), "S": peaks(Sn, 1.6), "H": peaks(Hh, 1.6)}
        st = {"K": K / np.percentile(K, 99), "S": Sn / np.percentile(Sn, 99), "H": Hh / np.percentile(Hh, 99)}
        # --- mélodie (composante harmonique) : hauteur dominante et attaques
        midis = np.arange(48, 84)
        binw = f[1]
        sal = np.zeros((len(midis), harm.shape[1]), np.float32)
        for i, m in enumerate(midis):
            f0 = 440 * 2 ** ((m - 69) / 12)
            for h, w in zip(range(1, 6), [1, .6, .4, .3, .2]):
                b = int(round(f0 * h / binw))
                if b + 1 < len(f): sal[i] += w * harm[b - 1:b + 2].max(0)
        bs = sal.max(0)
        pitch = np.where(bs / np.percentile(bs, 90) > 0.35, midis[sal.argmax(0)], 0)
        pitch = median_filter(pitch, size=5)
        Hs = np.log1p(harm[(f > 150) & (f < 2000)] * 20)
        hfl = np.maximum(0, np.diff(Hs, axis=1, prepend=Hs[:, :1])).sum(0)
        hpk = peaks(hfl, 1.5)
        tot = harm[(f > 30) & (f < 16000)].sum(0) + perc[(f > 30) & (f < 16000)].sum(0)
        self.loud = tot
        # --- slots
        n = int((t[-1] - off) / L * 4) + 1
        self.slots = []
        for s in range(n):
            c = off + s * L / 4
            i0, i1 = max(0, int((c - 0.035) * fps)), min(len(t), int((c + 0.04) * fps) + 1)
            if i1 <= i0 or c < 0:
                self.slots.append(None); continue
            ev = {}
            for k in "KSH":
                w = pk[k][i0:i1]
                ev[k] = float(st[k][i0:i1][w].max()) if w.any() else 0.0
            p = int(pitch[min(int(c * fps) + 4, len(t) - 1)])
            ev["V"] = bool(hpk[i0:i1].any()) and p > 0
            ev["P"] = p
            ev["E"] = float(tot[i0:i1].mean())
            self.slots.append(ev)
        # --- par temps : chroma normalisé + énergie (dB), pour la structure
        m = (f > 80) & (f < 2000)
        pc = np.round(12 * np.log2(f[m] / 440)).astype(int) % 12
        C = np.zeros((12, harm.shape[1]), np.float32)
        Hm = harm[m]
        for k in range(12): C[k] = Hm[pc == k].sum(0)
        nb = n // 4
        self.chroma = np.zeros((nb, 12)); self.db = np.zeros(nb); self.vox = np.zeros(nb)
        for b in range(nb):
            i0, i1 = int((off + b * L) * fps), int((off + (b + 1) * L) * fps)
            if i0 < 0 or i1 > C.shape[1] or i1 <= i0: continue
            self.chroma[b] = C[:, i0:i1].mean(1)
            self.db[b] = 20 * np.log10(tot[i0:i1].mean() + 1e-9)
            self.vox[b] = hpk[i0:i1].sum()
        self.chroma /= np.linalg.norm(self.chroma, axis=1, keepdims=True) + 1e-9
        self.db -= np.percentile(self.db, 95)
        self.nb = nb
        self.D = self.chroma @ self.chroma.T

    def slot(self, beat):
        i = int(round(beat * 4))
        return self.slots[i] if 0 <= i < len(self.slots) and self.slots[i] else None

    def sim(self, a, b, n):
        idx = [i for i in range(n) if 0 <= a + i < self.nb and 0 <= b + i < self.nb]
        if not idx: return 0.0
        return float(np.mean([self.D[a + i, b + i] for i in idx]))


def beat_onsets(song):
    return np.array([sum((song.slot(b) or {}).get(k, 0.0) for k in "KS") + (1.0 if (song.slot(b) or {}).get("V") else 0.0)
                     for b in range(song.nb)])


def estimate_meter(song):
    """Mesure à 3 ou 4 temps : les attaques se répètent-elles tous les 3/6 ou 4/8 temps ?"""
    x = beat_onsets(song)
    x = x - x.mean()
    v = float(np.var(x)) + 1e-9

    def acf(lag): return float(np.mean(x[lag:] * x[:-lag])) / v
    m3, m4 = acf(3) + acf(6), acf(4) + acf(8)
    return (3 if m3 > 1.3 * max(m4, 0.05) else 4), m3, m4


def downbeat_phase(song):
    """Parmi les temps possibles, celui où commencent les mesures (changements de musique, batterie)."""
    M = song.meter
    X = np.hstack([song.chroma, (song.db[:, None] / 10.0)])
    nb = len(X)
    nov = np.zeros(nb)
    for b in range(M, nb - M):
        nov[b] = np.linalg.norm(X[b - M:b].mean(0) - X[b:b + M].mean(0))
    K = np.array([(song.slot(b) or {}).get("K", 0.0) for b in range(nb)])
    S = np.array([(song.slot(b) or {}).get("S", 0.0) for b in range(nb)])
    allowed = list(range(M))
    bonus = [0.0] * M
    if M == 4:
        # parité : caisse claire sur les temps 2 et 4 (backbeat), à défaut grosse caisse sur 1 et 3
        snare = (S[1::4].mean() + S[3::4].mean()) - (S[0::4].mean() + S[2::4].mean())
        kick = (K[0::4].mean() + K[2::4].mean()) - (K[1::4].mean() + K[3::4].mean())
        cue = snare if abs(snare) > 0.05 else kick
        if abs(cue) > 0.05:
            allowed = [0, 2] if cue >= 0 else [1, 3]
    else:
        # mesure à 3 temps : le premier temps porte la basse / grosse caisse et les attaques
        o = beat_onsets(song)
        bonus = [0.3 * float(o[k::M].mean() / (o.mean() + 1e-9)) for k in range(M)]
    scores = []
    for k in range(M):
        sel = np.arange(k, nb, M)
        scores.append(float(np.mean(np.sort(nov[sel])[-max(4, len(sel) // 6):])) + bonus[k])
    k = max(allowed, key=lambda i: scores[i])
    return k, [scores[i] for i in allowed]


# --------------------------------------------------------------------------- structure

def find_sections(song):
    """[(début, fin, nom, focus, intensité[, décalage de reprise])] en temps."""
    nb = song.nb
    active = np.where(song.db > -30)[0]
    first = int(active[0]) if len(active) else 0
    last = int(active[-1]) + 1 if len(active) else nb
    M = song.meter
    fa = first - ((first - song.bar0) % M)          # mesures alignées sur le premier temps de mesure
    bars = list(range(fa, last, M))
    bars[0] = max(bars[0], 0)
    first = bars[0]
    nbar = len(bars)
    # caractéristiques par mesure
    F = np.array([np.hstack([song.chroma[b:b + M].mean(0), song.db[b:b + M].mean() / 8.0]) for b in bars])
    Fn = F / (np.linalg.norm(F, axis=1, keepdims=True) + 1e-9)
    SS = Fn @ Fn.T
    k = max(2, round(16 / M))                      # noyau de Foote : ~16 temps de chaque côté
    nov = np.zeros(nbar)
    for i in range(k, nbar - k + 1):
        a, b = SS[i - k:i, i - k:i].mean() + SS[i:i + k, i:i + k].mean(), SS[i - k:i, i:i + k].mean()
        nov[i] = a / 2 - b
    ejump = np.zeros(nbar)
    E = np.array([song.db[b:b + M].mean() for b in bars])
    ejump[1:] = np.abs(np.diff(E)) / 6.0
    score = nov / (nov.max() + 1e-9) + np.minimum(ejump, 1.0)
    # choix des frontières : les plus fortes d'abord, au moins ~8 temps d'écart, en moyenne une section / 24 temps
    cand = sorted(range(1, nbar), key=lambda i: -score[i])
    thr = np.mean(score) + 0.5 * np.std(score)
    min_gap = max(2, math.ceil(8 / M))
    cuts = []
    for i in cand:
        if score[i] < thr or len(cuts) >= (last - first) // 24: break
        if all(abs(i - c) >= min_gap for c in cuts) and i >= min_gap // 2 and nbar - i >= 2:
            cuts.append(i)
    edges = [0] + sorted(cuts) + [nbar]
    raw = [(bars[a], bars[b - 1] + M if b <= nbar else last) for a, b in zip(edges, edges[1:])]
    raw[-1] = (raw[-1][0], last)
    # fin du morceau : les petites sections qui se suivent (derniers coups, fondu) n'en font qu'une
    k = len(raw)
    while k > 1 and raw[k - 1][1] - raw[k - 1][0] <= 8:
        k -= 1
    if len(raw) - k >= 2:
        raw[k:] = [(raw[k][0], raw[-1][1])]
    # intensité : volume relatif au passage le plus fort (dB)
    secs = []
    vox_med = np.median(song.vox[first:last]) + 1e-9
    ons = beat_onsets(song)
    ons_ref = float(np.mean(ons[first:last])) + 1e-9
    for s, e in raw:
        db = float(np.mean(song.db[s:e]))
        inten = 3 if db > -2.5 else 2 if db > -5.5 else 1 if db > -10 else 0
        vox = float(np.mean(song.vox[s:e])) / vox_med
        focus = "drums" if (e - s <= 8 or (s == first and vox < 0.7)) else "vocal"
        busy = float(np.mean(ons[s:e])) / ons_ref
        secs.append([s, e, focus, inten, db + 4.0 * (busy - 1.0)])
    # contraste : si tout le morceau a le même volume, on ne met pas tout en « intense » ;
    # les passages les plus forts / chargés gardent leur niveau, les autres descendent d'un cran
    total = sum(e - s for s, e, *_ in secs)
    cum = 0
    for sec in sorted(secs, key=lambda x: -x[4]):
        if sec[3] == 3 and cum > 0.45 * total: sec[3] = 2
        if sec[3] >= 2 and cum > 0.75 * total: sec[3] = 1
        cum += sec[1] - sec[0]
    # reprises : un bloc d'au moins 16 temps identique (harmonie et volume) à un passage précédent
    # devient une copie en miroir ; le reste de la section est mappé normalement
    pieces = []
    for s0, e0, focus, inten, db in secs:
        pieces += split_repeats(song, s0, e0, first, focus, inten)
    out, labels = [], []            # labels : (début, fin, nom court) des parties déjà nommées
    next_letter = iter("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    for j, (s0, e0, focus, inten, shift) in enumerate(pieces):
        if shift:
            src = s0 - shift
            label = next((lab for a, b, lab in labels if a <= src < b), "passage")
            out.append((s0, e0, f"{label} (reprise en miroir)", "copy", inten, shift))
            labels.append((s0, e0, label))
            continue
        if j == 0 and s0 <= first + 8:
            label = "Intro"
        elif e0 - s0 <= 8 and inten <= 1:
            label = "Cassure"
        elif j == len(pieces) - 1 and (e0 - s0 <= 16 or inten <= 1):
            label = "Fin"
        else:
            label = f"Partie {next(next_letter)}"
        tag = ["calme", "moyenne", "forte", "intense"][inten]
        out.append((s0, e0, f"{label} ({tag})", focus, inten))
        labels.append((s0, e0, label))
    return out, first, last


def split_repeats(song, s, e, first, focus, inten):
    """Découpe [s, e) en morceaux [(début, fin, focus, intensité, décalage de reprise ou 0)]."""
    W = song.meter
    nw = (e - s) // W
    best = None                      # (nb de fenêtres, moyenne, i0, i1, décalage)
    if nw >= 4:
        for d in range(W * 4, s - first + 1):
            ok = []
            for i in range(nw):
                a, b = s + i * W, s + i * W - d
                if b < first or b + W > a:           # la source doit précéder (même dans la section)
                    ok.append(None); continue
                sm = float(np.mean([song.D[a + k, b + k] for k in range(W)]))
                ddb = abs(float(np.mean(song.db[a:a + W]) - np.mean(song.db[b:b + W])))
                ok.append(sm if sm >= 0.9 and ddb < 3.0 else None)
            i = 0
            while i < nw:                # plus longue suite de fenêtres qui correspondent
                if ok[i] is None: i += 1; continue
                j = i
                while j < nw and ok[j] is not None: j += 1
                run = ok[i:j]
                jj = min(j, i + d // W)              # la copie ne peut pas se recouvrir elle-même
                run = ok[i:jj]
                if len(run) >= 4 and np.mean(run) >= 0.95:
                    cand = (len(run), float(np.mean(run)), i, jj, d)
                    if best is None or cand[:2] > best[:2]: best = cand
                i = j
    if not best:
        return [(s, e, focus, inten, 0)]
    _, _, i0, i1, d = best
    a, b = s + i0 * W, s + i1 * W
    if i1 == nw: b = e
    out = []
    if a > s: out += split_repeats(song, s, a, first, focus, inten)
    out.append((a, b, focus, inten, d))
    if b < e: out += split_repeats(song, b, e, first, focus, inten)
    return out


# --------------------------------------------------------------------------- composition

def metric(beat, bar0=0, meter=4):
    f = (beat - bar0) % meter
    if f == 0: return 1.0
    if f == 2 and meter == 4: return 0.85
    if f % 1 == 0: return 0.75
    if f % 0.5 == 0: return 0.5
    return 0.3


# --------------------------------------------------------------------------- arrangement (2026-10-13)
# Le rythme (quand jouer, tenues, doubles, scratchs) est déjà décidé ; l'arrangeur décide QUI joue QUOI, mesure
# par mesure, comme un musicien : des motifs pensés en SENSATION (note répétée, main qui roule, question-réponse...)
# plutôt qu'en boutons exacts (interchangeables), des réponses entre mesures (miroir), des répétitions quand la
# musique se répète et des variations quand c'est à propos, des ponts avant les montées et des cadences en fin
# de phrase. Chaque morceau a sa « personnalité » (palette de sensations), différente des autres morceaux du pack.
#
# Sensations (une mesure) : h = main menante, o = l'autre main ; positions 0 haut / 1 milieu / 2 bas.
FEELS = {
    "jack":      "note répétée puis l'autre main (= x)",
    "jack3":     "trois fois la même note, puis l'autre main",
    "jackpair":  "chaque main répète sa note (= x =)",
    "roll":      "une main roule sur ses boutons (~ ~)",
    "handtrill": "une main va et vient entre deux boutons",
    "callresp":  "deux notes d'une main, réponse en miroir de l'autre",
    "anchor":    "une main garde le même bouton, l'autre bouge",
    "alternate": "gauche-droite à chaque note",
    "sweep":     "les deux mains descendent (ou montent) ensemble en alternant",
    "jump":      "une main saute haut-bas",
}
FEEL_SETS = {
    "EASY":   {"jack": 1.4, "jack3": 0.8, "jackpair": 1.0, "callresp": 1.0, "anchor": 0.8, "alternate": 0.8, "roll": 0.6},
    "NORMAL": {"jack": 1.2, "jack3": 0.6, "jackpair": 1.0, "callresp": 1.1, "anchor": 1.0, "alternate": 0.8,
               "roll": 1.0, "handtrill": 0.6, "sweep": 0.7},
    "HARD":   {"jack": 1.0, "jack3": 0.5, "jackpair": 0.9, "callresp": 1.0, "anchor": 1.0, "alternate": 0.9,
               "roll": 1.0, "handtrill": 0.8, "sweep": 0.9, "jump": 0.4},
    "EXPERT": {"jack": 0.9, "jack3": 0.5, "jackpair": 0.8, "callresp": 0.9, "anchor": 1.0, "alternate": 1.0,
               "roll": 1.1, "handtrill": 1.0, "sweep": 1.0, "jump": 0.7},
}
# note répétée (même bouton) : écart minimal propre, jamais sous la limite visuelle (18 % du trajet)
JACK_MS = {"EASY": 400, "NORMAL": 300, "HARD": 270, "EXPERT": 270}
ALT_CAP = {"EASY": 0.22, "NORMAL": 0.22, "HARD": 0.25, "EXPERT": 0.28}   # part max de mesures « gauche-droite »
# part max de mesures bâties sur des notes répétées (sinon c'est l'excès inverse : monotone)
JACK_CAP = {"EASY": 0.30, "NORMAL": 0.28, "HARD": 0.24, "EXPERT": 0.20}
JACK_FEELS = ("jack", "jack3", "jackpair", "anchor")


def feel_template(feel, n, h, s, rng):
    """Suite de (main, position) pour n notes selon la sensation ; h = main menante, s = position de départ."""
    o = "L" if h == "R" else "R"
    t = 2 - s                                        # position miroir (haut <-> bas)
    adj = 1 if s != 1 else rng.choice([0, 2])        # position voisine
    seq = []
    for i in range(n):
        if feel == "jack":       seq.append((h, s) if i % 3 < 2 else (o, t))
        elif feel == "jack3":    seq.append((h, s) if i % 4 < 3 else (o, adj))
        elif feel == "jackpair": seq.append((h, s) if (i // 2) % 2 == 0 else (o, t))
        elif feel == "roll":
            path = [s, 1, t, 1] if s != 1 else [1, adj, 1, 2 - adj]
            seq.append((h, path[i % 4]) if i < 3 or n <= 4 else (o, path[i % 4]))
        elif feel == "handtrill": seq.append((h, s if i % 2 == 0 else adj))
        elif feel == "callresp":
            half = max(1, n // 2)
            seq.append((h, [s, adj][i % 2]) if i < half else (o, [2 - s, 2 - adj][i % 2]))
        elif feel == "anchor":   seq.append((h, s) if i % 2 == 0 else (o, [1, 0, 1, 2][(i // 2) % 4]))
        elif feel == "alternate": seq.append((h if i % 2 == 0 else o, s if i % 4 < 2 else adj))
        elif feel == "sweep":
            path = [0, 1, 2] if s == 0 else [2, 1, 0]
            seq.append((h if i % 2 == 0 else o, path[(i // 2) % 3]))
        elif feel == "jump":     seq.append((h, [0, 2][i % 2]) if i % 3 < 2 else (o, 1))
        else:                    seq.append((h if i % 2 == 0 else o, s))
    return seq


def vary(seq, how):
    """Variation d'une mesure déjà jouée : miroir des mains, inversion haut/bas, ou les deux."""
    out = []
    for hnd, pos in seq:
        if how in ("hands", "both"): hnd = "L" if hnd == "R" else "R"
        if how in ("invert", "both"): pos = 2 - pos
        out.append((hnd, pos))
    return out


class Arranger:
    """Réattribue mains et boutons des notes d'une section (TAP / HOLD ; les doubles gardent leur place)."""

    def __init__(self, song, cfg, name, seed, character):
        self.song, self.cfg, self.name = song, cfg, name
        self.rng = random.Random(seed)
        self.memory = []          # (signature rythmique, indice de mesure, séquence (main, pos), nombre d'usages)
        self.last_feels = []
        self.bars = self.alt_bars = 0
        self.L = 60.0 / song.bpm
        # personnalité du morceau, tirée de SA musique (chaque chanson est abordée individuellement) :
        # voix qui répète ses notes -> notes répétées ; mélodie mobile -> roulements / balayages ;
        # batterie régulière -> ancres / alternances ; tempo lent -> triples répétitions, va-et-vient d'une main
        base = dict(FEEL_SETS[name])
        boost = {"jack": 1 + 2 * character["repeat"], "jackpair": 1 + 1.5 * character["repeat"],
                 "jack3": (1 + 2 * character["repeat"]) * (1.4 if character["slow"] else 0.7),
                 "roll": 1 + 2 * character["motion"], "sweep": 1 + 1.5 * character["motion"],
                 "anchor": 1.6 if character["steady"] else 1.0, "alternate": 1.3 if character["steady"] else 0.9,
                 "handtrill": 1.3 if character["slow"] else 0.9, "callresp": 1 + character["dialog"]}
        for k in base:
            base[k] *= boost.get(k, 1.0)
        keys = list(base)
        self.rng.shuffle(keys)
        fav = sorted(keys, key=lambda k: -base[k] * (0.5 + self.rng.random()))[:5]
        self.palette = {k: base[k] * (2.2 if k in fav else 0.6) for k in base}
        self.favorites = fav
        self.used = collections.Counter()
        self.forms = collections.Counter()   # répétition, variation, réponse, nouveau motif, pont, cadence

    # -- analyse musicale d'une mesure
    def bar_sim(self, a, b):
        M = self.song.meter
        return self.song.sim(int(a), int(b), M)

    def pitch_shape(self, b0, b1):
        ps = [s["P"] for x in range(int(b0 * 4), int(b1 * 4)) if (s := self.song.slot(x / 4)) and s["P"]]
        if len(ps) < 3: return "flat"
        steps = [((y - x) % 12 + 6) % 12 - 6 for x, y in zip(ps, ps[1:])]
        if sum(1 for d in steps if d == 0) >= 0.6 * len(steps): return "repeat"
        tot = sum(steps)
        return "up" if tot >= 3 else "down" if tot <= -3 else "flat"

    def choose_feel(self, b0, b1, n, next_rises):
        w = dict(self.palette)
        shape = self.pitch_shape(b0, b1)
        if shape == "repeat":                         # la voix répète une note : note répétée
            for k in ("jack", "jack3", "jackpair", "anchor"):
                if k in w: w[k] *= 2.5
        elif shape in ("up", "down"):                 # la mélodie monte / descend : la main roule, balayage
            for k in ("roll", "sweep"):
                if k in w: w[k] *= 2.2
        if next_rises:                                # pont : on prépare l'arrivée de la section suivante
            for k in ("roll", "sweep", "handtrill"):
                if k in w: w[k] *= 2.0
        if self.bars >= 4 and self.alt_bars / self.bars > ALT_CAP[self.name] and "alternate" in w:
            w["alternate"] *= 0.1
        chosen = sum(self.used[k] for k in self.used)
        if chosen >= 4 and sum(self.used[k] for k in JACK_FEELS) / chosen > JACK_CAP[self.name]:
            for k in JACK_FEELS:
                if k in w: w[k] *= 0.15
        for k in self.last_feels[-2:]:
            if k in w: w[k] *= 0.3
        if n <= 2:                                     # mesure presque vide : peu de sens à « rouler »
            for k in ("roll", "sweep", "handtrill", "jump"):
                w.pop(k, None)
        return weighted(self.rng, w), shape

    # -- réalisation sous contraintes
    def realize(self, items, seq, holds_busy):
        """items : notes (b, lane, typ, extra) de la mesure, seq : (main, pos) voulus. Respecte : tenue en cours,
        écart d'une même main (boutons différents), écart d'une note répétée, limite visuelle d'une même lane."""
        same_b = self.cfg["same"]
        jack_b = max(JACK_MS[self.name] / 1000.0 / self.L,
                     same_lane_beats(self.song.bpm, self.cfg.get("approach_ms")))
        out = []
        for (b, lane, typ, extra), (hnd, pos) in zip(items, seq):
            def ok(hh, pp):
                if holds_busy.get(hh, -1) > b - 0.25 + 1e-6: return False
                lt, lp = self.last[hh]
                if lp is None: return True
                need = jack_b if pp == lp else same_b
                return b - lt >= need - 1e-6
            other = "L" if hnd == "R" else "R"
            # repli : d'abord l'autre main (même hauteur, miroir, milieu), puis la même main sur un autre bouton
            cand = [(hnd, pos), (other, pos), (other, 2 - pos), (other, 1),
                    (hnd, (pos + 1) % 3 if pos != 1 else 0), (hnd, 2 - pos)]
            pick = next((c for c in cand if ok(*c)), None)
            if pick is None:
                continue                                   # aucune place jouable : la note est retirée
            hh, pp = pick
            nl = (LEFT if hh == "L" else RIGHT)[pp]
            out.append((b, nl, typ, extra))
            self.last[hh] = (b + (extra if typ == "HOLD" else 0.0), pp)
            if typ == "HOLD":
                holds_busy[hh] = b + extra
        return out

    def arrange(self, notes, start, end, next_rises):
        M, bar0 = self.song.meter, self.song.bar0
        sec = [n for n in notes if start <= n[0] < end]
        rest = [n for n in notes if not (start <= n[0] < end)]
        self.last = {"L": (-99.0, None), "R": (-99.0, None)}
        for b, lane, typ, extra in sorted(rest):           # état des mains juste avant la section
            if b >= start: break
            if typ in ("TAP", "HOLD"):
                h = "L" if lane in LEFT else "R"
                self.last[h] = (b + (extra if typ == "HOLD" else 0.0), POS_OF[lane])
        holds_busy = {}
        out, prev_seq, prev_sig, prev_bs = [], None, None, None
        bar_starts = sorted({bar0 + M * math.floor((n[0] - bar0) / M) for n in sec})
        for k, bs in enumerate(bar_starts):
            be = bs + M
            phrase_i = int(round((bs - bar0) / M))
            items = [n for n in sec if bs <= n[0] < be]
            playable = [n for n in items if n[2] in ("TAP", "HOLD")]
            for n in items:                                 # doubles / scratchs : gardés, mettent à jour les mains
                if n[2] == "DOUBLE":
                    self.last = {"L": (n[0], POS_OF[n[3]]), "R": (n[0], POS_OF[n[1]])}
            if not playable:
                out += items; continue
            sig = tuple(round((n[0] - bs) * 4) for n in playable) + tuple(n[2] for n in playable)
            # la musique se répète ? (même rythme et même harmonie qu'une mesure déjà arrangée)
            rep = next((m for m in self.memory if m[0] == sig and self.bar_sim(bs, m[1]) >= 0.92), None)
            n = len(playable)
            if rep:
                rep[3] += 1
                how = None if rep[3] % 3 != 0 else self.rng.choice(["hands", "invert", "both"])
                seq = vary(rep[2], how) if how else list(rep[2])     # répétition, et variation de temps en temps
                feel = "repeat" if not how else "variation"
                self.forms["répétition" if not how else "variation"] += 1
            elif (phrase_i % 2 == 1 and prev_seq and sig == prev_sig
                  and prev_bs == bs - M and self.rng.random() < 0.55):
                seq = vary(prev_seq, self.rng.choice(["hands", "hands", "both"]))   # réponse en miroir
                feel = "answer"
                self.forms["réponse en miroir"] += 1
            else:
                last_bar = k == len(bar_starts) - 1
                feel, shape = self.choose_feel(bs, be, n, next_rises and last_bar)
                self.forms["pont" if next_rises and last_bar else "nouveau motif"] += 1
                h = self.rng.choice("LR")
                s0 = self.rng.choice([0, 1, 2])
                seq = feel_template(feel, n, h, s0, self.rng)
                if phrase_i % 4 == 3 and n >= 2:            # cadence en fin de phrase : on se pose (note répétée)
                    seq[-1] = (seq[-2][0], seq[-2][1])
                    self.forms["cadence"] += 1
                self.last_feels.append(feel); self.used[feel] += 1
            self.bars += 1
            if all(seq[i][0] != seq[i + 1][0] for i in range(len(seq) - 1)) and n >= 3:
                self.alt_bars += 1
            placed = self.realize(playable, seq, holds_busy)
            out += placed + [x for x in items if x[2] not in ("TAP", "HOLD")]
            # Une reprise conserve son noyau initial : ne pas mémoriser sa variation
            # comme un nouveau modèle, et compter réellement ses occurrences.
            if not rep:
                self.memory.append([sig, bs, list(seq), 1])
            prev_seq, prev_sig, prev_bs = seq, sig, bs
        notes[:] = sorted(rest + out, key=lambda x: (x[0], x[1]))


POS_OF = {"UP": 0, "LEFT": 1, "DOWN": 2, "TRIANGLE": 0, "CIRCLE": 1, "CROSS": 2}


class Composer:
    attempt, tighten = 0, 0.0
    song_name = ""

    def __init__(self, song, sections, first, last):
        self.song = song
        self.sections = sections
        self.last = last
        bpm = song.bpm
        self.dens_scale = min(1.0, 155.0 / bpm)
        self.fast = bpm >= 170
        beats = [song.slot(b) for b in range(first, last)]
        kicks = [s["K"] for s in beats if s]
        self.four_floor = len(kicks) > 0 and np.mean(np.array(kicks) > 0.4) > 0.6
        self.anchors = {first} | {s[0] for s in sections if s[4] >= 2}
        self.arrangers = {}

    def cfg(self, name):
        c = dict(DIFF[name])
        c["approach_ms"] = (getattr(self, "approach_by_difficulty", {}).get(name)
                            or getattr(self, "approach_override", None) or approach_ms_for(self.song.bpm))
        c["dens"] = [x * self.dens_scale for x in c["dens"]]
        if self.fast:
            c["dbl"] *= 2
        beat_ms = 60000.0 / self.song.bpm
        while c["grid"] < 1.0 and c["grid"] * beat_ms < MIN_INTERVAL_MS[name]:
            c["grid"] *= 2                           # tempo trop rapide pour cette grille : on grossit
        if name in SIMPLE:
            c.update(SIMPLE[name])
            max_grid = c["max_grid"] if self.song.meter == 4 else 1.0
            while c["grid"] < max_grid and c["grid"] * beat_ms < c["gap_ms"]:
                c["grid"] *= 2
            c["same"] = max(c["same"], c["same_ms"] / beat_ms)
        c["gap"] = max(c["gap"], c["grid"])
        c["same"] = max(c["same"], c["grid"])
        return c

    def salience(self, beat, focus):
        s = self.song.slot(beat)
        if not s: return 0.0
        k, sn, h, v = s["K"], s["S"], s["H"], 1.0 if s["V"] else 0.0
        if focus == "drums":
            base = 1.2 * max(k, sn) + 0.4 * min(k, sn) + 0.25 * h + 0.3 * v
        else:
            base = 1.0 * v + 0.55 * max(k, sn) + 0.15 * h
        return base * (0.35 + metric(beat, self.song.bar0, self.song.meter))

    def sustain_len(self, beat, limit):
        """Durée de la note chantée qui démarre à beat (jusqu'à la prochaine attaque vocale)."""
        s0 = self.song.slot(beat)
        if not s0 or not s0["P"]: return 0.0
        b, unvoiced = beat + 0.25, 0
        while b < beat + limit:
            s = self.song.slot(b)
            if not s or s["V"]: break
            if not s["P"]:
                unvoiced += 1
                if unvoiced > 1: break
            else:
                unvoiced = 0
            b += 0.25
        return b - beat - 0.25 * unvoiced

    def pick_rhythm(self, cfg, start, end, focus, inten):
        simple = "max_nps" in cfg
        cands = []
        b = start
        if cfg["grid"] > 1:                          # blanches : sur les temps 1 et 3 de la mesure
            b = start + ((self.song.bar0 - start) % cfg["grid"])
        while b < end - 1e-6:
            sc = self.salience(b, focus)
            if simple and abs(b - round(b)) > 1e-6:
                sc *= cfg.get("off_weight", 0.6)     # on préfère nettement les temps
            cands.append((sc, b))
            b += cfg["grid"]
        target = max(1, round(cfg["dens"][inten] * (end - start)))
        if simple:                                   # plafond absolu en notes par seconde
            secs = (end - start) * 60.0 / self.song.bpm
            target = min(target, max(1, round(cfg["max_nps"] * NPS_BY_INTENSITY[inten] * secs)))
        max_off = int(cfg.get("offbeat", 1.0) * target)
        cands.sort(reverse=True)
        chosen, n_off = [], 0
        for sc, b in cands:
            if len(chosen) >= target or sc < 0.12: break
            off = abs(b - round(b)) > 1e-6
            if off and n_off >= max_off: continue
            if all(abs(b - c) >= cfg["gap"] - 1e-6 for c in chosen):
                chosen.append(b)
                n_off += off
        if (start not in chosen and self.salience(start, focus) > 0.05) or start in self.anchors:
            chosen = [c for c in chosen if abs(c - start) >= cfg["gap"] - 1e-6] + [start]
        chosen = sorted(set(chosen))
        if "max_run" in cfg:                         # rafales : au plus max_run notes de suite rapprochées
            run_beats = cfg["run_ms"] / 60000.0 * self.song.bpm
            kept, run = [], 0
            for b in chosen:
                if kept and b - kept[-1] < run_beats - 1e-6:
                    if run + 1 >= cfg["max_run"]:
                        continue                     # on saute cette note : respiration
                    run += 1
                else:
                    run = 0
                kept.append(b)
            chosen = kept
        # pas de trou de plus d'une mesure tant que la musique joue
        edges = [start - 1] + chosen + [end]
        for a, b in zip(edges, edges[1:]):
            if b - a <= 4.0: continue
            x = max(start, math.floor(a) + 2)
            while x < b - 1.5:
                s = self.song.slot(x)
                if s and s["E"] > 0:
                    chosen.append(max((self.salience(x + d, focus), x + d) for d in (0, 1) if x + d < b - 1)[1])
                x += 4 if cfg["grid"] >= 1 else 2
        return sorted(set(chosen))

    def is_accent(self, t):
        s = self.song.slot(t)
        return bool(s) and s["K"] > 0.5 and s["S"] > 0.4 and t % 1 == 0

    def limit_holds(self, notes, cfg, start, end):
        """Tenue + autre main : au plus hold_others notes de l'autre main pendant la tenue, espacées d'au moins
        un temps. Sinon la tenue est raccourcie juste avant la note de trop (ou devient un simple appui)."""
        for i, (b, lane, typ, dur) in enumerate(notes):
            if typ != "HOLD" or not (start <= b < end):
                continue
            left = lane in LEFT
            others = sorted(n[0] for n in notes if b < n[0] < b + dur and n[2] in ("TAP", "HOLD")
                            and (n[1] in LEFT) != left)
            cut, prev = None, None
            for k, o in enumerate(others):
                if k >= cfg["hold_others"] or (prev is not None and o - prev < cfg.get("hold_spacing", 1.0) - 1e-6):
                    cut = o; break
                prev = o
            if cut is not None:
                nd = math.floor((cut - b - 0.25) * 4) / 4
                notes[i] = (b, lane, "HOLD", nd) if nd >= cfg["hold"] else (b, lane, "TAP", None)

    def character(self):
        """Caractère musical du morceau (calculé une fois) : part des mesures où la voix répète ses notes, où la
        mélodie bouge, échanges voix / instruments (« dialogue »), batterie régulière, tempo lent."""
        if getattr(self, "_character", None):
            return self._character
        M, bar0 = self.song.meter, self.song.bar0
        rep = mot = dia = n = 0
        probe = Arranger.__new__(Arranger); probe.song = self.song
        for b in range(int(bar0), int(self.last) - M, M):
            sh = Arranger.pitch_shape(probe, b, b + M)
            rep += sh == "repeat"; mot += sh in ("up", "down")
            v = sum(1 for x in range(b * 4, (b + M) * 4) if (s := self.song.slot(x / 4)) and s["V"])
            d = sum(1 for x in range(b * 4, (b + M) * 4) if (s := self.song.slot(x / 4)) and max(s["K"], s["S"]) > 0.5)
            dia += 1 if v and d and abs(v - d) <= 2 else 0
            n += 1
        n = max(n, 1)
        self._character = dict(repeat=rep / n, motion=mot / n, dialog=dia / n, steady=self.four_floor,
                               slow=self.song.bpm < 125)
        return self._character

    def compose_checked(self, name):
        """compose(), puis contrôle des fourchettes validées (VALIDATED) ; jusqu'à 4 essais avec un régulateur de
        plus en plus strict, on garde le plus conforme."""
        best = None
        for attempt in range(4):
            self.attempt, self.tighten = attempt, 0.06 * attempt
            notes, comments = self.compose(name)
            change, same = placement_stats(notes, self.cfg(name), self.song.bpm)
            lim = VALIDATED[name]
            excess = max(0.0, change - lim["change"]) + max(0.0, same - lim["same_hand"])
            if best is None or excess < best[0]:
                best = (excess, notes, comments)
            if excess == 0.0:
                break
        self.attempt, self.tighten = 0, 0.0
        return best[1], best[2]

    def add_scratches(self, notes, name, start, end, inten):
        """Scratchs sur les gros accents d'une section : son attaque, puis (HARD/EXPERT) un accent fort tous
        les N temps. Les notes trop proches sont retirées ; jamais pendant une tenue."""
        min_inten, every = SCRATCH_RULES[name]
        if inten < min_inten or end - start < 8:
            return
        bpm = self.song.bpm
        clear = SCRATCH_CLEAR_MS[name] / 1000.0 * bpm / 60.0       # en temps
        M = self.song.meter
        cands = [start]
        if every:
            b = start + every
            while b < end - every / 2:
                # le temps fort le plus accentué autour de b (+/- une mesure)
                opts = [x for x in range(int(b - M), int(b + M) + 1)
                        if (x - self.song.bar0) % M == 0 and start + clear < x < end - clear]
                if opts:
                    cands.append(max(opts, key=lambda x: self.salience(x, "drums")))
                b += every
        last_dir = None
        min_hold = DIFF[name]["hold"]
        for t in cands:
            if self.salience(t, "drums") < 0.25 and t != start:
                continue
            if name == "EASY" and any(n[2] == "SCRATCH" and t - n[0] < 32 for n in notes):
                continue                                           # EASY : au plus un scratch toutes les 32 temps
            # une tenue qui empiète sur la zone dégagée est raccourcie (ou retirée si elle devient trop courte)
            kept = []
            for n in notes:
                b, lane, typ, extra = n
                if typ == "HOLD" and b <= t - clear and b + extra > t - clear:
                    dur = math.floor((t - clear - b) * 4) / 4
                    n = (b, lane, "HOLD", dur) if dur >= min_hold else (b, lane, "TAP", None)
                kept.append(n)
            notes[:] = [n for n in kept if not (t - clear < n[0] < t + clear) or n[2] == "SCRATCH"]
            # sens : la mélodie monte -> antihoraire, descend -> horaire ; sinon on alterne
            s1, s0 = self.song.slot(t), self.song.slot(t - 1)
            p1, p0 = (s1 or {}).get("P", 0), (s0 or {}).get("P", 0)
            d = ((p1 - p0) % 12 + 6) % 12 - 6 if p1 and p0 else 0
            if d > 0: direction = "CCW"
            elif d < 0: direction = "CW"
            else: direction = FLIP[last_dir] if last_dir else "CW"
            if last_dir == direction and t - start < 16:
                direction = FLIP[direction]                        # deux scratchs proches : on alterne
            notes.append((float(t), "SCRATCH", "SCRATCH", direction))
            last_dir = direction

    def compose(self, name):
        cfg = self.cfg(name)
        notes, comments = [], {}
        pl = Placer(self.song, cfg, seed=zlib.crc32(name.encode()) + self.attempt, name=name)
        pl.tighten = self.tighten
        arr = Arranger(self.song, cfg, name, seed=zlib.crc32((self.song_name + name).encode()) + self.attempt,
                       character=self.character()) if "repeat" in cfg else None
        self.arrangers[name] = arr
        sec_list = [x for x in self.sections]
        last_dbl = -99.0
        M = self.song.meter
        accent_every = 2 * M if self.four_floor else (2 if M == 4 else M)
        for start, end, title, focus, inten, *copy in self.sections:
            comments[start] = title
            if focus == "copy":
                shift = copy[0]
                for b, lane, typ, extra in [n for n in notes if start - shift <= n[0] < end - shift]:
                    nb = b + shift
                    if nb >= end: continue
                    if typ == "DOUBLE":
                        notes.append((nb, MIRROR[extra], "DOUBLE", MIRROR[lane]))
                    elif typ == "SCRATCH":
                        notes.append((nb, "SCRATCH", "SCRATCH", FLIP[extra]))   # miroir : sens inversé
                    else:
                        notes.append((nb, MIRROR[lane], typ, extra))
                continue
            rhythm = self.pick_rhythm(cfg, start, end, focus, inten)
            dbl_every = math.ceil(cfg["dbl"] / M) * M        # en mesures entières
            if "dbl_bars" in cfg:
                dbl_every = cfg["dbl_bars"] * M
            for i, t in enumerate(rhythm):
                nxt = rhythm[i + 1] if i + 1 < len(rhythm) else end
                prv = rhythm[i - 1] if i > 0 else -99
                want = (t == start and inten >= 2) or (inten >= 2 and (t - start) % dbl_every == 0)
                if name == "EXPERT" and "dbl_bars" not in cfg and self.is_accent(t) and inten >= 1 and (t - self.song.bar0) % accent_every == 0:
                    want = True
                if name == "HARD" and "dbl_bars" not in cfg and self.is_accent(t) and inten >= 1 and (t - self.song.bar0) % accent_every == 0:
                    want = True
                if want and "dbl_cap" in cfg:            # au plus dbl_cap doubles sur 10 s
                    win = 10.0 * self.song.bpm / 60.0
                    if sum(1 for n in notes if n[2] == "DOUBLE" and t - win < n[0] <= t) >= cfg["dbl_cap"]:
                        want = False
                if name == "EASY" and not (t == start and inten >= 2): want = False
                if want and t - last_dbl < min(dbl_every, M) / 2 and t != start: want = False
                if want and "dbl_clear_ms" in cfg:       # temps libre autour d'une double (les deux mains sont prises)
                    clr = cfg["dbl_clear_ms"] / 60000.0 * self.song.bpm
                    if nxt - t < clr - 1e-6 or t - prv < clr - 1e-6:
                        want = False
                if want and nxt - t >= 0.5 and t - prv >= 0.5:
                    d = pl.place_double(t)
                    if d:
                        last_dbl = t
                        notes.append((t, d[1], "DOUBLE", d[0]))
                        continue
                if name == "HARD" and t - prv < 0.5 - 1e-6 and any(pl.hold_until[h] > t for h in "LR"):
                    continue
                r = pl.place_tap(t, focus)
                if not r: continue
                hand, lane = r
                room = nxt - t
                sus = self.sustain_len(t, 4.0)
                s = self.song.slot(t)
                voiced = bool(s and s["P"])
                dur = 0.0
                if name in ("HARD", "EXPERT") and sus >= cfg["hold"]:
                    dur = min(sus, 4.0)                       # l'autre main continue pendant la tenue
                elif sus >= cfg["hold"] and room >= cfg["hold"] + cfg["grid"]:
                    dur = min(sus, room - cfg["grid"], 4.0)
                elif voiced and room >= max(2.0, cfg["hold"]) + cfg["grid"]:
                    dur = min(room - cfg["grid"], 4.0)        # voyelle qui s'étire jusqu'à la phrase suivante
                dur = math.floor(dur / cfg["grid"] + 1e-6) * cfg["grid"]
                if dur >= cfg["hold"] and pl.rng.random() < (1.0 if inten == 0 else cfg["hold_rate"]):
                    notes.append((t, lane, "HOLD", dur))
                    pl.hold_until[hand] = t + dur
                    continue
                notes.append((t, lane, "TAP", None))
            if arr:
                idx = sec_list.index((start, end, title, focus, inten, *copy))
                nxt_sec = sec_list[idx + 1] if idx + 1 < len(sec_list) else None
                arr.arrange(notes, start, end, bool(nxt_sec and nxt_sec[4] > inten))
            if "hold_others" in cfg:
                self.limit_holds(notes, cfg, start, end)
            self.add_scratches(notes, name, start, end, inten)
        notes = self.compose_ending(notes, name, cfg, pl)
        return fix(notes, self.song.bpm, cfg["approach_ms"]), comments

    def ending_kind(self):
        """Comment la musique se termine : 'stop' (forte jusqu'au bout puis silence), 'ring' (frappe finale qui
        résonne), 'fade' (fondu sans coup final). Renvoie (type, temps de la frappe finale, durée de résonance)."""
        db = self.song.db
        end_hits = [b / 4 for b in range(int((self.last - 12) * 4), int(self.last * 4))
                    if self.salience(b / 4, "drums") > 0.4 and b % 2 == 0
                    and 0 <= int(b / 4) < len(db) and db[int(b / 4)] > -9]
        final = end_hits[-1] if end_hits else float(self.last - 1)
        fb = int(final)
        ring = 0
        while fb + ring + 1 < len(db) and db[fb + ring + 1] > -25 and ring < 8:
            ring += 1
        pre = [db[b] for b in range(max(0, fb - 8), fb) if b < len(db)]
        slope = (pre[-1] - pre[0]) / max(1, len(pre) - 1) if len(pre) >= 4 else 0.0
        if not end_hits or (slope < -0.9 and db[min(fb, len(db) - 1)] < -8):
            return "fade", final, ring
        return ("ring" if ring >= 2 else "stop"), final, ring

    def compose_ending(self, notes, name, cfg, pl):
        """Fin composée selon la musique, différente selon le morceau et la difficulté."""
        kind, final, ring = self.ending_kind()
        rng = random.Random(zlib.crc32((self.song_name + name + "fin").encode()))
        M = self.song.meter
        notes = [n for n in notes if n[0] < final - 0.9]
        # position des mains juste avant la fin (pour enchaîner naturellement)
        lastpos = {"L": 1, "R": 1}
        for b, lane, typ, extra in notes:
            if typ in ("TAP", "HOLD"): lastpos["L" if lane in LEFT else "R"] = POS_OF[lane]
            elif typ == "DOUBLE": lastpos = {"L": POS_OF[extra], "R": POS_OF[lane]}
        def pair():
            opts = [(lastpos["L"], lastpos["R"]), (0, 0), (2, 2), (0, 2), (2, 0), (1, 1)]
            return rng.choice(opts)
        hard = name in ("HARD", "EXPERT")
        if kind == "fade":
            # fondu : on éclaircit les 2 dernières mesures, puis une tenue qui s'éteint avec la musique
            thin_from = final - 2 * M
            kept, k = [], 0
            for n in notes:
                if n[0] >= thin_from and n[2] in ("TAP", "HOLD"):
                    k += 1
                    if k % 2 == 0: continue
                kept.append(n)
            notes = kept
            h = rng.choice("LR")
            dur = max(cfg["hold"], min(4.0, float(max(ring, 2))))
            notes = [n for n in notes if n[0] < final - 1.0]
            notes.append((final, (LEFT if h == "L" else RIGHT)[lastpos[h]], "HOLD", dur))
            self.ending_desc = f"fondu : notes éclaircies puis tenue finale ({dur:g} temps)"
            return notes
        if kind == "ring":
            options = ["double", "hold"] + (["chord"] if hard else [])
            choice = rng.choice(options)
            dur = max(cfg["hold"], min(4.0, float(ring)))
            if choice == "chord":            # accord final : tenue des deux mains pendant la résonance
                a, b = pair()
                notes += [(final, LEFT[a], "HOLD", dur), (final, RIGHT[b], "HOLD", dur)]
                self.ending_desc = f"frappe qui résonne : accord final tenu à deux mains ({dur:g} temps)"
            elif choice == "hold":
                h = rng.choice("LR")
                notes.append((final, (LEFT if h == "L" else RIGHT)[lastpos[h]], "HOLD", dur))
                self.ending_desc = f"frappe qui résonne : tenue finale ({dur:g} temps)"
            else:
                a, b = pair()
                notes.append((final, RIGHT[b], "DOUBLE", LEFT[a]))
                self.ending_desc = "frappe qui résonne : double finale"
            return notes
        # arrêt sec
        options = ["double", "cadence"] + (["scratch"] if hard else [])
        choice = rng.choice(options)
        if choice == "scratch":
            clear = SCRATCH_CLEAR_MS[name] / 60000.0 * self.song.bpm
            notes = [n for n in notes if n[0] < final - clear]
            notes.append((final, "SCRATCH", "SCRATCH", rng.choice(["CW", "CCW"])))
            self.ending_desc = "arrêt sec : scratch final"
        elif choice == "cadence":
            # cadence : la même note répétée sur les temps qui mènent au dernier coup, puis la double
            h = rng.choice("LR")
            lane = (LEFT if h == "L" else RIGHT)[lastpos[h]]
            step = max(1.0, math.ceil(JACK_MS[name] / 60000.0 * self.song.bpm))
            beats = [final - 2 * step, final - step]
            notes = [n for n in notes if n[0] < beats[0] - 0.5]
            notes += [(b, lane, "TAP", None) for b in beats]
            a, b = pair()
            notes.append((final, RIGHT[b], "DOUBLE", LEFT[a]))
            self.ending_desc = "arrêt sec : cadence (note répétée) puis double"
        else:
            a, b = pair()
            notes.append((final, RIGHT[b], "DOUBLE", LEFT[a]))
            self.ending_desc = "arrêt sec : double finale"
        return notes


# Motifs d'une main (positions 0 = haut, 1 = milieu, 2 = bas), joués sur ses 4 notes suivantes.
# s = position de départ, o = voisine de s, d = sens (+1 vers le bas / -1 vers le haut) ; choisis selon la mélodie
# de la phrase, avec une pénalité pour ceux qui viennent d'être joués (demande du 2026-10-13 : moins de répétition).
def _clip(x): return max(0, min(2, x))
MOTIFS = {
    "repeat": lambda s, o, d: [s, s, s, s],
    "pair":   lambda s, o, d: [s, s, o, o],
    "trill":  lambda s, o, d: [s, o, s, o],
    "pedal":  lambda s, o, d: [s, s, o, s],
    "stair":  lambda s, o, d: [_clip(1 - d), 1, _clip(1 + d), 1],
    "arch":   lambda s, o, d: [1, _clip(1 - d), 1, _clip(1 + d)],
    "leap":   lambda s, o, d: [0, 2, 0, 2] if s != 2 else [2, 0, 2, 0],
}
MOTIF_SETS = {      # motifs permis et poids de base par difficulté
    "EASY":   {"repeat": 1.2, "pair": 1.2, "pedal": 1.0, "stair": 1.0, "trill": 0.5},
    "NORMAL": {"pair": 1.0, "pedal": 1.0, "stair": 1.0, "trill": 0.9, "arch": 0.9, "repeat": 0.5, "echo": 0.8},
    "HARD":   {"pair": 0.9, "pedal": 0.8, "stair": 1.0, "trill": 1.0, "arch": 1.0, "leap": 0.4, "echo": 0.9},
    "EXPERT": {"pair": 0.7, "pedal": 0.7, "stair": 1.0, "trill": 1.1, "arch": 1.0, "leap": 0.7, "echo": 1.0},
}
# Structures de mains par phrase : appel-réponse (blocs de longueur variable), ping-pong (on alterne à chaque note),
# main menante (une main porte la phrase, l'autre ponctue le 1er temps de chaque mesure).
# Plafond du taux de changement de bouton (12 dernières notes d'une main) : au-delà, motifs statiques favorisés.
# Bornes tirées des cartes validées en jeu (Fall 2026, 2026-10-12).
MAX_CHANGE = {"EASY": 0.55, "NORMAL": 0.56, "HARD": 0.62, "EXPERT": 0.74}
# Fourchettes validées en jeu (pack Fall 2026) que chaque carte générée doit respecter ; sinon elle est régénérée
# avec un régulateur plus strict (compose_checked). change = changements de bouton d'une même main ;
# same_hand = part des notes d'une même main plus proches que same_ms.
VALIDATED = {"EASY": dict(change=0.58, same_hand=0.088), "NORMAL": dict(change=0.64, same_hand=0.093),
             "HARD": dict(change=0.72, same_hand=0.045), "EXPERT": dict(change=0.81, same_hand=0.005)}
HAND_MODES = {
    "EASY":   {"call": 0.8, "pingpong": 0.2},
    "NORMAL": {"call": 0.55, "pingpong": 0.3, "lead": 0.15},
    "HARD":   {"call": 0.45, "pingpong": 0.35, "lead": 0.2},
    "EXPERT": {"call": 0.35, "pingpong": 0.45, "lead": 0.2},
}


def weighted(rng, weights):
    tot = sum(weights.values())
    r = rng.random() * tot
    for k, w in weights.items():
        r -= w
        if r <= 0: return k
    return k


class Placer:
    """Choix de la main (gauche = croix directionnelle, droite = boutons) et de la lane."""

    def __init__(self, song, cfg, seed, name="NORMAL"):
        self.song, self.cfg = song, cfg
        self.same_lane = same_lane_beats(song.bpm, cfg.get("approach_ms"))
        self.pos = {"L": 1, "R": 1}
        self.dir = {"L": 1, "R": -1}
        self.runs = {"L": 0, "R": 0}
        self.last_p = {"L": None, "R": None}
        self.last_t = {"L": -99.0, "R": -99.0}
        self.hold_until = {"L": -1.0, "R": -1.0}
        self.hand = "R"
        self.prev_t = -99.0
        self.run = 0
        self.rng = random.Random(seed)
        self.dbl_cycle = 0
        self.name = name
        self.motif = {"L": [], "R": []}            # positions restantes du motif en cours
        self.motif_hist = {"L": [], "R": []}       # derniers motifs joués (noms)
        self.last_shape = {"L": None, "R": None}   # positions du dernier motif complet (pour l'écho)
        self.seg_end, self.seg_mode, self.seg_hand = -1.0, "call", "R"
        self.last_pairs = []
        self.lane_hist = {"L": [], "R": []}
        self.tighten = 0.0                          # resserrement du régulateur (compose_checked)

    def choose_hand(self, t, focus):
        free = [h for h in "LR" if self.hold_until[h] <= t - 0.25]
        if len(free) == 1:
            h = free[0]
            # l'autre main tient une note : si celle-ci a joué trop récemment, on saute la note plutôt que de
            # casser l'écart minimal d'une même main (contrainte stricte)
            if "repeat" in self.cfg and t - self.last_t[h] < self.cfg["same"] - 1e-6:
                return None
            return h
        if not free: return None
        if t - self.prev_t < self.cfg["same"] - 1e-6:
            return "L" if self.hand == "R" else "R"          # notes rapprochées : on alterne
        s = self.song.slot(t)
        if focus == "drums" and s and self.cfg["grid"] < 1 and "repeat" not in self.cfg:   # HARD / EXPERT seulement
            if s["K"] > 0.5 and s["K"] > s["S"]: return "L"   # grosse caisse à gauche
            if s["S"] > 0.5 and s["S"] > s["K"] * 1.5 and self.hand == "L": return "R"
        blk = self.cfg["block"] * self.song.meter / 4
        if "repeat" not in self.cfg:
            return "L" if int((t - self.song.bar0) // blk) % 2 else "R"  # blocs : appel-réponse
        if t >= self.seg_end - 1e-6:                 # nouvelle phrase : structure et longueur tirées au sort
            self.seg_mode = weighted(self.rng, HAND_MODES[self.name])
            length = blk * self.rng.choice([0.5, 1, 1, 2] if self.name != "EASY" else [1, 1, 2])
            M = self.song.meter
            start = t - ((t - self.song.bar0) % M)
            self.seg_end = start + max(M, length)
            self.seg_hand = "L" if self.seg_hand == "R" else "R"
        if self.seg_mode == "pingpong":
            return "L" if self.hand == "R" else "R"
        if self.seg_mode == "lead" and (t - self.song.bar0) % self.song.meter == 0:
            return "L" if self.seg_hand == "R" else "R"  # l'autre main ponctue le temps fort
        return self.seg_hand

    def move(self, hand, t):
        s = self.song.slot(t)
        p = s["P"] if s and s["P"] else None
        lp = self.last_p[hand]
        cur = self.pos[hand]
        step = 0
        if p is not None and lp is not None:
            d = (p - lp) % 12
            if d > 6: d -= 12
            if d >= 1: step = -1       # plus aigu -> lane du haut
            elif d <= -1: step = 1
        gap = t - self.last_t[hand]
        if "repeat" in self.cfg:
            return self.move_motif(hand, t, p, lp, cur, gap)
        if step == 0 and (gap < 2.0 or self.run >= 1):
            step = self.dir[hand]      # balayage le long de l'arc
        if step:
            self.dir[hand] = step
        nxt = cur + step
        if nxt < 0 or nxt > 2:
            self.dir[hand] = -step     # rebond sur le bord
            nxt = cur - step
        if nxt == cur and gap < self.same_lane - 1e-6:
            nxt = (cur + 1) % 3
        self.run = self.run + 1 if nxt == cur else 0
        self.runs[hand] = self.runs[hand] + 1 if nxt == cur else 1
        self.pos[hand] = nxt
        self.last_p[hand] = p if p is not None else lp
        return nxt

    def pick_motif(self, hand, t, cur):
        """Choisit le motif des 4 prochaines notes de cette main, selon le contour de la mélodie."""
        # contour : hauteur moyenne sur la 1re et la 2de moitié des 4 temps à venir
        def mean_p(a, b):
            ps = [s["P"] for x in range(int(a * 4), int(b * 4)) if (s := self.song.slot(x / 4)) and s["P"]]
            return sum(ps) / len(ps) if ps else None
        p0, p1 = mean_p(t, t + 2), mean_p(t + 2, t + 4)
        trend = 0 if p0 is None or p1 is None else (1 if p1 - p0 > 1.5 else -1 if p0 - p1 > 1.5 else 0)
        w = dict(MOTIF_SETS[self.name])
        if trend:                                    # la mélodie monte ou descend : escalier / arche
            for k in ("stair", "arch"):
                if k in w: w[k] *= 2.5
        else:                                        # mélodie plate : motifs sur place
            for k in ("repeat", "pair", "pedal", "trill"):
                if k in w: w[k] *= 1.8
        other = "L" if hand == "R" else "R"
        if "echo" in w and not self.last_shape[other]:
            w.pop("echo")
        hist = self.lane_hist[hand][-13:]
        if len(hist) >= 6:                           # trop de mouvement récent : on calme le jeu
            rate = sum(1 for a, b in zip(hist, hist[1:]) if a != b) / (len(hist) - 1)
            if rate > MAX_CHANGE[self.name] - self.tighten:
                for k in ("repeat", "pair", "pedal"):
                    if k in w: w[k] *= 4.0
                for k in ("trill", "leap", "arch", "echo"):
                    if k in w: w[k] *= 0.3
        for k in self.motif_hist[hand][-2:]:         # pas deux fois de suite le même motif
            if k in w: w[k] *= 0.2
        name = weighted(self.rng, w)
        self.motif_hist[hand].append(name)
        if name == "echo":                           # reprise en miroir du motif que l'autre main vient de jouer
            return [2 - x for x in self.last_shape[other]] if self.rng.random() < 0.5 else list(self.last_shape[other])
        d = -trend if trend else self.rng.choice([-1, 1])   # plus aigu -> vers le haut (position 0)
        o = _clip(cur + self.rng.choice([-1, 1])) if cur == 1 else 1
        s0 = cur if self.rng.random() < 0.7 else o
        return MOTIFS[name](s0, o if o != s0 else (1 if s0 != 1 else 0), d)

    def move_motif(self, hand, t, p, lp, cur, gap):
        if not self.motif[hand] or gap > 4.0:        # motif fini (ou longue pause) : on en choisit un autre
            seq = self.pick_motif(hand, t, cur)
            self.motif[hand] = list(seq)
            self.last_shape[hand] = list(seq)
        nxt = self.motif[hand].pop(0)
        # contraintes : pas plus de `repeat` appuis identiques, et l'écart minimal d'une même lane
        if nxt == cur and (self.runs[hand] >= self.cfg["repeat"] or gap < self.same_lane - 1e-6):
            nxt = _clip(cur + self.dir[hand])
            if nxt == cur:
                self.dir[hand] = -self.dir[hand]
                nxt = _clip(cur + self.dir[hand])
        if nxt != cur:
            self.dir[hand] = 1 if nxt > cur else -1
        self.run = self.run + 1 if nxt == cur else 0
        self.runs[hand] = self.runs[hand] + 1 if nxt == cur else 1
        self.lane_hist[hand].append(nxt)
        self.pos[hand] = nxt
        self.last_p[hand] = p if p is not None else lp
        return nxt

    def place_tap(self, t, focus):
        h = self.choose_hand(t, focus)
        if h is None: return None
        idx = self.move(h, t)
        self.hand, self.prev_t, self.last_t[h] = h, t, t
        return h, (LEFT if h == "L" else RIGHT)[idx]

    def place_double(self, t):
        if any(self.hold_until[h] > t - 0.25 for h in "LR"): return None
        if "repeat" in self.cfg:
            # paire choisie selon le flux : proche de la position actuelle des mains, symétrique ou croisée,
            # jamais la même que l'une des deux précédentes
            w = {}
            for a_ in range(3):
                for b_ in range(3):
                    x = 1.5 if a_ == b_ else 1.0 if a_ + b_ == 2 else 0.7
                    x *= 1.5 if abs(a_ - self.pos["L"]) <= 1 and abs(b_ - self.pos["R"]) <= 1 else 1.0
                    if (a_, b_) in self.last_pairs[-2:]: x *= 0.15
                    w[(a_, b_)] = x
            a, b = weighted(self.rng, w)
            self.last_pairs.append((a, b))
        else:
            a, b = [(0, 0), (2, 2), (1, 1), (0, 2), (2, 0)][self.dbl_cycle % 5]
        self.dbl_cycle += 1
        self.pos["L"], self.pos["R"] = a, b
        self.prev_t = t
        self.last_t["L"] = self.last_t["R"] = t
        return LEFT[a], RIGHT[b]


def placement_stats(notes, cfg, bpm):
    """(taux de changement de bouton d'une même main, part des notes d'une même main plus proches que same_ms)."""
    L = 60.0 / bpm
    same_ms = cfg.get("same_ms", 0)
    last, ch, cnt, bad = {}, 0, 0, 0
    for b, lane, typ, extra in notes:
        if typ == "SCRATCH": continue
        if typ == "DOUBLE":
            last = {"L": (b, None), "R": (b, None)}; continue
        h = "L" if lane in LEFT else "R"
        if h in last:
            cnt += 1
            pb, pl = last[h]
            if pl and pl != lane: ch += 1
            if pl and pl != lane and (b - pb) * L * 1000 < same_ms - 1: bad += 1
        last[h] = (b, lane)
    return ch / max(cnt, 1), bad / max(cnt, 1)


def same_lane_beats(bpm, approach_ms=None):
    """Écart minimal (en temps) entre deux notes d'une même lane : une croche, et au moins 18 % du trajet."""
    return max(0.5, SAME_LANE_FRACTION * (approach_ms or approach_ms_for(bpm)) / 1000.0 * bpm / 60.0)


def fix(notes, bpm, approach_ms=None):
    """Retire les conflits : même lane pendant une tenue ou trop rapprochée, plus d'une note à la fois."""
    gap = same_lane_beats(bpm, approach_ms)
    notes.sort(key=lambda n: (n[0], n[1]))
    out, busy, last = [], {}, {}
    for b, lane, typ, extra in notes:
        if typ == "SCRATCH":
            if out and abs(out[-1][0] - b) < 1e-6:
                continue
            out.append((b, lane, typ, extra))
            continue
        lanes = [lane] + ([extra] if typ == "DOUBLE" else [])
        if any(busy.get(ln, -1) > b - 0.25 + 1e-6 or b - last.get(ln, -99) < gap - 1e-6 for ln in lanes):
            continue
        if out and abs(out[-1][0] - b) < 1e-6:
            o = out[-1]
            if not (typ == "HOLD" and o[2] == "HOLD" and (lane in LEFT) != (o[1] in LEFT)):
                continue                                   # sauf l'accord final : une tenue par main
        for ln in lanes: last[ln] = b
        if typ == "HOLD":
            busy[lane] = last[lane] = b + extra
        out.append((b, lane, typ, extra))
    return out


# --------------------------------------------------------------------------- sortie

def fmt(x):
    return f"{x:.3f}".rstrip("0").rstrip(".")


def parse_approach(value):
    """Un trajet est une décision d'auteur, pas une échelle automatique de niveaux."""
    try:
        level, ms = value.upper().split("=")
        ms = int(ms)
        if level not in DIFF or not 500 <= ms <= 5000:
            raise ValueError
        return level, ms
    except ValueError:
        raise argparse.ArgumentTypeError("attendu EASY|NORMAL|HARD|EXPERT=500..5000") from None


def chart_text(meta, diff, notes, comments, bpm, offset_ms):
    h = [f"# Carte générée par tools/mapper.py (make song). Relire, jouer, corriger."
         + (f" Mesure à {meta['meter']} temps." if meta.get("meter", 4) != 4 else ""),
         "FORMAT 1", f"ID {meta['id']}", "VERSION 1", f"TITLE {meta['title']}"]
    if meta.get("artist"): h.append(f"ARTIST {meta['artist']}")
    if meta.get("pack"): h.append(f"PACK {meta['pack']}")
    h += [f"DIFFICULTY {diff}", f"BPM {fmt(bpm)}", f"OFFSET_MS {offset_ms}", f"AUDIO {meta['audio']}"]
    approach = meta.get("approach_ms", approach_ms_for(bpm))
    if approach != GAME_DEFAULT_APPROACH_MS:
        h.append(f"APPROACH_MS {approach}")
    if meta.get("background"): h.append(f"BACKGROUND {meta['background']}")
    if meta.get("video"):
        h.append(f"VIDEO {meta['video']}")
        if meta.get("video_offset_ms"): h.append(f"VIDEO_OFFSET_MS {meta['video_offset_ms']}")
    h.append("BACKGROUND_DIM 30")
    lines = h + [""]
    starts, ci = sorted(comments), 0
    for b, lane, typ, extra in notes:
        while ci < len(starts) and starts[ci] <= b + 1e-6:
            lines.append(f"\n# --- {comments[starts[ci]]} ---")
            ci += 1
        if typ == "TAP": lines.append(f"{fmt(b)} {lane} TAP")
        elif typ == "HOLD": lines.append(f"{fmt(b)} {lane} HOLD {fmt(extra)}")
        elif typ == "SCRATCH": lines.append(f"{fmt(b)} SCRATCH {extra}")
        else: lines.append(f"{fmt(b)} {lane} DOUBLE {extra}")
    return "\n".join(lines) + "\n"


def stats(notes):
    n = len(notes)
    last = int(max((x[0] for x in notes), default=0))
    peak = max((sum(1 for x in notes if w <= x[0] < w + 8) for w in range(0, last + 1, 2)), default=0) / 8
    return (f"{n:4d} notes, {sum(x[2] == 'HOLD' for x in notes):3d} tenues, "
            f"{sum(x[2] == 'DOUBLE' for x in notes):3d} doubles, {sum(x[2] == 'SCRATCH' for x in notes):2d} scratchs, "
            f"pic {peak:.2f} notes/temps")


def analyze(audio, bpm=None, offset_ms=None, log=print, meter=None):
    """audio : chemin du fichier, ou (échantillons mono float32, fréquence) déjà décodés."""
    y, sr = audio if isinstance(audio, tuple) else load_mono(audio)
    on, low, fps = onset_envelope(y, sr)
    warnings = []
    ph = None
    if bpm is None:
        bpm, ph = estimate_tempo(on, fps)
        drift = tempo_drift(on, fps, bpm, ph)
        if drift > 0.03:
            warnings.append(f"tempo peut-être variable (dérive de phase jusqu'à {drift * 1000:.0f} ms) : "
                            f"la carte risque de se décaler ; à vérifier en jeu")
    L = 60.0 / bpm
    f, t, harm, perc, hfps = hpss(y, sr)
    if offset_ms is None:
        if ph is None:
            ph = max((grid_score(on, fps, bpm, p), p) for p in np.arange(0, L / 2, 0.002))[1]
        # croche ou temps ? la grosse caisse tombe sur les temps
        a = grid_score(low, fps, bpm, ph, step=1.0)
        b = grid_score(low, fps, bpm, ph + L / 2, step=1.0)
        beat_ph = ph if a >= b else ph + L / 2
        if min(a, b) > 0.85 * max(a, b):
            warnings.append("temps / contretemps difficile à distinguer : l'offset peut être décalé d'une croche")
        offset_ms = int(round(((beat_ph - ONSET_BIAS_S) % L) * 1000))
    song = Song(f, t, harm, perc, hfps, bpm, offset_ms / 1000.0)
    # début de mesure : le temps 0 est le premier temps de la piste, pas forcément un premier temps de mesure
    song.meter, m3, m4 = estimate_meter(song) if meter is None else (meter, 0, 0)
    if meter is None and 1.15 < m3 / max(m4, 1e-6) < 1.45:
        warnings.append(f"mesure à {song.meter} temps choisie de justesse (indices 3 temps {m3:.2f} / 4 temps {m4:.2f}) : "
                        f"à vérifier (--meter 3 ou 4)")
    song.bar0, scores = downbeat_phase(song)
    sc = sorted(scores)
    if len(sc) > 1 and sc[-1] - sc[-2] < 0.02 * sc[-1]:
        warnings.append("début de mesure incertain : les sections peuvent être décalées d'un ou deux temps")
    return song, bpm, offset_ms, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("audio", help="piste, dans le dossier de la map (assets/songs/<ID>/audio.ogg)")
    ap.add_argument("--name", help="ID de la map (par défaut : nom du dossier de la piste)")
    ap.add_argument("--title")
    ap.add_argument("--artist", default="")
    ap.add_argument("--pack", default="", help="pack du morceau (optionnel : vue par pack du menu)")
    ap.add_argument("--bpm", type=float)
    ap.add_argument("--offset-ms", type=int)
    ap.add_argument("--approach-ms", type=int, choices=range(500, 5001), metavar="MS",
                    help="trajet explicite (500–5000 ms ; défaut 1500), y compris avec --stdout")
    ap.add_argument("--approach", type=parse_approach, action="append", default=[], metavar="DIFFICULTÉ=MS",
                    help="choix d'auteur pour une difficulté, répétable ; aucune progression imposée")
    ap.add_argument("--background", default="", help="fichier du dossier de la map, ex. background.jpg")
    ap.add_argument("--video", default="", help="fichier du dossier de la map, ex. video.mpg")
    ap.add_argument("--video-offset-ms", type=int, default=0)
    ap.add_argument("--meter", type=int, choices=[3, 4], help="temps par mesure (sinon détecté)")
    ap.add_argument("--force", action="store_true", help="écraser les cartes existantes (les records sont liés au nom)")
    ap.add_argument("--stdout", choices=list(DIFF), metavar="DIFFICULTÉ",
                    help="n'écrit rien : affiche seulement les notes de cette difficulté (utilisé par l'éditeur)")
    ap.add_argument("--no-holds", action="store_true", help="avec --stdout : tenues remplacées par des TAP")
    ap.add_argument("--analyze", action="store_true",
                    help="n'écrit rien : affiche en JSON le tempo, l'offset, la mesure et les points à vérifier "
                         "(bouton « Détecter BPM » de l'éditeur)")
    args = ap.parse_args()
    if args.analyze:
        return print_analysis(args)
    if args.stdout:
        return print_notes(args)
    if not args.title:
        ap.error("--title est requis (sauf avec --stdout)")
    args.name = args.name or Path(args.audio).resolve().parent.name
    run(args)


# Bouton « Détecter BPM » et brouillon automatique de l'éditeur : appelés par la ligne de commande
# (version développeur) et directement par la version en ligne (Pyodide, échantillons décodés par le navigateur).

def analysis_summary(y, sr, bpm=None, offset_ms=None, meter=None, log=print):
    """Même analyse que make song : tempo, offset, mesure et points à vérifier."""
    song, bpm, offset_ms, warnings = analyze((y, sr), bpm, offset_ms, log, meter)
    L = 60000.0 / bpm
    return {"bpm": bpm, "offset_ms": offset_ms, "duration_s": round(len(y) / sr, 2),
            "meter": song.meter, "bar0": song.bar0,
            "downbeat_ms": int(round(offset_ms + song.bar0 * L)), "warnings": warnings}


def draft_notes(y, sr, level, name, bpm=None, offset_ms=None, holds=True, meter=None, log=print,
                approach_ms=GAME_DEFAULT_APPROACH_MS):
    """Notes d'une seule difficulté (sections en commentaires), sans l'en-tête ; et les points à vérifier."""
    song, bpm, offset_ms, warnings = analyze((y, sr), bpm, offset_ms, log, meter)
    sections, first, last = find_sections(song)
    comp = Composer(song, sections, first, last)
    comp.song_name = name                       # personnalité propre à la chanson
    comp.approach_override = max(500, min(5000, int(approach_ms)))
    notes, comments = comp.compose_checked(level)
    if not holds:
        notes = [(b, lane, "TAP", 0.0) if typ == "HOLD" else (b, lane, typ, extra) for b, lane, typ, extra in notes]
    body = chart_text(dict(id="", title="", audio=""), level, notes, comments, bpm, offset_ms)
    return (body.split("\n\n", 1)[1] if "\n\n" in body else ""), warnings


def print_analysis(args):
    """Résumé de l'analyse en une ligne JSON sur stdout (journal sur stderr)."""
    import json
    log = lambda *a: print(*a, file=sys.stderr)
    y, sr = load_mono(args.audio)
    print(json.dumps(analysis_summary(y, sr, args.bpm, args.offset_ms, args.meter, log), ensure_ascii=False))


def print_notes(args):
    """Notes d'une seule difficulté sur stdout, journal sur stderr."""
    log = lambda *a: print(*a, file=sys.stderr)
    y, sr = load_mono(args.audio)
    name = args.name or Path(args.audio).resolve().parent.name
    text, warnings = draft_notes(y, sr, args.stdout, name, args.bpm, args.offset_ms, not args.no_holds, args.meter, log,
                                approach_ms=args.approach_ms or GAME_DEFAULT_APPROACH_MS)
    print(text)
    for w in warnings:
        log("à vérifier : " + w)


def run(args, log=print):
    audio = Path(args.audio).resolve()
    folder = audio.parent                       # une map = un dossier : les cartes vont à côté de la piste
    rel_audio = audio.name
    outs = {d: folder / f"{d.lower()}.chart" for d in DIFF}
    existing = [p.name for p in outs.values() if p.exists()]
    if existing and not args.force:
        sys.exit(f"cartes déjà présentes : {', '.join(existing)} (ajoute --force / FORCE=1 pour les écraser)")

    log(f"Analyse de {rel_audio}…")
    song, bpm, offset_ms, warnings = analyze(audio, args.bpm, args.offset_ms, log, getattr(args, "meter", None))
    sections, first, last = find_sections(song)
    comp = Composer(song, sections, first, last)
    comp.song_name = args.name
    comp.approach_override = getattr(args, "approach_ms", None)
    comp.approach_by_difficulty = dict(getattr(args, "approach", []) or [])
    meta = dict(id=args.name, title=args.title, artist=args.artist, pack=getattr(args, "pack", "") or "", audio=rel_audio,
                background=args.background,
                video=args.video, video_offset_ms=args.video_offset_ms, meter=song.meter)

    ch = comp.character()
    report_char = (f"Caractère musical : voix qui répète ses notes {ch['repeat']*100:.0f} % des mesures, mélodie qui bouge "
                   f"{ch['motion']*100:.0f} %, dialogue voix / batterie {ch['dialog']*100:.0f} %"
                   + (", batterie régulière" if ch['steady'] else "") + (", tempo lent" if ch['slow'] else ""))
    report = [f"{args.title} — {args.artist}".rstrip(" —"),
              f"BPM {fmt(bpm)}, OFFSET_MS {offset_ms}, mesure à {song.meter} temps, premier temps de mesure : temps {song.bar0}, "
              f"musique du temps {first} au temps {last}"
              + (" (tempo rapide : densité réduite)" if comp.dens_scale < 1 else "")
              + ", trajet par défaut 1500 ms sauf choix d'auteur explicite",
              report_char, "", "Sections (temps) :"]
    for s in sections:
        extra = f", reprise de {s[0] - s[5]}-{s[1] - s[5]} (décalage {s[5]})" if s[3] == "copy" else f", focus {s[3]}"
        report.append(f"  {s[0]:4d}-{s[1]:4d}  {s[2]}{extra}")
    report.append("")
    for d in DIFF:
        notes, comments = comp.compose_checked(d)
        outs[d].write_text(chart_text(dict(meta, approach_ms=comp.cfg(d)["approach_ms"]), d, notes, comments, bpm, offset_ms), encoding="utf-8")
        report.append(f"  {d:6s} {stats(notes)}, trajet {comp.cfg(d)['approach_ms']} ms  -> {outs[d].relative_to(ROOT) if outs[d].is_relative_to(ROOT) else outs[d]}")
        arr = comp.arrangers.get(d)
        if arr:
            report.append("         personnalité : " + ", ".join(FEELS[k] for k in arr.favorites[:3]))
            report.append("         forme : " + ", ".join(f"{v} {k}" for k, v in arr.forms.most_common()))
        report.append(f"         fin : {getattr(comp, 'ending_desc', '?')}")
    if warnings:
        report += ["", "À vérifier :"] + [f"  - {w}" for w in warnings]
    text = "\n".join(report)
    REPORTS.mkdir(parents=True, exist_ok=True)
    (REPORTS / f"{args.name}.txt").write_text(text + "\n", encoding="utf-8")
    log(text)
    return text


if __name__ == "__main__":
    main()
