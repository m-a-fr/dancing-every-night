# Generating song difficulties

Read this guide before generating a batch or changing `mapper.py`. The shared generator powers `make song`, the local editor and the online editor's automatic draft. Automatic output is a draft to review and play, not a finished human playtest.

## Start with the song

1. Check existing song manifests and the library for duplicates, including translated titles and alternate edits.
2. Identify the exact recording. Prefer original artist/publisher audio and original release artwork. Record source URLs and credits separately for audio, artwork and video. Public availability is not redistribution permission.
3. Measure tempo, phase and meter on that recording. Review half/double-tempo and 3/2 or 4/3 pulse aliases against the audio, especially above 200 BPM. A reference tempo alone does not establish the offset of a different upload. Review changing tempo instead of forcing one grid over a drift.
4. Describe one playable signature for the song: what repeats, what answers it, where it breathes and how it ends. Do not invent section timestamps before analysis.
5. Generate four difficulty drafts, inspect their reports, repair conflicts, validate the packages, then play them.

For a batch, vary musical character, not only lane names. Two button sequences with identical hand changes and repetition relationships may feel interchangeable. More catalogue entries alone are not evidence of more variety.

## Musical gestures

`feel_template` proposes hand/position sequences for attacks already selected from the audio. It does not add timing events. `Arranger.realize` may change or remove a gesture to respect the physical constraints.

| Internal name | Gesture | Musical use | Eligibility |
|---|---|---|---|
| `jack`, `jack3`, `jackpair` | Repeated button, then release or relay | Repeated syllable, ostinato, repeated impact | Existing per-difficulty palettes and repeat limits |
| `anchor` | One hand returns to a fixed button while the other moves | Bass pulse and melodic reply | All levels |
| `callresp` | Compact call, mirrored reply | Musical dialogue | All levels |
| `roll`, `sweep`, `handtrill`, `jump` | Contour or local movement | Moving line, fill or transition | Existing palette and hand-spacing limits |
| `pedal_return` | A–B–A on one hand, then the other hand | Riff that leaves and returns to an anchor; not a fast same-hand stream | NORMAL/HARD/EXPERT, at least four attacks in the bar |
| `relay_pair` | One call, two identical replies from the other hand, then release | Pickup followed by repeated response; keep its repeated-button identity | All levels, at least three attacks; counts toward the repeated-gesture cap |
| `turnaround` | Cross to the other hand, take a short detour, return to the initial hand/button | A melodic turn or developed phrase; avoid applying it to every bar | HARD/EXPERT, at least five attacks |

These are possibilities, not quotas. Retain ordinary alternation or repeated notes when they fit. Do not force a new gesture into a sparse bar, or add attacks to make it fit. Inspect the report's intended-versus-preserved gesture counts: a template that is always changed by the safety rules is not a successful new feel.

The report compares exact requested hands and positions at the arrangement stage, before final cleanup. It is a diagnostic, not a fun score: a mirrored fallback can still preserve the musical feeling. Review the relationships between touches and the audio as well as the counts.

## Form and difficulty

- Preserve a recognizable core when the music returns. Variation is subordinate to the musical return, not obligatory every time.
- Mirrored answers require matching rhythms in adjacent bars. The same number of notes alone does not establish a response.
- Keep silent bars in the phrase count. Do not bridge every empty space with extra taps.
- Choose holds from sustained lines, doubles from meaningful impacts and scratches from clear accents with free space around them.
- End according to the audio: stop, resonance or fade. Avoid a fixed final button pair.
- EASY teaches pulse and the simplest identity; NORMAL states the hook; HARD develops selected syncopations; EXPERT adds meaningful details. Do not increase difficulty by changing every button or filling every subdivision.

Keep `SIMPLE`, `VALIDATED`, hold restrictions, scratch clearance, burst limits and visual spacing in force. Add no exception merely to preserve a template. Packaging validation alone does not check every ergonomic conflict: audit both hands during holds, actual inter-onset gaps and scratch clearances too.

## Travel time is an author choice

The default remains **1500 ms for every difficulty**. Do not assign a fixed speed ladder by difficulty or derive speed mechanically from BPM or note density. First ask what changing travel time adds to the feel: momentum, anticipation, weight, or readable phrasing. Keep the default when there is no benefit to test.

Try small changes first, normally 50–150 ms from the default, and compare in play. This is an audition range, not an automatic target. EXPERT may keep the same travel time as NORMAL, or a longer one. Store the intention alongside the batch manifest and distinguish an untested intention from player-validated feedback.

```
make song SRC="your-audio.ogg" TITLE="Your song" ARTIST="Artist" \
  APPROACH="NORMAL=1450 HARD=1600"
```

Only those two difficulties change; the others keep 1500 ms. `mapper.py` and `song_import.py` also accept repeated `--approach LEVEL=MS` arguments. The editor stores travel time separately for each difficulty; empty means 1500 ms. The generator's visual same-lane gap uses the actual travel time (at least 18% of travel, and at least half a beat).

## Local delivery checklist

- Keep each song independent unless a pack was requested. Four `.chart` files and their media belong in the same song folder.
- If a video budget was requested, test the completed compressed ZIP, including audio, artwork and credits. A candidate video's size alone is insufficient. Do not include a redundant movie when the source is only a still cover.
- Follow the current shared media defaults: raw H.264 Baseline (`video.h264`), 480×272, 24 fps, 720 kb/s. Existing MPEG-1 songs remain compatible, but do not inherit an old batch's video settings accidentally.
- Retain the original-quality artwork separately and fit the game background without stretching it.
- Run the physical audit and `make check MAP=...`; review tempo warnings and actual difficulty progression.
- Generate individual ZIPs and the requested aggregate archive. Verify hashes and download links. Record what was tested automatically and what remains a human playtest.
- Bound parallel analysis and encoding to avoid memory pressure and thread oversubscription. Write conversions to temporary files and rename only on success; a partial media file is not a cache hit or a completed song.
- Do not publish song media or deploy to the console merely because a local batch is ready.

Regression checks: `tools/.venv/bin/python tools/test_mapper.py` and `node tools/test_editor.cjs` (the latter needs the desktop game validator). Build the desktop and Vita targets according to `AGENTS.md` after shared-tool changes. `scripts/build-editor-site.sh` copies the shared generator into the online editor build.
