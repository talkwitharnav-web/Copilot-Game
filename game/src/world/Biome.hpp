#pragma once

#include "world/Block.hpp"
#include "world/Climate.hpp"

#include <cstdint>

namespace game {

/// Which region of the world a column belongs to.
///
/// **Order is table order and table order is priority.** Where two rows claim
/// overlapping boxes in climate space the earlier one wins, so the specific
/// rows are listed before the general ones. Nothing persists a `BiomeId`, so
/// this enum may be reordered freely — unlike `BlockId` and `ItemId`, which are
/// written to disk and can only ever be appended to.
enum class BiomeId : std::uint8_t {
    // Sea. Claimed on continentalness alone, before anything else looks.
    FrozenOcean,
    WarmOcean,
    DeepOcean,
    Ocean,

    // The narrow strip where the continental shelf meets the water.
    SnowyBeach,
    StonyShore,
    Beach,

    // Valleys. These sit on the zero contour of the ridge field, which is a
    // network of curves rather than a region, so they must be claimed before
    // any lowland row swallows them.
    FrozenRiver,
    River,

    // Low erosion: the land the 3D noise is not allowed to flatten.
    FrozenPeaks,
    JaggedPeaks,
    StonyPeaks,
    SnowySlopes,
    Grove,
    Meadow,

    // The reference's narrow erosion band 5, where relief comes back.
    WindsweptHills,
    GravellyHills,

    // Everything else, resolved on temperature against humidity.
    SnowyTaiga,
    SnowyPlains,
    Taiga,
    DenseForest,
    // The three jungles sit **before** the two general rows, because all three
    // overlap in climate space and the first row containing a point wins
    // outright. The swamp goes ahead of *them*, on the very eroded lowland it
    // actually belongs to, or a jungle would swallow every wet flat in the
    // warm band.
    Swamp,
    Jungle,
    SparseJungle,
    BambooJungle,
    Badlands,
    Desert,
    Savanna,
    Forest,
    Plains,

    Count,
};

/// Everything below this fills with water. Terrain that dips under it becomes
/// seabed rather than a dry pit.
constexpr int kSeaLevel = 24;

/// What a biome *is*, so that anything asking a question about a place does not
/// have to name every biome that answers it.
///
/// This is the reference's `has_biome_tag`, and it exists for one reason: the
/// creature spawn rules used to enumerate biomes by name, so adding a biome
/// meant editing fifty of them and forgetting one was silent. A rule now asks
/// for a property, and a new biome inherits every rule it qualifies for.
enum class BiomeTag : std::uint32_t {
    None = 0,
    Ocean = 1u << 0,
    River = 1u << 1,
    Beach = 1u << 2,
    /// Cold enough that exposed water freezes.
    Frozen = 1u << 3,
    Cold = 1u << 4,
    Hot = 1u << 5,
    Dry = 1u << 6,
    Wet = 1u << 7,
    Grassland = 1u << 8,
    Forest = 1u << 9,
    /// Hills and slopes: high ground that is not a summit.
    Highland = 1u << 10,
    Peak = 1u << 11,
    Stony = 1u << 12,
    Sandy = 1u << 13,
    Snowy = 1u << 14,
    Swamp = 1u << 15,
    Badlands = 1u << 16,
    /// Hot, wet and shaded. What a parrot, an ocelot and a panda ask for, and
    /// what a vine, a cocoa pod and a melon grow in.
    Jungle = 1u << 17,
};

constexpr std::uint32_t operator|(BiomeTag a, BiomeTag b) {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}

constexpr std::uint32_t operator|(std::uint32_t a, BiomeTag b) {
    return a | static_cast<std::uint32_t>(b);
}

/// Which building set a village here is made of, or `None` for a biome that
/// never holds one.
///
/// **This is a column on the biome table rather than a question asked of tags**,
/// and that is the reference's own design: since Bedrock 26.0 every biome that
/// can host a village carries a `minecraft:village_type` component, and a biome
/// without one simply never generates one. Defaulting to `None` means a new
/// biome opts in explicitly, which is the safe direction — the alternative is a
/// `villageTypeFor(BiomeId)` switch somewhere else, and a second copy of a fact
/// is the most common bug in this codebase.
enum class VillageType : std::uint8_t {
    None,
    Plains,
    Desert,
    Savanna,
    Taiga,
    /// The reference calls this one `ice`. It has **no architecture of its own**
    /// — it is the taiga set with snow laid over everything the sky can see.
    Snowy,
};

/// A tree's silhouette. One log and one leaf block cover both, because what
/// separates a conifer from a broadleaf here is the shape of the canopy rather
/// than its colour.
enum class TreeShape : std::uint8_t {
    None,
    Round,
    Tall,
    /// Three trees at once, in the reference's own proportions: mostly bushes,
    /// a good share of small jungle trees, and a few two-by-two giants. It is
    /// one shape rather than three biome rows because what separates a jungle
    /// from a sparse one is **how many**, not which.
    Jungle,
};

/// Half-open interval a biome claims along one climate axis.
///
/// **A biome claims a box, not a point.** That is the reference's structure and
/// it is genuinely different from what was here before: with centres and a
/// distance falloff every biome bleeds a little way into every other, and the
/// table has no way to say "this one is *only* ever cold". A box says exactly
/// that, and a point outside every box falls to the nearest one.
struct ClimateRange {
    float min = -1.0f;
    float max = 1.0f;
};

/// Out of reach, for a patch rule that should never fire.
constexpr float kNoPatch = 99.0f;

/// Everything that varies between regions, in one row per biome.
///
/// **There is no height here, and putting one back is the mistake this comment
/// exists to stop.** A biome used to carry `baseHeight` and `amplitude` and the
/// terrain was looked up from it, which made the land a consequence of the
/// label; height now comes from `Shape`, computed from the same climate fields
/// that pick the biome.
///
/// `warmth` is the one number that did *not* deserve that treatment, and
/// leaving it out is what put snow on plains. See its comment below.
struct Biome {
    const char* name;

    /// The biome's own fixed temperature, in the reference's units, straight
    /// out of its biome JSON. **This is a constant per biome and has nothing to
    /// do with `temperature` below**, which is the box the biome claims in the
    /// climate noise. Confusing the two is precisely the bug: the noise is
    /// continuous across a biome edge, so asking it whether a column freezes
    /// scatters snow through the warm side of every cold border.
    ///
    /// Only `freezesAt` reads it. Anything at or below 0.15 is white at sea
    /// level; above about 0.42 nothing in a world this short can ever chill it
    /// enough, which is why plains at 0.8 cannot whiten at any altitude.
    float warmth;

    /// The exposed block, what sits underneath it, and how deep that goes.
    /// `fillerDepth` is a floor: the surface rules add a noise-driven extra, so
    /// the boundary is not a contour line at a constant depth.
    BlockId top;
    BlockId filler;
    int fillerDepth;

    /// Scattered over `top` wherever the surface noise reaches `patchThreshold`.
    ///
    /// **This is the reference's "no else" pattern and it is load-bearing.** A
    /// biome names what it scatters and *anything that does not match falls
    /// through to the ordinary rule* — so windswept hills is grass with stone
    /// patches rather than a slab of stone. Writing it the other way round, as
    /// a solid top block, is how you manufacture a ring of bare stone wherever
    /// the biome's own band happens to fall.
    BlockId patch;
    float patchThreshold;

    /// What shows on a steep face, or `Air` for no rule.
    ///
    /// The reference's `steep`, and its only slope-driven bare stone. It is the
    /// difference between a peak that reads as rock with snow on it and one
    /// that reads as a white blob. Do **not** apply it to ordinary ground.
    BlockId steep;

    /// Chance that any given placement cell holds a tree, and what shape it is.
    float treeDensity;
    TreeShape treeShape;

    /// Ground cover: how much of the surface carries something, and what share
    /// of that comes up a flower rather than a tuft.
    float grassDensity;
    float flowerShare;

    std::uint32_t tags;

    /// Where this biome sits in climate space. `{-1, 1}` means "any".
    ClimateRange temperature;
    ClimateRange humidity;
    ClimateRange continentalness;
    ClimateRange erosion;
    ClimateRange ridges;
    /// **Raw weirdness, not the ridge field derived from it.** `ridges` folds
    /// the sign away - it reaches -1 wherever weirdness crosses zero from
    /// either side - so a biome that exists only on one side of that crossing
    /// cannot be written with it. The reference splits jungle from sparse and
    /// bamboo jungle on exactly this sign, and it is the only axis that says so.
    ///
    /// Defaulted to "any", which is what every row that predates it wants.
    ClimateRange weirdness{};

    /// Which village set stands here, if any. Ten biomes carry it in the
    /// reference; seven of ours do.
    VillageType villageType = VillageType::None;
};

const Biome& biomeInfo(BiomeId id);

bool biomeHasTag(BiomeId id, BiomeTag tag);

/// True if the biome carries **any** of the tags in the mask.
bool biomeHasAny(BiomeId id, std::uint32_t mask);

/// The biome whose box contains this climate, or the nearest one if none does.
BiomeId biomeFor(const Climate& climate);

/// Everything a column needs, sampled once.
///
/// Terrain shape is here rather than on the biome because it is a fact about
/// the *place*, not about the label — see `Climate.hpp`.
struct BiomeSample {
    BiomeId dominant;
    Climate climate;
    Shape shape;
};

BiomeSample sampleBiome(std::uint32_t seed, int worldX, int worldZ);

/// Highest `treeDensity` in the table. Structure placement uses it to reject
/// empty cells before doing any noise work.
float maxTreeDensity();

} // namespace game
