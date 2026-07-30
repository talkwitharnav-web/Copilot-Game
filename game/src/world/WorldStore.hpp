#pragma once

#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace game {

/// Where the player was when they last quit, so they resume rather than respawn.
struct SavedPlayer {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// Stores only the chunks the player actually changed.
///
/// Generation is a pure function of `(seed, chunkCoord)`, so an untouched chunk
/// is already perfectly reproducible and writing it down would be redundant. A
/// save is therefore the *difference* between the generated world and the played
/// one, which keeps it small no matter how far the player travels.
///
/// One file per chunk. Crude, but it makes a partial write damage exactly one
/// chunk instead of the whole world, and it needs no index to stay consistent.
/// A packed region format belongs with the rest of the M13 optimisation work, if
/// file count ever becomes the problem.
class WorldStore {
public:
    /// `root` is created if missing. Worlds with different seeds are kept apart,
    /// because a saved chunk is only meaningful against the terrain it was
    /// edited from.
    WorldStore(std::filesystem::path root, std::uint32_t seed);

    /// Returns the stored chunk, or nothing if this chunk was never modified.
    std::optional<Chunk> load(const ChunkCoord& coord) const;

    /// Overwrites any previous copy. Writes to a temporary file and renames it,
    /// so a crash mid-write cannot leave a half-written chunk behind.
    void save(const ChunkCoord& coord, const Chunk& chunk) const;

    /// Nothing on a world that has never been played.
    std::optional<SavedPlayer> loadPlayer() const;
    void savePlayer(const SavedPlayer& player) const;

    const std::filesystem::path& directory() const { return m_directory; }
    std::uint32_t seed() const { return m_seed; }

private:
    std::filesystem::path pathFor(const ChunkCoord& coord) const;

    std::filesystem::path m_directory;
    std::uint32_t m_seed;
};

} // namespace game
