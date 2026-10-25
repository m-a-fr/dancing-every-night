#!/usr/bin/env python3
"""Catalogue de la bibliothèque de chansons : library/catalog.json.

    tools/catalog.py add chanson.zip [--out dossier] [--submitter pseudo] [--issue 12]
    tools/catalog.py remove <ID> [--reason texte]
    tools/catalog.py check

`add` valide la chanson avec le jeu lui-même (vitagame --check-json), calcule l'empreinte SHA-256, prépare la vignette
(480x272, tirée de l'image de fond) et ajoute ou met à jour l'entrée du catalogue ; le zip et la vignette sont copiés dans
--out (par défaut library/dist/), prêts à être envoyés sur R2. Une mise à jour exige une version supérieure.
`remove` retire l'entrée et ajoute une ligne au journal des retraits (library/removals.json).

Le catalogue ne contient que des clés relatives (« songs/<ID>-v<version>.zip ») : l'adresse de téléchargement vient de
library/config.json (download_base), pour que le site comme le futur store du jeu puissent le lire tel quel.
Bibliothèque seulement : les chansons officielles (audio non redistribuable seul) n'y figurent pas.
"""
import argparse, datetime, hashlib, json, os, re, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "library"
CATALOG = LIB / "catalog.json"
REMOVALS = LIB / "removals.json"
SCHEMA = 1
RANK = {"EASY": 0, "NORMAL": 1, "HARD": 2, "EXPERT": 3}


def vitagame_path():
    for c in (os.environ.get("VITAGAME"), ROOT / "build-desktop" / "vitagame", ROOT / "build-desktop" / "Debug" / "vitagame"):
        if c and Path(c).is_file():
            return str(c)
    sys.exit("vitagame introuvable : make desktop, ou VITAGAME=/chemin/vitagame")


def load(path, default):
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else default


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=False) + "\n", encoding="utf-8")


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


def diff_key(d):
    # même ordre que le menu du jeu : ORDER s'il est posé, sinon EASY..EXPERT (« EASY 2 » avec EASY), puis le nombre de notes
    base = RANK.get(d["name"].upper().split(" ")[0], len(RANK))
    return (d["order"] if d["order"] > 0 else 10**6, base, d["notes"])


def check_zip(zip_path):
    out = subprocess.run([vitagame_path(), "--check-json", str(zip_path)], capture_output=True, text=True, timeout=120)
    try:
        return json.loads(out.stdout)
    except ValueError:
        sys.exit(f"le validateur n'a pas rendu de JSON :\n{out.stdout[-400:]}\n{out.stderr[-400:]}")


def background_name(zf):
    for n in zf.namelist():
        if n.endswith(".chart"):
            m = re.search(r"^\s*BACKGROUND\s+(\S+)", zf.read(n).decode("utf-8", "replace"), re.M)
            if m:
                return m.group(1)
    return None


def make_thumb(zip_path, dest):
    """Vignette 480x272 (rognée pour remplir) tirée de l'image de fond ; None si impossible."""
    try:
        from PIL import Image, ImageOps
    except ImportError:
        print("Pillow absent : pas de vignette (pip install pillow)", file=sys.stderr)
        return False
    with zipfile.ZipFile(zip_path) as zf:
        name = background_name(zf)
        member = next((n for n in zf.namelist() if name and n.split("/")[-1] == name), None)
        if not member:
            return False
        with zf.open(member) as fh:
            im = Image.open(fh).convert("RGB")
    dest.parent.mkdir(parents=True, exist_ok=True)
    ImageOps.fit(im, (480, 272), Image.LANCZOS).save(dest, quality=82, optimize=True)
    return True


def cmd_add(a):
    zip_path = Path(a.zip)
    rep = check_zip(zip_path)
    for m in rep["messages"]:
        print(f"{m['level']:5} {m['text']}")
    if not rep["ok"]:
        sys.exit(f"refusée : {rep['errors']} erreur(s)")
    for key in ("id", "title", "mapper"):   # la licence est facultative
        if not rep[key]:
            sys.exit(f"refusée : champ obligatoire vide pour la bibliothèque : {key.upper() if key != 'id' else 'ID'}")
    catalog = load(CATALOG, {"schema": SCHEMA, "songs": []})
    songs = catalog["songs"]
    old = next((s for s in songs if s["id"] == rep["id"]), None)
    if old and rep["version"] <= old["version"] and not a.force:
        sys.exit(f"refusée : {rep['id']} existe en version {old['version']} ; une mise à jour demande une version supérieure "
                 f"(VERSION {rep['version']} dans les difficultés)")
    out = Path(a.out) if a.out else LIB / "dist"
    key = f"songs/{rep['id']}-v{rep['version']}.zip"
    (out / "songs").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(zip_path, out / key)
    thumb = f"thumbs/{rep['id']}-v{rep['version']}.jpg"
    entry = {
        "id": rep["id"], "version": rep["version"], "title": rep["title"], "artist": rep["artist"], "pack": rep["pack"],
        "mapper": rep["mapper"], "source": rep["source"], "license": rep["license"],
        "difficulties": sorted(({"name": d["name"], "notes": d["notes"], "bpm": d["bpm"], "order": d["order"]}
                                for d in rep["difficulties"]), key=diff_key),
        "duration_s": rep["duration_s"], "size_bytes": rep["size_bytes"], "has_video": rep["has_video"],
        "zip": key, "zip_bytes": zip_path.stat().st_size, "sha256": hashlib.sha256(zip_path.read_bytes()).hexdigest(),
        "thumb": thumb if make_thumb(zip_path, out / thumb) else None,
        "added": old["added"] if old else now(), "updated": now(),
    }
    for d in entry["difficulties"]:
        d.pop("order")
    if a.submitter:
        entry["submitter"] = a.submitter
    if a.issue:
        entry["issue"] = a.issue
    if old:
        songs[songs.index(old)] = entry
    else:
        songs.append(entry)
    songs.sort(key=lambda s: (s["title"].lower(), s["id"]))
    save(CATALOG, catalog)
    print(("mise à jour" if old else "ajoutée") + f" : {entry['id']} v{entry['version']} ({entry['zip_bytes'] / 1048576:.1f} Mo, "
          f"{len(entry['difficulties'])} difficulté(s)) -> {out}")


def cmd_remove(a):
    catalog = load(CATALOG, {"schema": SCHEMA, "songs": []})
    old = next((s for s in catalog["songs"] if s["id"] == a.id), None)
    if not old:
        sys.exit(f"{a.id} n'est pas dans le catalogue")
    catalog["songs"].remove(old)
    save(CATALOG, catalog)
    log = load(REMOVALS, {"schema": SCHEMA, "removals": []})
    log["removals"].append({"id": old["id"], "version": old["version"], "title": old["title"], "date": now(), "reason": a.reason or ""})
    save(REMOVALS, log)
    print(f"retirée : {a.id} (fichiers à supprimer de R2 : {old['zip']}, {old.get('thumb')})")


def cmd_check(_a):
    catalog = load(CATALOG, {"schema": SCHEMA, "songs": []})
    errors, seen = [], set()
    for s in catalog["songs"]:
        i = s.get("id", "?")
        if i in seen:
            errors.append(f"{i}: ID en double")
        seen.add(i)
        for k in ("id", "version", "title", "mapper", "difficulties", "zip", "sha256", "size_bytes"):
            if not s.get(k):
                errors.append(f"{i}: champ manquant ou vide : {k}")
        if s.get("sha256") and not re.fullmatch(r"[0-9a-f]{64}", s["sha256"]):
            errors.append(f"{i}: sha256 invalide")
        if s.get("zip") and not re.fullmatch(r"songs/[A-Za-z0-9_-]+-v\d+\.zip", s["zip"]):
            errors.append(f"{i}: clé du zip invalide : {s['zip']}")
    print(f"{len(catalog['songs'])} chanson(s), {len(errors)} problème(s)")
    for e in errors:
        print("  ", e)
    sys.exit(1 if errors else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("add"); p.add_argument("zip"); p.add_argument("--out"); p.add_argument("--submitter")
    p.add_argument("--issue", type=int); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_add)
    p = sub.add_parser("remove"); p.add_argument("id"); p.add_argument("--reason"); p.set_defaults(fn=cmd_remove)
    p = sub.add_parser("check"); p.set_defaults(fn=cmd_check)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
