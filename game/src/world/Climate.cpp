#include "world/Climate.hpp"

#include "world/Noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

/// One control point of a spline: where it sits, what it evaluates to, and how
/// steeply it is leaving. The derivative is what stops a piecewise curve looking
/// like a set of straight segments joined at corners.
struct SplinePoint {
    float location;
    float value;
    float derivative;
};

/// Cubic Hermite between neighbouring points, **linear extrapolation outside**.
///
/// Extrapolating with the end derivative rather than clamping matters: a clamp
/// puts a dead flat shelf at every extreme of the parameter space, and since
/// the extremes are exactly where mountains and deep ocean live, that shelf is
/// visible as a plateau the size of a biome.
template <std::size_t N>
float evalSpline(const std::array<SplinePoint, N>& points, float x) {
    if (x <= points.front().location) {
        return points.front().value + points.front().derivative * (x - points.front().location);
    }
    if (x >= points.back().location) {
        return points.back().value + points.back().derivative * (x - points.back().location);
    }

    std::size_t i = 0;
    while (i + 1 < N && x > points[i + 1].location) {
        ++i;
    }

    const SplinePoint& a = points[i];
    const SplinePoint& b = points[i + 1];
    const float span = b.location - a.location;
    const float t = (x - a.location) / span;

    // Hermite basis. The derivatives are quoted per unit of the *parameter*, so
    // they are scaled by the span rather than used raw.
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;

    return h00 * a.value + h10 * span * a.derivative + h01 * b.value + h11 * span * b.derivative;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------
// Field spectra
// ---------------------------------------------------------------------------

/// Octave weights, straight from the reference's `noise/*.json`. **A zero is
/// meaningful** — temperature deliberately skips its second octave, which is
/// what keeps thermal regions enormous while still letting them have an edge.
///
/// The reference quotes these against a `firstOctave`, an absolute frequency.
/// Ours are relative to the wavelength below instead, because our world is a
/// quarter of the reference's height and copying its horizontal scale verbatim
/// would give continents whose slopes are a quarter as steep as intended.
constexpr std::array<float, 6> kTemperatureAmplitudes{1.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
constexpr std::array<float, 6> kHumidityAmplitudes{1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
constexpr std::array<float, 9> kContinentalAmplitudes{1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::array<float, 5> kErosionAmplitudes{1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
constexpr std::array<float, 6> kWeirdnessAmplitudes{1.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f};

/// Base wavelength of each field, in blocks.
///
/// **Temperature and humidity are the reference's own numbers, and the three
/// terrain fields are not.** That split is deliberate and is the whole reason
/// deserts stopped appearing as cutouts inside plains. Continentalness, erosion
/// and ridges decide *height*, so shrinking them horizontally is what keeps a
/// slope as steep as the reference's in a world a quarter its height. Nothing
/// like that is true of temperature or humidity: they pick a label and never
/// touch the ground, so there is no slope to preserve and no reason to shrink
/// them. Ours ran temperature at 1536, which put its detail octave at 384
/// blocks — a 200-block hot blob inside a temperate region, which is exactly
/// what a desert cutout is.
///
/// The reference: temperature `firstOctave -10` at `xz_scale 0.25` = 4096
/// blocks, vegetation `-8` at 0.25 = 1024. Both have **two octaves and nothing
/// smaller**, which is what makes a climate region big and blobby instead of
/// speckled.
constexpr float kTemperatureWavelength = 4096.0f;
constexpr float kHumidityWavelength = 1024.0f;
constexpr float kContinentalWavelength = 1792.0f;
constexpr float kErosionWavelength = 1280.0f;
constexpr float kWeirdnessWavelength = 704.0f;

/// Value noise piles up around its midpoint, so a normalised field reaches only
/// about a third of the way to +/-1 on its own and every biome at the edge of
/// the parameter space would be unreachable. This stretches it back out.
///
/// **Per field, and measured rather than guessed.** A temporary startup probe
/// sampled a 2048-block square and reported each field's mean absolute value; a
/// single shared gain of 2.6 left humidity at 0.65, meaning it spent almost all
/// its time clamped at one extreme or the other — so the three middle humidity
/// bands were nearly empty and forest, taiga and dense forest together came to
/// under 7% of the world while badlands alone took 6.5%. Each field now carries
/// the gain that puts its mean near 0.40, which is where the bands are evenly
/// used. Applied after normalisation, so the octave weights stay the
/// reference's.
constexpr float kTemperatureGain = 2.2f;
constexpr float kHumidityGain = 1.35f;
constexpr float kContinentalGain = 2.8f;
/// **Lowered from 2.35 after a playtest reported too many peaks.** The band
/// edges are the reference's, and it draws them across a bell-shaped field;
/// stretching ours toward uniform made every band at an end of an axis roughly
/// twice as likely as intended. Erosion's low end is exactly "mountain
/// country", so it was the one where that showed. Measured: mean |v| 0.41 with
/// the old gain against the reference's own ~0.28.
constexpr float kErosionGain = 1.75f;
constexpr float kWeirdnessGain = 2.4f;

template <std::size_t N>
float field(std::uint32_t seed, float worldX, float worldZ, const std::array<float, N>& amplitudes,
            float wavelength, float gain) {
    const float scale = 1.0f / wavelength;
    const float raw = noise::octaves2D(seed, worldX * scale, worldZ * scale, amplitudes.data(),
                                       static_cast<int>(N));
    return std::clamp(raw * gain, -1.0f, 1.0f);
}

/// The reference's `shift_x` / `shift_z`: every climate sample is taken a few
/// blocks away from where it was asked for, along a short-wavelength noise.
///
/// **This is the only thing stopping a biome boundary being a clean curve.** A
/// box edge in climate space is a contour line of a smooth field, and a contour
/// line is smooth by construction — which is why frozen ground met open water
/// along an edge straight enough to read as a rectangle. The reference shifts
/// all five fields by up to ~15 blocks on a ~32-block noise; ours is scaled to
/// its shorter terrain fields.
constexpr float kShiftWavelength = 32.0f;
constexpr float kShiftBlocks = 7.0f;

// ---------------------------------------------------------------------------
// Shaping splines
// ---------------------------------------------------------------------------

/// Target height by continentalness alone, in blocks. The band edges are the
/// reference's; the heights are ours, mapped through our 96-block world.
///
/// Sea level is 24. Deep ocean at 11 gives thirteen blocks of water to swim
/// down through, and far-inland at 31 leaves the whole of the relief term above
/// it without running out of world.
constexpr std::array<SplinePoint, 7> kContinentalOffset{{
    {-1.10f, 11.0f, 4.0f},
    {-0.455f, 16.0f, 8.0f},
    {-0.19f, 22.0f, 14.0f},
    {-0.11f, 26.0f, 8.0f},
    {0.03f, 29.0f, 5.0f},
    {0.30f, 32.0f, 3.0f},
    {1.00f, 35.0f, 1.0f},
}};

/// How much of the peak profile the land is allowed, by erosion.
///
/// The kink at 0.45 -> 0.55 is the reference's and is not a mistake: erosion
/// band 5 is a narrow window where relief comes *back*, and it is what produces
/// windswept and shattered terrain rather than another flat plain.
constexpr std::array<SplinePoint, 8> kErosionRelief{{
    {-1.00f, 1.00f, 0.0f},
    {-0.78f, 0.92f, 0.30f},
    {-0.375f, 0.62f, 0.60f},
    {-0.2225f, 0.42f, 0.55f},
    {0.05f, 0.22f, 0.30f},
    {0.45f, 0.09f, 0.0f},
    {0.55f, 0.17f, 0.0f},
    {1.00f, 0.05f, 0.0f},
}};

/// Blocks of relief by peaks-and-valleys, before erosion scales it.
///
/// The low end is deliberately shallow. A deep trough here puts every upland
/// valley under the sea, which showed up as a third of the snowy slopes coming
/// out as gravel seabed. Rivers get their depth from `kRiverCut` instead, which
/// is the term that is *meant* to reach the waterline.
constexpr std::array<SplinePoint, 6> kPeakProfile{{
    {-1.00f, -4.0f, 2.0f},
    {-0.85f, -2.0f, 5.0f},
    {-0.20f, 2.0f, 11.0f},
    {0.20f, 9.0f, 28.0f},
    {0.70f, 28.0f, 42.0f},
    {1.00f, 44.0f, 32.0f},
}};

/// How hard `offsetY` is enforced, by erosion. Bigger is smoother.
///
/// `kReliefBlocks / factor` is the blocks of vertical wobble the 3D noise is
/// allowed, so 5.6 is about +/-4 — which is the reference's own figure for
/// ordinary land, and it is far tighter than instinct suggests.
///
/// **The whole range used to run down to 1.15 at low erosion, and that was the
/// bug behind the dirt pillars standing out of the water.** Low erosion is a
/// third of the erosion axis, so a third of the world was being given peak-
/// level ruggedness: +/-19 blocks of wobble, enough for the noise to build
/// terrain that the ramp could not connect to the ground. The reference holds
/// 5.1-6.3 across all of this and only drops to 0.625 inside a narrow window
/// of high peaks-and-valleys, which is what `kRuggedFactor` below reproduces.
constexpr std::array<SplinePoint, 7> kErosionFactor{{
    {-1.00f, 4.6f, 0.0f},
    {-0.78f, 4.9f, 0.0f},
    {-0.375f, 5.3f, 0.0f},
    {0.05f, 5.6f, 0.0f},
    {0.45f, 6.0f, 0.0f},
    {0.55f, 3.4f, 0.0f},
    {1.00f, 6.2f, 0.0f},
}};

/// What `factor` becomes where terrain is *allowed* to break up: high peaks-
/// and-valleys on unworn ground, and nowhere else. Even here the ramp beats the
/// 3D noise's vertical slope threefold, so a summit is craggy without ever
/// detaching.
constexpr float kRuggedFactor = 1.4f;

/// Ocean floors are smooth. Nothing about a seabed wants the ruggedness that
/// makes a mountain interesting, and a jagged one is invisible anyway.
constexpr float kOceanFactor = 5.0f;

/// How far a river cuts below the land it crosses, in blocks.
///
/// Applied **outside** the erosion relief term deliberately. Scaling it by
/// relief like everything else looks principled and silently deletes every
/// river on flat ground, which is most of them — a plain has relief 0.2, so a
/// seven-block channel becomes one and a half and never reaches the waterline.
constexpr float kRiverCut = 9.0f;

/// Where the valley band starts and where it bottoms out. `ridges` hits -1 on
/// the zero contour of weirdness, so this is a width in ridge units, not blocks.
constexpr float kRiverOuter = -0.60f;
constexpr float kRiverInner = -1.00f;

/// Jaggedness is a **short**-wavelength height perturbation, not another slow
/// swell. The reference samples its jagged noise at `xz_scale 1500`, which is
/// far finer than anything else in its router, and that is the whole reason a
/// jagged peak is jagged. At 44 blocks the first cut produced summits whose
/// neighbouring columns differed by under half a block, so the `steep` rule
/// fired on 4% of them and the peaks came out as smooth white domes.
constexpr std::array<float, 3> kJaggedAmplitudes{1.0f, 1.0f, 0.5f};
constexpr float kJaggedWavelength = 16.0f;
constexpr float kJaggedBlocks = 13.0f;

} // namespace

float ridgesFromWeirdness(float weirdness) {
    return 1.0f - std::abs(3.0f * std::abs(weirdness) - 2.0f);
}

bool freezesAt(std::uint32_t seed, float warmth, int worldX, int worldY, int worldZ) {
    // Every number here is the reference's own, in the reference's own units,
    // because `warmth` is too. Only the vertical scale is converted: its sea
    // level + 17 is our y 29, and one of our blocks covers 3.57 of its own.
    constexpr float kFreezePoint = 0.15f;
    constexpr float kLapseBase = 29.0f;
    constexpr float kWorldScale = 0.28f;
    constexpr float kLapsePerBlock = 0.00125f / kWorldScale;
    constexpr float kJitterBlocks = 8.0f * kWorldScale;
    constexpr float kJitterWavelength = 8.0f;

    const float altitude = static_cast<float>(worldY) - kLapseBase;
    if (altitude <= 0.0f) {
        return warmth < kFreezePoint;
    }

    const float jitter = noise::value2D(seed ^ 0x1cef00du,
                                        static_cast<float>(worldX) / kJitterWavelength,
                                        static_cast<float>(worldZ) / kJitterWavelength) *
                             2.0f -
                         1.0f;
    return warmth - (altitude + jitter * kJitterBlocks) * kLapsePerBlock < kFreezePoint;
}

Climate climateAt(std::uint32_t seed, int worldX, int worldZ) {
    Climate climate{};

    // Warped once, then every field reads the same displaced point - so the
    // fields stay in register with each other and only their shared boundary
    // wanders.
    const float wx = static_cast<float>(worldX) / kShiftWavelength;
    const float wz = static_cast<float>(worldZ) / kShiftWavelength;
    const float shiftedX = static_cast<float>(worldX) +
                           (noise::value2D(seed ^ 0x5417f7a1u, wx, wz) * 2.0f - 1.0f) * kShiftBlocks;
    const float shiftedZ = static_cast<float>(worldZ) +
                           (noise::value2D(seed ^ 0x5417f7b2u, wx, wz) * 2.0f - 1.0f) * kShiftBlocks;

    // Each field gets its own salt rather than its own seed sequence, so adding
    // a sixth field later cannot shift the five already here. The reference
    // achieves the same thing by hashing each noise's own name.
    climate.temperature = field(seed ^ 0x7e3411a9u, shiftedX, shiftedZ, kTemperatureAmplitudes,
                                kTemperatureWavelength, kTemperatureGain);
    climate.humidity = field(seed ^ 0x4d01c3a7u, shiftedX, shiftedZ, kHumidityAmplitudes,
                             kHumidityWavelength, kHumidityGain);
    climate.continentalness = field(seed ^ 0xc0217e55u, shiftedX, shiftedZ, kContinentalAmplitudes,
                                    kContinentalWavelength, kContinentalGain);
    climate.erosion = field(seed ^ 0xe6051047u, shiftedX, shiftedZ, kErosionAmplitudes,
                            kErosionWavelength, kErosionGain);
    climate.weirdness = field(seed ^ 0x1dee5a1du, shiftedX, shiftedZ, kWeirdnessAmplitudes,
                              kWeirdnessWavelength, kWeirdnessGain);
    climate.ridges = ridgesFromWeirdness(climate.weirdness);

    return climate;
}

Shape shapeAt(std::uint32_t seed, const Climate& climate, int worldX, int worldZ) {
    const float base = evalSpline(kContinentalOffset, climate.continentalness);

    // Zero out at sea and ramp in across the coast, so an ocean floor is the
    // continental curve alone and never picks up a mountain's relief.
    const float inland = smoothstep(-0.19f, 0.05f, climate.continentalness);

    const float relief = evalSpline(kErosionRelief, climate.erosion);
    const float peak = evalSpline(kPeakProfile, climate.ridges);

    Shape shape{};
    shape.offsetY = base + inland * relief * peak;

    // Rivers fall out of the ridge field's own zero contour and need no system.
    const float river = smoothstep(kRiverOuter, kRiverInner, climate.ridges);
    shape.offsetY -= inland * river * kRiverCut;

    // Jaggedness is peaks-only and mountains-only: everywhere else it is a way
    // of making the whole world look noisy for no gain. The same window is what
    // lets `factor` drop — ruggedness and craggedness are one decision, and
    // splitting them is how a third of the world ended up rugged.
    const float peakiness = std::clamp((climate.ridges - 0.40f) / 0.60f, 0.0f, 1.0f);
    const float unworn = std::clamp((0.20f - climate.erosion) / 0.80f, 0.0f, 1.0f);
    shape.jaggedness = peakiness * unworn * inland;

    const float smooth = climate.continentalness < -0.19f ? kOceanFactor
                                                          : evalSpline(kErosionFactor, climate.erosion);
    shape.factor = smooth + (kRuggedFactor - smooth) * shape.jaggedness;

    if (shape.jaggedness > 0.0f) {
        const float scale = 1.0f / kJaggedWavelength;
        const float rough =
            noise::octaves2D(seed ^ 0x3a99ed01u, static_cast<float>(worldX) * scale,
                             static_cast<float>(worldZ) * scale, kJaggedAmplitudes.data(),
                             static_cast<int>(kJaggedAmplitudes.size()));
        // Absolute value, so roughness only ever adds height. Signed, it eats
        // holes out of the very ridge lines it is meant to sharpen.
        shape.offsetY += shape.jaggedness * std::abs(rough) * kJaggedBlocks;
    }

    return shape;
}

} // namespace game
