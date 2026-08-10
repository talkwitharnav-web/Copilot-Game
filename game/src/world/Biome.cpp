#include "world/Biome.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace game {
namespace {

// The band edges are the reference's own, straight out of `OverworldBiomeBuilder`.
// They are named rather than repeated so a row reads as "cold and damp" instead
// of as five pairs of unexplained decimals.

constexpr float kFrozenT = -0.45f;
constexpr float kColdT = -0.15f;
constexpr float kTemperateT = 0.20f;
constexpr float kWarmT = 0.55f;

constexpr float kAridH = -0.35f;
constexpr float kDryH = -0.10f;
constexpr float kDampH = 0.10f;
constexpr float kWetH = 0.30f;

constexpr float kDeepOceanC = -0.455f;
constexpr float kOceanC = -0.19f;
constexpr float kCoastC = -0.11f;

/// Erosion band 2 ends here: everything below is the unworn ground that keeps
/// its relief, everything above is lowland.
constexpr float kUplandE = -0.375f;
/// The reference's narrow band 5, where relief comes back and terrain shatters.
constexpr float kWindsweptLowE = 0.45f;
constexpr float kWindsweptHighE = 0.55f;

constexpr float kValleyPV = -0.85f;
constexpr float kHillPV = 0.20f;
constexpr float kPeakPV = 0.70f;

/// Where the reference splits a biome from its variant. **Not zero**: vanilla
/// slices weirdness into thirteen bands and tests whether a band's *maximum* is
/// below zero, and the band running from -0.267 to -0.05 therefore counts as
/// the low side. So the crossing sits at -0.05.
constexpr float kJungleW = -0.05f;

constexpr ClimateRange kAny{-1.0f, 1.0f};

/// One row per biome, in `BiomeId` order, **specific before general**.
///
/// The selector returns the first row whose box contains the climate, so an
/// earlier row shadows a later one wherever they overlap. That is deliberate
/// and is what lets the general rows quote wide boxes without having to carve
/// holes in them: `Plains` may claim the whole temperate band because `Savanna`
/// and `Forest` have already taken their corners of it.
constexpr std::array<Biome, static_cast<std::size_t>(BiomeId::Count)> kBiomes{{
    // The four ocean rows and the two river rows carry **grass and dirt**, not
    // a bed material. Their top block is only ever consulted on dry ground,
    // because everything below the waterline is decided by depth instead - so
    // a river that fails to reach the sea is a grassy dip rather than a ribbon
    // of gravel, which is exactly what the reference does by never naming
    // `river` in its surface rules at all.
    {.name = "Frozen Ocean", .warmth = 0.0f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Ocean | BiomeTag::Frozen | BiomeTag::Cold,
     .temperature = {-1.0f, kFrozenT}, .humidity = kAny, .continentalness = {-1.0f, kOceanC},
     .erosion = kAny, .ridges = kAny},

    {.name = "Warm Ocean", .warmth = 0.5f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Ocean | BiomeTag::Hot | BiomeTag::Sandy,
     .temperature = {kWarmT, 1.0f}, .humidity = kAny, .continentalness = {-1.0f, kOceanC},
     .erosion = kAny, .ridges = kAny},

    {.name = "Deep Ocean", .warmth = 0.5f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Prismarine, .patchThreshold = 0.45f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Ocean),
     .temperature = {kFrozenT, kWarmT}, .humidity = kAny, .continentalness = {-1.0f, kDeepOceanC},
     .erosion = kAny, .ridges = kAny},

    {.name = "Ocean", .warmth = 0.5f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Ocean),
     .temperature = {kFrozenT, kWarmT}, .humidity = kAny, .continentalness = {kDeepOceanC, kOceanC},
     .erosion = kAny, .ridges = kAny},

    {.name = "Snowy Beach", .warmth = 0.05f,
     .top = BlockId::Snow, .filler = BlockId::Sand, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Beach | BiomeTag::Sandy | BiomeTag::Snowy | BiomeTag::Cold | BiomeTag::Frozen,
     .temperature = {-1.0f, kFrozenT}, .humidity = kAny, .continentalness = {kOceanC, kCoastC},
     .erosion = kAny, .ridges = kAny},

    // The reference's stony shore really is mostly stone, with gravel in a
    // narrow window. It is a thin coastal strip rather than a region, so it
    // reads as a cliff foot rather than as a band painted across the land.
    {.name = "Stony Shore", .warmth = 0.2f,
     .top = BlockId::Stone, .filler = BlockId::Stone, .fillerDepth = 3,
     .patch = BlockId::Gravel, .patchThreshold = 0.0f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Beach | BiomeTag::Stony,
     .temperature = {kFrozenT, 1.0f}, .humidity = kAny, .continentalness = {kOceanC, kCoastC},
     .erosion = {-1.0f, kUplandE}, .ridges = kAny},

    {.name = "Beach", .warmth = 0.8f,
     .top = BlockId::Sand, .filler = BlockId::Sand, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Beach | BiomeTag::Sandy,
     .temperature = {kFrozenT, 1.0f}, .humidity = kAny, .continentalness = {kOceanC, kCoastC},
     .erosion = {kUplandE, 1.0f}, .ridges = kAny},

    {.name = "Frozen River", .warmth = 0.0f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::River | BiomeTag::Frozen | BiomeTag::Cold,
     .temperature = {-1.0f, kFrozenT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = kAny, .ridges = {-1.0f, kValleyPV}},

    {.name = "River", .warmth = 0.5f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.14f, .flowerShare = 0.10f,
     .tags = static_cast<std::uint32_t>(BiomeTag::River),
     .temperature = {kFrozenT, 1.0f}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = kAny, .ridges = {-1.0f, kValleyPV}},

    {.name = "Frozen Peaks", .warmth = -0.7f,
     .top = BlockId::PackedIce, .filler = BlockId::Stone, .fillerDepth = 2,
     .patch = BlockId::Snow, .patchThreshold = 0.10f, .steep = BlockId::PackedIce, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Peak | BiomeTag::Cold | BiomeTag::Frozen | BiomeTag::Snowy | BiomeTag::Stony,
     .temperature = {-1.0f, kFrozenT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f}},

    // `steep` is what makes this read as rock with snow on it. Without it a
    // peak is a solid white blob, which is exactly what the reference avoids.
    {.name = "Jagged Peaks", .warmth = -0.7f,
     .top = BlockId::Snow, .filler = BlockId::Stone, .fillerDepth = 2,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Stone, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Peak | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Stony,
     .temperature = {kFrozenT, kTemperateT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f}},

    {.name = "Stony Peaks", .warmth = 1.0f,
     .top = BlockId::Stone, .filler = BlockId::Stone, .fillerDepth = 2,
     .patch = BlockId::Andesite, .patchThreshold = 0.10f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Peak | BiomeTag::Stony,
     .temperature = {kTemperateT, 1.0f}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f}},

    {.name = "Snowy Slopes", .warmth = -0.3f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Stone, .treeDensity = 0.01f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Highland | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kColdT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kValleyPV, kPeakPV}},

    {.name = "Grove", .warmth = -0.2f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.38f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.04f, .flowerShare = 0.0f,
     .tags = BiomeTag::Forest | BiomeTag::Highland | BiomeTag::Cold | BiomeTag::Snowy,
     .temperature = {kColdT, kWarmT}, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kHillPV, kPeakPV}},

    {.name = "Meadow", .warmth = 0.5f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.04f,
     .treeShape = TreeShape::Round, .grassDensity = 0.34f, .flowerShare = 0.55f,
     .tags = BiomeTag::Grassland | BiomeTag::Highland,
     .temperature = {kColdT, 1.0f}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kValleyPV, kPeakPV}},

    // **Grass with stone patches, not a slab of stone.** The reference places
    // stone only above its own noise threshold and lets everything else fall
    // through to the ordinary rule; a solid stone top in a band this narrow is
    // what came out as rings on the landscape.
    //
    // No `steep` here, and that is the reference's own arrangement rather than
    // an omission: `minecraft:steep` appears five times in its surface rules and
    // every one of them is inside frozen peaks, jagged peaks or snowy slopes. A
    // slope test on ordinary hills draws a stone line along every hillside,
    // which is the streaking this biome was reported for.
    {.name = "Windswept Hills", .warmth = 0.2f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Stone, .patchThreshold = 0.12f, .steep = BlockId::Air, .treeDensity = 0.03f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.22f, .flowerShare = 0.04f,
     .tags = BiomeTag::Highland | BiomeTag::Stony,
     .temperature = kAny, .humidity = {-1.0f, kDampH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kWindsweptLowE, kWindsweptHighE}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Gravelly Hills", .warmth = 0.2f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Gravel, .patchThreshold = 0.05f, .steep = BlockId::Air, .treeDensity = 0.02f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.15f, .flowerShare = 0.02f,
     .tags = BiomeTag::Highland | BiomeTag::Stony,
     .temperature = kAny, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kWindsweptLowE, kWindsweptHighE}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Snowy Taiga", .warmth = -0.5f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.42f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.18f, .flowerShare = 0.0f,
     .tags = BiomeTag::Forest | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kFrozenT}, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Snowy Plains", .warmth = 0.0f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.01f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.14f, .flowerShare = 0.0f,
     .tags = BiomeTag::Grassland | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kFrozenT}, .humidity = {-1.0f, kDampH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Taiga", .warmth = 0.25f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.50f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.26f, .flowerShare = 0.04f,
     .tags = BiomeTag::Forest | BiomeTag::Cold,
     .temperature = {kFrozenT, kColdT}, .humidity = {kDryH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Dense Forest", .warmth = 0.7f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.80f,
     .treeShape = TreeShape::Round, .grassDensity = 0.30f, .flowerShare = 0.06f,
     .tags = BiomeTag::Forest | BiomeTag::Wet,
     .temperature = {kColdT, kTemperateT}, .humidity = {kWetH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Swamp", .warmth = 0.8f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Clay, .patchThreshold = 0.45f, .steep = BlockId::Air, .treeDensity = 0.14f,
     .treeShape = TreeShape::Round, .grassDensity = 0.34f, .flowerShare = 0.05f,
     .tags = BiomeTag::Swamp | BiomeTag::Wet | BiomeTag::Hot,
     .temperature = {kTemperateT, 1.0f}, .humidity = {kWetH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kWindsweptHighE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    // **The wettest third of the warm band.** The reference puts jungle at one
    // temperature level and the top two humidity levels of five; ours runs
    // drier overall, so the floor is the band left after the savanna rather
    // than the reference's raw number - a rank ported, not a value.
    {.name = "Jungle", .warmth = 0.95f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.88f,
     .treeShape = TreeShape::Jungle, .grassDensity = 0.34f, .flowerShare = 0.04f,
     .tags = BiomeTag::Jungle | BiomeTag::Forest | BiomeTag::Wet | BiomeTag::Hot,
     .temperature = {kTemperateT, kWarmT}, .humidity = {kDryH, 1.0f},
     .continentalness = {kCoastC, 1.0f}, .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .weirdness = {-1.0f, kJungleW}},

    // The variant branch, split from the plain jungle on the sign of weirdness
    // exactly as the reference splits it, and from each other on humidity.
    // **The sparse form is one of the emptiest biomes in the game** - two tree
    // attempts a chunk against the jungle's fifty - and that contrast is the
    // whole point of it existing.
    {.name = "Sparse Jungle", .warmth = 0.95f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.09f,
     .treeShape = TreeShape::Jungle, .grassDensity = 0.30f, .flowerShare = 0.04f,
     .tags = BiomeTag::Jungle | BiomeTag::Grassland | BiomeTag::Wet | BiomeTag::Hot,
     .temperature = {kTemperateT, kWarmT}, .humidity = {kDryH, kWetH},
     .continentalness = {kCoastC, 1.0f}, .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .weirdness = {kJungleW, 1.0f}},

    {.name = "Bamboo Jungle", .warmth = 0.95f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.62f,
     .treeShape = TreeShape::Jungle, .grassDensity = 0.28f, .flowerShare = 0.03f,
     .tags = BiomeTag::Jungle | BiomeTag::Forest | BiomeTag::Wet | BiomeTag::Hot,
     .temperature = {kTemperateT, kWarmT}, .humidity = {kWetH, 1.0f},
     .continentalness = {kCoastC, 1.0f}, .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .weirdness = {kJungleW, 1.0f}},

    {.name = "Badlands", .warmth = 2.0f,
     .top = BlockId::Terracotta, .filler = BlockId::Terracotta, .fillerDepth = 5,
     .patch = BlockId::Clay, .patchThreshold = 0.30f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Badlands | BiomeTag::Hot | BiomeTag::Dry | BiomeTag::Stony,
     .temperature = {kWarmT, 1.0f}, .humidity = {-1.0f, kAridH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Desert", .warmth = 2.0f,
     .top = BlockId::Sand, .filler = BlockId::Sand, .fillerDepth = 5,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Sandy | BiomeTag::Hot | BiomeTag::Dry,
     .temperature = {kWarmT, 1.0f}, .humidity = {kAridH, kWetH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Savanna", .warmth = 2.0f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.04f,
     .treeShape = TreeShape::Round, .grassDensity = 0.38f, .flowerShare = 0.03f,
     .tags = BiomeTag::Grassland | BiomeTag::Hot | BiomeTag::Dry,
     .temperature = {kTemperateT, kWarmT}, .humidity = {-1.0f, kDryH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Forest", .warmth = 0.7f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.45f,
     .treeShape = TreeShape::Round, .grassDensity = 0.34f, .flowerShare = 0.12f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Forest),
     .temperature = {kFrozenT, kWarmT}, .humidity = {kDryH, kWetH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Plains", .warmth = 0.8f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.10f,
     .treeShape = TreeShape::Round, .grassDensity = 0.32f, .flowerShare = 0.16f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Grassland),
     .temperature = {kFrozenT, kWarmT}, .humidity = {-1.0f, kDryH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},
}};

/// How far outside its box a value sits, or zero if it is inside.
float axisDistance(const ClimateRange& range, float value) {
    if (value < range.min) {
        return range.min - value;
    }
    if (value > range.max) {
        return value - range.max;
    }
    return 0.0f;
}

} // namespace

const Biome& biomeInfo(BiomeId id) {
    const auto index = static_cast<std::size_t>(id);
    return kBiomes[std::min(index, kBiomes.size() - 1)];
}

bool biomeHasTag(BiomeId id, BiomeTag tag) {
    return (biomeInfo(id).tags & static_cast<std::uint32_t>(tag)) != 0u;
}

bool biomeHasAny(BiomeId id, std::uint32_t mask) {
    return (biomeInfo(id).tags & mask) != 0u;
}

float maxTreeDensity() {
    // Derived rather than written down, so adding a leafier biome cannot
    // silently make the placement rejection wrong.
    static const float highest = [] {
        float best = 0.0f;
        for (const Biome& biome : kBiomes) {
            best = std::max(best, biome.treeDensity);
        }
        return best;
    }();
    return highest;
}

BiomeId biomeFor(const Climate& climate) {
    auto best = BiomeId::Plains;
    float bestDistance = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < kBiomes.size(); ++i) {
        const Biome& candidate = kBiomes[i];

        const float dt = axisDistance(candidate.temperature, climate.temperature);
        const float dh = axisDistance(candidate.humidity, climate.humidity);
        const float dc = axisDistance(candidate.continentalness, climate.continentalness);
        const float de = axisDistance(candidate.erosion, climate.erosion);
        const float dp = axisDistance(candidate.ridges, climate.ridges);
        const float dw = axisDistance(candidate.weirdness, climate.weirdness);

        const float distance = dt * dt + dh * dh + dc * dc + de * de + dp * dp + dw * dw;

        // Inside the box, so the first row that claims this point wins outright
        // and nothing later can beat it. This is the whole of the priority rule.
        if (distance <= 0.0f) {
            return static_cast<BiomeId>(i);
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<BiomeId>(i);
        }
    }

    // Nothing contained the point, so the nearest box takes it. The reference
    // does the same, and it is why a gap in the table is a soft edge rather
    // than a hole in the world.
    return best;
}

BiomeSample sampleBiome(std::uint32_t seed, int worldX, int worldZ) {
    BiomeSample sample{};
    sample.climate = climateAt(seed, worldX, worldZ);
    sample.shape = shapeAt(seed, sample.climate, worldX, worldZ);
    sample.dominant = biomeFor(sample.climate);
    return sample;
}

} // namespace game
