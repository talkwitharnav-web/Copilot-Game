// Probe for finding 578 - the neighbour-derived rail shape solver.
//
// Transcribes the three lambdas added to Main.cpp (railJoins, railClimbs,
// solveRailShape) over the REAL Block.hpp rail table, on a tiny map-backed
// world, and answers the two questions that matter:
//   1. does each documented reference case come out right, and
//   2. how many of the thirty rail ids can a player actually build?
//
// Throwaway. Delete with the rest of probe-fxconserve.

#include "world/Block.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <tuple>

using game::BlockId;
using game::FaceDirection;

struct Cell {
    int x, y, z;
    bool operator<(const Cell& o) const {
        return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
    }
};

struct World {
    std::map<Cell, BlockId> blocks;
    BlockId at(const Cell& c) const {
        const auto it = blocks.find(c);
        return it == blocks.end() ? BlockId::Air : it->second;
    }
    void set(const Cell& c, BlockId id) { blocks[c] = id; }
};

static Cell stepAlong(FaceDirection d) {
    switch (d) {
    case FaceDirection::PosX:
        return {1, 0, 0};
    case FaceDirection::NegX:
        return {-1, 0, 0};
    case FaceDirection::PosZ:
        return {0, 0, 1};
    case FaceDirection::NegZ:
        return {0, 0, -1};
    default:
        return {0, 0, 0};
    }
}

static bool railJoins(const World& w, const Cell& cell, FaceDirection dir) {
    const Cell s = stepAlong(dir);
    const Cell side{cell.x + s.x, cell.y, cell.z + s.z};
    return game::isRail(w.at(side)) || game::isRail(w.at({side.x, side.y + 1, side.z})) ||
           game::isRail(w.at({side.x, side.y - 1, side.z}));
}

static bool railClimbs(const World& w, const Cell& cell, FaceDirection dir) {
    const Cell s = stepAlong(dir);
    return game::isRail(w.at({cell.x + s.x, cell.y + 1, cell.z + s.z}));
}

static void solveRailShape(World& w, const Cell& cell) {
    const BlockId self = w.at(cell);
    if (!game::isRail(self)) {
        return;
    }
    const int family = game::railFamily(self);
    const bool mayCurve = family == 0;
    const bool east = railJoins(w, cell, FaceDirection::PosX);
    const bool west = railJoins(w, cell, FaceDirection::NegX);
    const bool south = railJoins(w, cell, FaceDirection::PosZ);
    const bool north = railJoins(w, cell, FaceDirection::NegZ);

    constexpr int kFlatNorthSouth = 0;
    constexpr int kFlatEastWest = 1;
    constexpr int kCurveSouthEast = 6;
    constexpr int kCurveSouthWest = 7;
    constexpr int kCurveNorthWest = 8;
    constexpr int kCurveNorthEast = 9;
    int shape = -1;
    if ((north || south) && !east && !west) {
        shape = kFlatNorthSouth;
    }
    if ((east || west) && !north && !south) {
        shape = kFlatEastWest;
    }
    if (mayCurve) {
        if (south && east && !north && !west) {
            shape = kCurveSouthEast;
        }
        if (south && west && !north && !east) {
            shape = kCurveSouthWest;
        }
        if (north && west && !south && !east) {
            shape = kCurveNorthWest;
        }
        if (north && east && !south && !west) {
            shape = kCurveNorthEast;
        }
    }
    if (shape < 0) {
        if (north || south) {
            shape = kFlatNorthSouth;
        }
        if (east || west) {
            shape = kFlatEastWest;
        }
        if (mayCurve) {
            if (west && north) {
                shape = kCurveNorthWest;
            }
            if (east && north) {
                shape = kCurveNorthEast;
            }
            if (west && south) {
                shape = kCurveSouthWest;
            }
            if (south && east) {
                shape = kCurveSouthEast;
            }
        }
    }
    if (shape == kFlatNorthSouth) {
        if (railClimbs(w, cell, FaceDirection::NegZ)) {
            shape = 2 + static_cast<int>(FaceDirection::NegZ);
        }
        if (railClimbs(w, cell, FaceDirection::PosZ)) {
            shape = 2 + static_cast<int>(FaceDirection::PosZ);
        }
    }
    if (shape == kFlatEastWest) {
        if (railClimbs(w, cell, FaceDirection::PosX)) {
            shape = 2 + static_cast<int>(FaceDirection::PosX);
        }
        if (railClimbs(w, cell, FaceDirection::NegX)) {
            shape = 2 + static_cast<int>(FaceDirection::NegX);
        }
    }
    if (shape < 0) {
        shape = game::railShape(self);
    }
    const BlockId want = game::railAt(family, shape, game::railPowered(self));
    if (want != self) {
        w.set(cell, want);
    }
}

static void refreshRailsAround(World& w, const Cell& cell) {
    solveRailShape(w, cell);
    for (int side = 0; side < 4; ++side) {
        const Cell s = stepAlong(static_cast<FaceDirection>(side));
        for (int rise = -1; rise <= 1; ++rise) {
            solveRailShape(w, {cell.x + s.x, cell.y + rise, cell.z + s.z});
        }
    }
}

// What the placement path now does: seed flat north-south (Bedrock's rule for
// an isolated rail), write, then derive.
static void place(World& w, const Cell& cell, int family) {
    w.set(cell, game::railAt(family, 0, false));
    refreshRailsAround(w, cell);
}

static int failures = 0;
static void expect(const char* what, int got, int want) {
    if (got != want) {
        ++failures;
        std::printf("  FAIL %-46s shape %d, wanted %d\n", what, got, want);
    } else {
        std::printf("  ok   %-46s shape %d\n", what, got);
    }
}

int main() {
    std::printf("=== the reference cases ===\n");
    {
        World w;
        place(w, {0, 0, 0}, 0);
        expect("an isolated rail lies north-south (Bedrock)", game::railShape(w.at({0, 0, 0})), 0);
    }
    {
        World w;
        place(w, {0, 0, 0}, 0);
        place(w, {1, 0, 0}, 0);
        expect("a rail laid east of one: the new one", game::railShape(w.at({1, 0, 0})), 1);
        expect("a rail laid east of one: the old one re-orients",
               game::railShape(w.at({0, 0, 0})), 1);
    }
    {
        // A straight east-west run, then a rail to the south of its east end.
        World w;
        place(w, {0, 0, 0}, 0);
        place(w, {1, 0, 0}, 0);
        place(w, {1, 0, 1}, 0);
        expect("the corner cell curves south-west", game::railShape(w.at({1, 0, 0})), 7);
        expect("the new south leg is north-south", game::railShape(w.at({1, 0, 1})), 0);
    }
    {
        // A rail one cell up and to the east: the lower one ramps toward it.
        World w;
        w.set({1, 1, 0}, game::railAt(0, 0, false));
        place(w, {0, 0, 0}, 0);
        expect("ramps east toward a rail one cell up",
               game::railShape(w.at({0, 0, 0})), 2 + static_cast<int>(FaceDirection::PosX));
    }
    {
        World w;
        w.set({0, 1, 1}, game::railAt(0, 0, false));
        place(w, {0, 0, 0}, 0);
        expect("ramps south toward a rail one cell up",
               game::railShape(w.at({0, 0, 0})), 2 + static_cast<int>(FaceDirection::PosZ));
    }
    {
        // The lower of the pair is the ramp; the upper stays flat.
        World w;
        place(w, {0, 0, 0}, 0);
        w.set({1, 1, 0}, game::railAt(0, 0, false));
        refreshRailsAround(w, {1, 1, 0});
        expect("the upper of a ramped pair stays flat", game::railShape(w.at({1, 1, 0})), 1);
    }
    {
        // T-junction: north, south and east legs, middle laid last.
        World w;
        w.set({0, 0, -1}, game::railAt(0, 0, false));
        w.set({0, 0, 1}, game::railAt(0, 0, false));
        w.set({1, 0, 0}, game::railAt(0, 0, false));
        place(w, {0, 0, 0}, 0);
        expect("a T-junction resolves to a curve", game::railShape(w.at({0, 0, 0})), 6);
    }
    {
        // Four-way: "the center rail always curves south-to-east".
        World w;
        w.set({0, 0, -1}, game::railAt(0, 0, false));
        w.set({0, 0, 1}, game::railAt(0, 0, false));
        w.set({1, 0, 0}, game::railAt(0, 0, false));
        w.set({-1, 0, 0}, game::railAt(0, 0, false));
        place(w, {0, 0, 0}, 0);
        expect("a four-way junction curves south-to-east", game::railShape(w.at({0, 0, 0})), 6);
    }
    for (int family = 1; family <= 3; ++family) {
        World w;
        place(w, {0, 0, 0}, family);
        place(w, {1, 0, 0}, family);
        place(w, {1, 0, 1}, family);
        const BlockId corner = w.at({1, 0, 0});
        if (game::railShape(corner) >= 6) {
            ++failures;
            std::printf("  FAIL family %d curved and must not\n", family);
        } else {
            std::printf("  ok   family %d never curves                       shape %d\n", family,
                        game::railShape(corner));
        }
    }
    {
        // Break the middle of a run and the survivors must re-derive.
        World w;
        place(w, {0, 0, 0}, 0);
        place(w, {1, 0, 0}, 0);
        place(w, {2, 0, 0}, 0);
        w.set({1, 0, 0}, BlockId::Air);
        refreshRailsAround(w, {1, 0, 0});
        // With nothing left adjacent the reference keeps the shape it had,
        // which is what leaves a broken run pointing the way it ran.
        expect("a rail orphaned by a break keeps its axis",
               game::railShape(w.at({0, 0, 0})), 1);
    }

    // ---------------------------------------------------------------------
    // Reachability. Brute-force every arrangement of the four sides at three
    // heights and record which ids the solver can produce.
    std::printf("\n=== how many rail ids can a player build? ===\n");
    for (int family = 0; family <= 3; ++family) {
        std::set<int> beforeFix;
        std::set<int> afterFix;
        // Before: placement emitted flat straight only, chosen by facing.
        beforeFix.insert(0);
        beforeFix.insert(1);
        // After: every arrangement of neighbours at three heights.
        for (int mask = 0; mask < (1 << 12); ++mask) {
            World w;
            for (int side = 0; side < 4; ++side) {
                const Cell s = stepAlong(static_cast<FaceDirection>(side));
                for (int rise = -1; rise <= 1; ++rise) {
                    if (mask & (1 << (side * 3 + (rise + 1)))) {
                        w.set({s.x, rise, s.z}, game::railAt(0, 0, false));
                    }
                }
            }
            w.set({0, 0, 0}, game::railAt(family, 0, false));
            solveRailShape(w, {0, 0, 0});
            afterFix.insert(game::railShape(w.at({0, 0, 0})));
        }
        const int width = family == 0 ? 10 : 6;
        std::printf("  family %d: %2d of %2d shapes reachable before, %2d of %2d after\n", family,
                    static_cast<int>(beforeFix.size()), width,
                    static_cast<int>(afterFix.size()), width);
        for (int shape = 0; shape < width; ++shape) {
            if (afterFix.count(shape) == 0) {
                std::printf("      shape %d STILL UNREACHABLE\n", shape);
                ++failures;
            }
        }
    }

    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
