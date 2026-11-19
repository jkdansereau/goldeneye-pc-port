# D327

<!-- from the old section-F/H table row -->
| D327 | **Mod support, resolved plan: reading the file table out of the ROM is ruled out by measurement; content-addressed offset remapping (gap inference over contiguously-packed files) is the viable route (1964GEPD fork review, 2026-09-22).** — full `## D327` entry at file tail | RESOLVED — superseded by D328; approach (a) is NOT ruled out (these scans missed the 1172-compressed data segment). `hw_address` absent from the ROM in all 4 encodings tested (0 hits for the segment-relative form); `file_resource_table.inc.c` is a decomp reconstruction. Viable route: locate unchanged files by content, bound changed ones by neighbour gaps (`obInit` differencing proves contiguity). Verified primitive: GE's `0x7F000000` segment maps uncompressed from ROM `0x34b30`, uniquely signature-findable. 1964 fork is GPL-2.0 — facts only, no code reuse. |

## D327 — Mod support, resolved plan: reading the file table out of the ROM is ruled out by measurement; content-addressed offset remapping is the viable route (1964GEPD fork review, 2026-09-22)

### Why this was run

D326 left one gating unknown — "can the file table be recovered from a modded
ROM at runtime?" — with a guess that it lives inside the compressed code image.
A community fork, `1964GEPD-automatic-mod-compatibility`, advertises automatic
mod compatibility and was reviewed for a technique. This entry settles the
gating question with measurements and replaces the guess with a plan.

### What the 1964 fork actually does (not what we need)

Its change is in the emulator's TLB setup (`InitTLBOther`). 1964 had a
GoldenEye **speed hack** that direct-maps the `0x7F000000` game segment with the
ROM offset hardcoded (`0x90034b30`); mods relocate that segment, so the hack
broke. The fork replaces the hardcode with a scan for a unique 5-instruction
signature at the segment's head. Its own code comments note the game *"still
work[s] without hack"* — this is an optimization guard, not mod-loading
machinery.

**Why it transfers so poorly:** 1964 *executes the ROM's MIPS code*, so it never
needs a file table at all. Mod compatibility is nearly free for an emulator and
structural for us. There is exactly one transferable fact (next paragraph).

### Transferable fact, independently verified

GE maps its `0x7F000000` game segment **directly from ROM, uncompressed**, and
the segment head is uniquely identifiable. Re-derived here against our own
vanilla NTSC ROM by scanning for the instruction sequence
`lui $at,0x3f80` / `mtc1 $at,$f0` / `addiu $v0,$zero,-1` / `lui $at,hi` /
`sw $v0,lo($at)`: **exactly one hit, ROM offset `0x34b30`** — matching the
emulator's old hardcoded `0x90034b30`. This locates a *relocated* segment in a
modded ROM in one pass. Useful, but on its own it does not yield the file table.

### (a) Reading the table out of the ROM — **CORRECTED BY D328: NOT ruled out**

> **This section's conclusion is wrong.** Every scan below was run over
> *uncompressed* ROM bytes. GE's file table lives at offset `0x252c4` inside
> the 1172-compressed data segment that starts at ROM `0x21990`, with
> pointers in a `0x80020d90`-based space — one raw-inflate call away, using
> the decompressor `port/src/rzdecomp.c` already ships. D328 verifies it
> against our own ROM (681 exact offset+size matches vs `filelist.u.csv`, 0
> mismatches) and reads two real mods' own tables with no hardcoded offsets.
> The measurements below remain accurate as *negative results about raw ROM
> bytes*; the inference drawn from them does not hold.

`file_resource_table`'s `hw_address` values are not present in the vanilla ROM
in any plaintext encoding. Measured over the first 100 `filelist.u.csv` offsets:

| Encoding | Hits |
|---|---|
| raw u32 offset | 4 (incidental) |
| `\|0x80000000` | 1 |
| `\|0xB0000000` | 1 |
| segment-relative `0x7F000000 + off − 0x34b30` | **0** |

Plus two structural scans for a 12-byte `{s32 index, char *filename,
u8 *hw_address}` run (`src/game/ob.h:16`) — one requiring ascending index and
in-range ascending address, one additionally requiring `filename` to point at a
plausible ASCII string. No candidate in either ROM. (The 40-entry run at
`0x29e24c` reported in D325 appears identically in vanilla *and* Goldfinger 64,
confirming it as heuristic noise.)

The retail table does live in the compressed image — that much was right — but
"which this port never unpacks" was the error: the data segment is a single
raw-inflate away and `port/src/rzdecomp.c` already does exactly that inflate.
See **D328**, which reads the table out of it and validates it end to end.
(`file_resource_table.inc.c` is still a decomp reconstruction; its *ordering*
matches the retail table, which is what makes index-keyed extraction work.)

### (b) Content-addressed offset remapping — the viable route

`obInit` derives each file's size by differencing adjacent `hw_address` values
(`src/game/ob.c:126`), which means **files are packed contiguously in the ROM**.
That is the lever:

1. We know every vanilla file's exact bytes. For a repacked ROM, locate each
   *unchanged* file by content search → its new offset.
2. A file the mod actually **changed** cannot be found that way, but because the
   layout is contiguous it is **bounded by the gap between its located
   neighbours** — offset and size both fall out.
3. Output is a per-mod offset/size table that replaces the compiled
   `hw_address` values at runtime and feeds `d43`/`d69`/`d88` in place of
   `scripts/filelist.u.csv`.

One-time cost at the convert step, not a runtime cost. Verified plausible
against Random Eye, whose content is preserved and merely shifted
(+7216 / +7392, one file −229888; D326).

**Known limits:** a run of *adjacent* changed files cannot be subdivided by gap
inference; files a mod *adds* get no help; and mods that add levels still need
new stage-table entries in `src/game` (AGENTS.md rule 2, not the ABI/layout
exception). ASM/code mods remain impossible — ROM code is never executed here.

### License posture (important)

`1964GEPD-automatic-mod-compatibility` is **GPL-2.0**; this is an MIT tree. Do
not vendor it, copy code from it, or paste its functions — the same hygiene call
as the 2026-09-19 removal of the vendored Mouse Injector (`AGENTS.md`, `reference/mouse-injector/README.md`).
The segment-start offset and instruction signature recorded above are facts
about *Rare's* code and were re-derived directly from our own ROM and decomp;
any implementation must be written from our sources, with provenance noted as
"technique observed in a GPL-2.0 project, reimplemented independently". Better
still: the segment start is obtainable from our own link map, so the scan can be
skipped entirely.

**Status:** SUPERSEDED BY D328 — approach (a) is **not** ruled out; this entry's
scans missed the compressed data segment. Approach (b) (content-addressed
remapping) remains a valid fallback for a mod that relocates the data segment
beyond what the scan finds. Mod support is viable; only implementation effort
remains (large, correct sequencing
is after first public release). No code changed. Cross-ref: D325 (Goldfinger 64
probe + mod taxonomy), D326 (Random Eye repack — cheap tier disproven),
D43/D69/D88 (converters carrying vanilla offsets), D179 (missing-sidecar abort).
