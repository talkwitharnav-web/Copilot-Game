#pragma once

#include "world/Block.hpp"
#include "world/Collision.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace game {

class World;

namespace blast {

/// The reference's "nothing can ever remove this" figure: 3,600,000, shared by
/// bedrock, the end portal frame, the command blocks and the barrier.
///
/// **Named because it was written as 3,600 for twenty milestones** - a dropped
/// thousand that no test could see, because the crater march stops every ray
/// at about 16.5 and both numbers are far past that. It is the one value in
/// this file a reader checks against the wiki, and 3,600 is published for
/// nothing at all.
constexpr float kUnblastable = 3600000.0f;

/// The five cuts whose **slab** parts company with the block it was cut from.
///
/// The reference pins the legacy stone-slab group at 6 whatever its material
/// is, so `['sandstone']='0.8'`, `['sandstone stairs']='0.8'` and
/// `['sandstone wall']='0.8'` sit beside `['sandstone slab']='6'`. Same split
/// for red sandstone, both cut forms and the block of quartz. The smooth cuts
/// are deliberately absent: they are 6 already, so their slabs inherit it.
constexpr bool isLegacyStoneSlabMaterial(BlockId material) {
    return material == BlockId::Sandstone || material == BlockId::CutSandstone ||
           material == BlockId::RedSandstone || material == BlockId::CutRedSandstone ||
           material == BlockId::QuartzBlock;
}

} // namespace blast

/// How well a block resists a blast.
///
/// **This is not mining hardness and must never be confused with it** — stone
/// is 1.5 to a pickaxe and 6 to an explosion, and obsidian is 50 against 1200.
/// `Tool.hpp` owns the mining number; this owns the blast one, and neither is
/// derived from the other.
///
/// **A table with an ordering problem, not a list of numbers.** Every rule is
/// either a *special* — one block or one family carrying its own published
/// figure — or a *family rule* answering for everything left. Specials sit
/// above the families that would otherwise answer for them, and every hoist
/// below says which general rule it is protecting from. Getting that order
/// wrong is what this function was: an audit against
/// `Module:Blast_resistance_values` found about a hundred and twenty blocks
/// reaching the rock rule at the bottom while having a published number of
/// their own, plus two per-run `default:`s handing out a plausible 1.0.
///
/// **There is exactly one value-returning catch-all left, and it is the last
/// line.** Every per-run switch `break`s instead, the way run two already did,
/// so a block nobody named falls to the family rules rather than to a run's
/// private guess. That is the single most important property of this file and
/// the sweep at the bottom is what holds it.
///
/// Every number is `Module:Blast_resistance_values` on minecraft.wiki, which
/// publishes one figure per block shared by both editions.
constexpr float blastResistance(BlockId block) {
    if (isFluid(block)) {
        // 100, and it is the whole reason a blast underwater breaks nothing:
        // a single 0.3 step costs 30, which no creeper ray can pay. Lava is the
        // same figure in the reference.
        return 100.0f;
    }
    // **A normalisation, not an answer**, and it goes first for the same reason
    // `Mining.hpp` and `Item.hpp` do it: the deepslate half of an ore is the
    // stone half's number in every table the reference publishes. Without it
    // all eight fell past every branch to the rock rule and a blast could not
    // open a deepslate-layer vein it clears one level higher.
    if (isDeepslateOre(block)) {
        return blastResistance(stoneOreFor(block));
    }

    // ---- The three cut shapes that are NOT their parent, above the forward. ----
    // A carpet's parent is its wool and a sign's is its planks, so the
    // `shapedParent` forwarding below answers for all three unless they are
    // asked here first.

    // 0.1 against wool's 0.8, and the same figure as moss carpet. The Cross/Flat
    // rule further down already promised this in its comment - "a plant, a torch
    // or a carpet offers a blast nothing at all" - and could never deliver it,
    // because the forward fired first.
    if (isCarpet(block)) {
        return 0.1f;
    }
    // Sign, hanging sign and banner are all 1 in the reference. Signs took
    // planks' 3, banners took wool's 0.8, and the eleven woods therefore
    // disagreed with each other about a plank of the same tree.
    if (isSignLike(block)) {
        return 1.0f;
    }
    if (isSlab(block) && blast::isLegacyStoneSlabMaterial(shapedParent(block))) {
        return 6.0f;
    }

    // ---- Redstone, asked before the cut-shape forwarding. ----
    // A button and a plate would otherwise inherit a plank's three, and every
    // one of these is fragile in the reference. Without the branch they all
    // reach the stone default at the bottom, which is the exact shape of bug
    // that once left two whole table runs proof against any explosion.
    if (isRedstoneWire(block) || isRedstoneTorch(block) || isRepeater(block) ||
        isComparator(block) || isTripwireHook(block) || isTripwire(block)) {
        return 0.0f;
    }
    if (isDaylightDetector(block)) {
        return 0.2f;
    }
    if (isButton(block) || isPressurePlate(block) || isLever(block) || isTarget(block)) {
        return 0.5f;
    }
    // **Out of the 0.5 rule above.** A piston is 1.5 in the reference; the 0.5
    // in that module belongs to `['six-sided piston']`, a Bedrock Education
    // block we do not have, and sharing a branch with the buttons made every
    // piston three times easier to blow up than published. 36 ids.
    if (isPiston(block) || isPistonHead(block)) {
        return 1.5f;
    }
    if (isRail(block)) {
        return 0.7f;
    }
    if (isNoteBlock(block)) {
        return 0.8f;
    }
    if (isObserver(block)) {
        return 3.0f;
    }
    // **Out of the observer's branch.** The two agree on hardness 3 and that is
    // what hid it: a lightning rod is copper, and every copper block in the
    // reference is 6.
    if (isLightningRod(block)) {
        return 6.0f;
    }
    if (isDispenserLike(block)) {
        return 3.5f;
    }
    // A cut shape resists exactly as the block it came from does, so an obsidian
    // stair is as blast-proof as obsidian and an oak fence is not.
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return blastResistance(material);
        }
    }
    // Every facing and both lit states share one resistance, so the family is
    // answered once rather than as eight cases.
    if (isFurnace(block)) {
        return 3.5f;
    }
    // **Both above `isChest`, which answers for them.** The reference gives an
    // ender chest 600 and a stowbox 2, against a wooden chest's 2.5 - so an
    // ender chest was as easy to blow open as a barrel, and the one container
    // whose whole point is that its contents survive anything was the one a
    // creeper could empty. Same shape as `isSmoker` having to be asked before
    // `isFurnace`: widening a family predicate kills the answers behind it.
    if (isEnderChest(block)) {
        return 600.0f;
    }
    if (isStowbox(block)) {
        return 2.0f;
    }
    // Wooden, so the stone default at the bottom would be badly wrong.
    if (isChest(block)) {
        return 2.5f;
    }
    // 4.8, its own tier and nobody else's. Named nowhere before, so all five
    // facings took the rock rule and a creeper hole left the sorting system
    // standing. `Mining.hpp` had the identical gap and closed it the same way.
    if (isHopper(block)) {
        return 4.8f;
    }
    // **All seven fill levels, not just the empty one.** The reference splits
    // nothing by level; only `BlockId::Cauldron` was named, at the chest's 2.5,
    // and the six filled ids in the later run fell to the rock rule - so
    // filling a cauldron made it nearly three times harder to blast.
    if (isCauldron(block)) {
        return 2.0f;
    }
    // Snow, which the stone default at the bottom would make blast-proof. The
    // solid cube is 0.2 and lives in the named switch below; this is the layer.
    if (isSnowLayer(block)) {
        return 0.1f;
    }
    if (isDoor(block) || isTrapdoor(block)) {
        const bool metal = isDoor(block)
                               ? kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal
                               : kTrapdoorFamilies[static_cast<std::size_t>(
                                     trapdoorFamily(block))].metal;
        return metal ? 5.0f : 3.0f;
    }
    if (isBed(block)) {
        return 0.2f;
    }
    if (isBeehive(block)) {
        return 0.6f;
    }
    // **Three families the shape rules cannot see.** A ladder, a vine and a
    // cocoa pod are each their own `BlockShape`, so the Cross/Flat rule further
    // down never covered them: the ladder took the rock rule and stood in
    // mid-air after a blast that erased the wall behind it, vines took the wood
    // rule's 2.0 and cocoa took the rock rule's 6.0.
    if (isLadder(block)) {
        return 0.4f;
    }
    if (isVine(block)) {
        return 0.2f;
    }
    if (isCocoa(block)) {
        return 3.0f;
    }

    // ---- The materials, above every table run. ----
    // Each of these used to sit *below* the run branches, where a run's private
    // `default:` answered first: that is how azalea leaves came to be five times
    // tougher than the other eight leaves, and how ten of the eleven plank
    // species came to be a log's 2 instead of a plank's 3.

    // 3, not the 2 a log is. The two numbers deliberately differ, and only oak
    // was named - so an identical spruce house was a third easier to blow open,
    // and every stair, slab, fence and gate cut from one inherited it.
    if (isPlanksBlock(block)) {
        return 3.0f;
    }
    if (isLeafBlock(block)) {
        return 0.2f;
    }
    if (isWoolBlock(block)) {
        return 0.8f;
    }
    if (isPane(block) || isGlassBlock(block)) {
        return 0.3f;
    }
    // **Named as the woods they are rather than as `isFlammable`.** That
    // predicate was standing in for "is timber" and is not: it also holds for
    // the coal block (6), the hay bale (0.5), vines (0.2), the bookshelf, the
    // crafting table and dried kelp, every one of which was handed a log's 2.
    if (isLogBlock(block) || isBarkBlock(block) || isStrippedBarkBlock(block)) {
        return 2.0f;
    }

    // ---- The thin shapes: the specials, then the blanket. ----
    // Candles are 0.1 and are asked before the blanket for the same reason
    // carpets are asked before the forwarding.
    if (isCandle(block)) {
        return 0.1f;
    }
    // **Eleven members of the Cross/Flat family have a real number**, and the
    // blanket below covers 302 ids. Every one of these was 0 - destroyed by any
    // blast at any range - and three of them had `case` labels further down
    // that could never be reached, which is worse than the wrong value because
    // the file read as correct.
    switch (block) {
    case BlockId::Cobweb:
        // 4, the largest error in the group: a cobweb is deliberately
        // blast-absorbing and one creeper cleared a whole mineshaft room.
        // `Mining.hpp` hoists it above its own identical Cross/Flat rule twice.
        return 4.0f;
    case BlockId::Conduit:
        // Its `case` in run six was dead code.
        return 3.0f;
    case BlockId::PointedDripstone:
        return 3.0f;
    case BlockId::AmethystCluster:
    case BlockId::SmallAmethystBud:
    case BlockId::MediumAmethystBud:
    case BlockId::LargeAmethystBud:
        return 1.5f;
    case BlockId::Bamboo:
        // The one plant here that is not free to destroy. Our mining table
        // already knew, with a named branch above the same shape rule. It draws
        // as a model now rather than a cross, which is why both branches sit
        // above their file's shape rule instead of relying on it.
        return 1.0f;
    case BlockId::ChorusPlant:
    case BlockId::ChorusFlower:
        return 0.4f;
    case BlockId::GlowLichen:
    case BlockId::SculkVein:
        // Both `case` labels in run five were dead code.
        return 0.2f;
    case BlockId::BigDripleaf:
        return 0.1f;
    case BlockId::MossCarpet:
        // 0.1, the moss block's figure, not the 0.2 the dead run-five label
        // claimed - and `Mining.hpp` already asserts 0.1 for its hardness.
        return 0.1f;
    case BlockId::SeaPickle:
        // **The twelfth, and it is here for a different reason than the other
        // eleven**: its 0 is the same answer the blanket below already gave it,
        // but the blanket stopped reaching it when it gained the reference's
        // model and left `BlockShape::Cross` behind. Without this label it
        // falls to run six's catch-all of 6.0 - blast-proof, which is the exact
        // fault the blanket was written to fix.
        return 0.0f;
    default:
        break;
    }
    // **Above the run tests below, not after them.** A plant, a torch or a
    // carpet offers a blast nothing at all, and each table run has its own
    // catch-all that would otherwise answer 6.0 for the ones nobody named -
    // which is what made bamboo, sweet berries, glow lichen, sea pickles and
    // the four nether plants blast-proof.
    if (blockShape(block) == BlockShape::Cross || blockShape(block) == BlockShape::Flat) {
        return 0.0f;
    }
    // The farm, above the run tests for the same reason: the fourth run has no
    // catch-all of its own, and soil is not rock.
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
    // The fifth run. **Named exceptions only** - its `default:` used to return
    // 1.0, which is a real, plausible number and therefore invisible: it is
    // what made azalea leaves 1.0 while every other leaf was 0.2, and what gave
    // the brewing stand, the scaffolding and the flower pot a value nobody
    // chose. It now falls through to the family rules, exactly as run two does.
    if (block >= kFirstExtraBlock5 && block <= kLastExtraBlock5) {
        switch (block) {
        case BlockId::CryingObsidian:
        case BlockId::RespawnAnchor:
        case BlockId::EnchantingTable:
        case BlockId::Anvil:
        case BlockId::ChippedAnvil:
        case BlockId::DamagedAnvil:
            return 1200.0f;
        // **The grindstone is deliberately not here.** The reference gives it
        // 6, the same as stone, so it falls through to the rock rule at the
        // bottom; it shared this label at 5 because a bell was next to it.
        case BlockId::Bell:
            return 5.0f;
        // `BlastFurnace` is not listed: `isFurnace` answers 3.5 for it long
        // before this run is reached, and a second label agreeing with it is a
        // second place to edit.
        case BlockId::Lodestone:
        case BlockId::Stonecutter:
            return 3.5f;
        case BlockId::SculkShrieker:
            return 3.0f;
        // `Barrel` is not listed for the same reason as the blast furnace:
        // `isChest` answers 2.5 for it above.
        case BlockId::CartographyTable:
        case BlockId::FletchingTable:
        case BlockId::Loom:
        case BlockId::Lectern:
            return 2.5f;
        // 2, not the 2.5 they were folded in with above - a campfire is a
        // log's worth of wood, not a chest's.
        case BlockId::Campfire:
        case BlockId::SoulCampfire:
            return 2.0f;
        case BlockId::ChiseledBookshelf:
            return 1.5f;
        // Out of the shrieker's 3.0: the reference gives the sensor 1.5 and
        // the shrieker 3, and `Mining.hpp` already uses 1.5 for its hardness.
        case BlockId::SculkSensor:
            return 1.5f;
        case BlockId::BrewingStand:
            return 0.5f;
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock:
        case BlockId::MushroomStem:
            return 0.2f;
        case BlockId::RedstoneLamp:
        case BlockId::RedstoneLampLit:
            return 0.3f;
        // 0.25 is a published tier of its own; 0.3 was a rounded transcription
        // and `Mining.hpp` already carries 0.25 for all three hardnesses.
        case BlockId::PowderSnow:
        case BlockId::SuspiciousSand:
        case BlockId::SuspiciousGravel:
            return 0.25f;
        // Both were the run's 1.0. Bedrock cut scaffolding from 0.9 to 0 in
        // 1.20.30 and a flower pot has never been anything but 0.
        case BlockId::Scaffolding:
        case BlockId::FlowerPot:
            return 0.0f;
        default:
            break;
        }
        // **Falls through to the family rules**, which is how the azalea leaves
        // above reach `isLeafBlock` and the coral blocks, waxed copper and
        // copper bulbs reach the rock rule that is genuinely theirs.
    }
    // The sixth run. Named exceptions only, for the reason run five gives.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        switch (block) {
        case BlockId::EndPortalFrame:
            return blast::kUnblastable;
        case BlockId::DragonEgg:
            return 9.0f;
        case BlockId::Beacon:
            return 3.0f;
        case BlockId::MonsterSpawner:
            return 5.0f;
        default:
            break;
        }
    }
    // The second table run. Almost all of it is rock at 6; the exceptions are
    // named and the rest falls through, which is what stops this needing an
    // edit every time the run grows.
    if (block >= kFirstExtraBlock2 && block <= kLastExtraBlock2) {
        switch (block) {
        case BlockId::Netherrack:
            return 0.4f;
        case BlockId::SoulSand:
        case BlockId::SoulSoil:
        case BlockId::Podzol:
            return 0.5f;
        case BlockId::NetherWartBlock:
        case BlockId::WarpedWartBlock:
        case BlockId::Shroomlight:
            return 1.0f;
        case BlockId::Cactus:
        case BlockId::CrimsonNylium:
        case BlockId::WarpedNylium:
            return 0.4f;
        // 0.3, out of the 0.4 they shared with the cactus and the nyliums;
        // `Mining.hpp` already uses 0.3 for all three hardnesses.
        case BlockId::OchreFroglight:
        case BlockId::VerdantFroglight:
        case BlockId::PearlescentFroglight:
            return 0.3f;
        // **`SmoothQuartz` is deliberately out of this group**: the reference
        // gives the smooth cut 6 while the other four are 0.8, the same split
        // it already has for hardness. It falls to the rock rule below, and so
        // do its slab and stairs through the forwarding.
        case BlockId::QuartzBlock:
        case BlockId::ChiseledQuartz:
        case BlockId::QuartzBricks:
        case BlockId::QuartzPillar:
            return 0.8f;
        case BlockId::EndStone:
        case BlockId::EndStoneBricks:
            return 9.0f;
        // The logs and stems of this run are answered by the wood rule above;
        // what is left here is the two that only look like timber. **The purpur
        // pillar is not here** - the reference gives it 6, the same as the
        // purpur block it is stacked between, and it took the rock rule below.
        case BlockId::BoneBlock:
            return 2.0f;
        // 2.5, and it is the one "block of dried plant" the reference prices
        // like a chest rather than like a leaf.
        case BlockId::DriedKelpBlock:
            return 2.5f;
        case BlockId::SculkCatalyst:
            return 3.0f;
        // 3, the same as their stone counterparts.
        case BlockId::NetherGoldOre:
        case BlockId::NetherQuartzOre:
            return 3.0f;
        case BlockId::BuddingAmethyst:
            return 1.5f;
        // 0.2, matching the sculk vein above - `Mining.hpp` already uses 0.2.
        case BlockId::Sculk:
            return 0.2f;
        // 0.6, grass block's figure rather than dirt's 0.5.
        case BlockId::Mycelium:
            return 0.6f;
        // 0.7, the mangrove roots' figure. It is mud, not timber, and it was
        // sharing the logs' 2.0.
        case BlockId::MuddyMangroveRoots:
            return 0.7f;
        // The solid snow cube, 0.2. `BlockId::Snow` below is the same block
        // under an older id and carries the same number; 0.1 is the *layer*,
        // which `isSnowLayer` answers far above.
        case BlockId::SnowBlock:
            return 0.2f;
        // All three are 0 in the reference: a slime block and an azalea bush
        // offer a blast nothing.
        case BlockId::SlimeBlock:
        case BlockId::Azalea:
        case BlockId::FloweringAzalea:
            return 0.0f;
        // The reference makes it blast-proof so a wither cannot open a vault.
        case BlockId::ReinforcedDeepslate:
            return 1200.0f;
        default:
            break;
        }
        if (isConcretePowder(block)) {
            return 0.5f;
        }
        // **Falls through to the family rules at the bottom rather than
        // answering 6.0 here.** A second copy of the rock default is a second
        // place for a leaf block or a stripped log to be quietly declared
        // blast-proof, which is exactly what happened to six of them.
    }
    switch (block) {
    case BlockId::Air:
        return 0.0f;
    // A charge offers no resistance at all, which is what lets one blast set
    // off a whole stack. **`Fire` is deliberately absent**: it is Cross-shaped,
    // so the blanket above already answers 0 for it and a `case` label here
    // would be dead code.
    case BlockId::Tnt:
    case BlockId::TntPrimed:
        return 0.0f;
    // A torch is a shaped model rather than a Cross, so it needs naming.
    // **`TallGrass` is deliberately absent** for the opposite reason: it is a
    // Cross and the blanket above owns it.
    case BlockId::Torch:
        return 0.0f;
    // All three are 0 and all three are shaped models, so neither the Cross/Flat
    // rule nor any run branch ever saw them - they were as blast-proof as stone.
    case BlockId::EndRod:
    case BlockId::SoulTorch:
    case BlockId::HoneyBlock:
        return 0.0f;
    // 0.1, the moss carpet's figure. Named nowhere, so it took the rock rule.
    case BlockId::MossBlock:
        return 0.1f;
    // The solid snow cube. See `SnowBlock` in run two: two ids, one block, and
    // the reference's 0.2 rather than the layer's 0.1.
    case BlockId::Snow:
        return 0.2f;
    case BlockId::Dirt:
    case BlockId::Sand:
        return 0.5f;
    // Four soft grounds nobody named, every one of them 0.5 like the dirt they
    // are - so a mangrove swamp and a nether floor were rock.
    case BlockId::CoarseDirt:
    case BlockId::RootedDirt:
    case BlockId::Mud:
    case BlockId::MagmaBlock:
        return 0.5f;
    case BlockId::Grass:
    case BlockId::Gravel:
        return 0.6f;
    case BlockId::Sponge:
    case BlockId::WetSponge:
    case BlockId::HoneycombBlock:
        return 0.6f;
    // **`Leaves`, `Log`, `Planks` and `Glass` are deliberately absent from this
    // switch.** Each is the first member of a family the hoisted material rules
    // above now answer for - `isLeafBlock`, `isLogBlock`, `isPlanksBlock` and
    // `isGlassBlock` respectively - so a `case` here would be dead code sitting
    // in the one file whose whole thesis is that dead rows are the bug.
    case BlockId::CraftingTable:
        return 2.5f;
    case BlockId::SmithingTable:
        return 2.5f;
    case BlockId::Glowstone:
        return 0.3f;
    // 0.3, and it is the light-emitting block that is *not* rock.
    case BlockId::SeaLantern:
        return 0.3f;
    case BlockId::Bricks:
        return 6.0f;
    case BlockId::Clay:
        return 0.6f;
    case BlockId::Sandstone:
        return 0.8f;
    // The four other sandstone cuts, all 0.8 like the plain block. Only the
    // *smooth* pair is 6, and that is the family's one genuine exception, so it
    // is left to the rock rule rather than written out here.
    case BlockId::CutSandstone:
    case BlockId::ChiseledSandstone:
    case BlockId::RedSandstone:
    case BlockId::CutRedSandstone:
    case BlockId::ChiseledRedSandstone:
        return 0.8f;
    case BlockId::Calcite:
        return 0.75f;
    case BlockId::Bookshelf:
        return 1.5f;
    case BlockId::AmethystBlock:
        return 1.5f;
    case BlockId::DripstoneBlock:
    case BlockId::Pumpkin:
    case BlockId::Melon:
        return 1.0f;
    // 0.5, not the log-like 2 the flammability rule used to hand it.
    case BlockId::HayBlock:
        return 0.5f;
    // 6, the same as every other storage block. It reached the timber rule
    // because a coal block genuinely burns, which is not the same question.
    case BlockId::CoalBlock:
        return 6.0f;
    // The three fired-clay families, and they are three different numbers - so
    // one shared "terracotta" rule would be wrong. Plain terracotta was the one
    // id anybody checked and the only one that was right.
    case BlockId::Terracotta:
        return 4.2f;
    case BlockId::Basalt:
    case BlockId::SmoothBasalt:
    case BlockId::PolishedBasalt:
        return 4.2f;
    case BlockId::PackedMud:
    case BlockId::MudBricks:
    case BlockId::LapisBlock:
        return 3.0f;
    case BlockId::Lantern:
    case BlockId::SoulLantern:
        return 3.5f;
    // **`Dandelion`, `Poppy` and `DeadBush` are deliberately absent.** All three
    // are Cross-shaped, so the blanket above owns them; the `static_assert` on
    // `Dandelion` below is what proves the blanket still answers 0 for a plain
    // flower now that nothing here says so.
    case BlockId::Obsidian:
        // 1200, and it is why obsidian is what you build a blast shelter from.
        return 1200.0f;
    case BlockId::AncientDebris:
    case BlockId::EmberiteBlock:
        // The reference's 1200 as well. Blasting for it is a real technique
        // there precisely because the blast cannot destroy what it uncovers.
        return 1200.0f;
    case BlockId::Bedrock:
        return blast::kUnblastable;
    case BlockId::PackedIce:
    case BlockId::Ice:
        return 0.5f;
    // 2.8 and its own tier. It shared the 0.5 of the two cheap ices, so the
    // most expensive block in the family was the easiest to lose.
    case BlockId::BlueIce:
        return 2.8f;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
        return 3.0f;
    default:
        break;
    }
    // The three dyed families, each a contiguous sixteen and each a different
    // published number. Written as ranges rather than forty-eight labels for
    // the reason the colour families are ranges everywhere else.
    if (block >= BlockId::WhiteConcrete && block <= BlockId::BlackConcrete) {
        return 1.8f;
    }
    if (block >= BlockId::WhiteTerracotta && block <= BlockId::BlackTerracotta) {
        return 4.2f;
    }
    if (block >= BlockId::WhiteGlazedTerracotta && block <= BlockId::BlackGlazedTerracotta) {
        return 1.4f;
    }
    // Stone, cobblestone and everything cut from them - slabs and stairs
    // inherit their material's resistance, and the march ignores shape anyway.
    //
    // **The only catch-all in the function, and it is a real family rule rather
    // than a guess**: what reaches it is rock, ore-bearing rock, the copper
    // family, coral blocks, iron bars and the smooth cuts, every one of which
    // the reference publishes at 6.
    return 6.0f;
}

// ---------------------------------------------------------------------------
// The compile-time sweep, which is why the table above is `constexpr` at all.
//
// Modelled on `everyBlockNamed` in `Block.hpp` and `MiningSweep` in
// `Mining.hpp`: strided so no single `static_assert` blows MSVC's constexpr
// step budget, generated rather than hand-listed so a deleted line cannot
// silently stop checking 512 ids, and with a coverage assert at the end so ids
// appended past the last stride cannot fall out of it.
//
// This is the guard the audit found missing. `blockName` has `everyBlockNamed`,
// `blockTextureLayer` has `everyBlockTextured`, mining has `MiningSweep` and
// drops have `DropSweep`; blast resistance was a plain runtime float in a .cpp
// and could carry neither, which is why a hundred and twenty blocks could sit
// on the wrong number through three clean builds and a soak.
// ---------------------------------------------------------------------------

/// 512 for the reason it is 512 in `Block.hpp` and `Mining.hpp`: seven of them
/// cover the enum with room to spare.
///
/// **This is the heaviest of the five sweeps per iteration** and the first one
/// to halve if MSVC's constexpr step budget ever runs short. Each id evaluates
/// `blastResistance`, `blockShape` and `shapedParent`, and a deepslate ore
/// evaluates a nested `blastResistance(stoneOreFor(block))` on top - and it
/// instantiates in all three translation units that include this header. If a
/// future block pushes it over, halve this to 256 and double `kBlastSweepPasses`
/// to 14; the coverage `static_assert` below holds either way and nothing else
/// has to change.
constexpr int kBlastSweepStride = 512;

/// How many strides the generated sweep instantiates.
constexpr int kBlastSweepPasses = 7;

namespace blast {

/// Whether this is one of the 31 values the table is allowed to emit.
///
/// **A change-detector, not a reference check, and the distinction matters.**
/// Every entry here was checked against `Module:Blast_resistance_values` during
/// the audit that produced this table, so nothing in the list is invented - but
/// the list *is* our own emitted set read back out, one entry per distinct value
/// the rules above return. It therefore cannot catch a published figure applied
/// to the wrong block: swap the hopper's 4.8 for the lantern's 3.5 and this
/// stays silent, because both are on the list. What it catches is a value that
/// is on **nobody's** list - a dropped zero, a slipped decimal point, a number
/// typed from memory - and that is a real class of edit, because it is the one
/// this table already made: 3,600 was emitted by bedrock alone and published for
/// nothing at all.
///
/// The nine invariants in `blastTableSound` are where the per-block correctness
/// lives; this is the tripwire under them, and it is deliberately a closed set
/// so that adding a tier has to be a conscious edit rather than a typo.
///
/// > Fails if: any rule above returns a number outside the set - restore
/// > `BlockId::Bedrock` to 3600.0f and the stride covering it stops compiling.
constexpr bool isAllowedTier(float resistance) {
    return resistance == 0.0f || resistance == 0.1f || resistance == 0.2f ||
           resistance == 0.25f || resistance == 0.3f || resistance == 0.4f ||
           resistance == 0.5f || resistance == 0.6f || resistance == 0.65f ||
           resistance == 0.7f || resistance == 0.75f || resistance == 0.8f ||
           resistance == 1.0f || resistance == 1.4f || resistance == 1.5f ||
           resistance == 1.8f || resistance == 2.0f || resistance == 2.5f ||
           resistance == 2.8f || resistance == 3.0f || resistance == 3.5f ||
           resistance == 4.0f || resistance == 4.2f || resistance == 4.8f ||
           resistance == 5.0f || resistance == 6.0f || resistance == 9.0f ||
           resistance == 100.0f || resistance == 600.0f || resistance == 1200.0f ||
           resistance == kUnblastable;
}

/// Every invariant that has to hold for all 3,269 ids at once.
///
/// None of these restates the table; each is a rule the table can break, and
/// **every one of them was broken by the version this replaced**.
constexpr bool blastTableSound(int stride) {
    const int first = stride * kBlastSweepStride;
    const int end = first + kBlastSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId block = static_cast<BlockId>(i);
        const float resistance = blastResistance(block);

        // 1. **The value is one the table is allowed to emit at all.** A
        //    tripwire under the other eight rather than a correctness check -
        //    see `isAllowedTier` for exactly what it can and cannot catch.
        //    > Fails if: `BlockId::Bedrock` goes back to 3600.0f.
        if (!isAllowedTier(resistance)) {
            return false;
        }

        // 2. **No thin shape reached the rock rule.** A ladder, a vine, a cocoa
        //    pod, a plant or a carpet standing at stone's 6 is the signature of
        //    a family that fell past every branch - which is precisely what the
        //    ladder, the cocoa pod and the cobweb all did.
        //    > Fails if: delete the `isLadder` branch; all four ladders reach
        //    > the final `return 6.0f`.
        const BlockShape shape = blockShape(block);
        if (resistance == 6.0f &&
            (shape == BlockShape::Cross || shape == BlockShape::Flat ||
             shape == BlockShape::Vine || shape == BlockShape::Cocoa ||
             shape == BlockShape::Ladder)) {
            return false;
        }

        // 3. **A plank and everything cut from it are 3.** 212 ids, and the one
        //    finding in this batch with the widest reach.
        //    > Fails if: delete the `isPlanksBlock` branch; the ten non-oak
        //    > species fall to the timber rule at 2.
        //
        //    The three exclusions are not holes in the rule - a button, a
        //    pressure plate and a sign each carry a *published* number of their
        //    own that does not depend on what they are cut from, which is why
        //    3b below pins them for every material rather than just for wood.
        const bool ownNumberDespiteParent =
            isButton(block) || isPressurePlate(block) || isSignLike(block);
        const BlockId material = shapedParent(block);
        if (isPlanksBlock(material) && !ownNumberDespiteParent && resistance != 3.0f) {
            return false;
        }

        // 3b. **A button and a plate are 0.5, a sign is 1, whatever they are
        //     made of** - `['button']`, `['pressure plate']`, `['stone button']`,
        //     `['polished blackstone pressure plate']`, `['sign']`,
        //     `['hanging sign']` and `['banner']` are all single entries in the
        //     module with no per-material variants.
        //     > Fails if: move the `isSignLike` branch below the `shapedParent`
        //     > forwarding; the 176 wooden signs become planks' 3.
        if ((isButton(block) || isPressurePlate(block)) && resistance != 0.5f) {
            return false;
        }
        if (isSignLike(block) && resistance != 1.0f) {
            return false;
        }

        // 4. **A leaf is a leaf.** Azalea leaves were 1.0 from a run's private
        //    `default:` while the other eight were 0.2.
        //    > Fails if: give run five back its `default: return 1.0f;`.
        if (isLeafBlock(block) && resistance != 0.2f) {
            return false;
        }

        // 5. **A carpet is not its wool**, which is the one place the cut-shape
        //    forwarding must not be trusted.
        //    > Fails if: move the `isCarpet` branch below the `shapedParent`
        //    > forwarding; all sixteen become wool's 0.8.
        if (isCarpet(block) && resistance != 0.1f) {
            return false;
        }

        // 6. **A cauldron is a cauldron however full it is.**
        //    > Fails if: replace `isCauldron` with `block == BlockId::Cauldron`;
        //    > the six filled ids reach the rock rule.
        if (isCauldron(block) && resistance != 2.0f) {
            return false;
        }

        // 7. **The deepslate half of an ore is the stone half's number.**
        //    > Fails if: delete the `isDeepslateOre` normalisation; all eight
        //    > reach the rock rule at 6 against their stone twins' 3.
        if (isDeepslateOre(block) && resistance != blastResistance(stoneOreFor(block))) {
            return false;
        }

        // 8. **A cut shape resists exactly as the block it was cut from, and
        //    the legacy stone slab is the only exception in the whole table.**
        //    The blast half of `everyCutShapeCostsItsParent` in `Mining.hpp`,
        //    and the binding finding 859 asked for: the mining side states the
        //    rule both ways and the blast side stated it nowhere.
        //
        //    It is written **without asking `isLegacyStoneSlabMaterial`**, on
        //    purpose. An assert that consults the same predicate the table
        //    consults proves only that the table agrees with itself - edit the
        //    list and both sides move together (`CLAUDE.md` bug shape #11,
        //    eleven such asserts once passed while pointing at the wrong
        //    texture). What this names instead is the reference's published
        //    *pair* - `['sandstone slab'] = 6` sitting beside
        //    `['sandstone'] = 0.8` in `Module:Blast_resistance_values` - so a
        //    slab may read 6 only where the block it was cut from reads 0.8,
        //    and every other cut shape must match its parent exactly.
        //    > Fails if: add a material that *has* a slab and whose own figure
        //    > is neither 0.8 nor 6 to `isLegacyStoneSlabMaterial`. Measured
        //    > by compiling a patched copy of this header: Mud Bricks (3) and
        //    > End Stone Bricks (9) each stop the build here, because their
        //    > slab would read 6 against a block that does not read 0.8.
        //    > Widening to planks is caught twice over, here and by invariant 3.
        //    > Widening to a material with no slab family - basalt has none -
        //    > changes nothing and correctly passes.
        //    > The other direction - *removing* one of the five - is caught by
        //    > `everyLegacySlabSplitsFromItsBlock` below, which spells the five
        //    > out and reads the numbers off the module; deleting the block of
        //    > quartz from it stops the build there. Neither assert alone is
        //    > the binding; the pair is, and both halves were made to fire
        //    > before either was believed.
        //
        //    The four exclusions are the blocks that carry a published figure
        //    of their own no matter what they are cut from, and invariants 3b
        //    and 5 already pin every one of them to it.
        if (material != block && !ownNumberDespiteParent && !isCarpet(block)) {
            const float parentResistance = blastResistance(material);
            const bool legacySlabSplit =
                isSlab(block) && resistance == 6.0f && parentResistance == 0.8f;
            if (!legacySlabSplit && resistance != parentResistance) {
                return false;
            }
        }
    }
    return true;
}

/// One `static_assert` per stride, generated rather than written out - the
/// pattern `MiningSweep` and `DropSweep` already use, and for the reason those
/// give: a hand-written list can have a middle line deleted and 512 ids stop
/// being checked while the build stays green.
template <int Pass>
struct BlastSweep {
    static_assert(blastTableSound(Pass),
                  "a block's blast resistance breaks one of the ten invariants above - the"
                  " numbered comments name the single edit that causes each");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyBlastPassSwept(std::integer_sequence<int, Pass...>) {
    return (BlastSweep<Pass>::swept && ...);
}

/// The five legacy stone slabs, spelled out, with the numbers read off the
/// module rather than off the predicate the table consults.
///
/// **The positive half of invariant 8's binding, and the reason 859 wanted a
/// pair rather than an assert.** Invariant 8 catches the list being *widened* -
/// it refuses any slab reading 6 whose block does not read 0.8 - but it cannot
/// catch the list being *narrowed*, because a slab that quietly falls through
/// to its parent's number satisfies it perfectly. This names the five and
/// demands the split, so deleting one stops the build. Neither half is the
/// binding on its own.
///
/// Every figure is from the reference's `Module:Blast_resistance_values`: the
/// block, its stairs and its wall are 0.8, and both halves of the slab are 6.
constexpr bool everyLegacySlabSplitsFromItsBlock() {
    constexpr BlockId kSplit[] = {
        BlockId::Sandstone,      BlockId::CutSandstone, BlockId::RedSandstone,
        BlockId::CutRedSandstone, BlockId::QuartzBlock,
    };
    // Stonecutter option 0 is the stairs and 2 is the wall; 1 is the slab, and
    // it is the one cut that does *not* stay with the block.
    constexpr int kCutsThatStayWithTheBlock[] = {0, 2};

    int found = 0;
    for (const BlockId material : kSplit) {
        if (blastResistance(material) != 0.8f) {
            return false;
        }
        for (const int option : kCutsThatStayWithTheBlock) {
            const BlockId cut = stonecutterOption(material, option);
            if (cut != BlockId::Air && blastResistance(cut) != 0.8f) {
                return false;
            }
        }
        // **Both halves.** A derivation applied to one of a pair and not the
        // other is `CLAUDE.md` bug shape #5, and a slab family is exactly such
        // a pair - `stonecutterOption` only ever hands back the bottom one.
        for (int family = 0; family < kSlabFamilyCount; ++family) {
            if (kSlabFamilies[static_cast<std::size_t>(family)].parent != material) {
                continue;
            }
            if (blastResistance(slabAt(family, false)) != 6.0f ||
                blastResistance(slabAt(family, true)) != 6.0f) {
                return false;
            }
            ++found;
        }
    }
    // ...and the loop is not quietly vacuous. Five materials, five slab
    // families reached: a check one failed lookup short of testing nothing at
    // all compiles just as green as one that works (`CLAUDE.md` bug shape #15).
    return found == 5;
}

} // namespace blast

static_assert(blast::everyBlastPassSwept(std::make_integer_sequence<int, kBlastSweepPasses>{}),
              "the blast table is wrong somewhere - the failing BlastSweep instantiation above "
              "names which stride");
/// The passes are generated, so this is what proves there are enough of them.
static_assert(kBlastSweepPasses * kBlastSweepStride >= static_cast<int>(kBlockIdCount),
              "the blast sweep no longer covers every block id - raise kBlastSweepPasses");

// ---------------------------------------------------------------------------
// The named checks: one per finding whose fix is a *value* rather than an
// invariant, each written as the whole expression a reader evaluates rather
// than one side of it compared against itself.
// ---------------------------------------------------------------------------

// **Change either literal back to 3600.0f and this fails.** Functionally the
// crater march cannot tell 3,600 from 3,600,000 - any resistance above about
// 30 stops every ray a power-4 charge can throw - so nothing but an assert was
// ever going to catch it.
static_assert(blastResistance(BlockId::Bedrock) == blast::kUnblastable &&
                  blastResistance(BlockId::EndPortalFrame) == blast::kUnblastable,
              "the reference's 3,600,000, not the 3,600 that is published for nothing");

// **Fold the pistons back into the button branch and this fails.** Both halves
// are named, so it also fires if the buttons are moved to the piston's number.
static_assert(blastResistance(BlockId::PistonRunFirst) == 1.5f &&
                  blastResistance(BlockId::ButtonRunFirst) == 0.5f,
              "a piston is 1.5 and a button is 0.5; the 0.5 piston in the module is Education's "
              "six-sided one");

// **Pair the rod with the observer again and this fails.** They agree on
// hardness 3, which is exactly what hid the disagreement on blast.
static_assert(blastResistance(BlockId::LightningRodRunFirst) == 6.0f &&
                  blastResistance(BlockId::ObserverRunFirst) == 3.0f,
              "a lightning rod is copper and copper is 6; an observer is 3");

// **Delete any one of these three hoists and the matching half fails.** All
// three are Cross-shaped, so the blanket 0.0 below them answers the moment they
// are not asked first - and two of them had `case` labels further down that
// were dead code for exactly that reason.
static_assert(blastResistance(BlockId::Cobweb) == 4.0f &&
                  blastResistance(BlockId::Conduit) == 3.0f &&
                  blastResistance(BlockId::PointedDripstone) == 3.0f &&
                  blastResistance(BlockId::Dandelion) == 0.0f,
              "three cross-shaped blocks with published numbers, and one that really is free");

// **The slab exception, written as the whole family and as a pair of asserts.**
// The reference pins the legacy stone slabs at 6 while the block, the stairs
// and the wall cut from it stay at 0.8. This is the *positive* half - it fires
// if the exception is dropped from any of the five, or applied to only one half
// of a slab, or leaked onto the stairs or wall. The *negative* half is
// invariant 8 in `blastTableSound`, which fires if a sixth material is added.
// Neither consults `isLegacyStoneSlabMaterial`, which is the point: an assert
// that asks the same predicate the table asks proves only that the table agrees
// with itself (`CLAUDE.md` bug shape #11).
static_assert(blast::everyLegacySlabSplitsFromItsBlock(),
              "a legacy stone slab is 6 where the block it was cut from, its stairs and its "
              "wall are all 0.8 - and there are five of them");

// **Widen `isFlammable` back into a wood rule and this fails.** Each of the
// four burns and none of them is timber, which is the whole of that finding.
static_assert(blastResistance(BlockId::CoalBlock) == 6.0f &&
                  blastResistance(BlockId::HayBlock) == 0.5f &&
                  blastResistance(BlockId::VineFirst) == 0.2f &&
                  blastResistance(BlockId::Log) == 2.0f,
              "flammable is not the same question as wooden");

// **The three fired-clay families are three different numbers**, so a single
// shared "terracotta" rule fails this the moment anyone writes one.
static_assert(blastResistance(BlockId::Terracotta) == 4.2f &&
                  blastResistance(BlockId::WhiteTerracotta) == 4.2f &&
                  blastResistance(BlockId::WhiteGlazedTerracotta) == 1.4f &&
                  blastResistance(BlockId::WhiteConcrete) == 1.8f,
              "plain and dyed terracotta 4.2, glazed 1.4, concrete 1.8");

// **The smooth cuts are the exception in two families at once.** Both fire if
// anyone re-groups the smooth form with the cut it looks like.
static_assert(blastResistance(BlockId::SmoothQuartz) == 6.0f &&
                  blastResistance(BlockId::QuartzBlock) == 0.8f &&
                  blastResistance(BlockId::SmoothSandstone) == 6.0f &&
                  blastResistance(BlockId::CutSandstone) == 0.8f,
              "smooth quartz and smooth sandstone are 6 where the other cuts are 0.8");

// **The solid snow cube against the layer**, which are two different published
// numbers under three ids. Fires if either cube id is given the layer's 0.1.
static_assert(blastResistance(BlockId::Snow) == 0.2f &&
                  blastResistance(BlockId::SnowBlock) == 0.2f &&
                  blastResistance(BlockId::SnowLayerFirst) == 0.1f,
              "the cube is 0.2 and the layer is 0.1");

/// Every block a blast of this power removes.
///
/// The reference's algorithm exactly: 1352 rays toward the surface of a 16³
/// index grid, each starting at `power × [0.7, 1.3)` and advancing 0.3 blocks
/// at a time, losing a flat 0.225 per step plus `(resistance + 0.3) × 0.3` for
/// whatever it is passing through. A block is taken only if the ray still has
/// intensity left *after* paying for it, so the one that finally stops a ray
/// survives — which is what makes a crater ragged rather than spherical.
///
/// `seed` varies the per-ray roll. Two blasts at the same spot should not carve
/// the same hole.
std::vector<glm::ivec3> explosionBlocks(const World& world, const glm::vec3& centre, float power,
                                        std::uint32_t seed);

/// The fraction of a box the blast can actually see, 0 to 1.
///
/// Rays from the centre to a grid of sample points across the box; the share
/// that reach it unobstructed. Water does not shield, which is the reference's
/// behaviour and falls out for free because the ray test asks for a block's
/// **collision** geometry — and collision geometry is the point: selection
/// geometry gives a tuft of grass a 0.75-wide column so the crosshair can pick
/// it out, and asking that question let a flower bed shelter you from TNT.
///
/// **Not free.** Roughly 36 sampled rays for a creature-sized box, each a DDA
/// walk. Call `withinBlast` first — everything outside twice the power takes
/// nothing whatever the exposure says, so paying for it is pure waste.
float explosionExposure(const World& world, const glm::vec3& centre, const Aabb& box);

/// Whether a blast reaches this point at all.
///
/// **One owner for the `2 × power` reach**, because it is asked in two places:
/// once here as the cheap pre-filter every caller must run before paying for
/// `explosionExposure`, and once inside `explosionImpact`, which cannot trust
/// that its caller did. Two copies of the number is the recurring bug; two
/// callers of one copy is the fix.
constexpr bool withinBlast(const glm::vec3& centre, float power, const glm::vec3& feet) {
    // Component-wise and squared: no square root, and no reliance on glm's
    // operators being usable in a constant expression.
    const float dx = feet.x - centre.x;
    const float dy = feet.y - centre.y;
    const float dz = feet.z - centre.z;
    const float reach = 2.0f * power;
    return dx * dx + dy * dy + dz * dz < reach * reach;
}

// Twice the power, and the reference's own figure: a charge of power four
// reaches eight blocks. **The single edit that makes this fail is changing the
// `2.0f` above** - which is exactly the edit that would silently double or
// halve every blast's radius while every crater still looked right, because the
// crater is carved by `explosionBlocks` and has nothing to do with this.
static_assert(withinBlast(glm::vec3{0.0f, 0.0f, 0.0f}, 4.0f, glm::vec3{7.9f, 0.0f, 0.0f}));
static_assert(!withinBlast(glm::vec3{0.0f, 0.0f, 0.0f}, 4.0f, glm::vec3{8.1f, 0.0f, 0.0f}));

/// The one quantity distance and shelter feed into. Damage and knockback both
/// read it, which is why it is computed once rather than twice.
///
/// **It cannot tell "fully shielded" from "out of range" - both come back
/// 0** - and that is the whole of finding 1039. The reference draws a hard line
/// between them: everything inside `2 x power` takes at least one point of
/// damage *however completely it is sheltered*, and only distance excuses an
/// entity entirely. A single float cannot carry both answers, so the callers
/// have to ask `withinBlast` for the range half - which `Main.cpp` already does
/// and `Creature.cpp` already does - and then **stop treating a zero impact as
/// a reason to skip the entity**. Checked 2026-08-18: both of them skip.
float explosionImpact(const glm::vec3& centre, float power, const glm::vec3& feet, float exposure);

/// Damage from that impact, in half-hearts, on Normal.
///
/// The reference's curve is `7 x power x (impact^2 + impact) + 1`, scaled to
/// Bedrock's softer numbers. **The trailing `+ 1` is the reference's floor** -
/// an impact of exactly zero, which is what a fully sheltered entity produces,
/// still yields one point. Measured 2026-08-18: this function delivers that,
/// and **neither caller ever reaches it**, because both skip an entity whose
/// impact is zero before asking. The floor is therefore correct here and absent
/// in play; finding 1039 routes the two call sites.
///
/// Only a *negative* impact returns nothing, and only because the curve turns
/// downward there - no caller can produce one, since `explosionImpact` clamps
/// to zero outside the reach.
int explosionDamage(float power, float impact);

// ---------------------------------------------------------------------------
// What a blast leaves behind.
// ---------------------------------------------------------------------------

namespace blast {

/// Blocks per second of shove at full impact, for **everything a blast
/// catches**.
///
/// **The reference's figure is one block per tick - twenty a second - and it is
/// wrong for this engine, for a reason that has nothing to do with what is
/// being thrown.** The reference decays an entity's velocity by 9% every tick,
/// so its impulse is spent in about a second; ours does not, so a thing thrown
/// off the ground keeps every bit of that speed until it lands. Twenty reads as
/// a rocket rather than a blast. Seven is the sustained-speed equivalent, tuned
/// to just under a jump's worth of lift at point blank.
///
/// **It lives here because the argument above is about our physics, not about
/// the entity**, and it had already been made twice and answered differently
/// both times: `Main.cpp` carries `kBlastKnockback = 7.0f` with that reasoning
/// written out, and `Creature.cpp` carries `kBlastThrow = 20.0f` with a comment
/// that only converts the units. Measured 2026-08-18: the identical impact
/// throws a cow **2.9 times** as far as it throws the player. That is
/// `CLAUDE.md` bug shape #14 - a rule that is correct, is commented, and
/// travelled to only one of the two places that needed it - and the fix for
/// that shape is one constant both callers read, never a corrected copy.
///
/// Named differently from either of them on purpose, and **that judgement has
/// since been paid for in full**. On 2026-08-19 `Mining.hpp` and `Creature.cpp`
/// were found to have independently invented the same `kTicksPerSecond` - one
/// at `game` scope in a header, one in an anonymous namespace - and because the
/// header reached that file the collision **stopped the build** with C2872
/// rather than quietly being called sharing. Finding 857 is the same shape a
/// third time, in `kDropSweepStride`. Two constants of one name in one
/// translation unit is never sharing; one constant read twice is.
///
/// **Neither caller reads this. Re-verified 2026-08-19 06:28** by enumerating
/// every occurrence tree-wide rather than counting: `kKnockbackSpeed` appears
/// exactly twice, at this definition and at the `static_assert` below it, while
/// `kBlastKnockback` has its definition plus one use in `Main.cpp` and
/// `kBlastThrow` its definition plus one use in `Creature.cpp`. Finding 1040
/// routes the two of them.
///
/// **What would make this false**, so the claim can be re-tested rather than
/// trusted: a third code site naming `kKnockbackSpeed`, or either private
/// constant losing its definition. Both are one grep, and a negative claim
/// with no stated falsifier is the kind that rots quietest.
constexpr float kKnockbackSpeed = 7.0f;

/// **Not the reference's number, and deliberately not.** If this is ever put
/// back to twenty, the per-tick velocity decay it assumes has to exist first -
/// so this fires as a reminder rather than as a rule.
static_assert(kKnockbackSpeed == 7.0f,
              "a blast's shove is a sustained speed here, not the reference's one-tick impulse");

/// The blocks the reference drops from **every** explosion, whatever the power
/// and whatever set it off.
///
/// The reference names six: dragon eggs, beacons, conduits, heads, shulker
/// boxes and decorated pots. **Three of those six do not exist in this game as
/// of 2026-08-18** - there is no head, no shulker box and no decorated pot - so
/// this names the three that do. Adding any of the other three is the moment to
/// come back here; the list is the reference's, not ours.
constexpr bool alwaysDropsFromBlast(BlockId block) {
    switch (block) {
    case BlockId::DragonEgg:
    case BlockId::Beacon:
    case BlockId::Conduit:
        return true;
    default:
        // A catch-all that returns a real value hides every missing entry
        // (`CLAUDE.md` bug shape #10) - so this one returns the *general* rule
        // rather than a second answer of its own, and the named asserts below
        // hold the three exceptions in place.
        return false;
    }
}

/// The chance in [0, 1] that a block destroyed by a blast leaves its drop.
///
/// **The reference states two different numbers and we were using one of them
/// for both.** Verbatim: *"Blocks destroyed by TNT have a 100% chance of
/// dropping. Other explosions have a 1/power chance of dropping items."* The
/// gamerule that used to make TNT decay too, `tntExplosionDropDecay`, is *"the
/// only one set to false by default"* since 1.21, so a new Bedrock world drops
/// everything a charge breaks. A creeper still pays 1/3 and a charged creeper
/// 1/6.
///
/// **DO NOT PUT THE CHARGE BACK TO 1/4, AND READ THIS BEFORE YOU TRY.** Twenty
/// five per cent is what Bedrock did *before* 1.21 and the internet is still
/// full of it: searching this question lands on MCPE-180168, whose answer is
/// that Bedrock hardcodes 25% with no gamerule. **That issue predates the fix.**
/// Bedrock 1.21 added `tntExplosionDropDecay` as a declared vanilla-parity
/// change and defaults it to `false` in new worlds. Three things pin the
/// edition, none of them a guess:
///   - the wiki tags the sibling rules `blockExplosionDropDecay` and
///     `mobExplosionDropDecay` *[JE only]* and tags this one **not at all**;
///   - its note *"upgraded worlds need to use /gamerule tntExplosionDropDecay
///     false to get the new behavior"* is tagged ***[BE only]***, which is only
///     a sentence anyone can write if Bedrock has the rule;
///   - Mojang's own `bedrock-samples/behavior_pack/entities/tnt.json` publishes
///     `"power": 4`, so the 1/power arm really would be a quarter.
/// The creeper arm is *not* covered by that change - `mobExplosionDropDecay` is
/// Java-only, so a Bedrock creeper still decays - which is exactly why the two
/// arms are separate here and separately asserted below.
///
/// `fromTnt` rather than a power comparison on purpose. Power four happens to
/// mean TNT today and creepers happen to be 3 and 6, so `power == 4` would work
/// and would keep working right up until something else is given four - which
/// is `CLAUDE.md` bug shape #1, a value derived somewhere other than the table
/// that owns it.
///
/// **Adopted by `Main.cpp` on 2026-08-19, and the way it was adopted is the
/// part worth reading.** This comment previously said "No caller, re-verified
/// 2026-08-19 07:28" and, worse, that the missing piece was "one defaulted
/// `bool fromTnt` on a struct in `Creature.hpp`". **Both statements are now
/// false, and the second one instructed a reader to make a change that must not
/// be made** - it was left standing for about three hours after the adoption
/// landed, and one agent was dispatched to add that field before another
/// refused it. That is `CLAUDE.md` bug shape #16 in its top-ranked form: a
/// comment that is wrong while the code is right, arguing for a breaking edit,
/// in a confident voice the rest of this file has earned.
///
/// **`Main.cpp` solved it by ordering rather than by a field, which is why no
/// struct changed.** Creeper blasts are appended to the shared vector first and
/// TNT is pushed after, so the boundary is known without storing it: the flag
/// is computed at `Main.cpp:10563` as `fromTnt = blastIndex >= creeperBlasts`
/// and consumed at `:10758`. **Do not add `bool fromTnt` to `CreatureExplosion`
/// now** - the question already has an answer, and a second one that nothing
/// reads is bug shape #1 and the most expensive shape in this project.
///
/// **What would make THIS claim false**, dated 2026-08-19 and stated so the
/// next reader checks in one grep rather than trusting the date, because a
/// negative claim rots fastest and this exact comment has already rotted once:
/// the disappearance of `blastIndex >= creeperBlasts` from `Main.cpp`, or TNT
/// ceasing to be appended after creepers - the ordering IS the mechanism, so a
/// reordering of those two pushes silently inverts every drop in the game.
///
/// **The 4x TNT loss this documented is fixed, and the creeper arm was never
/// broken.** `Main.cpp` rolls at `roll * blast.power < 1.0f`, which *is* the
/// 1/power rule rearranged; it was correct for creepers all along and was only
/// ever wrong because TNT was being sent through it too. `Main.cpp:10729`
/// records the measurement after the fix: **26 of 26 for TNT, with the creeper
/// side bit-identical either way**, which is the right shape for a control - it
/// fired, it moved in one direction only, and the untouched arm did not budge.
///
/// **So do not "fix" the arithmetic.** The expression looks like a bug to
/// anyone who expects to see `1.0f / power` written out, and it is not one.
///
/// The rule lives here because the two sources that need it - the charge in
/// `Main.cpp` and the creeper coming out of `Creature.cpp` - sit in different
/// files, and a rule stated in one of them is a rule the other cannot share.
constexpr float explosionDropChance(BlockId destroyed, float power, bool fromTnt) {
    if (fromTnt || alwaysDropsFromBlast(destroyed)) {
        return 1.0f;
    }
    // A blast of no power destroys nothing, so nothing can reach this; the
    // guard is here so the division cannot be the thing that proves it.
    return power > 0.0f ? 1.0f / power : 1.0f;
}

// **The TNT half, which is the half that is wrong in play.** Make `fromTnt`
// stop meaning anything and this fails; today's charge drops one block in four.
static_assert(explosionDropChance(BlockId::Stone, 4.0f, true) == 1.0f,
              "a charge drops everything it breaks - the reference's 100 per cent, not 1/4");

// **The other half, unchanged**, and both creeper powers named so that fixing
// the charge cannot quietly take the creeper with it.
static_assert(explosionDropChance(BlockId::Stone, 3.0f, false) == 1.0f / 3.0f &&
                  explosionDropChance(BlockId::Stone, 6.0f, false) == 1.0f / 6.0f,
              "a creeper is 1/3 and a charged creeper 1/6");

// **The three exceptions, and one block that is not one of them.** The last
// clause is what stops `alwaysDropsFromBlast` being widened into `return true`
// - a list that says yes to everything passes every positive test there is.
static_assert(explosionDropChance(BlockId::DragonEgg, 3.0f, false) == 1.0f &&
                  explosionDropChance(BlockId::Beacon, 3.0f, false) == 1.0f &&
                  explosionDropChance(BlockId::Conduit, 3.0f, false) == 1.0f &&
                  explosionDropChance(BlockId::Obsidian, 3.0f, false) != 1.0f,
              "a dragon egg, a beacon and a conduit always drop; obsidian takes its chances");

} // namespace blast

} // namespace game
