#version 450

// Opaque world geometry, written into the G-buffer instead of being shaded on
// the spot.
//
// A "G-buffer" is a set of screen-sized scratch images holding one line per
// pixel about what the surface there *is* - its colour, which way it faces,
// what it is made of - so a later pass can work out the lighting for the whole
// screen at once. That later pass is `deferred.frag`.
//
// Nothing here blends. Unity's own documentation says why: "pixel normals cannot
// be correctly combined using the alpha blend equation alone", and a blended
// material id is nonsense. Anything transparent goes through the forward pass.

#include "octahedral.glsl"

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in float fragViewDepth;
layout(location = 5) flat in uint fragNormalCode;
layout(location = 6) in float fragOcclusion;

layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    uvec4 flags;
} push;

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;
layout(set = 0, binding = 1) uniform sampler2DArray hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray fontTexture;
layout(set = 0, binding = 3) uniform sampler2DArray skinTexture;

#include "frame.glsl"

layout(set = 0, binding = 5) readonly buffer Materials {
    uint rows[];
} materials;

layout(location = 0) out vec4 outAlbedo;   // rgb albedo, a face shade
layout(location = 1) out vec4 outLight;    // r sky, g block, b occlusion, a emissive
layout(location = 2) out vec4 outMaterial; // r roughness, g metallic, ba normal

vec3 surfaceNormal() {
    vec3 axis;
    switch (fragNormalCode) {
    case 0u: axis = vec3( 1.0,  0.0,  0.0); break;
    case 1u: axis = vec3(-1.0,  0.0,  0.0); break;
    case 2u: axis = vec3( 0.0,  1.0,  0.0); break;
    case 3u: axis = vec3( 0.0, -1.0,  0.0); break;
    case 4u: axis = vec3( 0.0,  0.0,  1.0); break;
    case 5u: axis = vec3( 0.0,  0.0, -1.0); break;
    default:
        // A rotated creature limb or a plant blade at forty-five degrees has no
        // axis to name, so the triangle's own geometry answers instead.
        //
        // **Guarded, because the cross product is zero for a degenerate or
        // exactly edge-on quad** - and `normalize` of a zero vector is NaN,
        // which poisons the whole G-buffer row for that pixel: an octahedral
        // normal, a roughness and a metallic that the lighting pass then reads
        // back as a black or white speck flickering with the camera. Braced
        // because the declaration lives inside a `switch`.
        {
            vec3 derived = cross(dFdx(fragWorldPosition), dFdy(fragWorldPosition));
            axis = length(derived) > 1e-8 ? normalize(derived) : vec3(0.0, 1.0, 0.0);
        }
        break;
    }

    // **Turned toward the eye.** Leaves, glass and every other cutout are drawn
    // with both windings from one set of vertices, so the surface in front of
    // you may be either side of the quad - and a face lit from behind reads as
    // a hole. For ordinary geometry the far side is culled, so this changes
    // nothing at all.
    return dot(axis, frame.eye.xyz - fragWorldPosition) < 0.0 ? -axis : axis;
}

void main() {
    float layer = fragLayer;
    if (abs(layer - frame.animation.x) < 0.25) {
        layer = frame.animation.y;
    } else if (abs(layer - frame.animation.z) < 0.25) {
        layer = frame.animation.w;
    }

    vec4 texel;
    if (layer < -2.5) {
        texel = texture(skinTexture, vec3(fragUv, 0.0));
    } else if (layer < -1.5) {
        texel = texture(fontTexture, vec3(fragUv, 0.0));
    } else if (layer < 0.0) {
        texel = texture(hudTexture, vec3(fragUv, 0.0));
    } else {
        texel = texture(blockTextures, vec3(fragUv, layer));
    }

    // A hole in the texture is thrown away outright rather than blended. What
    // survives is fully present, never partly - mip levels average alpha, and
    // letting a 0.6 through turned every distant tree pale grey.
    if (texel.a < 0.5) {
        discard;
    }

    // **Not multiplied by the vertex colour.** For world geometry those three
    // channels are sky light, block light and face shade, not a tint - so
    // multiplying by them here scaled red by sky light and green by block light,
    // and every surface away from a torch came out purple. The lighting pass
    // consumes all three properly.
    vec3 albedo = texel.rgb;

    // A lit fuse strobes white; a struck creature flashes red. Applied to the
    // albedo here rather than to the shaded result, because the lighting pass
    // has no idea a creature was hit.
    if (fragLayer < -4.5) {
        albedo = mix(albedo, vec3(1.0), 0.5);
    } else if (fragLayer < -3.5) {
        albedo = clamp(albedo * vec3(2.9, 0.92, 0.86), 0.0, 1.0);
    }

    float roughness = 0.85;
    float metallic = 0.0;
    float emissive = 0.0;
    if (fragLayer >= 0.0) {
        // The layer the geometry was **meshed** with, not the redirected one:
        // which frame of an animation is showing does not change what the
        // surface is made of.
        uint row = materials.rows[int(fragLayer)];
        roughness = float(row & 0xffu) / 255.0;
        metallic = float((row >> 8) & 0xffu) / 255.0;
        emissive = float((row >> 16) & 0xffu) / 255.0;

        // Weighted by how bright the texel already is, so a torch's flame glows
        // and the stick holding it does not. Never all the way to zero: a
        // redstone torch's head is dark red and should still read as lit.
        if (emissive > 0.0) {
            float brightness = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
            emissive *= mix(0.35, 1.0, smoothstep(0.2, 0.7, brightness));
        }
    }

    outAlbedo = vec4(albedo, fragColor.b);
    outLight = vec4(fragColor.r, fragColor.g, fragOcclusion, emissive);
    outMaterial = vec4(roughness, metallic, octEncodeNormal(surfaceNormal()));
}
