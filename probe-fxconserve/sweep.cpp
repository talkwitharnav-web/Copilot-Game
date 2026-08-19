// Can the faller sweep produce a one?
//
// `fallersAreNeverOccupants` skips every id for which `isFalling` is false, so
// if that predicate ever collapsed the sweep would pass while proving nothing -
// a zero from a test that cannot produce a one. This measures the population it
// actually inspects and demonstrates the predicate rejecting a planted
// violation, which is the control the sweep itself is missing.

#include "item/BlockDrops.hpp"
#include "world/Block.hpp"

#include <cstdio>

namespace {

constexpr int kStride = 512;

/// The real rule, copied verbatim from Main.cpp.
constexpr bool realRule(int stride) {
    const int first = stride * kStride;
    const int end = first + kStride;
    for (int id = first; id < end && id < static_cast<int>(game::kBlockIdCount); ++id) {
        const game::BlockId block = static_cast<game::BlockId>(id);
        if (!game::isFalling(block)) {
            continue;
        }
        if (game::isReplaceable(block) || game::itemForBlock(block) == game::ItemId::None) {
            return false;
        }
    }
    return true;
}

/// CONTROL: the identical rule with the gate widened to every id, which plants
/// exactly the violation the real one is looking for. If this also returns true
/// for every stride, the rule cannot detect anything and the real zero is not
/// evidence.
constexpr bool plantedViolation(int stride) {
    const int first = stride * kStride;
    const int end = first + kStride;
    for (int id = first; id < end && id < static_cast<int>(game::kBlockIdCount); ++id) {
        const game::BlockId block = static_cast<game::BlockId>(id);
        if (game::isReplaceable(block) || game::itemForBlock(block) == game::ItemId::None) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    int falling = 0;
    int fallingWithItem = 0;
    for (int i = 0; i < static_cast<int>(game::kBlockIdCount); ++i) {
        const auto id = static_cast<game::BlockId>(i);
        if (game::isFalling(id)) {
            ++falling;
            if (game::itemForBlock(id) != game::ItemId::None) {
                ++fallingWithItem;
            }
        }
    }
    std::printf("ids that fall                    : %d\n", falling);
    std::printf("  ...of those, with an item       : %d\n", fallingWithItem);
    std::printf("the sweep therefore INSPECTS %d ids rather than skipping every one\n\n", falling);

    int realFalse = 0;
    int plantedFalse = 0;
    for (int pass = 0; pass < 7; ++pass) {
        if (!realRule(pass)) {
            ++realFalse;
        }
        if (!plantedViolation(pass)) {
            ++plantedFalse;
        }
    }
    std::printf("real rule   : %d of 7 passes false (expect 0 - this is the claim)\n", realFalse);
    std::printf("CONTROL     : %d of 7 passes false (expect >0 - proves the rule CAN say no)\n",
                plantedFalse);

    const bool ok = falling > 0 && realFalse == 0 && plantedFalse > 0 &&
                    game::isFalling(game::BlockId::Sand) && game::isFalling(game::BlockId::Gravel);
    std::printf("\n%s\n", ok ? "ALL PASS" : "FAILURES");
    return ok ? 0 : 1;
}
