#version 460
// D973: GE+ sky (gfx_opengl.cpp skyplus_build's fragment shader) with explicit
// bindings; the uniforms are packed into vec4s (struct DkSkyUniforms in
// gfx_deko3d.cpp). The position in the viewport comes from the fullscreen
// vertex shader (vUV = GL's (gl_FragCoord.xy - uVP.xy) / uVP.zw), never from
// gl_FragCoord, whose orientation differs between targets / Ryujinx / hardware.
// The sky itself and the water lighting are in skycommon.glsl. On a water
// level (uWave4.y) the sea is drawn here too, under the horizon.
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uCloudTex;
layout(binding = 1) uniform sampler2D uWaveTex;
layout(binding = 2) uniform sampler2D uSkyLut;   /* the reflection sky, drawn once per frame (skylut.frag.glsl) */
#define USE_SKY_LUT
layout(std140, binding = 0) uniform Sky {
    vec4 uVP;
    vec4 uAsp4;
    vec4 uC0_, uC1_, uC2_, uC3_;
    vec4 uEye4;
    vec4 uSky4;
    vec4 uCloud4;
    vec4 uOfs01;
    vec4 uOfs2X;
    vec4 uSunDir4;
    vec4 uSunCol4;
    vec4 uMoonDir4;
    vec4 uWater4;
    vec4 uWave4;
};
layout(binding = 3) uniform sampler2D uHalfSea;  /* the sea drawn at half width (pass 1), read by pass 2 */
#define uPass uC0_.w   /* 0: one pass, 1: sea only, into the reduced-width target, 2: read it, 4: bake the far sea into the panorama,
                        * 5: enlarge the half-size sky target onto the screen */
layout(binding = 4) uniform sampler2D uSeaPano;   /* the far sea, baked (pass 4): u = azimuth, v = sqrt(elevation) / panoMax */
layout(location = 0) out vec4 o;

#pragma skycommon

#define uBakeX uC1_.w   /* where (and how high above the sea) the panorama was baked from; free members of the corner rays */
#define uBakeZ uC2_.w
#define uBakeH uC3_.w
#define PANO_W 4096.0   /* the baked far sea's size (gfx_deko3d.cpp) */
#define PANO_H 512.0
#define NEAR_Z0 PRM(9, 600.0)  /* the live sea gives way to the baked one between these depths along the view */
#define NEAR_Z1 PRM(10, 6000.0)
/* the panorama's v range: 1 = straight down. The baked sea starts NEAR_Z0 deep: its elevation angle's sine is at most ~0.64 for the
 * eye heights of the sea levels (Frigate 670), whatever the eye does after the bake */
float panoMax(float h) { return 0.8; }

void main() {
    vec2 f = vUV;                             /* 0..1, y up */
    if (uPass > 4.5) {   /* 5: the half-size sky target (uHalfSea slot) enlarged onto the screen */
        o = vec4(textureLod(uHalfSea, vec2(f.x, 1.0 - f.y), 0.0).rgb, 1.0);
        return;
    }
    if (uPass > 3.5) {
        /* the far sea, baked: the still waves and the reflection along each direction under the horizon, filtered by the
         * panorama's own texel footprint. Drawn when the sun, the eye height or the sea level change, or the player has
         * moved far */
        float hh = max(uEye.y - uSeaY, 1.0), vm = panoMax(hh);
        float az = (f.x - 0.5) * 6.2831853, v = max(f.y * vm, sqrt(hh / SEA_FAR) * 1.05);   /* not past the sea's far end */
        float ay = v * v, cy = sqrt(1.0 - ay * ay);
        vec3 bd = vec3(cy * sin(az), -ay, cy * cos(az));
        float bt = min(hh / ay, SEA_FAR);
        vec2 bp = uEye.xz + bd.xz * bt;
        float rho = hh * cy / ay;                              /* the horizontal range of the sea point */
        vec2 er = vec2(sin(az), cos(az)), ea = vec2(cos(az), -sin(az));
        gDpdx = ea * rho * (6.2831853 / PANO_W);
        gDpdy = er * (-hh / (ay * ay * cy)) * 2.0 * v * (vm / PANO_H);
        gDpdx *= min(1.0, 3000.0 / max(length(gDpdx), 1e-3));   /* the footprint grows without bound at the horizon: keep the mip reads finite */
        gDpdy *= min(1.0, 3000.0 / max(length(gDpdy), 1e-3));
        gSeaMode = 2;
        gLite = true;   /* no reflection of the sky out there: the horizon's colour only */
        vec3 bn = waterNormal(bp, bt);
        o = vec4(clamp(waterShade(bd, bn, uSea, bt), 0.0, 1.0), 1.0);
        return;
    }
    float xg = ((f.x * 2.0 - 1.0) - uAsp.y) / uAsp.x;       /* back to the game's screen x */
    float u = xg * 0.5 + 0.5, v = 1.0 - f.y;                /* v = 0 at the top of the screen */
    vec3 d = skyRay(u, v);
    vec3 nd = normalize(d);
    vec3 F = uSky;
    /* under the horizon: GE's clear colour, or the sea, chosen at the single
     * write below. No early return: Ryujinx ran the cloud code under the
     * horizon anyway, so the pixels there got clouds with a negative shade
     * (saturated blue) or the previous frame (Cradle, looking down from the
     * cradle). */
    bool below = !(d.y > 0.0);
    /* the sea point under this pixel, p = eye - h * d.xz / d.y, and its change
     * per screen pixel. dFdy came out 0 in this pass (Ryujinx, 2026-11-17), so
     * the wave reads picked mips as if the sea were not seen at a grazing
     * angle, and the far sea was a pixel noise flickering with the view: the
     * change per pixel row is computed analytically (the ray d is linear on
     * the screen); dFdx is right */
    float h = uEye.y - uSeaY;
    float t = h > 0.0 ? h / max(-nd.y, 1e-4) : 0.0;
    t = min(t, SEA_FAR);
    vec2 p = uEye.xz + nd.xz * t;
    if (uPass < 1.5) {
        vec3 ddv = mix(uC2 - uC0, uC3 - uC1, u) / uVP.w;   /* the ray's change per pixel row */
        float dy = min(d.y, -1e-4);
        gDpdx = dFdx(p);
        gDpdy = -h * (ddv.xz * dy - d.xz * ddv.y) / (dy * dy);
    }
    vec3 c = F;
    /* above the horizon the reduced-width target holds the horizon colour, not black: the bilinear read at the
     * horizon would blend black into the sea (a dark band following the camera) */
    if (uPass > 0.5 && uPass < 1.5 && !below) { o = vec4(clamp(horizonColor(nd), 0.0, 1.0), 1.0); return; }
    if (uPass > 1.5 && below && uWave4.y > 0.5) {
        /* the sea, drawn at reduced width into a target of exactly its size: the whole target maps to the viewport */
        vec2 sz = vec2(textureSize(uHalfSea, 0));
        c = textureLod(uHalfSea, clamp(vec2(f.x, 1.0 - f.y), 0.5 / sz, 1.0 - 0.5 / sz), 0.0).rgb;
    } else if (!below) {
        c = SK(6) ? horizonColor(nd) : (SK(5) ? skyReflect(nd) : skyColor(nd));
    } else if (uWave4.y > 0.5) {
        /* the sea: a plane at the record's height (GE's water fan, sky.c). The near sea is computed here, live (it moves, and
         * the player's position matters); beyond NEAR_Z0..NEAR_Z1 of depth along the view it is read from the baked
         * panorama: no per-pixel waves out there, and a still image by construction. The blend runs along the camera's
         * depth: its line across the sea is straight (a plane cut by a plane), not a circle around the player */
        vec3 hc = horizonColor(nd);
        float zd = max(dot(vec3(p.x - uEye.x, uSeaY - uEye.y, p.y - uEye.z), normalize(uC0 + uC1 + uC2 + uC3)), 0.0);
        float kf = smoothstep(SV(10) ? 300.0 : (SV(9) ? 600.0 : NEAR_Z0), SV(10) ? 1500.0 : (SV(9) ? 2500.0 : NEAR_Z1), zd);
        if (SV(8)) kf = 1.0;
        if (uBakeH <= 0.0) kf = 0.0;   /* no (settled) bake yet: the whole sea live */
        if (NEAR_Z0 <= 0.0) kf = 0.0;   /* no baked sea */
        vec3 live = vec3(0.0), farc = vec3(0.0);
        if (kf < 1.0) {
            gSeaMode = 1;
#if defined(VIEWER) || defined(VIEWER_CONST)
            gMode = (SV(2) || SV(5)) ? 9 : ((SV(3) || SV(6) || SV(12)) ? 10 : (SV(4) ? 11 : 0));
            gLite = SV(7);
#endif
            vec3 wn = waterNormal(p, t);
            if (SV(5) || SV(6)) {   /* bump: the slope lit from the sunset's side, no reflection */
                vec2 L = length(uSunDir.xz) > 0.01 ? normalize(uSunDir.xz) : vec2(0.6, 0.8);
                live = mix(uSea, horizonColor(nd), 0.35) * clamp(0.85 + 4.0 * dot(wn.xz, L), 0.4, 1.6);
            } else live = waterShade(nd, wn, uSea, t);
#if defined(VIEWER) || defined(VIEWER_CONST)
            gMode = 0; gLite = false;
#endif
        }
        if (kf > 0.0) {
            /* the panorama was baked from another eye position (uBake*). The sea is a plane, so the point under this pixel is
             * looked up as the baked eye saw it: a strafe then slides the far sea with the world, like the live near sea, and
             * the blend between the two shows no shear */
            vec3 rd = uBakeH > 0.0 ? normalize(vec3(p.x - uBakeX, -uBakeH, p.y - uBakeZ)) : nd;
            float pu = atan(rd.x, rd.z) / 6.2831853 + 0.5, pv = min(sqrt(max(-rd.y, 0.0)) / panoMax(h), 1.0);
            farc = textureLod(uSeaPano, vec2(pu, 1.0 - pv), 0.0).rgb;   /* a render target is stored top row first, like uHalfSea */
        }
        vec3 sea = mix(live, farc, kf);
        /* a thin band at the horizon: sea and sky meet in the sky's own horizon
         * colour (GE's water fades the same way, sky.c skyIsCornerInWater's frac) */
        c = mix(hc, sea, smoothstep(0.0, PRM(12, 0.006), -nd.y));
    }
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
