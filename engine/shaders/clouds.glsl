// The cloud layer, as a field rather than as geometry.
//
// Two shaders need the same answer: the raymarch that draws them, and the
// lighting pass that darkens the ground under them. Sharing the function is
// what stops a cloud and its own shadow drifting apart.
//
// **Procedural, not the reference's 256x256 mask.** The reference's numbers are
// used - cells 12 blocks across, about 28% of the sky covered, drifting west -
// but the coverage itself is noise, which costs no texture, no descriptor and
// no staging, and lets the lighting pass evaluate it for free.

/// The reference puts its deck at y 192 with the player around y 64, so it is
/// roughly 128 blocks overhead and a cloud subtends a useful angle.
///
/// **Our world is only 96 blocks tall, but the deck is not part of it** - it is
/// a field, not geometry, so it can sit above the build ceiling entirely. That
/// is what keeps the same proportion: at 140 a player on the ground is about
/// 100 blocks under it, and several clouds fit across the sky. Put it inside
/// the world instead and the whole visible sky spans less than one cloud, which
/// reads as a flat white veil.
const float kCloudBottom = 140.0;
const float kCloudTop = 145.0;

/// Blocks across one cloud cell, from the reference. Cloud *shapes* are many
/// cells wide; this is the smallest feature either can have.
const float kCloudCell = 12.0;

/// How far a ray is allowed to march. A ray running nearly level with the slab
/// would otherwise cross tens of kilometres of it, which is the single most
/// common raymarched-cloud bug: either the step size explodes and the horizon
/// turns to mush, or the loop never finishes.
const float kCloudMaxDistance = 3000.0;

float cloudHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float cloudNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = cloudHash(cell);
    float b = cloudHash(cell + vec2(1.0, 0.0));
    float c = cloudHash(cell + vec2(0.0, 1.0));
    float d = cloudHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float cloudFbm(vec2 p) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amplitude * cloudNoise(p);
        p *= 2.03;
        amplitude *= 0.5;
    }
    return sum;
}

/// Whether there is cloud over this column: **1 or 0, never in between.**
///
/// The reference assembles a cloud out of square pieces 12 blocks across, and
/// that blockiness is the look. So the smooth field is sampled once per cell
/// and thresholded hard: the field decides *which* cells are cloud, and the
/// grid decides what a cloud is shaped like. Smoothing across the boundary - a
/// `smoothstep` on the raw field, which is what this did first - gives soft
/// round blobs that belong in a different game.
float cloudCoverage(vec2 world, float wind, float coverage) {
    vec2 cell = floor((world + vec2(wind, 0.0)) / kCloudCell);

    // One base feature spans about nine cells, which at a hundred blocks
    // overhead is a cloud rather than either a speck or the whole sky.
    float normalised = clamp((cloudFbm(cell / 9.0) - 0.30) / 0.34, 0.0, 1.0);
    return normalised > (1.0 - coverage) ? 1.0 : 0.0;
}

/// Density at a point inside the slab. Zero outside it.
///
/// A flat top and a flat bottom, so a cloud is a rectangular prism the way the
/// reference's is. There is deliberately no height profile and no erosion here:
/// both round the edges off, and round edges are exactly what this is not.
float cloudDensity(vec3 world, float wind, float coverage) {
    if (world.y < kCloudBottom || world.y > kCloudTop) {
        return 0.0;
    }
    return cloudCoverage(world.xz, wind, coverage);
}

/// How much of the sun reaches a point on the ground, given the cloud deck
/// above it. One coverage sample: walk from the point along the light until it
/// reaches cloud height.
float cloudSunShadow(vec3 world, vec3 lightDirection, float wind, float coverage, float strength) {
    if (strength <= 0.0 || lightDirection.y < 0.10) {
        return 1.0;
    }
    float middle = (kCloudBottom + kCloudTop) * 0.5;
    float travel = (middle - world.y) / lightDirection.y;
    if (travel <= 0.0) {
        return 1.0;
    }
    vec2 above = world.xz + lightDirection.xz * travel;
    return 1.0 - cloudCoverage(above, wind, coverage) * strength;
}
