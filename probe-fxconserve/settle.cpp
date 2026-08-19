// fx-conserve 727: mine the dirt under a tall flower and count the payments.
//
// Transcribes Main.cpp's `clearPairedHalf`, `hasItsSupport` and `settleAround`
// (both the old body and the fixed one), World.cpp's `updateSupports` and
// Main.cpp's wash drain, over the REAL Block.hpp predicates and the REAL
// `resolveBreak` drop table. Nothing is guessed: every payment goes through the
// table, so a double payment shows up as two items rather than as a comment.
//
// Throwaway. Delete when the round is over.

#include "item/BlockDrops.hpp"
#include "world/Block.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace game;

namespace {

struct Cell {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator<(const Cell& other) const {
        return std::tie(x, y, z) < std::tie(other.x, other.y, other.z);
    }
    bool operator==(const Cell& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct Paid {
    Cell at;
    BlockId block;
    ItemId item;
    int count;
};

// A one-column world, deep enough for a plant on a floor.
struct TinyWorld {
    std::map<Cell, BlockId> blocks;
    std::deque<Cell> supportUpdates;             // World::m_supportUpdates
    std::vector<std::pair<Cell, BlockId>> washed; // World::m_washedBlocks

    BlockId at(const Cell& cell) const {
        const auto found = blocks.find(cell);
        return found == blocks.end() ? BlockId::Air : found->second;
    }
    bool solid(const Cell& cell) const { return isSolid(at(cell)); }

    // World::setBlock, reduced to the two things this question turns on: the
    // write, and the unconditional support update it schedules.
    void set(const Cell& cell, BlockId block) {
        blocks[cell] = block;
        supportUpdates.push_back(cell);
    }
};

std::vector<Paid> ledger;
std::uint32_t breakNonce = 0;

// Main.cpp's spillBlockDrop, through the real table.
void spillBlockDrop(TinyWorld&, const Cell& at, BlockId block) {
    const ResolvedDrop rolled = resolveBreak(block, BreakContext{}, at.x, at.y, at.z, ++breakNonce);
    for (int i = 0; i < rolled.count; ++i) {
        const ItemStack& stack = rolled.stacks[static_cast<std::size_t>(i)];
        ledger.push_back(Paid{at, block, stack.item, stack.count});
    }
}

// Main.cpp's clearPairedHalf, tall-flower arm only - the doors and beds are
// Model-shaped and never enter this cascade, which is what makes the scope of
// this bug exactly the eight tall-flower halves.
bool clearPairedHalf(TinyWorld& world, const Cell& cell, BlockId removed) {
    if (!isTallFlower(removed)) {
        return false;
    }
    const int step = isTallFlowerUpper(removed) ? -1 : 1;
    const Cell other{cell.x, cell.y + step, cell.z};
    if (world.at(other) != static_cast<BlockId>(static_cast<int>(removed) + step)) {
        return false;
    }
    world.set(other, BlockId::Air);
    return true;
}

bool hasItsSupport(const TinyWorld& world, BlockId block, const Cell& at) {
    if (restsOnWater(block)) {
        return isWaterSource(world.at(Cell{at.x, at.y - 1, at.z}));
    }
    return world.solid(Cell{at.x, at.y - 1, at.z});
}

// Main.cpp's settleAround, the "whatever rested on it comes down" arm.
void settleAround(TinyWorld& world, const Cell& cell, BlockId removed, bool withFix) {
    clearPairedHalf(world, cell, removed);
    const Cell above{cell.x, cell.y + 1, cell.z};
    const BlockId resting = world.at(above);
    if (needsSupportBelow(resting) && !hasItsSupport(world, resting, above)) {
        world.set(above, BlockId::Air);
        spillBlockDrop(world, above, resting);
        if (withFix) {
            clearPairedHalf(world, above, resting);
        }
    }
}

// World::updateSupports: everything due, then the wash drain that pays it.
void drainSupports(TinyWorld& world) {
    // Two passes, because a cell cleared by the first schedules the cell above
    // it and the drains are `kFallDelay` apart in the real game - which is
    // exactly why the wash drain's per-batch `alreadyPaid` list cannot see them
    // together.
    for (int pass = 0; pass < 4; ++pass) {
        std::deque<Cell> due;
        due.swap(world.supportUpdates);
        while (!due.empty()) {
            const Cell p = due.front();
            due.pop_front();
            const Cell above{p.x, p.y + 1, p.z};
            const BlockId resting = world.at(above);
            if (!needsSupportBelow(resting) || hasItsSupport(world, resting, above)) {
                continue;
            }
            world.washed.push_back({above, resting});
            world.set(above, BlockId::Air);
        }

        // Main.cpp's wash drain, `alreadyPaid` and all.
        std::vector<Cell> alreadyPaid;
        std::vector<std::pair<Cell, BlockId>> batch;
        batch.swap(world.washed);
        for (const auto& entry : batch) {
            if (std::find(alreadyPaid.begin(), alreadyPaid.end(), entry.first) !=
                alreadyPaid.end()) {
                continue;
            }
            spillBlockDrop(world, entry.first, entry.second);
            const BlockId nowThere = world.at(entry.first);
            if (nowThere == BlockId::Air || isFluid(nowThere)) {
                const Cell twin{entry.first.x,
                                entry.first.y + (isTallFlowerUpper(entry.second) ? -1 : 1),
                                entry.first.z};
                if (clearPairedHalf(world, entry.first, entry.second)) {
                    alreadyPaid.push_back(twin);
                }
            }
        }
    }
}

int failures = 0;

void report(const char* what, BlockId lower, bool withFix, bool mineTheDirt) {
    ledger.clear();
    breakNonce = 0;

    TinyWorld world;
    const Cell floorCell{0, 10, 0};
    const Cell lowerCell{0, 11, 0};
    const Cell upperCell{0, 12, 0};
    world.blocks[Cell{0, 9, 0}] = BlockId::Stone;
    world.blocks[floorCell] = BlockId::Dirt;
    world.blocks[lowerCell] = lower;
    world.blocks[upperCell] = static_cast<BlockId>(static_cast<int>(lower) + 1);
    world.supportUpdates.clear();

    // The break path: pay the mined block, Air it, settle around it.
    const Cell mined = mineTheDirt ? floorCell : lowerCell;
    const BlockId broken = world.at(mined);
    spillBlockDrop(world, mined, broken);
    world.set(mined, BlockId::Air);
    settleAround(world, mined, broken, withFix);
    drainSupports(world);

    const ItemId flower = itemForBlock(lower);
    int flowers = 0;
    for (const Paid& paid : ledger) {
        if (paid.item == flower) {
            flowers += paid.count;
        }
    }
    const bool ok = flowers == 1;
    if (!ok) {
        ++failures;
    }
    std::printf("  %-46s payments %d, flowers %d  %s\n", what,
                static_cast<int>(ledger.size()), flowers, ok ? "ok" : "*** FAIL");
    // What is left standing, so an orphaned half shows up as well as a spare item.
    std::printf("       left standing: lower %s, upper %s\n",
                world.at(lowerCell) == BlockId::Air ? "gone" : "STILL THERE",
                world.at(upperCell) == BlockId::Air ? "gone" : "STILL THERE");
}

} // namespace

int main() {
    // Every tall flower, both ways round, both bodies.
    const BlockId lowers[]{BlockId::SunflowerLower, BlockId::LilacLower, BlockId::RoseBushLower,
                           BlockId::PeonyLower};

    std::printf("=== OLD settleAround (no clearPairedHalf on the cell above) ===\n");
    report("mine the flower itself", BlockId::SunflowerLower, false, false);
    report("mine the DIRT underneath", BlockId::SunflowerLower, false, true);
    const int oldFailures = failures;
    failures = 0;

    std::printf("\n=== NEW settleAround (twin cleared with the spill) ===\n");
    for (const BlockId lower : lowers) {
        report((std::string{"mine the flower itself: "} + blockName(lower)).c_str(), lower, true,
               false);
        report((std::string{"mine the DIRT underneath: "} + blockName(lower)).c_str(), lower, true,
               true);
    }

    std::printf("\nold failures %d, new failures %d\n", oldFailures, failures);
    return failures == 0 ? 0 : 1;
}
