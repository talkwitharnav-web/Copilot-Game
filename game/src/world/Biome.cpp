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
     .erosion = kAny, .ridges = kAny,
     // frozen_ocean.biome.json, minecraft:climate -> snow_accumulation.
     .snow = {0.125f, 0.25f}},

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
     .erosion = kAny, .ridges = kAny,
     // cold_beach.biome.json, whose temperature 0.05 is the warmth above.
     .snow = {0.125f, 0.25f}},

    // The reference's stony shore really is mostly stone, with gravel in a
    // narrow window. It is a thin coastal strip rather than a region, so it
    // reads as a cliff foot rather than as a band painted across the land.
    {.name = "Stony Shore", .warmth = 0.2f,
     .top = BlockId::Stone, .filler = BlockId::Stone, .fillerDepth = 3,
     .patch = BlockId::Gravel, .patchThreshold = 0.0f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Beach | BiomeTag::Stony,
     .temperature = {kFrozenT, 1.0f}, .humidity = kAny, .continentalness = {kOceanC, kCoastC},
     .erosion = {-1.0f, kUplandE}, .ridges = kAny,
     // stone_beach.biome.json, whose temperature 0.2 is the warmth above.
     .snow = {0.0f, 0.25f}},

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
     .erosion = kAny, .ridges = {-1.0f, kValleyPV},
     // frozen_river.biome.json.
     .snow = {0.125f, 0.25f}},

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
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f},
     // frozen_peaks.biome.json, whose temperature -0.7 is the warmth above.
     .snow = {0.125f, 0.25f}},

    // `steep` is what makes this read as rock with snow on it. Without it a
    // peak is a solid white blob, which is exactly what the reference avoids.
    {.name = "Jagged Peaks", .warmth = -0.7f,
     .top = BlockId::Snow, .filler = BlockId::Stone, .fillerDepth = 2,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Stone, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Peak | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Stony,
     .temperature = {kFrozenT, kTemperateT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f},
     // jagged_peaks.biome.json.
     .snow = {0.125f, 0.25f}},

    {.name = "Stony Peaks", .warmth = 1.0f,
     .top = BlockId::Stone, .filler = BlockId::Stone, .fillerDepth = 2,
     .patch = BlockId::Andesite, .patchThreshold = 0.10f, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Peak | BiomeTag::Stony,
     .temperature = {kTemperateT, 1.0f}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kPeakPV, 1.0f}},

    {.name = "Snowy Slopes", .warmth = -0.3f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     // **Powder snow, and this is the whole of its natural generation.**
     // minecraft.wiki [[Powder Snow]]: "Powder snow naturally generates in
     // groves and snowy slopes in strip formations", and it is the block's only
     // non-cauldron source. `TerrainGenerator.cpp`'s surface-material step
     // already reads this
     // column - `top = biome.patch` wherever the surface noise clears the
     // threshold - so no generator change was needed and none was made.
     //
     // **These two rows complete the chain rather than starting it.** The
     // collision half landed while this was being written - `Block.hpp`
     // asserts `collisionBoxes(PowderSnow).count == 0` and `!isSolid` - and
     // `Player.cpp` already carries the freezing damage and the leather-boot
     // rule. Nothing placed the block, so all of it was unreachable; this is
     // the call site those features were missing.
     //
     // **0.30 is the one number here that is not primary-source.** Mojang
     // publishes no scatter density (`bedrock-samples` has no `blocks/`
     // directory and the biome JSONs do not carry surface rules), so it is
     // calibrated against this table's own ladder: 0.0 covers most of a
     // surface, 0.05 and 0.10 read as common, 0.12 as frequent, 0.45 as rare.
     // 0.30 puts powder snow between "often" and "rare", which is the reading
     // of "strips". **This is a feel number and belongs to the playtester** -
     // if a grove is a minefield, raise it; if you cannot find any, lower it.
     // The noise is blob-shaped rather than strip-shaped, so the formation is
     // an approximation of the reference and is not claimed as a match.
     //
     // **DELETING EITHER ROW REMOVES A DAMAGE SOURCE, WITH A GREEN BUILD.**
     // (Finding 9816, fx-vitals, 2026-08-19; premise re-verified in source
     // before this was written rather than transcribed.) `Player.cpp`'s
     // `inPowderSnow` tests body occupancy of a `BlockId::PowderSnow` cell, and
     // that is the *only* thing that raises `Player::freezeSeconds`, which is
     // the only thing that reaches `kFreezeOnsetSeconds` and deals freezing
     // damage. These two rows are the whole of the block's natural generation,
     // so **worldgen owns the reachability of a survival damage source** - a
     // coupling worldgen would never guess at.
     //
     // **Three edits switch it off and none of them is a deletion**, which is
     // why this says more than "do not delete": raising `.patchThreshold`
     // toward 1.0 makes the patch rarer, `kNoPatch` disables the row outright,
     // and setting `.patch` back to `BlockId::Air` disables it via the *first*
     // guard in `TerrainGenerator.cpp`. Nothing here is visible to a
     // `static_assert` - the coupling is between a float and another file's
     // behaviour - so **this sentence is the only instrument that exists**.
     //
     // If powder snow should stop generating, say so in the freezing block of
     // `Survival.hpp` in the same edit, so the next reader is not left hunting
     // a hazard that cannot happen. **Falsified by a search for
     // `BlockId::PowderSnow` in this file returning zero.**
     .patch = BlockId::PowderSnow, .patchThreshold = 0.30f,
     .steep = BlockId::Stone, .treeDensity = 0.01f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Highland | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kColdT}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kValleyPV, kPeakPV},
     // snowy_slopes.biome.json. Four layers, the second deepest row we have.
     .snow = {0.125f, 0.5f}},

    {.name = "Grove", .warmth = -0.2f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     // The second and last biome the reference generates powder snow in; see
     // the note on Snowy Slopes above for the source and for why 0.30.
     .patch = BlockId::PowderSnow, .patchThreshold = 0.30f,
     .steep = BlockId::Air, .treeDensity = 0.38f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.04f, .flowerShare = 0.0f,
     .tags = BiomeTag::Forest | BiomeTag::Highland | BiomeTag::Cold | BiomeTag::Snowy,
     .temperature = {kColdT, kWarmT}, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kHillPV, kPeakPV},
     // grove.biome.json, whose temperature -0.2 is the warmth above.
     .snow = {0.125f, 0.25f}},

    // **0.3 is Bedrock's number and 0.5 is Java's.** Confirmed against a primary
    // source on 2026-08-19: Mojang's own published behaviour pack states
    // `"minecraft:climate": { "temperature": 0.3 }` in
    // `behavior_pack/biomes/meadow.biome.json` of `Mojang/bedrock-samples`. The
    // wiki agrees - it tags this row `0.5 {{je}} / 0.3 {{be}}` - but the JSON is
    // the one that cannot be a Java table with a missing edition marker.
    // [https://minecraft.wiki/w/Biome#List_of_biome_climates]
    // At 0.5 a meadow never freezes anywhere in this world, because
    // `freezingHeight(0.5)` is y 107 and the terrain stops at 90 - so a meadow on
    // a mountain shoulder got rain where the reference gives it snow. The check
    // that settles it is `Snowfall`'s published per-biome snow line, which puts
    // Bedrock's meadow at y 200 +/- 8; through `weather::kWorldScale` that is our
    // 24 + (200 - 63) * 0.28 = y 62.4, and `freezingHeight(0.3)` = 29 + 0.15 /
    // 0.0044643 = y 62.6. [https://minecraft.wiki/w/Snowfall#Behavior]
    //
    // **The whole column was checked against that pack, not just this row**:
    // 29 of our 30 `warmth` values matched Mojang's `minecraft:climate`
    // temperature exactly, and the one that did not was Savanna, fixed below.
    {.name = "Meadow", .warmth = 0.3f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.04f,
     .treeShape = TreeShape::Round, .grassDensity = 0.34f, .flowerShare = 0.55f,
     .beeNestChance = 1.0f,
     .tags = BiomeTag::Grassland | BiomeTag::Highland,
     .temperature = {kColdT, 1.0f}, .humidity = kAny, .continentalness = {kCoastC, 1.0f},
     .erosion = {-1.0f, kUplandE}, .ridges = {kValleyPV, kPeakPV},
     .villageType = VillageType::Plains},

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
     .erosion = {kWindsweptLowE, kWindsweptHighE}, .ridges = {kValleyPV, 1.0f},
     // extreme_hills.biome.json, whose temperature 0.2 is the warmth above.
     .snow = {0.0f, 0.25f}},

    {.name = "Gravelly Hills", .warmth = 0.2f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 3,
     .patch = BlockId::Gravel, .patchThreshold = 0.05f, .steep = BlockId::Air, .treeDensity = 0.02f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.15f, .flowerShare = 0.02f,
     .tags = BiomeTag::Highland | BiomeTag::Stony,
     .temperature = kAny, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kWindsweptLowE, kWindsweptHighE}, .ridges = {kValleyPV, 1.0f},
     // **extreme_hills_mutated, not extreme_hills** - this row is the
     // reference's windswept *gravelly* hills, and the two files really do
     // differ here: [0.0, 0.125] against [0.0, 0.25]. Copying the plain row's
     // pair across would have made the gravelly variant twice as deep as
     // Mojang has it, which is the sort of thing nothing would ever catch.
     .snow = {0.0f, 0.125f}},

    {.name = "Snowy Taiga", .warmth = -0.5f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.42f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.18f, .flowerShare = 0.0f,
     .tags = BiomeTag::Forest | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kFrozenT}, .humidity = {kDampH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     // **`Taiga`, not `Snowy`, and this is stated in Mojang's own data rather
     // than inferred.** `Mojang/bedrock-samples`,
     // `behavior_pack/biomes/cold_taiga.biome.json` - cold_taiga *is* snowy
     // taiga - carries `"minecraft:village_type": {"type": "taiga"}` as a
     // top-level component (a sibling of `minecraft:overworld_generation_rules`,
     // not nested inside it, which is where a reader looks first and does not
     // find it). `cold_taiga_hills` carries the same. **`ice_plains` is the
     // only biome in the whole pack carrying `"ice"`**, which is what makes
     // this a one-for-one mapping rather than a judgement call. Fetched and
     // read at source 2026-08-19; the same file's `"temperature": -0.5` is the
     // `warmth` two lines above, so this row is a faithful port of that JSON
     // and this field was the one thing in it that had drifted.
     //
     // A snowy taiga therefore gets the spruce taiga set with snow laid over
     // it by the ordinary surface rules, not the white `ice` architecture.
     // `Village.cpp` needs no change: it already builds whichever type it is
     // handed.
     .villageType = VillageType::Taiga,
     // cold_taiga.biome.json, the same file the village type above comes from.
     .snow = {0.125f, 0.5f}},

    {.name = "Snowy Plains", .warmth = 0.0f,
     .top = BlockId::Snow, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.01f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.14f, .flowerShare = 0.0f,
     .tags = BiomeTag::Grassland | BiomeTag::Cold | BiomeTag::Snowy | BiomeTag::Frozen,
     .temperature = {-1.0f, kFrozenT}, .humidity = {-1.0f, kDampH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .villageType = VillageType::Snowy,
     // **The deepest row in the table**, from ice_plains.biome.json - and its
     // maximum of a whole block is exactly eight layers, which is the last
     // depth a single cell can hold. `Biome.cpp`'s `deepestSnowLayers` assert
     // is what stops the next cold biome quietly needing the cell above.
     .snow = {0.25f, 1.0f}},

    {.name = "Taiga", .warmth = 0.25f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.50f,
     .treeShape = TreeShape::Tall, .grassDensity = 0.26f, .flowerShare = 0.04f,
     .tags = BiomeTag::Forest | BiomeTag::Cold,
     .temperature = {kFrozenT, kColdT}, .humidity = {kDryH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .villageType = VillageType::Taiga,
     // taiga.biome.json, whose temperature 0.25 is the warmth above.
     .snow = {0.0f, 0.25f}},

    {.name = "Dense Forest", .warmth = 0.7f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.80f,
     .treeShape = TreeShape::Round, .grassDensity = 0.30f, .flowerShare = 0.06f,
     .beeNestChance = 0.00035f,
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

    // **The whole hot half of the humidity axis that badlands does not take.**
    // It used to stop at `kWetH`, which left `T > kWarmT` and `H > kWetH`
    // claimed by nothing at all between erosion -0.375 and 0.45: swamp is the
    // only other row that hot and that wet and it needs E >= 0.55, and the
    // three jungles stop at kWarmT. The nearest-box fallback then drew a
    // desert/bamboo-jungle line at H = 0.55 - a number that appears in no row
    // of this table, and the reference has desert at *every* humidity in its
    // hottest slice. `tableCoversClimateSpace` below is now a build error if
    // this hole ever comes back.
    {.name = "Desert", .warmth = 2.0f,
     .top = BlockId::Sand, .filler = BlockId::Sand, .fillerDepth = 5,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.0f,
     .treeShape = TreeShape::None, .grassDensity = 0.0f, .flowerShare = 0.0f,
     .tags = BiomeTag::Sandy | BiomeTag::Hot | BiomeTag::Dry,
     .temperature = {kWarmT, 1.0f}, .humidity = {kAridH, 1.0f}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .villageType = VillageType::Desert},

    /// `.warmth` is Bedrock's 1.2 from Mojang's own published behaviour pack,
    /// `biomes/savanna.biome.json` in `Mojang/bedrock-samples`, read 2026-08-19.
    /// **Java's savanna is 2.0 and that is what stood here**, the same mistake
    /// the meadow row carried until this session. A primary source settles it:
    /// the wiki marks this split inconsistently, the JSON simply states the
    /// number, and `savanna_mutated` - windswept savanna, which we do not have -
    /// is the row that really is 2.0.
    ///
    /// **Behaviour-neutral today, and that was checked rather than assumed.**
    /// `warmth` is read only through `freezesAt`, and `Biome.hpp` records that
    /// nothing above about 0.42 can freeze anywhere in a 96-block world, so 2.0
    /// and 1.2 give the identical answer everywhere; `precipitationFor` never
    /// reaches the number at all, because `Dry` below returns `None` first. The
    /// check is not vacuous - run on the meadow row it *did* fire, because 0.5
    /// put its freezing line at y 107 against a `kMaxSurface` of 90 and 0.3
    /// brings it back to y 62.6. Fixed anyway: the next thing to read `warmth`
    /// (snow accumulation, grass tint, spawn rules) would inherit a Java value.
    {.name = "Savanna", .warmth = 1.2f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.04f,
     .treeShape = TreeShape::Acacia, .grassDensity = 0.38f, .flowerShare = 0.03f,
     .tags = BiomeTag::Grassland | BiomeTag::Hot | BiomeTag::Dry,
     .temperature = {kTemperateT, kWarmT}, .humidity = {-1.0f, kDryH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .villageType = VillageType::Savanna},

    {.name = "Forest", .warmth = 0.7f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.45f,
     .treeShape = TreeShape::Round, .grassDensity = 0.34f, .flowerShare = 0.12f,
     .beeNestChance = 0.00035f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Forest),
     .temperature = {kFrozenT, kWarmT}, .humidity = {kDryH, kWetH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f}},

    {.name = "Plains", .warmth = 0.8f,
     .top = BlockId::Grass, .filler = BlockId::Dirt, .fillerDepth = 4,
     .patch = BlockId::Air, .patchThreshold = kNoPatch, .steep = BlockId::Air, .treeDensity = 0.10f,
     .treeShape = TreeShape::Round, .grassDensity = 0.32f, .flowerShare = 0.16f,
     .beeNestChance = 0.05f,
     .tags = static_cast<std::uint32_t>(BiomeTag::Grassland),
     .temperature = {kFrozenT, kWarmT}, .humidity = {-1.0f, kDryH}, .continentalness = {kCoastC, 1.0f},
     .erosion = {kUplandE, 1.0f}, .ridges = {kValleyPV, 1.0f},
     .villageType = VillageType::Plains,
     // plains.biome.json. **Unreachable and kept anyway**: at warmth 0.8 a
     // plain never freezes anywhere in this world, so this pair can never be
     // consulted. It is here because a blank would be indistinguishable from
     // not having read the file, and because this row is the one a future
     // Sunflower Plains or Flower Forest gets copied from.
     .snow = {0.0f, 0.125f}},
}};

/// How far outside its box a value sits, or zero if it is inside.
///
/// **`constexpr` so the coverage proof below and `biomeFor` share one notion of
/// "inside a box".** A second copy of that rule, even a two-line one, is how a
/// proof ends up proving something the shipping code does not do.
constexpr float axisDistance(const ClimateRange& range, float value) {
    if (value < range.min) {
        return range.min - value;
    }
    if (value > range.max) {
        return value - range.max;
    }
    return 0.0f;
}

/// Every value at which some row cuts an axis, ends included.
///
/// **Walking the midpoint of every cell of the grid these lines induce is a
/// proof, not a sample.** Every box in the table has its edges among these
/// lines, so a box either covers a whole cell or misses it entirely - there is
/// nowhere for a gap narrower than a cell to hide, and a midpoint can never
/// land on a boundary and be counted by luck. Adding a row with a new edge
/// means adding that edge here, or the proof stops covering what it claims to.
constexpr std::array<float, 6> kTemperatureEdges{-1.0f, kFrozenT, kColdT, kTemperateT, kWarmT, 1.0f};
constexpr std::array<float, 6> kHumidityEdges{-1.0f, kAridH, kDryH, kDampH, kWetH, 1.0f};
constexpr std::array<float, 5> kContinentalEdges{-1.0f, kDeepOceanC, kOceanC, kCoastC, 1.0f};
constexpr std::array<float, 5> kErosionEdges{-1.0f, kUplandE, kWindsweptLowE, kWindsweptHighE, 1.0f};
constexpr std::array<float, 5> kRidgeEdges{-1.0f, kValleyPV, kHillPV, kPeakPV, 1.0f};
constexpr std::array<float, 3> kWeirdnessEdges{-1.0f, kJungleW, 1.0f};

/// True if some row's box contains this point outright - which is the same test
/// `biomeFor` makes before it gives up and takes the nearest box instead.
constexpr bool someBoxContains(float t, float h, float c, float e, float p, float w) {
    for (const Biome& biome : kBiomes) {
        if (axisDistance(biome.temperature, t) <= 0.0f &&
            axisDistance(biome.humidity, h) <= 0.0f &&
            axisDistance(biome.continentalness, c) <= 0.0f &&
            axisDistance(biome.erosion, e) <= 0.0f && axisDistance(biome.ridges, p) <= 0.0f &&
            axisDistance(biome.weirdness, w) <= 0.0f) {
            return true;
        }
    }
    return false;
}

/// Whether the table tiles the whole of climate space.
///
/// **This is worth more than any one hole it finds.** The nearest-box fallback
/// cannot fail loudly: where the table has a gap it quietly draws a boundary at
/// whichever distance metric happens to win, and that boundary appears in no
/// row of the table and in no document. It took a reviewer walking all thirty
/// rows axis by axis to find the last one. Now it is a build error.
constexpr bool tableCoversClimateSpace() {
    const auto mid = [](float a, float b) { return (a + b) * 0.5f; };

    for (std::size_t ti = 0; ti + 1 < kTemperatureEdges.size(); ++ti) {
        for (std::size_t hi = 0; hi + 1 < kHumidityEdges.size(); ++hi) {
            for (std::size_t ci = 0; ci + 1 < kContinentalEdges.size(); ++ci) {
                for (std::size_t ei = 0; ei + 1 < kErosionEdges.size(); ++ei) {
                    for (std::size_t pi = 0; pi + 1 < kRidgeEdges.size(); ++pi) {
                        for (std::size_t wi = 0; wi + 1 < kWeirdnessEdges.size(); ++wi) {
                            if (!someBoxContains(mid(kTemperatureEdges[ti], kTemperatureEdges[ti + 1]),
                                                 mid(kHumidityEdges[hi], kHumidityEdges[hi + 1]),
                                                 mid(kContinentalEdges[ci], kContinentalEdges[ci + 1]),
                                                 mid(kErosionEdges[ei], kErosionEdges[ei + 1]),
                                                 mid(kRidgeEdges[pi], kRidgeEdges[pi + 1]),
                                                 mid(kWeirdnessEdges[wi], kWeirdnessEdges[wi + 1]))) {
                                return false;
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

// Narrowing `Desert`'s humidity back to `{kAridH, kWetH}` is the single edit
// that makes this fail, and it is the edit that was in the table until
// 2026-08-18.
//
// **Counting the asserts in this file: search `^\s*static_assert`, not
// `static_assert`.** Two hits in this file are quoted inside doc comments,
// prose about asserts rather than asserts, and a raw count therefore shifts
// every index. Finding 9753 (2026-08-19) is the precedent: an auditor counted a
// quoted assert in `Loot.hpp` as real, which moved the one genuinely coupled
// assert to the wrong position, and the coupling was declared absent for an
// hour. No count is written here on purpose - re-run the search.
static_assert(tableCoversClimateSpace(),
              "A climate the biome table does not claim falls to the nearest-box metric, which "
              "invents a boundary no row of the table describes.");

/// **A missing row is not a compile error**, which is the trap waiting for the
/// 24 biomes queued in `GAPS.md` G5.10. `kBiomes` is sized off `BiomeId::Count`,
/// so adding an enumerator and forgetting its row leaves aggregate
/// initialisation to value-initialise the remainder: a biome whose `name` is
/// null and whose six climate boxes are all {0, 0}. `tableCoversClimateSpace`
/// above still passes - a zero-width box claims nothing and the surviving rows
/// still cover the space - so the biome simply never generates, and the first
/// thing to print its name dereferences null. MSVC's C4062 is off at /W4, so no
/// `switch` on `BiomeId` warns about it either.
constexpr bool everyRowIsFilledIn() {
    for (const Biome& row : kBiomes) {
        if (row.name == nullptr || row.name[0] == '\0') {
            return false;
        }
    }
    return true;
}
static_assert(everyRowIsFilledIn(),
              "A BiomeId enumerator has no row in kBiomes. Aggregate initialisation filled it "
              "with zeroes instead of failing, so the biome exists, never generates, and "
              "crashes whatever prints its name.");

/// The negative half, because an assert that cannot fail proves nothing: a
/// value-initialised row is exactly what a missing one looks like, and it has to
/// fail the predicate the assert above runs.
static_assert(Biome{}.name == nullptr,
              "A missing kBiomes row would be indistinguishable from a present one by its name, "
              "so everyRowIsFilledIn proves nothing.");

/// The snow column, checked three ways.
///
/// **A table of ported numbers is exactly where a wrong row looks identical to
/// a right one**, and there is no compiler warning for a pair that was never
/// filled in. These are what stand in for reading thirty rows again.
constexpr int deepestSnowLayers() {
    int deepest = 0;
    for (const Biome& row : kBiomes) {
        const int layers = snowLayersFrom(row.snow.maxBlocks);
        deepest = layers > deepest ? layers : deepest;
    }
    return deepest;
}

constexpr int rowsThatAccumulateSnow() {
    int rows = 0;
    for (const Biome& row : kBiomes) {
        rows += row.snow.maxBlocks > 0.0f ? 1 : 0;
    }
    return rows;
}

constexpr bool snowPairsAreOrdered() {
    for (const Biome& row : kBiomes) {
        if (row.snow.minBlocks < 0.0f || row.snow.maxBlocks < row.snow.minBlocks) {
            return false;
        }
    }
    return true;
}

/// The leafiest row's `treeDensity`, so the tree pass's early rejection is
/// derived from the table rather than written down beside it. Read by
/// `maxTreeDensity` below, which is what `Structures.cpp` calls.
constexpr float highestTreeDensity() {
    float best = 0.0f;
    for (const Biome& row : kBiomes) {
        best = row.treeDensity > best ? row.treeDensity : best;
    }
    return best;
}
constexpr float kMaxTreeDensity = highestTreeDensity();

// A zero here would silently *accept* every cell in the world rather than
// reject one, because the test in `Structures.cpp` is
// `presence >= maxTreeDensity()` against a unit hash - so the early-out that
// exists to answer "most cells are empty" in one hash would answer "yes" every
// time and the pass would sample the biome and the surface for all of them.
// The control is that a value-initialised table is exactly what this rejects.
static_assert(kMaxTreeDensity > 0.0f,
              "no kBiomes row has any tree density at all, which turns the tree pass's early "
              "rejection into an accept-everything rather than into a treeless world");

// **One cell, and this is the assert that says so.** `World.cpp` writes a
// drift into a single block position, and `snowLayerAt` runs out at eight -
// deeper than that needs the cell above and a rule for stacking, which does not
// exist. No row we have asks for it: Snowy Plains' `ice_plains` maximum of 1.0
// blocks is exactly eight. The reference *does* go further - `ice_mountains`
// and `ice_plains_spikes` both publish 1.5 blocks, twelve layers - so this
// fails the day either of those biomes is added, which is precisely when
// somebody has to write the stacking rather than discover a silently clamped
// drift. The `> 0` half is the control: it is not vacuous on both sides, and it
// fails if the whole column is ever value-initialised away.
static_assert(deepestSnowLayers() > 0 && deepestSnowLayers() <= kSnowLayersPerBlock,
              "settled snow deeper than one block needs the cell above it, which World.cpp does "
              "not write; ice_mountains and ice_plains_spikes are the two reference biomes that "
              "would trip this");

// The fourteen rows read out of `Mojang/bedrock-samples` on 2026-08-19, in
// table order: Frozen Ocean, Snowy Beach, Stony Shore, Frozen River, Frozen
// Peaks, Jagged Peaks, Snowy Slopes, Grove, Windswept Hills, Gravelly Hills,
// Snowy Taiga, Snowy Plains, Taiga, Plains. Every other row is blank for a
// reason `Biome.hpp` gives against the field, and two of those blanks -
// **Meadow and Stony Peaks - are measured absences rather than unread files**:
// neither `meadow.biome.json` nor `stony_peaks.biome.json` carries a
// `snow_accumulation` key at all, and Meadow at warmth 0.3 is cold enough for
// that to be a real answer. This count is what fails if a fifteenth row appears
// without the list above being updated, or if one of the fourteen is lost.
static_assert(rowsThatAccumulateSnow() == 14,
              "a row gained or lost its snow_accumulation pair; the list of which rows carry one "
              "is in the comment above this assert and needs updating with it");

// Ordering, which is the one thing a transposed pair would show as - and the
// accumulation step draws uniformly across the range, so a reversed pair would
// silently draw nothing rather than fail.
static_assert(snowPairsAreOrdered(),
              "a snow_accumulation pair runs [min, max] and neither end may be negative");

/// The bee-nest column, and where its numbers came from.
///
/// **Read off the reference's own table, not reproduced from memory.**
/// minecraft.wiki [[Bee Nest]] carries a per-biome table with separate Java and
/// Bedrock columns; it is stripped by every markdown conversion of the page, so
/// it was pulled as **raw wikitext** through the wiki's own API
/// (`action=parse&prop=wikitext`) on 2026-08-19. The **Bedrock** column reads:
/// meadow 100%; plains, sunflower plains and cherry grove 5%; mangrove swamp
/// 1%; flower forest 3% (Java 2%); and forest, birch forest, old growth birch
/// forest, wooded hills, birch forest hills and tall birch hills 0.035% (Java
/// 0.2%). The footnote on the column says it is "the chance for each
/// naturally-generated oak, birch, mangrove tree, or cherry tree to have a bee
/// nest", which is where the oak-family gate in `Structures.cpp`'s `treeInCell`
/// comes from.
///
/// **Corroborated a second time on the same page and independently of the
/// table**, by the Bedrock history section: beta 1.16.0.57 gives flower forest
/// 3%, plains and sunflower plains 5%, and "forest, wooded hills, birch forest,
/// tall birch forest, birch forest hills, and tall birch hills" 0.035%. Two
/// readings, one of them a changelog, agreeing on the three numbers that
/// matter here. Meadow's 100% is table-only - it spans both editions in one
/// cell - and is the one row to re-check first if the table is ever re-read.
///
/// **`Mojang/bedrock-samples` cannot settle it and it was asked**: worldgen
/// chances are engine-side, and its `behavior_pack/` has no `blocks/` directory
/// at all. Searching that repo for `bee_nest` returns textures, `blocks.json`
/// and `entities/bee.json`, and nothing about generation.
///
/// Our biome list has no flower forest, cherry grove, sunflower plains or
/// mangrove swamp, and birch is a *variant roll inside oak* rather than a
/// biome, so **four rows carry a rate and there are three distinct rates**:
/// Meadow 1.0, Plains 0.05, and Forest and Dense Forest sharing 0.00035.
/// `Dense Forest` reads the forest rate, and that is safe from either
/// direction: every Bedrock biome in the 0.035% row is a forest of some kind,
/// so whichever of them it stands for, the number is the same.
///
/// **CONTROL, and it is the mistake this table invites.** Java's column sits
/// beside Bedrock's on the same wiki page, one cell to the left, and its forest
/// rate is 0.2% - nearly six times ours. Reading the wrong column produces a
/// table that looks exactly as deliberate as the right one and puts roughly six
/// times as many nests in every forest in the world. This is deliberately still
/// a `switch` rather than a second column: it is not data the game may read, it
/// is the wrong answer kept alive so the assert below can tell the two apart.
constexpr float beeNestChanceJava(BiomeId biome) {
    switch (biome) {
    case BiomeId::Meadow:
        return 1.0f;
    case BiomeId::Plains:
        return 0.05f;
    case BiomeId::Forest:
    case BiomeId::DenseForest:
        return 0.002f;
    default:
        return 0.0f;
    }
}

/// The column as the rows actually hold it, so the asserts below test the
/// **table** rather than a restatement of it.
constexpr float rowBeeNestChance(BiomeId id) {
    return kBiomes[static_cast<std::size_t>(id)].beeNestChance;
}

static_assert(rowBeeNestChance(BiomeId::Forest) != beeNestChanceJava(BiomeId::Forest) &&
                  rowBeeNestChance(BiomeId::Meadow) == beeNestChanceJava(BiomeId::Meadow),
              "the forest rate must be Bedrock's 0.035%, not Java's 0.2% - and the two editions "
              "genuinely agree on meadow, so a control that differed everywhere would be "
              "proving nothing about which column was read");

// The four rows that carry a rate, listed rather than counted, because the set
// is small and a count is what got this wrong before: finding 9702 recorded a
// comment in `Structures.cpp` saying "three nest-bearing biomes" three times,
// which came from three `return` statements in the switch this column replaced
// while four enumerators reached them. All four carry `TreeShape::Round`, which
// is what makes the oak-family gate in `treeInCell` safe without a species
// test - checked against the table on 2026-08-19, not assumed.
static_assert(rowBeeNestChance(BiomeId::Meadow) == 1.0f &&
                  rowBeeNestChance(BiomeId::Plains) == 0.05f &&
                  rowBeeNestChance(BiomeId::Forest) == 0.00035f &&
                  rowBeeNestChance(BiomeId::DenseForest) == 0.00035f,
              "a bee-nest rate moved off the row it belongs to; the four rows that carry one are "
              "named in the comment above this assert and need updating with it");

// The negative half. Zero is what a row that was never filled in also reads,
// so this on its own would be vacuous - it is the assert above that makes these
// four mean "the reference gives them none" rather than "nobody wrote them".
static_assert(rowBeeNestChance(BiomeId::Desert) == 0.0f &&
                  rowBeeNestChance(BiomeId::Jungle) == 0.0f &&
                  rowBeeNestChance(BiomeId::Swamp) == 0.0f &&
                  rowBeeNestChance(BiomeId::Taiga) == 0.0f,
              "a biome the reference gives no nest chance must get none here - the default on the "
              "column is the whole of that rule and this is what stops a row being given one");

/// A `constexpr` text compare. `std::strcmp` is not usable in a constant
/// expression and this is the only place in the file that needs one.
///
/// **COMPILE-TIME ONLY, AND THAT IS THE SPECIFICATION, NOT AN OVERSIGHT.** This
/// and `rowIs` below are read by nothing but the `static_assert` that follows
/// them; so are `deepestSnowLayers`, `rowsThatAccumulateSnow` and
/// `snowPairsAreOrdered` above. A sweep for callers will report all five as
/// dead code and it will be wrong five times - a tripwire whose whole job is to
/// stop a build has no runtime caller by construction. **Do not delete them for
/// having none, and do not "wire them up" to give them one.**
constexpr bool sameText(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/// Whether the row an enumerator addresses is the row it is named after.
constexpr bool rowIs(BiomeId id, const char* name) {
    const auto index = static_cast<std::size_t>(id);
    return index < kBiomes.size() && kBiomes[index].name != nullptr &&
           sameText(kBiomes[index].name, name);
}

// **The table is addressed by index and consumed by name, and this is the only
// thing binding the two.**
//
// `biomeFor` returns `static_cast<BiomeId>(i)` where `i` is a row index, so row
// order *is* the enumeration - and nothing above proves it. `everyRowIsFilledIn`
// proves no row is blank; `tableCoversClimateSpace` proves the boxes tile. Both
// pass just as happily with the rows shuffled.
//
// The consumers stopped being local, which is what turns this from tidiness
// into a real trap: `Structures.cpp` switches over `BiomeId` names to decide
// where bee nests go, and village type is looked up the same way. Insert one row
// in the middle of `kBiomes` - which is exactly what a queued batch of new
// biomes does - and every `BiomeId::X` silently begins naming its neighbour.
// There is no compile error, because the cast cannot fail and C4062 is off at
// `/W4`, and no assert fires. The symptom is bee nests and villages in the wrong
// biome, which reads as worldgen tuning rather than as a bug.
//
// Every row rather than a handful of anchors, because an anchor set is exactly
// as good as the guess about which rows will move, and a row inserted between
// two anchors shifts everything after it while both anchors still pass. Yes,
// this is a second copy of thirty names - deliberately, and as a tripwire
// rather than as a derivation: it is not read at runtime, nothing computes an
// answer from it, and its whole job is to stop compiling when the two lists it
// compares stop agreeing.
static_assert(
    rowIs(BiomeId::FrozenOcean, "Frozen Ocean") && rowIs(BiomeId::WarmOcean, "Warm Ocean") &&
        rowIs(BiomeId::DeepOcean, "Deep Ocean") && rowIs(BiomeId::Ocean, "Ocean") &&
        rowIs(BiomeId::SnowyBeach, "Snowy Beach") && rowIs(BiomeId::StonyShore, "Stony Shore") &&
        rowIs(BiomeId::Beach, "Beach") && rowIs(BiomeId::FrozenRiver, "Frozen River") &&
        rowIs(BiomeId::River, "River") && rowIs(BiomeId::FrozenPeaks, "Frozen Peaks") &&
        rowIs(BiomeId::JaggedPeaks, "Jagged Peaks") && rowIs(BiomeId::StonyPeaks, "Stony Peaks") &&
        rowIs(BiomeId::SnowySlopes, "Snowy Slopes") && rowIs(BiomeId::Grove, "Grove") &&
        rowIs(BiomeId::Meadow, "Meadow") && rowIs(BiomeId::WindsweptHills, "Windswept Hills") &&
        rowIs(BiomeId::GravellyHills, "Gravelly Hills") && rowIs(BiomeId::SnowyTaiga, "Snowy Taiga") &&
        rowIs(BiomeId::SnowyPlains, "Snowy Plains") && rowIs(BiomeId::Taiga, "Taiga") &&
        rowIs(BiomeId::DenseForest, "Dense Forest") && rowIs(BiomeId::Swamp, "Swamp") &&
        rowIs(BiomeId::Jungle, "Jungle") && rowIs(BiomeId::SparseJungle, "Sparse Jungle") &&
        rowIs(BiomeId::BambooJungle, "Bamboo Jungle") && rowIs(BiomeId::Badlands, "Badlands") &&
        rowIs(BiomeId::Desert, "Desert") && rowIs(BiomeId::Savanna, "Savanna") &&
        rowIs(BiomeId::Forest, "Forest") && rowIs(BiomeId::Plains, "Plains"),
    "a kBiomes row no longer matches the BiomeId that indexes it - a row was inserted, removed or "
    "moved without the enum moving with it, and every switch over BiomeId in the tree is now "
    "naming the wrong biome");

// The control, and it is the whole reason the assert above can be believed:
// `rowIs` has to be capable of returning false. Both halves are non-vacuous -
// the first is a real row compared against a wrong name, the second is a
// one-character difference, which is the failure an inserted row actually
// produces at the boundary.
static_assert(!rowIs(BiomeId::Plains, "Forest") && !rowIs(BiomeId::Plains, "Plain"),
              "rowIs must be able to say no, or the thirty clauses above prove nothing");

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

/// **Bee nests are placed, and this is the biome data behind them.**
///
/// **This header said the opposite until 2026-08-19 09:xx and every word of it
/// was correct when written.** It opened *"Nothing anywhere places a bee nest,
/// and this is the table that would"*, and argued at length that three things
/// blocked it, none of them in this file. All three fell within four hours of
/// it being written: `Block.hpp` gained 24 `BeeNestRunFirst..BeeNestRunLast`
/// ids (six honey levels x four facings, level-major) at 06:00, `Structures.cpp`
/// gained the placement, and `Creature.cpp` gained the pollination behaviours.
/// The paragraph was then a confident, well-cited, entirely false statement
/// that the thing a reader needed did not exist - and the likely response to
/// one of those is to stop rather than to grep. **Negative claims rot fastest,
/// because the world only has to move once**, so date them and say what would
/// falsify them. Kept below is only what is still true.
///
/// **Two sources, and they cover different halves of the question.** Mojang's
/// own published Bedrock behaviour pack (`Mojang/bedrock-samples`) is primary
/// and settles *which* biomes host bees: ten of its `biomes/*.biome.json` carry
/// the tag `"bee_habitat"`, read 2026-08-19 -
///   `birch_forest`, `birch_forest_hills`, `birch_forest_mutated`,
///   `cherry_grove`, `flower_forest`, `forest`, `forest_hills`, `meadow`,
///   `plains`, `sunflower_plains`.
/// It does **not** settle the rates: that repository publishes no `features/`
/// directory at all, so the worldgen probabilities are not in any primary source
/// available to us and the wiki is all there is for them. Say which half a
/// number came from when you use it.
///
/// Two things the tag list disagrees with the wiki about, recorded rather than
/// resolved: the wiki's table includes **Mangrove Swamp**, which carries no
/// `bee_habitat` tag (and whose own rate the wiki self-contradicts, 1% on one
/// page against 4-5% on another); and it lists **Tall Birch Hills** `[BE only]`,
/// where `birch_forest_hills_mutated` carries no tag either. We have neither
/// biome, so neither affects us - but do not port those two rows without
/// checking. `pale_garden` carries no tag, which agrees with the wiki.
///
/// The rates, from minecraft.wiki `Bee Nest -> Obtaining -> Natural generation`,
/// reproduced with its own edition markers. The footnote is the unit: *"The
/// chance for each naturally-generated oak, birch, mangrove tree, or cherry tree
/// to have a bee nest"* - **per tree, not per chunk**:
///
///   | biome                                    | {{JE}}  | {{BE}}   |
///   | Meadow                                   | 100%    | 100%     |
///   | Plains / Sunflower Plains / Cherry Grove | 5%      | 5%       |
///   | Mangrove Swamp                           | 1%      | 1% *     |
///   | Flower Forest                            | 2%      | **3%**   |
///   | Forest / Birch / Old Growth Birch        | 0.2%    | **0.035%**|
///
/// **Bedrock is nearly six times rarer than Java in forests**, so 0.2% written
/// from memory would be wrong by that factor. (* Mangrove is the contested row
/// noted above; we have no mangrove swamp, so it does not arise.)
///
/// **Of that list we have three rows: Meadow, Plains and Forest.** Flower
/// Forest, Sunflower Plains, Cherry Grove, Birch Forest and Old Growth Birch are
/// all absent (`GAPS.md` G5.10), and Forest at 0.035% is one nest per 2857
/// trees, which is nothing. Tree *type* is not a blocker: all three are
/// `TreeShape::Round`, which `Structures.cpp` builds as oak or branching oak,
/// and the reference names oak and fancy oak explicitly.
///
/// **Measured, because the arithmetic off this table is wrong.** Multiplying
/// `treeDensity` by biome frequency predicts meadows supply 81% of all nests. A
/// census of real trunks over 2048x2048 blocks says otherwise: Meadow yields
/// **0.44 trunks per 1000 columns against Plains' 1.95**, a ratio of 0.23 where
/// the two `treeDensity` values alone imply 0.40. Meadow is `Highland`, and
/// `treeInCell`'s steep-neighbour rejection takes roughly half its trees. With
/// the global biome mix the split is Meadow 71%, Plains 28%, Forest 0.4%; over
/// that local patch it was Plains 89%. Either way **both are needed** - meadows
/// alone are too thin to be the supply.
///
/// **The nest and the hive are two blocks, and that is the part of the old
/// argument worth keeping.** It is primary-source confirmed rather than
/// wiki-only: `metadata/vanilladata_modules/mojang-blocks.json` in
/// `Mojang/bedrock-samples` publishes `bee_nest` at `raw_id` 473 and `beehive`
/// at 474 as two separate entries, each carrying exactly `direction` and
/// `honey_level`. The wiki supplies the rest, and is the only source for it
/// because **`behavior_pack/` has no `blocks/` directory** - Bedrock block
/// behaviour is engine-side and unpublished: different textures, hardness 0.3
/// vs 0.6, flammability 30 vs 5, and a nest **drops nothing without Silk
/// Touch** while a hive always drops itself.
///
/// **So do not place `BlockId::Beehive` on a wild tree** - it would show the
/// crafted texture in the wild and hand the player a free beehive off any
/// meadow oak, bypassing the honeycomb gate the whole chain exists to create.
/// The reason has changed and the conclusion has not: it is wrong now because
/// `beeNestAtLevel(facing, level)` is the right id for a generated home, not
/// because no such id exists. Both blocks carry `direction`, so a placed nest
/// needs a facing rather than being orientation-free, and `honey_level` runs
/// `[0,1,2,3,4,5]` on **both** of them - raised **+1 per pollinated bee
/// leaving, with a 1% chance of +2** (the rate is wiki-only; the field is
/// primary-source). Stated precisely, because a published value list is a
/// *storage domain* and not each block's range: two users means Mojang proves
/// the field is 0-5 and is shared by exactly these two blocks, not that each
/// one reaches 5. The wiki supplies that, via shearing at level 5 for 3
/// honeycomb. One user would have been authoritative; `moisturized_amount` on
/// `farmland` is the shape that is.
///
/// A naturally generated nest holds **2-3 bees, not 3** - stated on the `Bee
/// Nest`, `Bee`, `Oak`, `Birch` and `Cherry` pages with no edition marker on
/// any of them. 3 is the *capacity* (`Beehive/Usage -> Bee housing`), not the
/// spawn count.
///
/// **The rate itself now lives where this comment argued it should**, as of
/// 2026-08-19: `Biome::beeNestChance`, filled on four rows, with the source
/// note and the Java control beside `rowBeeNestChance` above. It spent part of
/// one day as a pair of switches in `Structures.cpp` - findings 9620 and 9685 -
/// and the paragraph describing that arrangement has gone with it, on its own
/// instruction, because a negative aged past its fix reads as a defect report.
float maxTreeDensity() {
    // Derived rather than written down, so adding a leafier biome cannot
    // silently make the placement rejection wrong.
    //
    // **`constexpr`, and that is not tidiness.** `Structures.cpp` asks this
    // once per tree placement cell, which is the hottest early-out in the tree
    // pass. A function-local `static` answers the same number, but every one of
    // those calls pays the thread-safe-initialisation guard - a real atomic, on
    // a value that is a pure function of a `constexpr` table and could never
    // have differed between runs, let alone between threads. It was written
    // that way until 2026-08-19.
    //
    // Written as a named function rather than an immediately-invoked lambda so
    // it is the same shape as `deepestSnowLayers` and the other three table
    // sweeps in this file, all of which are already proven `constexpr` over
    // `kBiomes`.
    return kMaxTreeDensity;
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
