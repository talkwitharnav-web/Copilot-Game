#include "item/Recipe.hpp"

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
            // A furnace wrapped in logs. Logs rather than planks because the
            // fuel is the point of the block.
            const ItemId log = itemForBlock(wood.log);
            all.push_back(shaped(3, 3,
                                 {kNone, log, kNone, log, itemForBlock(BlockId::Furnace), log, kNone,
                                  log, kNone},
                                 itemForBlock(BlockId::Smoker), 1));
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

ItemStack smithingResult(const ItemStack& base, const ItemStack& addition) {
    if (base.empty() || addition.empty() || addition.item != ItemId::EmberiteIngot) {
        return ItemStack{};
    }
    // The five diamond tools against their Emberite counterparts. Both runs are
    // contiguous and in the same order, so this is one range test rather than
    // five cases - the same arithmetic the spawn eggs and the tool sprites use.
    const int step = static_cast<int>(base.item) - static_cast<int>(ItemId::DiamondPickaxe);
    if (step < 0 || step >= 5) {
        return ItemStack{};
    }
    return ItemStack{static_cast<ItemId>(static_cast<int>(ItemId::EmberitePickaxe) + step), 1};
}

} // namespace game
