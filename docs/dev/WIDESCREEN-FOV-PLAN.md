# Widescreen & FOV Plan

Status: **proposal, parked** — like `UNLOCKED-FPS-PLAN.md`, resume after audio
work (project Phase 3). Research current as of this writing.

Scope decision (agreed): **target exactly two display aspects — 4:3 and 16:9.**
Arbitrary aspect ratios are TBD and out of scope; the window manager should
snap/letterbox to one of the two rather than trying to serve anything else.

Standard (agreed): **Option B (native 16:9 render, PD-parity) is the goal.**
The PD PC port's widescreen architecture is the reference standard for this
engine family. Option A (4:3-internal + stretch) remains an acceptable
**case-by-case workaround** only where B proves impractical for a specific
surface — each such fallback documented individually.

## Problem

At high resolutions (e.g. 4K) in a 16:9 window the game is disorienting. Root
cause, confirmed in code: the port renders in *window-resolution space*. All
320×240 logical screen coordinates are scaled per-axis by
`RATIO_X = win_w/320`, `RATIO_Y = win_h/240` (`port/fast3d/gfx_pc.cpp:53-54`).
In a 16:9 window everything — world geometry **and** HUD — is stretched 4/3
horizontally: circles become ellipses, and the horizontal FOV balloons from
~78° to ~91° while vertical stays 60°. That stretch is the disorientation.

## Research findings

### 1. The FOV model (where FOV lives)

- Vertical FOV is a fixed 60° base: `FOV_Y_F` (`src/fr.h:137`); per-player
  `fovy`/`aspect` fields default to 60 / `DEFAULT_ASPECT`
  (`src/game/player.c:454-455`).
- Setters: `viSetFovY` / `viSetAspect` / `viSetFov(fovx, fovy)`
  (`src/fr.c:911-934`) → `guPerspectiveF(g_viProjectionMatrixF, …, fovy,
  aspect, znear, zfar, 1.0f)` (`src/fr.c:718`).
- **FOV is dynamic**: aim zoom rewrites fovy every frame via
  `viSetFovY(g_CurrentPlayer->zoominfovy)` (`src/game/bondview2.c:3123`), and
  the camera path resets fovy/aspect per frame
  (`bondviewMovePlayerUpdateViewport`, `src/game/bondview2.c:7967-8034`).
  ⇒ Any FOV option must be a *multiplicative correction composable with zoom*,
  not an override of a constant.

### 2. The game ships with a (stretch-based) 16:9 path

This decomp contains a player-facing widescreen path from the original:
options-menu **"Ratio": Normal / 16:9** (`GAME_OPTIONS_INDEX_RATIO`, string
`OPTION_STR_23_169_LF`, `src/game/options.c:129`), save bit
`OPTION_SCREENRATIO` (`src/game/file2.c:1266-1270, 1323`),
`get/set_screen_ratio()` (`src/game/options.c:553-558`). When active, the
camera sets `aspect = (vp_w/vp_h) × 0.75 × 16/9` (= viewport ratio × 4/3,
`src/game/bondview2.c:8007-8034`) — the pre-distortion for a **4:3 render
stretched to 16:9** (N64-out-4:3, TV stretches). HUD compensation exists for
crosshair (`gunfire.c:6316-6318`, half-width ×0.75) and rocket aim
(`gunfire.c:501`); EU-only extras need an NTSC audit.

⇒ This path is exactly **Option A's** game-side machinery. It is *not* the
PD standard (which renders native 16:9), so under Option B it stays dormant
except as fallback.

### 3. Current port rendering pipeline (what to build on)

- Render happens at window size; an FBO path already exists and activates when
  internal dims ≠ window dims or MSAA > 1 (`gfx_pc.cpp:3041-3115+`).
- Native-res bookkeeping: `GE_NATIVE_W/H` = 640×480 (NTSC) / 640×400 (PAL)
  (`port/src/video.c:40-44`); VI size requests clamp to ≤640
  (`port/src/libultra.c:816`). Auto window defaults to 4:3
  (`port/fast3d/gfx_sdl2.cpp:107`).

### 4. fast3d EXT aspect commands — already present in GE's RSP

GE's software RSP (adapted from PD's) implements Rare's PC-port extension
geometry-mode commands `G_ASPECT_{LEFT,RIGHT,WIDE,CENTER}_EXT`
(`gfx_pc.cpp:1954-1979`, decoder at `gfx_sp_extra_geometry_mode`), and
`port/fast3d/gbiex.h` carries the `gSP…EXT` command macros. **The RSP side of
the PD standard already works in this codebase**; only the game-code emission
sites are missing.

### 5. The PD standard (reference architecture — what Option B replicates)

From the `pd_port` checkout (N64 PD had no widescreen; the port added it):

1. **Projection aspect comes from the port**: `videoGetAspect()` returns the
   real window aspect (`gfx_current_dimensions.aspect_ratio`,
   `port/src/video.c:232-235`). Game code calls `viSetAspect(videoGetAspect())`
   (e.g. `title.c:2495`; even `#define TITLE_ASPECT (videoGetAspect())`).
   Native 16:9 projection ⇒ correct perspective, vertical FOV preserved.
2. **Per-element screen-space corrections** where an element is positioned in
   screen coords: `sightGetAdjustedX(x)`, `×(SCREEN_ASPECT / videoGetAspect())`
   scalings (`sight.c:77,921`, `sky.c:2728+`, `menu.c:2332,4771`,
   `player.c:3108`).
3. **HUD alignment via the EXT commands**: screen-space draw segments wrapped
   in `gSPSetExtraGeometryModeEXT(gdl, G_ASPECT_CENTER_EXT)` …
   `gSPClearExtraGeometryModeEXT(…)`, all under `#ifndef PLATFORM_N64`. With
   the mode active, fast3d scales 4:3-logical screen coords by the *native*
   aspect and centers (or left/right-aligns) them in the wider window — no
   distortion. PD scale: **52 emission sites across 10 game files** (sight,
   hudmsg, bondgun, menu, radar, title, propobj, credits, activemenu,
   game_1531a0).
4. User-facing: vsync/framerate options + HUD-centering option
   (`g_HudCenter` → `g_HudAlignModeL/R`, `port/src/main.c:77-83`).

**M-87 PD-legacy-survey notes (`docs/dev/notes/PD-LEGACY-SURVEY.md`):**
- **Overscan/safe-frame (candidate 2, deliberate divergence, recorded here):**
  PD hardcodes its native viewport to 320×220 (`port/src/video.c:77-79`) — the
  N64-on-a-TV visible area after ~20 lines top/bottom of overscan. **GE's port
  renders the full 640×480/640×400 frame, no overscan crop** (`port/src/video.c:38-47`)
  — a deliberate PC-default choice (nothing hidden, no HUD-clipping risk from a
  TV-only limitation), not an oversight, and not something to "fix" toward PD's
  behavior. Net effect: GE shows ~9% more vertical content than PD at the
  equivalent native resolution. Revisit only if a `Video.SafeAreaOverlay` debug
  knob is ever wanted to visualize the original TV-safe region.
- **"Ratio" 4:3/16:9 option (candidate 3):** PD deleted its N64-era per-player
  Ratio dropdown on PC (`options.c` forces `SCREENRATIO_NORMAL` outside
  `PLATFORM_N64`). **GE deliberately keeps the opposite call** — the shipped
  `SCREEN_RATIO_16_9` path (`get/set_screen_ratio`, `options.c:553-558`,
  persisted per-save, consumed by the camera) is exactly what Option B above is
  planned to build on. Not a gap to close toward PD; a future reviewer should
  not "fix" this toward PD's behavior either.

## Option B — the standard (native 16:9, PD-parity)

### Architecture

Render directly at the 16:9 window resolution. World: projection aspect =
window aspect (vertical FOV preserved, no post-stretch). HUD/screen-space:
4:3-logical coordinates mapped undistorted via the EXT aspect modes
(centered by default; left/right alignment available à la PD). 4:3 windows:
byte-identical to today.

### Change inventory

**Port layer (no rules issues):**
- [ ] `videoGetAspect()` equivalent in `port/src/video.c` (return window
      aspect; GE already tracks `gfx_current_dimensions.aspect_ratio`).
- [ ] Window/aspect policy: `Video.Aspect` {4:3, 16:9}, snap on create/resize;
      out-of-scope aspects → nearest + note (TBD).
- [ ] Make `port/fast3d/gbiex.h` reachable from game TUs (include path or a
      shim header) — build-system only.
- [ ] HUD alignment mode plumbing if we adopt PD's center/wide option
      (`g_HudAlignModeL/R` pattern) — decide in Phase 1; default = centered.

**Game code (exception class — see below):**
- **(a) Projection aspect path.** `bondviewMovePlayerUpdateViewport`
      (`bondview2.c:8007-8034`): under PORT + 16:9, use window aspect instead
      of the ×4/3 stretch formula. Plus the menu/title/briefing call sites
      `viSetFovY(FOV_Y_F); viSetAspect(ASPECT_RATIO_SD)` (`front.c` — ~7+
      sites; enumerate in Phase 1) → PD pattern `viSetAspect(videoGetAspect())`.
- **(b) HUD / screen-space draw sites.** Audit + wrap with EXT aspect modes
      and/or per-element corrections, per surface: gameplay HUD (crosshair —
      replaces the existing ×0.75 compensation; ammo/timer/text via
      `textrelated.c`), menus (`front.c`), title/briefing/credits, letterbox
      bars (`fr.c` gDPFillRectangle calls). GE's known screen-space emitters:
      `front.c`, `textrelated.c`, `gunfire.c`, `bondwalk2.c` (initial grep;
      Phase 1 completes the audit). Expect PD-comparable scale (~10 files),
      possibly less.
- **(c) Anything the audit surfaces** (e.g. mouse→screen mapping for menus,
      cutscene camera framing) — each item logged individually.

### Rule #2 exception framing (needs explicit sign-off to start Phase 2)

This is a behavior-affecting game-code edit class, outside the ABI-only D3x
exception. The case for granting it:

1. **Direct sister-engine precedent**: PD's port made exactly these edits on
   the same Rare engine/fast3d; the project designates PD as standing
   reference for port patterns. We replicate a proven, bounded change class,
   not inventing one.
2. **Bounded and gated**: every edit is `#ifdef PORT` (and where sensible
   runtime aspect-gated) so the N64 build and the 4:3 default path stay
   byte-identical; golden/determinism tests unaffected at 4:3.
3. **The decomp already contains** both a shipped 16:9 branch and `#ifdef
   PORT` precedents in game files — the pattern is not foreign to this tree.

Conditions: explicit user sign-off before Phase 2; each change (or tight
cluster) gets a findings.md entry with §F index label citing the PD analogue;
4:3 byte-identity verified per phase; any edit that would alter *sim* behavior
(not just presentation) stops the work and comes back for review.

### Fallback (case-by-case, documented)

If the Phase 1 audit shows a specific surface is disproportionately costly or
can't be made clean under B (candidate: complex cutscene framing), that
surface may fall back to **Option A** — 4:3-internal render + edge-to-edge
stretch using the game's shipped `SCREEN_RATIO_16_9` path (`set_screen_ratio`
is an existing API; FBO+blit infra exists) — recorded as a finding with the
reason. Whole-feature fallback to A only if B fails verification overall.

## FOV adjustment design (port-only, applies to both options)

- **Hook: software RSP projection post-scale.** Wherever the RSP applies the
  uploaded projection matrix, scale its vertical row by
  `tan(fov_orig/2) / tan(fov_adj/2)`. Zoom rewrites the matrix every frame, so
  post-scaling whatever arrives composes with aim zoom for free. No game code.
- Rejected: editing `viSetFovY` call sites (game code); writing
  `g_ViBackData` from the port (racy).
- UI: `Video.FovScale` (percent of original vertical FOV, e.g. 50–150),
  default 100 = byte-identical to today.

## Phases

### Phase 0 — Verification research (port-only, read-mostly)

- [ ] Playtest the in-game Ratio option as-is on PC; document + screenshot
      what the shipped stretch path does under the current pipeline.
- [ ] Measure baseline FOVs (4:3 and current 16:9-stretch) from the projection
      matrix or a calibration render; record numbers.
- [ ] Confirm `gbiex.h` EXT commands round-trip through GE's RSP with a test
      DL (they should — decoder exists).
- [ ] Audit EU-only aspect constants (`g_GunSightAspectRatio`,
      `EU_CAMERA_8003642C_ASPECT`) for NTSC/PAL effect.
- [ ] Record a Dxxx finding consolidating baselines.

### Phase 1 — Port foundation + full change audit (port-only)

- [ ] `videoGetAspect()`, window/aspect policy, gbiex.h include path.
- [ ] Complete the screen-space draw-site audit for inventory item (b): every
      emitter, its coordinate assumptions, proposed wrap/correction, PD
      analogue cited. Output = the signed change list for Phase 2/3.
- [ ] Decide HUD alignment default (centered vs PD's center/wide option).

### Phase 2 — Projection aspect (game-code exception class, small wave)

- [ ] Item (a): camera path + front.c call sites, `#ifdef PORT`-gated.
- [ ] Verify: world perspective correct at 16:9 (FOV measurement vs Phase 0),
      aim zoom composes, 4:3 byte-identical.

### Phase 3 — HUD alignment wave (game-code exception class)

- [ ] Item (b), per surface in order: gameplay HUD → menus →
      title/briefing/credits → letterbox/misc. Each surface: change, visual
      verification at 4:3 + 16:9, finding entry. Fallback-to-A decisions
      happen here, per surface, with documented reason.

### Phase 4 — FOV slider (port-only) — **LANDED (M-83, D211)**

- [x] RSP projection post-scale (`gfx_apply_fov_scale()` in
      `port/fast3d/gfx_pc.cpp`, load-path only); `Video.FovScale` config
      (percent of vertical FOV, 50–150, default 100 = byte-identical) + F10
      overlay row. Perspective-only (ortho/HUD untouched); columns 0+1 scaled
      together so aspect is preserved. Build clean, `bunker1` no-op confirmed.
- [ ] Owed: interactive eyeball of composition with aim-zoom at scale ≠ 100.

This phase does **not** depend on the Phase 2/3 game-code exception — it
shipped standalone ahead of the rest of the plan.

### Phase 5 — Verification & sign-off

- [ ] FOV measurements per mode/scale vs Phase 0 baselines.
- [ ] Full HUD pass at 4:3 + 16:9; cosmetics → `GRAPHICS-BACKLOG.md`.
- [ ] 4K performance check; MSAA interaction; config persistence; screenshots.
- [ ] `./build-pc.sh` + `/linkcheck`; findings complete with §F index labels.

## Open questions

- HUD alignment UX: always-centered (simple) vs PD's center/wide option
  (more faithful, more surface area). Decide Phase 1.
- In-game Ratio menu entry under Option B: the shipped option means "stretch
  mode" — keep it as the *fallback* trigger, hide it, or repurpose? Decide
  Phase 1 (leaning: leave dormant; port config owns display mode).
- Mouse/menu coordinate mapping at 16:9 (PD had `videoGetAspect()`-scaled
  mouse math in menu.c) — confirm GE's menu input path needs the same.
- Ordering vs `UNLOCKED-FPS-PLAN.md`: both parked behind audio; B renders
  native so the FPS plan's present-stage duplication slots in unchanged.
  Decide order when audio lands.
- PAL: `GE_NATIVE` 640×400 bookkeeping — confirm aspect math per region.
- Split-screen viewports under 16:9 — likely out of scope for PC; note and close.
- **M-87 QoL ask (user, not critical, backlogged):** expose `Video.FovScale` as
  a real horizontal-or-vertical **degree** value instead of the current
  percent-of-original-vertical-FOV scale (D211) — the "50–150%" knob doesn't
  tell the user what degree FOV they're actually getting. Needs: pick a
  convention (report vertical, since that's what `frFovY` actually is post-D211
  refix — `fr.c:737`) and either (a) relabel the F10 row + INI value as degrees
  directly (`frFovY = clamp(requestedDeg, ~24°, 160°)`, drop the percent
  multiply), or (b) keep percent internally but show the *computed* degree
  value live in the F10 overlay/INI comment for transparency. (a) is cleaner
  and removes a level of indirection; check whether anything else reads
  `Video.FovScale` as a percent before renaming the key (search besides
  `fr.c`/`video.c`). Cosmetic/UX only, no render-path change beyond the
  input unit.
