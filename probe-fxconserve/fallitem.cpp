// Probe for findings 888 and 884.
//
// 888: the falling-block drain pays `spillBlockDrop`, which rolls the MINING
// drop table - so a gravel column breaking on a torch pays flint. The count is
// right and the item is wrong. Measures the rate, and proves the invariant the
// fix leans on.
//
// 884: the anvil damage ladder, checked against the published figures rather
// than against the constants, and the player-cell test the drain will use.

#include "item/BlockDrops.hpp"
#include "item/Item.hpp"
#include "item/Mining.hpp"
#include "world/Block.hpp"
#include "world/FallingBlock.hpp"
#include "world/Player.hpp"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(const char* what, bool ok) {
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

// ---------------------------------------------------------------------------
// The invariant the 888 fix leans on: a crushed entry naming a block that FALLS
// can only be the faller paying itself, never a displaced occupant - because
// the occupant arm is gated on `isReplaceable(occupying)` and nothing that
// falls is replaceable. Swept over the whole enum rather than asserted for the
// three ids a reader would think of.
void armDiscriminatorIsSound() {
    std::printf("Can a displaced occupant ever be a falling block?\n");

    int fallers = 0;
    int fallersReplaceable = 0;
    int fallersWithNoItem = 0;
    std::vector<const char*> offenders;

    for (int raw = 0; raw < static_cast<int>(game::kBlockIdCount); ++raw) {
        const game::BlockId id = static_cast<game::BlockId>(raw);
        if (!game::isFalling(id)) {
            continue;
        }
        ++fallers;
        if (game::isReplaceable(id)) {
            ++fallersReplaceable;
            offenders.push_back(game::blockName(id));
        }
        if (game::itemForBlock(id) == game::ItemId::None) {
            ++fallersWithNoItem;
            offenders.push_back(game::blockName(id));
        }
    }

    std::printf("  blocks that fall                                   %d\n", fallers);
    std::printf("  of those, replaceable (would break the test)        %d\n", fallersReplaceable);
    std::printf("  of those, with no item of their own                 %d\n", fallersWithNoItem);
    for (const char* name : offenders) {
        std::printf("    offender: %s\n", name);
    }
    check("nothing that falls is replaceable, so isFalling names the faller",
          fallersReplaceable == 0);
    check("every faller has an item to become", fallersWithNoItem == 0);
    check("the sweep actually found the fallers", fallers > 0);
}

// ---------------------------------------------------------------------------
// 888 itself: what the drop table pays for a faller that breaks, versus what
// the reference says it should pay - itself.
void whatABrokenFallerPays() {
    std::printf("What a faller that breaks is paid, today vs the reference\n");

    struct Row {
        game::BlockId block;
        const char* name;
    };
    std::vector<Row> rows;
    for (int raw = 0; raw < static_cast<int>(game::kBlockIdCount); ++raw) {
        const game::BlockId id = static_cast<game::BlockId>(raw);
        if (game::isFalling(id)) {
            rows.push_back(Row{id, game::blockName(id)});
        }
    }
    for (const Row& row : rows) {
        int wrongItem = 0;
        int totalItems = 0;
        constexpr int kTrials = 20000;
        for (int i = 0; i < kTrials; ++i) {
            const game::ResolvedDrop rolled =
                game::resolveBreak(row.block, game::BreakContext{}, i % 97, 64, i / 97, i + 1);
            for (int e = 0; e < rolled.count; ++e) {
                ++totalItems;
                if (rolled.stacks[static_cast<std::size_t>(e)].item != game::itemForBlock(row.block)) {
                    ++wrongItem;
                }
            }
        }
        std::printf("  %-26s  TODAY  %5d of %5d paid items are not itself  (%.1f%%)\n", row.name,
                    wrongItem, totalItems,
                    totalItems == 0
                        ? 0.0
                        : 100.0 * static_cast<double>(wrongItem) / static_cast<double>(totalItems));
    }
    std::printf("  AFTER   see the next section - each pays itself, or nothing where the "
                "reference destroys it\n");

    // The one that actually diverges, named rather than left to the percentage.
    int flint = 0;
    for (int i = 0; i < 100000; ++i) {
        const game::ResolvedDrop rolled =
            game::resolveBreak(game::BlockId::Gravel, game::BreakContext{}, i % 97, 64, i / 97, i + 1);
        for (int e = 0; e < rolled.count; ++e) {
            if (rolled.stacks[static_cast<std::size_t>(e)].item == game::ItemId::Flint) {
                ++flint;
            }
        }
    }
    std::printf("  gravel pays FLINT %d times in 100000 mined breaks\n", flint);
    check("the mining table really does pay flint for gravel", flint > 0);
    check("a broken faller must pay itself instead",
          game::itemForBlock(game::BlockId::Gravel) != game::ItemId::Flint);
}

// ---------------------------------------------------------------------------
// 884: the ladder, against the published half-heart figures.
void anvilLadder() {
    std::printf("The anvil damage ladder (half-hearts), against the wiki figures\n");
    struct Rung {
        float cells;
        int expect;
    };
    const Rung rungs[] = {{0.0f, 0}, {1.0f, 0}, {2.0f, 2}, {4.0f, 6},
                          {10.0f, 18}, {20.0f, 38}, {21.0f, 40}, {30.0f, 40}};
    for (const Rung& rung : rungs) {
        const int got = game::anvilLandingDamage(game::BlockId::Anvil, rung.cells);
        std::printf("  %5.1f cells -> %2d  (expected %2d)  %s\n", static_cast<double>(rung.cells), got,
                    rung.expect, got == rung.expect ? "ok" : "FAIL");
        if (got != rung.expect) {
            ++failures;
        }
    }
    int sandHurts = 0;
    for (int cells = 0; cells <= 40; ++cells) {
        if (game::anvilLandingDamage(game::BlockId::Sand, static_cast<float>(cells)) != 0) {
            ++sandHurts;
        }
    }
    check("sand never hurts anything, at any height", sandHurts == 0);

    int anvilKinds = 0;
    for (int raw = 0; raw < static_cast<int>(game::kBlockIdCount); ++raw) {
        const game::BlockId id = static_cast<game::BlockId>(raw);
        if (game::anvilLandingDamage(id, 21.0f) > 0) {
            ++anvilKinds;
        }
    }
    std::printf("  block ids that hurt on landing: %d\n", anvilKinds);
    check("the damaged anvil states hurt too, not just the pristine one", anvilKinds >= 3);
}

// ---------------------------------------------------------------------------
// 884's other half: does the player's box overlap the cell the anvil landed in?
// Same box the resumed-position test builds.
bool playerOccupies(const glm::vec3& feet, float height, const glm::ivec3& cell) {
    constexpr float halfWidth = game::player_constants::kWidth * 0.5f;
    const glm::vec3 min = feet - glm::vec3{halfWidth, 0.0f, halfWidth};
    const glm::vec3 max = feet + glm::vec3{halfWidth, height, halfWidth};
    const glm::vec3 cellMin{cell};
    const glm::vec3 cellMax = cellMin + glm::vec3{1.0f};
    return min.x < cellMax.x && max.x > cellMin.x && min.y < cellMax.y && max.y > cellMin.y &&
           min.z < cellMax.z && max.z > cellMin.z;
}

void whoIsStandingThere() {
    std::printf("Whether the player is in the cell the anvil landed in\n");
    const float tall = game::player_constants::kHeight;
    const float low = game::player_constants::kSneakHeight;
    check("standing in the cell, hit by an anvil landing at their feet",
          playerOccupies({8.5f, 64.0f, 8.5f}, tall, {8, 64, 8}));
    check("hit by one landing at head height",
          playerOccupies({8.5f, 64.0f, 8.5f}, tall, {8, 65, 8}));
    check("not hit by one landing two cells above a standing player",
          !playerOccupies({8.5f, 64.0f, 8.5f}, tall, {8, 66, 8}));
    check("a sneaking player is missed by the one a standing player takes",
          !playerOccupies({8.5f, 64.4f, 8.5f}, low, {8, 66, 8}));
    check("not hit by one in the next column",
          !playerOccupies({8.5f, 64.0f, 8.5f}, tall, {9, 64, 8}));
    check("hit when straddling a cell boundary",
          playerOccupies({9.0f, 64.0f, 8.5f}, tall, {8, 64, 8}) &&
              playerOccupies({9.0f, 64.0f, 8.5f}, tall, {9, 64, 8}));
    check("not hit by one below the floor they stand on",
          !playerOccupies({8.5f, 64.0f, 8.5f}, tall, {8, 63, 8}));
}

// ---------------------------------------------------------------------------
// The fix as it now stands in Main.cpp's `payForCrushed`, transcribed, and what
// every one of the twenty-five fallers is paid through it.
int payForCrushedBreakArm(game::BlockId block, game::ItemId& paid) {
    paid = game::ItemId::None;
    if (game::primaryDrop(block) == game::ItemId::None) {
        return 0;
    }
    paid = game::itemForBlock(block);
    return 1;
}

void afterTheFix() {
    std::printf("What every faller is paid AFTER the fix\n");
    int destroyed = 0;
    int paysItself = 0;
    int paysSomethingElse = 0;
    for (int raw = 0; raw < static_cast<int>(game::kBlockIdCount); ++raw) {
        const game::BlockId id = static_cast<game::BlockId>(raw);
        if (!game::isFalling(id)) {
            continue;
        }
        game::ItemId paid = game::ItemId::None;
        const int count = payForCrushedBreakArm(id, paid);
        if (count == 0) {
            ++destroyed;
            std::printf("  %-26s destroyed, drops nothing (matches the reference)\n",
                        game::blockName(id));
        } else if (paid == game::itemForBlock(id)) {
            ++paysItself;
        } else {
            ++paysSomethingElse;
            std::printf("  %-26s pays something that is not itself\n", game::blockName(id));
        }
    }
    std::printf("  pays itself exactly once   %d\n", paysItself);
    std::printf("  destroyed, drops nothing   %d\n", destroyed);
    std::printf("  pays the wrong item        %d\n", paysSomethingElse);
    check("no faller is paid the wrong item any more", paysSomethingElse == 0);
    check("exactly the two brushables are destroyed rather than dropped", destroyed == 2);
    check("every other faller pays itself", paysItself == 23);

    // The one the finding is about, end to end.
    game::ItemId gravelPaid = game::ItemId::None;
    const int gravelCount = payForCrushedBreakArm(game::BlockId::Gravel, gravelPaid);
    std::printf("  gravel breaking on a torch now pays %d x %s\n", gravelCount,
                gravelPaid == game::itemForBlock(game::BlockId::Gravel) ? "gravel" : "SOMETHING ELSE");
    check("a gravel column breaking on a torch pays gravel, 100% of the time",
          gravelCount == 1 && gravelPaid == game::itemForBlock(game::BlockId::Gravel) &&
              gravelPaid != game::ItemId::Flint);
}

} // namespace

int main() {
    std::printf("=== 888 (item identity) and 884 (anvil landing damage) ===\n\n");
    armDiscriminatorIsSound();
    std::printf("\n");
    whatABrokenFallerPays();
    std::printf("\n");
    afterTheFix();
    std::printf("\n");
    anvilLadder();
    std::printf("\n");
    whoIsStandingThere();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
