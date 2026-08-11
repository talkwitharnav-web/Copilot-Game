#pragma once

#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/Village.hpp"

#include <cstdint>

namespace game {

/// Places trees and anything else that spans more than one block.
///
/// The whole problem here is that a structure straddles chunk boundaries while
/// generation has to stay a **pure function of `(seed, chunkCoord)`** — a chunk
/// may not ask its neighbours what they decided. So nothing is ever "pushed"
/// into a neighbouring chunk. Instead each chunk works out which structures
/// could reach it, rebuilds every one of them from the seed, and keeps only the
/// blocks that land inside its own bounds. Two chunks generating the same tree
/// independently reach the same answer, which is what makes it seamless.
///
/// Candidates live on a fixed grid so the search is bounded: only cells within
/// `kStructureReach` of the chunk can possibly matter.
namespace structures {

/// Side of one placement cell, in blocks. At most one structure per cell, which
/// is what stops trees growing out of each other.
constexpr int kCellSize = 8;

/// Furthest any structure extends horizontally from its origin. Anything larger
/// than this would be silently clipped at chunk borders.
/// How far a structure may reach outside the cell it belongs to.
///
/// A branching oak throws clusters about `height/4 * 1.33` sideways and each
/// one is a blob two blocks wide, so 6 rather than the 3 a fixed canopy needed.
/// Too small and a tree straddling a chunk border is built by one chunk and not
/// the other, which is a seam you can walk up to.
constexpr int kReach = 6;

/// Writes every structure overlapping this chunk into it.
///
/// `villages` is passed in rather than looked up because the caller has already
/// solved it, and a tree cell asking for itself would rebuild the same village
/// plan once per candidate cell.
void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord,
                  const village::Nearby& villages);

} // namespace structures
} // namespace game
