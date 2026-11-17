#version 460
// D973: deko3d uber-shader, vertex stage. Passes every attribute location
// through; the backend marks the ones a combiner's layout lacks as fixed.
// Locations = DK_ATTR_* (port/fast3d/gfx_dk_layout.h).
layout(location = 0) in vec4 aVtxPos;
layout(location = 1) in vec2 aTexCoord0;
layout(location = 2) in vec2 aTexCoord1;
layout(location = 3) in float aTexClampS0;
layout(location = 4) in float aTexClampT0;
layout(location = 5) in float aTexClampS1;
layout(location = 6) in float aTexClampT1;
layout(location = 7) in vec4 aFog;
layout(location = 8) in vec4 aGrayscaleColor;
layout(location = 9) in vec4 aInput1;
layout(location = 10) in vec4 aInput2;
layout(location = 11) in vec4 aInput3;
layout(location = 12) in vec4 aInput4;
layout(location = 13) in vec4 aInput5;
layout(location = 14) in vec4 aInput6;
layout(location = 15) in vec4 aInput7;

layout(location = 1) out vec2 vTexCoord0;
layout(location = 2) out vec2 vTexCoord1;
layout(location = 3) out float vTexClampS0;
layout(location = 4) out float vTexClampT0;
layout(location = 5) out float vTexClampS1;
layout(location = 6) out float vTexClampT1;
layout(location = 7) out vec4 vFog;
layout(location = 16) out float vFogW;   // fog * w (per-pixel fog, Draw.fog_pixel)
layout(location = 8) out vec4 vGrayscaleColor;
layout(location = 9) out vec4 vInput1;
layout(location = 10) out vec4 vInput2;
layout(location = 11) out vec4 vInput3;
layout(location = 12) out vec4 vInput4;
layout(location = 13) out vec4 vInput5;
layout(location = 14) out vec4 vInput6;
layout(location = 15) out vec4 vInput7;

void main() {
    vTexCoord0 = aTexCoord0;
    vTexCoord1 = aTexCoord1;
    vTexClampS0 = aTexClampS0;
    vTexClampT0 = aTexClampT0;
    vTexClampS1 = aTexClampS1;
    vTexClampT1 = aTexClampT1;
    vFog = aFog;
    vFogW = aFog.a;
    vGrayscaleColor = aGrayscaleColor;
    vInput1 = aInput1;
    vInput2 = aInput2;
    vInput3 = aInput3;
    vInput4 = aInput4;
    vInput5 = aInput5;
    vInput6 = aInput6;
    vInput7 = aInput7;
    gl_Position = aVtxPos;
}
