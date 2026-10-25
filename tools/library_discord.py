#!/usr/bin/env python3
"""Annonce sur Discord une chanson qu'on vient de publier dans la bibliothèque (webhook d'un salon dédié).

    DISCORD_WEBHOOK_URL=… NUMBER=<n° de la proposition> tools/library_discord.py

Utilisé par .github/workflows/library-publish.yml après la publication. Ne fait jamais échouer la publication : sans
webhook (ou en cas d'erreur) le script le dit en une ligne et sort sans erreur. L'adresse du webhook est un secret : elle
n'est jamais affichée. Aucun @ ne peut mentionner qui que ce soit (allowed_mentions vide), et le texte venant des auteurs
est échappé pour ne pas se transformer en mise en forme ou en lien.
"""
import json, os, re, sys, urllib.request, urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOOK = re.compile(r"^https://(?:discord|discordapp)\.com/api/webhooks/\d+/[\w-]+$")
LEMON = 0xFBD63F


def clean(text):
    """Texte d'auteur sûr dans un message Discord : pas de mise en forme, pas de mention, pas de lien automatique."""
    text = re.sub(r"[\x00-\x1f]", " ", str(text or ""))
    text = re.sub(r"([\\*_~`|>#\[\]()])", r"\\\1", text)
    return text.replace("@", "@​").replace("://", ":​//").strip()[:120]


def join_url(base, key):
    return f'{base.rstrip("/")}/{key}' if base and key else ""


def build_payload(entry, config):
    diffs = [d["name"] for d in entry.get("difficulties", [])]
    seconds = int(round(entry.get("duration_s", 0)))
    updated = int(entry.get("version", 1)) > 1
    lines = []
    if entry.get("artist"):
        lines.append(f'**{clean(entry["artist"])}**')
    lines.append(f'Made by {clean(entry.get("mapper"))}')
    lines.append(" · ".join(clean(d) for d in diffs) + f' · {seconds // 60}:{seconds % 60:02d}')
    embed = {
        "title": clean(entry["title"]) or "Untitled",
        "url": f'{config["site_url"].rstrip("/")}/songs/{entry["id"]}/',
        "description": "\n".join(lines),
        "color": LEMON,
        "footer": {"text": "Dancing Every Night · song library"},
    }
    thumb = join_url(config.get("download_base", ""), entry.get("thumb"))
    if thumb:
        embed["image"] = {"url": thumb}
    return {"username": "Song library", "content": ("🔄 Song updated" if updated else "🎵 New song in the library"),
            "embeds": [embed], "allowed_mentions": {"parse": []}}


def main():
    hook = os.environ.get("DISCORD_WEBHOOK_URL", "").strip()
    if not hook:
        print("pas de webhook Discord configuré : rien annoncé")
        return
    if not HOOK.match(hook):
        print("adresse de webhook Discord refusée (format inattendu) : rien annoncé")
        return
    number = int(os.environ["NUMBER"])
    config = json.loads((ROOT / "library/config.json").read_text(encoding="utf-8"))
    songs = json.loads((ROOT / "library/catalog.json").read_text(encoding="utf-8"))["songs"]
    entry = next((s for s in songs if s.get("issue") == number), None)
    if not entry:
        print("chanson introuvable dans le catalogue : rien annoncé")
        return
    request = urllib.request.Request(hook, data=json.dumps(build_payload(entry, config)).encode("utf-8"),
                                     headers={"Content-Type": "application/json", "User-Agent": "dne-library"})
    try:
        urllib.request.urlopen(request, timeout=15).read()
        print("annonce Discord envoyée")
    except (urllib.error.URLError, TimeoutError) as error:
        print(f"annonce Discord impossible ({type(error).__name__})")


if __name__ == "__main__":
    sys.exit(main())
