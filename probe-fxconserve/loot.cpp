// Finding 1262 / 871 - a village loot chest refills itself across a
// kChunkFormatVersion bump.
//
// Mechanism, and it is two halves of one fact living in two stores with
// different lifetimes: "this chest has been rolled" is recorded ONLY in the
// block id (materialise overwrites the LootChest marker with plainChestFor),
// which rides out with the chunk - and a format bump discards every modified
// chunk and regenerates it, so the marker comes back. chests.dat carries its
// own version and survives. The next open therefore sees an unrolled marker
// standing over a live record, and loot::rollInto places on top of whatever is
// already there rather than clearing.
//
// This probe runs the real game::Chest, the real loot::rollInto and the real
// isLootChest / plainChestFor / isChest, models chests.dat through Main.cpp's
// actual save filter, and counts items.

#include "world/Block.hpp"
#include "world/Chest.hpp"
#include "world/Loot.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

int itemsIn(const game::Chest& chest) {
    int total = 0;
    for (const game::ItemStack& slot : chest.slots) {
        total += slot.empty() ? 0 : slot.count;
    }
    return total;
}

/// A cell ordering, so the model stays deterministic without borrowing
/// Main.cpp's hash.
struct ByCell {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    }
};

const glm::ivec3 kCell{112, 68, -240};

game::BlockId aVillageChest() { return game::BlockId::LootChestRunFirst; }

/// The whole world this question needs: one cell's block, the chests map, the
/// set of cells Main.cpp now knows have been rolled, and chests.dat.
struct Model {
    bool fixed = false; // false = the code as it stands, true = the fix
    game::BlockId block = game::BlockId::Air;
    std::map<glm::ivec3, game::Chest, ByCell> chests;
    std::set<glm::ivec3, ByCell> rolledLoot;
    std::vector<std::pair<glm::ivec3, game::Chest>> onDisk;
    std::uint32_t seed = 1337u;

    /// Main.cpp's `materialise`, both ways.
    game::Chest& open(const glm::ivec3& at) {
        // Both terms earn their place and for different histories: the flag
        // catches a chest emptied or broken, and the map catches a save written
        // before the flag existed, which has a record but no flag.
        const bool firstTouch = chests.find(at) == chests.end() && rolledLoot.count(at) == 0;
        game::Chest& chest = chests[at];
        if (game::isLootChest(block)) {
            if (!fixed || firstTouch) {
                game::loot::rollInto(chest, game::loot::tableFor(block), seed, at);
            }
            if (fixed) {
                rolledLoot.insert(at);
            }
            block = game::plainChestFor(block);
        }
        return chest;
    }

    /// Main.cpp's break path: spill the contents and erase the entry.
    void breakIt(const glm::ivec3& at) {
        open(at); // the break path materialises first, while the marker stands
        chests.erase(at);
        block = game::BlockId::Air;
    }

    /// Main.cpp's chest filter in `saveEverything`. `stillHasItsBlock` is
    /// modelled as "the cell still holds a chest-like block", which is what it
    /// tests; `isChest(LootChest)` is true and `Block.hpp` asserts it.
    void save() {
        onDisk.clear();
        for (const auto& entry : chests) {
            const bool keep = !entry.second.empty() && game::isChest(block);
            if (keep) {
                onDisk.emplace_back(entry.first, entry.second);
            } else if (fixed && rolledLoot.count(entry.first) != 0) {
                onDisk.emplace_back(entry.first, game::Chest{});
            }
        }
        if (!fixed) {
            return;
        }
        // The rolled cells with no entry left at all, which is what breaking a
        // loot chest leaves behind. No `stillHasItsBlock` test - the cell whose
        // block is gone is the case this exists for.
        for (const glm::ivec3& cell : rolledLoot) {
            if (chests.find(cell) == chests.end()) {
                onDisk.emplace_back(cell, game::Chest{});
            }
        }
    }

    /// A `kChunkFormatVersion` bump: every modified chunk is discarded and
    /// regenerated, so the village puts its `LootChest` marker back, while
    /// `chests.dat` carries its own version and is read as it stands.
    void formatBumpAndReload() {
        save();
        chests.clear();
        rolledLoot.clear();
        for (const auto& entry : onDisk) {
            chests.emplace(entry.first, entry.second);
            // Main.cpp seeds the set from the empty records, because that is
            // the only reason an empty one is ever written.
            if (fixed && entry.second.empty()) {
                rolledLoot.insert(entry.first);
            }
        }
        block = aVillageChest();
    }
};

enum class Took { Nothing, Half, Everything };

/// How much this cell's table actually rolls, so nothing below is guessed.
int oneRoll() {
    game::Chest fresh{};
    game::loot::rollInto(fresh, game::loot::tableFor(aVillageChest()), 1337u, kCell);
    return itemsIn(fresh);
}

int runOnce(bool fixed, Took took, int bumps, int* keptOut) {
    Model m;
    m.fixed = fixed;
    m.block = aVillageChest();

    game::Chest& first = m.open(kCell);
    if (took == Took::Everything) {
        first = game::Chest{};
    } else if (took == Took::Half) {
        for (std::size_t i = 0; i < first.slots.size() / 2; ++i) {
            first.slots[i] = {};
        }
    }
    if (keptOut != nullptr) {
        *keptOut = itemsIn(first);
    }

    for (int i = 0; i < bumps; ++i) {
        m.formatBumpAndReload();
        m.open(kCell);
    }
    return itemsIn(m.chests[kCell]);
}

const char* tookName(Took took) {
    return took == Took::Nothing ? "took nothing"
           : took == Took::Half  ? "took half"
                                 : "emptied it";
}

void scenario(Took took, int bumps) {
    int kept = 0;
    const int today = runOnce(false, took, bumps, &kept);
    const int fixed = runOnce(true, took, bumps, nullptr);

    char label[96];
    std::snprintf(label, sizeof(label), "%s, %d bump%s", tookName(took), bumps,
                  bumps == 1 ? "" : "s");
    std::printf("  %-24s left %2d in the chest -> today %2d, fixed %2d\n", label, kept, today,
                fixed);

    check(std::string{label} + " - the fix hands back exactly what was left", fixed == kept);
    if (bumps > 0) {
        check(std::string{label} + " - and today does not", today != kept);
    }
}

void theBugAndTheFix() {
    std::printf("A village chest opened, then a format bump, then opened again\n");
    scenario(Took::Nothing, 1);
    scenario(Took::Half, 1);
    scenario(Took::Everything, 1);
    std::printf("\nAnd it compounds, because every bump rolls again\n");
    scenario(Took::Nothing, 3);
    scenario(Took::Everything, 10);
}

void theFirstRollStillHappens() {
    std::printf("\nCONTROL: the fix must not stop a chest being rolled in the first place\n");

    const int roll = oneRoll();
    std::printf("  one roll of this table is %d items\n", roll);
    check("the table rolls something at all, or nothing below means anything", roll > 0);

    // Never touched, no bump: the ordinary case, and it must still pay.
    Model m;
    m.fixed = true;
    m.block = aVillageChest();
    const int got = itemsIn(m.open(kCell));
    std::printf("  a fresh chest opened for the first time: %d items\n", got);
    check("CONTROL - a first open still rolls the table", got == roll);
    check("CONTROL - and the marker is spent, so a second open adds nothing",
          itemsIn(m.open(kCell)) == roll);

    // Never touched, then a bump before anyone opens it: no record was written,
    // so it must still roll when the player finally arrives.
    Model n;
    n.fixed = true;
    n.block = aVillageChest();
    n.formatBumpAndReload();
    const int late = itemsIn(n.open(kCell));
    std::printf("  a chest nobody opened before the bump:   %d items\n", late);
    check("CONTROL - an untouched chest is not wrongly marked as rolled", late == roll);
    check("CONTROL - and nothing was written for it", n.onDisk.empty());
}

void theEmptyRecordIsWhatCarriesTheFlag() {
    std::printf("\nWhy the empty record has to be written\n");

    // The fix with the save filter left as it is: the emptied chest writes no
    // record, so the flag is lost and it refills. This is the "correct in 2 of
    // 3 cases" the finding warns about, measured.
    Model half;
    half.fixed = true;
    half.block = aVillageChest();
    game::Chest& c = half.open(kCell);
    c = game::Chest{};
    // Save the way today's filter does - drop the empty record.
    half.onDisk.clear();
    half.chests.clear();
    half.rolledLoot.clear();
    half.block = aVillageChest();
    const int refilled = itemsIn(half.open(kCell));
    std::printf("  guard alone, without the empty record: %d items back in an emptied chest\n",
                refilled);
    check("a materialise guard ALONE leaves the common case open", refilled == oneRoll());

    const int both = runOnce(true, Took::Everything, 1, nullptr);
    std::printf("  guard plus the empty record:           %d\n", both);
    check("the two together close it", both == 0);
}

void theBrokenChestIsTheOtherDoor() {
    std::printf("\nThe other door: break the chest instead of emptying it\n");
    for (int variant = 0; variant <= 1; ++variant) {
        Model m;
        m.fixed = variant != 0;
        m.block = aVillageChest();
        m.breakIt(kCell); // the contents spill as items, which is conserved
        m.formatBumpAndReload();
        const int again = itemsIn(m.open(kCell));
        std::printf("  %-5s a broken loot chest, one bump, reopened: %2d items\n",
                    m.fixed ? "fixed" : "today", again);
        if (m.fixed) {
            check("a broken loot chest stays broken across a bump", again == 0);
        } else {
            check("CONTROL - today it pays a second time", again == oneRoll());
        }
    }
}

} // namespace

int main() {
    std::printf("=== 1262: a village loot chest across a format bump ===\n\n");
    theBugAndTheFix();
    theBrokenChestIsTheOtherDoor();
    theFirstRollStillHappens();
    theEmptyRecordIsWhatCarriesTheFlag();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
