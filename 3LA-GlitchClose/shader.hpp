#pragma once

#include <string>

// Rendered as a full-viewport quad into the plugin's own framebuffer, so the
// vertex stage emits NDC straight from the 0..1 `pos` attribute — no projection
// matrix, and none of Hyprland's private matrix state is needed.
inline const std::string GLITCH_VERT = R"#(#version 300 es

in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;

void main() {
    v_texcoord  = texcoord;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)#";

// CCTV/VHS signal collapse, entirely per-pixel: time-stepped slice tearing,
// macroblock corruption, vertical melt, a rolling bright bar, per-channel
// chromatic aberration, digital static, scanlines, backdrop collapse, vignette.
//
// UV convention: v_texcoord.y == 0 is the TOP of the window and y runs DOWN,
// x runs left to right across it, both in the monitor's LOGICAL orientation --
// so every tear, slice and melt below is authored in screen space and stays
// that way whatever transform the monitor is on.
//
// uvOffset/uvXf map that local uv onto the snapshot texture: makeSnapshotFB
// hands back a monitor-sized framebuffer rendered through the monitor's own
// projection, so on a rotated (portrait) monitor the window's pixels sit
// ROTATED inside it. uvXf is the full 2x2 of that mapping -- rotation and flips
// included -- not just a scale, so the glitch keeps tearing across the window
// instead of down it. The framebuffer itself is stored top-down, matching the
// uv convention above, so no y flip is involved.
inline const std::string GLITCH_FRAG = R"#(#version 300 es

precision highp float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform sampler2D tex;
uniform float hasTex;       // 0.0 = capture failed, synthesise everything

uniform vec2  uvOffset;     // local uv (0,0) in `tex`
uniform mat2  uvXf;         // local uv basis in `tex`, carries the monitor transform
uniform vec2  resolution;   // this framebuffer, in px

uniform float progress;     // 0..1 over the burst, keeps climbing in the fade tail
uniform float env;          // 1 during the burst, ramps to 0 across the fade
uniform float seed;

uniform float strength;
uniform float aberration;
uniform float blocks;
uniform float noiseAmount;
uniform float scanlines;
uniform float roll;         // rolling bright bar only
uniform float melt;         // wavy vertical tear boundary
uniform float tear;         // v-sync style frame tear
uniform float tearSpeed;    // seam sweeps per burst
uniform float ghost;        // whole-frame echo copies
uniform float vignette;
uniform float backdropAlpha;

uniform vec3  colBackdrop;
uniform vec3  colFringe1;
uniform vec3  colFringe2;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7)) + seed) * 43758.5453123);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

// samples the captured window. outside the window rect it returns nothing, so a
// displaced band tears a hole through to the desktop instead of smearing an
// edge texel sideways.
vec4 sampleSrc(vec2 uv) {
    if (hasTex < 0.5)
        return vec4(0.0);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return vec4(0.0);
    return texture(tex, uvOffset + uvXf * uv);
}

void main() {
    vec2  uv = v_texcoord;
    float P  = clamp(progress, 0.0, 1.0);

    // eases in, spikes as the feed dies
    float S = strength * clamp(0.2 + P * P * 1.6, 0.0, 1.8);

    // geometry holds for a step at a time so bands visibly tear rather than
    // shimmering at the refresh rate
    float STEP = floor(progress * 16.0);

    // 0) v-sync frame tear: one hard horizontal seam sweeping down the window,
    //    everything under it shifted sideways as if that half of the frame
    //    arrived late. uv.y == 0 is the bottom, so seam counts down from 1.
    //    The shifted half is also evaluated one STEP out of date, which is what
    //    makes the seam read as a frame boundary instead of a plain offset.
    float sweep  = progress * max(tearSpeed, 0.0) + seed;
    float seam   = 1.0 - fract(sweep);
    float below  = step(uv.y, seam) * step(0.001, tear);
    float tearDx = (hash21(vec2(floor(sweep), 91.0)) - 0.5) * 0.22 * tear * S;
    float TSTEP  = STEP + below;

    // 1) horizontal slice displacement
    float bandCount = mix(8.0, 48.0, P);
    float band      = floor(uv.y * bandCount);
    float slice     = 0.0;
    if (hash21(vec2(band, TSTEP)) > 1.0 - 0.55 * blocks)
        slice = (hash21(vec2(band, TSTEP + 7.0)) - 0.5) * 0.35 * blocks * S;

    // 2) macroblock corruption
    vec2  blockId  = floor(uv * vec2(14.0, 9.0));
    vec2  blockOff = vec2(0.0);
    if (hash21(blockId + TSTEP * 3.7) > 1.0 - 0.30 * blocks * P)
        blockOff = (vec2(hash21(blockId + 11.0), hash21(blockId + 23.0)) - 0.5) * 0.12 * blocks * S;

    // 3) rolling bright bar (roll) and the wavy vertical tear boundary (melt).
    //    Independent dials: melt is what makes the torn edge ripple across x
    //    rather than shear flat, so it can be removed without losing the bar.
    float rollBar = smoothstep(0.10, 0.0, abs(uv.y - fract(progress * 1.7 + seed))) * roll;
    float meltAmt = vnoise(vec2(uv.x * 22.0, STEP)) * 0.06 * melt * S;

    vec2  duv = uv + vec2(slice + rollBar * 0.05 * S + below * tearDx, meltAmt) + blockOff;

    // 4) per-channel chromatic aberration
    float ab  = aberration * (0.004 + 0.028 * P) * S + rollBar * 0.01;
    vec4  cr  = sampleSrc(duv + vec2( ab, 0.0));
    vec4  cg  = sampleSrc(duv);
    vec4  cb  = sampleSrc(duv + vec2(-ab, 0.0));

    float a   = max(max(cr.a, cg.a), cb.a);
    // framebuffer textures are premultiplied; work straight, re-premultiply at the end
    vec3  rgb = a > 0.001 ? vec3(cr.r, cg.g, cb.b) / a : vec3(0.0);

    // 5) ghost copies: the whole frame echoed sideways at low alpha, added
    //    rather than blended so overlaps brighten. This is what makes a torn
    //    frame read as a DOUBLED SIGNAL instead of merely displaced strips, and
    //    is the single most recognisable part of the look.
    if (ghost > 0.001) {
        float gdx = (0.004 + 0.026 * P) * ghost * S;
        vec4  g1  = sampleSrc(duv + vec2(gdx, 0.0));
        vec4  g2  = sampleSrc(duv - vec2(gdx * 1.4, 0.0));
        vec3  s1  = g1.a > 0.001 ? g1.rgb / g1.a : vec3(0.0);
        vec3  s2  = g2.a > 0.001 ? g2.rgb / g2.a : vec3(0.0);
        rgb += (s1 * 0.22 + s2 * 0.16) * ghost;
        a    = max(a, max(g1.a * 0.22, g2.a * 0.16) * ghost);
    }

    // without a capture there is nothing to tear, so the static and backdrop
    // start immediately instead of ramping in over the window's own pixels
    float bdOn = mix(0.02, 0.35, hasTex);
    float stOn = mix(0.00, 0.25, hasTex);

    // 6) fringe tint on the displaced bands
    float displaced = clamp((abs(slice) + abs(blockOff.x) + abs(blockOff.y)) * 24.0, 0.0, 1.0);
    vec3  fringe    = mix(colFringe1, colFringe2, hash21(vec2(band, TSTEP + 3.0)));
    rgb = mix(rgb, rgb * 0.4 + fringe * 0.8, displaced * 0.55);
    rgb += rollBar * 0.35;

    // 7) backdrop collapse
    float bd = smoothstep(bdOn, min(bdOn + 0.6, 0.95), P) * backdropAlpha;
    rgb = mix(rgb, colBackdrop, bd);
    a   = max(a, bd);

    // 8) digital static
    float staticRamp = smoothstep(stOn, min(stOn + 0.75, 1.0), P);
    float grain      = hash21(floor(uv * resolution * 0.5) + TSTEP * 13.0);
    float nAmt       = noiseAmount * (0.12 + 0.9 * staticRamp);
    rgb = mix(rgb, vec3(grain), nAmt);
    a   = max(a, nAmt * staticRamp);

    // 9) scanlines — 2px on, 2px off, kept off trig to dodge precision loss at
    //    large arguments on hidpi framebuffers
    rgb *= 1.0 - scanlines * 0.45 * step(0.5, fract(uv.y * resolution.y * 0.25));

    // 10) vignette, tightening as the feed collapses
    vec2  vc = (uv - 0.5) * 2.0;
    rgb *= 1.0 - vignette * smoothstep(0.5, 1.6, dot(vc, vc)) * (0.3 + 0.7 * P);

    a         = clamp(a, 0.0, 1.0) * env;
    fragColor = vec4(clamp(rgb, 0.0, 1.0) * a, a);
}
)#";
