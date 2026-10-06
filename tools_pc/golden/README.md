# Golden baseline frames

`<level>/<platform>/frame_NNNNNN.png`: reference `GE_PCDUMP` captures for
`tools_pc/verify.sh`, one folder per solo level (all 21, `cuba` = the ending)
and per platform (`win` today; `linux` to come).

**Recipe** (2026-10-02 re-base; `verify.sh` applies all of it):
- `data/ge007.ini` pinned to ONLY `[Window] Width = 640, Height = 480` (every
  other option at its compiled-in default; verify.sh backs yours up).
- `GE_RSEED=0x0123456789abcdef` (pinned PRNG).
- `GE_INPUTSCRIPT="20:START"` (skips the intro flyby; the scripted pad is
  the only input) and `GE_PCDUMP="900-1500:300"`: frames 900/1200/1500 are
  settled gameplay on every level. **Cuba** instead: no input
  (`1:SNONE`) and `300-900:300` (the ending cutscene; it returns to the boot
  screens before frame 1200).
- Every run ends via `GE_QUITFRAME` (orderly quit; never a hard kill, D344).

**Gate** (two tiers): the structural framediff (cell means, coverage,
phash) catches gross breakage; then a per-pixel `--exact --tol 2` check
with a per-level limit: 1% of pixels, 3% for Jungle and Surface 2, Cuba
structural only. Measured run to run: <= 0.57% on 18 levels, Jungle 1.75%,
Surface 2 2.33%, Cuba 36%; a global texture-filter switch measured 22%.

Regenerate one level after a deliberate visual change: run
`tools_pc/verify.sh <level>` twice to confirm the new look is stable, then
capture with the recipe above and replace `<level>/<platform>/*.png`.

## History

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
