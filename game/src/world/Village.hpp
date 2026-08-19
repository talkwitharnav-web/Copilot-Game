#pragma once

#include "world/Biome.hpp"
#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"

#include <cstdint>

namespace game {

/// `Creature.hpp`'s species enum, declared opaquely so a pen can name the
/// animal in it **as a type rather than as a number**.
///
/// **Why a forward declaration and not the include.** `Structures.hpp` includes
/// this header, so including `Creature.hpp` here would pull 108 KB into every
/// translation unit that builds a tree, for one field. An opaque enum with a
/// fixed underlying type is a *complete* type — a member of it may be declared
/// and value-initialised — so the field costs nothing and stays typed.
///
/// **Why this cannot silently rot.** `Village.cpp` includes both this header
/// and `Creature.hpp`, so the compiler sees this declaration and the real
/// definition together in the one file that must agree with it. If the
/// underlying type ever changes, that file fails to compile. The check is
/// automatic and loud, which is the only reason an opaque declaration is
/// acceptable here at all.
///
/// **What was rejected, and why it matters.** The obvious alternative was to
/// reuse `Resident` and pack the species into its two bools. Two bools are four
/// states, and a species read back out of them is a value "routed through a
/// struct, a table or an index cast" — the pattern that produced five wrong
/// findings in this project on 2026-08-19 alone, because such a value is
/// invisible to a call-site search. `CreatureKind` stays the one table that
/// owns what a species is.
enum class CreatureKind : std::uint8_t;

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
///
/// ⚠ **That 34 and its 8 are `[JE]`.** They are the Java data pack's
/// `minecraft:village_plains` structure set, which ships as JSON and is
/// therefore quotable. Bedrock's placement is native C++ and appears in no file
/// of `Mojang/bedrock-samples`, so there is no Bedrock figure to port and none
/// is claimed here. The two below are ours, picked for the density the worldgen
/// probe measures — one village per 1.8 million blocks at seed 12 — and they
/// are not a conversion of anybody's.
constexpr int kCellBlocks = 448;

/// Padding kept clear at the edge of every cell, so two villages in adjacent
/// cells cannot end up next to each other. The reference's `separation`.
constexpr int kSeparationBlocks = 160;

/// Furthest a village reaches from its town centre. **Stated, not emergent**:
/// the layout enforces it, because a chunk outside this radius never even looks
/// and would silently miss anything that escaped.
constexpr int kReach = 56;

/// Furthest from the origin `occupies` may answer **true**.
///
/// Smaller than `kReach`, and the difference is exactly one structure reach.
/// A claimed column suppresses a tree, and a tree is planted by every chunk its
/// canopy touches — up to `structures::kReach` blocks away from its own trunk.
/// So a chunk can only be trusted to suppress a tree if *every* chunk that
/// could build that tree also has this village in its `Nearby`, and a chunk
/// only looks at villages whose origin is within `kReach`. Claim one block
/// further and one chunk drops the tree while its neighbour plants it: half a
/// tree standing at a chunk border, which is the worst failure this file has.
///
/// The `6` is `structures::kReach`, written here because `Structures.hpp`
/// already includes this header and the include cannot go both ways.
/// **`Structures.cpp` static_asserts that the two agree**, so the copy cannot
/// rot.
constexpr int kClaimReach = kReach - 6;

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
    /// 0 lamp post. **The only kind the layout produces today**, and the only
    /// one `buildDecor` knows how to build — a kerb, a bench and a hay pile
    /// were listed here long before anything placed one, which reads as four
    /// features where there is one.
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

/// How many villages one chunk column can be reached by.
///
/// **One, and that is arithmetic rather than luck.** `originOf` jitters an
/// origin over `[kSeparationBlocks / 2, kCellBlocks - kSeparationBlocks / 2)`
/// of its own cell, so two origins in cells that differ by one on an axis are
/// at least `kCellBlocks - (kCellBlocks - kSeparationBlocks - 1) =
/// kSeparationBlocks + 1` = 161 blocks apart *on that axis*. A chunk sees an
/// origin within `kReach` of any of its own 32 columns, which is a window
/// `Chunk::kSize + 2 * kReach` = 144 blocks wide. Any two distinct cells differ
/// on at least one axis, and on that axis 161 will not fit inside 144.
///
/// The slot of slack is kept because `plansNear` is the only writer and stops
/// at this bound, so being wrong costs a dropped village rather than a smashed
/// stack. Measured over 103,041 chunks at seed 12: **the count never exceeded
/// one, and 771 chunks carried a village.**
///
/// This was 4, which put four whole `Plan`s — 5.6 KB — on the stack and through
/// a by-value return for every chunk generated, three times per column in
/// `TerrainGenerator` and again per column in `Creature`. Three of them were
/// unreachable by construction.
constexpr int kMaxNearby = 2;

/// The villages whose footprint could reach one chunk column.
///
/// **Solving it once per chunk and handing it round is the point** — the tree
/// pass needs the same answer, and re-deriving it per tree cell would solve the
/// same plan a hundred and sixty-nine times.
struct Nearby {
    int count = 0;
    Plan plans[kMaxNearby];
};

static_assert(kSeparationBlocks + 1 > Chunk::kSize + 2 * kReach,
              "two village origins can now fall inside one chunk's search window, so kMaxNearby "
              "is no longer justified: raise it before raising kReach or lowering the separation");
static_assert(kMaxNearby >= 1, "plansNear would never record the village it just solved");

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

/// One animal standing in a pen, and which species it is.
///
/// **Not a `Plan` field, and that is load-bearing rather than stylistic.**
/// `guardsIn` is the precedent: it derives the golem's cell from plan geometry
/// at query time and stores nothing, because *generation must stay a pure
/// function of `(seed, chunkCoord)`* and *exactly one owner mutates the world,
/// on the main thread*. Deriving livestock the same way keeps both rules: the
/// pens are written by a worker, the animals are placed by `Creature.cpp`, and
/// no chunk has to tell another chunk anything.
///
/// It is also what unblocked this. The previous owner of this file declined to
/// add a `Plan` livestock list on the grounds that "a `Plan` field nothing
/// spawns from is the same defect one step further along", which was right. A
/// derivation has no such half-state: it does not exist until something asks.
struct Livestock {
    int x = 0;
    int y = 0;
    int z = 0;
    CreatureKind kind{};
};

/// How many animals one pen is stocked with, inclusive.
///
/// ⚠️ **SECONDARY SOURCE, AND HEDGED. Dated 2026-08-19.** `bedrock-samples`
/// carries no village structure data — the pens are `.mcstructure` binaries,
/// not JSON — so there is no primary figure to read, and this came from the
/// Minecraft Wiki's village page via search, which stated "most pens having 2-4
/// animals, sometimes less, occasionally more, but 2-4 is typical". That is a
/// *typical range*, not a rule, and the same summary contradicted this file's
/// own older note on two neighbouring points (it puts the desert camel at the
/// meeting point rather than in a pen, and denies armour stands are in pens at
/// all), so treat its confidence as low.
///
/// **What would settle it:** the entity list inside the pen `.mcstructure` in a
/// Bedrock release, or a decompiled `village/pens/*.json`. Until then this is a
/// documented placeholder chosen inside a published range, not a measurement.
/// **It is deliberately not 1 and not 6** — the failure this guards against is
/// an invented number entering as though it were reference-derived, which
/// survives for milestones because it looks like data.
constexpr int kMinPenAnimals = 2;
constexpr int kMaxPenAnimals = 4;

/// The most species one village type stocks its pens with.
///
/// **Shared deliberately, because it is the bound of a stack array.**
/// `livestockIn` declares `CreatureKind species[kMaxPenSpecies]` and
/// `penSpeciesFor` takes a *reference to an array of exactly that size*, so the
/// two cannot disagree — the bound is part of the function's type rather than a
/// literal written twice. Until 2026-08-19 11:41 both sites said a bare `4`.
///
/// That mattered: `Plains` already fills all four slots, and adding a chicken to
/// it is an entirely plausible edit — the reference does put chickens in plains
/// villages. With a `CreatureKind*` parameter, `out[4]` would have been a stack
/// write past the end with **no diagnostic available at all**, because a pointer
/// carries no bound. With the array reference the compiler has the size, so a
/// constant index past the end is reachable by its own checks.
///
/// **If a village type genuinely needs a fifth species, raise this — do not
/// widen the switch and leave the bound.** It is unrelated to `kMaxPenAnimals`
/// sharing its value today; one is a species count, the other a head count, and
/// tying them would be a coincidence dressed as a derivation.
constexpr int kMaxPenSpecies = 4;

static_assert(kMinPenAnimals >= 1 && kMinPenAnimals <= kMaxPenAnimals,
              "a pen would be stocked with nothing, which is the very bug this closes");

/// Every animal one chunk column can be handed at once.
///
/// **Derived, with its assumption named — and corrected once already, which is
/// why the derivation is written out rather than just the answer.** A pen
/// contributes its animals to the chunk holding its *centre* (the `guardsIn`
/// rule), so the bound is the number of pen centres one chunk can contain,
/// times `kMaxPenAnimals`. Pens are 7 wide and buildings do not overlap, so two
/// centres are at least 7 apart on one axis, and `Chunk::kSize` is **32**: that
/// admits centres at 0, 7, 14, 21 and 28, so **5 per axis, 25 centres, 100
/// animals**.
///
/// ⚠️ **This said 9 centres and 36 animals for twenty minutes on 2026-08-19,
/// because it was derived against a 16-wide chunk that this project has never
/// had.** `Chunk::kSize` is 32 and `Village.hpp`'s own `kMaxNearby` proof says
/// so out loud two hundred lines above — `Chunk::kSize + 2 * kReach` = 144 with
/// `kReach` 56, which only solves at 32. The number was too small by a factor
/// of nearly three, and the failure would have been **silent**: `livestockIn`
/// stops at `written < max`, so the cost is missing animals rather than a
/// smashed stack, and nothing would ever have reported it. `CLAUDE.md` bug
/// shape #3, a number ported against the wrong unit, caught only by writing the
/// `static_assert` that pins both inputs — which is now in `Village.cpp` beside
/// `livestockIn` and is the reason this cannot happen twice.
///
/// **The non-overlap is the layout's guarantee, not this header's**, which is
/// why the `written < max` stop stays exactly as its two siblings have it.
///
/// **The `Chunk::kSize` half of this is now computed rather than asserted, so
/// it cannot rot at all** (2026-08-19 11:26). That is deliberately the half
/// that broke: a chunk resize now re-derives the bound silently and correctly
/// instead of needing a human to notice. The only coupling left is `kPenSpan`
/// against the footprint table in `Village.cpp`, which is one variable and is
/// pinned by a `static_assert` beside `livestockIn`. A derivation beats an
/// assert beats a comment, and this is the derivation.
constexpr int kPenSpan = 7;

/// Pen centres one chunk column can hold on an axis, from the two facts above.
constexpr int kPenCentresPerAxis = Chunk::kSize / kPenSpan + 1;

constexpr int kMaxLivestock = kMaxPenAnimals * kPenCentresPerAxis * kPenCentresPerAxis;

/// The animals a village's pens are stocked with, clipped to one chunk column.
///
/// **Contract: `out[0 .. return - 1]` is written and the rest is INDETERMINATE.**
/// `out` is a raw array, so nothing structural stops a caller iterating to
/// `max` instead — and unlike a zero-filled table the surplus here is not a
/// harmless default, it is uninitialised stack holding a garbage `CreatureKind`
/// that `place()` would happily spawn. Both readers honour it today, verified
/// 2026-08-19 11:45 by tracing rather than counting: `Creature.cpp` loops
/// `i < stock`, and inside, the only read of the species array is
/// `species[roll.range(speciesCount)]`. The fill loop's own `written < max`
/// condition is what proves the returned count can never exceed `max`.
/// **Falsified by any read of `out` bounded by `kMaxLivestock` rather than by
/// the return value.**
///
/// A `std::span` return would carry the count with the data and make this
/// unstateable rather than merely stated — the right fix, deliberately NOT made
/// on 2026-08-19 because the tree cannot be compiled tonight and introducing a
/// new idiom blind is how a working thing breaks. Filed, not guessed at.
///
/// Pure, and a pure function of the plan alone — the per-pen roll is seeded off
/// `Building::style`, which the layout already derives from the world seed, so
/// this needs no seed argument and cannot disagree with itself between chunks.
int livestockIn(const Nearby& near, int chunkX, int chunkZ, Livestock* out, int max);

/// Every block a villager will take as a job site, in profession order. Exposed
/// so the creature code and the village builder cannot disagree about the set.
constexpr int kJobSiteCount = 13;

} // namespace village
} // namespace game
