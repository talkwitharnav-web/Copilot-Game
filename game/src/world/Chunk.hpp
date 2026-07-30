#pragma once

#include "world/Block.hpp"

#include <array>
#include <cstddef>

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

private:
    static constexpr std::size_t index(int x, int y, int z) {
        return static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * kSize +
               static_cast<std::size_t>(y) * kSize * kSize;
    }

    // Value-initialised, and Air is 0, so a fresh chunk is empty.
    std::array<BlockId, kBlockCount> m_blocks{};
};

} // namespace game
