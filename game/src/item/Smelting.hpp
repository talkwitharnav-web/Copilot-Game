#pragma once

#include "item/Item.hpp"
#include "world/Block.hpp"

namespace game {

/// One item's stay in the fire, in **seconds of the cooker's own clock**. Every
/// recipe takes the same time, which is what the reference does and what lets
/// the progress arrow be a single fraction rather than a per-recipe one.
///
/// The number is the reference's: wiki `[[Smelting]]` says "The furnace takes 10
/// seconds (200 in-game ticks) to smelt an item", and 200 ticks divided by the
/// 20 ticks a second the whole reference is published in is this. **The wiki
/// states times in ticks and this field is seconds**, so that division is the
/// one place the unit changes hands.
///
/// **It is a wall-clock second only in a plain furnace, and that is the trap
/// this constant lives in.** `tickFurnace` is handed
/// `deltaSeconds * cookSpeed(cooker)`, so `Furnace::cookElapsed` and
/// `Furnace::burnRemaining` are both measured against a clock that runs at
/// double speed inside a smoker or a blast furnace - one multiplier on the whole
/// tick rather than two rates to keep in step. What a player's own clock sees is
/// `cookSeconds` below.
constexpr float kSmeltSeconds = 10.0f;

/// **Wall-clock seconds** one item spends in `cooker`: 10 in a furnace, 5 in a
/// smoker or a blast furnace.
///
/// Wiki `[[Smelting]]` on the two specialised cookers: they "smelt twice as
/// quickly as furnaces, requiring only 5 seconds (100 game ticks) to smelt 1
/// item; they consume the same amount of fuel as regular furnaces per item
/// smelted" - and the fuel table's own footnote spells the second half out, a
/// fuel "is burned twice as fast but produces the same number of items". Neither
/// line carries the wiki's Java-only marker, so it is Bedrock's behaviour too,
/// and it is why the doubling is a single multiplier on the tick: halving the
/// cook time without halving the fuel would double every fuel's yield.
///
/// **This is the product of the two expressions the live path evaluates** -
/// `Main.cpp`'s `deltaSeconds * cookSpeed(present)` and `Furnace.cpp`'s
/// `cookElapsed >= kSmeltSeconds` - written once so the sweep in `Furnace.cpp`
/// can assert the number a player actually waits for rather than one half of it.
constexpr float cookSeconds(BlockId cooker) { return kSmeltSeconds / cookSpeed(cooker); }

/// What `input` turns into, or an empty stack if it does not smelt.
ItemStack smeltResult(ItemId input);

/// How long one of `item` keeps a furnace alight, in seconds. Zero means it is
/// not fuel.
///
/// **Seconds of the cooker's own clock**, exactly as `kSmeltSeconds` is: the
/// reference publishes one burn time per fuel and burns it twice as fast in a
/// smoker or a blast furnace, so the same 80 s of coal is the same eight items
/// in all three and only the wall clock differs.
///
/// **So every row of `kFuels` is one fuel item in a plain furnace, and not one
/// of them is ever scaled per cooker.** The reference's fuel table says it in
/// its own footnote - "All times given are for fuel burned in a furnace. When
/// burned in a blast furnace or smoker, fuel is burned twice as fast but
/// produces the same number of items." Halving a row to "match" a smoker would
/// **double** that smoker's items per fuel, because the tick this is measured
/// against arrives already doubled.
///
/// Kept separate from `smeltResult` because the two are unrelated questions: a
/// log is both an input and a fuel, and plenty of things are exactly one.
float fuelBurnSeconds(ItemId item);

/// What a spent fuel leaves behind in the fuel slot, or `ItemId::None` for the
/// fuels that are consumed whole.
///
/// **A container is not fuel; what it holds is.** Wiki `[[Smelting]]`'s fuel
/// table gives a lava bucket 1000 s - a hundred items - and leaves the bucket,
/// which is what the automation note beside it is describing when it says that
/// "in case of lava being used as fuel, any empty buckets come out of the bottom
/// hopper".
///
/// **`kFuels` carries `{ItemId::LavaBucket, 1000.0f}` and this branch is live.**
/// It was written before that row existed and said so, which was worse than
/// saying nothing: a reader who believes the row is missing concludes this
/// function is unreachable and deletes it, and every furnace fuelled with lava
/// then eats the player's bucket. So the two halves are bound rather than merely
/// agreeing - `Smelting.cpp` asserts `fuelIndexOf(LavaBucket) >= 0`, the
/// 1000 s, **and** `fuelRemainder(LavaBucket) == Bucket` in one expression, and
/// `Furnace.cpp` asserts this function's answer beside the `--fuel.count` that
/// depends on it. **Deleting either half fails the build, and those asserts are
/// the whole of the defence** - if this ever reads as dead code, the assert is
/// the thing to go and read, not the thing to delete alongside it.
///
/// Anything answered here must be an item that does not stack, or burning the
/// first of a pile would owe a container the slot has no room for - the sweep in
/// `Furnace.cpp` proves that over every item id rather than over the one that is
/// here now.
constexpr ItemId fuelRemainder(ItemId item) {
    return item == ItemId::LavaBucket ? ItemId::Bucket : ItemId::None;
}

} // namespace game
