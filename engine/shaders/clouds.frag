#version 450

// The cloud deck, raymarched.
//
// No geometry at all, which is what makes flying through it work: there is no
// near plane to slice a quad against, nothing to sort, and no inside face to
// vanish when the camera crosses the layer. It is drawn in the forward pass
// **after the sun and moon** so a cloud occludes them, and before water so a
// reflection can pick it up.
//
// `gl_FragDepth` carries the depth of the point where the ray enters the deck,
// so ordinary depth testing puts terrain in front of it. That costs early-Z,
// which is the price of not needing to sample the depth buffer while it is
// bound as a writable attachment.

#include "frame.glsl"
#include "sky.glsl"
#include "clouds.glsl"

layout(location = 0) in vec2 fragUv;

// The camera's view-projection, so the entry point can be turned back into a
// depth. Reuses the mesh block rather than growing the post one.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    uvec4 flags;
} push;

layout(location = 0) out vec4 outColor;

/// How forward-scattered the light is. Positive leans toward the sun, which is
/// what gives a cloud its bright rim when you look toward it. The phase
/// function itself is the atmosphere's, taking `g` as an argument - a droplet
/// and a dust grain differ in how forward they throw light, not in the maths.
const float kPhaseG = 0.32;

/// How much light reaches a point inside the deck, by marching toward it.
///
/// **Three taps, spaced across neighbouring cells.** The one-tap directional
/// derivative is cheaper and was tried first: it measures how fast density
/// changes over a few blocks, and a cloud cell is twelve across, so it read
/// zero everywhere and the whole deck came out unlit. The spacing here is
/// chosen against the cell size, so a cell with cloud beside it is shaded and
/// one on the edge of a cloud is not - which is what gives a flat deck of
/// boxes any relief at all.
float cloudLightMarch(vec3 point, float wind, float coverage) {
    float total = 0.0;
    float travelled = 0.0;
    for (int j = 0; j < 3; ++j) {
        float span = kCloudCell * (0.45 + float(j) * 1.1);
        travelled += span;
        total += cloudDensity(point + frame.sunDirection.xyz * travelled, wind, coverage) * span;
    }
    return exp(-total * 0.055);
}

/// Where a ray enters and leaves the slab. False when it never does.
bool slab(vec3 origin, vec3 direction, out float near, out float far) {
    // Level rays never reach it, and dividing by a direction of zero is what
    // turns that into a NaN rather than a miss.
    if (abs(direction.y) < 1e-4) {
        if (origin.y < kCloudBottom || origin.y > kCloudTop) {
            return false;
        }
        near = 0.0;
        far = kCloudMaxDistance;
        return true;
    }

    float toBottom = (kCloudBottom - origin.y) / direction.y;
    float toTop = (kCloudTop - origin.y) / direction.y;
    near = min(toBottom, toTop);
    far = max(toBottom, toTop);
    near = max(near, 0.0);
    return far > near;
}

void main() {
    float quality = frame.cloud.w;
    if (quality < 0.5) {
        discard;
    }

    // The ray for this pixel, from the matrix everything else was drawn with so
    // the two cannot disagree.
    vec4 target = frame.inverseViewProjection * vec4(fragUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(target.xyz / target.w - frame.eye.xyz);
    vec3 origin = frame.eye.xyz;

    float near;
    float far;
    if (!slab(origin, direction, near, far)) {
        discard;
    }
    far = min(far, near + kCloudMaxDistance);
    if (far - near < 0.01) {
        discard;
    }

    int steps = quality > 1.5 ? 32 : 14;
    float wind = frame.cloud.x;
    float coverage = frame.cloud.y;

    float stepLength = (far - near) / float(steps);
    // **No dither.** It is what a smooth field needs to hide banding at a low
    // step count, and it is exactly wrong here: the density is 1 or 0, so
    // offsetting each pixel's ray by a different amount frays the one thing
    // this deck is for, which is a crisp square edge.
    float travelled = near + stepLength * 0.5;

    float cosTheta = dot(direction, frame.sunDirection.xyz);
    // With a floor, because pure Henyey-Greenstein at this asymmetry gives a
    // cloud with its back to the sun about a tenth of the light, and a real one
    // is still plainly white.
    //
    // **The floor was far too low and the lobe far too strong**, so a cloud
    // facing away from the sun got half the light of one facing it and sank
    // into the sky, leaving a bright patch around the sun and pale blue
    // everywhere else. A cloud is white across the whole sky; the sun's side is
    // only *brighter*, by about a fifth.
    float phase = 0.85 + henyeyGreenstein(cosTheta, kPhaseG) * 1.7;

    // Lit from whichever body is up, through the same numbers the world uses.
    // The ambient term is pulled toward white rather than being the sky colour
    // itself, or a cloud is exactly as bright as what is behind it and cannot
    // be seen at all.
    //
    // How much light there is at all is carried by `frame.lighting` below, so
    // the deck goes dark at night without a second term saying the same thing.
    // **How stormy the deck is is derived from its own coverage, not passed in.**
    // The game already raises coverage as weather comes on, and an overcast sky
    // is exactly what a dark deck means - so a second uniform saying the same
    // thing could only ever disagree with the first.
    float storm = smoothstep(0.45, 0.88, coverage);
    // **Lit by the colour the sky says the body is, but only while the body is
    // low.** `glow.rgb` is a fixed warm colour for the sun and a fixed cool one
    // for the moon - it says which body is up, not what colour its light is
    // right now, and its *strength* is already near maximum at noon. Keying the
    // tint off that alone painted the whole deck orange in the middle of the
    // day. Elevation is the thing that actually reddens sunlight, so it is what
    // gates this.
    float lowBody = 1.0 - smoothstep(0.0, 0.35, frame.sunDirection.y);
    // **Only the sun's tint is gated on elevation.** Sunlight reddens as it
    // sinks; moonlight is cool wherever the moon is, so keying both off
    // elevation lit the deck with warm white whenever the moon was high - which
    // is why clouds at night came out white instead of grey.
    bool moonUp = frame.glow.b > frame.glow.r;
    vec3 bodyTint = mix(vec3(1.0, 0.97, 0.92), normalize(frame.glow.rgb + vec3(1e-4)) * 1.732,
                        moonUp ? 1.0 : lowBody * 0.8);
    // A cloud picked out by the moon is a grey shape rather than a lit one, so
    // the forward lobe that makes a sunlit cloud brilliant is flattened too.
    float bodyGain = moonUp ? 0.55 : 1.0;
    float bodyPhase = moonUp ? mix(phase, 1.0, 0.7) : phase;
    vec3 sunlight = bodyTint * frame.lighting.y * 2.2 * bodyGain * (1.0 - 0.80 * storm);
    // **Pulled most of the way to white, not a little.** This is what a cloud
    // receives from the sky rather than from the body, and it used to keep so
    // much of the sky's own blue that a cloud lit mainly by it *was* the sky
    // colour. Water droplets scatter every wavelength about equally, which is
    // the whole reason a cloud is white and the sky is not.
    vec3 skylight = mix(frame.fog.rgb, vec3(1.0), 0.82) *
                    (0.04 + frame.lighting.x * 1.05) * (1.0 - 0.62 * storm);

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float entry = -1.0;

    for (int i = 0; i < steps; ++i) {
        vec3 point = origin + direction * travelled;
        float density = cloudDensity(point, wind, coverage);
        if (density > 0.001) {
            if (entry < 0.0) {
                entry = travelled;
            }

            float lightAmount = cloudLightMarch(point, wind, coverage);

            // Beer's law, with the powder term that darkens the edges facing
            // you. Not physical - it fakes multiple scattering.
            //
            // **The powder term is inert today, and the claim that it is what
            // makes a cloud read as fluffy was false.** `cloudDensity` ends in
            // `normalised > (1.0 - coverage) ? 1.0 : 0.0`, so it returns
            // exactly 1.0 or exactly 0.0 and nothing between - and the branch
            // this sits in has already rejected 0.0. `density` is therefore
            // always exactly 1.0 here, `powder` is always 1 - exp(-6) =
            // 0.99752, and the whole term is a uniform 0.25% darkening with no
            // edge behaviour in it at all. Tuning the 6.0 does nothing
            // visible, which is the reason to say so here rather than to leave
            // a reader measuring it.
            //
            // Kept rather than deleted, because it is correct code waiting on
            // its input: the moment `cloudDensity` grows a height profile or
            // any erosion - which `clouds.glsl` says it deliberately has not -
            // density becomes continuous and this starts doing the job it
            // describes, with no edit needed here.
            float extinction = density * stepLength * 0.9;
            float absorbed = 1.0 - exp(-extinction);
            float powder = 1.0 - exp(-density * 6.0);

            vec3 luminance = skylight + sunlight * lightAmount * bodyPhase * powder;
            scattered += transmittance * absorbed * luminance;
            transmittance *= exp(-extinction);
            if (transmittance < 0.02) {
                break;
            }
        }
        travelled += stepLength;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.004 || entry < 0.0) {
        discard;
    }

    // Un-premultiplied, because the blend state multiplies by alpha itself.
    // Taken *before* the fades below, or thinning the layer would brighten it.
    vec3 colour = scattered / alpha;

    // Faded into the distance on its own curve, ending well before the clamp so
    // the deck dissolves instead of stopping at a visible circle.
    alpha *= 1.0 - smoothstep(kCloudMaxDistance * 0.45, kCloudMaxDistance, entry);

    // And thinned right around the camera's own altitude, or crossing the layer
    // means flying into a wall of white.
    if (origin.y > kCloudBottom && origin.y < kCloudTop) {
        alpha *= 0.30;
    } else {
        float toLayer = min(abs(origin.y - kCloudBottom), abs(origin.y - kCloudTop));
        alpha *= 1.0 - (1.0 - smoothstep(0.0, 6.0, toLayer)) * 0.55;
    }

    if (alpha < 0.004) {
        discard;
    }

    vec4 clip = push.viewProjection * vec4(origin + direction * entry, 1.0);
    // **Clamped to just inside the far plane, never to it.** A cloud near the
    // horizon is further away than the far plane, and a depth of exactly 1.0
    // loses the `LESS` test against the cleared sky - so the whole horizon
    // vanished. Just under 1 still loses to any terrain in front of it, which
    // is the only ordering a deck above the world can need.
    gl_FragDepth = min(clip.z / max(clip.w, 1e-4), frame.volumetric.z);

    // **The same fog the world fades into, on the same ramp and measured from
    // the same point the depth above is.** A deck that stays crisp white where
    // the terrain beneath it has already dissolved is the thing that gives the
    // boundary away, which is the whole job the fog was given.
    //
    // The colour is mixed but the alpha is left alone on purpose: a cloud you
    // cannot pick out through the haze is still there and still blocks the sun
    // behind it, and that is exactly what a hazy sunset looks like.
    //
    // **Reached further out than the ground's fog.** You see cloud much further
    // than ground haze, because you are looking up through thinner air rather
    // than along the ground through the thick of it - and at the terrain's own
    // reach this washed most of the visible deck into the sky colour, which is
    // the opposite of hiding a boundary.
    if (frame.fog.w > 0.0) {
        float reach = frame.fog.w * 2.5;
        float start = reach * frame.eye.w;
        float fogged = clamp((entry - start) / max(reach - start, 0.001), 0.0, 1.0);
        colour = mix(colour, frame.fog.rgb, fogged);
    }

    outColor = vec4(colour, alpha);
}
