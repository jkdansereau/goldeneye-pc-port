#version 460
// GE+ water on the level's own water surfaces (Dam, Caverns, Complex): the
// draws of GE's water combiner (unk_092E50.c MipMap2C_Something*_Setup:
// lerp(TEXEL0, TEXEL1, PRIM_LOD_FRAC) * SHADE, two cycles, opaque), tagged
// SHADER_OPT_WATER by fast3d. Paired with the uber vertex shader (same
// locations; vClip = the clip position). With the water off (uWave4.z == 0:
// no GE+ sky this frame, Visuals = Original) the result is GE's combiner
// exactly, as the uber-shader computes it.
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 7) in vec4 vFog;
layout(location = 16) in float vFogW;
layout(location = 9) in vec4 vInput1;    /* PRIM_LOD_FRAC */
layout(location = 10) in vec4 vInput2;   /* SHADE */
layout(location = 17) in vec3 vClip;     /* clip x, y, w */

layout(binding = 0) uniform sampler2D uTex0;
layout(binding = 1) uniform sampler2D uTex1;
layout(binding = 2) uniform sampler2D uCloudTex;
layout(binding = 3) uniform sampler2D uWaveTex;   /* the wave heights (skyplus.c's fbm) */

layout(std140, binding = 0) uniform Draw {
    int frame_count;
    float noise_scale;
    int three_point_filter0;
    int three_point_filter1;
    int flip_v0;
    int flip_v1;
    int fog_pixel;
    int zpass;
};

layout(std140, binding = 1) uniform Comb {
    uvec4 sel;
    uint flags;
    uint flags2;
};
#define CF_FOG (1u << 1)

layout(std140, binding = 2) uniform Sky {
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


layout(location = 0) out vec4 outColor;

#pragma skycommon

#define TEX_OFFSET(tex, uv, texSize, off) texture(tex, uv - (off)/texSize)
vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
    vec2 offset = fract(texCoord*texSize - vec2(0.5));
    offset -= step(1.0, offset.x + offset.y);
    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);
    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));
    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));
    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
}

void main() {
    vec2 uv0 = vTexCoord0, uv1 = vTexCoord1;
    if (flip_v0 != 0) uv0.t = 1.0 - uv0.t;
    if (flip_v1 != 0) uv1.t = 1.0 - uv1.t;
    vec4 t0 = three_point_filter0 != 0 ? filter3point(uTex0, uv0, vec2(textureSize(uTex0, 0))) : texture(uTex0, uv0);
    vec4 t1 = three_point_filter1 != 0 ? filter3point(uTex1, uv1, vec2(textureSize(uTex1, 0))) : texture(uTex1, uv1);
    vec4 lod = clamp(vInput1, 0.0, 1.0);
    vec4 shade = clamp(vInput2, 0.0, 1.0);
    /* GE's combiner (uber.frag: the same formula, wraps and clamp) */
    vec3 ge = (t1.rgb - t0.rgb) * lod.rgb + t0.rgb;
    ge = mod(ge + 1.01, 2.02) - 1.01;
    ge = ge * shade.rgb;
    ge = clamp(mod(ge + 0.51, 2.02) - 0.51, 0.0, 1.0);
    vec3 c = ge;

    /* the pixel's world ray from its screen position (the sky's corner rays),
     * computed in uniform control flow (the texture reads need its derivatives) */
    float wz = max(vClip.z, 1e-4);
    vec2 ndc = vClip.xy / wz;
    if (uAsp4.z > 0.5) ndc.y = -ndc.y;
    float xg = (ndc.x - uAsp.y) / uAsp.x;
    vec3 d = skyRay(xg * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    /* d is on the image plane (1 along the view axis), w = the view depth */
    vec3 p = uEye + d * wz;
    gDpdx = dFdx(p.xz);
    gDpdy = dFdy(p.xz);
    if (uWave4.z > 0.5 && vClip.z > 0.0) {
        float dist = length(d) * vClip.z;
        vec3 nd = normalize(d);
        vec3 n = waterNormal(p.xz, dist);
        /* the body is GE's own water (texture x vertex colour); the reflection
         * and glints are dimmed by the vertex colour, so dark water stays dark
         * (Complex's pits are lit black) */
        float lit = clamp(max(max(shade.r, shade.g), shade.b) * 1.4, 0.0, 1.0);
        vec3 w = waterShade(nd, n, ge * uSea, dist);   /* uSea: the level's tint (no sea on these levels) */
        c = mix(ge, w, lit);
    }
    if ((flags & CF_FOG) != 0u) {
        float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);
        c = mix(c, vFog.rgb, fogA);
    }
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
