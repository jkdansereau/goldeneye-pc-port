#version 460
// D973: deko3d uber-shader, fragment stage: any combiner, chosen by the Comb
// block (struct DkCombinerParams, port/fast3d/gfx_dk_layout.h). Same result as
// the generated per-combiner shader (tools_pc/dkshgen, itself a copy of
// gfx_opengl_gen_shader_src): the single / multiply / mix shortcuts are the
// same (a - b) * c + d, and with color_alpha_same the alpha of the vec4 formula
// is the alpha formula. Used for ids missing from the shipped pack (each one is
// logged once as "DKSH miss"). Sources are picked with a switch per slot, never
// a dynamically indexed local array (Maxwell would spill it to local memory).
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 3) in float vTexClampS0;
layout(location = 4) in float vTexClampT0;
layout(location = 5) in float vTexClampS1;
layout(location = 6) in float vTexClampT1;
layout(location = 7) noperspective in vec4 vFog;
layout(location = 8) in vec4 vGrayscaleColor;
layout(location = 9) noperspective in vec4 vInput1;
layout(location = 10) noperspective in vec4 vInput2;
layout(location = 11) noperspective in vec4 vInput3;
layout(location = 12) noperspective in vec4 vInput4;
layout(location = 13) noperspective in vec4 vInput5;
layout(location = 14) noperspective in vec4 vInput6;
layout(location = 15) noperspective in vec4 vInput7;

layout(binding = 0) uniform sampler2D uTex0;
layout(binding = 1) uniform sampler2D uTex1;

layout(std140, binding = 0) uniform Draw {
    int frame_count;
    float noise_scale;
    int three_point_filter0;
    int three_point_filter1;
    int flip_v0;
    int flip_v1;
};

layout(std140, binding = 1) uniform Comb {
    uvec4 sel;      // cycle 0 rgb, cycle 0 alpha, cycle 1 rgb, cycle 1 alpha: a | b << 8 | c << 16 | d << 24
    uint flags;     // DK_CF_*
    uint flags2;
};

#define CF_ALPHA     (1u << 0)
#define CF_FOG       (1u << 1)
#define CF_TEXEDGE   (1u << 2)
#define CF_NOISE     (1u << 3)
#define CF_2CYC      (1u << 4)
#define CF_ATHRESH   (1u << 5)
#define CF_INVISIBLE (1u << 6)
#define CF_GRAYSCALE (1u << 7)
#define CF_BLUR      (1u << 8)
#define CF_TEX0      (1u << 9)
#define CF_TEX1      (1u << 10)
#define CF_CLAMP_S0  (1u << 11)
#define CF_CLAMP_T0  (1u << 12)
#define CF_CLAMP_S1  (1u << 13)
#define CF_CLAMP_T1  (1u << 14)

#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)
#define TEX_OFFSET(tex, uv, texSize, off) texture(tex, uv - (off)/texSize)

layout(location = 0) out vec4 outColor;

float random(in vec3 value) {
    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));
    return fract(sin(random) * 143758.5453);
}

vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
    vec2 offset = fract(texCoord*texSize - vec2(0.5));
    offset -= step(1.0, offset.x + offset.y);
    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);
    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));
    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));
    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
}

vec4 sample1(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {
    return three_point_filter != 0 ? filter3point(t, uv, texSize) : texture(t, uv);
}

vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {
    if ((flags & CF_BLUR) == 0u) {
        return sample1(t, uv, texSize, three_point_filter);
    }
    vec4 cw = vec4(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 xy = vec2(float(i & 3), float(i >> 2));
        float w = 0.009947 - length(xy) * 0.001;
        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;
        vec4 tex = sample1(t, scaled_uv, texSize, three_point_filter);
        cw += vec4(tex.rgb * w, w);
    }
    return vec4(cw.rgb / cw.a, 1.0);
}

vec4 cIn1, cIn2, cIn3, cIn4, cIn5, cIn6, cIn7;
vec4 texVal0, texVal1;
vec4 texel;
float noiseVal;

vec3 srcRgb(uint s) {
    switch (s) {
        case 1u: return cIn1.rgb;
        case 2u: return cIn2.rgb;
        case 3u: return cIn3.rgb;
        case 4u: return cIn4.rgb;
        case 5u: return cIn5.rgb;
        case 6u: return cIn6.rgb;
        case 7u: return cIn7.rgb;
        case 8u: return texVal0.rgb;
        case 9u: return vec3(texVal0.a);
        case 10u: return texVal1.rgb;
        case 11u: return vec3(texVal1.a);
        case 12u: return vec3(1.0);
        case 13u: return texel.rgb;
        case 14u: return vec3(noiseVal);
    }
    return vec3(0.0);   // SHADER_0, LOD_FRAC (GL has no GLSL for it either)
}

float srcA(uint s) {
    switch (s) {
        case 1u: return cIn1.a;
        case 2u: return cIn2.a;
        case 3u: return cIn3.a;
        case 4u: return cIn4.a;
        case 5u: return cIn5.a;
        case 6u: return cIn6.a;
        case 7u: return cIn7.a;
        case 8u: return texVal0.a;
        case 9u: return texVal0.a;
        case 10u: return texVal1.a;
        case 11u: return texVal1.a;
        case 12u: return 1.0;
        case 13u: return texel.a;
        case 14u: return noiseVal;
    }
    return 0.0;
}

vec3 formulaRgb(uint s) {
    return (srcRgb(s & 0xffu) - srcRgb((s >> 8) & 0xffu)) * srcRgb((s >> 16) & 0xffu) + srcRgb(s >> 24);
}

float formulaA(uint s) {
    return (srcA(s & 0xffu) - srcA((s >> 8) & 0xffu)) * srcA((s >> 16) & 0xffu) + srcA(s >> 24);
}

void main() {
    const bool A = (flags & CF_ALPHA) != 0u;
    cIn1 = clamp(vInput1, 0.0, 1.0);
    cIn2 = clamp(vInput2, 0.0, 1.0);
    cIn3 = clamp(vInput3, 0.0, 1.0);
    cIn4 = clamp(vInput4, 0.0, 1.0);
    cIn5 = clamp(vInput5, 0.0, 1.0);
    cIn6 = clamp(vInput6, 0.0, 1.0);
    cIn7 = clamp(vInput7, 0.0, 1.0);
    noiseVal = (random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0;

    texVal0 = vec4(0.0);
    texVal1 = vec4(0.0);
    if ((flags & CF_TEX0) != 0u) {
        vec2 texSize0 = vec2(textureSize(uTex0, 0));
        vec2 uv = vTexCoord0;
        if ((flags & CF_CLAMP_S0) != 0u) uv.s = clamp(uv.s, 0.5 / texSize0.s, vTexClampS0);
        if ((flags & CF_CLAMP_T0) != 0u) uv.t = clamp(uv.t, 0.5 / texSize0.t, vTexClampT0);
        if (flip_v0 != 0) uv.t = 1.0 - uv.t;
        texVal0 = hookTexture2D(uTex0, uv, texSize0, three_point_filter0);
    }
    if ((flags & CF_TEX1) != 0u) {
        vec2 texSize1 = vec2(textureSize(uTex1, 0));
        vec2 uv = vTexCoord1;
        if ((flags & CF_CLAMP_S1) != 0u) uv.s = clamp(uv.s, 0.5 / texSize1.s, vTexClampS1);
        if ((flags & CF_CLAMP_T1) != 0u) uv.t = clamp(uv.t, 0.5 / texSize1.t, vTexClampT1);
        if (flip_v1 != 0) uv.t = 1.0 - uv.t;
        texVal1 = hookTexture2D(uTex1, uv, texSize1, three_point_filter1);
    }

    texel = vec4(0.0);
    texel = vec4(formulaRgb(sel.x), A ? formulaA(sel.y) : 1.0);
    texel = WRAP(texel, -1.01, 1.01);
    if ((flags & CF_2CYC) != 0u) {
        texel = vec4(formulaRgb(sel.z), A ? formulaA(sel.w) : 1.0);
    }
    texel = WRAP(texel, -0.51, 1.51);
    texel = clamp(texel, 0.0, 1.0);
    if ((flags & CF_FOG) != 0u) {
        float fogA = clamp(vFog.a, 0.0, 1.0);
        texel.rgb = mix(texel.rgb, vFog.rgb, fogA);
    }
    if (A && (flags & CF_TEXEDGE) != 0u) {
        if ((flags & CF_NOISE) != 0u) {
            if (texel.a < noiseVal) discard;
        } else if ((flags & CF_ATHRESH) != 0u) {
            if (texel.a < 1.0 / 256.0) discard;
        } else {
            if (texel.a < 0.5) discard;
        }
    }
    if (A && (flags & CF_NOISE) != 0u && (flags & CF_TEXEDGE) == 0u) {
        texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + texel.a, 0.0, 1.0));
    }
    if ((flags & CF_GRAYSCALE) != 0u) {
        float intensity = (texel.r + texel.g + texel.b) / 3.0;
        vec3 new_texel = vGrayscaleColor.rgb * intensity;
        texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);
    }
    if (A) {
        if ((flags & CF_ATHRESH) != 0u && texel.a < 8.0 / 256.0) discard;
        if ((flags & CF_INVISIBLE) != 0u) texel.a = 0.0;
        outColor = texel;
    } else {
        outColor = vec4(texel.rgb, 1.0);
    }
}
