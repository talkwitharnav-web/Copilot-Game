#pragma once

#include "world/Chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace game {

/// How tall the world is, in chunks. Terrain never reaches the top, so the
/// upper chunks exist purely as building room.
///
/// It lives here rather than on `World` because generation is what decides
/// where the ceiling is — the density function's top slide is quoted against
/// it, and a second copy would put the slide and the world edge in different
/// places.
constexpr int kWorldHeightChunks = 3;

/// Floor division, correct for negative coordinates. Plain integer division
/// truncates toward zero, which puts blocks at -1 and 0 in the same cell.
constexpr int floorDivInt(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

/// Position of a chunk in the world, measured in chunks rather than blocks.
struct ChunkCoord {
    int x = 0;
    int y = 0;
    int z = 0;

    friend bool operator==(const ChunkCoord& a, const ChunkCoord& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
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

/// Whether a cave has cut the very top block of a column, so nothing may be
/// planted on it.
///
/// The reference never needs to ask: its features read the `OCEAN_FLOOR`
/// heightmap, which is the topmost *solid* block scanned down from the sky, so
/// a void can never be chosen. Ours plants against the uncarved height, which
/// is why a tree over a cave mouth was left standing in the air with its
/// rooting dirt block beside it.
bool surfaceCarvedAt(std::uint32_t seed, int worldX, int worldZ);

} // namespace game

template <>
struct std::hash<game::ChunkCoord> {
    std::size_t operator()(const game::ChunkCoord& c) const noexcept {
        // Large odd multipliers keep neighbouring chunks, which differ by one on
        // a single axis, from landing in the same bucket.
        std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(c.x)) * 0x9E3779B1u;
        h ^= static_cast<std::size_t>(static_cast<std::uint32_t>(c.y)) * 0x85EBCA77u;
        h ^= static_cast<std::size_t>(static_cast<std::uint32_t>(c.z)) * 0xC2B2AE3Du;
        return h ^ (h >> 16);
    }
};
