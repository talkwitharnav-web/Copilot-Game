// Reading the sun's shadow map.
//
// Include `frame.glsl` first: the cascade matrices and their scale all live in
// the per-frame block, so this reads them straight rather than taking eleven
// parameters.

/// How much of the sun reaches `worldPosition`. 1 is full sun, 0 is fully
/// shadowed, and the values in between come from sampling more than one texel.
///
/// `normal` is the surface's own; the sample is nudged along it before being
/// projected, which is what stops a lit surface shadowing itself. Nudging along
/// the normal rather than toward the light is what keeps a wall's shadow
/// attached to the wall instead of sliding down it.
float sunShadowFactor(sampler2DArrayShadow shadowMap, vec3 worldPosition, vec3 normal, vec3 sunDirection,
                      float viewDistance) {
    int cascadeCount = int(frame.shadowParams.x + 0.5);
    if (cascadeCount <= 0) {
        return 1.0;
    }

    // A surface facing away from the sun is already dark from the Lambert term.
    // Testing it would only produce acne on the exact faces where it cannot
    // possibly matter.
    float nDotL = dot(normal, sunDirection);
    if (nDotL <= 0.0) {
        return 1.0;
    }

    float shadow = 1.0;
    for (int cascade = 0; cascade < cascadeCount; ++cascade) {
        // The offset grows as the light gets more oblique, because that is
        // exactly when one shadow texel spans more depth than the bias covers.
        float slope = clamp(1.0 - nDotL, 0.0, 1.0);
        vec3 offset = normal * frame.shadowTexelWorld[cascade] * (1.0 + 3.0 * slope);

        vec4 light = frame.shadowMatrices[cascade] * vec4(worldPosition + offset, 1.0);
        vec3 projected = light.xyz / light.w;
        vec2 uv = projected.xy * 0.5 + 0.5;

        // Outside this cascade: try the next, coarser one. The last one failing
        // means the point is beyond where shadows are drawn at all.
        if (any(lessThan(uv, vec2(0.002))) || any(greaterThan(uv, vec2(0.998))) || projected.z > 1.0) {
            continue;
        }

        float reference = projected.z - frame.shadowParams.z;
        float texel = frame.shadowParams.w;
        float sum = 0.0;
        // Nine comparisons, each of which the hardware has already filtered
        // across its own 2x2 neighbourhood - so the softened edge is wider than
        // the taps suggest.
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                sum += texture(shadowMap, vec4(uv + vec2(x, y) * texel, float(cascade), reference));
            }
        }
        shadow = sum * (1.0 / 9.0);
        break;
    }

    // Fade out over the last stretch, or the furthest cascade ends at a visible
    // line across the ground.
    float range = max(frame.shadowParams.y, 1.0);
    return mix(shadow, 1.0, smoothstep(0.8, 1.0, viewDistance / range));
}
