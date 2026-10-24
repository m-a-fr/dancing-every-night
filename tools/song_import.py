#!/usr/bin/env python3
"""Nouveau morceau de A à Z : source -> piste .ogg, vidéo .mpg, image de fond, 4 cartes.

    make song SRC="https://www.youtube.com/watch?v=..." [TITLE="..."] [ARTIST="..."] [PACK="..."] [NAME=x] [FORCE=1]
    make song SRC=~/Downloads/clip.mp4 TITLE="..." ARTIST="..."
    make song SRC=piste.ogg IMAGE=pochette.jpg TITLE="..."          (sans vidéo)
    make song SRC="https://youtu.be/..." IMAGE=illustration.jpg NOVIDEO=1 ...   (audio seul + image fixe)

    tools/.venv/bin/python tools/song_import.py SRC [--title T] [--artist A] [--pack P] [--name N]
        [--audio piste.ogg] [--image img.jpg|URL] [--no-video] [--force]

SRC : lien YouTube (téléchargé avec yt-dlp, brew install yt-dlp) ou fichier local (vidéo ou
audio). --audio : piste séparée (sinon extraite de la vidéo ; si fournie, le décalage avec la
vidéo est mesuré). L'original est gardé dans art/videos/ (hors du jeu). Les cartes sont
générées par tools/mapper.py ; le rapport d'analyse est dans art/analysis/<name>.txt.
--image : fichier ou lien http(s) (téléchargé). --no-video : pas de vidéo dans le jeu, image fixe seulement
(celle de --image, sinon une image choisie dans le clip) ; avec --image sur un lien YouTube, seul l'audio est
téléchargé (rapide, léger) et l'original n'est pas gardé.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
import media   # noqa: E402
import mapper  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SONGS = ROOT / "assets" / "songs"
VIDEO_SRC = ROOT / "art" / "videos"
VIDEO_EXT = {".mp4", ".mov", ".m4v", ".webm", ".mkv", ".avi"}
AUDIO_EXT = {".ogg", ".wav", ".mp3", ".flac", ".m4a", ".opus"}
OGG_QUALITY = 0.6   # qualité Vorbis (0-1) : ~190 kb/s, ~4,5 Mo pour 3 min


def step(msg):
    print(f"\n== {msg}", flush=True)


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True, stdin=subprocess.DEVNULL)
    if r.returncode != 0:
        tail = (r.stderr.strip().splitlines() or ["?"])[-1]
        sys.exit(f"{what} a échoué : {tail}")
    return r.stdout


def latin1(text):
    """Texte affichable par la police du jeu (ASCII + Latin-1) ; le reste est retiré."""
    text = unicodedata.normalize("NFKC", text)
    text = text.replace("’", "'").replace("“", '"').replace("”", '"').replace("–", "-")
    kept = "".join(c for c in text if ord(c) < 256 and c.isprintable())
    return re.sub(r"\s+", " ", kept).strip(" -|_")


def slug(text):
    s = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")[:40]


def clean_title(t):
    """Retire les mentions habituelles des titres YouTube : [MV], (Official Video)..."""
    t = re.sub(r"[\[【(（][^\]】)）]*(official|mv|m/v|video|lyric|audio|music|full|hd|4k)[^\]】)）]*[\]】)）]", "", t, flags=re.I)
    return re.sub(r"\s+", " ", t).strip(" -|")


def youtube(url, workdir, audio_only=False):
    ytdlp = shutil.which("yt-dlp")
    if not ytdlp:
        sys.exit("yt-dlp introuvable : installe-le avec « brew install yt-dlp »")
    step("Téléchargement YouTube")
    info = json.loads(run([ytdlp, "--no-playlist", "--dump-single-json", url], "yt-dlp (infos)"))
    print(f"   {info.get('title')} — {info.get('uploader')} ({info.get('duration', 0) // 60}:{info.get('duration', 0) % 60:02d})")
    out = Path(workdir) / "source.%(ext)s"
    cmd = [ytdlp, "--no-playlist", "-f", "bv*[height<=720]+ba/b[height<=720]/b",
           "--merge-output-format", "mp4", "-o", str(out), url]
    if audio_only:
        cmd = [ytdlp, "--no-playlist", "-f", "ba/b", "-o", str(out), url]
    for attempt in range(3):              # YouTube refuse parfois un téléchargement (HTTP 403) : on réessaie
        r = subprocess.run(cmd, capture_output=True, text=True, stdin=subprocess.DEVNULL)
        if r.returncode == 0: break
        print(f"   tentative {attempt + 1} échouée : {(r.stderr.strip().splitlines() or ['?'])[-1][:120]}")
    else:
        sys.exit("yt-dlp (téléchargement) a échoué 3 fois")
    files = sorted(Path(workdir).glob("source.*"))
    if not files:
        sys.exit("yt-dlp n'a produit aucun fichier")
    title = info.get("track") or clean_title(info.get("title") or "")
    artist = info.get("artist") or info.get("creator") or re.sub(r"\s*-\s*Topic$", "", info.get("uploader") or "")
    return files[0], title, artist


def fetch_image(spec, workdir):
    """--image : chemin local ou lien http(s) (téléchargé dans le dossier temporaire)."""
    if not re.match(r"https?://", spec):
        img = Path(spec).expanduser()
        if not img.is_file(): sys.exit(f"image introuvable : {img}")
        return img
    import urllib.request
    dst = Path(workdir) / "image_source"
    req = urllib.request.Request(spec, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r, open(dst, "wb") as f:
            shutil.copyfileobj(r, f)
    except OSError as e:
        sys.exit(f"téléchargement de l'image échoué : {e}")
    return dst


def to_ogg(src, dst):
    """Toute source (vidéo ou audio) -> OGG Vorbis stéréo, via ffmpeg (décodage) et libsndfile (encodage)."""
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "a.wav"
        run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-i", str(src), "-vn", "-ac", "2", "-ar", "44100", str(wav)], "ffmpeg (audio)")
        data, sr = sf.read(str(wav), dtype="float32")
        # écriture par blocs : libsndfile plante sur un gros bloc en Vorbis
        part = Path(tmp) / "out.ogg"      # fichier final seulement si l'encodage est allé au bout
        with sf.SoundFile(str(part), "w", sr, 2, format="OGG", subtype="VORBIS",
                          compression_level=1.0 - OGG_QUALITY) as out:
            for i in range(0, len(data), 32768):
                out.write(data[i:i + 32768])
        shutil.move(str(part), str(dst))
    return len(data) / sr


def av_offset_ms(video, audio):
    """Décalage (ms) entre le son de la vidéo et la piste : > 0 = la vidéo est en retard."""
    with tempfile.TemporaryDirectory() as tmp:
        a, v = Path(tmp) / "a.wav", Path(tmp) / "v.wav"
        run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-i", str(audio), "-t", "60", "-ac", "1", "-ar", "8000", str(a)], "ffmpeg")
        run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-i", str(video), "-t", "60", "-vn", "-ac", "1", "-ar", "8000", str(v)], "ffmpeg")
        xa, _ = sf.read(str(a)); xv, _ = sf.read(str(v))
    n = min(len(xa), len(xv))
    xa, xv = xa[:n] - xa[:n].mean(), xv[:n] - xv[:n].mean()
    c = np.fft.irfft(np.fft.rfft(xv, 2 * n) * np.conj(np.fft.rfft(xa, 2 * n)))
    lag = int(np.argmax(c))
    if lag > n: lag -= 2 * n
    return int(round(lag / 8.0))


def best_frame(video, workdir):
    """Image de fond : parmi 12 images réparties dans le clip, la plus colorée et contrastée."""
    from PIL import Image, ImageFilter, ImageStat
    dur = float(run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(video)], "ffprobe"))
    best = None
    for i in range(12):
        t = dur * (0.1 + 0.8 * i / 11)
        p = Path(workdir) / f"frame{i}.png"
        subprocess.run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-ss", f"{t:.2f}", "-i", str(video), "-frames:v", "1", str(p)])
        if not p.exists(): continue
        im = Image.open(p).convert("RGB")
        small = im.resize((160, 90))
        sat = ImageStat.Stat(small.convert("HSV")).mean[1]
        lum = ImageStat.Stat(small.convert("L"))
        edges = ImageStat.Stat(small.convert("L").filter(ImageFilter.FIND_EDGES)).mean[0]
        score = sat * 0.5 + lum.stddev[0] + edges * 2 - max(0, 60 - lum.mean[0]) * 2
        if best is None or score > best[0]: best = (score, p)
    return best[1] if best else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="lien YouTube ou fichier (vidéo ou audio)")
    ap.add_argument("--title"); ap.add_argument("--artist")
    ap.add_argument("--pack", default="", help="pack du morceau (optionnel : vue par pack du menu)")
    ap.add_argument("--name", help="nom des fichiers (défaut : dérivé du titre)")
    ap.add_argument("--audio", help="piste séparée (sinon extraite de la vidéo)")
    ap.add_argument("--image", help="image de fond (sinon choisie dans la vidéo)")
    ap.add_argument("--no-video", action="store_true", help="ne pas garder la vidéo (image fixe seulement)")
    ap.add_argument("--bpm", type=float); ap.add_argument("--offset-ms", type=int)
    ap.add_argument("--meter", type=int, choices=[3, 4], help="temps par mesure (sinon détecté)")
    ap.add_argument("--force", action="store_true", help="écraser un morceau du même nom")
    args = ap.parse_args()

    work = tempfile.TemporaryDirectory()
    src_title, src_artist = "", ""
    if re.match(r"https?://", args.src):
        audio_only = bool(args.no_video and args.image)     # image fournie, pas de vidéo : l'audio suffit
        src, src_title, src_artist = youtube(args.src, work.name, audio_only)
    else:
        src = Path(args.src).expanduser().resolve()
        if not src.is_file(): sys.exit(f"fichier introuvable : {src}")
        src_title = src.stem
    is_video = src.suffix.lower() in VIDEO_EXT and not (re.match(r"https?://", args.src) and args.no_video and args.image)
    if not is_video and src.suffix.lower() not in AUDIO_EXT | VIDEO_EXT:
        sys.exit(f"format non pris en charge : {src.suffix}")

    title = latin1(args.title or src_title)
    artist = latin1(args.artist if args.artist is not None else src_artist)
    pack = latin1(args.pack or "")
    if not title:
        sys.exit("titre non affichable par la police du jeu (caractères non latins) : ajoute TITLE=\"...\"")
    name = args.name or slug(title)
    if not re.fullmatch(r"[a-z0-9_-]+", name or ""):
        sys.exit("nom de fichier invalide : ajoute NAME=mon_morceau (lettres, chiffres, _)")
    folder = SONGS / name                     # une map = un dossier assets/songs/<ID>/ (format de map 1)
    ogg = folder / "audio.ogg"
    if ogg.exists() and not args.force:
        sys.exit(f"assets/songs/{name}/ existe déjà : choisis un autre NAME ou ajoute FORCE=1")
    folder.mkdir(parents=True, exist_ok=True)
    print(f"Morceau : « {title} »{' — ' + artist if artist else ''}  (dossier : assets/songs/{name}/)")

    step("Piste audio")
    dur = to_ogg(Path(args.audio).expanduser() if args.audio else src, ogg)
    print(f"   assets/songs/{name}/audio.ogg ({dur:.0f} s, {ogg.stat().st_size / 1048576:.1f} Mo)")

    video_rel, video_off, bg_rel = "", 0, ""
    if is_video:
        VIDEO_SRC.mkdir(parents=True, exist_ok=True)
        kept = VIDEO_SRC / f"{name}{src.suffix.lower()}"
        for old in VIDEO_SRC.glob(name + ".*"):
            if old != kept: old.unlink()
        shutil.copyfile(src, kept)
        if not args.no_video:
            step("Vidéo (MPEG-1)")
            res = media.convert_video(kept, folder / "video.mpg", 24, (480, 272), media.DEFAULT_KBPS)
            video_rel = "video.mpg"
            if args.audio:
                video_off = -av_offset_ms(kept, Path(args.audio).expanduser())
            print(f"   {video_rel} ({res['bytes'] / 1048576:.1f} Mo){f', VIDEO_OFFSET_MS {video_off}' if video_off else ''}")
    image = fetch_image(args.image, work.name) if args.image else (best_frame(src, work.name) if is_video else None)
    if image:
        step("Image de fond")
        res = media.convert_image(image, folder / "background.jpg", "auto")
        bg_rel = "background.jpg"
        print(f"   {bg_rel} ({res['fill']})")

    step("Analyse et cartes")
    margs = argparse.Namespace(audio=str(ogg), name=name, title=title, artist=artist, pack=pack, bpm=args.bpm,
                               offset_ms=args.offset_ms, background=bg_rel, video=video_rel,
                               video_offset_ms=video_off, force=args.force, meter=args.meter)
    mapper.run(margs)
    print(f"\nTerminé. Lance « make run » puis choisis « {title} » dans le menu.")
    work.cleanup()


if __name__ == "__main__":
    main()
