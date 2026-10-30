#!/usr/bin/env python3
"""Serveur local de l'éditeur de cartes (bibliothèque standard uniquement).

    python3 tools/chart_editor.py [--port 8765]   (ou : make editor)

Puis ouvrir http://localhost:8765. L'interface (tools/editor/index.html) lit et
écrit directement les maps de assets/songs/<ID>/ (format de map 1 : une map = un dossier, voir src/chart.h).
N'écoute que sur 127.0.0.1.

Côté interface, une carte s'appelle « <ID>/<difficulté> » et ses médias « songs/<ID>/<fichier> » (relatifs à
assets/) ; ce serveur traduit à l'enregistrement : simples noms de fichiers du dossier, lignes FORMAT / ID /
VERSION gardées, et un média pris dans le dossier d'une autre map y est recopié (une map ne lit que son dossier).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import time
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
SONGS = ASSETS / "songs"
EDITOR = Path(__file__).resolve().parent / "editor"
VENV_PY = ROOT / "tools" / ".venv" / "bin" / "python"  # numpy/scipy pour l'analyse du BPM
VIDEO_SRC = ROOT / "art" / "videos"  # vidéos d'origine (hors assets/) : aperçu dans l'éditeur et ré-encodage
MEDIA_PY = ROOT / "tools" / "media.py"
TRASH = ROOT / ".chart_trash"  # cartes supprimées depuis l'éditeur (hors assets/, donc hors du jeu)

CHART_NAME = re.compile(r"^([A-Za-z0-9_-]+)/([A-Za-z0-9_-]+)$")   # <ID>/<difficulté>
SONG_NAME = re.compile(r"^[A-Za-z0-9_-]+\.ogg$")
SONG_PATH = re.compile(r"^[A-Za-z0-9_-]+/[A-Za-z0-9_-]+\.ogg$")    # <ID>/audio.ogg, relatif à assets/songs/
MEDIA_LINE = re.compile(r"^(\s*)(AUDIO|BACKGROUND|VIDEO)(\s+)(\S+)(.*)$")
MAP_KEYS = ("FORMAT", "ID", "VERSION", "MAPPER", "SOURCE", "LICENSE")   # lignes que l'interface ne connaît pas
AUDIO_EXT = {".ogg": "audio/ogg", ".wav": "audio/wav", ".mp3": "audio/mpeg"}
IMAGE_EXT = {".jpg": "image/jpeg", ".png": "image/png"}
VIDEO_EXT = {".mp4": "video/mp4", ".mov": "video/quicktime", ".m4v": "video/mp4", ".webm": "video/webm", ".mkv": "video/x-matroska", ".avi": "video/x-msvideo"}
MEDIA_NAME = re.compile(r"^[A-Za-z0-9_-]+$")


def to_editor(text, map_id):
    """Texte du disque -> interface : médias relatifs à assets/ (songs/<ID>/<fichier>)."""
    out = []
    for line in text.splitlines():
        m = MEDIA_LINE.match(line)
        if m and "/" not in m.group(4):
            line = f"{m.group(1)}{m.group(2)}{m.group(3)}songs/{map_id}/{m.group(4)}{m.group(5)}"
        out.append(line)
    return "\n".join(out) + "\n"


def from_editor(text, map_id):
    """Interface -> disque : FORMAT 1 et ID posés d'après le dossier, VERSION / MAPPER / SOURCE / LICENSE tels que
    l'interface les envoie, simples noms de fichiers du dossier de la map. Un média d'un autre dossier est recopié
    (ou déplacé s'il y est seul) ici. Même règles que fromEditor() de tools/editor/local.js."""
    folder = SONGS / map_id
    kept = {}
    for line in text.splitlines():
        key = line.split(maxsplit=1)[0] if line.strip() else ""
        if key in MAP_KEYS and key not in ("FORMAT", "ID"):
            kept[key] = line.strip()
    head = ["FORMAT 1", f"ID {map_id}", kept.get("VERSION", "VERSION 1"),
            *[kept[k] for k in ("MAPPER", "SOURCE", "LICENSE") if k in kept]]
    out, inserted = [], False
    for line in text.splitlines():
        if line.strip() and line.split(maxsplit=1)[0] in MAP_KEYS:
            continue
        if not inserted and line.strip() and not line.lstrip().startswith("#"):
            out += head
            inserted = True
        m = MEDIA_LINE.match(line)
        if m:
            src = (ASSETS / m.group(4)).resolve()
            if src.parent != folder.resolve() and src.is_file() and src.is_relative_to(SONGS.resolve()):
                dst = folder / src.name
                if not dst.exists():
                    folder.mkdir(parents=True, exist_ok=True)
                    alone = not any(src.parent.glob("*.chart"))
                    (src.replace if alone else lambda d: shutil.copyfile(src, d))(dst)
            line = f"{m.group(1)}{m.group(2)}{m.group(3)}{Path(m.group(4)).name}{m.group(5)}"
        out.append(line)
    if not inserted:
        out = head + out
    return "\n".join(out) + "\n"


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # silencieux ; les erreurs passent par send_error

    def send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path, ctype):
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_file_range(self, path, ctype):
        size = path.stat().st_size
        start, end, status = 0, size - 1, 200
        m = re.match(r"bytes=(\d*)-(\d*)", self.headers.get("Range", ""))
        if m and (m.group(1) or m.group(2)):
            if m.group(1):
                start = int(m.group(1))
                end = int(m.group(2)) if m.group(2) else size - 1
            else:
                start = max(0, size - int(m.group(2)))
            end = min(end, size - 1)
            status = 206
        with open(path, "rb") as f:
            f.seek(start)
            data = f.read(end - start + 1)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(data)))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        self.wfile.write(data)

    def song_list(self):
        if not SONGS.is_dir():
            return []
        return [f"songs/{p.parent.name}/{p.name}" for p in sorted(SONGS.glob("*/*")) if p.suffix.lower() in AUDIO_EXT]

    def chart_path(self, query):
        """(chemin de la carte, ID de la map) depuis name=<ID>/<difficulté>, ou None après avoir répondu 400."""
        name = parse_qs(query).get("name", [""])[0]
        m = CHART_NAME.match(name)
        if not m:
            self.send_json({"error": "nom de carte invalide (<ID>/<difficulté> : lettres, chiffres, _ et -)"}, 400)
            return None
        return SONGS / m.group(1) / f"{m.group(2)}.chart", m.group(1)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            return self.send_file(EDITOR / "index.html", "text/html; charset=utf-8")
        if (url.path in ("/local.js", "/tempo.js", "/i18n.js", "/guide.html", "/guide.fr.html", "/guide.css")
                or url.path.startswith(("/vendor/", "/fonts/"))):   # scripts, polices et guide du créateur
            path = (EDITOR / unquote(url.path.lstrip("/"))).resolve()
            types = {".js": "text/javascript; charset=utf-8", ".woff2": "font/woff2",
                     ".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8"}
            if not path.is_relative_to(EDITOR.resolve()) or not path.is_file() or path.suffix not in types:
                return self.send_error(404)
            return self.send_file(path, types[path.suffix])
        if url.path == "/api/ping":   # présence du serveur (sinon l'interface passe en mode navigateur)
            return self.send_json({"ok": True})
        if url.path == "/api/all":
            charts = [{"name": f"{p.parent.name}/{p.stem}",
                       "text": to_editor(p.read_text(encoding="utf-8", errors="replace"), p.parent.name)}
                      for p in sorted(SONGS.glob("*/*.chart"))]
            return self.send_json({"charts": charts, "songs": self.song_list()})
        if url.path == "/api/chart":
            found = self.chart_path(url.query)
            if found is None:
                return
            path, map_id = found
            if not path.is_file():
                return self.send_json({"error": "carte introuvable"}, 404)
            return self.send_json({"name": f"{map_id}/{path.stem}", "text": to_editor(path.read_text(encoding="utf-8"), map_id)})
        if url.path == "/api/analyze":
            return self.analyze(url.query)
        if url.path == "/api/generate":
            return self.generate(url.query)
        if url.path.startswith("/assets/ui/"):  # atlas des icônes (mêmes sprites que le jeu)
            path = (ASSETS / "ui" / unquote(url.path[len("/assets/ui/"):])).resolve()
            if path.parent != (ASSETS / "ui").resolve() or not path.is_file() or path.suffix.lower() != ".png":
                return self.send_error(404)
            return self.send_file(path, "image/png")
        if url.path.startswith("/assets/songs/"):
            name = unquote(url.path[len("/assets/songs/"):])
            path = (SONGS / name).resolve()
            types = {**AUDIO_EXT, **IMAGE_EXT}
            if path.parent.parent != SONGS.resolve() or not path.is_file() or path.suffix.lower() not in types:
                return self.send_error(404)
            return self.send_file(path, types[path.suffix.lower()])
        if url.path == "/media/video":  # vidéo d'origine, pour l'aperçu (Range : nécessaire pour se déplacer dans la vidéo)
            name = parse_qs(url.query).get("name", [""])[0]
            found = [p for p in VIDEO_SRC.glob(name + ".*") if p.suffix.lower() in VIDEO_EXT] if MEDIA_NAME.match(name) else []
            if not found:
                return self.send_error(404)
            return self.send_file_range(found[0], VIDEO_EXT[found[0].suffix.lower()])
        self.send_error(404)

    def do_PUT(self):
        url = urlparse(self.path)
        if url.path == "/api/song":
            return self.put_song(url.query)
        if url.path == "/api/background":
            return self.put_background(url.query)
        if url.path == "/api/video":
            return self.put_video(url.query)
        if url.path == "/api/video/reencode":
            return self.reencode_video(url.query)
        if url.path != "/api/chart":
            return self.send_error(404)
        found = self.chart_path(url.query)
        if found is None:
            return
        path, map_id = found
        length = int(self.headers.get("Content-Length", "0"))
        text = self.rfile.read(length).decode("utf-8")
        path.parent.mkdir(parents=True, exist_ok=True)
        text = from_editor(text, map_id)
        tmp = path.with_suffix(".chart.tmp")
        tmp.write_text(text, encoding="utf-8", newline="\n")
        tmp.replace(path)  # écriture atomique : pas de fichier à moitié écrit
        self.send_json({"ok": True, "name": f"{map_id}/{path.stem}"})

    def put_song(self, query):
        """Import d'une piste : le jeu ne lit que l'Ogg Vorbis (stb_vorbis). Elle ouvre un dossier de map
        assets/songs/<nom>/audio.ogg (déplacée dans le dossier de la map qui l'utilise à l'enregistrement)."""
        name = parse_qs(query).get("name", [""])[0]
        if not SONG_NAME.match(name):
            return self.send_json({"error": "nom de piste invalide (lettres, chiffres, _ et -, extension .ogg)"}, 400)
        folder = SONGS / name[:-4]
        path = folder / "audio.ogg"
        if folder.exists():
            return self.send_json({"error": f"assets/songs/{folder.name}/ existe déjà"}, 409)
        data = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        if data[:4] != b"OggS":
            return self.send_json({"error": "ce n'est pas un fichier Ogg (.ogg) — le jeu ne lit que l'Ogg Vorbis"}, 400)
        folder.mkdir(parents=True)
        path.write_bytes(data)
        self.send_json({"ok": True, "audio": f"songs/{folder.name}/audio.ogg", "songs": self.song_list()})

    def run_media(self, python, args, timeout):
        """Lance tools/media.py ; retourne (dict, None) ou (None, message d'erreur)."""
        try:
            out = subprocess.run([str(python), str(MEDIA_PY), *args], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None, "conversion trop longue"
        last = (out.stdout.strip().splitlines() or ["{}"])[-1]
        try:
            res = json.loads(last)
        except ValueError:
            return None, (out.stderr.strip().splitlines() or ["échec de la conversion"])[-1][:200]
        return (None, res["error"]) if "error" in res else (res, None)

    def put_background(self, query):
        """Import d'une image de fond : recadrée et réduite à 960x544 en JPEG (assets/songs/<ID>/background.jpg)."""
        name = parse_qs(query).get("name", [""])[0]
        if not MEDIA_NAME.match(name):
            return self.send_json({"error": "nom invalide (lettres, chiffres, _ et -)"}, 400)
        if not VENV_PY.exists():
            return self.send_json({"error": "conversion d'image indisponible : crée l'environnement Python avec "
                                   "python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt"}, 500)
        data = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        folder = SONGS / name
        folder.mkdir(parents=True, exist_ok=True)
        tmp = folder / ".upload"
        tmp.write_bytes(data)
        try:
            res, err = self.run_media(VENV_PY, ["image", str(tmp), str(folder / "background.jpg")], 60)
        finally:
            tmp.unlink(missing_ok=True)
        if err:
            return self.send_json({"error": "image illisible (PNG, JPEG ou WebP attendus)"}, 400)
        self.send_json({"ok": True, "background": f"songs/{name}/background.jpg", **res})

    def video_params(self, query):
        """(nom, fps, kbps) validés, ou None après avoir répondu 400."""
        q = parse_qs(query)
        name = q.get("name", [""])[0]
        fps = q.get("fps", ["24"])[0]
        kbps = q.get("kbps", ["720"])[0]  # = DEFAULT_KBPS_H264 de media.py (non importé ici : Pillow absent du Python système)
        if (not MEDIA_NAME.match(name) or not fps.isdigit() or not 10 <= int(fps) <= 30
                or not kbps.isdigit() or not 200 <= int(kbps) <= 1500):
            self.send_json({"error": "paramètres invalides (nom, fps 10-30, débit 200-1500 kb/s)"}, 400)
            return None
        return name, fps, kbps

    def encode_video(self, src, name, fps, kbps):
        """Convertit src en assets/songs/<ID>/video.h264 (H.264 Baseline sans son) et répond au client."""
        folder = SONGS / name
        folder.mkdir(parents=True, exist_ok=True)
        py = VENV_PY if VENV_PY.exists() else "python3"
        res, err = self.run_media(py, ["video", str(src), str(folder / "video.h264"), "--fps", fps, "--kbps", kbps], 3600)
        if err:
            return self.send_json({"error": err}, 500)
        (folder / "video.mpg").unlink(missing_ok=True)   # ancienne version MPEG-1 de la même chanson
        self.send_json({"ok": True, "video": f"songs/{name}/video.h264", "source": f"/media/video?name={name}", **res})

    def put_video(self, query):
        """Import d'une vidéo : l'original est gardé dans art/videos/, la version du jeu est un .h264 sans son."""
        params = self.video_params(query)
        if not params:
            return
        name, fps, kbps = params
        ext = "." + parse_qs(query).get("ext", [""])[0].lower().lstrip(".")
        if ext not in VIDEO_EXT:
            return self.send_json({"error": "extension vidéo non prise en charge"}, 400)
        data = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        VIDEO_SRC.mkdir(parents=True, exist_ok=True)
        for old in VIDEO_SRC.glob(name + ".*"):
            old.unlink()
        src = VIDEO_SRC / f"{name}{ext}"
        src.write_bytes(data)
        self.encode_video(src, name, fps, kbps)

    def reencode_video(self, query):
        """Réencode depuis la vidéo d'origine gardée dans art/videos/ (autre qualité, ancien .mjpg)."""
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        params = self.video_params(query)
        if not params:
            return
        name, fps, kbps = params
        found = [p for p in VIDEO_SRC.glob(name + ".*") if p.suffix.lower() in VIDEO_EXT]
        if not found:
            return self.send_json({"error": f"vidéo d'origine introuvable dans art/videos/{name}.*"}, 404)
        self.encode_video(found[0], name, fps, kbps)

    def analyze(self, query):
        """BPM, premier temps, mesure et points à vérifier : même analyse que make song (tools/mapper.py --analyze :
        balayage de tous les tempos, temps / contretemps, mesure à 3 ou 4 temps)."""
        name = parse_qs(query).get("audio", [""])[0].removeprefix("songs/")
        path = SONGS / name
        if not SONG_PATH.match(name) or not path.is_file():
            return self.send_json({"error": "piste .ogg introuvable dans assets/songs"}, 404)
        if not VENV_PY.exists():
            return self.send_json({"error": "analyse indisponible : crée l'environnement Python avec "
                                   "python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt"}, 500)
        try:
            out = subprocess.run([str(VENV_PY), str(ROOT / "tools" / "mapper.py"), str(path), "--analyze"],
                                 capture_output=True, text=True, timeout=180, check=True)
            return self.send_json(json.loads(out.stdout.strip().splitlines()[-1]))
        except subprocess.TimeoutExpired:
            return self.send_json({"error": "analyse trop longue (> 3 min)"}, 500)
        except (subprocess.CalledProcessError, ValueError, IndexError) as e:
            detail = getattr(e, "stderr", "") or str(e)
            return self.send_json({"error": "échec de l'analyse : " + detail.strip().splitlines()[-1][:200]}, 500)

    def generate(self, query):
        """Brouillon d'une difficulté par tools/mapper.py --stdout (même générateur que make song),
        calé sur le BPM/OFFSET du morceau."""
        q = parse_qs(query)
        name = q.get("audio", [""])[0].removeprefix("songs/")
        level = q.get("level", ["normal"])[0]
        try:
            bpm, offset = float(q.get("bpm", [""])[0]), int(q.get("offset", [""])[0])
        except ValueError:
            return self.send_json({"error": "BPM ou OFFSET_MS invalide"}, 400)
        if not SONG_PATH.match(name) or not (SONGS / name).is_file():
            return self.send_json({"error": "piste .ogg introuvable dans assets/songs"}, 404)
        if level not in ("easy", "normal", "hard", "expert") or not bpm > 0:
            return self.send_json({"error": "paramètres invalides"}, 400)
        if not VENV_PY.exists():
            return self.send_json({"error": "génération indisponible : crée l'environnement Python avec "
                                   "python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt"}, 500)
        cmd = [str(VENV_PY), str(ROOT / "tools" / "mapper.py"), str(SONGS / name), "--stdout", level.upper(),
               "--bpm", str(bpm), "--offset-ms", str(offset)]
        if q.get("holds", ["1"])[0] == "0":
            cmd.append("--no-holds")
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=300, check=True)
            warnings = [l[len("à vérifier : "):] for l in out.stderr.splitlines() if l.startswith("à vérifier : ")]
            return self.send_json({"notes": out.stdout, "warnings": warnings})
        except subprocess.TimeoutExpired:
            return self.send_json({"error": "génération trop longue (> 5 min)"}, 500)
        except subprocess.CalledProcessError as e:
            detail = (e.stderr or str(e)).strip().splitlines() or ["?"]
            return self.send_json({"error": "échec de la génération : " + detail[-1][:200]}, 500)

    def do_DELETE(self):
        url = urlparse(self.path)
        if url.path != "/api/chart":
            return self.send_error(404)
        found = self.chart_path(url.query)
        if found is None:
            return
        path, map_id = found
        if not path.is_file():
            return self.send_json({"error": "carte introuvable"}, 404)
        TRASH.mkdir(exist_ok=True)
        path.replace(TRASH / f"{map_id}_{path.stem}-{time.strftime('%Y%m%d-%H%M%S')}.chart")
        self.send_json({"ok": True})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8765)),
                    help="port d'écoute (défaut : variable PORT, sinon 8765)")
    ap.add_argument("--no-browser", action="store_true", help="ne pas ouvrir le navigateur")
    args = ap.parse_args()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://localhost:{args.port}"
    print(f"Éditeur de cartes : {url}  (Ctrl+C pour arrêter)")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
