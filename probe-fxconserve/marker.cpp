// THE HOLLOW-FIX CHECK for finding 871/1262.
//
// My cure for the loot-chest duplication is a `rolledLoot` set whose entries are
// persisted as EMPTY `PlacedChest` records, because a fully looted chest - the
// ordinary way a chest ends up - leaves no ordinary record at all. The whole fix
// rests on one unproven assumption:
//
//     an EMPTY PlacedChest survives a real saveChests -> loadChests round trip
//
// My earlier probe modelled the filter and never drove `WorldStore`. If the
// store drops empty records, or drops the file, the marker evaporates and the
// fix closes two cases in three - which is exactly the correction filed against
// it. So this drives the REAL WorldStore on real bytes.

#include "world/WorldStore.hpp"
#include "world/Loot.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("  %-66s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

struct CellHash {
    std::size_t operator()(const glm::ivec3& v) const noexcept {
        return static_cast<std::size_t>(v.x * 73856093 ^ v.y * 19349663 ^ v.z * 83492791);
    }
};

int countItems(const game::Chest& chest) {
    int total = 0;
    for (std::size_t i = 0; i < chest.slots.size(); ++i) {
        total += chest.slots[i].count;
    }
    return total;
}

} // namespace

int main() {
    const std::filesystem::path root = "probe-loot-world";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    std::printf("=== 871: does the rolled marker actually survive the disk? ===\n\n");

    constexpr std::uint32_t kSeed = 1234567u;
    constexpr game::loot::TableId kTable = game::loot::TableId::VillageWeaponsmith;
    const glm::ivec3 lootedCell{40, 63, -17};   // emptied by the player
    const glm::ivec3 ordinaryCell{8, 70, 8};    // a normal chest with things in it

    // ---- what Main.cpp's save filter now writes ----
    {
        game::WorldStore store(root, kSeed);
        std::vector<game::PlacedChest> saved;

        game::PlacedChest ordinary;
        ordinary.position = ordinaryCell;
        ordinary.chest.slots[0] = game::ItemStack{game::ItemId::Diamond, 7};
        saved.push_back(ordinary);

        // The marker: an EMPTY record for a cell that has already been rolled.
        game::PlacedChest marker;
        marker.position = lootedCell;
        saved.push_back(marker);

        check("the save reports success", store.saveChests(saved));
    }

    // ---- what the next launch reads ----
    std::unordered_set<glm::ivec3, CellHash> rolledLoot;
    std::size_t loadedCount = 0;
    bool markerBack = false;
    bool ordinaryBack = false;
    {
        game::WorldStore store(root, kSeed);
        const std::vector<game::PlacedChest> loaded = store.loadChests();
        loadedCount = loaded.size();
        for (const game::PlacedChest& placed : loaded) {
            if (countItems(placed.chest) == 0) {
                rolledLoot.insert(placed.position);   // Main.cpp's seeding rule
                if (placed.position == lootedCell) {
                    markerBack = true;
                }
            }
            if (placed.position == ordinaryCell && countItems(placed.chest) == 7) {
                ordinaryBack = true;
            }
        }
    }

    std::printf("  records written 2, records read back %zu\n", loadedCount);
    check("BOTH records survive - the empty one is not filtered", loadedCount == 2);
    check("the ordinary chest comes back with its diamonds", ordinaryBack);
    check("THE MARKER SURVIVES the round trip", markerBack);
    check("and it seeds rolledLoot on the next launch", rolledLoot.count(lootedCell) != 0);

    // ---- and now the thing the marker exists to prevent ----
    // Main.cpp: firstTouch = chests.find(at) == end && rolledLoot.count(at) == 0
    const bool chestPresent = false;  // fully looted, so no live entry
    const bool firstTouch = !chestPresent && rolledLoot.count(lootedCell) == 0;
    check("firstTouch is FALSE, so materialise does not re-roll", !firstTouch);

    game::Chest wouldGet;
    if (firstTouch) {
        game::loot::rollInto(wouldGet, kTable, kSeed, lootedCell);
    }
    std::printf("\n  items handed to the player on the next launch: %d\n", countItems(wouldGet));
    check("a looted loot chest pays nothing on reload", countItems(wouldGet) == 0);

    std::printf("\nCONTROLS - a zero from a test that cannot produce a one is not evidence\n");

    // 1. The roll must be capable of paying, or "0" above proves nothing.
    game::Chest fresh;
    game::loot::rollInto(fresh, kTable, kSeed, lootedCell);
    std::printf("  an unrolled chest at that cell pays: %d\n", countItems(fresh));
    check("the roll can actually pay out", countItems(fresh) > 0);

    // 2. WITHOUT the marker - the fix's own counterfactual, which is the case
    //    the correction says a record-presence test misses.
    std::unordered_set<glm::ivec3, CellHash> noMarker;
    const bool firstTouchNoMarker = !chestPresent && noMarker.count(lootedCell) == 0;
    game::Chest duplicated;
    if (firstTouchNoMarker) {
        game::loot::rollInto(duplicated, kTable, kSeed, lootedCell);
    }
    std::printf("  without the marker the same cell pays: %d\n", countItems(duplicated));
    check("the counterfactual duplicates, so the marker is load-bearing",
          countItems(duplicated) > 0);

    // 3. A marker-only save must not delete the file - saveChests({}) removes it,
    //    and a world where every chest is a looted loot chest hits exactly that.
    {
        game::WorldStore store(root, kSeed);
        std::vector<game::PlacedChest> onlyMarkers;
        game::PlacedChest marker;
        marker.position = lootedCell;
        onlyMarkers.push_back(marker);
        store.saveChests(onlyMarkers);
    }
    {
        game::WorldStore store(root, kSeed);
        const std::vector<game::PlacedChest> loaded = store.loadChests();
        std::printf("  a save containing ONLY markers reads back: %zu\n", loaded.size());
        check("a markers-only table is not mistaken for an empty one", loaded.size() == 1);
    }

    // 4. And the genuinely empty case must still delete, or stale chests return.
    {
        game::WorldStore store(root, kSeed);
        store.saveChests({});
    }
    {
        game::WorldStore store(root, kSeed);
        check("an actually empty table still clears the file",
              store.loadChests().empty());
    }

    std::filesystem::remove_all(root, ec);
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
