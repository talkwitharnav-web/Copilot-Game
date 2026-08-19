#pragma once

#include "item/Item.hpp"
#include "item/Mining.hpp"
#include "item/SlotOps.hpp"
#include "world/Survival.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace game {

/// Slots reachable from the hotbar, and the width of the storage grid.
constexpr std::size_t kHotbarSlots = 9;

/// Rows of storage above the hotbar.
constexpr std::size_t kStorageRows = 3;

/// The hotbar is the **first** nine slots, not the last, so the number keys map
/// straight onto slot indices.
constexpr std::size_t kInventorySlots = kHotbarSlots * (kStorageRows + 1);

/// Worn pieces, indexed by `ArmourSlot`'s own order - head, chest, legs, feet.
///
/// **Derived from `ArmourSlot::None` rather than written as 4, so the coupling
/// cannot rot.** This count and `Item.hpp`'s enum are one fact stated once. The
/// two would otherwise have to agree by hand across a file boundary, and
/// **that disagreement fails silently rather than loudly**: a fifth slot added
/// to the enum would leave this at 4, and every `armourAt` loop in the tree
/// would simply skip it - `armourSet` would ignore its defence, and the death
/// drop in `Main.cpp` would leave the piece on the corpse and destroy it. No
/// assert fires, no warning is emitted, and the build is green. A derivation
/// needs none of that machinery, which is why it beats both an assert and a
/// comment. `kInventorySlots` two declarations above is the same idea.
///
/// **The premise is that `None` is the past-the-end sentinel**, which is what
/// the assert below pins. It stays true when a slot is *added* - the whole
/// point - and fails only if `None` stops being last, which is the one edit
/// that would make this derivation wrong.
///
/// **Deliberately a second array rather than four more entries on `m_slots`.**
/// Every existing loop over the inventory means "what the player is carrying",
/// and eight of them - `add`, `hasRoomFor`, `count`, `consume` and the craft
/// and drop paths - would silently start treating a worn helmet as loose
/// storage. That is `CLAUDE.md` bug shape #2, widening a family predicate and
/// killing the early-outs behind it, and the cost of avoiding it is one array.
constexpr std::size_t kArmourSlots = static_cast<std::size_t>(ArmourSlot::None);
static_assert(static_cast<std::size_t>(ArmourSlot::Head) < kArmourSlots &&
                  static_cast<std::size_t>(ArmourSlot::Chest) < kArmourSlots &&
                  static_cast<std::size_t>(ArmourSlot::Legs) < kArmourSlots &&
                  static_cast<std::size_t>(ArmourSlot::Feet) < kArmourSlots,
              "kArmourSlots is derived from ArmourSlot::None, which only works while None is the "
              "past-the-end sentinel sitting after every real slot. If this fails, a slot was given "
              "a value at or above None - fix the enum's order rather than hard-coding the count "
              "back, because a hand-written count is what this derivation exists to remove");

/// "Whatever wear it has, whatever it is holding." A real `damage` is never
/// negative, so this cannot collide with one.
constexpr int kAnyDamage = -1;

constexpr bool matchesDamage(const ItemStack& stack, int damage) {
    return damage == kAnyDamage || stack.damage == damage;
}

/// What the player is carrying.
///
/// Fixed size and plain data: no allocation, cheap to copy, and it is written
/// straight into `player.dat` by `WorldStore`.
class Inventory {
public:
    constexpr ItemStack& slot(std::size_t index) { return m_slots[index]; }
    constexpr const ItemStack& slot(std::size_t index) const { return m_slots[index]; }

    static constexpr std::size_t size() { return kInventorySlots; }

    /// Adds what it can, returning whatever did not fit.
    ///
    /// Tops up matching stacks before opening an empty slot, so picking things
    /// up does not scatter one item across several slots.
    /// `damage` is written only into a **freshly opened** slot, never onto a
    /// stack being topped up. Nothing that carries a meaningful damage stacks
    /// past one, so the two cases can never both apply - and a top-up asks
    /// `slots::roomFor`, which refuses a stack whose damage differs anyway.
    ///
    /// **`constexpr` and defined here so `addAgreesWithHasRoomFor` below can run
    /// it at compile time.** That proof is the only reason either of these two
    /// is in the header rather than the .cpp, and it is worth the move.
    constexpr int add(ItemId item, int count, int damage = 0) {
        if (item == ItemId::None || count <= 0) {
            return 0;
        }

        // Matching stacks first. Opening a fresh slot while a partial one exists
        // is how an inventory ends up holding the same thing four times over.
        for (ItemStack& stack : m_slots) {
            if (count <= 0) {
                break;
            }
            if (stack.empty()) {
                continue;
            }
            const int moved = std::min(count, slots::roomFor(stack, item, damage));
            stack.count += moved;
            count -= moved;
        }

        for (ItemStack& stack : m_slots) {
            if (count <= 0) {
                break;
            }
            if (stack.empty()) {
                stack.item = item;
                stack.count = std::min(count, maxStackFor(item));
                stack.damage = damage;
                count -= stack.count;
            }
        }

        return count;
    }

    /// True if `count` of `item` could be added without anything being lost.
    ///
    /// **Must answer exactly what `add` would do, `damage` and all**, which is
    /// why both count with `slots::roomFor` rather than each deciding for
    /// itself what a stack will accept - and why that claim is now a
    /// `static_assert` below rather than this sentence.
    constexpr bool hasRoomFor(ItemId item, int count, int damage = 0) const {
        if (item == ItemId::None || count <= 0) {
            return true;
        }

        // The same `slots::roomFor` `add` fills by, slot for slot. Asking a
        // different question here than `add` answers is the whole bug: this
        // counted a stack whose `damage` differs as room, and the craft loop
        // believed it.
        int room = 0;
        for (const ItemStack& stack : m_slots) {
            room += slots::roomFor(stack, item, damage);
            if (room >= count) {
                return true;
            }
        }
        return false;
    }

    /// Removes one from a slot, clearing it when the last is used.
    void consumeOne(std::size_t index);

    /// How many of an item are carried, across every slot.
    ///
    /// **`damage` is part of the question and the default is "any".** For
    /// anything that stacks past one, `damage` is meaningless and `kAnyDamage`
    /// is the right answer - which is why the arrows this is asked about today
    /// are correct without it. For a tool it is wear and for a stowbox it names
    /// which contents the box holds, so "how many stowboxes" has no single
    /// answer, and a recipe that ever asks for one must say which.
    int count(ItemId item, int damage = kAnyDamage) const;

    /// Spends up to `wanted` of an item wherever it is held, returning how many
    /// were actually taken. Ammunition is spent from the whole inventory rather
    /// than from the selected slot, which is holding the bow.
    ///
    /// Same rule as `count`, and here it has teeth: spending "three stowboxes"
    /// without saying which would take three boxes holding three different
    /// things and destroy two of them.
    int consume(ItemId item, int wanted, int damage = kAnyDamage);

    /// A worn piece, by the slot it is worn in.
    constexpr ItemStack& armour(ArmourSlot slot) {
        return m_armour[static_cast<std::size_t>(slot)];
    }
    constexpr const ItemStack& armour(ArmourSlot slot) const {
        return m_armour[static_cast<std::size_t>(slot)];
    }
    /// The same four by index, for the inventory screen, which walks slots
    /// rather than naming them. **`ArmourSlot::None` is 4 and would run off the
    /// end**, which is why the enum overload above is the one to reach for and
    /// this one takes a plain index the caller has already bounded.
    ///
    /// **`size()` DOES NOT COUNT THESE, AND THAT HAS COST ONE REAL BUG
    /// ALREADY** (finding 9635, 2026-08-19). `m_armour` is a second array
    /// deliberately outside `m_slots`, and the reasoning is sound: it stops
    /// `add`, `hasRoomFor`, `count`, `consume` and the craft and drop paths
    /// from treating a worn helmet as loose storage. But "outside the bag" is
    /// right for those loops and WRONG for any loop whose question is "what
    /// does this player have on them" rather than "what is in their bag", and
    /// the death drop in `Main.cpp` was written as the former when it needed to
    /// be the latter - so a full diamond set was the one thing dying did not
    /// cost.
    ///
    /// **So before you write `for (i = 0; i < inv.size(); ++i)`, ask which of
    /// the two questions you are asking.** If it is the second, you need this
    /// array too. There is deliberately no `everythingCarried()` helper: the
    /// death drop is currently the only loop on that side of the line, and a
    /// one-caller abstraction is the thing this project has been burned by
    /// often enough to have a rule about it. The day there is a second, write
    /// it and give it both.
    constexpr ItemStack& armourAt(std::size_t index) { return m_armour[index]; }
    constexpr const ItemStack& armourAt(std::size_t index) const { return m_armour[index]; }

    /// **What is worn, as the two numbers the damage curve wants.** The single
    /// place either number is derived, so `survival::armourDamageTaken` can
    /// never be handed a defence total from one reading and a toughness from
    /// another - the pairing failure `survival::ArmourSet` exists to prevent.
    ///
    /// **A piece in the wrong slot contributes nothing.** Boots stuffed into
    /// the helmet slot are not a helmet, and without this test they would arm
    /// the player exactly as if they were - which also means the UI can move
    /// stacks around without this function having to trust it.
    ///
    /// **This function is the ONLY bridge between `Main.cpp` and
    /// `armourDefence`/`armourToughness`, and that has now cost three separate
    /// agents an afternoon each.** A sweep of `Main.cpp` for either of those two
    /// names reads **0, correctly, and always will** - `Main.cpp` calls
    /// `inventory.armourSet()` and nothing below it. The chain is three hops:
    ///
    ///     Main.cpp  ->  Inventory::armourSet()  ->  armourDefence(ItemId)
    ///                   (this function)             (Item.hpp)
    ///
    /// Read as "armour is not applied", that zero is the false-positive class
    /// `CLAUDE.md` #15 names outright: *a value routed through a struct is
    /// invisible to a call-site sweep, so follow the data as well as the calls*.
    /// **The question a sweep should actually ask of `Main.cpp` is
    /// `armourSet`**, which read 6 on 2026-08-19 - one at the `updatePlayer`
    /// boundary and five at `hurtPlayer` sites.
    ///
    /// **What would make that false**, since a dated negative claim is worth
    /// nothing without one: `move.armour` losing its assignment, `hurtPlayer`
    /// regaining a defaulted `ArmourSet` parameter, or a sixth damage path
    /// appearing that calls `game::damagePlayer` directly instead of through
    /// the lambda. All three are visible in one grep of `Main.cpp` for
    /// `armourSet`, which is the search this paragraph exists to redirect you
    /// to.
    constexpr survival::ArmourSet armourSet() const {
        survival::ArmourSet set;
        for (std::size_t i = 0; i < kArmourSlots; ++i) {
            const ItemStack& worn = m_armour[i];
            if (worn.empty() || armourSlot(worn.item) != static_cast<ArmourSlot>(i)) {
                continue;
            }
            set.defence += armourDefence(worn.item);
            set.toughness += armourToughness(worn.item);
        }
        return set;
    }

    /// Charges one blow's wear to **every** worn piece, returning how many broke.
    ///
    /// `costPerPiece` comes from `survival::armourDurabilityCost`, which is
    /// computed from the **raw incoming** damage - see its comment, because
    /// feeding it the damage that actually landed is the unit slip that would
    /// more than double how long a set lasts.
    ///
    /// **Every piece pays the same cost; it is not divided among them.** That is
    /// the reference's rule and it is why this takes one number and applies it
    /// four times rather than sharing it out.
    ///
    /// **Nothing breaks today, and that is a missing table rather than a bug
    /// here.** The limit is `mining::toolProperties(item).durability`, whose own
    /// comment says "Zero means it never wears" - and that table carries rows
    /// for every pickaxe and for the bow but **not one row for any armour
    /// piece**. So armour currently wears without limit. When those rows land
    /// this function starts breaking pieces with no edit; until then the
    /// behaviour is "unbreakable", which is the safe direction to be wrong in.
    constexpr int wearArmour(int costPerPiece) {
        if (costPerPiece <= 0) {
            return 0;
        }
        int destroyed = 0;
        for (std::size_t i = 0; i < kArmourSlots; ++i) {
            ItemStack& worn = m_armour[i];
            if (worn.empty() || armourSlot(worn.item) != static_cast<ArmourSlot>(i)) {
                continue;
            }
            const int limit = mining::toolProperties(worn.item).durability;
            worn.damage += costPerPiece;
            if (limit > 0 && worn.damage >= limit) {
                worn = ItemStack{};
                ++destroyed;
            }
        }
        return destroyed;
    }

    /// **Wears a carried piece, handing back whatever it displaces.**
    ///
    /// The one thing that stood between the armour arithmetic and a player who
    /// benefits from it. On 2026-08-19 the whole rest of the chain was complete
    /// and correct - storage here, `armourSet` below, the reduction inside
    /// `damageWithResistance`, the durability drain in `Main.cpp` - while
    /// `SlotHit{Region::Armour` was produced by **nothing anywhere**, measured
    /// against Grid 2, Craft 1, CraftResult 1 and Chest 1 as live controls. So
    /// a full set could be mined, crafted and looked at, and never worn. That
    /// is `CLAUDE.md` bug shape #15 one level up: not a dead function, a whole
    /// finished feature with no way in.
    ///
    /// **That gap closed the same day.** `hud/InventoryScreen.cpp` now produces
    /// `SlotHit{Region::Armour, i}` from the four cells measured out of the
    /// panel art, and this function has two live callers in `Main.cpp` -
    /// shift-clicking a piece in the bag and right-clicking one in hand. The
    /// paragraph above is kept because it is the history that explains why the
    /// destination is derived here rather than passed in.
    ///
    /// **A swap rather than a move**, because equipping over a worn helmet has
    /// to return the old one rather than destroy it - and because that makes
    /// unequipping the same call rather than a second function to get wrong.
    ///
    /// **The destination is derived, never passed in.** `armourSlot` owns which
    /// piece goes where, so no caller can put boots on a head. `armourSet`
    /// already declines to count a mis-slotted piece; this declines to create
    /// one, so the two agree by construction rather than by agreement.
    constexpr bool equipArmour(std::size_t carried) {
        if (carried >= kInventorySlots) {
            return false;
        }
        ItemStack& held = m_slots[carried];
        if (held.empty() || !isArmour(held.item)) {
            return false;
        }
        const ArmourSlot destination = armourSlot(held.item);
        if (destination == ArmourSlot::None) {
            return false;
        }
        ItemStack& worn = m_armour[static_cast<std::size_t>(destination)];
        const ItemStack displaced = worn;
        worn = held;
        held = displaced;
        return true;
    }

private:    std::array<ItemStack, kInventorySlots> m_slots{};
    /// **Not part of `m_slots`, and saved separately.** The on-disk record has
    /// its own `std::array<ItemStack, kInventorySlots>` and copies slot by
    /// slot, so adding this member could not change the save format or corrupt
    /// an existing world.
    ///
    /// It was genuinely unsaved when it was written on 2026-08-19, and
    /// `WorldStore` carried it the same day in player record **version 7** -
    /// "version 6 plus the absorption clock and the worn armour" - so a worn
    /// set now survives a reload. **Dated because a negative claim about
    /// another file rots fastest**, and this one rotted within the hour.
    std::array<ItemStack, kArmourSlots> m_armour{};
};

/// Runs both halves over one inventory and reports whether they agreed.
///
/// "There was room" and "nothing was left over" are the same claim asked two
/// ways, and they have disagreed before: `hasRoomFor` compared `item` where
/// `add` compared `item` and `damage`, so the shift-click craft loop counted a
/// stowbox holding one set of contents as room for a box holding another, and
/// crafted into space that did not exist.
constexpr bool addAgreesWithHasRoomFor(const Inventory& start, ItemId item, int count,
                                       int damage) {
    Inventory inventory = start;
    const bool predicted = inventory.hasRoomFor(item, count, damage);
    return (inventory.add(item, count, damage) == 0) == predicted;
}

/// An inventory with **every** slot holding `filler`. Every one, because an
/// inventory with a spare slot has room for anything and proves nothing.
constexpr Inventory carryingOnly(const ItemStack& filler) {
    Inventory inventory;
    for (std::size_t i = 0; i < Inventory::size(); ++i) {
        inventory.slot(i) = filler;
    }
    return inventory;
}

/// The shapes the two answers have to agree about, checked at compile time.
///
/// This is the `foodTablesAgree()` arrangement: a claim that used to be a
/// comment, turned into something the compiler refuses to build without. The
/// whole catalogue is not worth walking here - thirty-six slots copied four
/// thousand times is a constant-evaluation budget spent on nothing - but the
/// shapes below are the ones the pair has actually been wrong about.
constexpr bool inventoryAnswersAgree() {
    constexpr ItemId kStowbox = itemForBlock(BlockId::Stowbox);
    const Inventory empty;
    // A full stack of coal in every slot, and the same one item short.
    const Inventory fullOfCoal = carryingOnly(ItemStack{ItemId::Coal, kMaxStack, 0});
    const Inventory nearlyFull = carryingOnly(ItemStack{ItemId::Coal, kMaxStack - 1, 0});
    // Coal at a damage nothing else carries. **The shape the two disagreed
    // about**: comparing `item` alone finds 36 x 63 room here that `add` will
    // not use.
    const Inventory wornCoal = carryingOnly(ItemStack{ItemId::Coal, 1, 5});
    const Inventory tools = carryingOnly(ItemStack{ItemId::IronPickaxe, 1, 0});
    const Inventory boxes = carryingOnly(ItemStack{kStowbox, 1, 3});

    Inventory oneSpare = boxes;
    oneSpare.slot(0) = ItemStack{};

    return addAgreesWithHasRoomFor(empty, ItemId::Coal, kMaxStack, 0) &&
           addAgreesWithHasRoomFor(empty, ItemId::Coal, kMaxStack * 40, 0) &&
           addAgreesWithHasRoomFor(fullOfCoal, ItemId::Coal, 1, 0) &&
           // Exactly the last item that fits, and the first that does not.
           addAgreesWithHasRoomFor(nearlyFull, ItemId::Coal,
                                   static_cast<int>(Inventory::size()), 0) &&
           addAgreesWithHasRoomFor(nearlyFull, ItemId::Coal,
                                   static_cast<int>(Inventory::size()) + 1, 0) &&
           addAgreesWithHasRoomFor(wornCoal, ItemId::Coal, 1, 0) &&
           addAgreesWithHasRoomFor(wornCoal, ItemId::Coal, 1, 5) &&
           // Two tools never merge however much room the count suggests.
           addAgreesWithHasRoomFor(tools, ItemId::IronPickaxe, 1, 0) &&
           addAgreesWithHasRoomFor(tools, ItemId::IronPickaxe, 1, 40) &&
           // A stowbox at two different damages is two different sets of
           // contents, and the one spare slot takes exactly one of them.
           addAgreesWithHasRoomFor(boxes, kStowbox, 1, 7) &&
           addAgreesWithHasRoomFor(oneSpare, kStowbox, 1, 7) &&
           addAgreesWithHasRoomFor(oneSpare, kStowbox, 2, 7);
}

static_assert(inventoryAnswersAgree(),
              "hasRoomFor must answer exactly what add would do, damage and all. Dropping the "
              "`stack.damage != damage` clause from slots::roomFor, or counting room in "
              "hasRoomFor by `stack.item == item` alone, fails on the worn-coal and stowbox "
              "shapes - which is the bug this pair has already had once");

/// A full set worn correctly, built by the same `helmet + i` derivation
/// `survival::fullSetDefence` uses - so if that offset is ever wrong, both go
/// wrong together and the cross-check below still catches it against the
/// reference's published totals rather than against itself. **`CLAUDE.md` #11:
/// an assert comparing one side of a derivation against itself proves nothing.**
constexpr Inventory wearingSet(ItemId helmet) {
    Inventory inventory;
    for (std::size_t i = 0; i < kArmourSlots; ++i) {
        inventory.armourAt(i) =
            ItemStack{static_cast<ItemId>(static_cast<int>(helmet) + static_cast<int>(i)), 1, 0};
    }
    return inventory;
}

static_assert(Inventory{}.armourSet().defence == 0 && Inventory{}.armourSet().toughness == 0.0f,
              "an empty inventory must arm nobody");

/// The claim that matters: what `Inventory` reports is what `Survival.hpp`'s
/// own table says a set is worth. Two independent readings of the same
/// reference numbers, one walking worn slots and one walking the item run.
static_assert(wearingSet(ItemId::DiamondHelmet).armourSet().defence ==
                      survival::fullSetDefence(ItemId::DiamondHelmet) &&
                  wearingSet(ItemId::DiamondHelmet).armourSet().toughness ==
                      survival::fullSetToughness(ItemId::DiamondHelmet) &&
                  wearingSet(ItemId::EmberiteHelmet).armourSet().defence ==
                      survival::fullSetDefence(ItemId::EmberiteHelmet) &&
                  wearingSet(ItemId::EmberiteHelmet).armourSet().toughness ==
                      survival::fullSetToughness(ItemId::EmberiteHelmet) &&
                  wearingSet(ItemId::LeatherHelmet).armourSet().defence ==
                      survival::fullSetDefence(ItemId::LeatherHelmet) &&
                  wearingSet(ItemId::IronHelmet).armourSet().defence ==
                      survival::fullSetDefence(ItemId::IronHelmet),
              "what the worn slots add up to must equal what the item table says the set is worth");

/// And pinned to the reference's own published totals, so the pair above cannot
/// both drift the same way.
static_assert(wearingSet(ItemId::DiamondHelmet).armourSet().defence == 20 &&
                  wearingSet(ItemId::DiamondHelmet).armourSet().toughness == 8.0f &&
                  wearingSet(ItemId::EmberiteHelmet).armourSet().toughness == 12.0f &&
                  wearingSet(ItemId::LeatherHelmet).armourSet().defence == 7,
              "full diamond is 20 points and 8 toughness, full Emberite 12 toughness, full "
              "leather 7 points");

/// **The negative twin, and it is the one a UI can actually cause**: armour in
/// the wrong slot arms nobody. Boots in the helmet slot are worth 1 point if
/// this test is dropped and 0 if it is kept.
constexpr Inventory bootsOnHead() {
    Inventory inventory;
    inventory.armour(ArmourSlot::Head) = ItemStack{ItemId::DiamondBoots, 1, 0};
    return inventory;
}
static_assert(bootsOnHead().armourSet().defence == 0 &&
                  armourDefence(ItemId::DiamondBoots) == 3,
              "a piece worn in the wrong slot must count for nothing - and the second clause is "
              "what proves the first is not simply reading a zero out of the table");

/// The turtle shell, which belongs to no set and so cannot be reached by the
/// `helmet + i` walk above.
constexpr Inventory turtleShell() {
    Inventory inventory;
    inventory.armour(ArmourSlot::Head) = ItemStack{ItemId::TurtleHelmet, 1, 0};
    return inventory;
}
static_assert(turtleShell().armourSet().defence == 2 &&
                  turtleShell().armourSet().toughness == 0.0f,
              "the turtle shell is two points and no toughness, and it is worn on the head "
              "despite sitting outside the armour run");

/// Wear reaches every worn piece, and reaches nothing else.
constexpr int wornDamageTotal(const Inventory& inventory) {
    int total = 0;
    for (std::size_t i = 0; i < kArmourSlots; ++i) {
        total += inventory.armourAt(i).damage;
    }
    return total;
}
constexpr Inventory afterOneBlow(ItemId helmet, int cost) {
    Inventory inventory = wearingSet(helmet);
    inventory.wearArmour(cost);
    return inventory;
}
static_assert(wornDamageTotal(afterOneBlow(ItemId::DiamondHelmet, 7)) == 7 * 4,
              "every worn piece pays the full cost of a blow - the cost is not divided among "
              "them, which would make a full set last four times too long");
static_assert(wornDamageTotal(afterOneBlow(ItemId::DiamondHelmet, 0)) == 0 &&
                  afterOneBlow(ItemId::DiamondHelmet, 0).armourSet().defence == 20,
              "a blow that cost nothing must not wear anything");
/// **This accepts exactly two values and rejects every other, which is what
/// lets the armour rows land in a separate file without a broken build in
/// between.** `0` is the world where `mining::toolProperties` still carries no
/// armour row, so the limit is zero and `wearArmour` never destroys - a full
/// diamond set is permanent free protection, which is a real and known bug.
/// `363` is the world where the rows have landed and the helmet carries the
/// reference's durability. Nothing else passes.
///
/// **Why this is a disjunction rather than the tripwire it replaced.** It was
/// written as `== 0` alone, deliberately, to fail the build the day the rows
/// arrived and so force whoever landed them to come here. That is a good
/// design when somebody can compile. It is a bad one tonight: the two edits
/// live in two files with two owners, **either half alone fails the build**,
/// and with no build available neither owner can discover which state the tree
/// is in. A tripwire that needs two files to change in one atomic step is a
/// coupling *documented*; accepting both legal states is that same coupling
/// *dissolved*, and dissolving beats documenting - the same reason
/// `kArmourSlots` above is derived from `ArmourSlot::None` instead of being
/// written as 4 and guarded by a note.
///
/// **It is NOT weakened to a range, which this file and `Mining.hpp` both
/// forbid.** A range would accept 300, or 1561 pasted across from the pickaxe.
/// This names two exact legal values and rejects all others, so the guard that
/// actually matters - the helmet's number being *right* - is fully intact. The
/// pickaxe clause is unchanged and still proves this reads a live table rather
/// than a default-constructed row.
///
/// **Collapse it to `== 363` in the first build that compiles the armour
/// rows**, where a failure is diagnosable, and add the assert this one has
/// always stood in for: a blow costing more than the limit must destroy the
/// piece and leave the slot empty. That is new `constexpr` code exercising a
/// `wearArmour` path that has never once run with a non-zero limit, which is
/// exactly why it is not being written blind here.
///
/// Hand-substituted for both reachable states, since it cannot be compiled:
/// today `(0 == 0 || 0 == 363) && 1561 == 1561` is `(true || false) && true`;
/// after the rows, `(false || true) && true`. A wrong helmet value gives
/// `(false || false) && true` and fires. **The parentheses are load-bearing** -
/// `&&` binds tighter than `||`, so without them the pickaxe control would
/// bind to the second disjunct alone and a wrong helmet value would pass.
///
/// **Falsified by** `toolProperties(ItemId::DiamondHelmet).durability` holding
/// anything but 0 or 363 - at which point the compiler tells you, which is the
/// whole job. The three gameplay preconditions for the rows were verified in
/// source on 2026-08-19: worn armour is dropped *and* cleared on death by the
/// `kArmourSlots` loop in `Main.cpp`'s death branch, and `respawnPlayer` zeroes
/// `player.armourWear` in `Player.cpp`. **Do not re-add either** - a second
/// writer for `armourWear` is worse than the bug it would be fixing.
static_assert((mining::toolProperties(ItemId::DiamondHelmet).durability == 0 ||
               mining::toolProperties(ItemId::DiamondHelmet).durability == 363) &&
                  mining::toolProperties(ItemId::DiamondPickaxe).durability == 1561,
              "the diamond helmet must carry either 0 (no armour rows in mining::toolProperties "
              "yet, so armour never wears out - a real and known bug) or the reference's 363 (the "
              "rows have landed). Any other value means a row was written wrong: check it against "
              "the reference rather than widening this assert. The pickaxe clause is the control "
              "proving this reads a real table; keep it. Once the rows are in and the build is "
              "green, collapse this to == 363 and add an assert that a blow above the limit "
              "destroys the piece");

/// **Equipping is a swap, and the destination is derived rather than trusted.**
/// Every clause below is a way the obvious implementation goes wrong: a move
/// that destroys the piece it replaces, a slot index taken on faith, or boots
/// accepted onto a head because the caller said so.
constexpr bool equipArmourBehaves() {
    Inventory inventory;
    inventory.slot(0) = ItemStack{ItemId::DiamondHelmet, 1, 0};
    inventory.slot(1) = ItemStack{ItemId::Coal, kMaxStack, 0};

    // Refusals first, and each must leave the inventory untouched.
    const bool refusesCoal = !inventory.equipArmour(1) && inventory.slot(1).item == ItemId::Coal;
    const bool refusesEmpty = !inventory.equipArmour(2);
    const bool refusesOffTheEnd = !inventory.equipArmour(Inventory::size());

    // The helmet lands on the head and leaves the carried slot empty.
    const bool worn = inventory.equipArmour(0) &&
                      inventory.armour(ArmourSlot::Head).item == ItemId::DiamondHelmet &&
                      inventory.slot(0).empty();
    // ...and the damage curve can now see it. Naked was 0, so this also proves
    // the set is read from the worn array rather than from storage.
    const bool counted = inventory.armourSet().defence == armourDefence(ItemId::DiamondHelmet) &&
                         inventory.armourSet().defence > 0;

    // Equipping over a worn piece hands the old one back rather than eating it.
    inventory.slot(3) = ItemStack{ItemId::IronHelmet, 1, 0};
    const bool swaps = inventory.equipArmour(3) &&
                       inventory.armour(ArmourSlot::Head).item == ItemId::IronHelmet &&
                       inventory.slot(3).item == ItemId::DiamondHelmet;

    // Boots go to the feet and leave the head alone, however they are offered.
    inventory.slot(4) = ItemStack{ItemId::DiamondBoots, 1, 0};
    const bool bootsToFeet = inventory.equipArmour(4) &&
                             inventory.armour(ArmourSlot::Feet).item == ItemId::DiamondBoots &&
                             inventory.armour(ArmourSlot::Head).item == ItemId::IronHelmet;

    return refusesCoal && refusesEmpty && refusesOffTheEnd && worn && counted && swaps &&
           bootsToFeet;
}

static_assert(equipArmourBehaves(),
              "equipping must swap rather than move, derive the destination from armourSlot "
              "rather than from the caller, refuse a non-armour item and an index off the end, "
              "and be visible to armourSet the moment it lands");

/// The negative twin. A wrong destination is the failure `armourSet` would then
/// silently absorb - it declines to count a mis-slotted piece, so boots forced
/// onto a head would read as defence 0 and look exactly like wearing nothing.
static_assert(armourSlot(ItemId::DiamondBoots) != ArmourSlot::Head &&
                  armourSlot(ItemId::DiamondHelmet) == ArmourSlot::Head &&
                  armourSlot(ItemId::Coal) == ArmourSlot::None,
              "armourSlot is the single owner of which piece goes where, and it must reject a "
              "non-armour item outright rather than defaulting it into the head slot");

} // namespace game