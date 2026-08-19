#pragma once

#include "world/Block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// A fixed cube of blocks.
///
/// Storage is one flat contiguous array rather than nested containers: it keeps
/// the whole chunk in a predictable block of memory, which is good for the CPU
/// cache now and a prerequisite for handing a chunk to a worker thread later.
class Chunk {
public:
    static constexpr int kSize = 32;
    static constexpr int kBlockCount = kSize * kSize * kSize;

    static constexpr bool contains(int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0 && x < kSize && y < kSize && z < kSize;
    }

    /// Reads outside the chunk return Air, so the mesher can ask about a
    /// neighbour without bounds-checking every query. At M6 this is where real
    /// neighbouring-chunk data gets supplied instead.
    BlockId at(int x, int y, int z) const {
        return contains(x, y, z) ? m_blocks[index(x, y, z)] : BlockId::Air;
    }

    void set(int x, int y, int z, BlockId id) {
        if (contains(x, y, z)) {
            m_blocks[index(x, y, z)] = id;
        }
    }

    /// Sky and block light packed one byte per block: sky in the high nibble,
    /// block in the low one. Outside the chunk reads as fully lit sky and no
    /// block light, which is the sane default for an unloaded neighbour.
    std::uint8_t lightAt(int x, int y, int z) const {
        return contains(x, y, z) ? m_light[index(x, y, z)] : kDefaultLight;
    }

    int skyLightAt(int x, int y, int z) const { return lightAt(x, y, z) >> 4; }
    int blockLightAt(int x, int y, int z) const { return lightAt(x, y, z) & 0x0F; }

    void setSkyLight(int x, int y, int z, int level) {
        if (contains(x, y, z)) {
            std::uint8_t& cell = m_light[index(x, y, z)];
            cell = static_cast<std::uint8_t>((cell & 0x0F) | (level << 4));
        }
    }

    void setBlockLight(int x, int y, int z, int level) {
        if (contains(x, y, z)) {
            std::uint8_t& cell = m_light[index(x, y, z)];
            cell = static_cast<std::uint8_t>((cell & 0xF0) | level);
        }
    }

    std::uint8_t* lightData() { return m_light.data(); }
    const std::uint8_t* lightData() const { return m_light.data(); }

    /// Whether this cell also holds water.
    ///
    /// **The reference's `waterlogged` blockstate, as one bit per cell.** A
    /// seagrass in the sea is not "seagrass instead of water", it is both at
    /// once - and without somewhere to record that, placing the plant deletes
    /// the water and punches a hole in the ocean. It cannot live in `BlockId`:
    /// that is one byte written straight to disk and every value is spoken for.
    ///
    /// Outside the chunk reads false, which is the right answer for an unloaded
    /// neighbour for the same reason `at` reads Air.
    bool waterloggedAt(int x, int y, int z) const {
        if (!contains(x, y, z)) {
            return false;
        }
        const std::size_t bit = index(x, y, z);
        return (m_waterlogged[bit >> 3] & (1u << (bit & 7u))) != 0u;
    }

    void setWaterlogged(int x, int y, int z, bool on) {
        if (!contains(x, y, z)) {
            return;
        }
        const std::size_t bit = index(x, y, z);
        const auto mask = static_cast<std::uint8_t>(1u << (bit & 7u));
        if (on) {
            m_waterlogged[bit >> 3] |= mask;
        } else {
            m_waterlogged[bit >> 3] &= static_cast<std::uint8_t>(~mask);
        }
    }

    static constexpr std::size_t kWaterloggedBytes = kBlockCount / 8;
    std::uint8_t* waterloggedData() { return m_waterlogged.data(); }
    const std::uint8_t* waterloggedData() const { return m_waterlogged.data(); }

    /// **The reference's one-bit blockstates, and what it means is decided by
    /// the block sitting in the cell.** Exactly like `waterlogged`, it cannot
    /// live in `BlockId`: that is written straight to disk and every value is
    /// spoken for, and both of the states below are *modifiers* on an existing
    /// id rather than a new block.
    ///
    /// Two readers, and only two, both in `World`:
    ///
    ///   leaves  - Bedrock's `persistent_bit`, "If the block persists
    ///             regardless of having no wood nearby" (minecraft.wiki
    ///             [[Leaves]], Block states, BE table). Set means player-placed
    ///             and immune to decay; clear means it grew there.
    ///   sapling - Bedrock's `age_bit`, "Specifies the sapling's growth stage"
    ///             (minecraft.wiki [[Sapling]], Block states, BE table). One
    ///             bit is the whole state there: stage 0, then stage 1, then
    ///             the tree.
    ///
    /// **Nothing else may read it, and the two can never be confused**, because
    /// `World::setBlock` rewrites this bit on every single write - so a cell
    /// that was a leaf and becomes a sapling starts from a cleared bit rather
    /// than inheriting a meaning from the block that left. That rewrite is what
    /// makes one array safe for two states; without it this would be the
    /// "one number meaning two things" bug in its purest form.
    ///
    /// Outside the chunk reads false: an unloaded neighbour is not a
    /// player-placed leaf and is not a half-grown sapling, which is the same
    /// reasoning `waterloggedAt` uses.
    bool stateBitAt(int x, int y, int z) const {
        if (!contains(x, y, z)) {
            return false;
        }
        const std::size_t bit = index(x, y, z);
        return (m_stateBits[bit >> 3] & (1u << (bit & 7u))) != 0u;
    }

    void setStateBit(int x, int y, int z, bool on) {
        if (!contains(x, y, z)) {
            return;
        }
        const std::size_t bit = index(x, y, z);
        const auto mask = static_cast<std::uint8_t>(1u << (bit & 7u));
        if (on) {
            m_stateBits[bit >> 3] |= mask;
        } else {
            m_stateBits[bit >> 3] &= static_cast<std::uint8_t>(~mask);
        }
    }

    static constexpr std::size_t kStateBitBytes = kBlockCount / 8;
    std::uint8_t* stateBitData() { return m_stateBits.data(); }
    const std::uint8_t* stateBitData() const { return m_stateBits.data(); }

    /// Raw storage, for bulk operations such as saving and loading. **Two bytes
    /// per block since the id was widened**, so use `kBlockBytes` rather than
    /// `kBlockCount` for anything measured in bytes.
    static constexpr std::size_t kBlockBytes = kBlockCount * sizeof(BlockId);
    BlockId* data() { return m_blocks.data(); }
    const BlockId* data() const { return m_blocks.data(); }

    /// Where a cell sits in that raw storage.
    ///
    /// **Public because a second file now copies whole rows out of a chunk**,
    /// and the one thing worse than exposing this is a second copy of the
    /// layout. X is the fastest-moving axis, so a run along X is contiguous -
    /// which is the property the bulk copy depends on.
    static constexpr std::size_t cellIndex(int x, int y, int z) {
        return static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * kSize +
               static_cast<std::size_t>(y) * kSize * kSize;
    }

private:
    static constexpr std::size_t index(int x, int y, int z) { return cellIndex(x, y, z); }

    /// Full sky, no block light.
    static constexpr std::uint8_t kDefaultLight = 0xF0;

    // Value-initialised, and Air is 0, so a fresh chunk is empty.
    std::array<BlockId, kBlockCount> m_blocks{};

    /// Half a chunk's block storage. Kept alongside the blocks rather than
    /// recomputed per mesh because light crosses chunk boundaries, so it cannot
    /// be derived from one chunk's contents.
    std::array<std::uint8_t, kBlockCount> m_light{};

    /// One bit per cell, so 4 KB against the block array's 32 KB.
    std::array<std::uint8_t, kWaterloggedBytes> m_waterlogged{};

    /// The same again, for the single blockstate bit described by
    /// `stateBitAt`. Value-initialised, and clear is the natural answer for
    /// both meanings - a generated leaf decays and a freshly planted sapling
    /// is at stage 0 - so a chunk that has never been touched is already right.
    std::array<std::uint8_t, kStateBitBytes> m_stateBits{};
};

} // namespace game
