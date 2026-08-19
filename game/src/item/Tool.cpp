#include "item/Tool.hpp"

#include "item/Mining.hpp"

namespace game {

// **Every table that used to live here is now one row per block id in
// `Mining.hpp`.** Four accessors each carried their own hand-written range
// tests over the block enum, and they had drifted apart: one covered extra run
// 7 and the others did not, one gave a redstone block a wood tier and another
// gave it a hand tier, and forty-three ids in extra run 1 were gated behind a
// tier that no tool accessor named a kind for - so a bare fist opened them.
//
// The fix is structural rather than a list of corrections. `kMiningRows` is
// sized `kBlockIdCount` and filled by walking the enum, so a newly appended
// block run cannot fall outside it; it either gets an answer from the family
// predicates it already belongs to, or the `static_assert` sweep at the bottom
// of that header stops the build. What is left in this file is the three
// out-of-line functions `Tool.hpp` still promises - see the note at the bottom
// for the three that were removed on 2026-08-19 and why.

ToolProperties toolFor(ItemId item) { return mining::toolProperties(item); }

float blockHardness(BlockId block) { return miningRow(block).hardness; }

/// Whether the block comes away in your hand rather than shattering.
///
/// **Right kind and right tier**, which is the split the old body could not
/// express: it compared tiers and made a single exception for shears, so an
/// emberite shovel collected diamond ore and a bare fist collected a block of
/// iron. `canHarvest` asks `MiningRow::tool` and `MiningRow::toolRequired` as
/// well, and the shears exception is now just one block among the ids that
/// demand a kind at any tier.
bool yieldsDrop(BlockId block, ItemId item) { return canHarvest(block, item); }

// **Three more forwards stood here until 2026-08-19 and are gone: `harvestTool`,
// `harvestTier` and `breakSecondsGrounded`.** They were one line each onto
// `miningRow`, and they had **no callers anywhere** - not in `game/src`, not in
// `engine/`, not in a tool script.
//
// The claim was checked rather than taken, because "nothing calls this" is the
// claim that rots fastest, and a sweep reporting nothing is worthless unless it
// can be shown to report something. The sweep stripped comments and string
// literals first, so a name appearing only in prose did not count as a use, and
// it carried a control: `miningRow` 60 sites, `canHarvest` 49, `blastResistance`
// 36, `toolFor` 9, `toolProperties` 5, `breakSeconds` 4, `yieldsDrop` 4,
// `blockHardness` 3 - and a deliberately fabricated name, 0. The three above
// came back with exactly two sites each: their own declaration in `Tool.hpp` and
// their own definition here.
//
// **Two of them were also a second answer to a question `MiningRow` already
// owns.** "What tool does this block need" was spelled both `harvestTool(b)` and
// `miningRow(b).tool`, and only the second was ever consulted - a value derived
// somewhere other than the one table that owns it, which is `CLAUDE.md` bug
// shape #1. The weighted-plate fix earlier this round went through four sites
// that now share one predicate; leaving a fifth, unconsulted spelling of the
// same question in the public header is the next version of that bug.
//
// **This is not a rule against the door.** `Tool.hpp` is deliberately thin - it
// includes `Item.hpp` and `Block.hpp` and nothing else - so a translation unit
// can ask a mining question without including `Mining.hpp`, which derives a
// `constexpr` row for every block id. Only three TUs pay that cost today, and
// `BlockDrops.hpp` and `Recipe.cpp` reach the table solely through this file.
// That design is intact and is now entirely live: every function left here has
// a caller.
//
// **What would make this note false**, and the shape to restore if it does: a
// TU behind the door needing the block's tool, tier or break time. Add the
// forward back - the body is `return miningRow(block).tool;`, and the siblings
// above show the rest - rather than including `Mining.hpp` from a header. For a
// break time call the four-argument `breakSeconds` in `Mining.hpp` directly and
// pass where the player actually is: the reference divides mining speed by five
// for a submerged head and by five again for feet off the ground, and neither
// is knowable from a block id. The deleted two-argument spelling hard-coded
// "dry and grounded" into a name a caller had to read to notice.

} // namespace game
