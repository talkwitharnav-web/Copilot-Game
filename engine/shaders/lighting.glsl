// The lighting model, in one place, because two passes run it.
//
// The deferred pass shades every opaque pixel from the G-buffer; the forward
// pass shades water, the sky and the block outline, which cannot go through a
// G-buffer because they blend. Both call `shadeSurface`, so they cannot drift.
//
// **Do not fold face shade into ambient occlusion, or drop either.** They are
// different facts - one is which way the surface points, the other is how
// boxed-in the corner is - and they were a single unrecoverable float until
// M23e. Keeping both is also why unlit caves are not perfectly flat.

struct Surface {
    vec3 albedo;
    vec3 normal;
    float skyLight;
    float blockLight;
    float faceShade;
    float occlusion;
    float emissive;
    float roughness;
    float metallic;
};

/// Only sky light answers to the sun. Modulating block light by daylight would
/// put torches out at dusk and leave a lit cave dark, because underground no
/// face points at the sun.
///
/// **The ambient sky and the sun are gated differently, and that is the whole
/// point.** Ambient scales straight off sky light, so a cave is dark. The
/// directional half is gated by the shadow map instead, and sky light only has
/// to say the cell is not sealed inside rock - because the shadow map can see a
/// hole in a leaf and baked sky light cannot. Without the split, only the outer
/// shell of a tree was ever lit and the inside was a dark mass.
///
/// `sunShadow` scales the directional half outright, and the ambient half only
/// as far as `shadowDarkness` asks. A shadowed surface still receives most of
/// the sky, which is why a shadow is dark blue-grey rather than black - and why
/// nothing outside the shadow map's reach looks wrong when it silently gets 1.0.
///
/// **Ambient is what a shadow's depth is actually tuned with.** Killing the
/// directional half is all a shadow map can do on its own, and that alone left
/// shadows reading as pale patches; a point the sun cannot see is also cut off
/// from a good part of the sky, so taking some ambient with it is both the
/// honest answer and the only dial with any range in it.
float diffuseLevel(Surface surface, vec3 sunDirection, vec3 lighting, float sunShadow, float shadowDarkness) {
    float lambert = max(dot(surface.normal, sunDirection), 0.0);
    float openToSky = smoothstep(0.0, 0.30, surface.skyLight);
    float ambientShadow = 1.0 - shadowDarkness * (1.0 - sunShadow) * openToSky;
    float ambient = lighting.x * surface.skyLight * ambientShadow;
    float direct = lighting.y * lambert * sunShadow * openToSky;
    float lit = max(surface.blockLight, ambient + direct);
    float ambientFloor = lighting.z;
    return surface.faceShade * surface.occlusion * (ambientFloor + (1.0 - ambientFloor) * lit);
}

// ---------------------------------------------------------------------------
// Cook-Torrance, in Filament's formulation.
//
// **`Fr = D * V * F`, with no further division.** V already has the 4·NoL·NoV
// denominator folded in; the G formulation does not, and mixing the two gives
// either a blown-out highlight or a completely black one. That is the classic
// bug in this equation.
// ---------------------------------------------------------------------------

float distributionGGX(float roughnessAlpha, float nDotH, vec3 normal, vec3 halfway) {
    // Filament's numerically stable form: computing 1 - NoH^2 directly loses
    // most of its precision at the sharp end, where the highlight lives.
    vec3 cross0 = cross(normal, halfway);
    float oneMinusNoHSquared = dot(cross0, cross0);
    float a = nDotH * roughnessAlpha;
    float k = roughnessAlpha / (oneMinusNoHSquared + a * a);
    return min(k * k * (1.0 / 3.14159265), 65504.0);
}

float visibilitySmithGGXCorrelated(float roughnessAlpha, float nDotV, float nDotL) {
    float a2 = roughnessAlpha * roughnessAlpha;
    float lambdaV = nDotL * sqrt((nDotV - a2 * nDotV) * nDotV + a2);
    float lambdaL = nDotV * sqrt((nDotL - a2 * nDotL) * nDotL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

vec3 fresnelSchlick(vec3 f0, float f90, float vDotH) {
    return f0 + (f90 - f0) * pow(1.0 - vDotH, 5.0);
}

/// The specular a single directional light adds. Diffuse is handled by
/// `diffuseLevel`, which carries the baked light the game already owns.
vec3 specularLobe(Surface surface, vec3 lightDirection, vec3 viewDirection) {
    // Perceptual roughness is what the material table stores; the distribution
    // wants its square. **A value copied from a LabPBR source is already
    // squared and must not be squared again.**
    float perceptual = clamp(surface.roughness, 0.045, 1.0);
    float alpha = perceptual * perceptual;

    vec3 halfway = normalize(lightDirection + viewDirection);
    float nDotV = max(dot(surface.normal, viewDirection), 1e-4);
    float nDotL = max(dot(surface.normal, lightDirection), 0.0);
    float nDotH = clamp(dot(surface.normal, halfway), 0.0, 1.0);
    float vDotH = clamp(dot(viewDirection, halfway), 0.0, 1.0);
    if (nDotL <= 0.0) {
        return vec3(0.0);
    }

    // 0.04 is the right reflectance for every dielectric in this world; a metal
    // takes its tint from its own albedo instead, which is what makes gold look
    // like gold rather than like white plastic.
    vec3 f0 = mix(vec3(0.04), surface.albedo, surface.metallic);

    float d = distributionGGX(alpha, nDotH, surface.normal, halfway);
    float v = visibilitySmithGGXCorrelated(alpha, nDotV, nDotL);
    vec3 f = fresnelSchlick(f0, clamp(50.0 * f0.g, 0.0, 1.0), vDotH);
    return d * v * f * nDotL;
}
