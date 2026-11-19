// seaview: the cloud textures to try (64x64, tiling): GE's own (from the captured frame), or periodic noises.
// Shared by the console viewer; the PC viewer (pc/seaview.cpp) carries the same code.
#pragma once
#include <math.h>

static inline float sv_vnoiseP(float x, float y, int px, int py, int seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
    auto hsh = [&](int a, int b) {
        a = ((a % px) + px) % px; b = ((b % py) + py) % py;
        unsigned h = (unsigned)(a * 374761393 + b * 668265263 + seed * 2246822519u); h = (h ^ (h >> 13)) * 1274126177u;
        return (float)(h & 0xffff) / 65535.0f;
    };
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    float a = hsh(ix, iy), b = hsh(ix + 1, iy), c = hsh(ix, iy + 1), d = hsh(ix + 1, iy + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

/* sel: 0 GE's, 1 soft fbm, 2 contrasted fbm, 3 cumulus, 4 stretched cirrus, 5 clear sky */
static inline void makeCloudTex(int sel, const unsigned char* ge, unsigned char* out) {
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float v = 0;
        if (sel == 0) v = ge[y * 64 + x] / 255.0f;
        else {
            const float ax = sel == 4 ? 1.0f : 4.0f, ay = sel == 4 ? 8.0f : 4.0f;
            float amp = 0.5f, sum = 0, norm = 0;
            for (int o = 0; o < 5; o++) {
                const int fx = (int)(ax * (1 << o)), fy = (int)(ay * (1 << o));
                sum += amp * sv_vnoiseP(x / 64.0f * fx, y / 64.0f * fy, fx, fy, o + sel * 17); norm += amp; amp *= 0.5f;
            }
            v = sum / norm;
            auto sstep = [](float a, float b, float t) { t = fminf(fmaxf((t - a) / (b - a), 0.f), 1.f); return t * t * (3 - 2 * t); };
            if (sel == 2) v = sstep(0.42f, 0.62f, v);
            else if (sel == 3) v = sstep(0.5f, 0.6f, v) * (0.6f + 0.4f * v);
            else if (sel == 5) v = sstep(0.6f, 0.78f, v);
        }
        out[y * 64 + x] = (unsigned char)(fminf(fmaxf(v, 0.f), 1.f) * 255.0f);
    }
}
