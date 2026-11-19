/* conv_main.c -- pipeline driver shared by the game and the CLI. */
#include "convert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define cv_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define cv_mkdir(p) mkdir((p), 0777)
#endif

static void mkdirs(const char *path)
{
    char tmp[1024];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char ch = tmp[i];
            tmp[i] = 0;
            cv_mkdir(tmp);
            tmp[i] = ch;
        }
    }
    cv_mkdir(tmp);
}

static const CvRegionData *region_by_country(int country)
{
    switch (country) {
    case 'E': return &cv_regions[0];
    case 'P': return &cv_regions[1];
    case 'J': return &cv_regions[2];
    }
    return NULL;
}

static int region_index_by_name(const char *name)
{
    for (int i = 0; i < CV_NREGIONS; i++)
        if (!strcmp(cv_regions[i].region, name)) return i;
    return -1;
}

int cv_convert_rom(const char *rom_path, const char *region, const char *out_root, int steps)
{
    FILE *f;
    uint8_t *rom;
    long sz;
    CvCtx c;
    char dir_models[1024], dir_cg[1024];
    int rc = 0;

    f = fopen(rom_path, "rb");
    if (!f) { fprintf(stderr, "cannot open ROM %s\n", rom_path); return 1; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)sz);
    if (!rom || fread(rom, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(rom); return 1; }
    fclose(f);

    memset(&c, 0, sizeof c);
    c.rom = rom;
    c.rom_size = (size_t)sz;
    if (region) {
        int i = region_index_by_name(region);
        c.rd = i < 0 ? NULL : &cv_regions[i];
    } else {
        /* big-endian .z64: country code byte at 0x3E */
        c.rd = sz > 0x40 ? region_by_country(rom[0x3E]) : NULL;
    }
    if (!c.rd) { fprintf(stderr, "unrecognised ROM region\n"); free(rom); return 1; }
    c.region = c.rd->region;

    snprintf(dir_models, sizeof dir_models, "%s/data/pcmodels-%s", out_root, c.region);
    snprintf(dir_cg, sizeof dir_cg, "%s/data/pccg-%s", out_root, c.region);
    mkdirs(dir_models);
    mkdirs(dir_cg);

    /* order matters: d88 appends to d69's output */
    if (!rc && (steps & CV_STEP_D43)) rc = cv_run_d43(&c, dir_models);
    if (!rc && (steps & CV_STEP_D69)) rc = cv_run_d69(&c, dir_cg);
    if (!rc && (steps & CV_STEP_D88)) rc = cv_run_d88(&c, dir_cg);

    if (rc) {
        printf("\n%d ERRORS:\n", c.nerrors);
        for (int i = 0; i < c.nerrors && i < 64; i++) printf("   %s\n", c.errors[i]);
    } else {
        printf("\nALL CHECKS PASSED\n");
    }
    free(rom);
    return rc ? 1 : 0;
}
