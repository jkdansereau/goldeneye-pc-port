# D644

<!-- from the old section-F/H table row -->
| D644 | **File-select royal crest drew on an opaque dark square (#146): IA4 decoded as I2:A2 instead of the RDP's I3:A1** — full `## D644` entry at file tail | FIXED 2026-10-12 (`import_texture_ia4`; 1964-confirmed symptom, maintainer-verified; Win sweep unchanged) |

## D644: File-select royal crest drew with an opaque dark square (#146): IA4 decoded as I2:A2 instead of I3:A1

**Symptom.** Each file-select folder's gold crest sat on an opaque dark noisy square; N64/1964 shows only the gold.

**Evidence.** `GE_D526` census at frame 1150 (crest region): the crest is four 65x65 tiles, `fmt=3 (IA) siz=0 (4b)`, 2-cycle, `oml=0x0c184340` (FORCE_BL, blend CLR_IN*A_IN + CLR_MEM*(1-A_IN)), `comb=0x00f38e4f020a2d12` (alpha = tex alpha x shade alpha); the blender path was already correct (`alpha=1`). The raw texture (`GE_TEXRAW`) holds only nibbles {0, 2, 4, 15}. Real RDP IA4 is I3:A1 (nibble = I<<1 | A): 15 = opaque white, 0/2/4 = transparent (A=0). `import_texture_ia4` (D266) decoded I2:A2, so nibble 2 -> alpha 170 and nibble 4 -> alpha 85 with black intensity: the dark square. D266's "hardware-correct" claim was never verified (its own census found zero IA4 draws at its target scene).

**Fix.** `port/fast3d/gfx_pc.cpp` `import_texture_ia4`: I3:A1 (intensity = nibble>>1 scaled by 255/7, alpha = nibble&1 ? 255 : 0). Removed the I2:A2 comment on `SCALE_2_8` (macro kept, unused). Before/after crops: `crest_before.png`, `crest_after.png`.

**Scope.** Every IA4 texture in the game (any level or UI). Needs the lead's sweep/verify.sh re-run; the old I2:A2 tree-strip motivation (D236/D265) never reproduced as an IA4 draw.

**Confidence.** High for cause and fix on the file-select crest (visually verified, gold intact). Medium on other IA4 users until the sweep.
