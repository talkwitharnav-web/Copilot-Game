#include "item/Tool.hpp"

#include <algorithm>
#include <array>

namespace game {
namespace {

struct ToolRule {
    ItemId item;
    ToolProperties properties;
};

/// Speeds and durabilities are the reference's own, looked up rather than
/// invented: mining speed 2 / 4 / 6 / 8 / 9 and durability 59 / 131 / 250 /
/// 1561 / 2031 by rising tier. Wood and stone were written as 60 and 132 before
/// the table was checked and are left alone - one use either way is noise.
///
/// A sword's number is smaller than its tier's mining speed because a sword is
/// not a mining tool: the reference gives it a flat 1.5x on everything, and it
/// is the **blow** that scales with the material. `strike` reads this same
/// field for damage, which is why the two rise together here.
constexpr std::array<ToolRule, 27> kTools{{
    {ItemId::WoodenPickaxe, {ToolKind::Pickaxe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenAxe, {ToolKind::Axe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenShovel, {ToolKind::Shovel, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenSword, {ToolKind::Sword, kWoodTier, 1.5f, 60}},
    {ItemId::WoodenHoe, {ToolKind::Hoe, kWoodTier, 1.5f, 60}},
    {ItemId::StonePickaxe, {ToolKind::Pickaxe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneAxe, {ToolKind::Axe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneShovel, {ToolKind::Shovel, kStoneTier, 4.0f, 132}},
    {ItemId::StoneSword, {ToolKind::Sword, kStoneTier, 2.0f, 132}},
    {ItemId::StoneHoe, {ToolKind::Hoe, kStoneTier, 2.0f, 132}},
    {ItemId::IronPickaxe, {ToolKind::Pickaxe, kIronTier, 6.0f, 250}},
    {ItemId::IronAxe, {ToolKind::Axe, kIronTier, 6.0f, 250}},
    {ItemId::IronShovel, {ToolKind::Shovel, kIronTier, 6.0f, 250}},
    {ItemId::IronSword, {ToolKind::Sword, kIronTier, 2.5f, 250}},
    {ItemId::IronHoe, {ToolKind::Hoe, kIronTier, 2.5f, 250}},
    {ItemId::DiamondPickaxe, {ToolKind::Pickaxe, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondAxe, {ToolKind::Axe, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondShovel, {ToolKind::Shovel, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondSword, {ToolKind::Sword, kDiamondTier, 3.0f, 1561}},
    {ItemId::DiamondHoe, {ToolKind::Hoe, kDiamondTier, 3.0f, 1561}},
    {ItemId::EmberitePickaxe, {ToolKind::Pickaxe, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteAxe, {ToolKind::Axe, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteShovel, {ToolKind::Shovel, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteSword, {ToolKind::Sword, kEmberiteTier, 3.5f, 2031}},
    {ItemId::EmberiteHoe, {ToolKind::Hoe, kEmberiteTier, 3.5f, 2031}},
    // A bow wears like a tool and mines like nothing, which is exactly what
    // `ToolKind::None` with a durability says. It is deliberately outside
    // `isTool`: that predicate gates mining speed, the harvest tier and an
    // axe's stripping behaviour, none of which a bow should gain.
    {ItemId::Bow, {ToolKind::None, kHandTier, 1.0f, kBowDurability}},
    // Shears are deliberately no faster than a bare hand on a vine - the axe is
    // the quick tool there. What they are is the only thing that collects one.
    {ItemId::Shears, {ToolKind::Shears, kHandTier, 1.0f, 238}},
}};

} // namespace

ToolProperties toolFor(ItemId item) {
    for (const ToolRule& rule : kTools) {
        if (rule.item == item) {
            return rule.properties;
        }
    }
    return ToolProperties{};
}

float blockHardness(BlockId block) {
    // Neither fluid, fire nor air is something you mine; fire is put out by a
    // touch, which is the same zero from the breaking code's point of view.
    if (isFluid(block) || block == BlockId::Air || block == BlockId::Fire) {
        return 0.0f;
    }
    // ---- Redstone, and it has to be asked first. ----
    // A button and a pressure plate are cut shapes, so the forwarding below
    // would hand them a plank's two seconds where the reference gives them half
    // of one; and wire, a rail and a tripwire are all flat shapes, which the
    // rule further down answers with a flat zero.
    if (isRedstoneWire(block) || isRedstoneTorch(block) || isRepeater(block) ||
        isComparator(block) || isTripwireHook(block) || isTripwire(block)) {
        return 0.0f;
    }
    if (isButton(block) || isPressurePlate(block) || isLever(block) || isTarget(block)) {
        return 0.5f;
    }
    if (isRail(block)) {
        return 0.7f;
    }
    if (isNoteBlock(block)) {
        return 0.8f;
    }
    if (isDaylightDetector(block)) {
        return 0.2f;
    }
    if (isPiston(block) || isPistonHead(block)) {
        return 1.5f;
    }
    if (isObserver(block) || isLightningRod(block)) {
        return 3.0f;
    }
    if (isDispenserLike(block)) {
        return 3.5f;
    }
    // A stair, slab, wall, fence or gate mines exactly like the block it was cut
    // from. **Answered before anything else**, because the old test named the
    // two families that existed and returned a flat 1.5 - which is right for
    // cobblestone and wrong for oak, obsidian and everything else.
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return blockHardness(material);
        }
    }
    // The reference gives a charge no hardness at all, lit or not.
    if (block == BlockId::Tnt || block == BlockId::TntPrimed) {
        return 0.0f;
    }
    // Settled snow is asked **before** the flat-shape rule below, exactly as
    // `harvestTool` does and for the same reason: a layer is dug rather than
    // brushed aside, so it has a real hardness where a carpet has none. Below
    // that rule this branch was unreachable and every layer broke instantly.
    //
    // The reference's own value, and deliberately below the solid snow block's
    // 0.2.
    if (isSnowLayer(block)) {
        return 0.1f;
    }
    if (blockShape(block) == BlockShape::Cross || blockShape(block) == BlockShape::Flat) {
        // Plants and torches come away instantly, whatever you are holding.
        return 0.0f;
    }
    // The deepslate half of an ore is the stone half in harder rock - the
    // reference's 4.5 against 3.0. Deriving it is what stops the tool and tier
    // tables below needing a second copy of the ore list.
    if (isDeepslateOre(block)) {
        return blockHardness(stoneOreFor(block)) * 1.5f;
    }
    if (isLeafBlock(block)) {
        return 0.2f;
    }
    // A door is the reference's 3, and iron is 5. The leaf is thin but it is
    // still a whole door's worth of timber.
    if (isDoor(block) || isTrapdoor(block)) {
        const bool metal = isDoor(block)
                               ? kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal
                               : kTrapdoorFamilies[static_cast<std::size_t>(
                                     trapdoorFamily(block))].metal;
        return metal ? 5.0f : 3.0f;
    }
    // A bed comes apart in a moment - the reference's 0.2, and no tool helps.
    if (isBed(block)) {
        return 0.2f;
    }
    // The farm. Tilled ground and a trodden path are barely firmer than the dirt
    // they came from; a composter is the planks it is built out of; and both
    // pumpkins are the reference's 1.0.
    if (isFarmland(block)) {
        return 0.6f;
    }
    if (block == BlockId::DirtPath) {
        return 0.65f;
    }
    if (isComposter(block)) {
        return 0.6f;
    }
    if (isCarvedPumpkin(block) || isJackOLantern(block)) {
        return 1.0f;
    }
    // The fifth run. **Named rather than left to the default**, because the
    // default is 1.0 and half of this run is either rock or wood.
    if (block >= kFirstExtraBlock5 && block <= kLastExtraBlock5) {
        if (block >= BlockId::OakWood && block <= BlockId::StrippedWarpedHyphae) {
            return 2.0f;
        }
        if (isCoralBlock(block)) {
            return 1.5f;
        }
        if ((block >= BlockId::WaxedCopperBlock && block <= BlockId::WaxedOxidizedCopperGrate) ||
            isCopperBulb(block)) {
            return 3.0f;
        }
        switch (block) {
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock:
        case BlockId::MushroomStem:
        case BlockId::SculkVein:
            return 0.2f;
        case BlockId::CryingObsidian:
        case BlockId::RespawnAnchor:
            return 50.0f;
        case BlockId::PowderSnow:
            return 0.25f;
        case BlockId::SuspiciousSand:
        case BlockId::SuspiciousGravel:
            return 0.25f;
        case BlockId::RedstoneLamp:
        case BlockId::RedstoneLampLit:
            return 0.3f;
        case BlockId::Lodestone:
        case BlockId::BlastFurnace:
        case BlockId::Stonecutter:
            return 3.5f;
        case BlockId::EnchantingTable:
        case BlockId::Bell:
        case BlockId::Anvil:
        case BlockId::ChippedAnvil:
        case BlockId::DamagedAnvil:
            return 5.0f;
        case BlockId::ChiseledBookshelf:
            return 1.5f;
        case BlockId::CartographyTable:
        case BlockId::FletchingTable:
        case BlockId::Loom:
        case BlockId::Barrel:
        case BlockId::Lectern:
            return 2.5f;
        case BlockId::Grindstone:
        case BlockId::Cauldron:
        case BlockId::Campfire:
        case BlockId::SoulCampfire:
            return 2.0f;
        case BlockId::SculkSensor:
            return 1.5f;
        case BlockId::SculkShrieker:
            return 3.0f;
        case BlockId::MossCarpet:
            return 0.1f;
        default:
            return 1.0f;
        }
    }
    // The third run: glass and its panes are the reference's 0.3, iron bars and
    // the lanterns are metal, and a torch comes away in a touch.
    if (block == BlockId::IronBars) {
        return 5.0f;
    }
    if (block == BlockId::Lantern || block == BlockId::SoulLantern) {
        return 3.5f;
    }
    if (block == BlockId::EndRod || isLadder(block)) {
        return 0.4f;
    }
    if (isVine(block) || isCocoa(block)) {
        return 0.2f;
    }
    if (block >= BlockId::WhiteStainedGlass && block <= BlockId::BlackStainedGlass) {
        return 0.3f;
    }
    if (isLogBlock(block)) {
        return 2.0f;
    }
    if (isFurnace(block)) {
        return 3.5f;
    }
    if (isChest(block) || block == BlockId::SmithingTable) {
        return 2.5f;
    }
    if (isBeehive(block)) {
        return 0.6f;
    }
    switch (block) {
    // The second table run. Grouped by the reference's own values rather than
    // listed one per line.
    case BlockId::Netherrack:
        return 0.4f;    case BlockId::SoulSand:
    case BlockId::SoulSoil:
    case BlockId::Podzol:
    case BlockId::Mycelium:
        return 0.5f;
    case BlockId::SlimeBlock:
    case BlockId::DriedKelpBlock:
        return 0.1f;
    case BlockId::NetherWartBlock:
        return 1.0f;
    case BlockId::WarpedWartBlock:
    case BlockId::Shroomlight:
        return 1.0f;
    case BlockId::Cactus:
    case BlockId::Target:
        return 0.4f;
    case BlockId::SnowBlock:
        return 0.2f;
    case BlockId::OchreFroglight:
    case BlockId::VerdantFroglight:
    case BlockId::PearlescentFroglight:
        return 0.3f;
    case BlockId::CrimsonNylium:
    case BlockId::WarpedNylium:
        return 0.4f;
    case BlockId::SculkCatalyst:
        return 3.0f;
    case BlockId::Azalea:
    case BlockId::FloweringAzalea:
        return 0.0f;
    case BlockId::CrimsonStem:
    case BlockId::WarpedStem:
    case BlockId::MangroveLog:
    case BlockId::BambooBlock:
    case BlockId::CrimsonPlanks:
    case BlockId::WarpedPlanks:
    case BlockId::MangrovePlanks:
    case BlockId::BambooPlanks:
    case BlockId::BambooMosaic:
    case BlockId::MuddyMangroveRoots:
    case BlockId::BoneBlock:
        return 2.0f;
    case BlockId::QuartzPillar:
        return 0.8f;
    case BlockId::PurpurPillar:
        return 1.5f;
    case BlockId::WhiteGlazedTerracotta:
    case BlockId::OrangeGlazedTerracotta:
    case BlockId::MagentaGlazedTerracotta:
    case BlockId::LightBlueGlazedTerracotta:
    case BlockId::YellowGlazedTerracotta:
    case BlockId::LimeGlazedTerracotta:
    case BlockId::PinkGlazedTerracotta:
    case BlockId::GrayGlazedTerracotta:
    case BlockId::LightGrayGlazedTerracotta:
    case BlockId::CyanGlazedTerracotta:
    case BlockId::PurpleGlazedTerracotta:
    case BlockId::BlueGlazedTerracotta:
    case BlockId::BrownGlazedTerracotta:
    case BlockId::GreenGlazedTerracotta:
    case BlockId::RedGlazedTerracotta:
    case BlockId::BlackGlazedTerracotta:
        return 1.4f;
    case BlockId::Sculk:
        return 0.2f;
    case BlockId::BuddingAmethyst:
        return 1.5f;
    case BlockId::Blackstone:
    case BlockId::PolishedBlackstone:
    case BlockId::PolishedBlackstoneBricks:
    case BlockId::ChiseledPolishedBlackstone:
    case BlockId::CrackedPolishedBlackstoneBricks:
    case BlockId::GildedBlackstone:
    case BlockId::NetherBricks:
    case BlockId::RedNetherBricks:
    case BlockId::CrackedNetherBricks:
    case BlockId::ChiseledNetherBricks:
    case BlockId::EndStone:
    case BlockId::EndStoneBricks:
    case BlockId::PurpurBlock:
    case BlockId::PolishedTuff:
    case BlockId::TuffBricks:
    case BlockId::ChiseledTuff:
    case BlockId::PolishedBasalt:
    case BlockId::ChiseledDeepslate:
    case BlockId::CrackedDeepslateBricks:
    case BlockId::CrackedDeepslateTiles:
    case BlockId::SmoothRedSandstone:
        return 2.0f;
    case BlockId::NetherGoldOre:
    case BlockId::NetherQuartzOre:
        return 3.0f;
    case BlockId::QuartzBlock:
    case BlockId::SmoothQuartz:
    case BlockId::ChiseledQuartz:
    case BlockId::QuartzBricks:
        return 0.8f;
    case BlockId::RawIronBlock:
    case BlockId::RawGoldBlock:
    case BlockId::RawCopperBlock:
    case BlockId::ExposedCopper:
    case BlockId::WeatheredCopper:
    case BlockId::OxidizedCopper:
    case BlockId::CutCopper:
    case BlockId::ExposedCutCopper:
    case BlockId::WeatheredCutCopper:
    case BlockId::OxidizedCutCopper:
    case BlockId::ChiseledCopper:
        return 3.0f;
    // Only the wither can break it in the reference; ours settles for making it
    // the hardest thing in the world short of bedrock.
    case BlockId::ReinforcedDeepslate:
        return 55.0f;
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
        return 0.5f;
    case BlockId::Dirt:
    case BlockId::Grass:
        return 0.6f;
    case BlockId::Leaves:
        return 0.2f;
    case BlockId::Planks:
    case BlockId::CraftingTable:
        return 2.0f;
    case BlockId::Log:
        return 2.0f;
    case BlockId::Stone:
        return 1.5f;
    case BlockId::Cobblestone:
        return 2.0f;
    case BlockId::Bricks:
        return 2.0f;
    case BlockId::Glowstone:
        return 0.3f;
    case BlockId::Glass:
        return 0.3f;
    case BlockId::Clay:
        return 0.6f;
    case BlockId::Sandstone:
        return 0.8f;
    case BlockId::Bookshelf:
        return 1.5f;
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::StoneBricks:
        return 1.5f;
    case BlockId::SmoothStone:
    case BlockId::MossyCobblestone:
        return 2.0f;
    case BlockId::Obsidian:
        // Deliberately punishing. At a stone pickaxe's speed this is about
        // nineteen seconds, which is the point of the block.
        return 50.0f;
    case BlockId::PackedIce:
        return 0.5f;
    case BlockId::Prismarine:
        return 1.5f;
    case BlockId::SeaLantern:
        // Glass-like: quick to break and it takes no tool to do it.
        return 0.3f;
    case BlockId::CoarseDirt:
        return 0.5f;
    // The appended run, by family. Ranges rather than forty-eight cases: the
    // enum is grouped for exactly this.
    case BlockId::Ice:
    case BlockId::BlueIce:
        return 0.5f;
    case BlockId::IronBlock:
    case BlockId::GoldBlock:
    case BlockId::DiamondBlock:
    case BlockId::EmeraldBlock:
    case BlockId::LapisBlock:
    case BlockId::CopperBlock:
        return 3.0f;
    case BlockId::CoalBlock:
    case BlockId::RedstoneBlock:
        return 5.0f;
    case BlockId::Sponge:
    case BlockId::WetSponge:
        return 0.6f;
    case BlockId::Terracotta:
        return 1.25f;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Deepslate:
        return 3.0f;
    case BlockId::AncientDebris:
        // The reference's 30, which is twenty times stone and the reason it is
        // worth blasting for rather than digging out.
        return 30.0f;
    case BlockId::EmberiteBlock:
        return 50.0f;
    // Appended 2026-08-07. Wool and the soft blocks come away by hand; the
    // coloured stone families sit where their plain forms do.
    case BlockId::HoneyBlock:
    case BlockId::MossBlock:
        return 0.1f;
    case BlockId::Mud:
    case BlockId::RootedDirt:
    case BlockId::MagmaBlock:
    case BlockId::HayBlock:
        return 0.5f;
    case BlockId::HoneycombBlock:
        return 0.6f;
    case BlockId::Calcite:
    case BlockId::RedSandstone:
    case BlockId::CutRedSandstone:
    case BlockId::ChiseledRedSandstone:
    case BlockId::NoteBlock:
        return 0.8f;
    case BlockId::PackedMud:
    case BlockId::Pumpkin:
    case BlockId::Melon:
        return 1.0f;
    case BlockId::SmoothBasalt:
    case BlockId::Basalt:
        return 1.25f;
    case BlockId::Tuff:
    case BlockId::DripstoneBlock:
    case BlockId::MudBricks:
    case BlockId::AmethystBlock:
        return 1.5f;
    case BlockId::Jukebox:
        return 2.0f;
    case BlockId::Cobweb:
        return 4.0f;
    case BlockId::Bedrock:
        // The reference's own value for "never". Nothing here treats a block as
        // unbreakable, so an absurd hardness is what enforces it, and creative
        // still ignores it - which is correct, it is a builder's tool.
        return 3600.0f;
    default:
        return 1.0f;
    }
}

ToolKind harvestTool(BlockId block) {
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }
    // The machines, asked before the flat-shape rule below hands a rail "no
    // tool" and before the forwarding hands a stone button a pickaxe it does
    // not need. A lever, a button and a plate keep their material's answer.
    if (isPiston(block) || isPistonHead(block) || isObserver(block) || isDispenserLike(block) ||
        isLightningRod(block) || isRail(block)) {
        return ToolKind::Pickaxe;
    }
    if (isDaylightDetector(block) || isNoteBlock(block)) {
        return ToolKind::Axe;
    }
    if (isRedstoneWire(block) || isRedstoneTorch(block) || isRepeater(block) ||
        isComparator(block) || isTripwireHook(block) || isTripwire(block) || isLever(block)) {
        return ToolKind::None;
    }
    // Same forwarding as the hardness: an oak fence wants an axe and a
    // blackstone wall wants a pickaxe, and neither needs a row of its own.
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return harvestTool(material);
        }
    }
    // Settled snow is asked **before** the flat-shape rule below, which answers
    // "no tool at all" for carpets and lily pads. Snow is the one flat block
    // that is dug rather than picked up, and a shovel is what digs it.
    if (isSnowLayer(block)) {
        return ToolKind::Shovel;
    }
    // A plant needs no tool, whatever run its id happens to sit in. Without
    // this the second run's pickaxe default reached every plant in it, and
    // seventeen of them broke instantly and dropped nothing.
    if (blockShape(block) == BlockShape::Cross || blockShape(block) == BlockShape::Flat) {
        return ToolKind::None;
    }
    if (isLogBlock(block) || isBeehive(block)) {
        return ToolKind::Axe;
    }
    // A smoker is a wooden block; a furnace is stone. `isFurnace` answers true
    // for both families, so asking it here gave the smoker the furnace's
    // pickaxe - which is the shape of bug widening a family predicate always
    // has. Its hardness and blast resistance were both already right.
    if (isSmoker(block)) {
        return ToolKind::Axe;
    }
    if (isFurnace(block)) {
        return ToolKind::Pickaxe;
    }
    if (isChest(block)) {
        return ToolKind::Axe;
    }
    if (isDoor(block) || isTrapdoor(block)) {
        const bool metal = isDoor(block)
                               ? kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal
                               : kTrapdoorFamilies[static_cast<std::size_t>(
                                     trapdoorFamily(block))].metal;
        return metal ? ToolKind::Pickaxe : ToolKind::Axe;
    }
    // Tilled ground and a path are dug; a composter is a wooden tub; a pumpkin
    // is cut. None of them withholds its drop from bare hands.
    if (isFarmland(block) || block == BlockId::DirtPath) {
        return ToolKind::Shovel;
    }
    if (isComposter(block) || isCarvedPumpkin(block) || isJackOLantern(block)) {
        return ToolKind::Axe;
    }
    // The fifth run, on the same rule the rest of the game uses: rock wants a
    // pickaxe, timber wants an axe, and the sculk family wants a hoe.
    if (block >= kFirstExtraBlock5 && block <= kLastExtraBlock5) {
        if (block >= BlockId::OakWood && block <= BlockId::StrippedWarpedHyphae) {
            return ToolKind::Axe;
        }
        if (isCoralBlock(block)) {
            return ToolKind::Pickaxe;
        }
        if ((block >= BlockId::WaxedCopperBlock && block <= BlockId::WaxedOxidizedCopperGrate) ||
            isCopperBulb(block)) {
            return ToolKind::Pickaxe;
        }
        switch (block) {
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock:
        case BlockId::MushroomStem:
        case BlockId::ChiseledBookshelf:
        case BlockId::CartographyTable:
        case BlockId::FletchingTable:
        case BlockId::Loom:
        case BlockId::Barrel:
        case BlockId::Lectern:
        case BlockId::Campfire:
        case BlockId::SoulCampfire:
            return ToolKind::Axe;
        case BlockId::PowderSnow:
        case BlockId::SuspiciousSand:
        case BlockId::SuspiciousGravel:
            return ToolKind::Shovel;
        case BlockId::SculkVein:
        case BlockId::SculkSensor:
        case BlockId::SculkShrieker:
        case BlockId::MossCarpet:
            return ToolKind::Hoe;
        case BlockId::CryingObsidian:
        case BlockId::RespawnAnchor:
        case BlockId::Lodestone:
        case BlockId::BlastFurnace:
        case BlockId::Stonecutter:
        case BlockId::EnchantingTable:
        case BlockId::Bell:
        case BlockId::Anvil:
        case BlockId::ChippedAnvil:
        case BlockId::DamagedAnvil:
        case BlockId::Grindstone:
        case BlockId::Cauldron:
        case BlockId::RedstoneLamp:
        case BlockId::RedstoneLampLit:
            return ToolKind::Pickaxe;
        default:
            return ToolKind::None;
        }
    }
    // The sixth run: glass wants nothing, the rest want a pickaxe.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        return block == BlockId::TintedGlass ? ToolKind::None : ToolKind::Pickaxe;
    }
    // The third run. Glass wants nothing, metal wants a pickaxe, and a ladder
    // is wood.
    if (block == BlockId::IronBars || block == BlockId::Lantern ||
        block == BlockId::SoulLantern) {
        return ToolKind::Pickaxe;
    }
    if (isLadder(block)) {
        return ToolKind::Axe;
    }
    if (isVine(block)) {
        return ToolKind::Shears;
    }
    if (isCocoa(block)) {
        return ToolKind::Axe;
    }
    // The second table run is almost entirely rock. Naming the handful that is
    // not, and letting the rest fall through to the pickaxe, is shorter than
    // fifty case labels and cannot go stale when the run grows.
    if (block >= kFirstExtraBlock2 && block <= kLastExtraBlock2) {
        switch (block) {
        case BlockId::SoulSand:
        case BlockId::SoulSoil:
        case BlockId::Podzol:
        case BlockId::Mycelium:
        case BlockId::SnowBlock:
            return ToolKind::Shovel;
        case BlockId::SlimeBlock:
        case BlockId::DriedKelpBlock:
        case BlockId::NetherWartBlock:
        case BlockId::WarpedWartBlock:
        case BlockId::Sculk:
        case BlockId::SculkCatalyst:
        case BlockId::Shroomlight:
        case BlockId::Cactus:
        case BlockId::Azalea:
        case BlockId::FloweringAzalea:
        case BlockId::Target:
            return ToolKind::None;
        case BlockId::CrimsonPlanks:
        case BlockId::WarpedPlanks:
        case BlockId::MangrovePlanks:
        case BlockId::MangroveLog:
        case BlockId::BambooPlanks:
        case BlockId::BambooMosaic:
        case BlockId::BambooBlock:
        case BlockId::CrimsonStem:
        case BlockId::WarpedStem:
        case BlockId::MuddyMangroveRoots:
            return ToolKind::Axe;
        default:
            break;
        }
        return isConcretePowder(block) ? ToolKind::Shovel : ToolKind::Pickaxe;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Bricks:
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::SmoothStone:
    case BlockId::StoneBricks:
    case BlockId::MossyCobblestone:
    case BlockId::Obsidian:
    case BlockId::Sandstone:
        return ToolKind::Pickaxe;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Deepslate:
    case BlockId::Terracotta:
    case BlockId::PackedIce:
    case BlockId::AncientDebris:
    case BlockId::EmberiteBlock:
    case BlockId::Prismarine:
    case BlockId::SeaLantern:
    case BlockId::CobbledDeepslate:
    case BlockId::Ice:
    case BlockId::BlueIce:
    case BlockId::CoalBlock:
    case BlockId::IronBlock:
    case BlockId::GoldBlock:
    case BlockId::DiamondBlock:
    case BlockId::EmeraldBlock:
    case BlockId::LapisBlock:
    case BlockId::RedstoneBlock:
    case BlockId::CopperBlock:
    case BlockId::PolishedAndesite:
    case BlockId::PolishedDiorite:
    case BlockId::PolishedGranite:
    case BlockId::ChiseledStoneBricks:
    case BlockId::MossyStoneBricks:
    case BlockId::CrackedStoneBricks:
    case BlockId::PolishedDeepslate:
    case BlockId::DeepslateBricks:
    case BlockId::DeepslateTiles:
    case BlockId::SmoothSandstone:
    case BlockId::CutSandstone:
    case BlockId::ChiseledSandstone:
    case BlockId::TubeCoralBlock:
    case BlockId::BrainCoralBlock:
    case BlockId::BubbleCoralBlock:
    case BlockId::FireCoralBlock:
    case BlockId::HornCoralBlock:
    case BlockId::DarkPrismarine:
    case BlockId::PrismarineBricks:
        return ToolKind::Pickaxe;
    case BlockId::Log:
    case BlockId::Planks:
    case BlockId::CraftingTable:
    case BlockId::SmithingTable:
    case BlockId::Bookshelf:
    case BlockId::SpruceLog:
    case BlockId::SprucePlanks:
    case BlockId::BirchLog:
    case BlockId::BirchPlanks:
        return ToolKind::Axe;
    case BlockId::Dirt:
    case BlockId::Grass:
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
    case BlockId::Clay:
    case BlockId::CoarseDirt:
        return ToolKind::Shovel;
    default:
        return ToolKind::None;
    }
}

int harvestTier(BlockId block) {
    // Only stone-family blocks withhold their drop, which is what makes the
    // first pickaxe the thing that opens the game up. Wood and soil always give
    // something, or a fresh world would be unplayable.
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }
    // Every redstone machine is iron-age work and needs nothing better than the
    // first pickaxe; everything else in the family comes away by hand.
    //
    // **A button and a plate are deliberately left out** so they fall through to
    // the forwarding below and keep their material's answer - the reference does
    // gate a stone pressure plate behind a pickaxe.
    if (isPiston(block) || isPistonHead(block) || isObserver(block) || isDispenserLike(block) ||
        isLightningRod(block) || isRail(block)) {
        return kWoodTier;
    }
    if (isRedstoneComponent(block) && !isButton(block) && !isPressurePlate(block)) {
        return kHandTier;
    }
    if (isNoteBlock(block)) {
        return kHandTier;
    }
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return harvestTier(material);
        }
    }
    // A smoker is wood and a furnace is stone, so they part company here as
    // well as in `harvestTool`: the reference gates a furnace behind a pickaxe
    // and lets a smoker be broken by hand. `isFurnace` answers for both, so
    // asking it alone made a smoker drop nothing bare-handed.
    if (isSmoker(block)) {
        return kHandTier;
    }
    if (isFurnace(block)) {
        return kWoodTier;
    }
    // The second table run. Only the ores and copper family gate above wood.
    if (block >= kFirstExtraBlock2 && block <= kLastExtraBlock2) {
        switch (block) {
        case BlockId::NetherGoldOre:
        case BlockId::NetherQuartzOre:
            return kWoodTier;
        case BlockId::RawIronBlock:
        case BlockId::RawCopperBlock:
        case BlockId::ExposedCopper:
        case BlockId::WeatheredCopper:
        case BlockId::OxidizedCopper:
        case BlockId::CutCopper:
        case BlockId::ExposedCutCopper:
        case BlockId::WeatheredCutCopper:
        case BlockId::OxidizedCutCopper:
        case BlockId::ChiseledCopper:
            return kStoneTier;
        case BlockId::RawGoldBlock:
        case BlockId::ReinforcedDeepslate:
            return kIronTier;
        default:
            break;
        }
        // Only rock withholds its drop, which is the rule stated at the top of
        // this function. A blanket wood tier here meant every plank, wool,
        // plant and soil block in the run - about two hundred of them - was
        // destroyed rather than collected when broken by hand.
        return harvestTool(block) == ToolKind::Pickaxe ? kWoodTier : kHandTier;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Bricks:
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::SmoothStone:
    case BlockId::StoneBricks:
    case BlockId::MossyCobblestone:
    case BlockId::Sandstone:
        return kWoodTier;
    case BlockId::CoalOre:
    case BlockId::Deepslate:
    case BlockId::Terracotta:
        return kWoodTier;
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::LapisOre:
        return kStoneTier;
    // **The reference's own gating, which we could not honour until now.**
    // These four demand an iron pickaxe there, and sat at stone here purely
    // because stone was the highest tier that existed - a named divergence that
    // this milestone closes rather than a balance decision.
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
        return kIronTier;
    case BlockId::Obsidian:
    case BlockId::AncientDebris:
    case BlockId::EmberiteBlock:
        return kDiamondTier;
    default:
        return kHandTier;
    }
}

float breakSeconds(BlockId block, ItemId item) {
    const float hardness = blockHardness(block);
    if (hardness <= 0.0f) {
        return 0.0f;
    }

    const ToolProperties tool = toolFor(item);
    const ToolKind wanted = harvestTool(block);
    // The right tool speeds it up; the wrong one is no worse than bare hands.
    const float speed = (wanted != ToolKind::None && tool.kind == wanted) ? tool.speed : 1.0f;

    // The reference's own pair, and the hardness table above is already its:
    // 1.5x with the kit a block demands and 5x without. The ratio is what makes
    // a tool a decision rather than a convenience - a block still comes away
    // bare-handed, it just takes long enough to be worth avoiding.
    const float penalty = tool.tier >= harvestTier(block) ? 1.5f : 5.0f;
    return hardness * penalty / std::max(0.1f, speed);
}

bool yieldsDrop(BlockId block, ItemId item) {
    const ToolProperties tool = toolFor(item);
    // **Some blocks are not gated on how good your tool is but on what kind it
    // is.** A vine comes away from anything and is collected by nothing but
    // shears, which no tier comparison can express - the reference's Silk Touch
    // does not do it either.
    if (harvestTool(block) == ToolKind::Shears) {
        return tool.kind == ToolKind::Shears;
    }
    return tool.tier >= harvestTier(block);
}

} // namespace game
