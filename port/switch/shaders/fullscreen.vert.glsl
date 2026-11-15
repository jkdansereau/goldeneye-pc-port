#version 460
// D973: fullscreen triangle (FXAA, GE+ sky), no vertex buffer. vUV = the
// position in the viewport, 0..1 with y up (GL's (gl_FragCoord - viewport.xy) /
// viewport.zw), so the passes never depend on how gl_FragCoord is oriented.
layout(location = 0) out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
