# D455

<!-- from the old section-F/H table row -->
| D455 | **Binary-first census of exe-static addresses truncated to 32 bits (D454 class) (2026-09-30).** — full `## D455` entry at file tail | FIXED/CLEAN (no further REAL site beyond D454) |

## D455 — census: exe-static addresses narrowed to 32 bits (D454 class) (ABI/layout exception, 2026-09-30)

**Question.** D454 (knife slash) was an exe-`.data` address (Windows image base `0x140000000`) held in a `u32`. Are there other sites?

**Method (binary-first).** `objdump -d -M intel` of the built exe, then a forward data-flow scan (`scratch` script, not committed): from every `lea/mov r64,[rip+...]` address load (incl. `.refptr.<sym>`), follow the register and its `mov r64,r64` copies for N instructions (following unconditional `jmp`, dropping the register on any overwrite or call), and flag (a) `mov r32,r32` same-register zero-extend, (b) `mov r32b,r32a` narrowing copy, (c) `mov DWORD PTR [..],r32` store. N=14 and N=60.

**Falsifier (done before fixing).** Run on the PRE-fix binary (release/v0.4.1 @ 48908d18 has no D454 fix): the scan re-detects D454 as 4 hits in `gunTickHandState` (`.refptr.D_80034CA4` / `D_80034E0C`, all joining at `mov ecx,ecx` before `call gunSample1PTransform`; the load precedes a `jmp` join, so the scan has to follow jumps; a naive adjacent-instruction scan misses it). Whole-exe hit counts: 29 (N=14) / 39 (N=60). Rebuilt with the two D454 commits cherry-picked (temp branch, dropped): 25 / 35, 0 in `gunTickHandState`. The remaining hits were triaged one by one.

**Triage of the remaining hits.** None is a REAL exe-static narrowing.
- OFFSET/ROM (benign): `_*SegmentEnd/RomStart/Start` are `.set` absolute constants in `port/src/romassets_u.s` (ROM addresses, not exe addresses): image_bank.c:251, initanitable.c:281, textrelated.c:123/146, music.c:686/709, romdata.c:268/275/372. `(s32)&ANIM_DATA_*` (title.c, initactorpropstuff.c) are D34 offsets.
- VALUE loads misflagged by the scan (register reloaded via a path the scan does not model, or a float/int value read from the global): bg.c:3232, bondview2.c:6512, chrprop.c:511, front.c:3424 (2 hits), image.c:2375, options.c:2049/4140, player.c:632, propobj.c:5376, spectrum.c:725, frontoptions.c:867, libultra.c:1457, fr.c:458, chr.c D_80030988/D_80030994, gfx_run_dl.
- Comparison-only narrowing (both sides truncated identically): chraction.c:2320/2324, zlib.c:312-441. Benign.
- Already guarded under `PORT`: bg.c:4238/4392, bondview2.c:10449, initplayergaitobject.c:16, model.c:1538 (in the `#else` arm), ramromreplay.c:225, initactorpropstuff.c ANIM_PTR, title.c:568.
- Not in the PC build: tlb_manage.c:134 (excluded; not in CMakeLists.txt).
- UNSURE (not exe-static, stack address): indy_comms.c:35 `(u32)&size`; dead on PC (`indy_ready`).
- Sites in the four files owned by another agent (front.c, options.c, propobj.c, language.c): only the value-load false positives above; no real site found. `language.c` produced no hit.

**Limits.** Flow-limited (N instructions, no full CFG, no loads through memory: an address stored to a 64-bit slot then re-read as `u32` later is not seen); heap/stack pointers truncation is out of scope (heap is mapped low). Link-time narrowing in static initialisers cannot occur (the linker rejects a truncated reloc). **No code change**: the only real site was D454. **Confidence: [M]** that no other straight-line narrowing remains; the scan is a filter, not a proof.
