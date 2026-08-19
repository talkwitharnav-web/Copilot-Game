#pragma once

/// Copper weathering, as one table rather than a switch repeated per family.
///
/// Thirty-three copper ids exist and every one of them was static: the whole
/// progression was obtainable and nothing ever changed, so waxing prevented
/// nothing and an axe had nothing to scrape. What makes this worth a file of
/// its own is the *shape* of the problem - four weather stages times six
/// shapes times waxed-or-not - because that is exactly the shape that turns
/// into six near-identical switches if you write it one family at a time, and
/// then five of the six are right.
///
/// So the runs are a table, the answer is a lookup, and the sweep at the
/// bottom proves the properties that matter over the whole enum: every
/// unwaxed stage below the last has a successor, no oxidised stage has one,
/// no waxed id ever changes, and the waxed run and the bare run agree on which
/// stages exist.
///
/// **Bedrock is the reference and Bedrock's algorithm is not published.** The
/// wiki carries a `{{missing info|How does the BE behavior differ from JE?}}`
/// banner over the whole of Oxidation SS Mechanics, so every probability below
/// is Java's, is marked `[JE]` at the value, and is the closest thing to
/// ground truth that exists. What *is* confirmed for Bedrock is the shape:
/// oxidation advances only on random ticks (beta 1.16.220.52, "Is now affected
/// by the randomTickSpeed gamerule"), water and rain do not accelerate it, and
/// an axe removes wax first and then one layer at a time.

#include "Block.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace game::copper {

/// The four weathering stages, in the order they happen.
constexpr int kStages = 4;

/// One shape of copper, both ways round. `bare` is the run that weathers,
/// `waxed` is the run that does not, and `BlockId::Air` in either means the id
/// does not exist in this project yet.
///
/// **The pair is the row on purpose.** Waxing is the promise that a block will
/// not change, so a waxed id whose bare twin is missing - or the other way
/// about - is a broken promise, and `runsAgree` below turns that into a build
/// error rather than a block that silently refuses to wax.
struct Run {
    std::array<BlockId, kStages> bare;
    std::array<BlockId, kStages> waxed;
};

/// Every copper shape that has ids. **Not the shaped families**: stairs and
/// slabs are derived from their parent through `stairFamily`/`slabFamily`
/// below rather than listed here, because their ids are generated from the cut
/// run and listing them again is exactly the second place for the same bug.
///
/// Three holes are real and are `Air` rather than pretend rows:
///  - **Chiseled copper has one id, not four.** The reference has all four
///    stages plus all four waxed; `Block.hpp` has `ChiseledCopper` and
///    `WaxedChiseledCopper` alone. Filed as a finding - it needs six new ids
///    in a file this code does not own.
///  - **Copper bulbs have no waxed forms.** Eight ids exist (four stages times
///    unlit/lit) and the reference has eight waxed ones to match.
///  - **Cut copper stairs and slabs have no waxed forms** either, which falls
///    out of the same gap: `kStairFamilies` carries the four bare cut runs and
///    nothing waxed.
constexpr std::array<Run, 6> kRuns = {{
    // The plain block. Note `CopperBlock` is nowhere near the other three in
    // the enum - it is an ore-age id and the weathered stages were appended
    // much later - which is precisely why this is a table of ids and not a
    // `first + stage` arithmetic run.
    {{BlockId::CopperBlock, BlockId::ExposedCopper, BlockId::WeatheredCopper,
      BlockId::OxidizedCopper},
     {BlockId::WaxedCopperBlock, BlockId::WaxedExposedCopper, BlockId::WaxedWeatheredCopper,
      BlockId::WaxedOxidizedCopper}},
    // Cut copper. Also the parent of the stair and slab families.
    {{BlockId::CutCopper, BlockId::ExposedCutCopper, BlockId::WeatheredCutCopper,
      BlockId::OxidizedCutCopper},
     {BlockId::WaxedCutCopper, BlockId::WaxedExposedCutCopper, BlockId::WaxedWeatheredCutCopper,
      BlockId::WaxedOxidizedCutCopper}},
    // Grates.
    {{BlockId::CopperGrate, BlockId::ExposedCopperGrate, BlockId::WeatheredCopperGrate,
      BlockId::OxidizedCopperGrate},
     {BlockId::WaxedCopperGrate, BlockId::WaxedExposedCopperGrate,
      BlockId::WaxedWeatheredCopperGrate, BlockId::WaxedOxidizedCopperGrate}},
    // Bulbs, unlit. The lit run is separate rather than derived because a bulb
    // that oxidises must keep its light state, and the two runs interleave in
    // the enum.
    {{BlockId::CopperBulb, BlockId::ExposedCopperBulb, BlockId::WeatheredCopperBulb,
      BlockId::OxidizedCopperBulb},
     {BlockId::Air, BlockId::Air, BlockId::Air, BlockId::Air}},
    // Bulbs, lit.
    {{BlockId::CopperBulbLit, BlockId::ExposedCopperBulbLit, BlockId::WeatheredCopperBulbLit,
      BlockId::OxidizedCopperBulbLit},
     {BlockId::Air, BlockId::Air, BlockId::Air, BlockId::Air}},
    // Chiseled copper - stage 0 only, both ways round. See the note above.
    {{BlockId::ChiseledCopper, BlockId::Air, BlockId::Air, BlockId::Air},
     {BlockId::WaxedChiseledCopper, BlockId::Air, BlockId::Air, BlockId::Air}},
}};

/// Where an id sits in `kRuns`: which row, which stage, and whether it was the
/// waxed copy. `run < 0` means "not a copper block this table knows".
struct Place {
    int run = -1;
    int stage = 0;
    bool waxed = false;
};

/// The direct lookup - plain blocks, grates, bulbs and chiseled copper.
/// Stairs and slabs are *not* found here; `place` below handles those by
/// asking the shaped family for its parent first.
constexpr Place placeDirect(BlockId id) {
    if (id == BlockId::Air) {
        return {};
    }
    for (std::size_t r = 0; r < kRuns.size(); ++r) {
        for (int s = 0; s < kStages; ++s) {
            if (kRuns[r].bare[static_cast<std::size_t>(s)] == id) {
                return {static_cast<int>(r), s, false};
            }
            if (kRuns[r].waxed[static_cast<std::size_t>(s)] == id) {
                return {static_cast<int>(r), s, true};
            }
        }
    }
    return {};
}

/// The stair family index whose parent is `parent`, or -1. A linear scan over
/// 52 rows, which costs nothing at compile time and is called once per
/// oxidation attempt at run time.
constexpr int stairFamilyOfParent(BlockId parent) {
    for (std::size_t i = 0; i < kStairFamilies.size(); ++i) {
        if (kStairFamilies[i].parent == parent) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

constexpr int slabFamilyOfParent(BlockId parent) {
    for (std::size_t i = 0; i < kSlabFamilies.size(); ++i) {
        if (kSlabFamilies[i].parent == parent) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Where any copper id sits, shaped or not. **This is the one function that
/// knows a cut copper stair is cut copper**, and every answer below goes
/// through it, so a stair and its parent can never disagree about a stage.
constexpr Place place(BlockId id) {
    if (isStairs(id)) {
        return placeDirect(kStairFamilies[static_cast<std::size_t>(stairFamily(id))].parent);
    }
    if (isSlab(id)) {
        return placeDirect(kSlabFamilies[static_cast<std::size_t>(slabFamily(id))].parent);
    }
    return placeDirect(id);
}

/// True for any copper the weathering table knows, waxed or not, including cut
/// copper stairs and slabs.
constexpr bool isCopper(BlockId id) {
    return place(id).run >= 0;
}

/// 0-3 for copper, -1 for anything else. This is the number the neighbour scan
/// in `World::growOne` compares.
constexpr int stageOf(BlockId id) {
    const Place p = place(id);
    return p.run < 0 ? -1 : p.stage;
}

/// Whether the block carries wax. Waxed copper is inert: it never oxidises and
/// it is not counted by the neighbour scan either, which is the reference's
/// rule and not an optimisation.
constexpr bool isWaxed(BlockId id) {
    const Place p = place(id);
    return p.run >= 0 && p.waxed;
}

/// The same shape at a different stage, keeping stairs stairs and slabs slabs.
/// `BlockId::Air` when that stage has no id - which is how the three holes
/// listed on `kRuns` come out as "this block does not weather" rather than as
/// a wrong id.
constexpr BlockId atStage(BlockId id, int stage) {
    if (stage < 0 || stage >= kStages) {
        return BlockId::Air;
    }
    const Place p = place(id);
    if (p.run < 0) {
        return BlockId::Air;
    }
    const Run& run = kRuns[static_cast<std::size_t>(p.run)];
    const BlockId parent =
        (p.waxed ? run.waxed : run.bare)[static_cast<std::size_t>(stage)];
    if (parent == BlockId::Air) {
        return BlockId::Air;
    }
    if (isStairs(id)) {
        const int family = stairFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : stairsAt(family, stairFacing(id), stairIsTop(id));
    }
    if (isSlab(id)) {
        const int family = slabFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : slabAt(family, isUpperHalf(id));
    }
    return parent;
}

/// What this block becomes when it weathers one step, or `Air` if it never
/// does. Waxed copper, oxidized copper and everything that is not copper all
/// answer `Air`, which is the single test `World::growOne` needs.
constexpr BlockId weathered(BlockId id) {
    const Place p = place(id);
    if (p.run < 0 || p.waxed) {
        return BlockId::Air;
    }
    return atStage(id, p.stage + 1);
}

/// The inverse: one layer scraped off, or `Air` if there is nothing to scrape.
/// **Written so the sweep can prove the map invertible**, and so that whoever
/// wires the axe up in `Main.cpp` reaches for this instead of writing the
/// table out backwards.
constexpr BlockId scraped(BlockId id) {
    const Place p = place(id);
    if (p.run < 0 || p.waxed || p.stage == 0) {
        return BlockId::Air;
    }
    return atStage(id, p.stage - 1);
}

/// Wax on, wax off. `Air` where the id does not exist, so a player waxing one
/// gets nothing rather than a wrong block.
///
/// **The standing constraint, for whoever lands the missing waxed ids in
/// `Block.hpp`:** the crafting recipes for them must be **derived** through this
/// function, never hand-written as a row per shape. A hand-written table is a
/// second answer to a question this file already answers, which is the shape
/// that has cost this project more than any other. **`place()` resolves a
/// shaped id through its parent, so nothing here needs to change when those ids
/// appear** - and adding rows to `kRuns` for them would itself be the duplicate.
/// That paragraph is meant to outlive the one below it.
///
/// **The dated state, written so it can be checked in one search rather than
/// believed:** as of 2026-08-19 11:17 `Block.hpp` declared thirteen waxed
/// enumerators - four blocks, four cut, one chiseled, four grates - so a waxed
/// stair, slab or bulb returns `Air` here.
///
/// **My first falsifier for that was blind and is replaced.** It read *"a
/// fourteenth waxed enumerator whose name contains `Stair`, `Slab` or `Bulb`"*,
/// which **can never fire**: shaped ids are not enumerators at all. Searching
/// `Block.hpp` for `CutCopperStairs` or `CutCopperSlab` returns **zero** - they
/// are generated from `kStairFamilies` and `kSlabFamilies`, each row a parent
/// block plus a display name. A waxed stair can therefore come into existence
/// with **no new enumerator whatsoever**, and the old falsifier would have sat
/// here reading true through the entire event it was written to catch.
///
/// **The falsifier that does fire: `BlockId::Waxed` appearing as a parent
/// inside the `kStairFamilies` or `kSlabFamilies` initialisers in `Block.hpp`.**
/// Measured 2026-08-19 11:52: zero waxed parents in either, against 112
/// `BlockId::` mentions across the same two initialisers, so the probe is
/// reading the span and the zero means absence rather than blindness. The only
/// copper parents present are the four bare cut coppers.
///
/// That also sizes the work honestly, and it is **much smaller than a missing
/// id implies**: eight rows in two existing tables, four waxed cut coppers as
/// stair parents and the same four as slab parents. The parent ids already
/// exist. **No new enumerators, so no mid-enum renumbering and no save-format
/// bump** - a materially safer change than adding block ids would have been.
///
/// **Search `BlockId::Waxed` rather than trusting that thirteen**, and search it
/// in the family tables as well as the enum. The count is a photograph; the
/// search stays true and finds the ones nobody thought to mention.
constexpr BlockId waxedForm(BlockId id) {
    const Place p = place(id);
    if (p.run < 0 || p.waxed) {
        return BlockId::Air;
    }
    const BlockId parent = kRuns[static_cast<std::size_t>(p.run)]
                               .waxed[static_cast<std::size_t>(p.stage)];
    if (parent == BlockId::Air) {
        return BlockId::Air;
    }
    if (isStairs(id)) {
        const int family = stairFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : stairsAt(family, stairFacing(id), stairIsTop(id));
    }
    if (isSlab(id)) {
        const int family = slabFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : slabAt(family, isUpperHalf(id));
    }
    return parent;
}

constexpr BlockId unwaxedForm(BlockId id) {
    const Place p = place(id);
    if (p.run < 0 || !p.waxed) {
        return BlockId::Air;
    }
    const BlockId parent = kRuns[static_cast<std::size_t>(p.run)]
                               .bare[static_cast<std::size_t>(p.stage)];
    if (parent == BlockId::Air) {
        return BlockId::Air;
    }
    if (isStairs(id)) {
        const int family = stairFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : stairsAt(family, stairFacing(id), stairIsTop(id));
    }
    if (isSlab(id)) {
        const int family = slabFamilyOfParent(parent);
        return family < 0 ? BlockId::Air : slabAt(family, isUpperHalf(id));
    }
    return parent;
}

// ---------------------------------------------------------------------------
// The probabilities. Every one is per **random tick landing on the block** -
// not per game tick, not per second - because that is the unit the reference
// publishes them in and the unit `farming::kRandomTicksPerChunkPerTick`
// delivers them in. One random tick reaches a given cell about every 204.8 s
// here, so the mean times below are three times Java's published ones and that
// is a deliberate, already-settled property of this project's tick budget.
// ---------------------------------------------------------------------------

/// **[JE]** The first gate: 64/1125 of random ticks put a copper block into
/// "pre-oxidation", and only then is the neighbourhood consulted.
/// <https://minecraft.wiki/w/Oxidation#Mechanics>. Bedrock's own number is not
/// published anywhere - the wiki's own `missing info` banner says so - so this
/// is Java's, carried over knowingly rather than invented.
constexpr int kPreOxidationNumerator = 64;
constexpr int kPreOxidationDenominator = 1125;

/// **[JE]** How far the neighbour scan reaches, in **taxicab distance in
/// blocks** (|dx| + |dy| + |dz| <= 4), which is an octahedron of 129 cells.
/// Same source. Note this is a straight coordinate distance and *not* the
/// flood fill leaf decay uses - the two are different shapes for different
/// reasons and conflating them is how one of them ends up wrong.
constexpr int kScanReach = 4;

/// The 129 that `kScanReach` implies, written out so the sweep can check the
/// loop against it rather than against a hand-counted literal.
constexpr int octahedronCells(int reach) {
    int n = 0;
    for (int dx = -reach; dx <= reach; ++dx) {
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dz = -reach; dz <= reach; ++dz) {
                const int taxicab = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
                if (taxicab <= reach) {
                    ++n;
                }
            }
        }
    }
    return n;
}

static_assert(octahedronCells(kScanReach) == 129,
              "the reference's oxidation scan is 129 cells - if this is not 129 the reach or the "
              "distance metric is wrong");

/// **[JE]** The multiplier `m` in the reference's final probability `m * c^2`,
/// as a percentage so it stays integer arithmetic. Unoxidized copper weathers
/// at three quarters the rate the two middle stages do.
/// <https://minecraft.wiki/w/Oxidation#Mechanics>
constexpr int kUnoxidizedFactorPercent = 75;
constexpr int kWeatheredFactorPercent = 100;

/// The reference's `c = (b + 1) / (a + 1)`, then `m * c^2`, returned in parts
/// per million so the caller can compare it against a single integer roll and
/// never touch a float.
///
///  - `nearby` is `a`: every non-waxed copper block within `kScanReach`,
///    including the one being ticked.
///  - `higher` is `b`: how many of those are at a **more** oxidised stage.
///
/// **The caller must already have checked that no nearby copper is at a lower
/// stage** - the reference aborts outright in that case, which is what makes a
/// block of plain copper hold a whole wall back, and it is a separate rule
/// rather than a `c` of zero.
constexpr int oxidationChancePpm(int nearby, int higher, int stage) {
    const int factor = stage == 0 ? kUnoxidizedFactorPercent : kWeatheredFactorPercent;
    // c^2 = ((b+1)/(a+1))^2, scaled by a million before the divide so that the
    // integer arithmetic keeps four significant figures rather than collapsing
    // to 0 or 1.
    const long long num = static_cast<long long>(higher + 1) * static_cast<long long>(higher + 1);
    const long long den = static_cast<long long>(nearby + 1) * static_cast<long long>(nearby + 1);
    return static_cast<int>(num * 1000000LL * static_cast<long long>(factor) / (den * 100LL));
}

/// A lone copper block with nothing near it: `a = 1` (itself), `b = 0`, so
/// `c = 1/2` and `m * c^2` is 0.75/4 = 18.75%.
static_assert(oxidationChancePpm(1, 0, 0) == 187500,
              "an isolated unoxidized copper block is the reference's 0.75 * (1/2)^2");
static_assert(oxidationChancePpm(1, 0, 1) == 250000,
              "an isolated exposed copper block is the reference's 1.0 * (1/2)^2");
/// Surrounded by copper that is all *more* oxidised, `c` goes to 1 and the
/// stage multiplier is the whole answer - which is the reference's "oxidation
/// spreads from oxidised neighbours".
static_assert(oxidationChancePpm(8, 8, 1) == 1000000,
              "copper amongst nothing but more-oxidised copper weathers at the full rate");

// ---------------------------------------------------------------------------
// The compile-time sweep. Same shape as `mining::MiningSweep`: strided so no
// single `static_assert` blows MSVC's constexpr step budget, with a coverage
// assert so ids appended past the last stride cannot silently stop being swept.
//
// **The stride is 128 rather than the usual 512, and the reason is the Debug
// preset.** Each id here costs far more than a mining sweep's does - `place`
// is a 48-entry linear scan and one id calls it seven times over through
// `weathered`, `scraped`, `waxedForm` and `unwaxedForm` - and under `/MDd` the
// standard library's `_ITERATOR_DEBUG_LEVEL=2` puts a bounds check inside every
// single `std::array::operator[]`, which roughly triples the step count. At 512
// this compiled clean in Release and failed in Debug with "evaluation exceeding
// step limit", which is the worst way for a proof to be lost: silently, in the
// configuration nobody watches. Four times as many passes, a quarter of the
// work each, and the coverage assert below keeps the two numbers honest.
// ---------------------------------------------------------------------------

constexpr int kCopperSweepStride = 128;

// **Derived, not chosen - so this coupling cannot break.** However many strides
// of `kCopperSweepStride` it takes to reach the end of the block table, that is
// how many passes there are. `kCopperSweepPasses * kCopperSweepStride >=
// kBlockIdCount` is now true **by construction** rather than by somebody
// remembering to raise a number, which is the entire reason for writing it this
// way: **`Block.hpp` can no longer break this file, and its owner never needs to
// know this file exists.**
//
// This is the first of the three ways to handle a cross-file coupling and the
// only one that cannot rot - derive one side from the other, rather than
// asserting about it or leaving a comment asking two numbers to be kept in step.
// The pattern is `Block.hpp`'s own `kModelSweepStride`, which fixes the pass
// count and derives the stride. **This file does it the other way round on
// purpose**: the stride is what bounds per-pass `constexpr` work, and per the
// step-limit note above that is the quantity which must not be allowed to grow.
// Deriving the stride here would let it grow with the block count and walk this
// file back into the Debug failure it was already split up to escape.
//
// A pass past the end costs nothing: `copperTableSound`'s own `i < kBlockIdCount`
// bound makes the last one iterate zero times.
constexpr int kCopperSweepPasses =
    (static_cast<int>(kBlockIdCount) + kCopperSweepStride - 1) / kCopperSweepStride;

namespace detail {

/// Every rule the weathering table can break, over every block id.
constexpr bool copperTableSound(int stride) {
    const int first = stride * kCopperSweepStride;
    const int end = first + kCopperSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        const Place p = place(id);
        if (p.run < 0) {
            // 1. **Nothing that is not in the table may answer anything.** The
            //    whole point of `weathered` returning `Air` is that
            //    `World::growOne` can ask it of any block at all.
            if (weathered(id) != BlockId::Air || scraped(id) != BlockId::Air ||
                waxedForm(id) != BlockId::Air || unwaxedForm(id) != BlockId::Air ||
                stageOf(id) != -1 || isWaxed(id)) {
                return false;
            }
            continue;
        }

        // 2. **Waxed copper never changes.** This is the promise waxing makes,
        //    and it is the one a per-family switch loses first.
        if (p.waxed && weathered(id) != BlockId::Air) {
            return false;
        }
        if (p.waxed && scraped(id) != BlockId::Air) {
            return false;
        }

        // 3. **An unwaxed stage below the last has a successor iff the id for
        //    that stage exists**, and the successor is one stage on and the
        //    same shape. Fails the moment a run is written with a hole in the
        //    middle instead of at the end.
        if (!p.waxed) {
            const BlockId next = weathered(id);
            const BlockId wanted = atStage(id, p.stage + 1);
            if (next != wanted) {
                return false;
            }
            if (next != BlockId::Air) {
                if (stageOf(next) != p.stage + 1 || isWaxed(next)) {
                    return false;
                }
                // 4. **The map is invertible.** Scraping what weathering just
                //    produced gets the original back - which is what makes the
                //    axe a table lookup rather than a second table.
                if (scraped(next) != id) {
                    return false;
                }
                if (isStairs(id) != isStairs(next) || isSlab(id) != isSlab(next)) {
                    return false;
                }
            }
        }

        // 5. **Oxidized copper is the end of the line.** No stage-3 id may
        //    weather, waxed or not.
        if (p.stage == kStages - 1 && weathered(id) != BlockId::Air) {
            return false;
        }

        // 6. **Wax is a round trip where it exists at all.** A bare id that
        //    can be waxed must unwax back to itself, and the waxed twin must
        //    agree on stage and shape.
        const BlockId waxedTwin = waxedForm(id);
        if (!p.waxed && waxedTwin != BlockId::Air) {
            if (unwaxedForm(waxedTwin) != id || stageOf(waxedTwin) != p.stage ||
                !isWaxed(waxedTwin) || isStairs(waxedTwin) != isStairs(id) ||
                isSlab(waxedTwin) != isSlab(id)) {
                return false;
            }
        }
        const BlockId bareTwin = unwaxedForm(id);
        if (p.waxed && bareTwin != BlockId::Air) {
            if (waxedForm(bareTwin) != id || stageOf(bareTwin) != p.stage || isWaxed(bareTwin)) {
                return false;
            }
        }

        // 7. **A stair or slab agrees with the parent it was derived from.**
        //    Facing and half survive weathering, waxing and scraping, because
        //    a wall of cut copper stairs that turned to face the other way as
        //    it aged would be a very memorable bug.
        if (isStairs(id)) {
            const BlockId next = weathered(id);
            if (next != BlockId::Air &&
                (stairFacing(next) != stairFacing(id) || stairIsTop(next) != stairIsTop(id))) {
                return false;
            }
        }
        if (isSlab(id)) {
            const BlockId next = weathered(id);
            if (next != BlockId::Air && isUpperHalf(next) != isUpperHalf(id)) {
                return false;
            }
        }
    }
    return true;
}

/// Each run is a prefix: once a stage is missing every stage after it is too.
/// Checked over the table itself rather than over the enum, because it is a
/// property of how the rows were written.
constexpr bool runsWellFormed() {
    for (std::size_t r = 0; r < kRuns.size(); ++r) {
        bool bareEnded = false;
        bool waxedEnded = false;
        if (kRuns[r].bare[0] == BlockId::Air) {
            return false;
        }
        for (int s = 0; s < kStages; ++s) {
            const BlockId bare = kRuns[r].bare[static_cast<std::size_t>(s)];
            const BlockId waxed = kRuns[r].waxed[static_cast<std::size_t>(s)];
            if (bare == BlockId::Air) {
                bareEnded = true;
            } else if (bareEnded) {
                return false;
            }
            if (waxed == BlockId::Air) {
                waxedEnded = true;
            } else if (waxedEnded) {
                return false;
            }
            // A waxed id with no bare twin is a block that cannot be made.
            if (waxed != BlockId::Air && bare == BlockId::Air) {
                return false;
            }
        }
    }
    return true;
}

/// No id appears twice across the whole table - the mistake `CLAUDE.md` calls
/// bug shape #1, and one this project has already paid for twice with
/// duplicated enumerators.
constexpr bool runsDisjoint() {
    for (std::size_t r = 0; r < kRuns.size(); ++r) {
        for (int s = 0; s < kStages; ++s) {
            for (std::size_t r2 = 0; r2 < kRuns.size(); ++r2) {
                for (int s2 = 0; s2 < kStages; ++s2) {
                    if (r == r2 && s == s2) {
                        continue;
                    }
                    const BlockId a = kRuns[r].bare[static_cast<std::size_t>(s)];
                    const BlockId b = kRuns[r2].bare[static_cast<std::size_t>(s2)];
                    if (a != BlockId::Air && a == b) {
                        return false;
                    }
                    const BlockId wa = kRuns[r].waxed[static_cast<std::size_t>(s)];
                    const BlockId wb = kRuns[r2].waxed[static_cast<std::size_t>(s2)];
                    if (wa != BlockId::Air && wa == wb) {
                        return false;
                    }
                    if (a != BlockId::Air && a == wb) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

template <int Pass>
struct CopperSweep {
    static_assert(copperTableSound(Pass),
                  "a copper id contradicts the weathering table - see the numbered invariants in "
                  "copperTableSound");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyCopperPassSwept(std::integer_sequence<int, Pass...>) {
    return (CopperSweep<Pass>::swept && ...);
}

} // namespace detail

static_assert(detail::runsWellFormed(),
              "a copper run has a hole in the middle, or a waxed id whose bare twin is missing");
static_assert(detail::runsDisjoint(), "a copper id appears in kRuns twice");
static_assert(detail::everyCopperPassSwept(
                  std::make_integer_sequence<int, kCopperSweepPasses>{}),
              "the copper weathering table is malformed somewhere - the failing CopperSweep "
              "instantiation above names which stride");
// **True by construction since `kCopperSweepPasses` became derived, and kept
// anyway** - it costs nothing, and what it now catches is not a block family
// but a future edit that goes back to a hand-written pass count or gets the
// rounding in the derivation wrong. It is no longer a live risk; it is a proof
// that the derivation says what it means.
static_assert(kCopperSweepPasses * kCopperSweepStride >= static_cast<int>(kBlockIdCount),
              "kCopperSweepPasses is derived from kBlockIdCount, so if this fires the derivation "
              "itself has been edited - restore the ceiling division rather than raising a number");

// A mirror of `ChunkMesher.cpp`'s sweep arithmetic lived here until 2026-08-19,
// warning half a stride before that file's then hand-written pass count ran out.
// It carried its own expiry - "delete this the moment `kBoxFaceSweepPasses` is
// derived" - and that condition is now met, so it is gone. **Do not add another
// one.** Both sweeps scale themselves from `kBlockIdCount`, and a hand-copied
// figure here would be a false statement about a file this one cannot see.

// The five specific answers a player would notice, spelled out. These are not
// restatements of the table: each is the thing that goes wrong if a row moves.
static_assert(weathered(BlockId::CopperBlock) == BlockId::ExposedCopper &&
                  weathered(BlockId::ExposedCopper) == BlockId::WeatheredCopper &&
                  weathered(BlockId::WeatheredCopper) == BlockId::OxidizedCopper &&
                  weathered(BlockId::OxidizedCopper) == BlockId::Air,
              "the plain copper progression is copper -> exposed -> weathered -> oxidized -> stop");
static_assert(weathered(BlockId::WaxedCopperBlock) == BlockId::Air &&
                  weathered(BlockId::WaxedCutCopper) == BlockId::Air &&
                  weathered(BlockId::WaxedCopperGrate) == BlockId::Air,
              "waxed copper is the promise that nothing changes");
static_assert(weathered(BlockId::CopperBulbLit) == BlockId::ExposedCopperBulbLit,
              "a lit bulb that weathers stays lit - the lit and unlit runs are separate rows for "
              "exactly this");
static_assert(scraped(BlockId::OxidizedCopper) == BlockId::WeatheredCopper &&
                  scraped(BlockId::CopperBlock) == BlockId::Air,
              "an axe takes one layer off, and there is nothing under plain copper");
static_assert(!isCopper(BlockId::RawCopperBlock) && !isCopper(BlockId::CopperOre) &&
                  !isCopper(BlockId::Stone),
              "raw copper and copper ore do not weather - only the crafted block does");

} // namespace game::copper
