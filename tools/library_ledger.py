#!/usr/bin/env python3
"""Registre PRIVÉ des chansons publiées sans licence (visible du seul mainteneur).

    tools/library_ledger.py note.json registre.md

Ajoute une ligne au tableau Markdown du dépôt privé de modération (m-a-fr/dne-moderation) pour la note écrite par
tools/library_publish.py quand la chanson n'a pas de LICENSE. Le dépôt public ne sait rien de cette information : ni label,
ni commentaire, ni journal d'Actions (ils sont publics), donc ce script n'affiche rien d'autre que « registre mis à jour ».
"""
import json, sys
from pathlib import Path

HEADER = ("# Songs published without a LICENSE (private)\n\n"
          "Written by the publication workflow; the public repository has no trace of it. Check these first if a rights\n"
          "holder writes in.\n\n"
          "| Date | Proposal | Published ID | v | Title | Artist | Author | Submitter |\n|---|---|---|---|---|---|---|---|\n")


def cell(v):
    return str(v).replace("|", "\\|").replace("\n", " ").strip() or "—"


def main():
    note = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    ledger = Path(sys.argv[2])
    if not ledger.exists() or not ledger.read_text(encoding="utf-8").strip():
        ledger.write_text(HEADER, encoding="utf-8")
    row = "| " + " | ".join(cell(note[k]) for k in ("date", "issue", "id", "version", "title", "artist", "mapper", "submitter")) + " |\n"
    row = row.replace(f"| {cell(note['issue'])} |", f"| #{note['issue']} |", 1)
    with ledger.open("a", encoding="utf-8") as fh:
        fh.write(row)
    print("registre mis à jour")


if __name__ == "__main__":
    main()
