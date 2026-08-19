// fx-conserve: does a held drag conserve items when something else writes the
// slots it is sweeping?
//
// Transcribes Main.cpp's drag block twice - OLD (snapshot + rewindDrag, what is
// in the tree) and NEW (record what the drag deposited and take back only that)
// - over the REAL slots::distribute / merge / leftClick / rightClick and the
// REAL Inventory::add, then runs the three interference paths that findings
// 728 / 729 / 730 name: the furnace tick, the hopper pass and item pickup.
//
// Throwaway. Delete when the round is over.

#include "item/Inventory.hpp"
#include "item/Item.hpp"
#include "item/SlotOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace game;

namespace {

// A slot the drag can address. Region is irrelevant to the arithmetic - what
// matters is that the pointer is live and something else may write through it.
struct World {
    std::vector<ItemStack> slots; // every slot the screen shows
    ItemStack cursor;
    // The hopper below, which the hopper pass drains into.
    ItemStack hopper;
    // Items that arrived from outside (a pickup), booked negative so the
    // before/after totals stay comparable.
    int arrived = 0;
    // What smelting has eaten, per item, each worth one product.
    std::vector<std::pair<ItemId, int>> smelted;

    void bookSmelt(ItemId item) {
        for (auto& entry : smelted) {
            if (entry.first == item) {
                ++entry.second;
                return;
            }
        }
        smelted.emplace_back(item, 1);
    }
    int smeltedOf(ItemId item) const {
        for (const auto& entry : smelted) {
            if (entry.first == item) {
                return entry.second;
            }
        }
        return 0;
    }
};

int countOf(const std::vector<ItemStack>& list, ItemId item) {
    int total = 0;
    for (const ItemStack& stack : list) {
        if (stack.item == item) {
            total += stack.count;
        }
    }
    return total;
}

int totalOf(const World& world, ItemId item) {
    int total = countOf(world.slots, item);
    if (world.cursor.item == item) {
        total += world.cursor.count;
    }
    if (world.hopper.item == item) {
        total += world.hopper.count;
    }
    return total + world.smeltedOf(item);
}

// ---------------------------------------------------------------- OLD design

struct OldDrag {
    bool armed = false;
    bool right = false;
    ItemStack originalCursor;
    std::vector<std::pair<std::size_t, ItemStack>> swept; // slot index + snapshot

    void rewind(World& world) {
        for (auto& entry : swept) {
            world.slots[entry.first] = entry.second;
        }
        world.cursor = originalCursor;
    }

    void apply(World& world) {
        rewind(world);
        std::vector<ItemStack*> targets;
        for (auto& entry : swept) {
            targets.push_back(&world.slots[entry.first]);
        }
        slots::distribute(targets, world.cursor, right);
    }

    void press(World& world, std::size_t index, bool rightButton) {
        armed = true;
        right = rightButton;
        originalCursor = world.cursor;
        swept.clear();
        swept.emplace_back(index, world.slots[index]);
        apply(world);
    }

    void sweepInto(World& world, std::size_t index) {
        for (const auto& entry : swept) {
            if (entry.first == index) {
                return;
            }
        }
        rewind(world);
        swept.emplace_back(index, world.slots[index]);
        apply(world);
    }

    void release(World& world) {
        if (swept.size() < 2) {
            rewind(world);
            if (!swept.empty()) {
                if (right) {
                    slots::rightClick(world.slots[swept.front().first], world.cursor);
                } else {
                    slots::leftClick(world.slots[swept.front().first], world.cursor);
                }
            }
        }
        armed = false;
        swept.clear();
    }
};

// ---------------------------------------------------------------- NEW design

struct Deposit {
    std::size_t index = 0;
    ItemId item = ItemId::None;
    int damage = 0;
    int placed = 0;
};

struct NewDrag {
    bool armed = false;
    bool right = false;
    std::vector<Deposit> deposits;

    void rewind(World& world) {
        for (Deposit& deposit : deposits) {
            if (deposit.placed <= 0) {
                continue;
            }
            ItemStack& stack = world.slots[deposit.index];
            const int limit = std::min(deposit.placed, stack.count);
            if (limit > 0 && stack.item == deposit.item && stack.damage == deposit.damage) {
                deposit.placed -= slots::merge(world.cursor, stack, limit);
            } else {
                deposit.placed = 0;
            }
        }
    }

    void apply(World& world) {
        rewind(world);
        std::vector<ItemStack*> targets;
        std::vector<int> before;
        for (Deposit& deposit : deposits) {
            targets.push_back(&world.slots[deposit.index]);
            before.push_back(world.slots[deposit.index].count);
        }
        const ItemStack spreading = world.cursor;
        slots::distribute(targets, world.cursor, right);
        for (std::size_t i = 0; i < deposits.size(); ++i) {
            const int placed = std::max(0, targets[i]->count - before[i]);
            deposits[i].placed = placed;
            if (placed > 0) {
                deposits[i].item = spreading.item;
                deposits[i].damage = spreading.damage;
            }
        }
    }

    void press(World& world, std::size_t index, bool rightButton) {
        armed = true;
        right = rightButton;
        deposits.clear();
        deposits.push_back(Deposit{index, ItemId::None, 0, 0});
        apply(world);
    }

    void sweepInto(World& world, std::size_t index) {
        for (const Deposit& deposit : deposits) {
            if (deposit.index == index) {
                return;
            }
        }
        rewind(world);
        deposits.push_back(Deposit{index, ItemId::None, 0, 0});
        apply(world);
    }

    void release(World& world) {
        if (deposits.size() < 2) {
            rewind(world);
            if (!deposits.empty()) {
                if (right) {
                    slots::rightClick(world.slots[deposits.front().index], world.cursor);
                } else {
                    slots::leftClick(world.slots[deposits.front().index], world.cursor);
                }
            }
        }
        armed = false;
        deposits.clear();
    }
};

// ------------------------------------------------------------- interference

// The furnace tick: one input consumed, one product made.
void smeltOnce(World& world, std::size_t inputSlot) {
    ItemStack& input = world.slots[inputSlot];
    if (input.empty()) {
        return;
    }
    world.bookSmelt(input.item);
    if (--input.count <= 0) {
        input = ItemStack{};
    }
}

// The hopper pass: one item out of the watched slot into the hopper below.
void hopperPull(World& world, std::size_t from) {
    slots::moveOne(world.hopper, world.slots[from]);
}

// ------------------------------------------------------------------ reports

int failures = 0;

void check(const char* what, int got, int want) {
    const bool ok = got == want;
    if (!ok) {
        ++failures;
    }
    std::printf("  %-52s %4d  (want %4d)  %s\n", what, got, want, ok ? "ok" : "*** FAIL");
}

// -------------------------------------------------------------- scenario 728

template <typename Drag>
void furnaceCase(const char* label) {
    // Cursor holds 64 raw iron, the furnace input holds 1. Press and hold on
    // the input, two smelts complete, release.
    World world;
    world.slots.assign(2, ItemStack{});
    world.slots[0] = ItemStack{ItemId::RawIron, 1, 0};
    world.cursor = ItemStack{ItemId::RawIron, 64, 0};

    Drag drag;
    drag.press(world, 0, false);
    smeltOnce(world, 0);
    smeltOnce(world, 0);
    drag.release(world);

    std::printf("%s 728 furnace (65 raw iron in, 2 smelts)\n", label);
    check("raw iron accounted for", totalOf(world, ItemId::RawIron), 65);
    std::printf("       input %d, cursor %d, ingots %d\n", world.slots[0].count, world.cursor.count,
                world.smeltedOf(ItemId::RawIron));
}

// -------------------------------------------------------------- scenario 729

template <typename Drag>
void hopperCase(const char* label) {
    // 32 diamonds in the chest slot, 8 on the cursor, a hopper pulling 20.
    World world;
    world.slots.assign(2, ItemStack{});
    world.slots[0] = ItemStack{ItemId::Diamond, 32, 0};
    world.cursor = ItemStack{ItemId::Diamond, 8, 0};

    Drag drag;
    drag.press(world, 0, false);
    for (int tick = 0; tick < 20; ++tick) {
        hopperPull(world, 0);
    }
    drag.release(world);

    std::printf("%s 729 hopper (32 in the chest, 8 on the cursor, 20 pulled)\n", label);
    check("diamonds accounted for", totalOf(world, ItemId::Diamond), 40);
    std::printf("       chest %d, hopper %d, cursor %d\n", world.slots[0].count, world.hopper.count,
                world.cursor.count);
}

// -------------------------------------------------------------- scenario 730

template <typename Drag>
void pickupCase(const char* label) {
    // 32 coal in the slot, 16 on the cursor, 5 picked up off the floor - and
    // the pickup goes through the real Inventory::add, which tops up the
    // matching stack first, so it lands in the very slot the drag swept.
    Inventory bag;
    bag.add(ItemId::Coal, 32);

    World world;
    world.slots.assign(1, ItemStack{});
    world.slots[0] = bag.slot(0);
    world.cursor = ItemStack{ItemId::Coal, 16, 0};

    Drag drag;
    drag.press(world, 0, false);
    bag.slot(0) = world.slots[0];
    bag.add(ItemId::Coal, 5);
    world.slots[0] = bag.slot(0);
    int strayed = 0;
    for (std::size_t i = 1; i < kInventorySlots; ++i) {
        if (bag.slot(i).item == ItemId::Coal) {
            strayed += bag.slot(i).count;
        }
    }
    drag.release(world);

    std::printf("%s 730 pickup (32 in the slot, 16 on the cursor, 5 off the floor)\n", label);
    check("coal accounted for", world.slots[0].count + world.cursor.count + strayed, 53);
    std::printf("       slot %d, cursor %d, elsewhere in the bag %d\n", world.slots[0].count,
                world.cursor.count, strayed);
}

// ------------------------------------------------------------ randomised run

template <typename Drag>
void soak(const char* label, unsigned seed, int rounds) {
    std::mt19937 rng{seed};
    int worst = 0;
    int broken = 0;
    for (int round = 0; round < rounds; ++round) {
        World world;
        const std::size_t count = 2 + (rng() % 6);
        world.slots.assign(count, ItemStack{});
        const ItemId watched = ItemId::Coal;
        for (ItemStack& slot : world.slots) {
            const unsigned roll = rng() % 3;
            if (roll == 0) {
                slot = ItemStack{watched, 1 + static_cast<int>(rng() % 40), 0};
            } else if (roll == 1) {
                slot = ItemStack{ItemId::Diamond, 1 + static_cast<int>(rng() % 10), 0};
            }
        }
        world.cursor = ItemStack{watched, 1 + static_cast<int>(rng() % 64), 0};
        const int before = totalOf(world, watched);

        Drag drag;
        drag.press(world, rng() % count, (rng() % 2) == 0);
        const int steps = static_cast<int>(rng() % 8);
        for (int step = 0; step < steps; ++step) {
            switch (rng() % 4) {
            case 0:
                drag.sweepInto(world, rng() % count);
                break;
            case 1:
                smeltOnce(world, rng() % count);
                break;
            case 2:
                hopperPull(world, rng() % count);
                break;
            default: {
                // Pickup: top up a matching stack, the way Inventory::add does,
                // and book the item as having arrived from outside.
                for (ItemStack& slot : world.slots) {
                    if (slot.item == watched && slots::roomFor(slot, watched, 0) > 0) {
                        ++slot.count;
                        ++world.arrived;
                        break;
                    }
                }
                break;
            }
            }
        }
        drag.release(world);

        const int after = totalOf(world, watched) - world.arrived;
        if (after != before) {
            ++broken;
            worst = std::max(worst, std::abs(after - before));
        }
    }
    std::printf("%s %d rounds, %d imbalanced, worst drift %d\n", label, rounds, broken, worst);
    if (broken != 0) {
        ++failures;
    }
}

} // namespace

int main() {
    std::printf("=== OLD (snapshot rewind, what is in the tree today) ===\n");
    furnaceCase<OldDrag>("[old]");
    hopperCase<OldDrag>("[old]");
    pickupCase<OldDrag>("[old]");
    soak<OldDrag>("[old] soak:", 20260819u, 20000);

    const int oldFailures = failures;
    failures = 0;

    std::printf("\n=== NEW (take back only what the drag deposited) ===\n");
    furnaceCase<NewDrag>("[new]");
    hopperCase<NewDrag>("[new]");
    pickupCase<NewDrag>("[new]");
    soak<NewDrag>("[new] soak:", 20260819u, 20000);

    std::printf("\nold failures %d, new failures %d\n", oldFailures, failures);
    return failures == 0 ? 0 : 1;
}
