#ifndef PORT_PCCG_H
#define PORT_PCCG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* D69 (docs/PCPortResearch.md): PC-layout stage bg/*.seg + Tbg_*_stanZ
 * sidecars -- same Plan-B pattern as D50/pcmodels.c.
 *
 * N64 bg/*.seg and Tbg_*_stanZ files are big-endian images with internal
 * self-relative 0x0Fxxxxxx offsets that the PC build cannot read in place
 * (D78-D81). tools_pc/d69_emit.py converts all of them offline to PC-layout
 * sidecars:
 *   data/pccg-<region>/pccg.bin      single concatenated image
 *   data/pccg-<region>/manifest.csv  name,offset,size (decimal)
 *
 * romdataInit reserves romSize + pcmodelsReserveSize() + pccgReserveSize()
 * at CART_BASE and pccgLoadSidecars() copies the sidecar image to
 * [CART_BASE+romSize+pcmodelsTotalSize(), ...) (right after the pcmodels
 * extension). pccgPatchTable() -- called once from the same
 * load_object_fill_header hook as pcmodelsPatchTable(), after obInit() has
 * run -- redirects every bg/stan entry's hw_address into the extension
 * region and sets rom_size to the PC (possibly re-compressed) size, so the
 * existing obLoadBGFileBytesAtOffset/_fileNameLoadToBank/decompressdata load
 * paths serve PC bytes unmodified.
 */

/* Bytes to reserve immediately after the pcmodels extension (0 = no sidecar
 * image found; the game boots but stage loads will fault as before D69).
 * romImg is the validated ROM image; the country byte at +0x3E selects the
 * region directory. */
uint32_t pccgReserveSize(const uint8_t *romImg);

/* Copy the sidecar image to base+... and parse the manifest. `base` is the
 * cart address immediately after the pcmodels extension (i.e.
 * cartBase + romSize + pcmodelsTotalSize()). Returns 1 on success. */
int      pccgLoadSidecars(uintptr_t base);

/* One-shot patch of file_resource_table + resource_lookup_data_array for
 * every manifest row. Must run after obInit(). No-op if the sidecar image
 * is absent. */
void     pccgPatchTable(void);

uint32_t pccgTotalSize(void);

/* Cart address where the sidecar image lives (0 if absent). */
uintptr_t pccgSidecarBase(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PCCG_H */
