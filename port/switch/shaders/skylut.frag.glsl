#version 460
// GE+ reflection sky, drawn once per frame into a small panorama (SKYLUT_W x
// SKYLUT_H, gfx_deko3d.cpp): u = the azimuth of a direction, v = sqrt of its
// elevation (finer near the horizon). The sea reads it (skyReflect in
// skycommon.glsl) instead of computing the sky for every pixel of water, and
// a low-resolution sky changes smoothly from pixel to pixel, so reflections
// hold still when the view moves.
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uCloudTex;
layout(binding = 1) uniform sampler2D uWaveTex;   /* only skycommon's wave code names it */
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
layout(location = 0) out vec4 o;

#pragma skycommon

void main() {
    float az = (vUV.x - 0.5) * 6.2831853;
    float y = vUV.y * vUV.y;
    float h = sqrt(max(1.0 - y * y, 0.0));
    vec3 nd = vec3(h * sin(az), y, h * cos(az));
    gReflect = true;   /* the reflected sky: GE's cloud layer only, held still */
    o = vec4(clamp(skyColor(nd), 0.0, 1.0), 1.0);
}
