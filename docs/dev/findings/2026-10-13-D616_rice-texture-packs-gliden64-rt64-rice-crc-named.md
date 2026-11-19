# D616

## D616: Rice texture packs (GLideN64/RT64 "rice"-CRC-named PNGs) map onto the port's `NNNN.png` texpack format via a ROM-side dumper + pack importer — ROADMAP #100, rice-pack stage

**Status:** TOOLING LANDED 2026-10-06 (`tools_pc/texpack/rice_dumper.py` + `tools_pc/texpack/rice_pack_import.py`; feature/texpack worktree). No port C changes. Validated against the `GoldenEye-007-HD` pack (RT64/`ge007.tdb` ecosystem, `rt64.json` + 2046 `GOLDENEYE#…#…png`).

**The problem.** Rice packs ship one PNG per N64 texture named
`GOLDENEYE#<crc>#<fmt>#<siz>[#<palcrc>]{_ciByRGBA,_all}.png` where `<crc>`
is a "RiceCRC32" of the texture's base image **as it sits in RDRAM**. The
pack targets the 1964tech recompile, so its tdb/offset identifiers are
recompile-side and not portable to the vanilla texnums. The only portable
join key is the rice CRC itself — so the port side must be able to *produce*
the same identifiers from the vanilla ROM.

**Domain solved (after a long 0-hit phase — the LE variant is the trap):**

- The pack's CRCs are **NOT** the `RiceCRC32` you get by porting
  GLideN64's `TxUtil.cpp` or RT64's `texture_hasher.cpp` as-is: those
  read `*(uint32*)(src+x)` (little-endian on x86), and that variant
  produces **0 hits** against the pack on every byte-domain combination
  (swapped/raw × LE/BE × stride/bpl, 1744 zlib-CI textures).
- The matching variant is the **GlideHQ / PD-dabs-mod convention**
  (see `ge-port-reference/perfect-dark-dabs-mod/port/src/texpack.c`):
  4-bit rotate-carry CRC, **big-endian** 32-bit reads, and a per-row
  `wordHash` that carries into the row-end XOR
  (`wordHash = pos ^ BE32(row+pos); crc = wordHash + rol4(crc); …
  crc += wordHash ^ counter`).
- Hashed bytes = the base image exactly as RDRAM holds it:
  1. zlib-CI decode (`texInflateZlib`): ctrl/format/nc-1/nc×2-byte
     big-endian palette, then `w`,`h`, then rarezip (`11 72` header +
     **raw** deflate, `wbits=-15`).
  2. `texAlignIndices` layout: rows of `ceil(nper)` bytes padded to an
     **8-byte-aligned stride** (nper = w for 8-bit idx, `(w+1)/2` for
     4-bit).
  3. `texSwapAltRowBytes` **iff `ctrl & 0x3F` (lod) > 0** (odd rows,
     32-bit word pairs; row advance `alignedwidth*4` bytes = the pool
     stride). The recompile's copy has **no `#ifdef PORT` guard** — it
     swaps on PC too — and the pack was dumped from the recompile, so
     the swapped domain is the right one for lod>0 textures.
- The hash's `rowStride` = the **pool stride (8-aligned)**, not the
  RDP-tile `bpl` formula.
- ID fields: `fmt` = 2 for all colour-index textures; `siz` = 1 for
  8-bit indices (RGBA16_CI8/IA16_CI8, image formats 9/11), 0 for 4-bit
  (10/12). The 4th field (`palcrc`) is the same hash over the first
  `cimax+1` big-endian 16-bit palette entries — but it is **not needed
  for the join**: no two pack textures share an image CRC (verified
  against all 532 CI entries), so the 3-field key `crc#2#siz` is
  unambiguous.
- Odd-width CI4 (8 of 1744): `texAlignIndices` writes `ceil(w/ipb)`
  bytes per row (the C loop `for(x=0;x<width;x+=ipb)`), i.e. a
  partial last byte, not `w//2`.

**Result on `GoldenEye-007-HD`:** dumper emits 1744 vanilla zlib-CI
identifiers; importer joins 555 CI pack PNGs → **251 unique keys / 256
files** written (fan-out to identical-content texnums, e.g. 299/300).
The 304 unmatched pack CI textures have no vanilla-ROM texnum
(recompile-side additions); the extension path is joining the pack's
`ge007.tdb` offsets to the recompile's `imagelist.u.csv`
(`GoldenEye-007-Plus/imagelist.u.csv`) — out of stage-1 scope.
Spot checks: tex 4's image CRC **and** palcrc (`5f045394`) both equal
the pack's own PNG name; written files are valid PNGs (e.g. 0004.png
320×320).

**Non-CI pack textures** (fmt 0/3/4: IA/RGBA/VP, 4597 entries) are not
covered: their rice IDs come from a different byte domain (RDP-tile
bpl, paletteless formats) and from recompile assets — stage 2, with the
tdb/imagelist join. Model packs: separate stage (ROADMAP #100).

**Tools** (stdlib-only, no machine paths):
- `tools_pc/texpack/rice_dumper.py <rom> <imagelist.u.csv> <out.csv>` —
  per-texnum `texnum,crc,fmt,siz,palcrc,w,h`.
- `tools_pc/texpack/rice_pack_import.py <rice_index.csv> <packdir>
  <outdir> [packname]` — lays out `<outdir>/<texnum:04d>.png` for
  `port/src/texpack.c` (D615) to consume.

**In-engine A/B (2026-10-06, feature worktree):** the `GoldenEye-007-HD`
pack was imported through the D616 pipeline (256 files under
`data/texpacks/GoldenEye-007-HD/`; `data/` gitignored — pack PNGs are
game content and stay out of the tree), `Video.TexturePack` set in
`ge007.ini`, `-level_09` runs with `GE_QUITFRAME`: the log shows the
substitution firing for real (13 decodes: 0315/1024 at 2048×2048,
0893/0901/0915 at 1024², …) with a clean exit. `GE_PCDUMP` A/B
captures at the same frame stem (vanilla vs packed): identical
camera/HUD; the packed run shows the HD grain/detail on the
corridor's wall and floor surfaces, the vanilla run flat. (Launch
gotchas on a fresh worktree: `data/` needs the `pcmodels-`/`pccg-`
asset sidecars and `C:\msys64\mingw64\bin` on PATH for SDL2.dll —
known launch requirements, not pack issues.)
