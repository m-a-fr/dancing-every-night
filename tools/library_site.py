#!/usr/bin/env python3
"""Site de la bibliothèque de chansons : pages statiques générées depuis library/catalog.json.

    tools/library_site.py <dossier du site> [--demo]

Écrit dans <site>/ : catalog.json (lu aussi par le futur store du jeu), songs/index.html (liste, filtres côté client),
songs/<ID>/index.html (une page par chanson, lisible sans JavaScript), songs/library.css et songs/library.js.
--demo : catalogue fictif pour essayer le site en local (jamais publié). Bibliothèque seulement : les chansons
officielles du jeu n'y figurent pas. Le texte des pages est en anglais ; library.js le traduit en français (même choix de
langue que l'éditeur et le guide : navigateur, ou bouton EN / FR mémorisé).
Uniquement la bibliothèque standard de Python : tourne sur le build de Cloudflare Pages.
"""
import html, json, random, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "library"
E = html.escape

HEAD = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{desc}">
<meta name="theme-color" content="#0b0720">
<link rel="icon" type="image/png" href="/favicon.png">
<link rel="canonical" href="{canonical}">
<meta property="og:type" content="website">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{desc}">
<meta property="og:image" content="{og_image}">
<meta name="twitter:card" content="summary_large_image">
<link rel="stylesheet" href="/guide.css">
<link rel="stylesheet" href="{rel}library.css">
</head>
<body>
<nav class="top">
  <a class="brand" href="/">Dancing Every Night</a>
  <a class="btn nav" href="{rel}" aria-current="{cur_lib}" data-i18n>Song library</a>
  <a class="btn nav" href="/guide.html" data-i18n>Creator guide</a>
  <span class="spacer"></span>
  <button type="button" class="btn lang" id="lang" aria-label="Français / English"></button>
  <a class="btn primary" href="/" data-i18n>Open the editor</a>
</nav>
<main>
"""
TAIL = """</main>
<script src="{rel}library.js"></script>
</body>
</html>
"""


def fmt_time(s):
    s = int(round(s))
    return f"{s // 60}:{s % 60:02d}"


def fmt_mb(b):
    return f"{b / 1048576:.1f} MB"


def bpm_text(song):
    v = sorted({round(d["bpm"]) for d in song["difficulties"]})
    return f"{v[0]}" if len(v) == 1 else f"{v[0]}–{v[-1]}"


def diff_class(name):
    n = name.upper().split(" ")[0]
    return n.lower() if n in ("EASY", "NORMAL", "HARD", "EXPERT") else "other"


def pills(song):
    return "".join(f'<li class="pill {diff_class(d["name"])}"><span class="dot" aria-hidden="true"></span>'
                   f'{E(d["name"])}</li>' for d in song["difficulties"])


def thumb_url(cfg, song):
    return f'{cfg["thumb_base"]}{song["thumb"]}' if song.get("thumb") else ""


def empty_library(cfg):
    submit = E(cfg["submit_url"])
    return f"""<div class="empty first">
  <h2 data-i18n>The library is empty for now</h2>
  <p data-i18n>Songs made by the community will appear here once they are approved. Be the first to share one!</p>
  <p><a class="btn primary" href="/guide.html" data-i18n>Read the creator guide</a>
     <a class="btn" href="{submit}" data-i18n>Submit a song</a></p>
</div>
"""


def list_page(cfg, songs):
    rel = "./"
    count = len(songs)
    h = HEAD.format(title="Song library · Dancing Every Night", rel=rel, cur_lib="page",
                    desc="Songs made by the community for Dancing Every Night, the rhythm game for PS Vita: download a .zip, copy it to your console, play.",
                    canonical=f'{cfg["site_url"]}/songs/', og_image=f'{cfg["site_url"]}/social.jpg')
    submit = E(cfg["submit_url"])
    h += f"""<h1 data-i18n>Song library</h1>
<p class="lead" data-i18n>Songs made by the community. Download a .zip, copy it to your Vita, and play.</p>
<details class="how">
  <summary data-i18n>How to install a song</summary>
  <ol>
    <li data-i18n>Download the song's .zip file. Don't unzip it.</li>
    <li data-i18n>Copy it into <code>ux0:data/dancingeverynight/import/</code> on your Vita (VitaShell, USB or FTP mode).</li>
    <li data-i18n>Launch the game: it installs the song while loading, and it shows up in the list.</li>
  </ol>
  <p data-i18n>On a computer, use the <code>import/</code> folder of the game's save folder instead.</p>
</details>
<p class="note" data-i18n>Want to share a song? Make it with the <a href="/">online editor</a>, then <a href="{submit}">submit it here</a>. It is checked, and published once approved.</p>
"""
    listing = f"""<section class="tools" aria-label="Search and filters">
  <div class="search">
    <label for="q" data-i18n>Search by title, artist or author</label>
    <input id="q" type="search" autocomplete="off" enterkeyhint="search">
  </div>
  <div class="sort">
    <label for="sort" data-i18n>Sort by</label>
    <select id="sort">
      <option value="new" data-i18n>Newest</option>
      <option value="title" data-i18n>Title</option>
      <option value="bpm" data-i18n>Tempo (slowest first)</option>
      <option value="length" data-i18n>Length (shortest first)</option>
    </select>
  </div>
  <fieldset class="chips" id="f-diff"><legend data-i18n>Difficulty</legend>
    <button type="button" class="chip" data-f="diff" data-v="easy" aria-pressed="false">EASY</button>
    <button type="button" class="chip" data-f="diff" data-v="normal" aria-pressed="false">NORMAL</button>
    <button type="button" class="chip" data-f="diff" data-v="hard" aria-pressed="false">HARD</button>
    <button type="button" class="chip" data-f="diff" data-v="expert" aria-pressed="false">EXPERT</button>
  </fieldset>
  <fieldset class="chips" id="f-tempo"><legend data-i18n>Tempo</legend>
    <button type="button" class="chip" data-f="tempo" data-v="slow" aria-pressed="false" data-i18n>Under 120 BPM</button>
    <button type="button" class="chip" data-f="tempo" data-v="mid" aria-pressed="false" data-i18n>120 to 160 BPM</button>
    <button type="button" class="chip" data-f="tempo" data-v="fast" aria-pressed="false" data-i18n>Over 160 BPM</button>
  </fieldset>
  <fieldset class="chips" id="f-extra"><legend data-i18n>Extras</legend>
    <button type="button" class="chip" data-f="video" data-v="1" aria-pressed="false" data-i18n>With video</button>
  </fieldset>
</section>
<div class="status"><p id="count" role="status" aria-live="polite" aria-atomic="true"></p>
  <button type="button" class="btn small" id="clear" hidden data-i18n>Clear filters</button></div>
<ul class="grid" id="grid">
"""
    for s in songs:
        thumb = thumb_url(cfg, s)
        img = (f'<img crossorigin="anonymous" src="{E(thumb)}" alt="" width="480" height="272" loading="lazy" decoding="async">'
               if thumb else '<span class="noimg" aria-hidden="true">♪</span>')
        listing += (f'  <li class="card" data-id="{E(s["id"])}">\n    <a class="cover" href="{E(s["id"])}/" tabindex="-1" aria-hidden="true">{img}</a>\n'
              f'    <div class="body"><h2><a href="{E(s["id"])}/">{E(s["title"])}</a></h2>\n'
              f'    <p class="artist">{E(s["artist"] or "—")}</p>\n'
              f'    <p class="meta"><span>{bpm_text(s)} BPM</span><span>{fmt_time(s["duration_s"])}</span><span>{fmt_mb(s["zip_bytes"])}</span>'
              f'{"<span data-i18n>Video</span>" if s["has_video"] else ""}</p>\n'
              f'    <ul class="pills" aria-label="Difficulties">{pills(s)}</ul>\n'
              f'    <p class="by"><span data-i18n>by</span> {E(s["mapper"])}</p></div>\n  </li>\n')
    listing += f"""</ul>
<div class="empty" id="empty" hidden>
  <h2 data-i18n>No song matches</h2>
  <p data-i18n>Try fewer filters, or another word.</p>
  <p><button type="button" class="btn" id="clear2" data-i18n>Clear filters</button>
     <a class="btn" href="{submit}" data-i18n>Submit a song</a></p>
</div>
<noscript><p class="note" data-i18n>Filters need JavaScript; the full list is shown above.</p></noscript>
"""
    h += listing if songs else empty_library(cfg)
    h += f"""<footer><span data-i18n>Songs belong to their authors. Something wrong?</span>
  <a href="/songs/legal.html" data-i18n>Takedown and content policy</a> · <a href="https://discord.gg/vTJDvgJe74">Discord</a></footer>
"""
    return h + TAIL.format(rel=rel)


def song_page(cfg, s):
    rel = "../"
    thumb = thumb_url(cfg, s)
    desc = f'{s["title"]} by {s["artist"] or "unknown"}: a song for Dancing Every Night, made by {s["mapper"]}.'
    h = HEAD.format(title=f'{E(s["title"])} · Song library · Dancing Every Night', rel=rel, cur_lib="false", desc=E(desc),
                    canonical=f'{cfg["site_url"]}/songs/{s["id"]}/', og_image=E(thumb or f'{cfg["site_url"]}/social.jpg'))
    base = cfg["download_base"]
    url = f'{base}{s["zip"]}' if base else ""
    report = f'{cfg["report_url"]}&song={s["id"]}&title=Report%3A+{s["id"]}'
    h += f'<p class="crumb"><a href="{rel}" data-i18n>← Song library</a></p>\n'
    h += f'<article class="song">\n<div class="hero">\n'
    if thumb:
        h += f'<img class="art" crossorigin="anonymous" src="{E(thumb)}" alt="" width="480" height="272" decoding="async">\n'
    h += f'<div><h1>{E(s["title"])}</h1>\n<p class="artist">{E(s["artist"] or "—")}</p>\n<ul class="pills" aria-label="Difficulties">{pills(s)}</ul>\n'
    if url:
        h += (f'<p class="dl"><a class="btn primary big" href="{E(url)}" download>'
              f'<span data-i18n>Download</span> <small>{fmt_mb(s["zip_bytes"])} · .zip</small></a></p>\n')
    else:
        h += '<p class="dl"><span class="btn big disabled" aria-disabled="true" data-i18n>Downloads open soon</span></p>\n'
    h += '</div>\n</div>\n'
    h += '<h2 data-i18n>Difficulties</h2>\n<table class="diffs"><thead><tr><th data-i18n>Name</th><th data-i18n>Notes</th><th>BPM</th></tr></thead><tbody>\n'
    for d in s["difficulties"]:
        h += (f'<tr><th scope="row"><span class="pill {diff_class(d["name"])}"><span class="dot" aria-hidden="true"></span>{E(d["name"])}</span></th>'
              f'<td>{d["notes"]}</td><td>{round(d["bpm"])}</td></tr>\n')
    h += '</tbody></table>\n<h2 data-i18n>Details</h2>\n<dl class="facts">\n'
    facts = [("Author", s["mapper"])] + ([("Licence", s["license"])] if s["license"] else []) + [("Source", s["source"] or "—"),
             ("Version", str(s["version"])), ("Length", fmt_time(s["duration_s"])), ("Added", s["added"]), ("Updated", s["updated"])]
    if s["pack"]:
        facts.insert(3, ("Pack", s["pack"]))
    for k, v in facts:
        val = f'<a href="{E(v)}" rel="noopener nofollow">{E(v)}</a>' if v.startswith("http") else E(v)
        h += f'<div><dt data-i18n>{k}</dt><dd>{val}</dd></div>\n'
    h += '</dl>\n<h2 data-i18n>Install</h2>\n<ol class="how-list">\n'
    h += '<li data-i18n>Download the .zip file. Don\'t unzip it.</li>\n'
    h += '<li data-i18n>Copy it into <code>ux0:data/dancingeverynight/import/</code> on your Vita (VitaShell, USB or FTP mode).</li>\n'
    h += '<li data-i18n>Launch the game: it installs the song while loading, then it shows up in the list.</li>\n</ol>\n'
    h += (f'<details class="hash"><summary data-i18n>Check the file (SHA-256)</summary>'
          f'<p><code>{s["sha256"]}</code></p></details>\n')
    h += (f'<p class="actions"><a class="btn" href="{E(report)}" rel="nofollow" data-i18n>Report this song</a></p>\n</article>\n')
    return h + TAIL.format(rel=rel)


def legal_page(cfg):
    rel = "./"
    h = HEAD.format(title="Takedown and content policy · Dancing Every Night", rel=rel, cur_lib="false",
                    desc="How to report a song, and how rights holders can ask for a removal.",
                    canonical=f'{cfg["site_url"]}/songs/legal.html', og_image=f'{cfg["site_url"]}/social.jpg')
    report = E(cfg["report_url"])
    contact = cfg.get("takedown_contact", "")
    mail = f'<p><span data-i18n>Contact:</span> <a href="mailto:{E(contact)}">{E(contact)}</a></p>\n' if contact else ""
    ask = ("If a song uses your work without permission, tell us and we will remove it quickly. Use the report form, or write to the contact address below. Please say who you are, which song (its title and ID) and what you ask for."
           if contact else "If a song uses your work without permission, tell us and we will remove it quickly. Use the report form. Please say who you are, which song (its title and ID) and what you ask for.")
    h += f"""<p class="crumb"><a href="/songs/" data-i18n>← Song library</a></p>
<h1 data-i18n>Takedown and content policy</h1>
<p class="lead" data-i18n>Songs in the library are made and shared by the community.</p>
<h2 data-i18n>Rights holders</h2>
<p data-i18n>{ask}</p>
<p><a class="btn primary" href="{report}" data-i18n>Report a song</a></p>
{mail}<h2 data-i18n>What happens</h2>
<ul>
  <li data-i18n>The song is taken off the library and its file is deleted from our storage.</li>
  <li data-i18n>The removal is written in a public log, with its date and reason.</li>
  <li data-i18n>The author can send a corrected version, or answer if they believe the removal was a mistake.</li>
</ul>
<h2 data-i18n>Content policy</h2>
<p data-i18n>We don't publish songs with illegal, hateful or sexually explicit content, or songs whose files are not safe or valid for the game. Every submission is checked by the game's own validator and read by a person before it goes online.</p>
<h2 data-i18n>Privacy</h2>
<p data-i18n>This site collects no personal data: no account, no cookies, no analytics. Your language choice is kept in your browser only.</p>
"""
    return h + TAIL.format(rel=rel)


def demo_catalog(out):
    """Catalogue fictif (jamais publié) : de quoi essayer filtres, tri, états vides et pages en local."""
    rnd = random.Random(7)
    titles = ["Neon Rain", "Midnight Express", "Glass Garden", "Paper Planes", "Static Hearts", "Velvet Sky", "Last Train Home",
              "Cherry Voltage", "Echo Park", "Blue Hour", "Lantern Festival", "Hyperdrive Lullaby"]
    artists = ["Aria Vale", "The Tidepools", "kumo", "Neko Station", "Orbit Choir", "", "Mira & the Lights"]
    levels = ["EASY", "NORMAL", "HARD", "EXPERT"]
    songs = []
    for i, t in enumerate(titles):
        n = rnd.choice([1, 2, 3, 4, 4])
        bpm = rnd.choice([90, 105, 120, 128, 140, 150, 174, 180])
        diffs = [{"name": levels[k], "notes": int((90 + 70 * k) * (bpm / 120) * rnd.uniform(.8, 1.2)), "bpm": float(bpm)} for k in range(n)]
        if i == 3:
            diffs.append({"name": "EASY 2", "notes": 110, "bpm": float(bpm)})
        sid = t.lower().replace(" ", "_").replace("&", "and")
        songs.append({
            "id": sid, "version": rnd.choice([1, 1, 2]), "title": t, "artist": rnd.choice(artists), "pack": rnd.choice(["", "", "Demo pack"]),
            "mapper": rnd.choice(["DemoMapper", "sakura_dev", "Bob"]), "source": rnd.choice(["", "https://example.org/original"]),
            "license": rnd.choice(["CC BY 4.0", "my own music", "", ""]), "difficulties": diffs,
            "duration_s": float(rnd.randint(75, 230)), "size_bytes": 4000000, "has_video": rnd.random() < .4,
            "zip": f"songs/{sid}-v1.zip", "zip_bytes": rnd.randint(2_500_000, 14_000_000), "sha256": "%064x" % rnd.getrandbits(256),
            "thumb": f"thumbs/{sid}.jpg" if i != 5 else None, "added": f"2026-10-{10 + i:02d}", "updated": f"2026-10-{10 + i:02d}",
        })
    try:
        from PIL import Image, ImageDraw
        for s in songs:
            if not s["thumb"]:
                continue
            r = random.Random(s["id"])
            im = Image.new("RGB", (480, 272), (11, 7, 32))
            d = ImageDraw.Draw(im)
            for k in range(14):
                c = r.choice([(247, 12, 147), (251, 214, 63), (79, 200, 240), (235, 134, 213), (106, 93, 184)])
                x, y, w = r.randint(-40, 440), r.randint(-40, 232), r.randint(60, 220)
                d.polygon([(x, y), (x + w, y + r.randint(-30, 30)), (x + w // 2, y + w)], fill=c)
            p = out / s["thumb"]
            p.parent.mkdir(parents=True, exist_ok=True)
            im.save(p, quality=80)
    except ImportError:
        for s in songs:
            s["thumb"] = None
    return songs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    demo = "--demo" in sys.argv
    if not args:
        sys.exit(__doc__)
    site = Path(args[0])
    cfg = json.loads((LIB / "config.json").read_text(encoding="utf-8"))
    songs_dir = site / "songs"
    if songs_dir.exists():
        shutil.rmtree(songs_dir)
    songs_dir.mkdir(parents=True)
    if demo:
        songs = demo_catalog(site / "_demo")
        cfg["download_base"] = "/_demo/"
        cfg["thumb_base"] = "/_demo/"
    else:
        songs = json.loads((LIB / "catalog.json").read_text(encoding="utf-8"))["songs"]
        cfg["thumb_base"] = cfg["download_base"]
    songs.sort(key=lambda s: s["added"], reverse=True)
    (site / "catalog.json").write_text(json.dumps({"schema": 1, "download_base": cfg["download_base"], "songs": songs}, ensure_ascii=False) + "\n", encoding="utf-8")
    (songs_dir / "index.html").write_text(list_page(cfg, songs), encoding="utf-8")
    (songs_dir / "legal.html").write_text(legal_page(cfg), encoding="utf-8")
    for s in songs:
        (songs_dir / s["id"]).mkdir()
        (songs_dir / s["id"] / "index.html").write_text(song_page(cfg, s), encoding="utf-8")
    for f in ("library.css", "library.js"):
        shutil.copyfile(LIB / "site" / f, songs_dir / f)
    print(f"==> Bibliothèque : {len(songs)} chanson(s){' (démo)' if demo else ''} -> {songs_dir}")


if __name__ == "__main__":
    main()
