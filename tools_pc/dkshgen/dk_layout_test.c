/* D973: host test of port/fast3d/gfx_dk_layout.h (vertex layout = GL's,
 * combiner parameters round-trip). Build and run (compiled as C++ because
 * gfx_cc.h is C++20 when __cplusplus is set; gfx_cc.cpp is linked):
 *   g++ -std=c++20 -O2 -Wall -I port/fast3d -x c++ tools_pc/dkshgen/dk_layout_test.c \
 *       -x c++ port/fast3d/gfx_cc.cpp -o <temp>/dklt && <temp>/dklt */
#include <stdio.h>
#include <stdlib.h>

#include "gfx_dk_layout.h"

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); s_fail++; } } while (0)

/* GL's float count (gfx_opengl_gen_shader_src), recomputed independently. */
static int gl_num_floats(const struct CCFeatures* f) {
    int n = 4;
    for (int i = 0; i < 2; i++) {
        if (f->used_textures[i]) {
            n += 2;
            for (int j = 0; j < 2; j++) n += f->clamp[i][j] ? 1 : 0;
        }
    }
    if (f->opt_fog) n += 4;
    if (f->opt_grayscale) n += 4;
    n += f->num_inputs * (f->opt_alpha ? 4 : 3);
    return n;
}

static void check_layout(const char* name, uint64_t id0, uint32_t id1, int expect) {
    struct CCFeatures f;
    memset(&f, 0, sizeof(f));
    gfx_cc_get_features(id0, id1, &f);
    struct DkLayout l;
    dk_layout_from_features(&f, &l);
    CHECK(l.num_floats == expect, "%s: num_floats %d, expected %d", name, l.num_floats, expect);
    CHECK(l.num_floats == gl_num_floats(&f), "%s: num_floats %d, GL %d", name, l.num_floats, gl_num_floats(&f));
    int sum = 0;
    for (int i = 0; i < l.count; i++) {
        CHECK(l.offset[i] == sum, "%s: offset[%d] %d, expected %d", name, i, l.offset[i], sum);
        CHECK(l.loc[i] < DK_ATTR_COUNT, "%s: loc[%d] %d out of range", name, i, l.loc[i]);
        sum += l.size[i];
    }
    CHECK(sum == l.num_floats, "%s: sizes sum %d != %d", name, sum, l.num_floats);
    CHECK(l.loc[0] == DK_ATTR_POS && l.size[0] == 4, "%s: first attribute is not pos/4", name);
    printf("ok   layout %-36s num_floats=%d count=%d\n", name, l.num_floats, l.count);
}

/* id0 nibble of cycle c, channel k, slot i */
static uint64_t sel(int c, int k, int i, int v) {
    return (uint64_t)(v & 0xf) << (c * 32 + k * 16 + i * 4);
}

int main(void) {
    /* no textures, one input (SHADER_INPUT_1 in d of both channels), no alpha: 4 + 3 = 7 */
    check_layout("no textures + 1 input", sel(0, 0, 3, SHADER_INPUT_1) | sel(0, 1, 3, SHADER_INPUT_1), 0, 7);

    /* two clamped textures + fog + 3 inputs with alpha: 4 + 2*(2+1+1) + 4 + 12 = 28 */
    {
        uint64_t id0 = sel(0, 0, 0, SHADER_TEXEL0) | sel(0, 0, 1, SHADER_TEXEL1) | sel(0, 0, 2, SHADER_INPUT_3) |
                       sel(0, 0, 3, SHADER_INPUT_1) | sel(0, 1, 3, SHADER_INPUT_2);
        uint32_t id1 = SHADER_OPT_ALPHA | SHADER_OPT_FOG | SHADER_OPT_TEXEL0_CLAMP_S | SHADER_OPT_TEXEL0_CLAMP_T |
                       SHADER_OPT_TEXEL1_CLAMP_S | SHADER_OPT_TEXEL1_CLAMP_T;
        check_layout("2 clamped tex + fog + 3 alpha inputs", id0, id1, 28);
    }

    /* grayscale only: 4 + 4 = 8 */
    check_layout("grayscale only", 0, SHADER_OPT_GRAYSCALE, 8);

    /* one texture clamped on T only, 2 inputs without alpha: 4 + 2 + 1 + 6 = 13 */
    check_layout("tex0 clamp T + 2 inputs", sel(0, 0, 0, SHADER_TEXEL0) | sel(0, 0, 3, SHADER_INPUT_2),
                 SHADER_OPT_TEXEL0_CLAMP_T, 13);

    /* combiner params round-trip for every cycle/channel/slot, with random ids */
    srand(12345);
    int rt = 0;
    for (int n = 0; n < 2000; n++) {
        uint64_t id0 = 0;
        for (int b = 0; b < 64; b += 15) id0 = (id0 << 15) ^ (uint64_t)rand();
        uint32_t id1 = (uint32_t)rand() & 0x1fff;
        struct CCFeatures f;
        memset(&f, 0, sizeof(f));
        gfx_cc_get_features(id0, id1, &f);
        struct DkCombinerParams p;
        dk_combiner_params(&f, &p);
        for (int c = 0; c < 2; c++)
            for (int k = 0; k < 2; k++)
                for (int i = 0; i < 4; i++) {
                    CHECK(dk_combiner_sel(&p, c, k, i) == f.c[c][k][i], "round-trip id0=%016llx c%d k%d i%d",
                          (unsigned long long)id0, c, k, i);
                    rt++;
                }
        CHECK(((p.flags & DK_CF_ALPHA) != 0) == f.opt_alpha, "alpha flag");
        CHECK(((p.flags & DK_CF_2CYC) != 0) == f.opt_2cyc, "2cyc flag");
        CHECK((int)((p.flags >> DK_CF_NUM_INPUTS_SHIFT) & 7) == f.num_inputs, "num_inputs");
    }
    printf("ok   combiner params round-trip (%d slots)\n", rt);
    CHECK(sizeof(struct DkCombinerParams) == 24, "DkCombinerParams size %u", (unsigned)sizeof(struct DkCombinerParams));
    CHECK(sizeof(struct DkDrawUniforms) == 32, "DkDrawUniforms size %u", (unsigned)sizeof(struct DkDrawUniforms));
    CHECK(sizeof(struct DkPackEntry) == 32, "DkPackEntry size %u", (unsigned)sizeof(struct DkPackEntry));

    if (s_fail) {
        printf("dk_layout_test: %d FAILED\n", s_fail);
        return 1;
    }
    printf("dk_layout_test: all PASS\n");
    return 0;
}
