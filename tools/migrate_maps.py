#!/usr/bin/env python3
"""Range les cartes de l'ancien format (assets/charts/*.chart + médias à plat dans assets/songs/) au format de map 1 :
une map = un dossier assets/songs/<ID>/ avec ses .chart (un par difficulté) et ses médias.

    python3 tools/migrate_maps.py            # simulation : affiche ce qui serait fait
    python3 tools/migrate_maps.py --apply    # déplace les fichiers et réécrit les en-têtes

Une map par piste audio : ID = nom de la piste (songs/<ID>.ogg). Dans le dossier, les fichiers prennent des noms
fixes (audio.ogg, background.jpg, video.mpg, <difficulté>.chart). Un journal JSON des déplacements est écrit dans
art/backups/ pour pouvoir revenir en arrière à la main.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
CHARTS = ASSETS / "charts"
SONGS = ASSETS / "songs"
MEDIA_KEYS = {"AUDIO": "audio", "BACKGROUND": "background", "VIDEO": "video"}
HEADER = re.compile(r"^\s*(AUDIO|BACKGROUND|VIDEO|DIFFICULTY|FORMAT)\s+(\S+)")
ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")


def read_header(text):
    h = {}
    for line in text.splitlines():
        m = HEADER.match(line)
        if m:
            h.setdefault(m.group(1), m.group(2))
    return h


def rewrite(text, map_id, media_names):
    """En-tête au format 1 : FORMAT/ID/VERSION avant la première ligne utile, médias renommés."""
    out, inserted = [], False
    for line in text.splitlines():
        m = HEADER.match(line)
        if not inserted and line.strip() and not line.lstrip().startswith("#"):
            out += ["FORMAT 1", f"ID {map_id}", "VERSION 1"]
            inserted = True
        if m and m.group(1) in MEDIA_KEYS:
            out.append(f"{m.group(1)} {media_names[m.group(1)]}")
            continue
        out.append(line)
    return "\n".join(out) + "\n"


def plan():
    maps = {}
    for chart in sorted(CHARTS.glob("*.chart")):
        text = chart.read_text(encoding="utf-8", errors="replace")
        h = read_header(text)
        if h.get("FORMAT"):
            continue  # déjà au nouveau format
        audio = h.get("AUDIO", "")
        map_id = Path(audio).stem if audio else chart.stem.rsplit("_", 1)[0]
        if not ID_RE.match(map_id):
            sys.exit(f"{chart.name} : ID '{map_id}' invalide")
        maps.setdefault(map_id, []).append((chart, text, h))

    actions = []   # (source, destination, nouveau texte ou None pour un simple déplacement)
    for map_id, charts in maps.items():
        folder = SONGS / map_id
        media_names, used = {}, set()
        for key, base in MEDIA_KEYS.items():
            srcs = {h[key] for _, _, h in charts if h.get(key)}
            if len(srcs) > 1:
                sys.exit(f"{map_id} : {key} diffère selon la difficulté ({srcs})")
            if srcs:
                src = ASSETS / srcs.pop()
                name = base + src.suffix.lower()
                media_names[key] = name
                if src.is_file():
                    actions.append((src, folder / name, None))
                else:
                    print(f"  ! {map_id} : {src.relative_to(ASSETS)} introuvable (ligne {key} gardée)")
        for chart, text, h in charts:
            name = (h.get("DIFFICULTY") or "normal").lower()
            name = re.sub(r"[^a-z0-9_-]", "_", name)
            while name in used:
                name += "_2"
            used.add(name)
            actions.append((chart, folder / f"{name}.chart", rewrite(text, map_id, media_names)))
    return maps, actions


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apply", action="store_true", help="faire les déplacements (sinon simulation)")
    args = ap.parse_args()

    maps, actions = plan()
    if not actions:
        print("Rien à migrer : toutes les cartes sont déjà au format 1.")
        return
    moved = {src for src, _, _ in actions}
    orphans = [p for p in SONGS.iterdir() if p.is_file() and p not in moved and not p.name.startswith(".")]
    print(f"{len(maps)} maps, {sum(len(c) for c in maps.values())} cartes, {len(actions)} fichiers")
    for src, dst, _ in actions[:8]:
        print(f"  {src.relative_to(ROOT)} -> {dst.relative_to(ROOT)}")
    if len(actions) > 8:
        print(f"  ... ({len(actions) - 8} de plus)")
    if orphans:
        print(f"Fichiers de assets/songs/ référencés par aucune carte (laissés en place) : {[p.name for p in orphans]}")
    if not args.apply:
        print("Simulation : relancer avec --apply pour migrer.")
        return

    for _, dst, _ in actions:
        if dst.exists():
            sys.exit(f"{dst.relative_to(ROOT)} existe déjà : migration annulée, rien n'a été déplacé")
    journal = []
    for src, dst, text in actions:
        dst.parent.mkdir(parents=True, exist_ok=True)
        if text is None:
            src.rename(dst)
        else:
            dst.write_text(text, encoding="utf-8")
            src.unlink()
        journal.append([str(src.relative_to(ROOT)), str(dst.relative_to(ROOT))])
    backups = ROOT / "art" / "backups"
    backups.mkdir(parents=True, exist_ok=True)
    log = backups / f"migrate_maps_{time.strftime('%Y%m%d-%H%M%S')}.json"
    log.write_text(json.dumps(journal, indent=1), encoding="utf-8")
    print(f"Migration faite. Journal des déplacements : {log.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
