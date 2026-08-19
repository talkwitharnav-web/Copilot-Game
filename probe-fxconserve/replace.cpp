// Finding 1117 - a comment above `spillReplaced` claims the replaceable
// non-fluid ids are "a strict subset of isWashedAway: 309 of them, 290 carrying
// a real drop", and separately that the Flat family is "the other 89 ids".
//
// Measured rather than read, because a wrong row looks exactly like a right one
// and there are three thousand of them.

#include "item/BlockDrops.hpp"
#include "world/Block.hpp"

#include <cstdio>
#include <vector>

int main() {
    const int count = static_cast<int>(game::kBlockIdCount);
    std::printf("kBlockIdCount = %d\n\n", count);

    int replaceable = 0;
    int withDrop = 0;
    int flat = 0;
    std::vector<game::BlockId> notWashed;

    for (int i = 0; i < count; ++i) {
        const auto id = static_cast<game::BlockId>(i);
        if (game::blockShape(id) == game::BlockShape::Flat) {
            ++flat;
        }
        if (id == game::BlockId::Air || game::isFluid(id) || !game::isReplaceable(id)) {
            continue;
        }
        ++replaceable;
        if (game::primaryDrop(id) != game::ItemId::None) {
            ++withDrop;
        }
        if (!game::isWashedAway(id)) {
            notWashed.push_back(id);
        }
    }

    std::printf("replaceable, not Air, not fluid : %d   (comment says 309)\n", replaceable);
    std::printf("  ...of those, with a real drop : %d   (comment says 290)\n", withDrop);
    std::printf("BlockShape::Flat ids            : %d   (comment says 89)\n\n", flat);

    std::printf("replaceable but NOT isWashedAway - the subset claim - %zu id(s):\n",
                notWashed.size());
    for (const game::BlockId id : notWashed) {
        std::printf("  %s\n", game::blockName(id));
    }
    if (notWashed.empty()) {
        std::printf("  (none - the subset claim holds)\n");
    }

    // CONTROL: the sweep must be able to find something, or a zero above proves
    // nothing. Count the washed-away ids that are NOT replaceable, which should
    // be a large number - the two predicates are not the same question.
    int washedNotReplaceable = 0;
    int flatReplaceable = 0;
    int flatNot = 0;
    for (int i = 0; i < count; ++i) {
        const auto id = static_cast<game::BlockId>(i);
        if (game::isWashedAway(id) && !game::isReplaceable(id)) {
            ++washedNotReplaceable;
        }
        if (game::blockShape(id) == game::BlockShape::Flat) {
            if (game::isReplaceable(id) && !game::isFluid(id) && id != game::BlockId::Air) {
                ++flatReplaceable;
            } else {
                ++flatNot;
            }
        }
    }
    std::printf("\nFlat and replaceable            : %d\n", flatReplaceable);
    std::printf("Flat and NOT replaceable        : %d   (\"the other 89\")\n", flatNot);
    std::printf("\nCONTROL: washed away but not replaceable: %d (a zero here would mean the\n"
                "         sweep is broken rather than that the claim is true)\n",
                washedNotReplaceable);
    return 0;
}
