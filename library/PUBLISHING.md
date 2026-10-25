# Community library operations / Publication des chansons

## Configuration (maintainer only)

Publication is disabled until `library/config.json` has an HTTPS `download_base`.
Create a dedicated Cloudflare R2 bucket for community ZIPs and thumbnails. Use its public HTTPS domain
as `download_base` (no final slash needed). Do not use the private S3 API endpoint as a public download URL.
Objects will be public: never put official audio, credentials or private submissions in this bucket.

In the GitHub repository, create the environment `library-production`, restrict it to `main`, and configure:

| Kind | Name | Value |
| --- | --- | --- |
| Variable | `R2_BUCKET` | Dedicated bucket name |
| Variable | `R2_ENDPOINT` | `https://<32-character-account-id>.r2.cloudflarestorage.com` |
| Secret | `R2_ACCESS_KEY_ID` | R2 S3 access key |
| Secret | `R2_SECRET_ACCESS_KEY` | R2 S3 secret key |

Scope the R2 credentials to object read/write in this bucket only. Put secrets directly in GitHub;
do not paste them into issues, this document or chat. Optionally require an environment reviewer
(an additional approval will then be required for every run).

The workflow needs permission to push catalogue commits to `main`. If branch rules prevent this,
configure an appropriate maintainer-controlled integration rather than disabling protections globally.
Cloudflare Pages must build catalogue changes pushed by the GitHub Actions bot; verify this on the first test.

## Publish / Publier

1. The contributor attaches the ZIP to the song proposal, or to a comment on that same proposal.
2. Wait for the automatic **Ready for review** report containing the source SHA-256.
3. Listen to and test that exact version, review credits and permission to redistribute all media.
4. Apply `approved`. The **Publish or remove a song** workflow verifies maintainer permissions,
   fetches the latest ZIP attached by the proposal author and requires its digest to match the successful report.
5. The workflow rewrites the folder and every chart ID to `community-<issue number>`, checks the rewritten
   ZIP, uploads it and its thumbnail to R2, pushes the catalogue and comments the song page link.
   Wait for the Cloudflare deployment before announcing availability.

Corrections to an already published song belong in the **same issue**, from the same contributor,
with a higher `VERSION`. Reapply `approved` after the new automatic report. The publication ID remains stable.
The original local ID and source digest are recorded in the catalogue. Local and published versions may
coexist on a console; **automatic migration of local records is not implemented**. Installing a publication
with a different ID does not transfer the original records. Do not promise otherwise to contributors.

A failed transfer or catalogue push is not a successful publication. Inspect the failed run before retrying.
Files can have reached R2 before the catalogue commit: inspect and clean any orphan objects if needed.

### Songs without a LICENSE (private register)

LICENSE is optional and never blocks a submission or appears as a warning. When a song is published with an empty
LICENSE, the publish workflow (step "Maintainer notes") appends one row to `unlicensed.md` in the private repository
`MODERATION_REPO` (secret `MODERATION_LEDGER_KEY`, a write-only deploy key, both in the `library-production`
environment). Nothing is printed in the public logs. The note is written at publication, not at check time, because the
check job handles untrusted zips and holds no secrets. Caveat: the code is public, so the mechanism is visible, and an
empty `license` in `catalog.json` is technically readable; the site simply hides the field.

## Sending from the editor, without a GitHub account

The editor's *Publish* step can send the song straight to the library (`functions/api/submit.js`, a Cloudflare Pages
Function). It stores the .zip in a separate R2 bucket and opens a normal `song-submission` issue with the editor's bot
account (`editor_login` in `library/config.json`, normally the owner). From there the flow is unchanged: automatic
check, then `approved` by a maintainer. The issue is created by that account, so the submitter shown in the catalogue
comes from the chosen name (`tools/library_submitter.py`), read only when the issue author is `editor_login`.
Updating a song from the editor opens a new proposal (a new `community-N`); remove the old one by hand.

Setup (once, in the Cloudflare dashboard and on GitHub):

1. R2 > create the bucket `dne-incoming`; enable public access (r2.dev address) and add an object lifecycle rule that
   deletes objects after 30 days. Put the address in `library/config.json` as `incoming_base` (no trailing `/`).
2. The bindings (R2 `INCOMING`, KV `STATE`) and `INCOMING_BASE` are declared in `wrangler.toml` at the repository root
   (done with `wrangler r2 bucket create`, `wrangler kv namespace create`).
3. GitHub > fine-grained personal access token, repository `dancing-every-night` only, permission *Issues: read and write*.
4. Pages project > Settings > Variables and secrets (production), as secrets only: `GITHUB_TOKEN`, `CHALLENGE_SECRET`
   (any 32+ random characters). Renew the token before it expires.
5. Redeploy. Until all of this exists the editor hides the direct send and only the GitHub form is offered.

Limits: 25 MB per song, 4 songs per hour per connection, 40 per day for everyone (`functions/api/submit.js`). No
third-party CAPTCHA: the editor needs cross-origin isolation, which Turnstile does not allow; a small proof-of-work
slows bulk sending instead. Nothing is published without the `approved` label.

Test: `node tools/test_submit.mjs`.

## Remove / Retirer

Run **Publish or remove a song** manually. Fill `remove` with the exact published ID and `reason` with
an explanatory public reason; leave `issue` empty. This deletes all recorded ZIP/thumbnail versions,
removes the catalogue entry and adds a withdrawal entry in `library/removals.json`.
The website updates after deployment; already downloaded copies remain on players' devices.
A removed publication ID stays reserved; a new proposal is required to republish.
If a network operation fails partway, rerun removal: deleting an already deleted R2 object is harmless.

## Verification before enabling real publication

- Local lifecycle: `tools/.venv/bin/python -m unittest discover -s tests -p test_library_publication.py`.
- Use the synthetic CC0 fixture `library/tests/test_clicks.zip` as a **real GitHub issue attachment**.
- Confirm check, approval, public ZIP download and installation, then removal and public object disappearance.
- Manual validator tests do not exercise GitHub attachment access. Private attachment access and R2 transfer
  have not yet been verified end to end. Do not publish a third-party song until the synthetic trial passes.

## Résumé administrateur

Configurer le bucket public, les deux variables et les deux secrets ci-dessus, puis l'adresse publique
`download_base`. Pour publier : vérifier le rapport et les droits, tester le ZIP, poser `approved`.
Pour mettre à jour : même proposition, même auteur, version supérieure. Pour retirer : lancement manuel,
identifiant et motif. Tester d'abord avec la chanson synthétique CC0, puis vérifier le déploiement Pages.

## Configured test infrastructure

Bucket: `dne-community-songs` (Western Europe, Standard). The public `r2.dev` address in
`library/config.json` is for testing; replace it with a custom domain before production.
Environment `library-production` is restricted to branch `main`; its R2 variables and secrets
are configured. CORS permits GET/HEAD from `https://dancing-every-night.pages.dev`;
thumbnail requests use anonymous CORS to work with the site cross-origin isolation headers.

### Private attachment limitation observed in CI

The default Actions token returns HTTP 404 for this private repository’s issue attachments,
even though an authenticated maintainer can fetch them. Do not interpret a green check job
as acceptance: inspect its report and `check-passed` label. Do not approve `check-failed`.
The manual `fixture_test` option publishes only the committed CC0 test ZIP, into a
maintainer-owned proposal explicitly marked `publication lifecycle test`. It tests R2,
catalogue updates and removal; it does not validate private attachment access.
A separate credential strategy or testing on the future public repository is still required.

### Cloudflare Pages build watch paths

In addition to the existing editor/media paths, include `library/*` and
`tools/library_site.py` in the Pages build watch paths. Otherwise catalogue commits
are received but skipped, leaving published downloads absent from the website.
These paths were configured and verified in the dashboard during the synthetic trial.

### Synthetic publication result (2026-10-25)

Run `40139937848` successfully published `community-2` using the committed fixture.
The anonymous R2 download passed the game's validator (two difficulties), and its SHA-256
matched the committed catalogue. The earlier attachment checks correctly reported a
private-download failure; the fixture trial does not resolve that separate limitation.


### Public repository migration (2026-10-25)

The source is published at `m-a-fr/dancing-every-night` from a new root commit.
The former repository remains private at `m-a-fr/dancing-every-night-private`;
do not push its history or the local `private-history` branch into the public repository.
Official song audio stays in the separate private audio repository.

Public proposal #1 used a new real GitHub attachment: anonymous download returned HTTP 200.
Validation run `40172935142` returned **Ready for review** and `check-passed`.
Applying `approved` triggered publication run `40173189473` (no fixture bypass):
it fetched the attachment, verified the digest, rewrote the publication ID to `community-1`,
uploaded to R2 and updated the catalogue. The public ZIP matched the catalogue SHA-256
and passed the game validator with two difficulties. This resolves the earlier private-attachment
limitation for new proposals submitted to the public repository; old private attachments remain private.

The new repository has its own R2 credentials in the main-only environment and a dedicated
read-only key for the private official-audio repository. Never copy credentials into this document.


The public trial was withdrawn successfully by run `40173453361`: the R2 ZIP returned
HTTP 404, the catalogue is empty again and proposal #1 is closed with label `removed`.
Cloudflare Pages was reconnected to the new public repository using the existing project,
so its address stays unchanged. The GitHub App is now selected for the new public repository
only, rather than the private archive. Reconnection defaults the watched paths to `*`.
