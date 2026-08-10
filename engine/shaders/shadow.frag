#version 450

// Writes no colour at all: the depth the rasterizer records *is* the output.
//
// It exists purely to throw away the see-through parts of a texture. Without
// it, a tree casts the shadow of a solid cube of leaves and a flower casts the
// shadow of a block.

layout(location = 0) in vec2 fragUv;
layout(location = 1) flat in float fragLayer;

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;
layout(set = 0, binding = 3) uniform sampler2DArray skinTexture;

void main() {
    float alpha;
    if (fragLayer < -2.5) {
        alpha = texture(skinTexture, vec3(fragUv, 0.0)).a;
    } else if (fragLayer < 0.0) {
        // Screen-space sheets never reach this pass; treat them as solid rather
        // than sampling a sheet whose alpha means something else.
        alpha = 1.0;
    } else {
        alpha = texture(blockTextures, vec3(fragUv, fragLayer)).a;
    }

    // The same threshold the G-buffer uses, and it has to stay the same: a
    // surface that survives there and not here casts no shadow, and the reverse
    // casts a shadow from nothing.
    if (alpha < 0.5) {
        discard;
    }
}
