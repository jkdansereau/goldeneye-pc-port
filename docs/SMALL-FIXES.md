# SMALL-FIXES — ready-to-execute fix notes for small / low-level open items

**Read-only document.** This file records *proposed fixes and verification
procedures* for the small, low-level bugs and noted issues that are parked in
`BACKLOG.md`, `GRAPHICS-BACKLOG.md`, `HANDOFF.md`, and the §F/§H finding log.
No code was changed when writing it (M-28). Each item is self-contained enough
to be picked up cold; items marked **VERIFY-ONLY** already have their fix in
the tree and only need confirmation + commit hygiene.

Standing rules for every item below:

- Game-code edits must stay inside the narrow `#ifdef PORT` ABI/layout
  exception (AGENTS.md #2); port-layer work has no such limit. Anything
  bigger than described here → stop, write it up with a confidence rating.
- New findings get the next free Dxx in `docs/PCPortResearch.md` §F/§H + an
  index row; recurring bug classes go to `docs/PORT-LEARNINGS.md`.
- After any converter change: full regen chain
  `python tools_pc/d43_emit.py ntsc-final && python tools_pc/d69_emit.py
  ntsc-final && python tools_pc/d88_emit.py ntsc-final --regen`.
- Verification anchors (quiet machine only — D153): target level boots to ≥1
  non-degenerate frame (`tools_pc/pixcount.py`), `-level_09` + `-level_20`
  unregressed (`tools_pc/framediff.py`, use `--mask` for the HUD).
- Crashes: `tools_pc/debug.ps1` (gdb + FATAL line + backtrace → `gdb.txt`).

---

## A. VERIFY-ONLY — fix is in the tree, needs confirmation

### A1. D154 — `bg.c` room-geometry hit-test GBI parser (committed WIP `072b5c44`)

**Status:** written + builds green; **never exercised** (a no-input
`GE_PCDUMP` capture fires no bullets, and the M-28 box was thrashed).

**What it is:** `bgTestRayIntersectionInRoom` (`src/game/bg.c:~3331`, called
from `bgTestBulletHitBackground` on every shot that resolves against
background geometry) was the D135 sibling: raw 8-byte-N64-`Gfx` byte/word
indexing into the 16-byte PC room DL → desync → OOB `vtxbase[idx]` / walk-off
AV the first time a wall or floor is shot. The `#ifdef PORT` port reads
`gdl->words.w0/.w1` (G_TRI1 from `(w1>>16/8/0)&0xff/10`; G_TRI4 nibbles from
`(u32)words.w0/.w1` with the byte-mapping table in the code comment); both
`texturenum` recoveries return `-1` (generic impact decal/sound, parked w/ D77).

**Procedure:**
1. Quiet machine. `./tools_pc/debug.ps1 -level_09`.
2. Get into a firefight and **shoot walls/floor deliberately** (not just
   guards) for ~60 s — this is the only trigger.
3. Pass = no AV in `bgTestRayIntersectionInRoom` / `bgTestBulletHitBackground`,
   bullets still resolve hits (decal/sound may be generic).
4. `-level_09` + `-level_20` framediff unregressed.
5. Then fix up the commit (`072b5c44` is labelled "WIP, unverified") and close
   the §F D154 row.

**Risk:** low — `#ifdef PORT` only, N64 path verbatim under `#else`. If a new
fault appears in this function, the nibble-mapping table is the thing to
re-derive against `rsp/graphics/gmain.s` (ground truth for GBI decoding).

### A2. D139 — stage-unload `cleanupObjects` type byte (fix committed M-23)

**Status:** fix in (`src/game/cleanup_objects.c`, `#ifdef PORT`
`CLEANUP_PDTYPE(o)` = `((PropDefHeaderRecord*)o)->type` instead of `(u8)obj[0]`,
which on LE reads `extrascale` and walks off the propDef blob). **Never
verified** — the fast teardown test hit D140 first (since fixed), and M-27's
level-exit loop was playtested through the *real* front-end flow, which does
exercise `lvlUnloadStageTextData` → `cleanupObjects` on level exit.

**Procedure:**
1. Re-check whether M-27's Dam→Facility playthrough already exercised it:
   if a real level exit completed without a teardown crash after the D139
   commit, this is done — just record "verified via M-27 exit loop" on the
   §F row and stop.
2. Otherwise: `debug.ps1 -level_09`, complete/abort a real level exit (not
   pause-abort) and confirm no AV in `objFree` / `objFreeEmbedmentOrProjectile`.

**Risk:** low — one-line read-source change, every other propDef consumer
already uses the struct member.

---

## B. Root-caused small fixes (not yet applied)

### B1. D74 — dead + OOB texture-wrap block in fast3d (`port/fast3d/gfx_pc.cpp:~1570`)

**Status:** latent. Harmless for CLAMP glyphs today; could corrupt wrapped
textures on triangles (room panels, model skins).

**Root cause (confirmed against the tree):** two independent defects in the
D74 pre-wrap block inside the per-vertex UV loop:
1. **Always-false guard.** The codebase's `gbi.h` (`include/PR/gbi.h`, used by
   both game and fast3d via `port/shim/PR/gbi.h`) encodes wrap mode as a
   2-bit field: `G_TX_NOMIRROR|G_TX_WRAP = 0x0`, `G_TX_MIRROR = 0x1`,
   `G_TX_CLAMP = 0x2`. So `cms & G_TX_WRAP` is `& 0` — **always false**. The
   "is this tile in wrap mode" test must be **"clamp bit not set"**:
   `(cms & G_TX_CLAMP) == 0`. (Compare the correct switch at
   `gfx_opengl.cpp:706-712`, which keys on the four combinations.)
2. **OOB index.** `tex_width2[i]` / `tex_height2[i]` are **per-texunit `[2]`
   arrays** (set up at `gfx_pc.cpp:~1471`), but the block is inside the
   **per-vertex** loop where `i` is 0..2 → read of slot `[2]` if the guard
   ever fires.

**Fix:**
1. Hoist the tile-window computation out of the vertex loop: for each used
   texunit `t`, compute once — `tw = tex_width2[t]`, `th = tex_height2[t]`,
   `wrapS = (cms & G_TX_CLAMP) == 0 && tw < tex_width[t]`, same for T.
2. In the vertex loop, apply `u = fmodf(u, tw); if (u<0) u += tw; u += uls/4`
   (and V) only when the precomputed flag is set.
3. Delete nothing else — leave the surrounding D74 comment, extend it with a
   line noting the `G_TX_WRAP == 0` encoding trap (PORT-LEARNINGS §D class:
   "N64 hardware idioms fast3d does not emulate" — add the bit-encoding note).

**Verify:** build; `-level_09` + `-level_20` framediff; then a level with
visible wrapped triangle textures — **Depot (`-level_30`)** is the known
worst case (overlaps B2, but D74's block only fires for WRAP tiles). Check
`tools_pc/golden/` dumps for no new garbage.

**Risk:** low-medium. The block has never run, so this is *new* behavior —
if wrapped textures suddenly look wrong, the fmod window size (`tw` from
`lrs-uls+4`/4) is the thing to validate against `rsp/graphics/gmain.s`'s
tile-window handling before "fixing" it further.

### B2. D152 — find the leaking `osSetIntMask` site + proper narrow fix

**Status:** mitigated (self-healing lock in `port/src/libultra.c`, M-28) — a
leaked section now costs ~2 s + a log line instead of a permanent black
screen. The **root leak is still unidentified**.

**Procedure to root it:**
1. Replay the trigger: Facility (`-level_34`), kill Trevelyan → mission-fail
   fade-out, under `tools_pc/debug.ps1`.
2. When the log shows `D152: ... stealing from owner=...`, take the logged
   stale-owner address **and** the stealing caller's return address;
   `addr2line -e build-pc/ge007.x86_64.exe -f -C <both>` (image base
   `0x140000000`). The *owner* is the leak site.
3. Audit target: libaudio's `alEvtqPostEvent` / `alEvtqNextEvent` /
   `sndSetSfxSlotVolume` paths (`src/libultra/audio/event.c`, `src/snd.c`) —
   look for early returns between an `osSetIntMask(OS_IM_NONE)` and its
   matching `OS_IM_ALL` (the D147 assumption that "the decomp never blocks /
   never early-returns while masked" is falsified on this path). Also check
   transient threads that acquire-and-exit (`gdb.txt` showed New Thread /
   exited churn every few k frames).

**Proper narrow fix (once the site is known), pick one:**
- **(a) Preferred:** a dedicated `ALEventQueue` lock (mutex per queue or one
  global in the port layer) taken by `alEvtqPostEvent`/`alEvtqNextEvent`, so
  the audio event paths stop using the process-wide `osSetIntMask` section at
  all; then the self-healing steal becomes dead weight that can be reverted to
  the plain D147 recursive mutex.
- **(b) Cheaper:** make the leak site balanced (the actual bug), keep the
  self-healing lock as belt-and-braces.

**Verify:** replay the mission-fail 3×: no `D152:` steal log lines, fade-out
completes, screen returns to the mission-failed UI. `-level_09` framediff.

**Risk:** (a) is port-layer only if the lock lives in the shimmed
`alEvtq*` wrappers — but note `event.c` is *compiled game-side libultra*, so
the lock likely has to go inside `event.c` under `#ifdef PORT` (ABI-exception
class, document as Dxx). Keep the self-healing lock either way until audio
(Phase 3) lands.

### B3. D120 — blood-stain hang: extend `d43_emit.py` for opcode-0x18 — **DONE (M-30)**

**Status:** FIXED (`tools_pc/d43_emit.py`, commit `da98cdf1` + validation
`4a7ab609`). The `CollisionVertices` sub-array + its `.index` /
`CollisionRelatedNode` / `CollisionRelatedIndex` fields were already converted
(`op24_is_collision` path). The real gap was narrower than this note assumed:
**`PointUsage[]` was reserved in the layout pass (`add_region(puo, 2*nv)`) but
the emit pass never wrote it → all-zero → `chr.c`'s decal walk
`index = PointUsage[index]` cycled 0→0** (capped by the guard). Fix:
`op24_pointusage[puo] = nv` + a byteswap loop in the emit pass. Converter now
validates: 1469 regions / 70177 `s16` entries round-trip, every index
< numVertices, none all-zero. The `chr.c` guard is kept as a safety net.
**Verify:** BUNKER1 firefight — blood decals on shot guards should appear, no
hang. Regen chain must be re-run for the sidecar to update.

**Procedure:**
1. Byte-spec the record: dump one converted guard model's opcode-0x18 region
   from a sidecar (`data/pccg-*`) and diff against the raw ROM bytes; write
   out the N64 layout (fields, sub-array strides, terminators) in the
   converter comment — same method as the D69/D88 audits.
2. Extend `d43_emit.py`: emit the widened struct with the 6 pointers via
   `put_ptr`, and convert both sub-arrays (endianness + PC stride).
3. Keep the `chr.c` guard until verification passes, then decide whether to
   keep it as a safety net (recommended — same philosophy as D85's bounds
   check) or revert.
4. Regen full chain; verify: BUNKER1 (`-level_09`) guard firefight — blood
   decals now appear and no hang; framediff anchors.

**Risk:** medium — converter work with a runtime guard underneath, so a bad
conversion degrades to today's behavior rather than crashing. Good subagent
brief on its own (per HANDOFF-ARCHIVE M-9).

### B4. B3 — RMB aim too sensitive (`port/src/input.c`)

**Status:** user-reported (M-28 playtest). Port-only.

**Current math (aim mode, `input.c:~471`):**
`m = 61 + |dx| * (MouseAimSpeed/100) * AIM_GAIN(4.0)`, clamped to
`60 + AIM_BAND(20)` = 80. At the default `MouseAimSpeed=50`, **25 px/poll
saturates the band** — a fast flick is "max stick" for many polls, and GE's
aim curve `(stick-60)/10` then runs at full rate the whole time.

**Fix (in order of cheapness):**
1. Lower the default: `mouseAimSpeed = 50 → 25` (`input.c:~128`). One line;
   users can still dial it back up in `ge007.ini`.
2. Make the band itself tunable: register `Input.AimBand` (int, 5..40,
   default 20) and replace the `AIM_BAND` constant in the two clamp sites —
   a smaller band = finer control near the gate, a larger one = more headroom.
3. Optional curve change if it still feels bad: make the mapping into the
   band sub-linear (`m = 61 + AIM_BAND * sqrt(gx / saturating_px)`) so small
   movements get proportionally less stick — but only after 1+2 are
   playtested; don't stack changes.

**Verify:** playtest-only (no headless signal): aim at a guard in BUNKER1,
flick the mouse, confirm no overshoot orbiting the target; hipfire turn
(`MouseTurnSpeed`) is untouched by this.

**Risk:** trivial — port-only, config-backed, default-reversible.

### B5. B4 — menu screens need an absolute pointer mode (`port/src/input.c`)

**Status:** user-reported (M-28). Port-only design item; **no code yet**.

**Problem:** front-end / watch menus with the crosshair cursor behave like
an analog-stick game, not a PC pointer. Today all mouse input is *delta*
based (turn/look), and menu navigation goes through `joyGetStickX/Y` level
checks in `front.c` / `options.c` — synthetic stick deltas make menu cursors
crawl or slam.

**Design (port-only, no `src/` change):**
1. Add a **menu-pointer mode** to the input layer: when active, emit
   *absolute* stick values derived from the cursor position —
   `sx = (mouseX / windowW) * 2 - 1 → [-80..80]`, same for Y. GE's front-end
   maps stick deflection to on-screen cursor position, so absolute→absolute
   gives true pointer behavior with zero game-code changes.
2. **Mode detection is the open question** — the port doesn't know when the
   game is in a menu. Options, cheapest first:
   - **(a)** Heuristic: mouse is *ungrabbed* (cursor visible) ⇒ menu mode.
     Pair with a rule that front-end screens release the grab and gameplay
     grabs it (the grab state already flips on focus loss; extend it to be
     driven by a key, e.g. hold-to-release, or auto-detect via the same
     signals D145 used for ESC-as-B). Simple, no game-state peeking.
   - **(b)** Peek a game-side "am I in the front end" flag through an existing
     exported symbol (check `front.c` / `lv.c` globals) and branch on it in
     `inputPoll`. Read-only access to a global is arguably inside the
     documented exception class, but **record a Dxx** if used.
   - **(c)** Explicit user toggle (config key + hotkey). Ugly but zero risk.
3. While in menu mode: suppress aim/turn delta emission entirely; wheel and
   clicks map to A/B as today.

**Verify:** drive the mission-select / SELECT FILE screens with the mouse —
cursor should track 1:1, click selects; in-game aim unaffected.

**Risk:** low (port-only) under option (a)/(c); (b) needs a Dxx write-up.
Sequence *after* B4 so the gameplay feel is settled first.

### B6. D118d latent sibling — `front.c` menu-nav stick level checks

**Status:** latent, noted M-27. Same bug class as the fixed watch-menu
over-scroll (`options.c`, `GE_WATCH_STICK_FAST{UP,DOWN}` → 0 under PORT).

**What to do:** find the `joyGetStick*InRange` / raw `joyGetStickY < -0x46`
style *level* checks in `front.c` menu navigation (main menu, mode-select,
mission-select paging) and apply the same treatment: under `#ifdef PORT`
drop the stick fast-scroll term so keyboard/digital input goes through the
latched single-step path only. Keep N64 verbatim under `#else`.

**Verify:** keyboard W/S (and D-pad) paging on every front-end menu = one
step per press, no multi-skip; controller sticks still fast-scroll.

**Risk:** low — mechanical copy of an already-landed fix. Do it together with
B5 so the menu-input area is touched once.

### B7. D118a residual — hipfire pitch is digital, yaw is analog

**Status:** parked minor feel issue. The *full* fix needs a game-side hook;
the deferred design is:

An `#ifdef PORT` analog-aim hook in `bondview.c`/`bondview2.c` where GE reads
digital C-up/C-down for hipfire pitch — replace the digital read with the
analog stick-Y value (which our input layer can emit even in hipfire, since
stick-Y is "move" only *when the game consumes it as such*; validate that
emitting a small stick-Y does not also make Bond walk). This is the first
*behavioral* `src/` edit of the aim path — it exceeds pure ABI/layout; **write
it up with a confidence rating before implementing** (AGENTS.md #2: "stop and
write it up"), and gate it behind a config key defaulting to *current*
behavior.

**Cheaper alternative worth trying first:** raise `MOUSE_PITCH_THRESH`
granularity by emitting the hipfire pitch as *repeated C-button pulses with
dwell proportional to speed* — pure port-layer, approximates analog. Try this
before touching `src/`.

---

## C. Cosmetic / graphics — investigation plans (root cause not fully known)

These are parked below crash/level work (`GRAPHICS-BACKLOG.md`). Each entry is
the *next diagnostic step*, because the root cause is not yet pinned. Do not
treat them as ready-to-apply fixes.

### C1. D114/D116 — HUD text + ammo digits X-mirrored per glyph

**Hard rule (PORT-LEARNINGS §D2): do NOT re-run the static trace** (four
sessions did; every stage verified non-mirrored at the GL boundary) and do NOT
apply a global texrect S-swap (mirrors the whole screen).

**Next attempt — pick one, in order:**
1. **RenderDoc (or apitrace) capture of exactly one glyph's texrect draw.**
   The contradiction is "VBO verified non-mirrored but pixels mirrored" — a
   GPU-side capture resolves it in one shot: inspect the bound texture's
   upload bytes vs the `gDPLoadTextureBlock` source, and the final quad's UVs.
2. **Asymmetric-1-texel experiment:** temporarily force one HUD glyph's image
   to a 2×1 or 3×1 *asymmetric* pattern (e.g. left half white) in the port
   layer; if it renders right-half-white, the flip is between upload and
   sample (texture origin / `glPixelStorei(GL_UNPACK_ROW_LENGTH)` / Y-flip);
   if it renders correctly, the mirror is applied *per-glyph placement*
   (texrect S-range inversion in `textRenderGlyph`'s GBI — check the
   `gDPTile`/`gDPSetTile` uls/lrs signs for the HUD tiles specifically).

Suspects to check with the capture in hand: 4-bit CI tile upload stride vs
`G_IM_SIZ_4b` sampling, TLUT index endianness (wrong-but-symmetric TLUT would
not mirror — deprioritize), and the `uls/lrs` sign convention for *rect*
draws (D116 is a rect path; triangles were never suspected).

### C2. D75 — front-end 3D models misplaced / absent (logo, gun-barrel Bond, cast)

Triage per §F D75 — **distinguish (a) vs (b) before any fix:**
1. For the Nintendo logo and one cast model, determine which matrix path
   builds their transforms: `guRotate`/`guLookAt` (→ candidate (a), D73
   scope gap in `guint.h` `DVAL()`) or the `animInit` + embedded raw-offsets
   into `struct player` path (→ candidate (b), independent model/RW-pool bug,
   cf. D56/D100/D102 — all now-fixed struct-pun classes; a *new* instance of
   the class is the leading hypothesis).
2. Capture `GE_PCDUMP` frames across the logo transition and gun-barrel/cast
   segments to localize which frame first diverges.
3. Files: `src/libultra/gu/guint.h`, `src/game/model.c`, `bondview2.c`
   (animInit / modelSetScale), `port/fast3d/gfx_pc.cpp`.

**Note:** D148/D149 below are the same family — fixing D75's root cause will
likely take them with it. The D149 garbled geometry is also worth one check on
the *converter* side: the malformed sub-DL (`seg5+0x9ee4`, unresolved matrix
ptr + garbage opcodes) may be a `d43_emit.py` conversion defect rather than a
runtime transform bug — dump that sub-DL's raw ROM bytes vs the sidecar before
assuming it's runtime.

### C3. D76 — disclaimer screen only partially drawn

Likely the D68 image-table class, incomplete: some `sImageTableEntry`s the
disclaimer references are missed by `port/src/gimgfixup.c`'s bswap/shadow-sync.

**Procedure:**
1. Enumerate the image entries the disclaimer screen references (trace
   `textRender`/`display_image_at_position` calls in the legal-screen code, or
   log every `gDPLoadTextureBlock` source during PPM frames f20–f100).
2. For each, confirm it is covered by the D68 fixup (bswapped IMAGESEG w1 +
   synced compiled globalDL shadow). The "only the classification line + one
   line below draw" symptom = exactly the covered subset rendering.
3. Files: `port/src/gimgfixup.c`, `src/game/tex.c`, image-table consumers.

### C4. D148 / D149 — Dam exit cutscene absent; MISSION COMPLETE models garbled

Both are D75 family (see C2). Additional notes:
- **D148:** the missing piece is a *scripted* sequence — cinematic camera +
  Bond rappel anim. Once D75's model path is fixed, verify the cutscene
  trigger still fires (check whether the exit trigger even dispatches the
  cinematic on PC — it may be gated on a front-end state flag our flow skips;
  `GE_STARTMENU` tooling can crash-test the screen in isolation).
- **D149:** see the converter-side check in C2. D144/D146 already keep the
  game alive on the corrupt DL; the fix should make the model *correct*, then
  D146's "unknown opcode → end DL" guard can stay as a safety net.

### C5. D85 (residual) — room DL `texLoadFromGdl` decode

**Status:** OPEN but safety-netted; geometry renders, so this is below level
work. Remaining: decode the room-specific opcodes / CC-RM-LUT markers that
`texLoadFromGdl` still handles generically, and verify `csize_*_DL_binary`
sizing. Only worth touching if a level shows placeholder/garbage textures on
room geometry that can't be explained by B2 (Depot wrong-colors).

---

## D. Housekeeping / cleanup (no gameplay effect)

### D1. Stale §F index row — D132 says "proposed fix (not applied)"

The D132 fix **is applied** in the tree: `tools_pc/d88_propdefs.py`
(`PROPDEF_PC_BYTES[14/19]=32, [44]=40`; the `(14,19,38,44)` handler emits each
`Index{k}` into the low 4 B of the 8-aligned slot at PC `8+8*k`) and
`src/game/loadobjectmodel.c sizepropdef()` (LINK/SWITCH/LOCK_DOOR → 8 words,
SAFE_ITEM → 10). The §F index row still reads "proposed fix (not applied) —
see below" while the subsection says "APPLIED (M-20, commit pending)".
**Action:** update the index row to `resolved (M-20)` and confirm the M-20
commit landed (git-blame both files). Docs-only.

### D2. Stale BACKLOG technical-cleanup items

`docs/BACKLOG.md` "Technical cleanup" lists:
- ~~"Add a D24-implications bug class to PORT-LEARNINGS"~~ — **done M-19**
  (PORT-LEARNINGS §E, lines ~374).
- ~~"Watch item: `osYieldThread` = `Sleep(0)`"~~ — **done M-19** (§E, line
  ~407). Keep the watch item itself; it's still accurate (the one place we
  fake cooperative scheduling — first suspect for level-sweep hot-loop
  misbehavior under host load).
- "Remove TEMP D63 debug scaffolding" — **partially done M-19** (`libultra.c`
  + game-side probe blocks removed). Remaining: `port/fast3d/gfx_pc.cpp`
  trail code — the `/* TEMP D63 (strip before commit) */` block at ~2462, the
  `GE_D63`-gated dram-branch trace at ~2516-2531, and the unknown-opcode dump
  at ~2759-2765. All env-gated / inert; strip in one pass (keep the
  `cached = getenv("GE_D63")` pattern only if a probe is retained — recommend
  removing all of it).

**Action:** one docs+code cleanup commit: mark the two done items, strip the
gfx_pc.cpp D63 trail, rebuild + `-level_09` framediff.

### D3. `osYieldThread = Sleep(0)` watch item (no action — recorded)

When a flaky timing bug appears (especially Phase 3 audio underruns): reach
first for host thread priorities (`SetThreadPriority`: scheduler thread
time-critical, tick normal) and the deferred `GE_DETERM` mode (D117) — not
for kernel changes. See PORT-LEARNINGS §E.

---

## Status (updated M-30)

- **B3 / D120** — DONE (PointUsage emit + validation).
- **B4** — DONE (M-29 `0bd0ceec`).
- **B5** — DONE (M-30, front-end mouse-pointer mode). **B6** — investigated,
  the `front.c` stick checks are already edge-gated / toggle-guarded, not a
  real over-scroll (unlike the fixed options.c watch case). No-op.
- **RC4** (palette off-by-one, was in `C` / TEXTURE-GLITCH-ANALYSIS) —
  RETRACTED: the current RGBA5551 decode is correct.
- **A2 / D139** — likely closeable: M-27's Dam→Facility playthrough ran a
  real `lvlUnloadStageTextData` → `cleanupObjects` with no teardown crash
  after the fix. Confirm the commit landed, then close the §F row.
- **D2 (D63 scaffolding strip)** — the `port/fast3d/gfx_pc.cpp` trail is
  stripped (M-30 `d0789358`). The remaining `#if defined(PORT)` `GE_D63`
  probe blocks in ~10 `src/game/*.c` files are inert but **some sibling
  `getenv`/`static` lines nearby are NOT `#if defined(PORT)`-guarded** (e.g.
  `bg.c:2880` `d63bgprimarycount`) — a strip pass must verify N64-build
  safety first (Non-Negotiable #1). Low value; defer.

## Suggested order for what's left

1. **A1** (D154 wall-shoot GBI parser) — needs a firefight-into-a-wall
   playtest, then close/commit.
2. **B1** (D74 wrap-block) — the in-place fix boot-crashes; needs the
   hoist-out-of-vertex-loop rework + per-level visual check. Cosmetic, gate
   behind a config flag like RC2. Risky, low priority.
3. **B2** (Depot ceiling blue-speckle) — RC2 didn't touch it; needs `GE_DTEX`
   / RenderDoc on that specific surface. RC1 (wallet-Bond) is the D75
   front-end-model track.
4. **C1–C4** cosmetics — after in-level crashes / campaign playtest.
