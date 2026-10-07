# Golden baseline frames

`<level>/<platform>/frame_NNNNNN.png`: reference `GE_PCDUMP` captures for
`tools_pc/verify.sh`, one folder per solo level (all 21, `cuba` = the ending)
and per platform (`win`, and `linux` — 21 levels x 3 frames on each, captured
2026-10-05 at the same stems; `deck` = the Steam Deck set, captured with
`capture_p7.sh -p deck`, per-level pixel limits = the linux ones for now).

**Recipe** (2026-10-02 re-base; `verify.sh` applies all of it):
- `data/ge007.ini` pinned to ONLY `[Window] Width = 640, Height = 480` (every
  other option at its compiled-in default). Each run gets its own copy in a
  temp work dir (see Isolation below); your `data/ge007.ini` is never touched.
- `GE_RSEED=0x0123456789abcdef` (pinned PRNG).
- `GE_INPUTSCRIPT="20:START"` (skips the intro flyby; the scripted pad is
  the only input). Window per level, exactly as `verify.sh`'s `golden_dump_for`
  states it (D522 re-base 2026-10-05 — the 900 stem is NOT settled gameplay on
  every level: it fails the 1% limit on Surface 2 / Streets / Depot / Cradle and
  is still inside Dam's flyby):
  `900-1500:300` (900/1200/1500) on the 14 levels where 900 is settled,
  `1000-1400:200` (1000/1200/1400) on Surface 2 / Streets / Depot / Cradle /
  Frigate, `1500-1700:100` (1500/1600/1700) on Dam, and **Cuba** with no input
  (`1:SNONE`) and `300-900:300` (the ending cutscene; it returns to the boot
  screens before frame 1200). `framediff` walks the CANDIDATE frames, so a
  sweep's `--dump` must hit the golden stems exactly — never widen a stride.
- The gate INSTALLS THE CANONICAL SAVE `tools_pc/golden/ge007.eep` (2 KB, in-tree;
  D523, **superseded by D529**): the frame depends on the save's **content**, not
  its presence — D523's "presence, not which save" claim (worst_cell 0.175625 on
  each of the four candidate saves, 21.557% with none) was only ever measured on
  Archives, and the 2026-10-05 A/B refutes it (facility 177.47 scene-level on the
  2026-10-05 playtest save, 0.0-0.25 on every other candidate on disk). `verify.sh`
  and `capture_p7.sh` install the canonical eep into each run's own work dir
  (the repo's `data/` is never modified), so the gate is reproducible on a fresh clone and in CI. A local playtest
  save is **never** the gate's input. The linux 63-frame set was re-captured
  on the box under the canonical eep 2026-10-05 (D531; the 2026-10-05
  re-round, D530, turned out to have run under the box-LOCAL save — the
  `capture_p7.sh` per-level re-pin bug, fixed in D531) and is valid.
- Every run ends via `GE_QUITFRAME` (orderly quit; never a hard kill, D344).
- **Isolation + parallelism:** `verify.sh` and `capture_p7.sh` run every level with
  CWD = a fresh temp work dir holding its own `data/` (ROM, `pccg-<romid>/`,
  `pcmodels-<romid>/`, the canonical save, the pinned ini) and its own `ppm/`; the
  repo's `data/` is only read (the old pin/restore of `data/ge007.ini`/`.eep` is
  gone). That makes levels safe to run concurrently:
  `tools_pc/verify.sh sweep -j 3` / `tools_pc/capture_p7.sh -j 2 ...`. Pixel
  results assume each instance holds 60 fps (D117): `-j 2-3` only on a strong GPU
  with nothing else loading it (no local LLM loaded), `-j 1` on weak boxes.
  Measured 2026-10-05 with a busy GPU (an LLM resident at ~100%): Surface 1's
  frame 900 failed at `-j 2` and `-j 3` and passed at `-j 1`.
- Windows set: from Git Bash, `bash tools_pc/capture_p7.sh -b build-pc -p win -j 3 <level>:<num> ...`
  (no DISPLAY on Windows; `-j 1` if the GPU is loaded).
- Deck set: on the device (Desktop Mode, `DISPLAY=:0`, prebuilt binary at
  `<build>/ge007.x86_64`): `bash tools_pc/capture_p7.sh -b <build> -p deck <level>:<num> ...`.

**Gate** (two tiers): the structural framediff (cell means, coverage,
phash) catches gross breakage; then a per-pixel `--exact --tol 2` check
with a per-level limit: 1% of pixels, 3% for Jungle and Surface 2, Cuba
structural only. Measured run to run: <= 0.57% on 18 levels, Jungle 1.75%,
Surface 2 2.33%, Cuba 36%; a global texture-filter switch measured 22%.

**Cross-platform rule (D521, P7):** a frame stem is comparable across
platforms only if it is settled gameplay on *every* platform in the set.
The intro flyby is wall-clock paced (D117): a box that cannot hold 60 fps
stretches wall time without advancing the sim-frame counter, so the flyby
settles at a different frame per box. On the Linux capture box (Mesa /
Intel HD 3000; the golden stems rendered in 11.8-15.1 ms) frame 900 is still
inside the flyby (it settles between ~830 and ~1130 run to run) while the
win/ golden at 900 is settled -- linux/900 vs win/900 is a scene offset
(92.863% over tol 2, phash 83 on Dam), NOT a renderer defect. After the 2026-10-05
re-round both platforms sit at the same stems and the cross-platform spread is
0.348-3.983% over tol 2 (structural tier 21/21 clean, phash <= 6 except cradle
at 40; Cuba 23.7-36.3% is structural-tier-only) — informational only. The gate
each platform ships is its own captures against its own goldens; never call the
cross-platform spread a parity gate. 1200/1500 are
settled on both and measure the real driver delta. `GE_DETERM=1` is for
Linux-vs-Linux regression runs only (it changes the frame -> sim mapping;
against the win/ goldens it measures 15.788% at 900, 14.594% at 1200 and
16.171% at 1500, which is expected and must never be read as a delta).

Regenerate one level after a deliberate visual change: run
`tools_pc/verify.sh <level>` twice to confirm the new look is stable, then
capture with the recipe above and replace `<level>/<platform>/*.png`.

## History

- **D531 (2026-10-05):** `capture_p7.sh`'s D529 adaptation was incomplete —
  the per-level re-pin restored the pre-capture LOCAL save (D524-era line),
  silently overwriting D529's pre-loop canonical install, so the 2026-10-05
  box re-round (D530) ran under the box-local save, not the canonical one.
  Fixed (loop now re-pins the canonical save per level; `cd "$ROOT"` added)
  and re-captured: fresh 21/21 box round, 63 frames committed, replacing the
  D530 set (47/63 frames differ — save content demonstrably moves the frames;
  16 byte-identical save-independent scenes).
- **D530 (2026-10-05, AMENDED BY D531):** the linux set was re-rounded on the
  box (X220, tree `442eb2ff`; `~/p7-reround.log` hit `P7 re-round DONE`
  2026-10-05 12:53 EDT, 21/21 rc=0) and committed, replacing the D525
  cleared-eep set (51/63 frames differ; 12 save-independent scenes byte-
  identical). The "canonical eep" provenance claim was wrong — see D531.

- **D168 (2026-09-01):** the `GE_PCDUMP` PPM writer emitted `glReadPixels` rows
  unreversed, so every capture — and this baseline set — was vertically
  flipped. Writer fixed (`port/fast3d/gfx_opengl.cpp`); this set regenerated
  from a fresh, correctly-oriented `-level_09` run.
- **2026-09-23 (v0.4.0):** re-baselined. The 2026-09-09 (D215) set was captured
  while the port still rendered at 30 fps (2 sim ticks per frame); D248 moved
  it to 60 fps (1 tick/frame), so frame N now lands at half the sim time, and
  frames 200/320/440 fell in the intro cutscene, whose camera is
  nondeterministic (D117); two fresh captures disagreed at 440. The window
  moved to `640-1120:240`, the same sim ticks the original set sampled.
  Two independent captures of it agree 3/3. The capture ini is now
  defaults-only, and `verify.sh` pins it the same way (it used to rewrite
  only Width/Height, which kept personal settings).
- **2026-09-23 (later):** window moved again, `400-880:240` → `640-1120:240`.
  Frame 400 is the last beat of the intro cutscene (Bond in the shaft) and
  its camera is nondeterministic (D117): 1 in ~5 gate runs failed it (dmean
  0.7 vs 20.7 run to run). All three frames are now settled gameplay; three
  independent captures agree (worst dmean ≤ 6.8).
- **2026-10-02:** re-based and extended to all 21 levels (`<level>/win/`),
  recipe above. The flat bunker1 set at the folder root was retired. Two
  independent 21-level passes agreed; noise per level measured as listed.
  `verify.sh` no longer hard-kills (taskkill of every `ge007.x86_64.exe`,
  including a maintainer's game); it quits via `GE_QUITFRAME` and only stops
  its own PID as a watchdog fallback.
- **2026-10-04 (P7):** first `<level>/linux/` set captured (21 levels, the
  recipe above verbatim) on a real-GL Linux box; `verify.sh`'s linux pixcount/
  framediff skip is now driver-conditional (a `glxinfo` renderer probe, D521)
  -- llvmpipe/softpipe/swrast still skip, a real driver runs the full pixel
  gate. Structural framediff linux-vs-win: 19/21 clean; Dam (frame 900, the
  flyby) and Streets (frame 1200, a diagonal region delta) fail. Exact over-
  tol2: 1.1-6.6% on 17 levels; Archives 20-24.6% (a constant ~20 delta over
  the top 3 grid rows only), Cuba 10.7-26.4% (uniform d 3-6, cell means <=
  11: dither noise floor), Jungle 5.9-6.6%, Streets 1200 9.6%. See D521.
- **2026-10-05 (D522/D523 re-base):** the win set's window moved on six levels —
  Surface 2 / Streets / Depot / Cradle / Frigate to `1000-1400:200`, Dam to
  `1500-1700:100`. Measured, same build, honest sequential passes: at 900 those
  levels fail the per-pixel limit (Dam 6.542% @900 worst_cell 12.11, Surface 2
  3.752%, Streets 1.001%, Depot 1.267%, Cradle 1.754%, Frigate 8.391% on a later
  run) and Dam's 900 is still flyby (run-to-run 3.6-3.8% over tol 2, maxchan 168);
  Dam's 1200 looked settled on two runs (0.000%) then diverged 5.761% on a third,
  so it moved to 1500-1700 (1500/1700 0.000% across three runs, 1600 keeps ~40
  moving pixels at 0.013%). At the new stems three independent runs agree within
  tol 2. `verify.sh` now pins the save file (D523). **The `<level>/linux/` set was
  stale everywhere**, not only on those six levels: P7's 2026-10-04 round ran with
  `data/ge007.eep` cleared, and D523 measures that save-file *presence* moves ~every
  level (Archives 21.557% with none, 0.175625 worst_cell with any of four saves).
- **2026-10-05 (P7b, linux re-round):** the box re-captured all 21 levels (63 frames,
  21/21 rc=0, frames=3, 25-29 s each) at the windows `verify.sh` carries, with the
  save pinned present, on the same real-GL box (Mesa 23.0.3 / Intel HD 3000). The
  set is now one frame per recipe stem on every level, both platforms. Cross-platform
  framediff (linux vs win, tol 2): **structural 21/21 PASS** (worst cell mean 4.43
  on streets; phash <= 6 except cradle at 40, a hash tie with cell means 1.17), exact
  **0.348-3.983%** on the 20 non-Cuba levels (Cuba 23.7-36.3%, structural tier only)
  — about half the stale set's 1.142-6.958%. **D521's two residual bands are closed
  by this measurement:** Archives 19.956-24.554% (cell means to 50.8, rows 0-2) is
  now 3.590-3.859% (worst cell 2.31, phash 1) and Streets 1200's 16.0-148.4 cell-mean
  band is now 3.616-3.983% (worst cell 4.43, phash 0) — both were the save-state
  difference, not a renderer. Dam's 900 (92.863%, phash 83) is out of the set; at
  1500-1700 Dam measures 2.964-3.443%. **Caveat:** the box's own sweep is crash-detect
  only — `glxinfo -B` fails there (rc 255, "unable to open display :0" from a non-login
  ssh shell) so `verify.sh`'s fail-closed probe keeps the pixcount/framediff skip even
  though the game's readback works; the cross-platform numbers are computed on Windows
  against the merged linux PNGs. See D525.
- **2026-10-05 (isolated + parallel capture, `deck` platform):** `verify.sh` and
  `capture_p7.sh` now run each level in its own temp work dir (own `data/` copy,
  pinned ini, canonical save, `ppm/`) instead of pinning/restoring the repo's
  `data/`; added `-j N` (concurrent levels, verdicts in level order) and a `deck`
  platform (`verify.sh --platform deck`, `capture_p7.sh -p deck`).
- **2026-10-05 (D540 re-base #2, win only):** final fog design (far clip scales
  by min(Fog, Draw distance); the game's fog math untouched, so fogged levels
  render the N64's own fog at defaults). Re-captured whole levels: aztec, cradle,
  dam, egypt, jungle, statue, surface1, streets (24 frames, win). Side-by-sides:
  fog only (Cradle's distant truss is a faint ghost, Frigate's sea unchanged);
  props, characters, gun and HUD present, no overlays. Pass-A vs pass-B
  (over tol 2): <=0.30% (aztec 0.19, egypt 0.26, statue 0.30), cradle/dam/jungle/
  surface1 <=0.003%; streets 0.75-2.1% (timer digits jitter; promoted from the
  pass whose state the confirming runs reproduce). Confirming pass 21/21 PASS
  (silo failed once with worst_cell 127 and passed on the single re-run; streets
  failed twice before its re-promotion, then passed twice). The `<level>/linux/`
  set is stale on the re-captured levels until the box re-captures.
- **2026-10-05 (D540, linux re-base + first `deck` set):** same final fog code,
  captured concurrently on the two boxes. **linux** (X220, `-j 1`, rebuilt from
  the working tree): pass A against the existing set; 13 levels already within
  limit and were left; re-captured aztec, cradle, egypt, statue, surface1, dam,
  jungle (A/B 0.00-0.45%, dam 0.000%, jungle 1.7% under its 3% limit; dam/jungle
  differ from the old set by the fog change, 8.4% / 37.9%). streets/silo A/B run
  0.3-3.0% on the box (left as captured earlier; they match their goldens).
  **deck** (Steam Deck, Desktop Mode, the X220-built binary + bundled SDL2,
  `capture_p7.sh -p deck -j 2`): all 21 levels, A/B <= 0.04% except jungle
  1.65% and statue 0.44%; `-j 3` failed its validation on the Deck (archives
  4.96%, frigate 7-11%), `-j 2` agreed (<= 0.031%), 286 s per 21-level pass.
  Both tools now set `GE_FAKE_DECK=0` on every run: on a Deck the D283 preset
  otherwise forces 1280x800 fullscreen over the pinned ini. Also on 2026-10-05,
  before the final fog design, a first D540 win re-base (17 levels) was
  superseded by re-base #2 above; the levels it alone touched pass re-base #2's
  confirming pass. **Run sweeps with the GPU otherwise idle** (no local LLM
  loaded): 60 fps pacing is part of the gate (D117).
- **2026-10-06 (v0.5.0 re-base, linux + deck):** D543 (CPU near-plane clip +
  per-vertex N64 fog) and D546 (Draw/LOD 2.0x default) re-captured on both
  boxes concurrently from one X220-built binary (tree `21c5b5c5`), canonical
  eep md5-checked on each box. Two full passes each (X220 `-j 1` 551 s/pass,
  Deck `-j 2` 286 s/pass, every run rc 0). Pass A vs B over tol 2: deck 63/63
  within limit; linux 60/63 — Dam drifted 5.2-6.0% between A and B (Bond's
  settle position after the wall-clock-paced flyby, D117), so two Dam-only
  runs C/D were taken: C vs D <= 0.022% and C vs the previous golden <= 0.023%,
  so C was promoted (A and B were the outliers). Change vs the previous set is
  concentrated where fog/draw distance bites: Statue ~26%, Surface 2 20-27%,
  Streets 19-20%, Surface 1 ~13%, Depot ~10%, Cradle ~5%; indoor levels
  <= 0.4%. Cradle and Frigate compared by eye against 1964/GEPD stills: match.
  **win (same day, same tree):** `capture_p7.sh` now runs on Windows too
  (Git Bash; `-b build-pc -p win`, no DISPLAY; picks the python that has
  Pillow). Two passes at `-j 3` on the dev box (190 s each, every run rc 0,
  a local LLM resident on the GPU): 62/63 A/B-stable; Streets 1400 jittered
  1.5% (timer digits), two Streets-only `-j 1` runs C/D sat within 0.81% of
  pass B, so B was promoted for Streets, A elsewhere. Deltas vs the previous
  win set mirror linux/deck (Statue ~26%, Surface 2 ~27%, Streets ~20%,
  Surface 1 ~13%, Depot ~10%, Cradle ~5%). Cross-platform spread on the new
  sets, over tol 2, non-Cuba: win-linux 0.406-4.553%, win-deck 0.188-3.264%,
  linux-deck 0.281-3.828% (informational, not a parity gate).
- **2026-10-06 (D553, near-clip fog recomputed):** Windows sweep after the
  change flagged surface1/depot/cradle; re-captured surface1/depot/statue/
  cradle on all three platforms, two passes each. Only **cradle** really
  moved (~4.9% over tol 2, A/B <= 0.12% on every platform; the near walkway
  is very slightly lighter, still in line with the 1964 still) and was
  re-based x3. depot/surface1 "regressions" were `-j 3` pacing flakes with a
  local LLM on the GPU: captured alone, depot is pixel-identical to its
  golden and surface1/statue are within 0.5% -- those sets were left as-is.
  The Deck must be in Desktop Mode for captures (Game Mode = Wayland/
  gamescope: every run times out with 0 frames).
