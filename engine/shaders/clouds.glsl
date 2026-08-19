// The cloud layer, as a field rather than as geometry.
//
// Three shaders need the same answer: the raymarch that draws them
// (`clouds.frag`), the lighting pass that darkens the ground under them
// (`deferred.frag`), and the forward pass that darkens water and glass to match
// (`triangle.frag`). Sharing the function is what stops a cloud and its own
// shadow drifting apart.
//
// **This file must declare no `layout()` of any kind - no binding, no sampler,
// no uniform, no push constant - and that is a hard constraint rather than a
// stylistic one.** It is included by fragment shaders belonging to *different*
// descriptor set layouts, and a binding declared here would be injected into
// all of them. The forward and lighting passes do not offer the same bindings,
// so the same number would name two different resources, or none: that is not
// a compile error, it is a wrong sampler read at runtime with no diagnostic.
// Everything here therefore takes its inputs as function parameters, and any
// new input must be a parameter too.
//
// Checked 2026-08-19 11:30, and the falsifier is one grep - but it must be run
// with comments stripped, because the paragraph above names all three forbidden
// tokens and a raw search therefore hits its own documentation. Measured both
// ways at the time of writing: 6 raw hits, 0 over the 50 lines of actual code.
// A positive control on `frame.glsl` through the identical filter returns 1, so
// a zero here means the file is clean rather than the search being blind. Note
// the third consumer is recent - `triangle.frag` began including this file on
// 2026-08-19 - so a reader who remembers only two callers is remembering a
// state this file has left.
//
// **Procedural, not the reference's 256x256 mask.** Some of the reference's
// numbers are used - cells 12 blocks across (`kCloudCell`), drifting west - but
// the coverage itself is noise, which costs no texture, no descriptor and no
// staging, and lets the lighting pass evaluate it for free.
//
// **How much sky is covered is NOT one of the reference's numbers.** An earlier
// version of this comment said "about 28% of the sky covered" as though that
// were configured here; it is not. Coverage is a runtime parameter - the engine
// falls back to 0.5 (`Renderer::m_cloudCoverage`) and the game supplies its own
// setting - and neither is 0.28. Measured over 360,000 cells: coverage 0.28
// gives 27.94% of sky, 0.36 gives 35.31%, 0.50 gives 49.37%. So the claim was
// describing the reference while sitting beside a knob set to something else,
// which invites exactly one bad edit: "correcting" the setting down to 0.28,
// thinning every sky by a fifth and taking the cloud shadow on the ground with
// it, because `cloudSunShadow` thresholds the same field.

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
    //
    // **The clamp floor puts a ceiling on coverage, and it is not 100%.**
    // 9.47% of cells (34,099 of 360,000 measured) have an fbm below 0.30, so
    // `normalised` clamps to exactly 0.0 - and 0.0 does not satisfy a strict
    // `>` even when `coverage` is 1.0 and the threshold is 0.0. A fully
    // overcast sky is therefore unreachable: 1.0 tops out at 90.53%, and the
    // holes are in fixed places because the hash is deterministic.
    //
    // Left as it is on purpose. Relaxing to `>=` would buy the top end at the
    // cost of the bottom: coverage 0.0 would then paint 8.88% of the sky,
    // because the same clamped cells all tie the threshold at once. Cloudless
    // weather matters more than perfectly overcast weather, and the ramp the
    // game drives reaches about 0.86 (82.16%), which reads as a storm anyway.
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
