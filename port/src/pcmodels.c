/*
 * PC-layout model sidecars (D50 / Plan B, D48/D49).
 *
 * Serves the offline-converted model files produced by tools_pc/d43_emit.py:
 *   data/pcmodels-<region>/pcmodels.bin    single concatenated RZ image
 *   data/pcmodels-<region>/manifest.csv    name,offset,size (decimal)
 *
 * The ROM is mapped at CART_BASE (romdata.c); the sidecar image is placed in
 * a reservation extension at [CART_BASE+romSize, ...). romCopy() on PC is a
 * host memcpy gated only by romdataCartAddrValid(), so once every model
 * entry's hw_address points into the extension region and rom_size holds the
 * PC compressed size, the existing load_resource()/decompressdata() path
 * serves PC-layout bytes unmodified (and sets poolRemaining = D_PC).
 *
 * The table patch must run AFTER obInit(): obInit derives rom_size from
 * adjacent-entry hw_address deltas (ob.c), so patching earlier would be
 * overwritten. load_object_fill_header calls pcmodelsPatchTable() on every
 * model load; it is a one-shot no-op after the first.
 */

#include <stdlib.h>
#include <string.h>

/* D38: <stdlib.h>/<string.h> resolve to the decomp's N64 stubs via the
 * include path; declare the host functions this file uses. */
extern void *malloc(unsigned long long size);
extern int   snprintf(char *str, size_t maxsize, const char *format, ...);

#include "platform.h"
#include "system.h"
#include "fs.h"
#include "pcmodels.h"

/* Order matters: <ultra64.h> must finish first — its PR/ucode.h anchor pulls
 * in pc_protos.h, which itself includes game/ob.h. Including ob.h before
 * ultra64.h leaves FILELOADMETHOD undefined mid-parse (circular guard). */
#include <ultra64.h>       /* u8/u32/s32 + full PR chain */
#include "game/ob.h"       /* fileentry, resource_lookup_data_entry */

/* Defined in src/game/ob.c (the table is included there from
 * assets/obseg/file_resource_table.inc.c). Declared here rather than in ob.h
 * so the game header stays untouched. */
extern struct fileentry file_resource_table[];
extern resource_lookup_data_entry resource_lookup_data_array[];
extern s32 file_entry_max;

#define PCMODES_CART_BASE     0x10000000u
#define PCMODES_MAX_FILES     1024
#define PCMODES_NAME_LEN      64

struct pcmodelsRow {
    char     name[PCMODES_NAME_LEN];
    uint32_t offset;
    uint32_t size;
};

static uint32_t s_total = 0;
static int      s_loaded = 0;
static int      s_patched = 0;
static uintptr_t s_base = 0; /* cart address of the sidecar image */
static struct pcmodelsRow s_rows[PCMODES_MAX_FILES];
static int      s_rowCount = 0;
static char     s_binPath[1024] = "";
static char     s_manPath[1024] = "";

static const char *pcmodelsRegionForCountry(u8 country)
{
    switch (country) {
    case 'E': return "ntsc-final";
    case 'P': return "pal-final";
    case 'J': return "jpn-final";
    default:  return NULL;
    }
}

/* Try $S/, $E/, ./ for <rel>; on success copy the resolved path into out. */
static int pcmodelsFindDataFile(const char *rel, char *out, size_t outsz)
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

uint32_t pcmodelsReserveSize(const uint8_t *romImg)
{
    const char *region = pcmodelsRegionForCountry(romImg[0x3E]);
    char rel[256];

    if (!region)
        return 0;

    snprintf(rel, sizeof(rel), "pcmodels-%s/pcmodels.bin", region);
    if (!pcmodelsFindDataFile(rel, s_binPath, sizeof(s_binPath))) {
        sysLogPrintf(LOG_WARNING,
                     "pcmodels: %s not found — model loads will fail; run "
                     "\"python tools_pc/d43_emit.py\" (see docs/building.md)",
                     rel);
        return 0;
    }

    snprintf(rel, sizeof(rel), "pcmodels-%s/manifest.csv", region);
    if (!pcmodelsFindDataFile(rel, s_manPath, sizeof(s_manPath))) {
        sysLogPrintf(LOG_ERROR, "pcmodels: manifest.csv missing next to %s",
                     s_binPath);
        return 0;
    }

    FSFile *f = fsOpen(s_binPath, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "pcmodels: cannot open %s", s_binPath);
        return 0;
    }
    uint32_t size = (uint32_t)fsSize(f);
    fsClose(f);
    s_total = size;
    return size;
}

int pcmodelsLoadSidecars(uintptr_t cartBase, uint32_t romSize)
{
    FSFile *f;
    int ok;

    if (!s_total)
        return 0;

    f = fsOpen(s_binPath, "rb");
    if (!f)
        return 0;
    s_base = cartBase + romSize;
    ok = fsRead(f, (void *)s_base, (int32_t)s_total) == (int32_t)s_total;
    fsClose(f);
    if (!ok) {
        sysLogPrintf(LOG_ERROR, "pcmodels: short read of %s", s_binPath);
        return 0;
    }

    /* Parse the manifest (one-shot data for pcmodelsPatchTable). */
    f = fsOpen(s_manPath, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "pcmodels: cannot open %s", s_manPath);
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
        sysLogPrintf(LOG_ERROR, "pcmodels: empty manifest %s", s_manPath);
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
        if (s_rowCount >= PCMODES_MAX_FILES) {
            sysLogPrintf(LOG_ERROR, "pcmodels: manifest exceeds %d rows",
                         PCMODES_MAX_FILES);
            break;
        }
        struct pcmodelsRow *r = &s_rows[s_rowCount];
        snprintf(r->name, sizeof(r->name), "%s", line);
        r->offset = (uint32_t)strtol(c1 + 1, NULL, 10);
        r->size   = (uint32_t)strtol(c2 + 1, NULL, 10);
        s_rowCount++;
    }
    free(buf);

    if (!s_rowCount) {
        sysLogPrintf(LOG_ERROR, "pcmodels: no rows parsed from %s", s_manPath);
        return 0;
    }

    s_loaded = 1;
    sysLogPrintf(LOG_INFO, "pcmodels: %d sidecars (%u bytes) at 0x%llX",
                 s_rowCount, s_total, (unsigned long long)s_base);
    return 1;
}

void pcmodelsPatchTable(void)
{
    int i, t, hit;

    if (s_patched || !s_loaded)
        return;
    s_patched = 1;

    hit = 0;
    for (i = 0; i < s_rowCount; i++) {
        const struct pcmodelsRow *r = &s_rows[i];
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
                     "pcmodels: patched %d/%d manifest rows (table mismatch?)",
                     hit, s_rowCount);
    else
        sysLogPrintf(LOG_INFO, "pcmodels: table patched (%d model entries)",
                     hit);
}

uint32_t pcmodelsTotalSize(void)
{
    return s_loaded ? s_total : 0;
}

uintptr_t pcmodelsSidecarBase(void)
{
    return s_loaded ? (uintptr_t)s_base : 0;
}
