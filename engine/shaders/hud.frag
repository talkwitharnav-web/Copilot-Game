#version 450

// The HUD's own shader. It samples a sheet, tints it and stops.
//
// It exists to get screen geometry out of the world pipeline and, with it, away
// from the depth buffer. A blended fragment still *writes* depth, so every
// transparent texel of every HUD quad used to reject whatever was drawn later
// behind it - which cost three shipped bugs in two days and a fourth nobody had
// noticed. The UI pass has no depth attachment at all now, and order alone
// decides what covers what.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;
layout(set = 0, binding = 1) uniform sampler2DArray hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray fontTexture;

#include "frame.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    // Kept, even though only the world really needs it: an inventory icon of a
    // water or fire block would otherwise freeze on the frame it was built with.
    float layer = fragLayer;
    if (abs(layer - frame.animation.x) < 0.25) {
        layer = frame.animation.y;
    } else if (abs(layer - frame.animation.z) < 0.25) {
        layer = frame.animation.w;
    }

    // Solid-colour HUD geometry samples the *block* array, at
    // TextureLayer::White. Reordering that list moves the whole HUD onto
    // whatever lands on that index.
    vec4 texel;
    if (layer < -1.5) {
        texel = texture(fontTexture, vec3(fragUv, 0.0));
    } else if (layer < 0.0) {
        texel = texture(hudTexture, vec3(fragUv, 0.0));
    } else {
        texel = texture(blockTextures, vec3(fragUv, layer));
    }

    outColor = vec4(texel.rgb * fragColor.rgb, texel.a * fragColor.a);
}
