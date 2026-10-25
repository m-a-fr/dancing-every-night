"use strict";
/* Mode autonome de l'éditeur (version en ligne, sans serveur Python) : un « serveur » dans le navigateur.

   Les fichiers des maps sont gardés dans ce navigateur (IndexedDB), rangés comme sur la Vita :
   « songs/<ID>/<fichier> » (une map = un dossier, format de map 1). localApi() répond aux mêmes requêtes que
   tools/chart_editor.py (/api/all, /api/chart, /api/song, /api/background, /api/video), avec les mêmes
   traductions de chemins : côté interface, une carte s'appelle « <ID>/<difficulté> » et ses médias
   « songs/<ID>/<fichier> » ; sur disque, les médias sont de simples noms de fichiers du dossier.
   On entre et on sort par des zips (importZip / exportMap), le format que le jeu importe. */

const LOCAL_DB = "dne-editor", LOCAL_STORE = "files";
const VFS = new Map();          // "songs/<ID>/<fichier>" -> Blob
const blobUrls = new Map();     // chemin -> URL blob: (images, vidéo, lecture audio)
let LOCAL = false;              // true : pas de serveur, tout se passe dans le navigateur
let idb = null;

const MEDIA_LINE = /^(\s*)(AUDIO|BACKGROUND|VIDEO)(\s+)(\S+)(.*)$/;
const MAP_KEYS = ["FORMAT", "ID", "VERSION", "MAPPER", "SOURCE", "LICENSE"];
const CHART_PATH = /^songs\/([A-Za-z0-9_-]+)\/([A-Za-z0-9_-]+)\.chart$/;
const ID_RE = /^[A-Za-z0-9_-]{1,47}$/;
// Repair older browser projects whose ID was the full media filename. Stable across exports.
function portableSongId(id) {
  if (!/^[A-Za-z0-9_-]+$/.test(id)) throw new Error(tr("L’identité de la chanson contient des caractères incompatibles avec la Vita."));
  if (id.length <= 47) return id;
  let hash = 2166136261;
  for (const c of id) hash = Math.imul(hash ^ c.charCodeAt(0), 16777619) >>> 0;
  return `${id.slice(0, 38)}_${hash.toString(16).padStart(8, "0")}`;
}
const AUDIO_EXT = /\.(ogg|wav|mp3)$/i;

/* ---------- Stockage (IndexedDB) ---------- */
function idbOpen() {
  return new Promise((resolve, reject) => {
    const rq = indexedDB.open(LOCAL_DB, 1);
    rq.onupgradeneeded = () => rq.result.createObjectStore(LOCAL_STORE);
    rq.onsuccess = () => resolve(rq.result);
    rq.onerror = () => reject(rq.error);
  });
}
function idbTx(mode, fn) {
  return new Promise((resolve, reject) => {
    const tx = idb.transaction(LOCAL_STORE, mode), store = tx.objectStore(LOCAL_STORE);
    const out = fn(store);
    tx.oncomplete = () => resolve(out && "result" in out ? out.result : undefined);
    tx.onerror = () => reject(tx.error);
  });
}
async function vfsLoad() {
  idb = await idbOpen();
  const keys = await idbTx("readonly", s => s.getAllKeys());
  const values = await idbTx("readonly", s => s.getAll());
  keys.forEach((k, i) => VFS.set(k, values[i]));
}
async function vfsPut(path, blob) {
  VFS.set(path, blob);
  if (blobUrls.has(path)) { URL.revokeObjectURL(blobUrls.get(path)); blobUrls.delete(path); }
  await idbTx("readwrite", s => s.put(blob, path));
}
async function vfsDelete(path) {
  VFS.delete(path);
  if (blobUrls.has(path)) { URL.revokeObjectURL(blobUrls.get(path)); blobUrls.delete(path); }
  await idbTx("readwrite", s => s.delete(path));
}
// Musique d'une chanson en cours de création : gardée en mémoire seulement (pas dans IndexedDB) jusqu'à « Créer la
// chanson », oubliée si l'assistant est fermé avant
function vfsHold(path, blob) { VFS.set(path, blob); }
function vfsForget(path) {
  VFS.delete(path);
  if (blobUrls.has(path)) { URL.revokeObjectURL(blobUrls.get(path)); blobUrls.delete(path); }
}
// Pistes orphelines laissées par les versions précédentes de l'éditeur (assistant fermé après l'import de la musique) :
// un dossier sans aucune difficulté n'est pas une chanson, on le retire du navigateur
async function dropOrphanTracks() {
  const ids = [...new Set([...VFS.keys()].filter(p => p.startsWith("songs/")).map(p => p.split("/")[1]))];
  for (const id of ids) {
    const files = [...VFS.keys()].filter(p => p.startsWith(`songs/${id}/`));
    if (!files.some(p => p.endsWith(".chart"))) for (const p of files) await vfsDelete(p);
  }
}
const mapFiles = id => [...VFS.keys()].filter(p => p.startsWith(`songs/${id}/`));
const mapIds = () => [...new Set([...VFS.keys()].filter(p => p.startsWith("songs/")).map(p => p.split("/")[1]))].sort();
const mapBytes = id => mapFiles(id).reduce((n, p) => n + (VFS.get(p).size || 0), 0);

/* Fiche d'une map (« meta/<ID> », jamais exportée) : date de modification, tempo vérifié, export fait */
const metaGet = id => VFS.get(`meta/${id}`) || {};
async function metaSet(id, fields) {
  const meta = { ...metaGet(id), ...fields };
  VFS.set(`meta/${id}`, meta);
  await idbTx("readwrite", s => s.put(meta, `meta/${id}`));
}
/* Vide toute la mémoire de l'application : maps, fiches et préférences de l'éditeur */
async function wipeAll() {
  await idbTx("readwrite", s => s.clear());
  VFS.clear();
  for (const url of blobUrls.values()) URL.revokeObjectURL(url);
  blobUrls.clear();
  try {
    for (const k of Object.keys(localStorage)) if (k.startsWith("chartEditor.")) localStorage.removeItem(k);
  } catch (_) {}
}

/* ---------- Médias : URL et contenu, avec ou sans serveur ---------- */
function mediaUrl(path) {          // path relatif à assets/ : « songs/<ID>/<fichier> »
  if (!path) return "";
  if (!LOCAL) return "/assets/" + path.split("/").map(encodeURIComponent).join("/") + `?t=${mediaStamp}`;
  const blob = VFS.get(path);
  if (!blob) return "";
  if (!blobUrls.has(path)) blobUrls.set(path, URL.createObjectURL(blob));
  return blobUrls.get(path);
}
async function mediaBuffer(path) {
  if (!LOCAL) {
    const res = await fetch("/assets/" + path.split("/").map(encodeURIComponent).join("/"));
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.arrayBuffer();
  }
  const blob = VFS.get(path);
  if (!blob) throw new Error(tr("fichier absent de la chanson"));
  return blob.arrayBuffer();
}

/* ---------- Traductions disque <-> interface (mêmes règles que tools/chart_editor.py) ---------- */
function toEditor(text, id) {
  return text.split(/\r?\n/).map(line => {
    const m = line.match(MEDIA_LINE);
    return m && !m[4].includes("/") ? `${m[1]}${m[2]}${m[3]}songs/${id}/${m[4]}${m[5]}` : line;
  }).join("\n") + "\n";
}
async function fromEditor(text, id) {
  const keys = {};
  const body = [];
  for (const line of text.split(/\r?\n/)) {
    const key = line.trim().split(/\s+/)[0];
    if (MAP_KEYS.includes(key)) { if (key !== "FORMAT" && key !== "ID") keys[key] = line.trim(); continue; }
    body.push(line);
  }
  const head = ["FORMAT 1", `ID ${id}`, keys.VERSION || "VERSION 1",
                ...["MAPPER", "SOURCE", "LICENSE"].filter(k => keys[k]).map(k => keys[k])];
  const out = [];
  let inserted = false;
  for (let line of body) {
    if (!inserted && line.trim() && !line.trim().startsWith("#")) { out.push(...head); inserted = true; }
    const m = line.match(MEDIA_LINE);
    if (m) {
      const src = m[4], name = src.split("/").pop();
      if (src.startsWith("songs/") && !src.startsWith(`songs/${id}/`) && VFS.has(src) && !VFS.has(`songs/${id}/${name}`)) {
        await vfsPut(`songs/${id}/${name}`, VFS.get(src));   // une map ne lit que son dossier : copie du média
      }
      line = `${m[1]}${m[2]}${m[3]}${name}${m[5]}`;
    }
    out.push(line);
  }
  if (!inserted) out.unshift(...head);
  return out.join("\n").replace(/\n*$/, "\n");
}

/* ---------- « Serveur » local ---------- */
function audioList() {
  return [...VFS.keys()].filter(p => /^songs\/[^/]+\/[^/]+$/.test(p) && AUDIO_EXT.test(p)).sort();
}
async function allCharts() {
  const out = [];
  for (const p of [...VFS.keys()].sort()) {
    const m = p.match(CHART_PATH);
    if (m) out.push({ name: `${m[1]}/${m[2]}`, text: toEditor(await VFS.get(p).text(), m[1]) });
  }
  return out;
}
function chartPath(params) {
  const m = (params.get("name") || "").match(/^([A-Za-z0-9_-]+)\/([A-Za-z0-9_-]+)$/);
  if (!m) throw new Error(tr("nom de difficulté invalide (<ID>/<difficulté> : lettres, chiffres, _ et -)"));
  return { path: `songs/${m[1]}/${m[2]}.chart`, id: m[1] };
}
async function bodyBlob(opts) {
  const b = opts && opts.body;
  return b instanceof Blob ? b : new Blob([b || ""]);
}
async function headBytes(blob, n) { return new Uint8Array(await blob.slice(0, n).arrayBuffer()); }

/* En-tête de séquence MPEG-1 (00 00 01 B3) : largeur, hauteur, cadence */
async function mpegInfo(blob) {
  const b = await headBytes(blob, 256 * 1024);
  if (!(b[0] === 0 && b[1] === 0 && b[2] === 1 && (b[3] === 0xBA || b[3] === 0xB3))) return null;
  for (let i = 0; i + 8 < b.length; i++) {
    if (b[i] === 0 && b[i + 1] === 0 && b[i + 2] === 1 && b[i + 3] === 0xB3) {
      const w = (b[i + 4] << 4) | (b[i + 5] >> 4), h = ((b[i + 5] & 15) << 8) | b[i + 6];
      const fps = [0, 23.976, 24, 25, 29.97, 30, 50, 59.94, 60][b[i + 7] & 15] || 0;
      return { width: w, height: h, fps };
    }
  }
  return null;
}

/* Image de fond : recadrée « cover » et réduite à 960x544 en JPEG, comme tools/media.py */
async function convertImage(blob) {
  const bmp = await createImageBitmap(blob);
  const cv = document.createElement("canvas");
  cv.width = 960; cv.height = 544;
  const k = Math.max(960 / bmp.width, 544 / bmp.height), w = bmp.width * k, h = bmp.height * k;
  cv.getContext("2d").drawImage(bmp, (960 - w) / 2, (544 - h) / 2, w, h);
  return new Promise(resolve => cv.toBlob(resolve, "image/jpeg", 0.9));
}

async function localApi(url, opts = {}) {
  const u = new URL(url, location.href), q = u.searchParams, method = opts.method || "GET";
  if (u.pathname === "/api/all") return { charts: await allCharts(), songs: audioList() };
  if (u.pathname === "/api/chart") {
    const { path, id } = chartPath(q);
    if (method === "GET") {
      if (!VFS.has(path)) throw new Error(tr("difficulté introuvable"));
      return { name: q.get("name"), text: toEditor(await VFS.get(path).text(), id) };
    }
    if (method === "PUT") {
      const text = await fromEditor(await (await bodyBlob(opts)).text(), id);
      await vfsPut(path, new Blob([text], { type: "text/plain" }));
      await metaSet(id, { modified: Date.now() });
      return { ok: true, name: q.get("name") };
    }
    if (method === "DELETE") { await vfsDelete(path); return { ok: true }; }
  }
  if (u.pathname === "/api/song" && method === "PUT") {
    const name = q.get("name") || "", blob = await bodyBlob(opts);
    if (!/^[A-Za-z0-9_-]+\.ogg$/.test(name)) throw new Error(tr("nom de piste invalide"));
    const id = name.slice(0, -4);
    if (mapFiles(id).length) throw new Error(tr("la chanson {id} existe déjà", { id }));
    const b = await headBytes(blob, 4);
    if (String.fromCharCode(...b) !== "OggS") throw new Error(tr("ce n'est pas un fichier Ogg (.ogg) — le jeu ne lit que l'Ogg Vorbis"));
    await vfsPut(`songs/${id}/audio.ogg`, blob);
    return { ok: true, audio: `songs/${id}/audio.ogg`, songs: audioList() };
  }
  if (u.pathname === "/api/background" && method === "PUT") {
    const id = q.get("name") || "";
    if (!ID_RE.test(id)) throw new Error("nom invalide");
    let jpg;
    try { jpg = await convertImage(await bodyBlob(opts)); } catch (_) { throw new Error(tr("image illisible (PNG, JPEG ou WebP attendus)")); }
    await vfsPut(`songs/${id}/background.jpg`, jpg);
    return { ok: true, background: `songs/${id}/background.jpg`, bytes: jpg.size };
  }
  if (u.pathname === "/api/video" && method === "PUT") {
    const id = q.get("name") || "", blob = await bodyBlob(opts);
    if (!ID_RE.test(id)) throw new Error("nom invalide");
    const info = await mpegInfo(blob);
    if (!info) throw new Error(tr("vidéo non prise en charge : fournis une vidéo MPEG-1 (.mpg), le seul format que le jeu lit"));
    await vfsPut(`songs/${id}/video.mpg`, blob);
    return { ok: true, video: `songs/${id}/video.mpg`, bytes: blob.size, ...info };
  }
  if (u.pathname === "/api/analyze") return mapperCall("analyze", q.get("audio"), {}, opts.onStatus);
  if (u.pathname === "/api/generate") {
    const audio = q.get("audio") || "";
    return mapperCall("generate", audio, {
      level: q.get("level"), name: audio.split("/")[1] || "", bpm: parseFloat(q.get("bpm")),
      offset: parseInt(q.get("offset"), 10), holds: q.get("holds") !== "0",
    }, opts.onStatus);
  }
  throw new Error(tr("action indisponible dans la version en ligne"));
}

/* ---------- Détection du BPM et brouillon automatique : tools/mapper.py dans Pyodide (mapper-worker.js) ---------- */
let mapperWorker = null, mapperSeq = 0;
const mapperJobs = new Map();   // n° de requête -> { resolve, reject, onStatus, retry }
const mapperSent = new Map();   // chemin -> Blob décodé et transmis au worker (autre piste ou piste remplacée : on renvoie)

// Échantillons mono 44,1 kHz décodés par le navigateur (le worker n'a pas accès à WebAudio)
async function decodeMono(blob) {
  const buf = await new OfflineAudioContext(1, 1, 44100).decodeAudioData(await blob.arrayBuffer());
  const y = new Float32Array(buf.length);
  for (let c = 0; c < buf.numberOfChannels; c++) {
    const ch = buf.getChannelData(c);
    for (let i = 0; i < y.length; i++) y[i] += ch[i] / buf.numberOfChannels;
  }
  return { y, sr: buf.sampleRate };
}

function mapperPost(op, path, args, job, withSamples) {
  const id = ++mapperSeq;
  mapperJobs.set(id, job);
  return (async () => {
    const msg = { id, op, path, args };
    if (withSamples) {
      const blob = VFS.get(path);
      if (!blob) throw new Error(tr("la chanson n'a pas de piste .ogg"));
      job.onStatus?.(tr("Décodage de la piste…"));
      try { const { y, sr } = await decodeMono(blob); msg.samples = y; msg.sr = sr; }
      catch (_) { throw new Error(tr("piste illisible par ce navigateur (Ogg Vorbis attendu)")); }
      mapperSent.clear(); mapperSent.set(path, blob);   // le worker ne garde qu'une piste
      mapperWorker.postMessage(msg, [msg.samples.buffer]);
    } else mapperWorker.postMessage(msg);
  })().catch(e => { mapperJobs.delete(id); job.reject(e); });
}

function mapperCall(op, path, args, onStatus) {
  if (!mapperWorker) {
    mapperWorker = new Worker("mapper-worker.js");
    mapperWorker.onmessage = ({ data: m }) => {
      const job = mapperJobs.get(m.id);
      if (!job) return;
      if (m.status) return job.onStatus?.(tr(m.status));   // messages du worker, en français
      mapperJobs.delete(m.id);
      if (m.needSamples) return job.retry();
      if (m.error) job.reject(new Error(m.error)); else job.resolve(m.result);
    };
    mapperWorker.onerror = e => {   // worker introuvable ou planté : toutes les requêtes en cours échouent
      e.preventDefault();
      for (const job of mapperJobs.values()) job.reject(new Error(tr("analyse indisponible dans ce navigateur")));
      mapperJobs.clear(); mapperSent.clear();
      mapperWorker.terminate(); mapperWorker = null;
    };
  }
  return new Promise((resolve, reject) => {
    const job = { resolve, reject, onStatus };
    job.retry = () => mapperPost(op, path, args, job, true);
    mapperPost(op, path, args, job, mapperSent.get(path) !== VFS.get(path));
  });
}

/* Pas de serveur Python (version en ligne, ou fichier ouvert directement) : mode autonome */
async function detectLocal() {
  try {
    const res = await fetch("/api/ping");
    return !res.ok || !(res.headers.get("content-type") || "").includes("json");
  } catch (_) { return true; }
}

/* ---------- Zips : ouvrir une map, exporter une map (même organisation que l'import du jeu) ---------- */
async function importZip(file) {
  let entries;
  try { entries = fflate.unzipSync(new Uint8Array(await file.arrayBuffer())); }
  catch (_) { throw new Error(tr("zip illisible ou abîmé")); }
  const files = Object.entries(entries).filter(([name]) =>
    !name.endsWith("/") && !name.startsWith("__MACOSX/") && !name.split("/").pop().startsWith("."));
  if (!files.length) throw new Error(tr("zip vide"));
  const tops = new Set(files.map(([name]) => name.includes("/") ? name.split("/")[0] : ""));
  if (files.some(([name]) => name.split("/").length > 2)) throw new Error(tr("sous-dossiers dans le zip (une chanson = un seul dossier)"));
  if (tops.size > 1) throw new Error(tr("fichiers répartis dans plusieurs dossiers du zip"));
  const top = [...tops][0];
  const charts = files.filter(([name]) => /\.chart$/i.test(name));
  if (!charts.length) throw new Error(tr("aucune difficulté (.chart) dans le zip"));
  // ID : ligne ID d'une carte, sinon dossier du zip, sinon nom du zip
  const firstText = new TextDecoder().decode(charts[0][1]);
  const idLine = firstText.match(/^\s*ID\s+(\S+)/m);
  const id = idLine ? idLine[1] : top || file.name.replace(/\.zip$/i, "");
  const portableId = portableSongId(id);
  if (files.some(([name]) => { const base = name.split("/").pop(); return new TextEncoder().encode(base).length >= 64 || /[:\\]/.test(base) || base === ".."; }))
    throw new Error(tr("Un nom de fichier est incompatible avec la Vita (63 caractères maximum)."));
  if (charts.some(([, bytes]) => { const m = new TextDecoder().decode(bytes).match(/^\s*ID\s+(\S+)/m); return m && m[1] !== id; }))
    throw new Error(tr("Les difficultés doivent appartenir à la même chanson."));
  return { id: portableId, files: files.map(([name, data]) => [name.split("/").pop(), /\.chart$/i.test(name)
    ? new TextEncoder().encode(new TextDecoder().decode(data).replace(/^([ \t]*ID[ \t]+)\S+/gm, `$1${portableId}`)) : data]) };
}
async function installMap(id, files) {
  for (const p of mapFiles(id)) await vfsDelete(p);
  for (const [name, data] of files) await vfsPut(`songs/${id}/${name}`, new Blob([data]));
  await metaSet(id, { modified: Date.now(), tempoOk: true });   // une map ouverte depuis un zip a déjà son tempo
}
async function buildMapZip(id, filename = id) {
  if (new TextEncoder().encode(`${filename}.zip`).length >= 96)
    throw new Error(tr("Le nom du ZIP est trop long pour la Vita. Choisis un nom plus court."));
  const portableId = portableSongId(id);
  const charts = mapFiles(id).filter(p => /\.chart$/i.test(p));
  if (!charts.length) throw new Error(tr("Ajoute une difficulté avant d’exporter."));
  for (const p of charts) {
    const text = await VFS.get(p).text();
    for (const line of text.split(/\r?\n/)) {
      const m = line.match(MEDIA_LINE);
      if (m && !VFS.has(`songs/${id}/${m[4]}`)) throw new Error(tr("Un média manque à la chanson : {name}. Importe-le à nouveau avant d’exporter.", { name: m[4] }));
    }
  }
  const out = {};
  for (const p of mapFiles(id)) {
    const name = p.split("/").pop();
    if (new TextEncoder().encode(name).length >= 64 || /[:\\]/.test(name)) throw new Error(tr("Un nom de fichier est incompatible avec la Vita (63 caractères maximum)."));
    out[`${portableId}/${name}`] = /\.chart$/i.test(name)
      ? new TextEncoder().encode((await VFS.get(p).text()).replace(/^([ \t]*ID[ \t]+)\S+/gm, `$1${portableId}`))
      : new Uint8Array(await VFS.get(p).arrayBuffer());
  }
  return fflate.zipSync(out, { level: 0 });   // médias déjà compressés : stockés tels quels
}
async function exportMap(id, filename = id) {
  const zip = await buildMapZip(id, filename);
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([zip], { type: "application/zip" }));
  a.download = `${filename}.zip`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  return zip.length;
}
async function removeMap(id) {
  for (const p of mapFiles(id)) await vfsDelete(p);
  VFS.delete(`meta/${id}`);
  await idbTx("readwrite", s => s.delete(`meta/${id}`));
}
