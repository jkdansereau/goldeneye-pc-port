#version 460
// D973: GE+ sky (gfx_opengl.cpp skyplus_build's fragment shader) with explicit
// bindings; the uniforms are packed into vec4s (struct DkSkyUniforms in
// gfx_deko3d.cpp). The position in the viewport comes from the fullscreen
// vertex shader (vUV = GL's (gl_FragCoord.xy - uVP.xy) / uVP.zw), never from
// gl_FragCoord, whose orientation differs between targets / Ryujinx / hardware.
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uTex;
layout(std140, binding = 0) uniform Sky {
    vec4 uVP;        /* GL viewport */
    vec4 uAsp4;      /* xy: x_out = uAsp.x * x_game + uAsp.y; zw unused */
    vec4 uC0_, uC1_, uC2_, uC3_;   /* image-plane rays TL TR BL BR, world */
    vec4 uEye4;      /* xyz eye, w cloud height */
    vec4 uSky4;      /* xyz sky, w gain */
    vec4 uCloud4;    /* xyz cloud, w texture scale */
    vec4 uOfs01;     /* layer 0 xy, layer 1 xy */
    vec4 uOfs2X;     /* layer 2 xy, z sun glow, w night */
    vec4 uSunDir4;
    vec4 uSunCol4;
    vec4 uMoonDir4;
};
#define uAsp uAsp4.xy
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
#define PLANET_R 300000.0
vec2 ofsOf(int i) { return i == 0 ? uOfs01.xy : (i == 1 ? uOfs01.zw : uOfs2X.xy); }
layout(location = 0) out vec4 o;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 f = vUV;                             /* 0..1, y up */
    float xg = ((f.x * 2.0 - 1.0) - uAsp.y) / uAsp.x;       /* back to the game's screen x */
    float u = xg * 0.5 + 0.5, v = 1.0 - f.y;                /* v = 0 at the top of the screen */
    vec3 d = mix(mix(uC0, uC1, u), mix(uC2, uC3, u), v);
    float hz = length(d.xz) + 1e-4;
    float e = max(d.y / length(d), 0.0);
    vec3 F = uSky;
    /* under the horizon: GE's clear colour, chosen at the single write below.
     * No early return: Ryujinx ran the cloud code under the horizon anyway, so
     * the pixels there got clouds with a negative shade (saturated blue) or
     * the previous frame (Cradle, looking down from the cradle). */
    bool below = !(d.y > 0.0);

    /* 1. sky behind everything: horizon = GE's colour, zenith a little deeper */
    vec3 c = mix(F, F * 0.82, smoothstep(0.0, 0.8, e));
    vec3 nd = normalize(d);
    if (uSunGlow > 0.0) {
        float cs = dot(nd, uSunDir);
        float sunAmt = max(cs, 0.0);
        c += uSunCol * (pow(sunAmt, 5.0) * 0.5 + pow(sunAmt, 48.0) * 0.7) * uSunGlow;
        if (cs > 0.9995) c = max(c, vec3(1.3));
    }
    float star = 0.0;
    if (uNight > 0.5 && e > 0.03) {
        vec2 cell = floor(vec2(atan(nd.x, nd.z), asin(e)) * 400.0);
        float h = hash(cell);
        if (h > 0.997) star = (h - 0.997) / 0.003 * smoothstep(0.03, 0.3, e) * 0.8;
    }

    /* 2. cloud layers, highest first, over the sky; layer 1 = GE's */
    vec3 ld = uSunGlow > 0.0 ? uSunDir : (uNight > 0.5 ? uMoonDir : vec3(0.0, 1.0, 0.0));
    float fr = 1.0 - min(1.0, 2.0 * d.y / hz) * uGain;
    vec3 shade = F + uCloud * (1.0 - F) * (1.0 - fr);
    float H = uCloudY - uEye.y;
    float cover = 0.0;
    if (H > 0.0) {
        for (int i = 0; i < 3; i++) {
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
            vec2 t = vec2(w.x * cr - w.y * sr, w.x * sr + w.y * cr) / kSc + ofsOf(i);
            float I = texture(uTex, t / 64.0).r;
            if (kK > 0.0) I = clamp((I - kK) / (1.0 - kK), 0.0, 1.0);
            float I2 = texture(uTex, (t + ld.xz * 1.5) / 64.0).r;
            float lit = 1.0 + clamp((I2 - I) * 2.5, -0.35, 0.35) * (i == 1 ? 0.6 : 1.0);
            /* haze: clouds dissolve into the horizon colour just above it (GE's own
             * fade only reaches the shade, not the opacity) */
            float haze = smoothstep(0.0, 0.08, e);
            float a = I * kOp * haze;
            cover = 1.0 - (1.0 - cover) * (1.0 - min(1.0, a * 3.0));
            /* light on the cloud's own colour only, never on the sky part of the shade */
            vec3 cc = min(vec3(1.1), F + (shade - F) * lit);
            if (uSunGlow > 0.0) cc += uSunCol * pow(max(dot(nd, uSunDir), 0.0), 12.0) * (1.0 - I) * 0.6 * haze;   /* backlit edges */
            c = mix(c, cc, a);
        }
    }
    /* 3. the sun's halo shines through: a tight core and a wide bloom, dimmed by the clouds */
    if (uSunGlow > 0.0) {
        float sa = max(dot(nd, uSunDir), 0.0);
        c += uSunCol * (pow(sa, 400.0) * 1.2 + pow(sa, 60.0) * 0.45 + pow(sa, 8.0) * 0.12) * (1.0 - 0.6 * cover);   /* the halo pierces the fog */
    }
    /* 4. stars and moon only where the clouds let them through */
    c += star * (1.0 - cover);
    if (uNight > 0.5) {
        float cs = dot(nd, uMoonDir);
        float gl = pow(max(cs, 0.0), 40.0) * 0.08 * (1.0 - cover * 0.5);
        c = cs > 0.9997 ? mix(c + gl, vec3(0.85), 1.0 - cover) : c + gl;
    }
    /* 5. horizon band: everything fades to the fog colour before the horizon,
     * fully below ~2.3 deg, gone above ~12.7 deg, so whatever the level draws
     * far away (trees popping in at the draw distance) meets a flat fog colour,
     * as with GE's plane, and never cuts into the sun */
    c = mix(F, c, smoothstep(0.04, 0.22, e));
    o = below ? vec4(F, 1.0) : vec4(clamp(c, 0.0, 1.0), 1.0);
}
