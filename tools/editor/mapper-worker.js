/* Détection du BPM et brouillon automatique de la version en ligne : tools/mapper.py tel quel (même analyse
   et même générateur que make song), exécuté par Pyodide (Python + numpy + scipy compilés en WebAssembly).
   Tourne dans un worker pour ne pas figer l'éditeur. Pyodide (~30 Mo) n'est téléchargé qu'au premier usage,
   puis gardé en cache par le navigateur. La piste arrive déjà décodée (échantillons mono) depuis local.js. */
const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v0.26.4/full/";
let ready = null;
const tracks = new Map();   // chemin de la piste -> { y: Float32Array, sr } (une seule à la fois : ~16 Mo par minute et demie)

function boot(status) {
  if (!ready) {
    ready = (async () => {
      status("Téléchargement de Python pour l'analyse (environ 30 Mo, la première fois seulement)…");
      importScripts(PYODIDE + "pyodide.js");
      const py = await loadPyodide({ indexURL: PYODIDE });
      status("Chargement de numpy et scipy…");
      await py.loadPackage(["numpy", "scipy"]);
      const res = await fetch("mapper.py");
      if (!res.ok) throw new Error(`mapper.py introuvable (HTTP ${res.status})`);
      py.FS.writeFile("/home/pyodide/mapper.py", await res.text());
      py.runPython("import sys, json\nsys.path.insert(0, '/home/pyodide')\nimport numpy as np, mapper");
      return py;
    })();
    ready.catch(() => { ready = null; });   // échec réseau : on pourra réessayer
  }
  return ready;
}

const RUN = `
y = np.asarray(Y.to_py(), dtype=np.float32)
a = json.loads(ARGS)
if a["op"] == "analyze":
    out = mapper.analysis_summary(y, SR)
else:
    notes, warnings = mapper.draft_notes(y, SR, a["level"].upper(), a["name"], a["bpm"], a["offset"], a["holds"])
    out = {"notes": notes, "warnings": warnings}
json.dumps(out, ensure_ascii=False)
`;

onmessage = async ({ data: m }) => {
  const status = text => postMessage({ id: m.id, status: text });
  try {
    if (m.samples) { tracks.clear(); tracks.set(m.path, { y: m.samples, sr: m.sr }); }
    const t = tracks.get(m.path);
    if (!t) return postMessage({ id: m.id, needSamples: true });
    const py = await boot(status);
    status(m.op === "analyze" ? "Analyse de la piste (de 15 s à 1 min)…" : "Analyse de la musique et génération de la difficulté (de 30 s à 2 min)…");
    const g = py.globals;
    g.set("Y", t.y); g.set("SR", t.sr);
    g.set("ARGS", JSON.stringify({ op: m.op, ...m.args }));
    const out = await py.runPythonAsync(RUN);
    postMessage({ id: m.id, result: JSON.parse(out) });
  } catch (e) {
    postMessage({ id: m.id, error: String(e && e.message || e).trim().split("\n").pop().slice(0, 300) });
  }
};
