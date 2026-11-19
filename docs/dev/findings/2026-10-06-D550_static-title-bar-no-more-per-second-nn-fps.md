# D550

<!-- from the old section-F/H table row -->
| D550 | **Static title bar (no more per-second "NN fps" refresh) + project mark as favicon / window / taskbar icon** — original generated crosshair art (no game content); full `## D550` entry at file tail | FIXED 2026-10-17 (port + tooling only, video.c/gfx_sdl2.cpp/make_icon.py; ninja-command-verified + full build 249/249; maintainer live window/taskbar check owed) |

## D550: static title bar (no per-second "NN fps" refresh) + project mark as favicon / window / taskbar icon — FIXED 2026-10-06 (v0.5.0 finalize, maintainer request 2026-10-06)

Two small surface items, both port/tooling-only (zero `src/game` diff, no
goldens):

**1. Static title bar.** `videoPumpEvents` (`port/src/video.c`) refreshed the
window title to `"GoldenEye 007 - NN fps"` about once a second (the
`wmAPI->set_window_title` block). Removed; the title is now set once at
window creation. The live-FPS readout is still available on the F10 overlay.

**2. Project mark in place of the game-still favicon.** The favicon / link
card / window icon used in-engine stills (shot-28.jpg et al.) — derivative
game content in every browser tab and the taskbar. Replaced with an ORIGINAL
generated mark: a generic version of the game's *default aim crosshair*
(circle + N/S/E/W lines crossing its edge), GE red `#c62828` on near-black
`#050607` with a faint glow — a generic functional shape, no logo art,
wordmark or screenshot (tree-hygiene rule 4). One generator,
`tools_pc/make_icon.py` (Pillow, deterministic; `--check` for CI staleness),
emits:
- `docs/img/icon/ge007-icon.png` (512, site favicon + README icon),
- `cmake/icon/ge007.ico` (16–256 multi-size, PNG-compressed entries; 16/24/32
  use a chunkier stroke variant so the mark survives tiny sizes),
- `port/fast3d/ge007_icon.h` (32×32 RGBA, plain R,G,B,A per pixel).

Wiring: `cmake/icon/ge007.rc` (static; the exe icon) via `target_sources` in
the CMakeLists WIN32 branch; `gfx_sdl2.cpp` builds an `SDL_Surface` against
explicit `0x00ff00ff`-style masks (portable, no endianness assumption) and
`SDL_SetWindowIcon`s it after the GL context; `docs/_layouts/default.html`
favicon + apple-touch-icon; `README.md` gets the mark above the badges. The
jekyll-seo `og:image` link-preview card keeps `shot-28.jpg` (a still is fine
there — it is a published page asset, not a tab/favicon) and
`scratch/build_preview.mjs` was restored to the original image constant.

**Implementation bugs found only at build verification (both mine):**
(a) the first `make_icon.py` emitted the C array with *flat* cells per row
(no per-row braces): all 1024 `{r,g,b,a}` cells then sit at the TOP level of
the `[32][32][4]` initializer — 1024 > first dim 32 → "too many
initializers". Both gcc AND clang reject it, correctly: it is genuinely
ill-formed C, not a toolchain bug (braced/nested forms of the same data
compile cleanly in both). (b) `gfx_sdl2.cpp` packed the channels unshifted
(`p[1] & 0x0000ff00` is always 0); masked only by (a) never having compiled.

**Verification-trap note:** in the mixed msys/native agent shell, `g++ ...
| head` reports the PIPE's rc (always 0), so earlier "compiles fine" claims
were false while the TU actually failed. Verified properly: `ninja -t
commands` exact command via cmd.exe with a sane `PATH`/`TMP` (rc 0, only the
pre-existing `gfx_pre_swap_hook` warning) + full `./build-pc.sh ntsc-final`
(249/249 link). Window/taskbar icon + static title owed a maintainer live
check.

**Mark re-proportioned 2026-10-06 (maintainer: "right direction, not accurate
to the in-game crosshair").** Measured off an in-game capture of the default
reticle: the four bars do NOT meet in the centre (open gap ~0.24x the ring's
outer radius) and reach ~1.43x the outer radius (the first cut crossed the
centre and reached ~1.27x). `make_icon.py` now draws eight bar segments from
the gap to the reach (512 master: ring r 150, reach 214, gap 36, stroke 24;
small variant: r 140, reach 200, gap 46, stroke 40 so the centre stays open at
16 px). All three outputs regenerated (deterministic re-run identical). The
`.github/social-preview.html` card's CSS reticle (same old proportions) was
replaced with an inline SVG at the new proportions and the PNG re-rendered.
Still a generic geometric reticle (circle + four bars), no game art copied.
Full `./build-pc.sh ntsc-final` rc 0 with the regenerated header.

**Re-drawn 2026-10-06 (gold).** The colour is now site gold `#e8a13a` (was
GE red `#c62828`), glow included; the proportions were re-measured from the
game's own default crosshair sprite (thin ring about 0.11x the ring radius,
bars about 0.09x the radius, reach about 1.45x the radius, open centre gap
about 0.30x the radius). There is a chunkier small-size variant (16/24/32 px
.ico slots + the 32 px window icon). The `.github/social-preview.html`
card's reticle stays red on purpose — the gold disappears on its manila
background. **Centring fix (same day):** shapes are now drawn symmetric about the exact centre (the line between the two middle supersample pixels; even bar widths, inclusive boxes `[c-r, c+r-1]`), which removes a half-pixel right/down shift at 48/24/16 px; `make_icon.py` (and `--check`) now fail if any emitted size is not pixel-exact left-right and top-bottom symmetric.
