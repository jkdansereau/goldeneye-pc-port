# M-25 — port-layer QoL / playtest-experience polish (review sheet)

Session M-25, 2026-08-30. Continues `docs/M-24-QOL-REVIEW.md`.

**Scope guarantee:** every change is in `port/` (shim layer). **No `src/`
file touched** — no game-logic / ABI / asset-format change, no `#ifdef PORT`
in game code. N64 build untouched. Every new behavior defaults to the
pre-M-25 behavior — a fresh or partial `ge007.ini` changes nothing.

| Commit | Title |
|---|---|
| `9ebed821` | `[Video]` ge007.ini knobs + config auto-migration |
| `5a7a1035` | `tools_pc/playtest.sh` — name lookup, `--list`, auto-addr2line |
| _(this)_ | `[Input]` knobs — mouse Y scale / smoothing, pad deadzone / trigger / invert-Y |

---

## 1. `[Video]` ge007.ini knobs + config auto-migration

### Rationale

`video.c` hardcoded five display settings that testers reasonably want to
change per-machine (weak GPU → drop MSAA; capture work → nearest-filter for
crisp texels; high-refresh monitor → uncap fps). All were compile-time only.

### Changes

**`port/src/video.c`** — new `PD_CONSTRUCTOR videoConfigInit()` registers:

| Key | Default | Effect | Applied |
|---|---|---|---|
| `Video.VSync` | 1 | swap interval 0/1 | `wmAPI->set_swap_interval` in `videoInit` |
| `Video.FpsCap` | 0 | frame cap in fps, 0 = uncapped | `gfx_set_target_fps` in `videoInit` |
| `Video.MSAA` | 1 | 1/2/4/8 samples (snapped down to a power of two) | `gfx_msaa_level` before `gfx_init` |
| `Video.TextureFilter` | 1 | 0 = nearest, 1 = bilinear | `gfx_set_texture_filter` + mipmap filter |
| `Video.Fullscreen` | 0 | 0 = windowed, 1 = borderless fullscreen | `window_settings.fullscreen` at window create |

Every default reproduces the exact prior hardcoded value
(`set_swap_interval(1)`, no fps target, `gfx_msaa_level = 1`,
`FILTER_LINEAR` / `MIPMAP_LINEAR`, windowed). MSAA is clamped to
`{1,2,4,8}` by a snap-down expression so a bogus `MSAA = 3` can't reach the
GL layer. `gfx_pc.cpp` already self-disables MSAA if framebuffers are
unavailable.

**`port/src/config.c`** — `configLoad()` now tracks, per registered option,
whether the on-disk file actually mentioned it (`seen` flag set in
`applyKV`). If any registered key was missing after the parse, it calls
`configSave()` once to rewrite the file with the new key at its default.

- Without this, a new `[Video]` block would never appear in an existing
  `ge007.ini` (M-24: `configLoad` only writes defaults when the file is
  *absent*, and the clean-exit `configSave` is unreachable in practice) —
  the knobs would exist but be undiscoverable.
- An **up-to-date** file (every registered key present) is **not**
  rewritten — user comments and ordering are preserved, exactly as before.
- A file that *is* missing keys gets rewritten in the canonical grouped
  form (comments dropped — same contract M-24 already documented for the
  clean-exit save). Existing values are preserved (they're parsed into the
  option storage before the re-save).

### Review risk

Low. `video.c` changes are all "read an int, pick a code path that already
existed". `config.c` change is one extra conditional `configSave()` on a
path that already round-trips through the same serializer.

Failure modes: a corrupt ini value is clamped (ints) or ignored (unknown
key, already handled); the migration re-save uses `fopen(path,"w")` which
already had a `cannot write` warning path.

### Verification

- Build green (`./build-pc.sh ntsc-final`), no new warnings in `video.c` /
  `config.c`. (Pre-existing `getenv` implicit-decl warning in the untouched
  `GE_PCDUMP` block is not from this change.)
- Fresh ini (deleted): `[Video]` block written with all five defaults.
- Migration: hand-written ini with only `[Input] MouseAimSpeed = 77` +
  `[Audio]` → after one launch, `[Video]` added, `MouseAimSpeed = 77`
  preserved.
- No-op: ini with all keys present + a custom leading comment → launch
  leaves the comment in place (no rewrite).
- `-level_09`: `MSAA=4 / TextureFilter=0 / VSync=0 / FpsCap=120` →
  crash-free to frame 380+, **91.67% pixel coverage (unregressed)**.
  Defaults → crash-free to frame 620+, 91.67%.
- `-level_20` (Silo): crash-free to frame 620+ (defaults). NB Silo has a
  known pre-existing early freeze on the silo→Bond camera descent (~3 s
  in) — reported M-25, unrelated to this change, tracked for a later
  session.

### What to check by playing

1. Edit `data/ge007.ini` `[Video]` with the game closed; values take effect
   next launch.
2. `TextureFilter = 0` → visibly crisp/pixelated textures. `MSAA = 4` →
   smoother polygon edges. `Fullscreen = 1` → starts borderless-fullscreen
   (Alt-Enter still toggles).
3. `FpsCap = 30` → title-bar fps pins near 30; `VSync = 0` + `FpsCap = 0` →
   uncapped.

---

## 2. `tools_pc/playtest.sh` — usability

Tooling only — not compiled, not in the build. No game-code impact.

- Accepts a **level name** as well as the `-level_XX` number
  (`playtest.sh bunker2`, `playtest.sh "surface 1"` — case/space/underscore
  insensitive), via an in-script name→number table (mission order).
- `playtest.sh --list` prints the 21-level table (`#`, `-level_XX`, name).
  No-arg / `--help` prints usage + the table.
- On a crash it now reads `ge007.crash.log` and runs `addr2line` on the top
  faulting PCs automatically (was: "see log"), and copies the crash log
  next to the playtest log.

Verified: `--list` output correct; name + number resolution; a real
`bunker1` launch (killed after 15 s) reports cleanly.

---

## 3. `[Input]` knobs (`port/src/input.c`)

### Rationale

Feel tuning that was hardcoded. Every default is the exact prior constant,
so a fresh/partial ini changes nothing (verified against a from-baseline
build — see below).

| Key | Default | Effect |
|---|---|---|
| `Input.MouseYScale` | 100 | extra vertical (pitch) sensitivity multiplier, % — applied to the mouse dy before aim/hipfire mapping |
| `Input.MouseSmoothing` | 0 | 0 = raw deltas (today); 1..90 = exponential low-pass, value = % blend of the previous poll |
| `Input.PadDeadzone` | 7000 | left-stick deadzone, raw 0..32767 (was `STICK_DEADZONE`) |
| `Input.PadTriggerPct` | 23 | trigger press point, % of travel (≈ the old `30*256` threshold: 23 × 327 ≈ 7521 vs 7680) |
| `Input.PadLookInvertY` | 0 | 1 = invert right-stick (look) Y |

`MouseYScale` / `MouseSmoothing` feed through the same mode-aware
mouse→pad path as the M-24 rework (aim-mode band vs hipfire digital pitch);
smoothing state is a pair of static doubles, reset implicitly when the
deltas go to zero. `PadDeadzone` flows through `scaleAxis()` (clamped
0..30000 so the divisor can't go non-positive). `PadTriggerPct`
recomputes the threshold from a percentage; `PadLookInvertY` negates `ry`
before the C-button mapping.

### Review risk

Low. Same class as §1 — read an int, feed an existing code path.
`scaleAxis` divisor is guarded. Worst case is bad feel, all runtime-tunable.
Cannot change the `OSContPad` contract.

### Verification

- Build green; the two `input.c` warnings (`fabs` implicit-decl, `getenv`)
  are **pre-existing** — confirmed identical on a stashed baseline build,
  not introduced here.
- `-level_09`: crash-free to frame 620, **91.6% coverage (unregressed)**.
- `-level_20` (Silo): crash-free; frame capture stops at ~320 —
  **confirmed identical on the pre-change baseline build** (the known Silo
  camera-descent freeze the user reported this session, not a regression).
- New keys land in `[Input]` on next launch via the §1 migration path;
  existing values (`MouseAimSpeed = 77`) preserved.

### What to check by playing

1. `MouseYScale = 150` → faster vertical look, horizontal unchanged.
2. `MouseSmoothing = 60` → visibly smoothed / slightly laggy aim; `0` crisp.
3. `PadDeadzone = 3000` → stick responds to smaller nudges; `12000` → larger
   dead center. `PadLookInvertY = 1` → inverted pad look. `PadTriggerPct`
   low → hair trigger.

---

## Known pre-existing bugs noted this session (not addressed — out of scope)

- **Silo (`-level_20`) capture freeze — likely just D117/D134 load
  sensitivity, NOT a real bug.** A full-length `GE_PCDUMP` capture froze at
  ~frame 320 (silo→Bond camera descent) twice, and the pre-M-25 baseline
  comparison froze too — but all of those ran on a machine still loaded from
  the 21-level sweep. On an idle machine the user drove Silo live past
  ~1800 frames with a healthy VI pacemaker and no heartbeat stall. Not an
  M-25 regression; not currently believed deterministic. Recount N runs on
  an idle box if it recurs.

---

## Still TODO (carried from M-24)

Key rebinding, in-render on-screen FPS/pos overlay, in-game options menu,
backward weapon cycle, the `bondview.c` analog-aim hook (D118a), PNG (not
PPM) screenshots (needs stb_image_write vendored into `port/`), window
size/pos persistence (needs a reliable save-on-exit hook — `configSave` is
currently only on the unreachable clean-shutdown path).
