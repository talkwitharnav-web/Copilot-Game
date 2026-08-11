#pragma once

#include "world/Biome.hpp"
#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"

#include <cstdint>

namespace game {

/// Villages: the first structure here big enough that a single chunk only ever
/// sees a slice of one.
///
/// The rule that shapes everything below is the same one `structures` follows —
/// generation is a **pure function of `(seed, chunkCoord)`**, so nothing may be
/// pushed into a neighbour. Every chunk that a village touches rebuilds the
/// *entire* village layout from the seed and then writes only the blocks that
/// land inside its own bounds. Thirty chunks each solve the same plan and reach
/// the same answer, which is what makes the seams invisible.
///
/// Two consequences are load-bearing and easy to break:
///   - **The solver never sees a chunk.** `solve` takes a grid cell and nothing
///     else. The moment a bounds test could skip a draw, two chunks would
///     disagree about the rest of the village and a house would be cut in half.
///   - **Per-cell cosmetic choices are hashed off the world position**, never
///     drawn from the layout stream, so the order they are visited in cannot
///     matter at all.
namespace village {

/// Side of one placement cell, in **blocks**.
///
/// ⚠ The reference quotes `spacing` and `separation` in *chunks*, and its chunk
/// is sixteen blocks wide against our thirty-two. Writing its 34 into a
/// chunk-counted field here would silently double every village distance in the
/// world, which is the ported-number-in-the-wrong-unit trap this project keeps
/// paying for. Both numbers are therefore stated in blocks and never converted.
constexpr int kCellBlocks = 448;

/// Padding kept clear at the edge of every cell, so two villages in adjacent
/// cells cannot end up next to each other. The reference's `separation`.
constexpr int kSeparationBlocks = 160;

/// Furthest a village reaches from its town centre. **Stated, not emergent**:
/// the layout enforces it, because a chunk outside this radius never even looks
/// and would silently miss anything that escaped.
constexpr int kReach = 56;

/// Bounds on one plan. Every array is fixed-size so a plan can live on the
/// stack of a worker thread's generate call.
constexpr int kMaxBuildings = 24;
constexpr int kMaxRoads = 8;
constexpr int kMaxDecor = 28;
constexpr int kMaxResidents = 14;

/// The building shapes a village is assembled from.
///
/// Each is one silhouette with rolled detail rather than a template of its own —
/// roof style, window pattern, wall banding, chimney and interior are drawn per
/// building from its own seed. That is deliberately "varied, not unique": eleven
/// shapes and a handful of switches read as a village, where eleven unrelated
/// buildings read as a theme park.
enum class Design : std::uint8_t {
    TownCentre,
    SmallHouseA,
    SmallHouseB,
    SmallHouseC,
    MediumHouse,
    LargeHouse,
    Workshop,
    Library,
    Temple,
    Farm,
    AnimalPen,
    Count,
};

/// A placed building, in world blocks. The footprint is `[minX, minX + width)`.
struct Building {
    int minX = 0;
    int minZ = 0;
    int width = 0;
    int depth = 0;
    /// The floor is the cell **at** this height; the slab under it is one lower.
    int floorY = 0;
    Design design = Design::SmallHouseA;
    /// Outward normal of the wall the door sits in.
    FaceDirection facing = FaceDirection::NegZ;
    /// Job-site block for whoever ends up living here, or `Air` for none.
    BlockId workstation = BlockId::Air;
    /// Seeds every cosmetic choice inside the builder.
    std::uint32_t style = 0;
};

/// One straight run of street, three cells wide, terrain-matching.
struct Road {
    int x0 = 0;
    int z0 = 0;
    int x1 = 0;
    int z1 = 0;
};

struct Decor {
    int x = 0;
    int z = 0;
    /// 0 lamp post, 1 well kerb, 2 bench, 3 hay pile.
    std::uint8_t kind = 0;
};

/// Where a villager generated with the village stands, and what it starts as.
///
/// **Nobody starts with a profession**, which is the reference's own behaviour
/// and much the simplest thing to build: 5% babies, and of the adults 90%
/// unemployed and 10% nitwit. Every armourer you ever meet claimed a blast
/// furnace during its first morning.
struct Resident {
    int x = 0;
    int y = 0;
    int z = 0;
    bool baby = false;
    bool nitwit = false;
};

/// Why a candidate cell produced nothing.
///
/// Kept because "villages are rarer than expected" has exactly four possible
/// causes and reading the code cannot tell you which one is firing. The probe
/// prints the histogram; nothing else reads it.
enum class Reject : std::uint8_t {
    None,
    /// The biome at the origin hosts no villages.
    Biome,
    /// At or under the waterline.
    Water,
    /// A cave has eaten the ground the town centre would stand on.
    Carved,
    /// Too steep.
    Rough,
};

/// A solved village. Held by value, so it must stay small enough for a stack.
struct Plan {
    bool valid = false;
    Reject reject = Reject::None;
    VillageType type = VillageType::None;
    int originX = 0;
    int originZ = 0;
    int centreY = 0;
    int minX = 0;
    int maxX = 0;
    int minZ = 0;
    int maxZ = 0;
    int minY = 0;
    int maxY = 0;
    std::uint8_t buildingCount = 0;
    std::uint8_t roadCount = 0;
    std::uint8_t decorCount = 0;
    std::uint8_t residentCount = 0;
    Building buildings[kMaxBuildings];
    Road roads[kMaxRoads];
    Decor decor[kMaxDecor];
    Resident residents[kMaxResidents];
};

/// The villages whose footprint could reach one chunk column.
///
/// At most four, and the arithmetic that says so is worth keeping: a chunk spans
/// 32 blocks and an origin up to `kReach` away can touch it, so the window of
/// origins is `32 + 2 * kReach` = 144 blocks wide against a 448-block grid.
/// **Solving it once per chunk and handing it round is the point** — the tree
/// pass needs the same answer, and re-deriving it per tree cell would solve the
/// same plan a hundred and sixty-nine times.
struct Nearby {
    int count = 0;
    Plan plans[4];
};

/// Solves every village that could reach the given chunk column.
Nearby plansNear(std::uint32_t seed, int chunkX, int chunkZ);

/// Solves one grid cell outright, whether or not it produces anything. Exposed
/// for the worldgen probe; ordinary generation goes through `plansNear`.
Plan solveCell(std::uint32_t seed, int cellX, int cellZ);

/// Whether a column is claimed by a village, so nothing else may be planted on
/// it. Trees inside a footprint grow through roofs.
bool occupies(const Nearby& near, int worldX, int worldZ);

/// Writes every village block that lands inside this chunk.
void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord, const Nearby& near);

/// The villagers a village places, clipped to one chunk column.
int residentsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max);

/// Where a village's iron golem stands, if this chunk column holds its centre.
///
/// **One golem, placed with the village.** Bedrock spawns the first one the
/// moment a village generates and only adds more once there are ten villagers
/// and twenty beds — thresholds ours can never reach, so the ongoing spawn
/// cycle would be dead code. `Resident` is reused for the position; its two
/// flags mean nothing here.
int guardsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max);

/// Every block a villager will take as a job site, in profession order. Exposed
/// so the creature code and the village builder cannot disagree about the set.
constexpr int kJobSiteCount = 13;

} // namespace village
} // namespace game
