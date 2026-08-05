#include "world/WorldStore.hpp"

#include <engine/core/Log.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>

namespace game {
namespace {

/// Identifies the file and pins the format. A chunk saved by an older build
/// would otherwise be read as garbage blocks rather than rejected.
constexpr std::array<char, 4> kMagic{'V', 'X', 'C', 'H'};
constexpr std::array<char, 4> kPlayerMagic{'V', 'X', 'P', 'L'};
constexpr std::array<char, 4> kFurnaceMagic{'V', 'X', 'F', 'N'};
constexpr std::array<char, 4> kCreatureMagic{'V', 'X', 'C', 'R'};
constexpr std::uint32_t kFormatVersion = 1;

/// Versioned separately from chunks and the player, because it stores
/// `ItemStack`s and so has to be invalidated whenever those change shape. A
/// shared version would throw away every saved chunk for the same reason.
constexpr std::uint32_t kFurnaceVersion = 2;
constexpr std::uint32_t kCreatureVersion = 2;

/// Sanity bound on a file the game did not write this run. Far more furnaces
/// than anyone would place, and small enough that a corrupt length cannot ask
/// for an enormous allocation.
constexpr std::uint32_t kMaxFurnaces = 1u << 20;
/// Far more than the population cap could ever reach, and small enough that a
/// corrupt count cannot ask for a huge allocation.
constexpr std::uint32_t kMaxCreatures = 1u << 16;

// These records are written and read as raw bytes. If anything in `Furnace`
// ever gains a pointer, a string or a virtual, that stops being valid and this
// is where it will be caught rather than in a corrupt save.
static_assert(std::is_trivially_copyable_v<PlacedFurnace>,
              "PlacedFurnace is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<SavedCreature>,
              "SavedCreature is written as raw bytes and must stay trivially copyable");

struct Header {
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::uint32_t blockCount = 0;
};

} // namespace

WorldStore::WorldStore(std::filesystem::path root, std::uint32_t seed)
    : m_directory(std::move(root) / ("world_" + std::to_string(seed)) / "chunks"), m_seed(seed) {
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (error) {
        engine::logError("Could not create save directory " + m_directory.string() + ": " + error.message());
    }
}

std::filesystem::path WorldStore::pathFor(const ChunkCoord& coord) const {
    return m_directory / ("c" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + "_" +
                          std::to_string(coord.z) + ".chunk");
}

std::optional<Chunk> WorldStore::load(const ChunkCoord& coord) const {
    const std::filesystem::path path = pathFor(coord);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        engine::logWarn("Truncated chunk file, ignoring: " + path.string());
        return std::nullopt;
    }

    // Every field is checked rather than trusted: this is the one place the game
    // reads bytes it did not produce this run.
    if (header.magic != kMagic || header.version != kFormatVersion || header.seed != m_seed ||
        header.x != coord.x || header.y != coord.y || header.z != coord.z ||
        header.blockCount != Chunk::kBlockCount) {
        engine::logWarn("Chunk file does not match this world, ignoring: " + path.string());
        return std::nullopt;
    }

    Chunk chunk;
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(Chunk::kBlockCount));
    if (file.gcount() != static_cast<std::streamsize>(Chunk::kBlockCount)) {
        engine::logWarn("Truncated chunk data, ignoring: " + path.string());
        return std::nullopt;
    }

    return chunk;
}

void WorldStore::save(const ChunkCoord& coord, const Chunk& chunk) const {
    const std::filesystem::path path = pathFor(coord);
    const std::filesystem::path temporary = path.string() + ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open chunk file for writing: " + temporary.string());
            return;
        }

        Header header{};
        header.magic = kMagic;
        header.version = kFormatVersion;
        header.seed = m_seed;
        header.x = coord.x;
        header.y = coord.y;
        header.z = coord.z;
        header.blockCount = Chunk::kBlockCount;

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(chunk.data()),
                   static_cast<std::streamsize>(Chunk::kBlockCount));

        if (!file) {
            engine::logError("Failed writing chunk: " + temporary.string());
            return;
        }
    }

    // Rename is atomic on Windows and POSIX alike, so the real file is either
    // the old complete version or the new complete version, never a mixture.
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace chunk file " + path.string() + ": " + error.message());
        std::filesystem::remove(temporary, error);
    }
}

std::optional<SavedPlayer> WorldStore::loadPlayer() const {
    // Sits beside the chunks directory, not inside it, so a chunk sweep never
    // has to filter it out.
    const std::filesystem::path path = m_directory.parent_path() / "player.dat";

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    SavedPlayer player;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&player), sizeof(player));

    if (!file || magic != kPlayerMagic || version != kFormatVersion || seed != m_seed) {
        engine::logWarn("Player file does not match this world, ignoring: " + path.string());
        return std::nullopt;
    }

    return player;
}

void WorldStore::savePlayer(const SavedPlayer& player) const {
    const std::filesystem::path path = m_directory.parent_path() / "player.dat";
    const std::filesystem::path temporary = path.string() + ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open player file for writing: " + temporary.string());
            return;
        }

        file.write(kPlayerMagic.data(), kPlayerMagic.size());
        file.write(reinterpret_cast<const char*>(&kFormatVersion), sizeof(kFormatVersion));
        file.write(reinterpret_cast<const char*>(&m_seed), sizeof(m_seed));
        file.write(reinterpret_cast<const char*>(&player), sizeof(player));

        if (!file) {
            engine::logError("Failed writing player file: " + temporary.string());
            return;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace player file " + path.string() + ": " + error.message());
        std::filesystem::remove(temporary, error);
    }
}

std::vector<PlacedFurnace> WorldStore::loadFurnaces() const {
    const std::filesystem::path path = m_directory.parent_path() / "furnaces.dat";

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (!file || magic != kFurnaceMagic || version != kFurnaceVersion || seed != m_seed) {
        engine::logWarn("Furnace file does not match this world, ignoring: " + path.string());
        return {};
    }
    // A corrupt count must not be trusted into a reserve; the file is small and
    // bounded by how many furnaces a player could plausibly place.
    if (count > kMaxFurnaces) {
        engine::logWarn("Furnace file claims " + std::to_string(count) + " entries, ignoring: " + path.string());
        return {};
    }

    std::vector<PlacedFurnace> furnaces;
    furnaces.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PlacedFurnace placed;
        file.read(reinterpret_cast<char*>(&placed), sizeof(placed));
        if (!file) {
            engine::logWarn("Truncated furnace file, keeping what was read: " + path.string());
            break;
        }
        furnaces.push_back(placed);
    }
    return furnaces;
}

void WorldStore::saveFurnaces(const std::vector<PlacedFurnace>& furnaces) const {
    const std::filesystem::path path = m_directory.parent_path() / "furnaces.dat";

    // Nothing to keep: remove the file rather than leaving a stale one that
    // would restore furnaces the player has already broken.
    if (furnaces.empty()) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open furnace file for writing: " + temporary.string());
            return;
        }

        const auto count = static_cast<std::uint32_t>(furnaces.size());
        file.write(kFurnaceMagic.data(), kFurnaceMagic.size());
        file.write(reinterpret_cast<const char*>(&kFurnaceVersion), sizeof(kFurnaceVersion));
        file.write(reinterpret_cast<const char*>(&m_seed), sizeof(m_seed));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const PlacedFurnace& placed : furnaces) {
            file.write(reinterpret_cast<const char*>(&placed), sizeof(placed));
        }

        if (!file) {
            engine::logError("Failed writing furnace file: " + temporary.string());
            return;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace furnace file " + path.string() + ": " + error.message());
        std::filesystem::remove(temporary, error);
    }
}

std::vector<SavedCreature> WorldStore::loadCreatures() const {
    const std::filesystem::path path = m_directory.parent_path() / "creatures.dat";

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (!file || magic != kCreatureMagic || version != kCreatureVersion || seed != m_seed) {
        engine::logWarn("Creature file does not match this world, ignoring: " + path.string());
        return {};
    }
    if (count > kMaxCreatures) {
        engine::logWarn("Creature file claims " + std::to_string(count) + " entries, ignoring: " + path.string());
        return {};
    }

    std::vector<SavedCreature> creatures;
    creatures.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        SavedCreature saved;
        file.read(reinterpret_cast<char*>(&saved), sizeof(saved));
        if (!file) {
            engine::logWarn("Truncated creature file, keeping what was read: " + path.string());
            break;
        }
        creatures.push_back(saved);
    }
    return creatures;
}

void WorldStore::saveCreatures(const std::vector<SavedCreature>& creatures) const {
    const std::filesystem::path path = m_directory.parent_path() / "creatures.dat";

    // An empty population removes the file rather than leaving a stale one, the
    // same reasoning the furnaces use: a leftover would repopulate a world the
    // player has already cleared.
    if (creatures.empty()) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open creature file for writing: " + temporary.string());
            return;
        }

        const auto count = static_cast<std::uint32_t>(creatures.size());
        file.write(kCreatureMagic.data(), kCreatureMagic.size());
        file.write(reinterpret_cast<const char*>(&kCreatureVersion), sizeof(kCreatureVersion));
        file.write(reinterpret_cast<const char*>(&m_seed), sizeof(m_seed));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const SavedCreature& saved : creatures) {
            file.write(reinterpret_cast<const char*>(&saved), sizeof(saved));
        }

        if (!file) {
            engine::logError("Failed writing creature file: " + temporary.string());
            return;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace creature file " + path.string() + ": " + error.message());
        std::filesystem::remove(temporary, error);
    }
}

} // namespace game
