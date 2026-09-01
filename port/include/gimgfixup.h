#ifndef GIMG_FIXUP_H
#define GIMG_FIXUP_H

#include <PR/ultratypes.h>

/* D68 (docs/internals.md): endianness fixup for the Globalimagetable
 * segment after texReset()'s romCopy(). The N64 CPU is big-endian, so every
 * u32 in the ROM copy is BE-encoded; PC consumers read them natively (LE).
 *
 * Two kinds of CPU-interpreted u32 live in the segment:
 *  - Gfx w1 words of IMAGESEG-marked G_SETTIMG commands (IMAGESEG(id) =
 *    0xABCD0000 | id, see bondconstants.h). texLoadFromDisplayList() scans
 *    for the AB CD marker bytes and texLoad() reads the word back as
 *    `*updateword & 0xffff` to get the image id.
 *  - sImageTableEntry.index (first u32 of each 12-byte entry) in the 32
 *    table arrays; read both as a raw word by texLoad() and as a struct
 *    field by texSelect()/explosion.c.
 *
 * Everything else in the segment is byte-level (Gfx opcodes, single-byte
 * table fields, raw pixel blocks referenced via 0x02xxxxxx segmented
 * addresses) and needs no transform.
 *
 * After the fixup the marker bytes sit at offsets 6..7 of each Gfx (LE
 * encoding of 0xABCDxxxx), so texLoadFromDisplayList() checks them there
 * under PORT.
 */
void gimgFixupGlobalimagetable(u8 *base);

/* Copy the IMAGESEG w1 words patched by texLoad() in the ROM copy back into
 * the compiled globalDL_0xNNN arrays (oddtextures.o), which explosion.c
 * executes via g_ExplosionDisplayLists[] on PC. The ROM copy and the
 * compiled arrays are byte-identical per D39, so command j of the ROM DL
 * maps to Gfx slot j of the compiled array (16-byte stride on PC). */
void gimgSyncCompiledGlobalDLs(u8 *base);

#endif
