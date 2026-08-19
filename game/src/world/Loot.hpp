#pragma once

#include "item/Item.hpp"
#include "world/Block.hpp"
#include "world/Chest.hpp"
#include "world/Noise.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace game::loot {

/// What a generated container holds, as three flat `constexpr` arrays.
///
/// The reference's loot tables are a list of **pools**; each pool is drawn a
/// number of times, and each draw picks one weighted **entry** and a count for
/// it. That is the whole model - Bedrock's schema is a fork of Java's pre-1.13
/// one and has no groups, no alternatives and no sequences - so it fits three
/// arrays and an enum index, the shape `kJobSites`, `kBiomes` and `kBlocks`
/// already use. Under a kilobyte of `.rodata`, no heap, and no allocation
/// during a roll.
///
/// **What is deliberately not modelled**, because no village table uses any of
/// it: conditions, `quality`, `bonus_rolls`, sub-table entries, and every
/// function except `set_count` - which is folded onto the entry row as a count
/// range rather than modelled as a function at all. Modelling "functions" would
/// be a plugin architecture for one plugin.
///
/// **Enchanting blocks nothing here.** Not one Bedrock village loot table
/// contains `enchant_randomly`, `enchant_with_levels` or an enchanted book;
/// every weapon, tool and armour piece in a village chest is plain. The only
/// items in the whole village set this game cannot produce are the snowball,
/// the bundle and the three horse armours - about 5% by weight, and the only
/// one of them reachable from a table below is the horse armour row in
/// `village_weaponsmith`, kept there as a weighted nothing.

/// Which table a container rolls.
///
/// **The order is written into saved worlds.** An unrolled chest stores its
/// table as `(id - LootChestRunFirst) / 4`, so reordering these renames the
/// loot in every village a player has already walked past but not opened.
/// Append; never insert.
enum class TableId : std::uint8_t {
    /// Bedrock `village_weaponsmith`, at its pre-copper weights.
    VillageWeaponsmith,
    /// Bedrock `village_toolsmith`.
    VillageToolsmith,
    /// Bedrock `village_plains_house`, standing in for all five biome houses
    /// until the rest of the village set arrives and can be keyed off
    /// `VillageType`.
    VillageHouse,
    /// Bedrock `village_mason`, for `Village.cpp`'s stonecutter job site.
    VillageMason,
    /// Bedrock `village_fletcher`, for the fletching table.
    VillageFletcher,
    /// Bedrock `village_shepherd`, for the loom.
    VillageShepherd,
    /// Bedrock `village_cartographer`, for the cartography table.
    VillageCartographer,
    /// Bedrock `village_armorer` (spelled Mojang's way in the pack, ours in the
    /// enum), for the blast furnace.
    VillageArmourer,
    /// Bedrock `village_butcher`, for the smoker.
    VillageButcher,
    /// Bedrock `village_tannery`, for the cauldron - the leatherworker's, named
    /// after the building rather than the job in the reference's own files.
    VillageTannery,
    /// Bedrock `village_temple`, for the brewing stand - the cleric's, likewise
    /// named after the building.
    VillageTemple,
    Count,
};

// **Three of `Village.cpp`'s thirteen job sites get no chest on purpose.**
//
// The composter (farmer), the barrel (fisherman) and the lectern (librarian)
// have **no loot table in the reference at all** - the shipped behaviour pack
// ships fifteen village chest tables and there is no `village_farmer`,
// `village_fisher` or `village_librarian` among them. So "the farmer's house
// has no chest" is the reference's behaviour, not a gap, and inventing a table
// for it would be inventing loot. Recorded here because it looks exactly like
// the eight that *were* missing, and the next person to count job sites
// against tables will otherwise find the same three and "fix" them.

/// Salt for the loot stream, beside `Village.cpp`'s `kLayoutSalt` and
/// `kDressSalt` and for the same reason: two different questions about the same
/// block must never accidentally share a stream.
constexpr std::uint32_t kLootSalt = 0x100731a1u;

/// One weighted row of a pool.
///
/// Six bytes. `ItemId::None` is the reference's `empty` entry - a real weighted
/// outcome that yields nothing, not a hole in the table - and it is how an item
/// this game does not have is kept out of a chest **without disturbing every
/// other item's rate in the same pool**.
struct Entry {
    ItemId item = ItemId::None;
    /// The reference defaults an omitted weight to 1, and many village entries
    /// omit it, so the default is the reference's rather than zero.
    std::uint16_t weight = 1;
    std::uint8_t countMin = 1;
    std::uint8_t countMax = 1;
};

/// A slice of `kEntries`, plus how many times it is drawn.
///
/// `total` is stored rather than summed at runtime **and asserted against the
/// sum below**, so a mistyped weight is a build failure instead of a drop rate
/// that is quietly wrong forever and biased toward the last row.
struct Pool {
    std::uint16_t first = 0;
    std::uint16_t count = 0;
    /// Inclusive of both ends, which is the reference's meaning.
    std::uint8_t rollsMin = 1;
    std::uint8_t rollsMax = 1;
    std::uint16_t total = 0;
};

/// A table is a slice of `kPools`, indexed by `TableId`.
struct Table {
    std::uint8_t firstPool = 0;
    std::uint8_t poolCount = 0;
};

/// Every entry in the game, one flat run per pool, in `TableId` order.
///
/// Numbers are Mojang's shipped Bedrock behaviour pack, not the wiki - the
/// wiki's village pages blank most weight cells and get the butcher's meat and
/// the plains house's poppy wrong. Where a recent version changed a table, the
/// pre-change baseline is used, because this game has no copper tier and no
/// spears.
inline constexpr std::array<Entry, 84> kEntries{{
    // ---- village_weaponsmith, 3-8 rolls, sum 94. ----
    // The one table the reference puts in *every* village.
    {ItemId::Bread, 15, 1, 3},
    {ItemId::Apple, 15, 1, 3},
    {ItemId::IronIngot, 10, 1, 5},
    {ItemId::GoldIngot, 5, 1, 3},
    {ItemId::IronPickaxe, 5, 1, 1},
    {ItemId::IronSword, 5, 1, 1},
    {ItemId::IronChestplate, 5, 1, 1},
    {ItemId::IronHelmet, 5, 1, 1},
    {ItemId::IronLeggings, 5, 1, 1},
    {ItemId::IronBoots, 5, 1, 1},
    {itemForBlock(BlockId::Obsidian), 5, 3, 7},
    {itemForBlock(BlockId::OakSapling), 5, 3, 7},
    {ItemId::Diamond, 3, 1, 3},
    {ItemId::Saddle, 3, 1, 1},
    // The reference's three horse armours, kept as a **weighted nothing**
    // rather than deleted. There are no horses to wear them, but dropping the
    // row outright would raise every other item in this pool by 3.3% against
    // the reference; an empty entry keeps all fourteen rates exactly right and
    // costs one row. Delete it the day horses arrive.
    {ItemId::None, 3, 1, 1},

    // ---- village_toolsmith, 3-8 rolls, sum 53. ----
    {ItemId::Stick, 20, 1, 3},
    {ItemId::Bread, 15, 1, 3},
    {ItemId::IronIngot, 5, 1, 5},
    {ItemId::IronPickaxe, 5, 1, 1},
    {ItemId::IronShovel, 5, 1, 1},
    {ItemId::Diamond, 1, 1, 3},
    {ItemId::GoldIngot, 1, 1, 3},
    {ItemId::Coal, 1, 1, 3},

    // ---- village_plains_house, 3-8 rolls, sum 43. ----
    // The reference adds a second pool here - a 1-in-3 bundle - which is
    // **omitted, not approximated**: there is no bundle item, and the pool is
    // its own pool, so leaving it out changes nothing else in the table.
    {ItemId::Potato, 10, 1, 7},
    {ItemId::Bread, 10, 1, 4},
    {ItemId::Apple, 10, 1, 5},
    {itemForBlock(BlockId::OakSapling), 5, 1, 2},
    {itemForBlock(BlockId::Dandelion), 2, 1, 1},
    {ItemId::Emerald, 2, 1, 4},
    {ItemId::GoldNugget, 1, 1, 3},
    {itemForBlock(BlockId::Poppy), 1, 1, 1},
    {ItemId::Book, 1, 1, 1},
    {ItemId::Feather, 1, 1, 1},

    // ---- village_mason, 1-5 rolls, sum 13. ----
    // The pack's `dye` rows carry the pre-flattening data value, and the dye
    // numbering is **not** the wool numbering: `dye` 11 is dandelion yellow,
    // not blue. Read the data value, never the slot order.
    {ItemId::ClayBall, 1, 1, 3},
    {itemForBlock(BlockId::FlowerPot), 1, 1, 1},
    {itemForBlock(BlockId::Stone), 2, 1, 1},
    {itemForBlock(BlockId::StoneBricks), 2, 1, 1},
    {ItemId::Bread, 4, 1, 4},
    {ItemId::YellowDye, 1, 1, 1},
    {itemForBlock(BlockId::SmoothStone), 1, 1, 1},
    {ItemId::Emerald, 1, 1, 1},

    // ---- village_fletcher, 1-5 rolls, sum 23. ----
    {ItemId::Emerald, 1, 1, 1},
    {ItemId::Arrow, 2, 1, 3},
    {ItemId::Feather, 6, 1, 3},
    {ItemId::Egg, 2, 1, 3},
    {ItemId::Flint, 6, 1, 3},
    {ItemId::Stick, 6, 1, 3},

    // ---- village_shepherd, 1-5 rolls, sum 23. ----
    // Five of the sixteen wools, by pack data value: 0 white, 15 black, 7 gray,
    // 12 brown, 8 light gray. The wool run in `Block.hpp` is declared in that
    // same order, so the arithmetic would work - and is written out as five
    // named ids anyway, because a `WhiteWool + 12` here is the shape that gave
    // a cherry door a mangrove recipe.
    {itemForBlock(BlockId::WhiteWool), 6, 1, 8},
    {itemForBlock(BlockId::BlackWool), 3, 1, 3},
    {itemForBlock(BlockId::GrayWool), 2, 1, 3},
    {itemForBlock(BlockId::BrownWool), 2, 1, 3},
    {itemForBlock(BlockId::LightGrayWool), 2, 1, 3},
    {ItemId::Emerald, 1, 1, 1},
    {ItemId::Shears, 1, 1, 1},
    {ItemId::Wheat, 6, 1, 6},

    // ---- village_cartographer, 1-5 rolls, sum 50. ----
    // The pack's `map` with no data is the **empty** map, not a filled one.
    {ItemId::EmptyMap, 10, 1, 3},
    {ItemId::Paper, 15, 1, 5},
    {ItemId::Compass, 5, 1, 1},
    {ItemId::Bread, 15, 1, 4},
    {itemForBlock(BlockId::OakSapling), 5, 1, 2},

    // ---- village_armorer, 1-5 rolls, sum 8. ----
    // Byte-identical to `village_desert_house` in the shipped pack. That is the
    // reference's own duplication, not a transcription slip, and the two are
    // **not** merged here: they are separate tables that are free to diverge,
    // and sharing a pool would make one silently follow the other.
    {ItemId::IronIngot, 2, 1, 3},
    {ItemId::Bread, 4, 1, 4},
    {ItemId::IronHelmet, 1, 1, 1},
    {ItemId::Emerald, 1, 1, 1},

    // ---- village_butcher, 1-5 rolls, sum 28. ----
    {ItemId::Emerald, 1, 1, 1},
    {ItemId::RawPorkchop, 6, 1, 3},
    {ItemId::Wheat, 6, 1, 3},
    {ItemId::RawBeef, 6, 1, 3},
    {ItemId::RawMutton, 6, 1, 3},
    {ItemId::Coal, 3, 1, 3},

    // ---- village_tannery, 1-5 rolls, sum 16. ----
    {ItemId::Leather, 1, 1, 3},
    {ItemId::LeatherChestplate, 2, 1, 1},
    {ItemId::LeatherBoots, 2, 1, 1},
    {ItemId::LeatherHelmet, 2, 1, 1},
    {ItemId::Bread, 5, 1, 4},
    {ItemId::LeatherLeggings, 2, 1, 1},
    {ItemId::Saddle, 1, 1, 1},
    {ItemId::Emerald, 1, 1, 4},

    // ---- village_temple, 3-8 rolls, sum 19. ----
    // `dye` 4 is **lapis lazuli**, the ore item, not blue dye - in the
    // pre-flattening numbering lapis *was* the blue dye and had no separate id.
    // Java's `village/temple` lists `lapis_lazuli` outright, which settles it.
    {ItemId::Redstone, 2, 1, 4},
    {ItemId::Bread, 7, 1, 4},
    {ItemId::RottenFlesh, 7, 1, 4},
    {ItemId::LapisLazuli, 1, 1, 4},
    {ItemId::GoldIngot, 1, 1, 4},
    {ItemId::Emerald, 1, 1, 4},
}};

inline constexpr std::array<Pool, 11> kPools{{
    {0, 15, 3, 8, 94},
    {15, 8, 3, 8, 53},
    {23, 10, 3, 8, 43},
    {33, 8, 1, 5, 13},
    {41, 6, 1, 5, 23},
    {47, 8, 1, 5, 23},
    {55, 5, 1, 5, 50},
    {60, 4, 1, 5, 8},
    {64, 6, 1, 5, 28},
    {70, 8, 1, 5, 16},
    {78, 6, 3, 8, 19},
}};

inline constexpr std::array<Table, static_cast<std::size_t>(TableId::Count)> kTables{{
    {0, 1},
    {1, 1},
    {2, 1},
    {3, 1},
    {4, 1},
    {5, 1},
    {6, 1},
    {7, 1},
    {8, 1},
    {9, 1},
    {10, 1},
}};

// ---------------------------------------------------------------------------
// The asserts. A malformed table is a build failure, not a wrong drop rate.
// ---------------------------------------------------------------------------

/// Whether an item id names something this game actually has.
///
/// A block item is named by its block, and `blockName`'s last resort is "Air",
/// so the two halves need different tests - which is exactly the trap
/// `itemDisplayName` exists to close for the HUD.
constexpr bool namedItem(ItemId item) {
    if (isBlockItem(item)) {
        return !blockNameIs(blockForItem(item), "Air");
    }
    return itemName(item)[0] != '\0';
}

/// **The load-bearing one.** Every pool's stored `total` must equal the sum of
/// its entries' weights, and its slice must lie inside `kEntries`. Without
/// this, a mistyped weight or a forgotten row skews the whole pool and biases
/// whichever entry happens to sit last - silently, forever, in a system nobody
/// can eyeball.
constexpr bool poolsSane() {
    for (const Pool& pool : kPools) {
        if (pool.count == 0 ||
            static_cast<std::size_t>(pool.first) + pool.count > kEntries.size()) {
            return false;
        }
        if (pool.rollsMin < 1 || pool.rollsMin > pool.rollsMax) {
            return false;
        }
        int sum = 0;
        for (std::size_t e = 0; e < pool.count; ++e) {
            sum += kEntries[static_cast<std::size_t>(pool.first) + e].weight;
        }
        if (sum != static_cast<int>(pool.total)) {
            return false;
        }
    }
    return true;
}

/// Every entry names a real item in a count range that can exist, and that fits
/// one slot - wheat at 8-12 against a tool's stack limit of 1 is the shape of
/// mistake this catches.
constexpr bool entriesSane() {
    for (const Entry& entry : kEntries) {
        if (entry.countMin < 1 || entry.countMin > entry.countMax) {
            return false;
        }
        if (entry.item == ItemId::None) {
            continue;
        }
        if (!namedItem(entry.item)) {
            return false;
        }
        if (entry.countMax > maxStackFor(entry.item)) {
            return false;
        }
    }
    return true;
}

/// Every table has pools behind it, they lie inside `kPools`, and **no table
/// can overflow a chest**. The last one is what lets the roll skip a "chest
/// full" branch, which would otherwise bias the result toward whatever was
/// drawn first.
constexpr bool tablesSane() {
    for (const Table& table : kTables) {
        if (table.poolCount == 0 ||
            static_cast<std::size_t>(table.firstPool) + table.poolCount > kPools.size()) {
            return false;
        }
        int stacks = 0;
        for (std::size_t p = 0; p < table.poolCount; ++p) {
            stacks += kPools[static_cast<std::size_t>(table.firstPool) + p].rollsMax;
        }
        if (stacks > static_cast<int>(kChestSlots)) {
            return false;
        }
    }
    return true;
}

/// **Every pool is claimed by exactly one table**, and every entry by exactly
/// one pool.
///
/// This replaces a `static_assert(kTables.size() == TableId::Count)` that could
/// not fail: `kTables` is *declared* as `std::array<Table, TableId::Count>`, so
/// it was comparing a number against itself and the compiler would have caught
/// the real mistake anyway. What it was reaching for - "a table row was added
/// or lost" - is covered here in the form that actually goes wrong: a pool that
/// nothing draws from is a loot table written and never wired up, and a pool
/// two tables share is one of them silently rolling the other's contents.
constexpr bool sliceOwnershipIsExclusive() {
    std::array<int, kPools.size()> poolClaims{};
    for (const Table& table : kTables) {
        for (std::size_t p = 0; p < table.poolCount; ++p) {
            ++poolClaims[static_cast<std::size_t>(table.firstPool) + p];
        }
    }
    for (const int claims : poolClaims) {
        if (claims != 1) {
            return false;
        }
    }
    std::array<int, kEntries.size()> entryClaims{};
    for (const Pool& pool : kPools) {
        for (std::size_t e = 0; e < pool.count; ++e) {
            ++entryClaims[static_cast<std::size_t>(pool.first) + e];
        }
    }
    for (const int claims : entryClaims) {
        if (claims != 1) {
            return false;
        }
    }
    return true;
}

static_assert(poolsSane(),
              "a pool's stored total disagrees with its weights, its slice runs off the end of "
              "kEntries, or its roll range is inverted");
static_assert(entriesSane(),
              "a loot entry names an item this game does not have, an inverted count range, or "
              "more of one item than a single slot holds");
static_assert(tablesSane(),
              "a loot table has no pools behind it, or can roll more stacks than a chest has "
              "slots");
static_assert(sliceOwnershipIsExclusive(),
              "a pool or an entry is drawn by two owners, or by none - a table row was added "
              "without its pool, or two tables were pointed at the same slice");
static_assert(kLootChestTables == static_cast<int>(TableId::Count),
              "every loot table needs four block ids to be placed as an unrolled chest - widen "
              "kLootChestTables in Block.hpp, or the new table can never reach a village");

// ---------------------------------------------------------------------------
// The roll
// ---------------------------------------------------------------------------

/// The stream seed for the container standing at `at`.
///
/// **The world seed and the block position, and nothing else.** Opening,
/// breaking and blowing up a chest each know a block position and no more, so
/// anything else would have to be re-derived in three places; and a
/// village-relative key - "building 4 of village 12" - would silently re-roll
/// every chest in every existing world the day `kPlotWeights` is retuned. A
/// block position never changes. It is also the established precedent here:
/// gravel's flint and every per-cell cosmetic choice in the village builder
/// already hash off world position for exactly this reason.
///
/// `noise::hashCoords3` is this hash and would be the natural call, but it
/// lives in `Noise.cpp`'s anonymous namespace and is not declared in the
/// header. Folding Y into the *seed* of the exported `hash2D` reaches the same
/// avalanche without a second copy of its constants sitting here to drift out
/// of step. Integer throughout, so the result cannot move between compilers.
inline std::uint32_t streamSeed(std::uint32_t worldSeed, const glm::ivec3& at) {
    const auto y = static_cast<std::uint32_t>(at.y);
    return noise::hash2D(worldSeed ^ kLootSalt ^ (y * 0xc2b2ae35u), at.x, at.z);
}

/// Puts one rolled stack into a chest.
///
/// The reference scatters loot rather than filling from slot zero - a chest
/// with its contents packed into the first four slots reads as generated the
/// moment a player opens one - so the roll picks a slot and this probes forward
/// from it, merging where the same item already sits and taking the first free
/// slot otherwise. **Nothing here draws from the stream**, so a chest that has
/// somehow been filled already (a hopper pointed into it, say) cannot change
/// what the rest of the roll produces.
inline void placeStack(Chest& chest, ItemId item, int count, int slot) {
    if (item == ItemId::None || count <= 0) {
        return;
    }
    const int limit = maxStackFor(item);
    for (std::size_t step = 0; step < kChestSlots; ++step) {
        ItemStack& into = chest.slots[(static_cast<std::size_t>(slot) + step) % kChestSlots];
        int put = 0;
        if (into.empty()) {
            put = std::min(count, limit);
            into = ItemStack{item, put, 0};
        } else if (into.item == item) {
            put = std::max(0, std::min(limit - into.count, count));
            into.count += put;
        }
        count -= put;
        if (count <= 0) {
            return;
        }
    }
}

/// Fills a container from a table, deterministically, from the world seed and
/// the container's own position.
///
/// **Nothing about the chest may change how the stream advances**, which is the
/// one rule that silently breaks a system like this. Every draw here is a
/// function of the seed, the table and what the stream itself has already said,
/// and of nothing else: the count and the slot are drawn for an entry that
/// yields nothing exactly as for one that yields something, and `placeStack`
/// draws nothing at all, so a stack with nowhere to go costs the stream what a
/// stack that fitted costs. Break that and two players on the same seed get
/// different chests, depending on state that is not in the hash.
///
/// > **What is *not* true is that every roll costs a fixed number of draws.**
/// > `Stream::range` returns 0 without drawing when `n <= 1` (`Noise.hpp`), so
/// > a fixed-count entry - every tool, every armour piece, the poppy - takes no
/// > count draw, and a single-entry pool would take no pick draw. That is still
/// > deterministic, because *which* entry was chosen is itself a function of
/// > the stream; it just means the rule to hold on to is the one above, and not
/// > "the same number of draws every time".
///
/// Rolls are sampled **with replacement**, as the reference does: one pool can
/// hand back five stacks of bread.
///
/// > **This ADDS to the container rather than replacing what is in it, so it
/// > must be called exactly once per chest.** The clear cannot live in here: a
/// > second call would then wipe whatever the player had already put in, which
/// > is worse than the doubling it would prevent. So a once-only guard is the
/// > *caller's* to bring, and the two that exist today both bring one -
/// > `Main.cpp` rolls on first open, and `WorldStore.cpp` deliberately hands
/// > back `plainChestFor(id)` rather than calling this at all.
/// >
/// > **The failure is quiet.** A chunk-format bump that leaves a loot chest
/// > sitting over a stale chest record rolls a second time into the stale
/// > contents rather than over them, and the player finds a doubled chest with
/// > nothing in any log to say so. Verified 2026-08-19: one call site,
/// > `Main.cpp`. That stops being true the day a second one appears, which is
/// > exactly when this paragraph matters.
inline void rollInto(Chest& chest, TableId table, std::uint32_t worldSeed, const glm::ivec3& at) {
    noise::Stream stream{streamSeed(worldSeed, at)};
    const Table& row = kTables[static_cast<std::size_t>(table)];
    for (std::size_t p = 0; p < row.poolCount; ++p) {
        const Pool& pool = kPools[static_cast<std::size_t>(row.firstPool) + p];
        const int rolls = pool.rollsMin + stream.range(pool.rollsMax - pool.rollsMin + 1);
        for (int r = 0; r < rolls; ++r) {
            int pick = stream.range(pool.total);
            std::size_t chosen = pool.first;
            for (std::size_t e = 0; e < pool.count; ++e) {
                chosen = static_cast<std::size_t>(pool.first) + e;
                pick -= kEntries[chosen].weight;
                if (pick < 0) {
                    break;
                }
            }
            const Entry& entry = kEntries[chosen];
            const int count = entry.countMin + stream.range(entry.countMax - entry.countMin + 1);
            const int slot = stream.range(static_cast<int>(kChestSlots));
            placeStack(chest, entry.item, count, slot);
        }
    }
}

/// The table an unrolled chest carries, ready to hand to `rollInto`.
///
/// One place converts the block id's plain index into a `TableId`, rather than
/// a cast at each of the three call sites that will need it.
inline TableId tableFor(BlockId lootChest) {
    return static_cast<TableId>(lootChestTable(lootChest));
}

} // namespace game::loot
