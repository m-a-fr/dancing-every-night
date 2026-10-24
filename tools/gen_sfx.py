#!/usr/bin/env python3
"""Génère les bruitages d'interface (WAV 16 bits mono 44100 Hz) dans assets/audio/sfx/.

Synthèse pure Python (aucune dépendance) : bips carrés/triangle, sweeps et arpèges façon arcade.
Usage : python3 tools/gen_sfx.py
"""
import math
import os
import struct
import wave

RATE = 44100
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "audio", "sfx")


def osc(kind, phase):
    p = phase % 1.0
    if kind == "square":
        return 1.0 if p < 0.5 else -1.0
    if kind == "pulse":
        return 1.0 if p < 0.25 else -1.0
    if kind == "tri":
        return 4.0 * abs(p - 0.5) - 1.0
    if kind == "saw":
        return 2.0 * p - 1.0
    return math.sin(2 * math.pi * p)


def note(freq, ms, kind="square", vol=0.5, f_end=None, attack_ms=2, decay=3.0, vibrato=0.0):
    """Une note : fréquence (glissant vers f_end si donné), enveloppe attaque + décroissance exponentielle."""
    n = int(RATE * ms / 1000)
    out, phase = [], 0.0
    for i in range(n):
        t = i / n
        f = freq if f_end is None else freq + (f_end - freq) * t
        if vibrato:
            f *= 1.0 + vibrato * math.sin(2 * math.pi * 6 * i / RATE)
        phase += f / RATE
        a = min(1.0, i / max(1, RATE * attack_ms / 1000))
        env = a * math.exp(-decay * t)
        out.append(osc(kind, phase) * env * vol)
    return out


def seq(*parts):
    out = []
    for p in parts:
        out += p
    return out


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def silence(ms):
    return [0.0] * int(RATE * ms / 1000)


def hz(semi_from_a4):
    return 440.0 * 2 ** (semi_from_a4 / 12)


def arp(semis, step_ms, kind="square", vol=0.4, tail_ms=0):
    parts = [note(hz(s), step_ms, kind, vol, decay=2.0) for s in semis]
    if tail_ms:
        parts.append(note(hz(semis[-1]), tail_ms, kind, vol, decay=4.0))
    return seq(*parts)


def noise_burst(ms, vol=0.3, decay=6.0):
    import random
    rnd = random.Random(7)
    n = int(RATE * ms / 1000)
    return [rnd.uniform(-1, 1) * vol * math.exp(-decay * i / n) for i in range(n)]


def band_noise(ms, f0, q=1.2, decay=8.0, vol=1.0, seed=3):
    """Bruit passe-bande (filtre à variables d'état) à enveloppe exponentielle : base d'un claquement de mains."""
    import random
    rnd = random.Random(seed)
    n = int(RATE * ms / 1000)
    f = 2 * math.sin(math.pi * f0 / RATE)
    damp = 1.0 / q
    low = band = 0.0
    out = []
    for i in range(n):
        x = rnd.uniform(-1, 1)
        low += f * band
        high = x - low - damp * band
        band += f * high
        out.append(band * vol * math.exp(-decay * i / n))
    return out


def clap(f0=1500, bursts=3, gap_ms=9, tail_ms=70, vol=1.0, q=1.1, seed=3):
    """Clap : quelques petits bruits rapprochés (les mains qui ne tombent pas pile ensemble) puis une queue courte."""
    parts = []
    for b in range(bursts):
        parts.append(mix(silence(b * gap_ms), band_noise(gap_ms * 1.4, f0 * (1 + 0.05 * b), q, 6.0, vol, seed + b)))
    head = parts[0]
    for part in parts[1:]:
        head = mix(head, part)
    tail = mix(silence(bursts * gap_ms), band_noise(tail_ms, f0 * 0.9, q, 7.0, vol * 0.8, seed + 9))
    return mix(head, tail)


# Gain final par son (0..1 du plein volume) : les sons de frappe restent discrets sous la chanson
GAINS = {"perfect": 0.38, "good": 0.30, "miss": 0.16, "scratch": 0.40}


SOUNDS = {
    # --- navigation
    "cursor":      note(hz(12), 45, "square", 0.35, decay=5.0),
    "confirm":     seq(note(hz(7), 55, "square", 0.4, decay=2.0), note(hz(14), 110, "square", 0.4, decay=4.0)),
    "cancel":      seq(note(hz(7), 55, "square", 0.35, decay=2.0), note(hz(0), 100, "square", 0.35, decay=4.0)),
    "tab":         mix(note(hz(9), 90, "tri", 0.5, f_end=hz(16), decay=3.0), note(hz(16), 90, "square", 0.15, decay=4.0)),
    "difficulty":  seq(note(hz(4), 40, "pulse", 0.35, decay=3.0), note(hz(11), 70, "pulse", 0.35, decay=4.0)),
    # --- départ / pause
    "start":       seq(arp([0, 4, 7, 12], 70, "square", 0.4), note(hz(19), 320, "square", 0.45, decay=3.0, vibrato=0.01)),
    "pause_open":  note(hz(14), 140, "square", 0.35, f_end=hz(2), decay=3.0),
    "pause_close": note(hz(2), 140, "square", 0.35, f_end=hz(14), decay=3.0),
    "tick":        note(hz(7), 90, "square", 0.4, decay=5.0),
    "tick_go":     mix(note(hz(19), 260, "square", 0.45, decay=3.0), note(hz(7), 260, "tri", 0.3, decay=3.0)),
    # --- résultats
    "score_tick":  note(hz(19), 22, "square", 0.22, decay=6.0),
    "rank_s":      seq(arp([0, 4, 7, 12, 16, 19, 24], 85, "square", 0.4), note(hz(28), 650, "square", 0.45, decay=2.5, vibrato=0.012)),
    "rank_a":      seq(arp([0, 4, 7, 12, 16], 90, "square", 0.4), note(hz(19), 500, "square", 0.42, decay=3.0)),
    "rank_b":      seq(arp([0, 4, 7, 12], 100, "tri", 0.5), note(hz(12), 420, "tri", 0.5, decay=3.0)),
    "rank_c":      seq(arp([0, 3, 7], 110, "tri", 0.5), note(hz(7), 380, "tri", 0.5, decay=3.5)),
    "rank_d":      seq(arp([7, 3, 0, -5], 130, "tri", 0.5), note(hz(-12), 500, "tri", 0.5, decay=3.0)),
    "new_record":  seq(arp([12, 16, 19, 24], 65, "pulse", 0.4), silence(30), arp([12, 16, 19, 24, 28], 65, "pulse", 0.4, tail_ms=420)),
    "full_combo":  seq(arp([7, 12, 16, 19], 70, "square", 0.38), note(hz(24), 520, "square", 0.42, decay=2.5)),
    "all_perfect": seq(arp([0, 7, 12, 16, 19, 24, 28], 65, "saw", 0.3), mix(note(hz(31), 800, "square", 0.35, decay=2.0, vibrato=0.015), note(hz(24), 800, "tri", 0.4, decay=2.0))),
    # --- divers
    "boot":        seq(note(hz(-5), 90, "square", 0.35), note(hz(0), 90, "square", 0.35), note(hz(7), 90, "square", 0.35), note(hz(12), 380, "square", 0.4, decay=3.0)),
    "quit":        note(hz(12), 280, "square", 0.35, f_end=hz(-12), decay=2.0),
    # --- gameplay (optionnels)
    # frappes : claps (bruit passe-bande en rafale courte), sobres sous la chanson ; GAINS règle le niveau
    "perfect":     clap(f0=1800, bursts=3, gap_ms=8, tail_ms=70, q=1.2, seed=3),
    "good":        clap(f0=1100, bursts=2, gap_ms=10, tail_ms=55, q=0.9, seed=11),
    "miss":        band_noise(70, 350, 0.8, 6.0, 1.0, seed=21),
    "scratch":     mix(band_noise(140, 2600, 1.4, 4.0, 0.9, seed=31), clap(f0=1400, bursts=2, gap_ms=14, tail_ms=60, vol=0.6, seed=41)),
}


def write(name, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = (0.9 / peak if name in GAINS else min(1.0, 0.9 / peak)) * GAINS.get(name, 1.0)   # normalisé, puis gain par son
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))
    return path


if __name__ == "__main__":
    total = 0
    for name, data in SOUNDS.items():
        p = write(name, data)
        total += os.path.getsize(p)
        print(f"{name:12s} {len(data) * 1000 // RATE:5d} ms")
    print(f"{len(SOUNDS)} fichiers, {total // 1024} Ko")
