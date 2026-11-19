// GE+ sky and water, shared by skyplus.frag.glsl (the sky pass, the sea of a
// water level) and water.frag.glsl (the level's own water surfaces). Not a
// shader of its own: cmake/SwitchPort.cmake pastes it where a shader has the
// line `#pragma skycommon`, after the shader has declared:
//   - the uniform block below's members (block `Sky`, any binding);
//   - `uCloudTex`, the cloud intensity texture (GE's bg/clouds);
//   - `uWaveTex`, the wave height texture (GE's bg/water1 or the level's own).
//
// layout(std140) uniform Sky {
//     vec4 uVP;        GL viewport
//     vec4 uAsp4;      xy: x_out = uAsp.x * x_game + uAsp.y; z: invert y (water draws);
//                      w: top of the horizon fog band, sin(elevation)
//     vec4 uC0_, uC1_, uC2_, uC3_;   image-plane rays TL TR BL BR, world
//     vec4 uEye4;      xyz eye, w cloud height
//     vec4 uSky4;      xyz sky, w gain
//     vec4 uCloud4;    xyz cloud, w texture scale
//     vec4 uOfs01;     layer 0 xy, layer 1 xy
//     vec4 uOfs2X;     layer 2 xy, z sun glow, w night
//     vec4 uSunDir4;     w: the level's swell (big waves)
//     vec4 uSunCol4;     w: the level's chop (small waves)
//     vec4 uMoonDir4;    w: the level's wave speed
//     vec4 uWater4;    xyz sea colour, w sea height
//     vec4 uWave4;     x time (s), y sea on, z water on (water draws), w unused
// };
#ifdef VIEWER   /* the standalone viewer's variant menus and editable values; the game never defines VIEWER */
#define SV(n) (uSeaVar == n)
#define SK(n) (uSkyVar == n)
#define PRM(i, d) uPrv[(i) / 4][(i) % 4]   /* block Viewer (binding 1), declared by the viewer */
#elif defined(VIEWER_CONST)   /* the script wrote SV, SK and PRM as constants above this file (tools_pc/seaview): the code the game would run */
#else
#define SV(n) false
#define SK(n) false
#define PRM(i, d) (d)
#endif
#define uAsp uAsp4.xy
#define uBand uAsp4.w
#define uC0 uC0_.xyz
#define uC1 uC1_.xyz
#define uC2 uC2_.xyz
#define uC3 uC3_.xyz
#define uEye uEye4.xyz
#define uCloudY uEye4.w
#define uSky uSky4.xyz
#define uGain uSky4.w
#define uCloud uCloud4.xyz
#define uTexScale uCloud4.w
#define uSunGlow uOfs2X.z
#define uNight uOfs2X.w
#define uSunDir uSunDir4.xyz
#define uSunCol uSunCol4.xyz
#define uMoonDir uMoonDir4.xyz
#define uSea uWater4.xyz
#define uSeaY uWater4.w
#define uTime uWave4.x
#define uSwell uSunDir4.w    /* the level's water settings (skyplus.c) */
#define uChop uSunCol4.w
#define uSpeed uMoonDir4.w
/* the cloud shell's planet radius: a round dome, the clouds dipping toward
 * the horizon (300000 read flat where the whole sky shows: Frigate, Surface) */
#define PLANET_R PRM(0, 100000.0)
#define SEA_FAR 300000.0   /* the sea plane's far end */

/* set by waterShade while it reads the sky for a reflection */
bool gReflect = false;
int gSeaMode = 0;   /* waterNormal: 0 both wave sets by the distance (the level's water draws), 1 the moving set only, 2 the still set only */
bool gLite = false;   /* waterShade reflects the horizon colour, not the sky panorama (the baked far sea, the viewers' tests) */

vec2 ofsOf(int i) { return i == 0 ? uOfs01.xy : (i == 1 ? uOfs01.zw : uOfs2X.xy); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

/* The world ray through a point of the player's screen: u, v in 0..1 from the
 * top-left corner, x already back in the game's screen x. */
vec3 skyRay(float u, float v) {
    return mix(mix(uC0, uC1, u), mix(uC2, uC3, u), v);
}

/* The sky seen along nd (normalised, nd.y > 0): GE's colour, the three cloud
 * layers, sun, stars and moon, faded into the fog colour at the horizon. */
vec3 skyColor(vec3 nd) {
    vec3 d = nd;
    float hz = length(d.xz) + 1e-4;
    float e = max(d.y, 0.0);
    vec3 F = uSky;

    /* 1. sky behind everything: horizon = GE's colour, zenith a little deeper */
    vec3 c = mix(F, F * PRM(13, 0.82), smoothstep(0.0, 0.8, e));
    /* a sun already set (blue hour) shows no disc and no halo, only the low
     * glow of step 6 */
    float sunUp = smoothstep(-0.01, 0.03, uSunDir.y);
    if (uSunGlow > 0.0 && sunUp > 0.0) {
        float cs = dot(nd, uSunDir);
        float sunAmt = max(cs, 0.0);
        c += uSunCol * (pow(sunAmt, 5.0) * 0.5 + pow(sunAmt, 48.0) * 0.7) * uSunGlow * sunUp;
        if (cs > 0.9995) c = mix(c, max(c, vec3(1.3)), sunUp);
    }
    float star = 0.0;
    if (uNight > 0.5 && e > 0.03 && !SK(4)) {
        vec2 cell = floor(vec2(atan(nd.x, nd.z), asin(e)) * 400.0);
        float h = hash(cell);
        if (h > 0.997) star = (h - 0.997) / 0.003 * smoothstep(0.03, 0.3, e) * 0.8;
    }

    /* 2. cloud layers, highest first, over the sky; layer 1 = GE's */
    vec3 ld = uSunGlow > 0.0 ? uSunDir : (uNight > 0.5 ? uMoonDir : vec3(0.0, 1.0, 0.0));
    float fr = 1.0 - min(1.0, 2.0 * d.y / hz) * uGain;
    /* the light the clouds get: from a high sun, all of it; from a low or set sun (or the moon), the clouds overhead fall back to
     * the sky's own colour and only the side of the sky toward the light stays bright. A level without a sun or a moon (ld straight
     * up) keeps GE's shading */
    float dayl = smoothstep(-0.12, 0.35, ld.y);
    vec2 lh = length(ld.xz) > 1e-3 ? normalize(ld.xz) : vec2(0.0);
    float side = pow(max(dot(d.xz / hz, lh), 0.0), 2.0) * (1.0 - smoothstep(0.0, 0.6, e));
    float cl = mix(PRM(14, 0.3), 1.0, max(dayl, side * PRM(15, 0.8)));
    vec3 shade = F + uCloud * (1.0 - F) * (1.0 - fr) * cl;
    float H = uCloudY - uEye.y;
    float cover = 0.0;
    if (H > 0.0) {
        for (int i = 0; i < 3; i++) {
            if (gReflect && i != 1) continue;
            if (!gReflect && ((SK(1) && i != 1) || (SK(2) && i == 0))) continue;   /* a reflection shows GE's layer only (GPU cost) */
            float kH  = i == 0 ? 1.8  : (i == 1 ? 1.0 : 0.55);
            float kSc = i == 0 ? 1.7  : (i == 1 ? 1.0 : 0.45);
            float kRo = i == 0 ? 0.9  : (i == 1 ? 0.0 : -0.6);
            float kOp = i == 0 ? 0.35 : (i == 1 ? 1.0 : 0.25);
            float kK  = i == 0 ? 0.15 : (i == 1 ? 0.0 : 0.25);
            /* a curved shell (a planet's cloud layer seen from the ground), not a
             * flat plane: overhead it is the plane, toward the horizon it bends
             * down, so the distance stays finite and the texture never shrinks
             * to a moire band */
            float Hl = H * kH;
            float b = PLANET_R * nd.y;
            float dl = -b + sqrt(b * b + 2.0 * PLANET_R * Hl + Hl * Hl);   /* distance to the shell */
            /* GE: S = x * 0.1, T = z * 0.1 + offset, in S10.5 (32 per texel) */
            vec2 w = (uEye.xz + nd.xz * dl) * (0.1 / 32.0) * uTexScale;
            float cr = cos(kRo), sr = sin(kRo);
            vec2 t = vec2(w.x * cr - w.y * sr, w.x * sr + w.y * cr) / kSc + (gReflect ? vec2(0.0) : ofsOf(i));   /* reflections hold still: the far sea is nearly all reflection, and a scrolling one slid the background sideways */
            /* a reflection reads a very low-resolution sky (8x8 of the 64x64
             * clouds): nothing fine to mirror, so it changes smoothly from
             * pixel to pixel and holds still when the view moves a sub-pixel */
            float I = gReflect ? textureLod(uCloudTex, t / 64.0, 3.0).r : texture(uCloudTex, t / 64.0).r;
            if (kK > 0.0) I = clamp((I - kK) / (1.0 - kK), 0.0, 1.0);
            /* the clouds' lighting: a second fetch, skipped in a reflection,
             * where it does not show (gReflect, set by waterShade) */
            float lit = 1.0;
            if (!gReflect && !SK(3)) {
                float I2 = texture(uCloudTex, (t + ld.xz * 1.5) / 64.0).r;
                lit += clamp((I2 - I) * 2.5, -0.35, 0.35) * (i == 1 ? 0.6 : 1.0);
            }
            /* haze: clouds dissolve into the horizon colour just above it (GE's own
             * fade only reaches the shade, not the opacity) */
            float haze = smoothstep(0.0, uBand * 0.36, e);
            float a = I * kOp * haze * PRM(8, 1.0);
            cover = 1.0 - (1.0 - cover) * (1.0 - min(1.0, a * 3.0));
            /* light on the cloud's own colour only, never on the sky part of the shade */
            vec3 cc = min(vec3(1.1), F + (shade - F) * lit);
            if (uSunGlow > 0.0) cc += uSunCol * pow(max(dot(nd, uSunDir), 0.0), 12.0) * (1.0 - I) * 0.6 * haze;   /* backlit edges */
            c = mix(c, cc, a);
        }
    }
    /* 3. the sun's halo shines through: a tight core and a wide bloom, dimmed by the clouds */
    if (uSunGlow > 0.0 && !SK(4)) {
        float sa = max(dot(nd, uSunDir), 0.0);
        c += uSunCol * (pow(sa, 400.0) * 1.2 + pow(sa, 60.0) * 0.45 + pow(sa, 8.0) * 0.12) * (1.0 - 0.6 * cover) * sunUp;   /* the halo pierces the fog */
    }
    /* 4. stars and moon only where the clouds let them through */
    c += star * (1.0 - cover);
    if (uNight > 0.5) {
        float cs = dot(nd, uMoonDir);
        float gl = pow(max(cs, 0.0), 40.0) * 0.08 * (1.0 - cover * 0.5);
        c = cs > 0.9997 ? mix(c + gl, vec3(0.85), 1.0 - cover) : c + gl;
    }
    /* 5. horizon band: everything fades to the fog colour before the horizon,
     * so whatever the level draws far away (trees popping in at the draw
     * distance) meets a flat fog colour, as with GE's plane, and never cuts
     * into the sun. Its top (uBand) is the elevation of the level's tallest
     * scenery at its fog distance (skyplus.c): high on Dam (12.7 deg), thin
     * over an open sea, where the clouds come down to the horizon. */
    c = mix(F, c, smoothstep(uBand * 0.18, uBand, e));
    /* 6. a sun already set (blue hour, uSunDir.y < 0): the light it leaves is a
     * pale glow hugging the horizon on its side, over the band */
    if (uSunGlow > 0.0 && uSunDir.y < 0.0) {
        float az = max(dot(d.xz / hz, normalize(uSunDir.xz)), 0.0);
        c += uSunCol * (pow(az, 3.0) * 0.6 + pow(az, 1.2) * 0.2) * exp(-e * 7.0) * uSunGlow;
    }
    return c;
}

/* Procedural value noise (no texture): a hash without sin (exact at large
 * coordinates), smooth cells. */
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 x) {
    vec2 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

/* The sky right at the horizon along nd's heading (skyColor with e = 0: the
 * band has faded everything to the fog colour but the set sun's glow), for a
 * fraction of skyColor's cost. */
vec3 horizonColor(vec3 nd) {
    vec3 c = uSky;
    if (uSunGlow > 0.0 && uSunDir.y < 0.0) {
        float az = max(dot(nd.xz / (length(nd.xz) + 1e-4), normalize(uSunDir.xz)), 0.0);
        c += uSunCol * (pow(az, 3.0) * 0.6 + pow(az, 1.2) * 0.2) * uSunGlow;
    }
    return c;
}

/* The sky mirrored by the water along r (normalised, r.y >= ~0.02). In the sky
 * pass (USE_SKY_LUT) it is read from the panorama drawn once per frame
 * (skylut.frag.glsl): one fetch instead of the sky's whole computation for
 * every pixel of sea, and a smooth, low-resolution sky. */
vec3 skyReflect(vec3 r) {
#ifdef USE_SKY_LUT
    return texture(uSkyLut, vec2(atan(r.x, r.z) * 0.15915494 + 0.5, 1.0 - sqrt(r.y))).rgb;
#else
    gReflect = true;
    vec3 c = skyColor(r);
    gReflect = false;
    return c;
#endif
}

/* ---------------- GE+ water ----------------
 * A surface point p (world xz) seen along nd from dist away. Stylised, after
 * photos of open sea from a ship's deck at dusk: wind waves are small sharp
 * crests (thin bright crescents over wide dark troughs, like scales) on a
 * broad irregular swell, low in contrast, settling into a smooth mirror
 * toward the horizon. Everything comes from one tiling fbm height texture
 * (skyplus.c) read in the wind's frame, stretched along the crests. No sine
 * trains: perfect sines read as parallel stripes in perspective. */


/* the height texture's slope to the next texel (x, y), precomputed in G, B
 * (skyplus.c, WTEX_SLOPE_K = 6): one fetch per sample */
/* the screen derivatives of the water point (world xz), set by the caller in
 * uniform control flow: the wave reads use them (textureGrad) for their mip
 * level and anisotropy, inside branches where implicit ones are undefined */
vec2 gDpdx = vec2(0.0), gDpdy = vec2(0.0);
float gK = 1.0;   /* the weight texWaves gave its last layer (1 present, 0 dropped for distance) */

int gMode = 0;   /* the viewers' wave-layer tests (8..11: 0..3 layers); 0 in the game: all of them */
vec2 texSlopeAt(vec2 uv, vec2 dx, vec2 dy, float lx, float ly) {   /* lx, ly = length(dx), length(dy), the caller has them */
    /* at grazing angles a pixel's footprint is far longer than wide; past the
     * sampler's 8x anisotropy the GPU would pick too fine a mip along the long
     * axis and alias (a pixel-level noise flickering frame to frame). Widen
     * the short axis so the ratio stays at 8: a filtered mip, detail kept
     * along the horizon. */
    const float R = 2.0;   /* = the sampler's anisotropy (gfx_deko3d.cpp) */
    if (ly > R * lx) dx *= ly / (R * max(lx, 1e-8));
    else if (lx > R * ly) dy *= lx / (R * max(ly, 1e-8));
    return (textureGrad(uWaveTex, uv, dx, dy).gb * 255.0 - 127.5) / (6.0 * 255.0);
}

/* The slope (world xz) of one texture layer: wind direction W, the tile
 * spanning Lx world units along the crests and Ly across them. Two copies of
 * the tile cross each other in opposite directions: their sum rises and falls
 * in place, irregularly (waves bob up and down), where one copy alone would
 * slide across the sea like a conveyor belt. Stylised strength: scaled to the
 * layer's short side, not physical. */
/* The slope (world xz) of one texture layer: wind direction W, the tile
 * spanning Lx world units along the crests and Ly across them, drifting
 * slowly with time. One copy per layer, each drifting its own way, so their
 * sum changes shape irregularly (two copies crossing made a standing wave that
 * pulsed like a 1990 demo). No fade with the distance: the texture's mipmaps,
 * read anisotropically (gfx_deko3d.cpp), filter the far waves themselves and
 * keep their detail along the horizon (open sea is busy to the horizon).
 * Stylised strength: scaled to the layer's short side, not physical. */
vec2 texWaves(vec2 p, vec2 W, float Lx, float Ly, vec2 drift, float time, float cut) {   /* cut: mips earlier than 6 at which the layer is dropped */
    float Lm = min(Lx, Ly);
    vec2 Wp = vec2(-W.y, W.x);
    vec2 dx = vec2(dot(gDpdx, Wp) / Lx, dot(gDpdx, W) / Ly), dy = vec2(dot(gDpdy, Wp) / Lx, dot(gDpdy, W) / Ly);
    /* the mip level this pixel reads (the anisotropy covers a 2:1 footprint):
     * past ~6 the 128-texel tile is averaged flat, so the layer adds nothing
     * there and is not read at all (GPU cost on the console: every layer read
     * everywhere was 17-20 ms) */
    float lx = length(dx), ly = length(dy);
    float mip = log2(max(max(lx, ly) / 2.0, min(lx, ly)) * 128.0 + 1e-6);
    float k = 1.0 - smoothstep(5.0 - cut, 6.0 - cut, mip);
    gK = k;
    if (k <= 0.0) return vec2(0.0);
    vec2 uv = vec2(dot(p, Wp) / Lx, dot(p, W) / Ly);
    vec2 g = texSlopeAt(uv + drift * time, dx, dy, lx, ly) * k;
    /* x4: per 1/32 of the tile, the strength scale the weights were tuned in
     * (the finest octave is 1/32 of the tile) */
    return (g.x * Wp * (Lm / Lx) + g.y * W * (Lm / Ly)) * 4.0;
}

/* layer 2 stops this many mips earlier than the finer layers: it costs the most and the far sea is busy enough without it */
#define LAYER2_CUT 1.5
/* The wave layers at a given time (0: still). */
vec2 waveLayers(vec2 p, float time) {
    const vec2 W = vec2(0.943, 0.330);    /* the wind, normalize(1, 0.35) */
    const vec2 W2 = vec2(0.706, 0.708);   /* a second wave field, 26 deg off it */
    const vec2 W3 = vec2(0.990, -0.139);  /* a third, 27 deg the other way */
    /* the viewers' tests: modes 8..11 compute 0..3 layers, anything else all 4 */
    int nL = (gMode >= 8 && gMode <= 11) ? gMode - 8 : 4;
    vec2 s = vec2(0.0);
    if (nL > 0) s = texWaves(p, W, 9000.0, 3500.0, vec2(0.004, 0.010), time, 0.0) * 2.1 * uSwell;   /* swell */
    if (nL > 1) s += texWaves(p, W, 420.0, 260.0, vec2(0.030, 0.110), time, LAYER2_CUT) * 3.0 * uChop;
    /* where layer 2 has faded out, a still fine grain from the same noise texture takes over (world-fixed, no drift:
     * the far sea stays busy and nothing moves there) */
    if (nL > 1 && gK < 1.0) {
        vec2 dxg = gDpdx / 38.0 * 0.6, dyg = gDpdy / 38.0;   /* sharper across, the full footprint along the depth: no swimming */
        s += texSlopeAt(p / 38.0, dxg, dyg, length(dxg), length(dyg)) * 12.0 * uChop * (1.0 - gK);
    }
    if (nL > 2) s += texWaves(p, W2, 210.0, 130.0, vec2(-0.050, 0.150), time, 0.0) * 2.6 * uChop;
    if (nL > 3) s += texWaves(p, W3, 105.0, 65.0, vec2(0.060, 0.200), time, 0.0) * 2.0 * uChop;
    return s;
}

/* p = world xz, dist = distance to the eye. The near sea moves; far off the
 * same layers stand still: a wave's drift is a pixel's worth of motion there
 * and the big layers only made the whole background pulse. Two sets blended
 * by the distance (a drift scaled by the distance would shear the sea more
 * and more as time goes on); each pixel reads only the set it needs. */
vec3 waterNormal(vec2 p, float dist) {
    float kf = gSeaMode == 1 ? 0.0 : (gSeaMode == 2 ? 1.0 : smoothstep(1200.0, 6000.0, dist));   /* both sets are computed only in this band */
    vec2 s = vec2(0.0);
    /* the medium swell: broad, round undulations that light and darken the sea in patches; near only, it is gone
     * where the sea turns still */
    if (kf < 1.0) s += (waveLayers(p, uTime * uSpeed) + texWaves(p, vec2(0.706, 0.708), 1100.0, 900.0, vec2(0.012, 0.030), uTime * uSpeed, 0.0) * PRM(11, 5.0) * uChop * ((SV(1) || SV(5) || SV(6) || SV(12)) ? 0.0 : 1.0)) * (1.0 - kf);
    if (kf > 0.0) s += (waveLayers(p, 0.0) + texWaves(p, vec2(0.706, 0.708), 1100.0, 900.0, vec2(0.012, 0.030), 0.0, 0.0) * PRM(11, 5.0) * uChop * ((SV(1) || SV(5) || SV(6) || SV(12)) ? 0.0 : 1.0)) * kf;
    /* wide patches, calmer or rougher (the slicks of a real sea: hundreds of
     * metres across, mirroring more or less of the sky), drifting slowly */
    {
        vec2 q = p + vec2(3.0, 7.0) * uTime;
        float n = vnoise(q / 2600.0) * 0.65 + vnoise(q / 6100.0 + 13.7) * 0.35;
        s *= (SV(11) || SV(5) || SV(6) || SV(12)) ? 1.0 : mix(PRM(6, 0.35), PRM(7, 1.45), smoothstep(0.30, 0.70, n));
    }
    /* specular anti-aliasing: at grazing angles the reflection swings on the
     * smallest change of the normal, so far off the waves keep only 35 % of
     * their slope: still textured (never a lake's mirror), no longer a pixel
     * noise flickering as the view moves */
    s *= mix(1.0, PRM(5, 0.35), smoothstep(3000.0, 20000.0, dist));
    return normalize(vec3(-s.x, 1.0, -s.y));
}

/* The water's colour: body under a reflection of the GE+ sky, mixed by the
 * Fresnel term, plus the sun's glints. nd = view ray (normalised, pointing
 * away from the eye), n = surface normal, dist = distance to the eye. */
vec3 waterShade(vec3 nd, vec3 n, vec3 body, float dist) {
    float cosv = max(dot(n, -nd), 0.0);
    float fres = 0.03 + 0.97 * pow(1.0 - cosv, PRM(1, 5.0));
    vec3 r = reflect(nd, n);
    r.y = 0.5 * (r.y + sqrt(r.y * r.y + 0.0016));   /* a smooth max(r.y, ~0.02): a hard clamp split neighbours at grazing angles */
    r = normalize(r);
    /* the baked far sea (gSeaMode 2) mirrors the plain sky colour: the low glow of a set sun, added by horizonColor on its side,
     * showed there as a white patch, the sea being nearly all reflection at a grazing angle */
    vec3 refl = gLite ? (gSeaMode == 2 ? uSky : horizonColor(r)) : skyReflect(r);
    vec3 c = mix(body, refl, clamp(fres * PRM(2, 0.9) + PRM(3, 0.08), 0.0, 1.0));
    if (gSeaMode == 2) c = mix(c, body, PRM(4, 0.5) * smoothstep(2500.0, 14000.0, dist));   /* the far sea's reflection dims with distance: no bright streaks toward the horizon */
    vec3 sunDir = uSunGlow > 0.0 ? uSunDir : normalize(vec3(0.3, 0.8, 0.5));
    vec3 sunCol = uSunGlow > 0.0 ? uSunCol : vec3(1.0, 0.97, 0.9);
    /* crisp highlight patches with a hard edge (a threshold, not a pow
     * speck), plus a faint wide sheen around them. The patches belong to the
     * near water: far off the waves flatten and the threshold would cut the
     * mirrored sun into a hard-edged block. A set sun (blue hour) leaves no
     * highlight at all: the water only mirrors the sky. */
    float g = max(dot(r, sunDir), 0.0);
    float near = 1.0 / (1.0 + dist / 2500.0);
    float glint = (smoothstep(0.990, 0.996, g) * 1.2 * near + pow(g, 40.0) * 0.12) * smoothstep(-0.01, 0.03, sunDir.y);
    if (uNight > 0.5) glint *= 0.15;
    return c + sunCol * glint;
}
