#include "world/WorldStore.hpp"

#include <engine/core/Log.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <string>

namespace game {
namespace {

/// Identifies the file and pins the format. A chunk saved by an older build
/// would otherwise be read as garbage blocks rather than rejected.
constexpr std::array<char, 4> kMagic{'V', 'X', 'C', 'H'};
constexpr std::array<char, 4> kPlayerMagic{'V', 'X', 'P', 'L'};
constexpr std::uint32_t kFormatVersion = 1;

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

} // namespace game
