#include "world/Village.hpp"
#include "world/Creature.hpp"
#include "world/Loot.hpp"
#include "world/Noise.hpp"

#include <algorithm>
#include <array>

namespace game::village {
namespace {

using Roll = noise::Stream;

/// Salts. Separate constants so two different questions about the same cell can
/// never accidentally share a stream.
constexpr std::uint32_t kLayoutSalt = 0x5ea70002u;
constexpr std::uint32_t kDressSalt = 0x5ea70003u;

/// How rough the ground under the town centre may be before the site is
/// rejected outright, in blocks.
///
/// The reference does no flatness test at all and bends the terrain instead
/// (`beard_thin`, a density-function modifier). We have no density lattice any
/// more — it was removed deliberately at M20p — so the honest substitute is to
/// be picky about where a village starts. A strict site test costs one village
/// in a hilly region; a lax one costs a house sunk to its eaves in a hillside.
///
/// **Measured rather than chosen.** At 6 the probe rejected 79% of every
/// biome-eligible cell and villages came out one per 2000 blocks; at 11 it
/// rejects about a third and they land roughly a thousand apart, which is the
/// reference's own density. Individual plots still have `kMaxFill`/`kMaxCut` of
/// their own, so a loose site test costs a few dropped plots and not a sunken
/// house.
constexpr int kSiteRoughness = 11;

/// How far a single building may cut into or fill over the ground before the
/// plot is abandoned. Fill is allowed to be deeper than cut because a platform
/// under a house reads as deliberate and a house buried to its windows does not.
constexpr int kMaxFill = 6;
constexpr int kMaxCut = 3;

/// How much headroom the town square's own foundation clears above its paving.
/// Stated once because it is both what `buildTownCentre` passes to `foundation`
/// and the deepest cut the site test may accept there.
constexpr int kSquareClearance = 8;

/// Street lengths, in blocks: `kArmMin` plus a roll over `kArmSpan`.
constexpr int kArmMin = 16;
constexpr int kArmSpan = 10;

/// The growth stage a village farm's crops generate at, as a **crop age**.
///
/// Seven, because this game follows Bedrock and Bedrock plants a ripe field:
/// `https://minecraft.wiki/w/Village/Structure/Blueprints` — *"In Java Edition
/// the crops that generate on a village farm spawn as seeds, while in Bedrock
/// Edition they spawn as full-grown crops."* `RESEARCH.md` §19.2 records it too.
///
/// **`[wiki/observational]` — the Bedrock half of that is not primary-sourced
/// and cannot be, so do not treat 7 as a published constant.**
/// `Mojang/bedrock-samples` says nothing about village generation at all (see
/// `kFarmCrops` below), so there is no file to check it against. What *is*
/// primary is the Java half, and it corroborates the split rather than
/// undermining it: the `processor_list/farm_*.json` rules name their crops as
/// bare `output_state` strings carrying no `age` property, which is age 0 -
/// seeds - exactly as the wiki says of Java. The claim is also explicitly
/// edition-tagged in the wiki's own prose, naming both editions in one
/// sentence, which is the opposite of the untagged-Java tables that have
/// misled this project repeatedly. Believed, sourced as far as it goes, and
/// labelled.
///
/// The unit matters and is the trap here: this is an **age**, the offset from a
/// family's age-0 id, not a count of stages and not a picture index. Beetroot
/// has four pictures over these eight ages, which is exactly the sort of number
/// that comes out wrong when the wrong table is asked.
constexpr int kRipeCrop = 7;

/// **Derived against `Block.hpp`'s own table, both ways.** Each family's ripe id
/// has to be the `...CropLast` that file names — which is the claim a reader
/// actually depends on — and asking for one age past it has to give the same
/// block back, which is what says a ripe beetroot cannot become whatever id
/// follows beetroot. Comparing `cropAt(f, 7)` against `f + 7` would restate the
/// definition and prove nothing.
static_assert(cropAt(BlockId::WheatCrop0, kRipeCrop) == BlockId::WheatCropLast &&
                  cropAt(BlockId::CarrotCrop0, kRipeCrop) == BlockId::CarrotCropLast &&
                  cropAt(BlockId::PotatoCrop0, kRipeCrop) == BlockId::PotatoCropLast &&
                  cropAt(BlockId::BeetrootCrop0, kRipeCrop) == BlockId::BeetrootCropLast,
              "kRipeCrop is not the last age of every crop family Block.hpp declares");
static_assert(cropAt(BlockId::BeetrootCrop0, kRipeCrop + 1) ==
                  cropAt(BlockId::BeetrootCrop0, kRipeCrop),
              "cropAt has stopped clamping, so an over-age crop now names the next block along");
static_assert(cropAt(BlockId::MelonStem0, kRipeCrop) == BlockId::MelonStemLast &&
                  cropAt(BlockId::PumpkinStem0, kRipeCrop) == BlockId::PumpkinStemLast,
              "kRipeCrop is not the last age of both stems, which farm tables below plant");

/// **The reference's five farm crop tables, one per village type.**
///
/// We had one table - the plains one - and ran it in every village, so a desert
/// farm grew the carrots and potatoes a desert farm has none of, a savanna farm
/// grew four crops where the reference grows one, and no farm anywhere ever
/// grew a melon or a pumpkin. The rows differ only in which rules they carry,
/// which is what makes this a table rather than five branches.
///
/// **`[JE]`, knowingly, because there is no Bedrock alternative to prefer.**
/// These are the `minecraft:worldgen/processor_list/farm_*.json` rule lists
/// from the reference's Java data pack. `Mojang/bedrock-samples` - Mojang's own
/// published Bedrock behaviour pack, and the primary source that beats the wiki
/// wherever it reaches - publishes **nothing whatever** about village
/// generation: `behavior_pack/` has no `features/`, no `feature_rules/` and no
/// `structures/`, and `metadata/vanilladata_modules/mojang-features.json` lists
/// `minecraft:village` as a bare name carrying no parameters. Bedrock's village
/// generator is native C++. So these are Java numbers used with their edition
/// written down, rather than a Bedrock value we do not have.
///
/// **Each `chance` is the rule's own probability exactly as the JSON states
/// it, not a share of the field.** The processor walks its rules in order and
/// the first match wins, so a later rule only ever sees the columns the earlier
/// ones left. The chaining is done where the draw is made, so the numbers here
/// stay the ones a reader can check straight against the source.
struct CropRule {
    BlockId family;  ///< Age-0 id of the family this rule plants.
    float chance;    ///< The rule's own probability, read off the JSON.
};

/// What the structure template itself stores, and what a column no rule claimed
/// therefore keeps. The reference's farms are laid as wheat and *replaced*; the
/// same block in all five tables, so it is not a column of the table.
constexpr BlockId kFarmBaseCrop = BlockId::WheatCrop0;

struct FarmCrops {
    CropRule rules[3];
    int count;
};

constexpr FarmCrops kFarmCrops[] = {
    // None - never used, but the array is indexed by VillageType so it needs a row.
    {{}, 0},
    // Plains - farm_plains.json: carrots .3, potatoes .2, beetroots .1.
    {{{BlockId::CarrotCrop0, 0.3f},
      {BlockId::PotatoCrop0, 0.2f},
      {BlockId::BeetrootCrop0, 0.1f}},
     3},
    // Desert - farm_desert.json: beetroots .2, melon stems .1. No carrots, no potatoes.
    {{{BlockId::BeetrootCrop0, 0.2f}, {BlockId::MelonStem0, 0.1f}}, 2},
    // Savanna - farm_savanna.json: melon stems .1 and nothing else, so nine
    // columns in ten stay wheat. One rule really is the whole table.
    {{{BlockId::MelonStem0, 0.1f}}, 1},
    // Taiga - farm_taiga.json: pumpkin stems .3, potatoes .2. No carrots, no beetroot.
    {{{BlockId::PumpkinStem0, 0.3f}, {BlockId::PotatoCrop0, 0.2f}}, 2},
    // Snowy - farm_snowy.json: carrots .1, potatoes .8, which lands at nearly
    // three quarters potato once chained.
    {{{BlockId::CarrotCrop0, 0.1f}, {BlockId::PotatoCrop0, 0.8f}}, 2},
};

// **`VillageType::Count`, not `Snowy + 1`, and `Biome.hpp` spells out why against
// the enumerator itself**: a type appended after `Snowy` leaves `Snowy` at 5, so
// this assert would still read 6 == 6 and pass while `kFarmCrops[6]` ran off the
// end of the array. Identical value today, and the whole difference tomorrow.
static_assert(std::size(kFarmCrops) == static_cast<std::size_t>(VillageType::Count),
              "one farm table per VillageType, None included");

/// The share of a field no rule claims, which is the share that stays wheat.
constexpr float unclaimedShare(const FarmCrops& table) {
    float left = 1.0f;
    for (int i = 0; i < table.count; ++i) {
        left *= 1.0f - table.rules[i].chance;
    }
    return left;
}

constexpr bool nearly(float a, float b) { return (a > b ? a - b : b - a) < 1e-4f; }

/// **Derived against a figure the source states separately, which is the whole
/// point.** Chaining the rule probabilities above has to reproduce the wheat
/// share the reference publishes per biome in its own farm summary - 50.4% in
/// plains, 72% desert, 90% savanna, 56% taiga, 18% snowy. Those percentages are
/// arrived at by a different route from these rules, so agreeing is evidence;
/// re-adding the same three numbers and comparing them to their own sum would
/// have proved nothing. Get a `chance` or a row order wrong and this fails.
static_assert(
    nearly(unclaimedShare(kFarmCrops[static_cast<std::size_t>(VillageType::Plains)]), 0.504f) &&
        nearly(unclaimedShare(kFarmCrops[static_cast<std::size_t>(VillageType::Desert)]), 0.720f) &&
        nearly(unclaimedShare(kFarmCrops[static_cast<std::size_t>(VillageType::Savanna)]),
               0.900f) &&
        nearly(unclaimedShare(kFarmCrops[static_cast<std::size_t>(VillageType::Taiga)]), 0.560f) &&
        nearly(unclaimedShare(kFarmCrops[static_cast<std::size_t>(VillageType::Snowy)]), 0.180f),
    "a farm table no longer chains to the wheat share the reference publishes for its biome");

/// Furthest from the origin a plot's **footprint** may reach.
///
/// Two separate rules cap this and the tighter one wins. The foundation pours a
/// ring two cells beyond the footprint, so the footprint plus 2 has to stay
/// inside `kReach` or a chunk that never looks would drop a skirt column. And
/// `occupies` claims `kClaimMargin` beyond the footprint, so the footprint plus
/// that margin has to stay inside `kClaimReach` or a tree is suppressed by one
/// chunk and planted by its neighbour. Real plots reach about 25, so this binds
/// on nothing today; it exists so that the day a street or a design grows, the
/// build fails instead of the world.
constexpr int kMaxFootprintReach = 47;

/// How far past a building or a street a village claims the ground, so nothing
/// plants a tree through a roof or in the middle of the road.
constexpr int kClaimMargin = 3;
constexpr int kRoadClaimMargin = 2;

static_assert(kMaxFootprintReach + 2 <= kReach,
              "the foundation ring would be written outside the radius chunks search");
static_assert(kMaxFootprintReach + kClaimMargin <= kClaimReach,
              "a claimed column would sit outside the window every chunk solves, so one chunk "
              "would suppress a tree its neighbour plants");
static_assert(kArmMin + kArmSpan - 1 + kRoadClaimMargin <= kClaimReach,
              "lengthen the streets and the far end of one claims ground a chunk beyond the "
              "village radius never hears about");

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

/// Finds a cut-shape family by the block it was cut from.
///
/// A search rather than a written-down index: `kStairFamilies` is the table that
/// owns the mapping, and a second copy of it here is exactly the bug this
/// codebase keeps paying for. It is `constexpr`, so it costs nothing at runtime.
constexpr int stairFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kStairFamilies.size(); ++f) {
        if (kStairFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int slabFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kSlabFamilies.size(); ++f) {
        if (kSlabFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int fenceFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kFenceFamilies.size(); ++f) {
        if (kFenceFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int gateFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kGateFamilies.size(); ++f) {
        if (kGateFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int wallFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kWallFamilies.size(); ++f) {
        if (kWallFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

/// **The fifth of five twins and the only one with no caller** — true as of
/// 2026-08-19, falsified by the first village design that builds a decorative
/// wall. Kept rather than deleted, because it is the only thing that turns a
/// parent block into a `kWallFamilies` index and deleting it means the first
/// caller writes the loop inline: a value derived somewhere other than the
/// table that owns it, which is this project's most repeated bug. Bound rather
/// than left to rot, because nothing else would notice it going wrong —
/// `constexpr` implies `inline`, which gives external linkage, so MSVC's C4505
/// never fires on an uncalled one at `/W4`.
constexpr bool everyWallFamilyFindsItself() {
    int checked = 0;
    for (std::size_t f = 0; f < kWallFamilies.size(); ++f) {
        if (wallFamilyOf(kWallFamilies[f].parent) != static_cast<int>(f)) {
            return false;
        }
        ++checked;
    }
    // The count pin `everyJobSiteIsClassified` carries and this file's other
    // proofs are worth copying for: without it an emptied table would leave the
    // loop finding nothing at all and passing in silence.
    return checked > 0 && checked == static_cast<int>(kWallFamilies.size());
}

static_assert(everyWallFamilyFindsItself(),
              "a wall family no longer finds its own index - kWallFamilies has been reordered or "
              "two rows now share a parent, and the search would hand back the wrong one");

/// **The trap this one has that its four twins do not, stated because there is
/// no caller to have taught anybody.** The fallback is family 0, and family 0
/// is Cobblestone Wall — a real answer, not a sentinel — so a parent with no
/// wall at all is indistinguishable from a correct cobblestone one.
/// `kWallFamilies` is rock only, the reference having no wooden wall, while
/// `Palette::wall` is the *bulk* block a building's wall is made of and is
/// planks on four of the five village types. So the natural first call, written
/// by symmetry with the four live twins that really are called
/// `fenceFamilyOf(palette.wall)`, silently gives every savanna a cobblestone
/// wall. **If this assert ever fires it is good news** — a wall family whose
/// parent is a plank has been added — and the fix is to say so here rather than
/// to widen the literal.
static_assert(wallFamilyOf(BlockId::Planks) == 0 && wallFamilyOf(BlockId::AcaciaPlanks) == 0 &&
                  kWallFamilies[0].parent == BlockId::Cobblestone,
              "the wall-family miss still returns a real family rather than a sentinel, so a "
              "palette block with no wall cannot be told from a cobblestone one");

/// Door and trapdoor families are declared in `kWoods` order, so a wood is an
/// index rather than a search.
constexpr int kOakOpening = 0;
constexpr int kSpruceOpening = 1;
constexpr int kAcaciaOpening = 4;

/// Everything a village type differs by.
///
/// **The palette is the table and the building is one forwarding function.**
/// Five palettes over eleven shapes is fifty-five buildings for eleven authored
/// silhouettes, which is the same trade `ShapedFamily` made for six hundred cut
/// blocks. Writing five sets of builders instead would be five places for the
/// same bug to live.
struct Palette {
    BlockId wall;        ///< The bulk of a wall.
    BlockId wallAlt;     ///< A banding course, and the upper storey.
    BlockId post;        ///< Corner posts and framing.
    BlockId stone;       ///< Plinths, chimneys and the well.
    BlockId stoneAlt;    ///< Weathering, scattered through the stone.
    BlockId roof;        ///< Parent block the roof stairs and slabs are cut from.
    BlockId roofFill;    ///< Solid roof core, and the whole roof on a flat one.
    BlockId floor;       ///< Under the floor, and the doorstep.
    BlockId path;        ///< The street surface.
    BlockId bridge;      ///< What the street becomes over water.
    BlockId accent;      ///< Wool, terracotta or snow: the type's colour.
    BlockId lamp;        ///< On top of a lamp post.
    BlockId foundation;  ///< The platform poured under a building on a slope.
    int opening;         ///< Door and trapdoor family.
    int bedColour;
    int bedColourAlt;
    /// Flat roofs only, which is what makes a desert village unmistakable.
    bool flatRoofs;
    /// Lay snow over the exposed horizontal surfaces afterwards.
    bool snowy;
};

constexpr Palette kPalettes[] = {
    // None — never used, but the array is indexed by VillageType so it needs a row.
    {},
    // Plains: oak and cobblestone, white and yellow accents, torches on fence posts.
    {BlockId::Planks, BlockId::StrippedOakLog, BlockId::Log, BlockId::Cobblestone,
     BlockId::MossyCobblestone, BlockId::Planks, BlockId::Planks, BlockId::Cobblestone,
     BlockId::DirtPath, BlockId::Planks, BlockId::WhiteWool, BlockId::Torch, BlockId::Dirt,
     kOakOpening, 14, 4, false, false},
    // Desert: sandstone throughout, flat terracotta roofs, smooth sandstone streets.
    {BlockId::Sandstone, BlockId::CutSandstone, BlockId::ChiseledSandstone,
     BlockId::SmoothSandstone, BlockId::Sandstone, BlockId::SmoothSandstone,
     BlockId::OrangeTerracotta, BlockId::SmoothSandstone, BlockId::SmoothSandstone,
     BlockId::Planks, BlockId::LightBlueTerracotta, BlockId::Torch, BlockId::Sandstone,
     kOakOpening, 1, 14, true, false},
    // Savanna: acacia logs stand in for the cobblestone everywhere else.
    {BlockId::AcaciaPlanks, BlockId::StrippedAcaciaLog, BlockId::AcaciaLog, BlockId::AcaciaLog,
     BlockId::StrippedAcaciaLog, BlockId::AcaciaPlanks, BlockId::AcaciaPlanks,
     BlockId::AcaciaPlanks, BlockId::DirtPath, BlockId::AcaciaPlanks, BlockId::OrangeTerracotta,
     BlockId::Torch, BlockId::Dirt, kAcaciaOpening, 1, 14, false, false},
    // Taiga: spruce and cobblestone, purple and blue beds.
    {BlockId::SprucePlanks, BlockId::StrippedSpruceLog, BlockId::SpruceLog, BlockId::Cobblestone,
     BlockId::MossyCobblestone, BlockId::SprucePlanks, BlockId::SprucePlanks,
     BlockId::Cobblestone, BlockId::DirtPath, BlockId::SprucePlanks, BlockId::Cobblestone,
     BlockId::Torch, BlockId::Dirt, kSpruceOpening, 10, 11, false, false},
    // Snowy: the taiga set with snow and lanterns. **No architecture of its own**,
    // which is the reference's own arrangement rather than a shortcut.
    {BlockId::SprucePlanks, BlockId::StrippedSpruceLog, BlockId::SpruceLog, BlockId::Cobblestone,
     BlockId::SnowBlock, BlockId::SprucePlanks, BlockId::SprucePlanks, BlockId::Cobblestone,
     BlockId::DirtPath, BlockId::SprucePlanks, BlockId::SnowBlock, BlockId::Lantern,
     BlockId::Dirt, kSpruceOpening, 0, 3, false, true},
};

// **A bare `6` was the weakest rung of the three available and this table is the
// one that could least afford it.** `paletteFor` below indexes straight into
// `kPalettes` with no clamp, so a seventh `VillageType` with no row here is not
// a wrong palette, it is a read past the end of a `constexpr` array - a village
// built out of whatever block ids happen to follow it in `.rdata`. A literal
// cannot see the enum grow; `VillageType::Count` is exactly the sentinel added
// for this, and `Biome.hpp` argues the case beside it.
static_assert(std::size(kPalettes) == static_cast<std::size_t>(VillageType::Count),
              "one palette per VillageType, None included");

const Palette& paletteFor(VillageType type) {
    return kPalettes[static_cast<std::size_t>(type)];
}

/// Every block a villager takes as a job site, one per profession, **in the
/// order `professionForJobSite` returns them** — `Creature.hpp`, not this file.
///
/// The comment here used to name `villagerProfessionFor`, **a function that has
/// never existed anywhere in the tree**. Anyone checking the ordering claim
/// would have grepped it, found nothing, and had to guess whether the contract
/// was real. Bug shape #16, and the reason the asserts below now exist: the
/// claim is true, it is load-bearing, and until now it was checked by a
/// sentence pointing at a name.
constexpr BlockId kJobSites[kJobSiteCount] = {
    BlockId::Composter0,        // Farmer
    BlockId::Barrel,            // Fisherman
    BlockId::FletchingTable,    // Fletcher
    BlockId::Loom,              // Shepherd
    BlockId::CartographyTable,  // Cartographer
    BlockId::Lectern,           // Librarian
    BlockId::Stonecutter,       // Mason
    BlockId::SmithingTable,     // Toolsmith
    BlockId::Grindstone,        // Weaponsmith
    BlockId::BlastFurnace,      // Armourer
    BlockId::Smoker,            // Butcher
    BlockId::Cauldron,          // Leatherworker
    BlockId::BrewingStand,      // Cleric
};

/// Whether reading `kJobSites` from `shift` still names profession `i + 1`.
///
/// Parameterised **so the check can be made to fail on purpose**, which is the
/// only thing that makes the passing case evidence. Copied in spirit from
/// `FaceGeometry.hpp`, which feeds a row back reversed and requires rejection.
constexpr bool jobSitesAgreeFrom(int shift) {
    for (int i = 0; i < kJobSiteCount; ++i) {
        if (professionForJobSite(kJobSites[(i + shift) % kJobSiteCount]) !=
            static_cast<std::uint8_t>(i + 1)) {
            return false;
        }
    }
    return true;
}

constexpr bool everyRotationRejected() {
    for (int shift = 1; shift < kJobSiteCount; ++shift) {
        if (jobSitesAgreeFrom(shift)) {
            return false;
        }
    }
    return true;
}

/// **Two tables in two files, written independently, and nothing tied them
/// together.** This one is an array of ids; `Creature.hpp`'s
/// `professionForJobSite` is a chain of family predicates returning 1..13. They
/// have to be the same ordering or a villager standing at a lectern becomes a
/// mason — and it would be **silently** wrong, visible only as the wrong
/// outfit on a villager nobody was watching. Both sides are `constexpr`, so
/// this is the `foodTablesAgree()` shape: an assert between two tables that can
/// never rot, rather than a comment that already had.
static_assert(jobSitesAgreeFrom(0),
              "kJobSites no longer matches Creature.hpp's professionForJobSite: a villager will "
              "take the wrong trade from its workstation");
static_assert(everyRotationRejected(),
              "NEGATIVE CONTROL: rotating kJobSites must break the check above. If a rotation "
              "passes, the positive assert is proving nothing");

/// The count is `Creature.hpp`'s, minus the nitwit, which no block offers.
// **Literal rows against declared size — the property the assert below does
// NOT cover, and they are independent.** That one ties `kJobSiteCount` to the
// profession enum, which is the *declared size vs the enum* half. A short
// initialiser is a different failure: C++ does not reject it, it
// value-initialises the tail, so twelve rows in a thirteen-wide table leaves
// `kJobSites[12] == BlockId{}` and **both asserts still pass**. The cost is
// silent and real — `workstation = kJobSites[...]` runs in three places, so a
// villager would claim block id 0 as a job site and no build would object.
//
// Compared against `BlockId{}` rather than against `BlockId::Air` on purpose:
// value-initialisation is what a short initialiser actually produces, whatever
// enumerator happens to sit at zero, so this cannot rot if the ids renumber.
// **Falsified by a legitimate job site whose id is 0**, which would require
// `Block.hpp` to put a workstation first in the enum.
constexpr bool jobSitesAllNamed() {
    for (const BlockId id : kJobSites) {
        if (id == BlockId{}) {
            return false;
        }
    }
    return true;
}

static_assert(jobSitesAllNamed(),
              "kJobSites has a value-initialised entry, which means the initialiser is SHORTER than "
              "kJobSiteCount and C++ filled the tail with block id 0. Add the missing row rather "
              "than shrinking the count, and check professionForJobSite covers it.");

static_assert(kJobSiteCount == kVillagerProfessionCount - 1,
              "a profession was added to Creature.hpp without a job site here, or the nitwit "
              "stopped being the one profession with no workstation");

/// A block nobody claims must stay unclaimed. **This is the widening trap**:
/// `professionForJobSite` asks family predicates, and a family that grows to
/// cover an ordinary block would hand out a trade for standing near planks.
///
/// The families are not incidental. A composter really does carry nine fill
/// states, and that one **is** primary-sourced rather than assumed: Mojang's
/// `mojang-blocks.json` publishes `composter_fill_level` as `[0..8]` with
/// **exactly one user, `minecraft:composter`** - and a single-user property is
/// the one case where the published list is authoritative for the block rather
/// than merely the width of a shared field. Nine ids is why `kJobSites` names
/// `Composter0` and the predicate side asks `isComposter`.
static_assert(professionForJobSite(BlockId::Air) == 0 &&
                  professionForJobSite(BlockId::Planks) == 0 &&
                  professionForJobSite(BlockId::Furnace) == 0,
              "a job-site family has widened to swallow a block that offers no trade");

/// Which bit of a building's `style` says it keeps a trade indoors.
///
/// **One bit — half of the ordinary houses — and that is the measured value,
/// not the tidy one.** The prose at the use site said "a quarter" and the code
/// has always said a half; the reasoning three lines under it is what settles
/// which is right: our villages are eight pieces where the reference's are
/// twenty, so the *share* has to be higher for the absolute count of trades to
/// come out the same. Halving it to match a word would have put five villages
/// in six back to a single profession, which is the thing this bit was added to
/// fix. Named rather than spelled `512u` at the use site so the next reader
/// finds this note instead of counting bits.
///
/// It swaps one chest for another rather than costing one, which is worth
/// stating: a house with a job block in it rolls that job's table instead of
/// the house table, and ten of the thirteen job blocks have one.
constexpr std::uint32_t kTradeIndoors = 0x200u;

/// Which of the reference's village chest tables a job block's building rolls,
/// or -1 for a job block the reference gives no chest to.
///
/// **Keyed on the workstation, because that is the one place that knows what a
/// building is.** A grindstone in it makes it the weaponsmith's whatever the
/// walls look like, `Building::workstation` already carries the answer, and
/// re-deriving "which building is this" from the design, the size or the style
/// would be a second place for the same fact to be wrong.
///
/// **The three that return -1 are a recorded match with the reference, not a
/// gap.** Bedrock's behaviour pack ships fifteen village chest tables and there
/// is no `village_farmer`, `village_fisher` or `village_librarian` among them,
/// so the composter, the barrel and the lectern get no chest *there* either;
/// inventing tables for them would be inventing loot. `Loot.hpp` records the
/// same fact at its own end. Both ends say it because it looks exactly like the
/// eight that really were missing, and the next reader to count thirteen job
/// sites against ten tables will otherwise "fix" it.
///
/// The `default:` returns a real value, which is normally how a missing entry
/// hides. It cannot hide here: `kJobSites` is the one table that owns the set of
/// job blocks, and `everyJobSiteIsClassified` below walks it and fails the build
/// for any member that is neither given a table nor named as chest-less.
constexpr int jobChestTable(BlockId workstation) {
    switch (workstation) {
    case BlockId::Grindstone:
        return static_cast<int>(loot::TableId::VillageWeaponsmith);
    case BlockId::SmithingTable:
        return static_cast<int>(loot::TableId::VillageToolsmith);
    case BlockId::Stonecutter:
        return static_cast<int>(loot::TableId::VillageMason);
    case BlockId::FletchingTable:
        return static_cast<int>(loot::TableId::VillageFletcher);
    case BlockId::Loom:
        return static_cast<int>(loot::TableId::VillageShepherd);
    case BlockId::CartographyTable:
        return static_cast<int>(loot::TableId::VillageCartographer);
    case BlockId::BlastFurnace:
        return static_cast<int>(loot::TableId::VillageArmourer);
    case BlockId::Smoker:
        return static_cast<int>(loot::TableId::VillageButcher);
    case BlockId::Cauldron:
        // `village_tannery` — the leatherworker's, named after the building
        // rather than the job in the reference's own files.
        return static_cast<int>(loot::TableId::VillageTannery);
    case BlockId::BrewingStand:
        // `village_temple` — the cleric's, likewise named after the building.
        return static_cast<int>(loot::TableId::VillageTemple);
    default:
        break;
    }
    return -1;
}

/// The three job blocks the reference itself gives no chest to.
///
/// Named rather than inferred from `jobChestTable` returning -1, so that "no
/// table" and "no table on purpose" are different statements. A fourteenth job
/// site added with neither fails the assert below.
constexpr bool referenceHasNoChestFor(BlockId workstation) {
    return workstation == BlockId::Composter0 ||  // farmer
           workstation == BlockId::Barrel ||      // fisherman
           workstation == BlockId::Lectern;       // librarian
}

/// Every job block `solve` can deal is either given a table or recorded as one
/// the reference does not stock, and never both.
///
/// **The single edit that fails this is adding a row to `kJobSites` without
/// deciding which it is** — the exact way the last eight job sites came to be
/// placed for twenty milestones with no chest and no complaint.
constexpr bool everyJobSiteIsClassified() {
    int recorded = 0;
    for (const BlockId job : kJobSites) {
        const int table = jobChestTable(job);
        if ((table < 0) != referenceHasNoChestFor(job)) {
            return false;
        }
        if (table >= static_cast<int>(loot::TableId::Count)) {
            return false;
        }
        if (referenceHasNoChestFor(job)) {
            ++recorded;
        }
    }
    // And the recorded three are all really dealt, so the exception cannot rot
    // into naming a block no builder places.
    return recorded == 3;
}

static_assert(everyJobSiteIsClassified(),
              "a job site has no loot table and is not recorded as chest-less");

/// Which loot table a building's chest rolls, or -1 for a building that gets no
/// chest.
constexpr int lootTableFor(Design design, BlockId workstation) {
    if (workstation != BlockId::Air) {
        return jobChestTable(workstation);
    }
    // A plain dwelling - no job block at all. The reference stocks its houses
    // by biome; we have one house table, so one house design carries it, which
    // keeps a village at roughly the reference's two-ish chests rather than one
    // per building.
    if (design == Design::MediumHouse) {
        return static_cast<int>(loot::TableId::VillageHouse);
    }
    return -1;
}

/// A job block turned so a customer stands at its mouth.
///
/// **One owner for the turn.** The house, the market stall and the farm all
/// place a job site, and the "is this a furnace?" rule had reached two of the
/// three: the farm hard-coded a composter instead of reading the field the
/// layout had already filled in, so retuning a farm's trade in the table would
/// have kept building a composter for ever.
BlockId orientedJobSite(BlockId job, FaceDirection front) {
    return isFurnace(job) ? cookerAt(job, front, false) : job;
}

/// Whether `buildHouse` is the builder for a design.
///
/// Three designs have builders of their own, and that fact was spelled out
/// inline at each place that needed it. It decides more than which function
/// runs: `buildHouse` is also the only builder that lays a bed, a torch or a
/// **loot chest**, so "does this build as a house?" is the same question as
/// "can this building hold a chest at all?".
///
/// **No catch-all**, so a design added later has to answer it. The dispatch at
/// the bottom of `generateInto` names the same three and falls through to
/// `buildHouse`; the two agree by construction, and a fourth builder has to be
/// added in both places.
constexpr bool buildsAsHouse(Design design) {
    switch (design) {
    case Design::TownCentre:
    case Design::Farm:
    case Design::AnimalPen:
    case Design::Count:
        return false;
    case Design::SmallHouseA:
    case Design::SmallHouseB:
    case Design::SmallHouseC:
    case Design::MediumHouse:
    case Design::LargeHouse:
    case Design::Workshop:
    case Design::Library:
    case Design::Temple:
        break;
    }
    return true;
}

/// Tallest walls any design stands on, floor to roof base. The section gate in
/// `generateInto` is quoted against it.
constexpr int kMaxWallHeight = 7;

/// How tall a design's walls stand.
///
/// Read by the builder *and* by the solver's height bounds, so the two cannot
/// disagree about how far above its floor a building reaches. `Design::Count`
/// and the three designs with builders of their own fall through to zero, which
/// is what the assert below catches for anything else.
constexpr int wallHeightOf(Design design, std::uint32_t style) {
    switch (design) {
    case Design::SmallHouseA:
    case Design::SmallHouseB:
    case Design::Workshop:
        return 4;
    case Design::SmallHouseC:
        return 4 + static_cast<int>(style & 1u);
    case Design::MediumHouse:
        return 5;
    case Design::LargeHouse:
        return 7;
    case Design::Library:
        return 6;
    case Design::Temple:
        return 7;
    case Design::TownCentre:
    case Design::Farm:
    case Design::AnimalPen:
    case Design::Count:
        break;
    }
    return 0;
}

/// Every design `buildHouse` can be handed has walls, and none is taller than
/// the headroom the section gate allows. Add a dwelling without a row above and
/// it comes out as a floor with a roof lying on it; this fails the build first.
constexpr bool everyHouseHasWalls() {
    for (int d = 0; d < static_cast<int>(Design::Count); ++d) {
        const Design design = static_cast<Design>(d);
        if (!buildsAsHouse(design)) {
            continue;
        }
        for (std::uint32_t style = 0; style < 2u; ++style) {
            const int height = wallHeightOf(design, style);
            if (height < 4 || height > kMaxWallHeight) {
                return false;
            }
        }
    }
    return true;
}

static_assert(everyHouseHasWalls(), "a design that buildHouse builds has no wall height");

/// How far above its own floor a building can write.
///
/// The deepest of the three is the headroom `foundation` clears inside the
/// walls, `wallHeight + 4`; the gable's topmost layer and the chimney both stop
/// one lower, and the snow that settles on a roof adds one. **The section gate
/// is quoted against this**, so a roof that reaches past it is a roof sheared
/// flat at y 32 or y 64 in a ninety-six block world.
constexpr int kBuildingHeadroom = kMaxWallHeight + 5;

static_assert(kBuildingHeadroom >= kSquareClearance + 1,
              "the town square clears more headroom than the gate admits");

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct Footprint {
    int minX;
    int minZ;
    int width;
    int depth;
};

bool overlaps(const Footprint& a, const Footprint& b, int margin) {
    return a.minX - margin < b.minX + b.width && b.minX - margin < a.minX + a.width &&
           a.minZ - margin < b.minZ + b.depth && b.minZ - margin < a.minZ + a.depth;
}

/// The footprint a design occupies, before rotation. Width runs along X.
///
/// **No catch-all returning a real size.** A `default:` here would have handed
/// every design added later a plausible five-by-five hut, which builds, passes
/// validation and is wrong everywhere; the assert below fails the build instead.
constexpr Footprint sizeOf(Design design) {
    switch (design) {
    case Design::TownCentre:
        return {0, 0, 9, 9};
    case Design::SmallHouseA:
        return {0, 0, 5, 6};
    case Design::SmallHouseB:
        return {0, 0, 6, 5};
    case Design::SmallHouseC:
        return {0, 0, 5, 5};
    case Design::MediumHouse:
        return {0, 0, 7, 7};
    case Design::LargeHouse:
        return {0, 0, 9, 7};
    case Design::Workshop:
        return {0, 0, 7, 6};
    case Design::Library:
        return {0, 0, 7, 8};
    case Design::Temple:
        return {0, 0, 5, 9};
    case Design::Farm:
        return {0, 0, 9, 7};
    case Design::AnimalPen:
        // ⚠️ Shrinking this re-derives `kMaxLivestock` in `Village.hpp`, which
        // counts how many 7-wide pen centres fit across a chunk. A narrower pen
        // fits more of them and the bound goes too low, which costs animals
        // silently rather than loudly. `livestockIn` carries the assert.
        return {0, 0, 7, 7};
    case Design::Count:
        break;
    }
    return {0, 0, 0, 0};
}

/// Every design has a row, and every row leaves a room a bed and a doorway can
/// both fit in. Five is the floor because the interior is `side - 2`: at four
/// there is a single interior column and the door opens onto the bed.
constexpr bool everyDesignHasASize() {
    for (int d = 0; d < static_cast<int>(Design::Count); ++d) {
        const Footprint shape = sizeOf(static_cast<Design>(d));
        if (shape.width < 5 || shape.depth < 5) {
            return false;
        }
    }
    return true;
}

static_assert(everyDesignHasASize(),
              "add an enumerator to Design without a row in sizeOf and it comes out zero-sized");

/// Largest interior any design can have, read off `sizeOf` rather than written
/// down a second time. Sizes the claim map every fitting asks before it builds.
constexpr int largestDesignSide() {
    int largest = 0;
    for (int d = 0; d < static_cast<int>(Design::Count); ++d) {
        const Footprint shape = sizeOf(static_cast<Design>(d));
        largest = std::max(largest, std::max(shape.width, shape.depth));
    }
    return largest;
}

constexpr int kMaxInteriorSide = largestDesignSide() - 2;

/// How badly a footprint disagrees with the ground it would stand on.
///
/// **One owner for the question every plot asks.** The town square used to ask
/// nothing at all — it was the single building placed with no fill or cut test
/// — so a square could be laid over a seven-block drop the foundation cannot
/// pour, leaving the well, the stalls and the bell hanging over a hole in the
/// hillside. Same measurement, two callers.
struct SiteFit {
    /// Blocks of platform the foundation would have to pour under the floor.
    int worstFill = 0;
    /// Blocks of hill that would have to come out above it.
    int worstCut = 0;
    /// Any column at or below the waterline.
    bool wet = false;
};

SiteFit measureSite(std::uint32_t seed, const Footprint& spot, int floorY) {
    SiteFit fit;
    for (int cz = 0; cz < spot.depth; ++cz) {
        for (int cx = 0; cx < spot.width; ++cx) {
            const int h = surfaceHeightAt(seed, spot.minX + cx, spot.minZ + cz);
            fit.worstFill = std::max(fit.worstFill, floorY - 1 - h);
            fit.worstCut = std::max(fit.worstCut, h - (floorY - 1));
            fit.wet = fit.wet || h <= kSeaLevel;
        }
    }
    return fit;
}

/// Every column a street covers, in one place.
///
/// The builder walks these to lay the paving and the solver walks them to work
/// out how high and how low the village reaches. Two walks that have to agree
/// exactly, so there is one of them.
template <typename Fn>
void forEachRoadColumn(const Road& road, Fn&& body) {
    const int dx = road.x1 == road.x0 ? 0 : (road.x1 > road.x0 ? 1 : -1);
    const int dz = road.z1 == road.z0 ? 0 : (road.z1 > road.z0 ? 1 : -1);
    const int length = std::max(std::abs(road.x1 - road.x0), std::abs(road.z1 - road.z0));

    for (int t = 0; t <= length; ++t) {
        for (int side = -1; side <= 1; ++side) {
            body(road.x0 + dx * t + dz * side, road.z0 + dz * t - dx * side);
        }
    }
}

/// Whether a design is somewhere a villager **lives** — which is what decides
/// whether it produces a resident and whether it may be dealt an indoor trade.
///
/// **No catch-all.** A `default: return false` here is the quietest possible
/// bug: a dwelling added later comes out with a bed, a door and nobody in it,
/// and nothing about that fails a build, a validation pass or a soak. Every
/// enumerator is named, so the compiler asks the question instead.
///
/// The temple is deliberately out. `buildHouse` builds it and gives it a bed
/// like every other room it builds, but nobody is generated for it — a village
/// keeps more beds than villagers, which is the reference's own arrangement and
/// what lets a population grow into one.
constexpr bool isDwelling(Design design) {
    switch (design) {
    case Design::SmallHouseA:
    case Design::SmallHouseB:
    case Design::SmallHouseC:
    case Design::MediumHouse:
    case Design::LargeHouse:
    case Design::Workshop:
    case Design::Library:
        return true;
    case Design::TownCentre:
    case Design::Temple:
    case Design::Farm:
    case Design::AnimalPen:
    case Design::Count:
        break;
    }
    return false;
}

/// Every building that can be *dealt* a job block is a building `buildHouse`
/// builds — and `buildHouse` is the only builder that puts a chest down.
///
/// This is the "rule that did not travel" shape, asserted instead of watched
/// for. A trade dealt to the town centre or the pen would place its workstation
/// and then quietly place no chest, because those have builders of their own
/// and none of them contains the lines that write a `LootChest`. The four deal
/// sites in `solve` are the workshop, the library, the temple and the indoor
/// trade, and the first two are dwellings, so the set reduces to these.
///
/// **A drift alarm rather than a check of the tree as it stands, and it has to
/// be described as one** (corrected 2026-08-19). `isDwelling` plus the temple
/// is exactly `buildsAsHouse` today, so the body currently evaluates
/// `buildsAsHouse(d) && !buildsAsHouse(d)` and cannot fail: it fires only on a
/// *future* divergence between two sets that coincide now. That is a legitimate
/// thing for an assert to be. What was not legitimate is what this comment used
/// to claim beside it — that it also caught "widening the indoor-trade branch
/// in `solve` back past `isDwelling`". It does not and cannot: the expression
/// never reads `solve`, and a reader who believed it skipped re-checking the
/// one place the rule is actually applied.
///
/// **The single edit that fails this** is adding a design with a builder of its
/// own to `isDwelling` — `Design::AnimalPen`, say — or giving an existing
/// dwelling its own builder in `buildsAsHouse`.
constexpr bool everyDealtJobLandsInAHouse() {
    int dealt = 0;
    for (int d = 0; d < static_cast<int>(Design::Count); ++d) {
        const Design design = static_cast<Design>(d);
        if (!isDwelling(design) && design != Design::Temple) {
            continue;
        }
        if (!buildsAsHouse(design)) {
            return false;
        }
        ++dealt;
    }
    // The count pin `everyJobSiteIsClassified` has and this one lacked: seven
    // dwellings and the temple. Without it, narrowing `isDwelling` to nothing
    // leaves the loop finding no design at all and passing in silence — which
    // is the failure this whole assert exists to make loud.
    return dealt == 8;
}

static_assert(everyDealtJobLandsInAHouse(),
              "either a design dealt a job block is built by something that lays no chest, or the "
              "count of designs this evaluates has moved - seven dwellings and the temple - and "
              "the second is a prompt to re-read the first, not to widen the number");

/// The farm is the one exception to the rule above: it carries a workstation
/// and `buildFarm` lays no chest. That is correct only for as long as the
/// composter has no table, so **the single edit that fails this is giving the
/// farmer one** — at which point the farm needs a chest of its own rather than
/// a silently dropped table.
static_assert(jobChestTable(BlockId::Composter0) < 0,
              "the farm carries a workstation but buildFarm places no chest");

/// How the plot table is weighted. Small houses dominate, the big set pieces
/// punctuate — the same lopsided shape the reference's own pools have, and the
/// reason a village reads as houses with a library in it rather than as one of
/// each.
///
/// **The workshop share is deliberately higher than the reference's.** Vanilla
/// gives each of eleven workstation buildings weight 2 out of about 104, which
/// over a twenty-piece village yields two or three trades. Ours are eight
/// pieces, so the same *share* would leave most villages with no job site at
/// all — measured, one in fifteen came out with none. The absolute count is
/// what matters, so the share follows the village size.
struct PlotWeight {
    Design design;
    int weight;
};

constexpr PlotWeight kPlotWeights[] = {
    {Design::SmallHouseA, 8}, {Design::SmallHouseB, 8}, {Design::SmallHouseC, 7},
    {Design::MediumHouse, 6}, {Design::LargeHouse, 3},  {Design::Workshop, 12},
    {Design::Library, 3},     {Design::Temple, 2},      {Design::Farm, 5},
    {Design::AnimalPen, 3},
};

/// Whether a weight table offers **every** `Design` exactly once, `TownCentre`
/// excepted because it is placed outright rather than rolled for.
///
/// Reference-to-array rather than pointer-and-count, so the bound is in the
/// function's type and the caller cannot disagree with it.
template <std::size_t N>
constexpr bool everyDesignIsRolled(const PlotWeight (&rows)[N]) {
    for (int d = 0; d < static_cast<int>(Design::Count); ++d) {
        const auto design = static_cast<Design>(d);
        int found = 0;
        for (const PlotWeight& row : rows) {
            found += row.design == design ? 1 : 0;
        }
        if (found != (design == Design::TownCentre ? 0 : 1)) {
            return false;
        }
    }
    return true;
}

// **A `Design` with no row here is a building the game contains and can never
// generate**, and nothing else in the tree says so: `pickDesign` walks this
// table alone, so an unweighted design produces no warning, no assert and no
// visible gap short of counting buildings across a hundred villages. That is
// bug shape #15 in its quietest form, and this file has eleven designs to lose
// one from.
//
// A count would have been the cheap version and it is not enough: it passes a
// table that names one design twice and drops another, which is precisely what
// a copy-paste row does.
static_assert(everyDesignIsRolled(kPlotWeights),
              "a Design enumerator is missing from kPlotWeights, duplicated in it, or TownCentre "
              "has been given a weight - pickDesign draws from this table and nothing else, so "
              "the first two make a building type unreachable and the third puts a second bell "
              "in a village");

// The control, because a predicate that cannot say no proves nothing. This
// table fails for both of the reasons the real one must not: it names
// `TownCentre`, and it is missing everything else.
constexpr PlotWeight kNotAPlotTable[] = {{Design::TownCentre, 1}};
static_assert(!everyDesignIsRolled(kNotAPlotTable),
              "everyDesignIsRolled must reject a table that weights TownCentre and omits the "
              "rest, or the assert above is vacuous");

Design pickDesign(Roll& roll) {
    int total = 0;
    for (const PlotWeight& row : kPlotWeights) {
        total += row.weight;
    }
    int pick = roll.range(total);
    for (const PlotWeight& row : kPlotWeights) {
        pick -= row.weight;
        if (pick < 0) {
            return row.design;
        }
    }
    return Design::SmallHouseA;
}

struct Step {
    int dx;
    int dz;
};

/// Which way each of the four streets runs, in the order the arm loop walks
/// them. This is the authority; the step table below is derived from it.
constexpr FaceDirection kOutward[4] = {FaceDirection::NegZ, FaceDirection::PosX,
                                       FaceDirection::PosZ, FaceDirection::NegX};

/// The one place in this file that turns a horizontal `FaceDirection` into a
/// step on the grid.
///
/// `Unknown` deliberately yields a zero step rather than a plausible-looking
/// direction, because a zero step is exactly what the assert below catches and
/// a plausible one is exactly what it could not.
constexpr Step stepFor(FaceDirection direction) {
    switch (direction) {
    case FaceDirection::PosX:
        return {1, 0};
    case FaceDirection::NegX:
        return {-1, 0};
    case FaceDirection::PosZ:
        return {0, 1};
    case FaceDirection::NegZ:
        return {0, -1};
    case FaceDirection::Unknown:
        break;
    }
    return {0, 0};
}

/// **Derived from `kOutward`, not written out beside it.**
///
/// These were two parallel arrays indexed by the same `arm` with *nothing*
/// binding them - no assert, no shared row type, not even a comment saying they
/// were a pair. Reorder or edit one and the streets keep running the old way
/// while every direction read off the other names the new one, on a clean
/// build. That is bug shape #5, a derivation applied to one of a pair and not
/// the other, and it is the shape that mirrored every shaped block in the game
/// for four milestones.
///
/// Deriving makes the disagreement inexpressible, which is a rung above
/// asserting it: there is now one statement of the compass order and one
/// function that reads it.
///
/// It also closed a smaller hole. `kOutward` had **no reader at all** - one
/// occurrence in the file, its own declaration - so it was a table that could
/// have said anything at all without any effect, sitting one index away from a
/// table that steers every street in every village. An unread `constexpr` array
/// at namespace scope draws no warning at `/W4`.
constexpr Step kSteps[4] = {stepFor(kOutward[0]), stepFor(kOutward[1]), stepFor(kOutward[2]),
                            stepFor(kOutward[3])};

/// Whether a step table gives genuinely different directions and no zero.
///
/// Reference-to-array so the bound travels with the argument.
template <std::size_t N>
constexpr bool areDistinctSteps(const Step (&steps)[N]) {
    for (std::size_t a = 0; a < N; ++a) {
        if (steps[a].dx == 0 && steps[a].dz == 0) {
            return false;
        }
        for (std::size_t b = a + 1; b < N; ++b) {
            if (steps[a].dx == steps[b].dx && steps[a].dz == steps[b].dz) {
                return false;
            }
        }
    }
    return true;
}

// A repeated direction is not a subtle fault. The arm loop lays a street, then
// hangs plots off it at a fixed pitch; two arms sharing a direction would build
// the same street twice, roll a second set of plots along it, and every one of
// them would clash against the first set and be dropped - so the village comes
// out with three arms and one bare road.
static_assert(areDistinctSteps(kSteps),
              "the four village streets must run four different ways - a duplicate builds one "
              "street twice and loses an arm, and a zero step (which is what "
              "FaceDirection::Unknown yields) collapses an arm into the town square");

// The control, and it fails for both of the reasons the real table must not.
constexpr Step kNotDistinctSteps[3] = {{1, 0}, {1, 0}, {0, 0}};
static_assert(!areDistinctSteps(kNotDistinctSteps),
              "areDistinctSteps must reject a repeated step and a zero one, or the assert above "
              "is vacuous");

/// The stream a cell's whole layout is drawn from.
using LayoutRoll = Roll;

LayoutRoll layoutRollFor(std::uint32_t seed, int cellX, int cellZ) {
    return LayoutRoll{noise::hash2D(seed ^ kLayoutSalt, cellX, cellZ)};
}

/// Where in its cell a village stands, jittered with the separation kept clear
/// at the edges so two villages in neighbouring cells can never end up next to
/// each other.
///
/// **`plansNear` asks this before it solves anything**, and it used to ask it by
/// keeping a character-for-character copy of these three lines. They agreed, but
/// the copy is the only thing deciding whether a chunk looks at a village at
/// all: insert one draw ahead of it in `solve` and chunks start rejecting a
/// village that exists, which is a village with arbitrary chunks blank.
struct Origin {
    int x;
    int z;
};

Origin originOf(LayoutRoll& roll, int cellX, int cellZ) {
    const int span = kCellBlocks - kSeparationBlocks;
    return {cellX * kCellBlocks + kSeparationBlocks / 2 + roll.range(span),
            cellZ * kCellBlocks + kSeparationBlocks / 2 + roll.range(span)};
}

/// Solves one village from nothing but its grid cell.
///
/// **This function never sees a chunk, and it must stay that way.** Thirty
/// chunks each running it reach the identical plan, which is the whole reason a
/// village can span more ground than any one of them holds.
///
/// The invariant that buys that is narrower than "every draw happens in a fixed
/// order whatever the outcome", which is what this comment used to claim and
/// which is not true of the code below: the two `roll.unit()` draws that decide
/// whether a resident is a baby or a nitwit sit inside
/// `isDwelling(design) && plan.residentCount < kMaxResidents`, downstream of
/// four `continue`s that skip a plot. **Draws may be skipped, and are.**
///
/// What must never happen is a draw skipped on something *chunk-dependent*.
/// Every early-out here is a function of the seed and the cell alone — the
/// biome, the terrain, the roll itself — so all thirty chunks skip the same
/// ones. Add a test against a chunk bound, a loaded neighbour or a clock and
/// two chunks would disagree about the rest of the stream: not a missing
/// villager, a house cut in half at the border.
///
/// The distinction is worth stating precisely because a reader who believes the
/// old wording is guarding on the wrong thing. Deleting a conditional draw
/// would not make this safer, and adding an unconditional one that reads a
/// chunk would not be caught by it.
Plan solve(std::uint32_t seed, int cellX, int cellZ) {
    Plan plan;

    LayoutRoll roll = layoutRollFor(seed, cellX, cellZ);

    const Origin origin = originOf(roll, cellX, cellZ);
    plan.originX = origin.x;
    plan.originZ = origin.z;

    const BiomeSample sample = sampleBiome(seed, plan.originX, plan.originZ);
    plan.type = biomeInfo(sample.dominant).villageType;
    if (plan.type == VillageType::None) {
        plan.reject = Reject::Biome;
        return plan;
    }

    // **The floor is the first air cell, not the ground.** `surfaceHeightAt`
    // answers with the topmost *solid* block, so every other building here adds
    // the one — and the square did not, which put its paving, its well, its
    // stalls, its bell and its golem one block under every street that reaches
    // them. A step down into the plaza, in every village in the world.
    const int ground = surfaceHeightAt(seed, plan.originX, plan.originZ);
    if (ground <= kSeaLevel + 2) {
        plan.reject = Reject::Water;
        return plan;
    }
    plan.centreY = ground + 1;
    if (surfaceCarvedAt(seed, plan.originX, plan.originZ)) {
        plan.reject = Reject::Carved;
        return plan;
    }

    // Roughness. Twenty-five probes over the inner half of the footprint: too
    // few and a village straddles a ridge, too many and every candidate costs
    // real noise work for a question usually answered by the first two.
    int lowest = ground;
    int highest = ground;
    for (int pz = -2; pz <= 2; ++pz) {
        for (int px = -2; px <= 2; ++px) {
            const int h = surfaceHeightAt(seed, plan.originX + px * 11, plan.originZ + pz * 11);
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
        }
    }
    if (highest - lowest > kSiteRoughness) {
        plan.reject = Reject::Rough;
        return plan;
    }
    if (lowest <= kSeaLevel + 1) {
        plan.reject = Reject::Water;
        return plan;
    }

    // The town centre, square on the origin, sized from the one table that owns
    // footprints rather than from a nine written down again here.
    const Footprint centreSize = sizeOf(Design::TownCentre);
    Footprint centre{plan.originX - centreSize.width / 2, plan.originZ - centreSize.depth / 2,
                     centreSize.width, centreSize.depth};

    // **The square gets the same site test every plot gets.** It was the one
    // building placed with none, guarded only by a lattice whose twenty-five
    // probes sit at ±11 and ±22 — exactly one of which lands inside the square.
    // The two limits differ because the two remedies do: `foundation` can pour
    // `kMaxFill` and no more, and it clears `kSquareClearance` above the paving.
    const SiteFit centreFit = measureSite(seed, centre, plan.centreY);
    if (centreFit.wet) {
        plan.reject = Reject::Water;
        return plan;
    }
    if (centreFit.worstFill > kMaxFill || centreFit.worstCut > kSquareClearance) {
        plan.reject = Reject::Rough;
        return plan;
    }

    // **Its `style` carries the four market stalls' trades**, which is what puts
    // a row of stalls round the square: the reference's own `meeting_point_2`
    // and `_4` are stalls, and they are the reason a village has trades in it at
    // all rather than only in whichever workshops the plot roll happened to
    // produce.
    const std::uint32_t stalls = roll.next();
    plan.buildings[plan.buildingCount++] = {centre.minX,          centre.minZ, centre.width,
                                            centre.depth,         plan.centreY, Design::TownCentre,
                                            FaceDirection::NegZ,  BlockId::Air, stalls};

    // The vertical extent, accumulated from **what is actually placed**. It used
    // to be read off the probe lattice, which reaches ±22 while the streets
    // reach ±25 and the buildings ±32, and each building takes its floor from
    // its own door column. The gate below it skips whole thirty-two block
    // sections, so a floor five blocks above every probe lost the top of its
    // roof to a horizontal plane at y 32 or y 64.
    int lowestWrite = plan.centreY - kMaxFill - 2;
    int highestWrite = plan.centreY + kBuildingHeadroom;
    const auto reaches = [&](int low, int high) {
        lowestWrite = std::min(lowestWrite, low);
        highestWrite = std::max(highestWrite, high);
    };

    Footprint taken[kMaxBuildings];
    int takenCount = 0;
    taken[takenCount++] = centre;

    // Four streets, one per compass direction. Lengths are rolled up front so
    // the roll order cannot depend on whether a plot succeeded.
    int armLength[4];
    for (int arm = 0; arm < 4; ++arm) {
        armLength[arm] = kArmMin + roll.range(kArmSpan);
    }

    // Which job site each workshop gets, dealt round-robin from a rolled start
    // so a village gets a spread of trades rather than five composters.
    int jobCursor = roll.range(kJobSiteCount);

    for (int arm = 0; arm < 4; ++arm) {
        const Step step = kSteps[arm];
        const int length = armLength[arm];
        const int endX = plan.originX + step.dx * length;
        const int endZ = plan.originZ + step.dz * length;
        if (plan.roadCount < kMaxRoads) {
            const Road road{plan.originX, plan.originZ, endX, endZ};
            plan.roads[plan.roadCount++] = road;
            forEachRoadColumn(road, [&](int x, int z) {
                const int columnGround = surfaceHeightAt(seed, x, z);
                if (columnGround <= kSeaLevel) {
                    // A bridge, at the waterline rather than at the ground.
                    reaches(kSeaLevel, kSeaLevel + 5);
                    return;
                }
                // Paving one below the surface, headroom three above it, and
                // the snow that settles on the street one above that.
                reaches(columnGround - 1, columnGround + 4);
            });
        }

        // Plots hang off the street at a fixed pitch, alternating sides. The
        // road is three wide, so a plot starts two cells out from its centre,
        // and the first ring starts far enough along that a nine-wide building
        // centred on it still clears the town square.
        for (int along = 9; along <= length - 2; along += 6) {
            for (int side = 0; side < 2; ++side) {
                const int outward = side == 0 ? 1 : -1;
                // The perpendicular direction, in world axes.
                const int px = step.dz * outward;
                const int pz = -step.dx * outward;

                const Design design = pickDesign(roll);
                const std::uint32_t style = roll.next();
                const bool leaveEmpty = roll.unit() < 0.12f;

                if (leaveEmpty || plan.buildingCount >= kMaxBuildings ||
                    takenCount >= kMaxBuildings) {
                    continue;
                }

                Footprint shape = sizeOf(design);
                // Turn the footprint so its depth runs away from the street.
                if (px != 0) {
                    std::swap(shape.width, shape.depth);
                }

                // The cell on the road the door will open onto.
                const int doorX = plan.originX + step.dx * along + px * 2;
                const int doorZ = plan.originZ + step.dz * along + pz * 2;

                // Anchored so the wall facing the street sits one cell out from
                // the road edge, centred on the plot.
                const int frontX = doorX + px;
                const int frontZ = doorZ + pz;
                Footprint spot{};
                if (px > 0) {
                    spot = {frontX, frontZ - shape.depth / 2, shape.width, shape.depth};
                } else if (px < 0) {
                    spot = {frontX - shape.width + 1, frontZ - shape.depth / 2, shape.width,
                            shape.depth};
                } else if (pz > 0) {
                    spot = {frontX - shape.width / 2, frontZ, shape.width, shape.depth};
                } else {
                    spot = {frontX - shape.width / 2, frontZ - shape.depth + 1, shape.width,
                            shape.depth};
                }

                // The whole plot has to stay inside the stated radius, or a
                // chunk beyond it would never look and would drop a wall.
                const int reachX = std::max(std::abs(spot.minX - plan.originX),
                                            std::abs(spot.minX + spot.width - plan.originX));
                const int reachZ = std::max(std::abs(spot.minZ - plan.originZ),
                                            std::abs(spot.minZ + spot.depth - plan.originZ));
                if (std::max(reachX, reachZ) > kMaxFootprintReach) {
                    continue;
                }

                bool clash = false;
                for (int i = 0; i < takenCount; ++i) {
                    if (overlaps(spot, taken[i], 2)) {
                        clash = true;
                        break;
                    }
                }
                if (clash) {
                    continue;
                }

                // The floor sits level with the street outside the door, which
                // is what puts the doorstep where a villager can walk over it.
                const int floorY = surfaceHeightAt(seed, doorX, doorZ) + 1;

                const SiteFit fit = measureSite(seed, spot, floorY);
                if (fit.wet || fit.worstFill > kMaxFill || fit.worstCut > kMaxCut) {
                    continue;
                }

                BlockId workstation = BlockId::Air;
                if (design == Design::Workshop) {
                    workstation = kJobSites[jobCursor % kJobSiteCount];
                    ++jobCursor;
                } else if (design == Design::Library) {
                    workstation = BlockId::Lectern;
                } else if (design == Design::Farm) {
                    workstation = BlockId::Composter0;
                } else if (design == Design::Temple) {
                    workstation = BlockId::BrewingStand;
                } else if (isDwelling(design) && (style & kTradeIndoors) == kTradeIndoors) {
                    // Half of the ordinary houses keep a trade indoors, which is
                    // what stops a small village having one profession in it.
                    // Ours are eight pieces where the reference's are twenty, so
                    // the *share* has to be higher for the absolute count to
                    // come out the same. See `kTradeIndoors`.
                    //
                    // **`isDwelling` is what makes the deal honest.** An animal
                    // pen reached this branch too and was dealt a job site the
                    // pen builder never places, so the trade was taken off the
                    // cursor and dropped on the floor — and where the draw was
                    // a grindstone or a smithing table, the village lost the one
                    // chest the reference guarantees along with it.
                    workstation = kJobSites[jobCursor % kJobSiteCount];
                    ++jobCursor;
                }

                // The door faces back at the street.
                const FaceDirection facing =
                    px > 0   ? FaceDirection::NegX
                    : px < 0 ? FaceDirection::PosX
                    : pz > 0 ? FaceDirection::NegZ
                             : FaceDirection::PosZ;

                plan.buildings[plan.buildingCount++] = {spot.minX,   spot.minZ, spot.width,
                                                        spot.depth,  floorY,    design,
                                                        facing,      workstation, style};
                taken[takenCount++] = spot;
                // The foundation pours `kMaxFill` under the floor slab and the
                // walls, roof and cleared headroom stand `kBuildingHeadroom`
                // over it.
                reaches(floorY - 1 - kMaxFill - 1, floorY + kBuildingHeadroom);

                if (isDwelling(design) && plan.residentCount < kMaxResidents) {
                    Resident& who = plan.residents[plan.residentCount++];
                    who.x = spot.minX + spot.width / 2;
                    who.z = spot.minZ + spot.depth / 2;
                    who.y = floorY;
                    who.baby = roll.unit() < 0.05f;
                    who.nitwit = !who.baby && roll.unit() < 0.10f;
                }
            }
        }

        // Street furniture, on alternating sides. Every village gets lamp
        // posts; a taiga village gets campfires among them.
        for (int along = 5; along <= length - 2; along += 9) {
            if (plan.decorCount >= kMaxDecor) {
                break;
            }
            const int site = along / 9;
            const int outward = site % 2 == 0 ? 1 : -1;
            Decor& piece = plan.decor[plan.decorCount++];
            piece.x = plan.originX + step.dx * along + step.dz * outward * 2;
            piece.z = plan.originZ + step.dz * along - step.dx * outward * 2;
            // **Campfires generate in taiga and snowy taiga villages**, and in
            // *Bedrock* the snowy taiga half is Bedrock's alone - Java has it in
            // plain taiga only (minecraft.wiki/w/Campfire, "Campfires can
            // generate in taiga and snowy taiga[Bedrock Edition only] villages",
            // read 2026-08-19). Both of those biomes carry
            // `VillageType::Taiga`, so the reference's rule comes out as a test
            // against one enumerator with nothing left over, and `Snowy` -
            // which is the reference's `ice` type and belongs to Snowy Plains -
            // correctly gets none.
            //
            // **Derived from the layout rather than rolled**, deliberately: a
            // `roll` here would consume from the same stream that lays out the
            // three arms after this one, so every plot, design and resident in
            // them would shift. This way a non-taiga village is bit-identical
            // to what it generated before campfires existed, and the change is
            // confined to the biomes the reference puts them in.
            //
            // `+ arm` staggers them, so the four streets do not all light their
            // first, fourth and seventh posts.
            const bool campfire = plan.type == VillageType::Taiga && (site + arm) % 3 == 1;
            piece.kind = campfire ? kDecorCampfire : kDecorLampPost;
            // A plinth on the ground, three fence sections, the light on top,
            // and room for snow above that. A campfire is shorter and fits
            // inside the same envelope.
            const int pieceGround = surfaceHeightAt(seed, piece.x, piece.z);
            reaches(pieceGround, pieceGround + 5);
        }
    }

    // Bounds. X and Z are the stated radius, which the plot cap and every
    // claim above are asserted against. **Y is accumulated from what is
    // actually placed** — a roof, a street or a foundation outside it would be
    // sheared off at a section boundary by the gate in `generateInto`.
    plan.minX = plan.originX - kReach;
    plan.maxX = plan.originX + kReach;
    plan.minZ = plan.originZ - kReach;
    plan.maxZ = plan.originZ + kReach;
    plan.minY = std::min(lowestWrite, lowest - kMaxFill - 2);
    // The probe lattice with its old margin is kept as a floor under the
    // answer, not as the answer: snow settles on plain terrain anywhere inside
    // the radius, and the probes only reach ±22 of it.
    plan.maxY = std::max(highestWrite, highest + 16);
    plan.valid = true;
    return plan;
}

// ---------------------------------------------------------------------------
// Writing blocks
// ---------------------------------------------------------------------------

/// Clips one village against one chunk. Everything below writes through this,
/// which is what lets the same village be built by every chunk it touches.
struct Writer {
    Chunk& chunk;
    ChunkCoord coord;
    std::uint32_t seed;

    void put(int x, int y, int z, BlockId block, bool onlyIntoAir = false) const {
        const int lx = x - coord.x * Chunk::kSize;
        const int ly = y - coord.y * Chunk::kSize;
        const int lz = z - coord.z * Chunk::kSize;
        if (!Chunk::contains(lx, ly, lz)) {
            return;
        }
        if (onlyIntoAir && chunk.at(lx, ly, lz) != BlockId::Air) {
            return;
        }
        chunk.set(lx, ly, lz, block);
        chunk.setWaterlogged(lx, ly, lz, false);
    }

    void box(int x0, int y0, int z0, int x1, int y1, int z1, BlockId block,
             bool onlyIntoAir = false) const {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    put(x, y, z, block, onlyIntoAir);
                }
            }
        }
    }
};

/// A position-hashed roll, so a cosmetic choice cannot depend on the order the
/// cells happened to be visited in. **Every per-cell decision here uses this
/// rather than the layout stream**, which removes a whole class of seam.
float dress(std::uint32_t seed, int x, int z, std::uint32_t salt) {
    return noise::hashUnit2D(seed ^ (kDressSalt + salt), x, z);
}

/// Weathers a stone course: one block in ten becomes the palette's alternate.
/// The reference's `mossify_10_percent`, and it is most of why a village reads
/// as lived-in rather than as freshly stamped.
BlockId weathered(const Palette& palette, std::uint32_t seed, int x, int z) {
    return dress(seed, x, z, 11u) < 0.10f ? palette.stoneAlt : palette.stone;
}

constexpr Facing eaveFacing(int dx, int dz) {
    if (dx > 0) {
        return Facing::East;
    }
    if (dx < 0) {
        return Facing::West;
    }
    if (dz > 0) {
        return Facing::South;
    }
    return Facing::North;
}

/// A pitched roof over a rectangle, sloping down along the short axis. The two
/// ends are closed with a triangular gable of the wall material.
void gableRoof(const Writer& w, const Palette& palette, int x0, int z0, int x1, int z1, int base) {
    const int width = x1 - x0 + 1;
    const int depth = z1 - z0 + 1;
    const int stairs = stairFamilyOf(palette.roof);
    const int slabs = slabFamilyOf(palette.roof);
    const bool alongX = width >= depth;
    const int layers = ((alongX ? depth : width) + 1) / 2;

    for (int k = 0; k < layers; ++k) {
        const int y = base + k;
        if (alongX) {
            const int lo = z0 + k;
            const int hi = z1 - k;
            // One cell of overhang at each end, which is what stops a roof
            // reading as a lid sitting on a box.
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                w.put(x, y, lo, stairsAt(stairs, eaveFacing(0, -1), false));
                if (hi != lo) {
                    w.put(x, y, hi, stairsAt(stairs, eaveFacing(0, 1), false));
                }
            }
            if (hi - lo >= 2) {
                // Gable ends, and the hollow the roof encloses.
                for (int z = lo + 1; z <= hi - 1; ++z) {
                    w.put(x0, y, z, palette.wall);
                    w.put(x1, y, z, palette.wall);
                    for (int x = x0 + 1; x <= x1 - 1; ++x) {
                        w.put(x, y, z, BlockId::Air);
                    }
                }
            } else if (hi == lo) {
                for (int x = x0 - 1; x <= x1 + 1; ++x) {
                    w.put(x, y, lo, slabAt(slabs, false));
                }
            }
        } else {
            const int lo = x0 + k;
            const int hi = x1 - k;
            for (int z = z0 - 1; z <= z1 + 1; ++z) {
                w.put(lo, y, z, stairsAt(stairs, eaveFacing(-1, 0), false));
                if (hi != lo) {
                    w.put(hi, y, z, stairsAt(stairs, eaveFacing(1, 0), false));
                }
            }
            if (hi - lo >= 2) {
                for (int x = lo + 1; x <= hi - 1; ++x) {
                    w.put(x, y, z0, palette.wall);
                    w.put(x, y, z1, palette.wall);
                    for (int z = z0 + 1; z <= z1 - 1; ++z) {
                        w.put(x, y, z, BlockId::Air);
                    }
                }
            } else if (hi == lo) {
                for (int z = z0 - 1; z <= z1 + 1; ++z) {
                    w.put(lo, y, z, slabAt(slabs, false));
                }
            }
        }
    }
}

/// A flat roof with a parapet, which is what a desert village has instead.
void flatRoof(const Writer& w, const Palette& palette, int x0, int z0, int x1, int z1, int base) {
    const int slabs = slabFamilyOf(palette.roof);
    w.box(x0, base, z0, x1, base, z1, palette.roofFill);
    // A one-cell overhang all the way round, laid as slabs so the edge reads.
    for (int x = x0 - 1; x <= x1 + 1; ++x) {
        w.put(x, base, z0 - 1, slabAt(slabs, false));
        w.put(x, base, z1 + 1, slabAt(slabs, false));
    }
    for (int z = z0; z <= z1; ++z) {
        w.put(x0 - 1, base, z, slabAt(slabs, false));
        w.put(x1 + 1, base, z, slabAt(slabs, false));
    }
    // The parapet, broken at the corners so it looks built rather than extruded.
    for (int x = x0; x <= x1; ++x) {
        if ((x - x0) % 2 == 0) {
            w.put(x, base + 1, z0, palette.wallAlt);
            w.put(x, base + 1, z1, palette.wallAlt);
        }
    }
    for (int z = z0; z <= z1; ++z) {
        if ((z - z0) % 2 == 0) {
            w.put(x0, base + 1, z, palette.wallAlt);
            w.put(x1, base + 1, z, palette.wallAlt);
        }
    }
}

/// Fills the hole under a building and clears whatever the hill left inside it.
///
/// This is our stand-in for the reference's `beard_thin` and its visible support
/// platform at once. `beard_thin` modifies the density field before the surface
/// exists, which we have no lattice for; pouring a foundation afterwards is the
/// honest substitute and it is what the reference's platform does anyway.
/// **Circular rather than square**, which is Bedrock's own shape.
void foundation(const Writer& w, const Palette& palette, const Building& building, int clearance) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const float halfX = static_cast<float>(building.width) * 0.5f + 0.9f;
    const float halfZ = static_cast<float>(building.depth) * 0.5f + 0.9f;
    const float midX = static_cast<float>(x0 + x1) * 0.5f;
    const float midZ = static_cast<float>(z0 + z1) * 0.5f;

    for (int z = z0 - 2; z <= z1 + 2; ++z) {
        for (int x = x0 - 2; x <= x1 + 2; ++x) {
            const bool inside = x >= x0 && x <= x1 && z >= z0 && z <= z1;
            if (!inside) {
                const float u = (static_cast<float>(x) - midX) / halfX;
                const float v = (static_cast<float>(z) - midZ) / halfZ;
                if (u * u + v * v > 1.0f) {
                    continue;
                }
            }

            const int ground = surfaceHeightAt(w.seed, x, z);
            const int floorBase = building.floorY - 1;

            // **`surfaceHeightAt` names the *uncarved* top and a cave can have
            // taken it away.** The fill below stops on the ground because the
            // ground is what holds it up; over a cave mouth there is nothing
            // there, so stopping leaves the floor slab spanning a void — a
            // house you drop out of by breaking one block, and a village that
            // hangs in the roof of a cavern seen from below.
            //
            // This is not a new rule. `surfaceCarvedAt`'s own comment in
            // `TerrainGenerator.hpp` states it, `Structures.cpp` acts on it for
            // trees, and `snowOver` below acts on it for snow. `solve` asks it
            // of the origin column and of nothing else, so it never reached the
            // eight-odd plots a village lays around that origin. Measured at
            // seed 12 over 48 villages: **393 of 17330 footprint columns
            // carved, across 17 of 402 buildings and 4 of 48 villages.**
            //
            // The remedy is the fill budget that already exists. `support` is
            // the height the pour may stop at, and a carved column has none
            // within reach, so it takes the full `kMaxFill + 1` — which plugs
            // the mouth with a visible foundation and is what the reference
            // reaches by dropping each column of a piece onto real support.
            const bool carved = surfaceCarvedAt(w.seed, x, z);
            const int support = carved ? floorBase - kMaxFill - 1 : ground;

            // **A skirt column over a big drop is not filled at all.** The loop
            // below stops after `kMaxFill` blocks, so filling one anyway would
            // leave a seven-tall pillar hanging in the air beside the house —
            // which is exactly the floating structure the plot test is careful
            // to avoid inside the footprint.
            //
            // A carved skirt column falls out of this same test rather than
            // needing one of its own: `support` is a full budget below the
            // floor, so the drop reads as too big and the skirt is dropped. The
            // footprint is poured because a hole in a floor is worse than a
            // seven-block plinth; the apron round it is not, because there it is
            // the other way about.
            if (!inside && floorBase - support > kMaxFill) {
                continue;
            }
            for (int y = floorBase; y > std::max(support, floorBase - kMaxFill - 1); --y) {
                w.put(x, y, z, y == floorBase && !inside ? palette.path : palette.foundation, true);
            }

            // Clear what the hill left standing. Inside the walls that is the
            // whole room; outside it only the first cell, so a house cut into a
            // slope keeps its bank instead of sitting in a trench.
            //
            // **Outside, only where there is actually hill to remove.** The
            // skirt reaches two blocks past the footprint and plots along an arm
            // can sit one apart, so an unconditional clear let a house punch a
            // two-tall notch through its neighbour's wall — and which one lost
            // depended on the order the plots came out of the layout, which is
            // as good as random. Where the ground is already below this floor
            // there is nothing of the hill in those cells, so anything standing
            // there belongs to somebody else.
            if (!inside && ground < building.floorY) {
                continue;
            }
            const int top = inside ? building.floorY + clearance : building.floorY + 1;
            for (int y = building.floorY; y <= top; ++y) {
                w.put(x, y, z, BlockId::Air);
            }
        }
    }
}

/// Where a wall's door goes, as an offset along that wall.
int doorSlot(int wallLength, std::uint32_t style) {
    const int slot = wallLength / 2;
    return slot + ((style >> 3) & 1u) * ((wallLength >= 7) ? 1 : 0) - ((wallLength >= 9) ? 1 : 0);
}

/// The interior floor of a building, as a map of what is already spoken for.
///
/// **One owner for "is anything standing here?"** Every fitting used to know
/// its own corner and nothing else, and all four collisions this file has had
/// were the same shape: a cell hard-coded in one branch that another branch did
/// not know about. The workshop's counter was laid straight over the bed on two
/// of its four facings — a shop with an orphan half-bed on the floor and a
/// villager that can never sleep — a five-deep house facing `PosX` put its bed
/// in its own doorway, and a library facing `NegX` bricked its door up with
/// bookshelves. A fitting asks now instead of assuming, so a design added later
/// cannot reintroduce any of them.
struct FloorPlan {
    int x0 = 0;
    int z0 = 0;
    int width = 0;
    int depth = 0;
    bool spoken[kMaxInteriorSide * kMaxInteriorSide] = {};

    bool holds(int x, int z) const {
        return x >= x0 && z >= z0 && x < x0 + width && z < z0 + depth;
    }

    bool free(int x, int z) const { return holds(x, z) && !spoken[index(x, z)]; }

    void claim(int x, int z) {
        if (holds(x, z)) {
            spoken[index(x, z)] = true;
        }
    }

    std::size_t index(int x, int z) const {
        return static_cast<std::size_t>(x - x0) +
               static_cast<std::size_t>(z - z0) * static_cast<std::size_t>(kMaxInteriorSide);
    }
};

FloorPlan interiorOf(const Building& building) {
    return {building.minX + 1, building.minZ + 1, building.width - 2, building.depth - 2, {}};
}

/// The first free interior cell, swept from one corner.
///
/// `towardX` and `towardZ` name that corner: `+1` the high wall, `-1` the low
/// one. **This replaced three hand-written exceptions** — the large house's
/// chest stepping round its ladder, and the workshop's stepping round its
/// counter on two of four facings — with one sweep that lands on the identical
/// cells and cannot be caught out by the fourth case nobody thought of.
bool pickInterior(const FloorPlan& plan, int towardX, int towardZ, int& outX, int& outZ) {
    for (int dz = 0; dz < plan.depth; ++dz) {
        const int z = towardZ > 0 ? plan.z0 + plan.depth - 1 - dz : plan.z0 + dz;
        for (int dx = 0; dx < plan.width; ++dx) {
            const int x = towardX > 0 ? plan.x0 + plan.width - 1 - dx : plan.x0 + dx;
            if (plan.free(x, z)) {
                outX = x;
                outZ = z;
                return true;
            }
        }
    }
    return false;
}

/// Where a bed may lie: foot in one of the four interior corners, head one step
/// along one of that corner's two walls.
///
/// **A table rather than a branch per design.** The rows differ by which corner
/// and which way round and by nothing else, and the first row is where the bed
/// has always gone — so a room whose usual corner is free comes out exactly as
/// it did, and only the rooms that were losing their bed or blocking their own
/// door move.
struct BedSpot {
    /// -1 the `x0` wall, +1 the `x1` wall.
    int cornerX;
    int cornerZ;
    /// The head lies one step along Z from the foot, else one step along X.
    bool alongZ;
};

constexpr BedSpot kBedSpots[8] = {
    {1, -1, true},  {1, 1, true},  {-1, -1, true},  {-1, 1, true},
    {1, -1, false}, {1, 1, false}, {-1, -1, false}, {-1, 1, false},
};

/// The one house builder. Everything that differs between the seven dwelling
/// shapes is a number passed in or a bit of `style`.
void buildHouse(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int floorY = building.floorY;
    const std::uint32_t style = building.style;

    const int wallHeight = wallHeightOf(building.design, style);

    const bool stonePlinth = (style & 2u) != 0 || building.design == Design::Temple;
    const bool cornerPosts = (style & 4u) != 0 || building.design != Design::SmallHouseC;
    const int roofBase = floorY + wallHeight;

    foundation(w, palette, building, wallHeight + 4);

    // Floor, one below the walking surface, plus a course of the same under the
    // whole footprint so a room never opens onto a cave.
    w.box(x0, floorY - 1, z0, x1, floorY - 1, z1, palette.floor);

    // Walls.
    for (int y = floorY; y < roofBase; ++y) {
        const int rel = y - floorY;
        BlockId course = palette.wall;
        if (stonePlinth && rel == 0) {
            course = palette.stone;
        } else if (rel == wallHeight - 1 && (style & 8u) != 0) {
            course = palette.wallAlt;
        }
        for (int x = x0; x <= x1; ++x) {
            const BlockId here = (stonePlinth && rel == 0) ? weathered(palette, w.seed, x, z0)
                                                           : course;
            w.put(x, y, z0, here);
            w.put(x, y, z1, (stonePlinth && rel == 0) ? weathered(palette, w.seed, x, z1) : course);
        }
        for (int z = z0 + 1; z <= z1 - 1; ++z) {
            const BlockId here = (stonePlinth && rel == 0) ? weathered(palette, w.seed, x0, z)
                                                           : course;
            w.put(x0, y, z, here);
            w.put(x1, y, z, (stonePlinth && rel == 0) ? weathered(palette, w.seed, x1, z) : course);
        }
        // Interior air, which also carves whatever the hill left behind.
        w.box(x0 + 1, y, z0 + 1, x1 - 1, y, z1 - 1, BlockId::Air);
    }

    // Corner posts, which is what makes a plank box read as a framed building.
    if (cornerPosts) {
        for (int y = floorY; y < roofBase; ++y) {
            w.put(x0, y, z0, palette.post);
            w.put(x1, y, z0, palette.post);
            w.put(x0, y, z1, palette.post);
            w.put(x1, y, z1, palette.post);
        }
    }

    // Windows: glass panes at eye height, spaced so they never land on a corner.
    const BlockId panes = paneAt(0);
    const int windowY = floorY + 1 + static_cast<int>((style >> 4) & 1u);
    const int pitch = 2 + static_cast<int>((style >> 5) & 1u);
    for (int x = x0 + 2; x <= x1 - 2; x += pitch) {
        w.put(x, windowY, z0, panes);
        w.put(x, windowY, z1, panes);
    }
    for (int z = z0 + 2; z <= z1 - 2; z += pitch) {
        w.put(x0, windowY, z, panes);
        w.put(x1, windowY, z, panes);
    }
    if (wallHeight >= 6) {
        for (int x = x0 + 2; x <= x1 - 2; x += pitch) {
            w.put(x, windowY + 3, z0, panes);
            w.put(x, windowY + 3, z1, panes);
        }
    }

    // The door. Its facing is the outward normal of the wall it sits in, which
    // is what `facingToward` would have produced for someone standing outside.
    int doorX = 0;
    int doorZ = 0;
    switch (building.facing) {
    case FaceDirection::NegZ:
        doorX = x0 + doorSlot(building.width, style);
        doorZ = z0;
        break;
    case FaceDirection::PosZ:
        doorX = x0 + doorSlot(building.width, style);
        doorZ = z1;
        break;
    case FaceDirection::NegX:
        doorX = x0;
        doorZ = z0 + doorSlot(building.depth, style);
        break;
    default:
        doorX = x1;
        doorZ = z0 + doorSlot(building.depth, style);
        break;
    }
    doorX = std::clamp(doorX, x0 + 1, x1 - 1);
    doorZ = std::clamp(doorZ, z0 + 1, z1 - 1);
    // Snap back onto the wall the facing named — clamping moved it off a corner
    // and could otherwise have moved it off the wall entirely.
    if (building.facing == FaceDirection::NegZ) {
        doorZ = z0;
    } else if (building.facing == FaceDirection::PosZ) {
        doorZ = z1;
    } else if (building.facing == FaceDirection::NegX) {
        doorX = x0;
    } else {
        doorX = x1;
    }

    w.put(doorX, floorY, doorZ, doorAt(palette.opening, building.facing, false, false, false));
    w.put(doorX, floorY + 1, doorZ, doorAt(palette.opening, building.facing, false, false, true));

    // A doorstep, so a hillside house is not entered by jumping.
    const int stepX = doorX + (building.facing == FaceDirection::PosX    ? 1
                               : building.facing == FaceDirection::NegX ? -1
                                                                        : 0);
    const int stepZ = doorZ + (building.facing == FaceDirection::PosZ    ? 1
                               : building.facing == FaceDirection::NegZ ? -1
                                                                        : 0);
    w.put(stepX, floorY - 1, stepZ, palette.floor);
    w.put(stepX, floorY, stepZ, BlockId::Air);
    w.put(stepX, floorY + 1, stepZ, BlockId::Air);

    // A light beside the door rather than loose on the ground somewhere. The
    // reference lights a village from lamp posts, walls and doorsteps, and
    // nowhere else - torches standing about in the open is what reads as a
    // scattering rather than as a village.
    {
        const bool acrossX = building.facing == FaceDirection::NegZ ||
                             building.facing == FaceDirection::PosZ;
        const int lampX = stepX + (acrossX ? 1 : 0);
        const int lampZ = stepZ + (acrossX ? 0 : 1);
        w.put(lampX, floorY - 1, lampZ, palette.floor);
        w.put(lampX, floorY, lampZ, palette.lamp);
    }

    // Roof.
    if (palette.flatRoofs || (style & 16u) != 0) {
        flatRoof(w, palette, x0, z0, x1, z1, roofBase);
    } else {
        gableRoof(w, palette, x0, z0, x1, z1, roofBase);
    }

    // A chimney on about half of them, which is the cheapest silhouette break
    // there is. **Starts at the top wall course, not at the floor** — run down
    // to the hearth it would stand in the middle of the room the workstation
    // and the bed already share.
    if ((style & 32u) != 0 && building.design != Design::Temple) {
        const int cx = x0 + 1;
        const int cz = z1 - 1;
        for (int y = roofBase - 1; y <= roofBase + 3; ++y) {
            w.put(cx, y, cz, weathered(palette, w.seed, cx, cz + y));
        }
    }

    // Interior. **Every fitting below claims its cells in one map and asks that
    // map before it writes**, in the order they must win in: the doorway first
    // (a blocked door is a villager that can never leave), then the fixtures
    // that are part of the shape, then the bed, then the chest, then the light.
    FloorPlan room = interiorOf(building);
    const int colour = (style & 64u) != 0 ? palette.bedColour : palette.bedColourAlt;

    // The cell a villager steps into. `stepX`/`stepZ` is the doorstep outside,
    // so the same offset the other way is the cell inside.
    const int insideX = doorX - (stepX - doorX);
    const int insideZ = doorZ - (stepZ - doorZ);
    room.claim(insideX, insideZ);

    // Where the trade or the crafting table stands. **Picked before the
    // fittings and written after them**: the workshop's counter runs along
    // whichever wall faces the street, and for two of the four facings that is
    // this cell's own wall, so the counter has to know to leave a gap and the
    // job block has to be the one that fills it.
    int jobX = 0;
    int jobZ = 0;
    const bool hasJob =
        (building.workstation != BlockId::Air || (style & 128u) != 0) &&
        pickInterior(room, -1, 1, jobX, jobZ);
    if (hasJob) {
        room.claim(jobX, jobZ);
    }

    if (building.design == Design::Library) {
        // Shelves down one long wall, which is the whole of what makes a
        // library legible from the doorway — and **not across the doorway**,
        // which is where a library facing NegX used to put two of them, one on
        // top of the other, with the librarian behind.
        for (int z = z0 + 1; z <= z1 - 2; ++z) {
            if (!room.free(x0 + 1, z)) {
                continue;
            }
            room.claim(x0 + 1, z);
            w.put(x0 + 1, floorY, z, BlockId::Bookshelf);
            w.put(x0 + 1, floorY + 1, z, BlockId::Bookshelf);
        }
    } else if (building.design == Design::LargeHouse) {
        // The upper storey gets a floor of its own and a ladder to reach it.
        const int upper = floorY + 4;
        w.box(x0 + 1, upper, z0 + 1, x1 - 1, upper, z1 - 1, palette.wallAlt);
        w.put(x1 - 1, upper, z1 - 1, BlockId::Air);
        room.claim(x1 - 1, z1 - 1);
        for (int y = floorY; y < upper; ++y) {
            w.put(x1 - 1, y, z1 - 1, ladderFacing(FaceDirection::NegX));
        }
        w.put(x0 + 1, upper + 1, z0 + 1, bedAt(colour, FaceDirection::PosX, false));
        w.put(x0 + 2, upper + 1, z0 + 1, bedAt(colour, FaceDirection::PosX, true));
        w.put(x1 - 1, upper + 1, z1 - 2, BlockId::Torch);
    } else if (building.design == Design::Temple) {
        // A stair climbing one wall, and a light at the head of it, so the tall
        // building stands over the roofs around it. **Claimed rather than
        // skipped**: a step missing out of the middle of a stair is a stair
        // nothing can climb, so the run is laid whole and the light moves aside
        // for it instead.
        const int stairs = stairFamilyOf(palette.roof);
        for (int k = 0; k < 4 && z0 + 1 + k <= z1 - 1; ++k) {
            room.claim(x0 + 1, z0 + 1 + k);
            w.put(x0 + 1, floorY + k, z0 + 1 + k, stairsAt(stairs, eaveFacing(0, 1), false));
        }
        w.put(x0 + 1, roofBase + 2, std::min(z0 + 2, z1 - 1), palette.lamp);
    } else if (building.design == Design::Workshop) {
        // An open counter facing the street, which is what separates a shop from
        // a house with a job block in it.
        const int slabs = slabFamilyOf(palette.roof);
        const bool acrossX = building.facing == FaceDirection::NegZ ||
                             building.facing == FaceDirection::PosZ;
        const int counterZ = building.facing == FaceDirection::NegZ ? z0 + 1 : z1 - 1;
        const int counterX = building.facing == FaceDirection::NegX ? x0 + 1 : x1 - 1;
        const int from = acrossX ? x0 + 1 : z0 + 1;
        const int to = acrossX ? x1 - 1 : z1 - 1;
        for (int along = from; along <= to; ++along) {
            const int cellX = acrossX ? along : counterX;
            const int cellZ = acrossX ? counterZ : along;
            if (!room.free(cellX, cellZ)) {
                continue;
            }
            room.claim(cellX, cellZ);
            w.put(cellX, floorY, cellZ, slabAt(slabs, true));
        }
    }

    // The bed, in the first corner nothing else wants. The head is claimable
    // and the foot is not, so a villager owns one bed however many it sleeps
    // beside.
    for (const BedSpot& spot : kBedSpots) {
        const int footX = spot.cornerX > 0 ? x1 - 1 : x0 + 1;
        const int footZ = spot.cornerZ > 0 ? z1 - 1 : z0 + 1;
        const int headX = footX + (spot.alongZ ? 0 : (spot.cornerX > 0 ? -1 : 1));
        const int headZ = footZ + (spot.alongZ ? (spot.cornerZ > 0 ? -1 : 1) : 0);
        if (!room.free(footX, footZ) || !room.free(headX, headZ)) {
            continue;
        }
        // Which way the head lies from the foot, which is what both halves
        // carry so either can find the other.
        const FaceDirection lie = spot.alongZ
                                      ? (spot.cornerZ > 0 ? FaceDirection::NegZ
                                                          : FaceDirection::PosZ)
                                      : (spot.cornerX > 0 ? FaceDirection::NegX
                                                          : FaceDirection::PosX);
        room.claim(footX, footZ);
        room.claim(headX, headZ);
        w.put(footX, floorY, footZ, bedAt(colour, lie, false));
        w.put(headX, floorY, headZ, bedAt(colour, lie, true));
        break;
    }

    if (hasJob) {
        w.put(jobX, floorY, jobZ,
              building.workstation != BlockId::Air
                  ? orientedJobSite(building.workstation, oppositeDirection(building.facing))
                  : BlockId::CraftingTable);
    }

    // The loot chest, after the job block — but for the opposite reason to the
    // one the ordering elsewhere in this function has. **This write does not
    // win its cell: it goes in with `onlyIntoAir`**, so it loses to anything
    // already standing there. What keeps it is the claim map, not the order:
    // `foundation` clears the interior to air, and every fitting above claims
    // before it writes, so `pickInterior` can only hand back an empty cell.
    // The air guard is a backstop that is provably inert today, kept because
    // "the chest quietly did not appear" is exactly the failure a build and a
    // soak have nothing to say about — and losing it silently would cost every
    // large house and half of every workshop its chest, the weaponsmith's being
    // the one the reference guarantees.
    //
    // **It goes in as a `LootChest` id, not a `Chest`.** Nothing is rolled here
    // — generation returns blocks and only blocks, so contents could not ride
    // out of it even if we wanted them to, and it must stay a pure function of
    // `(seed, chunkCoord)`. The id names the table, the block's own position
    // names the contents, and the first player to touch it rolls it.
    //
    // **And this line runs again on every chunk-format bump, which is what
    // makes finding 871 a duplication exploit.** `materialise` in `Main.cpp`
    // records "already rolled" by overwriting this id with `plainChestFor`, and
    // that mark lives *only* in the block id. So when `kChunkFormatVersion`
    // moves, `WorldStore::load` rightly refuses the stale chunk and it is
    // regenerated from `(seed, coord)` - this line puts the unrolled marker
    // back - while `chests.dat` is versioned separately and survives holding
    // whatever the player left. Opening it rolls a second full chest *around*
    // the leftovers, because `loot::placeStack` only fills empty slots and
    // merges onto matching ones, so nothing is overwritten. Probed at seed
    // 1337 over 22 village chests: a roll is 170 items, and a bump adds
    // **+170 every time**, whether the player took half of it or all of it.
    // The roll is reproducible from `(seed, position)` - 22 of 22 - so the
    // only thing a regeneration cannot re-derive is what the player took.
    //
    // **The fix cannot live here, and the tempting version of it is a trap.**
    // "Do not emit the marker where a chest record already exists" would make
    // generation read player-mutable state, from the worldgen thread, which is
    // the one rule this file exists to keep. What the fix needs instead is an
    // invariant checkable at the seam, and there is a clean one: **an unrolled
    // `LootChest` id and a `chests.dat` record at the same cell is a
    // contradiction**, because `materialise` overwrites the id in the same
    // breath as it creates the record. Both halves of that guard are
    // `Main.cpp`'s - skip the roll when a record is already there, and stop
    // dropping empty records, or "no record" keeps meaning both "never opened"
    // and "opened and emptied".
    const int lootTable = lootTableFor(building.design, building.workstation);
    int chestX = 0;
    int chestZ = 0;
    if (lootTable >= 0 && pickInterior(room, 1, 1, chestX, chestZ)) {
        room.claim(chestX, chestZ);
        // Back to whichever wall it stands against, so it opens into the room
        // and can be reached. Derived from where it landed rather than fixed,
        // because where it lands is no longer fixed either.
        const FaceDirection opens = chestX == x1 - 1   ? FaceDirection::NegX
                                    : chestX == x0 + 1 ? FaceDirection::PosX
                                    : chestZ == z1 - 1 ? FaceDirection::NegZ
                                                       : FaceDirection::PosZ;
        w.put(chestX, floorY, chestZ, lootChestAt(lootTable, opens), true);
    }

    // The light goes in last, so nothing above can land on top of it — and in
    // the first free corner, so a temple's stair and a library's shelves keep
    // the run they were drawn as.
    int torchX = x0 + 1;
    int torchZ = z0 + 1;
    if (pickInterior(room, -1, -1, torchX, torchZ)) {
        w.put(torchX, floorY, torchZ, BlockId::Torch);
    }
}

/// A well or a market square, whichever the roll picked. Both are nine across.
void buildTownCentre(const Writer& w, const Palette& palette, const Building& building) {
    // The well, the bell, the four stalls and the four corner lamps are all
    // laid out by hand against a nine-across square — `x0 + 4` is its middle and
    // `x0 + 7` its far stall. **Derived from the one table that owns the size**
    // so a change there fails the build rather than sliding the well off centre.
    static_assert(sizeOf(Design::TownCentre).width == 9 &&
                      sizeOf(Design::TownCentre).depth == 9,
                  "The town square's fittings are hand-placed on a 9x9 grid: "
                  "change TownCentre's row in sizeOf and this stops being true.");
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int y = building.floorY;
    const int stairs = stairFamilyOf(palette.roof);
    const int fences = fenceFamilyOf(palette.wall);

    foundation(w, palette, building, 8);

    // The apron: a paved square with its corners cut off, which is what stops it
    // reading as a slab dropped on the grass.
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = std::abs(x - (x0 + 4));
            const int dz = std::abs(z - (z0 + 4));
            if (dx + dz > 6) {
                continue;
            }
            const bool inner = dx <= 2 && dz <= 2;
            w.put(x, y - 1, z, inner ? weathered(palette, w.seed, x, z) : palette.path);
            w.box(x, y, z, x, y + 3, z, BlockId::Air);
        }
    }

    // The well itself: a rim of stone round a column of water, four posts and a
    // roof over it.
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        for (int x = x0 + 3; x <= x1 - 3; ++x) {
            w.put(x, y, z, weathered(palette, w.seed, x, z));
        }
    }
    w.put(x0 + 4, y, z0 + 4, BlockId::Water0);
    w.put(x0 + 4, y - 1, z0 + 4, BlockId::Water0);
    w.put(x0 + 4, y - 2, z0 + 4, palette.stone);

    for (int k = 1; k <= 2; ++k) {
        w.put(x0 + 3, y + k, z0 + 3, fenceAt(fences));
        w.put(x1 - 3, y + k, z0 + 3, fenceAt(fences));
        w.put(x0 + 3, y + k, z1 - 3, fenceAt(fences));
        w.put(x1 - 3, y + k, z1 - 3, fenceAt(fences));
    }
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        for (int x = x0 + 3; x <= x1 - 3; ++x) {
            w.put(x, y + 3, z, palette.roofFill);
        }
    }
    for (int x = x0 + 3; x <= x1 - 3; ++x) {
        w.put(x, y + 3, z0 + 2, stairsAt(stairs, eaveFacing(0, -1), false));
        w.put(x, y + 3, z1 - 2, stairsAt(stairs, eaveFacing(0, 1), false));
    }
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        w.put(x0 + 2, y + 3, z, stairsAt(stairs, eaveFacing(-1, 0), false));
        w.put(x1 - 2, y + 3, z, stairsAt(stairs, eaveFacing(1, 0), false));
    }

    // The bell hangs at the meeting point, because that is what a villager
    // gathers at.
    w.put(x0 + 4, y + 4, z0 + 4, BlockId::Bell);

    // Four market stalls on the apron, a job block apiece under a plank awning
    // on a post. **This is where most of a village's trades come from**, and
    // that is deliberate: the plot roll on its own left five villages in six
    // with a single profession in them, which is not what a village is.
    struct Stall {
        int dx;
        int dz;
        int backX;
        int backZ;
    };
    constexpr std::array<Stall, 4> kStalls{
        {{1, 4, 1, 0}, {7, 4, -1, 0}, {4, 1, 0, 1}, {4, 7, 0, -1}}};
    for (int i = 0; i < 4; ++i) {
        const Stall& stall = kStalls[static_cast<std::size_t>(i)];
        const int sx = x0 + stall.dx;
        const int sz = z0 + stall.dz;
        // **Unmasked on purpose.** `% kJobSiteCount` over a four-bit window
        // would draw from 0..15, and with 13 job sites that hands the first
        // three of them a second chance apiece — a 60% excess, and visible as
        // three trades recurring across the four stalls.
        //
        // Taking the whole remaining shift leaves only the modulo bias of
        // `2^(32 - 4i) mod 13`: negligible at the first stall, and about **one
        // part in eighty thousand** at the fourth, which has twenty bits left.
        // That is the measured figure. An earlier version of this comment said
        // "below a part in ten million", which was out by more than a hundred
        // times — a precise-sounding wrong number is worse than no number.
        //
        // The four draws are **correlated, not independent**: they are
        // overlapping windows of one `uint32`, not four rolls. Harmless for
        // stall variety, and worth stating because it would not be harmless for
        // anything that treated them as independent samples.
        const BlockId rolled = kJobSites[(building.style >> (i * 4)) % kJobSiteCount];
        // Turned to face the square, so a cooker's mouth is the side a customer
        // stands at.
        const FaceDirection front = stall.backX > 0   ? FaceDirection::NegX
                                    : stall.backX < 0 ? FaceDirection::PosX
                                    : stall.backZ > 0 ? FaceDirection::NegZ
                                                      : FaceDirection::PosZ;
        w.put(sx, y, sz, orientedJobSite(rolled, front));
        // The post stands behind the counter and the awning leans out over it.
        const int px = sx + stall.backX;
        const int pz = sz + stall.backZ;
        w.put(px, y, pz, fenceAt(fences));
        w.put(px, y + 1, pz, fenceAt(fences));
        w.put(px, y + 2, pz, palette.roofFill);
        w.put(sx, y + 2, sz, stairsAt(stairs, eaveFacing(-stall.backX, -stall.backZ), false));
    }

    // Lights at the four corners of the square, **on posts**. Stood loose on
    // the paving they read as torches dropped at random, which is what a
    // village is not - the reference only ever puts one on a lamp post, on a
    // wall, or beside a door.
    constexpr std::array<std::array<int, 2>, 4> kCorners{{{1, 1}, {7, 1}, {1, 7}, {7, 7}}};
    for (const std::array<int, 2>& corner : kCorners) {
        const int cx = x0 + corner[0];
        const int cz = z0 + corner[1];
        w.put(cx, y, cz, palette.stone);
        w.put(cx, y + 1, cz, fenceAt(fences));
        w.put(cx, y + 2, cz, palette.lamp);
    }
}

/// A worked field: farmland either side of a water channel, fenced, with a
/// composter at the near corner. **`type` picks the crop table** - the
/// reference publishes a different one per village type and they differ
/// radically, so this cannot be taken from the palette or defaulted.
void buildFarm(const Writer& w, const Palette& palette, const Building& building,
               VillageType type) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int y = building.floorY;
    const int fences = fenceFamilyOf(palette.wall);
    const int gates = gateFamilyOf(palette.wall);
    const FarmCrops& crops = kFarmCrops[static_cast<std::size_t>(type)];

    foundation(w, palette, building, 3);

    w.box(x0, y - 1, z0, x1, y - 1, z1, BlockId::Dirt);
    w.box(x0, y, z0, x1, y + 2, z1, BlockId::Air);

    const bool channelAlongX = building.width >= building.depth;
    const int midX = (x0 + x1) / 2;
    const int midZ = (z0 + z1) / 2;

    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        for (int x = x0 + 1; x <= x1 - 1; ++x) {
            const bool channel = channelAlongX ? z == midZ : x == midX;
            if (channel) {
                w.put(x, y - 1, z, BlockId::Water0);
                continue;
            }
            w.put(x, y - 1, z, BlockId::FarmlandMoist);
            // **The type's own crop table, rolled the way the reference's
            // processor rolls it: sequentially, not as one N-way split.**
            //
            // `kFarmCrops` above carries all five tables and cites them. The
            // shares in them are shares **of what is left**, not of the field:
            // plains is carrot 30%, then potato 20% of the surviving 70% = 14%,
            // then beetroot 10% of 56% = 5.6%, and wheat the remaining 50.4%.
            // Walking the rules against one uniform draw is the same
            // distribution in a single test.
            //
            // Two bugs lived here. The first read the three plains shares as an
            // additive split of the whole field (0.40 / 0.70 / 0.90) and called
            // it "the reference's own proportions", which made beetroot nearly
            // twice as common as it should be and wheat - half a real field -
            // the rarest thing in ours after carrot. The second, found only
            // once the tables were looked up per biome, was that **the plains
            // table was being used in all five village types**: deserts grew
            // carrots and potatoes they have none of, savannas grew four crops
            // where the reference grows one, and no farm in the game had ever
            // grown a melon or a pumpkin because no table here mentioned them.
            const float pick = dress(w.seed, x, z, 3u);
            // Walk the type's rules the way the processor walks them: each rule
            // is offered only the columns the earlier rules did not take, so
            // its own probability is applied to what is left rather than to the
            // field. `lo` is how much of the draw earlier rules have claimed and
            // `span` is what remains for this one.
            BlockId family = kFarmBaseCrop;
            float lo = 0.0f;
            float span = 1.0f;
            for (int r = 0; r < crops.count; ++r) {
                const float upto = lo + span * crops.rules[r].chance;
                if (pick < upto) {
                    family = crops.rules[r].family;
                    break;
                }
                lo = upto;
                span *= 1.0f - crops.rules[r].chance;
            }
            // **Fully grown.** See `kRipeCrop`, which carries the source and
            // says plainly how far it is sourced.
            //
            // The rolled age this replaces put a field at a mean of 3.5 of 7,
            // so a village farm yielded roughly half a harvest and every crop
            // you did not want to wait for had to be replanted. **`cropAt`
            // clamps rather than wrapping**, and beetroot is eight growth
            // states here and not Java's four, so 7 is the ripe picture for
            // every family and not an id belonging to the next block along.
            // That eight is **`Block.hpp`'s, and the assert on `kRipeCrop` is
            // bound to `Block.hpp` rather than to any reference number** -
            // which is the only reason this line is safe, because the
            // provenance I first wrote here was over-claimed and I have since
            // disproved it myself.
            //
            // The bad claim was that `Mojang/bedrock-samples`
            // `metadata/vanilladata_modules/mojang-blocks.json` proves beetroot
            // has eight stages because it gives `minecraft:beetroot` a `growth`
            // property of 0..7. **It proves no such thing.** A property's
            // published value list is the STORAGE DOMAIN OF THE FIELD, not any
            // one block's range, and `data_items` only says which blocks
            // *carry* a property - it never narrows it. Counted from the file
            // itself: **`growth` has THIRTEEN users** - beetroot, carrots,
            // potatoes, wheat, both stems, sweet_berry_bush, pitcher_crop,
            // torchflower_crop, pink_petals, leaf_litter, wildflowers and
            // shelf_mushroom. Thirteen blocks sharing one three-bit field tells
            // you the field is three bits wide and nothing whatever about
            // beetroot.
            //
            // **The rule, worth carrying: count the users. One user and the
            // list is authoritative for that block; more than one and you have
            // only learned how wide the field is.** It is the edition-split
            // mistake in a new costume - a real number, from a real primary
            // source, applied one level too specifically.
            w.put(x, y, z, cropAt(family, kRipeCrop));
        }
    }

    // The fence, with a gate on the street side.
    for (int x = x0; x <= x1; ++x) {
        w.put(x, y, z0, fenceAt(fences));
        w.put(x, y, z1, fenceAt(fences));
    }
    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        w.put(x0, y, z, fenceAt(fences));
        w.put(x1, y, z, fenceAt(fences));
    }
    switch (building.facing) {
    case FaceDirection::NegZ:
        w.put(midX, y, z0, gateAt(gates, FaceDirection::NegZ, false));
        break;
    case FaceDirection::PosZ:
        w.put(midX, y, z1, gateAt(gates, FaceDirection::PosZ, false));
        break;
    case FaceDirection::NegX:
        w.put(x0, y, midZ, gateAt(gates, FaceDirection::NegX, false));
        break;
    default:
        w.put(x1, y, midZ, gateAt(gates, FaceDirection::PosX, false));
        break;
    }

    // The job block the layout dealt this plot, read from the plan rather than
    // written out again here.
    //
    // **`solve` fixes every farm to a composter today, so this changes nothing
    // on screen** — it is the same block by a different route, and the previous
    // version was not producing a wrong farm. What it buys is that the two
    // cannot drift: the workstation is stated in exactly one place, and if a
    // farm is ever dealt a varied trade the builder already follows. Written
    // down because the honest reason is thin and the tempting one — "farms get
    // varied trades and this was dropping them" — is false, and a reader who
    // believes it will build on a premise `solve` does not support.
    if (building.workstation != BlockId::Air) {
        w.put(x0, y, z0, orientedJobSite(building.workstation, building.facing));
    }
    w.put(x1, y, z1, BlockId::HayBlock);
    w.put(x1, y + 1, z1, palette.lamp);
}

/// A fenced paddock with a gate and a bale.
///
/// **Stocked as of 2026-08-19.** For most of this project every paddock in
/// every village in the world was a fenced empty square: the fences were built
/// and nothing anywhere ever put an animal inside one, so a village was no
/// source of starting livestock. `village::livestockIn` at the foot of this
/// file now derives the herd and `Creature.cpp` spawns it.
///
/// **This builder deliberately knows nothing about that.** It writes blocks; it
/// does not place entities, and it must not start. Chunk generation is a pure
/// function of `(seed, chunkCoord)` and runs on a worker thread, while spawning
/// is a world mutation that belongs to the single owner of the creature list on
/// the main thread. The two halves meet through a derivation that stores
/// nothing, which is `guardsIn`'s arrangement for the iron golem.
///
/// **The bale at the centre is load-bearing to the stocking, not decoration.**
/// It is two blocks tall on the middle cell, so `livestockIn` skips that cell
/// when it deals standing room. Move it, or make it one tall, and an animal
/// will be dealt a square that already has a bale in it.
///
/// The reference set is horses, pigs, cows and sheep in the pens. Two pieces of
/// it are **not** built and are filed rather than guessed at: the single camel
/// a desert village gets, which the source places at the meeting point rather
/// than in a pen, and the two armour stands in a taiga one. See findings 1074
/// and 9652 — and note the per-pen count is a hedged secondary figure, with its
/// provenance on `kMinPenAnimals` in the header.
void buildPen(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int y = building.floorY;
    const int fences = fenceFamilyOf(palette.wall);
    const int gates = gateFamilyOf(palette.wall);

    foundation(w, palette, building, 3);

    w.box(x0, y - 1, z0, x1, y - 1, z1, BlockId::Grass);
    w.box(x0, y, z0, x1, y + 2, z1, BlockId::Air);
    for (int x = x0; x <= x1; ++x) {
        w.put(x, y, z0, fenceAt(fences));
        w.put(x, y, z1, fenceAt(fences));
    }
    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        w.put(x0, y, z, fenceAt(fences));
        w.put(x1, y, z, fenceAt(fences));
    }

    const int midX = (x0 + x1) / 2;
    const int midZ = (z0 + z1) / 2;
    switch (building.facing) {
    case FaceDirection::NegZ:
        w.put(midX, y, z0, gateAt(gates, FaceDirection::NegZ, false));
        break;
    case FaceDirection::PosZ:
        w.put(midX, y, z1, gateAt(gates, FaceDirection::PosZ, false));
        break;
    case FaceDirection::NegX:
        w.put(x0, y, midZ, gateAt(gates, FaceDirection::NegX, false));
        break;
    default:
        w.put(x1, y, midZ, gateAt(gates, FaceDirection::PosX, false));
        break;
    }

    w.put(midX, y, midZ, BlockId::HayBlock);
    w.put(midX, y + 1, midZ, BlockId::HayBlock);

    // **Whatever the layout deals, the builder places.** Half of this rule used
    // to live in the farm builder and nowhere else: a pen was dealt a job site
    // from the same cursor as every other plot and simply never put it down, so
    // the trade was taken off the cursor and dropped on the floor. `solve` no
    // longer deals one to a pen — and this is the other half, so that if it ever
    // does again the villager has somewhere to work rather than nowhere.
    if (building.workstation != BlockId::Air) {
        w.put(x0 + 1, y, z0 + 1, orientedJobSite(building.workstation, building.facing));
    }
}

/// Streets: terrain-matching, per column, which is the one thing a village does
/// that a building must never do.
///
/// The reference reaches the same result twice over — it slides the whole piece
/// onto the heightmap when it places it, then drops each column individually
/// through a gravity processor. Ours is the second half only, which is all that
/// is needed when the road is not made of pieces.
void buildRoad(const Writer& w, const Palette& palette, const Road& road) {
    forEachRoadColumn(road, [&](int x, int z) {
        const int ground = surfaceHeightAt(w.seed, x, z);

        if (ground <= kSeaLevel) {
            // Over water the street becomes a plank bridge, which is the
            // reference's own substitution.
            w.put(x, kSeaLevel + 1, z, palette.bridge);
            w.box(x, kSeaLevel + 2, z, x, kSeaLevel + 4, z, BlockId::Air);
            return;
        }

        // One cell in ten stays grass. It is the cheapest possible detail
        // and it is most of why a village road reads as worn rather than
        // stamped.
        const bool bare = dress(w.seed, x, z, 7u) < 0.12f;
        w.put(x, ground, z, bare ? BlockId::Grass : palette.path);
        w.put(x, ground - 1, z, palette.foundation, true);
        w.box(x, ground + 1, z, x, ground + 3, z, BlockId::Air);
    });
}

void buildDecor(const Writer& w, const Palette& palette, const Decor& item) {
    const int ground = surfaceHeightAt(w.seed, item.x, item.z);
    if (ground <= kSeaLevel) {
        return;
    }
    // **The same rule as the line above, for the other way a column can have no
    // top block.** `surfaceHeightAt` answers with the uncarved height, so a cave
    // mouth here left a four-tall lamp post standing on nothing at all. The
    // water guard was written and this one was not, which is the shape
    // `Structures.cpp` already fixed for trees off the very same call —
    // `surfaceCarvedAt`'s comment in `TerrainGenerator.hpp` describes that bug.
    // Measured at seed 12: 7 lamp posts of 402 over 48 villages.
    //
    // Dropping the post is right where plugging is right for a house: a lamp is
    // scenery and one missing reads as a village that never had one, whereas a
    // hole in a floor is a hole in a floor.
    if (surfaceCarvedAt(w.seed, item.x, item.z)) {
        return;
    }
    const int fences = fenceFamilyOf(palette.wall);
    switch (item.kind) {
    case kDecorLampPost: {
        // A lamp post: three fence sections and a light on top, which is the
        // reference's `<type>_lamp_1` in every village it has.
        w.put(item.x, ground, item.z, palette.stone);
        for (int k = 1; k <= 3; ++k) {
            w.put(item.x, ground + k, item.z, fenceAt(fences));
        }
        w.put(item.x, ground + 4, item.z, palette.lamp);
        break;
    }
    case kDecorCampfire: {
        // A campfire on the bare ground, which is where the reference puts it -
        // no plinth, unlike the lamp post beside it. Light 15, so it lights the
        // street on its own, and it is the only source of one in the world
        // outside a crafting table: `Recipe.cpp` can make one and nothing at
        // all placed one until now (finding 9947).
        //
        // **The ground block is deliberately left as it is.** A lamp post
        // stamps a stone plinth because a fence post standing in grass reads as
        // dropped rather than built; a campfire standing in grass, snow or
        // podzol reads as exactly right, and stamping stone under it would make
        // the one piece of taiga street furniture that has no stone in it look
        // like a fire pit somebody paved.
        w.box(item.x, ground + 1, item.z, item.x, ground + 4, item.z, BlockId::Air);
        w.put(item.x, ground + 1, item.z, BlockId::Campfire);
        break;
    }
    default:
        // **Silent, and that is the risk this switch carries.** A `Decor::kind`
        // the layout emits and this switch does not name builds nothing at all
        // and warns about nothing at all - the kinds are a `std::uint8_t`, so
        // there is no enumeration for `/W4` to check and C4062 is off anyway.
        // The two constants live in `Village.hpp` beside `Decor::kind` for that
        // reason: adding one there is meant to put you here.
        break;
    }
}

/// Whether a snow layer at this height would land **inside** a building rather
/// than on top of one.
///
/// The scan below takes the topmost solid block *in its own chunk section* and
/// calls it exposed. That is right whenever the roof is in the same section and
/// wrong whenever it is not: a house whose floor sits low in a section and whose
/// roof pokes into the next one presents its **interior floor** as the topmost
/// solid of the lower section, and the room gets snowed. Measured at seed 1337:
/// 4 of 306 villagers were generated standing in a snow layer on their own
/// living-room floor, and every one of them in a snowy village. Which houses it
/// happens to is decided by where the 32-block section boundary falls, so it is
/// also the one thing in this file whose answer depends on a chunk.
///
/// **Not `underBuilding`.** That one is an XZ test over the whole column and is
/// used a few lines down to stop the *re-derived ground* branch from snowing a
/// column the foundation cleared; borrowing it here would take the snow off
/// every roof in the village, which is most of what a snowy village looks like.
/// The question here is a height: the roof course sits at
/// `floorY + wallHeightOf(...)` — `buildHouse` derives its own `roofBase` from
/// exactly that — so a layer at or below it is under a roof, and one above it is
/// on top of one. Only the designs `buildHouse` builds have a roof to be under.
bool insideBuilding(const Plan& plan, int x, int y, int z) {
    for (int b = 0; b < plan.buildingCount; ++b) {
        const Building& building = plan.buildings[b];
        if (!buildsAsHouse(building.design)) {
            continue;
        }
        if (x < building.minX || x >= building.minX + building.width || z < building.minZ ||
            z >= building.minZ + building.depth) {
            continue;
        }
        if (y <= building.floorY + wallHeightOf(building.design, building.style)) {
            return true;
        }
    }
    return false;
}

/// Whether a column sits under a building or the skirt its foundation pours.
///
/// Used only by the boundary rule below, and conservative on purpose: the skirt
/// is an ellipse and this is the rectangle round it, so a column it wrongly
/// claims loses a snow layer rather than gaining a floating one.
bool underBuilding(const Plan& plan, int x, int z) {
    for (int b = 0; b < plan.buildingCount; ++b) {
        const Building& building = plan.buildings[b];
        if (x >= building.minX - 2 && x < building.minX + building.width + 2 &&
            z >= building.minZ - 2 && z < building.minZ + building.depth + 2) {
            return true;
        }
    }
    return false;
}

/// Snow settles on whatever the sky can see, which is the whole of what makes a
/// snowy village different from a taiga one.
void snowOver(const Writer& w, const Plan& plan, ChunkCoord coord) {
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // Whether the block that would hold up a snow layer on this chunk's floor
    // is worth asking about at all. Below the village nothing is exposed and
    // above it nothing is left, so this is one or two chunk layers per village.
    const bool floorMayBearSnow = baseY - 1 >= plan.minY && baseY - 1 <= plan.maxY;

    for (int lz = 0; lz < Chunk::kSize; ++lz) {
        const int z = baseZ + lz;
        if (z < plan.minZ || z > plan.maxZ) {
            continue;
        }
        for (int lx = 0; lx < Chunk::kSize; ++lx) {
            const int x = baseX + lx;
            if (x < plan.minX || x > plan.maxX) {
                continue;
            }
            // Top down, so the first solid block found is the exposed one.
            //
            // **Down to local zero, not to one.** The bottom row of a section is
            // a perfectly ordinary block with sky above it whenever the section
            // above is air, and stopping at 1 left a snow-free stripe wherever a
            // village's ground crossed y=32 or y=64.
            //
            // ⚠ "The first solid block found is the exposed one" is only true
            // **within this section**. `insideBuilding` is what carries the
            // difference: see its comment for the room this used to snow.
            bool settled = false;
            for (int ly = Chunk::kSize - 1; ly >= 0; --ly) {
                const BlockId here = w.chunk.at(lx, ly, lz);
                const BlockId above = w.chunk.at(lx, ly + 1, lz);
                if (here == BlockId::Air || above != BlockId::Air) {
                    continue;
                }
                settled = true;
                if (!occludesFace(here, 1) || isSnowLayer(here)) {
                    break;
                }
                if (baseY + ly + 1 <= plan.maxY &&
                    !insideBuilding(plan, x, baseY + ly + 1, z)) {
                    w.put(x, baseY + ly + 1, z, snowLayerAt(0), true);
                }
                break;
            }

            // The mirror case: the ground is the last block of the section
            // *below* and the snow belongs to this one. The scan above cannot
            // see it — that block is in another chunk and reading it would cost
            // the purity that lets generation run on a worker at all — so where
            // the support is plain ground it is **re-derived rather than read**,
            // exactly as a tall plant's root is. Both chunks compute the same
            // answer from the same hashes and each keeps the half of it that
            // lands inside itself.
            //
            // Reaching here means every cell of this column inside this chunk is
            // air, so the sky test is already answered and only the support is
            // in doubt. Three things can be wrong with it: it can be under
            // water, a cave can have eaten it, or a building's foundation can
            // have cleared it — and that last one is why the footprint test is
            // here rather than trusting the terrain outright.
            //
            // **A village-built surface topping out on the boundary is the
            // residue and stays unsnowed**: a roof course or a field exactly at
            // y 31 or y 63 is not a hash, it is the output of six builders and
            // their air-clears in order, and re-deriving that means keeping a
            // shadow copy of the column rather than calling a function.
            if (!settled && floorMayBearSnow && baseY <= plan.maxY &&
                !underBuilding(plan, x, z)) {
                const int support = surfaceHeightAt(w.seed, x, z);
                if (support == baseY - 1 && support > kSeaLevel &&
                    !surfaceCarvedAt(w.seed, x, z)) {
                    w.put(x, baseY, z, snowLayerAt(0), true);
                }
            }
        }
    }
}

} // namespace

Plan solveCell(std::uint32_t seed, int cellX, int cellZ) { return solve(seed, cellX, cellZ); }

Nearby plansNear(std::uint32_t seed, int chunkX, int chunkZ) {
    Nearby out;

    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    const int firstX = floorDivInt(baseX - kReach, kCellBlocks);
    const int lastX = floorDivInt(baseX + Chunk::kSize + kReach, kCellBlocks);
    const int firstZ = floorDivInt(baseZ - kReach, kCellBlocks);
    const int lastZ = floorDivInt(baseZ + Chunk::kSize + kReach, kCellBlocks);

    // **Bounded by the array, not by a copy of its length.** The `4` written
    // here twice was `std::size(out.plans)` spelled out again, which is the
    // shape that turns a shrunk array into an overrun. `kMaxNearby` owns the
    // number and the header asserts why it is what it is.
    for (int cz = firstZ; cz <= lastZ && out.count < kMaxNearby; ++cz) {
        for (int cx = firstX; cx <= lastX && out.count < kMaxNearby; ++cx) {
            // Reject on arithmetic before any noise work: the origin is one
            // hash, and most candidates are hundreds of blocks away.
            //
            // **The origin is derived here by the same function `solve` uses.**
            // It was three lines copied, which is a bug waiting for the day the
            // two drift: a chunk would skip a plan whose buildings reach into
            // it, and a village would be missing whichever slice that chunk
            // owns.
            LayoutRoll probe = layoutRollFor(seed, cx, cz);
            const Origin origin = originOf(probe, cx, cz);
            if (origin.x + kReach < baseX || origin.x - kReach >= baseX + Chunk::kSize ||
                origin.z + kReach < baseZ || origin.z - kReach >= baseZ + Chunk::kSize) {
                continue;
            }

            Plan plan = solve(seed, cx, cz);
            if (plan.valid) {
                out.plans[out.count++] = plan;
            }
        }
    }

    return out;
}

bool occupies(const Nearby& near, int worldX, int worldZ) {
    for (int i = 0; i < near.count; ++i) {
        const Plan& plan = near.plans[i];
        for (int b = 0; b < plan.buildingCount; ++b) {
            const Building& building = plan.buildings[b];
            if (worldX >= building.minX - kClaimMargin &&
                worldX < building.minX + building.width + kClaimMargin &&
                worldZ >= building.minZ - kClaimMargin &&
                worldZ < building.minZ + building.depth + kClaimMargin) {
                return true;
            }
        }
        for (int r = 0; r < plan.roadCount; ++r) {
            const Road& road = plan.roads[r];
            const int lowX = std::min(road.x0, road.x1) - kRoadClaimMargin;
            const int highX = std::max(road.x0, road.x1) + kRoadClaimMargin;
            const int lowZ = std::min(road.z0, road.z1) - kRoadClaimMargin;
            const int highZ = std::max(road.z0, road.z1) + kRoadClaimMargin;
            if (worldX >= lowX && worldX <= highX && worldZ >= lowZ && worldZ <= highZ) {
                return true;
            }
        }
    }
    return false;
}

void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord, const Nearby& near) {
    const Writer writer{chunk, coord, seed};
    const int baseY = coord.y * Chunk::kSize;

    for (int i = 0; i < near.count; ++i) {
        const Plan& plan = near.plans[i];
        // Two chunk layers in three are entirely below or above any village.
        if (baseY > plan.maxY || baseY + Chunk::kSize <= plan.minY) {
            continue;
        }

        const Palette& palette = paletteFor(plan.type);

        // Streets first, so a building's own doorstep wins where the two meet.
        for (int r = 0; r < plan.roadCount; ++r) {
            buildRoad(writer, palette, plan.roads[r]);
        }

        for (int b = 0; b < plan.buildingCount; ++b) {
            const Building& building = plan.buildings[b];
            switch (building.design) {
            case Design::TownCentre:
                buildTownCentre(writer, palette, building);
                break;
            case Design::Farm:
                buildFarm(writer, palette, building, plan.type);
                break;
            case Design::AnimalPen:
                buildPen(writer, palette, building);
                break;
            default:
                buildHouse(writer, palette, building);
                break;
            }
        }

        for (int d = 0; d < plan.decorCount; ++d) {
            buildDecor(writer, palette, plan.decor[d]);
        }

        if (palette.snowy) {
            snowOver(writer, plan, coord);
        }
    }
}

int residentsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max) {
    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    int written = 0;

    for (int i = 0; i < near.count && written < max; ++i) {
        const Plan& plan = near.plans[i];
        for (int r = 0; r < plan.residentCount && written < max; ++r) {
            const Resident& who = plan.residents[r];
            if (who.x < baseX || who.x >= baseX + Chunk::kSize || who.z < baseZ ||
                who.z >= baseZ + Chunk::kSize) {
                continue;
            }
            out[written++] = who;
        }
    }
    return written;
}

int guardsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max) {
    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    int written = 0;

    for (int i = 0; i < near.count && written < max; ++i) {
        const Plan& plan = near.plans[i];
        // A village with almost nobody in it gets no guard, which is the spirit
        // of the reference's villager-count rule without the bookkeeping.
        if (plan.residentCount < 3) {
            continue;
        }
        // **On the square's own paving, not beside it.**
        //
        // This used to be `originX + 6`, described as "just clear of the well".
        // It is clear of the well and it is also clear of the *square*: the
        // town centre is nine across and centred on the origin, so it ends at
        // `originX + 4`, and `foundation`'s skirt is an ellipse of half-width
        // `9 / 2 + 0.9 = 5.4` which does not reach 6 either. The column was
        // therefore untouched natural terrain, while the y handed out with it
        // was `centreY` — the square's floor. Measured at seed 12 over 48
        // villages: **12 golems spawned in mid-air and 12 inside the hillside,
        // against 23 that happened to land level; worst offset 4 blocks.**
        //
        // `x1` of the square is paved by the apron at `centreY - 1` (its cut
        // corners drop `|dx| + |dz| > 6` and this is `dx = 4, dz = 0`) and
        // cleared to `centreY + kSquareClearance` by `foundation`. So the
        // footing is written by the village rather than found in the ground,
        // and no terrain can move it. It is also the only free cell on the
        // well's row: `buildTownCentre` fills `x0 + 1` and `x0 + 7` with
        // stalls, `x0 + 2` and `x0 + 6` with their posts and `x0 + 3` through
        // `x0 + 5` with the well.
        const Footprint square = sizeOf(Design::TownCentre);
        static_assert(sizeOf(Design::TownCentre).width == 9 &&
                          sizeOf(Design::TownCentre).depth == 9,
                      "guardsIn stands the golem on the far edge of the well's own row, which is "
                      "free only on a 9x9 square: change TownCentre's row in sizeOf and this "
                      "lands on a stall.");
        const int x = plan.originX + square.width / 2;
        const int z = plan.originZ;
        if (x < baseX || x >= baseX + Chunk::kSize || z < baseZ || z >= baseZ + Chunk::kSize) {
            continue;
        }
        out[written++] = {x, plan.centreY, z, false, false};
    }
    return written;
}

/// The species a village's pens are stocked from, written into `out` and
/// counted. Empty for `VillageType::None`, which is what a plan that solved to
/// nothing carries — so an invalid plan stocks nothing rather than defaulting
/// to cattle.
///
/// ⚠️ **SECONDARY SOURCE, hedged, dated 2026-08-19.** See `kMinPenAnimals` in
/// the header for the provenance and for what would settle it. The biome
/// theming below is the wiki summary's own: horses, pigs, cows and sheep across
/// the pens generally; in taiga "horses are rarer, sheep and pigs more common";
/// snowy "usually only spawns sheep"; plains and savanna "mixes of cows, sheep,
/// pigs, or horses".
///
/// **Desert deliberately gets no camel.** The same summary places the desert
/// village's single camel *at the meeting point, not in a pen*, so putting one
/// here would contradict the only source this table has. The camel is a real
/// gap and it is filed rather than guessed at — see finding 9652.
///
/// There is no `default:` on purpose. `CLAUDE.md` bug shape #10 is a `default:`
/// that returns a real value and so hides every missing entry, and **MSVC's
/// C4062 is off at `/W4`**, so a new `VillageType` would take a catch-all in
/// silence. Every enumerator is named.
///
/// ⚠️ **But do not read that as the exposure being closed — a new enumerator
/// fails here SILENTLY, and an earlier version of this comment claimed the
/// opposite** (corrected 2026-08-19 11:26). The trailing `return 0` stocks
/// nothing, so a seventh village type gets fenced empty squares: green build,
/// no warning, no crash, no validation error, discovered only by a player
/// walking past. That is the exact emptiness this function was written to
/// remove, reopened for one village type. **The comment claiming it was loud
/// was the dangerous half**, because it argues a reader out of the one fix that
/// would work.
///
/// The fix cannot be written here: pinning `Snowy == 5` does not help, since a
/// type appended *after* `Snowy` leaves `Snowy` at 5 and passes. It needs a
/// `Count` sentinel on `VillageType` in `Biome.hpp`, which is another file's;
/// filed as 9835, and when it lands this gets the assert and this paragraph
/// goes. `TreeVariant::Count` in `Structures.cpp` is the same fix already made.
/// **Falsified by `VillageType` gaining a `Count` enumerator** — search
/// `VillageType::` in `Biome.hpp` rather than trusting this paragraph, since a
/// list rots and a search does not.
int penSpeciesFor(VillageType type, CreatureKind (&out)[kMaxPenSpecies]) {
    switch (type) {
    case VillageType::Plains:
        out[0] = CreatureKind::Cow;
        out[1] = CreatureKind::Sheep;
        out[2] = CreatureKind::Pig;
        out[3] = CreatureKind::Horse;
        return 4;
    case VillageType::Savanna:
        out[0] = CreatureKind::Cow;
        out[1] = CreatureKind::Sheep;
        out[2] = CreatureKind::Horse;
        return 3;
    case VillageType::Taiga:
        out[0] = CreatureKind::Sheep;
        out[1] = CreatureKind::Pig;
        return 2;
    case VillageType::Snowy:
        out[0] = CreatureKind::Sheep;
        return 1;
    case VillageType::Desert:
        out[0] = CreatureKind::Cow;
        out[1] = CreatureKind::Sheep;
        return 2;
    case VillageType::None:
        return 0;
    }
    return 0;
}

// **This is the pin `Biome.hpp` says is here** (finding 9835, landed 2026-08-19
// 12:2x). The sentinel was added at the other end by that file's owner *for this
// assert*, and until this line existed its comment there described an edit that
// had not happened - so the two halves are now consistent rather than one of
// them merely claiming to be.
//
// The switch above has **no `default:` label**, which is the right shape - a
// `default:` returning a real value is `CLAUDE.md` bug shape #10, a catch-all
// that hides every missing entry. But the protection a missing `default:`
// normally buys is a compiler diagnostic on an unhandled enumerator, and
// **C4062 is off at `/W4` in this project**, so a seventh village type produces
// no warning anywhere. It would fall out of the switch to `return 0`,
// `livestockIn` would skip the plan entirely, and every animal pen in every
// village of the new type would generate as a fenced empty square - which is
// exactly the bug findings 1074 and 9652 just closed, reopened for one type and
// **invisible from a clean build and a clean soak alike.**
//
// So this is the diagnostic the warning does not give us. Adding an enumerator
// moves `Count` and fires this line; the fix is a `case`, not a bigger number.
static_assert(static_cast<int>(VillageType::Count) == 6,
              "A VillageType was added or removed, and penSpeciesFor above still switches over the "
              "old set. Add a case for the new type - do not just update this 6, and do not add a "
              "default: label. A type with no case falls through to return 0, which livestockIn "
              "reads as 'this village keeps no animals', so every pen it builds is a fenced empty "
              "square. That failure is SILENT: C4062 is off, so nothing else in the tree reports "
              "it, and a village looks generated because the fences and the hay bale are there.");

// **The one coupling `kMaxLivestock` has left.** The header now *computes* the
// bound from `Chunk::kSize` and `kPenSpan`, so a chunk resize re-derives it
// automatically and that half can no longer rot. What a computation cannot
// reach is this file's footprint table, because `sizeOf` is `.cpp`-only — so
// `kPenSpan` is the header's copy of a number that lives here, and this pins
// the two together. One variable, which is the whole point.
//
// Note the assert deliberately does NOT restate the old whole-derivation form.
// Now that `kMaxLivestock` is built from `Chunk::kSize`, an assert mentioning
// `Chunk::kSize` on both sides would be comparing part of a derivation against
// itself and would pass while pointing at nothing - `CLAUDE.md` lesson 11, and
// eleven asserts once passed that way in this project.
//
// It is load-bearing rather than decorative, and that is measured: **the header
// said 9 centres for twenty minutes on 2026-08-19 because it was derived
// against a 16-wide chunk, and writing the first version of this assert is what
// found it.** `guardsIn` pins `TownCentre` the same way, eighty lines above.
static_assert(kPenSpan == sizeOf(Design::AnimalPen).width &&
                  kPenSpan == sizeOf(Design::AnimalPen).depth,
              "Village.hpp's kPenSpan no longer matches the pen's footprint in sizeOf, so "
              "kMaxLivestock is derived from the wrong pen size. Update kPenSpan to match; do not "
              "widen kMaxLivestock by eye. Too small clips animals SILENTLY, because livestockIn "
              "stops at written < max and nothing anywhere reports the loss.");

int livestockIn(const Nearby& near, int chunkX, int chunkZ, Livestock* out, int max) {
    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    int written = 0;

    for (int i = 0; i < near.count && written < max; ++i) {
        const Plan& plan = near.plans[i];

        CreatureKind species[kMaxPenSpecies];
        const int speciesCount = penSpeciesFor(plan.type, species);
        if (speciesCount == 0) {
            continue;
        }

        for (int b = 0; b < plan.buildingCount && written < max; ++b) {
            const Building& pen = plan.buildings[b];
            if (pen.design != Design::AnimalPen) {
                continue;
            }

            // **Keyed on the pen's centre, which is `guardsIn`'s rule.** A pen
            // is 7x7 and `Chunk::kSize` is 32, so a paddock can still straddle
            // a border — straddling depends on where the pen sits, not on how
            // wide the chunk is — and handing its animals out per-cell would
            // let two chunks each stock the part they own, so a fence-line pen
            // would come out at double strength. One chunk owns the whole pen.
            // The animals themselves may land a few blocks outside it, which
            // costs nothing: the creature list is global, not per-chunk.
            //
            // ⚠️ **This said "a chunk 16 wide" until 2026-08-19 11:37 — the
            // same wrong number that made `kMaxLivestock` 36 instead of 100.**
            // Fixing the constant and its header comment did not carry to here,
            // which is `CLAUDE.md` bug shape #14: a rule corrected in one of the
            // two places that state it. The conclusion above survived the error
            // because straddling does not depend on chunk width, so nothing
            // looked wrong — but anyone re-deriving the bound from this comment
            // would have got 9 centres again. **When you fix a number, grep the
            // number.**
            const int x0 = pen.minX;
            const int z0 = pen.minZ;
            const int x1 = x0 + pen.width - 1;
            const int z1 = z0 + pen.depth - 1;
            const int midX = (x0 + x1) / 2;
            const int midZ = (z0 + z1) / 2;
            if (midX < baseX || midX >= baseX + Chunk::kSize || midZ < baseZ ||
                midZ >= baseZ + Chunk::kSize) {
                continue;
            }

            // Seeded off the building's own style word, which the layout
            // already derives from the world seed. So this is a pure function
            // of the plan, needs no seed argument, and every chunk that asks
            // gets the same herd.
            Roll roll{noise::hash2D(pen.style, x0, z0)};
            const int wanted = kMinPenAnimals + roll.range(kMaxPenAnimals - kMinPenAnimals + 1);
            const CreatureKind kind = species[roll.range(speciesCount)];

            // The fence ring is the pen's outline, so the standing room is one
            // in from every side, and `buildPen` puts a two-tall bale on the
            // middle cell.
            const int insideW = pen.width - 2;
            const int insideD = pen.depth - 2;
            if (insideW <= 0 || insideD <= 0) {
                continue;
            }
            const int freeCells = insideW * insideD - 1;
            if (freeCells <= 0) {
                continue;
            }

            // Walk the standing room once, skipping the bale, and take every
            // `stride`-th free cell. Visiting each cell at most once is what
            // makes two animals in one square impossible without a search, and
            // the stride is what spreads them instead of lining them up along
            // the first row.
            const int stride = std::max(1, freeCells / std::max(1, wanted));
            int freeIndex = 0;
            int placed = 0;
            for (int dz = 0; dz < insideD && placed < wanted && written < max; ++dz) {
                for (int dx = 0; dx < insideW && placed < wanted && written < max; ++dx) {
                    const int cx = x0 + 1 + dx;
                    const int cz = z0 + 1 + dz;
                    if (cx == midX && cz == midZ) {
                        continue;
                    }
                    if (freeIndex++ % stride != 0) {
                        continue;
                    }
                    // `floorY` is the cell the fence stands in, so it is also
                    // the air the animal stands in; the grass is one lower.
                    out[written++] = {cx, pen.floorY, cz, kind};
                    ++placed;
                }
            }
        }
    }
    return written;
}

} // namespace game::village
