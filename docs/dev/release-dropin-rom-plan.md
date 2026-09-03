# Release plan — "drop in your ROM and play"

Status: proposal / not yet scheduled. Owner: TBD.

## Goal

An end user downloads the GitHub release zip, unzips it, drops their
legally-owned GoldenEye 007 N64 ROM (`.z64`, big-endian) into a folder,
double-clicks the exe, and plays. No Python, no MSYS2, no manual
asset-extraction, no command line.

## Where we are today

The Windows bundle produced by `tools_pc/bundle-win.sh` (and uploaded by the
`windows-build` CI job) already contains only: the engine exe, its MinGW /
SDL2 / zlib DLLs, a README, and licenses. It deliberately ships **no** ROM and
**no** game assets. That part is fine.

The gap is everything the runtime needs *alongside* the ROM before a level
will render:

| Dependency | Produced today by | Consumed at runtime by |
|---|---|---|
| `port/src/romassets_<region>.s` — absolute cart-address symbols for every obseg/ramrom/music asset + `ge007.ld` segment markers | `python3 scripts/gen_romassets.py <u\|e\|j>`, compiled **into** the exe | linker; `&symbol` + `romCopy()` in game code |
| `data/pcmodels-<region>/{pcmodels.bin,manifest.csv}` (~1.3 MB) — every model file re-laid-out to PC struct layout, RZ-compressed | `python3 tools_pc/d43_emit.py <region>` | `port/src/pcmodels.c` (`pcmodelsReserveSize`/`LoadSidecars`/`PatchTable`) |
| `data/pccg-<region>/{pccg.bin,manifest.csv}` (~3.6 MB) — every stage `bg/*.seg` + `Tbg_*_stanZ` re-laid-out to PC layout | `python3 tools_pc/d69_emit.py <region>` | `port/src/pccg.c` |
| ROM present at `data/ge007.<region>.z64` (or `baserom.<r>.z64`) | user copies it | `port/src/romdata.c` maps it at `0x10000000` |

Without the two sidecar trees the game shows the intro logos and then crashes
in `modelPromoteNodeOffsetsToPointers` (finding D179).

The full decomp asset-extraction toolchain (`scripts/extract_baserom.u.sh`,
`tools/extractor`, MIPS binutils) is **not** on the runtime path and never
needs to reach an end user — it only regenerates the committed `assets/**`
`.inc.c` / `.bin` metadata that a plain `git clone` already has.

### Important facts established while writing this plan

1. **`gen_romassets.py` does not need the ROM.** It resolves every symbol for
   `u`, `e`, and `j` from committed files (`scripts/filelist.<r>.csv`,
   `assets/obseg/file_resource_table.inc.c`, `assets/ramrom/ramrom.s`,
   `assets/music/*.s`, `ge007.ld`). The ROM is only opened to size the
   trailing `images` segment, and there is already a CSV-derived fallback when
   it is absent. So all three `romassets_<r>.s` files can be generated in CI
   with no ROM. (`docs/building.md` claims PAL/JP need "region-specific
   prop/font offsets from that region's ROM" — the current generator code
   shows no such ROM read; treat that sentence as stale, but verify before
   relying on it — see Open questions.)

2. **`d43_emit.py` / `d69_emit.py` are pure-stdlib Python 3.** They read only
   the ROM plus committed repo files, and their output is a deterministic
   function of the ROM. Both carry extensive built-in round-trip validation
   (re-parse the emitted image, check every field against the N64 source) and
   exit non-zero on any mismatch. `d43_emit.py` also runs a battery of
   buffer-fit cross-checks.

3. **The engine is region-locked at compile time.** `REGION_DEFS` in
   `CMakeLists.txt` differ per region (`VERSION_US/EU/JP`, `BUGFIX_*`,
   `LEFTOVERDEBUG`, …) and are used in 60+ game files. One exe cannot serve
   all three ROMs; the release must ship three exes (or one launcher that
   dispatches to three).

4. **Data/config location is already sane.** `sysResolvePath()` maps `$S/` to
   `./data/` (next to CWD if present, else next to the exe). `ge007.ini`,
   `ge007.eep`, and the sidecar trees all live under `$S/`. First launch
   already writes `ge007.ini` there. The sidecar loaders already search
   `$S/`, `$E/`, and `./`.

5. **ROM identity checking already half-exists.** `port/src/romdata.c`
   validates magic / title / cart-ID / country byte. `tools_pc/romverify.c`
   goes further (recomputes the `n64cksum` 6102 checksum over
   `[0x1000,0x100000)` using `tools/n64cksum.c`). Canonical SHA-1s for all
   three regions are in the README requirements table.

## Recommended approach

**Two-part: (A) fold the ROM-independent piece into CI now, and (B) add a
native first-run converter to the engine so the sidecars are generated on the
user's machine from their own ROM.**

### Part A — ship all three regions, pre-generate all `romassets`

Small, low-risk, unblocks a "download three exes, pick the one for your ROM"
release even before Part B lands.

- CI `windows-build` becomes a 3-way matrix (`ntsc-final`, `pal-final`,
  `jpn-final`). Each leg runs `gen_romassets.py <u|e|j>` then `build-pc.sh`.
- `bundle-win.sh` collects all three exes (`ge007.ntsc-final.exe`,
  `ge007.pal-final.exe`, `ge007.jpn-final.exe`) into one bundle, with shared
  DLLs.
- Add a tiny **launcher** `ge007.exe` (≈150 lines C, its own CMake target):
  reads `data/*.z64` (and `baserom.*.z64`), checks the header country byte +
  SHA-1 against the three known-good hashes, then `CreateProcess`es the
  matching region exe. On no ROM / unknown ROM it shows a message box naming
  the accepted versions and hashes and the folder to drop the ROM in.
  - Alternative if a separate launcher is unwanted: make each region exe, on
    detecting a ROM whose country byte is for a different region, re-exec its
    sibling. Slightly hackier; avoids a fourth binary.

### Part B — native first-run sidecar converter

Port `d43_emit.py` and `d69_emit.py` to C as a new engine module
`port/src/pcconvert/` (files: `pcconvert.h`, `d43.c`, `d69.c`,
`pcconvert.c` orchestrator). On startup, `romdataInit()` (after it has the
mapped ROM and region) calls `pcconvertEnsureSidecars(region)`:

1. If `$S/pcmodels-<region>/pcmodels.bin` **and** `$S/pccg-<region>/pccg.bin`
   exist and their embedded format-version tag matches the build → return
   immediately (the common case after first run).
2. Otherwise show a "Preparing game data from your ROM (one-time, ~5 s)…"
   splash / log line, run the two converters, write the four output files
   into `$S/`, then continue boot. Cache a `pcconvert.stamp` with the ROM
   SHA-1 + converter version so a ROM swap or engine upgrade re-runs it.

The converters need, besides the ROM:

- `scripts/filelist.u.csv`
- `assets/obseg/file_resource_table.inc.c`
- `assets/**/ModelFileHeader.inc.c` (NS/NT per model)
- the stage `bg` / `stan` `.inc.c` metadata `d69` reads

These are committed text/CSV, a few hundred KB total. **Embed them into the
exe at build time** via a CMake codegen step (`file(READ ...)` →
`.byte`/`const char[]`), so the release stays "exe + DLLs" with nothing loose.
A build-file change is required here (new `add_custom_command` + generated
source) — out of scope for this doc, but noted as the one unavoidable
`CMakeLists.txt` edit.

Reuse: `zlib` is already linked (RZ = `0x11 0x72` + raw deflate — the exact
codec `pcmodels`/`pccg` already decompress). The engine already has robust
byte-swap helpers in `romdata.c`. The Python round-trip validators should be
ported too and run in debug builds / behind an env flag.

**Effort estimate:** `d43_emit.py` is ~1000 lines of intricate model-graph
simulation (LOD/SWITCH rewire, BSP splice, marker-expansion bounds, DFS
placement) + validation; `d69_emit.py` ~500 lines. Call it a 2–4 week
focused task with careful diffing of C output against the Python output
byte-for-byte on all three ROMs before trust. This is the single biggest
work item.

### Interim option (faster Part B, if timeline pressure)

Freeze the two emit scripts with **PyInstaller / Nuitka in the Windows CI
job** (the runner has Python) into one `ge007-convert.exe`, ship it in the
bundle, and have the engine `CreateProcess` it on first run when sidecars are
missing. Pure-stdlib scripts freeze into a single clean exe with no user
Python. Downsides: ~8–15 MB added to the bundle, Windows-only, and it is a
second binary to sign / trust. Recommend this **only** as a stopgap that buys
time for the real C port; it satisfies "no user-visible Python/MSYS2" but not
"single native binary" or non-Windows.

### Rejected options

- **Precompute and ship the sidecars.** They are ROM-derived copyrighted game
  data (`data/pcmodels-*`, `data/pccg-*` are gitignored precisely for this).
  Redistribution is not acceptable. Rejected.
- **Region-independent sidecars.** `d43`/`d69` output is a function of the
  ROM; there is no region-independent form to precompute, and even if there
  were it would still be ROM-derived data. Rejected.
- **Bundle CPython + the .py scripts loose.** Meets the letter of "no MSYS2"
  but not "no Python", ages badly, and is bigger than a frozen exe for no
  gain. Rejected in favour of the interim freeze if a stopgap is needed.

## CI / release changes

1. `windows-build` → matrix over the three ROMIDs; each leg generates its
   `romassets_<r>.s` and builds.
2. Commit all three `port/src/romassets_{u,e,j}.s` (or generate in each CI
   leg — committing is simpler and lets non-CI builds skip the generator).
   Add an `e`/`j` regen + `git diff --stat` check to the `validate` job
   mirroring the existing US one.
3. `bundle-win.sh`: bundle all three region exes + the launcher; keep the
   "no ROM / size ≤ 60 MB" guards (raise the limit only if the interim frozen
   converter is included).
4. `release` job: unchanged shape (draft pre-release); update
   `.github/release-notes.md` with the drop-in-ROM instructions + the
   requirements/hash table.
5. Add a CI check that runs `d43_emit.py --check-only` / a `d69` equivalent
   against a **CI-only** ROM if one can be provided via encrypted secret;
   otherwise this stays a manual pre-publish smoke test (the release job is
   already a human-gated draft).
6. Once Part B lands: a CI step that builds the C converter, runs it and the
   Python converter on the same ROM (secret), and `cmp`s the outputs.

## Code to be written

| File(s) | Rough scope | Notes |
|---|---|---|
| `port/src/launcher/ge007_launch.c` + CMake target | ~150 lines | ROM detect, SHA-1 match vs 3 known hashes, `CreateProcess` region exe, message-box on failure |
| `port/src/pcconvert/d43.c` | ~1000–1300 lines | C port of `tools_pc/d43_emit.py` incl. graph sim + validation |
| `port/src/pcconvert/d69.c` | ~500–700 lines | C port of `tools_pc/d69_emit.py` |
| `port/src/pcconvert/pcconvert.c` + `.h` | ~200 lines | orchestrator, stamp file, splash text, wired into `romdataInit()` |
| CMake codegen for embedded metadata | ~40 lines CMake + generated `.c` | embeds `filelist.u.csv`, `file_resource_table.inc.c`, `ModelFileHeader.inc.c`, bg/stan `.inc.c` |
| `tools_pc/bundle-win.sh`, `.github/workflows/ci.yml` | edits | matrix, 3 exes, launcher, notes |
| SHA-1 helper (or reuse a vendored small SHA-1) | ~100 lines | for launcher + `pcconvert.stamp`; `romverify.c`'s n64cksum path is an alternative identity check |

## ROM SHA-1 / region handling

- Accept a ROM at `data/ge007.<region>.z64`, `data/*.z64` (any name — sniff
  the header), or `baserom.{u,e,j}.z64`.
- Verify: N64 magic + "GOLDENEYE" + "GE" + country byte (already done), then
  SHA-1 against the README table. On a header-valid but hash-mismatched ROM
  (bad dump, byte-swapped `.n64`/`.v64`, ROM hack): warn but allow a
  `--skip-rom-check` / ini opt-out; byte-swapped images should be detected and
  the user told to supply a true big-endian `.z64`.
- Region is taken from the country byte (`E`→ntsc-final, `P`→pal-final,
  `J`→jpn-final), which already drives `pcmodelsRegionForCountry()`.

## Config / data location

No change needed. `$S/` = `./data/` (or `<exedir>/data/`). First run creates
`ge007.ini`; Part B additionally creates `pcmodels-<region>/`,
`pccg-<region>/`, and `pcconvert.stamp` there. Document that the user makes a
`data/` folder next to the exe and drops the ROM in it (the launcher can
offer to create it).

## Open questions

1. **Does PAL/JP `romassets` generation truly need nothing from the ROM?**
   `docs/building.md` says it needs region-specific offsets; the code says
   otherwise. Generate `e` and `j` in CI and diff against a
   locally-ROM-generated copy to confirm before committing them.
2. **Does `d43_emit.py` really use `scripts/filelist.u.csv` for all three
   regions?** It hard-codes `FILELIST = "scripts/filelist.u.csv"` even when
   `REGION` is `pal-final`/`jpn-final`. Either model file offsets are
   US-identical across regions (plausible — models live in obseg which may not
   shift) or PAL/JP model conversion is currently untested. Confirm before
   promising PAL/JP support in the release.
3. **Launcher vs self-re-exec vs three shortcuts** — pick one UX.
4. **Splash UI for the first-run convert** — reuse the SDL window / a simple
   progress line, or a pre-SDL Win32 dialog? ~5 s is short enough that a log
   line + busy cursor may suffice.
5. **Non-Windows.** Part B's C converter is portable; the launcher and
   bundling are Windows-only today. Out of scope but the C port keeps the
   door open.
6. **Signing.** Unsigned exes + a "reads your ROM" first-run step will trip
   SmartScreen. Not blocking, but worth a release-notes note.

## Suggested milestone ordering

1. Part A (3-region matrix + pre-generated `romassets` + launcher) — days.
2. Interim frozen `ge007-convert.exe` wired to first-run — days, optional.
3. Part B C port of `d43`/`d69` + embedded metadata + output diffing — weeks.
4. Drop the interim binary; single-binary drop-in experience complete.
