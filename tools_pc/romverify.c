// romverify.c — one-shot ROM integrity check for the PC port.
//
// Verifies a .z64 against the repo's ground truths without modifying it:
//   1. size (must be >= 0x100000, the n64cksum coverage window)
//   2. GE header magic 80 37 12 40 at 0x00 (src/rom_header.s, port/src/romdata.c)
//   3. title "GOLDENEYE" at 0x20
//   4. country byte at 0x3E (E=US/LANG_US, P=EU, J=JP)
//   5. embedded checksums at 0x10/0x14 vs n64cksum_calc_6102() over [0x1000, 0x100000)
//      (tools/n64cksum.c — the exact routine the N64 build pipeline uses)
//
// Build (from tools_pc/): gcc -O2 -I../tools romverify.c ../tools/n64cksum.c
//                      ../tools/utils.c -o romverify.exe
// Usage: ./romverify.exe <file.z64>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n64cksum.h"
#include "utils.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.z64>\n", argv[0]);
        return 2;
    }

    unsigned char *rom;
    long len = read_file(argv[1], &rom);
    if (len < 0) {
        fprintf(stderr, "FAIL: cannot read %s\n", argv[1]);
        return 2;
    }

    int fail = 0;
#define CHECK(cond, ...) do { \
        printf("  [%s] ", (cond) ? "ok  " : "FAIL"); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        if (!(cond)) fail = 1; \
    } while (0)

    printf("ROM: %s (%ld bytes)\n", argv[1], len);

    CHECK(len >= 0x100000, "size >= 0x100000 (got 0x%lX)", (unsigned long)len);
    if (fail) { free(rom); return 1; }

    const uint8_t *h = rom;
    static const uint8_t magic[4] = { 0x80, 0x37, 0x12, 0x40 };
    CHECK(memcmp(h, magic, 4) == 0,
          "magic at 0x00 = %02x%02x%02x%02x (want 80371240)",
          h[0], h[1], h[2], h[3]);

    CHECK(memcmp(h + 0x20, "GOLDENEYE", 9) == 0, "title at 0x20 starts \"GOLDENEYE\"");

    char country = (char)h[0x3E];
    const char *cname = (country == 'E') ? "US (LANG_US)" :
                        (country == 'P') ? "EU (LANG_EU)" :
                        (country == 'J') ? "JP (LANG_JP)" : "?";
    printf("  [info] country byte at 0x3E = '%c' -> %s\n", country, cname);

    uint32_t stored1 = ((uint32_t)h[0x10] << 24) | ((uint32_t)h[0x11] << 16) |
                       ((uint32_t)h[0x12] << 8)  |  (uint32_t)h[0x13];
    uint32_t stored2 = ((uint32_t)h[0x14] << 24) | ((uint32_t)h[0x15] << 16) |
                       ((uint32_t)h[0x16] << 8)  |  (uint32_t)h[0x17];

    unsigned int calc[2];
    n64cksum_calc_6102(rom, calc);
    CHECK(calc[0] == stored1, "CRC1: header 0x%08X vs computed 0x%08X", stored1, calc[0]);
    CHECK(calc[1] == stored2, "CRC2: header 0x%08X vs computed 0x%08X", stored2, calc[1]);

    free(rom);
    printf(fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return fail;
}
