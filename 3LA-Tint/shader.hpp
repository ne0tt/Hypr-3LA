#pragma once

#include <string>

// Full-viewport quad: the viewport is set to the tinted box itself, so `pos`
// (0..1) maps straight to NDC and no projection matrix is needed.
inline const std::string TINT_VERT = R"#(#version 300 es

in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;

void main() {
    v_texcoord  = texcoord;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)#";

// Recolours low-chroma (grey/black/white) pixels toward `tint`:
//  - mask  = 1 - smoothstep(satLo, satHi, chroma), chroma = max(rgb) - min(rgb),
//            so already-coloured pixels are left alone
//  - tinted = HSL(hue(tint), sat(tint), L'), L' = the pixel's own HSL lightness
//            remapped into [lightLo, lightHi] so black/white also pick up colour
//  - surface alpha masks out translucent areas (popup shadows)
//  - a rounded-rect mask (radius/power in px) keeps the window's rounded
//    corners untouched, matching Hyprland's superellipse rounding
inline const std::string TINT_FRAG = R"#(#version 300 es

precision highp float;

in vec2 v_texcoord;
out vec4 fragColor;

uniform sampler2D tex;
uniform sampler2D surfTex; // the surface's own buffer, for its alpha
uniform int   useSurfAlpha;
uniform mat2  surfM;       // box-local panel px -> surface uv:
uniform vec2  surfO;       //   uv = surfM * (p + surfO), handles monitor rotation
uniform vec2  uvScale;  // 1 / copy FB size
uniform vec2  uvOffset; // copied sub-rect origin, px relative to the box
uniform vec2  resolution;
uniform float radius;
uniform float roundingPower;
uniform vec2  tintHS; // hue, saturation of the tint, computed on the CPU
uniform float strength;
uniform float satLo;
uniform float satHi;
uniform float minLuma;
uniform float lightLo;
uniform float lightHi;

vec3 rgb2hsl(vec3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float l  = (mx + mn) * 0.5;
    float d  = mx - mn;
    if (d < 1e-5)
        return vec3(0.0, 0.0, l);
    float s = d / (1.0 - abs(2.0 * l - 1.0));
    float h;
    if (mx == c.r)
        h = mod((c.g - c.b) / d, 6.0);
    else if (mx == c.g)
        h = (c.b - c.r) / d + 2.0;
    else
        h = (c.r - c.g) / d + 4.0;
    return vec3(h / 6.0, s, l);
}

vec3 hsl2rgb(vec3 hsl) {
    vec3 k = mod(vec3(0.0, 8.0, 4.0) + hsl.x * 12.0, 12.0);
    float a = hsl.y * min(hsl.z, 1.0 - hsl.z);
    return hsl.z - a * max(min(min(k - 3.0, 9.0 - k), 1.0), -1.0);
}

float cornerMask(vec2 p) {
    if (radius <= 0.5)
        return 1.0;
    vec2 q = max(abs(p - resolution * 0.5) - (resolution * 0.5 - radius), 0.0);
    if (q.x <= 0.0 && q.y <= 0.0)
        return 1.0;
    float d = pow(pow(q.x, roundingPower) + pow(q.y, roundingPower), 1.0 / roundingPower);
    return 1.0 - smoothstep(radius - 1.0, radius, d);
}

void main() {
    vec2 p   = v_texcoord * resolution;
    vec4 src = texture(tex, (p - uvOffset) * uvScale);
    vec3 c   = src.rgb;

    float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    float luma   = dot(c, vec3(0.2126, 0.7152, 0.0722));

    float mask = 1.0 - smoothstep(satLo, satHi, chroma);
    mask *= smoothstep(minLuma - 0.02, minLuma, luma) * step(0.0001, minLuma) + (1.0 - step(0.0001, minLuma));
    mask *= cornerMask(p) * strength;
    // only where the surface itself is (near-)opaque: a popup's translucent
    // drop shadow would otherwise tint whatever lies behind it
    if (useSurfAlpha != 0)
        mask *= smoothstep(0.5, 1.0, texture(surfTex, surfM * (p + surfO)).a);

    float l = mix(lightLo, lightHi, rgb2hsl(c).z);
    vec3 tinted = hsl2rgb(vec3(tintHS, l));

    fragColor = vec4(mix(c, tinted, clamp(mask, 0.0, 1.0)), src.a);
}
)#";
