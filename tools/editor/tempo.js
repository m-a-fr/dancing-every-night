"use strict";
/* Same calibration surface in the creation wizard and the song settings.
   Audio runs continuously; only future metronome clicks are rescheduled on edits. */
function createTempoCalibration(host, options) {
  host.className = "tempo-calibration";
  host.innerHTML = `<canvas height="120" tabindex="0" role="img"></canvas>
    <div class="tempo-actions"><button type="button" data-action="play"></button>
    <label><input type="checkbox" data-role="click" checked> ${tr("Métronome")}</label>
    <label>${tr("Zoom")} <select data-role="zoom"><option value="16">16 s</option><option value="8" selected>8 s</option><option value="4">4 s</option><option value="2">2 s</option></select></label></div>
    <label class="tempo-position">${tr("Position d’écoute")} <input data-role="seek" type="range" min="0" max="0" step="0.01" value="0"></label>
    <div class="tempo-actions"><output data-role="time"></output><button type="button" data-action="first">${tr("Premier temps ici")}</button></div>
    <p class="note">${tr("Clique pour déplacer le curseur, glisse une barre pour recaler les temps. Ajuste le BPM pendant l’écoute.")}</p>
    <p data-role="status" role="status"></p>`;
  const q = selector => host.querySelector(selector), canvas = q("canvas"), ctx = canvas.getContext("2d");
  canvas.setAttribute("aria-label", tr("Onde audio et grille des temps. Flèches gauche et droite : déplacer le curseur."));
  const seek = q('[data-role="seek"]'), playButton = q('[data-action="play"]');
  let path = "", entry = null, position = 0, source = null, startedAt = 0, timer = null, frame = null, next = 0, clicks = [], drag = null, loading = 0, starting = 0;
  const seconds = () => source ? Math.min(entry.buffer.duration, position + AC.currentTime - startedAt) : position;
  const stopClicks = () => { for (const o of clicks) { try { o.stop(); } catch (_) {} o.disconnect(); } clicks = []; };
  const stop = () => {
    starting++;
    if (source) { position = seconds(); source.onended = null; try { source.stop(); } catch (_) {} source.disconnect(); source = null; }
    clearInterval(timer); cancelAnimationFrame(frame); stopClicks(); render();
  };
  function schedule() {
    if (!source || !q('[data-role="click"]').checked) return;
    const bpm = +options.bpm.value, offset = (+options.offset.value || 0) / 1000;
    if (!(bpm > 0) || !Number.isFinite(bpm)) return;
    const beat = 60 / bpm, now = AC.currentTime, origin = startedAt - position;
    next = Math.max(next, Math.ceil((now - origin - offset) / beat));
    clicks = clicks.filter(o => { if (o.endTime > now) return true; o.disconnect(); return false; });
    for (let count = 0; count < 32; count++) {
      const when = origin + offset + next * beat;
      if (when > now + 0.12 || offset + next * beat >= entry.buffer.duration) break;
      const o = AC.createOscillator(), gain = AC.createGain();
      o.frequency.value = next % 4 === 0 ? 1600 : 1000;
      gain.gain.setValueAtTime(0.25, when); gain.gain.exponentialRampToValueAtTime(0.001, when + 0.04);
      o.onended = () => { o.disconnect(); gain.disconnect(); };
      o.connect(gain).connect(AC.destination); o.start(when); o.stop(when + 0.05); o.endTime = when + 0.05; clicks.push(o); next++;
    }
  }
  const refresh = () => { stopClicks(); next = -Infinity; schedule(); render(); };
  async function load(newPath) {
    if (path === newPath && entry) return;
    stop(); path = newPath; entry = null; position = 0;
    const job = ++loading;
    q('[data-role="status"]').textContent = path ? tr("Chargement de l’onde…") : tr("Choisis une piste audio pour régler le tempo.");
    render();
    if (!path) return;
    try {
      ensureAC();
      const cached = audioCache.get(path);
      const buffer = cached && cached.buffer ? cached.buffer : await AC.decodeAudioData(await mediaBuffer(path));
      if (job !== loading) return;
      entry = cached && cached.buffer ? cached : { buffer, peaks: computePeaks(buffer) };
      audioCache.set(path, entry); seek.max = buffer.duration;
      q('[data-role="status"]').textContent = ""; render();
    } catch (e) { if (job === loading) q('[data-role="status"]').textContent = tr("Lecture impossible : {err}", { err: e.message }); }
  }
  async function start() {
    if (!entry) { await load(options.path()); if (!entry) return; }
    if (source) return;
    options.beforePlay();
    const run = ++starting;
    await AC.resume();
    if (run !== starting || !host.isConnected || host.closest('[hidden]') || (host.closest('dialog') && !host.closest('dialog').open)) return;
    if (position >= entry.buffer.duration) position = 0;
    source = AC.createBufferSource(); source.buffer = entry.buffer; source.connect(musicGain);
    startedAt = AC.currentTime; source.start(startedAt, position);
    source.onended = () => { stop(); position = 0; render(); };
    next = -Infinity; schedule(); timer = setInterval(schedule, 40);
    const animate = () => { render(); if (source) frame = requestAnimationFrame(animate); }; animate();
  }
  function move(time) {
    const wasPlaying = !!source; stop(); position = Math.max(0, Math.min(entry ? entry.buffer.duration : 0, time));
    if (wasPlaying) start(); else render();
  }
  function bounds() {
    const span = +q('[data-role="zoom"]').value, t = seconds();
    return { span, left: Math.max(0, Math.min(Math.max(0, (entry ? entry.buffer.duration : 0) - span), t - span * 0.35)) };
  }
  function render() {
    playButton.textContent = tr(source ? "Arrêter l’écoute" : "Écouter"); playButton.disabled = !entry;
    q('[data-action="first"]').disabled = !entry; seek.disabled = !entry;
    const t = seconds(); seek.value = t;
    q('[data-role="time"]').textContent = `${t.toFixed(2)} s / ${entry ? entry.buffer.duration.toFixed(2) : "0.00"} s`;
    const w = Math.max(1, Math.floor(canvas.getBoundingClientRect().width)), h = 120, ratio = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(w * ratio)) { canvas.width = Math.round(w * ratio); canvas.height = Math.round(h * ratio); }
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0); ctx.fillStyle = "#100b27"; ctx.fillRect(0, 0, w, h);
    if (!entry) return;
    const { span, left } = bounds(), px = time => (time - left) / span * w;
    ctx.fillStyle = "#64e2db";
    for (let x = 0; x < w; x++) {
      const a = Math.floor((left + x / w * span) * 1000 / PEAK_MS), b = Math.ceil((left + (x + 1) / w * span) * 1000 / PEAK_MS);
      let peak = 0; for (let i = a; i <= b && i < entry.peaks.length; i++) peak = Math.max(peak, entry.peaks[i] || 0);
      ctx.fillRect(x, 60 - peak * 37, 1, Math.max(1, peak * 74));
    }
    const bpm = +options.bpm.value, beat = 60 / bpm, offset = (+options.offset.value || 0) / 1000;
    if (bpm > 0 && Number.isFinite(bpm)) {
      let k = Math.ceil((left - offset) / beat);
      for (let n = 0; n < 256 && offset + k * beat <= left + span; n++, k++) {
        const x = px(offset + k * beat); ctx.strokeStyle = k % 4 === 0 ? "#fff38a" : "#9586bd";
        ctx.beginPath(); ctx.moveTo(x, 20); ctx.lineTo(x, h); ctx.stroke();
        if (k % 4 === 0) { ctx.fillStyle = "#fff38a"; ctx.font = "11px sans-serif"; ctx.fillText(`${k + 1}`, x + 3, 15); }
      }
    }
    ctx.strokeStyle = "#ff4baf"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(px(t), 0); ctx.lineTo(px(t), h); ctx.stroke(); ctx.lineWidth = 1;
  }
  playButton.onclick = () => source ? stop() : start();
  seek.oninput = () => move(+seek.value);
  q('[data-role="zoom"]').onchange = render;
  q('[data-role="click"]').onchange = refresh;
  q('[data-action="first"]').onclick = () => { options.offset.value = Math.round(seconds() * 1000); options.offset.dispatchEvent(new Event("input", { bubbles: true })); };
  const pointerTime = e => { const r = canvas.getBoundingClientRect(), b = bounds(); return b.left + (e.clientX - r.left) / r.width * b.span; };
  canvas.onpointerdown = e => {
    if (!entry) return;
    const time = pointerTime(e), bpm = +options.bpm.value, offset = +options.offset.value || 0, beat = 60000 / bpm;
    const nearest = offset + Math.round((time * 1000 - offset) / beat) * beat;
    const near = Math.abs(nearest / 1000 - time) / bounds().span * canvas.getBoundingClientRect().width < 8;
    drag = { x: e.clientX, time, offset, grid: near, moved: false, span: bounds().span };
    canvas.setPointerCapture(e.pointerId);
  };
  canvas.onpointermove = e => {
    if (!drag || !drag.grid) return;
    const delta = (e.clientX - drag.x) / canvas.getBoundingClientRect().width * drag.span;
    if (Math.abs(e.clientX - drag.x) < 3 && !drag.moved) return;
    drag.moved = true; options.offset.value = Math.round(drag.offset + delta * 1000); options.offset.dispatchEvent(new Event("input", { bubbles: true }));
  };
  canvas.onpointerup = () => { if (drag && !drag.moved) move(drag.time); drag = null; };
  canvas.onpointercancel = () => { drag = null; };
  canvas.onkeydown = e => { if (e.key === "ArrowLeft" || e.key === "ArrowRight") { e.preventDefault(); move(seconds() + (e.key === "ArrowLeft" ? -1 : 1) * (e.shiftKey ? 1 : 0.01)); } };
  options.bpm.addEventListener("input", refresh); options.offset.addEventListener("input", refresh);
  new ResizeObserver(render).observe(host);
  render();
  return { load, stop, start, refresh, get playing() { return !!source; } };
}
