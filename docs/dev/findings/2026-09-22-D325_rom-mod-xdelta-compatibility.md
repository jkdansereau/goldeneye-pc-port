# D325

<!-- from the old section-F/H table row -->
| D325 | **ROM-mod (xdelta) compatibility: relocating/total-conversion mods cannot work while the file table is compiled in; in-place data-only mods are one sidecar-staleness fix away (Goldfinger 64 probe, 2026-09-22).** — full `## D325` entry at file tail | CLOSED as a question — mechanism measured (GF64 relocates 797/803 vanilla file slices; 12→24 MB expanded ROM; `d43_emit` dies on a vanilla-offset zlib slice). ASM mods never work; relocating mods need a runtime-parsed file table (rule-2 collision for added levels); in-place data-only mods need only a ROM-identity stamp in `manifest.csv` (`port/src/romconvert.c`). |

## D325 — ROM-mod (xdelta) compatibility: relocating/total-conversion mods cannot work while the file table is compiled in; in-place data-only mods are one sidecar-staleness fix away (Goldfinger 64 probe, 2026-09-22)

### Question

Can the port run classic GoldenEye ROM mods distributed as xdelta patches, and
is a `mods/` folder mechanism worth building? Probe target: **Goldfinger 64
v1.0** (12.8 MB xdelta, a total-conversion campaign), with the Random Eye
randomizer (286 KB xdelta) as the contrasting light-touch case.

### What the port actually does with the ROM

- `port/src/romdata.c` reads the `.z64` and memcpys it to the N64 cart address
  `0x10000000`; **all game data is read out of it at runtime**. `romSize` comes
  from `fsSize`, so ROM size is not fixed — an *expanded* ROM maps fine. The
  only header validation is the country byte at `0x3E` (`romHeaderValid`).
- ROM **code is never executed** — we run the compiled decomp C. Every ASM hack
  in a mod is inert by construction.
- **The binding constraint:** the file table is *compiled into our binary*, not
  read from the ROM. `src/game/ob.c:29` includes
  `assets/obseg/file_resource_table.inc.c`, whose `hw_address` fields are
  linker symbols resolved by `port/src/romassets_<region>.s` to **hardcoded
  vanilla ROM offsets**. The offline converters do the same at convert time:
  `tools_pc/d88_emit.py:96` reads that table, and `scripts/filelist.u.csv`
  carries 812 vanilla offset/size rows.
- Sidecars (`pccg-*/`, `pcmodels-*/`) are **presence-checked only**
  (`rcSidecarsPresent`, `port/src/romconvert.c`); `manifest.csv` is
  `name,offset,size` with **no ROM identity stamp**. Swapping the ROM therefore
  leaves stale converted data in place silently.

### Probe (isolated worktree off `main`; primary checkout untouched)

1. `xdelta3 -d` of `Goldfinger64_1_0.xdelta` against our vanilla NTSC-U ROM
   applied **cleanly** (rc=0). Output **25,165,824 bytes** — a 12 MB → 24 MB
   expanded ROM.
2. Header: magic `80371240` unchanged, internal name `GOLDENEYE` → `GOLDFINGER`,
   country byte still `E` (so `rcRegionForCountry` maps it to `ntsc-final` and
   `romHeaderValid` accepts it), CRC pair changed, boot segment
   `0x40..0x1000` byte-identical.
3. **Relocation measured:** slicing all 803 usable `filelist.u.csv`
   (offset, size) rows out of both ROMs and comparing — **797 of 803 differ**,
   6 identical. Total content replacement with a rebuilt file layout.
4. **Converter failure signature** (first hard stop, exactly as predicted):

   ```
   File "tools_pc/d43_emit.py", line 273, in process
       src = zlib.decompress(rom[addr:addr + size][2:], -15)
   zlib.error: Error -3 while decompressing data: invalid block type
   ```

   i.e. a hardcoded vanilla offset now lands on unrelated bytes. `d69`/`d88`
   and the runtime path would fail the same way; not run, and no build was
   attempted.

### Taxonomy + verdict

| Mod class | Verdict |
|---|---|
| **ASM / code hacks** | **Never.** ROM code is not executed here. |
| **Relocating / expanding / total conversions** (Goldfinger 64) | **Out of reach.** Every compiled `hw_address` and every `filelist.u.csv` row is wrong. Would require the file table (and, for added levels, the stage tables) to be **parsed from the ROM at runtime** instead of compiled from `file_resource_table.inc.c` + `romassets_*.s`. That is an architectural project, and the added-level half collides with AGENTS.md rule 2 (new stage-table entries are `src/game` logic, not an ABI/layout edit). Not a `mods/` folder feature. |
| **In-place data-only** (offsets and sizes preserved — e.g. a setup randomizer like Random Eye) | **Hypothesis — DISPROVEN by D326**, which found the Random Eye randomizer repacks the ROM as well (identical size, 803/803 slices shifted); no known mod actually inhabits this tier. The fix below is still worth doing on its own merits, but unlocks no mod by itself. Needs one fix: stamp ROM identity (size + hash) into `manifest.csv` and reconvert on mismatch, in `rcSidecarsPresent` / `port/src/romconvert.c`. Without it a swapped ROM keeps stale sidecars and mis-renders in a way that looks like corruption rather than like a stale cache. |

A bounded scan of both ROMs for an in-ROM 12-byte `fileentry` array
(`{s32 index, char *filename, u8 *hw_address}`, `src/game/ob.h:16`) found no
convincing ~800-entry candidate (only a 40-entry run at `0x29e24c`, present in
both ROMs, almost certainly a false positive of the heuristic). So **whether
the table is even recoverable from an arbitrary modded ROM is unresolved** and
is its own investigation — it is the gating question for the relocating-mod
refactor above.

### If a `mods/` loader is ever built

- xdelta3 is **GPLv2** — invoke it as a separate bundled executable (the
  existing `prepare-assets/ge007-convert` spawn pattern in
  `port/src/romconvert.c`) or implement VCDIFF (RFC 3284) decode; do not link
  it into this MIT tree.
- Never redistribute a patched ROM or a patch; user supplies both, same posture
  as the base ROM.
- Sidecar trees must be keyed per ROM hash, which is the same stamp fix as
  above.

**Status:** CLOSED as a question — mechanism established and measured; no code
changed. Follow-up (b) was run and **disproved the cheap tier — see D326**, which
also identifies the real keystone (recovering the file table from the ROM at
runtime) and carries the backlog pointer. Follow-up (a), the `manifest.csv`
ROM-stamp fix in `port/src/romconvert.c`, still stands on its own merits (a
genuine latent bug today for anyone swapping ROMs or regions). Cross-ref: D43/D69/D88 (the converters whose vanilla offsets
this exposes), D179 (missing-sidecar abort path).
