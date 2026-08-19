// Probe for finding 757 - wiring the copper axe-scrape and honeycomb wax.
//
// Checks the BRANCH ORDER added to Main.cpp against the real Copper.hpp,
// which is the part the header's own sweeps cannot see. Throwaway.

#include "world/Block.hpp"
#include "world/Copper.hpp"

#include <cstdio>

using game::BlockId;

static int failures = 0;

// The axe branch, exactly as Main.cpp now orders it.
static BlockId axeWorks(BlockId bark) {
    const BlockId stripped = game::strippedFor(bark);
    if (stripped != bark) {
        return stripped;
    }
    const BlockId unwaxed = game::copper::unwaxedForm(bark);
    if (unwaxed != BlockId::Air) {
        return unwaxed;
    }
    const BlockId scrapedBack = game::copper::scraped(bark);
    if (scrapedBack != BlockId::Air) {
        return scrapedBack;
    }
    return bark; // nothing happens
}

static BlockId honeycombWorks(BlockId bare) {
    const BlockId waxed = game::copper::waxedForm(bare);
    return waxed == BlockId::Air ? bare : waxed;
}

int main() {
    int copperIds = 0;
    int scrapeable = 0;
    int unwaxable = 0;
    int waxable = 0;
    int stripCollisions = 0;
    int oneWay = 0;

    for (int raw = 0; raw < static_cast<int>(game::kBlockIdCount); ++raw) {
        const BlockId id = static_cast<BlockId>(raw);
        if (!game::copper::isCopper(id)) {
            // Nothing outside the copper table may be touched by either branch.
            if (game::copper::scraped(id) != BlockId::Air ||
                game::copper::waxedForm(id) != BlockId::Air ||
                game::copper::unwaxedForm(id) != BlockId::Air) {
                std::printf("  FAIL non-copper id %d answers a copper helper\n", raw);
                ++failures;
            }
            continue;
        }
        ++copperIds;

        // **No copper block may also be strippable**, or the axe branch's first
        // test would swallow it and the scrape would be unreachable.
        if (game::strippedFor(id) != id) {
            ++stripCollisions;
            ++failures;
            std::printf("  FAIL copper id %d is also strippable\n", raw);
        }

        const int stage = game::copper::stageOf(id);
        const bool waxed = game::copper::isWaxed(id);

        if (waxed) {
            const BlockId got = axeWorks(id);
            if (got == id) {
                // Every waxed id in the table must have a bare twin, or waxing
                // it was a one-way trip.
                ++oneWay;
                ++failures;
                std::printf("  FAIL waxed id %d cannot be unwaxed\n", raw);
            } else {
                ++unwaxable;
                if (game::copper::isWaxed(got)) {
                    ++failures;
                    std::printf("  FAIL unwaxing id %d left it waxed\n", raw);
                }
                if (game::copper::stageOf(got) != stage) {
                    ++failures;
                    std::printf("  FAIL unwaxing id %d moved it a stage\n", raw);
                }
            }
        } else {
            const BlockId got = axeWorks(id);
            if (stage == 0) {
                if (got != id) {
                    ++failures;
                    std::printf("  FAIL an axe changed unoxidised bare id %d\n", raw);
                }
            } else if (got == id) {
                // Allowed only where the lower stage has no id at all.
                if (game::copper::atStage(id, stage - 1) != BlockId::Air) {
                    ++failures;
                    std::printf("  FAIL bare id %d at stage %d would not scrape\n", raw, stage);
                }
            } else {
                ++scrapeable;
                if (game::copper::stageOf(got) != stage - 1) {
                    ++failures;
                    std::printf("  FAIL scraping id %d did not go back one stage\n", raw);
                }
                if (game::copper::isWaxed(got)) {
                    ++failures;
                    std::printf("  FAIL scraping id %d waxed it\n", raw);
                }
            }
            const BlockId onWax = honeycombWorks(id);
            if (onWax != id) {
                ++waxable;
                if (!game::copper::isWaxed(onWax)) {
                    ++failures;
                    std::printf("  FAIL honeycomb on id %d did not wax it\n", raw);
                }
                if (game::copper::stageOf(onWax) != stage) {
                    ++failures;
                    std::printf("  FAIL honeycomb on id %d moved it a stage\n", raw);
                }
                // And back again, which is the whole point of 757.
                if (axeWorks(onWax) != id) {
                    ++failures;
                    std::printf("  FAIL wax-then-scrape on id %d is not a round trip\n", raw);
                }
            }
        }
        // Honeycomb must never touch a waxed block.
        if (waxed && honeycombWorks(id) != id) {
            ++failures;
            std::printf("  FAIL honeycomb re-waxed an already waxed id %d\n", raw);
        }
    }

    std::printf("copper ids in the table          : %d\n", copperIds);
    std::printf("an axe scrapes a stage off       : %d  (was 0 - no caller existed)\n", scrapeable);
    std::printf("an axe takes the wax off         : %d  (was 0)\n", unwaxable);
    std::printf("honeycomb waxes                  : %d  (was 0)\n", waxable);
    std::printf("copper ids an axe would strip    : %d  (must be 0)\n", stripCollisions);
    std::printf("waxed ids with no way back       : %d  (must be 0)\n", oneWay);
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
