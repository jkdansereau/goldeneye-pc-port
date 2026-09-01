#ifndef PORT_PCMODELS_H
#define PORT_PCMODELS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* D50 (docs/internals.md): PC-layout model sidecars — Plan B (D48/D49).
 *
 * N64 ROM model files are 32-bit-pointer big-endian images the PC build
 * cannot read in place (D37/D43). tools_pc/d43_emit.py converts all of them
 * offline to PC-layout RZ sidecars:
 *   data/pcmodels-<region>/pcmodels.bin   single concatenated image
 *   data/pcmodels-<region>/manifest.csv   name,offset,size (decimal)
 *
 * romdataInit reserves romSize + pcmodelsReserveSize() at CART_BASE and
 * pcmodelsLoadSidecars() copies the sidecar image to [CART_BASE+romSize, ...).
 * pcmodelsPatchTable() — called once from load_object_fill_header, after
 * obInit() has run — redirects every model entry's hw_address into the
 * extension region and sets rom_size to the PC compressed size, so the
 * existing romCopy/decompressdata load path serves PC bytes unmodified.
 */

/* Bytes to reserve immediately after the ROM (0 = no sidecar image found;
 * the game boots but model loads will fail). romImg is the validated ROM
 * image; the country byte at +0x3E selects the region directory. */
uint32_t pcmodelsReserveSize(const uint8_t *romImg);

/* Copy the sidecar image to cartBase+romSize and parse the manifest.
 * Returns 1 on success. */
int      pcmodelsLoadSidecars(uintptr_t cartBase, uint32_t romSize);

/* One-shot patch of file_resource_table + resource_lookup_data_array for
 * every manifest row. Must run after obInit() (obInit derives rom_size from
 * adjacent hw_address deltas). No-op if the sidecar image is absent. */
void     pcmodelsPatchTable(void);

uint32_t pcmodelsTotalSize(void);

/* Cart address where the sidecar image lives (0 if absent). */
uintptr_t pcmodelsSidecarBase(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PCMODELS_H */
