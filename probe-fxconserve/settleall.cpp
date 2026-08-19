// Probe for finding 873: a block in mid-flight at save time is deleted.
//
// Drives the REAL rule: `fallingCubeRest` and `applyFallingLanding` are both
// templated in FallingBlock.hpp precisely so a probe can run the same code the
// world does, so nothing here is a second copy that agrees today.
//
// Measures item conservation as   in == world + brokenFallers,  where a broken
// faller is the arm that pays itself as an item and leaves the occupant alone.

#include "world/FallingBlock.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

struct Cell {
    int x, y, z;
    bool operator<(const Cell& other) const {
        return std::tie(x, y, z) < std::tie(other.x, other.y, other.z);
    }
};

// Enough of `World` for `applyFallingLanding`: blockAt, setBlock, primeTnt.
struct FakeWorld {
    std::map<Cell, game::BlockId> cells;
    bool resident = true;

    game::BlockId blockAt(int x, int y, int z) const {
        const auto found = cells.find(Cell{x, y, z});
        return found == cells.end() ? game::BlockId::Air : found->second;
    }
    void setBlock(int x, int y, int z, game::BlockId block) { cells[Cell{x, y, z}] = block; }
    void primeTnt(const glm::ivec3&) {}
    bool columnResident(int, int) const { return resident; }

    int countOf(game::BlockId block) const {
        int n = 0;
        for (const auto& entry : cells) {
            if (entry.second == block) {
                ++n;
            }
        }
        return n;
    }
};

struct Faller {
    glm::vec3 position{0.0f};
    game::BlockId block = game::BlockId::Sand;
};

// `FallingBlocks::settleAll` re-typed over `FakeWorld`. Line for line with
// FallingBlock.cpp, which is what makes the measurement mean anything.
std::vector<game::FallingBlocks::Crushed> settleAll(FakeWorld& world, std::vector<Faller>& blocks) {
    std::vector<game::FallingBlocks::Crushed> crushed;

    for (const Faller& falling : blocks) {
        const int cellX = static_cast<int>(std::floor(falling.position.x));
        const int cellZ = static_cast<int>(std::floor(falling.position.z));
        const int fromCell = static_cast<int>(std::floor(falling.position.y));

        const game::CubeRest rest =
            world.columnResident(cellX, cellZ)
                ? game::fallingCubeRest(fromCell, 0,
                                        [&](int y) { return world.blockAt(cellX, y, cellZ); })
                : game::CubeRest{fromCell, true};

        game::applyFallingLanding(world, {cellX, rest.cell, cellZ}, falling.block,
                                  falling.position.y - static_cast<float>(rest.cell), crushed,
                                  nullptr);
    }

    blocks.clear();
    return crushed;
}

void check(const char* what, bool ok) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

// How many of the crushed entries are the faller paying itself, rather than an
// occupant being displaced. Re-derived from the same predicate the arm chose by.
int brokenFallers(const FakeWorld& world, const std::vector<game::FallingBlocks::Crushed>& crushed,
                  game::BlockId faller) {
    int n = 0;
    for (const game::FallingBlocks::Crushed& hit : crushed) {
        if (hit.block == faller && world.blockAt(hit.position.x, hit.position.y, hit.position.z) != faller) {
            ++n;
        }
    }
    return n;
}

void pillarCollapse() {
    std::printf("A twenty-high sand pillar whose base was knocked out\n");

    // BEFORE: quitting discards the fallers outright, and `World::updateFalls`
    // has already air-written every source cell.
    {
        FakeWorld world;
        world.setBlock(0, 0, 0, game::BlockId::Bedrock);
        std::vector<Faller> flying;
        for (int i = 0; i < 20; ++i) {
            flying.push_back(Faller{glm::vec3{0.5f, 4.0f + static_cast<float>(i), 0.5f}});
        }
        const int in = static_cast<int>(flying.size());
        flying.clear(); // what shutdown does today
        std::printf("  BEFORE  in=%d world=%d items=%d sum=%d\n", in, world.countOf(game::BlockId::Sand),
                    0, world.countOf(game::BlockId::Sand));
        check("today the pillar is annihilated by quitting", world.countOf(game::BlockId::Sand) == 0);
    }

    // AFTER: settleAll lands every one of them into the chunk about to be saved.
    {
        FakeWorld world;
        world.setBlock(0, 0, 0, game::BlockId::Bedrock);
        std::vector<Faller> flying;
        for (int i = 0; i < 20; ++i) {
            flying.push_back(Faller{glm::vec3{0.5f, 4.0f + static_cast<float>(i), 0.5f}});
        }
        const int in = static_cast<int>(flying.size());
        const std::vector<game::FallingBlocks::Crushed> crushed = settleAll(world, flying);
        const int inWorld = world.countOf(game::BlockId::Sand);
        const int items = brokenFallers(world, crushed, game::BlockId::Sand);
        std::printf("  AFTER   in=%d world=%d items=%d sum=%d\n", in, inWorld, items, inWorld + items);
        check("every cube is in the world", inWorld == in);
        check("conserved: in == world + items", in == inWorld + items);
        check("they stack rather than crushing each other", inWorld == 20);
    }
}

void ontoATorch() {
    std::printf("A sand column settling onto a torch (the torch trick)\n");
    FakeWorld world;
    world.setBlock(0, 0, 0, game::BlockId::Bedrock);
    world.setBlock(0, 1, 0, game::BlockId::Torch);
    std::vector<Faller> flying;
    for (int i = 0; i < 5; ++i) {
        flying.push_back(Faller{glm::vec3{0.5f, 6.0f + static_cast<float>(i), 0.5f}});
    }
    const int in = static_cast<int>(flying.size());
    const std::vector<game::FallingBlocks::Crushed> crushed = settleAll(world, flying);
    const int inWorld = world.countOf(game::BlockId::Sand);
    const int items = brokenFallers(world, crushed, game::BlockId::Sand);
    std::printf("  in=%d world=%d items=%d sum=%d\n", in, inWorld, items, inWorld + items);
    check("the torch is still standing", world.blockAt(0, 1, 0) == game::BlockId::Torch);
    check("the bottom cube paid itself as an item", items >= 1);
    check("conserved: in == world + items", in == inWorld + items);
}

void ontoASlab() {
    std::printf("A sand cube settling onto a slab\n");
    FakeWorld world;
    world.setBlock(0, 0, 0, game::BlockId::SlabRunFirst);
    std::vector<Faller> flying{Faller{glm::vec3{0.5f, 6.0f, 0.5f}}};
    const std::vector<game::FallingBlocks::Crushed> crushed = settleAll(world, flying);
    const int inWorld = world.countOf(game::BlockId::Sand);
    const int items = brokenFallers(world, crushed, game::BlockId::Sand);
    std::printf("  in=1 world=%d items=%d sum=%d\n", inWorld, items, inWorld + items);
    check("the slab survives", world.blockAt(0, 0, 0) == game::BlockId::SlabRunFirst);
    check("conserved: 1 == world + items", 1 == inWorld + items);
    check("it became an item, not a floating cube on a step", items == 1);
}

void overAnUnloadedColumn() {
    std::printf("A cube over a column that streamed out\n");
    FakeWorld world;
    world.resident = false;
    std::vector<Faller> flying{Faller{glm::vec3{0.5f, 60.0f, 0.5f}}};
    const std::vector<game::FallingBlocks::Crushed> crushed = settleAll(world, flying);
    const int items = brokenFallers(world, crushed, game::BlockId::Sand);
    std::printf("  in=1 world=%d items=%d sum=%d\n", world.countOf(game::BlockId::Sand), items,
                world.countOf(game::BlockId::Sand) + items);
    check("nothing was written into an absent chunk", world.countOf(game::BlockId::Sand) == 0);
    check("conserved: it went to the item channel", items == 1);
}

void anvilOntoWater() {
    std::printf("An anvil settling into water (the displaced-occupant arm)\n");
    FakeWorld world;
    world.setBlock(0, 0, 0, game::BlockId::Stone);
    world.setBlock(0, 1, 0, game::BlockId::Water0);
    std::vector<Faller> flying{Faller{glm::vec3{0.5f, 6.0f, 0.5f}, game::BlockId::Anvil}};
    const std::vector<game::FallingBlocks::Crushed> crushed = settleAll(world, flying);
    check("the anvil is a block again", world.blockAt(0, 1, 0) == game::BlockId::Anvil);
    check("exactly one entry, and it is the water it displaced",
          crushed.size() == 1 && crushed.front().block == game::BlockId::Water0);
    check("the anvil did not also pay itself",
          brokenFallers(world, crushed, game::BlockId::Anvil) == 0);
}

} // namespace

int main() {
    std::printf("=== 873: settling the fallers before a terminal save ===\n\n");
    pillarCollapse();
    std::printf("\n");
    ontoATorch();
    std::printf("\n");
    ontoASlab();
    std::printf("\n");
    overAnUnloadedColumn();
    std::printf("\n");
    anvilOntoWater();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
