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

    /// Raw storage, for bulk operations such as saving and loading. One byte per
    /// block, so the array is exactly `kBlockCount` bytes.
    BlockId* data() { return m_blocks.data(); }
    const BlockId* data() const { return m_blocks.data(); }

private:
    static constexpr std::size_t index(int x, int y, int z) {
        return static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * kSize +
               static_cast<std::size_t>(y) * kSize * kSize;
    }

    /// Full sky, no block light.
    static constexpr std::uint8_t kDefaultLight = 0xF0;

    // Value-initialised, and Air is 0, so a fresh chunk is empty.
    std::array<BlockId, kBlockCount> m_blocks{};

    /// Doubles a chunk's memory. Kept alongside the blocks rather than
    /// recomputed per mesh because light crosses chunk boundaries, so it cannot
    /// be derived from one chunk's contents.
    std::array<std::uint8_t, kBlockCount> m_light{};
};

} // namespace game
