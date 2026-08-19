#pragma once

#include "world/Block.hpp"
#include "world/Climate.hpp"

#include <cstdint>
#include <limits>

namespace game {

/// Which region of the world a column belongs to.
///
/// **Order is table order and table order is priority.** Where two rows claim
/// overlapping boxes in climate space the earlier one wins, so the specific
/// rows are listed before the general ones.
///
/// **Reorder this enum and `kBiomes` TOGETHER, never one alone.** Nothing
/// persists a `BiomeId` - the save layer does not mention one, checked
/// 2026-08-19 - so unlike `BlockId` and `ItemId`, which are written to disk and
/// can only ever be appended to, the *pair* may be rearranged at will to change
/// priority. But `biomeFor` returns `static_cast<BiomeId>(i)` off a row index,
/// so **row order is the enumeration**: move a row without its enumerator, or
/// insert one enumerator without a row, and every `BiomeId::X` silently begins
/// naming its neighbour. `Structures.cpp` and `Village.cpp` switch on these
/// names, so the symptom is bee nests and villages in the wrong biome.
///
/// An earlier version of this comment said the enum "may be reordered freely",
/// full stop. That was true about *persistence* and dangerously incomplete
/// about *indexing*, and it invited the exact edit that now fails to compile.
/// **If you hit that assert, the assert is right** - it is the 30-clause
/// `rowIs` tripwire in `Biome.cpp`, it binds each enumerator to the name of the
/// row it addresses, and it was added on 2026-08-19 precisely because this
/// comment had licensed the mistake for twenty milestones.
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
/// biome opts in explicitly, which is the safe direction - the alternative is a
/// `villageTypeFor(BiomeId)` switch somewhere else, and a second copy of a fact
/// is the most common bug in this codebase.
enum class VillageType : std::uint8_t {
    None,
    Plains,
    Desert,
    Savanna,
    Taiga,
    /// The reference calls this one `ice`. It has **no architecture of its own**
    /// - it is the taiga set with snow laid over everything the sky can see.
    ///
    /// **Sole user: Snowy Plains, and that is the reference's own count.** In
    /// `Mojang/bedrock-samples/behavior_pack/biomes/*.biome.json`, `ice_plains`
    /// is the only biome anywhere in the pack whose `minecraft:village_type` is
    /// `"ice"`; `cold_taiga` and `cold_taiga_hills` both say `"taiga"`. Snowy
    /// Taiga carried this value until 2026-08-19 and was wrong for it.
    Snowy,
    /// Not a village type. **`Village.cpp` pins its species table against this**
    /// (finding 9835, fx-bees, 2026-08-19), because `penSpeciesFor` switches
    /// over every enumerator with **no `default:` label** - which is the right
    /// shape, since a `default:` returning a real value is bug shape #10 - but
    /// **C4062 is off at `/W4`, so a seventh type produces no diagnostic
    /// anywhere.** It would fall through to `return 0`, `livestockIn` would skip
    /// the plan, and every animal pen in every village of the new type would be
    /// a fenced empty square. That is the bug findings 1074 and 9652 just
    /// closed, reopened for one village type and invisible from a build.
    ///
    /// **So add a type here and the build tells you where to go.** Pinning
    /// `Snowy == 5` instead would not work: a type appended after `Snowy`
    /// leaves `Snowy` at 5 and passes.
    Count,
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
    /// Savanna's forked diagonal trunk and flat two-layer plate.
    ///
    /// **Appended at the tail, and that was checked rather than assumed.**
    /// There is no `Count` sentinel, nothing anywhere `switch`es on this type,
    /// and the value never reaches disk: it appears in exactly three files, and
    /// `WorldStore.cpp` names `Biome` only inside `///` prose - never the
    /// struct, the table, nor `biomeInfo`. So this is pure runtime dispatch and
    /// an enumerator costs nothing. Appending anyway is habit worth keeping,
    /// because mid-enum insertion is the block-id hazard.
    ///
    /// **Draws as `Round` until `Structures.cpp` grows a branch for it, and as
    /// of 2026-08-19 13:35 it has none.** That chain tests `None`, `Tall` and
    /// `Jungle`, then leaves `Round` as a bare `else` - so a shape it does not
    /// recognise silently draws an oak instead of failing. Pointing Savanna
    /// here is therefore a behavioural no-op today rather than an improvement,
    /// and the acacia trunk placer stays unreachable from worldgen until that
    /// branch lands. **The date is the point: delete this paragraph when it
    /// does, because a negative aged past its fix reads as a defect report.**
    Acacia,
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
///
/// **Infinity rather than a large number, and that dissolves a cross-file
/// coupling instead of documenting one.** `TerrainGenerator.cpp`'s surface step
/// tests `patch >= biome.patchThreshold` against a surface-noise sample. While
/// this was `99.0f` its meaning depended on an agreement no assert could check
/// from here - the noise's range is not visible in this file - so it needed a
/// comment, and comments rot. **No finite sample can be `>=` infinity**, so the
/// sentinel is now correct for *any* noise, bounded or not, at any scale.
/// Nothing to keep in sync and nothing to re-verify.
///
/// Safe because this value is only ever **assigned and compared**, never
/// arithmetic: twenty rows in `Biome.cpp` write it into `patchThreshold` and
/// the single reader does one `>=`. **Do not introduce arithmetic on
/// `patchThreshold`** - a subtraction or a lerp would produce NaN or infinity
/// and this would stop being free. That is the one constraint left, and it is
/// about this file's own type rather than about another file's numbers.
///
/// NaN behaves correctly too, for free: every comparison against NaN is false,
/// so a corrupt sample also fails to place a patch rather than placing one.
constexpr float kNoPatch = std::numeric_limits<float>::infinity();

/// How many settled snow layers make one block, which is `Block.hpp`'s own
/// arithmetic rather than a number chosen here - `snowLayerAt` runs one to
/// seven and hands back the solid cube at eight.
constexpr int kSnowLayersPerBlock = 8;

static_assert(snowLayerAt(kSnowLayersPerBlock) == BlockId::Snow &&
                  isSnowLayer(snowLayerAt(kSnowLayersPerBlock - 1)),
              "one block of settled snow is kSnowLayersPerBlock layers; if this fails, the layer "
              "run in Block.hpp has been resized and every depth below is in the wrong unit");

/// How deep settled snow gets in a biome while it is snowing there, **in
/// blocks, exactly as the reference publishes it**.
///
/// **The unit is the whole point of this being a struct rather than two
/// floats.** `Mojang/bedrock-samples` publishes `snow_accumulation` as a
/// `[min, max]` pair nested inside the `minecraft:climate` component of each
/// `behavior_pack/biomes/*.biome.json` - *not* a `minecraft:snow_accumulation`
/// component, which does not exist and which cost one agent a whole failed
/// sweep. The pair is in blocks; the world stores layers; one block is
/// `kSnowLayersPerBlock` of them. Porting a number into a field measured in a
/// different unit is the single most expensive bug shape this project has, so
/// the published figure is kept here in the published unit and converted in
/// exactly one place, `snowLayersFrom` below.
///
/// **Do not scale this by `weather::kWorldScale`.** That factor converts
/// *altitudes* - it exists because our 96-block world stands in for a
/// 384-block one, so a lapse rate per block of elevation has to be restated.
/// A snow layer is not an altitude; it is a subdivision of a block, and a block
/// is the same size in both. A drift the reference draws 1.5 blocks deep is
/// 1.5 blocks deep here too. Applying the scale would be the same unit mistake
/// in the opposite direction, and it is the one a careful reader is most likely
/// to make, because the constant sits three files away arguing for itself.
///
/// **Read as a depth range rather than a rate, and this is now settled rather
/// than inferred.** The wiki calls it *"its own value determining how fast snow
/// accumulates"*, which reads as a rate; the data does not support that, and
/// two independent checks say so.
///
/// **Every published value is an exact multiple of one eighth.** Across the
/// files read at source: 0.0, 0.125, 0.25, 0.375, 0.5, 1.0, 1.5 - which in
/// layers are 0, 1, 2, 3, 4, 8, 12, whole numbers every one. A rate has no
/// reason to land on eighths; a depth quantised to layers can land nowhere
/// else.
///
/// **And the maximum agrees with the prose to the layer.** `ice_mountains` and
/// `ice_plains_spikes` both publish a maximum of 1.5 blocks, and minecraft.wiki
/// [[Snow (layer)]] states that in Bedrock *"anywhere from 1-12 layers of snow
/// can build up during snowfall, depending on the biome"*. 1.5 x 8 = 12,
/// exactly. Two sources that share no wording arriving at the same number is
/// the check; the same page also states outright that *"snow generates
/// randomized in multiple layers in Bedrock Edition, depending on the biome's
/// snow accumulation height"*, which is this field named.
///
/// The supporting reading, kept because it was what settled it first: the pair
/// is in blocks with no time unit anywhere in the file, and several biomes
/// publish a **minimum of exactly 0.0** (`plains`, `taiga`, `extreme_hills`,
/// `stone_beach`), which as a rate would mean "sometimes never accumulates at
/// all" and as a depth reads perfectly as "between nothing and a dusting".
struct SnowAccumulation {
    float minBlocks = 0.0f;
    float maxBlocks = 0.0f;
};

/// The published figure turned into the unit the world stores. Rounded to
/// nearest so 0.125 is one layer rather than zero.
constexpr int snowLayersFrom(float blocks) {
    const int layers = static_cast<int>(blocks * static_cast<float>(kSnowLayersPerBlock) + 0.5f);
    return layers < 0 ? 0 : layers;
}

// Every value the reference publishes, against the layer count a reader would
// check by hand. The single edit that fails these: dropping the rounding term,
// which silently turns every 0.125 row into no snow at all.
//
// 0.375 and 1.5 are not in our table - they are `ice_mountains` and
// `ice_plains_spikes`, read at source - and they are here because they are what
// prove the eighths argument above: 3 layers and 12, both whole, and the 12
// matches the wiki's published Bedrock maximum exactly.
static_assert(snowLayersFrom(0.0f) == 0 && snowLayersFrom(0.125f) == 1 &&
                  snowLayersFrom(0.25f) == 2 && snowLayersFrom(0.375f) == 3 &&
                  snowLayersFrom(0.5f) == 4 && snowLayersFrom(1.0f) == 8 &&
                  snowLayersFrom(1.5f) == 12,
              "a snow_accumulation pair is in blocks and one block is eight layers");

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
    /// through to the ordinary rule* - so windswept hills is grass with stone
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

    /// How deep settled snow gets here during snowfall, in blocks - the
    /// reference's `snow_accumulation`, in the reference's own unit. See
    /// `SnowAccumulation` above for the unit, the source and why it is read as
    /// a depth rather than a rate.
    ///
    /// **Defaulted to nothing, and every row that can ever see snow has been
    /// read at source.** A row left at the default is one of three things, and
    /// only the third is a mistake:
    ///   - **too warm to reach freezing anywhere in this world.** `freezesAt`
    ///     starts cooling at `weather::kWarmthBaseHeight` (y 29) at
    ///     `weather::kWarmthPerBlock`, so the highest surface this world
    ///     generates can only take `warmth` down by about 0.27. A row above
    ///     `0.15 + 0.27 = 0.42` can never snow, its pair could never be
    ///     consulted, and leaving it blank asserts nothing. That covers every
    ///     ocean, river, beach, jungle, forest, swamp, savanna, desert and
    ///     badlands row, and Stony Peaks at 1.0.
    ///   - **measured as genuinely absent.** `meadow` and `stony_peaks` carry
    ///     no `snow_accumulation` key at all in Mojang's pack, and `meadow` is
    ///     cold enough (0.3) that this is a real answer rather than an
    ///     unreachable one: a meadow above its snow line gets snowfall and no
    ///     drift. Both were fetched and read, 2026-08-19.
    ///   - **not yet ported**, which is what the count assert in `Biome.cpp`
    ///     exists to catch.
    SnowAccumulation snow{};
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
/// the *place*, not about the label - see `Climate.hpp`.
///
/// **`shape` has one writer and zero readers, and that is the compiler's
/// verdict rather than a text search's, as of 2026-08-19 05:20.** `sampleBiome`
/// fills it; all ten call sites in the tree - `Structures.cpp`, `Village.cpp`,
/// `World.cpp` and `Weather.cpp` one each, `Creature.cpp` two, `Main.cpp` four
/// - read only `.dominant` or `.climate`.
///
/// The method, recorded because a negative claim without one is unfalsifiable:
/// mark this field `[[deprecated]]` in a shadow copy of this header, put that
/// copy first on the include path, and compile **all 39 translation units** in
/// `game/src`. Exactly one warning came back - the write in `Biome.cpp`.
///
/// Three things are what make that zero *evidence* rather than merely an
/// absence of evidence. **One**, every TU was proven to have compiled, by exit
/// code *and* a freshly written `.obj` *and* a match on `error|fatal` rather
/// than the narrower `: error `, which silently misses
/// `cl : Command line error D8003` - a compiler that never starts is the
/// friendliest possible failure to mistake for success, and the narrow pattern
/// was measured missing exactly that string. Eight TUs were transiently red
/// during the run, every one of them another agent mid-write on `Block.hpp`,
/// and each was retried until it built, because a TU that fails to compile
/// cannot raise a warning and its silence proves nothing. **Two**, the control
/// was *graded* rather than merely present: three planted TUs holding 0, 1 and
/// 3 reads returned 0, 1 and 3. A control that only shows it can fire is worth
/// little - it has to differ from the claim in the right direction and by the
/// right magnitude, or claim and control can return the same wrong number.
/// **Three**, of the three planted reads one was a **structured binding**,
/// `auto [d, c, sh] = s`, and one a const-reference bind - **neither of which
/// any text search for `.shape` could ever have matched.** That is the hole
/// this method closes, and it is why the earlier search-based version of this
/// claim was weaker than it read. **To falsify this, redo it; do not grep.**
/// Do not read it as "safe to delete":
/// it is the one place the shape of a column is available beside its biome, and
/// the reason it is a field rather than a second call is that `shapeAt` needs
/// the `climate` that was just computed. Do read it as "not yet load-bearing" -
/// nothing today would break if it were wrong, which is precisely why it must
/// not be allowed to become wrong quietly.
///
/// **That "nothing today" is a measurement with a date on it, 2026-08-19, not
/// a property of this field.** Two claims of exactly that form in
/// `Weather.hpp` expired within six hours of being written, because other
/// agents spent the night making dormant things readable. **What would falsify
/// it: any read of `.shape` off this struct outside `Biome.cpp`.** If you find
/// one, this stops being a tripwire-only field and the sentence above must go.
///
/// **It cannot drift from the terrain's own copy, and that is by construction
/// rather than by agreement.** `TerrainGenerator.cpp`'s `sampleColumn` also
/// needs a shape, and both call the same `shapeAt(seed, climate, worldX,
/// worldZ)` with the same four arguments. There is one derivation and one
/// owner. `ColumnSample` is **not** a duplicate of this struct - it carries
/// `climate` plus an `int surface` and has no `dominant` and no `shape`,
/// because its job is to stop `climateAt`'s twenty noise evaluations being paid
/// twice per column. If you change the arguments to either `shapeAt` call,
/// change both.
///
/// Cost, measured rather than guessed, and reported as a range because the
/// measurement would not settle: filling `shape` is somewhere between 4% and
/// 16% of a `sampleBiome` call across repeated min-of-25 runs, while
/// `climateAt` alone is a stable ~70%. The spread is machine contention, not
/// the code. **That is too loose to optimise against** - if this ever needs to
/// be cheaper, re-measure on a quiet machine first.
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
