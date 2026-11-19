# D621

<!-- from the old section-F/H table row -->
| D621 | **The PD dabs-mod texture-pack notes mine: the HD route stays texnum-keyed — the PD port's key layer transfers nothing (its keys hash ROM bytes we hold; this pack's hash 1964tech's build, D620), but its format layer is a ready fix list; end-to-end plan lands** — ROADMAP #100; full `## D621` entry at file tail, plan in `docs/dev/TEXPACK-PLAN.md` | PLAN LANDED 2026-10-07 — Phase A (pad contract / alpha repair / F10 row / stall / manifest / merge), Phase B (B0 sprite-bank GE_TEXNUMDUMP confirmation → B1 folder→level → B2 per-level GDL census → B3 ambiguity gate → B4 content scorer → B5 review viewer), Phase C (join output as NNNN.png + per-level A/B); next task B0 |

## D621: the PD dabs-mod texture-pack notes mine — the HD-pack route stays texnum-keyed; the PD port's *key* layer transfers nothing (its keys hash ROM bytes we hold; this pack's keys hash 1964tech's build, D620), but its *format* layer is a ready fix list; plan lands

**Setup.** "How did the PD PC port get HD texture support, and why can't we
adapt that?" — answered by reading the dabs-mod tree's
`CLAUDE-notes/texture-packs.md` in full (the PD PC port fork that ships
the PD Plus HD packs, 3395 images).

**Findings.**

1. **The PD port's HD support is the *same architecture* we have.**
   `texpack.c` indexes `texture-packs/<name>/NNNN.png` by the port's own
   texture number, substitutes at `import_texture()`, decodes off the
   render thread, authors from an in-game F7 dump. The PD Plus HD pack
   was authored **natively** that way — no key join exists to solve.
   Our D615 system is this system.
2. **The PD port's *emulator-pack* layer is a key-domain trick, and it
   transfers nothing to our pack.** For Rice/GLideN64/1964tech
   artifacts it **recomputes the Rice checksums itself, over the ROM
   bytes it holds** (glyphs: 32 rows × 16 bytes at the 8-byte stride,
   "exactly one variant matched"), then matches pack records
   (`texpackIndexHtc`, `.htc`/`.dat` ingest). That works because PD's
   asset pipeline reads the stock ROM at startup — the pack's key domain
   *is* the ROM's domain. This HD pack's keys hash 1964tech's
   re-encoded build (D620): there is no byte stream of ours to
   recompute from. Same texnum architecture, different key domain —
   the plan keeps the texnum route and drops the key route.
3. **The *format* layer is a ready-made fix list for our importer**
   (adopted as Phase A of `docs/dev/TEXPACK-PLAN.md`): `gfx_pad_replacement()`
   (pack image = tile, not the padded 8-byte row — 278/3395 of the PD
   pack were "stretched" by exactly this); `bottomup.txt`/`ext_tex`
   row-order markers (rice packs are N64-row-order, must not flip);
   `gfx_replacement_alpha()` (opaque replacement standing in for an
   alpha texture gets repaired alpha — the PD notes describe our exact
   I8 failure: light beams "a solid grey sheet, every smoke puff a
   square"); F8/F9/F10 pack keys; worker-thread decode + kept store.
   Our decision: bake pad/alpha into the **authoring tools**
   (`rice_pack_import.py` emits padded, right-way-up, alpha-repaired
   `NNNN.png`) so the port stays format-agnostic; keep the
   in-port fallback for hand-made packs.

**Decision.** Plan: `docs/dev/TEXPACK-PLAN.md` — Phase A (make the
texnum path correct/shippable: pad contract, alpha repair, F10 row,
stall, manifest, merge), Phase B (measure the geometric join: B0
sprite-bank `GE_TEXNUMDUMP` confirmation, B1 folder→level table, B2
per-level GDL census, B3 ambiguity gate, B4 content scorer, B5 review
viewer), Phase C (land the join as `NNNN.png` + per-level A/B). Next
task: B0.
