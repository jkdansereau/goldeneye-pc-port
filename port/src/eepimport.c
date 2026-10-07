/*
 * port/src/eepimport.c — D514: import an emulator (1964 / Project64) .eep
 * save on load, so copying a `GOLDENEYE-usa.eep` in as data/ge007.eep just
 * works (ROADMAP §5a; supersedes the manual tools_pc/eep_convert.py step).
 *
 * Why conversion is needed (D492): the layout is byte-identical on both
 * sides (2048 B; block 0 smallSave, five 96-byte save_data slots from
 * block 4), but this port stores a few multi-byte fields in little-endian
 * host order — per slot chksum1/chksum2 (@0/@4) and options (u16 @12), plus
 * block 0's checksum pair — while emulator saves store them big-endian.
 * The CRC (fileGenerateCRC, src/game/crc.c) hashes raw bytes, so the value
 * is endian-independent; an unconverted save fails validation and every
 * slot is wiped on first boot.
 *
 * Rules (mirror tools_pc/eep_convert.py exactly):
 *   - per region (block 0 + the five slots), convert only regions that
 *     validate in emulator format AND not in port format;
 *   - a region invalid in both is left alone — the game wipes it, as today;
 *   - a port-valid region is untouched (an existing port save therefore
 *     stays byte-unchanged and gets no backup);
 *   - per slot: swap options (it lies INSIDE the CRC range [8..96), so a
 *     plain word swap would still fail), then re-stamp chksum1/chksum2 with
 *     the game's own fileGenerateCRC, stored little-endian; block 0 has no
 *     multi-byte field in its CRC range, so its checksum pair is simply
 *     byte-swapped;
 *   - before writing anything: back up the original bytes to
 *     ge007.eep.emulator.bak (never overwriting an existing backup — .bak2,
 *     .bak3, ...); log one line. Nothing converted -> no write, no log.
 */

#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "file.h"   /* save_data, smallSave */
#include "system.h" /* sysLogPrintf */

extern void fileGenerateCRC(u8 *addressA, u8 *addressB, save_data *retval);

#define EEP_SIZE 2048u

static u32 eepBe32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/* Does region r (len bytes, checksum words at [0..8), CRC range [8..len))
 * validate with its stored words read in `bigEndian` order? */
static int eepRegionValid(const u8 *r, int len, int bigEndian)
{
    save_data calc = {0}; /* only chksum1/chksum2 are read back */
    fileGenerateCRC((u8 *)r + 8, (u8 *)r + len, &calc);
    u32 s1, s2;
    if (bigEndian) { s1 = eepBe32(r); s2 = eepBe32(r + 4); }
    else           { memcpy(&s1, r, 4); memcpy(&s2, r + 4, 4); }
    return s1 == (u32)calc.chksum1 && s2 == (u32)calc.chksum2;
}

/* Convert one region in place from big- to little-endian. */
static void eepRegionConvert(u8 *r, int len)
{
    if (len == 96) { /* save_data: options (u16 @12) is inside the CRC range */
        u8 t = r[12]; r[12] = r[13]; r[13] = t;
    }
    /* checksum pair -> little-endian (block 0: plain swap of both words) */
    for (int w = 0; w < 2; w++) {
        const u8 *s = r + 4 * w;
        u8 o[4] = { s[3], s[2], s[1], s[0] };
        memcpy(r + 4 * w, o, 4);
    }
    if (len == 96) { /* re-stamp AFTER the options swap */
        save_data calc = {0};
        fileGenerateCRC(r + 8, r + len, &calc);
        memcpy(r,     &calc.chksum1, 4);
        memcpy(r + 4, &calc.chksum2, 4);
    }
}

/* Convert an emulator-format ge007.eep at `path` in place. Returns the
 * number of regions converted (0 = nothing done: no file, wrong size, or a
 * save that already is port format / invalid everywhere). */
int geEepImportEmulatorSave(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    u8 orig[EEP_SIZE];
    size_t got = fread(orig, 1, EEP_SIZE, fp);
    fclose(fp);
    if (got != EEP_SIZE) return 0; /* not a save file; the loader handles it */

    u8 buf[EEP_SIZE];
    memcpy(buf, orig, sizeof(buf));

    int converted = 0;
    for (int i = 0; i < 6; i++) {
        const int len   = (i == 0) ? (int)sizeof(smallSave) : (int)sizeof(save_data);
        u8 *r           = buf + (i == 0 ? 0 : 32 + (i - 1) * (int)sizeof(save_data));
        if (eepRegionValid(r, len, 0)) continue; /* port format: untouched */
        if (!eepRegionValid(r, len, 1)) continue; /* invalid in both: game wipes it */
        eepRegionConvert(r, len);
        converted++;
    }
    if (converted == 0) return 0;

    /* Backup first, never overwriting an existing one (.bak2, .bak3, ...). */
    char backup[1024];
    for (int n = 1; n <= 99; n++) {
        if (n == 1)
            snprintf(backup, sizeof(backup), "%s.emulator.bak", path);
        else
            snprintf(backup, sizeof(backup), "%s.emulator.bak%d", path, n);
        FILE *t = fopen(backup, "rb");
        if (!t) break;
        fclose(t);
    }
    FILE *bf = fopen(backup, "wb");
    if (!bf) {
        sysLogPrintf(LOG_ERROR, "eep: cannot write backup %s; save left unconverted", backup);
        return 0;
    }
    fwrite(orig, 1, EEP_SIZE, bf);
    fclose(bf);

    fp = fopen(path, "wb");
    if (!fp) {
        sysLogPrintf(LOG_ERROR, "eep: cannot write %s; backup kept at %s", path, backup);
        return 0;
    }
    fwrite(buf, 1, EEP_SIZE, fp);
    fclose(fp);

    sysLogPrintf(LOG_INFO, "eep: imported emulator save (%d regions converted, backup %s)",
                 converted, backup);
    return converted;
}
