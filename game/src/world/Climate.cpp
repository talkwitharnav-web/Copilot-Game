#include "world/Climate.hpp"

#include "world/Noise.hpp"
#include "world/Weather.hpp"

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

/// Every spline table's `location` column must ascend strictly.
///
/// `evalSpline` depends on that twice over: its scan walks forward assuming
/// order, and `span` divides by the gap between neighbours, so an equal pair is
/// a division by zero. But the failure this actually exists to catch is much
/// quieter. **Each table below states its own length, and writing one row fewer
/// than the stated count is legal C++** - the tail is value-initialised to
/// `{0, 0, 0}`. That phantom point lands *below* every real location at the end
/// of an ascending table, so `points.back().location` becomes 0, and every `x`
/// above zero takes the linear-extrapolation branch off a zero value with a
/// zero derivative. **Half the parameter space flattens to height 0**, from a
/// `constexpr` table, with no warning and no crash.
///
/// `Biome.cpp` guards its own table against the identical hazard with
/// `everyRowIsFilledIn`, which works because a `Biome`'s zero state has a null
/// name. A `SplinePoint`'s zero state is a perfectly legal-looking point, so
/// **ordering is the only signature the phantom row has** - which is why the
/// guard is spelled this way rather than as a fill test.
///
/// Falsified by any spline table whose stated length exceeds the number of rows
/// actually written under it; search `std::array<SplinePoint,` for the set
/// rather than trusting a list written here.
template <std::size_t N>
constexpr bool locationsAscend(const std::array<SplinePoint, N>& points) {
    for (std::size_t i = 0; i + 1 < N; ++i) {
        if (!(points[i].location < points[i + 1].location)) {
            return false;
        }
    }
    return true;
}

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
/// meaningful** - temperature deliberately skips its second octave, which is
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
/// blocks - a 200-block hot blob inside a temperate region, which is exactly
/// what a desert cutout is.
///
/// The reference: temperature `firstOctave -10` at `xz_scale 0.25` = 4096
/// blocks, vegetation `-8` at 0.25 = 1024. Both have **two octaves and nothing
/// smaller**, which is what makes a climate region big and blobby instead of
/// speckled.
///
/// **These wavelengths are in blocks, and any probe that samples this file must
/// be wider than the widest of them or it will manufacture a bug.** Paid for on
/// 2026-08-19: a 2048x2048 biome-frequency survey covers **0.50 of the 4096-block
/// temperature period**, so it sees one thermal blob and reports **9 of 30 biomes
/// as never appearing** -- every hot row, which reads exactly like a real
/// cold-bias bug. Three temperature samples inside that window all came back
/// negative and read as corroboration; they were one reading, not three. The same
/// probe widened, changing nothing else: **4 periods -> 0 zero-occurrence biomes,
/// 32 periods -> 0, and 8 seeds pooled over 524288 samples -> 0, with a
/// temperature mean of +0.00154 and a symmetric bell** (0.54% in each outermost
/// 0.1 bin). Eight seeds over the *same* 2048 window give spawn means from
/// **-0.374 to +0.476** -- seed 0x0BADF00D spawns entirely warm -- so the origin
/// carries no bias either. Seeing 8-11 biomes absent from a 2048-block window is
/// **correct**: half a climate period cannot contain 30 biomes, and the reference
/// behaves the same way. Measure across at least 4 periods (16384 blocks).
constexpr float kTemperatureWavelength = 4096.0f;
constexpr float kHumidityWavelength = 1024.0f;
constexpr float kContinentalWavelength = 1792.0f;
constexpr float kErosionWavelength = 1280.0f;
constexpr float kWeirdnessWavelength = 704.0f;

/// Value noise piles up around its midpoint, so a normalised field reaches only
/// about a third of the way to +/-1 on its own and every biome at the edge of
/// the parameter space would be unreachable. This stretches it back out.
///
/// **Per field, and measured rather than guessed.** A single shared gain of 2.6
/// left humidity at a mean |v| of 0.65, meaning it spent almost all its time
/// clamped at one extreme or the other - so the three middle humidity bands were
/// nearly empty and forest, taiga and dense forest together came to under 7% of
/// the world while badlands alone took 6.5%. Applied after normalisation, so the
/// octave weights stay the reference's.
///
/// **The target is the reference's own mean |v| of ~0.28.** An earlier note here
/// said 0.40 and no field met either number: measured over 465k columns (stride
/// 48, +/-16384 blocks, seed 0x5eed1234) the five means were T 0.547, H 0.348,
/// C 0.356, E 0.321, W 0.509. The band edges the biome table cuts against are
/// the reference's own, and it draws them across a bell-shaped field; stretching
/// ours toward uniform makes every band at an end of an axis roughly twice as
/// likely as intended, which is the same disease the 2.6 gain had.
///
/// **Temperature was 2.2 and is 1.2.** At 2.2, 18.9% of the world sat clamped at
/// exactly +/-1, where the field carries no information at all and the biome
/// choice collapses onto humidity alone; at 1.2 that is 0.65%. Scored against
/// minecraft.wiki's published overworld surface-area cover table
/// [https://minecraft.wiki/w/Biome] folded onto our 30 rows, the count of biomes
/// within 2x of their reference share goes 12 -> 19 of 30 (the peak of a sweep
/// from 1.00 to 2.20 in steps of 0.05) and the total variation 77.3 -> 49.4
/// percentage points. The two groups temperature actually owns both move toward
/// the reference: snowy plains + snowy taiga 13.25% -> 6.54% against 5.43, and
/// forest + taiga + dense forest + plains 14.43% -> 24.56% against 35.63. The
/// cost is the hot end, desert + badlands + savanna 11.54% -> 7.36% against
/// 8.85 - an error of 0.83x replacing one of 1.30x.
///
/// **Height-neutral by construction, and checked rather than assumed.**
/// `shapeAt` reads continentalness, erosion and ridges and never temperature, so
/// `kSurfaceAtOrAbove` in `TerrainGenerator.cpp` cannot move, and a re-measured
/// height CDF, surface min/max/mean, steep-rule share and cave-carved share came
/// back line-for-line identical. **The ore table's `measured` column is not quite
/// neutral and it would have been easy to claim it was**: a re-census over the
/// same 361 columns moved six of the nine rows by at most 0.1% (coal 282.47 ->
/// 282.72 blocks per 32x32 column), because fewer desert columns means less
/// sandstone under the sand and a little more stone left for an ore to be written
/// into. Nothing near the ordering the pairwise asserts below the table check.
/// **Erosion and weirdness are not the same case.** Their means are also above
/// the target and their own groups are still wrong - the eight upland biomes hold
/// 12.45% of the world against a reference 3.64%, at every temperature gain in
/// the sweep, and rivers 2.9% against 6.2% - but moving either gain moves every
/// column's height, so that pass has to re-measure the height CDF and all nine
/// ore counts with it.
constexpr float kTemperatureGain = 1.2f;
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
/// line is smooth by construction - which is why frozen ground met open water
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
///
/// **Every derivative here is a chord slope, and zero where the chords change
/// sign.** That is the Fritsch-Carlson condition for a monotone cubic, and it
/// is what stops the curve overshooting between its own control points. The
/// signs used to be copied from an increasing spline while the values fall from
/// 1.00 to 0.05, so relief *rose* to about 0.226 around E = 0.11 - a swell in
/// the middle of the flattest erosion band, worth only 0.006 today but pointing
/// the wrong way, which is the sort of thing a later edit multiplies. Use the
/// smaller of the two chords a point joins, and 0 at a turning point.
constexpr std::array<SplinePoint, 8> kErosionRelief{{
    {-1.00f, 1.00f, -0.36f},
    {-0.78f, 0.92f, -0.36f},
    {-0.375f, 0.62f, -0.74f},
    {-0.2225f, 0.42f, -0.73f},
    {0.05f, 0.22f, -0.33f},
    {0.45f, 0.09f, 0.0f},
    {0.55f, 0.17f, 0.0f},
    {1.00f, 0.05f, -0.27f},
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
/// allowed, so 5.6 is about +/-4 - which is the reference's own figure for
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

// **The negative control comes first, because an assert that cannot fail is
// worse than no assert.** A `SplinePoint` zero state is a legal-looking point,
// so this builds the exact corpse a short initialiser leaves - two real rows
// and a value-initialised tail - and requires the guard to reject it. If
// `locationsAscend` is ever weakened to `<=`, or to a test that ignores the
// last pair, this line fails before any real table does.
static_assert(!locationsAscend(std::array<SplinePoint, 3>{{{-1.0f, 1.0f, 0.0f},
                                                           {1.0f, 2.0f, 0.0f},
                                                           {}}}),
              "locationsAscend must reject a value-initialised tail row, or the four asserts "
              "below prove nothing");

static_assert(locationsAscend(kContinentalOffset),
              "kContinentalOffset: locations must ascend strictly - check its stated length "
              "against the rows actually written under it");
static_assert(locationsAscend(kErosionRelief),
              "kErosionRelief: locations must ascend strictly - check its stated length against "
              "the rows actually written under it");
static_assert(locationsAscend(kPeakProfile),
              "kPeakProfile: locations must ascend strictly - check its stated length against "
              "the rows actually written under it");
static_assert(locationsAscend(kErosionFactor),
              "kErosionFactor: locations must ascend strictly - check its stated length against "
              "the rows actually written under it");

/// What `factor` becomes where terrain is *allowed* to break up: high peaks-
/// and-valleys on unworn ground, and nowhere else. Even here the ramp beats the
/// 3D noise's vertical slope threefold, so a summit is craggy without ever
/// detaching.
constexpr float kRuggedFactor = 1.4f;

/// Ocean floors are smooth. Nothing about a seabed wants the ruggedness that
/// makes a mountain interesting, and a jagged one is invisible anyway.
constexpr float kOceanFactor = 5.0f;

/// Where the sea gives way to land, and how far the handover takes.
///
/// **Everything that changes across the coast has to use this one ramp.**
/// `inland` was already a smoothstep across it while `factor` was a hard
/// `continentalness < -0.19` branch two lines below, so the wobble multiplier
/// stepped 4.4 -> 6.5 at erosion 0.55 while the relief beside it was still
/// ramping - worth up to two blocks of height, along the whole of the C =
/// -0.19 contour. A contour of a smooth field is a curve, so that is the
/// "someone drew a line on the world" shape rather than a patch of noise.
///
/// The low edge is the same number `Biome.cpp` calls `kOceanC`: the boundary
/// between the ocean rows and the coastal strip. It is written twice because
/// the biome table is a table of boxes and this is a spline input, and neither
/// file can hand the other a constant without the biome table depending on the
/// shaping code.
constexpr float kCoastRampLow = -0.19f;
constexpr float kCoastRampHigh = 0.05f;

/// How far a river cuts below the land it crosses, in blocks.
///
/// Applied **outside** the erosion relief term deliberately. Scaling it by
/// relief like everything else looks principled and silently deletes every
/// river on flat ground, which is most of them - a plain has relief 0.2, so a
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
    // because `warmth` is too. **The vertical conversion is the one exception
    // and it has a single owner** - `weather::kWarmthPerBlock` and
    // `weather::kWarmthBaseHeight` - because the falling rain runs the same
    // rule, and while the two carried different numbers a mountain generated
    // snow-capped and was then rained on. Its sea level + 17 is our y 29, and
    // one of our blocks covers 3.57 of its own.
    constexpr float kFreezePoint = weather::kFreezingWarmth;
    constexpr float kLapseBase = weather::kWarmthBaseHeight;
    constexpr float kLapsePerBlock = weather::kWarmthPerBlock;
    // Eight of the **reference's** blocks of altitude, so it scales by the same
    // factor the lapse does: 8 * 0.28 = 2.24 of ours.
    constexpr float kJitterBlocks = 8.0f * weather::kWorldScale;
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
    const float inland = smoothstep(kCoastRampLow, kCoastRampHigh, climate.continentalness);

    const float relief = evalSpline(kErosionRelief, climate.erosion);
    const float peak = evalSpline(kPeakProfile, climate.ridges);

    Shape shape{};
    shape.offsetY = base + inland * relief * peak;

    // Rivers fall out of the ridge field's own zero contour and need no system.
    const float river = smoothstep(kRiverOuter, kRiverInner, climate.ridges);
    shape.offsetY -= inland * river * kRiverCut;

    // Jaggedness is peaks-only and mountains-only: everywhere else it is a way
    // of making the whole world look noisy for no gain. The same window is what
    // lets `factor` drop - ruggedness and craggedness are one decision, and
    // splitting them is how a third of the world ended up rugged.
    const float peakiness = std::clamp((climate.ridges - 0.40f) / 0.60f, 0.0f, 1.0f);
    const float unworn = std::clamp((0.20f - climate.erosion) / 0.80f, 0.0f, 1.0f);
    shape.jaggedness = peakiness * unworn * inland;

    // Blended across the coast on the **same** ramp `inland` uses, rather than
    // switched at its low edge: at C <= kCoastRampLow this is exactly
    // kOceanFactor and at C >= kCoastRampHigh exactly the erosion spline, which
    // is what the branch gave, and in between it now arrives instead of
    // stepping.
    const float land = evalSpline(kErosionFactor, climate.erosion);
    const float smooth = kOceanFactor + (land - kOceanFactor) * inland;
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
