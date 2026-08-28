/*
 * PC-layout stage bg/*.seg + Tbg_*_stanZ sidecars (D69 / Plan B).
 *
 * Serves the offline-converted stage files produced by tools_pc/d69_emit.py:
 *   data/pccg-<region>/pccg.bin      single concatenated image
 *   data/pccg-<region>/manifest.csv  name,offset,size (decimal)
 *
 * Same shape as port/src/pcmodels.c -- see that file's header comment for
 * the general mechanism. The pccg image is placed right after the pcmodels
 * extension in the cart-base reservation (romdata.c), so once every bg/stan
 * entry's hw_address points into this region and rom_size holds the PC
 * size, the existing load path (obLoadBGFileBytesAtOffset / romCopy /
 * _fileNameLoadToBank / decompressdata) serves PC-layout bytes unmodified.
 *
 * The table patch must run AFTER obInit() (same reasoning as pcmodels:
 * obInit derives rom_size from adjacent-entry hw_address deltas). It is
 * called from the same load_object_fill_header hook as
 * pcmodelsPatchTable() -- a one-shot no-op after the first.
 */

#include <stdlib.h>
#include <string.h>

extern void *malloc(unsigned long long size);
extern int   snprintf(char *str, unsigned long long maxsize, const char *format, ...);

#include "platform.h"
#include "system.h"
#include "fs.h"
#include "pccg.h"

/* Order matters: <ultra64.h> must finish before bondtypes.h (see
 * pcmodels.c's comment -- same PR/ucode.h anchor requirement). */
#include <ultra64.h>
#include "game/ob.h"

extern struct fileentry file_resource_table[];
extern resource_lookup_data_entry resource_lookup_data_array[];
extern s32 file_entry_max;

#define PCCG_MAX_FILES     128
#define PCCG_NAME_LEN      64

struct pccgRow {
    char     name[PCCG_NAME_LEN];
    uint32_t offset;
    uint32_t size;
};

static uint32_t s_total = 0;
static int      s_loaded = 0;
static int      s_patched = 0;
static uintptr_t s_base = 0; /* cart address of the sidecar image */
static struct pccgRow s_rows[PCCG_MAX_FILES];
static int      s_rowCount = 0;
static char     s_binPath[1024] = "";
static char     s_manPath[1024] = "";

static const char *pccgRegionForCountry(u8 country)
{
    switch (country) {
    case 'E': return "ntsc-final";
    case 'P': return "pal-final";
    case 'J': return "jpn-final";
    default:  return NULL;
    }
}

static int pccgFindDataFile(const char *rel, char *out, size_t outsz)
{
    static const char *locs[3] = { "$S/", "$E/", "./" };
    int i;
    for (i = 0; i < 3; i++) {
        char pathbuf[1024];
        snprintf(pathbuf, sizeof(pathbuf), "%s%s", locs[i], rel);
        if (fsExists(sysResolvePath(pathbuf))) {
            snprintf(out, outsz, "%s", sysResolvePath(pathbuf));
            return 1;
        }
    }
    return 0;
}

uint32_t pccgReserveSize(const uint8_t *romImg)
{
    const char *region = pccgRegionForCountry(romImg[0x3E]);
    char rel[256];

    if (!region)
        return 0;

    snprintf(rel, sizeof(rel), "pccg-%s/pccg.bin", region);
    if (!pccgFindDataFile(rel, s_binPath, sizeof(s_binPath))) {
        sysLogPrintf(LOG_WARNING,
                     "pccg: %s not found -- stage loads will fault; run "
                     "\"python tools_pc/d69_emit.py\" (see docs/HANDOFF.md)",
                     rel);
        return 0;
    }

    snprintf(rel, sizeof(rel), "pccg-%s/manifest.csv", region);
    if (!pccgFindDataFile(rel, s_manPath, sizeof(s_manPath))) {
        sysLogPrintf(LOG_ERROR, "pccg: manifest.csv missing next to %s",
                     s_binPath);
        return 0;
    }

    FSFile *f = fsOpen(s_binPath, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "pccg: cannot open %s", s_binPath);
        return 0;
    }
    uint32_t size = (uint32_t)fsSize(f);
    fsClose(f);
    s_total = size;
    return size;
}

int pccgLoadSidecars(uintptr_t base)
{
    FSFile *f;
    int ok;

    if (!s_total)
        return 0;

    f = fsOpen(s_binPath, "rb");
    if (!f)
        return 0;
    s_base = base;
    ok = fsRead(f, (void *)s_base, (int32_t)s_total) == (int32_t)s_total;
    fsClose(f);
    if (!ok) {
        sysLogPrintf(LOG_ERROR, "pccg: short read of %s", s_binPath);
        return 0;
    }

    f = fsOpen(s_manPath, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "pccg: cannot open %s", s_manPath);
        return 0;
    }
    int32_t msize = fsSize(f);
    char *buf = (char *)malloc((unsigned long long)(msize > 0 ? msize + 1 : 1));
    if (!buf) {
        fsClose(f);
        return 0;
    }
    int32_t nread = fsRead(f, buf, msize);
    fsClose(f);
    if (nread <= 0) {
        free(buf);
        sysLogPrintf(LOG_ERROR, "pccg: empty manifest %s", s_manPath);
        return 0;
    }
    buf[nread] = 0;

    for (char *line = strtok(buf, "\r\n"); line;
         line = strtok(NULL, "\r\n")) {
        char *c1 = strchr(line, ',');
        char *c2;
        if (!c1 || !strcmp(line, "name,offset,size"))
            continue;
        *c1 = 0;
        c2 = strchr(c1 + 1, ',');
        if (!c2)
            continue;
        *c2 = 0;
        if (s_rowCount >= PCCG_MAX_FILES) {
            sysLogPrintf(LOG_ERROR, "pccg: manifest exceeds %d rows",
                         PCCG_MAX_FILES);
            break;
        }
        struct pccgRow *r = &s_rows[s_rowCount];
        snprintf(r->name, sizeof(r->name), "%s", line);
        r->offset = (uint32_t)strtol(c1 + 1, NULL, 10);
        r->size   = (uint32_t)strtol(c2 + 1, NULL, 10);
        s_rowCount++;
    }
    free(buf);

    if (!s_rowCount) {
        sysLogPrintf(LOG_ERROR, "pccg: no rows parsed from %s", s_manPath);
        return 0;
    }

    s_loaded = 1;
    sysLogPrintf(LOG_INFO, "pccg: %d sidecars (%u bytes) at 0x%llX",
                 s_rowCount, s_total, (unsigned long long)s_base);
    return 1;
}

void pccgPatchTable(void)
{
    int i, t, hit;

    if (s_patched || !s_loaded)
        return;
    s_patched = 1;

    hit = 0;
    for (i = 0; i < s_rowCount; i++) {
        const struct pccgRow *r = &s_rows[i];
        for (t = 1; t < file_entry_max; t++) {
            if (file_resource_table[t].filename &&
                strcmp(file_resource_table[t].filename, r->name) == 0) {
                file_resource_table[t].hw_address =
                    (u8 *)(s_base + r->offset);
                resource_lookup_data_array[t].rom_size = r->size;
                hit++;
                break;
            }
        }
    }
    if (hit != s_rowCount)
        sysLogPrintf(LOG_WARNING,
                     "pccg: patched %d/%d manifest rows (table mismatch?)",
                     hit, s_rowCount);
    else
        sysLogPrintf(LOG_INFO, "pccg: table patched (%d bg/stan entries)",
                     hit);
}

uint32_t pccgTotalSize(void)
{
    return s_loaded ? s_total : 0;
}

uintptr_t pccgSidecarBase(void)
{
    return s_loaded ? (uintptr_t)s_base : 0;
}
