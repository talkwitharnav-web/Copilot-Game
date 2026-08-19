// What the sky looks like in a given direction.
//
// Include `frame.glsl` first: the fog colour, the sun and the daylight strength
// all live in the per-frame block.
//
// **Two passes read this and they must not drift.** The lighting pass writes it
// wherever nothing was drawn, and the forward pass reflects it in water - so a
// lake catching a different sunset from the one above it is exactly the bug
// this file exists to prevent.

// **Alpha in the scene image is how far away the pixel is, not opacity.** Water
// reflects the world by marching a ray through a copy of that image, and it can
// only tell a hit from a miss if each pixel says what distance it stands at.
// The sky is parked past anything a ray will travel, so it can never be a hit.
//
// **Finite on purpose, and 65504 is the reason.** The scene image is
// `VK_FORMAT_R16G16B16A16_SFLOAT`, so alpha is a half. This was 1.0e6, which is
// fifteen times the largest finite half and stores as `+Inf` - and the reader's
// `straight.a < kSkyDistance * 0.5` test was then satisfiable by no finite half
// at all, working only because IEEE overflow happens to put the sky on the
// right side of it. On any path that clamps instead of overflowing, the sky
// would arrive as 65504, read as a hit at 65504 blocks, and a waterfall in
// front of open sky would turn fully opaque and erase the sun, moon and clouds
// behind it - the exact failure these lines exist to prevent.
//
// 60000 is exactly representable as a half (the spacing there is 32, and
// 60000/32 is a whole number), leaves the threshold at 30000, and still sits
// far beyond any distance real geometry can report: render distance is capped
// in blocks, not tens of thousands of them.
const float kSkyDistance = 60000.0;

/// Rayleigh scattering per channel, in the reference's own proportions: it goes
/// as one over the fourth power of wavelength, which is why blue scatters about
/// six times as readily as red. **These three numbers are the whole reason a
/// sunset is orange** - nothing here tints anything by hand.
const vec3 kRayleigh = vec3(0.175, 0.408, 1.000);

/// Mie scattering, from dust and water droplets. Effectively colourless and
/// strongly forward-biased, which is the white haze that gathers around the sun
/// and along the horizon.
const float kMie = 0.180;
const float kMieG = 0.76;

/// How fast the air thins with height, in blocks. **Enormously exaggerated** -
/// the real figure is about 8500 m and our whole world is 96 blocks tall, so at
/// any honest scale climbing a mountain here would change nothing. At 70 the
/// summit of a tall peak reads as visibly thinner air than the shoreline, which
/// is the effect this is for.
const float kScaleHeight = 70.0;
const float kSeaLevelHeight = 24.0;

/// Rayleigh's own phase function: symmetric front to back, gently favouring
/// both. Normalised so the average over the sphere is one.
float rayleighPhase(float cosTheta) {
    return 0.75 * (1.0 + cosTheta * cosTheta);
}

/// Henyey-Greenstein, for anything that scatters mostly forwards. `g` is how
/// forward: 0 is uniform, and the closer to 1 the tighter the beam.
float henyeyGreenstein(float cosTheta, float g) {
    float gg = g * g;
    float denominator = 1.0 + gg - 2.0 * g * cosTheta;
    return (1.0 - gg) / (12.566370614 * pow(max(denominator, 1e-4), 1.5));
}

/// How much air a ray passes through, relative to straight up.
///
/// One at the zenith and about forty along the horizon, which is the real
/// ratio. **Clamped at the horizon rather than continued below it**: there is no
/// sky under the horizon, and the obvious closed forms all divide by zero or go
/// negative down there - which showed up as a black band exactly where the
/// loaded chunks stop, in the one place the fog exists to hide.
float airMass(float cosZenith) {
    float up = max(cosZenith, 0.0);
    return 1.0 / (up + 0.025 * exp(-11.0 * up));
}

/// Single-scattered sunlight arriving from one direction.
///
/// **The sunset falls out of this rather than being painted on.** Light reaching
/// the eye from near the horizon has crossed a great deal of air, so its blue
/// has been scattered out of it long before it arrives - which is what
/// `sunTransmit` computes and why it reddens on its own as the sun drops.
vec3 atmosphericScatter(vec3 direction, vec3 sunDirection, float density) {
    float cosTheta = dot(direction, sunDirection);
    float viewMass = airMass(direction.y) * density;
    float sunMass = airMass(sunDirection.y) * density;

    vec3 extinction = kRayleigh + vec3(kMie);
    vec3 sunTransmit = exp(-kRayleigh * sunMass * 0.55 - vec3(kMie) * sunMass * 0.22);
    // The 5.0 is a unit bridge, not a fudge, and it is why the two phase
    // functions above are normalised differently on purpose. `rayleighPhase`
    // averages one over the sphere; `henyeyGreenstein` carries its 4pi and so
    // *integrates* to one, averaging 1/4pi. Adding them therefore needs the Mie
    // term scaled by about 4pi = 12.566 before the two are in the same units,
    // and 5.0 supplies 0.398 of that. Why it is short of full strength is not
    // recorded anywhere and is not known - `kMie` is a free tuning constant, so
    // the pair was almost certainly settled by eye.
    //
    // **Do not "correct" this to 12.566** - that is the edit this comment
    // exists to prevent, and it multiplies the haze around the sun by 2.5.
    // The curve is confirmed, twice and independently: hand-evaluated at
    // g = 0.76 it gives 2.4315 / 0.016963 / 0.0061658 at cos = +1 / 0 / -1,
    // matching finding 1022's separately computed 2.431534 / 0.016964 /
    // 0.006166, and the forward-to-back ratio 394.3 matches the analytic
    // ((1+g)/(1-g))^3 = 394.4 - an identity that holds whatever the
    // normalisation, so it checks the shape rather than the scale. The balance
    // is right too: at the sun Mie 2.19 beats Rayleigh 1.50 and the aureole is
    // white, at 90 degrees Rayleigh wins 49:1 and the sky is deep blue. An
    // inverted curve - CLAUDE.md bug shape #9 - would read as a blue halo on a
    // white sky, which is not what this computes.
    vec3 inscatter = kRayleigh * rayleighPhase(cosTheta) +
                     vec3(kMie) * henyeyGreenstein(cosTheta, kMieG) * 5.0;
    return sunTransmit * inscatter * (1.0 - exp(-extinction * viewMass * 0.6)) / extinction;
}

/// The sky, as a gradient rather than one flat colour.
///
/// **The horizon is exactly the fog colour**, which is what terrain fades to -
/// so the join between the two is invisible and the world reads as continuing
/// rather than stopping at a wall. That constraint is why the scattering model
/// below is used as a **ratio against its own value at the horizon** rather than
/// as an absolute radiance: the shape, the colour shift and the sunset are all
/// real, and the one number the join depends on is still the fog colour exactly.
vec3 skyRadiance(vec3 direction) {
    vec3 horizon = frame.fog.rgb;

    // Underwater and in lava the fog starts at the eye and swallows everything,
    // including the sky. One flat colour is the whole of what should be seen.
    if (frame.eye.w <= 0.0 && frame.fog.w > 0.0) {
        return horizon;
    }

    // **Nothing below the horizon is sky.** Everything down there is the edge of
    // what has been loaded, and the fog colour is exactly what hides it - so the
    // whole lower half is answered with the horizon in the same direction
    // rather than with a model that has no meaning there.
    vec3 above = normalize(vec3(direction.x, max(direction.y, 0.0), direction.z) +
                           vec3(0.0, 1e-4, 0.0));

    // Thinner air the higher the eye is. This is the altitude term: less air
    // overhead means less scattered light, so the zenith deepens toward space
    // and the haze along the horizon drops away.
    float density = exp(-max(frame.eye.y - kSeaLevelHeight, 0.0) / kScaleHeight);

    vec3 sun = frame.sunDirection.xyz;
    vec3 scattered = atmosphericScatter(above, sun, density);
    // The same azimuth at the horizon, so the ratio is one exactly along the
    // skyline in every direction - including the bright part behind the sun.
    //
    // The epsilon is on x and z only, the two the `normalize` can be handed a
    // zero pair of. It used to be `vec3(1e-5)`, which also put 1e-5 back into
    // the y this line has just deliberately set to 0.0 - harmless for any
    // ordinary direction, and at the exact zenith texel, where x and z are both
    // 0, it returned (0.577, 0.577, 0.577): a reference sampled 35 degrees up
    // rather than along the horizon, which is the one place the comment above
    // promises a ratio of one. The twin ten lines up already writes its epsilon
    // on the single axis that needs it.
    vec3 alongHorizon = normalize(vec3(direction.x, 0.0, direction.z) + vec3(1e-5, 0.0, 1e-5));
    vec3 atHorizon = atmosphericScatter(alongHorizon, sun, density);

    vec3 shaped = scattered / max(atHorizon, vec3(1e-5));
    // Overcast air is a diffuser: it scatters so many times that the shaping
    // washes out and the sky goes flat and grey. Rain takes the model out.
    float clear = 1.0 - 0.75 * clamp(frame.weather.x, 0.0, 1.0);
    // **And so does the dark.** Scattered light is only ever as bright as what
    // is doing the lighting, and at night that is the moon at a fifth of the
    // sun. Shaping a full blue gradient out of it is what made a midnight sky
    // read as daytime; with the light this weak the sky is very nearly its own
    // flat colour, and the moon's own halo below is what lifts it.
    float lit = clamp(frame.lighting.y * 2.2, 0.0, 1.0);
    shaped = mix(vec3(1.0), shaped, clear * lit);

    vec3 colour = horizon * clamp(shaped, vec3(0.0), vec3(4.0));

    float toLight = max(dot(above, sun), 0.0);
    // Two lobes: a tight one that is the body's own halo, and a broad one that
    // is what turns half the sky orange at sunset.
    //
    // **The colour is supplied rather than fixed, because the moon owns this
    // too.** One directional light means `sunDirection` is whichever body is
    // up, so a hardcoded warm glow put a sunset around the moon every night.
    // The game sends warm for the sun and a cool white for the moon, at a
    // fraction of the strength, so the night sky stays dark with a white halo.
    colour += frame.glow.rgb * pow(toLight, 24.0) * frame.glow.w * 2.2;
    colour += frame.glow.rgb * pow(toLight, 3.0) * frame.glow.w * 0.30;
    return colour;
}
