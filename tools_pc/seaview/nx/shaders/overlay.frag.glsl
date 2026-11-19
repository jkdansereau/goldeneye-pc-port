#version 460
// seaview-nx: the on-screen counter. A small text image (white on black, top row first) drawn in a
// rectangle of the screen (vUV units, y up).
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uText;
layout(std140, binding = 0) uniform Ov { vec4 uRect; };
layout(location = 0) out vec4 o;
void main() {
    vec2 t = (vUV - uRect.xy) / uRect.zw;
    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) discard;
    o = vec4(texture(uText, vec2(t.x, 1.0 - t.y)).rgb, 1.0);
}
