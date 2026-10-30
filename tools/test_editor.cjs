/* Browser-free regression checks for ZIP compatibility and song-wide settings.
   Run: node tools/test_editor.cjs (after make desktop for the game validator). */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');
const fflate = require('./editor/vendor/fflate.js');
const html = fs.readFileSync(path.join(__dirname, 'editor/index.html'), 'utf8');
const downloads = [], blobs = new Map();
const context = vm.createContext({
  Blob, File: require('node:buffer').File, TextEncoder, TextDecoder, Uint8Array, setTimeout: () => 0, clearTimeout() {}, fflate,
  URL: { createObjectURL(blob) { blobs.set('blob:test', blob); return 'blob:test'; }, revokeObjectURL() {} },
  document: { createElement() { return { click() { downloads.push({name:this.download, blob:blobs.get(this.href)}); } }; } },
  tr: (text, vars = {}) => text.replace(/\{(\w+)\}/g, (_, key) => vars[key] ?? key),
});
vm.runInContext(fs.readFileSync(path.join(__dirname, 'editor/local.js'), 'utf8'), context);
function fn(name) {
  const start = html.indexOf(`function ${name}(`);
  assert(start >= 0, name);
  return html.slice(start, html.indexOf('\n}', start) + 2);
}
vm.runInContext(`
  const S = { charts: [], audioFiles: [], cur: null };
  const mapIdOf = ch => ch.name.split('/')[0];
  const songKey = ch => ch.chart.title || ch.name;
  const cmpDiff = () => 0;
  function updateDirty() {} function renderTabs() {}
  ${fn('regroup')}
  ${fn('inheritSongMedia')}
  ${fn('setSongField')}
  ${fn('uniqueChartName')}
  ${fn('freeSongId')}
  ${fn('zipNameForTitle')}
  vfsPut = async (p,b) => VFS.set(p,b);
`, context);
async function run() {
  // Imported per-difficulty travel times must survive editing and ZIP serialization.
  const chartContext = vm.createContext({ tr: context.tr });
  const headerKeys = html.match(/const HEADER_KEYS = (\[[^;]+);/)[1];
  vm.runInContext(`
    const HEADER_KEYS = ${headerKeys};
    const LANES = ["UP", "LEFT", "DOWN", "TRIANGLE", "CIRCLE", "CROSS"];
    const DEFAULT_DIM = 30, BEAT_DECIMALS = 4;
    const isLeft = lane => LANES.indexOf(lane) < 3;
    ${fn('emptyChart')}
    ${fn('parseChart')}
    function fmt(x) { return String(parseFloat(x.toFixed(BEAT_DECIMALS))); }
    ${fn('sortNotes')}
    ${fn('serializeChart')}
    const easy = parseChart('DIFFICULTY EASY\\nAPPROACH_MS 1700\\nBPM 120\\n0 CROSS TAP\\n').chart;
    const expert = parseChart('DIFFICULTY EXPERT\\nAPPROACH_MS 1250\\nBPM 120\\n0 CROSS TAP\\n').chart;
    if (parseChart(serializeChart(easy)).chart.approach !== 1700) throw Error('EASY travel time lost');
    if (parseChart(serializeChart(expert)).chart.approach !== 1250) throw Error('EXPERT travel time lost');
    easy.approach = 0;
    if (serializeChart(easy).includes('APPROACH_MS') || expert.approach !== 1250) throw Error('Reset crossed difficulties');
    if (parseChart('BPM 120').chart.approach !== 0) throw Error('Legacy default changed');
    if (parseChart('APPROACH_MS 9000').chart.approach !== 5000) throw Error('Travel clamp');
  `, chartContext);
  vm.runInContext(`
    const a = { name:'first/normal', chart:{title:'Same title', background:'old.jpg'} };
    const b = { name:'second/easy', chart:{title:'Same title', background:'other.jpg'} };
    S.charts = [a,b]; S.cur = a; regroup();
    if (S.songs.length !== 2) throw Error('Same titles merged');
    const staleSong = S.song;
    const added = { name:'first/hard', chart:{title:'Same title'} };
    S.charts.push(added); regroup(); setSongField('background','new.jpg',staleSong);
    added.chart.background=''; a.chart.backgroundDim=30; a.chart.video='clip.mpg'; a.chart.videoOffset=0; inheritSongMedia(S.song);
    if (added.chart.background !== 'new.jpg' || b.chart.background !== 'other.jpg') throw Error('Song inheritance crossed IDs or missed a new difficulty');
    const longFolder = 'a'.repeat(40);
    if (!uniqueChartName(longFolder+'/normal').endsWith('/normal')) throw Error('Difficulty path truncated');
    if (freeSongId('a'.repeat(100)).length > 47) throw Error('New ID too long');
    if (new TextEncoder().encode(zipNameForTitle('é'.repeat(100))).length > 91) throw Error('ZIP name exceeds Vita scan limit');
  `, context);
  const longId = 'very_long_original_video_filename_'.repeat(3);
  context.longId = longId;
  vm.runInContext(`
    VFS.set('songs/'+longId+'/normal.chart',new Blob(['FORMAT 1\\nID '+longId+'\\nTITLE Firestarter Test\\nARTIST Test\\nDIFFICULTY NORMAL\\nBPM 120\\nOFFSET_MS 0\\nBACKGROUND background.png\\n\\n1 CROSS TAP\\n']));
  `, context);
  context.imageBytes = fs.readFileSync(path.join(__dirname, '../sce_sys/icon0.png'));
  vm.runInContext(`VFS.set('songs/'+longId+'/background.png',new Blob([imageBytes]));`, context);
  await vm.runInContext(`exportMap(longId,'Firestarter - custom title')`, context);
  await assert.rejects(vm.runInContext(`exportMap(longId,'é'.repeat(50))`, context), /nom du ZIP est trop long/);
  assert.equal(downloads[0].name, 'Firestarter - custom title.zip');
  const bytes = new Uint8Array(await downloads[0].blob.arrayBuffer()), files = fflate.unzipSync(bytes);
  const chartName = Object.keys(files).find(p => p.endsWith('.chart')), id = chartName.split('/')[0];
  assert(id.length <= 47); assert.equal(Object.keys(files).length, 2);
  assert.match(new TextDecoder().decode(files[chartName]), new RegExp(`^ID ${id}$`, 'm'));
  assert.equal(vm.runInContext('portableSongId(longId)', context), id);
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'editor-regression-'));
  try {
    const zipPath = path.join(temp, 'Firestarter - custom title.zip'); fs.writeFileSync(zipPath, bytes);
    const check = spawnSync(path.join(__dirname, '../build-desktop/vitagame'), ['--check', zipPath], {encoding:'utf8'});
    assert.equal(check.status, 0, check.stdout + check.stderr);
    context.zipBlob = downloads[0].blob;
    const imported = await vm.runInContext(`importZip({name:'Custom title.zip',arrayBuffer:()=>zipBlob.arrayBuffer()})`, context);
    assert.equal(imported.id, id);
    // Browser importer also repairs an older ZIP, before touching stored projects.
    context.legacyZip = new Blob([fflate.zipSync({[`${longId}/normal.chart`]: new TextEncoder().encode(`FORMAT 1\nID ${longId}\nTITLE Test\n`)})]);
    const legacy = await vm.runInContext(`importZip({name:'old.zip',arrayBuffer:()=>legacyZip.arrayBuffer()})`, context);
    assert.equal(legacy.id, id);
    assert.match(new TextDecoder().decode(legacy.files[0][1]), new RegExp(`^ID ${id}$`, 'm'));
    vm.runInContext(`VFS.delete('songs/'+longId+'/background.png')`, context);
    await assert.rejects(vm.runInContext(`exportMap(longId,'test')`, context), /média manque/);
  } finally { fs.rmSync(temp, {recursive:true, force:true}); }
  // Execute the actual difficulty creation handler, with its UI dependencies stubbed.
  let submit;
  const fields = {ndDiff:'HARD', ndDiffOther:'', ndSource:''};
  context.document.getElementById = id => id === 'formDiff' ? {addEventListener(event, fn) {submit = fn;}} : {value:fields[id] || '', textContent:''};
  Object.assign(context, {
    diffNameFrom: select => select.value, AUTO:'__auto__', emptyChart:() => ({}),
    slug: value => value.toLowerCase(), selectChart() {}, setMsg() {}, LOCAL:true,
  });
  const start = html.indexOf('document.getElementById("formDiff").addEventListener("submit"');
  const end = html.indexOf('\n});', start) + 4;
  vm.runInContext(`S.cur=S.charts[0]; regroup(); S.cur.chart.video='clip.mpg'; S.cur.chart.videoOffset=24; S.cur.chart.backgroundDim=42; S.cur.chart.notes=[];`,context);
  vm.runInContext(html.slice(start,end),context);
  submit({submitter:{value:'ok'}});
  const inherited = vm.runInContext('S.charts[S.charts.length-1].chart',context);
  assert.equal(inherited.background,'new.jpg'); assert.equal(inherited.video,'clip.mpg');
  assert.equal(inherited.backgroundDim,42); assert.equal(inherited.videoOffset,24);
  // Preview preparation is shared by difficulties and must not restore replaced media.
  const conversions = [];
  context.ffmpegConvert = () => new Promise((resolve, reject) => conversions.push({resolve,reject}));
  vm.runInContext(`
    const videoSources = new Map(), videoPreviewJobs = new Map(), PREVIEW_ARGS = [];
    function setMediaMsg() {} function fillMedia() {} function drawPreview() {}
    ${fn('clearVideoPreview')}
    ${fn('ensureVideoPreview')}
    S.cur = {name:'preview/normal'};
    VFS.set('songs/preview/video.mpg', new Blob(['mpeg-test']));
  `,context);
  const firstPreview = vm.runInContext("ensureVideoPreview('preview','songs/preview/video.mpg')",context);
  assert.equal(vm.runInContext("ensureVideoPreview('preview','songs/preview/video.mpg')",context),firstPreview);
  assert.equal(conversions.length,1);
  vm.runInContext("clearVideoPreview('preview'); VFS.set('songs/preview/video.mpg', new Blob(['replacement']));",context);
  conversions[0].resolve(new Blob(['obsolete-preview'])); await firstPreview;
  assert.equal(vm.runInContext("videoSources.has('preview')",context),false);
  const failedPreview = vm.runInContext("ensureVideoPreview('preview','songs/preview/video.mpg')",context);
  conversions[1].reject(new Error('test failure')); await failedPreview;
  await vm.runInContext("ensureVideoPreview('preview','songs/preview/video.mpg')",context);
  assert.equal(conversions.length,2,'Failed preview must not trigger an automatic retry loop');
  const retriedPreview = vm.runInContext("ensureVideoPreview('preview','songs/preview/video.mpg',true)",context);
  conversions[2].resolve(new Blob(['valid-preview'])); await retriedPreview;
  assert.equal(vm.runInContext("videoSources.has('preview')",context),true);
  console.log('Editor regression checks passed: title collisions, media inheritance, legacy IDs, ZIP round trip, missing media, game import validator and video preview deduplication/replacement/retry.');
}
run().catch(e => {console.error(e); process.exitCode=1;});
