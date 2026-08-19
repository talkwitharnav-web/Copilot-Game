#pragma once

#include "item/Item.hpp"
#include "item/Tool.hpp"
#include "world/Block.hpp"
#include "world/Tick.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace game {

/// **One row per block id, and the array is sized by the enum.** Everything the
/// breaking code needs to know about a block is here, so hardness, tool and
/// tier are three reads of the same row rather than three hand-written sets of
/// range tests over the seven table runs - which is how the three accessors
/// that used to answer them came to disagree about which runs they covered at
/// all. Only `blockHardness` is still wrapped in a named accessor, in
/// `Tool.cpp`; tool and tier are read as `miningRow(b).tool` and `.tier` at the
/// point of use, which is what stops them drifting apart again.
///
/// Kept to eight bytes: it is read on every block break, by the drop, and by
/// anything asking whether a tool is worth swinging.
struct MiningRow {
    /// The reference's own hardness, in the units its break formula uses:
    /// `hardness * 30` ticks with the tool a block asks for, `hardness * 100`
    /// without. `kUnbreakableHardness` stands in for the reference's -1.
    float hardness = 1.0f;
    /// The kind that *speeds it up* - the reference's `isBestTool`. It says
    /// nothing about whether the block drops; `toolRequired` does that.
    ToolKind tool = ToolKind::None;
    /// The lowest tool tier that will collect it, or `kHandTier` for "any".
    std::int8_t tier = static_cast<std::int8_t>(kHandTier);
    /// Whether the drop needs the **right kind** of tool, at any tier. This is
    /// the half of the reference's `canHarvest` that a tier alone cannot say:
    /// an ender chest, a lantern and a weighted plate all want *a* pickaxe and
    /// do not care which, and snow wants a shovel or nothing.
    bool toolRequired = false;
};

/// Our stand-in for the reference's hardness of -1, "never breaks". Nothing
/// here treats a block as unbreakable, so an absurd hardness is what enforces
/// it - and creative still ignores it, which is correct.
constexpr float kUnbreakableHardness = 3600.0f;

/// One tick is 1/20 s, which is the unit the reference's break formula counts
/// in and rounds up to, and **`tick::kPerSecond` in `Tick.hpp` is the one owner
/// of the rate**.
///
/// A local `kTicksPerSecond` alias stood here for readability. **It is gone**:
/// `Creature.cpp` had independently invented the identical alias inside
/// `namespace game { namespace { ... } }`, and because this header is included
/// there, every unqualified use in that file resolved to two declarations at
/// once - **C2872, ambiguous symbol** - and stopped the build on 2026-08-19.
///
/// **A .cpp-local alias is harmless; a header one is the collision**, so the
/// header is the side that yields. This is finding 857's shape exactly:
/// `kDropSweepStride` and `kDropSweepPasses` are declared by `BlockDrops.hpp`
/// at `game` scope *and* by `ItemEntity.cpp` in an anonymous namespace, which
/// was filed as latent shadowing and has now been shown to be a build-stopper
/// rather than untidiness. **Do not reintroduce a convenience alias for a
/// constant another header already owns** - spell it `tick::kPerSecond`.

/// The reference's two damage divisors, both measured in ticks per unit of
/// hardness: 30 when the block will drop for you, 100 when it will not. Our old
/// pair of 1.5 and 5.0 were these two divided by `tick::kPerSecond`.
constexpr float kHarvestableDivisor = 30.0f;
constexpr float kUnharvestableDivisor = 100.0f;

/// Both of the reference's environment penalties are the same number: a head
/// underwater without Aqua Affinity divides the speed by five, and so do feet
/// off the ground. Both at once is 25x slower.
///
/// > Aqua Affinity is an enchantment and we have no enchanting, so the water
/// > penalty is unconditional until that exists.
constexpr float kEnvironmentPenalty = 5.0f;

namespace mining {

// ---------------------------------------------------------------------------
// The tool table, moved here from `Tool.cpp` so that it is `constexpr` and the
// break formula can be proved against the reference's published times at
// compile time. `toolFor` in `Tool.cpp` is now one line onto this.
// ---------------------------------------------------------------------------

struct ToolRule {
    ItemId item;
    ToolProperties properties;
};

/// Speeds and durabilities are the reference's own, looked up rather than
/// invented: mining speed 2 / 4 / 6 / 8 / 9 and durability 59 / 131 / 250 /
/// 1561 / 2031 by rising tier. Wood and stone were written as 60 and 132 before
/// the table was checked and are left alone - one use either way is noise.
///
/// **A sword and a hoe are the two rows this paragraph used to describe
/// wrongly.** The reference gives a sword a flat 1.5x whatever it is made of -
/// the *blow* scales with the material, not the swing - and these rows rose
/// 1.5 / 2 / 2.5 / 3 / 3.5 under a comment claiming `strike` read the same
/// field for damage. It does not, and never did: `Main.cpp` builds a blow from
/// `weaponDamage(kind, tier)`, and `ToolProperties::speed` has exactly one
/// reader in the whole tree - `breakTicksFor` at the foot of this file. So the
/// sword rows are the reference's flat 1.5 and nothing outside this file moved.
///
/// **A hoe is a mining tool and climbs its tier's ladder**, 2 / 4 / 6 / 8 / 9,
/// the same one every pickaxe, axe and shovel climbs; it had been given the
/// sword's numbers as well. The file already knew: the wart-block note in
/// `derivedTool` says a hoe "should make it 0.75 s" on a 1.0-hardness block,
/// which is 30 ticks over a wooden hoe's **2.0** and comes out at 1.00 s over
/// 1.5. A wooden hoe on a leaf is the reference's published 0.15 s again
/// rather than 0.20 s, and a diamond or emberite one breaks a leaf in the one
/// tick the reference breaks it in. <https://minecraft.wiki/w/Tiers>
constexpr std::array<ToolRule, 52> kTools{{
    {ItemId::WoodenPickaxe, {ToolKind::Pickaxe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenAxe, {ToolKind::Axe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenShovel, {ToolKind::Shovel, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenSword, {ToolKind::Sword, kWoodTier, 1.5f, 60}},
    {ItemId::WoodenHoe, {ToolKind::Hoe, kWoodTier, 2.0f, 60}},
    {ItemId::StonePickaxe, {ToolKind::Pickaxe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneAxe, {ToolKind::Axe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneShovel, {ToolKind::Shovel, kStoneTier, 4.0f, 132}},
    {ItemId::StoneSword, {ToolKind::Sword, kStoneTier, 1.5f, 132}},
    {ItemId::StoneHoe, {ToolKind::Hoe, kStoneTier, 4.0f, 132}},
    {ItemId::IronPickaxe, {ToolKind::Pickaxe, kIronTier, 6.0f, 250}},
    {ItemId::IronAxe, {ToolKind::Axe, kIronTier, 6.0f, 250}},
    {ItemId::IronShovel, {ToolKind::Shovel, kIronTier, 6.0f, 250}},
    {ItemId::IronSword, {ToolKind::Sword, kIronTier, 1.5f, 250}},
    {ItemId::IronHoe, {ToolKind::Hoe, kIronTier, 6.0f, 250}},
    {ItemId::DiamondPickaxe, {ToolKind::Pickaxe, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondAxe, {ToolKind::Axe, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondShovel, {ToolKind::Shovel, kDiamondTier, 8.0f, 1561}},
    {ItemId::DiamondSword, {ToolKind::Sword, kDiamondTier, 1.5f, 1561}},
    {ItemId::DiamondHoe, {ToolKind::Hoe, kDiamondTier, 8.0f, 1561}},
    {ItemId::EmberitePickaxe, {ToolKind::Pickaxe, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteAxe, {ToolKind::Axe, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteShovel, {ToolKind::Shovel, kEmberiteTier, 9.0f, 2031}},
    {ItemId::EmberiteSword, {ToolKind::Sword, kEmberiteTier, 1.5f, 2031}},
    {ItemId::EmberiteHoe, {ToolKind::Hoe, kEmberiteTier, 9.0f, 2031}},
    // **The twenty-five armour pieces, landed 2026-08-19 11:41 on the
    // dispatcher's explicit release of the interlock.** Until now this table
    // was silent about armour, `toolProperties` fell through to
    // `ToolProperties{}`, and `durability = 0` means "never wears" by that
    // struct's own comment - so **a full diamond set was permanent free
    // protection**, found independently by two agents (findings 9625, 9660).
    //
    // **Every row is the default `ToolProperties` with one field changed.**
    // `ToolKind::None`, `kHandTier` and `1.0f` are exactly what the fallback
    // returned a moment ago, so the ONLY behaviour that moves is durability.
    // That is deliberate and it is the whole safety argument: a row that gave
    // a helmet a real `kind` or `tier` would hand it mining speed, a harvest
    // tier and - for an axe kind - stripping, and a diamond helmet would start
    // opening obsidian. **The `Bow` row directly below is the precedent**, and
    // its comment already makes this argument: "wears like a tool and mines
    // like nothing, which is exactly what `ToolKind::None` with a durability
    // says", deliberately outside `isTool`. Armour is the same case.
    //
    // **Provenance, stated honestly because it is unlike the rest of this
    // file:** these are wiki armour-material tables, NOT primary.
    // `bedrock-samples` publishes recipes and entities but **no armour item
    // definitions**, so there is no source of record to cite the way the
    // smelting rows cite `furnace_*.json`. Chainmail and iron sharing one row
    // of values is the reference's own doing, not a copy-paste.
    // <https://minecraft.wiki/w/Armor>
    //
    // > **THIS HALF IS NOT SAFE ALONE AND MUST NOT BE BUILT ALONE.**
    // > `Inventory.hpp` carries a `static_assert` claiming
    // > `mining::toolProperties(ItemId::DiamondHelmet).durability == 0`. It is
    // > a tripwire written to fail on exactly this edit. **It must move to
    // > `== 363` in the same build**, keeping the `DiamondPickaxe == 1561`
    // > half as its control. Either half alone fails the build for every
    // > translation unit that includes `Inventory.hpp`, which is most of them.
    {ItemId::LeatherHelmet, {ToolKind::None, kHandTier, 1.0f, 55}},
    {ItemId::LeatherChestplate, {ToolKind::None, kHandTier, 1.0f, 81}},
    {ItemId::LeatherLeggings, {ToolKind::None, kHandTier, 1.0f, 76}},
    {ItemId::LeatherBoots, {ToolKind::None, kHandTier, 1.0f, 65}},
    {ItemId::ChainmailHelmet, {ToolKind::None, kHandTier, 1.0f, 165}},
    {ItemId::ChainmailChestplate, {ToolKind::None, kHandTier, 1.0f, 240}},
    {ItemId::ChainmailLeggings, {ToolKind::None, kHandTier, 1.0f, 225}},
    {ItemId::ChainmailBoots, {ToolKind::None, kHandTier, 1.0f, 195}},
    {ItemId::IronHelmet, {ToolKind::None, kHandTier, 1.0f, 165}},
    {ItemId::IronChestplate, {ToolKind::None, kHandTier, 1.0f, 240}},
    {ItemId::IronLeggings, {ToolKind::None, kHandTier, 1.0f, 225}},
    {ItemId::IronBoots, {ToolKind::None, kHandTier, 1.0f, 195}},
    {ItemId::GoldenHelmet, {ToolKind::None, kHandTier, 1.0f, 77}},
    {ItemId::GoldenChestplate, {ToolKind::None, kHandTier, 1.0f, 113}},
    {ItemId::GoldenLeggings, {ToolKind::None, kHandTier, 1.0f, 106}},
    {ItemId::GoldenBoots, {ToolKind::None, kHandTier, 1.0f, 91}},
    {ItemId::DiamondHelmet, {ToolKind::None, kHandTier, 1.0f, 363}},
    {ItemId::DiamondChestplate, {ToolKind::None, kHandTier, 1.0f, 528}},
    {ItemId::DiamondLeggings, {ToolKind::None, kHandTier, 1.0f, 495}},
    {ItemId::DiamondBoots, {ToolKind::None, kHandTier, 1.0f, 429}},
    {ItemId::EmberiteHelmet, {ToolKind::None, kHandTier, 1.0f, 407}},
    {ItemId::EmberiteChestplate, {ToolKind::None, kHandTier, 1.0f, 592}},
    {ItemId::EmberiteLeggings, {ToolKind::None, kHandTier, 1.0f, 555}},
    {ItemId::EmberiteBoots, {ToolKind::None, kHandTier, 1.0f, 481}},
    // A turtle helmet is its own material and has no matching pieces.
    {ItemId::TurtleHelmet, {ToolKind::None, kHandTier, 1.0f, 275}},
    // A bow wears like a tool and mines like nothing, which is exactly what
    // `ToolKind::None` with a durability says. It is deliberately outside
    // `isTool`: that predicate gates mining speed, the harvest tier and an
    // axe's stripping behaviour, none of which a bow should gain.
    {ItemId::Bow, {ToolKind::None, kHandTier, 1.0f, kBowDurability}},
    // Shears are deliberately no faster than a bare hand on a vine - the axe is
    // the quick tool there. What they are is the only thing that collects one.
    //
    // **The 1.0 is the shears' speed on everything `toolSpeedOverride` below
    // does not name.** The reference's 15x on leaves and cobweb and 5x on wool
    // are a per-block override rather than a tier speed, and no row here can
    // say one: a row is a single number for all `kBlockIdCount` blocks there are.
    {ItemId::Shears, {ToolKind::Shears, kHandTier, 1.0f, 238}},
}};

/// **The armour rows LANDED in the table above on 2026-08-19 11:41, and the
/// fall-through no longer covers armour at all.** Until that edit, nothing in
/// `kTools` named a helmet, a chestplate, a pair of leggings or boots, so every
/// armour piece left here with `durability == 0` - which
/// `Inventory::wearArmour` reads as "never wears out", by the explicit wording
/// of `ToolProperties::durability`'s own comment. A full diamond set was
/// permanent free protection for the life of the project (findings 9625, 9660).
///
/// **What made the rows safe to write is that every one of them is the default
/// `ToolProperties` with exactly one field changed.** `ToolKind::None`,
/// `kHandTier` and `1.0f` are precisely what the fall-through returned, so
/// durability is the only behaviour that moved. Give a helmet a real `kind` or
/// `tier` and it collects a mining speed, a harvest tier and an axe's
/// stripping, and a diamond helmet starts opening obsidian.
///
/// **This list carried three blockers and two of them had been retracted for
/// hours without it noticing** - a one-blocker change read as three, and the
/// worst case was a reader "clearing" 2 and 3 by writing a second death-drop
/// and a second `armourWear = 0`, giving one field two writers. **That is why
/// the survivors below are cited by symbol and dated.** One blocker is live:
///
///   1. **`Inventory.hpp` IS FAILING THE BUILD RIGHT NOW, on purpose, and
///      that is the assert doing its job rather than a regression.**
///      It carries a `static_assert` claiming
///      `mining::toolProperties(ItemId::DiamondHelmet).durability == 0`, with
///      `mining::toolProperties(ItemId::DiamondPickaxe).durability == 1561`
///      beside it as the control that proves it reads a live table rather than
///      a stale constant. That is a tripwire, written to fail on exactly this
///      edit. **It is a good assert. The constraint is: when the armour rows
///      land here, that claim must become `== 363` and the pickaxe control
///      must stay.** Do not delete it and do not weaken it to a range.
///      *Cited by symbol on purpose: this comment said "lines 464-473" for six
///      hours and the assert sits at 488-489 today - it drifted +24 while
///      neither file's meaning changed at all.*
///
///   2. **Worn armour IS dropped on death. Verified 2026-08-19 11:16.**
///      `Main.cpp` runs a second loop over `game::kArmourSlots`, calling
///      `inventory.armourAt(...)` and `game::dropStack`, directly after the
///      `inventory.size()` loop - and clears each cell in the same statement
///      that drops it. Finding 9635, landed.
///
///   3. **The banked `armourWear` IS cleared. Verified 2026-08-19 11:16.**
///      `world/Player.cpp` sets `player.armourWear = 0`, in the block whose
///      own comment explains that the killing blow's durability dies with the
///      blow. Finding 9637, landed.
///
/// **So one blocker remains, not three, and it is the tripwire.** That
/// difference is not bookkeeping. The previous wording would have sent a
/// reader to "clear" items 2 and 3 - writing a second armour death-drop and a
/// second `armourWear = 0`. **Two writers for one field, produced by a comment
/// that was entirely true on the day it was written.** A stale blocker list
/// does not merely waste time; it argues for a breaking edit in the confident
/// voice of the file around it.
///
/// **The method that caught it is worth more than the correction.** Items 2
/// and 3 were marked ATTRIBUTED, NOT VERIFIED, dated, and given explicit
/// falsifiers - *"a death-path drop of the armour slots for 2, and a clear of
/// `armourWear` in the respawn path for 3"*. **Both falsifiers fired the first
/// time anyone actually ran them**, four minutes of reading, and two thirds of
/// this blocker list evaporated. The lesson is not "write falsifiers", which
/// this file already did. It is that **an unre-run falsifier is a comment, not
/// a check** - it has to be pointed at source again on a later day than the
/// one it was written on, or it is just a well-phrased assumption.
///
/// **A neighbouring claim is now stale and is NOT mine to fix**: `Player.cpp`,
/// in the very block that clears `armourWear`, still says *"Today death does
/// not drop worn armour"* - which item 2 above disproves. Filed rather than
/// edited, because that file has another owner. **Do not act on it, and do not
/// copy it here.**
///
/// **What falsifies the one remaining blocker:** `Inventory.hpp`'s assert
/// reading `== 363` rather than `== 0`, or `toolProperties(DiamondHelmet)`
/// returning a non-zero durability. Either means the coordinated landing has
/// happened and this whole section should collapse to a single line.
///
/// So this is now a **two-file** landing - `Mining.hpp` and `Inventory.hpp` -
/// and **that owner must be told before the rows land, not after**, because
/// the assert fails the build for every translation unit that includes
/// `Inventory.hpp`, which is most of them.
///
/// Values are ready and recorded in the fx-items findings. **Note the
/// provenance honestly when they land: they are wiki armour-material tables,
/// not primary** - `bedrock-samples` publishes recipes and entities but no
/// armour item definitions - which is unlike the rest of this file.
constexpr ToolProperties toolProperties(ItemId item) {
    for (const ToolRule& rule : kTools) {
        if (rule.item == item) {
            return rule.properties;
        }
    }
    return ToolProperties{};
}

/// The reference's per-block speed overrides - the one thing a `ToolRule` row
/// cannot express, because a row is a single number for every block there is
/// and these five pairings are faster than that row on exactly one block each.
///
/// **Unit: the same `speedMultiplier` `kTools` carries** - a multiple of a bare
/// hand's 1.0, fed straight into `breakTicks` - and `0.0f` means "no override,
/// use the row". Every number below is the reference's own, and each is pinned
/// by a `static_assert` at the foot of this file against a *published time*
/// rather than against the arithmetic that produced it:
///
/// | pairing         | speed | hardness | reference time |
/// |-----------------|-------|----------|----------------|
/// | wool + shears   |     5 |      0.8 | 0.25 s         |
/// | leaves + shears |    15 |      0.2 | instant        |
/// | cobweb + shears |    15 |      4.0 | 0.40 s         |
/// | cobweb + sword  |    15 |      4.0 | 0.40 s         |
/// | bamboo + sword  |    30 |      1.0 | instant        |
///
/// <https://minecraft.wiki/w/Breaking>, <https://minecraft.wiki/w/Bamboo>
///
/// > **A sword is nobody's best tool here** - no row in the mining table names
/// > `ToolKind::Sword`, so before this its speed was never read at all and a
/// > cobweb took 6 s to cut with one where the reference takes 0.4 s. Bamboo
/// > was the same: the reference shears it away in a single tick.
///
/// > The five sit under two kinds rather than in a flat list because that is
/// > the shape that cannot answer for the wrong kind: a shears branch can only
/// > ever hand a number to shears.
constexpr float toolSpeedOverride(BlockId block, ToolKind kind) {
    if (kind == ToolKind::Shears) {
        if (block == BlockId::Cobweb || isLeafBlock(block)) {
            return 15.0f;
        }
        if (isWoolBlock(block)) {
            return 5.0f;
        }
        return 0.0f;
    }
    if (kind == ToolKind::Sword) {
        if (block == BlockId::Cobweb) {
            return 15.0f;
        }
        if (block == BlockId::Bamboo) {
            return 30.0f;
        }
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// The derivations. These are the bodies of the three accessors that used to
// live in `Tool.cpp` - hardness, tool and tier - made `constexpr` and
// corrected, and they are read exactly once each, when the table below is
// built at compile time.
// ---------------------------------------------------------------------------

/// The solid snow cube, which **the enum spells twice**: `BlockId::Snow` is
/// what `snowLayerAt(8)` returns and what six snowy biomes surface with, and
/// `BlockId::SnowBlock` is a second id for the same block in the second table
/// run. Every derivation in this file asks this rather than naming either id,
/// so the two cannot part company again - they were 0.5 and 0.2, and the
/// terrain generator was using the wrong one.
///
/// > Reported to `Block.hpp`'s owner rather than fixed here: the duplicate
/// > enumerator is theirs to collapse. This makes the mining answer one.
constexpr bool isSolidSnow(BlockId id) {
    return id == BlockId::Snow || id == BlockId::SnowBlock;
}

/// The blocks Bedrock **released from their tool gate outright**, in one place
/// because one sentence of the reference names all of them:
///
/// > *"All Copper Doors, Iron Door, Heavy Weighted Pressure Plate, Light
/// > Weighted Pressure Plate, Polished Blackstone Pressure Plate, and Stone
/// > Pressure Plate - the blocks that require support - now always drop when
/// > broken with any tool."*
/// > <https://minecraft.wiki/w/Bedrock_Edition_Preview_1.21.50.24>
///
/// One build earlier said the same of the stone button: *"a stone button is
/// most easily broken with a pickaxe. **It drops itself as an item when broken
/// using any tool**"* (<https://minecraft.wiki/w/Stone_Button>, Preview
/// 1.21.50.20).
///
/// **It is written as three whole families rather than as the six names**, and
/// that is deliberate. Every wooden door, button and plate was already
/// ungated, so "all doors, all buttons, all pressure plates" is the same set
/// the sentence describes and stays the same set when the two members this
/// game does not yet have arrive: there are twelve door families and none is
/// copper, and fourteen plate families and none is polished blackstone, so a
/// list of names would have had two dead entries today and needed editing on
/// the day either one is added. Trapdoors are **not** here - the door and the
/// trapdoor stopped being one answer in that same update, and
/// `{{breaking row|Iron Trapdoor|...|Pickaxe|Wooden}}` still carries its tier.
///
/// **This is why it exists rather than three hand-applied exceptions.** The
/// changelog was cited three times in this file and applied to the stone
/// button, the stone plate and the iron door; the two weighted plates named in
/// the same sentence were left in `requiresToolAtAnyTier`, so punching one
/// destroyed it and took 2.5 s doing it. `everyReleasedBlockComesAwayByHand`
/// below is the assert that would have caught that, and it can only be written
/// because the set has a name.
constexpr bool alwaysDropsToAnyTool(BlockId id) {
    return isDoor(id) || isButton(id) || isPressurePlate(id);
}

/// The blocks that withhold their drop from the **wrong kind** of tool while
/// asking nothing at all of its tier. The reference's own wording is "any
/// pickaxe is required to collect a block", and a tier comparison cannot say
/// it: our lowest pickaxe is `kWoodTier`, so writing wood here would be right
/// today and wrong the moment a gold pickaxe arrives - the reference makes gold
/// the *fastest* tool in the game and the weakest grade there is, level with
/// wood.
///
/// Snow is the same shape of rule from the other end: a shovel or nothing,
/// whatever tier the shovel is - *"the block must be broken with a shovel;
/// otherwise, the breaking time is greater, and the block drops nothing"*
/// (<https://minecraft.wiki/w/Snow_Block>, and the layer says the same). Both
/// snow ids and all seven layer depths are here, which is what makes the hand
/// times the reference's 1.00 s and 0.50 s rather than a third of each.
///
/// **The blocks Bedrock let go of are the one thing this must not name**, which
/// is what `alwaysDropsToAnyTool` above exists to say - a weighted plate was in
/// this list, so a bare fist destroyed it.
///
/// **A cobweb is the third shape**: the reference declares no best tool for it
/// at all, and *still* withholds the drop from a bare hand - shears take the
/// web and a sword cuts it into string, and nothing else gets either. That is
/// the whole reason a cobweb is 20 s by hand rather than 6 s: without a tool it
/// takes the unharvestable divisor.
///
/// **Six ids left this list**, every one of them because the reference hands
/// its drop to a bare fist and this was destroying it outright or slowing it
/// down for nothing:
///
/// * A conduit *"drops as an item when broken with any tool or by hand, but a
///   pickaxe is the fastest"* (<https://minecraft.wiki/w/Conduit>) and packed
///   mud *"drops itself even when broken by hand"*
///   (<https://minecraft.wiki/w/Packed_Mud>). Both were being **destroyed**.
/// * Ice, packed ice and blue ice *"can be easily destroyed without tools, but
///   the use of a pickaxe speeds up the process"* - what withholds those is
///   Silk Touch, which we do not have, so all three drop nothing either way and
///   the gate was pure delay: blue ice was 14.0 s by hand against the
///   reference's 4.2 s. (<https://minecraft.wiki/w/Ice>, `/Packed_Ice`,
///   `/Blue_Ice`.)
/// * A vine is a **loot** rule, not a mining one: *"can be destroyed with any
///   item, but using shears is the only way to collect them"*
///   (<https://minecraft.wiki/w/Vines>). `dropsForBlock` in `BlockDrops.hpp`
///   already enforces exactly that, and on every path - a flow washing a vine
///   off a wall and a blast reach the drop table without ever asking this file.
///   Stating it here as well only made the swing four times slower than the
///   reference's 0.30 s.
///
/// **This is NOT the same question as `MiningRow::toolRequired`, and the two
/// disagree on 851 ids on purpose.** One audit reached the point of writing it
/// up as a bug before catching itself, and it has since been pre-empted by name
/// in an agent's task brief under "do not re-file" - so it has cost review time
/// at least twice. Written here so the next reader stops before it costs a
/// third.
///
/// * `MiningRow::toolRequired` - "does this need a tool of sufficient **tier**,
///   or does it drop nothing?" True for stone, cobblestone, every ore,
///   obsidian: hundreds of ids.
/// * `requiresToolAtAnyTier` (below) - "does this need one specific tool
///   **kind**, whatever its tier?" The seven listed, and no others. A wooden
///   shears is still shears; a netherite pickaxe is still not.
///
/// They are a TIER question and a KIND question, so a disagreement is the
/// expected reading and not a fault. The relationship that *would* be a fault
/// is a reversal, and it has been checked: there is no id where this returns
/// true and `toolRequired` returns false, so this is a strict **subset** of
/// that one. If either name ever changes, keep "tier" and "kind" visible in
/// them - the 851 disagreements are what a sweep sees first, and a name that
/// does not say which question it asks is what made this look like a bug.
/// * `MiningRow::toolRequired` - "does this need a tool of sufficient **tier**,
///   or does it drop nothing?" True for stone, cobblestone, every ore,
///   obsidian: hundreds of ids.
/// * `requiresToolAtAnyTier` (below) - "does this need one specific tool
///   **kind**, whatever its tier?" The seven listed, and no others. A wooden
///   shears is still shears; a netherite pickaxe is still not.
///
/// They are a TIER question and a KIND question, so a disagreement is the
/// expected reading and not a fault. The relationship that *would* be a fault
/// is a reversal, and it has been checked: there is no id where this returns
/// true and `toolRequired` returns false, so this is a strict **subset** of
/// that one. If either name ever changes, keep "tier" and "kind" visible in
/// them - the 851 disagreements are what a sweep sees first, and a name that
/// does not say which question it asks is what made this look like a bug.
constexpr bool requiresToolAtAnyTier(BlockId id) {
    return isEnderChest(id) || isCoralBlock(id) || isSolidSnow(id) || isSnowLayer(id) ||
           id == BlockId::Lantern || id == BlockId::SoulLantern || id == BlockId::Cobweb;
}

/// **Nothing chose this row.** Both derivations return one of these from their
/// final `default:`, and the fifth sweep invariant below fails the build on any
/// row still carrying one.
///
/// This is what makes the headline claim true. "Sized by the enum" only ever
/// guaranteed that a row *exists* for every id - append a run of a hundred rock
/// ids today and every one of them silently took hardness 1.0, no tool, the
/// hand tier and no gate, and **all four of the other invariants passed**,
/// because each is conditioned on a tier, a required tool or a hardness the
/// unchosen row does not have. A sentinel is the difference between an answer
/// nobody wrote and an answer nobody wrote *down*.
///
/// > The single edit that fails the build: delete any one case label from
/// > `derivedHardness`'s final switch, or any one branch of `derivedTool`.
///
/// Negative, so it can never be mistaken for a hardness, and deliberately not
/// `kUnbreakableHardness` - "nobody chose" and "never breaks" are different
/// answers and must not share a value.
constexpr float kUnchosenHardness = -1.0f;

/// The same sentinel for a tool kind. **Not an eighth `ToolKind` enumerator**:
/// the enum genuinely crosses file boundaries - `ToolProperties::kind` comes
/// out of `toolFor` and is switched on in `Main.cpp` and read in
/// `BlockDrops.hpp` - and a value none of those can ever see is a case label
/// every future switch over the enum would have to carry for nothing. It stays
/// inside this file, where it is produced and consumed three lines apart.
///
/// > **The route in this comment was wrong until 2026-08-19 and the conclusion
/// > was right.** It used to say `harvestTool` handed the enum to those two
/// > files. It never did: `harvestTool` had zero callers and has since been
/// > deleted, and `BlockDrops.hpp` says in its own comment that it deliberately
/// > does *not* ask it. That is the dangerous shape - a reader checking the
/// > named route would have found nothing carrying the enum between files and
/// > concluded an eighth enumerator was free. It is not; `toolFor` is the route.
constexpr ToolKind kUnchosenTool = static_cast<ToolKind>(0xFF);

/// Whether a door or trapdoor is the metal one. **One owner**, because the same
/// lookup decides its hardness, its tool and - since this milestone - its tier,
/// and a rule that travels to two of three is this project's most expensive bug
/// shape. It is also why an iron door was hand-harvestable: the tier function
/// had no branch for doors at all.
constexpr bool isMetalDoor(BlockId block) {
    if (isDoor(block)) {
        return kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal;
    }
    if (isTrapdoor(block)) {
        return kTrapdoorFamilies[static_cast<std::size_t>(trapdoorFamily(block))].metal;
    }
    return false;
}

/// The one block the reference lets a **second** kind collect. `MiningRow`
/// names one tool kind, which is the whole truth for every other id in the
/// game; a cobweb has two collectors that hand back different things - shears
/// take the web itself, a sword cuts it into string - so the second one is
/// stated here rather than by widening three thousand rows for one block.
///
/// `BlockDrops.hpp` already knows which of the two yields what; this only says
/// that the swing collects *something*, which is what `canHarvest` decides.
constexpr bool alsoCollects(BlockId block, ToolKind kind) {
    return block == BlockId::Cobweb && kind == ToolKind::Sword;
}

/// The slabs the reference prices **above** the block they were cut from.
///
/// Every other cut shape inherits, and the sweep at the bottom of this file
/// proves it - but `Module:Hardness_values` keys a slab separately from its
/// parent, and for one named group the two numbers differ: the legacy stone
/// slab is a flat 2 whatever it was cut from, so `['stone slab'] = 2` against
/// `['stone'] = 1.5`, `['sandstone slab'] = 2` against `['sandstone'] = 0.8`
/// and `['quartz slab'] = 2` against `['block of quartz'] = 0.8`.
/// <https://minecraft.wiki/w/Module:Hardness_values>
///
/// **Only the ten whose parent actually disagrees are named.** A cobblestone,
/// brick, nether brick, smooth stone, smooth sandstone, smooth quartz or
/// polished blackstone slab is also 2 in that table - and so is its parent, so
/// listing it here would state a number the forwarding already gets right and
/// give it a second owner. `['mossy stone brick slab']` and
/// `['mud brick slab']` are 1.5, level with theirs, and are deliberately absent
/// for the same reason.
///
/// **One owner, because two readers need it**: `derivedHardness` below and the
/// `everyCutShapeCostsItsParent` sweep, which would otherwise fail the build on
/// exactly the ids this corrects.
constexpr bool isLegacyStoneSlab(BlockId block) {
    if (!isSlab(block)) {
        return false;
    }
    switch (shapedParent(block)) {
    case BlockId::Stone:
    case BlockId::StoneBricks:
    case BlockId::Sandstone:
    case BlockId::CutSandstone:
    case BlockId::RedSandstone:
    case BlockId::CutRedSandstone:
    case BlockId::QuartzBlock:
    case BlockId::PurpurBlock:
    case BlockId::Blackstone:
    case BlockId::PolishedBlackstoneBricks:
        return true;
    default:
        return false;
    }
}


constexpr ToolKind derivedTool(BlockId block);

constexpr float derivedHardness(BlockId block) {
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
    // **A torch is `BlockShape::Model`, not `Cross`**, so the plant rule far
    // below never reached one and both the plain and the soul torch cost a
    // second and a half to punch out where the reference gives them nothing at
    // all. The redstone torches are already answered by the branch above; this
    // is what makes the rule travel to the other two.
    if (isTorchBlock(block)) {
        return 0.0f;
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
    //
    // **A carpet is the one cut shape that is not its parent here.** It
    // forwards its tier and its "needs a tool" the way every other shape does,
    // but the reference prices it at 0.1 against wool's 0.8 - so asking it
    // above the forwarding is what stops a carpet costing a whole second to
    // lift.
    //
    // **`MossCarpet` is a separate id outside the carpet run**, and it is here
    // rather than in the fifth run's switch because it is `BlockShape::Flat`:
    // the plant rule below answered it with a flat zero, so the `case
    // BlockId::MossCarpet` that used to sit down there was unreachable. Same
    // shape as the cobweb - a rule inside a run branch that a broader rule
    // above it had already taken.
    if (isCarpet(block) || block == BlockId::MossCarpet) {
        return 0.1f;
    }
    // **A sign, a hanging sign and a banner are stated, not inherited.** All
    // three are cut shapes, so the forwarding below handed a sign its planks'
    // 2 and a banner its wool's 0.8; the reference keys all three at 1 -
    // `['sign'] = 1`, `['hanging sign'] = 1`, `['banner'] = 1`.
    // <https://minecraft.wiki/w/Module:Hardness_values>,
    // <https://minecraft.wiki/w/Sign>, <https://minecraft.wiki/w/Banner>
    if (isSignLike(block)) {
        return 1.0f;
    }
    // The one group of cut shapes the reference prices above its parent - see
    // `isLegacyStoneSlab`, which is also what lets the sweep at the bottom of
    // this file go on proving the other 630.
    if (isLegacyStoneSlab(block)) {
        return 2.0f;
    }
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return derivedHardness(material);
        }
    }
    // ---- The seventh run, plus the two cross-shaped blocks that are not
    // plants. Answered here, above the shape rule and above `isChest`, because
    // each of them is otherwise swallowed by a test written for something else.
    //
    // * An ender chest is `isChest`, so it wore a wooden chest's 2.5 where the
    //   reference gives it 22.5 - the longest break in the game. **The test
    //   goes here rather than inside `isChest`**: that predicate is asked by
    //   the pairing walk, the screen, the drop and the mesher, and widening it
    //   for one exception is the recorded smoker-in-`isFurnace` bug again.
    //   `Explosion.hpp` hoists the same id above the same `isChest` for its
    //   600 blast resistance, so **both files now say the same thing about
    //   what an ender chest is** - obsidian, not a wooden box.
    //   <https://minecraft.wiki/w/Ender_Chest>
    // * **A stowbox is `isChest` too, and wore that same wooden 2.5.** The
    //   reference prices it at 2 (`['shulker box'] = 2`) and - alone in the
    //   container family - hands it a *pickaxe* rather than an axe, which
    //   `derivedTool` now says on this same placement. `Explosion.hpp` already
    //   carries the matching 2 above `isChest`, so this is the mining half of
    //   a pairing that was written on one side only.
    //   <https://minecraft.wiki/w/Module:Hardness_values>
    //   <https://minecraft.wiki/w/Shulker_Box>: *"can be mined with any tool
    //   or by hand, but using a pickaxe is the most effective"*.
    // * A hopper and the cauldron's six filled states were named nowhere at
    //   all and fell to the 1.0 at the bottom of this function.
    // * `isCauldron` covers the empty one in the fifth run as well as the six
    //   filled ones here, so the two states cannot part company again.
    // * A conduit and a cobweb are both `isCrossBlock`, so the plant rule below
    //   answered them with a flat zero and they broke instantly to anything -
    //   the same shape of bug that settled snow already has a test above the
    //   shape rule to avoid.
    if (isEnderChest(block)) {
        return 22.5f;
    }
    if (isStowbox(block)) {
        return 2.0f;
    }
    if (isHopper(block)) {
        return 3.0f;
    }
    if (isCauldron(block)) {
        return 2.0f;
    }
    if (block == BlockId::Conduit) {
        return 3.0f;
    }
    if (block == BlockId::Cobweb) {
        return 4.0f;
    }
    // **Four more `BlockShape::Cross` blocks the plant rule below was pricing
    // at nothing** - and a fifth, flat one just after them - hoisted for
    // exactly the conduit's and the cobweb's reason: a rule written for grass
    // and flowers was answering for rock, wood and a gem. Every one of them
    // broke instantly to a bare fist.
    //
    // Values from the reference's own table, and the two at 1.5 matter most -
    // `BlockDrops.hpp` carefully models an amethyst cluster's four shards to a
    // pickaxe against two to anything else, for a block that cost nothing at
    // all to punch out. <https://minecraft.wiki/w/Module:Hardness_values>
    //
    // **The three buds are the cluster's own row and were left behind by that
    // hoist.** `Module:Hardness_values` keys `['small amethyst bud']`,
    // `['medium amethyst bud']` and `['large amethyst bud']` at 1.5 each, and
    // the `Amethyst Cluster` infobox covers all four growth stages with one
    // `tool = pickaxe`. <https://minecraft.wiki/w/Amethyst_Cluster>
    if (block == BlockId::AmethystCluster || block == BlockId::PointedDripstone ||
        block == BlockId::SmallAmethystBud || block == BlockId::MediumAmethystBud ||
        block == BlockId::LargeAmethystBud) {
        return 1.5f;
    }
    // **Chorus is the seventh and eighth block this rule was pricing at zero**,
    // and it is the same fix as the five above it: both are `BlockShape::Cross`,
    // so the plant rule below shattered a whole chorus tree to a bare fist.
    // `['chorus plant'] = 0.4` and `['chorus flower'] = 0.4`, and *"chorus
    // plants can be broken using any tool, but an axe is the quickest"*.
    // <https://minecraft.wiki/w/Chorus_Plant>,
    // <https://minecraft.wiki/w/Chorus_Flower>
    if (block == BlockId::ChorusPlant || block == BlockId::ChorusFlower) {
        return 0.4f;
    }
    if (block == BlockId::GlowLichen) {
        return 0.2f;
    }
    // **A sculk vein is the fifth**, and the one this pass found rather than
    // was handed: it is `BlockShape::Flat`, so the rule below took it and the
    // `case BlockId::SculkVein: return 0.2f;` inside the fifth run's switch has
    // been dead since it was written. The reference's 0.2 is what that dead
    // label already said. <https://minecraft.wiki/w/Sculk_Vein>
    if (block == BlockId::SculkVein) {
        return 0.2f;
    }
    if (block == BlockId::Bamboo) {
        return 1.0f;
    }
    // **A candle and a sea pickle stopped being cross-shaped** when they gained
    // the reference's own models, so the plant rule below stopped reaching them
    // and both would have fallen to the 1.0 at the bottom of this function -
    // exactly what happened to the torch when it gained one, and the reason it
    // is named above. The reference's own numbers:
    // `['candle'] = 0.1`, `['sea pickle'] = 0`, and `blastResistance` already
    // carries the candle's matching 0.1.
    // <https://minecraft.wiki/w/Module:Hardness_values>
    if (isCandle(block)) {
        return 0.1f;
    }
    if (block == BlockId::SeaPickle) {
        return 0.0f;
    }
    // The reference gives a charge no hardness at all, lit or not.
    if (block == BlockId::Tnt || block == BlockId::TntPrimed) {
        return 0.0f;
    }
    // The solid snow cube, and **one branch for both of the enum's two ids** -
    // see `isSolidSnow`. The reference's 0.2, which is what `BlockId::SnowBlock`
    // already carried in the second run while `BlockId::Snow` - the one the
    // terrain generator actually surfaces six snowy biomes with - sat at 0.5
    // among the sands. <https://minecraft.wiki/w/Module:Hardness_values>
    if (isSolidSnow(block)) {
        return 0.2f;
    }
    // Settled snow is asked **before** the flat-shape rule below, exactly as
    // `derivedTool` does and for the same reason: a layer is dug rather than
    // brushed aside, so it has a real hardness where a carpet has none. Below
    // that rule this branch was unreachable and every layer broke instantly.
    //
    // The reference's own value, and deliberately below the solid snow block's
    // 0.2.
    if (isSnowLayer(block)) {
        return 0.1f;
    }
    // **A big dripleaf is 0.1, and it is asked before the plant rule for the
    // torch's reason.** It is `BlockShape::Cross`, so that rule gave it zero
    // and a bare fist took it apart inside a single tick - no swing, no
    // durability, no cooldown. The reference gives it a real hardness and an
    // axe: `<!-- 0.1 --> {{breaking row|Big Dripleaf|Axe}}`. The small dripleaf
    // beside it genuinely is zero and toolless, which is why only one of the
    // pair is named here and in `derivedTool`.
    // <https://minecraft.wiki/w/Dripleaf>
    if (block == BlockId::BigDripleaf) {
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
        return derivedHardness(stoneOreFor(block)) * 1.5f;
    }
    if (isLeafBlock(block)) {
        return 0.2f;
    }
    // **One owner for coral**, live and dead. The live five sit in the first
    // table run and the dead five in the fifth, so the run-5 branch's own
    // `isCoralBlock` answered half of them and the other half fell to the
    // default - the same block, two prices, because the rule was written inside
    // one run's branch instead of above them all.
    if (isCoralBlock(block)) {
        return 1.5f;
    }
    // A door is the reference's 3, and iron is 5. The leaf is thin but it is
    // still a whole door's worth of timber.
    if (isDoor(block) || isTrapdoor(block)) {
        return isMetalDoor(block) ? 5.0f : 3.0f;
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
        if ((block >= BlockId::WaxedCopperBlock && block <= BlockId::WaxedOxidizedCopperGrate) ||
            isCopperBulb(block)) {
            return 3.0f;
        }
        switch (block) {
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock:
        case BlockId::MushroomStem:
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
        // The empty cauldron is deliberately **not** a case label here any
        // more: `isCauldron` above answers all seven fill states at once.
        case BlockId::Grindstone:
        case BlockId::Campfire:
        case BlockId::SoulCampfire:
            return 2.0f;
        case BlockId::SculkSensor:
            return 1.5f;
        case BlockId::SculkShrieker:
            return 3.0f;
        default:
            break;
        }
        // **Falls through to the switch below rather than answering 1.0 here**,
        // for the reason the sixth run's branch already gives: a per-run
        // catch-all that returns a real value is a second place for the
        // identical bug, and this one was hiding three ids - a brewing stand, a
        // scaffold and a flower pot were all priced like a pumpkin.
    }
    // The third run: glass and its panes are the reference's 0.3, iron bars and
    // the lanterns are metal, and a torch comes away in a touch.
    if (block == BlockId::IronBars) {
        return 5.0f;
    }
    if (block == BlockId::Lantern || block == BlockId::SoulLantern) {
        return 3.5f;
    }
    // **A ladder is 0.4 and an end rod is 0**, and they shared a branch where
    // only the ladder's number was right. The reference breaks a rod instantly,
    // like the torch it is shaped after.
    // <https://minecraft.wiki/w/Module:Hardness_values>
    if (isLadder(block)) {
        return 0.4f;
    }
    if (block == BlockId::EndRod) {
        return 0.0f;
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
    // The sixth run, which nothing named: every id in it fell to the 1.0 at the
    // bottom of the switch, so a beacon, a spawner and an end portal frame were
    // all priced like a pumpkin.
    //
    // **The bounds are the ones `blastResistance` in `Explosion.hpp` tests**, so
    // where a run begins and ends has one owner, and this branch sits *after*
    // the furnace and chest tests above for the same reason that function asks
    // them first: the trapped chest and the blast furnace's other seven states
    // live in this run and are already answered by their families.
    //
    // The candles were cross-shaped and were answered by the plant rule further
    // up, so they were deliberately not named here. **They are models now** and
    // are named above the plant rule instead, at their reference 0.1, so a case
    // label for one down here would still be dead code. The conduit is
    // cross-shaped too and is the exception: it is answered above the shape
    // rule, not here.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        switch (block) {
        // The reference makes it unbreakable; an absurd hardness is how this
        // table says that, exactly as it does for bedrock below.
        case BlockId::EndPortalFrame:
            return kUnbreakableHardness;
        case BlockId::MonsterSpawner:
            return 5.0f;
        // Two at the reference's 3. The egg's blast resistance is 9 rather than
        // its hardness, and `blastResistance` already says so; only this number
        // is shared. The conduit is the third at 3 there and is answered above.
        case BlockId::Beacon:
        case BlockId::DragonEgg:
            return 3.0f;
        case BlockId::TintedGlass:
            return 0.3f;
        default:
            break;
        }
        // **Falls through to the switch below rather than answering 1.0 here.**
        // A second copy of the catch-all is a second place for a block added to
        // this run to be quietly mispriced, which is the whole bug above.
    }
    // ---- The coloured families, by range rather than by eighty case labels.
    // Every one of these fell to the old 1.0 default, which is why a carpet
    // cost a second and a half and a block of concrete cost the same as a
    // pumpkin. Values from the reference's own hardness table, which is what
    // generates its published break times: planks 2, wool 0.8, concrete 1.8,
    // concrete powder 0.5, dyed terracotta 1.25.
    // <https://minecraft.wiki/w/Module:Hardness_values>
    if (isPlanksBlock(block)) {
        return 2.0f;
    }
    if (isWoolBlock(block)) {
        return 0.8f;
    }
    if (block >= BlockId::WhiteConcrete && block <= BlockId::BlackConcrete) {
        return 1.8f;
    }
    if (isConcretePowder(block)) {
        return 0.5f;
    }
    if (block >= BlockId::WhiteTerracotta && block <= BlockId::BlackTerracotta) {
        return 1.25f;
    }
    switch (block) {
    // The second table run. Grouped by the reference's own values rather than
    // listed one per line.
    case BlockId::Netherrack:
        return 0.4f;
    case BlockId::SoulSand:
    case BlockId::SoulSoil:
    case BlockId::Podzol:
        return 0.5f;
    // **Mycelium is 0.6, not the 0.5 it shared with podzol**, and it is the one
    // of the four the reference prices with the grass block rather than with
    // the dirt. <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::Mycelium:
        return 0.6f;
    // Honey and slime have **no hardness at all** in the reference - both are
    // gone in a single hit, however you touch them - and were sharing a moss
    // block's 0.1. Dried kelp is 0.5 and a hoe block, which is a different
    // table row from the slime it was grouped with.
    case BlockId::SlimeBlock:
        return 0.0f;
    case BlockId::DriedKelpBlock:
        return 0.5f;
    case BlockId::NetherWartBlock:
        return 1.0f;
    case BlockId::WarpedWartBlock:
    case BlockId::Shroomlight:
        return 1.0f;
    // **A target's 0.5 is answered by `isTarget` at the top of this function**,
    // so the `case BlockId::Target` that used to sit beside the cactus here was
    // both unreachable and the wrong number - the reference's 0.5, not 0.4.
    // <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::Cactus:
        return 0.4f;
    // The second run's `BlockId::SnowBlock` is gone from this switch as well:
    // `isSolidSnow` above answers it and `BlockId::Snow` together.
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
    case BlockId::BoneBlock:
        return 2.0f;
    // **Muddy mangrove roots are 0.7 and a shovel block**, not the 2.0 and the
    // axe they were filed under with the mangrove timber they grow among: the
    // reference calls them *"a decorative variant of mangrove roots with
    // dirt-like properties"* and gives them the highest hardness of anything a
    // shovel suits. <https://minecraft.wiki/w/Muddy_Mangrove_Roots>
    case BlockId::MuddyMangroveRoots:
        return 0.7f;
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
    // **Twenty-one ids shared one `return 2.0f;` and fourteen of them were
    // wrong.** Every value below is the reference's own, and the point is not
    // the numbers one at a time: this file already prices `Tuff` at 1.5 and
    // `CobbledDeepslate`, `DeepslateBricks` and `DeepslateTiles` at 3.5 twenty
    // lines from here, so the same family was being sold at two prices - and
    // every stair, slab and wall cut from one of them inherited the wrong one
    // through `shapedParent`. <https://minecraft.wiki/w/Module:Hardness_values>
    //
    // The seven that stay at 2.0 are stated by name below rather than left in
    // place, because "which of these was already right" is the question a
    // reader of a twenty-one-line case label cannot answer.
    case BlockId::ChiseledDeepslate:
    case BlockId::CrackedDeepslateBricks:
    case BlockId::CrackedDeepslateTiles:
        return 3.5f;
    case BlockId::EndStone:
    case BlockId::EndStoneBricks:
        return 3.0f;
    case BlockId::Blackstone:
    case BlockId::PolishedBlackstoneBricks:
    case BlockId::ChiseledPolishedBlackstone:
    case BlockId::CrackedPolishedBlackstoneBricks:
    case BlockId::GildedBlackstone:
    case BlockId::PolishedTuff:
    case BlockId::TuffBricks:
    case BlockId::ChiseledTuff:
    case BlockId::PurpurBlock:
        return 1.5f;
    // Basalt's three cuts are one price in the reference, and this was the only
    // one of them not carrying it - `Basalt` and `SmoothBasalt` are already
    // 1.25 further down.
    case BlockId::PolishedBasalt:
        return 1.25f;
    // The seven the group had right. **Polished blackstone is 2.0 while its
    // bricks are 1.5**, which reads like a typo and is not: the reference
    // splits them, and grouping the two is how the whole family drifted.
    case BlockId::PolishedBlackstone:
    case BlockId::NetherBricks:
    case BlockId::RedNetherBricks:
    case BlockId::CrackedNetherBricks:
    case BlockId::ChiseledNetherBricks:
    case BlockId::SmoothRedSandstone:
        return 2.0f;
    case BlockId::NetherGoldOre:
    case BlockId::NetherQuartzOre:
        return 3.0f;
    case BlockId::QuartzBlock:
    case BlockId::ChiseledQuartz:
    case BlockId::QuartzBricks:
        return 0.8f;
    // **Smooth quartz is 2.0, not the 0.8 the other three cuts share** - the
    // same one-cut-parts-company shape as smooth sandstone below, and from the
    // same table. <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::SmoothQuartz:
        return 2.0f;
    // The three raw blocks are 5.0, level with iron, diamond and emerald -
    // not the 3.0 of the copper family they were sitting in.
    case BlockId::RawIronBlock:
    case BlockId::RawGoldBlock:
    case BlockId::RawCopperBlock:
        return 5.0f;
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
        return 0.5f;
    // **Dirt is 0.5 and a grass block is 0.6**, and gravel goes with the grass
    // block rather than with the sand it looks like (research S1.6). The old
    // table had dirt sharing grass's 0.6 and gravel sharing sand's 0.5, so a
    // wooden shovel took 9 ticks on dirt where the reference publishes 8.
    case BlockId::Dirt:
        return 0.5f;
    case BlockId::Gravel:
    case BlockId::Grass:
        return 0.6f;
    case BlockId::Leaves:
        return 0.2f;
    case BlockId::Planks:
        return 2.0f;
    // **A crafting table is 2.5, not the planks' 2.0** - the reference prices
    // it with the other worked wooden utilities rather than with the boards it
    // is made of. <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::CraftingTable:
        return 2.5f;
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
        return 0.5f;
    // **Blue ice is not ice.** It sat in the same case label and took ice's
    // 0.5, where the reference gives it 2.8 - it is the block you make by
    // compressing packed ice twice, and it is meant to be slow.
    case BlockId::BlueIce:
        return 2.8f;
    // **The reference's own split**, which one case label of six was hiding:
    // iron, diamond and emerald are 5 and gold, lapis and copper are 3. Same
    // table as everything else here.
    case BlockId::IronBlock:
    case BlockId::DiamondBlock:
    case BlockId::EmeraldBlock:
        return 5.0f;
    case BlockId::GoldBlock:
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
    //
    // **Honey has no hardness at all in the reference** and was borrowing moss's
    // 0.1: it is gone in a single hit however you touch it, as is the slime
    // block above. <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::HoneyBlock:
        return 0.0f;
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
        // The reference's own 0.75, not the 0.8 it was grouped with. Calcite is
        // the softest of the three geode shells and is priced below sandstone.
        return 0.75f;
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
    // The cobweb's 4.0 used to sit here and was dead code, because the plant
    // rule above answered it first. It is now asked with the conduit, above.
    //
    // ---- The rest of the first table run, which nothing named at all. ----
    // Twenty-one rock ids, and the sweep could not see them because the row a
    // fall-through builds is a perfectly legal row: hardness 1.0, and the
    // pickaxe and the wooden tier they *did* get from the other two derivations
    // made it look chosen. Every value is the reference's own.
    // <https://minecraft.wiki/w/Module:Hardness_values>
    case BlockId::CobbledDeepslate:
    case BlockId::PolishedDeepslate:
    case BlockId::DeepslateBricks:
    case BlockId::DeepslateTiles:
        return 3.5f;
    case BlockId::PolishedAndesite:
    case BlockId::PolishedDiorite:
    case BlockId::PolishedGranite:
    case BlockId::ChiseledStoneBricks:
    case BlockId::MossyStoneBricks:
    case BlockId::CrackedStoneBricks:
    case BlockId::DarkPrismarine:
    case BlockId::PrismarineBricks:
        return 1.5f;
    // **Smooth sandstone is 2.0, not the 0.8 the other two cuts share.** It is
    // the one place the family parts company: Bedrock raised it from 0.8 to 2
    // in 1.21.20 to match the blast resistance it had always had.
    case BlockId::SmoothSandstone:
        return 2.0f;
    case BlockId::CutSandstone:
    case BlockId::ChiseledSandstone:
        return 0.8f;
    // The three the fifth run's own catch-all was hiding. A brewing stand is
    // the reference's 0.5 and comes away in any hand; a scaffold and a flower
    // pot are broken instantly.
    case BlockId::BrewingStand:
        return 0.5f;
    case BlockId::Scaffolding:
    case BlockId::FlowerPot:
        return 0.0f;
    case BlockId::Bedrock:
        // The reference's own value for "never". Nothing here treats a block as
        // unbreakable, so an absurd hardness is what enforces it, and creative
        // still ignores it - which is correct, it is a builder's tool.
        return kUnbreakableHardness;
    default:
        // **Not 1.0.** See `kUnchosenHardness`: a plausible number here is what
        // let a whole table run be appended and priced like a pumpkin while
        // every assert in the file went on passing.
        return kUnchosenHardness;
    }
}

constexpr ToolKind derivedTool(BlockId block) {
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
    //
    // **A carpet is again the exception**, and again on the reference's own
    // wording: wool is sheared and a carpet is not, so a carpet inheriting
    // wool's shears would be one derivation reaching past a stated rule.
    // `MossCarpet` joins it here for the reason `derivedHardness` gives - it is
    // `BlockShape::Flat`, so the plant rule below reached it first and the case
    // label in the fifth run's switch was dead.
    if (isCarpet(block)) {
        return ToolKind::None;
    }
    if (block == BlockId::MossCarpet) {
        return ToolKind::Hoe;
    }
    // **A target is hay and redstone, so a hoe is its tool** - and it has to be
    // asked here rather than beside the other odd hoe jobs at the bottom,
    // because its id sits in the second run whose fall-through is a pickaxe.
    // It used to carry a `case BlockId::Target: return None` inside that run's
    // switch, which was a third answer again: `isRedstoneComponent` already
    // gives it the hand tier, so the kind was only ever about speed.
    if (isTarget(block)) {
        return ToolKind::Hoe;
    }
    // **A banner is an axe block, not the shears it inherited from wool**, and
    // a sign and a hanging sign are axe blocks that only *happened* to be right
    // because planks are. Stating all three here is what stops the banner
    // answer depending on which material the family names:
    // *"banners can be broken with or without a tool, but an axe is fastest"*,
    // and Bedrock's `is_axe_item_destructible` tag carries `standing_banner`,
    // `wall_banner`, every `*_standing_sign`, `*_wall_sign` and
    // `*_hanging_sign`. <https://minecraft.wiki/w/Banner>,
    // <https://minecraft.wiki/w/Sign>,
    // <https://minecraft.wiki/w/Block_tag_(Bedrock_Edition)>
    if (isSignLike(block)) {
        return ToolKind::Axe;
    }
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return derivedTool(material);
        }
    }
    // ---- The seventh run and the conduit, on exactly the placement
    // `derivedHardness` uses and for the same three reasons: an ender chest is
    // swallowed by `isChest` below, a hopper and the six filled cauldrons were
    // named nowhere at all, and a conduit is cross-shaped so the plant rule
    // below answered it with no tool. All five are the reference's pickaxe.
    //
    // **The stowbox is the fifth and the least obvious.** Every other member of
    // `isChest` is a wooden box and takes the axe below; a shulker box is the
    // one the reference breaks fastest with a pickaxe, so the family answer was
    // wrong for it in kind as well as in hardness. It asks no tier and no tool
    // for its drop - *"can be mined with any tool or by hand... all shulker
    // boxes drop themselves"* - which is why `derivedTier` names it too.
    // <https://minecraft.wiki/w/Shulker_Box>
    if (isEnderChest(block) || isStowbox(block) || isHopper(block) || isCauldron(block) ||
        block == BlockId::Conduit) {
        return ToolKind::Pickaxe;
    }
    // **A cobweb, on the same placement and for the same reason**: it is
    // cross-shaped, so the plant rule below answered it with no tool, which -
    // with `requiresToolAtAnyTier` naming it - would be a gate nothing can
    // open. Shears are the kind, because in the reference they are the only
    // thing that hands back the web itself; `alsoCollects` carries the sword,
    // which cuts it into string instead.
    //
    // > `toolSpeedOverride` is what pays the reference's 15x for both shears
    // > and a sword here, since neither could come from a row: shears carry
    // > 1.0 and no block in the game names a sword as its kind. 0.40 s with
    // > either, and the 20 s by hand is exact - that is the unharvestable
    // > divisor doing the work.
    if (block == BlockId::Cobweb) {
        return ToolKind::Shears;
    }
    // **Four more cross-shaped blocks on the same placement and for the same
    // reason** - see the matching hoist in `derivedHardness`. Amethyst and
    // dripstone are *"mined with any tool, but a pickaxe is the quickest"*;
    // glow lichen names an axe (shears do the collecting, which `BlockDrops.hpp`
    // owns); bamboo names a sword first and an axe second, and an axe is the
    // one of those two we can express.
    // <https://minecraft.wiki/w/Amethyst_Cluster>,
    // <https://minecraft.wiki/w/Pointed_Dripstone>,
    // <https://minecraft.wiki/w/Glow_Lichen>, <https://minecraft.wiki/w/Bamboo>
    //
    // > The axe is what this row can say and `toolSpeedOverride` says the
    // > other half: the reference lets a sword shear bamboo away in a single
    // > tick, which is a per-block number no row here could carry.
    if (block == BlockId::AmethystCluster || block == BlockId::PointedDripstone ||
        block == BlockId::SmallAmethystBud || block == BlockId::MediumAmethystBud ||
        block == BlockId::LargeAmethystBud) {
        return ToolKind::Pickaxe;
    }
    if (block == BlockId::GlowLichen || block == BlockId::Bamboo) {
        return ToolKind::Axe;
    }
    // **Chorus, on the same placement and for the same reason** - see the
    // matching hoist in `derivedHardness`. Both pages name an axe and Bedrock's
    // `is_axe_item_destructible` tag carries `chorus_flower` and `chorus_plant`.
    // <https://minecraft.wiki/w/Chorus_Plant>,
    // <https://minecraft.wiki/w/Chorus_Flower>
    if (block == BlockId::ChorusPlant || block == BlockId::ChorusFlower) {
        return ToolKind::Axe;
    }
    // **A sculk vein is the fifth block the plant rule was swallowing**, found
    // by the sculk-family assert at the bottom of this file rather than by
    // reading: it is `BlockShape::Flat`, so the rule below answered "no tool"
    // and its `case BlockId::SculkVein` inside the fifth run's switch has been
    // dead since the day it was written. *"Sculk veins can be mined with any
    // tool, but hoes are the quickest."*
    // <https://minecraft.wiki/w/Sculk_Vein>
    if (block == BlockId::SculkVein) {
        return ToolKind::Hoe;
    }
    // The solid snow cube, on the same placement as the layer below and for the
    // same reason - **one branch for both of the enum's ids**, see
    // `isSolidSnow`. The second run's switch had `BlockId::SnowBlock` and the
    // bottom switch had `BlockId::Snow`, which is exactly the split that let
    // their hardnesses drift apart.
    if (isSolidSnow(block)) {
        return ToolKind::Shovel;
    }
    // Settled snow is asked **before** the flat-shape rule below, which answers
    // "no tool at all" for carpets and lily pads. Snow is the one flat block
    // that is dug rather than picked up, and a shovel is what digs it.
    if (isSnowLayer(block)) {
        return ToolKind::Shovel;
    }
    // **The torch's fault, twice more over, and it must sit here rather than at
    // the bottom of this function.** A candle and a sea pickle each gained the
    // reference's own model this pass, so they stopped being `BlockShape::Cross`
    // and the rule below stopped reaching them - and the run branches
    // underneath answer a pickaxe by default, which is what a sea pickle was
    // given: a wood tier on a block that breaks instantly, the exact pairing
    // the second sweep invariant fails the build on. `None` is the answer the
    // rule below gave both, and the reference agrees that nothing in the game
    // is faster than a fist at either.
    //
    // > **Bamboo was named here too and could never reach it** - the amethyst
    // > hoist thirty lines above already answers it `Axe`, which is the kind
    // > the reference names. A third id in a list that only two ids can enter
    // > is dead code that reads as a rule, and this one contradicted a live
    // > rule in the same function.
    if (isCandle(block) || block == BlockId::SeaPickle) {
        return ToolKind::None;
    }
    // **A big dripleaf is 0.1 and an axe, and it is asked here for the torch's
    // reason as well**: it is `BlockShape::Cross`, so the plant rule below
    // answered "no tool" and `derivedHardness`'s matching rule answered zero,
    // which made it the one leaf-sized block in the game that a bare fist took
    // apart inside a single tick. The reference gives it a real hardness and an
    // axe - `<!-- 0.1 --> {{breaking row|Big Dripleaf|Axe}}` - and the small
    // one genuinely is instant and toolless, which is why only one of the pair
    // is named. <https://minecraft.wiki/w/Dripleaf>
    if (block == BlockId::BigDripleaf) {
        return ToolKind::Axe;
    }
    // A plant needs no tool, whatever run its id happens to sit in. Without
    // this the second run's pickaxe default reached every plant in it, and
    // seventeen of them broke instantly and dropped nothing.
    if (blockShape(block) == BlockShape::Cross || blockShape(block) == BlockShape::Flat) {
        return ToolKind::None;
    }
    // **A leaf is that same plant rule for foliage that happens to be a full
    // cube.** `blockShape` is `Cube` for leaves, so the test above misses them,
    // and nine of the ten escaped only because they sit outside every run
    // branch and reach the `None` default at the bottom. Mangrove leaves do
    // not: their id landed in the second run, whose fall-through is a pickaxe,
    // so one leaf in ten asked for a wooden pickaxe. Asking the narrow family
    // before the broad default is the same shape as the smoker fix below, and
    // `derivedHardness` already asks `isLeafBlock` in the same place.
    //
    // **A hoe, not "no tool at all".** Every leaf id is in Bedrock's
    // `is_hoe_item_destructible` tag and the `Leaves` page states it outright:
    // *"hoes are the default tools for breaking leaves, but leaves can be
    // obtained only with shears or tools enchanted with Silk Touch"*. Answering
    // `None` here made every item on earth mine a leaf at a bare hand's speed,
    // because `breakTicksFor` only grants a multiplier when the row names a
    // kind. <https://minecraft.wiki/w/Leaves>,
    // <https://minecraft.wiki/w/Block_tag_(Bedrock_Edition)>
    //
    // > `toolSpeedOverride` carries the other half no row can: the reference
    // > gives **shears** 15x on a leaf, which at 0.2 hardness is instant, and
    // > that is a per-block `(block, kind)` number rather than a tier speed.
    // > The kind this row names is the one the *tool table* can pay out. The
    // > drop is unaffected either way: `leafDrop` in `BlockDrops.hpp` keys off
    // > the shears the *player* held, not off this.
    if (isLeafBlock(block)) {
        return ToolKind::Hoe;
    }
    // **A sea lantern is glass, not rock.** It sat in the pickaxe case list at
    // the bottom of this function beside prismarine, which is where a diver
    // would put it, but the reference files it with the blocks no tool helps
    // at all - beside glass, glowstone, tinted glass, a redstone lamp and the
    // three froglights, every one of which already answers `None` here. Its
    // own page says so outright: *"a sea lantern can be mined with any tool,
    // or without a tool"*, and the breaking table gives it one time and no
    // tool column. At 0.3 hardness the pickaxe was the difference between the
    // reference's 0.45 s and instant, and it named a tool for a block that has
    // none - which is the shape `requiresToolAtAnyTier` reads.
    // <https://minecraft.wiki/w/Sea_Lantern>
    if (block == BlockId::SeaLantern) {
        return ToolKind::None;
    }
    // One owner for coral, live and dead, exactly as the hardness has: the
    // fifth run's branch answered the dead five and the live five fell through
    // to a case label of their own in the switch at the bottom.
    if (isCoralBlock(block)) {
        return ToolKind::Pickaxe;
    }
    if (isLogBlock(block) || isBeehive(block)) {
        return ToolKind::Axe;
    }
    // A smoker and a furnace are one answer, and the exception that used to sit
    // here was wrong. `isSmoker` returned `ToolKind::Axe` on the reasoning that
    // *"a smoker is a wooden block"*; the reference says the opposite - *"a
    // smoker can be mined and obtained using any pickaxe. If mined without a
    // pickaxe, it does not drop itself"*, and Bedrock 1.21.50 (Preview
    // 1.21.50.24) added exactly that gate: *"smokers drop themselves only if
    // mined using a pickaxe, matching Java Edition"*. Its 3.5 hardness comes
    // from `isFurnace` above and was always right.
    // <https://minecraft.wiki/w/Smoker>
    if (isFurnace(block)) {
        return ToolKind::Pickaxe;
    }
    if (isChest(block)) {
        return ToolKind::Axe;
    }
    if (isDoor(block) || isTrapdoor(block)) {
        return isMetalDoor(block) ? ToolKind::Pickaxe : ToolKind::Axe;
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
        case BlockId::SuspiciousSand:
        case BlockId::SuspiciousGravel:
            return ToolKind::Shovel;
        // **Powder snow left that shovel label**, because the reference names
        // no tool for it at all: *"no tool can accelerate the process of
        // breaking powder snow"* (and it is in none of Bedrock's
        // `is_*_item_destructible` tags). The infobox's `tool = Bucket` is how
        // you *obtain* it, not what mines it faster.
        // <https://minecraft.wiki/w/Powder_Snow>
        //
        // **The redstone lamp left the pickaxe group** for the same reason:
        // *"a redstone lamp can be mined with any tool or by hand"*, its
        // breaking row carries no tool argument at all, and the wiki files the
        // omission as MC-192719 rather than as an oversight of its own.
        // <https://minecraft.wiki/w/Redstone_Lamp>
        //
        // Both are named rather than deleted, because this run falls through
        // to a sentinel and an unnamed id fails the build.
        case BlockId::PowderSnow:
        case BlockId::RedstoneLamp:
        case BlockId::RedstoneLampLit:
            return ToolKind::None;
        case BlockId::SculkSensor:
        case BlockId::SculkShrieker:
            return ToolKind::Hoe;
        // The empty cauldron is answered with its six filled states above and
        // is deliberately not a case label here any more.
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
            return ToolKind::Pickaxe;
        case BlockId::BrewingStand:
            return ToolKind::Pickaxe;
        default:
            break;
        }
        // Falls through, for the same reason the hardness does: a scaffold and
        // a flower pot want no tool and they should say so by name, not by
        // being the things a catch-all happened to cover.
    }
    // The sixth run: glass wants nothing, the rest want a pickaxe.
    // The sixth run, where a pickaxe is right for the spawner, the conduit, the
    // end portal frame and the eight blast furnace states - and wrong for three
    // ids, of which this branch named one. **`derivedTier` has said so in prose
    // since the run was written**: *"the reference asks a pickaxe only of the
    // spawner. A beacon and a dragon egg both come away by hand there"* - and
    // it acts on that, handing both `kHandTier`. The kind did not travel the
    // twenty lines to here, so both named a tool the reference does not:
    // *"a beacon can be mined successfully by hand or with any tool"*, and the
    // dragon egg's breaking row carries no tool column at all. Speed only, at
    // 3.0 hardness - 4.5 s by hand either way - but a named kind is what
    // `requiresToolAtAnyTier` and every "which tool for this?" reader sees.
    // <https://minecraft.wiki/w/Beacon>, <https://minecraft.wiki/w/Dragon_Egg>
    //
    // > A `switch` rather than a chain of `?:` so that the fourth exception is
    // > one line, and the catch-all stays outside it: this branch is the run's
    // > only answer, and CLAUDE.md's tenth bug shape is about a *second* copy
    // > of a rule, not about a run having one.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        switch (block) {
        case BlockId::TintedGlass:
        case BlockId::Beacon:
        case BlockId::DragonEgg:
        // **The end portal frame took this run's pickaxe fall-through**, and it
        // is the same shape as reinforced deepslate below: the reference's
        // infobox says `Tool: None` outright, because there is no tool - the
        // block's hardness is -1 and it never breaks at all. Naming a pickaxe
        // was not merely cosmetic here. `kUnbreakableHardness` is only a very
        // large number, so a kind that speeds it up *divides* it: the best
        // pickaxe in the game finished a frame in 12,000 ticks (10 minutes)
        // where bedrock beside it takes 108,000 (90 minutes), and a stronghold
        // has twelve of them. With no kind, nothing in the game is faster than
        // a fist on it and the two unbreakable blocks answer alike.
        // <https://minecraft.wiki/w/End_Portal_Frame>
        case BlockId::EndPortalFrame:
            return ToolKind::None;
        default:
            break;
        }
        return ToolKind::Pickaxe;
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
    // **A vine names an axe here, not the shears that collect it.** The two
    // questions are different and this field answers only the first: `kTools`
    // gives shears 1.0, so writing `Shears` bought no speed at all *and* took
    // the axe's multiplier away, leaving 6 ticks with everything in the game.
    // The reference is explicit both ways - *"vines can be destroyed with any
    // item, but using shears is the only way to collect them... using an axe
    // on vines can also increase efficiency, but does not allow for
    // collection"*, and its breaking row is `{{breaking row|Vines|Axe|drop=0
    // |shears=1|sword=0}}`. The row two hundred lines above this one already
    // said as much in prose - *"shears are deliberately no faster than a bare
    // hand on a vine - the axe is the quick tool there"* - while the code said
    // the opposite. Collection is `shearsHarvests` in `BlockDrops.hpp`, which
    // reads the player's held tool and never this.
    // <https://minecraft.wiki/w/Vines>
    if (isVine(block)) {
        return ToolKind::Axe;
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
        case BlockId::MuddyMangroveRoots:
            return ToolKind::Shovel;
        // **The three froglights, which had no case label at all** and so fell
        // through to this run's pickaxe - which, with the run's "a pickaxe
        // block gates at wood" tier rule, meant a bare fist **destroyed** them.
        // The reference declares no best tool: *"breaking speed is always the
        // same... no specific tool can break a froglight faster"*, and it
        // always drops itself. <https://minecraft.wiki/w/Froglight>
        case BlockId::OchreFroglight:
        case BlockId::VerdantFroglight:
        case BlockId::PearlescentFroglight:
        case BlockId::SlimeBlock:
        case BlockId::Cactus:
        case BlockId::Azalea:
        case BlockId::FloweringAzalea:
            return ToolKind::None;
        // **Reinforced deepslate had no case label**, so it took this run's
        // pickaxe fall-through and - through `pickaxeRunTier` - an iron gate,
        // for a block the reference states has none of either: *"reinforced
        // deepslate has no tool associated with it"*, `{{breaking row
        // |Reinforced Deepslate|drop=0}}`, infobox `tool = None`. Naming a tool
        // and a tier for something that never drops is three wrong answers, and
        // `BlockDrops.hpp` removes the third.
        // <https://minecraft.wiki/w/Reinforced_Deepslate>
        case BlockId::ReinforcedDeepslate:
            return ToolKind::None;
        // **A hoe is the sculk family's tool, and only three of the five said
        // so.** `SculkVein`, `SculkSensor` and `SculkShrieker` sit in the fifth
        // run and already answer `Hoe`; these two sit here and answered
        // "nothing", so the rule did not travel across the run boundary. Both
        // pages name a hoe and neither gates its drop.
        // <https://minecraft.wiki/w/Sculk>, `/Sculk_Catalyst`
        //
        // Dried kelp is the same kind for a different reason - the reference
        // makes it a hoe block outright, and it was grouped with the slime it
        // is nothing like. <https://minecraft.wiki/w/Dried_Kelp_Block>
        //
        // **And the three wart blocks are that same rule, one group up.** They
        // sat in the `None` list directly above while the comment for this
        // group already spelled the rule out - which is how a rule that exists
        // in the file still fails to travel. Bedrock's
        // `is_hoe_item_destructible` tag names `nether_wart_block`,
        // `warped_wart_block` and `shroomlight` outright, and each page's
        // infobox says `tool = hoe`. With `None` they took 5 s to punch out
        // where a hoe should make it 0.75 s; none of them gates its drop, so
        // this is speed only. <https://minecraft.wiki/w/Nether_Wart_Block>,
        // <https://minecraft.wiki/w/Shroomlight>,
        // <https://minecraft.wiki/w/Block_tag_(Bedrock_Edition)>
        //
        // > Swept the whole tag against this file afterwards. Its other 26
        // > entries are the eleven leaves (answered by `isLeafBlock` above),
        // > moss and pale moss and their two carpets, sponge and wet sponge,
        // > hay, target, and the six sculk ids - and every one of those already
        // > answered `Hoe`. These three and the leaves were the only misses.
        case BlockId::NetherWartBlock:
        case BlockId::WarpedWartBlock:
        case BlockId::Shroomlight:
        case BlockId::Sculk:
        case BlockId::SculkCatalyst:
        case BlockId::DriedKelpBlock:
            return ToolKind::Hoe;
        case BlockId::CrimsonPlanks:
        case BlockId::WarpedPlanks:
        case BlockId::MangrovePlanks:
        case BlockId::MangroveLog:
        case BlockId::BambooPlanks:
        case BlockId::BambooMosaic:
        case BlockId::BambooBlock:
        case BlockId::CrimsonStem:
        case BlockId::WarpedStem:
            return ToolKind::Axe;
        default:
            break;
        }
        return isConcretePowder(block) ? ToolKind::Shovel : ToolKind::Pickaxe;
    }
    // The first table run, which had no branch here at all. Forty-four of its
    // ids are rock the switch below never named, so they answered "no tool" -
    // and because the tier rule for a run is "a pickaxe block gates at wood",
    // that one gap is what left the whole 1.17 cave palette, all sixteen
    // concretes and all sixteen coloured terracottas droppable to a bare fist.
    //
    // **Falls through to the switch below rather than carrying a catch-all**,
    // for the reason the sixth run's branch in `derivedHardness` gives: a
    // second copy of the default is a second place for a new id to go wrong.
    if (block >= kFirstExtraBlock && block <= kLastExtraBlock) {
        if ((block >= BlockId::WhiteConcrete && block <= BlockId::BlackConcrete) ||
            (block >= BlockId::WhiteTerracotta && block <= BlockId::BlackTerracotta)) {
            return ToolKind::Pickaxe;
        }
        switch (block) {
        case BlockId::Tuff:
        case BlockId::Calcite:
        case BlockId::DripstoneBlock:
        case BlockId::PackedMud:
        case BlockId::MudBricks:
        case BlockId::AmethystBlock:
        case BlockId::SmoothBasalt:
        case BlockId::Basalt:
        case BlockId::MagmaBlock:
        case BlockId::RedSandstone:
        case BlockId::CutRedSandstone:
        case BlockId::ChiseledRedSandstone:
            return ToolKind::Pickaxe;
        default:
            break;
        }
    }
    // ---- The families the switch below never named. ----
    // Every one of these answered "no tool" by falling off the end, which for
    // the twenty-two that want a real kind is a straight speed error, and for
    // wool is the difference between half a second and a twentieth of one.
    // Kinds from the reference's own best-tool table.
    // <https://minecraft.wiki/w/Breaking#Best_tools>
    if (isPlanksBlock(block)) {
        return ToolKind::Axe;
    }
    if (isWoolBlock(block)) {
        return ToolKind::Shears;
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
    case BlockId::Clay:
    case BlockId::CoarseDirt:
    case BlockId::Mud:
    case BlockId::RootedDirt:
        return ToolKind::Shovel;
    case BlockId::Sponge:
    case BlockId::WetSponge:
    case BlockId::MossBlock:
    case BlockId::HayBlock:
        return ToolKind::Hoe;
    case BlockId::Pumpkin:
    case BlockId::Melon:
    case BlockId::Jukebox:
        return ToolKind::Axe;
    // ---- The ids the reference gives no best tool at all. ----
    // **Named rather than defaulted.** Every one of these is a deliberate
    // "nothing is faster than a fist": air and the fluids have no break at all,
    // TNT and glass and glowstone are broken instantly or nearly so, a bed and
    // a torch and a flower pot come away in one hit, and honey is sticky rather
    // than hard. Listing them is what lets the catch-all below become a
    // sentinel instead of a plausible answer.
    case BlockId::Air:
    case BlockId::Tnt:
    case BlockId::TntPrimed:
    case BlockId::Bedrock:
    case BlockId::Glass:
    case BlockId::Glowstone:
    case BlockId::EndRod:
    case BlockId::HoneycombBlock:
    case BlockId::HoneyBlock:
    case BlockId::Scaffolding:
    case BlockId::FlowerPot:
        return ToolKind::None;
    default:
        break;
    }
    if (isFluid(block) || isTorchBlock(block) || isBed(block) ||
        (block >= BlockId::WhiteStainedGlass && block <= BlockId::BlackStainedGlass)) {
        return ToolKind::None;
    }
    // **Not `None`.** See `kUnchosenTool`: "no best tool" is a real and common
    // answer, which is exactly why it is the worst possible default - it is
    // indistinguishable from nobody having decided, and it reads as correct for
    // any block whose tier happens to be hand.
    return kUnchosenTool;
}

/// The rule every table run's tier branch applies, in one place: rock gates at
/// the first pickaxe, everything else comes away by hand.
///
/// **The `requiresToolAtAnyTier` half is what stops it over-reaching.** A dead
/// coral block, an ender chest and a lantern are all pickaxe blocks the
/// reference asks *no tier* of, so answering wood here would gate them a step
/// too high.
constexpr int pickaxeRunTier(BlockId block) {
    return derivedTool(block) == ToolKind::Pickaxe && !requiresToolAtAnyTier(block) ? kWoodTier
                                                                                   : kHandTier;
}

constexpr int derivedTier(BlockId block) {
    // Only stone-family blocks withhold their drop, which is what makes the
    // first pickaxe the thing that opens the game up. Wood and soil always give
    // something, or a fresh world would be unplayable.
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }
    // Every redstone machine is iron-age work and needs nothing better than the
    // first pickaxe; everything else in the family comes away by hand.
    //
    // **Only the observer and the dispenser family are gated**, and the four
    // that used to ride along with them are three separate reference answers:
    //
    // * A piston, a sticky piston and a piston head: `{{breaking row|Piston
    //   |Pickaxe}}` with **no tier argument**, and *"a piston can be broken
    //   with any tool, though a pickaxe is fastest. It always drops itself"*.
    //   <https://minecraft.wiki/w/Piston>
    // * All four rails: `{{breaking row|Rail|pickaxe}}`, no tier argument -
    //   *"rails always drop as an item"*. <https://minecraft.wiki/w/Rail>
    // * A lightning rod: `{{breaking row|Lightning Rod|Pickaxe|Stone}}`, which
    //   is a **stone** gate, not wood - it was the one id here the reference
    //   asks *more* of. <https://minecraft.wiki/w/Lightning_Rod>
    //
    // A wood gate on the first two meant every piston and every rail in the
    // world dropped nothing to a bare hand, which is 24 + 12 + 46 ids.
    if (isObserver(block) || isDispenserLike(block)) {
        return kWoodTier;
    }
    if (isLightningRod(block)) {
        return kStoneTier;
    }
    if (isPiston(block) || isPistonHead(block) || isRail(block)) {
        return kHandTier;
    }
    // **A block of redstone is left out**, and it is the one id in this
    // family that is not a component but a building block: five hardness, the
    // reference's wooden pickaxe. `isRedstoneComponent` names it, so without
    // this it answered "hand" here while `derivedTool` answered "pickaxe" -
    // two functions disagreeing about one block, which is the whole reason
    // there is now a single row.
    //
    // **A button and a plate used to be left out too, on a comment that read
    // the reference backwards.** It claimed *"the reference does gate a stone
    // pressure plate behind a pickaxe"*; it does not - `{{breaking row
    // |Stone Pressure Plate|Pickaxe}}` carries no tier argument, and Bedrock
    // 1.21.50 removed the gate outright: *"stone buttons, stone pressure
    // plates and polished blackstone pressure plates no longer require a
    // pickaxe to be mined"* (Preview 1.21.50.20 / .24). Falling through left
    // them on their stone parent's wood gate, so a bare fist destroyed them.
    // <https://minecraft.wiki/w/Button>,
    // <https://minecraft.wiki/w/Pressure_Plate>
    if (isRedstoneComponent(block) && block != BlockId::RedstoneBlock) {
        return kHandTier;
    }
    if (isNoteBlock(block)) {
        return kHandTier;
    }
    // **The pickaxe blocks the reference asks no tier of at all.** For every id
    // here the kind is about *speed* and nothing else, so without naming them
    // they take their run's `pickaxeRunTier`, which reads the kind and gates at
    // wood. That is not `requiresToolAtAnyTier`'s job either - these do not
    // withhold their drop from a bare hand, they are simply slower in one.
    //
    // * A brewing stand: *"can be mined with anything, but pickaxes are the
    //   fastest"*. <https://minecraft.wiki/w/Brewing_Stand#Breaking>
    // * Packed mud: *"can be broken by hand... it drops itself even when broken
    //   by hand"*. <https://minecraft.wiki/w/Packed_Mud>
    // * A conduit: *"drops as an item when broken with any tool or by hand"*.
    //   Its run's branch answers `kHandTier` for everything but a spawner, so
    //   this is belt and braces - and the belt is what survives an id moving
    //   run. <https://minecraft.wiki/w/Conduit>
    // * The three ices: nothing but Silk Touch gets one back, so gating them
    //   only ever bought a fivefold slowdown - blue ice was 14.0 s by hand
    //   against the reference's 4.2 s. <https://minecraft.wiki/w/Blue_Ice>
    // * An amethyst cluster and a pointed dripstone: both are *"mined with any
    //   tool"*, and the cluster's hand drop is a modelled two shards against a
    //   pickaxe's four in `BlockDrops.hpp`, which a wood gate would delete.
    //   **The three buds share the cluster's breaking table** - one infobox
    //   covers all four growth stages - so now that they have its 1.5 hardness
    //   and its pickaxe they need its tier too, or the pickaxe they just gained
    //   would gate them at wood. <https://minecraft.wiki/w/Amethyst_Cluster>
    // * **A stowbox, which only became one of these a moment ago.** Giving it
    //   the reference's pickaxe in `derivedTool` would otherwise have gated it
    //   at wood through `pickaxeRunTier` below and taken the drop away from a
    //   bare hand - the exact trap the ices and packed mud were in. The
    //   reference asks nothing of it: *"all shulker boxes drop themselves when
    //   mined"*. <https://minecraft.wiki/w/Shulker_Box>
    // * **A bell.** Its infobox names a pickaxe as the fastest tool, but
    //   `{{breaking row|Bell|pickaxe}}` carries no tier and the Breaking
    //   section says outright *"a bell can be mined with anything"*. It was
    //   named in the tool-required list, so a bare fist destroyed a bell.
    //   <https://minecraft.wiki/w/Bell>
    // * **Reinforced deepslate**, which the reference gives no tool at all:
    //   *"reinforced deepslate has no tool associated with it"* and it *"drops
    //   nothing when broken"* in Survival, `{{breaking row|Reinforced
    //   Deepslate|drop=0}}`. An iron gate here was the wrong shape of answer -
    //   there is nothing to gate, because there is nothing to get. Its 55
    //   hardness stands: it is a 3.5-minute punch either way.
    //   <https://minecraft.wiki/w/Reinforced_Deepslate>
    if (block == BlockId::BrewingStand || block == BlockId::PackedMud ||
        block == BlockId::Conduit || block == BlockId::Ice || block == BlockId::PackedIce ||
        block == BlockId::BlueIce || block == BlockId::AmethystCluster ||
        block == BlockId::SmallAmethystBud || block == BlockId::MediumAmethystBud ||
        block == BlockId::LargeAmethystBud || block == BlockId::PointedDripstone ||
        block == BlockId::Bell || block == BlockId::ReinforcedDeepslate || isStowbox(block)) {
        return kHandTier;
    }
    // **The blocks Bedrock released from their tool gate**, and they have to be
    // asked before the forwarding below: a weighted plate is **not** the block
    // of metal it was pressed from - the gold one's parent is `GoldBlock`,
    // which the reference gates behind an iron pickaxe - and a stone button and
    // a stone plate are not stone. See `alwaysDropsToAnyTool` for the sentence
    // that names all six and why the set is written as three families.
    //
    // > The buttons and plates would also be caught by the redstone-component
    // > branch above; the doors would not, and stating it once here is what
    // > makes `derivedToolRequired` and the two sweeps able to ask the same
    // > question. **`isRedstoneComponent` is a different set** - it holds a
    // > lever, a rail and a repeater too, and none of those is in the
    // > changelog.
    if (alwaysDropsToAnyTool(block)) {
        return kHandTier;
    }
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return derivedTier(material);
        }
    }
    // A smoker and a furnace are one answer here too, and the exception that
    // used to sit above this line was the tier half of the same mistake
    // `derivedTool` carried: it let a smoker be broken by hand on the grounds
    // that it is wooden. The reference gates it exactly as it gates a furnace -
    // *"if mined without a pickaxe, it does not drop itself"*, added in Bedrock
    // 1.21.50 Preview 1.21.50.24. <https://minecraft.wiki/w/Smoker>
    if (isFurnace(block)) {
        return kWoodTier;
    }
    // **A door and a trapdoor sit between the sixth and seventh runs**, along
    // with the beds and the candle extras, so no run branch below can reach
    // them and all of them landed on the hand tier at the bottom. That is right
    // for a bed, a candle and every wooden door - and wrong for the iron
    // *trapdoor*, which the reference gates behind a pickaxe. Exactly the "run
    // with no branch" bug this function set out to kill, in the gap *between*
    // the runs where no branch could have caught it.
    //
    // > The lowest pickaxe we have is wood, so "a pickaxe is required" and
    // > "gates at wood" are the same statement here.
    //
    // **The door and the trapdoor stopped being one answer in Bedrock
    // 1.21.50.** `{{breaking row|Iron Trapdoor|...|Pickaxe|Wooden}}` still
    // carries its tier argument, but the iron *door*'s row lost one: Preview
    // 1.21.50.24, *"iron doors no longer require a pickaxe to drop
    // themselves"*. So `isMetalDoor` still owns "which family is the metal
    // one", and the gate that reads it now asks the trapdoors only - the doors
    // are answered by `alwaysDropsToAnyTool` far above, with the two weighted
    // plates and the stone button and plate that the same sentence names.
    // <https://minecraft.wiki/w/Door>, <https://minecraft.wiki/w/Trapdoor>
    if (isTrapdoor(block)) {
        return isMetalDoor(block) ? kWoodTier : kHandTier;
    }
    // ---- One branch per table run, in numeric order. ----
    // **Every run has one, including the four that never did.** Runs 1, 3, 4
    // and 7 had no branch here at all, so all 291 of their ids fell to the
    // `kHandTier` at the bottom - which was right for the 80 farm blocks of
    // run 4 by luck and wrong for a block of diamond, a hopper and a filled
    // cauldron. A run without a branch cannot be spotted by reading; a run
    // whose branch is missing shows up the moment the sweep below runs.
    if (block >= kFirstExtraBlock && block <= kLastExtraBlock) {
        switch (block) {
        case BlockId::IronBlock:
        case BlockId::LapisBlock:
        case BlockId::CopperBlock:
            return kStoneTier;
        case BlockId::GoldBlock:
        case BlockId::DiamondBlock:
        case BlockId::EmeraldBlock:
            return kIronTier;
        default:
            break;
        }
        return pickaxeRunTier(block);
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
            return kIronTier;
        default:
            break;
        }
        // Only rock withholds its drop, which is the rule stated at the top of
        // this function. A blanket wood tier here meant every plank, wool,
        // plant and soil block in the run - about two hundred of them - was
        // destroyed rather than collected when broken by hand.
        return pickaxeRunTier(block);
    }
    // The third run: sixteen stained glass, two torches and an end rod all come
    // away by hand, the iron bars are the reference's wooden pickaxe, and the
    // two lanterns want *a* pickaxe of any tier.
    if (block >= kFirstExtraBlock3 && block <= kLastExtraBlock3) {
        return pickaxeRunTier(block);
    }
    // The fourth run, the farm. **Nothing changes today** - all eighty ids are
    // legitimately hand-harvestable - and the branch exists so that the next id
    // appended to the run is a deliberate row rather than a silent hand-drop.
    if (block >= kFirstExtraBlock4 && block <= kLastExtraBlock4) {
        return pickaxeRunTier(block);
    }
    // The fifth run, which had no branch at all: the whole of it fell to the
    // hand tier at the bottom, and since a drop needs `tool.tier >= row.tier`,
    // an anvil, an enchanting table, a lodestone or a block of crying obsidian
    // came away - drop and all - from a bare fist.
    //
    // **The bounds are the ones `blastResistance` in `Explosion.hpp` tests**,
    // so where a run begins and ends has one owner rather than four.
    if (block >= kFirstExtraBlock5 && block <= kLastExtraBlock5) {
        // Copper is a stone-pickaxe metal wherever it turns up. The second run
        // above already prices its unwaxed forms that way, and waxing changes
        // nothing but the weathering.
        if ((block >= BlockId::WaxedCopperBlock && block <= BlockId::WaxedOxidizedCopperGrate) ||
            isCopperBulb(block)) {
            return kStoneTier;
        }
        switch (block) {
        // Obsidian in all but name, and the reference gates both behind a
        // diamond pickaxe exactly as it gates obsidian itself below.
        case BlockId::CryingObsidian:
        case BlockId::RespawnAnchor:
            return kDiamondTier;
        default:
            break;
        }
        // The rule stated at the top of this function: only rock withholds its
        // drop. Asking `derivedTool` rather than listing thirty ids is what
        // stops this going stale the next time the run grows.
        //
        // **The redstone lamp is not an exception to it here**, though the
        // reference asks no tool of one: `isRedstoneComponent` above answered it
        // by hand long before this branch, so a case label for it would be dead
        // code rather than a fix.
        return pickaxeRunTier(block);
    }
    // The sixth run, on the same bounds and for the same reason. Almost all of
    // it is already answered above by its family - the trapped chest is a
    // chest, the blast furnace's other seven states are furnaces - and of what
    // is left, the reference asks a pickaxe only of the spawner. A beacon and a
    // dragon egg both come away by hand there, so `pickaxeRunTier` is
    // deliberately **not** used: it would gate both behind a wooden pickaxe.
    // The conduit is the run's third pickaxe block and wants no tier, which the
    // no-tier branch at the top of this function now states by name.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        return block == BlockId::MonsterSpawner ? kWoodTier : kHandTier;
    }
    // The seventh run, which no function in the old file named at all. A hopper
    // and the six filled cauldrons are the reference's wooden pickaxe; an ender
    // chest wants *a* pickaxe and no tier; a stowbox is the reference's pickaxe
    // for speed only, which the no-tier branch at the top states by name -
    // without it `pickaxeRunTier` here would read the new kind and gate it.
    if (block >= kFirstExtraBlock7 && block <= kLastExtraBlock7) {
        return pickaxeRunTier(block);
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
    // The base run's own gap: the reference gates prismarine behind a wooden
    // pickaxe and this switch never named it, so it - and every prismarine
    // stair, slab and wall forwarding to it - came away by hand.
    case BlockId::Prismarine:
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

/// Whether the drop needs the right **kind** of tool as well as the right tier.
///
/// Two sources, and the forwarding between them is the point: a tier above the
/// hand's always implies a kind (there is no such thing as a gate that any tool
/// opens), and the ids in `requiresToolAtAnyTier` demand a kind with no tier at
/// all. A tuff stair inherits "pickaxe required" from tuff through
/// `shapedParent`, the same way it inherits the tier - a guard that travels to
/// one of a pair and not the other is this project's most expensive bug shape.
///
/// `tier` is the block's own `derivedTier`, passed in because the caller has
/// just computed it and the walk is long. Asking `requiresToolAtAnyTier` of the
/// parent as well as the block is what carries "any pickaxe" across the
/// forwarding: an ice stair is still ice. It costs nothing for the 1,929 ids
/// that are their own parent, because then it is the same question twice.
///
/// **And the forwarding is exactly what `alwaysDropsToAnyTool` has to be
/// stopped from taking a ride on.** A light weighted pressure plate's parent is
/// `GoldBlock`; the day gold joins `requiresToolAtAnyTier` for any reason, the
/// plate would silently inherit a gate the reference took off it. The early-out
/// costs one comparison and makes that impossible rather than merely untrue
/// today.
constexpr bool derivedToolRequired(BlockId block, int tier) {
    if (alwaysDropsToAnyTool(block)) {
        return false;
    }
    return requiresToolAtAnyTier(block) || requiresToolAtAnyTier(shapedParent(block)) ||
           tier > kHandTier;
}

constexpr MiningRow derivedRow(BlockId block) {
    const int tier = derivedTier(block);
    return MiningRow{derivedHardness(block), derivedTool(block), static_cast<std::int8_t>(tier),
                     derivedToolRequired(block, tier)};
}

/// The table is built in **chunks of 256 ids** for one reason only: deriving
/// all `kBlockIdCount` rows in a single constant expression costs more than MSVC's
/// million-step budget, and raising the budget with `/constexpr:steps` is not
/// available here because `game/CMakeLists.txt` is another agent's file. Each
/// chunk is its own `constexpr` variable, so each gets its own budget, and the
/// flat table below is then a plain copy - which is cheap, because the
/// expensive part has already been folded into constants.
constexpr std::size_t kMiningChunkSize = 256;
constexpr std::size_t kMiningChunkCount = (kBlockIdCount + kMiningChunkSize - 1) / kMiningChunkSize;

/// **How many chunks are actually written out below**, which is a different
/// question from how many the enum currently needs and must not be spelled with
/// the same constant.
///
/// This used to be one number doing both jobs: the pointer array was sized by
/// `kMiningChunkCount` and there were exactly thirteen pointers in it, so the
/// margin between the enum and a build failure was whatever `3328 - kBlockIdCount`
/// happened to be that hour. It was measured at **nineteen ids** on 2026-08-19,
/// with two other agents actively appending block runs - a 24-id bee nest run
/// and a 6-id farmland run were both landed that same evening. Nineteen is not a
/// margin, it is a coin toss.
///
/// **Note what the failure would have been.** Aggregate initialisation does not
/// object to *too few* initialisers - it value-initialises the rest - so a
/// fourteenth slot would have been a null `MiningChunk*` that `makeMiningRows`
/// dereferences for every id from 3,328 up. The `static_assert` below is the
/// only thing that turned a silent null dereference into a build error, and it
/// earned its place.
///
/// **And the obvious repair was a trap.** "Add `kMiningChunk13`, add its
/// pointer, bump the assert to 14" breaks the build *immediately* on a tree that
/// is still under 3,328 ids: the array would be sized 13 by the derived count
/// and handed 14 initialisers, which unlike too few is a hard error, and the
/// `== 14` assert would fail as well. Splitting the constant is what makes the
/// spare chunk landable ahead of need instead of after the breakage.
///
/// Chunk 13 costs nothing while it is unused. `makeMiningChunk`'s loop is
/// `base + i < kBlockIdCount`, so at base 3,328 it does not execute at all and
/// the chunk is 256 value-initialised rows that `makeMiningRows` never reads,
/// because that walk also stops at `kBlockIdCount`. No derivation runs, so none
/// of the constexpr step budget this chunking exists to dodge is spent.
///
/// The ceiling is now **3,584 ids** rather than 3,328, which against the last
/// probe-measured `kBlockIdCount` of 3,309 is roughly 275 ids of headroom
/// instead of nineteen. To extend it again, add a `kMiningChunkNN`, add its
/// pointer, and raise this one number - and the assert stays `<=`, so it keeps
/// meaning "the enum has outgrown the chunks written here" rather than having
/// to be retuned every time either side moves.
constexpr std::size_t kMiningChunkCapacity = 14;

using MiningChunk = std::array<MiningRow, kMiningChunkSize>;

/// Ids past the end of the enum in the final chunk stay value-initialised and
/// are never read; `makeMiningRows` stops at `kBlockIdCount`.
constexpr MiningChunk makeMiningChunk(std::size_t base) {
    MiningChunk rows{};
    for (std::size_t i = 0; i < kMiningChunkSize && base + i < kBlockIdCount; ++i) {
        rows[i] = derivedRow(static_cast<BlockId>(base + i));
    }
    return rows;
}

inline constexpr MiningChunk kMiningChunk00 = makeMiningChunk(0 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk01 = makeMiningChunk(1 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk02 = makeMiningChunk(2 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk03 = makeMiningChunk(3 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk04 = makeMiningChunk(4 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk05 = makeMiningChunk(5 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk06 = makeMiningChunk(6 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk07 = makeMiningChunk(7 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk08 = makeMiningChunk(8 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk09 = makeMiningChunk(9 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk10 = makeMiningChunk(10 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk11 = makeMiningChunk(11 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk12 = makeMiningChunk(12 * kMiningChunkSize);
inline constexpr MiningChunk kMiningChunk13 = makeMiningChunk(13 * kMiningChunkSize);
// **`kMiningChunk13` is deliberately never read today, and no caller should be
// added.** `makeMiningRows` indexes `id / kMiningChunkSize`, which cannot reach
// 13 while `kBlockIdCount` is under 3,329, and the control below walks only
// `kMiningChunkCount` chunks. So the slot is defined, pointed at, and never
// dereferenced.
//
// **It is spare capacity, and it is self-activating**: the crossing condition
// is `kBlockIdCount > 3328`, and on the day an appended block id crosses it
// `kMiningChunkCount` becomes 14 by its own arithmetic, `makeMiningRows` starts
// indexing this chunk, and the control starts checking it - with no edit here
// or anywhere else. That is the whole point, because the fault it replaces was
// silent: the array used to be sized by the *count*, so a fourteenth chunk
// arrived as a null pointer that `makeMiningRows` then dereferenced.
//
// **Do not file this as dead code and do not delete it to quieten a sweep.**
// An unread constant is normally a second unwired answer, which is why one was
// deleted from this file tonight - this is the exception, and the difference is
// that nothing consults it for an answer it could get elsewhere.

inline constexpr std::array<const MiningChunk*, kMiningChunkCapacity> kMiningChunks{{
    &kMiningChunk00,
    &kMiningChunk01,
    &kMiningChunk02,
    &kMiningChunk03,
    &kMiningChunk04,
    &kMiningChunk05,
    &kMiningChunk06,
    &kMiningChunk07,
    &kMiningChunk08,
    &kMiningChunk09,
    &kMiningChunk10,
    &kMiningChunk11,
    &kMiningChunk12,
    &kMiningChunk13,
}};

static_assert(kMiningChunkCount <= kMiningChunkCapacity,
              "the enum has grown past 3,584 ids: add a `kMiningChunkNN`, add its pointer, and "
              "raise `kMiningChunkCapacity` - otherwise the trailing chunk pointers are null and "
              "`makeMiningRows` dereferences one");

/// **Every slot the walk can reach holds the chunk it claims to.** This is the
/// control on `kMiningChunkCapacity`, and it deliberately does **not** compare
/// pointers.
///
/// An earlier draft asserted `kMiningChunks[kMiningChunkCapacity - 1] ==
/// &kMiningChunk13`. That is sound C++ on paper - both operands are the address
/// of one object with static storage duration - but pointer *identity* in a
/// constant expression is exactly the shape MSVC is narrowest about, and the
/// kind of thing that compiles under one standard-library version and not the
/// next. Comparing the **values the pointers yield** asks the same question and
/// leans on nothing exotic.
///
/// Note what it reuses: dereferencing an array-indexed chunk pointer and then
/// indexing the chunk is the same expression shape `makeMiningRows` below
/// already evaluates at compile time - it writes `(*kMiningChunks[id /
/// kMiningChunkSize])[id % kMiningChunkSize]`, this writes
/// `(*kMiningChunks[chunk])[0]`. So this cannot fail for a reason that would
/// not also have broken the table itself. That is the whole argument for this
/// form over the last one - it is not safer because it looks tamer, it is safer
/// because the compiler is already known to do exactly this here.
///
/// It catches both failures the capacity split could have introduced:
///   - **a null slot** - dereferencing one is ill-formed in a constant
///     expression, so raising `kMiningChunkCapacity` without adding the
///     matching pointer stops the build instead of going quiet until the enum
///     grows into it;
///   - **a mis-wired slot** - a pointer to the wrong chunk yields the wrong
///     base's row and the comparison fails.
///
/// Non-vacuous by construction: `kMiningChunkCount` is `ceil(kBlockIdCount /
/// 256)`, so `(chunk * 256) < kBlockIdCount` for every chunk it walks and row 0
/// of each is a genuinely derived row, never a value-initialised hole. All four
/// fields are compared rather than one, because `tier` alone collides freely -
/// most blocks share a handful of tiers, so a wrong chunk would often match.
///
/// **If this ever fails to compile** (as opposed to failing the assertion):
/// delete this function and its `static_assert`, keep the `kMiningChunkCount <=
/// kMiningChunkCapacity` one above, and add a comment that the capacity must be
/// kept equal to the number of `kMiningChunkNN` pointers by hand. Do **not**
/// keep the `<=` assert alone and silently, because on its own it can be
/// satisfied by raising the capacity past the pointer list, which is the null
/// this exists to prevent.
/// **This has exactly one caller - the `static_assert` immediately below - and
/// it must never gain a runtime one.** It is a control, not a service: calling
/// it at run time would walk thirteen chunks to re-establish something the
/// compiler has already proved, and every answer it computes is available for
/// free from `miningRow`. If a sweep reports it as having no callers, that
/// report is wrong in the way a bare-name search is always wrong about a
/// compile-time consumer, and this comment is the refutation.
constexpr bool chunkPointersMapToTheirBases() {
    for (std::size_t chunk = 0; chunk < kMiningChunkCount; ++chunk) {
        const MiningRow got = (*kMiningChunks[chunk])[0];
        const MiningRow want = derivedRow(static_cast<BlockId>(chunk * kMiningChunkSize));
        if (got.hardness != want.hardness || got.tool != want.tool || got.tier != want.tier ||
            got.toolRequired != want.toolRequired) {
            return false;
        }
    }
    return true;
}

static_assert(chunkPointersMapToTheirBases(),
              "a kMiningChunks slot does not hold the chunk for its own base - either "
              "kMiningChunkCapacity was raised without adding the matching kMiningChunkNN "
              "pointer, or two pointers are out of order");

/// The table itself. **Sized by the enum, and filled by walking the enum**, so
/// there is no range test anywhere in this file that a newly appended block id
/// can fall outside of - which is exactly how four ids in four different runs
/// came to have four different sets of answers.
///
/// **On its own that guarantees a row exists, not that anyone chose it**, and
/// the difference is the whole of finding #2. Append a run of a hundred rock
/// ids and every one of them used to come out hardness 1.0, no tool, hand tier,
/// no tool required - a row that is perfectly self-consistent, so all four of
/// the sweep's original invariants passed it, because each of them is
/// conditioned on a tier above hand, on a tool being required, or on a hardness
/// at or below zero. `kUnchosenHardness` and `kUnchosenTool` are what close
/// that: the fall-through now builds a row that cannot be self-consistent, and
/// invariant #5 turns it into a build error naming the id.
constexpr std::array<MiningRow, kBlockIdCount> makeMiningRows() {
    std::array<MiningRow, kBlockIdCount> rows{};
    for (std::size_t id = 0; id < kBlockIdCount; ++id) {
        rows[id] = (*kMiningChunks[id / kMiningChunkSize])[id % kMiningChunkSize];
    }
    return rows;
}

inline constexpr std::array<MiningRow, kBlockIdCount> kMiningRows = makeMiningRows();

/// What an id from outside the enum answers. It cannot come from the world, but
/// it can come from a corrupt save, and reading past the array would be far
/// worse than answering like a plain untooled cube. **At namespace scope**
/// because `miningRow` hands back a reference to it.
inline constexpr MiningRow kFallbackRow{};

} // namespace mining

/// One block's mining row. **The single read point** - hardness, tool and tier
/// are each one field of this, and `blockHardness` in `Tool.cpp` is the only
/// one of the three still wrapped in a named accessor.
///
/// **It is the *sole* authority, measured rather than claimed (2026-08-19).**
/// Enumerated across `game/src` and `engine/` with comments and string literals
/// stripped first, so a name appearing only in prose does not count as a use:
/// there is exactly **one** function anywhere that turns a `BlockId` into a
/// `ToolKind`, and it is `mining::derivedTool`, which exists only to fill this
/// table at compile time and is read by nothing afterwards. So "what tool does
/// this block need" has one spelling for a caller - `miningRow(b).tool` - and
/// one derivation behind it.
///
/// It had two until this round. `harvestTool` in `Tool.cpp` was a second,
/// never-consulted answer, and a comment here claimed `BlockDrops.hpp` and
/// `Main.cpp` read it. **Neither ever did** - `Main.cpp` does not contain the
/// string, and `BlockDrops.hpp` says twice in its own comments that it reads
/// this table instead - so the claim was false when it was written rather than
/// having rotted. The forward is deleted and the note at its grave says why.
///
/// **If you are about to add a second way to ask this, that is the bug**
/// (`CLAUDE.md` #1): widen `derivedTool` or a predicate it calls. The weighted
/// plates are the worked example - one changelog sentence naming six blocks,
/// fixed as one predicate that four sites share rather than six names written
/// into three lists, which is why the same sentence cannot be half-applied
/// again. That fix also silently repaired a path nobody had connected to it:
/// `Main.cpp`'s `settleAround` pays a block knocked loose by a blast with an
/// **empty hand**, so all 32 weighted plates - which need support below - were
/// destroyed for nothing when their floor went, and now drop.
constexpr const MiningRow& miningRow(BlockId block) {
    return static_cast<std::size_t>(block) < kBlockIdCount
               ? mining::kMiningRows[static_cast<std::size_t>(block)]
               : mining::kFallbackRow;
}

// ---------------------------------------------------------------------------
// The break formula, in the reference's own order.
// ---------------------------------------------------------------------------

/// What `breakTicksFor` answers when the swing can never finish. **A caller
/// passing a `speedScale` of zero means "cannot mine"** - the natural way to
/// say a full Mining Fatigue has stopped the swing - and the old code read that
/// as "leave the scale alone", handing back the *unscaled* full-speed answer.
/// A real zero is no better: it divides by zero two calls down, which
/// `breakTicks` defended against by substituting a bare hand's 1.0, so the one
/// caller who said "never" got "as fast as possible" either way.
///
/// A number rather than an optional, because every caller of `breakTicksFor`
/// compares or divides the result: at 20 ticks a second this is about 621 days,
/// it is exactly representable as a `float` so `breakSeconds` stays exact, and
/// it is nowhere near overflowing an `int`.
constexpr int kNeverBreaksTicks = 1 << 30;

/// Whole ticks to break something, which is the unit the reference works in:
/// **any remainder is rounded up to the next tick**, so a 22.5-tick break costs
/// 23 ticks (1.15 s) and not 1.125 s.
///
/// `speed` is the reference's `speedMultiplier`, measured against a bare hand
/// on a block the hand suits - so 1.0 is "as fast as a fist", 8.0 is a diamond
/// pickaxe on rock, and 1/25 is a fist while swimming and airborne.
/// `canHarvest` picks between the reference's two damage divisors, 30 ticks per
/// unit of hardness when the block will drop for you and 100 when it will not.
constexpr int breakTicks(float hardness, float speed, bool canHarvest) {
    if (hardness <= 0.0f) {
        return 0;
    }
    // A speed of zero or less is "never", not "as fast as a fist". Nothing in
    // `kTools` produces one - every tool is at least 1.0 and the environment
    // penalties only divide by 5 or 25 - so the only way here is a caller's
    // `speedScale`, and substituting 1.0 turned that caller's "cannot mine"
    // into full speed.
    if (speed <= 0.0f) {
        return kNeverBreaksTicks;
    }
    const float damage = speed / hardness / (canHarvest ? kHarvestableDivisor
                                                        : kUnharvestableDivisor);
    // The reference's instant break: one tick's damage already finishes it, and
    // the six-tick cooldown between blocks is skipped as well.
    if (damage >= 1.0f) {
        return 0;
    }
    const float ticks = 1.0f / damage;
    const int whole = static_cast<int>(ticks);
    return static_cast<float>(whole) < ticks ? whole + 1 : whole;
}

/// Whether breaking `block` with `item` actually yields its drop - the
/// reference's `canHarvest`, which is the **right kind and the right tier**,
/// not the tier alone. A diamond shovel is tier 4 and collects no stone.
constexpr bool canHarvest(BlockId block, ItemId item) {
    const MiningRow& row = miningRow(block);
    const ToolProperties tool = mining::toolProperties(item);
    if (row.toolRequired && tool.kind != row.tool && !mining::alsoCollects(block, tool.kind)) {
        return false;
    }
    return tool.tier >= static_cast<int>(row.tier);
}

/// How long `item` takes to break `block`, in whole ticks - the unit the
/// reference counts in, and the one a caller wants when it needs to know
/// whether the break was instant.
///
/// **`underwater` and `onGround` are facts about the player, not the block**,
/// which is why they are parameters: the reference divides the speed by five
/// for a submerged head without Aqua Affinity and by five again for feet off
/// the ground, so treading water above a hole is 25x slower than standing in
/// There is deliberately no two-argument spelling. `Tool.hpp` carried one until
/// 2026-08-19; it had no callers, and a name that hard-codes "dry and grounded"
/// is a way to get that answer while believing the general question was asked.
///
/// Effects are **applied here when the caller passes `speedScale`**, and not
/// otherwise. The scale multiplies the tool's speed *before* the tick count is
/// rounded up, which is the order the reference uses: rounding first and
/// dividing after can land a tick either side, and it leaves `breaksInstantly`
/// blind to Haste entirely, so a block the reference finishes inside one tick
/// still pays the six-tick cooldown. The default of 1.0 keeps every existing
/// call site exact, and **a scale of zero or less is `kNeverBreaksTicks`** -
/// see there for why it used to be full speed instead.
constexpr int breakTicksFor(BlockId block, ItemId item, bool underwater, bool onGround,
                            float speedScale = 1.0f) {
    if (speedScale <= 0.0f) {
        return kNeverBreaksTicks;
    }
    const MiningRow& row = miningRow(block);
    if (row.hardness <= 0.0f) {
        return 0;
    }
    const ToolProperties tool = mining::toolProperties(item);
    // The reference's `isBestTool`: the right **kind** speeds the block up, and
    // the wrong kind is worth exactly a bare hand however good it is.
    const bool bestTool = row.tool != ToolKind::None && tool.kind == row.tool;
    const bool harvests = canHarvest(block, item);
    // **A per-block override outranks the row's kind**, because the reference's
    // five of them are the whole reason shears and swords are worth holding:
    // shears cut a leaf 15x however the row is written, and a sword is nobody's
    // best tool here yet still cuts a cobweb 15x. Read *before* the tier reset
    // below, so a tool that cannot harvest still loses it - which is what keeps
    // a wooden sword from speeding through a block it could not collect.
    const float overrideSpeed = mining::toolSpeedOverride(block, tool.kind);
    float speed = overrideSpeed > 0.0f ? overrideSpeed : (bestTool ? tool.speed : 1.0f);
    // **The Bedrock line Java dropped in 1.5.** A tool of the right kind but
    // too low a tier loses its multiplier entirely: a wooden pickaxe on iron
    // ore is 15.0 s here where Java gives 7.5 s. Without this reset the speed
    // and the divisor were chosen by two independent tests, which is why a
    // wooden pickaxe on iron ore took half as long as it should.
    if (!harvests) {
        speed = 1.0f;
    }
    speed *= speedScale;
    if (underwater) {
        speed /= kEnvironmentPenalty;
    }
    if (!onGround) {
        speed /= kEnvironmentPenalty;
    }
    return breakTicks(row.hardness, speed, harvests);
}

constexpr float breakSeconds(BlockId block, ItemId item, bool underwater, bool onGround,
                             float speedScale = 1.0f) {
    return static_cast<float>(breakTicksFor(block, item, underwater, onGround, speedScale)) /
           tick::kPerSecond;
}

/// Whether a break was over inside a single tick. The reference skips both the
/// six-tick cooldown and the tool's durability cost for exactly these, which is
/// a sharper test than "the block had no hardness".
constexpr bool breaksInstantly(BlockId block, ItemId item, bool underwater, bool onGround,
                               float speedScale = 1.0f) {
    return breakTicksFor(block, item, underwater, onGround, speedScale) <= 1;
}

// ---------------------------------------------------------------------------
// The compile-time sweep. Modelled on `everyBlockNamed` in `Block.hpp`: strided
// so that no single `static_assert` blows MSVC's constexpr step budget, and
// with the coverage assert at the end so that ids appended past the last stride
// cannot silently stop being swept.
// ---------------------------------------------------------------------------

/// Matches `kNameSweepStride` in `Block.hpp` for the same reason it is 512
/// there: seven of them cover the enum with room to spare.
constexpr int kMiningSweepStride = 512;

/// How many strides the generated sweep below instantiates. **One number, not
/// a hand-written list plus a literal that restates its length** - see
/// `mining::MiningSweep`.
constexpr int kMiningSweepPasses = 7;

namespace mining {

/// The best tool of the kind a block asks for - the emberite one, because it
/// satisfies every tier that exists. `ItemId::None` where the block names no
/// kind, and the shears, which have no tiers.
constexpr ItemId bestItemFor(ToolKind kind) {
    switch (kind) {
    case ToolKind::Pickaxe:
        return ItemId::EmberitePickaxe;
    case ToolKind::Axe:
        return ItemId::EmberiteAxe;
    case ToolKind::Shovel:
        return ItemId::EmberiteShovel;
    case ToolKind::Sword:
        return ItemId::EmberiteSword;
    case ToolKind::Hoe:
        return ItemId::EmberiteHoe;
    case ToolKind::Shears:
        return ItemId::Shears;
    case ToolKind::None:
        break;
    }
    return ItemId::None;
}

/// The best tool of the *wrong* kind: an emberite shovel unless the block wants
/// a shovel, in which case an emberite pickaxe. Highest tier in the game, so
/// anything it fails at, it fails at on kind alone.
constexpr ItemId wrongItemFor(ToolKind kind) {
    return kind == ToolKind::Shovel ? ItemId::EmberitePickaxe : ItemId::EmberiteShovel;
}

/// Every invariant that has to hold for all `kBlockIdCount` ids at once.
///
/// These are not restatements of the table: each one is a rule the table can
/// break, and three of the four were broken by the file this replaced.
constexpr bool miningTableSound(int stride) {
    const int first = stride * kMiningSweepStride;
    const int end = first + kMiningSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId block = static_cast<BlockId>(i);
        const MiningRow& row = miningRow(block);

        // 5. **Nobody chose this row.** Asked first, because a sentinel makes
        //    the four below fail in ways that name the wrong thing.
        //
        //    This is the invariant the design was sold as having and did not.
        //    "Sized by the enum" guarantees a row *exists* for every id; it
        //    says nothing about whether a human picked what is in it. Append a
        //    run of a hundred rock ids and the old defaults handed every one of
        //    them hardness 1.0, no tool, hand tier and no tool required - a row
        //    with no internal contradiction at all, so #1 (needs a tier or a
        //    kind), #2 (breaks instantly yet gated), #3 (wrong kind beats
        //    right) and #4 (a cut shape disagreeing with its parent) all pass
        //    it, every one of them being conditioned on a tier above hand, on
        //    a tool being required, or on a hardness at or below zero.
        //
        //    So the two catch-alls return values that cannot be true of any
        //    block instead, and this turns them into the build error. **The
        //    list of ids legitimately left to a default is empty**, and that is
        //    deliberate: every "no best tool" and every zero hardness is now a
        //    named case label, because the alternative is an escape hatch that
        //    grows one id at a time until it is the default again.
        //
        //    > Fails if: delete any one case label from the bottom switch of
        //    > `derivedHardness` or `derivedTool` - say `BlockId::FlowerPot` -
        //    > and the stride covering it stops compiling.
        if (row.hardness == kUnchosenHardness || row.tool == kUnchosenTool) {
            return false;
        }

        // 1. **A gate nothing can open.** A block that demands a tier, or the
        //    right kind, must name a kind - and must not name a tier no tool in
        //    `kTools` reaches. Forty-three run-1 ids failed this: the reference
        //    gates them behind a pickaxe and the old tool accessor said `None`.
        if ((row.tier > kHandTier || row.toolRequired) && row.tool == ToolKind::None) {
            return false;
        }
        if (row.tier < kHandTier || row.tier > kEmberiteTier) {
            return false;
        }

        // 2. **A block that breaks instantly cannot be gated**, because there
        //    is no swing to fail: whatever you are holding, it is gone in under
        //    a tick. The conduit and the cobweb both sat here - hardness 0 from
        //    the plant rule, and a pickaxe demanded on top of it.
        if (row.hardness <= 0.0f && (row.tier > kHandTier || row.toolRequired)) {
            return false;
        }

        // 3. **A gate that cannot be opened, and one that cannot be closed.**
        //    The best tool of the row's own kind must always collect, and where
        //    a kind is required the wrong kind must always come away with
        //    nothing.
        //
        //    > Fails if: raise any row's tier above `kEmberiteTier`'s tools -
        //    > or, the way it actually happened, name a kind in
        //    > `requiresToolAtAnyTier` whose best item sits below the row's
        //    > tier. It also fires if `alsoCollects` is widened to hand a
        //    > required-kind block to the wrong kind.
        //
        //    **A third comparison used to sit here** - that the wrong tool must
        //    not beat the right one - and it was one of the eleven asserts this
        //    project has been burned by: `wrong` is *derived from `row.tool`*,
        //    so it is never of that kind, so `breakTicksFor` gives it a speed
        //    of exactly 1.0 and a divisor no smaller than the best tool's, in
        //    every branch. No edit to the table could make it fire, and its
        //    comment claimed it caught a kind inversion, which it cannot: both
        //    tools are read off the row, so inverting the row inverts both.
        //    What pins the speed rules is `publishedTicks`, whose rows are
        //    anchored on the reference rather than on the table.
        const ItemId best = bestItemFor(row.tool);
        const ItemId wrong = wrongItemFor(row.tool);
        if (!canHarvest(block, best)) {
            return false;
        }
        if (row.toolRequired && canHarvest(block, wrong)) {
            return false;
        }

        // 4. **A shaped block is its parent, and nothing above the forwarding
        //    may quietly claim one.** Six hundred and forty stairs, slabs and
        //    walls have no rows of their own; the machine, redstone and note
        //    block early-outs all sit above the forwarding in `derivedTier`,
        //    and this is what fires if one of them ever starts matching a cut
        //    shape. A weighted plate is one deliberate exception, because the
        //    reference asks *a* pickaxe of it where its parent block of gold
        //    wants an iron one.
        //
        //    **A button and a pressure plate are the other two**, and they
        //    became exceptions the moment `derivedTier` stopped hand-excluding
        //    them from the redstone early-out. That exclusion existed only to
        //    keep this comparison true, on a comment that read the reference
        //    backwards - `{{breaking row|Stone Button|Pickaxe}}` carries no
        //    tier and Bedrock 1.21.50 removed the gate outright, so a stone
        //    button drops to a bare hand where stone itself does not.
        //
        //    **All three are now one question**, because the same sentence of
        //    the reference releases all three: `alwaysDropsToAnyTool` is the
        //    only place the set is written down, and the three-name list that
        //    used to sit here is exactly how the two weighted plates came to be
        //    excluded here and gated in `requiresToolAtAnyTier` at the same
        //    time. `everyCutShapeCostsItsParent` reads the same predicate.
        //    <https://minecraft.wiki/w/Bedrock_Edition_Preview_1.21.50.24>
        const BlockId material = shapedParent(block);
        if (material != block && !mining::alwaysDropsToAnyTool(block)) {
            if (row.tier != miningRow(material).tier ||
                row.toolRequired != miningRow(material).toolRequired) {
                return false;
            }
        }
    }
    return true;
}

} // namespace mining

namespace mining {

/// One `static_assert` per stride, **generated rather than written out**.
///
/// Ported verbatim from `BlockDrops.hpp`'s `DropSweep`, and for the reason that
/// file gives: the seven hand-written `miningTableSound(N)` lines that used to
/// sit here could have a middle one deleted and 512 ids would silently stop
/// being checked while the build stayed green. The coverage assert below could
/// not catch it either - it compared a *literal* 7 against the enum size, so it
/// went on agreeing with a list that no longer had seven entries in it.
///
/// **Split rather than folded into one assert**: MSVC counts constexpr steps
/// per evaluation and one pass over the whole enum is over the budget. A
/// `static_assert` inside a template is its own evaluation with its own budget,
/// exactly as one at namespace scope is, so the generated list costs the split
/// nothing.
template <int Pass>
struct MiningSweep {
    static_assert(miningTableSound(Pass),
                  "a block's mining row contradicts itself, or nobody chose it - see"
                  " invariant 5");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyMiningPassSwept(std::integer_sequence<int, Pass...>) {
    return (MiningSweep<Pass>::swept && ...);
}

} // namespace mining

static_assert(mining::everyMiningPassSwept(
                  std::make_integer_sequence<int, kMiningSweepPasses>{}),
              "a mining row is malformed somewhere - the failing MiningSweep instantiation above "
              "names which stride");
/// The passes are generated, so this is what proves there are enough of them:
/// without it, ids appended past the last stride would simply stop being swept
/// and the sweep would go on passing. **Derived from `kMiningSweepPasses`**, so
/// it moves with the list instead of restating a literal beside it.
static_assert(kMiningSweepPasses * kMiningSweepStride >= static_cast<int>(kBlockIdCount),
              "the mining sweep no longer covers every block id - raise kMiningSweepPasses");

// ---------------------------------------------------------------------------
// The reference's published break times, end to end: the real table, the real
// tools, the real formula. Every number on the right is Bedrock's own, from the
// breaking research (ml-01-mining.md 1.1 and 1.9) or arithmetic on a hardness
// that document sources - not a value read back out of this file.
// ---------------------------------------------------------------------------

/// Seconds a break takes, as whole ticks, so the asserts below can be written
/// in the unit the reference publishes. Standing on dry ground, which is what
/// every published time assumes.
constexpr int publishedTicks(BlockId block, ItemId item) {
    return breakTicksFor(block, item, false, true);
}

static_assert(publishedTicks(BlockId::Stone, ItemId::None) == 150,
              "stone by hand is the reference's 7.50 s");
static_assert(publishedTicks(BlockId::Stone, ItemId::WoodenPickaxe) == 23,
              "stone with a wooden pickaxe is the reference's 1.15 s - 22.5 ticks rounded up");
static_assert(publishedTicks(BlockId::Stone, ItemId::DiamondShovel) == 150 &&
                  !canHarvest(BlockId::Stone, ItemId::DiamondShovel),
              "the wrong kind of tool is worth exactly a bare hand and collects nothing, "
              "however good it is");
static_assert(publishedTicks(BlockId::IronOre, ItemId::WoodenPickaxe) == 300,
              "a wooden pickaxe on iron ore is the reference's 15.0 s: the right kind at too "
              "low a tier loses its multiplier entirely");
static_assert(publishedTicks(BlockId::IronOre, ItemId::StonePickaxe) == 23,
              "a stone pickaxe on iron ore is the reference's 1.15 s");
static_assert(publishedTicks(BlockId::Obsidian, ItemId::DiamondPickaxe) == 188,
              "obsidian with a diamond pickaxe is the reference's 9.40 s");
static_assert(publishedTicks(BlockId::EnderChest, ItemId::WoodenPickaxe) == 338,
              "an ender chest with a wooden pickaxe is the reference's 16.90 s - it needs "
              "hardness 22.5 and a pickaxe with no tier gate");
// This assert used to call the ender chest "the longest break in the game". Measured
// 2026-08-19 over every id at both extremes, and it was false on both readings: the
// longest break using the tool the block asks for is reinforced deepslate at 1650 ticks
// (82.50 s, 4.9x the ender chest, with obsidian a distant second at 188), and the longest
// bare-handed is obsidian at 5000 ticks (250.00 s, 14.8x). Corrected rather than kept,
// because a wrong superlative in an assert message is the kind a reader *acts* on - it
// reads as a ceiling, and anyone sanity-checking a new hardness against 338 would size
// it wrongly by an order of magnitude.
static_assert(publishedTicks(BlockId::Dirt, ItemId::WoodenShovel) == 8,
              "dirt with a wooden shovel is the reference's 0.40 s");

// Reinforced deepslate, pinned as the *outcome* rather than as its inputs.
//
// There is already an assert further down this file fixing its `tool` to ToolKind::None
// and its `tier` to kHandTier. That one is necessary and not sufficient: it pins the two
// fields but not the 82.50 s the player actually stands there for, so an edit to the
// hardness column alone would sail through it while changing the only thing anyone
// experiences. Assert the whole expression the real reader evaluates.
//
// The equality *between* the three is the load-bearing half. Reinforced deepslate is in
// no mineable tool tag in the reference, so a pickaxe does not speed it up - which looks
// so much like a bug that this session nearly "fixed" it. Measured: all seven tool kinds
// return an identical 1650. The control that makes that meaningful is obsidian directly
// above, hardness 50 and pickaxe-tagged, where a diamond pickaxe turns 5000 ticks into
// 188 - a 26.6x speedup on the same machinery in the same probe run. So the tie here is
// the reference's rule showing through, not the tool lookup failing to fire.
// Source: minecraft.wiki/w/Reinforced_Deepslate, hardness 55, breaking time 82.5 s with
// every tool, re-read 2026-08-19.
static_assert(publishedTicks(BlockId::ReinforcedDeepslate, ItemId::None) == 1650 &&
                  publishedTicks(BlockId::ReinforcedDeepslate, ItemId::DiamondPickaxe) == 1650 &&
                  publishedTicks(BlockId::ReinforcedDeepslate, ItemId::DiamondShovel) == 1650,
              "reinforced deepslate is the reference's 82.50 s and NO tool speeds it up - it "
              "is in no mineable tag, so a diamond pickaxe ties with a bare hand. If you are "
              "here because this looks absurdly slow, it is correct; check obsidian above, "
              "where the same code path gives a diamond pickaxe a 26x speedup");

// The four hardnesses this milestone corrects, each pinned through the whole
// formula rather than read back as a number.
static_assert(publishedTicks(BlockId::Hopper, ItemId::WoodenPickaxe) == 45,
              "a hopper is hardness 3.0 and a pickaxe block: 3.0 * 30 / 2 = 45 ticks, 2.25 s");
static_assert(publishedTicks(BlockId::CauldronExtraFirst, ItemId::WoodenPickaxe) == 30 &&
                  publishedTicks(BlockId::Cauldron, ItemId::WoodenPickaxe) == 30,
              "a filled cauldron mines exactly like the empty one - hardness 2.0, 1.50 s");
static_assert(publishedTicks(BlockId::Conduit, ItemId::None) == 90 &&
                  publishedTicks(BlockId::Conduit, ItemId::WoodenPickaxe) == 45 &&
                  canHarvest(BlockId::Conduit, ItemId::None),
              "a conduit is hardness 3.0 and *"
              "drops as an item when broken with any tool or by hand* - it was in "
              "`requiresToolAtAnyTier`, which destroyed it outright and cost 15.0 s doing it");
static_assert(publishedTicks(BlockId::Cobweb, ItemId::None) == 400,
              "a cobweb is hardness 4.0 and is NOT hand-harvestable, so it takes the "
              "unharvestable divisor: 4.0 * 100 = 400 ticks, 20.00 s");
static_assert(!canHarvest(BlockId::Cobweb, ItemId::None) &&
                  canHarvest(BlockId::Cobweb, ItemId::Shears) &&
                  canHarvest(BlockId::Cobweb, ItemId::WoodenSword),
              "shears take the web and a sword cuts it into string; a bare hand gets neither, "
              "which is what the 20 s is");

// The environment penalties, which are what the two new parameters buy. Stone
// with a wooden pickaxe is 22.5 ticks dry and grounded; a fifth of the speed is
// 112.5 ticks and a twenty-fifth is 562.5, each rounded up on its own.
static_assert(breakTicksFor(BlockId::Stone, ItemId::WoodenPickaxe, true, true) == 113 &&
                  breakTicksFor(BlockId::Stone, ItemId::WoodenPickaxe, false, false) == 113,
              "a submerged head and airborne feet each cost the same fivefold");
static_assert(breakTicksFor(BlockId::Stone, ItemId::WoodenPickaxe, true, false) == 563,
              "swimming and airborne at once is the reference's 25x");
static_assert(breakTicksFor(BlockId::TallGrass, ItemId::None, true, false) == 0,
              "no penalty makes an instant break take a tick - a plant is gone whatever you "
              "are doing when you hit it");

// The gates themselves, stated as drops rather than as tiers, because a drop is
// what the player sees.
static_assert(!canHarvest(BlockId::GoldBlock, ItemId::StonePickaxe) &&
                  canHarvest(BlockId::GoldBlock, ItemId::IronPickaxe),
              "a block of gold is the reference's iron pickaxe");
static_assert(!canHarvest(BlockId::WhiteConcrete, ItemId::None) &&
                  canHarvest(BlockId::WhiteConcrete, ItemId::WoodenPickaxe),
              "concrete is a pickaxe block - forty-four run-1 ids said 'no tool' and so "
              "gated at nothing");
static_assert(!canHarvest(BlockId::Tuff, ItemId::None) &&
                  !canHarvest(BlockId::Tuff, ItemId::EmberiteAxe) &&
                  canHarvest(BlockId::Tuff, ItemId::WoodenPickaxe),
              "the cave palette is a pickaxe block, and an axe of any tier is not a pickaxe");
static_assert(canHarvest(BlockId::TubeCoralBlock, ItemId::WoodenPickaxe) &&
                  !canHarvest(BlockId::TubeCoralBlock, ItemId::None) &&
                  !canHarvest(BlockId::TubeCoralBlock, ItemId::EmberiteShovel),
              "a coral block wants *a* pickaxe and no tier - a rule a tier comparison cannot "
              "state. This assert used to be anchored on ice, which no longer requires a tool "
              "at all");
static_assert(!canHarvest(BlockId::EnderChest, ItemId::EmberiteAxe) &&
                  canHarvest(BlockId::EnderChest, ItemId::WoodenPickaxe),
              "an ender chest is a pickaxe block, not the wooden chest `isChest` made it");
// > **Fails if:** `isEnderChest` leaves `requiresToolAtAnyTier`, which turns the
// > reference's 112.5 s bare-handed slog into 33.8 s and hands over the drop.
// > It pins the gate from the hand's side, where the pair above pins it from the
// > tool's, and it pins the 22.5 that `Explosion.hpp`'s 600 is the other half of.
static_assert(publishedTicks(BlockId::EnderChest, ItemId::None) == 2250,
              "an ender chest by hand is the reference's 112.5 s - the longest break in the "
              "game, and only because nothing about a fist can harvest it");
static_assert(canHarvest(BlockId::Stowbox, ItemId::None),
              "a stowbox stays chest-like and hand-harvestable - the seventh run's fix must "
              "not sweep it up");
// The stowbox is a shulker box: `['shulker box'] = 2` in the hardness module,
// and *"can be mined with any tool or by hand, but using a pickaxe is the most
// effective"*. Three asserts because there are three separate ways to lose it,
// and `isChest` had already taken all three.
// > **Fails if:** the `isStowbox` branch leaves `derivedHardness` - the chest's
// > 2.5 makes this 75.
static_assert(publishedTicks(BlockId::Stowbox, ItemId::None) == 60,
              "a stowbox by hand is the reference's 3.0 s, not a wooden chest's 3.75");
// > **Fails if:** the `isStowbox` branch leaves `derivedTool` - the axe below it
// > makes a pickaxe the wrong kind, and a wrong kind is worth a bare hand's 60.
static_assert(publishedTicks(BlockId::Stowbox, ItemId::WoodenPickaxe) == 30,
              "a stowbox with a wooden pickaxe is the reference's 1.5 s");
// > **Fails if:** the stowbox is folded back into the chest family's axe, which
// > would make this 30. It is the half of the pair that states what the tool is
// > *not*, and it is the half a kind swap alone would leave green.
static_assert(publishedTicks(BlockId::Stowbox, ItemId::WoodenAxe) == 60,
              "an axe is the wrong kind for a stowbox and buys nothing - only the chests and "
              "barrels around it are wooden");
// > **Fails if:** any of those three branches is narrowed to `block ==
// > BlockId::Stowbox`, which would leave the sixteen dyed ids in the chest
// > family - a fix that travels to one of a pair and not the other is this
// > project's most expensive bug shape.
static_assert(publishedTicks(BlockId::StowboxDyedFirst, ItemId::WoodenPickaxe) == 30 &&
                  publishedTicks(BlockId::StowboxDyedLast, ItemId::WoodenPickaxe) == 30,
              "every dyed stowbox mines like the plain one - dye is not a material");

// ---- The five findings this pass closes, each pinned through the formula. ----

// #4. One case label of six was holding all the metal blocks at 3.0.
static_assert(publishedTicks(BlockId::IronBlock, ItemId::EmberitePickaxe) == 17 &&
                  publishedTicks(BlockId::IronBlock, ItemId::None) == 500,
              "iron, diamond and emerald blocks are hardness 5.0, not the 3.0 they shared "
              "with gold, lapis and copper: 5.0 * 100 = 500 ticks by hand");
static_assert(publishedTicks(BlockId::GoldBlock, ItemId::None) == 300,
              "gold, lapis and copper stay at 3.0 - the split is real in the reference and "
              "this is the half that must not move");

// #5. Doors and trapdoors sit between two runs and matched no branch, so every
// one of the 384 + 192 landed on the hand tier. Swept rather than sampled,
// because the ids are reached by arithmetic and a sample would only prove the
// one offset it names.
//
// **It now asks the trapdoors only.** Bedrock 1.21.50 dropped the iron door's
// gate and kept the iron trapdoor's, so a sweep that still demanded
// `canHarvest == !isMetalDoor` of both would fail on 8 iron door ids the moment
// `derivedTier` told the truth. The door half is asserted just as hard, in the
// other direction: **every** door, iron included, must come away by hand.
constexpr bool everyMetalTrapdoorNeedsAPickaxe() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (isDoor(id)) {
            if (!canHarvest(id, ItemId::None)) {
                return false;
            }
            continue;
        }
        if (!isTrapdoor(id)) {
            continue;
        }
        if (canHarvest(id, ItemId::None) != !mining::isMetalDoor(id)) {
            return false;
        }
        if (!canHarvest(id, ItemId::WoodenPickaxe)) {
            return false;
        }
    }
    return true;
}

static_assert(everyMetalTrapdoorNeedsAPickaxe(),
              "an iron trapdoor is gated behind a pickaxe, every wooden one is not, and no "
              "door of any material is - merge the `isDoor` and `isTrapdoor` branches in "
              "`derivedTier` back into one and the 8 iron door ids fail this");

/// **The assert the changelog needed and did not have.** Bedrock Preview
/// 1.21.50.24 released six named blocks from their tool gate outright, the
/// citation was pasted into this file three times, and it reached the stone
/// button, the stone plate and the iron door while the two weighted plates
/// named in the same sentence stayed in `requiresToolAtAnyTier` - 32 ids that a
/// bare fist destroyed and took 50 ticks (2.5 s) rather than 15 (0.75 s) doing
/// it. Nothing could have caught that: three hand-applied lists cannot
/// disagree with each other, only with the sentence, and a sentence does not
/// compile.
///
/// So the set has a name now, and this is what a name buys - **one loop that
/// asks all three questions of every member**: not gated by tier, not gated by
/// kind, and actually collected by an empty hand. All three, because they fail
/// independently: `derivedTier` sets the first, `derivedToolRequired` the
/// second, and `canHarvest` is what the *player* experiences.
///
/// > **Fails if:** put any door, button or pressure plate family back into
/// > `requiresToolAtAnyTier`, or give one a tier above the hand's anywhere in
/// > `derivedTier` - including by letting the `shapedParent` forwarding reach
/// > it, which is how a gold block's iron gate would arrive on a light weighted
/// > pressure plate.
///
/// Not strided: it is one pass over the enum with two table reads per id, the
/// same shape and cost as `everyCutShapeCostsItsParent` below, which fits the
/// budget in both presets.
constexpr bool everyReleasedBlockComesAwayByHand() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (!mining::alwaysDropsToAnyTool(id)) {
            continue;
        }
        const MiningRow& row = miningRow(id);
        if (row.tier != kHandTier || row.toolRequired || !canHarvest(id, ItemId::None)) {
            return false;
        }
    }
    return true;
}

static_assert(everyReleasedBlockComesAwayByHand(),
              "a door, a button or a pressure plate is gated behind a tool - Bedrock "
              "1.21.50.24 says all six of the blocks it names 'always drop when broken with "
              "any tool', and `alwaysDropsToAnyTool` is where that set is written down");
// The counted half, so the sweep above cannot pass by covering nothing: 384
// doors (12 families x 32 states), 144 buttons (12 x 12) and 224 pressure
// plates (14 x 16), which is every id the three families have.
//
// **Those three numbers are pinned below rather than trusted.** The sentence
// above said *"168 buttons (14 x 12)"* until 2026-08-19, because the 14 was
// copied from the pressure-plate family beside it; there are 12 button
// families and 144 buttons. A count stated in prose has nothing holding it, and
// a tree-wide audit that day found **7 of 9** such claims in this file and
// `BlockDrops.hpp` had gone stale - so the fix is the assert, not the
// correction (`CLAUDE.md`: the fix is usually a `constexpr` assert rather than
// care).
static_assert(kDoorFamilyCount == 12 && kButtonFamilyCount == 12 &&
                  kPressurePlateFamilyCount == 14,
              "a family count moved, so the 384/144/224 in the comment above are now wrong. "
              "Re-measure and rewrite the sentence - do NOT simply widen this assert, because "
              "the sentence is what the next reader believes");
static_assert(miningRow(pressurePlateAt(kPressurePlateFamilyCount - 1, 0)).tier == kHandTier &&
                  !miningRow(pressurePlateAt(kPressurePlateFamilyCount - 1, 15)).toolRequired &&
                  !miningRow(pressurePlateAt(kPressurePlateFamilyCount - 2, 0)).toolRequired,
              "the heavy and light weighted plates are the two the changelog names that this "
              "file kept gating - both ends of the signal run, because the tier and the gate "
              "are per id");
static_assert(publishedTicks(pressurePlateAt(kPressurePlateFamilyCount - 1, 0), ItemId::None) ==
                      publishedTicks(pressurePlateAt(11, 0), ItemId::None) &&
                  publishedTicks(pressurePlateAt(kPressurePlateFamilyCount - 1, 0),
                                 ItemId::None) == 15,
              "a weighted plate is the reference's 0.5 hardness at the harvestable divisor, "
              "0.5 * 30 = 15 ticks (0.75 s), level with the stone plate beside it - it was "
              "taking 50 (2.5 s), which is what the unharvestable divisor costs");

/// **Exactly two blocks never break, and both of them must also yield nothing.**
///
/// This is the half of that rule this file can check. `kUnbreakableHardness` is
/// how the table says the reference's hardness of -1, and the reference gives
/// it to bedrock and the end portal frame and to nothing else: *"end portal
/// frames, like bedrock, are unobtainable and unbreakable in Survival or
/// Adventure mode"*. <https://minecraft.wiki/w/End_Portal_Frame>
///
/// **The other half is `yieldsNothingEver` in `BlockDrops.hpp`**, and the two
/// files cannot see each other, so this is deliberately a pair of asserts
/// rather than one: this one fails if a *third* id ever becomes unbreakable,
/// and its message is what sends the next reader to the drop table. The frame
/// is why the pair exists - it carried a drop of itself for the whole of that
/// table's life, invisible because no swing on it can ever finish, and bedrock
/// was only right by accident because `itemForBlock(Bedrock)` happens to be
/// `None`.
///
/// > **Fails if:** give any other block `kUnbreakableHardness` - a barrier, a
/// > command block, a second portal frame - without adding it to
/// > `yieldsNothingEver`, where the reference's "unobtainable" half lives.
constexpr bool onlyTwoBlocksNeverBreak() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        const bool never = miningRow(id).hardness >= kUnbreakableHardness;
        if (never != (id == BlockId::Bedrock || id == BlockId::EndPortalFrame)) {
            return false;
        }
    }
    return true;
}

static_assert(onlyTwoBlocksNeverBreak(),
              "a third block became unbreakable, or one of the two stopped being - whichever "
              "it is, `yieldsNothingEver` in BlockDrops.hpp has to be told, because an "
              "unbreakable block that names a drop is a drop the blast and `primaryDrop` "
              "still reach");
static_assert(breakTicksFor(BlockId::Bedrock, ItemId::EmberitePickaxe, false, true) ==
                      breakTicksFor(BlockId::EndPortalFrame, ItemId::EmberitePickaxe, false, true) &&
                  breakTicksFor(BlockId::EndPortalFrame, ItemId::EmberitePickaxe, false, true) ==
                      108000,
              "the best pickaxe in the game must buy nothing on either of the two, and it "
              "bought the frame 12,000 ticks against bedrock's 108,000 - 10 minutes against 90 "
              "- because a `default:` in `derivedTool` handed it this run's pickaxe. An "
              "absurd hardness only says 'never' while no tool divides it");

// #3. The run-1 rock the old 1.0 default was pricing, one from each group.
static_assert(publishedTicks(BlockId::DeepslateBricks, ItemId::None) == 350 &&
                  publishedTicks(BlockId::PolishedGranite, ItemId::None) == 150 &&
                  publishedTicks(BlockId::SmoothSandstone, ItemId::None) == 200 &&
                  publishedTicks(BlockId::CutSandstone, ItemId::None) == 80,
              "the first table run's stonework is 3.5, 1.5, 2.0 and 0.8, not the flat 1.0 a "
              "fall-through gave every one of them");
static_assert(publishedTicks(BlockId::WhiteConcrete, ItemId::None) == 180 &&
                  publishedTicks(BlockId::WhiteTerracotta, ItemId::None) == 125,
              "concrete is 1.8 and dyed terracotta 1.25");
static_assert(publishedTicks(BlockId::WhiteWool, ItemId::None) == 24 &&
                  publishedTicks(carpetAt(0), ItemId::None) == 3,
              "wool is 0.8 and a carpet is 0.1 - the one cut shape that is not its parent, "
              "and it was costing a second and a half to lift");
static_assert(publishedTicks(BlockId::BlueIce, ItemId::None) == 84 &&
                  publishedTicks(BlockId::Ice, ItemId::None) == 15 &&
                  publishedTicks(BlockId::PackedIce, ItemId::None) == 15,
              "blue ice is 2.8 and shared a case label with ice's 0.5. All three take the "
              "*harvestable* divisor: nothing but Silk Touch gets one back, so the tool gate "
              "they used to carry only ever bought a fivefold slowdown");

// #2's other half: two whole families that reached the tool default as `None`.
static_assert(publishedTicks(BlockId::SprucePlanks, ItemId::EmberiteAxe) == 7 &&
                  publishedTicks(BlockId::Torch, ItemId::None) == 0,
              "planks want an axe, and a torch is `BlockShape::Model` so the plant rule never "
              "reached it - it was a second and a half to punch out");

// Three ids whose only answer came from a `case` label the plant rule or a run
// fall-through had already taken - dead code that read as a decision.
static_assert(publishedTicks(BlockId::MossCarpet, ItemId::None) == 3 &&
                  miningRow(BlockId::MossCarpet).tool == ToolKind::Hoe,
              "moss carpet is 0.1 and a hoe block; it is `BlockShape::Flat`, so its case "
              "label inside the fifth run was unreachable and it broke instantly for nothing");
static_assert(publishedTicks(BlockId::Target, ItemId::EmberiteHoe) == 2 &&
                  publishedTicks(BlockId::Target, ItemId::EmberitePickaxe) == 15,
              "a target is a hoe block - move the `isTarget` test below the second run's "
              "branch and the run's pickaxe fall-through takes it instead");
static_assert(canHarvest(BlockId::BrewingStand, ItemId::None) &&
                  publishedTicks(BlockId::BrewingStand, ItemId::None) == 15,
              "a brewing stand can be mined with anything - naming a pickaxe as its fast tool "
              "must not let `pickaxeRunTier` gate it");

/// Every leaf, whatever run its id sits in, breaks like a leaf: 0.2 hardness,
/// a hoe, no tier, six ticks by hand. **All ten now reach that by the one
/// `isLeafBlock` test above the run branches**, so this is a family sweep and
/// not a comparison of two derivation paths - an earlier version of this
/// comment said it was, which stopped being true the moment that test was
/// hoisted. What it still catches is the hoist being undone: move `isLeafBlock`
/// below the second run's pickaxe fall-through and mangrove leaves fail it
/// again, which is exactly how the bug arrived.
///
/// **The tool it asserts changed from `None` to `Hoe`** with the tag the
/// reference publishes, and the two ticks either side of it are what pins the
/// change to *speed only*: a bare hand still pays 6 and an iron hoe pays none
/// at all, where before it paid the hand's 6 as well.
///
/// The `canHarvest` clause is the one `BlockDrops.hpp`'s gaps note used to deny:
/// it claimed mangrove leaves were still refused to a bare hand. They are not,
/// and this is the line that says so rather than a comment.
constexpr bool everyLeafBreaksLikeALeaf() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId leaf = static_cast<BlockId>(i);
        if (!isLeafBlock(leaf)) {
            continue;
        }
        const MiningRow& row = miningRow(leaf);
        if (row.tool != ToolKind::Hoe || row.tier != kHandTier || row.toolRequired) {
            return false;
        }
        if (publishedTicks(leaf, ItemId::None) != 6) {
            return false;
        }
        if (!canHarvest(leaf, ItemId::None)) {
            return false;
        }
    }
    return true;
}

static_assert(everyLeafBreaksLikeALeaf(),
              "a leaf block wants a hoe and comes away in 0.30 s by hand - mangrove leaves "
              "landed inside the second table run, whose fall-through is a pickaxe");

/// The half of that which the fix was *for*. **The single edit that fails it:
/// put `ToolKind::None` back in the `isLeafBlock` branch of `derivedTool`** -
/// the hand row is unchanged by that, so the sweep above would still pass on
/// nine of its three clauses and only this line notices.
static_assert(publishedTicks(BlockId::Leaves, ItemId::None) == 6 &&
                  publishedTicks(BlockId::Leaves, ItemId::WoodenHoe) == 3 &&
                  publishedTicks(BlockId::Leaves, ItemId::EmberiteHoe) == 0,
              "naming the hoe is what lets `breakTicksFor` pay out a multiplier at all - with "
              "`ToolKind::None` every one of these three is 6");

// ---- The five per-block speed overrides, each against a published time. ----
// `publishedTicks` is `breakTicksFor` itself, so every one of these evaluates
// the whole expression a real swing does - mining row, tool row and override
// together - rather than comparing one side of `toolSpeedOverride` against the
// other. Figures are the reference's own, at 20 ticks to the second.
static_assert(publishedTicks(BlockId::WhiteWool, ItemId::None) == 24 &&
                  publishedTicks(BlockId::WhiteWool, ItemId::Shears) == 5,
              "shears are 5x on wool - 0.8 hardness is the reference's 1.20 s by hand and "
              "0.25 s with shears, where the shears' own 1.0 row paid the hand's 24");
static_assert(publishedTicks(BlockId::Leaves, ItemId::Shears) == 0,
              "shears are 15x on a leaf, which at 0.2 hardness is more than one tick's "
              "damage - the reference breaks a sheared leaf instantly");
static_assert(publishedTicks(BlockId::Cobweb, ItemId::None) == 400 &&
                  publishedTicks(BlockId::Cobweb, ItemId::Shears) == 8 &&
                  publishedTicks(BlockId::Cobweb, ItemId::WoodenSword) == 8 &&
                  publishedTicks(BlockId::Cobweb, ItemId::EmberiteSword) == 8,
              "shears and a sword are both 15x on a cobweb - the reference's 0.40 s with "
              "either and 20.00 s with neither, and a sword's material does not enter it");
static_assert(publishedTicks(BlockId::Bamboo, ItemId::None) == 30 &&
                  publishedTicks(BlockId::Bamboo, ItemId::WoodenSword) == 0 &&
                  miningRow(BlockId::Bamboo).tool == ToolKind::Axe,
              "a sword shears bamboo away inside one tick - 30x on 1.0 hardness is exactly "
              "one tick's damage - and the axe the row names is unchanged by dropping the "
              "dead `ToolKind::None` label bamboo also carried");
static_assert(publishedTicks(BlockId::Stone, ItemId::Shears) == 150 &&
                  publishedTicks(BlockId::Stone, ItemId::WoodenSword) == 150,
              "and no override reaches anything else: shears and a sword are worth a bare "
              "hand on rock, which is 7.50 s at the unharvestable divisor");

// ---- Four rows that named a tool the reference does not, and one that named
// ---- the wrong one. Each pairs the kind with the time it produces, because a
// ---- kind on its own is invisible until `breakTicksFor` reads it.
static_assert(miningRow(BlockId::VineFirst).tool == ToolKind::Axe &&
                  publishedTicks(BlockId::VineFirst, ItemId::None) == 6 &&
                  publishedTicks(BlockId::VineFirst, ItemId::WoodenAxe) == 3 &&
                  publishedTicks(BlockId::VineFirst, ItemId::Shears) == 6,
              "an axe is a vine's tool and shears are only its harvest - with `Shears` in "
              "the row the axe lost its multiplier and every item in the game paid the "
              "hand's 6, which is what the shears' own comment says must not happen");
static_assert(miningRow(BlockId::SeaLantern).tool == ToolKind::None &&
                  publishedTicks(BlockId::SeaLantern, ItemId::None) == 9 &&
                  publishedTicks(BlockId::SeaLantern, ItemId::EmberitePickaxe) == 9,
              "a sea lantern is glass, not rock - the reference's 0.45 s with anything at "
              "all, where the pickaxe case label it sat in beside prismarine made it "
              "instant for whoever happened to be holding one");
static_assert(miningRow(BlockId::Beacon).tool == ToolKind::None &&
                  miningRow(BlockId::DragonEgg).tool == ToolKind::None &&
                  publishedTicks(BlockId::Beacon, ItemId::EmberitePickaxe) == 90 &&
                  publishedTicks(BlockId::DragonEgg, ItemId::EmberitePickaxe) == 90,
              "the sixth run's pickaxe fall-through reached a beacon and a dragon egg, and "
              "the reference mines both by hand at 4.50 s and no faster with any tool - "
              "`derivedTier` has said exactly that in prose since the run was written");
static_assert(miningRow(BlockId::BigDripleaf).hardness == 0.1f &&
                  miningRow(BlockId::BigDripleaf).tool == ToolKind::Axe &&
                  publishedTicks(BlockId::BigDripleaf, ItemId::None) == 3 &&
                  publishedTicks(BlockId::BigDripleaf, ItemId::WoodenAxe) == 2 &&
                  miningRow(BlockId::SmallDripleaf).hardness == 0.0f,
              "a big dripleaf is 0.1 and an axe where the plant rule made it zero and "
              "toolless; the small one really is instant, which is why one of the pair is "
              "named here and the other is not");

// ---- This pass: ~215 ids re-read against the reference's own tables. ----
// Every number below is `hardness * 30 / speed` when the swing collects and
// `hardness * 100` when it does not, on hardnesses taken from
// <https://minecraft.wiki/w/Module:Hardness_values> and tool rules taken from
// the per-block pages cited beside each derivation above.

// The headline: one `return 2.0f;` label held twenty-one ids and fourteen were
// wrong, while the *same families* were already priced correctly twenty lines
// away. Written as an agreement between the two halves rather than as bare
// numbers, because "two prices for one family" is the bug, not "3.5".
//
// > Fails if: put any of these six back in a shared label with the others - or
// > change one half of a pair without the other, which is the exact edit that
// > caused it.
static_assert(publishedTicks(BlockId::ChiseledDeepslate, ItemId::None) ==
                      publishedTicks(BlockId::CobbledDeepslate, ItemId::None) &&
                  publishedTicks(BlockId::CrackedDeepslateBricks, ItemId::None) ==
                      publishedTicks(BlockId::DeepslateBricks, ItemId::None) &&
                  publishedTicks(BlockId::ChiseledDeepslate, ItemId::None) == 350,
              "the whole deepslate family is 3.5 - three of its cuts were at 2.0");
static_assert(publishedTicks(BlockId::PolishedTuff, ItemId::None) ==
                      publishedTicks(BlockId::Tuff, ItemId::None) &&
                  publishedTicks(BlockId::ChiseledTuff, ItemId::None) ==
                      publishedTicks(BlockId::Tuff, ItemId::None) &&
                  publishedTicks(BlockId::TuffBricks, ItemId::None) == 150,
              "the whole tuff family is 1.5 - three of its cuts were at 2.0");
static_assert(publishedTicks(BlockId::PolishedBasalt, ItemId::None) ==
                      publishedTicks(BlockId::Basalt, ItemId::None) &&
                  publishedTicks(BlockId::PolishedBasalt, ItemId::None) == 125,
              "basalt's three cuts are one price, and the polished one was at 2.0");
static_assert(publishedTicks(BlockId::Blackstone, ItemId::None) == 150 &&
                  publishedTicks(BlockId::GildedBlackstone, ItemId::None) == 150 &&
                  publishedTicks(BlockId::PolishedBlackstone, ItemId::None) == 200,
              "blackstone is 1.5 and *polished* blackstone is 2.0 - the reference really does "
              "split them, and grouping the two is how the family drifted");
static_assert(publishedTicks(BlockId::EndStone, ItemId::None) == 300 &&
                  publishedTicks(BlockId::EndStoneBricks, ItemId::None) == 300 &&
                  publishedTicks(BlockId::PurpurBlock, ItemId::None) ==
                      publishedTicks(BlockId::PurpurPillar, ItemId::None),
              "end stone is 3.0, and purpur's block and pillar are both 1.5");

/// The corrected hardness has to reach the 1,356 cut shapes as well, and that is
/// the whole reason this group mattered more than twenty-one rows: a wall, a
/// slab and four stair states have no rows of their own.
///
/// **Five deliberate exceptions**, every one of them a rule `derivedHardness`
/// states *above* the forwarding: a carpet is 0.1 against wool's 0.8, a button
/// and a pressure plate are the reference's flat 0.5 whatever they were cut
/// from, the ten legacy stone slabs are 2 against a parent that is 1.5 or 0.8,
/// and a sign or banner is 1 with an axe against planks' 2 or wool's 0.8 and
/// shears. Skipping five named families rather than dropping the sweep is the
/// point - an exception is a rule with a reason, not a general licence.
///
/// The two new ones are skipped *narrowly*: `isLegacyStoneSlab` still has its
/// tool compared, because a slab that stopped inheriting its parent's pickaxe
/// would be a real bug, and so do a button and a pressure plate - a stone
/// button is 0.5 where stone is 1.5, but it is still a pickaxe block, and its
/// tool is the *only* thing left tying it to the stone it was cut from now
/// that `alwaysDropsToAnyTool` has taken its tier and its gate as well. Only
/// `isCarpet` and `isSignLike` skip both, and both differ from their parent on
/// kind: a carpet asks for no tool where wool asks for shears, and a sign is an
/// axe where its planks or wool are not.
///
/// > Fails if: give any corrected family a hardness label of its own instead of
/// > letting `shapedParent` forward - the cut shape keeps the old number while
/// > the parent moves, which is how a blackstone wall would have stayed at 2.0.
constexpr bool everyCutShapeCostsItsParent() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId cut = static_cast<BlockId>(i);
        const BlockId parent = shapedParent(cut);
        if (parent == cut || isCarpet(cut) || isSignLike(cut)) {
            continue;
        }
        if (!mining::isLegacyStoneSlab(cut) && !isButton(cut) && !isPressurePlate(cut) &&
            miningRow(cut).hardness != miningRow(parent).hardness) {
            return false;
        }
        if (miningRow(cut).tool != miningRow(parent).tool) {
            return false;
        }
    }
    return true;
}

static_assert(everyCutShapeCostsItsParent(),
              "a stair, slab or wall is priced by its parent block - so correcting blackstone "
              "corrects its six cut shapes and nothing has to be listed twice");

/// The ten families the line above has to make an exception for, pinned by
/// name so the exception cannot quietly widen. `Module:Hardness_values` keys
/// `['stone slab'] = 2` beside `['stone'] = 1.5` and `['quartz slab'] = 2`
/// beside `['block of quartz'] = 0.8`, and the sweep below proves the
/// membership test and the number agree for all `kBlockIdCount` ids at once - a slab that
/// escapes `isLegacyStoneSlab` inherits, and one that joins it must be 2.
/// <https://minecraft.wiki/w/Module:Hardness_values>
///
/// > Fails if: add any other slab family to `isLegacyStoneSlab` - a mossy stone
/// > brick slab is 1.5 in the reference, exactly its parent, so it would be
/// > claimed at 2 here and this line would catch it.
constexpr bool everyLegacySlabCostsTwo() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (!mining::isLegacyStoneSlab(id)) {
            continue;
        }
        if (miningRow(id).hardness != 2.0f) {
            return false;
        }
    }
    return true;
}

static_assert(everyLegacySlabCostsTwo(),
              "the ten slab families the reference prices above their parent are all 2.0");
static_assert(miningRow(BlockId::StoneSlab).hardness == 2.0f &&
                  miningRow(BlockId::Stone).hardness == 1.5f &&
                  miningRow(BlockId::StoneSlab).tool == miningRow(BlockId::Stone).tool,
              "a stone slab is 2.0 where stone is 1.5 - delete the `isLegacyStoneSlab` branch "
              "in `derivedHardness` and it inherits 1.5 again");

// Snow, which the world is made of. **One rule for both of the enum's ids**,
// and a shovel gate the reference states outright: *"the block must be broken
// with a shovel; otherwise... the block drops nothing"*.
// <https://minecraft.wiki/w/Snow_Block>, <https://minecraft.wiki/w/Snow>
//
// > Fails if: give either id a hardness of its own again - the equality below
// > is what stopped `BlockId::Snow` sitting at 0.5 among the sands while
// > `BlockId::SnowBlock` sat at the correct 0.2 in the second run, with the
// > terrain generator using the first.
static_assert(miningRow(BlockId::Snow).hardness == miningRow(BlockId::SnowBlock).hardness &&
                  miningRow(BlockId::Snow).tool == miningRow(BlockId::SnowBlock).tool &&
                  miningRow(BlockId::Snow).toolRequired ==
                      miningRow(BlockId::SnowBlock).toolRequired,
              "the enum spells the solid snow cube twice and `isSolidSnow` is the one owner - "
              "two rows is how they came to be 0.5 and 0.2");
static_assert(publishedTicks(BlockId::Snow, ItemId::None) == 20 &&
                  publishedTicks(BlockId::Snow, ItemId::WoodenShovel) == 3 &&
                  !canHarvest(BlockId::Snow, ItemId::None) &&
                  !canHarvest(BlockId::Snow, ItemId::EmberitePickaxe),
              "a snow block is 0.2 and shovel-only: 1.00 s by hand and 0.15 s with a wooden "
              "shovel, and an emberite pickaxe still gets nothing");
static_assert(publishedTicks(snowLayerAt(1), ItemId::None) == 10 &&
                  publishedTicks(snowLayerAt(7), ItemId::WoodenShovel) == 2 &&
                  !canHarvest(snowLayerAt(4), ItemId::None),
              "a snow layer is 0.1 and shovel-only at every depth - it was ungated, so a fist "
              "collected snowballs the reference does not give");

// The three froglights, which fell through the second run's pickaxe default and
// so were **destroyed** by a bare hand. The reference declares no best tool:
// *"breaking speed is always the same... no specific tool can break a froglight
// faster"*. <https://minecraft.wiki/w/Froglight>
//
// > Fails if: delete their case label from the second run's `derivedTool`
// > switch - the run's pickaxe fall-through returns, `pickaxeRunTier` gates
// > them at wood, and 9 ticks becomes 30 with nothing to show for it.
static_assert(publishedTicks(BlockId::OchreFroglight, ItemId::None) == 9 &&
                  publishedTicks(BlockId::VerdantFroglight, ItemId::EmberitePickaxe) == 9 &&
                  canHarvest(BlockId::PearlescentFroglight, ItemId::None),
              "a froglight is 0.3, always drops itself, and no tool is faster than a fist "
              "on one");

// The four the Cross/Flat rule was pricing at nothing. Hoisted above it exactly
// as the conduit and the cobweb already were.
//
// > Fails if: move any of the four hoists back below the
// > `BlockShape::Cross || BlockShape::Flat` test in `derivedHardness` - the
// > rule answers 0 again and every one of these breaks instantly.
static_assert(publishedTicks(BlockId::AmethystCluster, ItemId::None) == 45 &&
                  publishedTicks(BlockId::AmethystCluster, ItemId::WoodenPickaxe) == 23 &&
                  canHarvest(BlockId::AmethystCluster, ItemId::None),
              "an amethyst cluster is 1.5 and mined with any tool - it was free to punch out, "
              "which deleted the two-shards-by-hand rule `BlockDrops.hpp` models");
static_assert(publishedTicks(BlockId::PointedDripstone, ItemId::None) == 45 &&
                  canHarvest(BlockId::PointedDripstone, ItemId::None),
              "a pointed dripstone is 1.5 and mined with any tool");
static_assert(publishedTicks(BlockId::GlowLichen, ItemId::None) == 6 &&
                  publishedTicks(BlockId::Bamboo, ItemId::None) == 30,
              "glow lichen is 0.2 and bamboo is 1.0 - both were 0 through the plant rule");

// The nine single-id corrections, and the four gates that came off with them.
//
// > Fails if: change any one of these hardnesses back, or re-add its id to
// > `requiresToolAtAnyTier`.
static_assert(publishedTicks(BlockId::RawIronBlock, ItemId::None) == 500 &&
                  publishedTicks(BlockId::RawGoldBlock, ItemId::None) == 500 &&
                  publishedTicks(BlockId::RawCopperBlock, ItemId::None) == 500,
              "the three raw blocks are 5.0, level with iron - they were at the copper "
              "family's 3.0");
static_assert(publishedTicks(BlockId::SmoothQuartz, ItemId::WoodenPickaxe) == 30 &&
                  publishedTicks(BlockId::QuartzBlock, ItemId::WoodenPickaxe) == 12,
              "smooth quartz is 2.0 where the other three quartz cuts are 0.8");
static_assert(publishedTicks(BlockId::MuddyMangroveRoots, ItemId::None) == 21 &&
                  miningRow(BlockId::MuddyMangroveRoots).tool == ToolKind::Shovel,
              "muddy mangrove roots are 0.7 and dirt-like, not the mangrove timber's 2.0 "
              "and an axe");
static_assert(publishedTicks(BlockId::DriedKelpBlock, ItemId::None) == 15 &&
                  miningRow(BlockId::DriedKelpBlock).tool == ToolKind::Hoe,
              "a dried kelp block is 0.5 and a hoe block - it was 0.1 and grouped with slime");
static_assert(publishedTicks(BlockId::CraftingTable, ItemId::None) == 75 &&
                  publishedTicks(BlockId::Planks, ItemId::None) == 60,
              "a crafting table is 2.5 where the planks it is made of are 2.0");
static_assert(publishedTicks(BlockId::Mycelium, ItemId::None) == 18 &&
                  publishedTicks(BlockId::Podzol, ItemId::None) == 15,
              "mycelium is 0.6 and goes with the grass block, not podzol's 0.5");
static_assert(publishedTicks(BlockId::HoneyBlock, ItemId::None) == 0 &&
                  publishedTicks(BlockId::SlimeBlock, ItemId::None) == 0 &&
                  publishedTicks(BlockId::EndRod, ItemId::None) == 0 &&
                  publishedTicks(BlockId::LadderNorth, ItemId::None) == 12,
              "honey, slime and an end rod have no hardness at all; a ladder's 0.4 - which "
              "the end rod was sharing - is right and must not move with them");
static_assert(publishedTicks(BlockId::PackedMud, ItemId::None) == 30 &&
                  canHarvest(BlockId::PackedMud, ItemId::None),
              "packed mud *drops itself even when broken by hand* - it was in "
              "`requiresToolAtAnyTier`, which destroyed it outright");
static_assert(publishedTicks(BlockId::VineFirst, ItemId::None) == 6 &&
                  canHarvest(BlockId::VineFirst, ItemId::None),
              "a vine is 0.2 and swings freely here; *which* item collects one is "
              "`BlockDrops.hpp`'s shears rule, and stating it twice cost 0.30 s -> 1.00 s");

// A hoe is the sculk family's tool and only three of the five said so, because
// the two in the second run and the three in the fifth never met.
// <https://minecraft.wiki/w/Sculk>, <https://minecraft.wiki/w/Sculk_Catalyst>
//
// > Fails if: take `Sculk` or `SculkCatalyst` back out of the second run's hoe
// > label - they fall to `ToolKind::None` and a hoe stops being faster.
static_assert(miningRow(BlockId::Sculk).tool == miningRow(BlockId::SculkVein).tool &&
                  miningRow(BlockId::SculkCatalyst).tool == miningRow(BlockId::SculkShrieker).tool &&
                  miningRow(BlockId::Sculk).tool == ToolKind::Hoe &&
                  publishedTicks(BlockId::SculkVein, ItemId::None) == 6,
              "all five sculk blocks are hoe blocks - the rule was written in the fifth run "
              "and did not travel to the second, and the vein's own 0.2 was dead code under "
              "the flat-shape rule");

// ---------------------------------------------------------------------------
// This pass: the tool, tier and hardness rows re-read against the reference's
// own breaking tables. Each is written as the thing a *player* would notice,
// because "tier 1" is not a symptom and "a bare fist destroys a rail" is.
// ---------------------------------------------------------------------------

// A piston, a rail and a lightning rod were one branch and are three answers.
// <https://minecraft.wiki/w/Piston>, <https://minecraft.wiki/w/Rail>,
// <https://minecraft.wiki/w/Lightning_Rod>
//
// > Fails if: put `isPiston`, `isPistonHead` or `isRail` back in the
// > `kWoodTier` branch at the top of `derivedTier` - the first two clauses go
// > false. Move `isLightningRod` into either of the other two and the third
// > does.
static_assert(canHarvest(BlockId::PistonRunFirst, ItemId::None) &&
                  canHarvest(BlockId::PistonHeadRunFirst, ItemId::None) &&
                  canHarvest(BlockId::RailRunFirst, ItemId::None) &&
                  canHarvest(BlockId::PoweredRailRunFirst, ItemId::None) &&
                  !canHarvest(BlockId::LightningRodRunFirst, ItemId::WoodenPickaxe) &&
                  canHarvest(BlockId::LightningRodRunFirst, ItemId::StonePickaxe),
              "a piston and a rail always drop themselves; a lightning rod asks a stone "
              "pickaxe, which is the one id in that branch the reference asks *more* of");

// A smoker is a furnace for all three answers, and used to be an exception for
// two of them. <https://minecraft.wiki/w/Smoker>
//
// > Fails if: put either `isSmoker` hoist back - one above `isFurnace` in
// > `derivedTool`, one above `isFurnace` in `derivedTier`.
static_assert(miningRow(BlockId::Smoker).tool == miningRow(BlockId::Furnace).tool &&
                  miningRow(BlockId::Smoker).hardness == miningRow(BlockId::Furnace).hardness &&
                  !canHarvest(BlockId::Smoker, ItemId::None) &&
                  canHarvest(BlockId::Smoker, ItemId::WoodenPickaxe),
              "a smoker is mined exactly as a furnace is - 3.5, a pickaxe, and no drop to a "
              "bare hand");

// The three ids whose *gate* was the bug rather than their tool: a bell, a
// stone button and a stone pressure plate all drop to a bare hand.
// <https://minecraft.wiki/w/Bell>, <https://minecraft.wiki/w/Button>,
// <https://minecraft.wiki/w/Pressure_Plate>
//
// > Fails if: drop `BlockId::Bell` from the no-tier list in `derivedTier`, or
// > put the `!isButton && !isPressurePlate` exclusions back on the
// > `isRedstoneComponent` branch above it.
static_assert(canHarvest(BlockId::Bell, ItemId::None) &&
                  canHarvest(buttonAt(kButtonFamilyCount - 1, 0, false), ItemId::None) &&
                  canHarvest(pressurePlateAt(kPressurePlateFamilyCount - 3, 0), ItemId::None),
              "all three are 'can be mined with anything' in the reference - naming a pickaxe "
              "as the fast tool must not gate the drop");

// The four ids the reference gives no best tool at all, which had one each.
// <https://minecraft.wiki/w/Powder_Snow>, <https://minecraft.wiki/w/Redstone_Lamp>,
// <https://minecraft.wiki/w/Reinforced_Deepslate>
//
// > Fails if: put `PowderSnow` back on the fifth run's shovel label, or
// > `RedstoneLamp`/`RedstoneLampLit` back on its pickaxe label, or delete
// > `ReinforcedDeepslate`'s label in the second run so it takes that run's
// > pickaxe fall-through.
static_assert(miningRow(BlockId::PowderSnow).tool == ToolKind::None &&
                  miningRow(BlockId::RedstoneLamp).tool == ToolKind::None &&
                  miningRow(BlockId::RedstoneLampLit).tool == ToolKind::None &&
                  miningRow(BlockId::ReinforcedDeepslate).tool == ToolKind::None &&
                  miningRow(BlockId::ReinforcedDeepslate).tier == kHandTier,
              "no tool is faster on any of these - and reinforced deepslate cannot be gated "
              "at iron for a drop it never gives");

// Chorus and the three amethyst buds were `BlockShape::Cross`, so the plant
// rule priced all five at nothing. <https://minecraft.wiki/w/Chorus_Plant>,
// <https://minecraft.wiki/w/Amethyst_Cluster>
//
// > Fails if: move either hoist in `derivedHardness` below the
// > `blockShape(block) == BlockShape::Cross` test - all five return to 0.0f and
// > `breakTicks` answers 0 for a hardness of zero.
static_assert(publishedTicks(BlockId::ChorusPlant, ItemId::None) == 12 &&
                  publishedTicks(BlockId::ChorusPlant, ItemId::EmberiteAxe) == 2 &&
                  miningRow(BlockId::ChorusFlower).tool == ToolKind::Axe,
              "chorus is 0.4 with an axe, not an instant punch");
static_assert(publishedTicks(BlockId::SmallAmethystBud, ItemId::None) ==
                      publishedTicks(BlockId::AmethystCluster, ItemId::None) &&
                  miningRow(BlockId::LargeAmethystBud).tool == ToolKind::Pickaxe &&
                  canHarvest(BlockId::MediumAmethystBud, ItemId::None),
              "one infobox covers all four growth stages, so a bud is priced exactly as the "
              "cluster is - 1.5, a pickaxe, and no tier");

// A sign, a hanging sign and a banner are stated rather than inherited, which
// is the one place `isSignLike` had to be asked *above* the forwarding.
// <https://minecraft.wiki/w/Sign>, <https://minecraft.wiki/w/Banner>
//
// > Fails if: delete the `isSignLike` branch from `derivedHardness` or from
// > `derivedTool` - the banner takes wool's 0.8 and shears, the sign takes
// > planks' 2.0.
static_assert(miningRow(BlockId::BannerRunFirst).hardness == 1.0f &&
                  miningRow(BlockId::BannerRunFirst).tool == ToolKind::Axe &&
                  miningRow(BlockId::WhiteWool).tool == ToolKind::Shears &&
                  publishedTicks(BlockId::BannerRunFirst, ItemId::None) == 30,
              "a banner is 1.0 with an axe where its parent wool is 0.8 with shears - it is "
              "the one shaped family whose *kind* differs from its parent's");

// The whole file counts in ticks and `breakSeconds` is the only thing that
// converts, so this is where the two spellings of the tick rate have to meet:
// the seconds a caller is handed must be the published tick count measured with
// the *other* constant. Stone by hand is the reference's own 150 ticks and 7.5
// seconds, and both sides land on exactly 7.5f.
//
// > Fails if: the tick rate is re-literalised here rather than derived from
// > `tick::kPerSecond`, or either half of `Tick.hpp` moves without the other.
static_assert(breakSeconds(BlockId::Stone, ItemId::None, false, true) ==
                  static_cast<float>(publishedTicks(BlockId::Stone, ItemId::None)) * tick::kSeconds,
              "the break clock's two units must describe one rate - 150 ticks is 7.5 s only "
              "at a twenty-tick second");

// A `speedScale` of zero means "cannot mine", which used to mean "full speed".
//
// > Fails if: put the `speedScale > 0.0f` guard back around the multiply, or
// > the `speed > 0.0f ? speed : 1.0f` substitution back into `breakTicks`.
static_assert(breakTicksFor(BlockId::Stone, ItemId::WoodenPickaxe, false, true, 0.0f) ==
                      kNeverBreaksTicks &&
                  breakTicksFor(BlockId::Stone, ItemId::WoodenPickaxe, false, true, 2.0f) == 12 &&
                  !breaksInstantly(BlockId::TallGrass, ItemId::None, false, true, 0.0f),
              "a scale of zero is 'never', not 'unscaled' - and it must stop even a plant, "
              "which is the case that made the old bug invisible");

} // namespace game
