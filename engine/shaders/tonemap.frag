#version 450

// The only pass that writes to the screen.
//
// The world is rendered into a floating-point image where brightness may go far
// above 1.0 - a torch really is brighter than white paper. A monitor cannot show
// that, so a tone mapping curve squashes the range back down. Which curve is a
// taste decision, which is why there are four of them and a setting.
//
// The swapchain is already an _SRGB format, so the hardware converts what is
// written here from linear to sRGB. Do **not** also raise anything to 1/2.2 -
// that encodes twice and washes the whole image out.

#include "post_common.glsl"

layout(location = 0) in vec2 fragUv;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

layout(location = 0) out vec4 outColor;

/// Khronos PBR Neutral - the default, and chosen over ACES deliberately.
///
/// Any base colour below sRGB 231 comes out of it unchanged under even white
/// light, and it shifts no hues anywhere. ACES turns saturated red toward
/// yellow, green toward cyan and blue toward magenta as they brighten, and a
/// blocky palette is made almost entirely of saturated near-primaries the player
/// already knows the colour of. Mojang shipped ACES and then built their own
/// curve to avoid it.
vec3 toneMapPbrNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

/// Hable's Uncharted 2 filmic curve. What GTA V and DOOM 2016 shipped.
vec3 hableCurve(vec3 x) {
    const float a = 0.15;
    const float b = 0.50;
    const float c = 0.10;
    const float d = 0.20;
    const float e = 0.02;
    const float f = 0.30;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

vec3 toneMapHable(vec3 color) {
    const float whitePoint = 11.2;
    return hableCurve(color * 2.0) / hableCurve(vec3(whitePoint));
}

/// Reinhard on luminance only, so hue and saturation survive. What Mojang's own
/// sample resource pack uses.
vec3 toneMapReinhardLuminance(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if (luminance <= 0.0) {
        return color;
    }
    return color * ((luminance / (1.0 + luminance)) / luminance);
}

/// Narkowicz's fit of the ACES filmic curve. Here to be compared against, not
/// as a default - see the note on PBR Neutral above.
vec3 toneMapAces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/// Everything this pass does to one pixel, as a function, because the
/// anti-aliaser has to compare neighbours **after** the curve rather than
/// before it.
vec3 graded(vec2 uv) {
    vec3 color = texture(sceneColor, uv).rgb;

    // A lerp, not an add. Adding brightens the entire image and is very hard to
    // balance in a dark scene; mixing toward a blurred copy of the same image
    // changes nothing where the picture is already flat and only shows where
    // there is contrast - which is what light bleeding actually looks like.
    float bloomStrength = post.imageParams.z;
    if (bloomStrength > 0.0) {
        color = mix(color, texture(bloomTexture, uv).rgb, bloomStrength);
    }

    color *= post.imageParams.x;

    int curve = int(post.imageParams.y + 0.5);
    if (curve == 1) {
        color = toneMapHable(color);
    } else if (curve == 2) {
        color = toneMapReinhardLuminance(color);
    } else if (curve == 3) {
        color = toneMapAces(color);
    } else {
        color = toneMapPbrNeutral(color);
    }
    return max(color, vec3(0.0));
}

/// A cheap edge-directed blur, run **after** the tone curve.
///
/// Anti-aliasing has to happen where the image is display-referred: a bright
/// pixel next to a dark one is a 40:1 ratio in HDR and about 2:1 after the
/// curve, so blending before the curve is dominated by whichever neighbour is
/// brightest and barely moves the edge at all.
///
/// **Deliberately weak, and off by default.** Everything in this game is a
/// hard-edged square, and an anti-aliaser strong enough to soften a diagonal
/// also softens every texel boundary in the world - which is the look, not an
/// artefact.
vec3 antiAlias(vec2 uv, vec3 centre, float amount) {
    vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));

    vec3 up = graded(uv + vec2(0.0, -texel.y));
    vec3 down = graded(uv + vec2(0.0, texel.y));
    vec3 left = graded(uv + vec2(-texel.x, 0.0));
    vec3 right = graded(uv + vec2(texel.x, 0.0));

    const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);
    float m = dot(centre, kLuma);
    float n = dot(up, kLuma);
    float s = dot(down, kLuma);
    float w = dot(left, kLuma);
    float e = dot(right, kLuma);

    float highest = max(m, max(max(n, s), max(w, e)));
    float lowest = min(m, min(min(n, s), min(w, e)));
    float range = highest - lowest;
    // Flat enough that a blend could only be blurring the artwork.
    if (range < max(0.06, highest * 0.14)) {
        return centre;
    }

    // Blended **along** the edge, never across it: the vertical pair differing
    // more than the horizontal one means the edge runs horizontally.
    bool vertical = abs(n - s) >= abs(w - e);
    vec2 along = vertical ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec3 blended = 0.5 * (graded(uv + along * texel * 0.5) + graded(uv - along * texel * 0.5));
    return mix(centre, blended, amount * clamp(range * 2.0, 0.0, 1.0));
}

void main() {
    vec3 color = graded(fragUv);

    float smoothing = post.filterParams.x;
    if (smoothing > 0.0) {
        color = antiAlias(fragUv, color, smoothing);
    }

    outColor = vec4(color, 1.0);
}
