// Tests de functions/api/submit.js avec un environnement Cloudflare simulé (R2, KV, API GitHub) : node tools/test_submit.mjs
import assert from "node:assert/strict";
import { onRequestGet, onRequestPost, leadingZeroBits, POW_BITS, MAX_BYTES, cleanTitle, issueBody } from "../functions/api/submit.js";

const kv = () => { const m = new Map(); return { m, get: async k => m.get(k) ?? null, put: async (k, v) => void m.set(k, v) }; };
const r2 = () => { const m = new Map(); return { m, put: async (k, v) => void m.set(k, v), delete: async k => void m.delete(k) }; };
const env = () => ({ INCOMING: r2(), STATE: kv(), INCOMING_BASE: "https://incoming.example", GITHUB_TOKEN: "t", CHALLENGE_SECRET: "s".repeat(32) });
const issues = [];
globalThis.fetch = async (url, init) => {
  if (String(url).startsWith("https://api.github.com/")) {
    if (globalThis.githubDown) return new Response("no", { status: 500 });
    issues.push(JSON.parse(init.body));
    return new Response(JSON.stringify({ number: 7, html_url: "https://github.com/x/y/issues/7" }), { status: 201 });
  }
  throw new Error("unexpected fetch " + url);
};

async function solve(token) {
  for (let i = 0; ; i++) {
    const answer = i.toString(36);
    const d = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(`${token}:${answer}`)));
    if (leadingZeroBits(d) >= POW_BITS) return answer;
  }
}
async function challenge(e) { const { token } = await (await onRequestGet({ env: e })).json(); return { challenge: token, answer: await solve(token) }; }
const zip = (n = 100) => new Uint8Array([0x50, 0x4b, ...new Uint8Array(n)]);

async function post(e, over = {}) {
  const ch = over.noChallenge ? {} : await challenge(e);
  const form = new FormData();
  const fields = { pseudo: "Alice", title: "My *song*", consent: "1", ...ch, ...over.fields };
  for (const [k, v] of Object.entries(fields)) if (v !== undefined) form.set(k, v);
  if (over.file !== null) form.set("file", new Blob([over.file ?? zip()]), "s.zip");
  const body = new Request("https://x.test/api/submit", { method: "POST", body: form });
  const headers = new Headers(over.headers || {});
  headers.set("content-type", body.headers.get("content-type"));
  headers.set("content-length", String(over.length ?? (await body.clone().arrayBuffer()).byteLength));
  return onRequestPost({ request: new Request("https://x.test/api/submit", { method: "POST", body: await body.arrayBuffer(), headers }), env: e });
}

assert.equal(leadingZeroBits(new Uint8Array([0, 0x1f])), 11);

// envoi valide : fichier sur R2, issue au format du formulaire, texte nettoyé
{
  const e = env();
  const res = await post(e, { fields: { previous_issue: "3" } });
  assert.equal(res.status, 200); assert.deepEqual(await res.json(), { issue: 7, url: "https://github.com/x/y/issues/7" });
  assert.equal(e.INCOMING.m.size, 1);
  const issue = issues.at(-1);
  assert.equal(issue.title, "Song: My song (from the editor)"); assert.deepEqual(issue.labels, ["song-submission"]);
  assert.match(issue.body, /^### Your song \(\.zip\)\n\nhttps:\/\/incoming\.example\/[0-9a-f]{32}\.zip\n/);
  assert.match(issue.body, /\*\*Alice\*\*.*Replaces #3/); assert.match(issue.body, /<!-- editor-submission v1 -->/);
}
// refus
const cases = [
  ["pas de consentement", { fields: { consent: "0" } }, 400],
  ["pseudo avec lien", { fields: { pseudo: "x https://evil.example/a.zip" } }, 400],
  ["pseudo trop court", { fields: { pseudo: "A" } }, 400],
  ["pas de fichier", { file: null }, 400],
  ["pas un zip", { file: new Uint8Array([1, 2, 3, 4]) }, 400],
  ["pas de défi", { noChallenge: true }, 400],
  ["mauvaise réponse", { fields: { answer: "0" } }, 400],
  ["autre origine", { headers: { origin: "https://evil.example" } }, 403],
  ["trop gros annoncé", { length: MAX_BYTES + 1024 * 1024 }, 413],
];
for (const [name, over, status] of cases) {
  const e = env(); const before = issues.length;
  const res = await post(e, over);
  assert.equal(res.status, status, name); assert.equal(e.INCOMING.m.size, 0, name); assert.equal(issues.length, before, name);
}
// défi à usage unique
{
  const e = env(); const ch = await challenge(e);
  assert.equal((await post(e, { noChallenge: true, fields: ch })).status, 200);
  assert.equal((await post(e, { noChallenge: true, fields: ch })).status, 400);
}
// limite par adresse
{
  const e = env(); const h = { "cf-connecting-ip": "1.2.3.4" };
  for (let i = 0; i < 4; i++) assert.equal((await post(e, { headers: h })).status, 200);
  assert.equal((await post(e, { headers: h })).status, 429);
  assert.equal((await post(e, { headers: { "cf-connecting-ip": "5.6.7.8" } })).status, 200);
}
// GitHub en panne : le fichier déposé est retiré
{
  const e = env(); globalThis.githubDown = true;
  assert.equal((await post(e)).status, 502); assert.equal(e.INCOMING.m.size, 0); globalThis.githubDown = false;
}
// non configuré
assert.equal((await onRequestPost({ request: new Request("https://x.test/api/submit", { method: "POST" }), env: {} })).status, 503);
assert.equal(cleanTitle("<b>[x](y)</b> @a #1"), "bxy/b a 1"); assert.equal(cleanTitle("   "), "Untitled");
assert.ok(!issueBody({ url: "u", pseudo: "p", previous: "" }).includes("Replaces"));
console.log("Submit function checks passed: valid send, rejections, one-time challenge, rate limit, GitHub failure cleanup.");
