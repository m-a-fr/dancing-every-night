#!/usr/bin/env python3
"""Rapport de validation d'une chanson proposée à la bibliothèque (commentaire d'issue, en anglais).

    tools/library_report.py chanson.zip [--submitter pseudo] [--status-file fichier]

Utilise le validateur du jeu (vitagame --check-json) puis les règles propres à la bibliothèque : champs obligatoires
(TITLE, MAPPER ; la licence est facultative : jamais signalée comme manquante dans le commentaire public), ID qui ne heurte pas une chanson officielle, version supérieure en cas de mise à jour, mise à jour
par l'auteur d'origine. Écrit le rapport en Markdown sur stdout et « valid » ou « invalid » dans --status-file.
Utilisé par .github/workflows/song-validate.yml ; ne modifie rien (la publication est faite par tools/catalog.py).
"""
import argparse, hashlib, json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import catalog as cat  # noqa: E402

ROOT = cat.ROOT
MIN_NOTES = 20


def official_ids():
    d = ROOT / "assets" / "songs"
    return {p.name for p in d.iterdir() if p.is_dir()} if d.is_dir() else set()


def mmss(s):
    s = int(round(s))
    return f"{s // 60}:{s % 60:02d}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("--submitter", default="")
    ap.add_argument("--status-file")
    a = ap.parse_args()

    rep = cat.check_zip(a.zip)
    problems = [("ERROR", m["text"]) for m in rep["messages"] if m["level"] == "ERROR"]
    notes = [("WARN", m["text"]) for m in rep["messages"] if m["level"] == "WARN"]

    def err(t): problems.append(("ERROR", t))
    def warn(t): notes.append(("WARN", t))

    if rep["ok"] or rep["id"]:
        for key, label in (("title", "TITLE"), ("mapper", "MAPPER (the author's name)")):
            if not rep[key]:
                err(f"missing {label}: the library needs it on every difficulty")
        catalog = cat.load(cat.CATALOG, {"songs": []})
        if rep["id"] in official_ids():
            err(f"the ID `{rep['id']}` belongs to an official song of the game: choose another ID")
        old = next((s for s in catalog["songs"] if s["id"] == rep["id"]), None)
        if old:
            if rep["version"] <= old["version"]:
                err(f"`{rep['id']}` is already in the library at version {old['version']}: an update needs a higher VERSION "
                    f"(this zip says {rep['version']})")
            elif old.get("submitter") and a.submitter and old["submitter"].lower() != a.submitter.lower():
                warn(f"this updates a song first submitted by @{old['submitter']}: a maintainer will check with them")
        for d in rep["difficulties"]:
            if d["notes"] < MIN_NOTES:
                warn(f"{d['name']} has only {d['notes']} notes")
        if not rep["has_background"]:
            warn("no background image: the song will show a plain card in the library")

    ok = not problems and rep["ok"]
    out = [f"### {'✅ Ready for review' if ok else '❌ Needs changes'}", ""]
    out += ["Source SHA-256: `" + hashlib.sha256(Path(a.zip).read_bytes()).hexdigest() + "`", ""]
    if rep["id"]:
        out += ["| | |", "|---|---|",
                f"| Song | **{rep['title'] or '?'}** — {rep['artist'] or 'unknown artist'} |",
                f"| ID | `{rep['id']}` (version {rep['version']}) |",
                f"| Author | {rep['mapper'] or '—'} |",
                f"| Length | {mmss(rep['duration_s'])} |", f"| Size | {rep['size_bytes'] / 1048576:.1f} MB |",
                f"| Video | {'yes' if rep['has_video'] else 'no'} |"] + ([f"| Licence | {rep['license']} |"] if rep["license"] else []) + [""]
        diffs = sorted(rep["difficulties"], key=cat.diff_key)
        out += ["| Difficulty | Notes | BPM |", "|---|---|---|"] + [f"| {d['name']} | {d['notes']} | {d['bpm']:.0f} |" for d in diffs] + [""]
    if problems:
        out += ["**To fix**", ""] + [f"- ❌ {t}" for _, t in problems] + [""]
    if notes:
        out += ["**Worth a look**", ""] + [f"- ⚠️ {t}" for _, t in notes] + [""]
    if ok:
        out += ["The file passes the game's own checks. A maintainer will listen to it, then approve it or ask for changes. "
                "Thank you for sharing!"]
    else:
        out += ["Fix these, export the `.zip` again and attach it to this issue (edit the issue, or comment with the new file): "
                "it is checked again automatically. You can also run the same check yourself with `make check MAP=your_song.zip`."]
    print("\n".join(out))
    if a.status_file:
        Path(a.status_file).write_text("valid" if ok else "invalid")


if __name__ == "__main__":
    main()
