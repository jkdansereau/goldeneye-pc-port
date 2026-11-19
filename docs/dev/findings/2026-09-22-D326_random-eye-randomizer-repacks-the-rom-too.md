# D326

<!-- from the old section-F/H table row -->
| D326 | **Random Eye randomizer repacks the ROM too — the hypothesised "in-place data-only" mod tier is empty in practice; the in-ROM file table is the single keystone for any mod support (2026-09-22).** — full `## D326` entry at file tail | CLOSED as a question — identical ROM size but 96.5% of bytes and 803/803 file slices differ (content preserved, shifted +7216/+7392/−229888); same `d43_emit` zlib failure. D325's cheap tier disproven. Two bounded scans failed to find the in-ROM `fileentry` table (likely inside the compressed code image); that recovery question gates everything. Backlogged (local notes §5, extends T3.6). |

## D326 — Random Eye randomizer repacks the ROM too: the hypothesised "in-place data-only" mod tier is empty in practice; the in-ROM file table is the single keystone for any mod support (2026-09-22)

### Why this was run

D325 closed with a hypothesis: mods that preserve every file's offset and size
would work almost for free, needing only a ROM-identity stamp on the sidecars.
The Random Eye randomizer (`Murks-Random-eye-zer-v1.1.xdelta`, 286 KB) was the
obvious candidate — a *setup* randomizer, no new content, and a patch 45× smaller
than Goldfinger 64's. This tests that hypothesis. **It fails.**

### Measurements (same isolated worktree method as D325)

- Patch applies cleanly (rc=0) to our vanilla NTSC-U ROM. Output size is
  **identical** — 12,582,912 bytes. Internal name still `GOLDENEYE`, country
  byte still `E`, boot segment `0x40..0x1000` byte-identical, CRC pair changed.
  Every surface signal says "small in-place edit".
- It is not. **96.5% of all ROM bytes differ** (12,144,703 of 12,582,912), and
  **803 of 803** usable `filelist.u.csv` slices differ — every single file,
  including fonts and the Rareware logo, which a randomizer has no reason to
  touch.
- The content is preserved and **shifted**: searching the modded ROM for
  vanilla file content finds it intact at a new offset —
  `jfont_dl` / `jfont_chardata` / `efont_chardata` / `animationtable_entries`
  all at **+7216**, `GwppksilZ` / `PICBMZ` at **+7392**, and
  `GwristdartZ` at **−229888** (genuinely rearranged, not merely displaced).
- Converter failure is the D325 signature again:
  `tools_pc/d43_emit.py:273` → `zlib.error: Error -3 while decompressing data:
  invalid stored block lengths`.

**Conclusion:** the community toolchain (GE Setup Editor lineage) *repacks* the
ROM. Even a content-neutral randomizer comes out with a rebuilt file layout. So
the D325 taxonomy's cheap tier — "in-place data-only" — has no known real
inhabitants, and the `manifest.csv` ROM-stamp fix, while still worth doing on
its own merits (swapping ROMs/regions today silently serves stale sidecars),
**does not by itself unlock any actual mod**.

### Revised verdict: one keystone, not a tiering problem

All ROM-mod support now reduces to a single research question: **can the file
table be recovered from an arbitrary modded ROM at runtime?** Our table is
compiled in (`src/game/ob.c:29` → `file_resource_table.inc.c` → linker symbols
in `port/src/romassets_<region>.s`), and the converters re-use the same vanilla
offsets (`tools_pc/d88_emit.py:96`, `scripts/filelist.u.csv`).

Two bounded scans failed to locate the table in either ROM:

1. 12-byte-stride runs of `{s32 index, char *filename, u8 *hw_address}`
   (`src/game/ob.h:16`) with ascending index and in-range ascending address —
   best candidate was a 40-entry run at `0x29e24c` present *identically in both
   vanilla and Goldfinger 64*, i.e. a heuristic false positive, not an ~800-entry
   table.
2. Known file offsets encoded as big-endian u32 in raw / `|0x10000000` /
   `|0xB0000000` form, over the first 200 `filelist.u.csv` entries: 20 raw hits
   across the whole ROM, 1 each for the other two encodings, **no dense cluster
   anywhere**.

**Answered in D327:** the table is provably not in the ROM in any plaintext
encoding (including the segment-relative `0x7F000000` form, 0 hits), so approach
(a) is ruled out — `file_resource_table.inc.c` is a decomp reconstruction, and
the retail table lives in the loaded/compressed code image this port never
unpacks. D327 replaces this open question with a viable route: content-addressed
offset remapping with gap inference over contiguously-packed files.

**Not fixable regardless:** ASM/code mods (ROM code is never executed here), and
mods that add levels (new stage-table entries are `src/game` logic — AGENTS.md
rule 2, not the ABI/layout exception).

**Status:** CLOSED as a question — D325's cheap tier disproven, keystone
identified and then **resolved in D327** (table-in-ROM ruled out; content-addressed
remapping specified). No code changed. Backlogged as §5 of the local
`docs/dev/notes/MODERN-PORT-FEATURES-BACKLOG.md` (extends T3.6), sized
large/multi-session and gated on the table-recovery question; correct sequencing
is after first public release. Cross-ref: D325 (Goldfinger 64 probe + the
taxonomy this revises), D43/D69/D88 (the converters carrying vanilla offsets),
D179 (missing-sidecar abort path).
