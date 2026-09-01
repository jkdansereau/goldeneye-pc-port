#ifndef PORT_ROMDATA_H
#define PORT_ROMDATA_H

/*
 * ROM loading: reads the .z64 ROM from the data dir and exposes its segments.
 * Modelled on the PD port's port/include/romdata.h.
 *
 * The N64 build links assets in from the ROM at fixed addresses. The PC port
 * instead loads the ROM file and maps the game's data segments (code, data,
 * assets) into a heap-allocated image, then patches the segment table so the
 * game's IMAGESEG / asset references resolve into that image.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load the ROM for the configured ROMID from the data dir.
 * Returns 0 on success, non-zero on failure.
 */
int romdataInit(void);
void romdataDestroy(void);

/* Pointer to the loaded ROM image (start of the .z64). When the image is
 * mapped at the N64 cart base this is 0x10000000 itself. */
const u8 *romdataGetRom(void);
/* Size of the loaded ROM in bytes. */
u32       romdataGetRomSize(void);

/*
 * True if [addr, addr+size) is a valid cart address (0x10000000+) inside the
 * mapped image. The PI shims call this before servicing DMA.
 */
int romdataCartAddrValid(u32 addr, u32 size);

/*
 * D33 (docs/internals.md): the .z64 file stores structured multi-byte
 * fields in big-endian byte order (bit-packed streams are raw). Per-field
 * BE->LE transform of the animation data segment after romCopy() and before
 * expand_ani_table_entries(): u32 -> bswap32, u16 -> bswap16, u8 -> identity.
 *
 * blob/blobSize: the animation_data segment as copied into DRAM by
 * alloc_load_expand_ani_table(). tableA/tableB: the null-terminated offset
 * arrays (animation_table_ptrs1/2); an entry of 1 is the null sentinel and
 * is skipped. Transforms each 20-byte record header at blob+off and its
 * [bd, bs) descriptor array (6-byte ModelAnimBitField records).
 */
void romdataFixupAnimationData(u8 *blob, u32 blobSize,
                               const s32 *tableA, const s32 *tableB);

/*
 * D35 (docs/internals.md): the music sequence table segment ( RareALSeqBankFile )
 * stores its fields big-endian: +0 u16 seqCount, then 8-byte entries at +4:
 * u32 address offset, u16 uncompressed_len, u16 len. Per-field BE->LE
 * transform after romCopy(). Entries are decoded only as far as blobSize
 * allows (the header-only 0x10-byte copy decodes the header plus entry 0;
 * the caller re-copies fresh BE bytes before the full-size call).
 */
void romdataFixupMusicSeqTable(u8 *blob, u32 blobSize);

/*
 * D54 (docs/internals.md): decode a decompressed compact-sequence file's
 * big-endian ALCMidiHdr in place (16 x u32 trackOffset + u32 division;
 * see romdata.c). Call after decompressdata(), before alCSeqNew().
 */
void romdataFixupCseq(u8 *blob);

/*
 * D50 (docs/internals.md): decode a language bank's big-endian offset
 * table in place ([u32 offsets x N][text]; see romdata.c). blobSize is the
 * decompressed bank size (resource_lookup_data_array[].poolRemaining).
 */
void romdataFixupLangBank(u8 *blob, u32 blobSize);

/*
 * D50 (docs/internals.md): re-lay out a font segment (struct font:
 * kerning[169] + chars[94] + glyph pixel data) from the N64 ROM layout to
 * the PC C layout, shifting the pixel block below the expanded char array.
 * src/n64Size locate the N64 image (ROM segment); the caller allocates
 * romdataFontPcSize() bytes, romCopies n64Size of them, then calls
 * romdataFixupFont(). pixeldata fields are left as relative offsets —
 * load_font_tables()'s `pixeldata += base` loop promotes them.
 */
u32  romdataFontPcSize(const u8 *src, u32 n64Size);
void romdataFixupFont(u8 *blob, u32 n64Size);

/*
 * D37 (docs/internals.md): the libaudio SFX/instrument bank segments
 * (ALBankFile trees: ALBank -> ALInstrument -> ALSound -> ALWaveTable /
 * ALEnvelope / ALKeyMap / ALADPCMloop / ALADPCMBook) are serialized in ROM
 * as big-endian scalars with 4-byte packed table-relative offsets where the
 * C structs have pointers. On N64 (4-byte pointers, BE MIPS) the ROM layout
 * and the C layout coincide; on x86-64 they diverge both in stride and
 * endianness, so alBnkfNew() misparses the tree (bankCount read as 256,
 * wild pointer walks).
 *
 * The PC image is larger than the ROM segment (each 4-byte packed offset
 * becomes an 8-byte pointer slot), and structs are packed back-to-back in
 * ROM, so the tree cannot be converted in place: it is re-laid out into a
 * compact image where every sub-struct sits once, 8-byte aligned, and each
 * pointer slot stores the sub-struct's new offset from the image start
 * (zero-extended). alBnkfNew()/_bnkfPatch*() then rebase those offsets to
 * absolute pointers unmodified (ptr + (s32)file). Wavetable `base` fields
 * are offsets into the separate wavetable data segment and stay offsets;
 * _bnkfPatchWaveTable() rebases them with the `table` argument.
 *
 * romdataAudioBankPcSize(): size in bytes of the PC-native image for a
 * ROM-layout bank blob (src/srcSize), 16-aligned. The caller must allocate
 * at least this much (music.c grows the alHeapAlloc accordingly).
 *
 * romdataFixupAudioBank(): converts in place; blob must hold srcSize bytes
 * of pristine ROM layout and allocSize >= romdataAudioBankPcSize() bytes.
 * Call after romCopy() (which copies exactly srcSize bytes) and before
 * alBnkfNew().
 */
u32  romdataAudioBankPcSize(const u8 *src, u32 srcSize);
void romdataFixupAudioBank(u8 *blob, u32 srcSize, u32 allocSize);

/*
 * Map an N64 virtual address (0xA0000000 RDRAM space) to the corresponding
 * pointer in the loaded image. Used to resolve asset/segment references.
 */
const void *romdataMapVa(u32 va);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROMDATA_H */
