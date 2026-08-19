// What makes water read as water rather than as a blue pane of glass.
//
// Include `frame.glsl` first. `frame.water` carries the clock, how steep the
// ripples are and how much sky the surface returns.

#include "waves.glsl"

/// How far light is lost per block travelled through water. A couple of blocks
/// already halves it and twenty leave nothing, which is what makes a pond clear
/// at its edge and solid in the middle.
const float kWaterAbsorption = 0.30;

/// How thin the water has to get before the surface breaks up into foam, in
/// blocks. Shallow enough that it only ever draws the line where water meets
/// land, rather than a band across every pond.
const float kFoamDepth = 0.55;

/// Two independent values from one point. The standard hash22, and it matters
/// here that the two are genuinely independent: deriving a drop's second
/// coordinate from its first lays every drop on a diagonal.
vec2 rippleHash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}

/// The rings rain punches into a water surface.
///
/// Nine tiles around the point, each carrying at most one drop per cycle, its
/// ring expanding and dying as it goes. Returned as a **slope** so it adds
/// straight onto the wave gradient rather than needing its own normal - which
/// is also why the ring is a `cos` scaled by the direction to its centre rather
/// than a height field somebody has to differentiate.
///
/// **The cycle number is part of the hash, not just the tile.** Hashing on the
/// tile alone gives every tile one fixed spot and one fixed moment, so the same
/// four places on the lake are struck over and over for ever - which is exactly
/// how it was reported. Folding the cycle in means the next drop lands
/// somewhere else, at its own strength, and whether a tile is struck at all is
/// re-rolled against how hard it is raining.
///
/// `choppiness` is the wave's own gradient where the drop lands. A ring on a
/// moving swell is swamped by it; on flat water it stands out. That is the
/// cheapest honest way to make the drops answer to the state of the water
/// rather than ignoring it.
vec2 rainRipples(vec2 world, float seconds, float strength, float choppiness) {
    if (strength <= 0.0) {
        return vec2(0.0);
    }

    const float kTile = 3.0;
    const float kCycle = 0.9;
    const float kReach = 1.5;

    vec2 base = floor(world / kTile);
    vec2 slope = vec2(0.0);

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 tile = base + vec2(float(i), float(j));

            // Each tile runs on its own offset, so no two rings start together.
            vec2 fixedHash = rippleHash(tile);
            float turn = seconds / kCycle + fixedHash.x;
            float cycle = floor(turn);
            float t = turn - cycle;

            vec2 where = rippleHash(tile + vec2(cycle * 17.31, cycle * 9.77));
            vec2 what = rippleHash(tile - vec2(cycle * 5.13, cycle * 11.70));

            // Heavier rain strikes more of the tiles, more often.
            if (what.y > 0.20 + 0.65 * strength) {
                continue;
            }

            vec2 centre = (tile + 0.15 + 0.70 * where) * kTile;
            vec2 toCentre = world - centre;
            float distance = length(toCentre) + 1e-4;
            float radius = t * kReach;
            // A tight ring at the wavefront, fading as it spreads.
            float ring = sin((distance - radius) * 9.0) * exp(-abs(distance - radius) * 3.5);
            slope += (toCentre / distance) * ring * (1.0 - t) * (0.6 + 0.8 * what.x);
        }
    }

    // Swamped on a swell, crisp on flat water.
    float calm = 1.0 - clamp(choppiness * 11.0, 0.0, 0.75);
    return slope * strength * 0.22 * calm;
}

///
/// The bright net of light a rippled surface focuses onto whatever lies under
/// it.
///
/// Three crossing wave fields, and the lines are where they all pass through
/// zero at once - which is why this is `abs` raised to a high power rather than
/// a noise lookup. It is not physically derived from the surface above it and
/// does not pretend to be; it is the pattern the eye recognises, for four sines.
float waterCaustics(vec2 world, float seconds) {
    vec2 direction = normalize(vec2(0.94, -0.34));
    float frequency = 1.35;
    float speed = 1.60;

    float sum = 0.0;
    for (int field = 0; field < 3; ++field) {
        sum += abs(sin(dot(world, direction) * frequency + seconds * speed));
        // Eighty degrees apart, so no two fields share a ridge direction.
        direction = vec2(direction.x * 0.1736 - direction.y * 0.9848,
                         direction.x * 0.9848 + direction.y * 0.1736);
        frequency *= 1.21;
        // Reversed each time, or the whole net slides in one direction like a
        // sheet instead of churning.
        speed *= -0.83;
    }

    float ridges = 1.0 - sum * (1.0 / 3.0);
    return pow(max(ridges, 0.0), 4.0);
}

/// True when this fragment was meshed with the water sprite.
///
/// **Asked of the layer the geometry was meshed with, never the redirected
/// one** - the animation swaps in one of thirty-two frames every tenth of a
/// second, and every one of them is still water. The redirection table already
/// names the meshed layer, so this needs nothing new plumbed through.
bool isWaterLayer(float meshedLayer) {
    return frame.animation.x >= 0.0 && abs(meshedLayer - frame.animation.x) < 0.25;
}

/// The surface, as a normal pointing up from a flat sheet.
///
/// Three ripple trains crossing at different angles, each shorter, shallower
/// and slower-moving than the last - which is what deep water actually does,
/// since a gravity wave's speed falls as its wavelength does. Their gradients
/// are analytic, so this is three sines and no texture fetch.
///
/// **The trains are long and the tilt is small, and that is not timidity.** The
/// first attempt ran four trains down to a wavelength under two blocks, and at
/// a grazing angle - which is most of any lake you are looking at - a tilt of a
/// few degrees swings the reflected ray by twice that. Adjacent texels then
/// returned completely different parts of the sky and the whole surface read as
/// coloured static rather than as water.
///
/// **The geometry *is* displaced, and this normal is deliberately steeper than
/// it.** An earlier note here said the surface was not moved at all, which was
/// false and argued for deleting working code: `displacedPosition` in
/// `displace.glsl` raises a fluid-top vertex by `height * frame.water.y *
/// kWaveDisplacement`, and the seam this warned about is prevented instead by
/// only ever moving vertices the mesher marked as a fluid top, so a shoreline's
/// side faces and the seabed stay put.
///
/// The slope below is the wave's *undamped* derivative, while the geometry is
/// raised by `kWaveDisplacement` (0.45) of the wave's height. So the shading
/// normal tilts 1/0.45 = 2.22 times as far as the surface actually does -
/// measured at 2.654 degrees of shading tilt against 1.195 of geometric tilt.
/// That is on purpose: the displacement is kept small so a swell cannot open a
/// visible step against the block edge it is meshed against, while the lighting
/// still needs enough tilt to read as moving water rather than as a flat sheet.
vec3 waterNormal(vec2 world, float seconds, float strength) {
    float height = 0.0;
    vec2 slope = vec2(0.0);
    waterWave(world, seconds, height, slope);
    // Rain lands on water too, and a lake that stays glassy through a downpour
    // is the single most obvious thing missing from one. The wave's own
    // gradient is handed over as how choppy it is here, so a ring on a swell is
    // swamped by it and one on flat water is not.
    //
    // **Rain, not "whatever is falling".** `weather.x` is the intensity of the
    // precipitation and `weather.y` says which kind it is, and this read the
    // first without the second - so every lake, river and pond grew expanding
    // impact rings at rain's full rate throughout a snowstorm. The two shaders
    // that also consume the pair both split on it: `deferred.frag` gates the
    // wet-surface darkening with this exact expression and `precipitation.frag`
    // picks its sprite off `weather.y > 0.5`, so the bank beside the water was
    // correctly not wet while the water was correctly not water. Written in
    // `deferred.frag`'s form rather than as a hard step, so the two stay
    // together through any value between the two states rather than only at
    // the ends.
    slope += rainRipples(world, seconds, frame.weather.x * (1.0 - frame.weather.y), length(slope));
    return normalize(vec3(-slope.x * strength, 1.0, -slope.y * strength));
}

/// How much of what is behind the surface is replaced by what is reflected in
/// it, by Schlick's approximation against water's own 0.02 reflectance.
///
/// **This is the whole reason a lake mirrors the sky at a distance and shows
/// you the bottom at your feet**, and it is not a stylistic choice - the fifth
/// power is what real water does. Looking straight down returns 2%; the last
/// few degrees before the horizon return nearly all of it.
float waterFresnel(vec3 normal, vec3 viewDirection) {
    float cosine = clamp(dot(normal, viewDirection), 0.0, 1.0);
    float grazing = 1.0 - cosine;
    float grazing2 = grazing * grazing;
    return 0.02 + 0.98 * grazing2 * grazing2 * grazing;
}

/// Walks the reflected ray through a copy of the scene taken before any water
/// was drawn, and returns what it hit. `confidence` is 0 when it hit nothing,
/// so the caller can fall back to the sky.
///
/// **Alpha in that copy is the pixel's distance from the eye, not opacity** -
/// which is the whole trick. A step is a hit when the ray has gone further than
/// the surface standing at that pixel, and the sky is parked at a million so it
/// can never be one.
///
/// Two honest limits, both faded rather than hidden. Anything off the edge of
/// the screen was never drawn and cannot be reflected, so the reflection fades
/// out toward the border; and the copy is taken before the sun, the moon and
/// the clouds, so those come from the sky gradient instead.
vec3 marchReflection(sampler2D scene, mat4 viewProjection, vec3 eye, vec3 origin, vec3 direction,
                     out float confidence) {
    confidence = 0.0;

    // Starts clear of the surface, then lengthens - near the water a step has
    // to be short or a shoreline is missed entirely, and far away a short one
    // would never reach anything.
    float travelled = 0.25;
    float step = 0.45;

    for (int i = 0; i < 24; ++i) {
        travelled += step;
        step *= 1.30;

        vec3 point = origin + direction * travelled;
        vec4 clip = viewProjection * vec4(point, 1.0);
        if (clip.w <= 0.0) {
            break;
        }

        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            break;
        }

        vec4 sampled = texture(scene, uv);
        float behind = distance(eye, point) - sampled.a;
        // The upper bound is what stops a ray that passed behind a tree from
        // claiming the ground half a world further on. It is measured against
        // the step, because a long step legitimately overshoots by more.
        if (behind > 0.0 && behind < step * 2.0 + 0.35) {
            float border = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
            confidence = smoothstep(0.0, 0.09, border);
            return sampled.rgb;
        }
    }

    return vec3(0.0);
}
