#include "item/Recipe.hpp"

#include "item/Tool.hpp"

#include "item/Inventory.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace game {
namespace {

constexpr ItemId kNone = ItemId::None;

/// **Every builder here is `constexpr`, and that is load-bearing rather than
/// tidiness.** It is what lets `patternsUnique()` further down evaluate the
/// whole table at compile time and prove no two recipes claim the same shape -
/// a collision the matcher resolves silently, by declaration order.
constexpr Recipe shaped(int width, int height, std::array<ItemId, kMaxCraftSlots> pattern,
                        ItemId result, int count) {
    Recipe recipe;
    recipe.pattern = pattern;
    recipe.width = width;
    recipe.height = height;
    recipe.result = ItemStack{result, count};
    return recipe;
}

constexpr Recipe shapeless(std::array<ItemId, kMaxCraftSlots> ingredients, int used, ItemId result,
                           int count) {
    Recipe recipe;
    recipe.pattern = ingredients;
    recipe.width = used;
    recipe.height = 1;
    recipe.shapeless = true;
    recipe.result = ItemStack{result, count};
    return recipe;
}

/// The two shapes that repeat most: four of a thing in a square, and nine of it
/// filling the grid. Written once because thirty of these by hand is thirty
/// chances to fill one cell with the wrong item.
constexpr Recipe square4(ItemId input, ItemId result, int count) {
    return shaped(2, 2, {input, input, input, input}, result, count);
}

constexpr Recipe square4(BlockId input, BlockId result, int count) {
    return square4(itemForBlock(input), itemForBlock(result), count);
}

/// Which slab family is cut from a given block, or -1 if none is. Scanned from
/// the family table rather than written down, so a new slab material needs no
/// edit here.
constexpr int slabFamilyOf(BlockId parent) {
    for (int family = 0; family < kSlabFamilyCount; ++family) {
        if (kSlabFamilies[static_cast<std::size_t>(family)].parent == parent) {
            return family;
        }
    }
    return -1;
}

constexpr Recipe square9(ItemId input, ItemId result) {
    return shaped(3, 3, {input, input, input, input, input, input, input, input, input}, result, 1);
}

constexpr Recipe square9(ItemId input, BlockId result) { return square9(input, itemForBlock(result)); }

/// A row of three, which is the shape the reference uses for slabs and paper.
///
/// **Not the glass bottle**, which this comment claimed for four milestones: a
/// bottle is a V, `[[Glass Bottle]]` on the wiki, and it is written out at its
/// own call site because of it.
constexpr Recipe row3(ItemId input, ItemId result, int count) {
    return shaped(3, 1, {input, input, input}, result, count);
}

/// Eight around an empty middle, and the same eight around something. Both have
/// to be stored at 3x3 rather than trimmed, because the hole is part of the
/// shape.
constexpr Recipe ring8(ItemId input, ItemId result, int count) {
    return shaped(3, 3, {input, input, input, input, kNone, input, input, input, input}, result,
                  count);
}

constexpr Recipe ring8Around(ItemId input, ItemId centre, ItemId result, int count) {
    return shaped(3, 3, {input, input, input, input, centre, input, input, input, input}, result,
                  count);
}

/// Every wood, and the three blocks each one is the same shape in. The reference
/// treats all planks as interchangeable through an item tag; ours holds concrete
/// ids, so the shared recipes are **generated per wood** instead.
///
/// **Named divergence:** you cannot mix two woods in one recipe here. The
/// reference can, and matching it would mean an ingredient that names a *set* -
/// which the craftable check explicitly relies on not existing.
struct Wood {
    BlockId log;
    BlockId stripped;
    BlockId planks;

    /// Planks from one log, or from one stripped log - **the two always agree,
    /// and bamboo is the only wood that is not 4.**
    ///
    /// Source: `Mojang/bedrock-samples` `behavior_pack/recipes/oak_planks.json`
    /// and the nine like it state `"count": 4`; `bamboo_planks.json` and
    /// `bamboo_planks_from_stripped.json` both state `"count": 2`. That is not
    /// an oversight in the reference - a bamboo block is itself nine bamboo, so
    /// four planks from one would make bamboo the cheapest wood in the game
    /// rather than the dearest.
    ///
    /// **This lives in the table because it is a fact about a wood.** It was a
    /// literal `4` in the generating loop, which is bug shape #1 exactly: a
    /// value derived somewhere other than the one table that owns it. Every
    /// bamboo plank, and so every bamboo stair, slab, door, sign, fence and
    /// raft below it, cost half what the reference charges.
    int plankYield;
};

constexpr std::array<Wood, 11> kWoods{{
    {BlockId::Log, BlockId::StrippedOakLog, BlockId::Planks, 4},
    {BlockId::SpruceLog, BlockId::StrippedSpruceLog, BlockId::SprucePlanks, 4},
    {BlockId::BirchLog, BlockId::StrippedBirchLog, BlockId::BirchPlanks, 4},
    {BlockId::JungleLog, BlockId::StrippedJungleLog, BlockId::JunglePlanks, 4},
    {BlockId::AcaciaLog, BlockId::StrippedAcaciaLog, BlockId::AcaciaPlanks, 4},
    {BlockId::DarkOakLog, BlockId::StrippedDarkOakLog, BlockId::DarkOakPlanks, 4},
    {BlockId::CherryLog, BlockId::StrippedCherryLog, BlockId::CherryPlanks, 4},
    {BlockId::MangroveLog, BlockId::StrippedMangroveLog, BlockId::MangrovePlanks, 4},
    {BlockId::CrimsonStem, BlockId::StrippedCrimsonStem, BlockId::CrimsonPlanks, 4},
    {BlockId::WarpedStem, BlockId::StrippedWarpedStem, BlockId::WarpedPlanks, 4},
    {BlockId::BambooBlock, BlockId::StrippedBambooBlock, BlockId::BambooPlanks, 2},
}};

/// **Bamboo yields two planks and every other wood yields four - stated as an
/// iff, so both directions are proved.**
///
/// Written as a biconditional rather than as kWoods[10].plankYield == 2,
/// because that form compares one side of the derivation against itself and
/// proves nothing (bug shape #11). This one fails if bamboo is ever set back to
/// 4, fails if any other wood is dropped to 2, and fails if a twelfth wood
/// arrives carrying some third number nobody checked.
constexpr bool onlyBambooYieldsTwoPlanks() {
    for (const Wood& wood : kWoods) {
        const bool isBamboo = wood.planks == BlockId::BambooPlanks;
        if (isBamboo != (wood.plankYield == 2)) {
            return false;
        }
        if (wood.plankYield != 2 && wood.plankYield != 4) {
            return false;
        }
    }
    return true;
}
static_assert(onlyBambooYieldsTwoPlanks(),
              "Every wood yields 4 planks except bamboo, which yields 2 - see "
              "bedrock-samples behavior_pack/recipes/bamboo_planks.json");

/// **The eleven woods lead all four switch-and-opening family tables, in
/// `kWoods` order, with the non-wood families behind them.**
///
/// Four files state this in prose - here, `Block.hpp` at both family runs, and
/// `Village.cpp` - and until now nothing stated it in code. The tail was proved
/// (the stone button and the three weighted plates, further down) and the head
/// was not, which is this project's "a rule that did not travel" shape wearing
/// a different hat.
///
/// **What it costs to be wrong:** add a twelfth wood to `kWoods` without
/// extending `kDoorFamilies`, and `family = 11` is no longer the bamboo door -
/// it is `kDoorFamilyCount - 1`, which *is* iron. Six birch planks would then
/// yield an **iron door**, the twelfth wood's door would be uncraftable, and
/// two recipes would produce the same item. **`patternsUnique()` cannot see
/// it**: six planks and six ingots are genuinely different patterns, so the
/// keys differ and the collision is invisible to it.
///
/// Buttons and plates carry `.parent`, so those two get an **exact** order
/// proof. Doors and trapdoors carry no parent block, so theirs is the count
/// relation plus the `metal` flag: the woods fill the front, exactly one family
/// sits behind them, and it is the only one marked metal.
constexpr bool woodsLeadEveryFamilyTable() {
    for (std::size_t w = 0; w < kWoods.size(); ++w) {
        if (kButtonFamilies[w].parent != kWoods[w].planks ||
            kPressurePlateFamilies[w].parent != kWoods[w].planks) {
            return false;
        }
        if (kDoorFamilies[w].metal || kTrapdoorFamilies[w].metal) {
            return false;
        }
    }
    if (kDoorFamilyCount != static_cast<int>(kWoods.size()) + 1 ||
        kTrapdoorFamilyCount != static_cast<int>(kWoods.size()) + 1) {
        return false;
    }
    return kDoorFamilies[kDoorFamilyCount - 1].metal &&
           kTrapdoorFamilies[kTrapdoorFamilyCount - 1].metal;
}

static_assert(woodsLeadEveryFamilyTable(),
              "kWoods and the door, trapdoor, button and pressure-plate family tables have gone "
              "out of step - the recipe loops index straight across all five, so a wood would "
              "get another wood's recipe, and a twelfth wood with no twelfth door family would "
              "craft the iron one");

/// **The fifth family table the wood loops index, and the only one that was
/// not proved.** `woodsLeadEveryFamilyTable` above covers buttons, plates,
/// doors and trapdoors; `kSlabFamilies` is read by the same loops through
/// `slabFamilyOf(wood.planks)` and had nothing under it at all.
///
/// **What it costs to be wrong is a silent skip, not a wrong answer**, which is
/// worse: both call sites are written `if (const int f = slabFamilyOf(...);
/// f >= 0)`, so a wood whose planks are not a slab parent takes the `-1`,
/// falls straight past the block and generates nothing. That is **five recipes
/// per wood** - the barrel, the composter, the lectern, the chiselled bookshelf
/// and the daylight detector, 55 across the eleven woods - vanishing on a clean
/// build with no warning, no validation error and no reader anywhere. It is
/// exactly the shape `everyChiselledParentHasASlab` further down was written
/// for, one section away and never carried up here: a rule that did not travel.
///
/// **The guard itself is right and stays.** `slabFamilyOf` genuinely can return
/// `-1` and indexing `slabAt(-1, false)` would walk off the front of the slab
/// run into unrelated blocks, so the `if` is the correct handling of a case
/// this assert now proves cannot arise. Deleting the guard on the strength of
/// this line would trade a silent skip for a silent wrong block.
///
/// Takes the wood list as an argument for the same reason `copperCutsAreSound`
/// does: a check wired to the real table can never be shown to say no.
constexpr bool everyWoodHasASlab(const std::array<Wood, kWoods.size()>& woods) {
    for (const Wood& wood : woods) {
        if (slabFamilyOf(wood.planks) < 0) {
            return false;
        }
    }
    return true;
}

/// One wood's planks replaced by `Air`, which is the exact value a short
/// `kSlabFamilies` initialiser zero-fills its tail with - so the negative pin
/// rehearses the real failure rather than an invented one.
constexpr std::array<Wood, kWoods.size()> woodsWithAPlankThatIsNoSlab() {
    std::array<Wood, kWoods.size()> broken = kWoods;
    broken[0].planks = BlockId::Air;
    return broken;
}

/// **Delete any plank row from `kSlabFamilies` and this fires.** Nothing else
/// would: every other recipe for that wood still generates, the build is clean,
/// and the only symptom is five recipes that were there yesterday and are not
/// there today.
static_assert(everyWoodHasASlab(kWoods),
              "a wood's planks are no longer a slab family parent, so slabFamilyOf returns -1 for "
              "it and the barrel, composter, lectern, chiselled bookshelf and daylight detector "
              "loops below skip that wood in silence");

static_assert(!everyWoodHasASlab(woodsWithAPlankThatIsNoSlab()),
              "the wood-slab check cannot fail, so the assert above proves nothing");

/// Which flower yields which dye. The reference's own pairings; everything not
/// named here is mixed from two others.
struct FlowerDye {
    BlockId flower;
    int colour;
};

/// Offsets into the dye run, which is white-first in the same order as the wool,
/// terracotta and concrete families - so a colour is arithmetic everywhere.
enum DyeColour {
    kWhite = 0,
    kOrange,
    kMagenta,
    kLightBlue,
    kYellow,
    kLime,
    kPink,
    kGray,
    kLightGray,
    kCyan,
    kPurple,
    kBlue,
    kBrown,
    kGreen,
    kRed,
    kBlack,
};

constexpr ItemId dye(int colour) {
    return static_cast<ItemId>(static_cast<int>(kFirstDye) + colour);
}

/// **The arithmetic against the names, at both ends and in the middle.**
/// `dye()` is the one place a colour index becomes a dye id, and nothing about
/// it is checkable by eye - the offsets above are an unbroken run of sixteen
/// bare enumerators, so inserting a seventeenth colour anywhere but the end
/// renames every dye after it and no compiler would say a word. Naming the
/// enumerator on the right is what makes this a proof rather than a restatement
/// of the same sum.
static_assert(dye(kWhite) == ItemId::WhiteDye && dye(kGreen) == ItemId::GreenDye &&
                  dye(kBrown) == ItemId::BrownDye && dye(kBlack) == ItemId::BlackDye &&
                  static_cast<int>(kBlack) == kDyeColours - 1,
              "the DyeColour offsets no longer land on the dye ids they are named after - a "
              "colour was inserted into one run and not the other");

constexpr std::array<FlowerDye, 12> kFlowerDyes{{
    {BlockId::Dandelion, kYellow},
    {BlockId::Poppy, kRed},
    {BlockId::Cornflower, kBlue},
    {BlockId::OxeyeDaisy, kLightGray},
    {BlockId::AzureBluet, kLightGray},
    {BlockId::Allium, kMagenta},
    {BlockId::RedTulip, kRed},
    {BlockId::OrangeTulip, kOrange},
    {BlockId::BlueOrchid, kLightBlue},
    {BlockId::PinkTulip, kPink},
    {BlockId::WhiteTulip, kLightGray},
    {BlockId::LilyOfTheValley, kWhite},
}};

/// The mixes, exactly the reference's.
struct DyeMix {
    int result;
    int a;
    int b;
};

constexpr std::array<DyeMix, 9> kDyeMixes{{
    {kOrange, kRed, kYellow},
    {kLime, kGreen, kWhite},
    {kLightBlue, kBlue, kWhite},
    {kMagenta, kPurple, kPink},
    {kPink, kRed, kWhite},
    {kGray, kBlack, kWhite},
    {kLightGray, kGray, kWhite},
    {kCyan, kBlue, kGreen},
    {kPurple, kBlue, kRed},
}};

/// A colour's member of a sixteen-long block run, which every dyed family is.
constexpr ItemId tinted(BlockId first, int colour) {
    return itemForBlock(static_cast<BlockId>(static_cast<int>(first) + colour));
}

/// Every tool is its material over a stick or two, and every shape is the same
/// for both materials - so the table is generated rather than written out
/// twenty times.
///
/// The pattern is written as a 3x3 picture and **stored trimmed to the cells it
/// actually fills**, because the matcher compares a recipe against the bounding
/// box of what is in the grid. Padded to 3x3, every shape with an empty column
/// - the axe, shovel, sword and hoe - could never match anything.
struct ToolShape {
    const char* pattern;
    ItemId wooden;
    ItemId stone;
    ItemId iron;
    ItemId diamond;
    ItemId emberite;
};

/// The five shapes, at namespace scope so `toolPatternsAreLegible` can be
/// proved against them. Every one is the reference's own - `[[Pickaxe]]`,
/// `[[Axe]]`, `[[Shovel]]`, `[[Sword]]` and `[[Hoe]]` on the wiki.
constexpr std::array<ToolShape, 5> kToolShapes{{
    {"MMM.S..S.", ItemId::WoodenPickaxe, ItemId::StonePickaxe, ItemId::IronPickaxe,
     ItemId::DiamondPickaxe, ItemId::EmberitePickaxe},
    {"MM.MS..S.", ItemId::WoodenAxe, ItemId::StoneAxe, ItemId::IronAxe, ItemId::DiamondAxe,
     ItemId::EmberiteAxe},
    {".M..S..S.", ItemId::WoodenShovel, ItemId::StoneShovel, ItemId::IronShovel,
     ItemId::DiamondShovel, ItemId::EmberiteShovel},
    {".M..M..S.", ItemId::WoodenSword, ItemId::StoneSword, ItemId::IronSword,
     ItemId::DiamondSword, ItemId::EmberiteSword},
    {"MM..S..S.", ItemId::WoodenHoe, ItemId::StoneHoe, ItemId::IronHoe, ItemId::DiamondHoe,
     ItemId::EmberiteHoe},
}};

/// Every picture is exactly nine cells of `M`, `S` or `.`, and nothing else.
///
/// **This is what stops `tool`'s `default:` being silent.** A mistyped letter
/// still counts as *occupied* when the bounding box is measured, so it would
/// widen the shape and then leave that cell empty - a recipe nobody can craft
/// and nothing to say so. A short string does the same from the other end, by
/// reading the picture after it.
constexpr bool toolPatternsAreLegible() {
    for (const ToolShape& shape : kToolShapes) {
        for (std::size_t c = 0; c < kMaxCraftSlots; ++c) {
            const char cell = shape.pattern[c];
            if (cell != 'M' && cell != 'S' && cell != '.') {
                return false;
            }
        }
        if (shape.pattern[kMaxCraftSlots] != '\0') {
            return false;
        }
    }
    return true;
}

static_assert(toolPatternsAreLegible(),
              "a tool pattern is not nine characters of 'M', 'S' and '.' - which would widen the "
              "shape by a cell it then leaves empty, and make the tool uncraftable in silence");

constexpr Recipe tool(ItemId material, ItemId result, const char* pattern) {
    int minX = kMaxCraftSize;
    int minY = kMaxCraftSize;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < kMaxCraftSize; ++y) {
        for (int x = 0; x < kMaxCraftSize; ++x) {
            if (pattern[y * kMaxCraftSize + x] == '.') {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    std::array<ItemId, kMaxCraftSlots> cells{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            switch (pattern[(minY + y) * kMaxCraftSize + minX + x]) {
            case 'M':
                cells[static_cast<std::size_t>(y * width + x)] = material;
                break;
            case 'S':
                cells[static_cast<std::size_t>(y * width + x)] = ItemId::Stick;
                break;
            case '.':
            default:
                // Unreachable, and `toolPatternsAreLegible` above is the proof
                // rather than a comment: the only characters that can arrive
                // here are the three, and this one is the gap.
                break;
            }
        }
    }

    Recipe recipe;
    recipe.pattern = cells;
    recipe.width = width;
    recipe.height = height;
    recipe.result = ItemStack{result, 1};
    return recipe;
}

/// The copper forms that can still oxidise, in the order the waxed run repeats
/// them - the loop below adds the offset to `WaxedCopperBlock`, so this order
/// is load-bearing.
///
/// **At namespace scope on purpose.** A `static constexpr` local inside a
/// `constexpr` function is a C++23 relaxation, not a C++20 one; MSVC accepts
/// the declaration and then quietly refuses to constant-evaluate the whole
/// function, which is exactly the kind of silence `patternsUnique()` exists to
/// end. Three arrays lived there and cost the assert its proof.
constexpr BlockId kUnwaxed[] = {
    BlockId::CopperBlock,        BlockId::ExposedCopper,
    BlockId::WeatheredCopper,    BlockId::OxidizedCopper,
    BlockId::CutCopper,          BlockId::ExposedCutCopper,
    BlockId::WeatheredCutCopper, BlockId::OxidizedCutCopper,
    BlockId::ChiseledCopper,
};

/// **The ninth entry, which nothing else pins.** `copperCutsAreSound` below
/// checks indices 0-3 and 4-7 against `kCopperCuts`, and that is where it
/// stops - but the waxing loop walks all nine and derives its result as
/// `WaxedCopperBlock + i`, so chiselled copper's row has been riding on the
/// enum order with no check under it at all. Insert one enumerator anywhere in
/// the nine-long waxed run and a honeycomb on chiselled copper produces some
/// other block entirely, silently, with no other symptom.
///
/// Written as the **whole expression the loop evaluates, base and all** - the
/// cast off `WaxedCopperBlock`, not a comparison of one side of the derivation
/// against itself. This project has had eleven `static_assert`s pass while
/// pointing at the wrong texture for exactly that reason.
static_assert(std::size(kUnwaxed) == 9 && kUnwaxed[8] == BlockId::ChiseledCopper &&
                  static_cast<BlockId>(static_cast<int>(BlockId::WaxedCopperBlock) + 8) ==
                      BlockId::WaxedChiseledCopper,
              "the last unwaxed form and the last waxed one must stay at the same offset - "
              "chiselled copper is the only entry in kUnwaxed that kCopperCuts does not also "
              "carry, so this line is the whole of its protection");

constexpr BlockId kBarkLogs[] = {
    BlockId::Log,         BlockId::SpruceLog,   BlockId::BirchLog,
    BlockId::JungleLog,   BlockId::AcaciaLog,   BlockId::DarkOakLog,
    BlockId::CherryLog,   BlockId::MangroveLog, BlockId::CrimsonStem,
    BlockId::WarpedStem,
};

constexpr BlockId kStrippedLogs[] = {
    BlockId::StrippedOakLog,      BlockId::StrippedSpruceLog,
    BlockId::StrippedBirchLog,    BlockId::StrippedJungleLog,
    BlockId::StrippedAcaciaLog,   BlockId::StrippedDarkOakLog,
    BlockId::StrippedCherryLog,   BlockId::StrippedMangroveLog,
    BlockId::StrippedCrimsonStem, BlockId::StrippedWarpedStem,
};

static_assert(std::size(kBarkLogs) == std::size(kStrippedLogs),
              "the two log runs are walked by one index, so a wood in one and not the other would "
              "give a stripped block its unstripped recipe");

/// The four copper grates, which `kUnwaxed` above cannot reach and must not be
/// added to.
///
/// That loop derives its result by adding a table index to `WaxedCopperBlock`,
/// and the grates are **a separate enum run** with their own waxed forms four
/// ids along - so the nine-stage offset walks straight past them and all four
/// waxed grates had no recipe at all. Same shape as the copper-bulb gap: a
/// table-driven derivation that covers one run and silently misses a sibling.
///
/// Mojang's own `behavior_pack/recipes/waxing_copper_grate.json` seals a grate
/// with a honeycomb on exactly the same terms as every other copper form, so
/// this is a primary-source fact rather than a reading of the reference's wiki.
///
/// **The published files are named `waxing_*`, not `waxed_*`.** A glob for the
/// latter matches nothing at all, which reads as "the reference does not
/// publish waxing" - a wrong conclusion this audit reached once and had to
/// withdraw. Search the recipe list by substring, never by a guessed prefix.
constexpr int kCopperGrateStages = 4;

static_assert(static_cast<int>(BlockId::OxidizedCopperGrate) -
                      static_cast<int>(BlockId::CopperGrate) + 1 == kCopperGrateStages &&
                  static_cast<int>(BlockId::WaxedCopperGrate) -
                      static_cast<int>(BlockId::CopperGrate) == kCopperGrateStages &&
                  static_cast<int>(BlockId::WaxedOxidizedCopperGrate) -
                      static_cast<int>(BlockId::OxidizedCopperGrate) == kCopperGrateStages,
              "the four grates and their four waxed forms must stay one contiguous run, waxed "
              "second - insert one enumerator between OxidizedCopperGrate and WaxedCopperGrate "
              "and a honeycomb would seal a grate into some other block entirely");

/// **The reference publishes 25 shaped waxed-copper recipes and this is where
/// 8 of them live. Audited 2026-08-19 against the primary source, not the
/// wiki.** A listing of all 1756 files in `behavior_pack/recipes` contains 27
/// paths matching `*waxed*`, of which 25 are `crafting_table_waxed_*.json`:
/// 6 shapes over 4 oxidation stages (chiselled, bulb, grate, cut, cut slab,
/// cut stairs) plus a single `crafting_table_waxed_copper_door.json`. Four of
/// the 25 are the copper bulbs, which want a blaze rod and are Nether and
/// therefore out of scope, leaving **21 in scope**.
///
/// **RE-MEASURED 2026-08-19 15:30, after `Block.hpp` landed the 40 waxed ids.
/// 17 of the 21 are now live and 4 remain blocked** - the paragraph that used
/// to sit here said 8 live and 13 blocked and was true only until those ids
/// arrived. Of the 21:
///
///  - **8 are here**, and they are the two families this table drives: waxed
///    cut copper (2x2 -> 4) and the waxed grate (diamond -> 4), four stages
///    each. Shapes and yields read off `crafting_table_waxed_cut_copper.json`
///    and `crafting_table_waxed_copper_grate.json`, which are identical to the
///    unwaxed files beside them and differ only in the block named.
///  - **8 more came for free and cost this file nothing**, exactly as the
///    paragraph below predicted: `Block.hpp` appended `WaxedCutCopperStairsRun`
///    and `WaxedCutCopperSlabRun` as a tail run and added the four waxed cut
///    coppers to `kStairFamilies` and `kSlabFamilies`, and the two family loops
///    in `buildRecipes` walk `kStairFamilyCount` and `kSlabFamilyCount`, which
///    are totals including the tail. **Verified rather than assumed** - both
///    tables were counted declared-against-literal (56 == 56, 59 == 59), which
///    is what catches the failure mode that would look like a bug in this file:
///    ids appended without a family row have no parent, generate no recipe, and
///    report nothing anywhere.
///  - **1 more was landed by hand today**, and only because its *input* finally
///    existed: waxed chiselled copper stage 0, two waxed cut copper slabs
///    stacked, added as a row in `kChiselledFromSlabs` below. It is the worked
///    example of the rule that a recipe is blocked by its inputs as well as its
///    output - `WaxedChiseledCopper` has had an id all along and the recipe was
///    still unwritable.
///  - **4 remain blocked, all for want of a `BlockId`.** Three waxed chiselled
///    coppers: only oxidation stage 0 exists, so exposed, weathered and
///    oxidised have no output at all. One waxed copper **door**: there is no
///    copper door in this game in any form, waxed or bare, so that one needs an
///    id *and* a new door family.
///
/// **A missing id is a `Block.hpp` finding, not a recipe waiting to be typed.**
/// Do not stand a different block in for a missing output - that is inventing a
/// recipe, and the honeycomb route already reaches `WaxedChiseledCopper`
/// anyway, so nothing here is unobtainable for want of these rows.
///
/// **AND WHEN THOSE IDS DO ARRIVE, DO NOT COME BACK HERE AND TYPE ROWS FOR THE
/// DERIVED SHAPES.** That is the whole point of the paragraph above and it is
/// easy to miss: the blocker is upstream, so the *fix* is upstream too, and
/// this file needed no new rows for the stairs and slabs when they landed.
/// `Copper.hpp` already exports `waxedForm` and `unwaxedForm`, they already
/// cover derived stairs and slabs, and the stair and slab families in
/// `Block.hpp` are what gain the new parents. Once a waxed cut copper stair has
/// an id, it is a stair whose parent is a waxed cut copper, and the loop below
/// already emits a recipe for every family member it is given. **Hand-written
/// waxed stair and slab rows would be a second answer to a question
/// `waxedForm` already answers** - bug shape #1, and precisely what collapsing
/// the oxidation matrix into one table was built to prevent. If landing an id
/// does not make its recipe appear by itself, the bug is that the new id was
/// not put in its family, and the repair is there rather than a table of
/// literals here. **The chiselled row is the one exception and is not one:**
/// chiselled copper is not a derived shape, it has no family table, and the
/// four rows in `kChiselledFromSlabs` are already the only place that shape
/// lives.
///
/// The stock block each cut form and each grate is cut from, both families and
/// all eight oxidation stages in one table, because the rows differ by exactly
/// one thing: which stock they name.
///
/// **Written out rather than walked by arithmetic, and that is not caution.**
/// `CopperBlock` does not sit next to its own oxidation run - `ExposedCopper`
/// and the two after it are a separate contiguous block hundreds of ids away -
/// so a `CopperBlock + stage` walk lands in unrelated blocks from its very
/// first step. The waxed stock *is* contiguous, which is the trap rather than
/// the reassurance: writing one family as arithmetic because it happens to be
/// adjacent and the other as a table is how the two drift apart. The assert
/// below ties this table back to the runs that already own those facts.
///
/// Shapes and yields are Mojang's own published recipes, not the wiki:
/// `behavior_pack/recipes/crafting_table_cut_copper.json` and
/// `crafting_table_copper_grate.json`, plus the oxidised and waxed files beside
/// them, which are identical in shape and differ only in the block named.
///
/// **The grate is a diamond, not a square** - a hole, a block, a hole over a
/// block, a hole, a block over the first row again: four blocks around an empty
/// centre, yielding 4. That is the one value here a reader would otherwise
/// assume wrong, because every other 4-in-4-out copper recipe on this page is a
/// 2x2 and a grate looks like one in the inventory.
struct CopperCut {
    BlockId stock; ///< the solid block both products are cut from
    BlockId cut;   ///< `stock` as a 2x2 -> 4 of these
    BlockId grate; ///< `stock` as a diamond -> 4 of these
};

constexpr std::array<CopperCut, 8> kCopperCuts{{
    {BlockId::CopperBlock, BlockId::CutCopper, BlockId::CopperGrate},
    {BlockId::ExposedCopper, BlockId::ExposedCutCopper, BlockId::ExposedCopperGrate},
    {BlockId::WeatheredCopper, BlockId::WeatheredCutCopper, BlockId::WeatheredCopperGrate},
    {BlockId::OxidizedCopper, BlockId::OxidizedCutCopper, BlockId::OxidizedCopperGrate},
    {BlockId::WaxedCopperBlock, BlockId::WaxedCutCopper, BlockId::WaxedCopperGrate},
    {BlockId::WaxedExposedCopper, BlockId::WaxedExposedCutCopper, BlockId::WaxedExposedCopperGrate},
    {BlockId::WaxedWeatheredCopper, BlockId::WaxedWeatheredCutCopper,
     BlockId::WaxedWeatheredCopperGrate},
    {BlockId::WaxedOxidizedCopper, BlockId::WaxedOxidizedCutCopper,
     BlockId::WaxedOxidizedCopperGrate},
}};

/// Checks the table against the runs that already own those ids, rather than
/// against itself.
///
/// Takes the table as an argument for one reason: a check that can only ever be
/// handed the real table cannot be shown to fail, and an assert comparing one
/// side of a derivation against itself proves nothing. The negative assert below
/// feeds it a table with one row corrupted and requires a `false`.
constexpr bool copperCutsAreSound(const std::array<CopperCut, 8>& table) {
    for (std::size_t a = 0; a < table.size(); ++a) {
        // No stock may be its own product, and no id may appear in two columns
        // of one row - that is what a copy-paste slip looks like here.
        if (table[a].stock == table[a].cut || table[a].cut == table[a].grate ||
            table[a].stock == table[a].grate) {
            return false;
        }
        for (std::size_t b = a + 1; b < table.size(); ++b) {
            if (table[a].stock == table[b].stock || table[a].cut == table[b].cut ||
                table[a].grate == table[b].grate) {
                return false;
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        const std::size_t plain = static_cast<std::size_t>(i);
        const std::size_t waxed = plain + 4;
        // The unwaxed stock and its cut form are the first and fifth quarters of
        // `kUnwaxed`, which is the table the waxing loop already walks.
        if (table[plain].stock != kUnwaxed[i] || table[plain].cut != kUnwaxed[i + 4]) {
            return false;
        }
        // The waxed halves are the offsets that same loop derives, so if a new
        // enumerator ever splits either run this stops agreeing with it.
        if (table[waxed].stock !=
                static_cast<BlockId>(static_cast<int>(BlockId::WaxedCopperBlock) + i) ||
            table[waxed].cut !=
                static_cast<BlockId>(static_cast<int>(BlockId::WaxedCopperBlock) + i + 4)) {
            return false;
        }
        // And the grates are their own contiguous run, unwaxed then waxed, which
        // is precisely what `kCopperGrateStages` asserts above.
        if (table[plain].grate != static_cast<BlockId>(static_cast<int>(BlockId::CopperGrate) + i) ||
            table[waxed].grate !=
                static_cast<BlockId>(static_cast<int>(BlockId::CopperGrate) + kCopperGrateStages +
                                     i)) {
            return false;
        }
    }
    return true;
}

/// One row deliberately broken, so the check above is shown to be able to fail.
/// Copies a neighbour's grate over row 5's, which is the exact mistake a hand
/// edit to the waxed half would make.
constexpr std::array<CopperCut, 8> copperCutsWithADuplicateGrate() {
    std::array<CopperCut, 8> broken = kCopperCuts;
    broken[5].grate = broken[4].grate;
    return broken;
}

static_assert(copperCutsAreSound(kCopperCuts),
              "a copper cut row disagrees with kUnwaxed, with the waxed offsets or with the grate "
              "run - one of the eight stages is cutting into the wrong block");

static_assert(!copperCutsAreSound(copperCutsWithADuplicateGrate()),
              "the copper cut check cannot fail, so the assert above proves nothing");

/// The four chiselled blocks the reference makes from two of their own slabs
/// stacked, rather than from the whole block - wiki `[[Chiseled Deepslate]]`,
/// `[[Chiseled Tuff]]` and `[[Chiseled Quartz Block]]` all show the same 1x2.
///
/// Chiselled copper is the same shape and is sourced primarily rather than from
/// the wiki: `behavior_pack/recipes/crafting_table_chiseled_copper.json` is two
/// `cut_copper_slab` stacked, and it **omits** the `count` field, so the yield
/// is 1. That omission is meaningful rather than an oversight - the cut copper
/// file beside it states `"count": 4` explicitly.
///
/// The **parent** is stored, not the slab, so the slab id is read out of
/// `kSlabFamilies` at the recipe rather than named twice.
struct ChiselledFromSlabs {
    BlockId parent;
    BlockId result;
};

constexpr ChiselledFromSlabs kChiselledFromSlabs[] = {
    {BlockId::CobbledDeepslate, BlockId::ChiseledDeepslate},
    {BlockId::Tuff, BlockId::ChiseledTuff},
    {BlockId::QuartzBlock, BlockId::ChiseledQuartz},
    // Chiselled copper had no recipe at all, so the only chiselled block in the
    // game that a honeycomb can seal was itself unobtainable.
    {BlockId::CutCopper, BlockId::ChiseledCopper},
    // **Landed 2026-08-19, the moment the waxed cut copper slab got an id.**
    // `crafting_table_waxed_chiseled_copper.json` is two `waxed_cut_copper_slab`
    // stacked, and until today its *input* had no `BlockId` even though its
    // *output* did - the exact trap the note above `kCopperCuts` warns about,
    // where counting only outputs says a recipe is writable when it is not.
    // `kSlabFamilies` now carries `WaxedCutCopper` in its tail run, so
    // `slabFamilyOf` finds it and the assert below covers this row like the
    // other four. Only oxidation stage 0 exists; the exposed, weathered and
    // oxidised waxed chiselled coppers still have no id at all, which is a
    // `Block.hpp` gap and not a row waiting to be typed here.
    //
    // **This is a second route to a block a honeycomb already reaches, and the
    // reference has both** - so it is not a duplicate. It is a different shape
    // over different ingredients, which is what `noRecipeSharesAGrid` and
    // `patternsUnique` below actually test; a shared *output* is not a clash.
    {BlockId::WaxedCutCopper, BlockId::WaxedChiseledCopper},
};

constexpr bool everyChiselledParentHasASlab() {
    for (const ChiselledFromSlabs& row : kChiselledFromSlabs) {
        if (slabFamilyOf(row.parent) < 0) {
            return false;
        }
    }
    return true;
}

/// **Delete `{BlockId::Tuff, "Tuff Slab"}` from `kSlabFamilies` and this
/// fires.** Without it the loop below would take the `-1`, quietly skip the
/// row, and leave the block uncraftable again with a clean build - which is
/// precisely the state this recipe is fixing.
static_assert(everyChiselledParentHasASlab(),
              "each chiselled block is two slabs of its own family, so every parent named here "
              "must still have a slab family to be cut from");

/// The pane family table's colours must sit one along from its plain glass, in
/// the same white-first order as every other dyed run - which is what lets the
/// stained-pane dye recipe below say `paneAt(colour + 1)`.
constexpr bool paneColoursFollowTheDyeRun() {
    if (kPaneFamilyCount != kDyeColours + 1 || kPaneFamilies[0].parent != BlockId::Glass) {
        return false;
    }
    for (int colour = 0; colour < kDyeColours; ++colour) {
        if (itemForBlock(kPaneFamilies[static_cast<std::size_t>(colour) + 1].parent) !=
            tinted(BlockId::WhiteStainedGlass, colour)) {
            return false;
        }
    }
    return true;
}

/// **Swap any two rows in `kPaneFamilies` and this fires.** Nothing else would:
/// both panes exist, both recipes are unique, and eight glass panes round a red
/// dye would simply produce the wrong colour.
static_assert(paneColoursFollowTheDyeRun(),
              "the pane family table must be plain glass followed by the sixteen stained glasses "
              "in white-first order, because the dye recipe indexes it as colour + 1");

/// Every recipe in the game.
///
/// Shapes and yields are taken from the reference recipe data - see
/// `CRAFTABLE.md`, which records each one and where it came from.
///
/// **`constexpr`, and its own function rather than an initialiser**, so
/// `patternsUnique()` below can run the whole thing at compile time and prove
/// the table has no shape in it twice. There is exactly one builder, so what is
/// proved is what ships.
constexpr std::vector<Recipe> buildRecipes() {
    return []() constexpr {
        std::vector<Recipe> all{
            // A ring of eight, hollow in the middle - which is why the pattern
            // has to be stored at 3x3 and cannot be trimmed to its filled cells.
            ring8(itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::Furnace), 1),
            // Charcoal comes from smelting a log, so torches need no ore at all.
            shaped(1, 2, {ItemId::Charcoal, ItemId::Stick}, itemForBlock(BlockId::Torch), 4),
            shaped(1, 2, {ItemId::Coal, ItemId::Stick}, itemForBlock(BlockId::Torch), 4),
            shaped(2, 2,
                   {itemForBlock(BlockId::Stone), itemForBlock(BlockId::Stone),
                    itemForBlock(BlockId::Stone), itemForBlock(BlockId::Stone)},
                   itemForBlock(BlockId::StoneBricks), 4),
            shaped(2, 2,
                   {itemForBlock(BlockId::Sand), itemForBlock(BlockId::Sand), itemForBlock(BlockId::Sand),
                    itemForBlock(BlockId::Sand)},
                   itemForBlock(BlockId::Sandstone), 1),
            // Cobblestone and a vine, which is the reference's own shapeless
            // recipe. This was tall grass until 2026-08-19, under a note saying
            // vines "do not exist here yet". They do, and had for some time:
            // `isVine` covers a sixteen-id run, worldgen hangs them, the player
            // places them, and `shearsHarvests` in `BlockDrops.hpp` gives one
            // back. **The file already contradicted itself** - the shears row
            // below calls them "the only thing that collects a vine", so both
            // sentences could not be true at once. Nothing revisited the note
            // when vines landed, which is the measured direction: a claim that
            // something is ABSENT rots, because the world only has to move once.
            //
            // **Spelled the way `dropForBlock` spells it**, rather than by
            // naming an id. A vine is one id per combination of sides it clings
            // to, and the one that reaches an inventory is the all-sides form;
            // deriving the ingredient from the same expression the drop uses is
            // what stops this asking for an id no drop ever produces. Falsifier:
            // `Item.hpp`'s `isVine` arm returning anything but `vineWith`.
            shapeless({itemForBlock(BlockId::Cobblestone), itemForBlock(vineWith(ConnectAll))}, 2,
                      itemForBlock(BlockId::MossyCobblestone), 1),
            // Coarse dirt is the reference's own 2x2 checker of dirt and gravel.
            shaped(2, 2,
                   {itemForBlock(BlockId::Dirt), itemForBlock(BlockId::Gravel),
                    itemForBlock(BlockId::Gravel), itemForBlock(BlockId::Dirt)},
                   itemForBlock(BlockId::CoarseDirt), 4),
            // Prismarine is quarried rather than crafted in the reference, from
            // shards a drowned drops. With neither in the game it is built from
            // the ocean floor it belongs to.
            shaped(2, 2,
                   {itemForBlock(BlockId::Gravel), itemForBlock(BlockId::Clay),
                    itemForBlock(BlockId::Clay), itemForBlock(BlockId::Gravel)},
                   itemForBlock(BlockId::Prismarine), 4),
            shaped(2, 2,
                   {itemForBlock(BlockId::Prismarine), itemForBlock(BlockId::Prismarine),
                    itemForBlock(BlockId::Prismarine), itemForBlock(BlockId::Glowstone)},
                   itemForBlock(BlockId::SeaLantern), 1),

            // 2x2 polish, the reference's own shape for all three.
            square4(BlockId::Andesite, BlockId::PolishedAndesite, 4),
            square4(BlockId::Diorite, BlockId::PolishedDiorite, 4),
            square4(BlockId::Granite, BlockId::PolishedGranite, 4),
            // And the three base stones themselves, which were worldgen-only.
            // The reference builds all three out of cobblestone and nether
            // quartz, which is the one overworld use quartz has - wiki
            // `[[Diorite]]` is the 2x2 checker yielding two, `[[Andesite]]` is
            // a diorite and a cobblestone yielding two, `[[Granite]]` is a
            // diorite and a quartz yielding one.
            shaped(2, 2,
                   {itemForBlock(BlockId::Cobblestone), ItemId::Quartz, ItemId::Quartz,
                    itemForBlock(BlockId::Cobblestone)},
                   itemForBlock(BlockId::Diorite), 2),
            shapeless({itemForBlock(BlockId::Diorite), itemForBlock(BlockId::Cobblestone)}, 2,
                      itemForBlock(BlockId::Andesite), 2),
            shapeless({itemForBlock(BlockId::Diorite), ItemId::Quartz}, 2,
                      itemForBlock(BlockId::Granite), 1),
            square4(BlockId::CobbledDeepslate, BlockId::PolishedDeepslate, 4),
            square4(BlockId::PolishedDeepslate, BlockId::DeepslateBricks, 4),
            square4(BlockId::DeepslateBricks, BlockId::DeepslateTiles, 4),
            square4(BlockId::Prismarine, BlockId::PrismarineBricks, 4),
            square4(BlockId::Sandstone, BlockId::CutSandstone, 4),
            // The mossy cobblestone rule above, on stone bricks - the reference
            // moss-grows both the same way. **This one carried no comment at
            // all**, so it inherited the vine premise silently and a sweep for
            // the note would never have found it: one recipe was documented as
            // a divergence and its twin just looked like a recipe.
            shapeless({itemForBlock(BlockId::StoneBricks), itemForBlock(vineWith(ConnectAll))}, 2,
                      itemForBlock(BlockId::MossyStoneBricks), 1),

            // Nine into one, the storage-block shape, **and back out again** -
            // the reference makes every one of these reversible and the note
            // that used to sit here ("one way until a 1x1 unpack recipe
            // exists") was describing a missing row rather than a decision.
            // Nine loose in, one block out; one block in, nine loose out.
            square9(ItemId::Coal, BlockId::CoalBlock),
            square9(ItemId::IronIngot, BlockId::IronBlock),
            square9(ItemId::GoldIngot, BlockId::GoldBlock),
            square9(ItemId::Diamond, BlockId::DiamondBlock),
            square9(ItemId::Emerald, BlockId::EmeraldBlock),
            square9(ItemId::LapisLazuli, BlockId::LapisBlock),
            square9(ItemId::Redstone, BlockId::RedstoneBlock),
            square9(ItemId::CopperIngot, BlockId::CopperBlock),
            shapeless({itemForBlock(BlockId::CoalBlock)}, 1, ItemId::Coal, 9),
            shapeless({itemForBlock(BlockId::IronBlock)}, 1, ItemId::IronIngot, 9),
            shapeless({itemForBlock(BlockId::GoldBlock)}, 1, ItemId::GoldIngot, 9),
            shapeless({itemForBlock(BlockId::DiamondBlock)}, 1, ItemId::Diamond, 9),
            shapeless({itemForBlock(BlockId::EmeraldBlock)}, 1, ItemId::Emerald, 9),
            shapeless({itemForBlock(BlockId::LapisBlock)}, 1, ItemId::LapisLazuli, 9),
            shapeless({itemForBlock(BlockId::RedstoneBlock)}, 1, ItemId::Redstone, 9),
            shapeless({itemForBlock(BlockId::CopperBlock)}, 1, ItemId::CopperIngot, 9),
            // **And the waxed copper block, which is the one that did not
            // travel.** The reference publishes two unpack recipes for copper,
            // not one: `behavior_pack/recipes/ingots_from_copper.json` is the
            // line above and `ingots_from_waxed_copper.json` is this one, both
            // 1 block -> 9 `minecraft:copper_ingot`. Wax is a promise a block
            // will not oxidise, not a promise it can never be spent, so sealing
            // a block of copper used to strand nine ingots for good.
            //
            // **Only the un-oxidised waxed block, and that is the reference's
            // own line rather than an omission here.** A listing of all 1756
            // published recipes has four files naming copper and an ingot -
            // `copper_block_from_ingots`, `copper_ingot_from_nuggets`,
            // `ingots_from_copper` and `ingots_from_waxed_copper` - and the
            // last two are the only unpack pair there is. There is no
            // `ingots_from_exposed_copper` and no waxed-exposed one either, so
            // an oxidised block stays oxidised: weathering is one-way in both
            // editions, and only pristine copper goes back to ingots.
            shapeless({itemForBlock(BlockId::WaxedCopperBlock)}, 1, ItemId::CopperIngot, 9),

            // The two ices are the deliberate exception: the reference has no
            // way back from packed or blue ice either, so these stay one-way.
            square9(itemForBlock(BlockId::Ice), BlockId::PackedIce),
            square9(itemForBlock(BlockId::PackedIce), BlockId::BlueIce),
            // Three ingots in a V. Stored 3x2 rather than trimmed, because the
            // empty cells are part of the shape.
            shaped(3, 2,
                   {ItemId::IronIngot, kNone, ItemId::IronIngot, kNone, ItemId::IronIngot, kNone},
                   ItemId::Bucket, 1),
            // Four scrap and four gold, the reference's own recipe. Shapeless,
            // so it needs a table only because eight ingredients will not fit a
            // 2x2 - which `fitsInTwoByTwo` works out from the count for itself.
            shapeless({ItemId::EmberiteScrap, ItemId::EmberiteScrap, ItemId::EmberiteScrap,
                       ItemId::EmberiteScrap, ItemId::GoldIngot, ItemId::GoldIngot, ItemId::GoldIngot,
                       ItemId::GoldIngot},
                      8, ItemId::EmberiteIngot, 1),
            // Nine ingots into a block and back out again, like every other
            // metal in the reference.
            shaped(3, 3,
                   {ItemId::EmberiteIngot, ItemId::EmberiteIngot, ItemId::EmberiteIngot,
                    ItemId::EmberiteIngot, ItemId::EmberiteIngot, ItemId::EmberiteIngot,
                    ItemId::EmberiteIngot, ItemId::EmberiteIngot, ItemId::EmberiteIngot},
                   itemForBlock(BlockId::EmberiteBlock), 1),
            shapeless({itemForBlock(BlockId::EmberiteBlock)}, 1, ItemId::EmberiteIngot, 9),
        };

        // Armour. **Generated from one shape table and one material table**,
        // because four shapes times six materials written out is twenty-four
        // chances to put a cell in the wrong place - and the shapes are shared
        // exactly, which is the whole reason this is a loop.
        //
        // Chainmail is deliberately absent: the reference does not craft it
        // either, and a made-up recipe would be worse than leaving it to loot.
        // Emberite is absent for the same reason its tools are - it is a
        // smithing upgrade, not a recipe.
        {
            struct ArmourMaterial {
                ItemId unit;
                ItemId firstPiece;
            };
            const std::array<ArmourMaterial, 4> kArmourMaterials{{
                {ItemId::Leather, ItemId::LeatherHelmet},
                {ItemId::IronIngot, ItemId::IronHelmet},
                {ItemId::GoldIngot, ItemId::GoldenHelmet},
                {ItemId::Diamond, ItemId::DiamondHelmet},
            }};
            for (const ArmourMaterial& material : kArmourMaterials) {
                const ItemId u = material.unit;
                const auto piece = [&](int slot) {
                    return static_cast<ItemId>(static_cast<int>(material.firstPiece) + slot);
                };
                // Five across the top and sides.
                all.push_back(shaped(3, 2, {u, u, u, u, kNone, u}, piece(0), 1));
                all.push_back(
                    shaped(3, 3, {u, kNone, u, u, u, u, u, u, u}, piece(1), 1));
                all.push_back(
                    shaped(3, 3, {u, u, u, u, kNone, u, u, kNone, u}, piece(2), 1));
                all.push_back(shaped(3, 2, {u, kNone, u, u, kNone, u}, piece(3), 1));
            }
            // The one head that belongs to no set, from the shell an armoured
            // reptile leaves behind.
            all.push_back(shaped(3, 2,
                                 {ItemId::Scute, ItemId::Scute, ItemId::Scute, ItemId::Scute, kNone,
                                  ItemId::Scute},
                                 ItemId::TurtleHelmet, 1));
        }

        // The farm's own blocks and the equipment that came with the batch.
        all.push_back(shapeless({ItemId::Wheat, ItemId::Wheat, ItemId::Wheat, ItemId::Wheat,
                                 ItemId::Wheat, ItemId::Wheat, ItemId::Wheat, ItemId::Wheat,
                                 ItemId::Wheat},
                                9, itemForBlock(BlockId::HayBlock), 1));
        all.push_back(shapeless({itemForBlock(BlockId::HayBlock)}, 1, ItemId::Wheat, 9));
        // A lit pumpkin is the carved one with a light put in it.
        all.push_back(shapeless({itemForBlock(BlockId::CarvedPumpkinFirst), itemForBlock(BlockId::Torch)},
                                2, itemForBlock(BlockId::JackOLanternFirst), 1));
        // Both dials are four ingots in a diamond round one redstone. They had
        // been written as the cauldron's U of seven, which cost three ingots
        // too many and, because the compass is declared first, **shadowed the
        // cauldron's own recipe so it could not be made at all**.
        all.push_back(shaped(3, 3,
                             {kNone, ItemId::IronIngot, kNone, ItemId::IronIngot, ItemId::Redstone,
                              ItemId::IronIngot, kNone, ItemId::IronIngot, kNone},
                             ItemId::Compass, 1));
        all.push_back(shaped(3, 3,
                             {kNone, ItemId::GoldIngot, kNone, ItemId::GoldIngot, ItemId::Redstone,
                              ItemId::GoldIngot, kNone, ItemId::GoldIngot, kNone},
                             ItemId::Clock, 1));
        all.push_back(square9(ItemId::Paper, ItemId::EmptyMap));
        all.push_back(shaped(3, 3,
                             {kNone, kNone, ItemId::Stick, kNone, ItemId::Stick, ItemId::String,
                              ItemId::Stick, kNone, ItemId::String},
                             ItemId::FishingRod, 1));
        // **The centre is a tripwire hook, not a fourth stick.**
        // `recipes/crossbow.json`: pattern `#I#` / `STS` / ` # `, with
        // `#` stick, `I` iron ingot, `S` string and **`T` tripwire hook**
        // at B2. Ours put a stick there, which made the crossbow a strictly
        // cheaper bow and left the tripwire hook - which has its own recipe
        // just above - with no use but the tripwire itself.
        all.push_back(shaped(3, 3,
                             {ItemId::Stick, ItemId::IronIngot, ItemId::Stick, ItemId::String,
                              itemForBlock(tripwireHookAt(FaceDirection::NegZ, false, false)),
                              ItemId::String, kNone, ItemId::Stick, kNone},
                             ItemId::Crossbow, 1));
        // The shard goes on **top**, which is the end you look through: the
        // wiki's `[[Spyglass]]` is B1 amethyst over B2 and B3 copper, and ours
        // had it upside down, so the recipe could not be found at all.
        all.push_back(shaped(1, 3,
                             {ItemId::AmethystShard, ItemId::CopperIngot, ItemId::CopperIngot},
                             ItemId::Spyglass, 1));
        all.push_back(shaped(1, 3, {ItemId::Feather, ItemId::CopperIngot, ItemId::Stick},
                             ItemId::Brush, 1));
        all.push_back(shapeless({ItemId::Paper, ItemId::Gunpowder}, 2, ItemId::FireworkRocket, 3));
        // A star is a dye burst into shape by gunpowder, and a rocket built on
        // one carries that colour up with it. **The loop reads the dye run**,
        // so a seventeenth dye would be a seventeenth star without an edit here.
        for (int colour = 0; colour < kDyeColours; ++colour) {
            const ItemId star =
                static_cast<ItemId>(static_cast<int>(ItemId::FireworkStarFirst) + colour);
            all.push_back(shapeless({ItemId::Gunpowder, dye(colour)}, 2, star, 1));
            all.push_back(shapeless({ItemId::Paper, ItemId::Gunpowder, star}, 3,
                                    ItemId::FireworkRocket, 3));
        }
        all.push_back(shapeless({ItemId::Book, ItemId::Feather, ItemId::InkSac}, 3,
                                ItemId::BookAndQuill, 1));
        // **Five string, and no slimeball - this is the edition split, not a
        // simplification.** `recipes/lead.json` is shaped `~~ ` / `~~ ` /
        // `  ~` with `~` string throughout, `"count": 2`. It is the only
        // recipe in the whole pack whose result is `minecraft:lead`, and
        // `slime_ball` appears in exactly four recipes there - magma cream,
        // slime block, slime ball and sticky piston - none of them this one.
        // **The four-string-plus-a-slimeball form is Java's**, and carrying it
        // gated every lead behind a slime, which spawns in one biome.
        all.push_back(shaped(3, 3,
                             {ItemId::String, ItemId::String, kNone, ItemId::String, ItemId::String,
                              kNone, kNone, kNone, ItemId::String},
                             ItemId::Lead, 2));
        // The bowls. Every one of them is the reference's own, and all four
        // reduce to "something in a bowl", which is why they share no shape.
        all.push_back(shapeless({itemForBlock(BlockId::RedMushroom),
                                 itemForBlock(BlockId::BrownMushroom), ItemId::Bowl},
                                3, ItemId::MushroomStew, 1));
        all.push_back(shapeless({ItemId::Beetroot, ItemId::Beetroot, ItemId::Beetroot,
                                 ItemId::Beetroot, ItemId::Beetroot, ItemId::Beetroot, ItemId::Bowl},
                                7, ItemId::BeetrootSoup, 1));
        all.push_back(shapeless({ItemId::CookedRabbit, ItemId::Carrot, ItemId::BakedPotato,
                                 itemForBlock(BlockId::BrownMushroom), ItemId::Bowl},
                                5, ItemId::RabbitStew, 1));
        all.push_back(shapeless({itemForBlock(BlockId::RedMushroom),
                                 itemForBlock(BlockId::BrownMushroom), ItemId::Bowl,
                                 itemForBlock(BlockId::Dandelion)},
                                4, ItemId::SuspiciousStew, 1));

        // Candles and waxed copper, both generated because both are one shape
        // repeated over a colour or an oxidation stage - and both runs are
        // declared in the same order as the family they index, so the offset
        // *is* the mapping.
        all.push_back(shapeless({ItemId::String, ItemId::Honeycomb}, 2,
                                itemForBlock(BlockId::Candle), 1));
        // A hook and a chest, which is the reference's own recipe -
        // `[[Trapped Chest]]`. It had stood redstone in for the hook and said
        // in a comment that no hook existed; one is crafted eighty lines down,
        // and has been for as long as the comment claimed otherwise.
        all.push_back(shapeless({itemForBlock(tripwireHookAt(FaceDirection::NegZ, false, false)),
                                 itemForBlock(BlockId::Chest)},
                                2, itemForBlock(BlockId::TrappedChest), 1));

        // **Named divergence: obsidian where the reference wants two shulker
        // shells.** The shell drops from a mob that belongs to a dimension we
        // do not have, and adding an item with no other source or use would be
        // worse than substituting the toughest block we do have.
        all.push_back(shapeless({itemForBlock(BlockId::Chest), itemForBlock(BlockId::Obsidian),
                                 itemForBlock(BlockId::Obsidian)},
                                3, itemForBlock(BlockId::Stowbox), 1));
        // And the recolour, which *is* the reference's own: any stowbox plus a
        // dye becomes that colour, so one box can be redyed for ever.
        for (int colour = 0; colour < kDyeColours; ++colour) {
            const ItemId became = tinted(BlockId::StowboxDyedFirst, colour);
            all.push_back(shapeless({itemForBlock(BlockId::Stowbox), dye(colour)}, 2, became, 1));
            for (int from = 0; from < kDyeColours; ++from) {
                if (from != colour) {
                    all.push_back(shapeless(
                        {tinted(BlockId::StowboxDyedFirst, from), dye(colour)}, 2, became, 1));
                }
            }
        }

        for (int colour = 0; colour < kDyeColours; ++colour) {
            all.push_back(shapeless({itemForBlock(BlockId::Candle), dye(colour)}, 2,
                                    itemForBlock(static_cast<BlockId>(
                                        static_cast<int>(BlockId::WhiteCandle) + colour)),
                                    1));
        }
        // Wax is a promise the block will not oxidise any further, so every
        // waxed form is its own unwaxed form plus a honeycomb.
        {
            // The bound is the table's own length rather than a nine written
            // out beside it, so a tenth unwaxed form is one row and no edit.
            for (std::size_t stage = 0; stage < std::size(kUnwaxed); ++stage) {
                all.push_back(shapeless({itemForBlock(kUnwaxed[stage]), ItemId::Honeycomb}, 2,
                                        itemForBlock(static_cast<BlockId>(
                                            static_cast<int>(BlockId::WaxedCopperBlock) +
                                            static_cast<int>(stage))),
                                        1));
            }
            // **And the four grates, which that loop cannot reach.** They are
            // their own enum run with their own waxed forms, so the offset
            // above lands nowhere near them - see `kCopperGrateStages`. Same
            // recipe, same reference.
            for (int stage = 0; stage < kCopperGrateStages; ++stage) {
                all.push_back(shapeless(
                    {itemForBlock(static_cast<BlockId>(static_cast<int>(BlockId::CopperGrate) +
                                                       stage)),
                     ItemId::Honeycomb},
                    2,
                    itemForBlock(static_cast<BlockId>(static_cast<int>(BlockId::WaxedCopperGrate) +
                                                      stage)),
                    1));
            }
        }
        // Bark blocks: four logs in a square, the reference's own recipe, and
        // the one thing that made a *wood* block worth having.
        {
            for (std::size_t wood = 0; wood < std::size(kBarkLogs); ++wood) {
                const int step = static_cast<int>(wood);
                all.push_back(square4(
                    kBarkLogs[wood],
                    static_cast<BlockId>(static_cast<int>(BlockId::OakWood) + step), 3));
                all.push_back(square4(
                    kStrippedLogs[wood],
                    static_cast<BlockId>(static_cast<int>(BlockId::StrippedOakWood) + step), 3));
            }
        }

        // Nuggets, both ways. Nine to an ingot, which is what makes the two
        // gilded foods below cost a ninth of what whole ingots would, and the
        // lanterns further down a ninth of what an ingot each would.
        all.push_back(shapeless({ItemId::IronIngot}, 1, ItemId::IronNugget, 9));
        all.push_back(shapeless({ItemId::GoldIngot}, 1, ItemId::GoldNugget, 9));
        all.push_back(square9(ItemId::IronNugget, ItemId::IronIngot));
        all.push_back(square9(ItemId::GoldNugget, ItemId::GoldIngot));
        all.push_back(ring8Around(ItemId::GoldNugget, ItemId::Carrot, ItemId::GoldenCarrot, 1));
        all.push_back(
            ring8Around(ItemId::GoldNugget, ItemId::MelonSlice, ItemId::GlisteringMelonSlice, 1));
        // **The third gilded food, and it is whole ingots rather than nuggets.**
        // `recipes/golden_apple.json`: eight gold ingots round an apple, one out.
        // It had no recipe and no drop, so the item, its four-minute
        // Absorption and `RESEARCH.md`'s own row for it were all unreachable.
        // The enchanted one stays absent on purpose - the reference has had no
        // recipe for it since 1.9 and it is a chest-loot item there too.
        all.push_back(ring8Around(ItemId::GoldIngot, ItemId::Apple, ItemId::GoldenApple, 1));

        // Doors and trapdoors. **The family index is the wood index** - both
        // tables are declared in `kWoods` order for exactly this, so there is
        // no per-wood lookup to get wrong.
        for (std::size_t wood = 0; wood < kWoods.size(); ++wood) {
            const ItemId planks = itemForBlock(kWoods[wood].planks);
            const int family = static_cast<int>(wood);
            all.push_back(shaped(2, 3, {planks, planks, planks, planks, planks, planks},
                                 itemForBlock(doorCanonical(family)), 3));
            all.push_back(shaped(3, 2, {planks, planks, planks, planks, planks, planks},
                                 itemForBlock(trapdoorCanonical(family)), 2));
        }
        // Iron is the last family of each, and is metal rather than a wood.
        all.push_back(shaped(2, 3,
                             {ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot,
                              ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot},
                             itemForBlock(doorCanonical(kDoorFamilyCount - 1)), 3));
        all.push_back(shaped(2, 2,
                             {ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot,
                              ItemId::IronIngot},
                             itemForBlock(trapdoorCanonical(kTrapdoorFamilyCount - 1)), 1));

        // Seven iron in a U, and eight obsidian round a pearl. Both the
        // reference's own shapes; the centre is our pearl because no eye item
        // exists here.
        all.push_back(shaped(3, 3,
                             {ItemId::IronIngot, kNone, ItemId::IronIngot, ItemId::IronIngot, kNone,
                              ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot,
                              ItemId::IronIngot},
                             itemForBlock(BlockId::Cauldron), 1));
        {
            const ItemId obsidian = itemForBlock(BlockId::Obsidian);
            all.push_back(shaped(3, 3,
                                 {obsidian, obsidian, obsidian, obsidian, ItemId::VoidPearl,
                                  obsidian, obsidian, obsidian, obsidian},
                                 itemForBlock(BlockId::EnderChest), 1));
        }

        // Five iron round a chest, in a V. The reference's own.
        all.push_back(shaped(3, 3,
                             {ItemId::IronIngot, kNone, ItemId::IronIngot, ItemId::IronIngot,
                              itemForBlock(BlockId::Chest), ItemId::IronIngot, kNone,
                              ItemId::IronIngot, kNone},
                             itemForBlock(BlockId::Hopper), 1));

        // The village workstation that answers to no wood. The lectern and the
        // grindstone are per-plank recipes and are generated with the rest of
        // the wood ones below.
        all.push_back(shaped(3, 2, {kNone, ItemId::IronIngot, kNone,
                                    itemForBlock(BlockId::Stone), itemForBlock(BlockId::Stone),
                                    itemForBlock(BlockId::Stone)},
                             itemForBlock(BlockId::Stonecutter), 1));

        // Beds: three wool over three planks, the reference's own recipe. The
        // wool colour picks the bed colour, so this is one loop rather than a
        // sixteen-row table.
        for (int colour = 0; colour < kBedColours; ++colour) {
            const ItemId wool = itemForBlock(
                static_cast<BlockId>(static_cast<int>(BlockId::WhiteWool) + colour));
            const ItemId planks = itemForBlock(BlockId::Planks);
            all.push_back(shaped(3, 2, {wool, wool, wool, planks, planks, planks},
                                 itemForBlock(bedCanonical(colour)), 1));
        }

        // Everything a wood is the same shape in. Generated rather than written
        // out, because eleven woods times nine recipes is ninety-nine chances
        // to paste the wrong plank into one cell.
        for (const Wood& wood : kWoods) {
            const ItemId planks = itemForBlock(wood.planks);
            all.push_back(shapeless({itemForBlock(wood.log)}, 1, planks, wood.plankYield));
            all.push_back(
                shapeless({itemForBlock(wood.stripped)}, 1, planks, wood.plankYield));
            all.push_back(shaped(1, 2, {planks, planks}, ItemId::Stick, 4));
            all.push_back(square4(planks, itemForBlock(BlockId::CraftingTable), 1));
            all.push_back(ring8(planks, itemForBlock(BlockId::Chest), 1));
            all.push_back(ring8Around(planks, ItemId::Redstone, itemForBlock(BlockId::NoteBlock), 1));
            all.push_back(ring8Around(planks, ItemId::Diamond, itemForBlock(BlockId::Jukebox), 1));
            // Three planks in a V.
            all.push_back(shaped(3, 2, {planks, kNone, planks, kNone, planks, kNone}, ItemId::Bowl, 4));
            all.push_back(shaped(3, 3,
                                 {planks, planks, planks, ItemId::Book, ItemId::Book, ItemId::Book,
                                  planks, planks, planks},
                                 itemForBlock(BlockId::Bookshelf), 1));
            // **The campfire, which the beehive rule already depends on.**
            // `recipes/campfire.json`: B1 stick, A2/C2 stick, and row three
            // `minecraft:logs`, with `minecraft:coals` in the middle - two
            // recipes per wood rather than an ingredient naming a set, which is
            // this file's standing rule. The block was finished, lit,
            // light-emitting and named as the way to calm bees, and nothing
            // could make one.
            //
            // The soul variant swaps the fuel for soul sand or soul soil, per
            // `recipes/soul_campfire.json`'s `soul_fire_base_blocks` - same shape.
            const ItemId logForFire = itemForBlock(wood.log);
            for (const ItemId fuel : {ItemId::Coal, ItemId::Charcoal}) {
                all.push_back(shaped(3, 3,
                                     {kNone, ItemId::Stick, kNone, ItemId::Stick, fuel,
                                      ItemId::Stick, logForFire, logForFire, logForFire},
                                     itemForBlock(BlockId::Campfire), 1));
            }
            for (const BlockId soul : {BlockId::SoulSand, BlockId::SoulSoil}) {
                all.push_back(shaped(3, 3,
                                     {kNone, ItemId::Stick, kNone, ItemId::Stick,
                                      itemForBlock(soul), ItemId::Stick, logForFire, logForFire,
                                      logForFire},
                                     itemForBlock(BlockId::SoulCampfire), 1));
            }
            // Two iron over four planks.
            all.push_back(shaped(2, 3,
                                 {ItemId::IronIngot, ItemId::IronIngot, planks, planks, planks, planks},
                                 itemForBlock(BlockId::SmithingTable), 1));
            // A shield with an iron boss.
            all.push_back(shaped(3, 3,
                                 {planks, ItemId::IronIngot, planks, planks, planks, planks, kNone,
                                  planks, kNone},
                                 ItemId::Shield, 1));
            // Two sticks over a stone slab, on plank shoulders - the wiki's
            // `[[Grindstone]]`, A1/C1 stick, B1 stone slab, A2/C2 any planks.
            // It had been six cells of a 2x3 with the sticks across the middle,
            // which is not a shape the reference has anywhere.
            all.push_back(shaped(3, 2,
                                 {ItemId::Stick, itemForBlock(BlockId::StoneSlab), ItemId::Stick,
                                  planks, kNone, planks},
                                 itemForBlock(BlockId::Grindstone), 1));
            // A barrel is planks walled round two slabs of the same wood, which
            // is the reference's own recipe and the one that makes a slab worth
            // cutting for something other than stairs.
            if (const int slabFamily = slabFamilyOf(wood.planks); slabFamily >= 0) {
                const ItemId slab = itemForBlock(slabAt(slabFamily, false));
                all.push_back(shaped(3, 3,
                                     {planks, slab, planks, planks, kNone, planks, planks, slab,
                                      planks},
                                     itemForBlock(BlockId::Barrel), 1));
                // **Seven slabs, not six planks.** The wiki's `[[Composter]]`
                // is A1/C1, A2/C2 and the whole of row three, every one of them
                // "Any Wooden Slab" - so it costs three and a half planks
                // rather than six, and the bottom is closed.
                all.push_back(shaped(3, 3,
                                     {slab, kNone, slab, slab, kNone, slab, slab, slab, slab},
                                     itemForBlock(BlockId::Composter0), 1));
                // And the lectern, which is four of the same slab round a
                // bookshelf - `[[Lectern]]`, A1/B1/C1 and B3 "Any Wooden Slab".
                // It had been cut from *stone*, which is not a slab the
                // reference accepts here at all.
                all.push_back(shaped(3, 3,
                                     {slab, slab, slab, kNone, itemForBlock(BlockId::Bookshelf),
                                      kNone, kNone, slab, kNone},
                                     itemForBlock(BlockId::Lectern), 1));
                // And the chiselled one, which is the plain bookshelf's shape
                // with its own wood's slabs where the books go - wiki
                // `recipes/chiseled_bookshelf.json`: planks, slabs, planks.
                // Silk touch is the only way to pick one up again, so without
                // this the block was creative-only.
                all.push_back(shaped(3, 3,
                                     {planks, planks, planks, slab, slab, slab, planks, planks,
                                      planks},
                                     itemForBlock(BlockId::ChiseledBookshelf), 1));
            }
            // A furnace wrapped in logs. Logs rather than planks because the
            // fuel is the point of the block.
            const ItemId log = itemForBlock(wood.log);
            all.push_back(shaped(3, 3,
                                 {kNone, log, kNone, log, itemForBlock(BlockId::Furnace), log, kNone,
                                  log, kNone},
                                 itemForBlock(BlockId::Smoker), 1));
        }

        // ---- Redstone. ----
        // The per-wood half first, generated the same way every other plank
        // recipe here is. `kButtonFamilies` and `kPressurePlateFamilies` are
        // declared in `kWoods` order on purpose, so this indexes straight
        // across them - the same arrangement the doors and trapdoors use, and
        // the same reason: a different order there would quietly give a cherry
        // button a mangrove recipe.
        for (std::size_t w = 0; w < kWoods.size(); ++w) {
            const ItemId planks = itemForBlock(kWoods[w].planks);
            const int family = static_cast<int>(w);
            all.push_back(shapeless({planks}, 1, itemForBlock(buttonAt(family, 0, false)), 1));
            all.push_back(shaped(2, 1, {planks, planks},
                                 itemForBlock(pressurePlateAt(family, 0)), 1));
            // A piston: three planks over a stone shell round an iron core.
            all.push_back(shaped(3, 3,
                                 {planks, planks, planks, itemForBlock(BlockId::Cobblestone),
                                  ItemId::IronIngot, itemForBlock(BlockId::Cobblestone),
                                  itemForBlock(BlockId::Cobblestone), ItemId::Redstone,
                                  itemForBlock(BlockId::Cobblestone)},
                                 itemForBlock(pistonAt(Facing6North, false, false)), 1));
            // A sign is six planks over a stick, and a hanging one swaps the
            // stick for two iron - the reference uses chains, which we have no
            // item for.
            all.push_back(shaped(3, 3,
                                 {planks, planks, planks, planks, planks, planks, kNone,
                                  ItemId::Stick, kNone},
                                 itemForBlock(game::signAt(0, family, FaceDirection::NegZ, false)),
                                 3));
            all.push_back(shaped(3, 3,
                                 {ItemId::IronIngot, kNone, ItemId::IronIngot, planks, planks,
                                  planks, planks, planks, planks},
                                 itemForBlock(game::signAt(1, family, FaceDirection::NegZ, false)),
                                 6));
            // A detector needs a slab of the same wood, which is what makes a
            // slab worth cutting for something other than a stair.
            if (const int slab = slabFamilyOf(kWoods[w].planks); slab >= 0) {
                const ItemId half = itemForBlock(slabAt(slab, false));
                all.push_back(shaped(3, 3,
                                     {itemForBlock(BlockId::Glass), itemForBlock(BlockId::Glass),
                                      itemForBlock(BlockId::Glass), ItemId::Quartz, ItemId::Quartz,
                                      ItemId::Quartz, half, half, half},
                                     itemForBlock(daylightDetectorAt(0, false)), 1));
            }
        }

        {
            const ItemId redstone = ItemId::Redstone;
            const ItemId stick = ItemId::Stick;
            const ItemId cobble = itemForBlock(BlockId::Cobblestone);
            const ItemId stone = itemForBlock(BlockId::Stone);
            const ItemId iron = ItemId::IronIngot;
            const ItemId gold = ItemId::GoldIngot;
            const ItemId torch = itemForBlock(BlockId::RedstoneTorch);

            // Stone answers to the same two shapes the woods do. **The family
            // index is derived from the tables' own lengths**, not the 11, 12
            // and 13 that used to sit here: the woods fill the front of both
            // runs and these three are what is left at the back, so a twelfth
            // wood moves every one of them and nothing would have said so.
            constexpr int kStoneButtonFamily = kButtonFamilyCount - 1;
            constexpr int kStonePlateFamily = kPressurePlateFamilyCount - 3;
            constexpr int kGoldPlateFamily = kPressurePlateFamilyCount - 2;
            constexpr int kIronPlateFamily = kPressurePlateFamilyCount - 1;
            static_assert(kButtonFamilies[kStoneButtonFamily].parent == BlockId::Stone &&
                              kPressurePlateFamilies[kStonePlateFamily].parent == BlockId::Stone &&
                              kPressurePlateFamilies[kGoldPlateFamily].parent == BlockId::GoldBlock &&
                              kPressurePlateFamilies[kIronPlateFamily].parent == BlockId::IronBlock,
                          "the four non-wood switch families are not where counting back from the "
                          "end of kButtonFamilies and kPressurePlateFamilies says they are");
            all.push_back(
                shapeless({stone}, 1, itemForBlock(buttonAt(kStoneButtonFamily, 0, false)), 1));
            all.push_back(shaped(2, 1, {stone, stone},
                                 itemForBlock(pressurePlateAt(kStonePlateFamily, 0)), 1));
            // The two that weigh what stands on them are cut from the metal
            // they measure with.
            all.push_back(shaped(2, 1, {gold, gold},
                                 itemForBlock(pressurePlateAt(kGoldPlateFamily, 0)), 1));
            all.push_back(shaped(2, 1, {iron, iron},
                                 itemForBlock(pressurePlateAt(kIronPlateFamily, 0)), 1));

            // A torch is a stick with dust on the end of it. **The only one** -
            // this exact recipe was written a second time three hundred lines
            // down, among the ordinary torches, and which of the two the
            // matcher found was decided by declaration order and by nothing
            // else. `patternsUnique` below is now what stops the third.
            all.push_back(shaped(1, 2, {redstone, stick}, torch, 1));
            all.push_back(shaped(1, 2, {stick, cobble},
                                 itemForBlock(leverAt(LeverFloorX, false)), 1));
            all.push_back(shaped(3, 2, {torch, redstone, torch, stone, stone, stone},
                                 itemForBlock(repeaterAt(FaceDirection::NegZ, 1, false, false)), 1));
            all.push_back(shaped(3, 3,
                                 {kNone, torch, kNone, torch, ItemId::Quartz, torch, stone, stone,
                                  stone},
                                 itemForBlock(comparatorAt(FaceDirection::NegZ, false, false)), 1));
            // A sticky piston is a piston with a slimeball stuck on the plate,
            // which is why it is shapeless rather than a shape of its own.
            all.push_back(shapeless({ItemId::Slimeball,
                                     itemForBlock(pistonAt(Facing6North, false, false))},
                                    2, itemForBlock(pistonAt(Facing6North, false, true)), 1));
            all.push_back(shaped(3, 3,
                                 {cobble, cobble, cobble, redstone, redstone, ItemId::Quartz, cobble,
                                  cobble, cobble},
                                 itemForBlock(observerAt(Facing6North, false)), 1));
            all.push_back(shaped(3, 3,
                                 {cobble, cobble, cobble, cobble, ItemId::Bow, cobble, cobble,
                                  redstone, cobble},
                                 itemForBlock(dispenserAt(Facing6North, false)), 1));
            all.push_back(shaped(3, 3,
                                 {cobble, cobble, cobble, cobble, kNone, cobble, cobble, redstone,
                                  cobble},
                                 itemForBlock(dispenserAt(Facing6North, true)), 1));
            all.push_back(shaped(1, 3, {ItemId::CopperIngot, ItemId::CopperIngot,
                                        ItemId::CopperIngot},
                                 itemForBlock(lightningRodAt(Facing6Up, false)), 1));
            all.push_back(shaped(1, 3, {iron, stick, itemForBlock(BlockId::Planks)},
                                 itemForBlock(tripwireHookAt(FaceDirection::NegZ, false, false)),
                                 2));
            // **Four dust in a cross, not eight in a ring.** The wiki's
            // `[[Redstone Lamp]]` is B1, A2, C2 and B3 dust round a B2
            // glowstone; the ring cost twice the redstone and left the corners
            // filled, so a correctly laid-out lamp would not craft either.
            all.push_back(shaped(3, 3,
                                 {kNone, redstone, kNone, redstone,
                                  itemForBlock(BlockId::Glowstone), redstone, kNone, redstone,
                                  kNone},
                                 itemForBlock(BlockId::RedstoneLamp), 1));

            // The four rails, all six of a metal round a spine. Sixteen plain
            // ones and six of each of the rest, which is the reference's own
            // yield and the reason a plain rail is what long track is made of.
            all.push_back(shaped(3, 3, {iron, kNone, iron, iron, stick, iron, iron, kNone, iron},
                                 itemForBlock(railAt(0, 0, false)), 16));
            all.push_back(shaped(3, 3,
                                 {gold, kNone, gold, gold, stick, gold, gold, redstone, gold},
                                 itemForBlock(railAt(1, 0, false)), 6));
            all.push_back(shaped(3, 3,
                                 {iron, kNone, iron, iron,
                                  itemForBlock(pressurePlateAt(kStonePlateFamily, 0)), iron, iron,
                                  redstone, iron},
                                 itemForBlock(railAt(2, 0, false)), 6));
            // **The activator rail's centre is a redstone torch, not dust.**
            // `recipes/activator_rail.json` is `XSX` / `X#X` / `XSX` with
            // `X` iron, `S` stick and `#` `minecraft:redstone_torch`. The
            // other three above are cell-for-cell correct against `rail.json`,
            // `golden_rail.json` and `detector_rail.json`; this was the only
            // one that was not, and it made the rail a dust cheaper than the
            // reference charges.
            all.push_back(shaped(3, 3,
                                 {iron, stick, iron, iron, torch, iron, iron, stick, iron},
                                 itemForBlock(railAt(3, 0, false)), 6));
        }

        // Sixteen banners, each six of its own wool over a stick. The colour is
        // read off `kBannerFamilies` rather than assumed contiguous, so this
        // cannot drift from the blocks it makes.
        for (int colour = 0; colour < kBannerFamilyCount; ++colour) {
            const ItemId wool =
                itemForBlock(kBannerFamilies[static_cast<std::size_t>(colour)].parent);
            all.push_back(shaped(3, 3,
                                 {wool, wool, wool, wool, wool, wool, kNone, ItemId::Stick, kNone},
                                 itemForBlock(signAt(2, colour, FaceDirection::NegZ, false)), 1));
        }

        // Every cut shape, for every material it comes in. Generated from the
        // same family tables the blocks themselves come from, so a new material
        // is one row there and needs nothing here.
        //
        // **One staircase, not two.** The second, mirrored pattern that used to
        // sit here is gone because `matchesShaped` now flips a recipe itself -
        // which is the reference's own rule ("ingredients in shaped recipes ...
        // can be flipped horizontally, but not vertically", wiki `[[Crafting]]`)
        // and covers the axe, the hoe, the shears, the bow, the fishing rod,
        // the observer and coarse dirt in the same stroke. Every one of those
        // was uncraftable left-handed while only the stairs carried a twin.
        for (int family = 0; family < kStairFamilyCount; ++family) {
            const ItemId material = itemForBlock(kStairFamilies[static_cast<std::size_t>(family)].parent);
            all.push_back(shaped(3, 3,
                                 {material, kNone, kNone, material, material, kNone, material,
                                  material, material},
                                 itemForBlock(stairsAt(family, Facing::North, false)), 4));
        }
        for (int family = 0; family < kSlabFamilyCount; ++family) {
            all.push_back(row3(itemForBlock(kSlabFamilies[static_cast<std::size_t>(family)].parent),
                               itemForBlock(slabAt(family, false)), 6));
        }
        // Six across two rows, which is the reference's shape for a wall and
        // for the block it produces six of.
        for (int family = 0; family < kWallFamilyCount; ++family) {
            const ItemId material = itemForBlock(kWallFamilies[static_cast<std::size_t>(family)].parent);
            all.push_back(shaped(3, 2, {material, material, material, material, material, material},
                                 itemForBlock(wallAt(family)), 6));
        }
        // Four planks and two sticks for a fence; two planks and four sticks for
        // the gate, both the reference's own arrangements. Nether brick fence is
        // the one that is not wood: it uses loose nether bricks as its spacer and
        // yields six rather than three, which is the reference's figure.
        for (int family = 0; family < kFenceFamilyCount; ++family) {
            const BlockId parent = kFenceFamilies[static_cast<std::size_t>(family)].parent;
            const bool stone = parent == BlockId::NetherBricks;
            all.push_back(shaped(3, 2,
                                 {itemForBlock(parent),
                                  stone ? ItemId::NetherBrickItem : ItemId::Stick,
                                  itemForBlock(parent), itemForBlock(parent),
                                  stone ? ItemId::NetherBrickItem : ItemId::Stick,
                                  itemForBlock(parent)},
                                 itemForBlock(fenceAt(family)), stone ? 6 : 3));
        }
        for (int family = 0; family < kGateFamilyCount; ++family) {
            const ItemId material = itemForBlock(kGateFamilies[static_cast<std::size_t>(family)].parent);
            all.push_back(shaped(3, 2,
                                 {ItemId::Stick, material, ItemId::Stick, ItemId::Stick, material,
                                  ItemId::Stick},
                                 itemForBlock(gateAt(family, FaceDirection::NegZ, false)), 1));
        }

        // The dye chain: a flower each, the four that come from something else,
        // and the reference's own nine mixes.
        for (const FlowerDye& pairing : kFlowerDyes) {
            all.push_back(shapeless({itemForBlock(pairing.flower)}, 1, dye(pairing.colour), 1));
        }
        all.push_back(shapeless({ItemId::Bone}, 1, ItemId::BoneMeal, 3));
        all.push_back(shapeless({ItemId::BoneMeal}, 1, dye(kWhite), 1));
        all.push_back(shapeless({ItemId::InkSac}, 1, dye(kBlack), 1));
        all.push_back(shapeless({ItemId::LapisLazuli}, 1, dye(kBlue), 1));
        // Two brown things, both of them real. Cocoa is the reference's own
        // brown dye and is added further down; the mushroom stays as a second
        // source because a jungle is a long way to walk. The comment that used
        // to sit here called the mushroom a divergence "because we have no
        // source for cocoa", which stopped being true when the bean did.
        all.push_back(shapeless({itemForBlock(BlockId::BrownMushroom)}, 1, dye(kBrown), 1));
        for (const DyeMix& mix : kDyeMixes) {
            all.push_back(shapeless({dye(mix.a), dye(mix.b)}, 2, dye(mix.result), 2));
        }

        // The three dyed families, sixteen colours each. All four runs are
        // declared white-first in the same order, so a colour is one offset.
        all.push_back(square4(ItemId::String, itemForBlock(BlockId::WhiteWool), 1));
        for (int colour = 0; colour < kDyeColours; ++colour) {
            // **Any wool re-dyes to any colour, not white to any colour.** Wiki
            // `[[Wool]]`: "Wool of any color can be re-dyed into any other
            // color", and the recipe is stated as *Any Wool* + *Matching Dye*.
            // The input was hard-wired to white, so a player holding blue wool
            // had a dead end - which is the same shape the stowbox loop two
            // hundred lines up already gets right, and the two families sitting
            // beside each other disagreeing is what gave it away.
            for (int from = 0; from < kDyeColours; ++from) {
                if (from != colour) {
                    all.push_back(shapeless({tinted(BlockId::WhiteWool, from), dye(colour)}, 2,
                                            tinted(BlockId::WhiteWool, colour), 1));
                }
            }
            all.push_back(ring8Around(itemForBlock(BlockId::Terracotta), dye(colour),
                                      tinted(BlockId::WhiteTerracotta, colour), 8));
            all.push_back(shapeless({itemForBlock(BlockId::Sand), itemForBlock(BlockId::Sand),
                                     itemForBlock(BlockId::Sand), itemForBlock(BlockId::Sand),
                                     itemForBlock(BlockId::Gravel), itemForBlock(BlockId::Gravel),
                                     itemForBlock(BlockId::Gravel), itemForBlock(BlockId::Gravel),
                                     dye(colour)},
                                    9, tinted(BlockId::WhiteConcretePowder, colour), 8));
        }

        // Four in a square, which is the reference's shape for every polished
        // and brick form of a stone.
        all.push_back(square4(BlockId::Blackstone, BlockId::PolishedBlackstone, 4));
        all.push_back(square4(BlockId::PolishedBlackstone, BlockId::PolishedBlackstoneBricks, 4));
        all.push_back(square4(BlockId::Tuff, BlockId::PolishedTuff, 4));
        all.push_back(square4(BlockId::PolishedTuff, BlockId::TuffBricks, 4));
        all.push_back(square4(BlockId::Basalt, BlockId::PolishedBasalt, 4));
        all.push_back(square4(BlockId::EndStone, BlockId::EndStoneBricks, 4));
        // Cut copper and the copper grates, both families, all four oxidation
        // stages and the waxed half of each, from `kCopperCuts`.
        //
        // Only the plain 2x2 cut copper used to be here. The other three cut
        // stages were reachable solely by waiting for an already-cut block to
        // oxidise, every waxed cut form had no recipe at all, and **no grate of
        // any kind was obtainable by any means** - all four had no recipe, and a
        // sweep for the id found it only in `Block.hpp` and `Copper.hpp`, so
        // nothing in worldgen or any structure ever places one either. A block
        // dropping itself is not an independent source when nothing can put one
        // in the world to be broken.
        for (const CopperCut& row : kCopperCuts) {
            all.push_back(square4(row.stock, row.cut, 4));
            const ItemId stock = itemForBlock(row.stock);
            all.push_back(shaped(3, 3,
                                 {kNone, stock, kNone, stock, kNone, stock, kNone, stock, kNone},
                                 itemForBlock(row.grate), 4));
        }
        all.push_back(square4(ItemId::Quartz, itemForBlock(BlockId::QuartzBlock), 1));
        all.push_back(square4(BlockId::QuartzBlock, BlockId::QuartzBricks, 4));
        // Four of a loose mineral in a square, same 4-in/1-out shape as the
        // quartz block above - wiki `[[Block of Amethyst]]` and
        // `[[Dripstone Block]]`. Both were the only way to store or build with
        // what they compact, and neither existed.
        all.push_back(square4(ItemId::AmethystShard, itemForBlock(BlockId::AmethystBlock), 1));
        all.push_back(square4(BlockId::PointedDripstone, BlockId::DripstoneBlock, 1));
        // Two slabs of a family stacked, which is the reference's own shape for
        // all three chiselled blocks. The slab is read out of `kSlabFamilies`
        // rather than named, so it stays whatever that table says it is.
        for (const ChiselledFromSlabs& row : kChiselledFromSlabs) {
            const ItemId half = itemForBlock(slabAt(slabFamilyOf(row.parent), false));
            all.push_back(shaped(1, 2, {half, half}, itemForBlock(row.result), 1));
        }
        all.push_back(square4(ItemId::PoppedChorusFruit, itemForBlock(BlockId::PurpurBlock), 4));
        all.push_back(square4(ItemId::NetherBrickItem, itemForBlock(BlockId::NetherBricks), 1));
        all.push_back(square4(ItemId::Brick, itemForBlock(BlockId::Bricks), 1));
        all.push_back(shaped(1, 2, {itemForBlock(BlockId::QuartzBlock), itemForBlock(BlockId::QuartzBlock)},
                             itemForBlock(BlockId::QuartzPillar), 2));
        all.push_back(shaped(1, 2, {itemForBlock(BlockId::PurpurBlock), itemForBlock(BlockId::PurpurBlock)},
                             itemForBlock(BlockId::PurpurPillar), 2));

        // Nine into one, for everything whose loose form exists - and back out
        // again, same as the metals above.
        all.push_back(square9(ItemId::BoneMeal, BlockId::BoneBlock));
        all.push_back(square9(ItemId::Slimeball, BlockId::SlimeBlock));
        all.push_back(square9(ItemId::DriedKelp, BlockId::DriedKelpBlock));
        all.push_back(square9(ItemId::RawIron, BlockId::RawIronBlock));
        all.push_back(square9(ItemId::RawGold, BlockId::RawGoldBlock));
        all.push_back(square9(ItemId::RawCopper, BlockId::RawCopperBlock));
        all.push_back(shapeless({itemForBlock(BlockId::BoneBlock)}, 1, ItemId::BoneMeal, 9));
        all.push_back(shapeless({itemForBlock(BlockId::SlimeBlock)}, 1, ItemId::Slimeball, 9));
        all.push_back(shapeless({itemForBlock(BlockId::DriedKelpBlock)}, 1, ItemId::DriedKelp, 9));
        all.push_back(shapeless({itemForBlock(BlockId::RawIronBlock)}, 1, ItemId::RawIron, 9));
        all.push_back(shapeless({itemForBlock(BlockId::RawGoldBlock)}, 1, ItemId::RawGold, 9));
        all.push_back(shapeless({itemForBlock(BlockId::RawCopperBlock)}, 1, ItemId::RawCopper, 9));
        all.push_back(square4(ItemId::MagmaCream, itemForBlock(BlockId::MagmaBlock), 1));
        all.push_back(square4(ItemId::Honeycomb, itemForBlock(BlockId::HoneycombBlock), 1));

        // Carpets and panes, from the same family tables the blocks come from.
        for (int family = 0; family < kCarpetFamilyCount; ++family) {
            all.push_back(shaped(2, 1,
                                 {itemForBlock(kCarpetFamilies[static_cast<std::size_t>(family)].parent),
                                  itemForBlock(kCarpetFamilies[static_cast<std::size_t>(family)].parent)},
                                 itemForBlock(carpetAt(family)), 3));
        }
        for (int family = 0; family < kPaneFamilyCount; ++family) {
            const ItemId glass = itemForBlock(kPaneFamilies[static_cast<std::size_t>(family)].parent);
            all.push_back(shaped(3, 2, {glass, glass, glass, glass, glass, glass},
                                 itemForBlock(paneAt(family)), 16));
        }
        // Eight glass round a dye, the reference's own shape and yield.
        for (int colour = 0; colour < kDyeColours; ++colour) {
            all.push_back(ring8Around(itemForBlock(BlockId::Glass), dye(colour),
                                      tinted(BlockId::WhiteStainedGlass, colour), 8));
        }
        // **And the same ring made of panes, which is the reference's second
        // route to a stained pane** - wiki `[[Stained Glass Pane]]` gives both
        // "6 matching stained glass -> 16 panes" (the loop above) and "8 glass
        // panes + 1 matching dye -> 8 matching panes" (this one). Only the
        // first existed, so a player holding plain panes and a dye had to melt
        // back through blocks. `paneAt(colour + 1)` is the stained pane for
        // this colour; `paneColoursFollowTheDyeRun` is what proves it.
        for (int colour = 0; colour < kDyeColours; ++colour) {
            all.push_back(ring8Around(itemForBlock(paneAt(0)), dye(colour),
                                      itemForBlock(paneAt(colour + 1)), 8));
        }
        all.push_back(shaped(3, 2,
                             {ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot,
                              ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot},
                             itemForBlock(BlockId::IronBars), 16));
        // Seven sticks in an H, the reference's ladder.
        all.push_back(shaped(3, 3,
                             {ItemId::Stick, kNone, ItemId::Stick, ItemId::Stick, ItemId::Stick,
                              ItemId::Stick, ItemId::Stick, kNone, ItemId::Stick},
                             itemForBlock(BlockId::LadderNorth), 3));
        // Eight iron nuggets caged round a torch, the reference's own lantern.
        // The comment that used to sit here declared a divergence - "there is
        // no nugget here" - that stopped being true when `IronNugget` was
        // added; one ingot was standing in for the eight, at a ninth of the
        // price, and nothing said so.
        all.push_back(ring8Around(ItemId::IronNugget, itemForBlock(BlockId::Torch),
                                  itemForBlock(BlockId::Lantern), 1));
        all.push_back(ring8Around(ItemId::IronNugget, itemForBlock(BlockId::SoulTorch),
                                  itemForBlock(BlockId::SoulLantern), 1));
        // A torch over soul sand, which is exactly how the reference makes one.
        all.push_back(shaped(1, 3,
                             {ItemId::Coal, ItemId::Stick, itemForBlock(BlockId::SoulSand)},
                             itemForBlock(BlockId::SoulTorch), 4));
        all.push_back(shaped(1, 2, {ItemId::CinderRod, ItemId::PoppedChorusFruit},
                             itemForBlock(BlockId::EndRod), 4));

        // Two iron ingots offset by one, which is the reference's shears - the
        // only thing that collects a vine.
        all.push_back(shaped(2, 2, {kNone, ItemId::IronIngot, ItemId::IronIngot, kNone},
                             ItemId::Shears, 1));
        // **Cocoa beans are the reference's own brown dye.** Ours had been
        // standing a brown mushroom in for it, which stays as a second source
        // rather than being removed - a jungle is a long way to walk for dye.
        all.push_back(shapeless({ItemId::CocoaBeans}, 1, dye(kBrown), 1));
        // Two wheat round a bean, and the reference's yield of eight.
        all.push_back(shaped(3, 1, {ItemId::Wheat, ItemId::CocoaBeans, ItemId::Wheat},
                             ItemId::Cookie, 8));
        // Nine slices back into a melon, which is the reference's own recipe and
        // the only thing that made a melon worth carrying.
        all.push_back(square9(ItemId::MelonSlice, BlockId::Melon));

        // The bow and its ammunition, both the reference's own shapes. Three
        // sticks down the middle with string bowed round them, and an arrow
        // built head, shaft, flight from the top down.
        all.push_back(shaped(3, 3,
                             {kNone, ItemId::Stick, ItemId::String, ItemId::Stick, kNone,
                              ItemId::String, kNone, ItemId::Stick, ItemId::String},
                             ItemId::Bow, 1));
        all.push_back(shaped(1, 3, {ItemId::Flint, ItemId::Stick, ItemId::Feather}, ItemId::Arrow, 4));

        // The small things that had no recipe and no other source.
        all.push_back(row3(itemForBlock(BlockId::SugarCane), ItemId::Paper, 3));
        all.push_back(shapeless({itemForBlock(BlockId::SugarCane)}, 1, ItemId::Sugar, 1));
        // **The one reagent the whole corrupting half of the brewing tree
        // stands on.** `recipes/fermented_spider_eye.json`: spider eye + brown
        // mushroom + sugar, shapeless, one out. It had no recipe, and
        // `kBrews` names it in **fifteen** rows - so every potion of Weakness,
        // Slowness, Harming and Invisibility, drinkable and splash alike, was
        // unreachable while the brewing stand, the screen and the table all
        // worked perfectly.
        all.push_back(shapeless({ItemId::SpiderEye, itemForBlock(BlockId::BrownMushroom),
                                 ItemId::Sugar},
                                3, ItemId::FermentedSpiderEye, 1));
        // `recipes/pumpkin_pie.json`: a pumpkin, sugar and an egg, shapeless. The
        // egg comes from a village fletcher's chest, which is the only source
        // of one here - chickens do not lay.
        all.push_back(shapeless({itemForBlock(BlockId::Pumpkin), ItemId::Sugar, ItemId::Egg}, 3,
                                ItemId::PumpkinPie, 1));
        all.push_back(shapeless({ItemId::Paper, ItemId::Paper, ItemId::Paper, ItemId::Leather}, 4,
                                ItemId::Book, 1));
        // **A V, not a row.** Wiki `[[Glass Bottle]]` is A2, C2 and B3 glass -
        // three panes leaning into a base. Written as a row it read as a
        // plausible-looking shape that nothing in the reference makes, and it
        // meant a correctly laid-out bottle would not craft.
        all.push_back(shaped(3, 2,
                             {itemForBlock(BlockId::Glass), kNone, itemForBlock(BlockId::Glass),
                              kNone, itemForBlock(BlockId::Glass), kNone},
                             ItemId::GlassBottle, 3));
        all.push_back(square4(ItemId::ClayBall, itemForBlock(BlockId::Clay), 1));
        all.push_back(square4(ItemId::GlowstoneDust, itemForBlock(BlockId::Glowstone), 1));
        all.push_back(shapeless({ItemId::IronIngot, ItemId::Flint}, 2, ItemId::FlintAndSteel, 1));
        // Five charges around four sand, the reference's checker.
        all.push_back(shaped(3, 3,
                             {ItemId::Gunpowder, itemForBlock(BlockId::Sand), ItemId::Gunpowder,
                              itemForBlock(BlockId::Sand), ItemId::Gunpowder,
                              itemForBlock(BlockId::Sand), ItemId::Gunpowder,
                              itemForBlock(BlockId::Sand), ItemId::Gunpowder},
                             itemForBlock(BlockId::Tnt), 1));

        struct ToolShape {
            const char* pattern;
            ItemId wooden;
            ItemId stone;
            ItemId iron;
            ItemId diamond;
            ItemId emberite;
        };
        constexpr std::array<ToolShape, 5> shapes{{
            {"MMM.S..S.", ItemId::WoodenPickaxe, ItemId::StonePickaxe, ItemId::IronPickaxe,
             ItemId::DiamondPickaxe, ItemId::EmberitePickaxe},
            {"MM.MS..S.", ItemId::WoodenAxe, ItemId::StoneAxe, ItemId::IronAxe, ItemId::DiamondAxe,
             ItemId::EmberiteAxe},
            {".M..S..S.", ItemId::WoodenShovel, ItemId::StoneShovel, ItemId::IronShovel,
             ItemId::DiamondShovel, ItemId::EmberiteShovel},
            {".M..M..S.", ItemId::WoodenSword, ItemId::StoneSword, ItemId::IronSword,
             ItemId::DiamondSword, ItemId::EmberiteSword},
            {"MM..S..S.", ItemId::WoodenHoe, ItemId::StoneHoe, ItemId::IronHoe, ItemId::DiamondHoe,
             ItemId::EmberiteHoe},
        }};
        for (const ToolShape& shape : shapes) {
            // A wooden tool from **any** wood, for the same reason the planks
            // themselves are generated: the reference matches an item tag and
            // ours has to name each one.
            for (const Wood& wood : kWoods) {
                all.push_back(tool(itemForBlock(wood.planks), shape.wooden, shape.pattern));
            }
            all.push_back(tool(itemForBlock(BlockId::Cobblestone), shape.stone, shape.pattern));
            all.push_back(tool(ItemId::IronIngot, shape.iron, shape.pattern));
            all.push_back(tool(ItemId::Diamond, shape.diamond, shape.pattern));
            // The top tier is **not** here on purpose: it is an upgrade rather
            // than a craft, and it lives at the smithing table. See
            // `smithingResult`.
        }

        // Derived here rather than at each recipe, so it cannot be forgotten on
        // a new row. A shapeless recipe keeps its ingredient *count* in
        // `width`, so it fits the player's grid when it uses four or fewer -
        // comparing it against the pattern extents would be wrong.
        for (Recipe& recipe : all) {
            recipe.fitsInTwoByTwo =
                recipe.shapeless ? recipe.width <= 4 : recipe.width <= 2 && recipe.height <= 2;
        }
        return all;
    }();
}

/// The key two recipes would have to share to be indistinguishable to
/// `findMatch`, and it is deliberately built the way the matcher reads rather
/// than the way the table is written:
///
/// * a shapeless recipe's ingredients are **sorted**, because `matchesShapeless`
///   does not care what order they were listed in;
/// * a shaped recipe takes the smaller of its pattern and its **mirror**,
///   because `matchesShaped` now accepts either hand.
///
/// So two rows collide under this key exactly when a player could lay out one
/// grid that both claim - which is the thing that has no defined answer.
struct MatchKey {
    std::array<ItemId, kMaxCraftSize * kMaxCraftSize> cells{};
    int width = 0;
    int height = 0;
    bool shapeless = false;

    constexpr bool operator==(const MatchKey& other) const {
        return width == other.width && height == other.height &&
               shapeless == other.shapeless && cells == other.cells;
    }
};

constexpr MatchKey matchKeyOf(const Recipe& recipe) {
    MatchKey key;
    key.width = recipe.width;
    key.height = recipe.height;
    key.shapeless = recipe.shapeless;
    if (recipe.shapeless) {
        for (int i = 0; i < recipe.width; ++i) {
            key.cells[static_cast<std::size_t>(i)] = recipe.pattern[static_cast<std::size_t>(i)];
        }
        // Insertion sort: `std::sort` is not constexpr before C++20's ranges
        // land everywhere, and nine elements do not need better.
        for (int i = 1; i < recipe.width; ++i) {
            const ItemId held = key.cells[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && static_cast<int>(key.cells[static_cast<std::size_t>(j)]) >
                                 static_cast<int>(held)) {
                key.cells[static_cast<std::size_t>(j + 1)] = key.cells[static_cast<std::size_t>(j)];
                --j;
            }
            key.cells[static_cast<std::size_t>(j + 1)] = held;
        }
        return key;
    }
    bool mirrorIsSmaller = false;
    for (int i = 0; i < recipe.width * recipe.height; ++i) {
        const int x = i % recipe.width;
        const int y = i / recipe.width;
        const ItemId here = recipe.pattern[static_cast<std::size_t>(i)];
        const ItemId there =
            recipe.pattern[static_cast<std::size_t>(y * recipe.width + (recipe.width - 1 - x))];
        if (here != there) {
            mirrorIsSmaller = static_cast<int>(there) < static_cast<int>(here);
            break;
        }
    }
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const int source = mirrorIsSmaller ? y * recipe.width + (recipe.width - 1 - x)
                                               : y * recipe.width + x;
            key.cells[static_cast<std::size_t>(y * recipe.width + x)] =
                recipe.pattern[static_cast<std::size_t>(source)];
        }
    }
    return key;
}

constexpr std::size_t hashOf(const MatchKey& key) {
    std::size_t hash = 1469598103934665603ull;
    for (const ItemId cell : key.cells) {
        hash = (hash ^ static_cast<std::size_t>(cell)) * 1099511628211ull;
    }
    hash = (hash ^ static_cast<std::size_t>(key.width * 16 + key.height)) * 1099511628211ull;
    return (hash ^ static_cast<std::size_t>(key.shapeless ? 1 : 0)) * 1099511628211ull;
}

/// **No two recipes claim the same grid.** Proved at compile time over the
/// *generated* table, not a hand-written list, so the eleven-wood and
/// sixteen-colour loops are covered along with everything typed out by hand.
///
/// This is worth a `static_assert` rather than a test because a collision is
/// silent: `findMatch` returns the first row that matches, so whichever of the
/// two was pushed earlier wins, and the loser simply never happens. That is
/// exactly how a second redstone torch recipe lived three hundred lines from
/// the first one without anybody noticing.
///
/// Open-addressed rather than a nested loop: 1,150 recipes compared pairwise is
/// two thirds of a million steps and MSVC gives us 100,000 by default.
constexpr bool noRecipeSharesAGrid(const std::vector<Recipe>& all) {
    constexpr std::size_t kBuckets = 4096;
    if (all.size() * 2 >= kBuckets) {
        return false;  // load factor too high; the probe below could not terminate
    }
    std::array<MatchKey, kBuckets> table{};
    std::array<bool, kBuckets> used{};
    for (const Recipe& recipe : all) {
        const MatchKey key = matchKeyOf(recipe);
        std::size_t slot = hashOf(key) % kBuckets;
        while (used[slot]) {
            if (table[slot] == key) {
                return false;
            }
            slot = (slot + 1) % kBuckets;
        }
        used[slot] = true;
        table[slot] = key;
    }
    return true;
}

/// The **multiset** of what a recipe consumes, with the arrangement thrown
/// away: sorted ingredients for a shapeless row, sorted non-empty cells for a
/// shaped one.
constexpr MatchKey ingredientMultisetOf(const Recipe& recipe) {
    MatchKey key;
    key.shapeless = true;
    for (int i = 0; i < (recipe.shapeless ? recipe.width : recipe.width * recipe.height); ++i) {
        const ItemId cell = recipe.pattern[static_cast<std::size_t>(i)];
        if (cell != kNone) {
            key.cells[static_cast<std::size_t>(key.width++)] = cell;
        }
    }
    for (int i = 1; i < key.width; ++i) {
        const ItemId held = key.cells[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 &&
               static_cast<int>(key.cells[static_cast<std::size_t>(j)]) > static_cast<int>(held)) {
            key.cells[static_cast<std::size_t>(j + 1)] = key.cells[static_cast<std::size_t>(j)];
            --j;
        }
        key.cells[static_cast<std::size_t>(j + 1)] = held;
    }
    return key;
}

/// **No shapeless recipe shadows a shaped one.**
///
/// The other half of the question, and it is not covered by the key above -
/// that one carries `shapeless` as a field, so a shapeless row and a shaped row
/// can never compare equal however alike they are. They still collide: a
/// shapeless recipe matches *any* arrangement of exactly its ingredients, so if
/// a shaped recipe's non-empty cells are the same multiset, the shaped
/// recipe's own layout satisfies both and whichever was pushed first wins.
/// Shapeless `{Redstone, Stick}` beside shaped 1x2 `{Redstone, Stick}` is
/// precisely that, and to the player they are one craft.
///
/// Multiset equality is the exact condition, in both directions: if the
/// multisets differ, no grid can satisfy both, because a shapeless match
/// requires the filled slots to be its multiset and nothing else.
///
/// Shaped-against-shaped is deliberately *not* checked this way - six planks in
/// a 3x2 and six planks in a 2x3 are the same multiset and two honestly
/// different crafts.
constexpr bool noShapelessShadowsAShape(const std::vector<Recipe>& all) {
    constexpr std::size_t kBuckets = 4096;
    if (all.size() * 2 >= kBuckets) {
        return false;
    }
    std::array<MatchKey, kBuckets> shapes{};
    std::array<bool, kBuckets> used{};
    for (const Recipe& recipe : all) {
        if (recipe.shapeless) {
            continue;
        }
        const MatchKey key = ingredientMultisetOf(recipe);
        std::size_t slot = hashOf(key) % kBuckets;
        while (used[slot] && !(shapes[slot] == key)) {
            slot = (slot + 1) % kBuckets;
        }
        used[slot] = true;
        shapes[slot] = key;
    }
    for (const Recipe& recipe : all) {
        if (!recipe.shapeless) {
            continue;
        }
        const MatchKey key = ingredientMultisetOf(recipe);
        std::size_t slot = hashOf(key) % kBuckets;
        while (used[slot]) {
            if (shapes[slot] == key) {
                return false;
            }
            slot = (slot + 1) % kBuckets;
        }
    }
    return true;
}

constexpr bool patternsUnique(const std::vector<Recipe>& all) {
    return noRecipeSharesAGrid(all) && noShapelessShadowsAShape(all);
}

/// **Every shaped pattern must fill its own outer edges**, or it can never be
/// crafted at all.
///
/// `occupiedBounds` trims the player's grid to the box that actually holds
/// items, and `matchesShaped` refuses unless that box is exactly `width` by
/// `height`. So a recipe *declared* three wide whose last column is empty, or
/// three tall whose top row is empty, is unreachable by any arrangement of any
/// items: the player can never produce a bounding box with a blank edge,
/// because a blank edge is precisely what the trim removes. The result item is
/// simply uncraftable.
///
/// **Only the outermost row and column each way can be wrong.** An empty
/// *interior* column is perfectly fine and several real recipes have one - a
/// boot's `X.X / X.X` leaves the middle column blank, and the bounds still
/// measure three wide because the outer cells are what set them. Testing every
/// column instead of the outer two is a stricter rule than the matcher's, and
/// it wrongly condemns the boots, the bucket, the bowl and the glass bottle.
///
/// **Neither uniqueness proof covers this.** Such a recipe has a perfectly
/// unique key and perfectly real ingredients; it collides with nothing and the
/// build is clean. The only symptom is a player reporting that one item cannot
/// be made.
constexpr bool everyShapedPatternIsTight(const std::vector<Recipe>& all) {
    const auto cell = [](const Recipe& recipe, int x, int y) constexpr {
        return recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)];
    };
    for (const Recipe& recipe : all) {
        if (recipe.shapeless) {
            continue;
        }
        bool top = false;
        bool bottom = false;
        for (int x = 0; x < recipe.width; ++x) {
            top = top || cell(recipe, x, 0) != kNone;
            bottom = bottom || cell(recipe, x, recipe.height - 1) != kNone;
        }
        bool left = false;
        bool right = false;
        for (int y = 0; y < recipe.height; ++y) {
            left = left || cell(recipe, 0, y) != kNone;
            right = right || cell(recipe, recipe.width - 1, y) != kNone;
        }
        if (!top || !bottom || !left || !right) {
            return false;
        }
    }
    return true;
}

/// The three proofs the recipe table gets, over **one** build of it - a second
/// `buildRecipes()` call would be a second run of the whole generator against
/// the compiler's step budget, which is what forced the hashed scans below to
/// be linear in the first place.
constexpr bool patternsAreSound() {
    const std::vector<Recipe> all = buildRecipes();
    return patternsUnique(all) && everyShapedPatternIsTight(all);
}

static_assert(patternsAreSound(),
              "either two recipes claim the same grid - findMatch would silently pick whichever "
              "was pushed first - or a shaped recipe has an entirely empty edge row or column, "
              "which occupiedBounds trims away and matchesShaped then rejects, leaving the item "
              "uncraftable. Restore the second redstone torch row, or the mirrored stairs "
              "pattern, to watch the first fire; write a shapeless row whose ingredients are "
              "some shaped row's cells to watch the second; widen any recipe's declared width "
              "by one without filling the new column to watch the third.");

}  // namespace

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> table = buildRecipes();
    return table;
}

namespace {

/// Bounding box of the occupied cells, so a pattern can be compared where it
/// actually sits rather than where it was dropped.
struct Bounds {
    int minX = kMaxCraftSize;
    int minY = kMaxCraftSize;
    int maxX = -1;
    int maxY = -1;

    bool empty() const { return maxX < 0; }
    int width() const { return maxX - minX + 1; }
    int height() const { return maxY - minY + 1; }
};

Bounds occupiedBounds(const ItemStack* slots, int size) {
    Bounds bounds;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (slots[static_cast<std::size_t>(y * size + x)].empty()) {
                continue;
            }
            bounds.minX = std::min(bounds.minX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }
    return bounds;
}

/// One hand of a shaped recipe against the grid.
bool matchesOneHand(const Recipe& recipe, const ItemStack* slots, int size, const Bounds& bounds,
                    bool mirrored) {
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const int column = mirrored ? recipe.width - 1 - x : x;
            const ItemId wanted =
                recipe.pattern[static_cast<std::size_t>(y * recipe.width + column)];
            const ItemStack& have =
                slots[static_cast<std::size_t>((bounds.minY + y) * size + bounds.minX + x)];
            if (wanted == kNone) {
                if (!have.empty()) {
                    return false;
                }
            } else if (have.empty() || have.item != wanted) {
                return false;
            }
        }
    }
    return true;
}

/// **Either hand.** The reference's rule, from wiki `[[Crafting]]`: "ingredients
/// in shaped recipes can be moved up, down, left, or right ... they can also be
/// flipped horizontally, but not vertically". Doing it here rather than by
/// writing a second row per recipe is what stopped the axe, the hoe, the shears,
/// the bow, the fishing rod, the observer and coarse dirt being uncraftable
/// left-handed - only the stairs had ever been given a twin, and one twin per
/// asymmetric recipe is a rule nothing was enforcing.
///
/// Vertical flipping stays refused, which is why the ladder, the bucket and the
/// spyglass keep their up-down order.
bool matchesShaped(const Recipe& recipe, const ItemStack* slots, int size, const Bounds& bounds) {
    if (bounds.width() != recipe.width || bounds.height() != recipe.height) {
        return false;
    }
    return matchesOneHand(recipe, slots, size, bounds, false) ||
           matchesOneHand(recipe, slots, size, bounds, true);
}

bool matchesShapeless(const Recipe& recipe, const ItemStack* slots, int size) {
    // Tick off each ingredient against one filled slot. Counting rather than
    // comparing sets, so a recipe wanting two of something is not satisfied by
    // one.
    std::array<bool, kMaxCraftSlots> used{};
    int matched = 0;

    for (int i = 0; i < recipe.width; ++i) {
        bool found = false;
        for (int s = 0; s < size * size && !found; ++s) {
            const auto at = static_cast<std::size_t>(s);
            if (used[at] || slots[at].empty() || slots[at].item != recipe.pattern[static_cast<std::size_t>(i)]) {
                continue;
            }
            used[at] = true;
            found = true;
            ++matched;
        }
        if (!found) {
            return false;
        }
    }

    // Nothing may be left over, or a grid holding an extra item would still
    // craft and quietly eat it.
    int filled = 0;
    for (int s = 0; s < size * size; ++s) {
        if (!slots[static_cast<std::size_t>(s)].empty()) {
            ++filled;
        }
    }
    return filled == matched;
}

const Recipe* findMatch(const ItemStack* slots, int size) {
    const Bounds bounds = occupiedBounds(slots, size);
    if (bounds.empty()) {
        return nullptr;
    }

    for (const Recipe& recipe : recipes()) {
        const bool hit = recipe.shapeless ? matchesShapeless(recipe, slots, size)
                                          : matchesShaped(recipe, slots, size, bounds);
        if (hit) {
            return &recipe;
        }
    }
    return nullptr;
}

} // namespace

ItemStack craftResult(const ItemStack* slots, int size) {
    const Recipe* recipe = findMatch(slots, size);
    return recipe != nullptr ? recipe->result : ItemStack{};
}

void consumeIngredients(ItemStack* slots, int size) {
    if (findMatch(slots, size) == nullptr) {
        return;
    }
    // One of everything present, which is right for every recipe here: a cell
    // holding a stack contributes exactly one item per craft.
    for (int s = 0; s < size * size; ++s) {
        ItemStack& slot = slots[static_cast<std::size_t>(s)];
        if (slot.empty()) {
            continue;
        }
        if (--slot.count <= 0) {
            slot = ItemStack{};
        }
    }
}

std::unordered_set<ItemId> craftableItems(const Inventory& inventory, int gridSize) {    std::unordered_map<ItemId, int> owned;
    for (std::size_t i = 0; i < Inventory::size(); ++i) {
        const ItemStack& stack = inventory.slot(i);
        if (!stack.empty()) {
            owned[stack.item] += stack.count;
        }
    }

    std::unordered_set<ItemId> makeable;
    std::unordered_map<ItemId, int> wanted;
    for (const Recipe& recipe : recipes()) {
        if (gridSize < kMaxCraftSize && !recipe.fitsInTwoByTwo) {
            continue;
        }
        if (makeable.count(recipe.result.item) != 0) {
            continue;
        }

        // A shapeless recipe keeps its ingredient *count* in `width`; a shaped
        // one spells out its whole rectangle and leaves gaps as `None`. Every
        // filled cell consumes exactly one item, which is true of every recipe
        // in the reference - there is no "this slot needs three sticks".
        wanted.clear();
        const int cells = recipe.shapeless ? recipe.width : recipe.width * recipe.height;
        for (int c = 0; c < cells; ++c) {
            const ItemId ingredient = recipe.pattern[static_cast<std::size_t>(c)];
            if (ingredient != ItemId::None) {
                ++wanted[ingredient];
            }
        }

        // **Greedy, and correct only while every ingredient is one concrete
        // item.** Do not simplify this comment away with the check.
        //
        // The general problem is bipartite matching, not counting. Suppose a
        // recipe wants `any plank` + `oak plank` and the player holds one oak
        // plank and one spruce plank: a greedy pass spends the oak on
        // `any plank` because it matches, then fails `oak plank`, and reports
        // "not craftable" - wrongly, because reassigning `any plank` to the
        // spruce frees the oak. A matcher backtracks and finds that; a count
        // cannot. It fails **silently**, and only for the players who happen to
        // be holding the awkward combination.
        //
        // `Recipe::pattern` is an array of plain `ItemId`s today, so no
        // ingredient can name a set and the counts can never collide. The day
        // that changes - the first second wood type will do it - this has to
        // become Kuhn's algorithm, and nobody will connect the two unless it is
        // written down here.
        bool affordable = true;
        for (const auto& [ingredient, count] : wanted) {
            const auto held = owned.find(ingredient);
            if (held == owned.end() || held->second < count) {
                affordable = false;
                break;
            }
        }
        if (affordable) {
            makeable.insert(recipe.result.item);
        }
    }
    return makeable;
}

/// One brewing step: what goes in, what is stirred into it, and what comes out.
/// Indices are into `kPotions`.
struct Brew {
    int from;
    ItemId reagent;
    int to;
};

/// Mojang's own brewing tree, transcribed from the shipped `brew_*.json` files.
///
/// **The first match wins and the table is read in order**, which matters for
/// the fermented spider eye: it appears against a dozen different inputs and
/// means something different against each.
constexpr std::array<Brew, 53> kBrews{{
    // Water into the three things that are not junk.
    {0, ItemId::NetherWart, 3},
    {0, ItemId::GlowstoneDust, 2},
    {0, ItemId::FermentedSpiderEye, 34},
    // Awkward into every effect there is.
    {3, ItemId::GoldenCarrot, 4},
    {3, ItemId::RabbitFoot, 8},
    {3, ItemId::CinderPowder, 31},
    {3, ItemId::MagmaCream, 11},
    {3, ItemId::Sugar, 13},
    // **The reference's pufferfish, and it is a loose one.**
    // `recipes/brew_awkward_puffer_fish.json` states it outright:
    // `"input": "minecraft:potion_type:awkward"`,
    // `"reagent": "minecraft:pufferfish"`, `"output":
    // "minecraft:potion_type:water_breathing"` - a **loose item, not a
    // bucket.** This row said `PufferfishBucket` under a comment claiming "we
    // have no loose pufferfish item" - `ItemId::RawPufferfish` has existed all
    // along and is a live common drop off the pufferfish in `Creature.cpp`'s
    // `kLoot`, while **nothing in the game produces a bucket of anything**, so
    // the divergence made water breathing, its extended form and both splash
    // forms unobtainable rather than merely dearer. `CRAFTABLE.md` repeats the
    // same false premise and needs the same correction.
    //
    // **The whole table is now checked against that source, not the wiki**: all
    // 61 rows it shares with the shipped `brew_*.json` agree exactly, reagent
    // and output both. The nine it lacks are the four 1.21 potions and their
    // water-junk rows, which need effects this game has no machinery for.
    {3, ItemId::RawPufferfish, 19},
    {3, ItemId::GlisteringMelonSlice, 21},
    {3, ItemId::DrifterTear, 28},
    {3, ItemId::SpiderEye, 25},
    {3, ItemId::PhantomMembrane, 39},
    {3, ItemId::TurtleHelmet, 36},
    {3, ItemId::FermentedSpiderEye, 34},
    // Redstone lengthens. Every row here is a potion that has an extended form,
    // and it always sits one along from the base one.
    {4, ItemId::Redstone, 5},
    {6, ItemId::Redstone, 7},
    {8, ItemId::Redstone, 9},
    {11, ItemId::Redstone, 12},
    {13, ItemId::Redstone, 14},
    {16, ItemId::Redstone, 17},
    {19, ItemId::Redstone, 20},
    {25, ItemId::Redstone, 26},
    {28, ItemId::Redstone, 29},
    {31, ItemId::Redstone, 32},
    {34, ItemId::Redstone, 35},
    {36, ItemId::Redstone, 37},
    {39, ItemId::Redstone, 40},
    // Glowstone strengthens, and always shortens what it strengthens.
    {8, ItemId::GlowstoneDust, 10},
    {13, ItemId::GlowstoneDust, 15},
    {16, ItemId::GlowstoneDust, 18},
    {21, ItemId::GlowstoneDust, 22},
    {23, ItemId::GlowstoneDust, 24},
    {25, ItemId::GlowstoneDust, 27},
    {28, ItemId::GlowstoneDust, 30},
    {31, ItemId::GlowstoneDust, 33},
    {36, ItemId::GlowstoneDust, 38},
    // The fermented spider eye corrupts, and what it corrupts a thing *into*
    // depends entirely on what it was. Strong strength becomes plain weakness
    // rather than a strong one, which is a Bedrock quirk and not a mistake.
    {1, ItemId::FermentedSpiderEye, 34},
    {2, ItemId::FermentedSpiderEye, 34},
    {13, ItemId::FermentedSpiderEye, 16},
    {14, ItemId::FermentedSpiderEye, 17},
    {8, ItemId::FermentedSpiderEye, 16},
    {9, ItemId::FermentedSpiderEye, 17},
    {4, ItemId::FermentedSpiderEye, 6},
    {5, ItemId::FermentedSpiderEye, 7},
    {21, ItemId::FermentedSpiderEye, 23},
    {22, ItemId::FermentedSpiderEye, 24},
    {25, ItemId::FermentedSpiderEye, 23},
    {26, ItemId::FermentedSpiderEye, 23},
    {27, ItemId::FermentedSpiderEye, 24},
    {31, ItemId::FermentedSpiderEye, 34},
    {32, ItemId::FermentedSpiderEye, 35},
    {33, ItemId::FermentedSpiderEye, 34},
}};

/// Everything that turns water into a mundane potion - which is to say, every
/// reagent the reference bothers to have a recipe for that leads nowhere.
constexpr bool brewsToMundane(ItemId reagent) {
    return reagent == ItemId::Redstone || reagent == ItemId::Sugar ||
           reagent == ItemId::SpiderEye || reagent == ItemId::DrifterTear ||
           reagent == ItemId::MagmaCream || reagent == ItemId::CinderPowder ||
           reagent == ItemId::GlisteringMelonSlice || reagent == ItemId::RabbitFoot;
}

ItemStack brewingResult(const ItemStack& bottle, const ItemStack& reagent) {
    if (bottle.empty() || reagent.empty()) {
        return ItemStack{};
    }
    const bool splash = isSplashPotion(bottle.item);
    // **The water bottle you brew from is `WaterBottle`, the filled bottle that
    // predates every potion here** - and the duplicate this comment used to
    // call hypothetical is already in the catalogue. Index 0 of the potion run
    // is a real row, "Bottle of Water" sitting beside `WaterBottle`'s "Water
    // Bottle", and the `index` line below is what makes it unreachable: water
    // enters *as* index 0, so nothing ever names index 0 as a result. Measured
    // 2026-08-19 - 0 brew pairs and 0 recipes produce it, against 11 recipes
    // for a stick.
    //
    // **It is not a spare row to delete.** `potionAt` addresses four parallel
    // runs off one index, and index 0's other forms are live: splash water is
    // gunpowder on this bottle, lingering water is dragon's breath on that, and
    // the tipped run deliberately starts past it at `kFirstTippedPotion`.
    // Dropping the enumerator slides every id above it in all four runs -
    // through both `potionAt` static_asserts in `Item.hpp` and through every
    // potion already sitting in a saved inventory.
    const bool water = bottle.item == ItemId::WaterBottle;
    if (!water && !isDrinkablePotion(bottle.item) && !splash) {
        return ItemStack{};
    }
    const int index = water ? 0 : potionIndex(bottle.item);
    // Gunpowder changes the bottle rather than the brew, so it works on every
    // potion there is - one rule against forty-one, which is how the reference
    // does it too. Dragon's breath does the same again, one form further on.
    if (reagent.item == ItemId::Gunpowder) {
        return splash || isLingeringPotion(bottle.item) ? ItemStack{}
                                                       : ItemStack{potionAt(index, 1), 1};
    }
    if (reagent.item == ItemId::DragonBreath) {
        return splash ? ItemStack{potionAt(index, 3), 1} : ItemStack{};
    }
    for (const Brew& brew : kBrews) {
        if (brew.from == index && brew.reagent == reagent.item) {
            return ItemStack{potionAt(brew.to, splash ? 1 : 0), 1};
        }
    }
    // A junk reagent in water is not nothing: it is a mundane potion, and that
    // is the reference's own answer rather than a refusal.
    if (index == 0 && brewsToMundane(reagent.item)) {
        return ItemStack{potionAt(1, splash ? 1 : 0), 1};
    }
    return ItemStack{};
}

ItemStack smithingResult(const ItemStack& base, const ItemStack& addition) {
    if (base.empty() || addition.empty() || addition.item != ItemId::EmberiteIngot) {
        return ItemStack{};
    }
    // The five diamond tools against their Emberite counterparts. Both runs are
    // contiguous and in the same order, so this is one range test rather than
    // five cases - the same arithmetic the spawn eggs and the tool sprites use.
    const int step = static_cast<int>(base.item) - static_cast<int>(ItemId::DiamondPickaxe);
    if (step >= 0 && step < 5) {
        return ItemStack{static_cast<ItemId>(static_cast<int>(ItemId::EmberitePickaxe) + step), 1};
    }
    // And the four diamond armour pieces against theirs, which are contiguous
    // and in the same slot order for exactly the same reason.
    const int piece = static_cast<int>(base.item) - static_cast<int>(ItemId::DiamondHelmet);
    if (piece >= 0 && piece < 4) {
        return ItemStack{static_cast<ItemId>(static_cast<int>(ItemId::EmberiteHelmet) + piece), 1};
    }
    return ItemStack{};
}

ItemStack repairResult(const ItemStack& left, const ItemStack& right) {
    if (left.empty() || right.empty() || left.item != right.item) {
        return ItemStack{};
    }
    const ToolProperties properties = toolFor(left.item);
    const int maximum = properties.durability > 0 ? properties.durability
                                                  : (left.item == ItemId::Bow ? kBowDurability : 0);
    if (maximum <= 0) {
        return ItemStack{};
    }
    // Damage counts up from zero, so what is left is the maximum minus it.
    const int remaining = (maximum - left.damage) + (maximum - right.damage) + maximum / 20;
    const int repaired = maximum - (remaining > maximum ? maximum : remaining);
    ItemStack result{left.item, 1};
    result.damage = static_cast<decltype(result.damage)>(repaired < 0 ? 0 : repaired);
    return result;
}

} // namespace game
