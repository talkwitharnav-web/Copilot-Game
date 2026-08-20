#pragma once

#include "item/Item.hpp"
#include "item/Tool.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace game {

/// What breaking a block yields: **several items, in counts that are ranges, on
/// chances that are rolled off the block's own position.**
///
/// This supersedes `dropForBlock`/`dropCountForBlock` in `Item.hpp`, which
/// between them can only say "one item, this many". That was enough while every
/// drop was a single certain thing, and it stopped being enough the moment
/// gravel needed to give flint *instead of* gravel and oak leaves needed to roll
/// a sapling, a stick and an apple independently.
///
/// **The old pair is still here and still correct for the identity of 96.6% of
/// ids**, and this table asks it for exactly that - so the 1,356 cut shapes, the
/// 136 candles, both door halves and every container state are answered by the
/// code that already answers them, and there is one place a family rule can
/// live rather than two that drift.
///
/// > **What this function does *not* do is the tier gate.** `yieldsDrop`
/// > (`Tool.cpp`) still decides whether your pickaxe is good enough for the
/// > stone. What lives here is the narrower question a tier cannot answer:
/// > which tool changes *what* falls - shears on a cobweb, a shovel on snow, a
/// > pickaxe on an amethyst cluster.
///
/// > **Where these numbers come from, and where they cannot come from**
/// > (measured 2026-08-19, so nobody spends the hour I did). The fleet rule is
/// > *"prefer `Mojang/bedrock-samples` behaviour-pack JSON over minecraft.wiki,
/// > and say which source you used"*. **That source cannot answer this file.**
/// > `behavior_pack/` publishes biomes, entities, items, loot_tables, recipes,
/// > shapes, spawn_rules and trading - and Bedrock block behaviour is
/// > engine-side, so there is **no `behavior_pack/blocks/` and no
/// > `loot_tables/blocks/`**. Block drops, hardness and tool tiers are simply
/// > not published anywhere by Mojang.
/// >
/// > Checked rather than assumed, with a control, because a 404 from a typo
/// > and a 404 from a thing that does not exist look identical: the GitHub
/// > contents API returned a **list** for `loot_tables/entities` (122) and
/// > `items` (77), and **404** for `loot_tables/blocks`, `blocks`, and a
/// > deliberately fabricated directory name. So the absence is real.
/// >
/// > **Therefore every drop in this table is minecraft.wiki-sourced and the
/// > citations say so.** That is the best authority available for it, not a
/// > second-best that someone should upgrade - do not re-file these rows as
/// > "unverified against the primary source". The one thing Mojang *does*
/// > publish that touches drops is `metadata/vanilladata_modules/
/// > mojang-blocks.json` (1415 blocks, 155 state properties), which settles
/// > block *state* questions only - see the candle note at `dropsForBlock`.

/// The most kinds of item one broken block can yield.
///
/// **Three, because oak leaves are the worst case in the Overworld**: a
/// sapling, a stick and an apple, each on its own roll. A ripe potato is two
/// (potatoes, and a 2% poisonous one) and gravel is two alternatives. Nothing
/// asks for a fourth, and the array is fixed so a `BlockDrop` is a value rather
/// than an allocation - it is returned on every block break.
constexpr int kMaxDropEntries = 3;

/// How many independent rolls one block may take. Eight is far past what
/// anything needs; it exists so the sweep below can prove a salt is in range.
constexpr int kMaxDropSalts = 8;

/// Where the "how many" rolls start, so they can never collide with the
/// "whether" rolls. A block asks `dropHash` at most six times: up to three
/// appearance rolls at salts 0..2 and up to three count rolls at 8..10.
constexpr int kCountSaltBase = kMaxDropSalts;

/// One kind of item a break can yield.
///
/// The count is a **range**, because almost every reference count is one -
/// copper 2-5, lapis 4-9, melon 3-7 - and a midpoint is what `dropCountForBlock`
/// stores today. `chanceNum/chanceDen` is whether the entry appears at all, kept
/// as two integers rather than a float so the whole thing stays `constexpr` and
/// the roll is integer arithmetic on a hashed position.
///
/// **`salt` is the one field the research's shape did not have, and gravel is
/// why.** Gravel gives flint *instead of* itself, so its two entries are
/// alternatives rather than independent rolls - and two independent rolls at
/// 10% and 90% would hand back both about one time in eleven, which is the
/// duplication bug this milestone exists to close. So: **entries sharing a salt
/// are alternatives decided by one roll**, in table order, by cumulative
/// weight; entries with different salts are independent. Leaves use salts 0, 1
/// and 2 and are three separate rolls; gravel uses salt 0 twice and is one.
struct DropEntry {
    ItemId item = ItemId::None;
    std::uint8_t min = 1;
    std::uint8_t max = 1;
    std::uint8_t salt = 0;
    std::uint16_t chanceNum = 1;
    std::uint16_t chanceDen = 1;

    constexpr bool operator==(const DropEntry&) const = default;
};

/// Everything one broken block yields, before the rolls are resolved.
struct BlockDrop {
    std::array<DropEntry, kMaxDropEntries> entries{};
    std::uint8_t count = 0;

    constexpr bool empty() const { return count == 0; }
};

static_assert(sizeof(BlockDrop) <= 32, "a drop must stay cheap enough to return by value");

/// What the *player* brought to the break.
///
/// Passed as one struct rather than four arguments precisely so Silk Touch and
/// Fortune can arrive later without touching a single call site. **Neither is
/// an enchantment this game has**, so no call site sets either today and every
/// row keyed off them is dormant - but they are *read*: `silkTouchRecovers` and
/// forty-odd conditional rows below answer `silkTouch`, and gravel's flint roll
/// answers `fortune`. The ground-truth block dump has a column for each, which
/// is what turns "dormant" into something that can be checked. `explosion` is
/// Bedrock's blast rule in one flag rather than a second function: a blast
/// drops *"as if mined with an unenchanted diamond tool, or an empty hand if
/// the correct tool would be shears"*.
///
/// > **A default-constructed context is bare hands, and it means it.** A
/// > cauldron, an ender chest and a cobweb all yield nothing to one, because
/// > that is what they yield to bare hands. Call sites must pass what the player
/// > is actually holding; `breakContextFor` below does that in one line.
struct BreakContext {
    ToolKind tool = ToolKind::None;
    int tier = kHandTier;
    bool silkTouch = false;
    /// 0-3 in the reference. An `int` rather than the `bool` the brief sketched,
    /// because the three Fortune shapes are all level-dependent and a bool
    /// could not carry any of them.
    int fortune = 0;
    bool explosion = false;
};

/// **`explosion` is LIVE, and it is the one field here a bare-name sweep cannot
/// see. Re-verified 2026-08-19 15:57.**
///
/// `Main.cpp` builds the blast context in the local named **`blasted`** - cited
/// by symbol, not by line, for the reason in the note below - and it is reached
/// in play whenever TNT or a creeper takes a block:
///
/// ```
/// const game::BreakContext blasted{game::ToolKind::Pickaxe,
///                                  game::kDiamondTier, false, 0, true};
/// ```
///
/// > **That is a POSITIONAL aggregate initialiser, so the `true` is `explosion`
/// > by its ORDINAL and the field name appears nowhere.** A sweep for
/// > `.explosion` or `explosion =` returns **zero hits in `Main.cpp`** and reads
/// > exactly like a dormant flag - which is CLAUDE.md's "a value routed through
/// > a struct is invisible to a call-site sweep", and it very nearly cost this
/// > audit a false dormancy verdict today. The honest instrument is to find the
/// > call sites of the *consumer* (`resolveBreak`, via `spillBlockDrop`) and
/// > read what each one passes, not to search for the field.
///
/// **This is why the layout is pinned below rather than merely described.**
/// There are five positional initialisers of this struct and only one of them
/// is in a file whose owner would notice: `breakContextFor` just below, three
/// sweep asserts in this file, and the live blast in `Main.cpp`. **Not one of
/// them names a field.** Inserting a member anywhere above `explosion` - a
/// `bool fireAspect`, say - shifts every one of them silently: the blast's
/// `true` would land in the new field, `explosion` would fall back to its
/// default `false`, and every blasted block would start dropping as if mined by
/// hand. It compiles, it validates, and nothing in the tree says a word.
///
/// The check reproduces the `blasted` initialiser exactly and asserts each
/// value arrives where that call site means it to. **Sited here, in the file
/// whose edit would break it**, per CLAUDE.md: the reader about to add a field
/// is the one who has to be told, and `Main.cpp`'s owner cannot see this
/// coupling from there. Adding a member is fine - it just has to go **after**
/// `explosion`, or every positional call site has to be updated in the same
/// batch, and this assert is what forces that choice to be made deliberately.
///
/// > **Cited by SYMBOL because the line number was already wrong.** This
/// > paragraph said `Main.cpp:11513` when it was written at 15:37; by 15:57
/// > that line held `&found->second.output}) {` and the blast context had moved
/// > to 11668 - 155 lines in twenty minutes, with the code unchanged. Search
/// > `\bBreakContext\b` in `Main.cpp` and take the one declaration that is a
/// > named local rather than a temporary; there is exactly one, and every other
/// > hit is `BreakContext{}` or a designated `.tool` form. **Word-bound the
/// > search**: in this codebase `Name` is routinely a prefix of `NameFirst` and
/// > `NameLast`, and an unanchored enum probe overcounted elsewhere tonight.
constexpr bool blastContextFieldsLandWhereMeant() {
    const BreakContext blast{ToolKind::Pickaxe, kDiamondTier, false, 0, true};
    return blast.tool == ToolKind::Pickaxe && blast.tier == kDiamondTier &&
           !blast.silkTouch && blast.fortune == 0 && blast.explosion;
}

static_assert(blastContextFieldsLandWhereMeant(),
              "a BreakContext member was inserted above `explosion`, so the five positional "
              "initialisers of this struct - `breakContextFor` below, three sweep asserts in this "
              "file, and the live blast context `blasted` in Main.cpp - now fill the wrong fields; "
              "move the new member below `explosion` or update all five together");

/// The context for a player holding `held`.
///
/// **Not `constexpr`**, because `toolFor` is a runtime table in `Tool.cpp`. The
/// table below never calls this; it exists so a call site does not have to know
/// how to take a `ToolKind` apart.
inline BreakContext breakContextFor(ItemId held) {
    const ToolProperties tool = toolFor(held);
    return BreakContext{tool.kind, tool.tier, false, 0, false};
}

/// The one hash every drop roll uses. `salt` separates independent rolls at the
/// same cell, which is what lets oak leaves roll a sapling, a stick and an apple
/// without the three agreeing with each other. `nonce` separates one *break*
/// from the next, and zero - the default, and what every one-shot block passes -
/// is the position-only answer this started as.
///
/// **Derived from the block position, never from a running generator**, so
/// breaking the same gravel twice gives the same haul and a reloaded save does
/// not re-roll it. This is the rule gravel already followed, and the three
/// mixing constants here are `Main.cpp`'s own `BlockPositionHash` constants,
/// moved rather than invented - find it by searching `73856093`, which appears
/// exactly once in that file. The rate is unchanged; **which cells give flint
/// moves**, because the salt and the avalanche steps are new.
///
/// **Two mix rounds, not one, and the second one is here to make the paragraph
/// above true rather than nearly true.** A salt folds in as an XOR *before* any
/// mixing, so two salts at one cell start out differing by one fixed pattern of
/// bits, and a single Murmur2 finalizer does not spread that far enough to call
/// the two rolls independent. Measured **offline, over 8 million cells** with
/// one round: `P(2 sticks | a stick dropped)` came out 0.5330 against 0.5
/// (z = +26) and `P(sapling and apple)` 0.0310% against 0.0250% (z = +11).
/// Both marginals were exact and no player could ever have felt either, but the
/// paragraph on `DropEntry` promises *independent* rolls and this costs two
/// instructions. Re-measured over 4 million cells after adding the round: the
/// same two numbers land at z = +0.1 and z = -0.4, and the marginals are
/// unmoved.
///
/// > **Neither of those measurements is re-derived at compile time, and the
/// > agreement band below does not stand in for them** - it is a different
/// > statistic, and deleting the second round leaves it inside its band (264
/// > and 253 against the current 249 and 265). What guards the round is the
/// > pair of golden values immediately below, which pin the whole function:
/// > drop either round, reorder them, or change any one of the seven constants
/// > and they fail naming the block that moved.
///
/// The multiplies are done in `std::uint32_t` rather than `int`. Signed
/// overflow is undefined and a `constexpr` evaluator refuses it outright, so the
/// original expression could not have been used at compile time at all.
constexpr std::uint32_t dropHash(int x, int y, int z, int salt, std::uint32_t nonce = 0) {
    std::uint32_t h = (static_cast<std::uint32_t>(x) * 73856093u) ^
                      (static_cast<std::uint32_t>(y) * 19349663u) ^
                      (static_cast<std::uint32_t>(z) * 83492791u) ^
                      (static_cast<std::uint32_t>(salt) * 2654435761u) ^
                      (nonce * 0x85ebca6bu);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    h *= 0x27d4eb2du;
    h ^= h >> 15;
    return h;
}

// The hash itself, pinned to the bit. **These replace a `dropHash(...) ==
// dropHash(...)` that compared one call against an identical one** - a
// tautology no edit could break, sitting where the file's account of its own
// determinism was supposed to be.
//
// > Fails if: change any constant, drop or reorder either mix round, move the
// > salt or the nonce, or widen the intermediate type. Every one of those is a
// > silent change today - the drops stay legal, the rates stay right, and only
// > *which* cells pay out moves.
static_assert(dropHash(17, 64, -3, 0) == 0x8f714c3bu &&
                  dropHash(17, 64, -3, 1) == 0x37446010u &&
                  dropHash(17, 64, -3, 0, 1) == 0xd8a30786u,
              "the roll for one cell is a fixed number, and this file's promise that a "
              "reloaded save gives back the same haul is exactly that number not moving");

// A roll is a function of the cell, the salt and the break, and salts that
// collided would silently make a leaf's sapling and its apple the same coin
// flip.
static_assert(dropHash(17, 64, -3, 0) != dropHash(17, 64, -3, 1) &&
                  dropHash(17, 64, -3, 1) != dropHash(17, 64, -3, 2) &&
                  dropHash(17, 64, -3, 0) != dropHash(17, 64, -3, 2),
              "the three leaf salts must not collide");
static_assert(dropHash(17, 64, -3, 0) != dropHash(17, 64, -4, 0),
              "two cells one block apart must not roll together");
static_assert(dropHash(17, 64, -3, 0, 1) != dropHash(17, 64, -3, 0) &&
                  dropHash(17, 64, -3, 0, 1) != dropHash(17, 64, -3, 0, 2),
              "a second break of one cell must not repeat the first");

/// How often two rolls at the same cell agree on one bit, over `cells` cells.
///
/// **Inequality is not independence**, which is the whole of what the three
/// asserts above prove: two answers can differ every time and still move
/// together. Independent rolls agree on any given bit about half the time, and
/// drifting off that is the first thing a hash does when it stops separating
/// what it was handed. Deterministic and identical on every compiler - it is
/// all `std::uint32_t` arithmetic - so a band around 50% cannot be flaky.
constexpr int rollAgreement(int cells, int saltA, int saltB, std::uint32_t nonceA,
                            std::uint32_t nonceB) {
    int agreed = 0;
    for (int i = 0; i < cells; ++i) {
        const int x = i * 7 - 300;
        const int z = i * 3 - 91;
        if ((dropHash(x, 64, z, saltA, nonceA) & 1u) == (dropHash(x, 64, z, saltB, nonceB) & 1u)) {
            ++agreed;
        }
    }
    return agreed;
}

/// 512 cells, so one standard deviation is about 11 and the band below is four
/// of them either side of 256. Wide on purpose: the value is not the point, and
/// a hash that stopped decorrelating would land nowhere near it.
constexpr int kAgreementCells = 512;
constexpr int kSaltAgreement = rollAgreement(kAgreementCells, 0, 1, 0, 0);
constexpr int kNonceAgreement = rollAgreement(kAgreementCells, 0, 0, 1, 2);

static_assert(kSaltAgreement > 210 && kSaltAgreement < 302,
              "two salts at one cell must agree no more often than two coins do - a leaf's "
              "sapling and its apple are supposed to be independent rolls, not correlated ones");
static_assert(kNonceAgreement > 210 && kNonceAgreement < 302,
              "two breaks of one cell must agree no more often than two coins do");

/// A `BlockDrop` with every range and chance settled. Three stacks at most, the
/// same as the input, and fewer whenever a roll came up empty.
struct ResolvedDrop {
    std::array<ItemStack, kMaxDropEntries> stacks{};
    int count = 0;
};

/// Settle `drop` for the block at `(x, y, z)`, broken for the `nonce`th time.
///
/// Pure, and pure *of the position* - see `dropHash`. Two identical breaks of
/// the same cell at the same nonce give the same haul, which is the property
/// `Main.cpp`'s gravel roll already had and the reason none of this reaches for
/// a generator.
///
/// **`nonce` defaults to zero, which is exactly the position-only behaviour**,
/// so every one-shot feature - gravel's flint, an ore's count - keeps the
/// "same cell, same haul" rule it was written with. `resolveBreak` below is
/// what decides which blocks pass a live one.
constexpr ResolvedDrop resolveDrop(const BlockDrop& drop, int x, int y, int z,
                                   std::uint32_t nonce = 0) {
    ResolvedDrop out{};
    for (int i = 0; i < static_cast<int>(drop.count); ++i) {
        const DropEntry entry = drop.entries[static_cast<std::size_t>(i)];
        if (entry.item == ItemId::None || entry.chanceDen == 0) {
            continue;
        }
        // Entries sharing a salt are alternatives off one roll, so an entry
        // owns the slice of that roll its predecessors have not already claimed.
        std::uint32_t claimed = 0;
        for (int j = 0; j < i; ++j) {
            const DropEntry earlier = drop.entries[static_cast<std::size_t>(j)];
            if (earlier.salt == entry.salt) {
                claimed += earlier.chanceNum;
            }
        }
        const std::uint32_t roll = dropHash(x, y, z, entry.salt, nonce) % entry.chanceDen;
        if (roll < claimed || roll >= claimed + entry.chanceNum) {
            continue;
        }
        int amount = entry.min;
        if (entry.max > entry.min) {
            const std::uint32_t span = static_cast<std::uint32_t>(entry.max - entry.min) + 1u;
            amount += static_cast<int>(dropHash(x, y, z, kCountSaltBase + i, nonce) % span);
        }
        // A range that reaches zero - a dead bush's sticks, a stem's seeds -
        // really can come up empty, and an empty stack must not be spawned.
        if (amount <= 0) {
            continue;
        }
        out.stacks[static_cast<std::size_t>(out.count)] = ItemStack{entry.item, amount, 0};
        ++out.count;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Builders. Every row in the table below is one of these, so a malformed entry
// has to be written deliberately rather than by leaving a field out.
// ---------------------------------------------------------------------------

constexpr BlockDrop noDrop() { return BlockDrop{}; }

constexpr BlockDrop oneDrop(ItemId item, int count = 1) {
    if (item == ItemId::None || count <= 0) {
        return BlockDrop{};
    }
    BlockDrop drop{};
    drop.entries[0] = DropEntry{item, static_cast<std::uint8_t>(count),
                                static_cast<std::uint8_t>(count), 0, 1, 1};
    drop.count = 1;
    return drop;
}

constexpr BlockDrop rangeDrop(ItemId item, int low, int high) {
    if (item == ItemId::None) {
        return BlockDrop{};
    }
    BlockDrop drop{};
    drop.entries[0] = DropEntry{item, static_cast<std::uint8_t>(low),
                                static_cast<std::uint8_t>(high), 0, 1, 1};
    drop.count = 1;
    return drop;
}

constexpr BlockDrop chanceDrop(ItemId item, int low, int high, int num, int den, int salt = 0) {
    if (item == ItemId::None) {
        return BlockDrop{};
    }
    BlockDrop drop{};
    drop.entries[0] =
        DropEntry{item,
                  static_cast<std::uint8_t>(low),
                  static_cast<std::uint8_t>(high),
                  static_cast<std::uint8_t>(salt),
                  static_cast<std::uint16_t>(num),
                  static_cast<std::uint16_t>(den)};
    drop.count = 1;
    return drop;
}

/// Append one more entry. Silently ignores an empty item or a full drop, so a
/// conditional row - a leaf's sapling, a potato's poisonous twin - reads as one
/// expression rather than a branch.
constexpr BlockDrop withEntry(BlockDrop drop, DropEntry entry) {
    if (entry.item == ItemId::None || drop.count >= kMaxDropEntries) {
        return drop;
    }
    drop.entries[static_cast<std::size_t>(drop.count)] = entry;
    ++drop.count;
    return drop;
}

// ---------------------------------------------------------------------------
// Leaves - the one place that is genuinely a row per wood.
// ---------------------------------------------------------------------------

/// What one leaf block can shed. Sticks and the 1/50 they come on are the same
/// for every wood, so they are not in here; only what differs is.
struct LeafDrop {
    BlockId leaf;
    /// The sapling, or `ItemId::None` where this wood has no sapling item.
    /// **Mangrove has none in the reference either** - its propagule does not
    /// come off leaves - and cherry has none *here*, because the enum stops at
    /// six saplings. Azalea sheds the bush block rather than a sapling.
    ItemId sapling;
    /// One in this many. Twenty for everything, forty for jungle.
    std::uint16_t saplingDen;
    /// Oak and dark oak, and nothing else.
    bool apples;
};

/// One row per `isLeafBlock` id, asserted below.
constexpr std::array<LeafDrop, 10> kLeafDrops{{
    {BlockId::Leaves, itemForBlock(BlockId::OakSapling), 20, true},
    {BlockId::SpruceLeaves, itemForBlock(BlockId::SpruceSapling), 20, false},
    {BlockId::BirchLeaves, itemForBlock(BlockId::BirchSapling), 20, false},
    {BlockId::JungleLeaves, itemForBlock(BlockId::JungleSapling), 40, false},
    {BlockId::AcaciaLeaves, itemForBlock(BlockId::AcaciaSapling), 20, false},
    {BlockId::DarkOakLeaves, itemForBlock(BlockId::DarkOakSapling), 20, true},
    // **These next two rows say `ItemId::None` for OPPOSITE reasons, and the
    // difference matters because closing one is a fix and closing the other is
    // a bug.** Verified against the reference 2026-08-19 16:07.
    //
    // * **Cherry is a GAP, and the row is already tuned for the fix.** The
    //   reference drops a cherry sapling at **5%**, which is exactly the `20`
    //   this row already carries - so the only thing missing is the id.
    //   `CherrySapling` does not exist: `Block.hpp` has 32 `Cherry` ids -
    //   log, leaves, planks, stripped log, wood, stripped wood - and no
    //   sapling, and `Item.hpp` has none either. **Whoever adds the id should
    //   change this `ItemId::None` to `itemForBlock(BlockId::CherrySapling)`
    //   and nothing else**, because the rate is already right. Until then
    //   cherry trees are non-renewable: the leaves shed sticks only.
    // * **Mangrove is CORRECT and must be left alone.** The reference gives
    //   mangrove leaves no sapling at all - mangroves propagate by
    //   *propagules*, which hang from the tree as their own block and are
    //   collected there, not shed by the leaves. `Propagule` returns 0 in
    //   `Block.hpp`, so that block does not exist here either, but adding a
    //   `MangroveSapling` would be inventing a drop the reference does not
    //   have. **Do not mirror the cherry fix onto this row.**
    //
    // Both rows are present rather than absent so the coverage assert below
    // stays honest. Source tier: SECONDARY (wiki drop tables); leaf drop
    // chances are not in the behaviour-pack JSON.
    {BlockId::CherryLeaves, ItemId::None, 20, false},
    {BlockId::MangroveLeaves, ItemId::None, 20, false},
    {BlockId::AzaleaLeaves, itemForBlock(BlockId::Azalea), 20, false},
    {BlockId::FloweringAzaleaLeaves, itemForBlock(BlockId::FloweringAzalea), 20, false},
}};

/// The row for a leaf, or the oak one for anything that is not a leaf. Callers
/// ask `isLeafBlock` first; the fallback exists so this is total.
constexpr const LeafDrop& leafRowFor(BlockId leaf) {
    for (const LeafDrop& row : kLeafDrops) {
        if (row.leaf == leaf) {
            return row;
        }
    }
    return kLeafDrops[0];
}

/// Whether every `isLeafBlock` id has a row. **This is the assert that stops
/// leaf number eleven silently dropping itself**, which is exactly what the
/// other nine do today.
constexpr bool everyLeafHasARow() {
    for (int id = 0; id < static_cast<int>(kBlockIdCount); ++id) {
        const BlockId block = static_cast<BlockId>(id);
        if (!isLeafBlock(block)) {
            continue;
        }
        bool found = false;
        for (const LeafDrop& row : kLeafDrops) {
            if (row.leaf == block) {
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static_assert(everyLeafHasARow(), "a leaf without a row in kLeafDrops silently drops itself");

/// A leaf's two other rolls, named here so that the ladders below and the
/// entries in `leafDrop` read the same literal rather than two copies of it.
/// **Unit: one in this many, per broken leaf**, which is what `DropEntry`'s
/// `chanceDen` means. 2% for sticks and 0.5% for an apple.
constexpr std::uint16_t kLeafStickDen = 50;
constexpr std::uint16_t kLeafAppleDen = 200;

/// **Fortune moves all three of a leaf's rolls, and the reference publishes a
/// separate ladder for each.** Without this every leaf paid its Fortune-0
/// chance whatever the tool carried, which made Fortune worth precisely
/// nothing on the one block a player farms it on.
///
/// Each row is `{ level 0, I, II, III }` and **the head of each row is the
/// base it answers for** - that is what `leafChanceDen` matches on, and what
/// `everyLeafDenominatorHasALadder` proves is total, so the fall-through at
/// the bottom of that function is provably dead rather than a second place for
/// a missing entry to hide. Denominators, not percentages, because that is the
/// unit `chanceDen` is in; the reference's own figures are beside each.
///
/// | roll                    |    0 |    I |   II |  III |
/// |-------------------------|------|------|------|------|
/// | sapling, all but jungle |   20 |   16 |   12 |   10 |  5% -> 10%
/// | jungle sapling          |   40 |   36 |   32 |   24 |  2.5% -> 4.17%
/// | sticks                  |   50 |   45 |   40 |   30 |  2% -> 3.33%
/// | apple, oak and dark oak |  200 |  180 |  160 |  120 |  0.5% -> 0.833%
///
/// <https://minecraft.wiki/w/Leaves>
///
/// > The jungle sapling is a *different shape*, not a scaled copy: the other
/// > three ladders all step 9/10, 4/5, 3/5 of their base and the common sapling
/// > steps 4/5, 3/5, 1/2. Deriving one from the other would be right for three
/// > rows and wrong for the fourth, which is why all four are written out.
constexpr std::array<std::array<std::uint16_t, 4>, 4> kLeafFortuneLadders{{
    {{20, 16, 12, 10}},
    {{40, 36, 32, 24}},
    {{kLeafStickDen, 45, 40, 30}},
    {{kLeafAppleDen, 180, 160, 120}},
}};

/// The denominator one of a leaf's rolls uses at a given Fortune level.
constexpr std::uint16_t leafChanceDen(std::uint16_t base, int fortune) {
    const int level = fortune < 0 ? 0 : (fortune > 3 ? 3 : fortune);
    for (const std::array<std::uint16_t, 4>& ladder : kLeafFortuneLadders) {
        if (ladder[0] == base) {
            return ladder[static_cast<std::size_t>(level)];
        }
    }
    return base;
}

/// Whether every base a leaf actually asks for has a ladder - the two literals
/// above and every `saplingDen` in the table. **This is what makes the
/// fall-through in `leafChanceDen` unreachable**, so adding an eleventh leaf
/// with a new base fails the build here rather than quietly leaving that leaf
/// deaf to Fortune.
constexpr bool everyLeafDenominatorHasALadder() {
    if (leafChanceDen(kLeafStickDen, 3) == kLeafStickDen ||
        leafChanceDen(kLeafAppleDen, 3) == kLeafAppleDen) {
        return false;
    }
    for (const LeafDrop& row : kLeafDrops) {
        if (leafChanceDen(row.saplingDen, 3) == row.saplingDen) {
            return false;
        }
    }
    return true;
}

static_assert(everyLeafDenominatorHasALadder(),
              "a leaf roll whose base has no ladder pays its Fortune-0 chance at every level, "
              "which is the bug this table exists to close");

// ---------------------------------------------------------------------------
// The table.
// ---------------------------------------------------------------------------

/// True where the reference says a break yields nothing at all, whatever you
/// are holding.
///
/// **Asked before every family rule**, because most of these would otherwise be
/// caught further down and hand back a souvenir - which is precisely what they
/// do today. Two of them are duplication exploits rather than tidiness: a
/// monster spawner that drops itself is an unlimited supply of spawners, and
/// budding amethyst that drops itself is an unlimited supply of amethyst.
constexpr bool yieldsNothingEver(BlockId block) {
    // Glass shatters. Tinted glass is the reference's single exception and is
    // deliberately not here. The panes go with it: all seventeen pane families
    // are cut from glass, so `isPane` is the whole test.
    if ((isGlassBlock(block) && block != BlockId::TintedGlass) || isPane(block)) {
        return true;
    }
    // Ice is never carried without Silk Touch. **It also becomes water where
    // something solid sat under it, and that half is not this file's to do** -
    // an earlier comment here said it was. See the deliberate-gaps block near
    // the bottom. <https://minecraft.wiki/w/Ice>
    //
    // Powder snow is a fourth kind of nothing: a bucket collects it and no
    // enchantment does, so unlike the three ices it is absent from
    // `silkTouchRecovers`. <https://minecraft.wiki/w/Powder_Snow>
    if (block == BlockId::Ice || block == BlockId::PackedIce || block == BlockId::BlueIce ||
        block == BlockId::PowderSnow) {
        return true;
    }
    // Live coral and its fans die out of water rather than being collected, and
    // the dead ones are no more collectable than the live ones: *"dead coral
    // can be broken instantly with any tool. It can only be obtained with any
    // Silk Touch enchanted tool [BE]"*, and the same for dead coral fans. The
    // `&& !isDeadCoral(block)` that used to be here let all ten dead plants and
    // fans through to the tail line and hand themselves back for free - a rule
    // stated for one half of a family and not the other.
    // The coral *blocks* are not here - they hand back their dead form below,
    // and a dead coral block really does drop itself.
    // <https://minecraft.wiki/w/Dead_Coral>,
    // <https://minecraft.wiki/w/Dead_Coral_Fan>
    if (isCoralPlant(block) || isCoralFan(block)) {
        return true;
    }
    switch (block) {
    // The whole sculk family. Every one of them pays in experience, which does
    // not exist yet, so for now they pay nothing.
    case BlockId::Sculk:
    case BlockId::SculkCatalyst:
    case BlockId::SculkVein:
    case BlockId::SculkSensor:
    case BlockId::SculkShrieker:
    // A spawner and budding amethyst are the two duplication exploits: both
    // drop themselves today and both are infinite-resource machines.
    case BlockId::MonsterSpawner:
    case BlockId::BuddingAmethyst:
    // The buds are the growth of the budding block, not three more items.
    case BlockId::SmallAmethystBud:
    case BlockId::MediumAmethystBud:
    case BlockId::LargeAmethystBud:
    // Archaeology: brushing them is the point, and breaking one destroys it.
    case BlockId::SuspiciousSand:
    case BlockId::SuspiciousGravel:
    // A mushroom block's stem carries no mushroom.
    case BlockId::MushroomStem:
    // **A cave vine without berries, which is the other half of a pair whose
    // first half was right by accident.** `CaveVinesBerries` reaches the tail
    // line and `itemForBlock` turns it into glow berries, so the berry-bearing
    // vine has always been correct; the berry-less one reached the same line
    // and handed back a *cave vines* block item, which is not an item the
    // reference has at all - the thing you plant is the berry. *"A cave vine
    // can be broken by hand or with any tool, yielding one unit of glow berries
    // if the vine has berries or nothing when it does not. This is not affected
    // by Fortune."* Silk Touch is not an exception either, so this is the right
    // list rather than `silkTouchRecovers`.
    // <https://minecraft.wiki/w/Glow_Berries>
    case BlockId::CaveVines:
    // The books it holds always drop; the shelf itself does not.
    case BlockId::ChiseledBookshelf:
    // **Reinforced deepslate joins them**, and it is a third kind of "never":
    // not an exploit and not a shatter, simply a block the reference does not
    // let you have - *"reinforced deepslate is not obtainable in Survival, even
    // with Silk Touch"*, *"reinforced deepslate drops nothing when destroyed"*.
    // It used to fall to the tail line and hand itself back, which - with the
    // iron gate `Mining.hpp` was giving it - made the strongest block in the
    // game an ordinary mining reward.
    // <https://minecraft.wiki/w/Reinforced_Deepslate>
    case BlockId::ReinforcedDeepslate:
    // **And the end portal frame is that same rule, one block over** - the
    // reference puts it in the same sentence as bedrock: *"end portal frames,
    // like bedrock, are unobtainable and unbreakable in Survival or Adventure
    // mode"*, and its hardness is the reference's -1. It reached the tail line
    // and handed itself back for the whole of this table's life, which nothing
    // could see because `Mining.hpp` gives it `kUnbreakableHardness` and no
    // swing ever finishes - a drop nobody can reach is still a drop the *next*
    // caller reaches, and this table is read by the blast and by
    // `primaryDrop` as well as by the break. Bedrock itself never had the bug
    // for the dullest possible reason: `itemForBlock(Bedrock)` is `None`, so
    // the tail line already answered nothing. **That is luck, not a rule** -
    // the frame has an item because it is in the creative catalogue, and one
    // of the two unbreakable blocks in the game having an item is exactly the
    // difference. `everyUnbreakableBlockYieldsNothing` below is the rule.
    // <https://minecraft.wiki/w/End_Portal_Frame>, <https://minecraft.wiki/w/Bedrock>
    case BlockId::EndPortalFrame:
        return true;
    default:
        return false;
    }
}

/// The blocks Silk Touch hands back whole, where every other break gives
/// something else or nothing at all.
///
/// **One list, asked before `yieldsNothingEver`**, because the block that
/// yields nothing to a fist and itself to Silk Touch is the same block, and
/// the old order meant the second half could never be reached: glass, ice,
/// coral and the sculk family were swallowed by the "nothing, ever" test long
/// before `ctx.silkTouch` was consulted, so the generated table read
/// `dropsSilkTouch = (nothing)` for all seventy-odd of them. This is *the*
/// reason to keep the list separate from `yieldsNothingEver` rather than
/// widening it: "nothing ever" now means what it says, and its remaining
/// members - a spawner, budding amethyst, the two suspicious blocks, reinforced
/// deepslate - are the ones the reference really does refuse to Silk Touch.
/// <https://minecraft.wiki/w/Silk_Touch>
///
/// > **This does not make anything reachable in play today.** No enchantment
/// > exists in this project and `breakContextFor` passes `silkTouch = false`,
/// > so every row here is dormant. It is written because the flag is read by
/// > forty-seven other rows already and a table that answers *half* a question
/// > is the harder thing to correct later - and because the ground-truth dump
/// > has a `dropsSilkTouch` column, which was lying.
///
/// **The exclusions are the interesting half**, and each is quoted where it is
/// named: powder snow (a bucket, not an enchantment), budding amethyst
/// (*"using a tool enchanted with Silk Touch does not drop anything"*), a
/// monster spawner (*"cannot be obtained in Survival, even with Silk Touch"*),
/// the two suspicious blocks (*"can be obtained only through the Creative
/// inventory"* in Bedrock), reinforced deepslate (*"not obtainable in Survival,
/// even with Silk Touch"*), a chorus plant (*"the item form cannot be obtained
/// in Survival mode, even with the Silk Touch enchantment"*), and farmland and
/// the dirt path, which hand back dirt however they are broken.
constexpr bool silkTouchRecovers(BlockId block) {
    // Glass shatters unless it does not: *"glass drops itself only if it is
    // broken with a tool enchanted with Silk Touch"*, and the panes say the
    // same. Tinted glass is left out because it always drops itself and the
    // tail line already gives it back - naming it here would be a second owner
    // for an answer that is already right. 1 + 16 glass and 17 pane families.
    // <https://minecraft.wiki/w/Glass>, <https://minecraft.wiki/w/Glass_Pane>
    if ((isGlassBlock(block) && block != BlockId::TintedGlass) || isPane(block)) {
        return true;
    }
    // *"Ice can be easily destroyed without tools... However, the block drops
    // only when using a tool enchanted with Silk Touch."* Packed and blue ice
    // read the same. Powder snow is deliberately absent - it is collected with
    // a bucket and Silk Touch does nothing for it.
    // <https://minecraft.wiki/w/Ice>, <https://minecraft.wiki/w/Packed_Ice>,
    // <https://minecraft.wiki/w/Blue_Ice>
    if (block == BlockId::Ice || block == BlockId::PackedIce || block == BlockId::BlueIce) {
        return true;
    }
    // Live and dead alike: *"coral can be broken instantly by hand, but can be
    // obtained only when mined with a Silk Touch enchanted tool"*, and dead
    // coral is the same in Bedrock (Java asks a Silk Touch *pickaxe*, which is
    // the one edition split in this group and not the edition we follow).
    // <https://minecraft.wiki/w/Coral>, <https://minecraft.wiki/w/Dead_Coral>
    if (isCoralPlant(block) || isCoralFan(block)) {
        return true;
    }
    // **The living coral *block* is the third form of the same rule** and was
    // the one that did not travel: *"coral blocks drop themselves only when
    // mined with a tool enchanted with Silk Touch"*, and otherwise they drop
    // the dead variant, which the `deadCoralFor` clause in `dropsForBlock`
    // already pays out. `isDeadCoral` is excluded because a dead block hands itself back
    // however it is broken, and naming it here would be a second owner for an
    // answer the tail line already gets right. <https://minecraft.wiki/w/Coral_Block>
    if (isCoralBlock(block) && !isDeadCoral(block)) {
        return true;
    }
    // **Every ore in the game**, which is the largest single thing this list was
    // missing: *"if mined with a pickaxe enchanted with Silk Touch, the ore
    // block drops itself instead of its resource"*, and it reads the same for
    // all eight overworld ores, their eight deepslate twins and the two nether
    // ores. `isOre` is the one table that owns "is this an ore" - deriving it
    // here as a case list would be a second one - and it also carries ancient
    // debris, which drops itself either way and is therefore unchanged by
    // being named. The nether pair sit outside `isOre` because nothing in the
    // terrain generator veins them, so they are spelled out.
    // <https://minecraft.wiki/w/Silk_Touch>, <https://minecraft.wiki/w/Ore>
    if (isOre(block) || block == BlockId::NetherGoldOre || block == BlockId::NetherQuartzOre) {
        return true;
    }
    switch (block) {
    // The whole sculk family, all five: *"it drops itself only if mined with
    // any tool enchanted with Silk Touch"*. **The vein included** - what makes
    // the vein different is that it is the only one of the five that pays no
    // experience without Silk Touch, not that it refuses the enchantment.
    // <https://minecraft.wiki/w/Sculk>, <https://minecraft.wiki/w/Sculk_Vein>
    case BlockId::Sculk:
    case BlockId::SculkCatalyst:
    case BlockId::SculkVein:
    case BlockId::SculkSensor:
    case BlockId::SculkShrieker:
    // *"The amethyst cluster block drops itself in any of its growth stages
    // (rather than its amethyst shard drops) when mined with any tool with the
    // Silk Touch enchantment."* The three buds are those growth stages; the
    // budding block that grows them is not, and stays a "nothing, ever".
    // <https://minecraft.wiki/w/Amethyst_Cluster>
    case BlockId::SmallAmethystBud:
    case BlockId::MediumAmethystBud:
    case BlockId::LargeAmethystBud:
    // *"The blocks themselves can be retrieved only by using a tool enchanted
    // with Silk Touch. Mining the mushroom cap or stem yields a block with the
    // cap or stem texture."* All three, cap and stem alike.
    // <https://minecraft.wiki/w/Mushroom_Block>
    case BlockId::BrownMushroomBlock:
    case BlockId::RedMushroomBlock:
    case BlockId::MushroomStem:
    // *"The block itself drops only when broken using a tool enchanted with
    // Silk Touch"* - and an axe changes the *speed*, nothing else, which is the
    // half a finding against this file had backwards.
    // <https://minecraft.wiki/w/Chiseled_Bookshelf>
    case BlockId::ChiseledBookshelf:
    // *"When broken by a tool enchanted with Silk Touch, a bookshelf drops
    // itself"*, against the three books it gives otherwise.
    // <https://minecraft.wiki/w/Bookshelf>
    case BlockId::Bookshelf:
    // *"A grass block can be obtained by mining it using a tool enchanted with
    // Silk Touch. Otherwise, it drops dirt."* Podzol and mycelium read the
    // same. Farmland and the dirt path are **not** here: both hand back dirt
    // whatever you hold, so the tail line is already right for them.
    // <https://minecraft.wiki/w/Grass_Block>, <https://minecraft.wiki/w/Podzol>,
    // <https://minecraft.wiki/w/Mycelium>
    case BlockId::Grass:
    case BlockId::Podzol:
    case BlockId::Mycelium:
    // *"Gravel drops itself when destroyed using a tool with Silk Touch"* -
    // which is the one row here that changes an answer rather than creating
    // one: without it, the flint roll below runs anyway.
    // <https://minecraft.wiki/w/Gravel>
    case BlockId::Gravel:
    // *"If mined with a tool enchanted with Silk Touch, the soul campfire
    // instead drops itself as an item."*
    // <https://minecraft.wiki/w/Soul_Campfire>
    case BlockId::SoulCampfire:
    // **And the ordinary campfire, which is the same sentence** - *"if mined
    // with a tool enchanted with Silk Touch, the campfire drops itself"* - and
    // was left out when its soul twin was written. One of a pair derived and
    // the other not is the shape that has cost this project most.
    // <https://minecraft.wiki/w/Campfire>
    case BlockId::Campfire:
    // *"Stone drops cobblestone... unless mined with Silk Touch, in which case
    // it drops itself."* Deepslate reads the same against cobbled deepslate.
    // <https://minecraft.wiki/w/Stone>, <https://minecraft.wiki/w/Deepslate>
    case BlockId::Stone:
    case BlockId::Deepslate:
    // *"Glowstone drops 2-4 glowstone dust... mining it with a Silk Touch tool
    // drops the block itself."* <https://minecraft.wiki/w/Glowstone>
    case BlockId::Glowstone:
    // *"Clay drops 4 clay balls, or itself if mined with a Silk Touch tool."*
    // <https://minecraft.wiki/w/Clay>
    case BlockId::Clay:
    // *"Sea lanterns drop 2-3 prismarine crystals... using Silk Touch drops the
    // block itself."* <https://minecraft.wiki/w/Sea_Lantern>
    case BlockId::SeaLantern:
    // *"A melon drops 3-7 melon slices... a melon mined with Silk Touch drops
    // itself."* <https://minecraft.wiki/w/Melon>
    case BlockId::Melon:
    // *"The amethyst cluster block drops itself... when mined with any tool
    // with the Silk Touch enchantment."* **The three buds are already above**
    // and the full-grown cluster - the only one of the four that pays shards -
    // was the one left out. <https://minecraft.wiki/w/Amethyst_Cluster>
    case BlockId::AmethystCluster:
    // *"Nylium drops netherrack unless mined with a Silk Touch tool, in which
    // case it drops itself."* Both colours.
    // <https://minecraft.wiki/w/Nylium>
    case BlockId::CrimsonNylium:
    case BlockId::WarpedNylium:
    // *"If a gilded blackstone is mined with a Silk Touch enchanted pickaxe, it
    // always drops itself"* - which is the half of its roll that is certain.
    // The other half is the nugget chance beside `Gravel` below.
    // <https://minecraft.wiki/w/Gilded_Blackstone>
    case BlockId::GildedBlackstone:
        return true;
    default:
        return false;
    }
}

/// What a leaf block sheds.
constexpr BlockDrop leafDrop(BlockId block, const BreakContext& ctx) {
    // Shears take the leaf itself, and so would Silk Touch. This is also the
    // rule a blast follows: the reference's blast uses "an empty hand where the
    // correct tool would be shears", so an exploded leaf rolls its chances.
    if (ctx.tool == ToolKind::Shears || ctx.silkTouch) {
        return oneDrop(itemForBlock(block));
    }
    const LeafDrop& row = leafRowFor(block);
    BlockDrop drop{};
    // Three independent rolls, three salts - one leaf really can shed a
    // sapling, a stick and an apple at once.
    // **Each roll climbs its own Fortune ladder**, which is the whole of what
    // the enchantment does to a leaf - the counts never move, only the chances.
    drop = withEntry(drop,
                     DropEntry{row.sapling, 1, 1, 0, 1, leafChanceDen(row.saplingDen, ctx.fortune)});
    drop = withEntry(drop,
                     DropEntry{ItemId::Stick, 1, 2, 1, 1, leafChanceDen(kLeafStickDen, ctx.fortune)});
    if (row.apples) {
        drop = withEntry(
            drop, DropEntry{ItemId::Apple, 1, 1, 2, 1, leafChanceDen(kLeafAppleDen, ctx.fortune)});
    }
    return drop;
}

/// What a crop yields, by family and by age.
///
/// **Bedrock's counts, and this is where the two editions differ most**, so
/// every row below is the Bedrock one and the Java number is named beside it:
/// a ripe wheat pays one wheat and **0-3** seeds where Java pays 1-4, and a
/// ripe beetroot pays **1-2** beetroots where Java pays one. Carrots and
/// potatoes are 2-5 in both, and the potato's poisonous one is an extra 2% on
/// top of that. (minecraft.wiki, *Carrot*: "Fully grown carrot crops drop 2 to
/// 5 carrots (3 5/7 per crop harvested on average)"; *Beetroot*: "drops 1
/// beetroot [JE] or 1 to 2 beetroots [BE] and 1 to 4 beetroot seeds";
/// *Wheat Seeds*: "between 1 and 4 seeds ... in Java Edition, or between 0 and
/// 3 seeds in Bedrock Edition".)
///
/// **Every range is the reference's; only the shape inside it is uniform where
/// the reference's is binomial**, because a `min`/`max` pair cannot say "two
/// are fixed, then three more attempts at 57%". That costs the mean and nothing
/// else: a carrot averages 3.5 here against the reference's 3 5/7, and a
/// beetroot's seeds 2.5 against 2 5/7. Named here rather than hidden - it is
/// the one place in this table where the shape of the distribution differs
/// rather than the numbers.
constexpr BlockDrop cropDrop(BlockId block) {
    const BlockId family = cropFamily(block);
    const bool ripe = cropAge(block) >= 7;
    if (family == BlockId::WheatCrop0) {
        if (!ripe) {
            return oneDrop(ItemId::WheatSeeds);
        }
        return withEntry(oneDrop(ItemId::Wheat),
                         DropEntry{ItemId::WheatSeeds, 0, 3, 1, 1, 1});
    }
    if (family == BlockId::CarrotCrop0) {
        return ripe ? rangeDrop(ItemId::Carrot, 2, 5) : oneDrop(ItemId::Carrot);
    }
    if (family == BlockId::PotatoCrop0) {
        if (!ripe) {
            return oneDrop(ItemId::Potato);
        }
        // The poisonous one is an *extra*, on its own 2% roll, not a
        // replacement - so it is a second entry with a second salt.
        return withEntry(rangeDrop(ItemId::Potato, 2, 5),
                         DropEntry{ItemId::PoisonousPotato, 1, 1, 1, 1, 50});
    }
    if (!ripe) {
        return oneDrop(ItemId::BeetrootSeeds);
    }
    // **1-4 seeds, not 0-3.** A beetroot's seed row is not the wheat row: the
    // wheat one is the bare `Binomial(3, 4/7)`, and this one has a fixed seed
    // under it, so a harvest can never leave you with nothing to replant.
    return withEntry(rangeDrop(ItemId::Beetroot, 1, 2),
                     DropEntry{ItemId::BeetrootSeeds, 1, 4, 1, 1, 1});
}

/// Where the reference's *correct* tool is shears.
///
/// **This exists for the blast rule and nothing else.** A blast breaks a block
/// "as an unenchanted diamond tool would, or as an empty hand where the correct
/// tool would be shears", and the second half is the half a `ToolKind` cannot
/// say - the caller can pass the diamond tool, but it cannot pass "and by the
/// way, not for this one". Every id here is a row in `dropsForBlock` below, so
/// this list is read off that function rather than off the block's mining-row
/// tool field, which lives in `Mining.hpp` - a header this one deliberately
/// does not include, because it derives a row for every block id in every
/// translation unit that does.
constexpr bool shearsHarvests(BlockId block) {
    return block == BlockId::Cobweb || isLeafBlock(block) || isVine(block) ||
           block == BlockId::GlowLichen || block == BlockId::HangingRoots ||
           block == BlockId::SmallDripleaf || block == BlockId::Seagrass ||
           block == BlockId::DeadBush || block == BlockId::TallGrass ||
           block == BlockId::Fern || block == BlockId::LargeFern ||
           // The three nether plants, added with their rows below. A blast
           // therefore breaks nether sprouts to nothing and rolls the two
           // vines' 33% rather than handing back a guaranteed one, which is
           // what the reference's *"an empty hand where the correct tool would
           // be shears"* says for them.
           block == BlockId::NetherSprouts || block == BlockId::TwistingVines ||
           block == BlockId::WeepingVines;
}

/// What breaking `block` yields, before the rolls are resolved.
///
/// **The tier gate is not here.** `yieldsDrop` still decides whether the tool is
/// good enough; this decides what falls once it is, and the tool rows below are
/// only the ones where the *kind* of tool changes the answer rather than
/// gating it.
///
/// **Both halves of a two-cell plant return a drop, and that is deliberate.**
/// A lilac is `LilacLower` + `LilacUpper`, and this function hands back one
/// lilac for *each* - so read from this file alone it looks like a doubling
/// bug, and all four tall flowers "fail" the same way. It is not a bug, and
/// zeroing the upper row would be an item-*loss* bug: the pairing is owned by
/// the caller, not the table. `Main.cpp`'s `clearPairedHalf` empties the twin
/// without paying for it, so whichever half the player breaks is the half that
/// pays and the other is taken silently. Make the upper row barren and breaking
/// a lilac from the top yields nothing at all.
///
/// **Measured on the blast path, because that is the one that destroys both
/// cells in the same batch** (2026-08-18, probe linking the real
/// `explosionBlocks` against a stub world holding 25 lilacs, all 50 cells
/// inside the blast): 25 lilacs out, one per plant. The reason it is safe is a
/// single line in that loop - `world.blockAt` is re-read *inside* the loop, so
/// by the time it reaches the twin's cell the twin is already `Air`, and `Air`
/// drops nothing. The control replayed the identical loop with the ids
/// snapshotted up front instead and produced 50, so the measurement can see a
/// doubling when there is one; single-cell dandelions in the same blast stayed
/// at 24 in both replays, so the control doubles the paired plant rather than
/// everything.
///
/// > So the invariant this table depends on is **"the destroy loop re-reads
/// > each cell"**, not anything in this file. If a caller is ever written that
/// > snapshots block ids before clearing them, it double-pays every tall
/// > flower, every door and every bed, and nothing here will stop it.
///
/// **The candle count is player-visible, not the stored state, and that is the
/// one place Mojang's block metadata reaches this file.**
/// `metadata/vanilladata_modules/mojang-blocks.json` publishes `candles` as
/// `[0,1,2,3]` - **zero-based storage**, so a four-candle block stores 3. The
/// identity tail below returns `dropCountForBlock`, which forwards a candle to
/// `candleCount`; had that returned the stored value, a single candle would
/// drop **nothing** and a four-stack would drop three. Silent loss on 136 ids,
/// and it would read as perfectly correct.
///
/// Measured 2026-08-19 rather than reasoned: 136 candle ids swept, 0 dropping
/// zero, 0 disagreeing with `candleCount`, observed range exactly **1..4**.
/// The control re-took the same reading zero-based and found **34** ids that
/// would then drop nothing - so the check can produce a one, which is what
/// makes its zero worth having.
///
/// > **The trap in that source, because it produces confident wrong numbers.**
/// > A property's published value list is its *storage domain*, not any one
/// > block's range; `data_items` says which properties a block carries and
/// > never narrows them. The rule is **count the users**: one user means
/// > authoritative, more than one means you have only learned how wide the
/// > field is. `candles` has **17** users, so the `[0,1,2,3]` above is a
/// > domain, and it is `candleCount` and the measurement - not the JSON - that
/// > pin our range. By contrast `cluster_count` has exactly **one** user
/// > (`sea_pickle`), so its zero-basing *is* authoritative - which costs us
/// > nothing here only because `SeaPickle` is a single id with no cluster
/// > state, so it drops 1 and cannot be off by one.
constexpr BlockDrop dropsForBlock(BlockId block, BreakContext ctx = {}) {
    // **The blast flag, read.** The caller passes an unenchanted diamond
    // pickaxe, which is the first half of the reference's rule and stands in
    // exactly for the diamond tool on every conditional row this table has
    // today. The second half is this line: where shears are the correct tool,
    // a blast breaks the block with an empty hand instead. It changes nothing
    // now - a pickaxe already falls through every shears row to the same
    // answer - and it is here so that the first row where the two differ, a
    // shovel on snow or any Silk Touch row, cannot silently ignore the flag
    // the way it would have to if this lived at the call site.
    //
    // > **"A shovel on snow" is not hypothetical any more - and it turned out
    // > not to be a defect at all.** Measured 2026-08-19 over all 3314 ids: a
    // > blast loses the drop on **9 snow ids** - Snow, Snow Block and seven Top
    // > Snow depths - plus Cobweb. It happens at `canHarvest`, not here, and
    // > finding 2029 carried it.
    // >
    // > **2029 is now resolved, and it resolved the other way: all ten are
    // > CORRECT and only the comment describing them was false.** Three
    // > reference facts pin it, and exactly one model satisfies all three -
    // > exploded stone gives cobblestone, exploded cobweb gives nothing,
    // > exploded snow gives nothing. "Harvests with the correct tool" hands a
    // > cobweb its shears and fails fact two; "empty hand with the harvest gate
    // > applied" drops nothing from stone and fails fact one; **"the block's own
    // > loot table with no tool, harvest gate skipped"** satisfies all three,
    // > because stone's table names no tool while cobweb's wants shears and
    // > snow's wants a shovel.
    // >
    // > **So do not "fix" the nine snow ids, and in particular do not change
    // > the blast context to `miningRow(block).tool`** - that was 2029's own
    // > original proposal and it is a trap: asking each block for its required
    // > kind hands a cobweb its shears and starts dropping **the web itself**
    // > from every exploded cobweb, breaking a case the reference pins, to
    // > repair a case that was never broken. It would also pass any "does
    // > exploded snow drop now?" check written to test it.
    // >
    // > *Corrected 2026-08-19 11:12, and the correction is worth more than the
    // > line it replaced. This paragraph said "string" until now. String is
    // > what a **sword** yields; **shears yield the web itself**, and the
    // > three-way `static_assert` at the foot of this file has pinned both
    // > answers the whole time. `miningRow(Cobweb).tool` is `ToolKind::Shears`
    // > (set by the `Cobweb` branch of `derivedTool` in `Mining.hpp` - cited by
    // > symbol because the line this named, 1397, now holds an unrelated
    // > `case BlockId::HayBlock:`), and **no mining row in the game names a
    // > sword**, so string was never the reachable failure at all.*
    // >
    // > *The trap itself is unchanged and real - a blast must yield nothing,
    // > and either item breaks that. But the **symptom** was wrong, and a trap
    // > whose predicted symptom does not match what the reader sees is a trap
    // > they will talk themselves past: "I was warned about string, I am
    // > looking at webs, so this must not be the case I was warned about."
    // > That is the specific way a correct warning gets discarded.*
    // >
    // > Residual risk, inherited from 2029 and repeated because a reader who
    // > only sees this file would not otherwise know: `bedrock-samples`
    // > publishes no `blocks/` and no `loot_tables/blocks/`, so the model above
    // > rests on that three-fact set rather than on a primary source. If
    // > exploded cobweb is ever shown to drop **anything at all** - the web,
    // > string, or otherwise - the derivation collapses and the nine snow ids
    // > become real. **Re-derive from the fact set, not from this line.**
    // >
    // > *This falsifier also said "string" until 2026-08-19 11:12, and that was
    // > the more dangerous of the two errors. A falsifier naming the wrong
    // > observation is worse than no falsifier: a reader who watched exploded
    // > webs drop **webs** would have checked it against "string", seen no
    // > match, and concluded the derivation had **held** - taking the one
    // > observation that refutes this model as confirmation of it.*
    // >
    // > **The split, because it cost three probes to find and it will cost the
    // > next reader the same.** This function is the *loot table*: what a block
    // > yields once you are allowed to have it. It is **not** the harvest gate.
    // > `canHarvest` is, and the **caller** applies it. The magnitudes say it
    // > plainly - across every id, `dropsForBlock` differs pickaxe-vs-hand on
    // > **11** ids while `canHarvest` differs on **731**, a 66x gap. Stone
    // > bare-handed returns cobblestone from this table and is refused by
    // > `canHarvest`; that is correct and deliberate, and it means **a clean
    // > answer from here proves nothing about what the player receives.**
    // >
    // > So a sweep that asks *this* function whether some tool loses a drop
    // > gets a truthful zero to a question nobody asked. Mine did. Ask
    // > `canHarvest` as well, and treat any control under about 700 on a
    // > pickaxe-vs-hand comparison as evidence you are on the wrong door.
    //
    // **It has to run before the two tests below**, which is why they moved
    // rather than it: clearing `silkTouch` after the Silk Touch row has already
    // answered would be a flag read too late.
    if (ctx.explosion && shearsHarvests(block)) {
        ctx.tool = ToolKind::None;
        ctx.tier = kHandTier;
        ctx.silkTouch = false;
    }

    // **Silk Touch, before "nothing, ever".** See `silkTouchRecovers`: for
    // about seventy ids the reference's answer is "nothing to anything else,
    // itself to this", and asking the "nothing" half first meant the other half
    // was unreachable code.
    if (ctx.silkTouch && silkTouchRecovers(block)) {
        return oneDrop(itemForBlock(block));
    }

    // The deepslate half of an ore yields exactly what the stone half does -
    // **counts included**. `dropForBlock` collapsed the identity and
    // `dropCountForBlock` did not, so deepslate lapis paid one lapis where
    // stone lapis paid six. Collapsing once, here, is what stops that shape
    // coming back.
    //
    // **It sits below the Silk Touch branch and not above it**, which is the
    // one answer where the two halves are genuinely different blocks: a
    // silk-mined deepslate diamond ore is a *deepslate* diamond ore. Collapsing
    // first handed back the stone one - a substitution the reference never
    // makes, and one no resource row can see because every resource row wants
    // the collapse. `everyOreComesBackWholeToSilkTouch` is the assert that
    // caught it and the assert that keeps the order.
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }

    if (yieldsNothingEver(block)) {
        return noDrop();
    }

    // ---- The tool decides *what*, not just whether. ----
    // The reference's whole "shears give you X instead of Y" family, which a
    // binary tier gate cannot say at all.
    if (block == BlockId::Cobweb) {
        if (ctx.tool == ToolKind::Shears || ctx.silkTouch) {
            return oneDrop(itemForBlock(BlockId::Cobweb));
        }
        // A sword cuts the web into string instead of collecting it, and
        // `mining::alsoCollects` is the half of that rule that gets the swing
        // past `canHarvest` - the row names shears, so without it the sword
        // would never reach this line.
        //
        // **An exploded web reaches it holding nothing and pays nothing**, and
        // that is the blast rule at the top of this function doing its job: a
        // cobweb is in `shearsHarvests`, so the flag has already emptied the
        // hand by the time the two tests above are asked. An earlier version of
        // this comment claimed the opposite - that a blast gives string "exactly
        // as a sword does" - which is not what the code does and not what the
        // reference does; the assert beside the blast rule pins the real answer.
        if (ctx.tool == ToolKind::Sword) {
            return oneDrop(ItemId::String);
        }
        return noDrop();
    }
    // Shears or nothing. Every one of these comes away whole with shears and is
    // simply destroyed by anything else.
    if (block == BlockId::GlowLichen || block == BlockId::HangingRoots ||
        block == BlockId::SmallDripleaf || block == BlockId::Seagrass) {
        return (ctx.tool == ToolKind::Shears || ctx.silkTouch) ? oneDrop(itemForBlock(block))
                                                               : noDrop();
    }
    // Vines are the same rule with two twists. **Through `dropForBlock`**,
    // because a vine's sixteen connection ids are one item and that mapping
    // already lives there; and **without the Silk Touch escape**, because the
    // reference refuses even that (minecraft.wiki, *Vines*: "Vines can be
    // destroyed with any item, but using shears is the only way to collect
    // them ... Using an axe on vines can also increase efficiency, but does not
    // allow for collection, even with Silk Touch").
    //
    // The row is here rather than left to the mining table's tool gate, because
    // only asked on the *mining* path: a flow washing a vine off a wall, a
    // falling column crushing one and a blast all reach this table directly,
    // and every one of them was handing back a free vine.
    if (isVine(block)) {
        return ctx.tool == ToolKind::Shears ? oneDrop(dropForBlock(block)) : noDrop();
    }
    // A dead bush is firewood unless you cut it.
    if (block == BlockId::DeadBush) {
        return (ctx.tool == ToolKind::Shears || ctx.silkTouch)
                   ? oneDrop(itemForBlock(BlockId::DeadBush))
                   : rangeDrop(ItemId::Stick, 0, 2);
    }
    // Grass and ferns: the plant with shears, else one seed in eight.
    //
    // **A large fern is the one that pays two.** *"When broken using shears, a
    // large fern drops two ferns instead"* - two of the *small* plant, not one
    // of itself, because the two-tall one has no item form to give. Tall grass
    // here is this game's single-block grass and really does pay one of itself;
    // the reference's two-block Tall Grass, which pays two short grass, has no
    // id yet. <https://minecraft.wiki/w/Large_Fern>,
    // <https://minecraft.wiki/w/Fern>
    if (block == BlockId::TallGrass || block == BlockId::Fern || block == BlockId::LargeFern) {
        if (ctx.tool != ToolKind::Shears && !ctx.silkTouch) {
            return chanceDrop(ItemId::WheatSeeds, 1, 1, 1, 8);
        }
        return block == BlockId::LargeFern ? oneDrop(itemForBlock(BlockId::Fern), 2)
                                           : oneDrop(itemForBlock(block));
    }
    // The three nether plants, which had no rows at all and so fell to the tail
    // line and handed themselves back to a bare fist.
    //
    // Nether sprouts are shears or nothing, and **not** a Silk Touch row: the
    // page says only *"the block drops only if broken with shears"* and the
    // reference's own Silk Touch table does not list it, so claiming one here
    // would be inventing a value. <https://minecraft.wiki/w/Nether_Sprouts>
    if (block == BlockId::NetherSprouts) {
        return ctx.tool == ToolKind::Shears ? oneDrop(itemForBlock(block)) : noDrop();
    }
    // The two nether vines are a *chance*, which is the shape the old free
    // drop got most wrong: *"they have a 33% chance to drop a single twisting
    // vine when broken... They always drop a single twisting vine when broken
    // with shears or with a tool enchanted with Silk Touch."* Weeping vines are
    // word for word the same. <https://minecraft.wiki/w/Twisting_Vines>,
    // <https://minecraft.wiki/w/Weeping_Vines>
    if (block == BlockId::TwistingVines || block == BlockId::WeepingVines) {
        if (ctx.tool == ToolKind::Shears || ctx.silkTouch) {
            return oneDrop(itemForBlock(block));
        }
        return chanceDrop(itemForBlock(block), 1, 1, 1, 3);
    }
    // Four shards to a pickaxe and two to anything else - a *count* that a
    // tool changes, which is the other thing the old gate could not say.
    if (block == BlockId::AmethystCluster) {
        return oneDrop(ItemId::AmethystShard, ctx.tool == ToolKind::Pickaxe ? 4 : 2);
    }
    // An ender chest is eight obsidian back, and only to a pickaxe.
    if (isEnderChest(block)) {
        return ctx.tool == ToolKind::Pickaxe ? oneDrop(itemForBlock(BlockId::Obsidian), 8)
                                             : noDrop();
    }
    // A cauldron is iron and comes up with a pickaxe or not at all. Whatever it
    // held is lost either way, which is what `dropForBlock` already says.
    if (isCauldron(block)) {
        return ctx.tool == ToolKind::Pickaxe ? oneDrop(itemForBlock(BlockId::Cauldron)) : noDrop();
    }

    // ---- Families. ----
    if (isLeafBlock(block)) {
        return leafDrop(block, ctx);
    }
    // Live coral dies the moment it is taken, exactly as it does out of water.
    if (isCoralBlock(block) && !isDeadCoral(block)) {
        return oneDrop(itemForBlock(deadCoralFor(block)));
    }
    if (isCropBlock(block)) {
        return cropDrop(block);
    }
    // **Nether wart had no branch at all**, so a ripe one fell to the tail line
    // and paid the single wart it cost to plant - a crop that can never turn a
    // profit, which is most of what nether wart is for. *"Breaking a fully
    // grown Nether wart drops 2 to 4 Nether warts, while an immature one drops
    // a single Nether wart."* Ripe is age 3, the last of this enum's four:
    // *"nether wart is ready to harvest when it reaches its fourth stage
    // (equivalent to age 3)"*. <https://minecraft.wiki/w/Nether_Wart>
    if (isNetherWart(block)) {
        return netherWartAge(block) >= 3 ? rangeDrop(ItemId::NetherWart, 2, 4)
                                         : oneDrop(ItemId::NetherWart);
    }
    // A stem is worth a handful of its own seeds, and **how big a handful is
    // the whole of its age**.
    //
    // The old row was a flat uniform 0-3 whatever the stem had grown to, which
    // averages 1.5 seeds for a stem that cost one - a free-item loop reachable
    // by planting and immediately breaking. The reference's distribution is
    // `Binomial(3, (age + 1) / 15)`, so a fresh stem averages 0.2 seeds and
    // only a ripe one averages 1.6 (minecraft.wiki, *Melon Seeds*: *"a melon
    // stem drops 0-3 melon seeds. The chance for melon seeds to drop increases
    // with the stem's age"*, tabulated at 81.3% nothing for age 0 against
    // 10.16% for age 7). Pumpkin seeds carry the identical table, and an
    // attached stem uses the age-7 row - which `stemAge` already returns for
    // one.
    //
    // **Three independent 1-in-`15/(age+1)` entries on three salts is exactly
    // that binomial**, not an approximation of it: three Bernoulli trials at
    // the same probability, counted, *is* `Binomial(3, p)`. It is the one shape
    // in this table where the model's `min`/`max` pair could not have said what
    // the reference says and the salts could.
    // <https://minecraft.wiki/w/Melon_Seeds>,
    // <https://minecraft.wiki/w/Pumpkin_Seeds>
    if (isStemBlock(block)) {
        const ItemId seed = stemGrowsMelon(block) ? ItemId::MelonSeeds : ItemId::PumpkinSeeds;
        const std::uint16_t num = static_cast<std::uint16_t>(stemAge(block) + 1);
        BlockDrop drop{};
        for (std::uint8_t salt = 0; salt < 3; ++salt) {
            drop = withEntry(drop, DropEntry{seed, 1, 1, salt, num, 15});
        }
        return drop;
    }

    // ---- Named blocks, all of them the reference's own numbers. ----
    switch (block) {
    // Gravel is why `salt` exists: **two alternatives off one roll**, so flint
    // *replaces* the gravel one time in ten rather than arriving beside it,
    // which is what happens today. Gravel is written first so a caller that
    // only wants one id gets the ordinary answer rather than the rare one.
    case BlockId::Gravel: {
        // **Fortune moves the flint chance and Silk Touch removes it**, which
        // are the two halves this row was missing: *"gravel has a 10% chance to
        // drop flint if this tool isn't enchanted with Fortune; it has a
        // 14.2857...% (1/7) to drop flint if the tool is enchanted with Fortune
        // I, 25% for Fortune II, and 100% for Fortune III. If the flint is not
        // dropped, gravel drops itself instead."* Silk Touch is answered above
        // by `silkTouchRecovers`. <https://minecraft.wiki/w/Gravel>
        //
        // At Fortune III there is no roll left to make, and it is written as a
        // plain drop rather than as a 0/1 alternative because a zero numerator
        // is not a legal entry - see `dropTableSane`.
        if (ctx.fortune >= 3) {
            return oneDrop(ItemId::Flint);
        }
        const std::uint16_t den = ctx.fortune >= 2 ? 4 : (ctx.fortune >= 1 ? 7 : 10);
        BlockDrop drop{};
        drop = withEntry(drop, DropEntry{itemForBlock(BlockId::Gravel), 1, 1, 0,
                                         static_cast<std::uint16_t>(den - 1), den});
        drop = withEntry(drop, DropEntry{ItemId::Flint, 1, 1, 0, 1, den});
        return drop;
    }
    // **Gilded blackstone is gravel's roll wearing gold**, and it is written
    // immediately below gravel because that is the only reason either of them
    // is shaped this way: one salt, two entries whose numerators sum to the
    // denominator, so the pair is a choice rather than a pair of gifts.
    // *"Gilded blackstone has a 10% chance to drop 2-5 gold nuggets when mined
    // with any pickaxe. If it does not drop gold nuggets, it drops itself as a
    // block... As with the drop chance of flint from gravel, the Fortune
    // enchantment does not increase the amount dropped, but increases the
    // chance of gold nuggets dropping. Fortune I increases the chance for this
    // drop to 14.29% (1/7), Fortune II increases it to 25%, and Fortune III
    // guarantees the gold nugget drop."* Every one of those four denominators
    // is gravel's, which is the point of putting them side by side.
    //
    // Before this it fell to the tail line and handed itself back every time,
    // which is the *rare* half of the reference's roll given away as the
    // certain one - and left the nuggets unreachable.
    // <https://minecraft.wiki/w/Gilded_Blackstone>
    case BlockId::GildedBlackstone: {
        if (ctx.fortune >= 3) {
            return rangeDrop(ItemId::GoldNugget, 2, 5);
        }
        const std::uint16_t den = ctx.fortune >= 2 ? 4 : (ctx.fortune >= 1 ? 7 : 10);
        BlockDrop drop{};
        drop = withEntry(drop, DropEntry{itemForBlock(BlockId::GildedBlackstone), 1, 1, 0,
                                         static_cast<std::uint16_t>(den - 1), den});
        drop = withEntry(drop, DropEntry{ItemId::GoldNugget, 2, 5, 0, 1, den});
        return drop;
    }
    // Worked and grazed dirt is still dirt.
    case BlockId::Podzol:
    case BlockId::Mycelium:
        return oneDrop(itemForBlock(BlockId::Dirt));
    // **And the nether's version of exactly that rule**, which is why it sits
    // here rather than anywhere else: *"nylium drops netherrack unless mined
    // with a Silk Touch tool."* It handed itself back before, so a bastion's
    // floor was a renewable supply of a block the reference only gives you with
    // an enchantment - the Silk Touch half is in `silkTouchRecovers` above.
    // <https://minecraft.wiki/w/Nylium>
    case BlockId::CrimsonNylium:
    case BlockId::WarpedNylium:
        return oneDrop(itemForBlock(BlockId::Netherrack));
    // A mushroom block is mostly cap: nothing about four times in five, else
    // one or two mushrooms. 2/9 to appear then a uniform 1-2 is 11.11% each,
    // which is the reference's table exactly.
    case BlockId::BrownMushroomBlock:
        return chanceDrop(itemForBlock(BlockId::BrownMushroom), 1, 2, 2, 9);
    case BlockId::RedMushroomBlock:
        return chanceDrop(itemForBlock(BlockId::RedMushroom), 1, 2, 2, 9);
    // Shelves come apart into their books, and the wood is lost.
    case BlockId::Bookshelf:
        return oneDrop(ItemId::Book, 3);
    // A campfire is logs that have already burnt.
    case BlockId::Campfire:
        return oneDrop(ItemId::Charcoal, 2);
    // **A soul campfire is not**, and it used to fall to the tail line and hand
    // itself back - which made it the one campfire you could pick up and put
    // down again. *"A soul campfire drops soul soil, as well as any items
    // currently cooking on it. If mined with a tool enchanted with Silk Touch,
    // the soul campfire instead drops itself as an item."* The second sentence
    // is `silkTouchRecovers` above; this is the first.
    // <https://minecraft.wiki/w/Soul_Campfire>
    case BlockId::SoulCampfire:
        return oneDrop(itemForBlock(BlockId::SoulSoil));
    // **A chorus plant is fruit, not a block.** It dropped itself, which both
    // handed out a block the reference will not give you at all - *"the item
    // form cannot be obtained in Survival mode, even with the Silk Touch
    // enchantment"* - and left chorus fruit unobtainable, which is why it is
    // absent from `silkTouchRecovers` as well. *"Upon breaking, a chorus plant
    // drops 0-1 chorus fruit. This is not affected by Fortune."*
    //
    // The chorus *flower* is deliberately not here: it does drop itself, and
    // the tail line is already right for it.
    // <https://minecraft.wiki/w/Chorus_Plant>,
    // <https://minecraft.wiki/w/Chorus_Flower>
    case BlockId::ChorusPlant:
        return rangeDrop(ItemId::ChorusFruit, 0, 1);
    case BlockId::SeaLantern:
        return rangeDrop(ItemId::PrismarineCrystals, 2, 3);
    // ---- The ranges the midpoints stood in for. ----
    case BlockId::CopperOre:
        return rangeDrop(ItemId::RawCopper, 2, 5);
    case BlockId::RedstoneOre:
        return rangeDrop(ItemId::Redstone, 4, 5);
    case BlockId::LapisOre:
        return rangeDrop(ItemId::LapisLazuli, 4, 9);
    // **Nether gold ore had the item and not the count.** `dropForBlock`
    // already answers gold nuggets and `dropCountForBlock` already answers 4,
    // so the ore paid a fixed four where the reference pays a range: *"nether
    // gold ore drops 2-6 gold nuggets when mined with any pickaxe"*. The
    // pickaxe half is `Mining.hpp`'s wood gate and was already right.
    // <https://minecraft.wiki/w/Nether_Gold_Ore>
    case BlockId::NetherGoldOre:
        return rangeDrop(ItemId::GoldNugget, 2, 6);
    case BlockId::Melon:
        return rangeDrop(ItemId::MelonSlice, 3, 7);
    case BlockId::Glowstone:
        return rangeDrop(ItemId::GlowstoneDust, 2, 4);
    // Exactly four, not a range - and the assert below says so, because a clay
    // bank is the brick chain's first link and a range there would be wrong.
    case BlockId::Clay:
        return oneDrop(ItemId::ClayBall, 4);
    // **A mature bush is 2-3 berries.** The id carries no age, so this models
    // the grown bush - the only one a player ever picks on purpose - rather
    // than the third-stage 1-2 or the flat 2 that was neither: *"a mature
    // sweet berry bush yields 2-3 sweet berries... on its third growth stage,
    // it yields 1-2"*. It rerolls per break, which the list below says: the
    // berries replant the bush, so a fixed roll would make one cell a rate the
    // player owns. <https://minecraft.wiki/w/Sweet_Berries>
    case BlockId::SweetBerryBush:
        return rangeDrop(ItemId::SweetBerries, 2, 3);
    default:
        break;
    }

    // ---- Everything else keeps the identity `dropForBlock` already works out.
    // **This is the line that keeps 1,356 cut shapes, 136 candles, both door
    // halves, both bed halves and every container facing free**, and it keeps
    // them free by asking the same code they are answered by today rather than
    // by a second table that could fall out of step with it.
    const ItemId item = dropForBlock(block);
    if (item == ItemId::None) {
        return noDrop();
    }
    return oneDrop(item, dropCountForBlock(block));
}

/// The single item a break yields, for a caller that only wants an id.
///
/// The migration handle: `dropForBlock(b)` becomes `primaryDrop(b, ctx)` one
/// call site at a time. **It is lossy on purpose** - gravel, oak leaves and a
/// ripe potato all have more than one entry, and a chance entry may not appear
/// at all - so a site that actually spawns items must use `resolveDrop`.
///
/// > **Do not put this inside `isCanonicalBlockItem`.** That filter asks
/// > `dropForBlock` because it is asking "is this id the one form of itself
/// > that belongs in a catalogue", which is a question about *state*, not about
/// > loot. Asking this instead would take glass, ice and every cauldron level
/// > out of the creative catalogue, because they now drop nothing.
constexpr ItemId primaryDrop(BlockId block, BreakContext ctx = {}) {
    const BlockDrop drop = dropsForBlock(block, ctx);
    return drop.empty() ? ItemId::None : drop.entries[0].item;
}

/// Whether this block's rolls are drawn afresh on every break, or settled once
/// and for all by the cell it stands in.
///
/// **The split is one-shot against renewable, and it is the difference between
/// a lucky cell and a machine.** A roll off the position alone is exactly right
/// for a feature you can only harvest once: the gravel at (12, 40, -7) has
/// always given flint and always will, which is what stops a save reload
/// re-rolling it and is the precedent every ore count already follows. It stops
/// being right the moment the block *comes back on the same cell*, because then
/// the outcome is not a property of the world, it is a rate the player has
/// found and can repeat for ever - a melon farm where every cell pays the same
/// 3-7 slices to the end of time, a known "apple cell" that pays an apple at
/// 100%, and gravel placed on a known flint cell to farm flint at 100%.
/// Bedrock re-rolls every break, so these do too.
///
/// Crops, stems and their fruit are the ones a player can drive today - a crop
/// is replanted on its own farmland and a stem re-fruits into the same four
/// cells. **Leaves and mushroom blocks are in the list before they need to be**,
/// because saplings do not grow into trees yet and bone meal does not raise a
/// huge mushroom yet, and the day either arrives is the day nobody remembers
/// this rule existed. Short grass and ferns are *not* here for the same reason
/// read the other way: bone meal only advances crops and stems in this game
/// (`World::applyBoneMeal`), so there is no way to make one come back, and they
/// join the list the day there is.
constexpr bool rerollsEachBreak(BlockId block) {
    return isCropBlock(block) || isStemBlock(block) || isLeafBlock(block) ||
           block == BlockId::Melon || block == BlockId::Pumpkin ||
           block == BlockId::SweetBerryBush || block == BlockId::BrownMushroomBlock ||
           block == BlockId::RedMushroomBlock;
}

/// **What an actual break should call**: the table, the renewable rule and the
/// rolls, in one answer.
///
/// `nonce` is a number that differs from one break to the next - `Main.cpp`
/// keeps a counter - and this is the one place that decides whether it is used
/// or thrown away. Two pieces of knowledge that have to agree (which rows roll,
/// and which of them may roll twice) then live one line apart in the same
/// header, rather than one here and one at a call site that cannot see the
/// table.
///
/// > A pumpkin has no roll to re-draw, and is on the renewable list anyway: it
/// > is the melon's twin, it comes back on the same cell the same way, and a
/// > count added to it later should not have to remember this.
constexpr ResolvedDrop resolveBreak(BlockId block, const BreakContext& ctx, int x, int y, int z,
                                    std::uint32_t nonce) {
    return resolveDrop(dropsForBlock(block, ctx), x, y, z, rerollsEachBreak(block) ? nonce : 0u);
}

// ---------------------------------------------------------------------------
// What is deliberately *not* in the table, and why. Every one of these is a row
// the reference has and this does not, so the next reader finds the reason here
// rather than assuming it was missed.
//
//  * **Snow.** The reference pays a snow block four snowballs and a layer one to
//    four by depth, all shovel-gated. **`ItemId::Snowball` does not exist**, and
//    adding one is not a drop-table change: it needs a row in `kExtraItems`, a
//    sprite staged by `tools/make-reference-blocks.ps1` and a placeholder under
//    `assets/`. Until then snow keeps handing back its own block, which is what
//    it does today and what Silk Touch would do anyway. Layers here are also
//    **seven** deep against the reference's eight, so the last row of that
//    table has nowhere to live either.
//  * **The sweet berry bush's third growth stage.** `BlockId::SweetBerryBush`
//    is a single id with no age, so only one of the reference's two counts can
//    be written. The table now pays the **mature** bush's 2-3 - that is the one
//    a player picks on purpose, and the one the berry economy is balanced
//    against - and the third stage's 1-2 has nowhere to live until the id
//    grows an age the way the crops did.
//  * **The flower pot's two items.** A pot is one id here with no plant state,
//    so "the pot and the plant in it" has nothing to read.
//  * ~~**Mangrove leaves needing a wooden pickaxe.**~~ **Stale - this was fixed
//    and the note was not.** `derivedTool` now answers `isLeafBlock` with
//    `ToolKind::Hoe` *above* the shape runs, so all ten leaves - mangrove
//    included - carry `tool = Hoe`, `toolRequired = false`, `tier = 0`, and
//    `canHarvest(MangroveLeaves, ItemId::None)` is true. A bare hand gets the
//    sticks. Proven by `everyLeafBreaksLikeALeaf` in `Mining.hpp`, whose
//    `canHarvest` clause exists because of this note rather than by reading it.
//  * **Fortune and Silk Touch.** `BreakContext` carries both and **nothing
//    sets either**, because enchanting does not exist - so every row in this
//    file that reads them is correct and unreachable, and will stay that way
//    until an enchantment table does. The rows are written anyway because the
//    ground-truth block dump has a column per condition and it was reporting
//    seventy blocks as permanently unobtainable, which is a statement about
//    this table rather than about the reference. **What must not be built here
//    is the enchantment**: no call site should learn to set these flags from
//    inside a drop table.
//  * **Ice becoming a water source.** *"If there is a movement-blocking block
//    or any fluid directly underneath the ice block, the ice becomes a water
//    source block when broken."* Not expressible here at all - this file
//    returns items, and that is a *block placement* decided by what is under
//    the cell. It belongs to whoever owns the break-and-replace path, and the
//    comment beside `Ice` in `yieldsNothingEver` used to claim this file was
//    already doing it. <https://minecraft.wiki/w/Ice>
//  * **Brushing suspicious sand and gravel.** Both ids exist, both correctly
//    drop nothing when *broken*, and a brush item exists - but brushing is a
//    use-item interaction with a per-structure loot table, and neither half of
//    that lives here. <https://minecraft.wiki/w/Suspicious_Sand>
//  * **The huge-mushroom cap's Silk Touch texture.** With Silk Touch the
//    reference hands back *"a block with the cap or stem texture on all
//    faces"*, which is a different block state from the naturally generated
//    one. This table hands back the id it was given, which is the closest
//    thing the enum can say. <https://minecraft.wiki/w/Mushroom_Block>
//  * **Experience.** A spawner, sculk, its catalyst, its sensor and its shrieker
//    all pay in experience in the reference. There is no experience, so for now
//    they pay nothing at all. **The sculk vein is the one that is genuinely
//    right**: it is the only member of the family that pays no experience.
//    <https://minecraft.wiki/w/Sculk_Vein>
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The sweep. Every id, every entry, every invariant.
// ---------------------------------------------------------------------------

/// How many ids one pass of the sweep covers.
///
/// The same stride as the name sweep in `Block.hpp`, and **measured rather than
/// guessed** - a throwaway build at a stride of 1024 also passed, so this has
/// room to spare even though `dropsForBlock` costs more per id than `blockName`
/// does. Why the sweep is split at all is under `DropSweep` below.
///
/// > **This name and `kDropSweepPasses` are spelled a second time, with the
/// > same two values, in an anonymous namespace in `world/ItemEntity.cpp`.**
/// > Checked rather than assumed: this header is included by `Main.cpp` and by
/// > nothing else, directly or through any other header, so the two pairs sit
/// > in *separate* translation units today and neither hides the other. **They
/// > are two different questions wearing one name** - this pair bounds the
/// > sweep over the drop *table*, that one bounds the sweep over how a dropped
/// > block is *drawn* (`DropModelSweep`, `everyModelIconBlockHasBoxes`), and
/// > the day the enum grows past one of them says nothing about the other.
/// >
/// > The hazard is one `#include` line away, and it is a line somebody will
/// > plausibly write, because drop *rules* and drop *rendering* are obviously
/// > related: give `ItemEntity.cpp` this header and its anonymous-namespace
/// > pair silently wins every unqualified use below it. Nothing would catch
/// > that - two identical values cannot disagree, and each sweep carries its
/// > own `kBlockIdCount` coverage assert against whichever constants it can
/// > see, so both stay green while meaning different things. **This pair owns
/// > the name**; the fix on the other side is a rename to match what it
/// > actually measures, not a shared constant that would couple two files with
/// > no other reason to know about each other. Filed for that file's owner
/// > rather than done here.
constexpr int kDropSweepStride = 512;

/// Whether every drop in one stride, broken with `ctx`, is well formed.
///
/// **The context is a parameter because the bare-hand answer is not the whole
/// table.** Every tool-conditional row - the cobweb's string, a leaf's chances,
/// four amethyst shards, an ender chest's eight obsidian, a cauldron and the
/// whole shears list - is reachable only through some other context, so a
/// transposed range in any of them used to compile perfectly clean. The passes
/// below sweep the four kinds the game actually hands this table, plus the
/// blast's own context, which is the one that rewrites what it was given.
///
/// The invariants are the ones a mistyped row breaks:
///   * a range that is not one - `min > max`, which is what a transposed 4-9
///     would look like;
///   * a range that can never yield anything - `max == 0`, an entry that is
///     rolled, passes, and then hands back nothing at all;
///   * a chance that is impossible or certain by accident - a zero
///     denominator, a zero numerator, or a numerator past the denominator;
///   * more entries than the array holds, or an entry past `count` that still
///     names an item, which is what a hand-built `BlockDrop` gets wrong;
///   * an entry naming nothing, which is a row somebody meant to fill in;
///   * **alternatives that do not add up** - entries sharing a salt must share
///     its denominator, and two or more of them must claim the whole of that
///     roll: no more, or gravel's flint arrives beside its gravel rather than
///     in place of it, and no less, or some breaks silently yield nothing.
///     (A salt with one entry is the ordinary "this appears one time in
///     twenty" case and claims what it likes. A weighted *nothing* would have
///     to be written as a row, which is what `Loot.hpp` does for the same
///     reason - and cannot be, because a row naming no item is refused above.)
constexpr bool dropTableSane(int stride, const BreakContext& ctx) {
    const int first = stride * kDropSweepStride;
    const int end = first + kDropSweepStride;
    for (int id = first; id < end && id < static_cast<int>(kBlockIdCount); ++id) {
        const BlockDrop drop = dropsForBlock(static_cast<BlockId>(id), ctx);
        if (drop.count > kMaxDropEntries) {
            return false;
        }
        std::array<int, kMaxDropSalts> claimed{};
        std::array<int, kMaxDropSalts> denominator{};
        std::array<int, kMaxDropSalts> sharing{};
        for (int e = 0; e < static_cast<int>(drop.count); ++e) {
            const DropEntry entry = drop.entries[static_cast<std::size_t>(e)];
            if (entry.item == ItemId::None) {
                return false;
            }
            if (entry.min > entry.max || entry.max == 0) {
                return false;
            }
            if (entry.chanceDen == 0 || entry.chanceNum == 0 ||
                entry.chanceNum > entry.chanceDen) {
                return false;
            }
            if (entry.salt >= kMaxDropSalts) {
                return false;
            }
            const auto salt = static_cast<std::size_t>(entry.salt);
            if (denominator[salt] == 0) {
                denominator[salt] = entry.chanceDen;
            } else if (denominator[salt] != entry.chanceDen) {
                return false;
            }
            claimed[salt] += entry.chanceNum;
            ++sharing[salt];
            if (claimed[salt] > denominator[salt]) {
                return false;
            }
        }
        for (std::size_t salt = 0; salt < kMaxDropSalts; ++salt) {
            if (sharing[salt] > 1 && claimed[salt] != denominator[salt]) {
                return false;
            }
        }
        for (int e = static_cast<int>(drop.count); e < kMaxDropEntries; ++e) {
            if (drop.entries[static_cast<std::size_t>(e)].item != ItemId::None) {
                return false;
            }
        }
    }
    return true;
}

/// How many strides it takes to reach the end of the enum.
///
/// **The passes are generated from this number rather than written out**, which
/// is the whole point of it: a hand-written list of `static_assert` lines is
/// checked by the coverage assert only for being too *short* at the end, so
/// deleting the line in the middle of it stopped 512 ids being swept and
/// nothing said a word. `DropSweep<Pass>` below cannot be deleted one pass at a
/// time, and the assert under it ties this number to the size of the enum.
///
/// > Spelled a second time in `world/ItemEntity.cpp`'s anonymous namespace, in
/// > a *different* translation unit - see `kDropSweepStride` above for what the
/// > two actually measure and why one `#include` line would turn a coincidence
/// > into shadowing.
///
/// **Derived rather than written by hand, 2026-08-19.** It was a hand-written
/// `7` and a tree-wide census of `*SweepPasses` had missed it, reporting
/// `Mining.hpp` as the last literal when there were two; both moved in the same
/// batch. `Block.hpp`, `Copper.hpp` and `ItemEntity.cpp` took this form first,
/// and `kNameSweepPasses`'s note says why it costs nothing: the *stride* is what
/// the constexpr step budget cares about and it does not move, so a pass stays
/// exactly as dear as it was and only their number grows. Rounding is **up** -
/// rounding down under-covers silently, which is the failure this sweep exists
/// to catch.
///
/// > **This knowingly retires the coverage assert below into a form-guard, and
/// > the assert is KEPT anyway.** `ceil(N/S) * S >= N` holds for every
/// > `N >= 0`, so it can no longer fire; the failure it caught is now
/// > unexpressible rather than merely detected, which is the better end of the
/// > trade. `ChunkMesher.cpp` and `ItemEntity.cpp` both kept theirs through the
/// > identical conversion.
constexpr int kDropSweepPasses =
    (static_cast<int>(kBlockIdCount) + kDropSweepStride - 1) / kDropSweepStride;

/// One pass of the sweep, in **five separate constant evaluations**.
///
/// **Split rather than folded into one assert, for `blockName`'s reason** (see
/// `Block.hpp`'s `kNameSweepStride`): MSVC counts constexpr steps per
/// evaluation, and one pass over the whole enum is over the budget. A
/// `static_assert` inside a template is its own evaluation with its own budget,
/// exactly as a `static_assert` at namespace scope is, so this buys the
/// generated list at no cost to the split.
template <int Pass>
struct DropSweep {
    static_assert(dropTableSane(Pass, BreakContext{}),
                  "a bare-handed drop entry names no item, an impossible range or a bad chance");
    static_assert(dropTableSane(Pass, BreakContext{.tool = ToolKind::Shears}),
                  "a shears drop entry names no item, an impossible range or a bad chance");
    static_assert(dropTableSane(Pass, BreakContext{.tool = ToolKind::Pickaxe}),
                  "a pickaxe drop entry names no item, an impossible range or a bad chance");
    static_assert(dropTableSane(Pass, BreakContext{.tool = ToolKind::Sword}),
                  "a sword drop entry names no item, an impossible range or a bad chance");
    static_assert(dropTableSane(Pass, BreakContext{ToolKind::Pickaxe, kDiamondTier, false, 0,
                                                   true}),
                  "a blasted drop entry names no item, an impossible range or a bad chance");
    // **Two contexts this sweep did not have**, and both are new surface rather
    // than tidiness: `silkTouchRecovers` and gravel's Fortune ladder are the
    // first rows in this file whose *shape* depends on these two fields, and a
    // sweep that never sets them would go on passing over a Fortune III entry
    // with a zero numerator or a Silk Touch row naming `ItemId::None`.
    //
    // > The single edit that fails the Fortune line: write Fortune III's gravel
    // > as a `0/1` alternative beside the flint instead of returning the flint
    // > outright - `chanceNum == 0` is what `dropTableSane` rejects.
    static_assert(dropTableSane(Pass, BreakContext{.silkTouch = true}),
                  "a Silk Touch drop entry names no item, an impossible range or a bad chance");
    static_assert(dropTableSane(Pass, BreakContext{.fortune = 3}),
                  "a Fortune III drop entry names no item, an impossible range or a bad chance");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyPassSwept(std::integer_sequence<int, Pass...>) {
    return (DropSweep<Pass>::swept && ...);
}

static_assert(everyPassSwept(std::make_integer_sequence<int, kDropSweepPasses>{}),
              "the drop table is malformed somewhere - the failing DropSweep instantiation above "
              "names which stride and which tool");
/// The passes are generated, so this is what proves there are enough of them:
/// without it, ids appended past the last stride would simply stop being swept
/// and the sweep would go on passing.
static_assert(kDropSweepPasses * kDropSweepStride >= static_cast<int>(kBlockIdCount),
              "the drop sweep no longer covers every block id - raise kDropSweepPasses");

// The rule that made 1,356 cut shapes free, and it must keep holding: a cut shape
// hands back its own family's canonical form, never its parent's rubble.
static_assert(dropsForBlock(BlockId::CobbleStairs0).entries[0].item ==
                      itemForBlock(shapedCanonical(BlockId::CobbleStairs0)) &&
                  dropsForBlock(BlockId::StoneSlabTop).entries[0].item ==
                      itemForBlock(shapedCanonical(BlockId::StoneSlabTop)),
              "a cut shape hands back its own family, never its parent's rubble");
static_assert(shapedParent(BlockId::CobbleStairs0) == BlockId::Cobblestone &&
                  dropsForBlock(BlockId::CobbleStairs0).entries[0].item !=
                      itemForBlock(BlockId::Cobblestone),
              "forwarding through shapedParent must not leak the parent into the drop");

// One door from two blocks, whichever half you break - and one bed likewise.
static_assert(dropsForBlock(doorAt(0, FaceDirection::NegZ, false, false, true)).entries[0] ==
                  dropsForBlock(doorAt(0, FaceDirection::NegZ, false, false, false)).entries[0],
              "both halves of a door give one door, and the same door");
static_assert(dropsForBlock(doorAt(0, FaceDirection::NegZ, false, false, true)).entries[0].max == 1,
              "a door is one item, not two");

// The counts that are exact, and the two that are not.
static_assert(dropsForBlock(BlockId::Clay).entries[0].min == 4 &&
                  dropsForBlock(BlockId::Clay).entries[0].max == 4,
              "clay is exactly four balls, not a range");
static_assert(dropsForBlock(BlockId::LapisOre).entries[0].min == 4 &&
                  dropsForBlock(BlockId::LapisOre).entries[0].max == 9,
              "Bedrock lapis is 4-9 since 1.20.60");
static_assert(dropsForBlock(BlockId::LapisOre).entries[0] ==
                      dropsForBlock(BlockId::DeepslateLapisOre).entries[0] &&
                  dropsForBlock(BlockId::CopperOre).entries[0] ==
                      dropsForBlock(BlockId::DeepslateCopperOre).entries[0],
              "the deepslate half of an ore yields exactly what the stone half does");

// The exploit blocks. A regression here is worth a build failure.
static_assert(dropsForBlock(BlockId::MonsterSpawner).empty() &&
                  dropsForBlock(BlockId::BuddingAmethyst).empty() &&
                  dropsForBlock(BlockId::Bedrock).empty() &&
                  dropsForBlock(BlockId::Glass).empty() &&
                  dropsForBlock(BlockId::Ice).empty(),
              "none of these is a souvenir");
static_assert(!dropsForBlock(BlockId::TintedGlass).empty(),
              "tinted glass is the one glass the reference lets you keep");

// A cobweb answers three different ways and all three matter.
static_assert(dropsForBlock(BlockId::Cobweb).empty() &&
                  dropsForBlock(BlockId::Cobweb, {.tool = ToolKind::Sword}).entries[0].item ==
                      ItemId::String &&
                  dropsForBlock(BlockId::Cobweb, {.tool = ToolKind::Shears}).entries[0].item ==
                      itemForBlock(BlockId::Cobweb),
              "a cobweb answers three different ways and all three matter");
static_assert(dropsForBlock(BlockId::AmethystCluster).entries[0].min == 2 &&
                  dropsForBlock(BlockId::AmethystCluster, {.tool = ToolKind::Pickaxe})
                          .entries[0]
                          .min == 4,
              "a pickaxe changes an amethyst cluster's count, which a tier gate cannot say");

// **Both halves of the cave vine, in one assert, because only one of them was
// wrong.** The berry-bearing vine pays a glow berry through `itemForBlock` and
// always did; the berry-less one paid a *cave vines* block item, which the
// reference does not have. Asserting only the fixed half would have passed
// before the fix, and asserting only the item would have passed with the count
// wrong - so this reads the whole expression a caller reads, both sides, and
// pins the "nothing" as a `.empty()` rather than as an item comparison.
static_assert(dropsForBlock(BlockId::CaveVines).empty() &&
                  dropsForBlock(BlockId::CaveVines, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::CaveVinesBerries).count == 1 &&
                  dropsForBlock(BlockId::CaveVinesBerries).entries[0].item ==
                      ItemId::GlowBerries &&
                  dropsForBlock(BlockId::CaveVinesBerries).entries[0].min == 1 &&
                  dropsForBlock(BlockId::CaveVinesBerries).entries[0].max == 1,
              "a cave vine pays one glow berry with berries and nothing without, and there is no "
              "cave vines item in between");

// Gravel: one roll, two outcomes, never both. The two together must claim the
// whole of their roll, or some breaks would yield nothing at all.
static_assert(dropsForBlock(BlockId::Gravel).count == 2 &&
                  dropsForBlock(BlockId::Gravel).entries[0].salt ==
                      dropsForBlock(BlockId::Gravel).entries[1].salt &&
                  dropsForBlock(BlockId::Gravel).entries[0].chanceNum +
                          dropsForBlock(BlockId::Gravel).entries[1].chanceNum ==
                      dropsForBlock(BlockId::Gravel).entries[0].chanceDen,
              "flint replaces gravel; it does not arrive beside it, and it never leaves nothing");
static_assert(primaryDrop(BlockId::Gravel) == itemForBlock(BlockId::Gravel),
              "the ordinary outcome is written first, so a one-id caller gets gravel not flint");

// Leaves roll three separate coins, and a shear takes the leaf whole.
static_assert(dropsForBlock(BlockId::Leaves).count == 3 &&
                  dropsForBlock(BlockId::Leaves).entries[0].salt !=
                      dropsForBlock(BlockId::Leaves).entries[1].salt &&
                  dropsForBlock(BlockId::Leaves).entries[1].salt !=
                      dropsForBlock(BlockId::Leaves).entries[2].salt,
              "an oak leaf's sapling, stick and apple are three rolls, not one");
static_assert(dropsForBlock(BlockId::Leaves, {.tool = ToolKind::Shears}).entries[0].item ==
                  itemForBlock(BlockId::Leaves),
              "shears take the leaf block itself");

// A resolved drop must never carry an empty stack or overrun its array.
static_assert(resolveDrop(dropsForBlock(BlockId::Stone), 0, 64, 0).count == 1 &&
                  resolveDrop(dropsForBlock(BlockId::Stone), 0, 64, 0).stacks[0].item ==
                      itemForBlock(BlockId::Cobblestone),
              "stone still breaks into its rubble, one piece of it");
/// How many of `cells` sample cells give flint instead of gravel.
///
/// **This replaces a `count == count` conjunct** - one resolve compared against
/// an identical resolve, which was true of any table at all. What is actually
/// worth proving about gravel is that its two entries are *one* roll carved in
/// two: every cell must yield exactly one stack, and the rare half must really
/// be rare rather than never.
///
/// `-1` for "some cell yielded something other than one stack", so the band
/// below reads as a single number.
///
/// > Fails if: change either `chanceNum` so the two stop summing to the
/// > denominator - the uncovered slice yields nothing and this returns -1. Give
/// > flint a salt of its own and it becomes an independent roll, so 9% of cells
/// > yield nothing and 1% yield both. Change the rate from 1-in-10 and the
/// > count leaves the band.
constexpr int flintCells(int cells) {
    int flint = 0;
    for (int i = 0; i < cells; ++i) {
        const ResolvedDrop rolled =
            resolveDrop(dropsForBlock(BlockId::Gravel), i * 5 - 200, 40, i * 11 - 37);
        if (rolled.count != 1) {
            return -1;
        }
        if (rolled.stacks[0].item == ItemId::Flint) {
            ++flint;
        }
    }
    return flint;
}

/// 512 cells at a published 1-in-10, so the mean is 51.2 and one standard
/// deviation is 6.8. The band is four of them either side - wide on purpose,
/// exactly as `kAgreementCells` is: the value is not the point, and a rate that
/// had actually moved would land nowhere near it.
constexpr int kFlintSampleCells = 512;
constexpr int kFlintCells = flintCells(kFlintSampleCells);

static_assert(kFlintCells > 24 && kFlintCells < 79,
              "gravel's two entries are one roll carved in two: every cell gives exactly one "
              "stack, and about one in ten of them is flint");
static_assert(resolveDrop(dropsForBlock(BlockId::Gravel), 12, 40, -7).count == 1,
              "one cell gives one of gravel or flint, and never neither");
static_assert(resolveDrop(dropsForBlock(BlockId::MonsterSpawner), 0, 0, 0).count == 0,
              "nothing in means nothing out");

// The crop counts, which are the ones the two editions disagree about and the
// ones a slip is invisible in - a field still pays *something* either way.
static_assert(dropsForBlock(cropAt(BlockId::CarrotCrop0, 7)).entries[0].min == 2 &&
                  dropsForBlock(cropAt(BlockId::CarrotCrop0, 7)).entries[0].max == 5 &&
                  dropsForBlock(cropAt(BlockId::PotatoCrop0, 7)).entries[0].min == 2 &&
                  dropsForBlock(cropAt(BlockId::PotatoCrop0, 7)).entries[0].max == 5,
              "a ripe carrot and a ripe potato are 2-5, not 1-4: 1 is not an outcome the "
              "reference has, and the mean is 3 5/7 rather than 2.5");
static_assert(dropsForBlock(cropAt(BlockId::WheatCrop0, 7)).entries[1].min == 0 &&
                  dropsForBlock(cropAt(BlockId::WheatCrop0, 7)).entries[1].max == 3 &&
                  dropsForBlock(cropAt(BlockId::BeetrootCrop0, 7)).entries[1].min == 1 &&
                  dropsForBlock(cropAt(BlockId::BeetrootCrop0, 7)).entries[1].max == 4,
              "the two seed rows are two different tables - wheat is Bedrock's 0-3 and beetroot "
              "is 1-4, so a harvest of one can leave you nothing to replant and the other cannot");
static_assert(dropsForBlock(cropAt(BlockId::PotatoCrop0, 7)).count == 2 &&
                  dropsForBlock(cropAt(BlockId::PotatoCrop0, 7)).entries[0].salt !=
                      dropsForBlock(cropAt(BlockId::PotatoCrop0, 7)).entries[1].salt,
              "the poisonous potato is an extra on its own roll, not an alternative to the crop");

// Vines: shears or nothing, and one item however many sides it clung to.
static_assert(dropsForBlock(vineWith(ConnectAll)).empty() &&
                  dropsForBlock(vineWith(1)).empty() &&
                  dropsForBlock(vineWith(1), {.tool = ToolKind::Shears}).entries[0].item ==
                      dropsForBlock(vineWith(ConnectAll), {.tool = ToolKind::Shears})
                          .entries[0]
                          .item,
              "a vine comes away only to shears, and every connection gives the same one vine");

// The blast rule's second half, which is the half a ToolKind cannot say. The
// caller passes a diamond pickaxe; a shears-correct block must still see an
// empty hand, or an exploded cobweb hands back string a mined one would not.
static_assert(dropsForBlock(BlockId::Cobweb, {ToolKind::Pickaxe, kDiamondTier, false, 0, true})
                  .empty(),
              "a blast breaks a shears block with an empty hand, so an exploded web pays nothing");
static_assert(dropsForBlock(BlockId::Leaves, {ToolKind::Pickaxe, kDiamondTier, false, 0, true})
                      .count == 3 &&
                  !dropsForBlock(BlockId::AmethystCluster,
                                 {ToolKind::Pickaxe, kDiamondTier, false, 0, true})
                       .empty(),
              "and the first half is untouched: a blasted leaf rolls its chances and a blasted "
              "cluster is still worth a pickaxe's four shards");

// ---------------------------------------------------------------------------
// This pass: the rows re-read against the reference's own drop tables. Each is
// written as the thing a *player* would notice, and each names the single edit
// that puts the bug back.
// ---------------------------------------------------------------------------

// Silk Touch, which the table carried an answer for and could never reach.
// <https://minecraft.wiki/w/Silk_Touch>
//
// > Fails if: move the `silkTouchRecovers` branch in `dropsForBlock` back below
// > the `yieldsNothingEver` call - every one of these returns empty again.
static_assert(dropsForBlock(BlockId::Glass, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Glass) &&
                  dropsForBlock(BlockId::PaneRunFirst, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::PaneRunFirst) &&
                  dropsForBlock(BlockId::Ice, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Ice) &&
                  dropsForBlock(BlockId::SculkVein, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::SculkVein) &&
                  dropsForBlock(BlockId::LargeAmethystBud, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::LargeAmethystBud),
              "glass, a pane, ice, sculk and an amethyst bud all come back whole to Silk Touch "
              "and to nothing else");
static_assert(dropsForBlock(BlockId::Grass, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Grass) &&
                  dropsForBlock(BlockId::Grass).entries[0].item == itemForBlock(BlockId::Dirt) &&
                  dropsForBlock(BlockId::Bookshelf, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Bookshelf) &&
                  dropsForBlock(BlockId::Bookshelf).entries[0].item == ItemId::Book,
              "a grass block and a bookshelf are the two that answer *differently* rather than "
              "not at all - dirt and three books without it");
// **And the exclusions, which are the half that can silently widen.** Nothing
// in this group may ever answer Silk Touch, and each is quoted at its own line
// in `silkTouchRecovers`.
//
// > Fails if: add any of these five to `silkTouchRecovers` - which is exactly
// > the tempting edit, because they sit in `yieldsNothingEver` beside blocks
// > that do.
static_assert(dropsForBlock(BlockId::BuddingAmethyst, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::MonsterSpawner, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::SuspiciousSand, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::ReinforcedDeepslate, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::PowderSnow, {.silkTouch = true}).empty(),
              "budding amethyst, a spawner, suspicious sand, reinforced deepslate and powder "
              "snow are refused the enchantment by the reference, not merely unlisted");
static_assert(dropsForBlock(BlockId::ReinforcedDeepslate).empty(),
              "reinforced deepslate used to fall to the tail line and hand itself back");
// **The two blocks the reference never lets break, and so never lets you
// have.** The frame reached the tail line and handed back an End Portal Frame
// item; nothing could see it, because `Mining.hpp` prices it at
// `kUnbreakableHardness` and no swing on it can finish - but `dropsForBlock`
// is also what a blast and `primaryDrop` read, and neither asks how long the
// block would have taken. Bedrock was right by accident all along:
// `itemForBlock(Bedrock)` is `None`, so its tail line already answered
// nothing, which is exactly why the frame's case is written down rather than
// left to the same luck.
//
// > The other half is `onlyTwoBlocksNeverBreak` in `Mining.hpp`: the two files
// > cannot see each other, so that sweep fails if a *third* id ever becomes
// > unbreakable and its message names this list.
// <https://minecraft.wiki/w/End_Portal_Frame>
static_assert(dropsForBlock(BlockId::EndPortalFrame).empty() &&
                  dropsForBlock(BlockId::EndPortalFrame, {.silkTouch = true}).empty() &&
                  dropsForBlock(BlockId::Bedrock).empty(),
              "an unbreakable block hands back an item - the reference puts the end portal "
              "frame in bedrock's own sentence, 'unobtainable and unbreakable in Survival'");

// **Every ore, swept rather than listed.** The bug this replaces was that
// `silkTouchRecovers` named glass, ice, coral plants and sculk and stopped -
// so the eighteen blocks a Silk Touch pickaxe is *most* often carried for
// answered it with their resource. A loop is what proves it, because a case
// list here would be a second place that has to agree with `isOre`, and the
// two would drift the first time an ore is added.
//
// > Fails if: drop `isOre` from `silkTouchRecovers`, or add an ore to
// > `BlockId` without adding it to `isOre` - the second is the one nothing
// > else in this file would notice.
constexpr bool everyOreComesBackWholeToSilkTouch() {
    for (int i = 0; i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId ore = static_cast<BlockId>(i);
        if (!isOre(ore) && ore != BlockId::NetherGoldOre && ore != BlockId::NetherQuartzOre) {
            continue;
        }
        const BlockDrop silk = dropsForBlock(ore, {.silkTouch = true});
        if (silk.count != 1 || silk.entries[0].item != itemForBlock(ore) ||
            silk.entries[0].min != 1 || silk.entries[0].max != 1) {
            return false;
        }
    }
    return true;
}

static_assert(everyOreComesBackWholeToSilkTouch(),
              "an ore mined with Silk Touch is the ore block, exactly one of it, and never "
              "the resource inside it");

// And the half a sweep of one context cannot see: without the enchantment the
// same ids must still pay the resource. Ancient debris is deliberately absent -
// it is inside `isOre` and drops itself either way, so it is the one member
// the pair of rules cannot tell apart, and saying so here is cheaper than
// carving it out of `isOre`.
static_assert(dropsForBlock(BlockId::CoalOre).entries[0].item == ItemId::Coal &&
                  dropsForBlock(BlockId::DeepslateDiamondOre).entries[0].item == ItemId::Diamond &&
                  dropsForBlock(BlockId::NetherQuartzOre).entries[0].item == ItemId::Quartz &&
                  dropsForBlock(BlockId::AncientDebris).entries[0].item ==
                      itemForBlock(BlockId::AncientDebris),
              "the ore rows below are untouched by the Silk Touch branch above them - and "
              "ancient debris is the one ore whose two answers are the same block");
static_assert(dropsForBlock(BlockId::DeepslateLapisOre, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::DeepslateLapisOre) &&
                  dropsForBlock(BlockId::DeepslateLapisOre).entries[0].item ==
                      dropsForBlock(BlockId::LapisOre).entries[0].item &&
                  dropsForBlock(BlockId::DeepslateLapisOre).entries[0].max ==
                      dropsForBlock(BlockId::LapisOre).entries[0].max,
              "the deepslate-to-stone collapse must run *below* the Silk Touch branch: the "
              "resource and its count are shared, the block itself is not");

// **The eight named blocks that turn into something else**, each asserted on
// both sides because a one-sided assert here would pass with the whole
// non-silk row deleted. Stone and deepslate are the pair a player meets first;
// the campfire is the one that was written for its soul twin and not itself.
//
// > Fails if: delete any of these from `silkTouchRecovers` - each falls back
// > to the resource, which is what it did before this pass.
static_assert(dropsForBlock(BlockId::Stone, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Stone) &&
                  dropsForBlock(BlockId::Stone).entries[0].item ==
                      itemForBlock(BlockId::Cobblestone) &&
                  dropsForBlock(BlockId::Deepslate, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Deepslate) &&
                  dropsForBlock(BlockId::Deepslate).entries[0].item ==
                      itemForBlock(BlockId::CobbledDeepslate),
              "stone is stone to Silk Touch and cobblestone to everything else, and deepslate "
              "reads the same against cobbled deepslate");
static_assert(dropsForBlock(BlockId::Glowstone, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Glowstone) &&
                  dropsForBlock(BlockId::Glowstone).entries[0].item == ItemId::GlowstoneDust &&
                  dropsForBlock(BlockId::Clay, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Clay) &&
                  dropsForBlock(BlockId::Clay).entries[0].item == ItemId::ClayBall &&
                  dropsForBlock(BlockId::SeaLantern, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::SeaLantern) &&
                  dropsForBlock(BlockId::SeaLantern).entries[0].item ==
                      ItemId::PrismarineCrystals &&
                  dropsForBlock(BlockId::Melon, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Melon) &&
                  dropsForBlock(BlockId::Melon).entries[0].item == ItemId::MelonSlice,
              "glowstone, clay, a sea lantern and a melon are four blocks whose whole value is "
              "the thing they shatter into, and Silk Touch is what keeps them whole");
static_assert(dropsForBlock(BlockId::AmethystCluster, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::AmethystCluster) &&
                  dropsForBlock(BlockId::AmethystCluster).entries[0].item == ItemId::AmethystShard &&
                  dropsForBlock(BlockId::LargeAmethystBud, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::LargeAmethystBud),
              "the full-grown cluster answers Silk Touch exactly as its three buds already did "
              "- it was the one of the four left out");
static_assert(dropsForBlock(BlockId::Campfire, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Campfire) &&
                  dropsForBlock(BlockId::Campfire).entries[0].item == ItemId::Charcoal &&
                  dropsForBlock(BlockId::SoulCampfire, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::SoulCampfire) &&
                  dropsForBlock(BlockId::SoulCampfire).entries[0].item ==
                      itemForBlock(BlockId::SoulSoil),
              "both campfires, both ways - the ordinary one was missing from the list its soul "
              "twin was already on, which is the one-of-a-pair shape");

// **Living coral is the third form of a rule the other two already had.** The
// plants and the fans were listed; the *block* was not, so a Silk Touch pickaxe
// handed back the dead variant - the one thing the enchantment exists to avoid.
static_assert(dropsForBlock(BlockId::TubeCoralBlock, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::TubeCoralBlock) &&
                  dropsForBlock(BlockId::TubeCoralBlock).entries[0].item ==
                      itemForBlock(BlockId::DeadTubeCoralBlock) &&
                  dropsForBlock(BlockId::DeadTubeCoralBlock, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::DeadTubeCoralBlock),
              "a live coral block survives Silk Touch and dies to everything else, and the "
              "already-dead one is unchanged by being outside the clause");

// **Nylium was a renewable supply of a block the reference does not give away.**
static_assert(dropsForBlock(BlockId::CrimsonNylium).entries[0].item ==
                      itemForBlock(BlockId::Netherrack) &&
                  dropsForBlock(BlockId::WarpedNylium).entries[0].item ==
                      itemForBlock(BlockId::Netherrack) &&
                  dropsForBlock(BlockId::CrimsonNylium, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::CrimsonNylium) &&
                  dropsForBlock(BlockId::WarpedNylium, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::WarpedNylium),
              "nylium is a crust on netherrack and comes off it, both colours - it used to hand "
              "itself back to a bare fist");

// **Gilded blackstone is gravel's roll on gold**, so it is asserted the way
// gravel is: the whole ladder, both entries, and the numerators summing to the
// denominator at every rung - which is what makes the pair a choice rather than
// two gifts. The four denominators are the reference's own 10, 7, 4 and
// "always". <https://minecraft.wiki/w/Gilded_Blackstone>
constexpr bool gildedBlackstoneRollsLikeGravel() {
    for (int f = 0; f <= 2; ++f) {
        const BlockDrop gilded = dropsForBlock(BlockId::GildedBlackstone, {.fortune = f});
        const BlockDrop gravel = dropsForBlock(BlockId::Gravel, {.fortune = f});
        if (gilded.count != 2 || gravel.count != 2) {
            return false;
        }
        // Same denominator as gravel at the same rung, same single salt, and a
        // numerator pair that sums to it.
        if (gilded.entries[0].chanceDen != gravel.entries[0].chanceDen) {
            return false;
        }
        if (gilded.entries[0].salt != gilded.entries[1].salt) {
            return false;
        }
        if (gilded.entries[0].chanceNum + gilded.entries[1].chanceNum !=
            gilded.entries[0].chanceDen) {
            return false;
        }
        if (gilded.entries[0].item != itemForBlock(BlockId::GildedBlackstone) ||
            gilded.entries[1].item != ItemId::GoldNugget || gilded.entries[1].min != 2 ||
            gilded.entries[1].max != 5) {
            return false;
        }
    }
    return true;
}

static_assert(gildedBlackstoneRollsLikeGravel(),
              "gilded blackstone must be one roll with two outcomes, on gravel's own ladder - "
              "two independent rolls would hand out block *and* nuggets");
static_assert(dropsForBlock(BlockId::GildedBlackstone).entries[1].chanceDen == 10 &&
                  dropsForBlock(BlockId::GildedBlackstone, {.fortune = 3}).count == 1 &&
                  dropsForBlock(BlockId::GildedBlackstone, {.fortune = 3}).entries[0].item ==
                      ItemId::GoldNugget &&
                  dropsForBlock(BlockId::GildedBlackstone, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::GildedBlackstone),
              "one time in ten unenchanted, always at Fortune III, and always the block itself "
              "under Silk Touch - it used to be the block every single time");

// A stem's seeds are its age, and a flat range was a free-item loop.
// <https://minecraft.wiki/w/Melon_Seeds>
//
// > Fails if: go back to `rangeDrop(seed, 0, 3)` - the first line's `chanceNum`
// > comparison goes false, because a range has a numerator of 1 at every age.
static_assert(dropsForBlock(BlockId::MelonStem0).entries[0].chanceNum == 1 &&
                  dropsForBlock(BlockId::MelonStem0).entries[0].chanceDen == 15 &&
                  dropsForBlock(BlockId::MelonStemLast).entries[0].chanceNum == 8 &&
                  dropsForBlock(BlockId::MelonStemLast).entries[0].chanceDen == 15,
              "a fresh stem pays 1/15 per seed and a ripe one 8/15, which is the reference's "
              "Binomial(3, (age + 1) / 15) exactly");
static_assert(dropsForBlock(BlockId::MelonStem0).count == 3 &&
                  dropsForBlock(BlockId::MelonStem0).entries[0].salt !=
                      dropsForBlock(BlockId::MelonStem0).entries[1].salt &&
                  dropsForBlock(BlockId::MelonStem0).entries[1].salt !=
                      dropsForBlock(BlockId::MelonStem0).entries[2].salt &&
                  dropsForBlock(BlockId::MelonStem0).entries[0].max == 1,
              "three independent one-seed rolls on three salts *are* the binomial - one salt "
              "shared would make them alternatives and cap the haul at one seed");
static_assert(dropsForBlock(BlockId::MelonStemAttachedFirst).entries[0].chanceNum ==
                      dropsForBlock(BlockId::MelonStemLast).entries[0].chanceNum &&
                  dropsForBlock(BlockId::PumpkinStem0).entries[0].item == ItemId::PumpkinSeeds,
              "an attached stem uses the age-7 row, and the pumpkin half pays pumpkin seeds");

// Nether wart: the crop that could not turn a profit.
// <https://minecraft.wiki/w/Nether_Wart>
//
// > Fails if: delete the `isNetherWart` branch - both ages fall to the tail
// > line and pay exactly one.
static_assert(dropsForBlock(BlockId::NetherWartLast).entries[0].min == 2 &&
                  dropsForBlock(BlockId::NetherWartLast).entries[0].max == 4 &&
                  dropsForBlock(BlockId::NetherWart0).entries[0].max == 1,
              "a ripe nether wart is 2-4 and an immature one is exactly the wart it cost");

// The four named rows, each of which used to hand back the block itself.
// <https://minecraft.wiki/w/Soul_Campfire>, <https://minecraft.wiki/w/Chorus_Plant>,
// <https://minecraft.wiki/w/Nether_Gold_Ore>, <https://minecraft.wiki/w/Large_Fern>
//
// > Fails if: delete any one of the four case labels - each falls to the tail
// > line, which answers `itemForBlock` for the first three and one fern for
// > the fourth.
static_assert(dropsForBlock(BlockId::SoulCampfire).entries[0].item ==
                      itemForBlock(BlockId::SoulSoil) &&
                  dropsForBlock(BlockId::SoulCampfire, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::SoulCampfire) &&
                  dropsForBlock(BlockId::Campfire).entries[0].item == ItemId::Charcoal,
              "a soul campfire is soul soil, not itself - and the ordinary campfire's two "
              "charcoal must not move with it");
static_assert(dropsForBlock(BlockId::ChorusPlant).entries[0].item == ItemId::ChorusFruit &&
                  dropsForBlock(BlockId::ChorusPlant).entries[0].min == 0 &&
                  dropsForBlock(BlockId::ChorusPlant).entries[0].max == 1 &&
                  dropsForBlock(BlockId::ChorusFlower).entries[0].item ==
                      itemForBlock(BlockId::ChorusFlower),
              "a chorus plant is 0-1 fruit and the flower beside it really does drop itself - "
              "the two are one block family and two answers");
static_assert(dropsForBlock(BlockId::NetherGoldOre).entries[0].item == ItemId::GoldNugget &&
                  dropsForBlock(BlockId::NetherGoldOre).entries[0].min == 2 &&
                  dropsForBlock(BlockId::NetherGoldOre).entries[0].max == 6,
              "nether gold ore is 2-6 nuggets, where the tail line paid a fixed 4");
static_assert(dropsForBlock(BlockId::LargeFern, {.tool = ToolKind::Shears}).entries[0].item ==
                      itemForBlock(BlockId::Fern) &&
                  dropsForBlock(BlockId::LargeFern, {.tool = ToolKind::Shears}).entries[0].max ==
                      2 &&
                  dropsForBlock(BlockId::Fern, {.tool = ToolKind::Shears}).entries[0].item ==
                      itemForBlock(BlockId::Fern),
              "a sheared large fern is two of the small one - it has no item form of its own");

// The three nether plants, which had no rows and so were free.
// <https://minecraft.wiki/w/Nether_Sprouts>, <https://minecraft.wiki/w/Twisting_Vines>
//
// > Fails if: delete either branch - both fall to the tail line and hand back a
// > guaranteed one of themselves to a bare fist.
static_assert(dropsForBlock(BlockId::NetherSprouts).empty() &&
                  dropsForBlock(BlockId::NetherSprouts, {.tool = ToolKind::Shears})
                          .entries[0]
                          .item == itemForBlock(BlockId::NetherSprouts),
              "nether sprouts are shears or nothing, and the reference states no Silk Touch "
              "row for them - so this table does not invent one");
static_assert(dropsForBlock(BlockId::TwistingVines).entries[0].chanceNum == 1 &&
                  dropsForBlock(BlockId::TwistingVines).entries[0].chanceDen == 3 &&
                  dropsForBlock(BlockId::WeepingVines, {.tool = ToolKind::Shears})
                          .entries[0]
                          .chanceDen == 1,
              "a twisting vine is a 33% roll by hand and a certainty to shears");

// Dead coral is no more collectable than live coral, and the block is.
// <https://minecraft.wiki/w/Dead_Coral>
//
// > Fails if: put `&& !isDeadCoral(block)` back on the coral clause in
// > `yieldsNothingEver` - the first two lines go false and ten dead plants and
// > fans hand themselves back for free.
static_assert(dropsForBlock(BlockId::DeadTubeCoral).empty() &&
                  dropsForBlock(BlockId::DeadTubeCoralFan).empty() &&
                  dropsForBlock(BlockId::DeadTubeCoral, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::DeadTubeCoral) &&
                  dropsForBlock(BlockId::DeadTubeCoralBlock).entries[0].item ==
                      itemForBlock(BlockId::DeadTubeCoralBlock),
              "a dead coral plant needs Silk Touch; a dead coral *block* drops itself, which is "
              "the distinction the old clause was reaching for and got backwards");

// Gravel's flint ladder, which ignored both enchantments it depends on.
// <https://minecraft.wiki/w/Gravel>
//
// > Fails if: drop the `ctx.fortune` ladder back to a fixed 1-in-10 - the first
// > three comparisons go false and Fortune III stops being a certainty.
static_assert(dropsForBlock(BlockId::Gravel).entries[1].chanceDen == 10 &&
                  dropsForBlock(BlockId::Gravel, {.fortune = 1}).entries[1].chanceDen == 7 &&
                  dropsForBlock(BlockId::Gravel, {.fortune = 2}).entries[1].chanceDen == 4 &&
                  dropsForBlock(BlockId::Gravel, {.fortune = 3}).count == 1 &&
                  dropsForBlock(BlockId::Gravel, {.fortune = 3}).entries[0].item ==
                      ItemId::Flint &&
                  dropsForBlock(BlockId::Gravel, {.silkTouch = true}).entries[0].item ==
                      itemForBlock(BlockId::Gravel),
              "10%, 1/7, 25%, then certainty - and Silk Touch takes the roll away entirely");

// A leaf's three Fortune ladders, read the way a real break reads them - off
// `dropsForBlock`, never off `leafChanceDen` - so no clause here compares one
// side of the derivation against the other. <https://minecraft.wiki/w/Leaves>
//
// > Fails if: hand `row.saplingDen` straight to the entry again. Every
// > Fortune-0 comparison goes on passing and the nine that follow do not,
// > which is the shape the bug had.
static_assert(dropsForBlock(BlockId::Leaves).entries[0].chanceDen == 20 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 1}).entries[0].chanceDen == 16 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 2}).entries[0].chanceDen == 12 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[0].chanceDen == 10,
              "an oak sapling is the reference's 5% climbing to 6.25 / 8.33 / 10% - a fixed "
              "1-in-20 makes Fortune worth nothing on the one block it is farmed on");
static_assert(dropsForBlock(BlockId::JungleLeaves).entries[0].chanceDen == 40 &&
                  dropsForBlock(BlockId::JungleLeaves, {.fortune = 1}).entries[0].chanceDen == 36 &&
                  dropsForBlock(BlockId::JungleLeaves, {.fortune = 3}).entries[0].chanceDen == 24,
              "a jungle sapling climbs a ladder of its own, 2.5% to 4.17%, and not a scaled "
              "copy of the other five's - it is the one row whose base is 40");
static_assert(dropsForBlock(BlockId::Leaves).entries[1].chanceDen == 50 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[1].chanceDen == 30 &&
                  dropsForBlock(BlockId::Leaves).entries[2].chanceDen == 200 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[2].chanceDen == 120,
              "sticks are 2% climbing to 3.33% and an apple 0.5% to 0.833%, on two more "
              "ladders and two more salts - three independent rolls, not one");
static_assert(dropsForBlock(BlockId::Leaves, {.fortune = 9}).entries[0].chanceDen == 10 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = -1}).entries[0].chanceDen == 20,
              "a level either side of the published four clamps rather than reading off the "
              "end of a ladder");
static_assert(dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[0].min == 1 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[0].max == 1 &&
                  dropsForBlock(BlockId::Leaves, {.fortune = 3}).entries[1].max == 2,
              "Fortune moves a leaf's chances and never its counts - one sapling or none, "
              "one or two sticks, at every level");

// The mature sweet berry bush, which paid a flat two.
static_assert(dropsForBlock(BlockId::SweetBerryBush).entries[0].item == ItemId::SweetBerries &&
                  dropsForBlock(BlockId::SweetBerryBush).entries[0].min == 2 &&
                  dropsForBlock(BlockId::SweetBerryBush).entries[0].max == 3 &&
                  dropsForBlock(BlockId::SweetBerryBush).count == 1,
              "a mature bush is the reference's 2-3 berries; the flat 2 came from "
              "`dropCountForBlock`, which has no range to give");

// The renewable split. A one-shot feature is a property of its cell for ever; a
// block that grows back must not be, or the cell is a rate the player owns.
static_assert(!rerollsEachBreak(BlockId::Gravel) && !rerollsEachBreak(BlockId::LapisOre) &&
                  rerollsEachBreak(BlockId::Melon) && rerollsEachBreak(BlockId::Leaves) &&
                  rerollsEachBreak(BlockId::SweetBerryBush) &&
                  rerollsEachBreak(cropAt(BlockId::CarrotCrop0, 7)),
              "gravel's flint is a fixed property of its cell and a melon's yield must not be - "
              "and a berry bush is replanted from what it drops, so it is a melon");
static_assert(resolveBreak(BlockId::Gravel, {}, 12, 40, -7, 1).count ==
                      resolveBreak(BlockId::Gravel, {}, 12, 40, -7, 9999).count &&
                  resolveBreak(BlockId::Gravel, {}, 12, 40, -7, 1).stacks[0].item ==
                      resolveBreak(BlockId::Gravel, {}, 12, 40, -7, 9999).stacks[0].item,
              "no nonce reaches a one-shot block: breaking the same gravel twice must give the "
              "same haul twice, which is what stops a save reload re-rolling it");

/// Whether one melon cell pays differently across a handful of breaks.
///
/// Written as a search rather than as two nonces picked out of the air, because
/// a 3-7 range really does repeat itself one time in five and an assert that
/// happened to land on a repeat would be a build failure nobody could read.
constexpr bool melonYieldMoves() {
    const int first = resolveBreak(BlockId::Melon, {}, 12, 40, -7, 1).stacks[0].count;
    for (std::uint32_t nonce = 2; nonce < 8; ++nonce) {
        if (resolveBreak(BlockId::Melon, {}, 12, 40, -7, nonce).stacks[0].count != first) {
            return true;
        }
    }
    return false;
}

static_assert(melonYieldMoves(),
              "a melon that regrew on the same cell must not pay the identical slices for ever");

/// Whether Fortune moves what a block pays, holding everything else equal.
///
/// A helper rather than two hand-written comparisons, because a `BlockDrop` is
/// five fields per entry and an assert that compared only the item would pass
/// happily while the counts and the chances moved underneath it.
constexpr bool fortuneChangesDrop(BlockId block, ToolKind tool, int tier) {
    const BlockDrop plain = dropsForBlock(block, {.tool = tool, .tier = tier, .fortune = 0});
    const BlockDrop lucky = dropsForBlock(block, {.tool = tool, .tier = tier, .fortune = 3});
    if (plain.count != lucky.count) {
        return true;
    }
    for (int i = 0; i < plain.count; ++i) {
        if (plain.entries[i].item != lucky.entries[i].item ||
            plain.entries[i].min != lucky.entries[i].min ||
            plain.entries[i].max != lucky.entries[i].max ||
            plain.entries[i].chanceNum != lucky.entries[i].chanceNum ||
            plain.entries[i].chanceDen != lucky.entries[i].chanceDen) {
            return true;
        }
    }
    return false;
}

// **A GAP, WRITTEN AS A TRIPWIRE RATHER THAN AS A COMMENT.** Measured 2026-08-19
// over all 3314 ids: Fortune III changes the drop for exactly 12 of them - gravel,
// ten leaf types and gilded blackstone. **Not one of the 18 ore ids moves.** Diamond
// ore pays one diamond at Fortune 0 and one diamond at Fortune III.
//
// **Deliberately not implemented here, and the reason is not laziness.** There are no
// enchantments in this project, so `ctx.fortune` can only ever arrive as 0 from a real
// break - the same dormancy Silk Touch has, which is recorded and accepted. Writing the
// ore multiplier now would add a table nobody can reach, which is `CLAUDE.md` bug shape
// #15 in its purest form: a complete feature sitting one predicate short of being
// reachable at all, building clean and doing nothing.
//
// **What this assert is for.** The day enchantments land, ores will silently keep paying
// one apiece while gravel and leaves quietly do the right thing - a gap with no symptom
// except "mining feels unrewarding". This fails the build at that moment and says why.
// Its first two clauses are the gap; **the third is the control**, and it is the half
// that makes the other two mean anything: gravel really does move under Fortune, so a
// pass here cannot be `fortuneChangesDrop` having quietly become "return false".
//
// **To whoever removes this:** the reference multiplies the ORE'S DROPPED ITEM COUNT by
// a random factor rising with the level - it does not change the drop chance, which is
// the shape the leaf and gravel rows use, so do not copy them. Ores that drop themselves
// as a block (iron, gold, copper) are unaffected; only the ones dropping an item are.
// **Verify the exact distribution against the wiki before writing it** - deliberately
// not quoted here, because a number in a comment is the thing a later reader implements
// without re-checking, and this file has already been bitten by that once.
static_assert(!fortuneChangesDrop(BlockId::DiamondOre, ToolKind::Pickaxe, kDiamondTier) &&
                  !fortuneChangesDrop(BlockId::LapisOre, ToolKind::Pickaxe, kDiamondTier) &&
                  fortuneChangesDrop(BlockId::Gravel, ToolKind::Shovel, kDiamondTier),
              "Fortune reaches gravel and leaves but no ore. If this failed because you "
              "wired ore multipliers, DELETE THE FIRST TWO CLAUSES - the gap is closed and "
              "the assert has done its job. If it failed because gravel stopped moving, "
              "the Fortune plumbing itself is broken and that is a real bug");

} // namespace game
