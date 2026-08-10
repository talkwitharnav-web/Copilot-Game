// The shape of a water surface, in one place.
//
// **Two stages read this and they must not drift.** The vertex stage moves the
// geometry with it and the fragment stage shades with its slope; if they came
// from different constants the light would run down the wrong side of every
// swell, which is the kind of wrongness nobody can name but everyone sees.
//
// Deliberately free of `frame`, so the vertex stage can include it without
// pulling in samplers it has no use for.

/// Height above the flat sheet, and the slope that goes with it.
///
/// Three crossing trains, each shorter, shallower and slower-moving than the
/// last - which is what deep water does, since a gravity wave's speed falls
/// with its wavelength. The slope is the analytic derivative, not a difference
/// of two samples, so it cannot disagree with the height.
///
/// **The trains are long and the tilt is small, and that is not timidity.** An
/// earlier version ran four trains down to a wavelength under two blocks, and
/// at a grazing angle - which is most of any lake you are looking at - a tilt
/// of a few degrees swings a reflected ray by twice that. Adjacent pixels then
/// returned completely different parts of the sky and the surface read as
/// coloured static rather than as water.
void waterWave(vec2 world, float seconds, out float height, out vec2 slope) {
    vec2 direction = normalize(vec2(0.78, 0.62));
    float frequency = 0.30;
    float amplitude = 0.055;
    float speed = 0.30;

    height = 0.0;
    slope = vec2(0.0);

    for (int train = 0; train < 3; ++train) {
        float phase = dot(world, direction) * frequency + seconds * speed;
        height += amplitude * sin(phase);
        slope += direction * frequency * amplitude * cos(phase);
        // Turned about a radian each time, so no two trains line up and the
        // pattern never reads as stripes.
        direction = vec2(direction.x * 0.5403 - direction.y * 0.8415,
                         direction.x * 0.8415 + direction.y * 0.5403);
        frequency *= 1.7;
        amplitude *= 0.55;
        speed *= 1.2;
    }
}

/// How far the surface is actually allowed to move, against the height the
/// trains produce.
///
/// **A water source's top sits exactly at the block boundary**, level with the
/// grass beside it. The mesher pins every surface vertex that touches anything
/// other than water, so this only ever moves open water and the shoreline stays
/// exactly flush - which is the only arrangement that neither lifts the
/// waterline above a bank nor drops it below one.
const float kWaveDisplacement = 0.45;
