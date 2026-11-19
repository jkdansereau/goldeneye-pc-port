/* ge007_convert_cli.c -- ge007-convert: ROM -> PC sidecars (replaces
 * tools_pc/dist/prepare-assets/prepare-assets.py + the d43/d69/d88 emit
 * scripts). */
#include "convert.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: ge007-convert --rom ROM.z64 [--out DIR] [--region ntsc-final|pal-final|jpn-final]\n"
        "                     [--only d43|d69|d88]...\n"
        "  Writes DIR/data/pcmodels-<region>/ and DIR/data/pccg-<region>/ (DIR defaults to .).\n");
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *out = ".", *region = NULL;
    int steps = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--region") && i + 1 < argc) region = argv[++i];
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            const char *s = argv[++i];
            if (!strcmp(s, "d43")) steps |= CV_STEP_D43;
            else if (!strcmp(s, "d69")) steps |= CV_STEP_D69;
            else if (!strcmp(s, "d88")) steps |= CV_STEP_D88;
            else { usage(); return 2; }
        } else { usage(); return 2; }
    }
    if (!rom) { usage(); return 2; }
    return cv_convert_rom(rom, region, out, steps ? steps : CV_STEP_ALL);
}
