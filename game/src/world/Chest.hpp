#pragma once

#include "item/Item.hpp"
#include "world/Tick.hpp"

#include <array>
#include <cstddef>

namespace game {

/// How many slots a chest holds. Three rows of nine, which is the reference's
/// and - not by accident - exactly the shape of the player's own storage, so
/// one panel layout serves both halves of the screen.
constexpr std::size_t kChestSlots = 27;

/// How many a hopper holds. It stores its five in the same `Chest` struct and
/// simply never touches the rest, which is what lets the whole container path -
/// saving, spilling on break, clicking, shift-clicking - serve both with no
/// second type.
constexpr std::size_t kHopperSlots = 5;

/// How often a hopper moves one item. Eight game ticks at the reference's
/// twenty a second, written as the seconds our frame loop actually counts in -
/// and taking the tick from `Tick.hpp` rather than restating it, which is how
/// there came to be seven copies of it.
constexpr float kHopperTransferSeconds = 8.0f * tick::kSeconds;

/// The transfer gap is a whole number of ticks, said in the *other* spelling of
/// the rate - the only thing that would notice this being written back out as a
/// bare `8.0f / 20.0f`, which is what it was.
///
/// > Fails if: this stops deriving from `tick::kSeconds`, or the tick rate is
/// > changed, which is never the fix for anything.
static_assert(kHopperTransferSeconds * tick::kPerSecond == 8.0f,
              "a hopper moves an item every eight ticks, not every 0.4 s of wall clock");

/// One chest's contents.
///
/// A **block entity**, for the same reason a furnace's is: which way a chest
/// opens is part of *which block it is* and lives in the id, but an inventory is
/// not and could never fit there.
///
/// **A generated chest has no entry in this map at all until someone touches
/// it.** Village chests are placed by the generator as `LootChest` ids, and
/// `loot::rollInto` fills one the first time it is opened, broken or blown up -
/// at which point the block becomes a plain chest and the entry appears.
/// That is why emptiness can still honestly mean "forget me" for an ordinary
/// chest: an unrolled chest is not an empty entry, it is *no* entry, and its
/// contents are a pure function of the world seed and its own position until
/// the moment they are not.
///
/// It is also what closes a duplication exploit that would otherwise be
/// unavoidable. Loot living only in this map would be dropped by `saveChests`
/// the moment a player emptied a chest, and the next load would regenerate the
/// chunk and hand them the same loot again, forever. Because the roll is
/// recorded in the *block id* instead, and writing that id flags the chunk
/// modified, an emptied chest stays emptied with no save-format change.
///
/// **One documented exception to "an empty chest is no entry", added 2026-08-19
/// (finding 1262):** a cell whose loot table has already been rolled keeps an
/// empty record on purpose. See `Main.cpp`'s `rolledLoot` set, which is the
/// other half. The reason is that the block id is the *only* other copy of
/// "already paid for", and a `kChunkFormatVersion` bump throws the chunk away -
/// so without the empty record a fully looted or broken village chest refills
/// on every bump, measured at 11 items. **So do not tidy the empty records
/// away**; that reopens 1262 exactly. The exception makes the store strictly
/// safer rather than looser: it is a second independent record of "rolled", it
/// carries no items to duplicate, and the placement path clears a cell's
/// records before building.
struct Chest {
    std::array<ItemStack, kChestSlots> slots{};

    /// Nothing in it.
    ///
    /// **This is a question, not permission to drop the entry - and it is the
    /// exact sentence the exception above contradicts, so read that first.**
    /// It used to end "so it can be forgotten rather than written to disk",
    /// which is true of an ordinary chest and false of a rolled loot cell: that
    /// one is saved *as an empty record on purpose*, because the record is the
    /// only copy of "already paid for" that survives a `kChunkFormatVersion`
    /// bump. `Main.cpp` measured the cost of the old reading at 11 items handed
    /// back per bump, and filed it against this comment rather than editing a
    /// file it does not own, on 2026-08-19. This is that edit.
    ///
    /// So: this answers whether there is anything in here, and **the caller
    /// decides what that is worth.** `Main.cpp`'s save path asks it and then
    /// asks `rolledLoot` as a second, separate question; `WorldStore`'s
    /// `saveChests` deliberately does not ask it at all.
    bool empty() const {
        for (const ItemStack& slot : slots) {
            if (!slot.empty()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace game
