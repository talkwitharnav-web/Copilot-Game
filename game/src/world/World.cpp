#include "world/World.hpp"

#include "world/ChunkMesher.hpp"
#include "world/Farming.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>

namespace game {
namespace {

using Clock = std::chrono::steady_clock;

/// Floor division, correct for negative coordinates. Plain integer division
/// truncates toward zero, which puts blocks at -1 and 0 in the same chunk.
int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

int floorMod(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

int chebyshevDistance(const ChunkCoord& a, const ChunkCoord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

/// The six directions light travels.
constexpr std::array<glm::ivec3, 6> kLightSteps{glm::ivec3{1, 0, 0},  glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
                                                glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1}};

/// The four directions water spreads sideways.
constexpr std::array<glm::ivec3, 4> kFlowSteps{glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1},
                                               glm::ivec3{0, 0, -1}};

/// One block every five ticks, which is the reference's water flow speed.
constexpr std::chrono::milliseconds kFluidSpreadDelay{250};

/// Thirty ticks, the reference's Overworld lava. Six times slower than water,
/// and combined with a level step of two it is what makes lava creep three
/// blocks where water runs seven.
constexpr std::chrono::milliseconds kLavaSpreadDelay{1500};

/// Eighty ticks. The reference's fuse for anything lit by hand, by fire or by
/// redstone; only a charge set off by another blast gets a shorter one.
constexpr std::chrono::milliseconds kTntFuse{4000};

/// How fast a lit charge blinks, and how many blinks it gets. Their product is
/// the reference's four-second fuse.
constexpr std::chrono::milliseconds kTntBlink{250};
constexpr int kTntBlinks = 16;

/// The reference re-evaluates a fire every 30-40 ticks. One figure in the
/// middle of that band is enough here, because nothing else keys off the exact
/// number.
constexpr std::chrono::milliseconds kFireTickDelay{1750};

/// Chance per tick that a fire takes an adjacent fuel block, and the chance it
/// dies once there is nothing left to eat. Ours are one number each where the
/// reference has a per-block table of ignite odds - a named simplification, and
/// the place to start if fire ever feels wrong.
constexpr unsigned kFireSpreadPercent = 25;
constexpr unsigned kFireBurnoutPercent = 40;

/// How often flowing lava quenched by water sets as obsidian rather than
/// cobblestone. Ours alone - the reference has no such chance.
constexpr unsigned kFlowingLavaObsidianPercent = 20;

/// One block of fall per step. Short enough to read as falling rather than as
/// teleporting, and it is the only thing setting how fast a column collapses.
constexpr std::chrono::milliseconds kFallDelay{60};

/// Everything a mesh job reads, allocated once and shared with the job.
///
/// Captured by `shared_ptr` rather than by value: a lambda holding a whole chunk
/// gets copied into the `std::function` that carries it, so the volume would be
/// copied twice per job. A pointer-sized capture also fits inside
/// `std::function` without a second heap allocation.
struct MeshJobInput {
    ChunkCoord coord;
    std::uint32_t revision = 0;
    glm::vec3 origin{0.0f};
    /// Decided on the main thread and carried with the volume, so the job never
    /// has to ask where the player is.
    MeshDetail detail = MeshDetail::Full;
    ChunkVolume volume;
};

/// How far past `m_detailRadius` a chunk keeps decoration it already has.
///
/// One chunk. The boundary is measured between chunk coordinates, so without
/// this a player pacing across a single chunk edge would flip a whole ring in
/// and out on alternate steps, re-meshing it every time.
constexpr int kDetailHysteresisChunks = 1;

} // namespace

World::World(std::uint32_t seed, std::filesystem::path saveRoot, engine::JobSystem& jobs, int visibleRadiusChunks)
    : m_seed(seed), m_visibleRadius(std::max(1, visibleRadiusChunks)), m_loadRadius(m_visibleRadius + 1),
      m_unloadRadius(m_loadRadius + 2), m_detailRadius(m_visibleRadius),
      m_store(std::make_shared<WorldStore>(std::move(saveRoot), seed)), m_jobs(jobs),
      m_results(std::make_shared<JobResults>()) {}

/// Jobs hold `shared_ptr`s to the results buffer and the store, so anything
/// still running here keeps what it touches alive and simply writes into a
/// buffer nobody will read. Nothing needs to be waited for.
World::~World() = default;

int World::skyLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    // Unloaded reads as full sky, so the frontier does not darken as it streams.
    if (chunk == nullptr) {
        return kMaxLight;
    }
    return chunk->skyLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

int World::blockLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->blockLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

void World::setSkyLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setSkyLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                  level);
    lightChangedAt(x, y, z);
}

void World::setBlockLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setBlockLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                    level);
    lightChangedAt(x, y, z);
}

/// Records that a chunk's geometry needs rebuilding because its light moved.
///
/// Only collects coordinates; the actual invalidation happens once per frame in
/// `flushLightDirty`. Propagation touches tens of thousands of cells, and
/// invalidating per cell would mean a linear scan of the pending queue each time.
void World::lightChangedAt(int x, int y, int z) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    m_lightDirty.insert(coord);

    // A lit cell on a chunk face shades the neighbour's geometry too.
    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);
    constexpr int last = Chunk::kSize - 1;

    if (lx == 0) {
        m_lightDirty.insert({coord.x - 1, coord.y, coord.z});
    }
    if (lx == last) {
        m_lightDirty.insert({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
        m_lightDirty.insert({coord.x, coord.y - 1, coord.z});
    }
    if (ly == last) {
        m_lightDirty.insert({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
        m_lightDirty.insert({coord.x, coord.y, coord.z - 1});
    }
    if (lz == last) {
        m_lightDirty.insert({coord.x, coord.y, coord.z + 1});
    }
}

void World::flushLightDirty() {
    for (const ChunkCoord& coord : m_lightDirty) {
        invalidateMesh(coord);
    }
    m_lightDirty.clear();
}

bool World::columnLoaded(int chunkX, int chunkZ) const {
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        if (!hasChunk({chunkX, cy, chunkZ})) {
            return false;
        }
    }
    return true;
}

bool World::columnResident(int x, int z) const {
    return columnLoaded(floorDiv(x, Chunk::kSize), floorDiv(z, Chunk::kSize));
}

void World::seedColumnLight(int chunkX, int chunkZ) {
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
                              static_cast<std::uint32_t>(chunkZ);
    if (!m_litColumns.insert(key).second) {
        return;
    }

    constexpr int worldTop = kWorldHeightChunks * Chunk::kSize - 1;
    constexpr int size = Chunk::kSize;

    // Writes go straight into the chunks rather than through `setSkyLightAt`:
    // this touches ~98,000 cells per column, and marking dirty per cell would
    // dwarf the actual work. The whole column is marked once at the end.
    std::array<ChunkSlot*, kWorldHeightChunks> column{};
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        const auto it = m_chunks.find({chunkX, cy, chunkZ});
        if (it == m_chunks.end()) {
            return;
        }
        column[static_cast<std::size_t>(cy)] = &it->second;
    }

    // Lowest y still reached by open sky, per column. One cell of margin so the
    // seeding step below can compare against neighbouring chunk-columns.
    constexpr int span = size + 2;
    std::array<int, span * span> skyFloor{};

    for (int mz = 0; mz < span; ++mz) {
        for (int mx = 0; mx < span; ++mx) {
            const int lx = mx - 1;
            const int lz = mz - 1;
            const bool inside = lx >= 0 && lx < size && lz >= 0 && lz < size;
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;

            int floorY = 0;
            for (int y = worldTop; y >= 0; --y) {
                const BlockId block =
                    inside ? column[static_cast<std::size_t>(y / size)]->blocks.at(lx, y % size, lz)
                           : blockAt(worldX, y, worldZ);
                if (!isSkyTransparent(block)) {
                    floorY = y + 1;
                    break;
                }
            }
            skyFloor[static_cast<std::size_t>(mz) * span + mx] = floorY;
        }
    }

    for (int lz = 0; lz < size; ++lz) {
        for (int lx = 0; lx < size; ++lx) {
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;
            const int floorY = skyFloor[static_cast<std::size_t>(lz + 1) * span + (lx + 1)];

            for (int y = worldTop; y >= 0; --y) {
                ChunkSlot& slot = *column[static_cast<std::size_t>(y / size)];
                const int ly = y % size;
                const BlockId block = slot.blocks.at(lx, ly, lz);

                // Sky falls straight down at full strength until something stops
                // it. Vertical travel costs nothing, which is what makes open
                // ground uniformly bright; only sideways spread dims.
                slot.blocks.setSkyLight(lx, ly, lz, y >= floorY ? kMaxLight : 0);

                if (const int emission = blockLightEmission(block); emission > 0) {
                    slot.blocks.setBlockLight(lx, ly, lz, emission);
                    m_blockAdditions.push_back({worldX, y, worldZ});
                }
            }

            // Only cells that can actually spread anywhere are worth queueing.
            // A lit cell whose neighbours are all lit has nothing to give, and
            // seeding every one of them buried the queue under tens of millions
            // of entries that did nothing.
            const int highestDarkNeighbour =
                std::max({skyFloor[static_cast<std::size_t>(lz + 1) * span + lx],
                          skyFloor[static_cast<std::size_t>(lz + 1) * span + lx + 2],
                          skyFloor[static_cast<std::size_t>(lz) * span + lx + 1],
                          skyFloor[static_cast<std::size_t>(lz + 2) * span + lx + 1]});

            // The lowest full-sky cell always gets queued: whatever stopped the
            // free fall may still be something light seeps into, such as a
            // canopy, and nothing else would ever push light down into it.
            if (floorY <= worldTop) {
                m_skyAdditions.push_back({worldX, floorY, worldZ});
            }

            for (int y = floorY + 1; y < highestDarkNeighbour && y <= worldTop; ++y) {
                m_skyAdditions.push_back({worldX, y, worldZ});
            }
        }
    }

    // The column and everything touching it, since border light shades
    // neighbouring geometry.
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        m_lightDirty.insert({chunkX, cy, chunkZ});
        m_lightDirty.insert({chunkX - 1, cy, chunkZ});
        m_lightDirty.insert({chunkX + 1, cy, chunkZ});
        m_lightDirty.insert({chunkX, cy, chunkZ - 1});
        m_lightDirty.insert({chunkX, cy, chunkZ + 1});
    }
}

void World::unpropagate(std::deque<LightRemoval>& removals, std::deque<glm::ivec3>& additions, bool sky) {
    while (!removals.empty()) {
        const LightRemoval entry = removals.front();
        removals.pop_front();

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = entry.position + step;
            if (n.y < 0 || n.y >= kWorldHeightChunks * Chunk::kSize) {
                continue;
            }

            const int level = sky ? skyLightAt(n.x, n.y, n.z) : blockLightAt(n.x, n.y, n.z);
            if (level == 0) {
                continue;
            }

            // Sky light falls straight down at full strength, so the cell under
            // a removed full-strength one was lit by it at the *same* level -
            // which "dimmer than its source" can never detect. Without this a
            // roof left the whole column under it lit by a sky it can no longer
            // see.
            const bool litFromAbove = sky && step.y == -1 &&
                                      entry.previousLevel == kMaxLight && level == kMaxLight;

            if (level < entry.previousLevel || litFromAbove) {
                // This neighbour was lit by what we just removed.
                if (sky) {
                    setSkyLightAt(n.x, n.y, n.z, 0);
                } else {
                    setBlockLightAt(n.x, n.y, n.z, 0);
                }
                removals.push_back({n, level});
            } else {
                // Brighter than the source, so it has its own supply and will
                // fill the hole back in.
                additions.push_back(n);
            }
        }
    }
}

void World::propagateLight(const BudgetCheck& budgetSpent) {
    unpropagate(m_skyRemovals, m_skyAdditions, true);
    unpropagate(m_blockRemovals, m_blockAdditions, false);

    const int worldHeight = kWorldHeightChunks * Chunk::kSize;

    // Block light goes first even though sky light is what makes newly streamed
    // terrain look right. Block light is rare and its queue is short, so it
    // costs sky light almost nothing - but behind sky light it can be starved
    // outright, because streaming refills the sky queue every frame and the two
    // share one budget. That is a placed torch that never lights anything.
    while (!m_blockAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_blockAdditions.front();
        m_blockAdditions.pop_front();

        const int level = blockLightAt(position.x, position.y, position.z);
        if (level <= 0) {
            continue;
        }

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            if (!isLightTransparent(blockAt(n.x, n.y, n.z))) {
                continue;
            }
            if (blockLightAt(n.x, n.y, n.z) >= level - 1) {
                continue;
            }

            setBlockLightAt(n.x, n.y, n.z, level - 1);
            m_blockAdditions.push_back(n);
        }
    }

    while (!m_skyAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_skyAdditions.front();
        m_skyAdditions.pop_front();

        const int level = skyLightAt(position.x, position.y, position.z);
        if (level <= 0) {
            continue;
        }

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            if (!isLightTransparent(blockAt(n.x, n.y, n.z))) {
                continue;
            }

            // Straight down keeps full strength, but only through cells that
            // are sky-transparent. A canopy breaks the free fall, and from there
            // light dims one level per block downward like any other direction.
            const int reaching =
                (step.y == -1 && level == kMaxLight && isSkyTransparent(blockAt(n.x, n.y, n.z))) ? kMaxLight
                                                                                                 : level - 1;
            if (reaching <= 0 || skyLightAt(n.x, n.y, n.z) >= reaching) {
                continue;
            }

            setSkyLightAt(n.x, n.y, n.z, reaching);
            m_skyAdditions.push_back(n);
        }
    }
}

void World::scheduleFluidUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fluidUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFluidSpreadDelay});
}

void World::scheduleLavaUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_lavaUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kLavaSpreadDelay});
}

void World::primeTnt(const glm::ivec3& at) {
    // The fuse is spent as a run of blinks rather than one long wait, so the
    // charge visibly counts down. The last one detonates.
    m_tntFuses.push_back({at, std::chrono::steady_clock::now() + kTntBlink, kTntBlinks});
}

void World::scheduleFireUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fireUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFireTickDelay});
}

bool World::fireCanSurvive(int x, int y, int z) const {
    const BlockId under = blockAt(x, y - 1, z);
    if (feedsEternalFire(under) || game::isSolid(under)) {
        return true;
    }
    for (const glm::ivec3& step : kLightSteps) {
        if (isFlammable(blockAt(x + step.x, y + step.y, z + step.z))) {
            return true;
        }
    }
    return false;
}

bool World::farmlandIsHydrated(int x, int y, int z) const {
    // The reference's rule is a **nine-by-nine box at this level or one above**
    // - not a radius and not a line of sight, so nothing in between matters and
    // flowing water counts the same as a source.
    constexpr int reach = farming::kHydrationReach;
    for (int dy = 0; dy <= 1; ++dy) {
        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dx = -reach; dx <= reach; ++dx) {
                if (isWater(blockAt(x + dx, y + dy, z + dz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

void World::growOne(const glm::ivec3& at) {
    // The sampler's own xorshift, advanced in place. Kept apart from the fire's
    // so a field growing cannot perturb what a burning forest does next.
    const auto nextRandom = [this] {
        m_growthRandom ^= m_growthRandom << 13;
        m_growthRandom ^= m_growthRandom >> 17;
        m_growthRandom ^= m_growthRandom << 5;
        return m_growthRandom;
    };
    const BlockId here = blockAt(at.x, at.y, at.z);

    // ---- Tilled ground. ----
    if (isFarmland(here)) {
        const bool wet = farmlandIsHydrated(at.x, at.y, at.z);
        const BlockId above = blockAt(at.x, at.y + 1, at.z);
        const bool planted = isCropBlock(above) || isStemBlock(above);
        if (!wet && !planted) {
            // Dry and unsown goes back to dirt. **Sown ground never does**,
            // which is what makes dry farming legitimate rather than doomed.
            setBlock(at.x, at.y, at.z, BlockId::Dirt);
            return;
        }
        const BlockId wanted = wet ? BlockId::FarmlandMoist : BlockId::Farmland;
        if (wanted != here) {
            setBlock(at.x, at.y, at.z, wanted);
        }
        return;
    }

    // ---- Nether wart. ----
    // A flat one-in-ten, and the one plant the reference gates on nothing else
    // at all: no light, no biome, no dimension, no bone meal.
    if (isNetherWart(here)) {
        if (netherWartAge(here) >= 3 || (nextRandom() % 10) != 0) {
            return;
        }
        setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(here) + 1));
        return;
    }

    const bool crop = isCropBlock(here);
    const bool stem = isGrowingStem(here);
    if (!crop && !stem) {
        return;
    }

    // A plant needs tilled ground under it and light at its own cell.
    const glm::ivec3 soil{at.x, at.y - 1, at.z};
    const BlockId under = blockAt(soil.x, soil.y, soil.z);
    if (!isFarmland(under)) {
        return;
    }
    if (std::max(skyLightAt(at.x, at.y, at.z), blockLightAt(at.x, at.y, at.z)) < 9) {
        return;
    }

    // The reference's points, counted in quarters so the sum stays integral.
    // They come from the **three-by-three patch of farmland**, not from the
    // plant - which is why a lone crop in a field grows faster than one on its
    // own however well watered it is.
    int quarters = (under == BlockId::FarmlandMoist) ? farming::kPointsWetUnder
                                                     : farming::kPointsDryUnder;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const BlockId neighbour = blockAt(soil.x + dx, soil.y, soil.z + dz);
            if (neighbour == BlockId::FarmlandMoist) {
                quarters += farming::kPointsWetNear;
            } else if (neighbour == BlockId::Farmland) {
                quarters += farming::kPointsDryNear;
            }
        }
    }

    const float roll = static_cast<float>(nextRandom() & 0xFFFFFF) /
                       static_cast<float>(0x1000000);
    if (roll >= farming::growthChance(quarters)) {
        return;
    }

    if (crop) {
        const int age = cropAge(here);
        if (age < 7) {
            setBlock(at.x, at.y, at.z, cropAt(cropFamily(here), age + 1));
        }
        return;
    }

    // ---- A stem. ----
    const int age = stemAge(here);
    if (age < 7) {
        setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(here) + 1));
        return;
    }

    // Ripe: try to put a fruit down. **The support test looks at the block
    // beneath the candidate cell, not at the cell**, and the reference's own
    // preference order is east, west, north, south.
    static constexpr glm::ivec3 kSides[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
    const bool melon = stemGrowsMelon(here);
    for (int side = 0; side < 4; ++side) {
        const glm::ivec3 cell = at + kSides[side];
        if (blockAt(cell.x, cell.y, cell.z) != BlockId::Air) {
            continue;
        }
        if (!farming::supportsFruit(blockAt(cell.x, cell.y - 1, cell.z))) {
            continue;
        }
        setBlock(cell.x, cell.y, cell.z, melon ? BlockId::Melon : BlockId::Pumpkin);
        // The stem now points at what it grew and produces nothing more until
        // that fruit is taken. Attached order matches `kSides`, so the facing
        // is the loop index rather than a second table.
        const BlockId attached = melon ? BlockId::MelonStemAttachedFirst
                                       : BlockId::PumpkinStemAttachedFirst;
        setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(attached) + side));
        return;
    }
}

bool World::applyBoneMeal(const glm::ivec3& at) {
    const BlockId here = blockAt(at.x, at.y, at.z);
    if (!isCropBlock(here) && !isGrowingStem(here)) {
        return false;
    }
    m_growthRandom ^= m_growthRandom << 13;
    m_growthRandom ^= m_growthRandom >> 17;
    m_growthRandom ^= m_growthRandom << 5;
    // **Beetroot takes a single stage and only three times in four**, which is
    // the reference's own split and the reason bone meal is poor value on it.
    if (isCropBlock(here) && farming::boneMealIsSingleStage(cropFamily(here))) {
        if ((m_growthRandom % 4u) != 0u) {
            const int age = cropAge(here);
            if (age < 7) {
                setBlock(at.x, at.y, at.z, cropAt(cropFamily(here), age + 1));
            }
        }
        return true;
    }
    // Everything else jumps two to five stages. Straight to the advance,
    // skipping the chance roll: bone meal is not a faster tick, it is a
    // guaranteed one.
    const int steps = 2 + static_cast<int>(m_growthRandom % 4u);
    for (int step = 0; step < steps; ++step) {
        const BlockId current = blockAt(at.x, at.y, at.z);
        if (isCropBlock(current)) {
            const int age = cropAge(current);
            if (age >= 7) {
                break;
            }
            setBlock(at.x, at.y, at.z, cropAt(cropFamily(current), age + 1));
        } else if (isGrowingStem(current)) {
            // A stem is advanced but **never fruited** by bone meal, which is
            // the reference's rule and the whole reason a melon farm takes time.
            if (stemAge(current) >= 7) {
                break;
            }
            setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(current) + 1));
        } else {
            break;
        }
    }
    return true;
}

void World::updateGrowth(const BudgetCheck& budgetSpent) {
    const auto now = Clock::now();
    if (m_nextGrowthTick.time_since_epoch().count() == 0) {
        m_nextGrowthTick = now;
    }
    if (now < m_nextGrowthTick) {
        return;
    }
    // One game tick. Every rate below is quoted per tick, so this is the number
    // that must not drift - raising it would silently speed every farm up.
    m_nextGrowthTick = now + std::chrono::milliseconds(50);

    for (auto& [coord, slot] : m_chunks) {
        if (budgetSpent()) {
            return;
        }
        // Sampled straight out of the chunk rather than through `blockAt`, so
        // the common case - a cell of stone or air - costs one array read
        // instead of a hash lookup.
        for (int sample = 0; sample < farming::kRandomTicksPerChunkPerTick; ++sample) {
            m_growthRandom ^= m_growthRandom << 13;
            m_growthRandom ^= m_growthRandom >> 17;
            m_growthRandom ^= m_growthRandom << 5;
            const std::uint32_t roll = m_growthRandom;
            const int lx = static_cast<int>((roll >> 2) % Chunk::kSize);
            const int ly = static_cast<int>((roll >> 10) % Chunk::kSize);
            const int lz = static_cast<int>((roll >> 18) % Chunk::kSize);
            const BlockId here = slot.blocks.at(lx, ly, lz);
            if (!isFarmland(here) && !isCropBlock(here) && !isGrowingStem(here) &&
                !isNetherWart(here)) {
                continue;
            }
            growOne(glm::ivec3{coord.x * Chunk::kSize + lx, ly, coord.z * Chunk::kSize + lz});
        }
    }
}

void World::updateFire(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fireUpdates.empty() && !budgetSpent()) {        if (m_fireUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fireUpdates.front().position;
        m_fireUpdates.pop_front();
        if (blockAt(p.x, p.y, p.z) != BlockId::Fire) {
            continue;
        }

        const BlockId under = blockAt(p.x, p.y - 1, p.z);
        // Netherrack and its cousins keep a fire for ever; everything else
        // needs either fuel beside it or a floor beneath it.
        const bool eternal = feedsEternalFire(under);

        bool fuelNearby = false;
        for (const glm::ivec3& step : kLightSteps) {
            if (isFlammable(blockAt(p.x + step.x, p.y + step.y, p.z + step.z))) {
                fuelNearby = true;
                break;
            }
        }

        if (!fireCanSurvive(p.x, p.y, p.z)) {
            setBlock(p.x, p.y, p.z, BlockId::Air);
            continue;
        }

        // Rain puts out anything the sky can reach. This is what makes a bolt
        // safe to hand a real fire to - the reference lights them during a
        // storm precisely because the same storm is already putting them out.
        if (m_precipitating && !feedsEternalFire(under) &&
            skyLightAt(p.x, p.y, p.z) >= kMaxLight) {
            setBlock(p.x, p.y, p.z, BlockId::Air);
            continue;
        }
        auto roll = [this]() {
            m_fireRandom ^= m_fireRandom << 13;
            m_fireRandom ^= m_fireRandom >> 17;
            m_fireRandom ^= m_fireRandom << 5;
            return m_fireRandom;
        };

        if (fuelNearby) {
            // **Spread is what consumes the fuel**, rather than a separate
            // burn-away pass: the block that catches is replaced by fire, so a
            // log wall is eaten one cell at a time and the flame front moves.
            for (const glm::ivec3& step : kLightSteps) {
                const glm::ivec3 n = p + step;
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isFlammable(neighbour) || (roll() % 100u) >= kFireSpreadPercent) {
                    continue;
                }
                // A charge does not burn away - it lights.
                if (neighbour == BlockId::Tnt) {
                    setBlock(n.x, n.y, n.z, BlockId::TntPrimed);
                    primeTnt(n);
                    continue;
                }
                setBlock(n.x, n.y, n.z, BlockId::Fire);
            }
        }

        // Left burning: come back and ask again. A fire with nothing left to eat
        // and no eternal floor dies on one of these passes.
        if (blockAt(p.x, p.y, p.z) == BlockId::Fire) {
            if (!eternal && !fuelNearby && (roll() % 100u) < kFireBurnoutPercent) {
                setBlock(p.x, p.y, p.z, BlockId::Air);
                continue;
            }
            scheduleFireUpdate(p.x, p.y, p.z);
        }
    }
}

std::vector<glm::ivec3> World::takeDetonations() {
    return std::exchange(m_detonations, {});
}

void World::updateTnt(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_tntFuses.empty() && !budgetSpent()) {
        if (m_tntFuses.front().due > now) {
            break;
        }
        const PendingFluid entry = m_tntFuses.front();
        m_tntFuses.pop_front();
        const glm::ivec3 p = entry.position;
        const BlockId here = blockAt(p.x, p.y, p.z);
        // Broken or already gone. The fuse is not cancelled when the block is
        // mined - it is simply found missing here, which costs nothing and
        // means breaking a lit charge needs no bookkeeping of its own.
        if (!isTntBlock(here)) {
            continue;
        }
        if (entry.blinks > 0) {
            // **The blink is the block, not a texture swap.** Both animated
            // layer slots are spent on water and fire, and toggling the id
            // costs one remesh of one chunk a few times a second - which is
            // also what makes the countdown visible in the world's own state.
            setBlock(p.x, p.y, p.z,
                     here == BlockId::TntPrimed ? BlockId::Tnt : BlockId::TntPrimed);
            m_tntFuses.push_back({p, now + kTntBlink, entry.blinks - 1});
            continue;
        }
        setBlock(p.x, p.y, p.z, BlockId::Air);
        m_detonations.push_back(p);
    }
}

void World::scheduleFallUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fallUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFallDelay});
}

void World::updateFalls(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fallUpdates.empty() && !budgetSpent()) {
        if (m_fallUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fallUpdates.front().position;
        m_fallUpdates.pop_front();

        const BlockId falling = blockAt(p.x, p.y, p.z);
        if (p.y <= 0 || !isFalling(falling)) {
            continue;
        }
        // An unloaded chunk reads as air, so a column on the edge of the loaded
        // world would otherwise pour itself into nothing and never come back.
        if (!columnResident(p.x, p.z)) {
            continue;
        }
        if (!isReplaceable(blockAt(p.x, p.y - 1, p.z))) {
            continue;
        }

        // Detached, not moved. Where it ends up is the entity's business; all
        // the world does is take it out of the grid and say so - which also
        // schedules whatever was resting on top of it, so a column collapses
        // from the bottom without a loop here.
        setBlock(p.x, p.y, p.z, BlockId::Air);
        m_detachedBlocks.push_back({p, falling});
    }
}

std::vector<World::WashedBlock> World::takeDetachedBlocks() {
    return std::exchange(m_detachedBlocks, {});
}

namespace {

/// How far the reference looks for a way down before giving up: four blocks.
constexpr int kSlopeSearch = 4;

/// What an unreachable drop scores. The reference's own sentinel.
constexpr int kNoSlope = 1000;

} // namespace

bool World::fluidCanEnter(int x, int y, int z) const {
    const BlockId here = blockAt(x, y, z);
    // A plant does not dam a stream, it is swept away by it - so the search has
    // to path straight through one, or a meadow full of grass turns every flow
    // into a maze.
    return here == BlockId::Air || isWater(here) || isWashedAway(here);
}

bool World::fluidCanDrainFrom(int x, int y, int z) const {
    // Somewhere below that could *receive* water. Water already there does not
    // count, and that is the whole point: once a hole has filled it stops being
    // a hole, the weight search stops steering everything into it, and the flow
    // spreads on past. Counting any water below as a drop dead-ended every
    // stream at the first dip it found.
    const BlockId below = blockAt(x, y - 1, z);
    return below == BlockId::Air || isWashedAway(below);
}

bool World::fluidFeedsSideways(int x, int y, int z) const {
    // Solid ground underfoot is what makes water pool. Anything that can still
    // go down goes down instead - which is what keeps a waterfall one block
    // wide - and a cell resting on more water is partway down a column, not the
    // bottom of one. A source is the exception: it spreads across water, which
    // is how a lake has a surface at all.
    const BlockId below = blockAt(x, y - 1, z);
    return game::isSolid(below) || (isWaterSource(blockAt(x, y, z)) && isWater(below));
}

int World::slopeDistance(int x, int z, int y, int fromDirection) const {
    // Breadth-first, so the first hole found is genuinely the nearest. Depth is
    // capped at four, which is the reference's `slopeFindDistance` and is what
    // makes water seek a hole it can nearly reach and ignore one it cannot.
    struct Node {
        int x;
        int z;
        int depth;
        int from;
    };

    std::array<Node, 4 * kSlopeSearch * kSlopeSearch + 4> queue{};
    std::size_t head = 0;
    std::size_t tail = 0;
    queue[tail++] = {x, z, 1, fromDirection};

    while (head < tail) {
        const Node node = queue[head++];
        if (!fluidCanEnter(node.x, y, node.z)) {
            continue;
        }
        if (fluidCanDrainFrom(node.x, y, node.z)) {
            return node.depth;
        }
        if (node.depth >= kSlopeSearch) {
            continue;
        }
        for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
            // Never turn straight back the way we came; the reference does the
            // same, and without it the search wastes most of its budget
            // re-examining the cell it just left.
            if (static_cast<int>(i ^ 1u) == node.from) {
                continue;
            }
            if (tail >= queue.size()) {
                break;
            }
            queue[tail++] = {node.x + kFlowSteps[i].x, node.z + kFlowSteps[i].z, node.depth + 1,
                             static_cast<int>(i)};
        }
    }
    return kNoSlope;
}

bool World::fluidSpreadsToward(const glm::ivec3& from, std::size_t direction) const {
    // Every direction starts at 1000 and is replaced by the distance to the
    // nearest reachable drop. Water then runs **only** the lowest-scoring ways,
    // which is what sends a stream one block wide at a cliff edge instead of
    // fanning into a diamond. With no hole in range every direction ties at
    // 1000, and a flat floor gets the even spread it should.
    int best = kNoSlope;
    std::array<int, 4> weights{};
    for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
        const glm::ivec3 step = from + kFlowSteps[i];
        weights[i] = fluidCanEnter(step.x, step.y, step.z)
                         ? slopeDistance(step.x, step.z, step.y, static_cast<int>(i))
                         : kNoSlope;
        best = std::min(best, weights[i]);
    }
    return weights[direction] == best;
}

void World::updateFluids(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fluidUpdates.empty() && !budgetSpent()) {
        // Every entry waits the same delay, so the queue is already in due
        // order and the front one not being ready means none of them is.
        if (m_fluidUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fluidUpdates.front().position;
        m_fluidUpdates.pop_front();

        const BlockId current = blockAt(p.x, p.y, p.z);
        // Concrete powder sets the instant water touches it, on any of the six
        // sides. It is answered before the "is this the fluid system's
        // business" test below, because a powder is neither air nor water and
        // would otherwise fall straight through.
        if (isConcretePowder(current)) {
            for (const glm::ivec3& step : kLightSteps) {
                if (isWater(blockAt(p.x + step.x, p.y + step.y, p.z + step.z))) {
                    setBlock(p.x, p.y, p.z, concreteFor(current));
                    break;
                }
            }
            continue;
        }
        // Only air, water and things a flow sweeps aside are the fluid system's
        // business. Testing for "not solid" was the same thing while every block
        // was a full cube or water, but a slab is neither - and falling through
        // here rewrites it to air.
        if (current != BlockId::Air && !isWater(current) && !isWashedAway(current)) {
            continue;
        }
        // Sources are the fixed points of the whole system. Without something
        // that never drains, every body of water eventually empties itself.
        if (isWaterSource(current)) {
            continue;
        }

        int supply = kMaxWaterLevel + 1;
        int adjacentSources = 0;
        for (const glm::ivec3& step : kFlowSteps) {
            if (isWaterSource(blockAt(p.x + step.x, p.y, p.z + step.z))) {
                ++adjacentSources;
            }
        }

        // **The block above is answered before any neighbour**, because a cell
        // fed from overhead is *falling*: full, and on its way down. Ours used
        // to record it as merely "nearly full", which let a column poured off a
        // tower fan out sideways at every level it passed.
        const bool fedFromAbove = isWater(blockAt(p.x, p.y + 1, p.z));

        // **The slope only decides which empty cells a flow spreads into; it
        // never re-decides a cell that already holds water.** The reference
        // keeps these apart - its `getNewLiquid` recomputes a level from the
        // neighbours with no slope test in it at all, and the weights are
        // consulted only when spreading. Folding the two together meant a
        // settled cell could be starved by a weight that changed *because of
        // its own outflow*: water reaches an edge, the column below it fills,
        // that direction stops scoring as a drop, a rival direction wins, the
        // cell empties, the column drains, the weight flips back. Air, water,
        // air, water, once per tick, for ever - which is what "the water near
        // the edge goes mad" was.
        const bool arriving = !isWater(current);

        if (!fedFromAbove) {
            for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
                const glm::ivec3 n = p + kFlowSteps[i];
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isWater(neighbour)) {
                    continue;
                }

                // **A neighbour that can still go down does not run sideways at
                // all.** This one rule is what makes a waterfall a column: every
                // cell in mid-air has somewhere to drop, so none of them feeds
                // anything to the side, and only the cell that finally lands on
                // solid ground has nowhere left to go and pools.
                if (!fluidFeedsSideways(n.x, n.y, n.z)) {
                    continue;
                }

                const int level = waterLevel(neighbour);
                if (level >= kMaxWaterLevel) {
                    continue;
                }
                // A cell that can itself drain is always worth flowing into -
                // nothing can score better than a drop one step away - so the
                // search is skipped rather than run to reach the same answer.
                if (arriving && !fluidCanDrainFrom(p.x, p.y, p.z) &&
                    !fluidSpreadsToward(n, i ^ 1u)) {
                    continue;
                }
                // Competing flows resolve to whichever supply is strongest.
                supply = std::min(supply, level + 1);
            }
        }

        BlockId wanted = BlockId::Air;
        if (adjacentSources >= 2 &&
            (game::isSolid(blockAt(p.x, p.y - 1, p.z)) || isWaterSource(blockAt(p.x, p.y - 1, p.z)))) {
            // Two sources meeting over solid ground - or over another source -
            // fill the gap permanently.
            wanted = BlockId::Water0;
        } else if (fedFromAbove) {
            wanted = BlockId::WaterFalling;
        } else if (supply <= kMaxWaterLevel) {
            wanted = waterAtLevel(supply);
        }

        if (wanted == current) {
            continue;
        }
        // Water only *arrives* in a plant's cell; it never leaves air behind in
        // one. Without this an update that decided on nothing would quietly mow
        // the lawn.
        if (isWashedAway(current)) {
            if (wanted == BlockId::Air) {
                continue;
            }
            m_washedBlocks.push_back({p, current});
        }
        setBlock(p.x, p.y, p.z, wanted);
    }
}

bool World::coolLava(const glm::ivec3& p, BlockId lava) {
    // The reference's mixing rules 1 and 3. **Which block wins is decided by
    // source-versus-flowing, not by where the water is**: a source touched
    // anywhere above or beside becomes obsidian, and anything flowing becomes
    // cobblestone. Water *below* is excluded from both - that case is lava
    // falling in, and it is the water that changes, not the lava.
    bool touchingWater = isWater(blockAt(p.x, p.y + 1, p.z));
    for (const glm::ivec3& step : kFlowSteps) {
        if (touchingWater) {
            break;
        }
        touchingWater = isWater(blockAt(p.x + step.x, p.y, p.z + step.z));
    }
    if (!touchingWater) {
        return false;
    }
    // A source always sets to obsidian. Flowing lava gives cobblestone in the
    // reference, but a share of it comes out as obsidian here - it is the one
    // place obsidian is renewable, and always-cobblestone makes a lava-and-water
    // meeting worth nothing.
    if (isLavaSource(lava)) {
        setBlock(p.x, p.y, p.z, BlockId::Obsidian);
        return true;
    }
    m_fireRandom ^= m_fireRandom << 13;
    m_fireRandom ^= m_fireRandom >> 17;
    m_fireRandom ^= m_fireRandom << 5;
    setBlock(p.x, p.y, p.z,
             (m_fireRandom % 100u) < kFlowingLavaObsidianPercent ? BlockId::Obsidian
                                                                 : BlockId::Cobblestone);
    return true;
}

void World::updateLava(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_lavaUpdates.empty() && !budgetSpent()) {
        if (m_lavaUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_lavaUpdates.front().position;
        m_lavaUpdates.pop_front();

        const BlockId current = blockAt(p.x, p.y, p.z);

        if (isLava(current) && coolLava(p, current)) {
            continue;
        }

        // Lava sets light to what is near it. Bedrock's rule is the simple one:
        // the fuel must sit inside the 3x3x3 centred on the lava and have air
        // above it, and the fire appears in that air cell.
        //
        // **A settled lava cell would otherwise never tick again**, so this is
        // also the one place lava reschedules itself - and only while there is
        // something nearby worth burning, which is what keeps a lava lake in
        // open stone from queueing work for ever.
        if (isLava(current)) {
            bool fuelNearby = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const glm::ivec3 n{p.x + dx, p.y + dy, p.z + dz};
                        if (!isFlammable(blockAt(n.x, n.y, n.z))) {
                            continue;
                        }
                        fuelNearby = true;
                        if (blockAt(n.x, n.y + 1, n.z) == BlockId::Air) {
                            setBlock(n.x, n.y + 1, n.z, BlockId::Fire);
                        }
                    }
                }
            }
            if (fuelNearby) {
                scheduleLavaUpdate(p.x, p.y, p.z);
            }
        }

        // Rule 2, and the only case where the *water* is what changes: lava
        // arriving from directly overhead sets the water it lands in.
        if (isWater(current) && isLava(blockAt(p.x, p.y + 1, p.z))) {
            setBlock(p.x, p.y, p.z, BlockId::Stone);
            continue;
        }

        if (current != BlockId::Air && !isLava(current) && !isWashedAway(current)) {
            continue;
        }
        // A source never drains. Unlike water, nothing here ever *creates* one:
        // the reference does not let lava sources self-generate, so there is no
        // two-neighbours rule to mirror.
        if (isLavaSource(current)) {
            continue;
        }

        const bool fedFromAbove = isLava(blockAt(p.x, p.y + 1, p.z));
        int supply = kMaxLavaLevel + 1;
        if (!fedFromAbove) {
            for (const glm::ivec3& step : kFlowSteps) {
                const glm::ivec3 n = p + step;
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isLava(neighbour)) {
                    continue;
                }
                // A neighbour with somewhere to fall does not also run sideways,
                // which is what keeps a lava fall a column.
                //
                // **A cell resting on more lava is partway down that column,
                // not the bottom of one**, and leaving that out is what the
                // user reported as lava stacking on top of itself: every cell
                // of a fall counted as pooling, so it shelved out sideways at
                // every height it passed and the drop grew a wall instead of a
                // stream. A source is the exception, because that is how a lava
                // lake has a surface at all. Deliberately the same shape as
                // `fluidFeedsSideways`, which is water's own copy of this rule -
                // the two fluids are parallel here on purpose, since they differ
                // in spread step, delay and what they do on contact.
                const BlockId under = blockAt(n.x, n.y - 1, n.z);
                const bool pools = game::isSolid(under) || isWater(under) ||
                                   (isLavaSource(neighbour) && isLava(under));
                if (!pools) {
                    continue;
                }
                const int level = lavaLevel(neighbour);
                if (level + kLavaSpreadStep > kMaxLavaLevel) {
                    continue;
                }
                supply = std::min(supply, level + kLavaSpreadStep);
            }
        }

        BlockId wanted = BlockId::Air;
        if (fedFromAbove) {
            wanted = BlockId::LavaFalling;
        } else if (supply <= kMaxLavaLevel) {
            wanted = lavaAtLevel(supply);
        }

        if (wanted == current) {
            continue;
        }
        // Flowing lava destroys what it sweeps aside rather than dropping it,
        // which is the one place it differs from water.
        if (isWashedAway(current) && wanted == BlockId::Air) {
            continue;
        }
        setBlock(p.x, p.y, p.z, wanted);
    }
}

std::vector<World::WashedBlock> World::takeWashedBlocks() {
    return std::exchange(m_washedBlocks, {});
}

void World::setVisibleRadius(int chunks) {
    const int radius = std::clamp(chunks, 1, 64);
    if (radius == m_visibleRadius) {
        return;
    }

    const int previous = m_visibleRadius;
    m_visibleRadius = radius;
    m_loadRadius = m_visibleRadius + 1;
    m_unloadRadius = m_loadRadius + 2;

    // Chunks are kept loaded a few rings past the visible radius so pacing back
    // and forth does not thrash them. That band would otherwise stay on screen
    // after shrinking, making the change look like it did nothing.
    m_radiusShrunk = radius < previous;

    // Forces the next update to run its recentre path, which is what unloads
    // what no longer fits and re-queues what is newly wanted.
    m_hasCentre = false;
}

void World::setDetailRadius(int chunks) {
    const int radius = std::max(1, chunks);
    if (radius == m_detailRadius) {
        return;
    }
    m_detailRadius = radius;

    // Every chunk has to be re-asked, and the recentre path is what does that.
    m_hasCentre = false;
}

MeshDetail World::detailFor(const ChunkCoord& coord, bool currentlyDetailed) const {
    // At or beyond the render distance the tier is off, and every chunk that is
    // drawn at all is drawn whole. Written as a comparison rather than a
    // separate flag so there is one number to reason about.
    if (m_detailRadius >= m_visibleRadius) {
        return MeshDetail::Full;
    }
    const int distance = chebyshevDistance(coord, m_centre);
    const int limit = m_detailRadius + (currentlyDetailed ? kDetailHysteresisChunks : 0);
    return distance <= limit ? MeshDetail::Full : MeshDetail::TerrainOnly;
}

void World::refreshDetail(const ChunkCoord& centre) {
    // Deliberately not short-circuited when the tier is off: turning it off is
    // exactly the case where chunks built terrain-only have to be told to put
    // their decoration back, and `detailFor` already answers `Full` for every
    // chunk in that state.
    for (auto& [coord, slot] : m_chunks) {
        // Only what is drawable can be looked at, and a chunk that comes back
        // into range is re-asked when it is next dispatched.
        if (chebyshevDistance(coord, centre) > m_visibleRadius) {
            continue;
        }
        const bool wanted = detailFor(coord, slot.detailWanted) == MeshDetail::Full;
        if (wanted == slot.detailWanted) {
            continue;
        }
        // A tier crossing genuinely changes what the chunk looks like, so it
        // goes through the same door an edit does: bumping the revision throws
        // away any job already building the old tier.
        slot.detailWanted = wanted;
        invalidateMesh(coord);
    }
}

std::size_t World::detailedChunkCount() const {
    std::size_t count = 0;
    for (const auto& [coord, slot] : m_chunks) {
        if (slot.meshed && slot.detailBuilt) {
            ++count;
        }
    }
    return count;
}

World::DetailLag World::detailLag() const {    DetailLag lag;
    for (const auto& [coord, slot] : m_chunks) {
        const int distance = chebyshevDistance(coord, m_centre);
        if (distance > m_visibleRadius) {
            continue;
        }
        if (slot.meshed && slot.detailBuilt != slot.detailWanted) {
            ++lag.chunks;
            lag.nearest = lag.nearest < 0 ? distance : std::min(lag.nearest, distance);
        }
    }
    return lag;
}

void World::saveIfModified(const ChunkCoord& coord, ChunkSlot& slot) {
    if (!slot.modified) {
        return;
    }
    m_store->save(coord, slot.blocks);
    slot.modified = false;
    ++m_savedChunkCount;
}

void World::saveAll() {
    for (auto& [coord, slot] : m_chunks) {
        saveIfModified(coord, slot);
    }
}

const Chunk* World::chunkAt(const ChunkCoord& coord) const {
    const auto it = m_chunks.find(coord);
    return it == m_chunks.end() ? nullptr : &it->second.blocks;
}

bool World::hasChunk(const ChunkCoord& coord) const {
    return m_chunks.find(coord) != m_chunks.end();
}

BlockId World::blockAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return BlockId::Air;
    }
    return chunk->at(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

bool World::waterloggedAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize),
                           floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    return chunk != nullptr && chunk->waterloggedAt(floorMod(x, Chunk::kSize),
                                                    floorMod(y, Chunk::kSize),
                                                    floorMod(z, Chunk::kSize));
}

bool World::isSolid(int x, int y, int z) const {
    return game::isSolid(blockAt(x, y, z));}

int World::highestSolid(int x, int z) const {
    for (int y = kWorldHeightChunks * Chunk::kSize - 1; y >= 0; --y) {
        if (isSolid(x, y, z)) {
            return y;
        }
    }
    return -1;
}

int World::groundHeight(int x, int z) const {
    const int top = highestSolid(x, z);
    if (top >= 0) {
        return top + 1;
    }
    // Nothing solid all the way down is not a real column - bedrock is - so
    // this only ever means the chunks are not here. Ask whoever made them.
    return surfaceHeightAt(m_seed, x, z) + 1;
}

void World::queueMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    if (m_pendingMeshSet.insert(coord).second) {
        m_pendingMesh.push_back(coord);
    }
}

void World::invalidateMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    ++it->second.revision;
    queueMesh(coord);
}

void World::setBlock(int x, int y, int z, BlockId block) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }

    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }

    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);

    const BlockId previous = it->second.blocks.at(lx, ly, lz);
    if (previous == block) {
        return;
    }

    // **A waterlogged cell that loses its block becomes water, not air.** Take
    // the plant out of a patch of seagrass and the sea has to close over it;
    // leaving air there is the hole this whole feature exists to stop. And
    // anything that fills the cell displaces the water it was sharing.
    const bool wasLogged = it->second.blocks.waterloggedAt(lx, ly, lz);
    BlockId placed = block;
    if (wasLogged && block == BlockId::Air) {
        placed = BlockId::Water0;
        it->second.blocks.setWaterlogged(lx, ly, lz, false);
    } else if (!canWaterlog(block)) {
        it->second.blocks.setWaterlogged(lx, ly, lz, false);
    } else if (isWater(previous)) {
        // Put a plant into water and it takes the water with it, which is the
        // other half of the rule above.
        it->second.blocks.setWaterlogged(lx, ly, lz, true);
    }

    it->second.blocks.set(lx, ly, lz, placed);
    it->second.modified = true;

    // Relight around the change. Removals have to run before additions, because
    // stale light must be cleared out before anything fills the gap.
    const int oldSky = it->second.blocks.skyLightAt(lx, ly, lz);
    const int oldBlockLight = it->second.blocks.blockLightAt(lx, ly, lz);

    if (!isLightTransparent(block)) {
        if (oldSky > 0) {
            it->second.blocks.setSkyLight(lx, ly, lz, 0);
            m_skyRemovals.push_back({{x, y, z}, oldSky});
        }
        if (oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
    } else {
        // Newly transparent: whatever surrounds it can now flow in.
        if (blockLightEmission(previous) > 0 && oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
        for (const glm::ivec3& step : kLightSteps) {
            m_skyAdditions.push_back(glm::ivec3{x, y, z} + step);
            m_blockAdditions.push_back(glm::ivec3{x, y, z} + step);
        }
    }

    if (const int emission = blockLightEmission(block); emission > 0) {
        it->second.blocks.setBlockLight(lx, ly, lz, emission);
        m_blockAdditions.push_back({x, y, z});
    }

    lightChangedAt(x, y, z);

    // Water re-evaluates itself and everything touching it. Removing a block
    // under a stream is what makes it fall; removing its supply is what makes it
    // recede.
    scheduleFluidUpdate(x, y, z);
    for (const glm::ivec3& step : kLightSteps) {
        scheduleFluidUpdate(x + step.x, y + step.y, z + step.z);
    }

    // And the same for lava. **Both queues get every edit** rather than one
    // being chosen by what is in the cell right now: the cell that just changed
    // may be air that lava is about to reach, and a queue that finds the wrong
    // fluid there simply does nothing. Guessing here is how a flow stalls one
    // block short of where it should stop.
    scheduleLavaUpdate(x, y, z);
    for (const glm::ivec3& step : kLightSteps) {
        scheduleLavaUpdate(x + step.x, y + step.y, z + step.z);
    }

    // A fire that has just been placed needs its first tick; one that already
    // exists reschedules itself, so this is the only way in.
    if (block == BlockId::Fire) {
        scheduleFireUpdate(x, y, z);
    }

    // Only two cells can have lost their footing: this one, if what just landed
    // in it falls, and the one resting on top of it.
    scheduleFallUpdate(x, y, z);
    scheduleFallUpdate(x, y + 1, z);

    invalidateMesh(coord);

    // Only a block on a chunk face can change a neighbour's mesh, but missing
    // those neighbours leaves holes at chunk seams. These must invalidate rather
    // than merely queue: a neighbour may already be meshing against the old
    // border, and that result would otherwise be accepted as current.
    constexpr int last = Chunk::kSize - 1;
    if (lx == 0) {
        invalidateMesh({coord.x - 1, coord.y, coord.z});
    }
    if (lx == last) {
        invalidateMesh({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
        invalidateMesh({coord.x, coord.y - 1, coord.z});
    }
    if (ly == last) {
        invalidateMesh({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
        invalidateMesh({coord.x, coord.y, coord.z - 1});
    }
    if (lz == last) {
        invalidateMesh({coord.x, coord.y, coord.z + 1});
    }
}

bool World::neighboursLoaded(const ChunkCoord& coord) const {
    return hasChunk({coord.x - 1, coord.y, coord.z}) && hasChunk({coord.x + 1, coord.y, coord.z}) &&
           hasChunk({coord.x, coord.y, coord.z - 1}) && hasChunk({coord.x, coord.y, coord.z + 1});
}

namespace {

/// How far a run of chests is followed before giving up. A row this long is not
/// something anyone builds by accident, and the cap keeps the walk bounded.
constexpr int kMaxChestRun = 64;

} // namespace

std::optional<glm::ivec3> World::chestPartnerAt(const glm::ivec3& at) const {
    const BlockId id = blockAt(at.x, at.y, at.z);
    if (!isChest(id) || !chestPairs(id)) {
        return std::nullopt;
    }
    const glm::ivec3 axis = chestJoinsAlongX(id) ? glm::ivec3{1, 0, 0} : glm::ivec3{0, 0, 1};
    const auto sameChest = [&](const glm::ivec3& cell) {
        return blockAt(cell.x, cell.y, cell.z) == id;
    };
    // Walk back to the start of the run, then pair off in twos from there. Both
    // halves reach the same answer because they do the same walk.
    int back = 0;
    while (back < kMaxChestRun && sameChest(at - axis * (back + 1))) {
        ++back;
    }
    const glm::ivec3 partner = back % 2 == 0 ? at + axis : at - axis;
    return sameChest(partner) ? std::optional<glm::ivec3>{partner} : std::nullopt;
}

ChestHalf World::chestHalfAt(const glm::ivec3& at) const {
    const std::optional<glm::ivec3> partner = chestPartnerAt(at);
    if (!partner) {
        return ChestHalf::Single;
    }
    const glm::ivec3 offset = *partner - at;
    return chestHalfFor(chestFacing(blockAt(at.x, at.y, at.z)), offset.x, offset.z);
}

void World::gatherVolume(const ChunkCoord& coord, ChunkVolume& volume) const {
    // The padded region spans at most one chunk in each direction, so the 27
    // possible source chunks are looked up once rather than per cell.
    const Chunk* sources[3][3][3]{};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                sources[dx + 1][dy + 1][dz + 1] = chunkAt({coord.x + dx, coord.y + dy, coord.z + dz});
            }
        }
    }

    constexpr int size = Chunk::kSize;
    constexpr int pad = ChunkVolume::kPad;

    // Blocks and light are overwritten in full below, but the flags are written
    // only where something is set, so they are cleared here. **Self-contained
    // on purpose**: relying on the caller to hand over zeroed storage is the
    // sort of unstated precondition that survives until somebody pools the
    // buffers, and then fails as stale waterlogging in a chunk that has none.
    volume.flags.fill(0u);

    // **Copied a row at a time, not a cell at a time.** X is the fastest-moving
    // axis in both layouts, so the thirty-two interior cells of every row are
    // one contiguous run in the source and one in the destination. This runs on
    // the main thread for every chunk that is meshed, which while flying is the
    // budget the whole streaming pipeline is metered against - the per-cell
    // version re-derived a chunk pointer, a local coordinate and three
    // bounds-checked accessors for each of thirty-nine thousand cells.
    const auto copyRow = [&](int y, int z) {
        const int dy = (y < 0) ? -1 : (y >= size ? 1 : 0);
        const int ly = y - dy * size;
        const int dz = (z < 0) ? -1 : (z >= size ? 1 : 0);
        const int lz = z - dz * size;

        // A row's three pieces: one cell from the -X neighbour, the whole
        // interior span, one cell from the +X neighbour.
        struct Piece {
            int dx;
            int fromX;
            int toX;
            int count;
        };
        const Piece pieces[3]{{-1, size - 1, -pad, pad}, {0, 0, 0, size}, {1, 0, size, pad}};

        for (const Piece& piece : pieces) {
            const Chunk* source = sources[piece.dx + 1][dy + 1][dz + 1];
            const std::size_t at = ChunkVolume::index(piece.toX, y, z);
            const auto count = static_cast<std::size_t>(piece.count);

            if (source == nullptr) {
                // Missing neighbours read as open sky rather than as solid, so
                // the streaming frontier does not draw a wall of faces or a
                // band of darkness.
                std::fill_n(volume.blocks.begin() + static_cast<std::ptrdiff_t>(at), count, BlockId::Air);
                std::fill_n(volume.light.begin() + static_cast<std::ptrdiff_t>(at), count,
                            static_cast<std::uint8_t>(0xF0));
                std::fill_n(volume.flags.begin() + static_cast<std::ptrdiff_t>(at), count,
                            static_cast<std::uint8_t>(0u));
                continue;
            }

            const std::size_t from = Chunk::cellIndex(piece.fromX, ly, lz);
            std::memcpy(volume.blocks.data() + at, source->data() + from, count * sizeof(BlockId));
            std::memcpy(volume.light.data() + at, source->lightData() + from, count);

            // The waterlogged bits are one per cell in the same order, so a run
            // along X is a run of consecutive bits - and an interior row starts
            // on a byte boundary, which is what lets the whole row be dismissed
            // with one test. **Almost every row in the world has none**, and
            // the caller hands us storage that is already zeroed, so the common
            // case is a single load and nothing written.
            const std::uint8_t* bits = source->waterloggedData();
            const std::size_t firstByte = from >> 3;
            const std::size_t byteCount = (count + 7) / 8;
            bool any = false;
            for (std::size_t b = 0; b < byteCount && !any; ++b) {
                any = bits[firstByte + b] != 0u;
            }
            if (!any) {
                continue;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t bit = from + i;
                if ((bits[bit >> 3] & (1u << (bit & 7u))) != 0u) {
                    volume.flags[at + i] |= ChunkVolume::kWaterlogged;
                }
            }
        }

        // Only a chest can answer anything but `Single`, and chests are rare, so
        // the run walk is never paid for on the cells that make up the world.
        for (int x = -pad; x < size + pad; ++x) {
            const std::size_t at = ChunkVolume::index(x, y, z);
            if (!isChest(volume.blocks[at])) {
                continue;
            }
            const glm::ivec3 world{coord.x * size + x, coord.y * size + y, coord.z * size + z};
            volume.flags[at] |= static_cast<std::uint8_t>(static_cast<int>(chestHalfAt(world))
                                                          << ChunkVolume::kChestHalfShift);
        }
    };

    for (int y = -pad; y < size + pad; ++y) {
        for (int z = -pad; z < size + pad; ++z) {
            copyRow(y, z);
        }
    }
}

void World::refreshQueues(const ChunkCoord& centre) {
    m_pendingLoad.clear();

    for (int dz = -m_loadRadius; dz <= m_loadRadius; ++dz) {
        for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
            for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
                const ChunkCoord coord{centre.x + dx, cy, centre.z + dz};
                if (!hasChunk(coord)) {
                    m_pendingLoad.push_back(coord);
                }
            }
        }
    }

    // Sorted farthest-first so the nearest chunk is at the back, where removing
    // it is free. Draining from the front would be quadratic.
    std::sort(m_pendingLoad.begin(), m_pendingLoad.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        return chebyshevDistance(a, centre) > chebyshevDistance(b, centre);
    });

    // Chunks that exist but were never meshed: they were dropped from the queue
    // because a neighbour was missing or because they sat outside the visible
    // radius. Nothing else would ever pick them up again, which shows as a
    // permanent hole when the player walks back toward them.
    //
    // **And chunks whose drawn tier is not the tier they should be at.** A
    // queued re-mesh can be dropped for the same two reasons, and a chunk that
    // is already meshed would otherwise never be looked at again - which is how
    // a plant fails to reappear until something unrelated dirties the chunk.
    //
    // Queued, not invalidated: their contents have not changed, and bumping the
    // revision here would throw away perfectly good work every time the player
    // crossed a chunk boundary.
    for (const auto& [coord, slot] : m_chunks) {
        if (chebyshevDistance(coord, centre) > m_visibleRadius) {
            continue;
        }
        if (!slot.meshed || slot.detailBuilt != slot.detailWanted) {
            queueMesh(coord);
        }
    }

    // The player's chunk has moved, so the detail boundary has moved with it.
    // This is the one place a chunk crosses it in either direction.
    refreshDetail(centre);
}

std::size_t World::jobCapacity() const {
    // Enough to keep every worker fed with a little slack, and no more. Split
    // between the two kinds of work so a long stream of loads cannot starve
    // meshing and leave the player standing in an invisible world.
    return static_cast<std::size_t>(m_jobs.threadCount()) * 2 + 2;
}

void World::dispatchLoads(const BudgetCheck& budgetSpent, std::size_t capacity) {
    // The budget matters even with workers, and matters completely without them:
    // a pool with no threads runs `submit` inline, so this loop *is* the work.
    while (!m_pendingLoad.empty() && m_loadInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();

        if (hasChunk(coord) || m_loadInFlight.count(coord) != 0) {
            continue;
        }

        m_loadInFlight.insert(coord);

        // Captures only copies and shared owners. Nothing here reaches back into
        // the world, which is what makes it safe to run anywhere.
        m_jobs.submit([coord, seed = m_seed, store = m_store, results = m_results] {
            // A saved chunk replaces generation entirely: it already contains
            // the generated terrain plus whatever the player did to it.
            std::optional<Chunk> stored = store->load(coord);
            Chunk blocks = stored.has_value() ? std::move(*stored) : generateChunk(seed, coord);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->loaded.push_back(LoadedChunk{coord, std::move(blocks)});
        });
    }
}

void World::dispatchMeshes(const ChunkCoord& centre, const BudgetCheck& budgetSpent, std::size_t capacity) {
    // **Nearest first, or the chunk you are flying toward waits behind the one
    // you just left.** The queue is drained from the back and entries arrive in
    // whatever order the chunk map iterates and light propagation dirties them,
    // which is effectively random - so with a deep queue a tier upgrade landed
    // at an arbitrary distance and plants appeared a couple of chunks away
    // rather than at the boundary.
    //
    // `nth_element` rather than a full sort: only the handful about to be
    // dispatched need to be the right ones, and this is linear where a sort of
    // several thousand entries every frame is not.
    if (m_pendingMesh.size() > capacity) {
        const auto farthestFirst = [&](const ChunkCoord& a, const ChunkCoord& b) {
            return chebyshevDistance(a, centre) > chebyshevDistance(b, centre);
        };
        std::nth_element(m_pendingMesh.begin(),
                         m_pendingMesh.end() - static_cast<std::ptrdiff_t>(capacity),
                         m_pendingMesh.end(), farthestFirst);
    }

    while (!m_pendingMesh.empty() && m_meshInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingMesh.back();
        m_pendingMesh.pop_back();
        m_pendingMeshSet.erase(coord);

        const auto it = m_chunks.find(coord);
        if (it == m_chunks.end()) {
            continue;
        }
        if (chebyshevDistance(coord, centre) > m_visibleRadius || !neighboursLoaded(coord)) {
            continue;
        }
        // Already being meshed. Dropping it is safe: if the chunk has changed
        // since that job started, the revision check on collection re-queues it.
        if (m_meshInFlight.count(coord) != 0) {
            continue;
        }

        m_meshInFlight.insert(coord);

        // The tier is resolved here, on the main thread, and travels with the
        // volume. A job that asked where the player was would give two chunks
        // meshed in the same frame different answers.
        const MeshDetail detail = detailFor(coord, it->second.detailWanted);
        it->second.detailWanted = detail == MeshDetail::Full;

        // The chunk and its borders are copied here, on the main thread, so the
        // job owns everything it reads. That copy is what removes the whole
        // question of what the main thread may do to this chunk meanwhile.
        auto input = std::make_shared<MeshJobInput>();
        input->coord = coord;
        input->revision = it->second.revision;
        input->detail = detail;
        input->origin = glm::vec3{static_cast<float>(coord.x * Chunk::kSize),
                                  static_cast<float>(coord.y * Chunk::kSize),
                                  static_cast<float>(coord.z * Chunk::kSize)};
        gatherVolume(coord, input->volume);
        m_jobs.submit([input = std::move(input), results = m_results] {
            ChunkMeshes meshes = meshChunk(input->volume, input->origin, input->detail);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->meshed.push_back(
                MeshedChunk{input->coord, std::move(meshes), input->revision, input->detail});
        });
    }
}

void World::collectFinishedJobs() {
    std::vector<LoadedChunk> loaded;
    std::vector<MeshedChunk> meshed;

    {
        std::lock_guard<std::mutex> lock(m_results->mutex);
        loaded.swap(m_results->loaded);
        meshed.swap(m_results->meshed);
    }

    for (LoadedChunk& result : loaded) {
        m_loadInFlight.erase(result.coord);

        // The player may have walked far enough that this is no longer wanted,
        // or a later pass may already have loaded it.
        if (hasChunk(result.coord)) {
            continue;
        }
        if (m_hasCentre && chebyshevDistance(result.coord, m_centre) > m_unloadRadius) {
            continue;
        }

        m_chunks.emplace(result.coord, ChunkSlot{std::move(result.blocks), false, false, false, false, 0});

        // Sky light is traced from the top of the world down, so it can only be
        // done once every chunk in the column is present.
        if (columnLoaded(result.coord.x, result.coord.z)) {
            seedColumnLight(result.coord.x, result.coord.z);
        }

        // The new chunk and its neighbours may all have gained or lost visible
        // faces along the shared border, so a neighbour already being meshed is
        // now building against a border that no longer matches.
        queueMesh(result.coord);
        invalidateMesh({result.coord.x - 1, result.coord.y, result.coord.z});
        invalidateMesh({result.coord.x + 1, result.coord.y, result.coord.z});
        invalidateMesh({result.coord.x, result.coord.y, result.coord.z - 1});
        invalidateMesh({result.coord.x, result.coord.y, result.coord.z + 1});
    }

    for (MeshedChunk& result : meshed) {
        m_meshInFlight.erase(result.coord);
        m_readyMeshes.push_back(std::move(result));
    }
}

std::vector<ChunkMeshUpdate> World::update(const glm::vec3& playerPosition, float budgetSeconds) {
    const auto start = Clock::now();
    std::vector<ChunkMeshUpdate> updates;

    const ChunkCoord centre{floorDiv(static_cast<int>(std::floor(playerPosition.x)), Chunk::kSize), 0,
                            floorDiv(static_cast<int>(std::floor(playerPosition.z)), Chunk::kSize)};

    if (!m_hasCentre || centre.x != m_centre.x || centre.z != m_centre.z) {
        m_centre = centre;
        m_hasCentre = true;

        // Unload first so memory is released before anything new is allocated.
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            if (chebyshevDistance(it->first, centre) > m_unloadRadius) {
                // Must happen before the erase, or an edited chunk is lost the
                // moment the player walks away from it.
                saveIfModified(it->first, it->second);

                // A column is only "lit" while all of it is resident. Leaving
                // the key behind makes `seedColumnLight` return immediately when
                // the chunk comes back, so it reloads with no sky light at all -
                // which is what lowering render distance and raising it again
                // used to do.
                m_litColumns.erase((static_cast<std::uint64_t>(static_cast<std::uint32_t>(it->first.x))
                                    << 32) |
                                   static_cast<std::uint32_t>(it->first.z));

                if (it->second.meshed) {
                    updates.push_back(ChunkMeshUpdate{it->first, {}, {}, true});
                }
                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }

        // Queued work for chunks that no longer exist or are out of range would
        // otherwise pile up forever as the player walks. In-flight jobs are left
        // alone; they are discarded on collection instead, because a running job
        // cannot be recalled.
        const auto outOfRange = [&](const ChunkCoord& c) {
            if (chebyshevDistance(c, centre) <= m_unloadRadius) {
                return false;
            }
            m_pendingMeshSet.erase(c);
            return true;
        };
        m_pendingMesh.erase(std::remove_if(m_pendingMesh.begin(), m_pendingMesh.end(), outOfRange),
                            m_pendingMesh.end());

        refreshQueues(centre);
    }

    if (m_radiusShrunk) {
        m_radiusShrunk = false;
        for (auto& [coord, slot] : m_chunks) {
            if (slot.meshed && chebyshevDistance(coord, centre) > m_visibleRadius) {
                slot.meshed = false;
                updates.push_back(ChunkMeshUpdate{coord, {}, {}, true});
            }
        }
    }

    collectFinishedJobs();

    const auto budgetSpent = [&] {
        return std::chrono::duration<float>(Clock::now() - start).count() >= budgetSeconds;
    };

    propagateLight(budgetSpent);
    flushLightDirty();
    updateFluids(budgetSpent);
    updateLava(budgetSpent);
    updateFire(budgetSpent);
    updateGrowth(budgetSpent);
    updateTnt(budgetSpent);
    updateFalls(budgetSpent);

    // Uploading is the only part still on the main thread, so it is what the
    // budget now meters.
    while (!m_readyMeshes.empty() && !budgetSpent()) {
        MeshedChunk result = std::move(m_readyMeshes.back());
        m_readyMeshes.pop_back();

        const auto it = m_chunks.find(result.coord);
        if (it == m_chunks.end()) {
            continue; // Unloaded while the job ran.
        }
        if (it->second.revision != result.revision) {
            // Something changed while the job ran. The revision has already moved
            // on, so this only needs re-queueing, not another bump.
            queueMesh(result.coord);
            continue;
        }

        it->second.meshed = true;
        it->second.detailBuilt = result.detail == MeshDetail::Full;
        updates.push_back(ChunkMeshUpdate{result.coord, std::move(result.meshes.opaque),
                                          std::move(result.meshes.translucent), false});
    }

    dispatchLoads(budgetSpent, jobCapacity());
    dispatchMeshes(centre, budgetSpent, jobCapacity());

    return updates;
}

bool World::isSettled() const {
    return pendingChunkCount() == 0 && m_skyAdditions.empty() && m_blockAdditions.empty() &&
           m_skyRemovals.empty() && m_blockRemovals.empty() && m_lightDirty.empty() && m_fluidUpdates.empty();
}

World::LoadStatus World::loadStatus() const {
    LoadStatus status;

    const int loadSpan = 2 * m_loadRadius + 1;
    const auto wantedChunks = static_cast<float>(loadSpan * loadSpan * kWorldHeightChunks);
    status.generated =
        wantedChunks <= 0.0f ? 1.0f : std::min(1.0f, static_cast<float>(m_chunks.size()) / wantedChunks);

    if (!m_hasCentre) {
        return status;
    }

    // **Counted over the visible box, not over whatever happens to be loaded.**
    // The old measure was meshed-over-loaded, which reads high from the very
    // first frame - two chunks in with one meshed is "half drawn" - and could
    // never reach 1 anyway, because the outer ring is loaded on purpose and
    // never meshed.
    std::size_t drawn = 0;
    std::size_t wanted = 0;
    for (int dz = -m_visibleRadius; dz <= m_visibleRadius; ++dz) {
        for (int dx = -m_visibleRadius; dx <= m_visibleRadius; ++dx) {
            for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
                ++wanted;
                const auto it = m_chunks.find(ChunkCoord{m_centre.x + dx, cy, m_centre.z + dz});
                if (it != m_chunks.end() && it->second.meshed) {
                    ++drawn;
                }
            }
        }
    }

    status.drawn = wanted == 0 ? 1.0f : static_cast<float>(drawn) / static_cast<float>(wanted);
    status.settled = isSettled();
    // Every chunk you could see is built and uploaded, every chunk the streamer
    // asked for exists, and every queue behind them is empty. Anything weaker
    // and the world carries on assembling after the bar has gone.
    status.complete = status.generated >= 1.0f && drawn == wanted && status.settled;
    return status;
}

float World::initialLoadProgress() const {
    const LoadStatus status = loadStatus();

    // Two weighted checkpoints rather than one number that guesses. Meshing gets
    // the larger share because it is the half you can actually see; the last
    // sliver is the drain, which is quick and would otherwise let the bar sit at
    // 100% while light and water finish behind it.
    constexpr float kGeneratedShare = 0.40f;
    constexpr float kDrawnShare = 0.59f;
    const float reported = kGeneratedShare * status.generated + kDrawnShare * status.drawn;

    return status.complete ? 1.0f : std::min(reported, 0.99f);
}

std::vector<ChunkMeshUpdate> World::loadImmediately(const glm::vec3& position) {
    std::vector<ChunkMeshUpdate> all;

    // Startup is the one place a stall is preferable to popping, and nothing is
    // moving yet, so every chunk is queued at once rather than in capacity-sized
    // batches with a barrier between each. Batching here left eleven workers
    // waiting on each other and was slower than doing it single-threaded.
    constexpr std::size_t kUnlimited = ~std::size_t{0};
    const BudgetCheck never = [] { return false; };

    const ChunkCoord centre{floorDiv(static_cast<int>(std::floor(position.x)), Chunk::kSize), 0,
                            floorDiv(static_cast<int>(std::floor(position.z)), Chunk::kSize)};

    // First pass establishes the centre and fills the load queue.
    std::vector<ChunkMeshUpdate> batch = update(position, 1000.0f);
    all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));

    for (int pass = 0; pass < 16; ++pass) {
        // Loads, then light, then meshes. Meshing before the light has settled
        // bakes darkness into the geometry, and the world then visibly brightens
        // over the next second as everything is rebuilt.
        dispatchLoads(never, kUnlimited);
        m_jobs.waitForIdle();
        collectFinishedJobs();

        propagateLight(never);
        flushLightDirty();

        dispatchMeshes(centre, never, kUnlimited);
        m_jobs.waitForIdle();
        collectFinishedJobs();

        // Newly arrived chunks unlock their neighbours for meshing, so this
        // repeats until neither queue has anything left.
        if (m_pendingLoad.empty() && m_pendingMesh.empty() && m_loadInFlight.empty() && m_meshInFlight.empty()) {
            break;
        }
    }

    batch = update(position, 1000.0f);
    all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));

    return all;
}

} // namespace game
