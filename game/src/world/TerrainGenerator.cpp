#include "world/TerrainGenerator.hpp"

#include "world/Biome.hpp"
#include "world/Climate.hpp"
#include "world/Noise.hpp"
#include "world/Structures.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

// ---------------------------------------------------------------------------
// Terrain height
// ---------------------------------------------------------------------------

constexpr int kWorldHeight = kWorldHeightChunks * Chunk::kSize;
constexpr int kWorldTop = kWorldHeight - 1;

/// The surface never reaches the bedrock floor or the build ceiling. These
/// replace the reference's top and bottom density slides, which exist to do
/// exactly this and are only needed when the answer is a density field rather
/// than a height.
///
/// **`kMinSurface` was 4 and was a DEAD CLAMP - finding 981 measured the real
/// minimum over 1,048,576 columns at y 7, so the lower arm of the `std::clamp`
/// below never once bound.** It is 7 now, and the change is deliberately a
/// no-op on today's terrain: for every column whose raw height already floors
/// to 7 or more, `std::clamp(v, 7, 90)` and `std::clamp(v, 4, 90)` return the
/// identical value, and by measurement no column floors below 7.
///
/// **What it buys is a guarantee rather than a behaviour.** `kLavaLevel` is 6,
/// and the lava fill writes into carved cells at or below it. A surface at 4
/// would sit *underneath* the lava level, and the bottom of that hollow would
/// be a lake of fire open to the sky. Raising the clamp to 7 makes
/// `surface > kLavaLevel` structurally true instead of true-by-measurement,
/// and the `static_assert` beside `kLavaLevel` now says so out loud.
///
/// **The 3-block margin is not the safety net - the fill's own test is.** That
/// loop still asks `worldY > surface` before placing anything, because the y-7
/// measurement is a fact about today's splines and not a promise, and a spline
/// edit is exactly the change that would never think to check a lava constant.
/// Two independent guards, which is the point.
///
/// > Fails if: a future spline genuinely wants a hollow below y 7. Then this
/// > clamp starts binding and flattening it, and the honest fix is to lower
/// > `kLavaLevel` with it rather than to lower this alone.
constexpr int kMinSurface = 7;
constexpr int kMaxSurface = 90;

/// Where the height stops being itself and starts bending towards the ceiling.
///
/// **A hard clamp is a dead-flat plateau, and the tail it flattens is the exact
/// tail the mountain generator exists to produce.** The parts sum to 35 + 44 +
/// 13 + 15.7 = 107.7 at the extreme, so `kMaxSurface` is genuinely reachable
/// and the summit that reached it would come out as a table top at precisely
/// y 90 - every peak in the range sheared to the same altitude.
///
/// So bend instead of cutting: above the soft start, `h` becomes
/// `kMaxSurface - span * exp(-(h - start) / span)`, which is asymptotic to the
/// ceiling and never touches it. Choosing `span = kMaxSurface - start` makes
/// the derivative exactly 1 at the join, so the bend is invisible where it
/// begins - a summit is compressed, not clipped.
///
/// 84 is measured, not chosen: across 708k sampled columns the tallest was
/// 86.3, so the bend costs the tallest real peak about 0.4 blocks while leaving
/// 99.99% of the world bit-identical to the unbent height. The `std::clamp`
/// stays below it as a now-unreachable guard - it costs nothing and it is the
/// thing that would catch a future spline handing us an infinity.
constexpr float kCeilingSoftStart = 84.0f;

/// Spectrum of the noise that displaces the target height.
///
/// **Halving amplitude per octave is the reference's shape and it matters more
/// than it looks.** Its octaves carry amplitude proportional to wavelength, so
/// every octave contributes the same vertical slope and the total stays gentle.
/// The first cut here used `{1, 1, 0.5, 0.25}`, which puts most of the energy
/// in the short wavelengths and produced terrain the ramp could not connect to
/// the ground.
constexpr std::array<float, 4> kBaseAmplitudes{1.0f, 0.5f, 0.25f, 0.125f};
constexpr float kBaseWavelengthXZ = 84.0f;

/// The noise is read at the column's own target height rather than at a fixed
/// plane, which decorrelates a mountain's texture from a lowland's for free.
constexpr float kBaseWavelengthY = 210.0f;

/// Blocks of **signed** fall across a **two-column** span - the column one step
/// south against the column one step north - above which that facing is steep.
///
/// **The reference's number, unconverted, because this is the one quantity on
/// this page the world scale does not touch.** `SurfaceRules.Steep` is
/// literally `getHeight(x, z+1) >= getHeight(x, z-1) + 4` over those same two
/// columns, and `+Z` is *south*: `Block.hpp`'s `lidTurnsFacing` pins
/// `facing=north` to zero rotation and to `NegZ`, so it is the **south**
/// neighbour that has to stand 4 higher. An earlier version of this paragraph
/// had the two names the wrong way round while the code was right, which is a
/// reader one "fix" away from frosting the wrong side of every mountain. Every
/// other ported figure here is a *height* or a *depth* and has to be squeezed
/// into a 96-block world against the reference's 384, but a slope is blocks of
/// fall per blocks of run and **a block is the same size in both worlds** - the
/// conversion is 4 x (1 block / 1 block) = 4. The scale factor would only enter
/// if the span were a fraction of the world rather than two fixed columns.
///
/// It was 2, from a comment that divided the fall by the height ratio and left
/// the run alone. Neither reading of that comment produces 2 either: a quarter
/// of 96 is 1, and the fraction-of-relief argument gives 4 outright. Measured
/// on 11,735 columns of the three biomes that read `steep`, a fall of 4 or more
/// happens on 3.5% of them, so the rule still fires and now marks the faces
/// that are genuinely cliff-like rather than every gentle hillside.
///
/// **This is not the number `Structures.cpp` uses to refuse a tree, and making
/// the two share one would not make them agree.** That test is an *absolute*
/// difference across a *single* column, asked of four neighbours; this one is a
/// *signed* difference across *two*, asked once. Same English word, different
/// question - so each site owns a constant whose name says what it measures.
constexpr int kSteepDropOverTwoColumns = 4;

/// How far below the waterline the bed is still soil rather than gravel.
///
/// **The reference's `water offset -6`, converted, because a depth below sea
/// level is exactly the kind of number the scale eats.** Its seabeds run from
/// y -64 to the waterline at 63, which is 127 blocks of drowned column; ours
/// run from the bedrock floor to y 24, which is 24. So one of our blocks is
/// 127/24 = 5.3 of the reference's below the waterline, and six of its blocks
/// is 6 x 24/127 = 1.13 -> **1** of ours.
///
/// Used raw at 6 it swallowed everything: our ocean floor sits 2-8 blocks down,
/// every river bed 1-4, and the drowned half of every beach - so the whole sea
/// bed was "shallow" and there was no gravel anywhere. At 1 the shelf is a
/// shelf again.
///
/// **The consequence is a big visual swing and it is the faithful one.** With
/// `submerged` meaning `surface < 24`, this reduces to `surface == 23` exactly,
/// so only one drowned column in seventeen is shallow and the rest of every
/// non-sandy bed is gravel: measured, 73.8% gravel, 8.1% sand, 2.5% dirt over
/// 49,562 drowned columns. That is what the reference looks like - `ocean`,
/// `cold_ocean` and `frozen_ocean` and their deep variants all floor in gravel,
/// only `warm_ocean` and `lukewarm_ocean` floor in sand, and a river is gravel
/// down the channel with a dirt fringe where it is shallow enough to fail the
/// -6 test. **Our rivers being 1-4 deep is 5-21 of the reference's**, which is
/// past that cutoff for all but the fringe, so gravel down the middle is the
/// converted answer and not an accident of rounding.
///
/// **Tagging `River` to force dirt was the alternative and it is the wrong
/// one**: it would hard-code a bed material the reference derives, and it would
/// still leave every lake and every cold ocean shelf to this rule. The rule
/// this constant expresses has only three reachable values here - 0, 1 or 2 -
/// so if the fringe ever needs to be wider, widen it here and say why.
constexpr int kShallowBedDepth = 1;

/// The 2D field that scatters a biome's patches over its ordinary top block.
///
/// 64 blocks is the reference's `noise/surface.json` — `firstOctave -6` with
/// three octaves, so 64/32/16 — and it takes no `xz_scale`, which makes this one
/// of the few numbers we can copy outright. At 34 the same patches came out
/// half the size and read as streaks rather than as patches.
constexpr std::array<float, 3> kPatchAmplitudes{1.0f, 0.6f, 0.3f};
constexpr float kPatchWavelength = 64.0f;

// ---------------------------------------------------------------------------
// Caves
// ---------------------------------------------------------------------------

/// **Spaghetti** — long winding tunnels, taken as a band either side of a
/// signed field's zero crossing. Thresholding the field itself instead gives
/// disconnected blobs, which read as holes rather than as caves.
constexpr std::array<float, 3> kSpaghettiAmplitudes{1.0f, 0.6f, 0.3f};
constexpr float kSpaghettiWavelength = 58.0f;
constexpr float kSpaghettiYStretch = 0.62f;
constexpr float kSpaghettiWidth = 0.060f;

/// **Cheese** — occasional open caverns, a low-frequency field thresholded
/// high. Separate from the spaghetti because the two have completely different
/// surface-area budgets, which is exactly why the reference keeps three systems
/// rather than one: geometry cost tracks cave *surface*, not hollow volume.
constexpr std::array<float, 2> kCheeseAmplitudes{1.0f, 0.5f};
constexpr float kCheeseWavelength = 104.0f;
constexpr float kCheeseYStretch = 0.72f;
constexpr float kCheeseThreshold = 0.44f;

/// **Noodle** — thin branching passages that break up the cheese. Gated on a
/// low-frequency patchiness field so they arrive in clusters instead of
/// riddling the entire underground.
constexpr std::array<float, 2> kNoodleAmplitudes{1.0f, 0.5f};
constexpr float kNoodleWavelength = 27.0f;
constexpr float kNoodleWidth = 0.032f;
constexpr std::array<float, 2> kNoodlePatchAmplitudes{1.0f, 0.5f};
constexpr float kNoodlePatchWavelength = 190.0f;
constexpr float kNoodlePatchThreshold = 0.10f;
constexpr int kNoodleCeiling = 34;

/// No caves within this distance of the surface, fading in below it. Without it
/// tunnels breach open ground constantly and the landscape reads as rotten
/// rather than as hollow.
constexpr int kCaveSurfaceMargin = 6;
constexpr int kCaveFadeDepth = 10;

/// The same for caverns, and it is **never** relaxed by an entrance. A tunnel
/// that surfaces is a cave mouth; a cavern that surfaces is a crater.
constexpr int kCavernSurfaceMargin = 12;

/// Where that margin is allowed to close to nothing, which is a cave mouth.
/// A 2D field, so an entrance is a *place* you can come back to rather than
/// something that happens at random along a tunnel.
///
/// **The threshold is quoted against a measured distribution, not against
/// [-1, 1].** Two octaves of value noise have a mean |v| of 0.27 and clear 0.46
/// on 8.6% of columns — so the old 0.46 with a 0.22 ramp needed 0.68 to open
/// fully, which is the top 1-2% of the field, and cave mouths were rare enough
/// that a playtest found none at all. 0.26 with a 0.14 ramp opens fully above
/// 0.40, which the same measurement puts at 12% of the world.
constexpr std::array<float, 2> kEntranceAmplitudes{1.0f, 0.5f};
constexpr float kEntranceWavelength = 230.0f;
constexpr float kEntranceThreshold = 0.26f;
constexpr float kEntranceWidth = 0.14f;

/// The world's floor is never carved, so there is always something to stand on.
constexpr int kBedrockSolid = 0;
constexpr int kBedrockTop = 4;

/// **The lava that floors the deepest caves, and the one thing in this file
/// that had no source at all.** A census of 361 full column stacks - 35,486,208
/// blocks - found ZERO lava, so the fluid worked, the block existed, and nothing
/// ever wrote one. Beyond the missing hazard that meant **obsidian had no
/// natural source**, and every recipe behind obsidian with it.
///
/// **SOURCE, AND IT IS SECONDARY - SAID PLAINLY BECAUSE THE HOUSE RULE IS THAT
/// A PORTED NUMBER NAMES WHAT IT WAS MEASURED AGAINST.** `Mojang/bedrock-samples`
/// has no `blocks/` directory and carries no worldgen at all, so it cannot
/// settle terrain; this is minecraft.wiki plus the aquifer write-up it links,
/// read 2026-08-19. The rule they agree on is the reference's **`lava_level`:
/// below y = -54 an aquifer is always lava rather than water.** That is a hard
/// published anchor rather than a remembered one, which is why it is the arm
/// implemented here and the other two lava sources are filed instead of guessed.
///
/// **THIS IS A MAPPED NUMBER, NOT A PORTED ONE - `CLAUDE.md` bug shape #3.**
/// Dropping -54 into this field would be meaningless: the reference's world runs
/// y -64 to 320 with sea level 63, and ours runs 0 to 95 with `kSeaLevel` 24.
/// The project's own mapping is already on file from the deepslate work: our y5
/// is the reference's -59 (the first block above bedrock) and our y24 is its 63
/// (sea level), so **6.42 reference blocks make one of ours**. Through it,
/// -54 lands at 5 + (-54 + 59) / 6.42 = **y 5.78**. Derived a second way as a
/// sanity check, because one derivation is not a check: the reference's lava
/// sits 5 blocks above a 378-block playable range, and the same fraction of our
/// 91-block range is 1.2 blocks above our floor, i.e. **y 6.2**. The two agree
/// to within a block and 6 is between them.
///
/// **What that buys, and why one layer would have been too literal.** The
/// reference's lava band is 6 blocks deep (-54 down to -59) which is 0.93 of our
/// blocks, so a strict reading gives a single layer. Six gives two - y 5 and
/// y 6 - which is inside the disagreement between the two derivations above and
/// is what makes the floor findable rather than a curiosity. The measured air
/// share underground is 25% at y 6, so this fills roughly a quarter of the cells
/// in those two layers and leaves the rock alone.
///
/// **What would make this wrong**, so it can be checked rather than trusted:
/// `kSeaLevel` or `kBedrockTop` moving, which changes the mapping and therefore
/// this number; or a primary worldgen source appearing for Bedrock, which would
/// replace the secondary one above outright.
constexpr int kLavaLevel = 6;
static_assert(kLavaLevel > kBedrockTop,
              "lava must sit above the bedrock floor or it can never be carved into");
static_assert(kLavaLevel < kSeaLevel,
              "lava belongs to the deep, below the waterline - if these ever cross, the ocean fill "
              "and the lava fill are fighting over the same cells");
static_assert(kMinSurface > kLavaLevel,
              "the terrain floor must stay above the lava level, or the bottom of a hollow is a "
              "lake of fire open to the sky - see finding 981, which is why kMinSurface is 7");

/// Stone gives way to deepslate across this band rather than at a line, on a
/// per-block roll — the reference's `vertical_gradient`, which is one of the
/// very few places it uses a genuine dice roll instead of a noise field.
constexpr int kDeepslateAlways = 9;
constexpr int kDeepslateNever = 15;

/// How far the sand of a desert turns to sandstone before it reaches stone.
constexpr int kSandstoneDepth = 3;

/// A flower region is 64 blocks across, and this fraction of its blooms are some
/// other species. Both together are what make a meadow read as mostly one
/// flower without being a monoculture with a seam at the region edge.
constexpr int kFlowerRegionShift = 6;
constexpr float kFlowerStrays = 0.30f;

/// How wide a stand of ground cover is, and how bare the gaps between them get.
/// Squaring the field is what puts most of the world at the thin end and gives
/// the thick patches somewhere to stand out from.
constexpr float kCoverPatchWavelength = 42.0f;
constexpr float kCoverPatchFloor = 0.30f;

/// How far below a column's top the surface rules can still reach. Only used to
/// bound the classification walk; the counter itself is exact.
constexpr float kSurfaceDepthWavelength = 12.0f;

// ---------------------------------------------------------------------------
// Ore veins
// ---------------------------------------------------------------------------

/// Where each ore appears, how many veins are attempted, and how big each is.
///
/// **A vein is a placed feature with a hard size cap, not a thresholded noise
/// field.** The old version tested a smooth 3D field against a high threshold,
/// which has no cap at all: wherever the field happened to stay high, the blob
/// kept growing, and a playtest found coal seams the size of a room. The
/// reference never does this for ore — it attempts `count` veins per chunk and
/// each one lays down at most `size` blocks. The largest vein_size anywhere in
/// vanilla is 20, and nothing it generates can exceed 52 blocks.
///
/// The bands are ours, mapped onto our world: it runs y 0-96 with sea level 24,
/// against the reference's -64 to 320 with sea level 63, so depths below sea
/// level compress by about 0.17 and heights above it by 0.28.
///
/// **`attempts` is quoted per 32x32 column, not per chunk.**
///
/// **The reference counts per 16x16** - one of our columns is four of its
/// chunks - so these are *not* its numbers and reading them as if they were
/// will quadruple whatever is ported next. They were set by measuring realised
/// block counts against a previous session's, because the reported problem was
/// the size of a seam and not how much ore there is. A column rather than a
/// chunk is the unit here because a vein's height comes from its own band
/// rather than from whichever of the three stacked chunks is asking.
struct OreVein {
    BlockId block;
    std::uint32_t salt;
    /// The reference's `vein_size`: how many spheres are strung along the
    /// spindle. Realised block counts land near half of it.
    int size;
    int attempts;
    int minY;
    int peakY;
    int maxY;
    /// Refuses to generate against open air, so it cannot be spotted from
    /// inside a cave. The reference's `discard_chance_on_air_exposure`, tested
    /// **per block** rather than per vein, and what makes the rarest ores
    /// something you dig for rather than find.
    float airDiscard;
    /// Blocks of this ore a 32x32 column actually ends up holding.
    ///
    /// **Measured, never derived, and it is the key this table is sorted by.**
    /// 361 columns, seed 0x5eed1234, counted out of finished chunks. Nothing
    /// short of generating the world can predict it, and the attempt to try is
    /// what produced the bug this field exists to close: an estimator built
    /// from `attempts * size * P(rock at that height)` put emerald at three
    /// times diamond, and the truth is *half* of it, because `airDiscard = 1.0`
    /// throws away every emerald block that ends up beside a cave and emerald's
    /// band is high enough that most of them do. Air exposure is a property of
    /// the finished world, so it belongs in a measurement and not in a formula.
    ///
    /// **Re-measure whenever `size`, `attempts`, a band, `airDiscard`,
    /// `veinHeight`'s shape or the terrain heights change** - and whenever a
    /// *surface* rule changes what material the column is made of, which is the
    /// one that does not look like it belongs on this list. Lowering
    /// `kTemperatureGain` moved no height at all and still moved five of these
    /// rows, by at most 0.1% (coal 282.47 -> 282.72): fewer desert columns meant
    /// less sandstone under the sand, and `placeVein` writes only into plain
    /// rock. `kOreCeiling` below catches the half of that which is
    /// one-directional; the rest is on whoever edits the row.
    float measured;
};

/// **Rarest first, by what actually ends up in the ground**, so a common ore can
/// never overwrite a scarce one where their bands overlap. `placeVein` writes
/// only into plain rock, so whichever vein runs first keeps the cell.
///
/// Sizes are the reference's own, shrunk about a quarter: a vein is a gameplay
/// unit measured against the player, so it does not scale with world height the
/// way the bands do.
///
/// **Reading the invariant off the `attempts` column is what made it false, and
/// reading it off a formula is what kept it false.** Emerald has eighteen times
/// diamond's attempt count, which reads as a rule broken; an estimator built
/// from attempts, size and how often there is rock at that height agreed, and
/// put emerald at three times diamond. A census of finished chunks says emerald
/// is *half* of diamond - `airDiscard = 1.0` and a band high enough that most
/// of it ends up beside a cave. Emerald above diamond, where the table has
/// always had it, is right. Copper and iron were genuinely inverted and are the
/// one row pair this ordering pass moved.
///
/// Emerald being in every biome at all is a deliberate deviation from the
/// reference, which restricts it to mountains; ours has no per-biome ore filter
/// and adding one is a table change, not an ordering change.
///
/// Coal's `attempts` is the one count not inherited from the previous session's
/// tuning: giving `veinHeight` its documented shape moved coal's mode down off
/// the band midpoint and back to `peakY`, which put 41% more of it under rock,
/// so 70 -> 99 holds the realised total where that tuning had already put it
/// (283 blocks per column, measured both sides of the change).
constexpr std::array<OreVein, 9> kOreVeins{{
    {BlockId::AncientDebris, 0xd41f07u, 3, 3, 3, 8, 16, 1.0f, 0.86f},
    {BlockId::EmeraldOre, 0x51c3b7u, 3, 36, 10, 46, 62, 1.0f, 2.15f},
    {BlockId::DiamondOre, 0x2f9a41u, 5, 2, 3, 6, 17, 0.7f, 4.25f},
    {BlockId::LapisOre, 0x7b31d9u, 6, 3, 3, 13, 25, 0.0f, 8.72f},
    {BlockId::GoldOre, 0x1de4a3u, 7, 3, 3, 9, 20, 0.5f, 10.88f},
    {BlockId::RedstoneOre, 0x64b8f2u, 7, 10, 3, 5, 17, 0.0f, 35.30f},
    {BlockId::CopperOre, 0x9e271bu, 8, 26, 10, 26, 39, 0.0f, 63.98f},
    {BlockId::IronOre, 0x3ac05eu, 7, 35, 3, 14, 27, 0.0f, 121.61f},
    {BlockId::CoalOre, 0x0c7d86u, 12, 99, 8, 26, 70, 0.5f, 282.47f},
}};

/// The share of world columns whose surface stands at or above a given height,
/// every fourth block from 0 to 96.
///
/// **Measured, not derived** - 116,964 columns on a stride-48 lattice over
/// +/-8192 blocks, seed 0x5eed1234, taken from this file as it stands. It is
/// here for one reason: a vein rolled above the local surface is rolled into
/// open air and thrown away, so this is the difference between how often an ore
/// is *attempted* and how much of it a player can ever find.
///
/// **Re-measure it if terrain heights move.** It was re-measured after the
/// `kErosionRelief` derivatives, the coastal `factor` ramp and
/// `kCeilingSoftStart` landed - all three move surface heights - and every knot
/// agreed with the run before them to within 0.002, on a lattice two and a
/// quarter times coarser. It feeds `oreBlockCeiling` below, which is a bound
/// rather than an estimate, so drift of that size cannot flip anything.
constexpr int kSurfaceCdfStride = 4;
constexpr std::array<float, 25> kSurfaceAtOrAbove{
    1.0000f, 1.0000f, 1.0000f, 0.9771f, 0.8493f, 0.7217f, 0.6034f, 0.5079f, 0.3597f,
    0.1685f, 0.0855f, 0.0523f, 0.0341f, 0.0219f, 0.0140f, 0.0090f, 0.0052f, 0.0029f,
    0.0014f, 0.0006f, 0.0002f, 0.0001f, 0.0000f, 0.0000f, 0.0000f};

constexpr float surfaceAtOrAbove(int y) {
    if (y <= 0) {
        return 1.0f;
    }
    if (y >= kWorldHeight) {
        return 0.0f;
    }
    const std::size_t i = static_cast<std::size_t>(y / kSurfaceCdfStride);
    const float t = static_cast<float>(y % kSurfaceCdfStride) / static_cast<float>(kSurfaceCdfStride);
    return kSurfaceAtOrAbove[i] + (kSurfaceAtOrAbove[i + 1] - kSurfaceAtOrAbove[i]) * t;
}

// **The `y >= kWorldHeight` line above is the array bounds check, and nothing
// said so.** The interpolation reads `[i + 1]`, so the highest `y` it may be
// handed is `stride * (knots - 1) - 1`, which is 95 - and it is the world
// ceiling that keeps it there, not any property of the table. Those are three
// separately hand-written numbers in two files: `kWorldHeightChunks` (3, in the
// header), `Chunk::kSize` (32, in a file this one does not own) and the pair
// `kSurfaceCdfStride` (4) and `kSurfaceAtOrAbove.size()` (25). 4 * 24 == 3 * 32
// is exact today and is held together by nothing at all.
//
// Raise `kWorldHeightChunks` to 4 and the guard lets `y` reach 127, `i` reaches
// 31 and this reads sixteen floats off the end of a 25-element array. It would
// not even fail immediately: every caller today is `oreBlockCeiling`, which
// walks `[minY, maxY]` and the deepest band tops out at coal's 70, so the fault
// would lie dormant until somebody raised an ore band above 95 - and then
// surface as "expression did not evaluate to a constant" against a
// `static_assert` lambda two hundred lines below the table that is actually
// wrong.
//
// > Fails if: the world gets taller, or the CDF is re-measured at a different
// > stride or knot count. The cure in the first case is to extend the table
// > with the zeros it already ends in, and re-measuring is not needed - a
// > taller ceiling does not move a surface the splines already clamp at 90.
static_assert(kSurfaceCdfStride * static_cast<int>(kSurfaceAtOrAbove.size() - 1) == kWorldHeight,
              "kSurfaceAtOrAbove must have a knot exactly at the world ceiling - it is the "
              "y >= kWorldHeight guard in surfaceAtOrAbove that keeps the [i + 1] read inside "
              "the array, so the table's span and the world's height are the same fact and one "
              "of them has moved");

/// The share of `veinHeight`'s draws that land in cell `y`.
///
/// `veinHeight` is the inverse of a triangular CDF applied to a uniform draw,
/// and that map is monotone, so the chance of landing in `[y, y+1)` is exactly
/// `F(y+1) - F(y)` for that same CDF - written out here without the square root
/// the sampler needs, so it is usable in a constant expression. **Change the
/// sampler and this has to change with it**; they are two faces of one shape,
/// and only the asserts below tie them together.
constexpr float veinHeightCdf(const OreVein& vein, float y) {
    const float a = static_cast<float>(vein.minY);
    const float c = static_cast<float>(vein.maxY) + 1.0f;
    const float b = static_cast<float>(vein.peakY) + 0.5f;
    if (y <= a) {
        return 0.0f;
    }
    if (y >= c) {
        return 1.0f;
    }
    if (y <= b) {
        return (y - a) * (y - a) / ((c - a) * (b - a));
    }
    return 1.0f - (c - y) * (c - y) / ((c - a) * (c - b));
}

constexpr float veinHeightWeight(const OreVein& vein, int y) {
    return veinHeightCdf(vein, static_cast<float>(y) + 1.0f) - veinHeightCdf(vein, static_cast<float>(y));
}

/// The most blocks of this ore a 32x32 column could possibly hold.
///
/// `attempts * size` is the budget, and the sum weighs each height by how often
/// there is any rock at all up there to write into - a vein rolled above the
/// local surface is rolled into open sky and thrown away whole. Everything else
/// that happens to a vein only ever *subtracts*: air discard, caves eaten out
/// underneath it, and `placeVein` refusing to overwrite anything that is not
/// plain stone or deepslate. **So this is an upper bound, not an estimate**,
/// which is the only claim about realised counts a table can honestly make.
constexpr float oreBlockCeiling(const OreVein& vein) {
    float inRock = 0.0f;
    for (int y = vein.minY; y <= vein.maxY; ++y) {
        inRock += veinHeightWeight(vein, y) * surfaceAtOrAbove(y);
    }
    return static_cast<float>(vein.attempts) * static_cast<float>(vein.size) * inRock;
}

// A band whose peak sits outside it would make `veinHeight` take the square
// root of a negative number. Moving any `peakY` outside its own `[minY, maxY]`
// is the single edit that makes this fail, and it has to be checked before the
// two asserts below, which evaluate the distribution.
static_assert([] {
    for (const OreVein& vein : kOreVeins) {
        if (vein.minY >= vein.peakY || vein.peakY >= vein.maxY) {
            return false;
        }
    }
    return true;
}(), "An ore band's peak is outside its own range.");

// `peakY` is named for where the ore is densest, and this is what holds it to
// that. **Restoring the sum form `minY + range(lowSpan+1) + range(highSpan+1)`
// is the single edit that makes it fail** - the convolution of two uniforms
// peaks at the band's *midpoint* whatever the split, so coal's mode would move
// 26 -> 39, which is above the median surface and cost 47% of all coal when it
// shipped that way. Diamond, redstone and emerald fail it too.
static_assert([] {
    for (const OreVein& vein : kOreVeins) {
        int mode = vein.minY;
        for (int y = vein.minY; y <= vein.maxY; ++y) {
            if (veinHeightWeight(vein, y) > veinHeightWeight(vein, mode)) {
                mode = y;
            }
        }
        if (mode != vein.peakY) {
            return false;
        }
    }
    return true;
}(), "veinHeight is not densest at peakY, so the table's middle column no longer means what it "
     "is named.");

// Every `measured` has to be something the row could physically have placed.
// **Cutting an `attempts` without re-measuring is the single edit that makes
// this fail** - which is the direction that matters, because that is the edit
// that would otherwise leave the ordering below sorted on a number the table
// can no longer produce. Raising an `attempts` is not caught here; it is caught
// only if it moves the row past its neighbour.
static_assert([] {
    for (const OreVein& vein : kOreVeins) {
        if (vein.measured > oreBlockCeiling(vein)) {
            return false;
        }
    }
    return true;
}(), "An ore's measured block count is higher than its table row could ever place, so the "
     "measurement is stale.");

/// True when the row at `upper` really is scarcer than the row at `lower`.
constexpr bool rarerThan(std::size_t upper, std::size_t lower) {
    return kOreVeins[upper].measured < kOreVeins[lower].measured;
}

// One assert per adjacent pair, so a spline tweak that moves terrain names the
// pair it broke instead of reading as an ore-table bug. Swapping either row of
// a pair is the single edit that makes that pair fail.
static_assert(kOreVeins.size() == 9, "A row was added or removed - add its pairwise ordering "
                                     "assert too, or the new row is unprotected.");
static_assert(rarerThan(0, 1), "AncientDebris must run before EmeraldOre.");
static_assert(rarerThan(1, 2), "EmeraldOre must run before DiamondOre - it is the rarer of the "
                               "two in realised blocks, because airDiscard 1.0 culls most of it.");
static_assert(rarerThan(2, 3), "DiamondOre must run before LapisOre; their bands overlap at y 3-17.");
static_assert(rarerThan(3, 4), "LapisOre must run before GoldOre; their bands overlap at y 3-20.");
static_assert(rarerThan(4, 5), "GoldOre must run before RedstoneOre; their bands overlap at y 3-17.");
static_assert(rarerThan(5, 6), "RedstoneOre must run before CopperOre.");
static_assert(rarerThan(6, 7), "CopperOre must run before IronOre; their bands overlap at y 10-27.");
static_assert(rarerThan(7, 8), "IronOre must run before CoalOre; their bands overlap at y 8-27.");

/// The furthest, in blocks, that a vein of this size can write from its origin
/// column - the bound the search radius has to beat.
///
/// The spindle runs `size/8` either side of the origin; the fattest sphere on
/// it has radius `size/16 + 0.5`; the cell loop then walks `int(radius) + 1`
/// cells outward from a `floor`ed centre, which can lose another whole block on
/// the low side. That is `3*size/16 + 2.5`, and this rounds every part of it
/// up so the answer is never optimistic.
constexpr int veinExtent(int size) { return (3 * size + 15) / 16 + 3; }

/// How far outside its own column a vein may reach, in blocks.
///
/// **Six is not a safety margin, it is exactly the largest vein in the table
/// plus rounding, and the table is one edit away from outgrowing it.** At
/// today's max `size = 12` the true reach is 4.75; at 16 it is 5.5; at 20 -
/// vanilla's largest `vein_size` and the obvious number for someone adding a
/// bigger ore - it is 6.25. The moment it passes `kVeinReach` the search below
/// stops finding the vein from the far chunk, and **the same vein comes out as
/// two mismatched halves either side of a chunk border**: the seam report that
/// has cost two playtests, reachable by editing one integer in a table that
/// says nothing about this constant.
constexpr int kVeinReach = 6;

// Raising any `size` in `kOreVeins` past 16 is the single edit that makes this
// fail; the fix is to raise `kVeinReach` to match, which costs search time and
// nothing else.
static_assert([] {
    int worst = 0;
    for (const OreVein& vein : kOreVeins) {
        worst = std::max(worst, veinExtent(vein.size));
    }
    return worst;
}() <= kVeinReach,
              "A vein can now write further than the neighbour search looks, so it will be cut in "
              "half at a chunk border. Raise kVeinReach.");

// ---------------------------------------------------------------------------
// Height
// ---------------------------------------------------------------------------

/// Surface height at a column, in blocks.
///
/// **There is no density field to threshold.** Terrain is single-valued, so the
/// noise displaces the target height directly and the answer is exact for every
/// block. That is what removed the last of the reported artefacts: an
/// *interpolated* density is piecewise-linear across a lattice cell, so its
/// contour lines cluster on the cell boundaries and a gentle slope comes out
/// banded every four blocks - regular enough to read as a pattern rather than
/// as landscape.
///
/// The reference needs its lattice because its terrain is genuinely 3D and it
/// cannot afford a density evaluation per block. We gave up overhangs when we
/// made terrain single-valued, so the lattice was buying nothing but its own
/// artefact, and one noise evaluation per column is *cheaper* than the four
/// climate samples and fifty-two density samples the interpolated version cost.
///
/// The noise is still sampled in three dimensions, at the target height. It is
/// a function of `(x, z)` either way, but reading it at the column's own
/// altitude decorrelates a mountain's texture from a lowland's for free.
float columnHeightFrom(std::uint32_t seed, int worldX, int worldZ, const Shape& shape) {
    const float wobble =
        noise::octaves3D(seed ^ 0x7e44a1c3u, static_cast<float>(worldX) / kBaseWavelengthXZ,
                         shape.offsetY / kBaseWavelengthY, static_cast<float>(worldZ) / kBaseWavelengthXZ,
                         kBaseAmplitudes.data(), static_cast<int>(kBaseAmplitudes.size()));

    return shape.offsetY + wobble * kReliefBlocks / shape.factor;
}

/// The one place a float height becomes a block index.
///
/// The soft ceiling lives here rather than in `columnHeightFrom` so that
/// *every* caller gets it - a second path that clamped for itself is how the
/// plateau would come back.
int surfaceFrom(float height) {
    if (height > kCeilingSoftStart) {
        constexpr float kCeilingSpan = kMaxSurface - kCeilingSoftStart;
        height = kMaxSurface - kCeilingSpan * std::exp(-(height - kCeilingSoftStart) / kCeilingSpan);
    }
    return std::clamp(static_cast<int>(std::floor(height)), kMinSurface, kMaxSurface);
}

/// A column's climate and its finished surface height, from one pass over the
/// noise.
///
/// **The two belong together because the height is derived from the climate**,
/// and asking for them separately - `columnHeight` for the height ring, then
/// `climateAt` all over again for each interior column - was paying for the
/// twenty noise evaluations behind `climateAt` twice for every column of every
/// chunk.
struct ColumnSample {
    Climate climate;
    int surface;
};

ColumnSample sampleColumn(std::uint32_t seed, int worldX, int worldZ) {
    ColumnSample sample{};
    sample.climate = climateAt(seed, worldX, worldZ);
    const Shape shape = shapeAt(seed, sample.climate, worldX, worldZ);
    sample.surface = surfaceFrom(columnHeightFrom(seed, worldX, worldZ, shape));
    return sample;
}

/// **Defined in terms of `sampleColumn` rather than beside it**, so the height
/// a chunk caches for itself and the height `surfaceHeightAt` hands a tree
/// cannot drift apart - and drifting apart is exactly what leaves a tree
/// hanging in the air.
int columnHeight(std::uint32_t seed, int worldX, int worldZ) {
    return sampleColumn(seed, worldX, worldZ).surface;
}


// ---------------------------------------------------------------------------
// Caves
// ---------------------------------------------------------------------------

/// True where a cave should hollow out the rock.
bool isCave(std::uint32_t seed, int worldX, int worldY, int worldZ, int surfaceHeight) {
    if (worldY <= kBedrockTop) {
        return false;
    }

    const auto fx = static_cast<float>(worldX);
    const auto fy = static_cast<float>(worldY);
    const auto fz = static_cast<float>(worldZ);

    const float depth = static_cast<float>(surfaceHeight - worldY);

    // **Only the tunnel carver is ever allowed near daylight.** This is the
    // reference's `sloped_cheese < 1.5625 -> entrances only` gate, and it is the
    // whole of the "caves do not eat the landscape" rule. Letting the shared
    // surface margin relax for *every* system opened cheese caverns at ground
    // level, and a cavern that surfaces is not a cave mouth - it is a crater the
    // size of the cavern, with the trees that were standing on it left in
    // mid-air.
    //
    // A submerged column is excluded outright: its top blocks are holding the
    // sea up, and carving them stranded water in the air. The reference reaches
    // the same place from the other side, by letting its aquifer flood any cave
    // it cuts under the waterline instead of leaving a hole.
    const float entrance = noise::octaves2D(seed ^ 0x0be11a5eu, fx / kEntranceWavelength,
                                            fz / kEntranceWavelength, kEntranceAmplitudes.data(),
                                            static_cast<int>(kEntranceAmplitudes.size()));
    const float opening =
        surfaceHeight < kSeaLevel
            ? 0.0f
            : std::clamp((entrance - kEntranceThreshold) / kEntranceWidth, 0.0f, 1.0f);

    const float tunnelMargin = static_cast<float>(kCaveSurfaceMargin) * (1.0f - opening);
    if (depth >= tunnelMargin) {
        // Inside an entrance the tunnel keeps its full width right up to the
        // open air; outside one it fades in with depth, so ordinary ground stays
        // intact. Relaxing the margin without also relaxing the fade left a
        // mouth choked to a tenth of its width, which is a pinhole nobody sees.
        const float fadeDepth = static_cast<float>(kCaveFadeDepth) * (1.0f - opening);
        const float fade =
            fadeDepth <= 0.0f ? 1.0f : std::clamp((depth - tunnelMargin) / fadeDepth, 0.0f, 1.0f);
        if (fade > 0.0f) {
            const float spaghetti =
                noise::octaves3D(seed ^ 0x5eed1234u, fx / kSpaghettiWavelength,
                                 fy / kSpaghettiWavelength * (1.0f / kSpaghettiYStretch),
                                 fz / kSpaghettiWavelength, kSpaghettiAmplitudes.data(),
                                 static_cast<int>(kSpaghettiAmplitudes.size()));
            if (std::abs(spaghetti) < kSpaghettiWidth * fade) {
                return true;
            }
        }
    }

    // Caverns and noodles keep the full margin whatever the entrance field says.
    const float deepMargin = static_cast<float>(kCavernSurfaceMargin);
    if (depth < deepMargin) {
        return false;
    }
    const float fade =
        std::clamp((depth - deepMargin) / static_cast<float>(kCaveFadeDepth), 0.0f, 1.0f);
    if (fade <= 0.0f) {
        return false;
    }

    const float cheese = noise::octaves3D(seed ^ 0x0cbee5e0u, fx / kCheeseWavelength,
                                          fy / kCheeseWavelength * (1.0f / kCheeseYStretch),
                                          fz / kCheeseWavelength, kCheeseAmplitudes.data(),
                                          static_cast<int>(kCheeseAmplitudes.size()));
    if (cheese > kCheeseThreshold + (1.0f - fade)) {
        return true;
    }

    if (worldY > kNoodleCeiling) {
        return false;
    }

    // The patch test comes first because it is 2D and rejects most of the
    // world, which keeps the third noise off the common path entirely.
    const float patch = noise::octaves2D(seed ^ 0x0d1e0d1eu, fx / kNoodlePatchWavelength,
                                         fz / kNoodlePatchWavelength, kNoodlePatchAmplitudes.data(),
                                         static_cast<int>(kNoodlePatchAmplitudes.size()));
    if (patch < kNoodlePatchThreshold) {
        return false;
    }

    const float noodle = noise::octaves3D(seed ^ 0x0000d1e5u, fx / kNoodleWavelength, fy / kNoodleWavelength,
                                          fz / kNoodleWavelength, kNoodleAmplitudes.data(),
                                          static_cast<int>(kNoodleAmplitudes.size()));
    return std::abs(noodle) < kNoodleWidth * fade;
}

// ---------------------------------------------------------------------------
// Ores
// ---------------------------------------------------------------------------

/// A height inside the vein's band, densest at `peakY` and tapering to nothing
/// at both ends - an asymmetric triangle, sampled by inverting its CDF.
///
/// **Three shapes were on the table and only this one lets `peakY` mean what it
/// is named.** The reference's `TrapezoidHeight(plateau 0)` is a *sum* of two
/// uniform rolls, `minY + nextInt(l+1) + nextInt(k+1)`, and vanilla splits the
/// band into two *near-equal* halves (`l = (k - plateau) / 2; m = k - l`), so
/// its peak is always the band's midpoint - it has no `peakY` parameter at all,
/// and gets asymmetry instead by giving an ore two features (coal is a uniform
/// upper band plus a triangular lower one). Hand that sum an *arbitrary* split
/// and the convolution still peaks at `(minY + maxY) / 2` however the split
/// moves; `peakY` would set only the width of the modal plateau. Measured, that
/// cost 47% of all coal, because coal's mode slid from 26 to 39 and
/// `surfaceAtOrAbove(39)` is 0.17 - most rolls landed in open sky.
///
/// The shape before that rolled both halves and picked one at 50/50, which is a
/// *mixture*: flat within each half with a hard step at `peakY`. Coal came out
/// 2.4x denser at y 25 than at y 27, a shelf you cross rather than a gradient.
///
/// A triangle is a strict generalisation rather than a departure: put `peakY`
/// at the band's midpoint and it *is* vanilla's zero-plateau trapezoid, to
/// within the discretisation. One row per ore then says what two vanilla
/// features say, which is what our table has always been shaped to do.
///
/// Inversion, for `a = minY`, `c = maxY + 1`, mode `b = peakY + 0.5` (the half
/// keeps `floor` landing on `peakY` rather than splitting the mode across two
/// cells): draw `u` uniform, and take `a + sqrt(u (c-a)(b-a))` below the split
/// `(b-a)/(c-a)`, `c - sqrt((1-u)(c-a)(c-b))` above it.
///
/// **The draw count per attempt falls from three to one, which moves every vein
/// in the world - and that is safe.** `veinHeight` and the vein's own
/// `noise::Stream` are both consumed *before* the "is this vein near this
/// chunk" reject below, so every chunk that considers a given vein consumes
/// exactly the same values in the same order. It is one `unit()` call for every
/// row of the table, unconditionally, so nothing here can vary by chunk.
int veinHeight(const OreVein& vein, noise::Stream& roll) {
    const float a = static_cast<float>(vein.minY);
    const float c = static_cast<float>(vein.maxY) + 1.0f;
    const float b = static_cast<float>(vein.peakY) + 0.5f;
    const float split = (b - a) / (c - a);
    const float u = roll.unit();
    const float y = u < split ? a + std::sqrt(u * (c - a) * (b - a))
                              : c - std::sqrt((1.0f - u) * (c - a) * (c - b));
    return std::clamp(static_cast<int>(y), vein.minY, vein.maxY);
}

/// The reference's `OreFeature`: a spindle of overlapping spheres strung along a
/// short, randomly angled segment through the origin.
///
/// **This is the shape, and it is why a vein reads as a vessel rather than a
/// dot.** Three things do the work. The segment gives it a direction, and its
/// two ends drift independently in y so it is never an axis-aligned slab. The
/// `sin(pi t)` term tapers the radius to nothing at both tips. And the scale is
/// re-rolled at every step, so consecutive spheres jump in size and the
/// silhouette comes out knobbly instead of smooth.
void placeVein(Chunk& chunk, const ChunkCoord& coord, const OreVein& vein, int originX, int originY,
               int originZ, noise::Stream& roll,
               const std::function<bool(int, int, int)>& exposedToAir) {
    const float angle = roll.unit() * 3.14159265f;
    const float half = static_cast<float>(vein.size) / 8.0f;

    const float x0 = static_cast<float>(originX) + std::sin(angle) * half;
    const float x1 = static_cast<float>(originX) - std::sin(angle) * half;
    const float z0 = static_cast<float>(originZ) + std::cos(angle) * half;
    const float z1 = static_cast<float>(originZ) - std::cos(angle) * half;
    const float y0 = static_cast<float>(originY + roll.range(3) - 2);
    const float y1 = static_cast<float>(originY + roll.range(3) - 2);

    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int step = 0; step < vein.size; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(vein.size);
        const float cx = x0 + (x1 - x0) * t;
        const float cy = y0 + (y1 - y0) * t;
        const float cz = z0 + (z1 - z0) * t;

        // Re-rolled every step even when the sphere lands outside this chunk,
        // or the same vein would come out different from a neighbouring chunk.
        const float scale = roll.unit() * static_cast<float>(vein.size) / 16.0f;
        const float radius = ((std::sin(3.14159265f * t) + 1.0f) * scale + 1.0f) * 0.5f;
        const int reach = static_cast<int>(radius) + 1;

        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dz = -reach; dz <= reach; ++dz) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int wx = static_cast<int>(std::floor(cx)) + dx;
                    const int wy = static_cast<int>(std::floor(cy)) + dy;
                    const int wz = static_cast<int>(std::floor(cz)) + dz;

                    const float nx = (static_cast<float>(wx) + 0.5f - cx) / radius;
                    const float ny = (static_cast<float>(wy) + 0.5f - cy) / radius;
                    const float nz = (static_cast<float>(wz) + 0.5f - cz) / radius;
                    if (nx * nx + ny * ny + nz * nz >= 1.0f) {
                        continue;
                    }

                    // **Rolled for every cell the sphere covers**, before any
                    // test that depends on which chunk is asking. Rolling it
                    // after the bounds check would consume a different number of
                    // values from each side of a chunk border, and the vein
                    // would come out as two mismatched halves.
                    const bool discard = vein.airDiscard > 0.0f && roll.unit() < vein.airDiscard;

                    const int lx = wx - baseX;
                    const int ly = wy - baseY;
                    const int lz = wz - baseZ;
                    if (!Chunk::contains(lx, ly, lz)) {
                        continue;
                    }

                    // Only ever replaces plain rock, so a vein cannot eat the
                    // surface, a cave wall or another ore.
                    const BlockId here = chunk.at(lx, ly, lz);
                    if (here != BlockId::Stone && here != BlockId::Deepslate) {
                        continue;
                    }
                    if (discard && exposedToAir(wx, wy, wz)) {
                        continue;
                    }
                    // **Deepslate rock carries the deepslate form of the ore.**
                    // Read off the block actually being replaced rather than
                    // from a depth test of its own, so the swap lands on exactly
                    // the dithered boundary the stone-to-deepslate transition
                    // already drew instead of a block either side of it.
                    const bool deep = here == BlockId::Deepslate && hasDeepslateForm(vein.block);
                    chunk.set(lx, ly, lz, deep ? deepslateOreFor(vein.block) : vein.block);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Amethyst geodes
// ---------------------------------------------------------------------------

/// **The missing call site that made two craftable items unreachable.**
///
/// `ItemId::AmethystShard` has exactly one producer in the whole game - mining
/// an `AmethystCluster` - and until this pass existed **nothing anywhere placed
/// one**, so the spyglass and the block of amethyst were dead recipes standing
/// on live ids. Every other link in the chain was already built and idle:
/// `Block.hpp` declares all seven blocks with textures, `BlockDrops.hpp` gives a
/// cluster four shards to a pickaxe and two without, and `World.cpp`'s random
/// tick already grows `BuddingAmethyst` -> small -> medium -> large -> cluster.
/// That growth code's own comment - *"Budding amethyst is never consumed, so a
/// geode keeps paying"* - was written about a geode that did not exist. This is
/// bug shape #15, a complete feature one call site short of being reachable, and
/// worldgen was the one place holding it.
///
/// Geodes are an overworld feature, so the Nether/End scope ruling does not
/// reach them.
constexpr std::uint32_t kGeodeSalt = 0x5a7e13u;

// A geode drawing the same stream as an ore would put the two in lockstep: the
// same columns would hold both, forever, and neither table says a word about
// the other. `kOreVeins` is above, so this is checkable rather than hopeful.
static_assert([] {
    for (const OreVein& vein : kOreVeins) {
        if (vein.salt == kGeodeSalt) {
            return false;
        }
    }
    return true;
}(),
              "kGeodeSalt collides with an ore vein's salt, so geodes and that ore would be rolled "
              "from the same stream and land in the same columns.");

/// **Areal density carried over from the reference exactly, and derived rather
/// than guessed.** The reference attempts one geode per 24 chunks, and its
/// chunk is 16x16; ours is 32x32, which is four of them. 24 / 4 = 6, so one
/// column in six carries an attempt and the blocks-per-geode figure is
/// identical on both sides - 6144 either way. Writing it as a division means a
/// future `Chunk::kSize` cannot silently change how rare amethyst is, which a
/// hand-written `6` would have done in complete silence.
constexpr int kGeodeReferencePerChunks = 24;
constexpr int kGeodeReferenceChunkSide = 16;
constexpr int kGeodeOneInColumns = (kGeodeReferencePerChunks * kGeodeReferenceChunkSide *
                                    kGeodeReferenceChunkSide) /
                                   (Chunk::kSize * Chunk::kSize);
static_assert(kGeodeReferencePerChunks * kGeodeReferenceChunkSide * kGeodeReferenceChunkSide %
                      (Chunk::kSize * Chunk::kSize) ==
                  0,
              "The reference's geode spacing no longer divides evenly by our column area, so "
              "kGeodeOneInColumns has silently rounded and geodes are no longer at the reference's "
              "density. Work out the intended figure rather than letting integer division pick.");
static_assert(kGeodeOneInColumns > 0,
              "A rarity of zero would make range() return 0 always and put a geode under every "
              "single column.");

/// The reference's `outer_wall_distance` is 4-6; ours is 4-5, trimmed at the
/// top for the same reason the vein sizes were shrunk about a quarter - a geode
/// is measured against the player, but this world's rock column is a third of
/// the reference's depth and the widest one would breach the surface far more
/// often than it buries.
constexpr int kGeodeOuterMin = 4;
constexpr int kGeodeOuterMax = 5;

/// `distribution_points` 3-4 and `point_offset` up to 2, both the reference's
/// own. This is what makes a geode a lumpy several-lobed cavity rather than a
/// billiard ball, and it is the whole of the shape.
constexpr int kGeodePointsMin = 3;
constexpr int kGeodePointsMax = 4;
constexpr int kGeodePointOffset = 2;

/// The reference's `use_alternate_layer0_chance` 0.083 and
/// `use_potential_placements_chance` 0.35, as one-in-N and percent.
constexpr int kGeodeBuddingOneIn = 12;
constexpr int kGeodeClusterPercent = 35;

/// **Derived from the shape, not chosen.** A distribution point sits up to
/// `kGeodePointOffset` from the origin and its shell reaches `kGeodeOuterMax`
/// beyond that, so this is exactly how far a geode can write - the same role
/// `kVeinReach` plays for veins, and the same failure if it is ever short: the
/// far chunk stops finding the geode and it comes out as two mismatched halves
/// either side of a chunk border.
constexpr int kGeodeReach = kGeodeOuterMax + kGeodePointOffset;

/// The height at or above which half of all columns stand, read straight off
/// the measured CDF rather than restated.
constexpr int medianSurfaceHeight() {
    int knot = 0;
    for (std::size_t i = 0; i < kSurfaceAtOrAbove.size(); ++i) {
        if (kSurfaceAtOrAbove[i] >= 0.5f) {
            knot = static_cast<int>(i);
        }
    }
    return knot * kSurfaceCdfStride;
}

/// **Both ends of the band are derived from constants that already own the
/// answer, so no edit can quietly strand a geode.**
///
/// The floor clears the lava fill: a geode centred at `kGeodeMinY` reaches
/// `kGeodeReach` below itself, and putting that one block above `kLavaLevel`
/// means the shell can never open into the lava sheet. The ceiling is the
/// median surface less the same reach, so at least half of all columns bury a
/// geode at the very top of the band completely.
constexpr int kGeodeMinY = kLavaLevel + kGeodeReach + 1;
constexpr int kGeodeMaxY = medianSurfaceHeight() - kGeodeReach;
static_assert(kGeodeMinY <= kGeodeMaxY,
              "The geode band has inverted, so range() would be handed a non-positive count and "
              "every geode in the world would sit at exactly kGeodeMinY.");
static_assert(kGeodeMinY - kGeodeReach > kBedrockTop,
              "A geode can now reach into the bedrock floor, where its shell would be cut off flat "
              "against blocks the player cannot mine.");
static_assert(kGeodeMinY - kGeodeReach > kLavaLevel,
              "A geode can now open into the lava sheet at kLavaLevel, which would drain into the "
              "hollow and destroy the crystals it exists to hold.");

// The layout is built in local codes rather than block ids because "not part of
// this geode" and "the hollow middle" are different answers and `BlockId::Air`
// cannot say both.
constexpr std::uint8_t kGeodeNone = 0;
constexpr std::uint8_t kGeodeHollow = 1;
constexpr std::uint8_t kGeodeAmethyst = 2;
constexpr std::uint8_t kGeodeBudding = 3;
constexpr std::uint8_t kGeodeCalcite = 4;
constexpr std::uint8_t kGeodeBasalt = 5;
constexpr std::uint8_t kGeodeBudSmall = 6;
constexpr std::uint8_t kGeodeBudMedium = 7;
constexpr std::uint8_t kGeodeBudLarge = 8;
constexpr std::uint8_t kGeodeClusterCode = 9;

constexpr BlockId geodeBlockFor(std::uint8_t code) {
    switch (code) {
    case kGeodeHollow:
        return BlockId::Air;
    case kGeodeAmethyst:
        return BlockId::AmethystBlock;
    case kGeodeBudding:
        return BlockId::BuddingAmethyst;
    case kGeodeCalcite:
        return BlockId::Calcite;
    case kGeodeBasalt:
        return BlockId::SmoothBasalt;
    case kGeodeBudSmall:
        return BlockId::SmallAmethystBud;
    case kGeodeBudMedium:
        return BlockId::MediumAmethystBud;
    case kGeodeBudLarge:
        return BlockId::LargeAmethystBud;
    case kGeodeClusterCode:
        return BlockId::AmethystCluster;
    default:
        return BlockId::Air;
    }
}

// The four growth stages in the order `World.cpp` advances them, so a bud
// placed here is always somewhere on the same ladder the random tick walks.
constexpr std::array<std::uint8_t, 4> kGeodeInnerPlacements{kGeodeBudSmall, kGeodeBudMedium,
                                                            kGeodeBudLarge, kGeodeClusterCode};
static_assert(geodeBlockFor(kGeodeInnerPlacements[0]) == BlockId::SmallAmethystBud &&
                  geodeBlockFor(kGeodeInnerPlacements[1]) == BlockId::MediumAmethystBud &&
                  geodeBlockFor(kGeodeInnerPlacements[2]) == BlockId::LargeAmethystBud &&
                  geodeBlockFor(kGeodeInnerPlacements[3]) == BlockId::AmethystCluster,
              "The inner placements no longer name the four stages World.cpp grows, so a bud "
              "generated here may sit on no growth ladder at all and could never mature.");

constexpr int kGeodeSpan = 2 * kGeodeReach + 1;

constexpr std::size_t geodeIndex(int dx, int dy, int dz) {
    return (static_cast<std::size_t>(dz + kGeodeReach) * kGeodeSpan +
            static_cast<std::size_t>(dy + kGeodeReach)) *
               kGeodeSpan +
           static_cast<std::size_t>(dx + kGeodeReach);
}

/// One geode, hollowed and dressed.
///
/// **The whole shape is built into a local buffer before a single cell is
/// written to the chunk, and that is a purity decision rather than a tidiness
/// one.** Deciding where a crystal goes needs to know whether the cell beside it
/// is budding amethyst, and reading that off the *chunk* would be reading a
/// neighbour that may not be in this chunk at all - so the answer would depend
/// on which chunk happened to be asking and the geode would come out different
/// from either side of a border. Built here, the layout is a pure function of
/// the rolls, every chunk computes the identical buffer, and each chunk copies
/// out only the cells it owns.
///
/// This is stronger than the rule `placeVein` follows next door. That one keeps
/// its rolls in step by rolling before every bounds test; this one has no
/// chunk-dependent branch to get wrong in the first place.
void placeGeode(Chunk& chunk, const ChunkCoord& coord, int originX, int originY, int originZ,
                noise::Stream& roll) {
    const int outer = kGeodeOuterMin + roll.range(kGeodeOuterMax - kGeodeOuterMin + 1);
    const int points = kGeodePointsMin + roll.range(kGeodePointsMax - kGeodePointsMin + 1);

    // Rolled for every slot the array can hold, not for the `points` actually
    // used, so that adding a lobe later cannot shift the crystals of every
    // geode already in the world.
    std::array<int, static_cast<std::size_t>(kGeodePointsMax) * 3> lobes{};
    for (std::size_t i = 0; i < lobes.size(); ++i) {
        lobes[i] = roll.range(2 * kGeodePointOffset + 1) - kGeodePointOffset;
    }

    std::array<std::uint8_t, static_cast<std::size_t>(kGeodeSpan) * kGeodeSpan * kGeodeSpan>
        layout{};

    const int hollowRadius = outer - 3;
    const int amethystRadius = outer - 2;
    const int calciteRadius = outer - 1;

    for (int dz = -kGeodeReach; dz <= kGeodeReach; ++dz) {
        for (int dy = -kGeodeReach; dy <= kGeodeReach; ++dy) {
            for (int dx = -kGeodeReach; dx <= kGeodeReach; ++dx) {
                int best = kGeodeReach * kGeodeReach * 4;
                for (int p = 0; p < points; ++p) {
                    const int ox = dx - lobes[static_cast<std::size_t>(p) * 3 + 0];
                    const int oy = dy - lobes[static_cast<std::size_t>(p) * 3 + 1];
                    const int oz = dz - lobes[static_cast<std::size_t>(p) * 3 + 2];
                    best = std::min(best, ox * ox + oy * oy + oz * oz);
                }

                std::uint8_t code = kGeodeNone;
                if (best <= hollowRadius * hollowRadius) {
                    code = kGeodeHollow;
                } else if (best <= amethystRadius * amethystRadius) {
                    // Rolled for every amethyst cell whether or not it becomes
                    // budding, so the budding blocks of one geode do not depend
                    // on how many cells happened to precede them.
                    code = roll.range(kGeodeBuddingOneIn) == 0 ? kGeodeBudding : kGeodeAmethyst;
                } else if (best <= calciteRadius * calciteRadius) {
                    code = kGeodeCalcite;
                } else if (best <= outer * outer) {
                    code = kGeodeBasalt;
                }
                layout[geodeIndex(dx, dy, dz)] = code;
            }
        }
    }

    // **Crystals go only on a face of budding amethyst**, which is not
    // decoration: `World.cpp` advances a bud only while it is attached to
    // budding amethyst, so one placed anywhere else would be frozen at whatever
    // stage it was born at, forever. Seeding the ladder where the tick can
    // reach it is what makes these grow back after they are harvested.
    for (int dz = -kGeodeReach; dz <= kGeodeReach; ++dz) {
        for (int dy = -kGeodeReach; dy <= kGeodeReach; ++dy) {
            for (int dx = -kGeodeReach; dx <= kGeodeReach; ++dx) {
                if (layout[geodeIndex(dx, dy, dz)] != kGeodeHollow) {
                    continue;
                }
                const bool attached =
                    (dx > -kGeodeReach && layout[geodeIndex(dx - 1, dy, dz)] == kGeodeBudding) ||
                    (dx < kGeodeReach && layout[geodeIndex(dx + 1, dy, dz)] == kGeodeBudding) ||
                    (dy > -kGeodeReach && layout[geodeIndex(dx, dy - 1, dz)] == kGeodeBudding) ||
                    (dy < kGeodeReach && layout[geodeIndex(dx, dy + 1, dz)] == kGeodeBudding) ||
                    (dz > -kGeodeReach && layout[geodeIndex(dx, dy, dz - 1)] == kGeodeBudding) ||
                    (dz < kGeodeReach && layout[geodeIndex(dx, dy, dz + 1)] == kGeodeBudding);
                if (!attached) {
                    continue;
                }
                if (roll.range(100) >= kGeodeClusterPercent) {
                    continue;
                }
                layout[geodeIndex(dx, dy, dz)] =
                    kGeodeInnerPlacements[static_cast<std::size_t>(
                        roll.range(static_cast<int>(kGeodeInnerPlacements.size())))];
            }
        }
    }

    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int dz = -kGeodeReach; dz <= kGeodeReach; ++dz) {
        for (int dy = -kGeodeReach; dy <= kGeodeReach; ++dy) {
            for (int dx = -kGeodeReach; dx <= kGeodeReach; ++dx) {
                const std::uint8_t code = layout[geodeIndex(dx, dy, dz)];
                if (code == kGeodeNone) {
                    continue;
                }

                const int lx = originX + dx - baseX;
                const int ly = originY + dy - baseY;
                const int lz = originZ + dz - baseZ;
                if (!Chunk::contains(lx, ly, lz)) {
                    continue;
                }

                // Only ever replaces plain rock, exactly as a vein does, so a
                // geode cannot eat the surface, a cave wall, an ore or a
                // village. Where a cave already cuts through, the shell simply
                // stops - which is the reference's behaviour too, and is how a
                // player finds one without digging blind.
                const BlockId here = chunk.at(lx, ly, lz);
                if (here != BlockId::Stone && here != BlockId::Deepslate) {
                    continue;
                }
                chunk.set(lx, ly, lz, geodeBlockFor(code));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Plants
// ---------------------------------------------------------------------------

/// How tall each multi-block plant grows: `base` blocks plus `0 .. spread-1`.
///
/// **Named, and in one place, because `kMaxPlantHeight` is derived from them.**
/// A species that grows taller than the overhang walk looks is a stalk sheared
/// off at a chunk ceiling, and deriving the bound means adding a taller plant
/// cannot reintroduce that - there is no second number to remember.
constexpr int kBambooBase = 3;
constexpr int kBambooSpread = 5;
constexpr int kCactusBase = 1;
constexpr int kCactusSpread = 3;
constexpr int kSugarCaneBase = 2;
constexpr int kSugarCaneSpread = 2;

constexpr int tallestPlant(int base, int spread) { return base + spread - 1; }

/// The furthest below itself a chunk has to look for a stalk rooted lower down.
constexpr int kMaxPlantHeight = std::max({tallestPlant(kBambooBase, kBambooSpread),
                                          tallestPlant(kCactusBase, kCactusSpread),
                                          tallestPlant(kSugarCaneBase, kSugarCaneSpread)});

/// One species and one height for a whole column.
struct PlantColumn {
    BlockId block = BlockId::Air;
    int height = 0;
};

/// What grows on this column, from hashes and the column's own surface alone.
///
/// **Pure on purpose, and that is the whole fix for a bug this file has already
/// paid for once.** A stalk whose base lands in one chunk and whose top lands
/// in the next is written by *both* of them, independently, from this one
/// answer. Generation may not read a neighbouring chunk - so the chunk above
/// does not ask what was planted below, it re-derives it, and gets the same
/// species and the same height because it feeds in the same numbers.
///
/// Before this, the growth loop stopped at the chunk ceiling and the chunk
/// above rejected the column outright for having a negative base, so **the top
/// of every tall stalk was simply lost.** Bamboo is 3-7 tall, so every stand
/// rooted between y 25 and y 31 was sheared off at exactly y 31 - a dead flat
/// horizontal line across a bamboo jungle, and the same again at y 63. It is
/// the identical mistake as the one two comments below, which cost every plant
/// at y 32 and y 64: the base placement was made pure and the stalk was not.
///
/// `waterAdjacent` is the one thing here that is not a hash, and it is still
/// pure: it comes from the neighbouring *column heights*, which this chunk
/// already computed for itself in its 34x34 ring.
PlantColumn plantAt(std::uint32_t seed, int worldX, int worldZ, int surface, BiomeId biomeId,
                    const Biome& biome, BlockId top, bool waterAdjacent) {
    // **Sugar cane is asked first because it is the most specific rule here.**
    // It stands on ground that is *level with* the sea and beside water - and
    // that is not a taste, it is forced: our water fills every air cell at or
    // below y 24, so the only dry supporting block that can have water against
    // its own side is one at exactly y 24. The old test asked for `kSeaLevel +
    // 1` and had no water test at all, so cane grew along any dry inland
    // contour that happened to bottom out at y 25 and grew nowhere near an
    // actual river.
    if (surface == kSeaLevel && waterAdjacent &&
        (top == BlockId::Grass || top == BlockId::Sand) &&
        !biomeHasAny(biomeId, BiomeTag::Frozen | BiomeTag::Snowy) &&
        noise::hashUnit2D(seed ^ 0x58c31a9u, worldX, worldZ) < 0.18f) {
        return {BlockId::SugarCane,
                kSugarCaneBase + static_cast<int>(noise::hashUnit2D(seed ^ 0x9b12f7u, worldX,
                                                                    worldZ) *
                                                  static_cast<float>(kSugarCaneSpread))};
    }

    const bool wetBiome = biomeHasAny(biomeId, BiomeTag::Wet | BiomeTag::Swamp);
    const bool coldBiome = biomeHasAny(biomeId, BiomeTag::Cold | BiomeTag::Snowy);

    // **Cover comes in patches, not as an even speckle.** A flat per-column
    // probability spreads the same plant every N columns across a whole
    // continent, which reads as noise rather than as meadow; the reference
    // scatters ~32 tries inside a small radius and leaves the ground between
    // them bare. One low-frequency field multiplying the density buys the same
    // thing: thick stands with clearings between them, at the same average.
    const float coverPatch =
        noise::value2D(seed ^ 0x6d1e5a3u, static_cast<float>(worldX) / kCoverPatchWavelength,
                       static_cast<float>(worldZ) / kCoverPatchWavelength);
    const float coverDensity =
        biome.grassDensity * (kCoverPatchFloor + (2.0f - kCoverPatchFloor) * coverPatch * coverPatch);

    // Snow is a top block here rather than a layer over grass, so asking only
    // for grass left the three snowy biomes with no ground cover at all - and
    // their `grassDensity` rows unread.
    if ((top == BlockId::Grass || top == BlockId::Snow) &&
        noise::hashUnit2D(seed ^ 0x91a5eedu, worldX, worldZ) < coverDensity) {
        const float pick = noise::hashUnit2D(seed ^ 0x5f3aa17u, worldX, worldZ);
        BlockId cover = BlockId::TallGrass;
        // A share of the plain cover is fern rather than grass, and only where
        // it belongs - the reference puts it in forest and taiga, not on open
        // plains.
        if (biomeHasTag(biomeId, BiomeTag::Forest) &&
            noise::hashUnit2D(seed ^ 0x3e5b1c9u, worldX, worldZ) < 0.35f) {
            cover = BlockId::Fern;
        }
        if (pick < biome.flowerShare) {
            // Which flower is a **regional** choice, so a meadow reads as a
            // meadow rather than as confetti - but the region is a *discrete
            // cell hash*, not a smooth field. A smooth field piles up around
            // its midpoint, so mapping one across the list handed most of the
            // world whichever species happened to sit in the middle of it. A
            // hash is flat, so all of them are equally likely.
            //
            // **Eleven, and it was twelve with `Cornflower` written twice.**
            // The reference has twelve small flowers; the twelfth is the blue
            // orchid, which is the swamp's alone and is set below rather than
            // rolled for, so eleven is every species this list may offer. The
            // duplicate made cornflowers *twice* as common as anything else -
            // 2/12 against 1/12 - three lines under a comment promising that
            // "all of them are equally likely", which is the shape where the
            // prose is right and the table quietly is not. **Do not pad this to
            // a round number**: the size is the count of species, and `%
            // kFlowers.size()` below reads it off the array rather than from a
            // literal, so removing a row cannot leave an index behind.
            constexpr std::array<BlockId, 11> kFlowers{
                BlockId::Dandelion,  BlockId::Poppy,          BlockId::Cornflower,
                BlockId::OxeyeDaisy, BlockId::AzureBluet,     BlockId::Allium,
                BlockId::RedTulip,   BlockId::OrangeTulip,    BlockId::PinkTulip,
                BlockId::WhiteTulip, BlockId::LilyOfTheValley};
            const int cellX = worldX >> kFlowerRegionShift;
            const int cellZ = worldZ >> kFlowerRegionShift;
            std::size_t index = noise::hash2D(seed ^ 0x1f0e9a3u, cellX, cellZ) % kFlowers.size();

            // A minority of every patch is something else. Without it each
            // region is a monoculture with a hard seam at the cell edge.
            if (noise::hashUnit2D(seed ^ 0x77c1a2bu, worldX, worldZ) < kFlowerStrays) {
                index = noise::hash2D(seed ^ 0x2b90c17u, worldX, worldZ) % kFlowers.size();
            }
            cover = kFlowers[index];
            // The blue orchid is the swamp's alone in the reference, and giving
            // it a home is what stops it being one more face in the crowd.
            if (biomeHasTag(biomeId, BiomeTag::Swamp)) {
                cover = BlockId::BlueOrchid;
            }
        }
        return {cover, 1};
    }

    if (biomeHasTag(biomeId, BiomeTag::Dry) && top == BlockId::Sand && surface > kSeaLevel + 2) {
        const float scrub = noise::hashUnit2D(seed ^ 0x2c9b4d1u, worldX, worldZ);
        // Clear of the shoreline, so a beach does not sprout scrub.
        if (scrub < 0.010f) {
            // Cactus stands in ones and twos, never in a thicket - the
            // reference refuses to place one touching another, and a low enough
            // roll is the same thing statistically without needing to read a
            // neighbouring column.
            return {BlockId::Cactus,
                    kCactusBase + static_cast<int>(noise::hashUnit2D(seed ^ 0x7c2a91u, worldX,
                                                                     worldZ) *
                                                   static_cast<float>(kCactusSpread))};
        }
        if (scrub < 0.030f) {
            return {BlockId::DeadBush, 1};
        }
        return {};
    }

    if (top == BlockId::Grass && (wetBiome || coldBiome) &&
        noise::hashUnit2D(seed ^ 0x6b1f7a3u, worldX, worldZ) < 0.006f) {
        // Mushrooms in the damp and the dark, and berries in the cold. Both
        // existed as blocks and generated nowhere.
        if (coldBiome) {
            return {BlockId::SweetBerryBush, 1};
        }
        return {noise::hash2D(seed ^ 0x11c7e5u, worldX, worldZ) % 2u == 0u ? BlockId::BrownMushroom
                                                                           : BlockId::RedMushroom,
                1};
    }

    // Bamboo, which wants the wet forest rather than the open grassland, and
    // stands taller than anything else here. Last, so it only takes ground
    // nothing else claimed - which is what the old `chunk.at(...) == Air` test
    // was doing, expressed as an order rather than as a read.
    if (top == BlockId::Grass && biomeHasTag(biomeId, BiomeTag::Wet) &&
        biomeHasTag(biomeId, BiomeTag::Forest) &&
        noise::hashUnit2D(seed ^ 0x4d90b13u, worldX, worldZ) < 0.045f) {
        return {BlockId::Bamboo,
                kBambooBase + static_cast<int>(noise::hashUnit2D(seed ^ 0x2f77c1u, worldX, worldZ) *
                                               static_cast<float>(kBambooSpread))};
    }

    return {};
}

} // namespace

int surfaceHeightAt(std::uint32_t seed, int worldX, int worldZ) {
    // Chunk generation calls the identical function, so the two cannot disagree
    // about where the ground is - which is what stops a tree hanging in the air.
    return columnHeight(seed, worldX, worldZ);
}

bool surfaceCarvedAt(std::uint32_t seed, int worldX, int worldZ) {
    const int surface = columnHeight(seed, worldX, worldZ);
    return isCave(seed, worldX, surface, worldZ, surface);
}

Chunk generateChunk(std::uint32_t seed, ChunkCoord coord) {
    Chunk chunk;
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // The terrain top of every column in this chunk **and one ring around it**,
    // because the `steep` rule has to ask its neighbours how far they drop -
    // and, for the columns of this chunk, the climate that produced it.
    //
    // This one array is the whole world model for the chunk: terrain is
    // single-valued, so "is this cell rock" is `y <= top` and nothing else.
    //
    // **Climate is cached rather than asked for twice.** It is about twenty
    // noise evaluations - eighty hashes; the height ring already computes one
    // for every column it touches, and the surface pass below used to throw
    // that away and compute a second identical one. Keeping it is bit-for-bit
    // the same answer, because it is the same call with the same arguments.
    //
    // **It is not the dominant cost of generating a chunk, which is what an
    // earlier note here claimed, and the measurement is not close.** 144 column
    // stacks (12 x 12, seed 0x5eed1234, /O2, this machine): a stack is 13.6 ms
    // and this ring is ~1.1 ms of each of the three chunks in it, so rebuilding
    // it per chunk rather than per stack wastes about 15% and hoisting it is a
    // 1.18x win at best - which would also cost `generateChunk` its (seed,
    // chunkCoord) signature. Binning the same stacks by height band says where
    // the time actually is: **cy 0 (y 0-31) 66.9%, cy 1 25.2%, cy 2 8.2%.** The
    // bottom third of the world is two thirds of generation, and it is `isCave`
    // plus the ore, deepslate and bedrock passes grinding over solid rock. That
    // is the place to optimise; this is not.
    constexpr int kSpan = Chunk::kSize + 2;
    std::array<int, static_cast<std::size_t>(kSpan) * kSpan> heights{};
    std::array<Climate, static_cast<std::size_t>(kSpan) * kSpan> climates{};
    for (int z = -1; z <= Chunk::kSize; ++z) {
        for (int x = -1; x <= Chunk::kSize; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(z + 1) * kSpan + static_cast<std::size_t>(x + 1);
            const ColumnSample sample = sampleColumn(seed, baseX + x, baseZ + z);
            heights[index] = sample.surface;
            climates[index] = sample.climate;
        }
    }
    const auto heightAt = [&](int lx, int lz) {
        return heights[static_cast<std::size_t>(lz + 1) * kSpan + static_cast<std::size_t>(lx + 1)];
    };
    const auto climateAtLocal = [&](int lx, int lz) -> const Climate& {
        return climates[static_cast<std::size_t>(lz + 1) * kSpan + static_cast<std::size_t>(lx + 1)];
    };

    const int chunkTop = std::min(kWorldTop, baseY + Chunk::kSize - 1);

    for (int z = 0; z < Chunk::kSize; ++z) {
        for (int x = 0; x < Chunk::kSize; ++x) {
            const int worldX = baseX + x;
            const int worldZ = baseZ + z;
            const int surface = heightAt(x, z);

            const Climate& climate = climateAtLocal(x, z);
            const BiomeId biomeId = biomeFor(climate);
            const Biome& biome = biomeInfo(biomeId);

            // **Steep is one facing, not all four.** The reference's condition
            // compares the column one step south against one step north and
            // fires only when the south side is the high one, so a summit shows
            // rock on one face and keeps its snow on the other. `+Z` is south
            // here, pinned by `lidTurnsFacing` in `Block.hpp`, so this reads
            // exactly as `SurfaceRules.Steep` does. Testing all four
            // neighbours symmetrically, as ours did, fires on every slope from
            // either side and frosts the whole mountain in bare stone instead.
            const bool steep = heightAt(x, z + 1) >= heightAt(x, z - 1) + kSteepDropOverTwoColumns;

            const bool submerged = surface < kSeaLevel;

            // **First match wins, and every branch that does not match falls
            // through to the biome's ordinary top block.** That is the
            // reference's "no else" pattern, and writing it the other way round
            // \u2014 a solid stone top on a biome that occupies a narrow band \u2014 is
            // what put rings of bare stone across the landscape.
            BlockId filler = biome.filler;
            BlockId top = biome.top;
            const float patch = noise::octaves2D(
                seed ^ 0x5a7c4e11u, static_cast<float>(worldX) / kPatchWavelength,
                static_cast<float>(worldZ) / kPatchWavelength, kPatchAmplitudes.data(),
                static_cast<int>(kPatchAmplitudes.size()));
            if (submerged) {
                // **Above the waterline a bed material is not reachable at all
                // in the reference.** Gravel is placed only after both of its
                // water tests have failed, which cannot happen with no water
                // overhead - which is why `river` is named nowhere in its
                // surface rules and a dry river strip through the mountains
                // comes out as plain grass. Ours painted the biome's own top
                // block regardless of the waterline, so a channel that never
                // reached the sea came out as a long ribbon of gravel and sand
                // across dry grassland. Sand never appears on a riverbed there.
                const bool warm = biomeHasAny(biomeId, BiomeTag::Sandy | BiomeTag::Hot);
                const bool shallow = (kSeaLevel - surface) <= kShallowBedDepth;

                // **The biome's own material is asked first, and the depth rule
                // only decides what a biome that has nothing to say gets.**
                // That is the reference's ordering - a biome surface rule sits
                // above the water-depth rule - and reversing it is what put a
                // ring of brown dirt at the exact waterline of every sandy
                // shore in the world: `shallow` matched first, so the sand a
                // beach is made of was unreachable the moment it went under.
                top = warm ? BlockId::Sand : (shallow ? BlockId::Dirt : BlockId::Gravel);
                filler = warm ? BlockId::Sandstone : (shallow ? BlockId::Dirt : BlockId::Stone);

                // The one bed material a biome may name for itself. Everything
                // else about a seabed is decided by depth, so this is opt-in per
                // biome rather than a rule the shoreline has to dodge.
                if (!shallow && biome.patch == BlockId::Prismarine &&
                    patch >= biome.patchThreshold) {
                    top = BlockId::Prismarine;
                    filler = BlockId::Prismarine;
                }
            } else if (steep && biome.steep != BlockId::Air) {
                top = biome.steep;
            } else if (biome.patch != BlockId::Air) {
                if (patch >= biome.patchThreshold) {
                    top = biome.patch;
                }
            }

            // Snow is an altitude rule against the **biome's own** temperature,
            // so a warm lowland never whitens however cold its neighbour is,
            // and a cold one is white at sea level. Deliberately does not
            // override bare rock on a steep face — that is the point of `steep`.
            if (top == BlockId::Grass && freezesAt(seed, biome.warmth, worldX, surface, worldZ)) {
                top = BlockId::Snow;
            }

            // Varying the filler depth is most of why the reference's ground
            // does not look extruded: a constant one puts the dirt-to-stone
            // boundary on a contour line.
            const float depthNoise =
                noise::value2D(seed ^ 0x5c1f00du, static_cast<float>(worldX) / kSurfaceDepthWavelength,
                               static_cast<float>(worldZ) / kSurfaceDepthWavelength);
            const int fillerDepth = biome.fillerDepth + static_cast<int>(depthNoise * 3.0f);

            // One downward walk over the column. `stoneDepth` is exact without
            // having to start above this chunk, because terrain is single-valued
            // and the top is already known.
            for (int y = std::min(chunkTop, surface); y >= baseY; --y) {
                const int stoneDepth = surface - y + 1;

                BlockId block = BlockId::Stone;

                if (y <= kBedrockSolid ||
                    (y <= kBedrockTop &&
                     noise::hashUnit3D(seed ^ 0xbed0c4u, worldX, y, worldZ) <
                         static_cast<float>(kBedrockTop + 1 - y) / static_cast<float>(kBedrockTop + 1))) {
                    block = BlockId::Bedrock;
                } else if (stoneDepth == 1) {
                    // The reference's top rule is exactly one block deep. Depth
                    // belongs to the filler underneath it.
                    block = top;
                } else if (stoneDepth <= 1 + fillerDepth) {
                    block = filler;
                } else if (filler == BlockId::Sand &&
                           stoneDepth <= 1 + fillerDepth + kSandstoneDepth) {
                    // Sand sits on sandstone rather than straight on stone.
                    // Keyed off the filler so it needs no biome name.
                    block = BlockId::Sandstone;
                } else if (y <= kDeepslateAlways ||
                           (y < kDeepslateNever &&
                            noise::hashUnit3D(seed ^ 0xdee9a1eu, worldX, y, worldZ) <
                                static_cast<float>(kDeepslateNever - y) /
                                    static_cast<float>(kDeepslateNever - kDeepslateAlways))) {
                    block = BlockId::Deepslate;
                }

                // A cave is a hole in a finished world rather than a different
                // world, which is why carving comes after classification and
                // never after the surface rules have read the ground.
                if (block != BlockId::Bedrock && isCave(seed, worldX, y, worldZ, surface)) {
                    continue;
                }

                chunk.set(x, y - baseY, z, block);
            }

            // **Lava does for the floor of the world what the sea does for its
            // top, and it is written in the same place and the same way.** The
            // carve pass has just finished, so which cells are empty is settled;
            // filling them is a decision about this column and nothing else, so
            // it stays pure and two chunks that share a cave agree without
            // either reading the other.
            //
            // **Only cells at or below the terrain top are filled**, which is
            // the ocean rule read the other way round. There it fills what is
            // *above* the ground and under the waterline; here it fills what the
            // carver took out of ground that is still overhead. The measured
            // minimum surface anywhere is y 7, so no column can reach down here
            // and no lava can ever be open to the sky - but the test is written
            // rather than assumed, because that measurement is a fact about
            // today's splines and not a guarantee.
            //
            // **Source lava, exactly as the sea is all source.** `Lava0` is the
            // level-0 id, so a cave floor does not drain into the first tunnel
            // the player digs into it - the same reasoning that made the ocean
            // all source, and the difference between a hazard and a slow flood
            // that empties itself.
            //
            // **Bedrock is not special-cased and does not need to be.** The
            // carve pass refuses to cut bedrock at all, so those cells are never
            // empty, and the `!= BlockId::Air` test below already declines them.
            const int lavaTop = std::min(kLavaLevel - baseY, Chunk::kSize - 1);
            for (int y = 0; y <= lavaTop; ++y) {
                const int worldY = y + baseY;
                if (worldY > surface || chunk.at(x, y, z) != BlockId::Air) {
                    continue;
                }
                chunk.set(x, y, z, BlockId::Lava0);
            }

            // Everything the **uncarved** terrain leaves empty below sea level
            // is ocean. Testing against the terrain top rather than against what
            // is in the chunk now is the whole difference between a sea and a
            // drowned world: a cave cut into rock that happens to sit under the
            // waterline stays dry, which is what the reference's aquifers are
            // for and what we get here for nothing. Sea water is all source, so
            // it never drains into whatever the player digs.
            // Ice on the water is the *same* rule as snow on the ground — one
            // `freeze_top_layer` does both in the reference — so it asks the
            // same question rather than consulting a hand-written tag that
            // means the same thing and can disagree with it.
            const bool frozen = freezesAt(seed, biome.warmth, worldX, kSeaLevel, worldZ);
            const int waterTop = std::min(kSeaLevel - baseY, Chunk::kSize - 1);
            for (int y = 0; y <= waterTop; ++y) {
                const int worldY = y + baseY;
                if (worldY <= surface || chunk.at(x, y, z) != BlockId::Air) {
                    continue;
                }
                chunk.set(x, y, z, frozen && worldY == kSeaLevel ? BlockId::Ice : BlockId::Water0);
            }

            // Seabed cover. Kelp and seagrass need water over them and coral
            // wants the warm shallows, so all three ride on the same test the
            // water fill just did rather than re-deriving where the sea is.
            const int bedY = surface + 1 - baseY;
            if (submerged && bedY >= 1 && bedY < Chunk::kSize && surface < kSeaLevel &&
                chunk.at(x, bedY, z) != BlockId::Air &&
                chunk.at(x, bedY - 1, z) != BlockId::Air) {
                const float roll = noise::hashUnit2D(seed ^ 0x5ea9a55u, worldX, worldZ);
                const bool warm = biomeHasAny(biomeId, BiomeTag::Hot | BiomeTag::Sandy);
                if (warm && roll < 0.05f) {
                    constexpr std::array<BlockId, 5> kCorals{
                        BlockId::TubeCoralBlock, BlockId::BrainCoralBlock, BlockId::BubbleCoralBlock,
                        BlockId::FireCoralBlock, BlockId::HornCoralBlock};
                    chunk.set(x, bedY - 1, z,
                              kCorals[noise::hash2D(seed ^ 0xc0a1u, worldX, worldZ) % kCorals.size()]);
                    // Sea pickles cluster on a reef and nowhere else, which is
                    // what makes finding one mean something.
                    if (roll < 0.018f) {
                        chunk.set(x, bedY, z, BlockId::SeaPickle);
                        chunk.setWaterlogged(x, bedY, z, true);
                    }
                } else if (roll < 0.14f) {
                    // Waterlogged rather than written over the water: the plant
                    // shares the cell, which is the whole point of the bit.
                    chunk.set(x, bedY, z, roll < 0.07f ? BlockId::Seagrass : BlockId::Kelp);
                    chunk.setWaterlogged(x, bedY, z, true);
                }
            }

            // Plants stand on the surface block, so **the decision belongs to
            // the column and only the writing belongs to a chunk.** Every gate
            // here is pure - a hash, this column's own surface, or a
            // neighbouring column's height out of the ring - so the chunk that
            // holds the root and the chunk that holds the tip reach the same
            // answer without either one reading the other.
            //
            // **`plantY == 0` is a real placement, not an out-of-range guard.**
            // It is the column whose surface is the last block of the chunk
            // below, so the plant belongs to this chunk and the ground it stands
            // on does not. Refusing it deleted every plant at y 32 and y 64 -
            // two bare horizontal bands through every world, at each chunk
            // boundary. **A negative `plantY` is a real placement too**, for the
            // same reason read the other way round: a bamboo stalk rooted at
            // y 28 has four blocks of itself in the chunk above, and rejecting
            // the column there is what shaved every stand in the band flat at
            // exactly y 31. The ground is asked for the pure way in both cases -
            // this is `surfaceCarvedAt` with the height it already has, so it
            // cannot disagree with the block the walk above wrote.
            const int plantY = surface + 1 - baseY;
            if (plantY < Chunk::kSize && plantY + kMaxPlantHeight > 0 && surface >= kSeaLevel &&
                !isCave(seed, worldX, surface, worldZ, surface)) {
                // Water against the *supporting* block's own side, which is
                // what the reference asks of sugar cane. A neighbour column
                // lower than this one is flooded from its own top up to sea
                // level, so it holds water at this column's surface height
                // exactly when it is shorter - no block read needed, and the
                // ring already has every height involved.
                const bool waterAdjacent =
                    surface <= kSeaLevel &&
                    (heightAt(x - 1, z) < surface || heightAt(x + 1, z) < surface ||
                     heightAt(x, z - 1) < surface || heightAt(x, z + 1) < surface);

                const PlantColumn plant =
                    plantAt(seed, worldX, worldZ, surface, biomeId, biome, top, waterAdjacent);

                // Clipped at both ends rather than at the top only. The cells
                // outside this chunk are not skipped work, they are another
                // chunk's copy of the same stalk.
                for (int i = 0; i < plant.height; ++i) {
                    const int y = plantY + i;
                    if (y >= 0 && y < Chunk::kSize) {
                        chunk.set(x, y, z, plant.block);
                    }
                }
            }

            // Lily pads, which float rather than stand: the cell is the water
            // surface itself, so this is the one plant placed *at* sea level.
            // **Rivers as well as swamps**, because the swamp is two tenths of
            // a percent of the world and almost none of it is under water - a
            // scan of three hundred chunk columns found not one pad until the
            // river was allowed too.
            const int padY = kSeaLevel + 1 - baseY;
            if (padY >= 1 && padY < Chunk::kSize && surface < kSeaLevel &&
                surface > kSeaLevel - 7 &&
                biomeHasAny(biomeId, BiomeTag::Swamp | BiomeTag::River) &&
                !biomeHasAny(biomeId, BiomeTag::Frozen | BiomeTag::Snowy) &&
                chunk.at(x, padY, z) == BlockId::Air &&
                isWater(chunk.at(x, padY - 1, z)) &&
                noise::hashUnit2D(seed ^ 0x3ab7e91u, worldX, worldZ) < 0.10f) {
                chunk.set(x, padY, z, BlockId::LilyPad);
            }
        }
    }

    // Ores go in after the rock is finished and after caves are cut, so a vein
    // can only ever replace stone that survived, and a cave wall stays rock.
    //
    // Veins belong to a **column**, not to this chunk: a vein's height comes
    // from its own band, so the column that owns it may be asking about any of
    // the three chunks stacked above it. Neighbouring columns are rolled too,
    // because a vein anchored near a border reaches across one - and every roll
    // happens whether or not the block lands here, or the same vein would come
    // out differently depending on which chunk generated it.
    const auto exposedToAir = [&](int wx, int wy, int wz) {
        const auto open = [&](int nx, int ny, int nz) {
            const int nTop = heightAt(nx - baseX, nz - baseZ);
            return ny > nTop || isCave(seed, nx, ny, nz, nTop);
        };
        return open(wx - 1, wy, wz) || open(wx + 1, wy, wz) || open(wx, wy, wz - 1) ||
               open(wx, wy, wz + 1) || open(wx, wy + 1, wz) || open(wx, wy - 1, wz);
    };

    // **Geodes run before the veins, and the order is load-bearing.**
    // `placeVein` writes only into `Stone` and `Deepslate`, and every block a
    // geode leaves behind is neither - so running first is what stops an ore
    // appearing embedded in a calcite shell or floating in the hollow. The
    // reverse order would also let a vein's cell block a shell cell, cutting a
    // hole in the geode wherever the two met.
    //
    // Same neighbour-search shape as the vein loop below and for the same
    // reason: a geode centred in the next chunk still reaches into this one.
    {
        const int geodeSpan = (kGeodeReach + Chunk::kSize - 1) / Chunk::kSize;
        for (int nz = -geodeSpan; nz <= geodeSpan; ++nz) {
            for (int nx = -geodeSpan; nx <= geodeSpan; ++nx) {
                const int columnX = coord.x + nx;
                const int columnZ = coord.z + nz;

                noise::Stream site{noise::hash2D(seed ^ kGeodeSalt, columnX, columnZ)};
                if (site.range(kGeodeOneInColumns) != 0) {
                    continue;
                }

                const int originX = columnX * Chunk::kSize + site.range(Chunk::kSize);
                const int originZ = columnZ * Chunk::kSize + site.range(Chunk::kSize);
                const int originY = kGeodeMinY + site.range(kGeodeMaxY - kGeodeMinY + 1);

                // Drawn before the range test, so a geode nowhere near this
                // chunk can be dropped without shifting the one after it.
                noise::Stream shape{site.next()};
                if (originY + kGeodeReach < baseY ||
                    originY - kGeodeReach >= baseY + Chunk::kSize) {
                    continue;
                }
                placeGeode(chunk, coord, originX, originY, originZ, shape);
            }
        }
    }

    // **The vein loop is outermost, so "rarest first" is a fact about the world
    // rather than about one column.** With columns outside, a neighbouring
    // column's coal ran before this column's diamond and could take a cell from
    // it - the ordering held nine times over and never once between them.
    // Nothing else moves: each vein's stream is seeded from its own salt and its
    // own column, so the rolls are identical whichever way the loops nest, and
    // only who wins a contested cell changes.
    const int columnSpan = (kVeinReach + Chunk::kSize - 1) / Chunk::kSize;
    for (const OreVein& vein : kOreVeins) {
        for (int nz = -columnSpan; nz <= columnSpan; ++nz) {
            for (int nx = -columnSpan; nx <= columnSpan; ++nx) {
                const int columnX = coord.x + nx;
                const int columnZ = coord.z + nz;

                noise::Stream site{noise::hash2D(seed ^ vein.salt, columnX, columnZ)};
                for (int attempt = 0; attempt < vein.attempts; ++attempt) {
                    const int originX = columnX * Chunk::kSize + site.range(Chunk::kSize);
                    const int originZ = columnZ * Chunk::kSize + site.range(Chunk::kSize);
                    const int originY = veinHeight(vein, site);

                    // Each vein carries a stream of its own, drawn from the
                    // column's. That is what lets a vein nowhere near this chunk
                    // be dropped outright without shifting every vein after it -
                    // and two thirds of them are nowhere near, because a column
                    // spans three chunks and a vein sits in one.
                    noise::Stream shape{site.next()};
                    if (originY + kVeinReach < baseY ||
                        originY - kVeinReach >= baseY + Chunk::kSize) {
                        continue;
                    }
                    placeVein(chunk, coord, vein, originX, originY, originZ, shape, exposedToAir);
                }
            }
        }
    }

    // Villages are solved once here and handed to both passes that need the
    // answer: the tree pass, so nothing grows through a roof, and the village
    // pass itself. Solving it twice would rebuild the same plan per tree cell.
    const village::Nearby villages = village::plansNear(seed, coord.x, coord.z);

    structures::generateInto(chunk, seed, coord, villages);
    village::generateInto(chunk, seed, coord, villages);

    return chunk;
}

} // namespace game
