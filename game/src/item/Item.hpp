#pragma once

#include "world/Block.hpp"

#include <cstdint>

namespace game {

/// A thing that can sit in an inventory.
///
/// Deliberately not the same type as `BlockId`. Every placeable block has an
/// item form, but the reverse will not hold: a pickaxe is an item that is never
/// a block. Keeping them distinct now avoids unpicking it during crafting.
///
/// Block items **share the block's numbering**, so there is one list to
/// maintain rather than two that can silently drift apart. Anything that is not
/// a block starts above `kFirstToolItem`.
enum class ItemId : std::uint16_t {
    None = 0,

    /// Everything below this is a block item whose value equals its `BlockId`.
    kFirstToolItem = 256,

    Stick = kFirstToolItem,
};

/// Layer in the block texture array, for anything that is not a block.
///
/// Block items draw as a little cube built from their block textures; these
/// have nothing to build from and draw as a flat sprite instead. Negative means
/// this item has no sprite of its own.
constexpr int itemTextureLayer(ItemId item) {
    switch (item) {
    case ItemId::Stick:
        return static_cast<int>(TextureLayer::Stick);
    default:
        return -1;
    }
}

constexpr const char* itemName(ItemId item) {
    switch (item) {
    case ItemId::Stick:
        return "Stick";
    default:
        return "";
    }
}

constexpr ItemId itemForBlock(BlockId block) {
    return static_cast<ItemId>(block);
}

constexpr bool isBlockItem(ItemId item) {
    return item != ItemId::None && static_cast<std::uint16_t>(item) < static_cast<std::uint16_t>(ItemId::kFirstToolItem);
}

/// Only meaningful when `isBlockItem` holds.
constexpr BlockId blockForItem(ItemId item) {
    return static_cast<BlockId>(item);
}

/// How many of one item fit in a single slot.
constexpr int kMaxStack = 64;

/// An item and how many of it. A count of zero means the slot is empty, and the
/// item is then meaningless.
struct ItemStack {
    ItemId item = ItemId::None;
    int count = 0;

    constexpr bool empty() const { return count <= 0 || item == ItemId::None; }
    constexpr int space() const { return empty() ? kMaxStack : kMaxStack - count; }
};

/// What a block turns into when broken.
///
/// Usually itself, but not always: stone yields cobblestone, and grass yields
/// plain dirt. Stairs and slabs collapse to their upright form so an inventory
/// does not fill with eight orientations of the same thing.
constexpr ItemId dropForBlock(BlockId block) {
    if (isStairs(block)) {
        return itemForBlock(BlockId::CobbleStairs0);
    }
    if (isSlab(block)) {
        return itemForBlock(BlockId::StoneSlab);
    }
    if (isWater(block)) {
        return ItemId::None;
    }
    switch (block) {
    case BlockId::Air:
        return ItemId::None;
    case BlockId::Stone:
        return itemForBlock(BlockId::Cobblestone);
    case BlockId::Grass:
        return itemForBlock(BlockId::Dirt);
    default:
        return itemForBlock(block);
    }
}

} // namespace game
