#!/usr/bin/env python3
"""Nom du soumetteur d'une proposition : tools/library_submitter.py proposal.json

Une proposition ouverte depuis l'éditeur (sans compte GitHub) est créée par le compte de la fonction d'envoi
(library/config.json, « editor_login ») avec un marqueur et le nom choisi par l'auteur. Ce nom n'est lu que si ces deux
conditions sont réunies : un inconnu ne peut pas se faire passer pour quelqu'un d'autre en recopiant le marqueur.
Sinon : le compte GitHub de l'auteur de l'issue.
"""
import json, re, sys
from pathlib import Path

MARK = "<!-- editor-submission v1 -->"
BY = re.compile(r"Sent from the online editor by \*\*([\w .\'’-]{2,40})\*\*", re.U)


def submitter_of(proposal, editor_login):
    login = proposal["author"]["login"]
    body = proposal.get("body", "")
    if login == editor_login and MARK in body:
        m = BY.search(body)
        if m:
            return m.group(1).strip()
    return login


if __name__ == "__main__":
    cfg = json.loads((Path(__file__).resolve().parent.parent / "library" / "config.json").read_text(encoding="utf-8"))
    print(submitter_of(json.load(open(sys.argv[1])), cfg.get("editor_login", "")))
