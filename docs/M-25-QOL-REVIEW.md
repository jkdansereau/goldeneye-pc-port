# M-25 — port-layer QoL / playtest-experience polish (review sheet)

Session M-25, 2026-08-30. Continues `docs/M-24-QOL-REVIEW.md`.

**Scope guarantee:** every change is in `port/` (shim layer). **No `src/`
file touched** — no game-logic / ABI / asset-format change, no `#ifdef PORT`
in game code. N64 build untouched. Every new behavior defaults to the
pre-M-25 behavior — a fresh or partial `ge007.ini` changes nothing.

| Commit | Title |
|---|---|
| `9ebed821` | `[Video]` ge007.ini knobs + config auto-migration |
| _(this)_ | `tools_pc/playtest.sh` — name lookup, `--list`, auto-addr2line |

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

## Still TODO (carried from M-24)

Key rebinding, in-render on-screen FPS/pos overlay, in-game options menu,
backward weapon cycle, the `bondview.c` analog-aim hook (D118a), PNG (not
PPM) screenshots (needs stb_image_write vendored into `port/`), window
size/pos persistence (needs a reliable save-on-exit hook — `configSave` is
currently only on the unreachable clean-shutdown path).
