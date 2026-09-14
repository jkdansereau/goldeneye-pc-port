/*
 * romconvert.c — first-run sidecar conversion ("drop in your ROM and play").
 *
 * The engine needs two ROM-derived sidecar trees that are NOT shipped with a
 * release (they are copyrighted game data derived from the user's own ROM):
 *
 *     $S/pcmodels-<region>/   (pcmodels.bin + manifest.csv)  — port/src/pcmodels.c
 *     $S/pccg-<region>/       (pccg.bin    + manifest.csv)  — port/src/pccg.c
 *
 * The release bundle ships a frozen copy of the asset-prep tool
 * (prepare-assets.py, PyInstaller --onefile) as
 * <exedir>/prepare-assets/ge007-convert[.exe]. When the sidecars are missing
 * at boot, romdataInit() calls romConvertEnsureSidecars(), which spawns that
 * binary with `--rom <this ROM> --out <exedir>`, waits for it to finish, and
 * re-checks. The conversion is deterministic and takes a few seconds; after
 * the first run the sidecars exist and this path is a no-op (two fsExists).
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

#if defined(PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
#endif

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

#if defined(PLATFORM_WINDOWS)
/* Spawn `exePath --rom "<rom>" --out "<exedir>"` and wait for it.
 * Returns the exit code, or -1 if it could not be started. */
static int rcRunConverter(const char *exePath, const char *rom, const char *out)
{
    char cmd[4096];
    int n = snprintf(cmd, sizeof(cmd), "\"%s\" --rom \"%s\" --out \"%s\"",
                     exePath, rom, out);
    if (n < 0 || (size_t)n >= sizeof(cmd))
        return -1;   /* paths too long to build a command line */

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
    if (wlen <= 0)
        return -1;
    WCHAR *wcmd = (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR));
    if (!wcmd)
        return -1;
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    int rc = -1;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = (int)code;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    free(wcmd);
    return rc;
}
#else
static int rcRunConverter(const char *exePath, const char *rom, const char *out)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        /* Child: stdio is inherited (console apps see the converter's
         * progress lines; GUI builds simply discard them). */
        execv(exePath, (char *const[]){ exePath, "--rom", (char *)rom,
                                        "--out", (char *)out, NULL });
        _exit(127);   /* execv failed */
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        ;   /* EINTR */
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}
#endif

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
    snprintf(outDir, sizeof(outDir), "%s", exedir);

    /* Converter candidates: bundled under <exedir>/prepare-assets/ (the
     * release layout), then the CWD-relative form (running from a source
     * tree or an unpacked bundle in the CWD). */
#if defined(PLATFORM_WINDOWS)
    static const char *candRel[4] = {
        "$E/prepare-assets/ge007-convert.exe",
        "./prepare-assets/ge007-convert.exe",
        "$E/ge007-convert.exe",
        "./ge007-convert.exe",
    };
#else
    static const char *candRel[4] = {
        "$E/prepare-assets/ge007-convert",
        "./prepare-assets/ge007-convert",
        "$E/ge007-convert",
        "./ge007-convert",
    };
#endif
    char convPath[1024] = "";
    for (int i = 0; i < 4; i++) {
        if (rcExistsResolved(candRel[i])) {
            /* sysResolvePath() returns a static buffer — snapshot it. */
            snprintf(convPath, sizeof(convPath), "%s", sysResolvePath(candRel[i]));
            break;
        }
    }

    if (!convPath[0]) {
        sysLogPrintf(LOG_ERROR,
            "romconvert: sidecars for '%s' are missing and no ge007-convert "
            "binary was found (looked in <exedir>/prepare-assets/). Put your "
            "ROM at data/ge007.%s.z64 and re-run from the bundle folder, or "
            "run prepare-assets manually.", region, region);
        return -1;
    }

    sysLogPrintf(LOG_INFO,
        "romconvert: sidecars missing — running %s (one-time, a few seconds)…",
        convPath);
    int code = rcRunConverter(convPath, romPath, outDir);
    if (code != 0)
        sysLogPrintf(LOG_ERROR,
            "romconvert: ge007-convert exited with code %d — see its output "
            "above (a non-retail or byte-swapped ROM will fail here)", code);

    if (!rcSidecarsPresent(region)) {
        sysLogPrintf(LOG_ERROR,
            "romconvert: data/pcmodels-%s/ and data/pccg-%s/ are still "
            "missing — cannot continue", region, region);
        return -1;
    }
    sysLogPrintf(LOG_INFO, "romconvert: sidecars generated — continuing boot");
    return 1;
}
