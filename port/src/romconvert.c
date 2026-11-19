/*
 * romconvert.c — first-run sidecar conversion ("drop in your ROM and play").
 *
 * The engine needs two ROM-derived sidecar trees that are NOT shipped with a
 * release (they are copyrighted game data derived from the user's own ROM):
 *
 *     $S/pcmodels-<region>/   (pcmodels.bin + manifest.csv)  — port/src/pcmodels.c
 *     $S/pccg-<region>/       (pccg.bin    + manifest.csv)  — port/src/pccg.c
 *
 * The conversion is the in-process C converter (port/convert/, a port of the
 * old tools_pc/d43/d69/d88_emit.py scripts): when the sidecars are missing at
 * boot, romdataInit() calls romConvertEnsureSidecars(), which converts the ROM
 * into <exedir>/data/ and re-checks. The conversion is deterministic and takes
 * a few seconds; after the first run the sidecars exist and this path is a
 * no-op (two fsExists). On Switch the output lands next to the NRO
 * (sdmc:/switch/<app>/data/), since libnx starts with that as the CWD.
 *
 * Returns 1 when both sidecars are present (already or after converting),
 * -1 when they could not be produced (caller should abort boot with a
 * message rather than crash later in modelPromoteNodeOffsetsToPointers, D179).
 */

#include <stdlib.h>
#include <string.h>

/* D38: <stdio.h>/<stdlib.h> resolve to the decomp's N64 stubs via the include
 * path; declare the host functions this file uses. */
extern void *malloc(unsigned long long size);
extern int snprintf(char *str, size_t maxsize, const char *format, ...);

#include "platform.h"

#include "system.h"
#include "fs.h"

/* Same country→region map as pcmodels.c / pccg.c (finding: plan Q3). */
static const char *rcRegionForCountry(unsigned char country)
{
    switch (country) {
    case 'E': return "ntsc-final";
    case 'P': return "pal-final";
    case 'J': return "jpn-final";
    default:  return NULL;
    }
}

/* sysResolvePath() returns a static buffer — copy before the next call. */
static int rcExistsResolved(const char *rel)
{
    const char *p = sysResolvePath(rel);
    return fsExists(p);
}

static int rcSidecarsPresent(const char *region)
{
    char rel[256];
    snprintf(rel, sizeof(rel), "$S/pcmodels-%s/pcmodels.bin", region);
    if (!rcExistsResolved(rel))
        return 0;
    snprintf(rel, sizeof(rel), "$S/pccg-%s/pccg.bin", region);
    return rcExistsResolved(rel);
}

/* port/convert/conv_main.c (CV_STEP_ALL == 7). Declared here, not via
 * convert.h: this TU sees the decomp include path. */
extern int cv_convert_rom(const char *rom_path, const char *region, const char *out_root, int steps);
static int rcConvert(const char *rom, const char *out) { return cv_convert_rom(rom, NULL, out, 7); }

int romConvertEnsureSidecars(const unsigned char *romImg, const char *romRelPath)
{
    const char *region = rcRegionForCountry(romImg[0x3E]);
    if (!region)
        return 1;   /* unknown country — romHeaderValid() rejects it anyway */

    if (rcSidecarsPresent(region))
        return 1;

    /* Resolve the ROM path we were given ("$S/...", "$E/..." or "./...") to
     * an absolute form for the child's argv. */
    char romPath[1024];
    snprintf(romPath, sizeof(romPath), "%s", sysResolvePath(romRelPath));

    const char *exedir = sysGetExeDir();
    char outDir[1024];
    snprintf(outDir, sizeof(outDir), "%s", exedir[0] ? exedir : ".");

    sysLogPrintf(LOG_INFO,
        "romconvert: sidecars missing � converting the ROM (one-time, a few seconds)�");
    int code = rcConvert(romPath, outDir);
    if (code != 0)
        sysLogPrintf(LOG_ERROR,
            "romconvert: conversion failed (code %d) � a non-retail or "
            "byte-swapped ROM will fail here", code);

    if (!rcSidecarsPresent(region)) {
        sysLogPrintf(LOG_ERROR,
            "romconvert: data/pcmodels-%s/ and data/pccg-%s/ are still "
            "missing — cannot continue", region, region);
        return -1;
    }
    sysLogPrintf(LOG_INFO, "romconvert: sidecars generated — continuing boot");
    return 1;
}
