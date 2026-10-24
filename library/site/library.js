/* Bibliothèque de chansons : traduction FR/EN et, sur la liste, recherche, filtres et tri côté client.
   Les pages sont complètes sans JavaScript (cartes générées) ; ce script ne fait que filtrer et traduire. */
"use strict";
const LANG_KEY = "dne-editor-lang";   // même clé que l'éditeur : le choix de langue suit d'une page à l'autre
const LANG = (() => {
  try { const v = localStorage.getItem(LANG_KEY); if (v === "fr" || v === "en") return v; } catch (_) {}
  return /^fr\b/i.test(navigator.language || "") ? "fr" : "en";
})();

/* Texte anglais (clé) -> français */
const FR = {
  "Song library": "Bibliothèque de chansons", "Creator guide": "Guide du créateur", "Open the editor": "Ouvrir l'éditeur",
  "Songs made by the community. Download a .zip, copy it to your Vita, and play.": "Des chansons faites par la communauté. Télécharge un .zip, copie-le sur ta Vita, et joue.",
  "How to install a song": "Comment installer une chanson",
  "Download the song's .zip file. Don't unzip it.": "Télécharge le fichier .zip de la chanson. Ne le décompresse pas.",
  "Copy it into <code>ux0:data/dancingeverynight/import/</code> on your Vita (VitaShell, USB or FTP mode).": "Copie-le dans <code>ux0:data/dancingeverynight/import/</code> sur ta Vita (VitaShell, mode USB ou FTP).",
  "Launch the game: it installs the song while loading, and it shows up in the list.": "Lance le jeu : il installe la chanson pendant le chargement, et elle apparaît dans la liste.",
  "On a computer, use the <code>import/</code> folder of the game's save folder instead.": "Sur ordinateur, utilise plutôt le dossier <code>import/</code> du dossier de sauvegarde du jeu.",
  "Want to share a song? Make it with the <a href=\"/\">online editor</a>, then <a href=\"{submit}\">submit it here</a>. It is checked, and published once approved.": "Envie de partager une chanson ? Fais-la avec l'<a href=\"/\">éditeur en ligne</a>, puis <a href=\"{submit}\">propose-la ici</a>. Elle est vérifiée, puis publiée une fois approuvée.",
  "The library is empty for now": "La bibliothèque est vide pour le moment",
  "Songs made by the community will appear here once they are approved. Be the first to share one!": "Les chansons de la communauté apparaîtront ici une fois approuvées. Sois le premier à en partager une !",
  "Read the creator guide": "Lire le guide du créateur",
  "Search by title, artist or author": "Chercher par titre, artiste ou auteur", "Sort by": "Trier par",
  "Newest": "Plus récentes", "Title": "Titre", "Tempo (slowest first)": "Tempo (du plus lent au plus rapide)", "Length (shortest first)": "Durée (de la plus courte à la plus longue)",
  "Difficulty": "Difficulté", "Tempo": "Tempo", "Extras": "Options", "Under 120 BPM": "Moins de 120 BPM", "120 to 160 BPM": "De 120 à 160 BPM",
  "Over 160 BPM": "Plus de 160 BPM", "With video": "Avec vidéo", "Video": "Vidéo", "by": "par", "Clear filters": "Effacer les filtres",
  "No song matches": "Aucune chanson ne correspond", "Try fewer filters, or another word.": "Essaie avec moins de filtres, ou un autre mot.",
  "Submit a song": "Proposer une chanson", "Filters need JavaScript; the full list is shown above.": "Les filtres demandent JavaScript ; la liste complète est affichée ci-dessus.",
  "Songs belong to their authors and are shared under the licence they declare. Something wrong?": "Les chansons appartiennent à leurs auteurs et sont partagées sous la licence qu'ils déclarent. Un problème ?",
  "Takedown and content policy": "Retrait et politique de contenu",
  "← Song library": "← Bibliothèque de chansons", "Download": "Télécharger", "Downloads open soon": "Les téléchargements ouvrent bientôt",
  "Difficulties": "Difficultés", "Name": "Nom", "Notes": "Notes", "Details": "Détails", "Author": "Auteur", "Licence": "Licence", "Source": "Source",
  "Pack": "Pack", "Version": "Version", "Length": "Durée", "Added": "Ajoutée", "Updated": "Mise à jour", "Install": "Installer",
  "Download the .zip file. Don't unzip it.": "Télécharge le fichier .zip. Ne le décompresse pas.",
  "Launch the game: it installs the song while loading, then it shows up in the list.": "Lance le jeu : il installe la chanson pendant le chargement, puis elle apparaît dans la liste.",
  "Check the file (SHA-256)": "Vérifier le fichier (SHA-256)", "Report this song": "Signaler cette chanson",
  "Songs in the library are made and shared by the community. Each song carries the licence its author declared.": "Les chansons de la bibliothèque sont faites et partagées par la communauté. Chaque chanson porte la licence que son auteur a déclarée.",
  "Rights holders": "Ayants droit",
  "If a song uses your work without permission, tell us and we will remove it quickly. Use the report form, or write to the contact address below. Please say who you are, which song (its title and ID) and what you ask for.": "Si une chanson utilise ton travail sans autorisation, dis-le-nous et nous la retirerons vite. Utilise le formulaire de signalement, ou écris à l'adresse de contact ci-dessous. Précise qui tu es, quelle chanson (son titre et son ID) et ce que tu demandes.",
  "Report a song": "Signaler une chanson", "Contact:": "Contact :", "What happens": "Que se passe-t-il",
  "The song is taken off the library and its file is deleted from our storage.": "La chanson est retirée de la bibliothèque et son fichier est supprimé de notre stockage.",
  "The removal is written in a public log, with its date and reason.": "Le retrait est inscrit dans un journal public, avec sa date et son motif.",
  "The author can send a corrected version, or answer if they believe the removal was a mistake.": "L'auteur peut envoyer une version corrigée, ou répondre s'il pense que le retrait est une erreur.",
  "Content policy": "Politique de contenu",
  "We don't publish songs with illegal, hateful or sexually explicit content, or songs whose files are not safe or valid for the game. Every submission is checked by the game's own validator and read by a person before it goes online.": "Nous ne publions pas de chansons au contenu illégal, haineux ou sexuellement explicite, ni dont les fichiers ne sont pas sûrs ou valides pour le jeu. Chaque proposition est vérifiée par le validateur du jeu et lue par une personne avant d'être mise en ligne.",
  "Privacy": "Vie privée",
  "This site collects no personal data: no account, no cookies, no analytics. Your language choice is kept in your browser only.": "Ce site ne collecte aucune donnée personnelle : pas de compte, pas de cookies, pas de statistiques. Ton choix de langue reste dans ton navigateur.",
  "Songs made by the community: Dancing Every Night": "Chansons de la communauté : Dancing Every Night",
};
const FR_DYN = {
  "{n} song": "{n} chanson", "{n} songs": "{n} chansons", "{n} of {total} songs": "{n} chansons sur {total}", "{n} of {total} song": "{n} chanson sur {total}",
};
const norm = s => s.replace(/\s+/g, " ").trim();
const tr = (en, vars) => {
  let s = LANG === "fr" ? (FR_DYN[en] || FR[norm(en)] || en) : en;
  return vars ? s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? vars[k] : m)) : s;
};

function applyLang() {
  document.documentElement.lang = LANG;
  const btn = document.getElementById("lang");
  btn.textContent = LANG === "fr" ? "EN" : "FR";
  btn.onclick = () => { try { localStorage.setItem(LANG_KEY, LANG === "fr" ? "en" : "fr"); } catch (_) {} location.reload(); };
  if (LANG !== "fr") return;
  for (const el of document.querySelectorAll("[data-i18n]")) {
    const html = norm(el.innerHTML);
    // le lien de soumission varie : on cherche la clé avec le marqueur {submit}
    const sub = el.querySelector('a[href*="issues/new"]');
    let out = FR[html] || FR[norm(el.textContent)];
    if (!out && sub) out = FR[norm(el.innerHTML.replace(sub.getAttribute("href"), "{submit}"))];
    if (out) el.innerHTML = sub ? out.replace("{submit}", sub.getAttribute("href")) : out;
  }
  for (const el of document.querySelectorAll("[aria-label]")) {
    const v = el.getAttribute("aria-label");
    if (v === "Difficulties") el.setAttribute("aria-label", tr("Difficulties"));
    if (v === "Search and filters") el.setAttribute("aria-label", "Recherche et filtres");
  }
  const t = document.title.replace(" · Dancing Every Night", "");
  if (t === "Song library") document.title = tr("Song library") + " · Dancing Every Night";
}

/* ---------- Liste : recherche, filtres, tri ---------- */
const fold = s => (s || "").normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
async function initList() {
  const grid = document.getElementById("grid");
  if (!grid) return;
  let data;
  try { data = (await (await fetch("../catalog.json")).json()).songs; } catch (_) { return; }   // sans catalogue : liste complète, non filtrée
  const by = new Map(data.map(s => [s.id, s]));
  const cards = [...grid.querySelectorAll(".card")].map(el => ({ el, s: by.get(el.dataset.id) })).filter(c => c.s);
  const q = document.getElementById("q"), sortSel = document.getElementById("sort"), count = document.getElementById("count");
  const clear = [document.getElementById("clear"), document.getElementById("clear2")], empty = document.getElementById("empty");
  const chips = [...document.querySelectorAll(".chip")];
  const state = { q: "", sort: "new", diff: new Set(), tempo: new Set(), video: false };
  const baseDiff = n => fold(n).split(" ")[0];
  const tempoOf = s => { const m = Math.min(...s.difficulties.map(d => d.bpm)); return m < 120 ? "slow" : m <= 160 ? "mid" : "fast"; };
  const text = s => fold([s.title, s.artist, s.mapper, s.pack].join(" "));
  const sorters = {
    new: (a, b) => (b.s.added.localeCompare(a.s.added)) || a.s.title.localeCompare(b.s.title),
    title: (a, b) => a.s.title.localeCompare(b.s.title, undefined, { sensitivity: "base" }),
    bpm: (a, b) => Math.min(...a.s.difficulties.map(d => d.bpm)) - Math.min(...b.s.difficulties.map(d => d.bpm)),
    length: (a, b) => a.s.duration_s - b.s.duration_s,
  };
  function readUrl() {
    const p = new URLSearchParams(location.search);
    state.q = p.get("q") || ""; state.sort = sorters[p.get("sort")] ? p.get("sort") : "new";
    state.diff = new Set((p.get("diff") || "").split(",").filter(Boolean)); state.tempo = new Set((p.get("tempo") || "").split(",").filter(Boolean));
    state.video = p.get("video") === "1";
  }
  function writeUrl() {
    const p = new URLSearchParams();
    if (state.q) p.set("q", state.q);
    if (state.sort !== "new") p.set("sort", state.sort);
    if (state.diff.size) p.set("diff", [...state.diff].join(","));
    if (state.tempo.size) p.set("tempo", [...state.tempo].join(","));
    if (state.video) p.set("video", "1");
    history.replaceState(null, "", location.pathname + (p.toString() ? "?" + p : ""));
  }
  const active = () => state.q || state.diff.size || state.tempo.size || state.video;
  function render() {
    const words = fold(state.q).split(" ").filter(Boolean);
    const shown = cards.filter(({ s }) =>
      words.every(w => text(s).includes(w)) &&
      (!state.diff.size || s.difficulties.some(d => state.diff.has(baseDiff(d.name)))) &&
      (!state.tempo.size || state.tempo.has(tempoOf(s))) && (!state.video || s.has_video));
    cards.sort(sorters[state.sort]);
    for (const c of cards) grid.appendChild(c.el);
    for (const c of cards) c.el.hidden = !shown.includes(c);
    const n = shown.length, total = cards.length;
    count.textContent = active() ? tr(n === 1 ? "{n} of {total} song" : "{n} of {total} songs", { n, total })
                                 : tr(total === 1 ? "{n} song" : "{n} songs", { n: total });
    empty.hidden = n > 0; grid.hidden = n === 0;
    for (const c of clear) if (c) c.hidden = !active() && c.id === "clear";
    chips.forEach(b => b.setAttribute("aria-pressed", String(b.dataset.f === "video" ? state.video : state[b.dataset.f].has(b.dataset.v))));
    q.value = state.q; sortSel.value = state.sort;
    writeUrl();
  }
  let timer;
  q.addEventListener("input", () => { clearTimeout(timer); timer = setTimeout(() => { state.q = q.value.trim(); render(); }, 120); });
  sortSel.addEventListener("change", () => { state.sort = sortSel.value; render(); });
  chips.forEach(b => b.addEventListener("click", () => {
    if (b.dataset.f === "video") state.video = !state.video;
    else { const set = state[b.dataset.f]; set.has(b.dataset.v) ? set.delete(b.dataset.v) : set.add(b.dataset.v); }
    render();
  }));
  clear.forEach(b => b && b.addEventListener("click", () => { Object.assign(state, { q: "", diff: new Set(), tempo: new Set(), video: false }); render(); q.focus(); }));
  readUrl(); render();
}

applyLang();
initList();
