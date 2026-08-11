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
constexpr std::array<char, 4> kChestMagic{'V', 'X', 'C', 'T'};
constexpr std::array<char, 4> kStowboxMagic{'V', 'X', 'S', 'B'};
constexpr std::array<char, 4> kCreatureMagic{'V', 'X', 'C', 'R'};
/// Versions `player.dat` alone, and is deliberately **not** the chunk version:
/// bumping that one regenerates stale terrain, and doing so must never cost the
/// player their inventory or position. Bumped to 2 at M21, when the record grew
/// health and hunger.
constexpr std::uint32_t kFormatVersion = 2;

/// Chunks carry their own version, separate from the player file's.
///
/// **Bumped to 2 when waterlogging arrived**, and to 3 when `BlockId` was
/// widened to sixteen bits. Version 2 was read and upgraded in place for a
/// while, because the id *numbers* did not change when the type did.
///
/// **That upgrade path is gone, and removing it is what fixed a real bug.** A
/// playtest found water hanging in mid-air; a probe put it at a 32x2 sheet at
/// y=64 in the chunk at the origin, left there by a long-deleted test harness
/// that wrote into the real world. Generating the same chunk from the same seed
/// produced none of it - so the damage was a fact on disk, and the only thing
/// that can undo a fact on disk is refusing to read it. Bumping this number was
/// tried first and did nothing, because a file that old is not version 3 at all.
///
/// The cost is that anything built in a chunk untouched since the id widening
/// goes with it. That is the trade this number exists to make.
///
/// **Deliberately NOT bumped for the 2026-08-10 ground-cover change.** Making
/// patchier grass reach an already-played world would mean rejecting every
/// chunk on disk and regenerating it, and everything the player has built in
/// those chunks goes with them. Cosmetic ground cover is not worth a world.
/// New chunks get the new rule; already-visited ones keep the old sparse cover.
/// Bump it to 4 if that mixed state ever matters more than the builds do.
constexpr std::uint32_t kChunkFormatVersion = 4;

/// Versioned separately from chunks and the player, because it stores
/// `ItemStack`s and so has to be invalidated whenever those change shape. A
/// shared version would throw away every saved chunk for the same reason.
///
/// Both were bumped when the block/item boundary moved from 256 to 4096, and
/// like the chunks the previous version is **upgraded rather than rejected** -
/// the stored ids are still readable, they just need shifting back into the
/// range they now mean.
constexpr std::uint32_t kFurnaceVersion = 3;
constexpr std::uint32_t kFurnaceLegacyItemVersion = 2;
constexpr std::uint32_t kChestVersion = 2;
constexpr std::uint32_t kChestLegacyItemVersion = 1;
/// New at M29c, so there is nothing older to upgrade from.
constexpr std::uint32_t kStowboxVersion = 1;
constexpr std::uint32_t kCreatureVersion = 3;

/// Sanity bound on a file the game did not write this run. Far more furnaces
/// than anyone would place, and small enough that a corrupt length cannot ask
/// for an enormous allocation.
constexpr std::uint32_t kMaxFurnaces = 1u << 20;
constexpr std::uint32_t kMaxChests = 1u << 20;
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
static_assert(std::is_trivially_copyable_v<PlacedChest>,
              "PlacedChest is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<StowedBox>,
              "StowedBox is written as raw bytes and must stay trivially copyable");

/// Shifts one stack's id back into the range it means now. An empty slot has
/// id `None`, which is below the old boundary and so is left alone.
void upgradeStack(ItemStack& stack) {
    stack.item = upgradeLegacyItemId(stack.item);
}

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
    if (header.version != kChunkFormatVersion) {
        // Silent, unlike the checks below. An out-of-date chunk is not a fault -
        // it is the version number doing exactly the job it exists for, and a
        // warning here would print once per chunk in the world.
        return std::nullopt;
    }
    if (header.magic != kMagic || header.seed != m_seed || header.x != coord.x ||
        header.y != coord.y || header.z != coord.z ||
        header.blockCount != Chunk::kBlockCount) {
        engine::logWarn("Chunk file does not match this world, ignoring: " + path.string());
        return std::nullopt;
    }

    Chunk chunk;
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(Chunk::kBlockBytes));
    if (file.gcount() != static_cast<std::streamsize>(Chunk::kBlockBytes)) {
        engine::logWarn("Truncated chunk data, ignoring: " + path.string());
        return std::nullopt;
    }

    // Appended after the blocks, so a file written before waterlogging existed
    // simply runs out here and comes back with none of it - which is the right
    // answer for a world that never had any.
    file.read(reinterpret_cast<char*>(chunk.waterloggedData()),
              static_cast<std::streamsize>(Chunk::kWaterloggedBytes));

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
        header.version = kChunkFormatVersion;
        header.seed = m_seed;
        header.x = coord.x;
        header.y = coord.y;
        header.z = coord.z;
        header.blockCount = Chunk::kBlockCount;

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(chunk.data()),
                   static_cast<std::streamsize>(Chunk::kBlockBytes));
        file.write(reinterpret_cast<const char*>(chunk.waterloggedData()),
                   static_cast<std::streamsize>(Chunk::kWaterloggedBytes));

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

    const bool legacyItems = version == kFurnaceLegacyItemVersion;
    if (!file || magic != kFurnaceMagic || seed != m_seed ||
        (version != kFurnaceVersion && !legacyItems)) {
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
        if (legacyItems) {
            upgradeStack(placed.furnace.input);
            upgradeStack(placed.furnace.fuel);
            upgradeStack(placed.furnace.output);
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

std::vector<PlacedChest> WorldStore::loadChests() const {
    const std::filesystem::path path = m_directory.parent_path() / "chests.dat";

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

    const bool legacyItems = version == kChestLegacyItemVersion;
    if (!file || magic != kChestMagic || seed != m_seed ||
        (version != kChestVersion && !legacyItems)) {
        engine::logWarn("Chest file does not match this world, ignoring: " + path.string());
        return {};
    }
    if (count > kMaxChests) {
        engine::logWarn("Chest file claims " + std::to_string(count) + " entries, ignoring: " + path.string());
        return {};
    }

    std::vector<PlacedChest> chests;
    chests.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PlacedChest placed;
        file.read(reinterpret_cast<char*>(&placed), sizeof(placed));
        if (!file) {
            engine::logWarn("Truncated chest file, keeping what was read: " + path.string());
            break;
        }
        if (legacyItems) {
            for (ItemStack& slot : placed.chest.slots) {
                upgradeStack(slot);
            }
        }
        chests.push_back(placed);
    }
    return chests;
}

void WorldStore::saveChests(const std::vector<PlacedChest>& chests) const {
    const std::filesystem::path path = m_directory.parent_path() / "chests.dat";

    // An empty table deletes its file rather than leaving a stale one, which
    // would restore chests the player has already broken.
    if (chests.empty()) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open chest file for writing: " + temporary.string());
            return;
        }

        const auto count = static_cast<std::uint32_t>(chests.size());
        file.write(kChestMagic.data(), kChestMagic.size());
        file.write(reinterpret_cast<const char*>(&kChestVersion), sizeof(kChestVersion));
        file.write(reinterpret_cast<const char*>(&m_seed), sizeof(m_seed));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const PlacedChest& placed : chests) {
            file.write(reinterpret_cast<const char*>(&placed), sizeof(placed));
        }

        if (!file) {
            engine::logError("Failed writing chest file: " + temporary.string());
            return;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace chest file " + path.string() + ": " + error.message());
        std::filesystem::remove(temporary, error);
    }
}

std::vector<StowedBox> WorldStore::loadStowboxes() const {
    const std::filesystem::path path = m_directory.parent_path() / "stowboxes.dat";

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

    if (!file || magic != kStowboxMagic || seed != m_seed || version != kStowboxVersion) {
        engine::logWarn("Stowbox file does not match this world, ignoring: " + path.string());
        return {};
    }
    if (count > kMaxChests) {
        engine::logWarn("Stowbox file claims " + std::to_string(count) +
                        " entries, ignoring: " + path.string());
        return {};
    }

    std::vector<StowedBox> boxes;
    boxes.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        StowedBox stowed;
        file.read(reinterpret_cast<char*>(&stowed), sizeof(stowed));
        if (!file) {
            engine::logWarn("Truncated stowbox file, keeping what was read: " + path.string());
            break;
        }
        boxes.push_back(stowed);
    }
    return boxes;
}

void WorldStore::saveStowboxes(const std::vector<StowedBox>& boxes) const {
    const std::filesystem::path path = m_directory.parent_path() / "stowboxes.dat";

    if (boxes.empty()) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            engine::logError("Could not open stowbox file for writing: " + temporary.string());
            return;
        }

        const auto count = static_cast<std::uint32_t>(boxes.size());
        file.write(kStowboxMagic.data(), kStowboxMagic.size());
        file.write(reinterpret_cast<const char*>(&kStowboxVersion), sizeof(kStowboxVersion));
        file.write(reinterpret_cast<const char*>(&m_seed), sizeof(m_seed));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const StowedBox& stowed : boxes) {
            file.write(reinterpret_cast<const char*>(&stowed), sizeof(stowed));
        }

        if (!file) {
            engine::logError("Failed writing stowbox file: " + temporary.string());
            return;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        engine::logError("Could not replace stowbox file " + path.string() + ": " +
                         error.message());
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
