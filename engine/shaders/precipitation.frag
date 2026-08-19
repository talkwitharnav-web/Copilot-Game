#version 450

// Falling rain and snow.
//
// One vertical quad per world column, built on the CPU and reaching from the
// top of whatever blocks that column up past the camera. **The streaks are
// procedural rather than a texture**, which is what lets them sit on the same
// pixel grid the blocks do - a smooth streak would read as an effect layer
// pasted on from a different game.

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

#include "frame.glsl"

layout(set = 0, binding = 6) uniform sampler2D sceneCopy;

layout(location = 0) out vec4 outColor;

float streakHash(vec2 cell) {
    return fract(sin(dot(cell, vec2(41.317, 289.71))) * 43758.5453);
}

void main() {
    float level = frame.weather.x;
    if (level <= 0.0) {
        discard;
    }

    bool snow = frame.weather.y > 0.5;
    float texels = frame.waterDetail.w;
    float height = fragUv.y;

    // **Sheared in the world's directions, not the quad's.** A drop blown along
    // +wind travels *with* it as it falls, so its path is `x = C - drift·y` and
    // the constant that identifies the whole streak is `C = x + drift·y`.
    //
    // **The sign is the whole of which way the weather is going.** Negated, the
    // streaks lean into the wind while the grass bends away from it, and the two
    // read as two different storms happening at once - which is exactly how it
    // was reported.
    //
    // The drift has to be the wind's component *along this quad*, which the mesh
    // carries: applying a flat slant in quad-local space instead rotates the
    // lean with every quad's own facing, and looking straight up at that reads
    // as the rain spiralling round you.
    float alongWind = fragColor.b * 2.0 - 1.0;
    float across = fragUv.x + alongWind * frame.weather.w * height;

    // Snow wanders where rain does not, on two frequencies that do not divide
    // into each other. The per-column phase is what stops a whole field of it
    // swaying in unison, which reads as a shader fault rather than as snow.
    if (snow) {
        float wander = fragColor.g * 6.2831;
        across += 0.30 * sin(0.7 * frame.water.x + wander) +
                  0.10 * sin(1.9 * frame.water.x + wander * 2.3);
    }

    // One texel of the block grid is one drop wide, which is the whole of what
    // keeps this in the game's own art style. **This is the streak's identity**
    // - an earlier version hashed on it and then also demanded it equal a
    // second random lane in 0-15, which an absolute texel index never does, so
    // every fragment in the game was discarded.
    float column = floor(across * texels);

    // **Anchored to absolute world height, never to the quad's own bottom.**
    // That foot moves whenever the terrain under it changes, and a pattern
    // measured from there jumps every time you cross a column.
    float travel = (height + frame.weather.z) * texels;

    float period = snow ? 26.0 : 14.0;
    // **Short.** A drop long enough to see individually is long enough to read
    // as a shard of glass; what makes rain look like rain is many small ones,
    // not few large ones. Three texels is a fifth of a block, which still
    // clears the distance a drop covers between frames at 60 fps.
    float lit = snow ? 1.0 : 3.0;
    float row = floor(travel + fragColor.g * period);
    float slot = floor(row / period);

    // Not every repetition of every column carries a drop, which is what stops
    // the curtain reading as a comb.
    //
    // **These two numbers are small because the quads stack.** One per column
    // means twenty-odd of them line up along any horizontal view, so whatever
    // each one draws is drawn twenty times over; the first tuning looked right
    // on a single quad and rendered as a wall of translucent bars.
    // **Small, because a drop here is four times fatter than the reference's.**
    // Ours is a whole texel of the block grid - six centimetres - where the
    // reference draws its rain at sixty-four texels to the block. Keeping its
    // drop *count* with our drop *width* filled the screen with white slabs.
    float density = (snow ? 0.05 : 0.06) * (0.4 + 0.6 * level);
    if (streakHash(vec2(column, slot)) > density) {
        discard;
    }
    if (row - slot * period >= lit) {
        discard;
    }

    float sky = max(fragColor.r, 0.2);
    // Measured off the reference's own art: rain is a long translucent blue
    // streak, snow a short fully opaque white dot. That pair of differences
    // carries most of the signal on its own.
    //
    // **Rain is darker than the scene, not brighter.** Lit like a surface it
    // reads as a shaft of light rather than as water; the reference's own
    // texture is a muted blue at about half alpha for exactly this reason.
    vec3 tint = snow ? vec3(1.0) : vec3(0.42, 0.50, 0.68);
    // Snow is forced bright, or a blizzard turns grey at dusk.
    float lighting = snow ? mix(0.75, 1.0, sky)
                          : mix(0.25, 0.85, sky * max(frame.lighting.y, 0.25) + 0.25);
    float alpha = fragColor.a * (snow ? 0.85 : 0.34);

    // Faded out right in front of the eye. A drop two blocks away covers a
    // sixth of the screen's height, and no amount of tuning the count fixes
    // that - the reference simply never has one there either.
    alpha *= smoothstep(0.8, 3.5, fragViewDepth);

    // Softened where a streak meets the ground, or the curtain ends in a hard
    // line across every hillside. The scene copy's alpha is that distance.
    //
    // **Both sides of the subtraction must be the same kind of distance.** The
    // copy's alpha is `length(worldPosition - eye)` - radial, written by
    // `deferred.frag` - while `fragViewDepth` is `gl_Position.w`, the distance
    // along the view axis, and the two differ by `1/cos` of the angle off the
    // centre of the screen: about 1.74 at the corners at a 70 degree field of
    // view. Subtracting one from the other therefore cleared the clamp's
    // ceiling of 1.0 for any surface more than about 1.4 m off-centre, so the
    // fade only ever worked in a narrow cone around the middle of the screen
    // and the hard line this exists to prevent was visible everywhere else -
    // appearing and disappearing on the same hillside as the camera turned.
    // The precipitation mesh is drawn with a plain `viewProjection`, so
    // `fragWorldPosition` really is world space here.
    vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(sceneCopy, 0));
    float behind = texture(sceneCopy, screenUv).a;
    alpha *= clamp(behind - length(fragWorldPosition - frame.eye.xyz), 0.0, 1.0);

    outColor = vec4(tint * lighting, alpha);
}
