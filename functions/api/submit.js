// Envoi d'une chanson à la bibliothèque depuis l'éditeur, sans compte GitHub (Cloudflare Pages Function).
//
//   GET  /api/submit            -> { token, bits } : défi de preuve de travail (à résoudre dans le navigateur)
//   POST /api/submit (multipart) -> { issue, url }  : dépose le .zip sur R2 (bucket « incoming ») et ouvre l'issue
//                                                     de proposition, que le contrôle automatique et le mainteneur traitent
//                                                     comme n'importe quelle proposition (voir library/PUBLISHING.md).
//
// Liaisons et variables (Pages > Settings > Functions / Variables) :
//   INCOMING        bucket R2 des envois (suppression automatique après 30 jours)
//   STATE           namespace KV : limites de débit et défis déjà utilisés
//   INCOMING_BASE   adresse publique du bucket, sans « / » final (https://pub-….r2.dev)
//   GITHUB_TOKEN    jeton à droit « Issues: write » sur ce seul dépôt (secret)
//   CHALLENGE_SECRET secret quelconque et long pour signer les défis (secret)
//   GITHUB_REPO     propriétaire/dépôt (défaut m-a-fr/dancing-every-night)
//
// Pas de CAPTCHA tiers : l'éditeur a besoin de l'isolation cross-origin (ffmpeg.wasm) que Turnstile n'accepte pas. À la
// place : preuve de travail, limite par adresse IP et plafond par jour. Rien ne se publie sans le label « approved »
// d'un mainteneur, et le fichier reste vérifié par le jeu lui-même (job sans secret).
export const MAX_BYTES = 25 * 1024 * 1024;
export const POW_BITS = 18;
export const LIMIT_PER_IP_PER_HOUR = 4;
export const LIMIT_PER_DAY = 40;
const CHALLENGE_TTL_S = 600;
const NAME = /^[\p{L}\p{N} ._'’-]{2,40}$/u;
const TITLE_BAD = /[\p{C}<>\[\]()`|@#*_~\\]/gu;

const enc = new TextEncoder();
const hex = buf => [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, "0")).join("");
const sha256 = async text => hex(await crypto.subtle.digest("SHA-256", enc.encode(text)));
async function hmac(secret, text) {
  const key = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return hex(await crypto.subtle.sign("HMAC", key, enc.encode(text)));
}
const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json", "cache-control": "no-store" } });
const fail = (status, error) => json({ error }, status);

export function leadingZeroBits(bytes) {
  let n = 0;
  for (const b of bytes) {
    if (b === 0) { n += 8; continue; }
    return n + Math.clz32(b) - 24;
  }
  return n;
}

export async function onRequestGet({ env }) {
  if (!env.CHALLENGE_SECRET) return fail(503, "not configured");
  const issued = Math.floor(Date.now() / 1000);
  const nonce = hex(crypto.getRandomValues(new Uint8Array(12)));
  const body = `${issued}.${nonce}`;
  return json({ token: `${body}.${await hmac(env.CHALLENGE_SECRET, body)}`, bits: POW_BITS });
}

async function checkChallenge(env, token, answer) {
  const parts = String(token || "").split(".");
  if (parts.length !== 3 || !/^\d{10}$/.test(parts[0]) || !/^[0-9a-f]{24}$/.test(parts[1])) return "bad challenge";
  if (parts[2] !== await hmac(env.CHALLENGE_SECRET, `${parts[0]}.${parts[1]}`)) return "bad challenge";
  const age = Date.now() / 1000 - Number(parts[0]);
  if (age < 0 || age > CHALLENGE_TTL_S) return "challenge expired, try again";
  if (!/^[0-9a-z]{1,24}$/.test(String(answer || ""))) return "bad challenge answer";
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", enc.encode(`${token}:${answer}`)));
  if (leadingZeroBits(digest) < POW_BITS) return "bad challenge answer";
  if (await env.STATE.get(`used:${parts[1]}`)) return "challenge already used";
  await env.STATE.put(`used:${parts[1]}`, "1", { expirationTtl: CHALLENGE_TTL_S + 120 });
  return "";
}

async function checkLimits(env, request) {
  const ip = request.headers.get("cf-connecting-ip") || "unknown";
  const now = new Date();
  const ipKey = `ip:${(await sha256(ip + env.CHALLENGE_SECRET)).slice(0, 24)}:${now.toISOString().slice(0, 13)}`;
  const dayKey = `day:${now.toISOString().slice(0, 10)}`;
  const [perIp, perDay] = await Promise.all([env.STATE.get(ipKey), env.STATE.get(dayKey)]);
  if (Number(perDay || 0) >= LIMIT_PER_DAY) return "The library is receiving a lot of songs today. Please try again tomorrow.";
  if (Number(perIp || 0) >= LIMIT_PER_IP_PER_HOUR) return "Too many songs from your connection. Please try again in an hour.";
  await Promise.all([env.STATE.put(ipKey, String(Number(perIp || 0) + 1), { expirationTtl: 3700 }),
                     env.STATE.put(dayKey, String(Number(perDay || 0) + 1), { expirationTtl: 90000 })]);
  return "";
}

export const cleanTitle = t => String(t || "").replace(TITLE_BAD, "").replace(/\s+/g, " ").trim().slice(0, 80) || "Untitled";

export function issueBody({ url, pseudo, previous }) {
  return [
    "### Your song (.zip)", "", url, "",
    "### Anything to tell us?", "",
    `Sent from the online editor by **${pseudo}**.` + (previous ? ` Replaces #${previous}.` : ""), "",
    "### Rights and permission", "",
    "- [x] I understand that I am responsible for the music, images and video in this song, that it will be public, and that it will be removed if a rights holder asks.",
    "", "<!-- editor-submission v1 -->",
  ].join("\n");
}

export async function onRequestPost({ request, env }) {
  for (const k of ["INCOMING", "STATE", "INCOMING_BASE", "GITHUB_TOKEN", "CHALLENGE_SECRET"]) if (!env[k]) return fail(503, "not configured");
  const origin = request.headers.get("origin");
  if (origin && origin !== new URL(request.url).origin) return fail(403, "forbidden");
  const length = Number(request.headers.get("content-length") || 0);
  if (!length) return fail(411, "length required");
  if (length > MAX_BYTES + 64 * 1024) return fail(413, "The song is larger than 25 MB. Lower the video bitrate and send it again.");

  let form;
  try { form = await request.formData(); } catch { return fail(400, "bad request"); }
  const file = form.get("file");
  const pseudo = String(form.get("pseudo") || "").trim();
  const title = cleanTitle(form.get("title"));
  const previous = /^\d{1,7}$/.test(String(form.get("previous_issue") || "")) ? String(form.get("previous_issue")) : "";
  if (form.get("consent") !== "1") return fail(400, "Please confirm that you are responsible for the content.");
  if (!NAME.test(pseudo)) return fail(400, "Choose a name of 2 to 40 letters, digits or spaces.");
  if (!file || typeof file === "string" || !file.size) return fail(400, "No song attached.");
  if (file.size > MAX_BYTES) return fail(413, "The song is larger than 25 MB. Lower the video bitrate and send it again.");
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes[0] !== 0x50 || bytes[1] !== 0x4b) return fail(400, "That file is not a valid .zip. Export it again from the editor.");

  const bad = await checkChallenge(env, form.get("challenge"), form.get("answer"));
  if (bad) return fail(400, bad);
  const limited = await checkLimits(env, request);
  if (limited) return fail(429, limited);

  const key = `${hex(crypto.getRandomValues(new Uint8Array(16)))}.zip`;
  await env.INCOMING.put(key, bytes, { httpMetadata: { contentType: "application/zip", contentDisposition: "attachment" } });
  const url = `${env.INCOMING_BASE}/${key}`;
  const repo = env.GITHUB_REPO || "m-a-fr/dancing-every-night";
  const res = await fetch(`https://api.github.com/repos/${repo}/issues`, {
    method: "POST",
    headers: { authorization: `Bearer ${env.GITHUB_TOKEN}`, accept: "application/vnd.github+json", "user-agent": "dne-editor-submit", "content-type": "application/json" },
    body: JSON.stringify({ title: `Song: ${title} (from the editor)`, body: issueBody({ url, pseudo, previous }), labels: ["song-submission"] }),
  });
  if (!res.ok) {
    await env.INCOMING.delete(key);
    return fail(502, "The library could not register your song. Please try again, or use the GitHub form.");
  }
  const issue = await res.json();
  return json({ issue: issue.number, url: issue.html_url });
}
