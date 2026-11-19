# D454

<!-- from the old section-F/H table row -->
| D454 | **Knife slash crashed on Windows: `var_a0_2` in `gunTickHandState` was `u32`, truncating the exe-data address of `D_80034CA4/E0C` (PE image base 0x140000000) before `gunSample1PTransform` (ABI/layout exception, §A1).** Full `## D454` entry at file tail | FIXED (verified by maintainer live: knife slash no longer crashes). |

## D454 — knife slash crash: keyframe pointer held in `u32` (ABI/layout exception, 2026-09-30)

**Symptom.** Windows: equipping the knife and slashing crashed at once (EXCEPTION 0xc0000005, fault `0x40169764`, `Rcx=0x40169720`). Found by the read-side 64-bit census (`ABI-CENSUS-READSIDE.md`), confirmed by the maintainer.

**Cause.** `gunTickHandState` (`src/game/gunfire.c`) picks the keyframe table with `var_a0_2 = D_80034CA4;` (or `D_80034E0C`) and passes it to `gunSample1PTransform(Weapon1PTransformKeyframe *, ...)`. `var_a0_2` was declared `u32`, and the tables are `u32[]` in the exe's `.data`, which on Windows sits above 4 GiB (image base `0x140000000`, ASLR off, CMakeLists.txt ~305). The store truncated the address. Linux is unaffected (image base 0x20000000). Class: §A1 (pointer-width misread from 32→64 widening).

**Trap.** The declaration exists once per region arm (US, JP, EU). The first fix (`75559906`) patched only the EU arm and the US build still crashed with the identical fault; the disassembly showed `mov ecx,ecx` before the call. All three arms are now patched (`8ba7dbb7`). Lesson: a decomp-style function can repeat a local per region `#if` arm; grep the declaration in every arm and check the disassembly (`mov reg32,reg32` before a pointer argument).

**Fix.** `#ifdef PORT` declares `Weapon1PTransformKeyframe *var_a0_2` and casts the two table assignments; the `#else` arm is the original. No logic change.

**Verified.** Maintainer: knife slash no longer crashes (Windows, this build). Disassembly: no `mov ecx,ecx` before any `gunSample1PTransform` call. **Confidence: [H].**
