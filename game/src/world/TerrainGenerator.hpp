#pragma once

#include "world/Chunk.hpp"

#include <cstdint>

namespace game {

/// Position of a chunk in the world, measured in chunks rather than blocks.
struct ChunkCoord {
    int x = 0;
    int y = 0;
    int z = 0;
};

/// Builds a chunk from nothing but the seed and where the chunk sits.
///
/// **This function must stay pure.** It may not read neighbouring chunks, global
/// state, the clock, or anything else. Two consequences follow directly, and
/// both are load-bearing:
///   - the same seed always rebuilds the identical world, so a save file needs
///     one integer instead of every block;
///   - any number of threads can generate any chunks in any order and get the
///     same result, which is what makes the M12 job system a migration rather
///     than a rewrite.
Chunk generateChunk(std::uint32_t seed, ChunkCoord coord);

/// Surface height in blocks at a world column. Exposed so the game can place the
/// player, and so terrain shape can be reasoned about without meshing anything.
int surfaceHeightAt(std::uint32_t seed, int worldX, int worldZ);

} // namespace game
