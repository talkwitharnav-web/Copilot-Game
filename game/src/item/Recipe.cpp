#include "item/Recipe.hpp"

#include "item/Tool.hpp"

#include "item/Inventory.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace game {
namespace {

constexpr ItemId kNone = ItemId::None;

Recipe shaped(int width, int height, std::array<ItemId, kMaxCraftSlots> pattern, ItemId result, int count) {
    Recipe recipe;
    recipe.pattern = pattern;
    recipe.width = width;
    recipe.height = height;
    recipe.result = ItemStack{result, count};
    return recipe;
}

Recipe shapeless(std::array<ItemId, kMaxCraftSlots> ingredients, int used, ItemId result, int count) {
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
Recipe square4(ItemId input, ItemId result, int count) {
    return shaped(2, 2, {input, input, input, input}, result, count);
}

Recipe square4(BlockId input, BlockId result, int count) {
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

Recipe square9(ItemId input, ItemId result) {
    return shaped(3, 3, {input, input, input, input, input, input, input, input, input}, result, 1);
}

Recipe square9(ItemId input, BlockId result) { return square9(input, itemForBlock(result)); }

/// A row of three, which is the shape the reference uses for slabs, paper and
/// glass bottles alike.
Recipe row3(ItemId input, ItemId result, int count) {
    return shaped(3, 1, {input, input, input}, result, count);
}

/// Eight around an empty middle, and the same eight around something. Both have
/// to be stored at 3x3 rather than trimmed, because the hole is part of the
/// shape.
Recipe ring8(ItemId input, ItemId result, int count) {
    return shaped(3, 3, {input, input, input, input, kNone, input, input, input, input}, result,
                  count);
}

Recipe ring8Around(ItemId input, ItemId centre, ItemId result, int count) {
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
};

constexpr std::array<Wood, 11> kWoods{{
    {BlockId::Log, BlockId::StrippedOakLog, BlockId::Planks},
    {BlockId::SpruceLog, BlockId::StrippedSpruceLog, BlockId::SprucePlanks},
    {BlockId::BirchLog, BlockId::StrippedBirchLog, BlockId::BirchPlanks},
    {BlockId::JungleLog, BlockId::StrippedJungleLog, BlockId::JunglePlanks},
    {BlockId::AcaciaLog, BlockId::StrippedAcaciaLog, BlockId::AcaciaPlanks},
    {BlockId::DarkOakLog, BlockId::StrippedDarkOakLog, BlockId::DarkOakPlanks},
    {BlockId::CherryLog, BlockId::StrippedCherryLog, BlockId::CherryPlanks},
    {BlockId::MangroveLog, BlockId::StrippedMangroveLog, BlockId::MangrovePlanks},
    {BlockId::CrimsonStem, BlockId::StrippedCrimsonStem, BlockId::CrimsonPlanks},
    {BlockId::WarpedStem, BlockId::StrippedWarpedStem, BlockId::WarpedPlanks},
    {BlockId::BambooBlock, BlockId::StrippedBambooBlock, BlockId::BambooPlanks},
}};

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
Recipe tool(ItemId material, ItemId result, const char* pattern) {
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
            default:
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

} // namespace

/// Every recipe in the game.
///
/// Shapes and yields are taken from the reference recipe data - see
/// `CRAFTABLE.md`, which records each one and where it came from.
const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> table = [] {
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
            // The reference grows moss from vines, which do not exist here yet;
            // tall grass is the stand-in and is a deliberate divergence.
            shapeless({itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::TallGrass)}, 2,
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
            square4(BlockId::CobbledDeepslate, BlockId::PolishedDeepslate, 4),
            square4(BlockId::PolishedDeepslate, BlockId::DeepslateBricks, 4),
            square4(BlockId::DeepslateBricks, BlockId::DeepslateTiles, 4),
            square4(BlockId::Prismarine, BlockId::PrismarineBricks, 4),
            square4(BlockId::Sandstone, BlockId::CutSandstone, 4),
            shapeless({itemForBlock(BlockId::StoneBricks), itemForBlock(BlockId::TallGrass)}, 2,
                      itemForBlock(BlockId::MossyStoneBricks), 1),

            // Nine into one, the storage-block shape. Reversible in the
            // reference; here it is one way until a 1x1 unpack recipe exists.
            square9(ItemId::Coal, BlockId::CoalBlock),
            square9(ItemId::IronIngot, BlockId::IronBlock),
            square9(ItemId::GoldIngot, BlockId::GoldBlock),
            square9(ItemId::Diamond, BlockId::DiamondBlock),
            square9(ItemId::Emerald, BlockId::EmeraldBlock),
            square9(ItemId::LapisLazuli, BlockId::LapisBlock),
            square9(ItemId::Redstone, BlockId::RedstoneBlock),
            square9(ItemId::CopperIngot, BlockId::CopperBlock),

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
        all.push_back(shaped(3, 3,
                             {ItemId::Stick, ItemId::IronIngot, ItemId::Stick, ItemId::String,
                              ItemId::Stick, ItemId::String, kNone, ItemId::Stick, kNone},
                             ItemId::Crossbow, 1));
        all.push_back(shaped(1, 3,
                             {ItemId::CopperIngot, ItemId::CopperIngot, ItemId::AmethystShard},
                             ItemId::Spyglass, 1));
        all.push_back(shaped(1, 3, {ItemId::Feather, ItemId::CopperIngot, ItemId::Stick},
                             ItemId::Brush, 1));
        all.push_back(shapeless({ItemId::Paper, ItemId::Gunpowder}, 2, ItemId::FireworkRocket, 3));
        // A star is a dye burst into shape by gunpowder, and a rocket built on
        // one carries that colour up with it. **The loop reads the dye run**,
        // so a seventeenth dye would be a seventeenth star without an edit here.
        for (int colour = 0; colour < kDyeColours; ++colour) {
            const ItemId dye = static_cast<ItemId>(static_cast<int>(kFirstDye) + colour);
            const ItemId star =
                static_cast<ItemId>(static_cast<int>(ItemId::FireworkStarFirst) + colour);
            all.push_back(shapeless({ItemId::Gunpowder, dye}, 2, star, 1));
            all.push_back(shapeless({ItemId::Paper, ItemId::Gunpowder, star}, 3,
                                    ItemId::FireworkRocket, 3));
        }
        all.push_back(shapeless({ItemId::Book, ItemId::Feather, ItemId::InkSac}, 3,
                                ItemId::BookAndQuill, 1));
        all.push_back(shapeless({ItemId::String, ItemId::String, ItemId::String, ItemId::String,
                                 ItemId::Slimeball},
                                5, ItemId::Lead, 2));
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
        // The gilded pair are built from nuggets, above, which is the
        // reference's own recipe and a ninth of the cost.


        // Candles and waxed copper, both generated because both are one shape
        // repeated over a colour or an oxidation stage - and both runs are
        // declared in the same order as the family they index, so the offset
        // *is* the mapping.
        all.push_back(shapeless({ItemId::String, ItemId::Honeycomb}, 2,
                                itemForBlock(BlockId::Candle), 1));
        // **Named divergence: redstone where the reference wants a tripwire
        // hook.** There is no hook in the game, and adding one to serve a single
        // recipe would be a whole block for no other purpose.
        all.push_back(shapeless({itemForBlock(BlockId::Chest), ItemId::Redstone}, 2,
                                itemForBlock(BlockId::TrappedChest), 1));

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
        for (int stage = 0; stage < 9; ++stage) {
            static constexpr BlockId kUnwaxed[9] = {
                BlockId::CopperBlock,        BlockId::ExposedCopper,
                BlockId::WeatheredCopper,    BlockId::OxidizedCopper,
                BlockId::CutCopper,          BlockId::ExposedCutCopper,
                BlockId::WeatheredCutCopper, BlockId::OxidizedCutCopper,
                BlockId::ChiseledCopper,
            };
            all.push_back(shapeless({itemForBlock(kUnwaxed[stage]), ItemId::Honeycomb}, 2,
                                    itemForBlock(static_cast<BlockId>(
                                        static_cast<int>(BlockId::WaxedCopperBlock) + stage)),
                                    1));
        }
        // Bark blocks: four logs in a square, the reference's own recipe, and
        // the one thing that made a *wood* block worth having.
        {
            static constexpr BlockId kBarkLogs[10] = {
                BlockId::Log,         BlockId::SpruceLog,   BlockId::BirchLog,
                BlockId::JungleLog,   BlockId::AcaciaLog,   BlockId::DarkOakLog,
                BlockId::CherryLog,   BlockId::MangroveLog, BlockId::CrimsonStem,
                BlockId::WarpedStem,
            };
            static constexpr BlockId kStrippedLogs[10] = {
                BlockId::StrippedOakLog,      BlockId::StrippedSpruceLog,
                BlockId::StrippedBirchLog,    BlockId::StrippedJungleLog,
                BlockId::StrippedAcaciaLog,   BlockId::StrippedDarkOakLog,
                BlockId::StrippedCherryLog,   BlockId::StrippedMangroveLog,
                BlockId::StrippedCrimsonStem, BlockId::StrippedWarpedStem,
            };
            for (int wood = 0; wood < 10; ++wood) {
                all.push_back(square4(
                    kBarkLogs[wood],
                    static_cast<BlockId>(static_cast<int>(BlockId::OakWood) + wood), 3));
                all.push_back(square4(
                    kStrippedLogs[wood],
                    static_cast<BlockId>(static_cast<int>(BlockId::StrippedOakWood) + wood), 3));
            }
        }

        // Nuggets, both ways. Nine to an ingot, which is what makes the two
        // gilded foods below cost a ninth of what whole ingots would.
        all.push_back(shapeless({ItemId::IronIngot}, 1, ItemId::IronNugget, 9));
        all.push_back(shapeless({ItemId::GoldIngot}, 1, ItemId::GoldNugget, 9));
        all.push_back(square9(ItemId::IronNugget, ItemId::IronIngot));
        all.push_back(square9(ItemId::GoldNugget, ItemId::GoldIngot));
        all.push_back(ring8Around(ItemId::GoldNugget, ItemId::Carrot, ItemId::GoldenCarrot, 1));
        all.push_back(
            ring8Around(ItemId::GoldNugget, ItemId::MelonSlice, ItemId::GlisteringMelonSlice, 1));

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

        // The three village workstations that had no source at all. All the
        // reference's own shapes.
        all.push_back(shaped(3, 2, {kNone, ItemId::IronIngot, kNone,
                                    itemForBlock(BlockId::Stone), itemForBlock(BlockId::Stone),
                                    itemForBlock(BlockId::Stone)},
                             itemForBlock(BlockId::Stonecutter), 1));
        {
            const ItemId slab = itemForBlock(BlockId::StoneSlab);
            all.push_back(shaped(3, 3,
                                 {slab, slab, slab, kNone, itemForBlock(BlockId::Bookshelf), kNone,
                                  kNone, slab, kNone},
                                 itemForBlock(BlockId::Lectern), 1));
        }
        all.push_back(shaped(2, 3,
                             {itemForBlock(BlockId::Planks), itemForBlock(BlockId::Planks),
                              ItemId::Stick, ItemId::Stick, itemForBlock(BlockId::Planks),
                              itemForBlock(BlockId::Planks)},
                             itemForBlock(BlockId::Grindstone), 1));

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
            all.push_back(shapeless({itemForBlock(wood.log)}, 1, planks, 4));
            all.push_back(shapeless({itemForBlock(wood.stripped)}, 1, planks, 4));
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
            // Two iron over four planks.
            all.push_back(shaped(2, 3,
                                 {ItemId::IronIngot, ItemId::IronIngot, planks, planks, planks, planks},
                                 itemForBlock(BlockId::SmithingTable), 1));
            // Six planks round a hollow: the reference's own composter, and a
            // shield with an iron boss.
            all.push_back(shaped(3, 3,
                                 {planks, kNone, planks, planks, kNone, planks, planks, planks,
                                  planks},
                                 itemForBlock(BlockId::Composter0), 1));
            all.push_back(shaped(3, 3,
                                 {planks, ItemId::IronIngot, planks, planks, planks, planks, kNone,
                                  planks, kNone},
                                 ItemId::Shield, 1));
            // A barrel is planks walled round two slabs of the same wood, which
            // is the reference's own recipe and the one that makes a slab worth
            // cutting for something other than stairs.
            if (const int slabFamily = slabFamilyOf(wood.planks); slabFamily >= 0) {
                const ItemId slab = itemForBlock(slabAt(slabFamily, false));
                all.push_back(shaped(3, 3,
                                     {planks, slab, planks, planks, kNone, planks, planks, slab,
                                      planks},
                                     itemForBlock(BlockId::Barrel), 1));
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

            // Stone answers to the same two shapes the woods do.
            all.push_back(shapeless({stone}, 1, itemForBlock(buttonAt(11, 0, false)), 1));
            all.push_back(shaped(2, 1, {stone, stone}, itemForBlock(pressurePlateAt(11, 0)), 1));
            // The two that weigh what stands on them are cut from the metal
            // they measure with.
            all.push_back(shaped(2, 1, {gold, gold}, itemForBlock(pressurePlateAt(12, 0)), 1));
            all.push_back(shaped(2, 1, {iron, iron}, itemForBlock(pressurePlateAt(13, 0)), 1));

            // A torch is a stick with dust on the end of it.
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
            all.push_back(ring8Around(redstone, itemForBlock(BlockId::Glowstone),
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
                                 {iron, kNone, iron, iron, itemForBlock(pressurePlateAt(11, 0)),
                                  iron, iron, redstone, iron},
                                 itemForBlock(railAt(2, 0, false)), 6));
            all.push_back(shaped(3, 3,
                                 {iron, stick, iron, iron, redstone, iron, iron, stick, iron},
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
        // **Stairs get both mirrorings.** The reference matches either hand of
        // the staircase and our matcher does not mirror, so the second pattern
        // is what stops half the players finding stairs uncraftable.
        for (int family = 0; family < kStairFamilyCount; ++family) {
            const ItemId material = itemForBlock(kStairFamilies[static_cast<std::size_t>(family)].parent);
            const ItemId result = itemForBlock(stairsAt(family, Facing::North, false));
            all.push_back(shaped(3, 3,
                                 {material, kNone, kNone, material, material, kNone, material,
                                  material, material},
                                 result, 4));
            all.push_back(shaped(3, 3,
                                 {kNone, kNone, material, kNone, material, material, material,
                                  material, material},
                                 result, 4));
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
        // **Named divergence:** the reference's brown dye is cocoa, which needs
        // a jungle crop we have no source for. The brown mushroom is the only
        // brown thing that grows here.
        all.push_back(shapeless({itemForBlock(BlockId::BrownMushroom)}, 1, dye(kBrown), 1));
        for (const DyeMix& mix : kDyeMixes) {
            all.push_back(shapeless({dye(mix.a), dye(mix.b)}, 2, dye(mix.result), 2));
        }

        // The three dyed families, sixteen colours each. All four runs are
        // declared white-first in the same order, so a colour is one offset.
        all.push_back(square4(ItemId::String, itemForBlock(BlockId::WhiteWool), 1));
        for (int colour = 0; colour < kDyeColours; ++colour) {
            if (colour != kWhite) {
                all.push_back(
                    shapeless({itemForBlock(BlockId::WhiteWool), dye(colour)}, 2,
                              tinted(BlockId::WhiteWool, colour), 1));
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
        all.push_back(square4(BlockId::CopperBlock, BlockId::CutCopper, 4));
        all.push_back(square4(ItemId::Quartz, itemForBlock(BlockId::QuartzBlock), 1));
        all.push_back(square4(BlockId::QuartzBlock, BlockId::QuartzBricks, 4));
        all.push_back(square4(ItemId::PoppedChorusFruit, itemForBlock(BlockId::PurpurBlock), 4));
        all.push_back(square4(ItemId::NetherBrickItem, itemForBlock(BlockId::NetherBricks), 1));
        all.push_back(square4(ItemId::Brick, itemForBlock(BlockId::Bricks), 1));
        all.push_back(shaped(1, 2, {itemForBlock(BlockId::QuartzBlock), itemForBlock(BlockId::QuartzBlock)},
                             itemForBlock(BlockId::QuartzPillar), 2));
        all.push_back(shaped(1, 2, {itemForBlock(BlockId::PurpurBlock), itemForBlock(BlockId::PurpurBlock)},
                             itemForBlock(BlockId::PurpurPillar), 2));

        // Nine into one, for everything whose loose form exists.
        all.push_back(square9(ItemId::BoneMeal, BlockId::BoneBlock));
        all.push_back(square9(ItemId::Slimeball, BlockId::SlimeBlock));
        all.push_back(square9(ItemId::DriedKelp, BlockId::DriedKelpBlock));
        all.push_back(square9(ItemId::RawIron, BlockId::RawIronBlock));
        all.push_back(square9(ItemId::RawGold, BlockId::RawGoldBlock));
        all.push_back(square9(ItemId::RawCopper, BlockId::RawCopperBlock));
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
        all.push_back(shaped(3, 2,
                             {ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot,
                              ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot},
                             itemForBlock(BlockId::IronBars), 16));
        // Seven sticks in an H, the reference's ladder.
        all.push_back(shaped(3, 3,
                             {ItemId::Stick, kNone, ItemId::Stick, ItemId::Stick, ItemId::Stick,
                              ItemId::Stick, ItemId::Stick, kNone, ItemId::Stick},
                             itemForBlock(BlockId::LadderNorth), 3));
        // **Named divergence on both lanterns**: the reference cages a torch in
        // eight iron nuggets, and there is no nugget here. One ingot stands in
        // for the eight, which keeps the shape of the idea - metal around a
        // flame - at a price a player can actually pay.
        all.push_back(shapeless({ItemId::IronIngot, itemForBlock(BlockId::Torch)}, 2,
                                itemForBlock(BlockId::Lantern), 1));
        all.push_back(shapeless({ItemId::IronIngot, itemForBlock(BlockId::SoulTorch)}, 2,
                                itemForBlock(BlockId::SoulLantern), 1));
        // A torch over soul sand, which is exactly how the reference makes one.
        all.push_back(shaped(1, 3,
                             {ItemId::Coal, ItemId::Stick, itemForBlock(BlockId::SoulSand)},
                             itemForBlock(BlockId::SoulTorch), 4));
        all.push_back(shaped(1, 2, {ItemId::Redstone, ItemId::Stick},
                             itemForBlock(BlockId::RedstoneTorch), 1));
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
        all.push_back(shapeless({ItemId::Paper, ItemId::Paper, ItemId::Paper, ItemId::Leather}, 4,
                                ItemId::Book, 1));
        all.push_back(shaped(3, 1,
                             {itemForBlock(BlockId::Glass), itemForBlock(BlockId::Glass),
                              itemForBlock(BlockId::Glass)},
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

        // Derived here rather than at each recipe, so neither can be forgotten
        // on a new row. A shapeless recipe keeps its ingredient *count* in
        // `width`, so it fits the player's grid when it uses four or fewer -
        // comparing it against the pattern extents would be wrong.
        for (Recipe& recipe : all) {
            recipe.category = categoryFor(recipe.result.item);
            recipe.fitsInTwoByTwo =
                recipe.shapeless ? recipe.width <= 4 : recipe.width <= 2 && recipe.height <= 2;
        }
        return all;
    }();
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

bool matchesShaped(const Recipe& recipe, const ItemStack* slots, int size, const Bounds& bounds) {
    if (bounds.width() != recipe.width || bounds.height() != recipe.height) {
        return false;
    }
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const ItemId wanted = recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)];
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
    {3, ItemId::BlazePowder, 31},
    {3, ItemId::MagmaCream, 11},
    {3, ItemId::Sugar, 13},
    // **Named divergence: the reference brews water breathing from a raw
    // pufferfish, and we have no loose pufferfish item** - only the bucket the
    // live one swims in. The bucket is what this costs instead.
    {3, ItemId::PufferfishBucket, 19},
    {3, ItemId::GlisteringMelonSlice, 21},
    {3, ItemId::GhastTear, 28},
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
           reagent == ItemId::SpiderEye || reagent == ItemId::GhastTear ||
           reagent == ItemId::MagmaCream || reagent == ItemId::BlazePowder ||
           reagent == ItemId::GlisteringMelonSlice || reagent == ItemId::RabbitFoot;
}

ItemStack brewingResult(const ItemStack& bottle, const ItemStack& reagent) {
    if (bottle.empty() || reagent.empty()) {
        return ItemStack{};
    }
    const bool splash = isSplashPotion(bottle.item);
    // **The water bottle you brew from is the one that was already in the
    // game**, not a forty-second potion that happens to be water. Two items
    // with the same name in the catalogue would be the wart, and the filled
    // bottle predates every potion here.
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
