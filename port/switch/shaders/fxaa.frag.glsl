#version 460
// D973: gfx_opengl_fxaa_pass's shader with explicit bindings. The pass's
// viewport is the source's size, so GL's gl_FragCoord.xy * uInvRes is vUV (from
// the fullscreen vertex shader): no dependence on gl_FragCoord's orientation.
// The source is stored top-down (deko3d): T() mirrors t. uFlip is unused.
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uTex;
layout(std140, binding = 0) uniform Fxaa {
    vec2 uInvRes;
    vec2 uFlip;
};
layout(location = 0) out vec4 o;
#define T(p) texture(uTex, vec2((p).x, 1.0 - (p).y))
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 uv = vUV;
    vec3 m  = T(uv).rgb;
    vec3 nw = T(uv + vec2(-1.0,  1.0) * uInvRes).rgb;
    vec3 ne = T(uv + vec2( 1.0,  1.0) * uInvRes).rgb;
    vec3 sw = T(uv + vec2(-1.0, -1.0) * uInvRes).rgb;
    vec3 se = T(uv + vec2( 1.0, -1.0) * uInvRes).rgb;
    float lm = luma(m), lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se);
    float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));
    if (lmax - lmin < max(0.0625, lmax * 0.125)) {
        o = vec4(m, 1.0);
        return;
    }
    vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), -((lnw + lsw) - (lne + lse)));
    float reduce = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uInvRes;
    vec3 a = 0.5 * (T(uv + dir * (1.0 / 3.0 - 0.5)).rgb + T(uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (T(uv - dir * 0.5).rgb + T(uv + dir * 0.5).rgb);
    float lb = luma(b);
    o = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);
}
