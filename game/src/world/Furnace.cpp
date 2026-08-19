#include "world/Furnace.hpp"

#include "item/SlotOps.hpp"
#include "item/Smelting.hpp"

#include <algorithm>
#include <utility>

namespace game {
namespace {

/// What a smoker will take: anything that cooks into food.
///
/// Asked of the **result** rather than the input, so it needs no second table
/// and cannot drift from one. Wiki `[[Smoker]]`: it "cooks food twice as
/// quickly as a furnace, but cannot smelt anything else". That lands exactly on
/// the five raw meats, the four fish, the potato and the kelp, and excludes
/// sand, stone, ore and log - all of which a smoker used to accept.
bool smokerAccepts(ItemId result) { return isFood(result); }

/// What a blast furnace will take: raw metal, ore, and metal gear.
///
/// Wiki `[[Blast Furnace]]`: it smelts "raw metal, ore blocks, ancient debris,
/// and tools and armor made of iron, gold, chainmail or copper". The gear half
/// used to say "we have no smelt rule for tools or armour, so there is nothing
/// to add for those; when one appears it belongs here". One appeared on
/// 2026-08-19 - `Smelting.cpp` gained `nuggetFor`, seventeen iron, gold and
/// chainmail pieces smelt down - and until this branch existed a blast furnace
/// refused every one of them while the plain furnace took them, i.e. the
/// specialised cooker was the *slower* of the two on its own speciality.
///
/// **Asked of the result, exactly as `smokerAccepts` is, and for the same
/// reason**: the seventeen ids are already named in one place and a second list
/// here could only fall out of step with it. Nothing else in `kSmelting` yields
/// a nugget, so "smelts into a nugget" and "is iron, gold or chainmail gear"
/// are the same set today. **What would make that false:** a nugget becoming
/// the output of something that is not gear - a nugget-from-ore-dust rule, say
/// - at which point this has to ask the input's own question instead. Copper
/// gear is deliberately absent on both sides: `CopperNugget` does not exist
/// here and copper tools postdate our baseline.
bool blastFurnaceAccepts(ItemId input, ItemId result) {
    if (result == ItemId::IronNugget || result == ItemId::GoldNugget) {
        return true;
    }
    if (input == ItemId::RawIron || input == ItemId::RawGold || input == ItemId::RawCopper) {
        return true;
    }
    if (!isBlockItem(input)) {
        return false;
    }
    const BlockId block = blockForItem(input);
    // `isOre` already covers ancient debris and the sixteen vein ores. **Both**
    // nether ores are deliberately outside it - that predicate answers
    // "anything a vein places" and neither of these is placed by one - so both
    // have to be named. Naming one and forgetting the other is exactly how the
    // gold ore came to be refused: wiki `[[Blast Furnace]]` blasts "raw metal,
    // ore blocks, ancient debris..." and wiki `[[Nether Gold Ore]]` gives it a
    // gold ingot, so a plain furnace would smelt it while this one shrugged.
    return isOre(block) || block == BlockId::NetherQuartzOre ||
           block == BlockId::NetherGoldOre;
}

/// Whether this cooker will take this input at all, before asking whether the
/// input smelts into anything.
///
/// The plain furnace is the general case and says yes to everything; the two
/// specialised cookers each narrow it. **Both directions matter**: a smoker
/// that took ore would make the plain furnace pointless, and a blast furnace
/// that took food would make the two of them the same block with two textures.
///
/// Both narrowings get the input *and* the result and each picks the one its
/// question is actually about - the smoker asks what comes out, the blast
/// furnace asks both because ore is an input question and gear is a result one.
bool cookerAccepts(BlockId cooker, ItemId input, ItemId result) {
    if (isSmoker(cooker)) {
        return smokerAccepts(result);
    }
    if (isBlastFurnace(cooker)) {
        return blastFurnaceAccepts(input, result);
    }
    return true;
}

/// Whether what is in the input can actually become what is in the output right
/// now, and if not, **which** of the two reasons it is. They are not the same
/// answer: wiki `[[Smelting]]` pauses a part-cooked item when the output slot is
/// full - "smelting (but not fuel consumption) is paused until the output slot
/// becomes available" - and undoes one "at double speed" when the fire goes out.
/// Collapsing both into a bool is what had a full output quietly rewinding work
/// the reference keeps.
enum class Stall {
    /// Cooking, or ready to.
    None,
    /// Nothing in the input, nothing this input smelts into, or a cooker that
    /// will not take it.
    NoRecipe,
    /// A real result with nowhere to put it.
    OutputFull,
};

Stall cookStall(const Furnace& furnace, BlockId cooker, ItemStack& wanted) {
    if (furnace.input.empty()) {
        return Stall::NoRecipe;
    }
    wanted = smeltResult(furnace.input.item);
    if (wanted.empty()) {
        return Stall::NoRecipe;
    }
    if (!cookerAccepts(cooker, furnace.input.item, wanted.item)) {
        return Stall::NoRecipe;
    }
    // **One question, one owner.** `slots::roomFor` answers the empty slot, the
    // matching slot and the mismatched one, and it asks `maxStackFor` for the
    // cap and compares `damage` while it is there. What used to be here was an
    // `output.empty()` early-out, an `output.item == wanted.item` test and
    // `ItemStack::space()` - three partial restatements of it, and `space()`
    // was blind to `damage`, which is also *which contents a stowbox holds*.
    return slots::roomFor(furnace.output, wanted) >= wanted.count ? Stall::None : Stall::OutputFull;
}

// ---------------------------------------------------------------------------
// The compile-time sweep over every cooker. Modelled on `MiningSweep` in
// `Mining.hpp` and `BlastSweep` in `Explosion.hpp`: strided so no single
// `static_assert` blows MSVC's constexpr step budget, generated rather than
// hand-listed so a deleted line cannot silently stop checking 512 ids, and with
// a coverage assert at the end so ids appended past the last stride cannot fall
// out of it.
//
// **This is the guard the smoker went twenty milestones without.** Cook speed is
// two expressions in two files - `cookSpeed` in `Block.hpp` and the threshold
// here - and either one alone looks complete, which is exactly how a reviewer
// reading this file came away certain the doubling was missing. Nothing but a
// proof that spans both was ever going to settle it.
// ---------------------------------------------------------------------------

/// 512 for the reason it is 512 in `Block.hpp`, `Mining.hpp` and `Explosion.hpp`:
/// it splits the enum into few enough chunks to sweep and small enough ones that
/// Debug's `constexpr` step limit is never in sight. **The stride is the fixed
/// half of this pair and the pass count below is derived from it**, so no number
/// here has to be revisited when blocks are added.
constexpr int kCookSweepStride = 512;

/// How many strides the generated sweep instantiates - **derived, so it cannot
/// fall behind the block table.** Whatever `kBlockIdCount` becomes, this is the
/// number of `kCookSweepStride` chunks needed to reach the end of it, so
/// coverage holds by construction and nobody adding a block family has to know
/// this file exists. Deriving the *passes* and fixing the *stride* is the right
/// way round here for the same reason as in `Copper.hpp`: the stride bounds
/// per-pass `constexpr` work, and that is the quantity Debug's step limit cares
/// about, so it must not be allowed to grow with the block count.
constexpr int kCookSweepPasses =
    (static_cast<int>(kBlockIdCount) + kCookSweepStride - 1) / kCookSweepStride;

/// Every invariant the three cookers' speeds have to satisfy at once.
///
/// None of these restates `cookSpeed`; each is a rule it can break.
constexpr bool cookerSpeedsSound(int stride) {
    const int first = stride * kCookSweepStride;
    const int end = first + kCookSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId block = static_cast<BlockId>(i);
        const float speed = cookSpeed(block);

        // 1. **A speed at or below zero divides by zero in `cookSeconds` and
        //    stops or reverses the fire in the live path**, where it is
        //    multiplied into `deltaSeconds`. Nothing else in the game would say
        //    so: the furnace would simply sit there lit and never finish.
        if (!(speed > 0.0f)) {
            return false;
        }

        // 2. **Only a cooker may carry a speed at all.** A campfire or a
        //    composter picking one up would mean `cookSpeed`'s family predicates
        //    had widened past what `tickFurnace` is ever asked about, and the
        //    number would be applied to nothing.
        //    > Fails if: add any non-furnace id to the `isSmoker`/`isBlastFurnace`
        //    > test in `cookSpeed`.
        if (speed != 1.0f && !isFurnace(block)) {
            return false;
        }

        if (!isFurnace(block)) {
            continue;
        }

        const float seconds = cookSeconds(block);
        const bool specialised = isSmoker(block) || isBlastFurnace(block);

        // 3. **The whole of finding 163 in one line**: a smoker and a blast
        //    furnace are strictly faster than a plain furnace, and a plain
        //    furnace is not faster than itself. Their narrower input set is the
        //    price of exactly this, so a smoker that cooked at furnace speed
        //    would be a strict downgrade for the player who built one.
        //    > Fails if: `cookSpeed` returns 1.0f for smokers and blast
        //    > furnaces, which is the state this was filed against.
        if (specialised != (seconds < cookSeconds(BlockId::Furnace))) {
            return false;
        }

        // 4. **And by the reference's own factor, in every one of their states.**
        //    A cooking smoker is a *lit* smoker and a placed one is a facing
        //    variant, so a family predicate that answered only for the canonical
        //    id would leave every smoker a player ever looks at on furnace time.
        //    > Fails if: narrow `isSmoker` to `id == BlockId::Smoker`.
        if (seconds != (specialised ? kSmeltSeconds / 2.0f : kSmeltSeconds)) {
            return false;
        }
    }
    return true;
}

template <int Pass>
struct CookSweep {
    static_assert(cookerSpeedsSound(Pass),
                  "a cooker's speed breaks one of the four invariants above - the numbered "
                  "comments name the single edit that causes each");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyCookPassSwept(std::integer_sequence<int, Pass...>) {
    return (CookSweep<Pass>::swept && ...);
}

static_assert(everyCookPassSwept(std::make_integer_sequence<int, kCookSweepPasses>{}),
              "a cooker cooks at the wrong speed - the failing CookSweep instantiation above "
              "names which stride");
/// **True by construction since `kCookSweepPasses` became derived, and kept
/// anyway.** What it catches is no longer a block family - it is a future edit
/// that reverts the derivation to a hand-written count or gets its rounding
/// wrong.
static_assert(kCookSweepPasses * kCookSweepStride >= static_cast<int>(kBlockIdCount),
              "kCookSweepPasses is derived from kBlockIdCount, so if this fires the derivation "
              "itself has been edited - restore the ceiling division rather than raising a number");

// **There is no half-stride early warning here, and after the derivation above
// there is nothing left for one to warn about.** An early warning exists to
// give somebody notice before a hand-written pass count runs out; this one
// cannot run out, so a warning here would be a guard that can never fire, which
// is worse than no guard because it reads as protection.
//
// **This replaced an earlier comment that was already wrong.** That version
// argued a warning here was unnecessary because `Copper.hpp` was the tightest
// sweep and would fire first - true when written, false an hour later once
// copper's own count became derived and could no longer fire at all. The
// reasoning was sound and its premise expired, which is the whole argument for
// dissolving a coupling instead of describing it.
//
// **Do not rebuild the list of sweeps that used to be here.** It was wrong
// twice within one hour - it missed `ChunkMesher.cpp`'s `kBoxFaceSweepPasses`,
// then missed `Block.hpp`'s `kModelSweepStride`. **Search `*SweepPasses` and
// `*SweepStride` instead**: the search finds sweeps nobody has told you about
// and a list never will. As of 2026-08-19 the only one still carrying a
// hand-written pass count is `ChunkMesher.cpp`'s, and `Copper.hpp` holds the
// assert that warns for it.

// **The two published times themselves**, because the sweep above proves the
// smoker is faster than the furnace and would go on passing if both were halved
// together. Wiki `[[Smelting]]`: 200 ticks in a furnace, 100 in the other two.
static_assert(cookSeconds(BlockId::Furnace) == 10.0f && cookSeconds(BlockId::Smoker) == 5.0f &&
                  cookSeconds(BlockId::BlastFurnace) == 5.0f,
              "10 s and 5 s, the reference's 200 and 100 ticks at 20 ticks a second");

/// Nothing that leaves a container behind may stack, or burning the first of a
/// pile would owe a bucket to a slot that still holds the rest of them. Swept
/// over every item id rather than asserted of the lava bucket alone, so a second
/// remainder-bearing fuel cannot arrive without meeting the same rule.
///
/// > Fails if: give `fuelRemainder` an answer for something that stacks.
constexpr bool everyRemainderIsSingle() {
    for (int id = static_cast<int>(ItemId::kFirstToolItem);
         id <= static_cast<int>(ItemId::kLastItem); ++id) {
        const auto item = static_cast<ItemId>(id);
        if (fuelRemainder(item) != ItemId::None && maxStackFor(item) != 1) {
            return false;
        }
    }
    return true;
}

static_assert(everyRemainderIsSingle(),
              "a fuel that leaves a container behind has to be a stack of one - see the "
              "`--fuel.count` line in `tickFurnace`, which relies on it");
static_assert(fuelRemainder(ItemId::LavaBucket) == ItemId::Bucket,
              "the reference's lava bucket burns and hands the bucket back; deleting this is a "
              "bucket destroyed every hundred items");

} // namespace

float Furnace::cookFraction() const {
    return std::clamp(cookElapsed / kSmeltSeconds, 0.0f, 1.0f);
}

bool tickFurnace(Furnace& furnace, float deltaSeconds, BlockId cooker) {
    // **`kSmeltSeconds` is right for all three cookers, and the doubling is not
    // missing.** A smoker and a blast furnace cook in five seconds rather than
    // ten *and* drain their fuel twice as fast, so items-per-fuel is unchanged -
    // which is one multiplier on the whole tick, not two rates to keep in step.
    // The caller applies it: `Main.cpp` passes `deltaSeconds * cookSpeed(id)`,
    // so `cookElapsed` and `burnRemaining` are both already scaled by the time
    // they reach here. Adding a per-cooker cook time here would double it.
    //
    // **`cookSeconds` above is that whole arrangement written as one number**,
    // and the sweep beside it proves the arrangement rather than either half:
    // 10 s in a furnace, 5 s in the other two, strictly faster in every lit and
    // facing variant, for all 3,269 ids.
    ItemStack wanted;
    const Stall stall = cookStall(furnace, cooker, wanted);
    const bool ready = stall == Stall::None;

    if (furnace.burnRemaining > 0.0f) {
        furnace.burnRemaining = std::max(0.0f, furnace.burnRemaining - deltaSeconds);
    }

    // A fresh piece of fuel is only lit when there is work for it. Burning
    // through a stack of charcoal in an empty furnace would be the kind of
    // silent loss that is very hard to notice.
    if (furnace.burnRemaining <= 0.0f && ready && !furnace.fuel.empty()) {
        const float seconds = fuelBurnSeconds(furnace.fuel.item);
        if (seconds > 0.0f) {
            furnace.burnTotal = seconds;
            furnace.burnRemaining = seconds;
            // **The container comes back; it is not burned with its contents.**
            // A lava bucket leaves an empty bucket in the fuel slot in the
            // reference, and `--count` alone would destroy it - the same silent
            // loss the placement paths have already been caught in three times.
            // `fuelRemainder` is `None` for everything that is consumed whole,
            // so this is the ordinary path with one branch, not a special case
            // for buckets.
            const ItemId leftover = fuelRemainder(furnace.fuel.item);
            if (--furnace.fuel.count <= 0) {
                furnace.fuel =
                    leftover == ItemId::None ? ItemStack{} : ItemStack{leftover, 1, 0};
            }
        }
    }

    if (furnace.burnRemaining > 0.0f && ready) {
        furnace.cookElapsed += deltaSeconds;
        if (furnace.cookElapsed >= kSmeltSeconds) {
            furnace.cookElapsed -= kSmeltSeconds;
            // **`merge` empties the stack it moves out of, so anything the
            // caller needs about the result has to be taken before the call.**
            // What was here compared `merge`'s return against `wanted.count`
            // *after* it, which is always zero once the move succeeds - so the
            // guard never held, the input was never consumed, and every
            // completed smelt was an ingot conjured out of nothing for the price
            // of the fuel. One raw iron and a stack of coal was 512 ingots. A
            // probe driving 64 raw iron through a furnace came out with 64 still
            // in the input slot and 8 in the output.
            //
            // **So the question is asked before the mutation, not after it.**
            // `roomFor` is the same function `cookStall` used at the top of the
            // tick and the same one `merge` itself asks, so re-asking it here
            // costs nothing and makes "all of the result moves" a local fact
            // rather than a chain of reasoning about what has happened since.
            // A partial move is the same duplication in miniature - half a
            // result in the output and an input still waiting to be smelted -
            // and this is what makes it unreachable rather than merely unlikely.
            const int want = wanted.count;
            if (slots::roomFor(furnace.output, wanted) >= want) {
                // **One owner for the move.** `slots::merge` opens the empty
                // slot carrying `wanted`'s `damage` and tops up a matching one,
                // which is what the `empty() ? assign : count +=` written here
                // got right only by luck: nothing smelted is damaged *yet*, and
                // the first thing that is would have come out of the slot new.
                slots::merge(furnace.output, wanted, want);
                if (--furnace.input.count <= 0) {
                    furnace.input = ItemStack{};
                }
            }
        }
    } else if (furnace.burnRemaining > 0.0f && stall == Stall::OutputFull) {
        // **Held, not rewound.** The reference pauses a part-cooked item while
        // the output has nowhere to put it and keeps burning the fuel; only the
        // fire going out undoes progress. Rewinding here cost a player the work
        // done on the item they were mid-way through every time an output slot
        // filled - which is every automated furnace nobody has emptied yet.
    } else {
        // Progress slides back rather than snapping to zero, so pulling an item
        // out for a moment does not throw away all of its cooking. Double speed
        // is the reference's own rate for an item whose fire has gone out.
        furnace.cookElapsed = std::max(0.0f, furnace.cookElapsed - deltaSeconds * 2.0f);
    }

    return furnace.burnRemaining > 0.0f;
}

} // namespace game
