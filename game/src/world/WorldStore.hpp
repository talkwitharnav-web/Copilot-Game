#pragma once

#include "world/Chunk.hpp"
#include "world/Furnace.hpp"
#include "world/Chest.hpp"
#include "world/TerrainGenerator.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace game {

/// Where the player was when they last quit, so they resume rather than respawn.
///
/// Widened at M21 to carry survival state. **`kFormatVersion` was bumped with
/// it**, so an older file is refused and the player resumes at full health at
/// spawn rather than being read as garbage - a save record is a stated list of
/// fields, and growing one silently is how a struct starts lying about itself.
struct SavedPlayer {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
};

/// A furnace and the block it belongs to.
struct PlacedFurnace {
    glm::ivec3 position{0};
    Furnace furnace;
};

/// A chest and the block it belongs to.
struct PlacedChest {
    glm::ivec3 position{0};
    Chest chest;
};

/// One creature as it goes to disk.
///
/// Deliberately its own record rather than the live `Creature`. A save format
/// has to be an explicit list of fields, or it changes silently every time the
/// struct gains one - and everything transient (gait, timers, what it was
/// thinking) is exactly what a reload should throw away.
struct SavedCreature {
    std::uint8_t kind = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    std::int32_t health = 0;
    float scale = 1.0f;
    /// Whether a Bramble is carrying a charge. Kept because it is the one
    /// per-individual property that is not transient and not derivable - a
    /// charged one that reloaded as ordinary would look like the charge simply
    /// wearing off.
    std::uint8_t charged = 0;
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

    /// Block entities, in one file for the whole world rather than split across
    /// the chunk files.
    ///
    /// Deliberate: a chunk is loaded and saved by worker threads, and threading
    /// block-entity data through that path buys a whole class of ordering bugs
    /// for something a player only ever has a handful of. The cost is that these
    /// are written when the world is saved rather than when a chunk unloads.
    std::vector<PlacedFurnace> loadFurnaces() const;
    void saveFurnaces(const std::vector<PlacedFurnace>& furnaces) const;

    std::vector<PlacedChest> loadChests() const;
    void saveChests(const std::vector<PlacedChest>& chests) const;

    std::vector<SavedCreature> loadCreatures() const;
    void saveCreatures(const std::vector<SavedCreature>& creatures) const;

    const std::filesystem::path& directory() const { return m_directory; }
    std::uint32_t seed() const { return m_seed; }

private:
    std::filesystem::path pathFor(const ChunkCoord& coord) const;

    std::filesystem::path m_directory;
    std::uint32_t m_seed;
};

} // namespace game
