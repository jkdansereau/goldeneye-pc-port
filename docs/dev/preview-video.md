# GitHub preview video — runbook

The README embeds `docs/media/goldeneye-gh-preview.mp4` (the gif of the same
name is too heavy for GitHub; keep it only if someone wants a local copy).
It's a ~32 s, 8-clip montage of gameplay cut from ShadowPlay session
recordings.

## Tooling

`scratchpad/` is gitignored, so `make-preview.py` is a **local** tool (it
hardcodes local video paths anyway) — this doc is the tracked artifact. If it
goes missing, the commands below describe exactly what to rebuild. Run with
plain `python`.
Working files (per-clip mp4s, concat list, palette, `state.json`) go to
`scratchpad/.preview-work/` — disposable, safe to wipe (but then re-run
`build` after any edit).

```sh
python scratchpad/make-preview.py list                  # montage index -> source session + time
python scratchpad/make-preview.py shift <i> <±sec>      # nudge a clip within its source video
python scratchpad/make-preview.py set   <i> <sec>       # absolute source time
python scratchpad/make-preview.py replace <i> <.10|.12|.18|.19>  # new random spot in a session
python scratchpad/make-preview.py regen --seed N        # full random redo (same clips/lengths)
python scratchpad/make-preview.py build                 # re-concat + re-render from state.json
```

Every edit command rebuilds mp4 + gif automatically. A full rebuild takes
~30–50 s; a single-clip shift is the same cost (concat dominates). Budget
one command per step if running under a timeout — cut+concat and the two gif
passes together fit in ~55 s.

## Source material

`C:\Users\james\Videos\NVIDIA\Build-pc\new\` — ShadowPlay recordings
(3072x1728 @ 60 fps, h264), no usable audio track (cut with `-an`). The four
sessions and their tags are hardcoded in `SESSIONS` at the top of the script;
add a new session there (tag, filename suffix, duration) to use it.

## Rules learned from review rounds

- **Never start a clip in the first ~60 s** of a session: desktop / boot /
  menus. (`MIN_START`)
- **Bad zones** — source times that show non-gameplay; random picks avoid
  them and `replace` should too. Currently known (in `BAD_ZONES`):
  - `.10` @ ~395–425 s: file-select menu
  - `.10` @ ~1290–1420 s: watch close-up (appears twice in that session)
  Add new ones here whenever a review round finds another.
- **Clip length** 4 s is the sweet spot; 5 s starts to drag, <3 s flickers.
- **Same-session spacing**: two clips from one session ≥90 s apart, or they
  read as a repeat of the same scene.
- When told "roll forward/back ~N seconds", use N ± a few s and prefer a
  high-motion moment in that window if you have keyframe stats (see below);
  otherwise a flat offset is fine.
- If a pick lands on a menu/loading screen, step it forward again rather
  than picking a new random time — the user's "a bit later/earlier" almost
  always means the *same* scene, different part.

## Output spec (don't change casually)

- mp4: 1920x1080 @ 30 fps, libx264 crf 20, `+faststart`, no audio. ~25 MB for 32 s.
- gif: 512 px wide, 12 fps, two-pass palette (`palettegen` then
  `paletteuse=dither=bayer:bayer_scale=3`). ~28 MB — local use only.

## Optional: motion data to find "cool" moments

`scratchpad/analyze-sessions.py` / the old `best-of*.py` scripts dump
per-keyframe YAVG diffs (`k<N>.txt` in `.preview-work/`) for exactly these
session files; the diff between consecutive keyframes is a decent proxy for
"something is happening". If you need to find the coolest moment in a time
window, maximize it — but note plain random picks with spacing constraints
have been preferred by the reviewer over signal-optimized cuts, so use this
only when asked for "something cool".

## ffmpeg notes (Windows)

No system ffmpeg; the script falls back to the `imageio-ffmpeg` static
binary (`pip install imageio-ffmpeg`). Pass paths as forward-slash or bare
filenames with `cwd=` set — drive-letter colons and backslashes break
filter-graph argument parsing.
