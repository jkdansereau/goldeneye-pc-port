/*
 * PC-port replacement for src/game/decompress.c + src/game/zlib.c, which are
 * EXCLUDED from the build (see CMakeLists.txt and docs/PCPortResearch.md §F D32).
 *
 * GE's RZ files are: a 2-byte header (0x11 0x72) followed by a raw deflate
 * stream that ends with an end-of-block marker. The original decompressdata()
 * skipped the 2-byte header and ran a hand-rolled gzip-1.2.4 inflate whose
 * Huffman tables were built contiguously into a caller-supplied fixed buffer of
 * `struct huft`. That struct is 8 bytes on MIPS (the union holds a 32-bit
 * pointer) but 16 bytes on x86-64 (the union holds a 64-bit pointer), so on a
 * 64-bit host the tables overflow load_resource's `u8 buffer[0x2100]` and clobber
 * the caller's return address -> SIGSEGV in langInit()'s first file load.
 *
 * Backing decompressdata() with real zlib removes the fixed-buffer assumption
 * entirely: zlib allocates and manages its own table memory. This mirrors the
 * Perfect Dark port, which swaps its assembly rzip for a real-zlib-backed
 * rzip_c.c. The only externally-referenced symbols from the two excluded files
 * are decompressdata() and rzipGetSomething(); both are defined here.
 */

#include <ultra64.h>
#include "realzlib.h"   // host zlib by absolute path (CMake-generated)

struct huft;            // opaque; only used as a pointer parameter

/* Input position just past the last consumed compressed byte, set by the most
 * recent decompressdata(). image.c uses it to locate the next texture section. */
static u8 *s_rz_nextin = NULL;

u32 decompressdata(u8 *src, u8 *dst, struct huft *huffman_table)
{
    (void)huffman_table;   // real zlib allocates its own tables; arg ignored

    z_stream strm = {0};

    /* Raw deflate: windowBits=-15 tells zlib to expect no zlib/gzip wrapper. */
    if (inflateInit2(&strm, -15) != Z_OK) {
        return 0;
    }

    /* Skip the 2-byte RZ header; the deflate stream starts at src+2. The
     * compressed size is not in the header, so bound avail_in generously —
     * inflate stops at the end-of-block marker regardless, and src points into
     * mapped DRAM (the mempool), so a short over-read is harmless and never
     * reached for valid data. */
    strm.next_in  = src + 2;
    strm.avail_in = 0x400000;   /* 4 MiB ceiling, far above any single RZ file */

    u32 produced = 0;
    int ret;
    do {
        strm.next_out  = dst + produced;
        strm.avail_out = 0x400000;   /* caller sized dst for the full output (as on N64) */
        ret = inflate(&strm, Z_FINISH);
        produced = strm.total_out;
    } while (ret == Z_OK);

    s_rz_nextin = strm.next_in;   /* just past the compressed data */
    inflateEnd(&strm);
    return produced;
}

s32 rzipGetSomething(void)
{
    return (s32)(uintptr_t)s_rz_nextin;
}
