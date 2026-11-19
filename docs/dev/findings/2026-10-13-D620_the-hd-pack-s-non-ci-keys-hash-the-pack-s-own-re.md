# D620

<!-- from the old section-F/H table row -->
| D620 | **The HD pack's non-CI keys hash the pack's own re-encoded content — no vanilla/recompile byte domain produces them; the join must come from external signals (level folder + tile geometry)** — ROADMAP #100 rice-pack stage; full `## D620` entry at file tail | MEASURED 2026-10-07 — xdelta "plus" ROMs unusable (32 MB, 98 % diff, unparseable at imagelist offsets; prior recomp probes void); vanilla 953 non-CI keys → 0 tdb hits, pack 1491 `_all` keys → 0 vanilla hits, 541 pack palcrcs → 3 vanilla matches (HD palettes regenerated); joined-pair scale factors 4×–128× (dims join dead) but folder→level mapping holds (257 pairs / 21 folders); B2 rewritten, B4 narrowed (sprite-bank UI/Effects art is registered via texLoad) |

## D620: the HD pack's non-CI keys hash the *pack's own* re-encoded content — no vanilla/recompile byte domain produces them; the pack→texnum join must come from external signals (level folder + tile geometry). The recompile-side domain B2 named is actually measurable, and B2/B4 are refined, not overturned (ROADMAP #100, rice-pack stage)

**Setup.** D619 left B2 ("the non-CI hash domain is the recompile's bytes")
on an *unmeasured* recompile side. This session had the whole recompile
side local: the `GoldenEye-007-Plus` source tree (its `imagelist.u.csv`,
2698 rows, offset-aligned with vanilla), its image build docs
(`scripts/IMAGE_BUILD_SYSTEM.md`), the 1964 GEPD bundle ROMs, and the two
xdelta patches. Measure every remaining byte domain before accepting B2.

**Findings.**

1. **The xdelta "plus" ROMs are not usable as a measurement domain.**
   `xdelta3 -d` (v4 *decode* — the patches carry no xdelta3-applyable
   stream; only the output length is honoured) of
   `goldeneye_007_plus(.xdelta|_v2)` onto the 12 MB vanilla base yields a
   **32 MB** ROM differing from the base in 98 % of its first 12 MB —
   another layout/endian than any of the GEPD bundle's 12 MB ROMs
   (`007 - GoldenEye (USA).n64`, sha1 `7dd376e9…`, 95 % different from
   vanilla, same size). Image bytes at the (shared) `imagelist.u.csv`
   offsets in **all three** ROMs parse as no GE bitstream under any
   ctrl-offset × bit-order × zlib/non-zlib interpretation (brute-forced
   header scan: 0 valid fmt/nc/w/h). Every earlier "recomp byte" probe ran
   on the corrupt xdelta output — its "2283 undecodable" is void. The
   *correct* recompile-side byte set is inside the recompile tree:
   `assets/images/combined/combined.bin` / `assets/images/split/*.bin`
   (per its `IMAGE_BUILD_SYSTEM.md`, byte-perfect against the vanilla
   region: sha1 `044fca472bf6…`), i.e. for untouched images, identical to
   the vanilla ROM region — a domain D619 already measured (0 hits).
2. **The pack's keys hash pack content; no byte domain on our side can
   produce them.** Vanilla non-CI keys (953, from the verified
   `rice_nonci.py` pipeline — decoded pool bytes, n64-swapped when
   lod>0, rice_HQ CRC, `scratch/van_nonci_keys.csv`) hit **0** of the
   tdb's 2567 non-CI keys. The pack's own 1491 `_all` keys
   (I8×573 / IA8×389 / RGBA16×343 / I4×136 / RGBA32×41 / IA4×9) hit 0 of
   the 953. The 553 `ciByRGBA` entries carry palcrcs (541 non-`$`) that
   match a vanilla palCrc in only **3** cases → the pack **regenerates
   palettes**, so even the CI-side palette channel of its keys is
   pack-side. Non-CI/CI-by-RGBA keys are hashes of 1964tech's
   re-encoding of HD art; a join has to come from signals *outside* the
   pack: (a) level folder ↔ the level that draws the texnum, (b) tile
   geometry, (c) elimination among a level's texnums after the CI-joined
   siblings are removed.
3. **The scale factor is wild, so a dimension bijection is dead.** Over
   the 257 confirmed CI-joined pairs (251 keys, fan-out included) the
   pack/vanilla dim ratio is 4×–128×: 32× ×135, 64× ×83, 16× ×31, 128×
   ×6, 8× ×1, 4× ×1. But every joined pair sits in its *correct* level
   folder (257 across 21 folders, e.g. texnums 423/240/1193/1258 in
   `Areas/Mission 01 - Dam`) — the folder→level mapping is a working
   signal, and D619's "unmeasured level-scoped join" should be formulated
   as *folder → level's drawn-texnum set (per-level GDL census) +
   elimination + tile-geometry scoring*, not a dims match.
4. **B4 narrowed.** Image-ID sprite art (explosion/smoke/gunbarrel-style
   banks) loads through `texLoad`/`texLoadFromTextureNum`
   (`front.c:4964`, `image_bank.c:327`) → **already inside the registered
   texnum path** (D615). D619's "822 PNGs in UI/Hacks are outside the
   identity space" therefore applies only to (i) in-exe C-array art
   (rareware logo, font echardata) and (ii) `Hacks` (character-model
   swaps = model packs). Owed: one `GE_TEXNUMDUMP` pass over the
   title/menu + an effects-heavy level to confirm the sprite/UI texnums
   appear (cheap, closes (i) vs (ii) empirically).

**Decision.** B2 is rewritten: "pack keys hash pack content; an
external-signal join is required" (not "hash-domain mismatch to fix").
B4 narrowed per 4. Next experiments, in order: (1) `GE_TEXNUMDUMP` on
title/menu + effects-heavy level → registered-texnum confirmation;
(2) folder→level bijection from the 257 joined pairs + a per-level GDL
texnum census → measure the elimination ambiguity for the 481 `Areas` +
734 `Effects` PNGs before committing to the content-join machinery.
