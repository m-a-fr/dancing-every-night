#!/usr/bin/env python3
"""Récupère le .zip d'une chanson proposée dans une issue (texte du formulaire ou d'un commentaire).

    TEXT="…" tools/library_fetch.py sortie.zip

Ne télécharge que des pièces jointes GitHub (github.com/user-attachments/files/…, …/files/…, githubusercontent.com) ou un envoi
de l'éditeur (bucket « incoming », adresse dans library/config.json) : jamais une adresse quelconque donnée par un inconnu. Taille limitée (25 Mo, comme les pièces jointes GitHub), délai limité. Code de sortie :
0 zip récupéré (adresse sur stdout), 2 aucun zip trouvé, 3 refusé ou illisible (message sur stdout).
"""
import json, os, re, sys, urllib.request, urllib.error
from urllib.parse import urlparse

MAX_BYTES = 25 * 1024 * 1024
URL = re.compile(r"https://[^\s)\]>\"']+\.zip\b[^\s)\]>\"']*", re.I)
def incoming_host():
    """Hôte du bucket R2 des envois depuis l'éditeur (library/config.json, « incoming_base ») ; vide tant qu'il n'est pas créé."""
    try:
        base = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "library", "config.json"))).get("incoming_base", "")
    except (OSError, ValueError):
        return ""
    parsed = urlparse(base)
    return parsed.hostname if parsed.scheme == "https" and parsed.hostname else ""


INCOMING = incoming_host()
ALLOWED = (lambda u: u.hostname == "github.com" and (u.path.startswith("/user-attachments/files/") or "/files/" in u.path)
           or (u.hostname or "").endswith(".githubusercontent.com")
           or (bool(INCOMING) and u.hostname == INCOMING and re.fullmatch(r"/[0-9a-f]{32}\.zip", u.path or "") is not None))


class CheckedRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        parsed = urlparse(newurl)
        if parsed.scheme != "https" or not ALLOWED(parsed):
            raise urllib.error.URLError("redirect outside GitHub attachments")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def attachment_request(url):
    request = urllib.request.Request(url, headers={"User-Agent": "dne-library-check"})
    token = os.environ.get("GH_TOKEN")
    if token and urlparse(url).hostname == "github.com":
        # Private issue attachments need authentication. Never forward the credential
        # to a signed asset URL or another redirect destination.
        request.add_unredirected_header("Authorization", "Bearer " + token)
    return request


def main():
    out = sys.argv[1]
    urls = URL.findall(os.environ.get("TEXT", ""))
    if not urls:
        sys.exit(2)
    url = urls[-1]   # le plus récent : en cas de correction, le dernier fichier joint
    if not ALLOWED(urlparse(url)):
        print(f"I only download files attached to GitHub issues, not `{urlparse(url).hostname}`. Please attach the `.zip` to the issue itself.")
        sys.exit(3)
    try:
        with urllib.request.build_opener(CheckedRedirect()).open(attachment_request(url), timeout=60) as r:
            data = r.read(MAX_BYTES + 1)
    except (urllib.error.URLError, TimeoutError) as e:
        print(f"I could not download the file ({e}). Please attach it again.")
        sys.exit(3)
    if len(data) > MAX_BYTES:
        print("The file is larger than 25 MB. Please make it lighter (a lower video bitrate helps) and attach it again.")
        sys.exit(3)
    if data[:2] != b"PK":
        print("The attached file is not a valid `.zip`. Please export it again from the editor.")
        sys.exit(3)
    open(out, "wb").write(data)
    print(url)


if __name__ == "__main__":
    main()
