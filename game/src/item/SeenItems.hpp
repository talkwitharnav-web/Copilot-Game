#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// **Every item the player has ever held, and it never forgets.**
///
/// The recipe book asks a different question from "can you make this right
/// now": a player who picked up a log, built a hut out of it and has none left
/// has still *met* logs, and hiding every plank and stick recipe from them
/// afterwards reads as the game losing track rather than as a rule. So this is
/// **append-only by construction** - `mark` sets a bit and nothing anywhere
/// clears one - and a recipe is listed once **any one** of its ingredients has
/// been seen, drawn red until it can actually be afforded.
///
/// **A bitset over the raw `ItemId` space, not over the catalogue.** The
/// display list `allItems()` is a filtered run of canonical entries whose
/// *positions* shift every time a block is added, so indexing by catalogue
/// position would silently repoint every saved bit at a different item the next
/// time the roster grows. The raw id is the far more stable of the two, which
/// is the same reason `SavedCreature` stores a `kind` rather than a table row -
/// but read the next paragraph before treating it as stable outright.
///
/// **The raw id is stable only BETWEEN renumberings, and this ledger has no
/// migration.** `ItemId` has been renumbered twice already - once when items
/// stopped beginning at 256, and once when eighteen duplicate ids were deleted
/// on 2026-08-18 - which is precisely why `upgradeLegacyItemId`,
/// `upgradeDuplicateRuns`, `ItemEra` and `migrateStack` exist. Every *stack* on
/// disk is carried across those two renumberings by that machinery. **These
/// bits are carried by nothing**, so a third renumbering would repoint all of
/// them at whatever item now holds each id, silently and permanently, since
/// nothing ever clears one.
///
/// A version rung could not express that repair the way every rung so far has
/// either: moving a set of ids is a **bit permutation** - read each set bit,
/// pass it through `migrateItemId`, set it in a fresh array - and not the field
/// copy the player rungs are made of. The tripwire is hung on the
/// `static_assert` beside `ItemEra::kCount` in `WorldStore.cpp`, because a
/// fourth era is where the next renumbering has to stop anyway.
///
/// Block items and non-block items share this one space: block ids run
/// `[0, 4096)` and the rest run from `kFirstToolItem`, so one run of bits
/// covers both.
///
/// **`std::uint32_t` words rather than `std::uint64_t`, deliberately.** This
/// array is written into `SavedPlayer`, which is a trivially-copyable blob with
/// a `static_assert` proving it has no padding. `SavedPlayer` is 4-byte aligned
/// today; 8-byte words would raise its alignment to 8, leave a 4-byte tail hole
/// and fail that assert - uninitialised stack bytes going to disk is exactly
/// what it exists to prevent.
inline constexpr std::size_t kSeenWordBits = 32;
inline constexpr std::size_t kSeenItemCount = static_cast<std::size_t>(ItemId::kLastItem) + 1;

/// **How many words the roster needs today - 4589 ids, so 144 words - and
/// deliberately NOT the width of the array.** This is the answer to "how many
/// ids exist", it moves every time `Item.hpp` grows, and everything that only
/// wants to know where the live bits stop reads it. `kSeenWordsOnDisk` below is
/// the answer to "how wide is the record", and the two are allowed to disagree
/// on purpose.
inline constexpr std::size_t kSeenWords = (kSeenItemCount + kSeenWordBits - 1) / kSeenWordBits;

/// **Frozen, not derived, and that is the whole point.**
///
/// This array is a member of `SavedPlayer`, so a width derived from `kLastItem`
/// makes `sizeof(SavedPlayer)` - the size of a record on a player's disk - a
/// function of `Item.hpp`. It was written that way for one afternoon on
/// 2026-08-20 and defused the same day, before version 9 reached any disk,
/// because the failure it sets up destroys saves:
///
/// About twenty more non-block ids - one more music-disc-sized run, or twenty
/// more mobs, since `Coal = SpawnEggFirst + kSpawnEggLayers` puts the spawn
/// eggs *inside* the item run and so a new creature pushes `kLastItem` too -
/// takes a derived width from 144 words to 145, and with it the record from the
/// 1844 bytes it measured under that scheme to 1848. That fires the size
/// `static_assert` in `WorldStore.hpp` in front of someone who **added no
/// member**, so the doc block above it, which explains what to do "if you add a
/// member", reads as somebody else's problem. The natural repair is to update
/// the literal - and it **builds completely clean**: `sizeof(SavedPlayer) ==
/// sizeof(LegacyPlayerV8) + sizeof(SavedPlayer::seenItems)` still passes
/// because both sides moved together, the member sum still passes, the version
/// ladder is untouched and `kFormatVersion` stays 9. Then `loadPlayer` takes
/// the current-version branch on every existing world, asks for 1848 bytes from
/// an 1844-byte payload, short-reads, returns `nullopt`, `Main.cpp` keeps the
/// freshly spawned character, and the next autosave writes it over the real
/// save. Inventory, ender chest, bed, effects, armour and position, gone for
/// every world, from one commit that added an item.
///
/// **So the width is reserved with headroom instead.** 160 words is 5120 bits,
/// covering ids 0 to 5119: **531 ids more than the 4589 that exist today**, as
/// of 2026-08-20. That figure is `kSeenWordsOnDisk * kSeenWordBits -
/// kSeenItemCount`, a constant expression, so recompute it rather than trusting
/// this sentence once the roster has moved on. Spend the headroom without a
/// thought - adding items is free and cannot move `sizeof(SavedPlayer)` by
/// design, which is what the size assert in `WorldStore.hpp` now says in as
/// many words. When it runs out the assert below fires and says what that
/// costs.
///
/// **This is the one place `CLAUDE.md`'s "derive one side from the other" rung
/// is the wrong answer**, and the retired comment here cited it. That ladder is
/// for two constants that must *agree*; these two must be allowed to
/// *disagree*, because one is a fact about an enum that changes and the other
/// is a fact about bytes already written down. A derivation cannot hold a fact
/// on disk still. The assert is the correct rung precisely because this
/// coupling fails loudly.
inline constexpr std::size_t kSeenWordsOnDisk = 160;

using SeenItems = std::array<std::uint32_t, kSeenWordsOnDisk>;

static_assert(kSeenWordsOnDisk * kSeenWordBits >= kSeenItemCount,
              "the item roster has outgrown the ledger width reserved in SavedPlayer. This is a "
              "kFormatVersion bump with a new LegacyPlayer rung, NOT a new literal in the size "
              "assert - widening the array without a rung makes loadPlayer short-read every "
              "existing save and the next autosave overwrite it");
static_assert(sizeof(SeenItems) % 4 == 0,
              "SavedPlayer is 4-byte aligned and asserts it has no padding; a bitset whose size is "
              "not a multiple of four would open a tail hole in it");

/// Whether `item` is a real id this bitset has a bit for.
///
/// `ItemId::None` is deliberately *not* seeable: it is the empty-stack sentinel
/// and a swept inventory is full of it, so letting it through would set bit 0
/// on the first frame of every world and unlock whatever id 0 happens to be.
constexpr bool seenIsAddressable(ItemId item) {
    return item != ItemId::None && static_cast<std::size_t>(item) < kSeenItemCount;
}

/// Records that the player has held `item`. Idempotent, and never removes.
constexpr void seenMark(SeenItems& seen, ItemId item) {
    if (!seenIsAddressable(item)) {
        return;
    }
    const auto index = static_cast<std::size_t>(item);
    seen[index / kSeenWordBits] |= std::uint32_t{1} << (index % kSeenWordBits);
}

/// Whether the player has ever held `item`.
constexpr bool seenHas(const SeenItems& seen, ItemId item) {
    if (!seenIsAddressable(item)) {
        return false;
    }
    const auto index = static_cast<std::size_t>(item);
    return (seen[index / kSeenWordBits] & (std::uint32_t{1} << (index % kSeenWordBits))) != 0;
}

/// Clears every bit above `kLastItem` - the tail of the last live word, and the
/// whole reserved words after it.
///
/// **For data off a disk, not for data we produced.** `seenMark` cannot set one
/// of these, so an in-memory set is always clean; a file written by a build
/// with a longer roster can carry ids this build has no name for, and leaving
/// them set would let `seenHas` answer for an id outside the enum. Called on
/// load beside the stack sanitiser for the same reason it exists.
///
/// **The reserved words are the half that is easy to miss.** While the array
/// width was derived there was only ever one partial word to mask. The frozen
/// width means the spare now spans sixteen whole words as well, and a longer
/// roster fills them first, so masking the partial word alone would trim
/// nineteen stray bits and leave five hundred.
constexpr void seenTrimToRoster(SeenItems& seen) {
    // **Measured against the derived word count and not the frozen one, which
    // is what keeps this shift legal.** A ceiling division leaves at most 31
    // bits over, so the shift below is always 0 to 31; the same expression
    // written with `kSeenWordsOnDisk` would ask for `>> 531`, which is
    // undefined behaviour rather than a zeroed word. The assert states that
    // invariant here, where the breaking edit would be made.
    constexpr std::size_t spare = kSeenWords * kSeenWordBits - kSeenItemCount;
    static_assert(spare < kSeenWordBits,
                  "the partial-word mask shifts by `spare`, so it has to stay inside one word; a "
                  "shift of 32 or more is undefined behaviour, not an empty word");
    if constexpr (spare != 0) {
        seen[kSeenWords - 1] &= std::uint32_t{0xFFFFFFFFu} >> spare;
    }
    for (std::size_t word = kSeenWords; word < kSeenWordsOnDisk; ++word) {
        seen[word] = 0;
    }
}

/// A compile-time proof that the three operations agree, so a future edit to
/// the word size or the indexing cannot quietly desynchronise them.
constexpr bool seenRoundTrips() {
    SeenItems seen{};
    if (seenHas(seen, ItemId::Stick)) {
        return false;
    }
    seenMark(seen, ItemId::Stick);
    if (!seenHas(seen, ItemId::Stick)) {
        return false;
    }
    // The empty sentinel must never take a bit, or a swept inventory unlocks id 0.
    seenMark(seen, ItemId::None);
    if (seenHas(seen, ItemId::None)) {
        return false;
    }
    // The last real id has to be addressable, which is what proves the word
    // count covers the roster rather than merely looking as though it does.
    seenMark(seen, ItemId::kLastItem);
    if (!seenHas(seen, ItemId::kLastItem)) {
        return false;
    }
    seenTrimToRoster(seen);
    return seenHas(seen, ItemId::kLastItem) && seenHas(seen, ItemId::Stick);
}

static_assert(seenRoundTrips(),
              "marking an item must make it seen, the empty sentinel must never be seeable, and the "
              "last id in the roster must still have a bit");

/// A compile-time proof that the trimmer clears the **whole** spare and not
/// just the nineteen bits at the top of the last live word.
///
/// **The one thing `seenRoundTrips` cannot show**, because nothing this build
/// can call will set a bit up there - `seenIsAddressable` refuses every id past
/// `kLastItem`, which is exactly why the bad case has to be forged by hand
/// here. The bits it forges are what a file written by a longer-rostered build
/// actually contains, and the reserved words are where that build's new items
/// would live.
constexpr bool seenTrimsWholeSpare() {
    SeenItems seen{};
    seen[kSeenWords - 1] = 0xFFFFFFFFu;
    for (std::size_t word = kSeenWords; word < kSeenWordsOnDisk; ++word) {
        seen[word] = 0xFFFFFFFFu;
    }
    seenMark(seen, ItemId::kLastItem);
    seenTrimToRoster(seen);
    // A trim that clears the spare by clearing everything would pass every
    // other test in this file, so the real last id is checked first.
    if (!seenHas(seen, ItemId::kLastItem)) {
        return false;
    }
    for (std::size_t word = kSeenWords; word < kSeenWordsOnDisk; ++word) {
        if (seen[word] != 0) {
            return false;
        }
    }
    constexpr std::uint32_t live =
        std::uint32_t{0xFFFFFFFFu} >> (kSeenWords * kSeenWordBits - kSeenItemCount);
    return seen[kSeenWords - 1] == live;
}

static_assert(seenTrimsWholeSpare(),
              "seenTrimToRoster must clear every reserved word as well as the tail of the last "
              "live one; a file from a build with a longer roster sets the reserved words first, "
              "and leaving them would let seenHas answer for an id this build has no name for");

} // namespace game
