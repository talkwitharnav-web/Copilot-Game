#include "world/Village.hpp"

#include "world/Noise.hpp"

#include <algorithm>
#include <array>

namespace game::village {
namespace {

using Roll = noise::Stream;

/// Salts. Separate constants so two different questions about the same cell can
/// never accidentally share a stream.
constexpr std::uint32_t kLayoutSalt = 0x5ea70002u;
constexpr std::uint32_t kDressSalt = 0x5ea70003u;

/// How rough the ground under the town centre may be before the site is
/// rejected outright, in blocks.
///
/// The reference does no flatness test at all and bends the terrain instead
/// (`beard_thin`, a density-function modifier). We have no density lattice any
/// more — it was removed deliberately at M20p — so the honest substitute is to
/// be picky about where a village starts. A strict site test costs one village
/// in a hilly region; a lax one costs a house sunk to its eaves in a hillside.
///
/// **Measured rather than chosen.** At 6 the probe rejected 79% of every
/// biome-eligible cell and villages came out one per 2000 blocks; at 11 it
/// rejects about a third and they land roughly a thousand apart, which is the
/// reference's own density. Individual plots still have `kMaxFill`/`kMaxCut` of
/// their own, so a loose site test costs a few dropped plots and not a sunken
/// house.
constexpr int kSiteRoughness = 11;

/// How far a single building may cut into or fill over the ground before the
/// plot is abandoned. Fill is allowed to be deeper than cut because a platform
/// under a house reads as deliberate and a house buried to its windows does not.
constexpr int kMaxFill = 6;
constexpr int kMaxCut = 3;

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

/// Finds a cut-shape family by the block it was cut from.
///
/// A search rather than a written-down index: `kStairFamilies` is the table that
/// owns the mapping, and a second copy of it here is exactly the bug this
/// codebase keeps paying for. It is `constexpr`, so it costs nothing at runtime.
constexpr int stairFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kStairFamilies.size(); ++f) {
        if (kStairFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int slabFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kSlabFamilies.size(); ++f) {
        if (kSlabFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int fenceFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kFenceFamilies.size(); ++f) {
        if (kFenceFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int gateFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kGateFamilies.size(); ++f) {
        if (kGateFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

constexpr int wallFamilyOf(BlockId parent) {
    for (std::size_t f = 0; f < kWallFamilies.size(); ++f) {
        if (kWallFamilies[f].parent == parent) {
            return static_cast<int>(f);
        }
    }
    return 0;
}

/// Door and trapdoor families are declared in `kWoods` order, so a wood is an
/// index rather than a search.
constexpr int kOakOpening = 0;
constexpr int kSpruceOpening = 1;
constexpr int kAcaciaOpening = 4;

/// Everything a village type differs by.
///
/// **The palette is the table and the building is one forwarding function.**
/// Five palettes over eleven shapes is fifty-five buildings for eleven authored
/// silhouettes, which is the same trade `ShapedFamily` made for six hundred cut
/// blocks. Writing five sets of builders instead would be five places for the
/// same bug to live.
struct Palette {
    BlockId wall;        ///< The bulk of a wall.
    BlockId wallAlt;     ///< A banding course, and the upper storey.
    BlockId post;        ///< Corner posts and framing.
    BlockId stone;       ///< Plinths, chimneys and the well.
    BlockId stoneAlt;    ///< Weathering, scattered through the stone.
    BlockId roof;        ///< Parent block the roof stairs and slabs are cut from.
    BlockId roofFill;    ///< Solid roof core, and the whole roof on a flat one.
    BlockId floor;       ///< Under the floor, and the doorstep.
    BlockId path;        ///< The street surface.
    BlockId bridge;      ///< What the street becomes over water.
    BlockId accent;      ///< Wool, terracotta or snow: the type's colour.
    BlockId lamp;        ///< On top of a lamp post.
    BlockId foundation;  ///< The platform poured under a building on a slope.
    int opening;         ///< Door and trapdoor family.
    int bedColour;
    int bedColourAlt;
    /// Flat roofs only, which is what makes a desert village unmistakable.
    bool flatRoofs;
    /// Lay snow over the exposed horizontal surfaces afterwards.
    bool snowy;
};

constexpr Palette kPalettes[] = {
    // None — never used, but the array is indexed by VillageType so it needs a row.
    {},
    // Plains: oak and cobblestone, white and yellow accents, torches on fence posts.
    {BlockId::Planks, BlockId::StrippedOakLog, BlockId::Log, BlockId::Cobblestone,
     BlockId::MossyCobblestone, BlockId::Planks, BlockId::Planks, BlockId::Cobblestone,
     BlockId::DirtPath, BlockId::Planks, BlockId::WhiteWool, BlockId::Torch, BlockId::Dirt,
     kOakOpening, 14, 4, false, false},
    // Desert: sandstone throughout, flat terracotta roofs, smooth sandstone streets.
    {BlockId::Sandstone, BlockId::CutSandstone, BlockId::ChiseledSandstone,
     BlockId::SmoothSandstone, BlockId::Sandstone, BlockId::SmoothSandstone,
     BlockId::OrangeTerracotta, BlockId::SmoothSandstone, BlockId::SmoothSandstone,
     BlockId::Planks, BlockId::LightBlueTerracotta, BlockId::Torch, BlockId::Sandstone,
     kOakOpening, 1, 14, true, false},
    // Savanna: acacia logs stand in for the cobblestone everywhere else.
    {BlockId::AcaciaPlanks, BlockId::StrippedAcaciaLog, BlockId::AcaciaLog, BlockId::AcaciaLog,
     BlockId::StrippedAcaciaLog, BlockId::AcaciaPlanks, BlockId::AcaciaPlanks,
     BlockId::AcaciaPlanks, BlockId::DirtPath, BlockId::AcaciaPlanks, BlockId::OrangeTerracotta,
     BlockId::Torch, BlockId::Dirt, kAcaciaOpening, 1, 14, false, false},
    // Taiga: spruce and cobblestone, purple and blue beds.
    {BlockId::SprucePlanks, BlockId::StrippedSpruceLog, BlockId::SpruceLog, BlockId::Cobblestone,
     BlockId::MossyCobblestone, BlockId::SprucePlanks, BlockId::SprucePlanks,
     BlockId::Cobblestone, BlockId::DirtPath, BlockId::SprucePlanks, BlockId::Cobblestone,
     BlockId::Torch, BlockId::Dirt, kSpruceOpening, 10, 11, false, false},
    // Snowy: the taiga set with snow and lanterns. **No architecture of its own**,
    // which is the reference's own arrangement rather than a shortcut.
    {BlockId::SprucePlanks, BlockId::StrippedSpruceLog, BlockId::SpruceLog, BlockId::Cobblestone,
     BlockId::SnowBlock, BlockId::SprucePlanks, BlockId::SprucePlanks, BlockId::Cobblestone,
     BlockId::DirtPath, BlockId::SprucePlanks, BlockId::SnowBlock, BlockId::Lantern,
     BlockId::Dirt, kSpruceOpening, 0, 3, false, true},
};

static_assert(std::size(kPalettes) == 6, "one palette per VillageType, None included");

const Palette& paletteFor(VillageType type) {
    return kPalettes[static_cast<std::size_t>(type)];
}

/// Every block a villager takes as a job site, one per profession, in the same
/// order `villagerProfessionFor` reads them.
constexpr BlockId kJobSites[kJobSiteCount] = {
    BlockId::Composter0,        // Farmer
    BlockId::Barrel,            // Fisherman
    BlockId::FletchingTable,    // Fletcher
    BlockId::Loom,              // Shepherd
    BlockId::CartographyTable,  // Cartographer
    BlockId::Lectern,           // Librarian
    BlockId::Stonecutter,       // Mason
    BlockId::SmithingTable,     // Toolsmith
    BlockId::Grindstone,        // Weaponsmith
    BlockId::BlastFurnace,      // Armourer
    BlockId::Smoker,            // Butcher
    BlockId::Cauldron,          // Leatherworker
    BlockId::BrewingStand,      // Cleric
};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct Footprint {
    int minX;
    int minZ;
    int width;
    int depth;
};

bool overlaps(const Footprint& a, const Footprint& b, int margin) {
    return a.minX - margin < b.minX + b.width && b.minX - margin < a.minX + a.width &&
           a.minZ - margin < b.minZ + b.depth && b.minZ - margin < a.minZ + a.depth;
}

/// The footprint a design occupies, before rotation. Width runs along X.
Footprint sizeOf(Design design) {
    switch (design) {
    case Design::TownCentre:
        return {0, 0, 9, 9};
    case Design::SmallHouseA:
        return {0, 0, 5, 6};
    case Design::SmallHouseB:
        return {0, 0, 6, 5};
    case Design::SmallHouseC:
        return {0, 0, 5, 5};
    case Design::MediumHouse:
        return {0, 0, 7, 7};
    case Design::LargeHouse:
        return {0, 0, 9, 7};
    case Design::Workshop:
        return {0, 0, 7, 6};
    case Design::Library:
        return {0, 0, 7, 8};
    case Design::Temple:
        return {0, 0, 5, 9};
    case Design::Farm:
        return {0, 0, 9, 7};
    case Design::AnimalPen:
        return {0, 0, 7, 7};
    default:
        return {0, 0, 5, 5};
    }
}

/// Whether a design is somewhere a villager lives, and therefore whether it
/// carries a bed and produces a resident.
bool isDwelling(Design design) {
    switch (design) {
    case Design::SmallHouseA:
    case Design::SmallHouseB:
    case Design::SmallHouseC:
    case Design::MediumHouse:
    case Design::LargeHouse:
    case Design::Workshop:
    case Design::Library:
        return true;
    default:
        return false;
    }
}

/// How the plot table is weighted. Small houses dominate, the big set pieces
/// punctuate — the same lopsided shape the reference's own pools have, and the
/// reason a village reads as houses with a library in it rather than as one of
/// each.
///
/// **The workshop share is deliberately higher than the reference's.** Vanilla
/// gives each of eleven workstation buildings weight 2 out of about 104, which
/// over a twenty-piece village yields two or three trades. Ours are eight
/// pieces, so the same *share* would leave most villages with no job site at
/// all — measured, one in fifteen came out with none. The absolute count is
/// what matters, so the share follows the village size.
struct PlotWeight {
    Design design;
    int weight;
};

constexpr PlotWeight kPlotWeights[] = {
    {Design::SmallHouseA, 8}, {Design::SmallHouseB, 8}, {Design::SmallHouseC, 7},
    {Design::MediumHouse, 6}, {Design::LargeHouse, 3},  {Design::Workshop, 12},
    {Design::Library, 3},     {Design::Temple, 2},      {Design::Farm, 5},
    {Design::AnimalPen, 3},
};

Design pickDesign(Roll& roll) {
    int total = 0;
    for (const PlotWeight& row : kPlotWeights) {
        total += row.weight;
    }
    int pick = roll.range(total);
    for (const PlotWeight& row : kPlotWeights) {
        pick -= row.weight;
        if (pick < 0) {
            return row.design;
        }
    }
    return Design::SmallHouseA;
}

struct Step {
    int dx;
    int dz;
};

constexpr Step kSteps[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

constexpr FaceDirection kOutward[4] = {FaceDirection::NegZ, FaceDirection::PosX,
                                       FaceDirection::PosZ, FaceDirection::NegX};

/// Solves one village from nothing but its grid cell.
///
/// **This function never sees a chunk, and it must stay that way.** Every draw
/// happens in a fixed order whatever the outcome, so thirty chunks each running
/// it reach the identical plan. The moment a bounds test could skip a roll, two
/// chunks would disagree about the rest of the village.
Plan solve(std::uint32_t seed, int cellX, int cellZ) {
    Plan plan;

    Roll roll{noise::hash2D(seed ^ kLayoutSalt, cellX, cellZ)};

    // Jittered inside the cell, with the separation kept clear at the edges so
    // two villages in neighbouring cells can never end up next to each other.
    const int span = kCellBlocks - kSeparationBlocks;
    plan.originX = cellX * kCellBlocks + kSeparationBlocks / 2 + roll.range(span);
    plan.originZ = cellZ * kCellBlocks + kSeparationBlocks / 2 + roll.range(span);

    const BiomeSample sample = sampleBiome(seed, plan.originX, plan.originZ);
    plan.type = biomeInfo(sample.dominant).villageType;
    if (plan.type == VillageType::None) {
        plan.reject = Reject::Biome;
        return plan;
    }

    plan.centreY = surfaceHeightAt(seed, plan.originX, plan.originZ);
    if (plan.centreY <= kSeaLevel + 2) {
        plan.reject = Reject::Water;
        return plan;
    }
    if (surfaceCarvedAt(seed, plan.originX, plan.originZ)) {
        plan.reject = Reject::Carved;
        return plan;
    }

    // Roughness. Twenty-five probes over the inner half of the footprint: too
    // few and a village straddles a ridge, too many and every candidate costs
    // real noise work for a question usually answered by the first two.
    int lowest = plan.centreY;
    int highest = plan.centreY;
    for (int pz = -2; pz <= 2; ++pz) {
        for (int px = -2; px <= 2; ++px) {
            const int h = surfaceHeightAt(seed, plan.originX + px * 11, plan.originZ + pz * 11);
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
        }
    }
    if (highest - lowest > kSiteRoughness) {
        plan.reject = Reject::Rough;
        return plan;
    }
    if (lowest <= kSeaLevel + 1) {
        plan.reject = Reject::Water;
        return plan;
    }

    // The town centre, square on the origin. **Its `style` carries four job
    // indices, four bits apiece**, which is what puts a row of market stalls
    // round the square: the reference's own `meeting_point_2` and `_4` are
    // stalls, and they are the reason a village has trades in it at all rather
    // than only in whichever workshops the plot roll happened to produce.
    Footprint centre{plan.originX - 4, plan.originZ - 4, 9, 9};
    const std::uint32_t stalls = roll.next();
    plan.buildings[plan.buildingCount++] = {centre.minX,          centre.minZ, centre.width,
                                            centre.depth,         plan.centreY, Design::TownCentre,
                                            FaceDirection::NegZ,  BlockId::Air, stalls};

    Footprint taken[kMaxBuildings];
    int takenCount = 0;
    taken[takenCount++] = centre;

    // Four streets, one per compass direction. Lengths are rolled up front so
    // the roll order cannot depend on whether a plot succeeded.
    int armLength[4];
    for (int arm = 0; arm < 4; ++arm) {
        armLength[arm] = 16 + roll.range(10);
    }

    // Which job site each workshop gets, dealt round-robin from a rolled start
    // so a village gets a spread of trades rather than five composters.
    int jobCursor = roll.range(kJobSiteCount);

    for (int arm = 0; arm < 4; ++arm) {
        const Step step = kSteps[arm];
        const int length = armLength[arm];
        const int endX = plan.originX + step.dx * length;
        const int endZ = plan.originZ + step.dz * length;
        if (plan.roadCount < kMaxRoads) {
            plan.roads[plan.roadCount++] = {plan.originX, plan.originZ, endX, endZ};
        }

        // Plots hang off the street at a fixed pitch, alternating sides. The
        // road is three wide, so a plot starts two cells out from its centre,
        // and the first ring starts far enough along that a nine-wide building
        // centred on it still clears the town square.
        for (int along = 9; along <= length - 2; along += 6) {
            for (int side = 0; side < 2; ++side) {
                const int outward = side == 0 ? 1 : -1;
                // The perpendicular direction, in world axes.
                const int px = step.dz * outward;
                const int pz = -step.dx * outward;

                const Design design = pickDesign(roll);
                const std::uint32_t style = roll.next();
                const bool leaveEmpty = roll.unit() < 0.12f;

                if (leaveEmpty || plan.buildingCount >= kMaxBuildings ||
                    takenCount >= kMaxBuildings) {
                    continue;
                }

                Footprint shape = sizeOf(design);
                // Turn the footprint so its depth runs away from the street.
                if (px != 0) {
                    std::swap(shape.width, shape.depth);
                }

                // The cell on the road the door will open onto.
                const int doorX = plan.originX + step.dx * along + px * 2;
                const int doorZ = plan.originZ + step.dz * along + pz * 2;

                // Anchored so the wall facing the street sits one cell out from
                // the road edge, centred on the plot.
                const int frontX = doorX + px;
                const int frontZ = doorZ + pz;
                Footprint spot{};
                if (px > 0) {
                    spot = {frontX, frontZ - shape.depth / 2, shape.width, shape.depth};
                } else if (px < 0) {
                    spot = {frontX - shape.width + 1, frontZ - shape.depth / 2, shape.width,
                            shape.depth};
                } else if (pz > 0) {
                    spot = {frontX - shape.width / 2, frontZ, shape.width, shape.depth};
                } else {
                    spot = {frontX - shape.width / 2, frontZ - shape.depth + 1, shape.width,
                            shape.depth};
                }

                // The whole plot has to stay inside the stated radius, or a
                // chunk beyond it would never look and would drop a wall.
                const int reachX = std::max(std::abs(spot.minX - plan.originX),
                                            std::abs(spot.minX + spot.width - plan.originX));
                const int reachZ = std::max(std::abs(spot.minZ - plan.originZ),
                                            std::abs(spot.minZ + spot.depth - plan.originZ));
                if (std::max(reachX, reachZ) > kReach - 2) {
                    continue;
                }

                bool clash = false;
                for (int i = 0; i < takenCount; ++i) {
                    if (overlaps(spot, taken[i], 2)) {
                        clash = true;
                        break;
                    }
                }
                if (clash) {
                    continue;
                }

                // The floor sits level with the street outside the door, which
                // is what puts the doorstep where a villager can walk over it.
                const int floorY = surfaceHeightAt(seed, doorX, doorZ) + 1;

                int worstFill = 0;
                int worstCut = 0;
                bool wet = false;
                for (int cz = 0; cz < spot.depth; ++cz) {
                    for (int cx = 0; cx < spot.width; ++cx) {
                        const int h = surfaceHeightAt(seed, spot.minX + cx, spot.minZ + cz);
                        worstFill = std::max(worstFill, floorY - 1 - h);
                        worstCut = std::max(worstCut, h - (floorY - 1));
                        wet = wet || h <= kSeaLevel;
                    }
                }
                if (wet || worstFill > kMaxFill || worstCut > kMaxCut) {
                    continue;
                }

                BlockId workstation = BlockId::Air;
                if (design == Design::Workshop) {
                    workstation = kJobSites[jobCursor % kJobSiteCount];
                    ++jobCursor;
                } else if (design == Design::Library) {
                    workstation = BlockId::Lectern;
                } else if (design == Design::Farm) {
                    workstation = BlockId::Composter0;
                } else if (design == Design::Temple) {
                    workstation = BlockId::BrewingStand;
                } else if ((style & 512u) != 0) {
                    // A quarter of the ordinary houses keep a trade indoors,
                    // which is what stops a small village having one
                    // profession in it. Ours are eight pieces where the
                    // reference's are twenty, so the *share* has to be higher
                    // for the absolute count to come out the same.
                    workstation = kJobSites[jobCursor % kJobSiteCount];
                    ++jobCursor;
                }

                // The door faces back at the street.
                const FaceDirection facing =
                    px > 0   ? FaceDirection::NegX
                    : px < 0 ? FaceDirection::PosX
                    : pz > 0 ? FaceDirection::NegZ
                             : FaceDirection::PosZ;

                plan.buildings[plan.buildingCount++] = {spot.minX,   spot.minZ, spot.width,
                                                        spot.depth,  floorY,    design,
                                                        facing,      workstation, style};
                taken[takenCount++] = spot;

                if (isDwelling(design) && plan.residentCount < kMaxResidents) {
                    Resident& who = plan.residents[plan.residentCount++];
                    who.x = spot.minX + spot.width / 2;
                    who.z = spot.minZ + spot.depth / 2;
                    who.y = floorY;
                    who.baby = roll.unit() < 0.05f;
                    who.nitwit = !who.baby && roll.unit() < 0.10f;
                }
            }
        }

        // Lamp posts down each street, on alternating sides.
        for (int along = 5; along <= length - 2; along += 9) {
            if (plan.decorCount >= kMaxDecor) {
                break;
            }
            const int outward = (along / 9) % 2 == 0 ? 1 : -1;
            Decor& lamp = plan.decor[plan.decorCount++];
            lamp.x = plan.originX + step.dx * along + step.dz * outward * 2;
            lamp.z = plan.originZ + step.dz * along - step.dx * outward * 2;
            lamp.kind = 0;
        }
    }

    // Bounds, taken from what was actually placed rather than from the radius,
    // so a chunk can reject a village on arithmetic before touching it.
    plan.minX = plan.originX - kReach;
    plan.maxX = plan.originX + kReach;
    plan.minZ = plan.originZ - kReach;
    plan.maxZ = plan.originZ + kReach;
    plan.minY = lowest - kMaxFill - 2;
    plan.maxY = highest + 16;
    plan.valid = true;
    return plan;
}

// ---------------------------------------------------------------------------
// Writing blocks
// ---------------------------------------------------------------------------

/// Clips one village against one chunk. Everything below writes through this,
/// which is what lets the same village be built by every chunk it touches.
struct Writer {
    Chunk& chunk;
    ChunkCoord coord;
    std::uint32_t seed;

    void put(int x, int y, int z, BlockId block, bool onlyIntoAir = false) const {
        const int lx = x - coord.x * Chunk::kSize;
        const int ly = y - coord.y * Chunk::kSize;
        const int lz = z - coord.z * Chunk::kSize;
        if (!Chunk::contains(lx, ly, lz)) {
            return;
        }
        if (onlyIntoAir && chunk.at(lx, ly, lz) != BlockId::Air) {
            return;
        }
        chunk.set(lx, ly, lz, block);
        chunk.setWaterlogged(lx, ly, lz, false);
    }

    void box(int x0, int y0, int z0, int x1, int y1, int z1, BlockId block,
             bool onlyIntoAir = false) const {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    put(x, y, z, block, onlyIntoAir);
                }
            }
        }
    }
};

/// A position-hashed roll, so a cosmetic choice cannot depend on the order the
/// cells happened to be visited in. **Every per-cell decision here uses this
/// rather than the layout stream**, which removes a whole class of seam.
float dress(std::uint32_t seed, int x, int z, std::uint32_t salt) {
    return noise::hashUnit2D(seed ^ (kDressSalt + salt), x, z);
}

/// Weathers a stone course: one block in ten becomes the palette's alternate.
/// The reference's `mossify_10_percent`, and it is most of why a village reads
/// as lived-in rather than as freshly stamped.
BlockId weathered(const Palette& palette, std::uint32_t seed, int x, int z) {
    return dress(seed, x, z, 11u) < 0.10f ? palette.stoneAlt : palette.stone;
}

constexpr Facing eaveFacing(int dx, int dz) {
    if (dx > 0) {
        return Facing::East;
    }
    if (dx < 0) {
        return Facing::West;
    }
    if (dz > 0) {
        return Facing::South;
    }
    return Facing::North;
}

/// A pitched roof over a rectangle, sloping down along the short axis. The two
/// ends are closed with a triangular gable of the wall material.
void gableRoof(const Writer& w, const Palette& palette, int x0, int z0, int x1, int z1, int base) {
    const int width = x1 - x0 + 1;
    const int depth = z1 - z0 + 1;
    const int stairs = stairFamilyOf(palette.roof);
    const int slabs = slabFamilyOf(palette.roof);
    const bool alongX = width >= depth;
    const int layers = ((alongX ? depth : width) + 1) / 2;

    for (int k = 0; k < layers; ++k) {
        const int y = base + k;
        if (alongX) {
            const int lo = z0 + k;
            const int hi = z1 - k;
            // One cell of overhang at each end, which is what stops a roof
            // reading as a lid sitting on a box.
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                w.put(x, y, lo, stairsAt(stairs, eaveFacing(0, -1), false));
                if (hi != lo) {
                    w.put(x, y, hi, stairsAt(stairs, eaveFacing(0, 1), false));
                }
            }
            if (hi - lo >= 2) {
                // Gable ends, and the hollow the roof encloses.
                for (int z = lo + 1; z <= hi - 1; ++z) {
                    w.put(x0, y, z, palette.wall);
                    w.put(x1, y, z, palette.wall);
                    for (int x = x0 + 1; x <= x1 - 1; ++x) {
                        w.put(x, y, z, BlockId::Air);
                    }
                }
            } else if (hi == lo) {
                for (int x = x0 - 1; x <= x1 + 1; ++x) {
                    w.put(x, y, lo, slabAt(slabs, false));
                }
            }
        } else {
            const int lo = x0 + k;
            const int hi = x1 - k;
            for (int z = z0 - 1; z <= z1 + 1; ++z) {
                w.put(lo, y, z, stairsAt(stairs, eaveFacing(-1, 0), false));
                if (hi != lo) {
                    w.put(hi, y, z, stairsAt(stairs, eaveFacing(1, 0), false));
                }
            }
            if (hi - lo >= 2) {
                for (int x = lo + 1; x <= hi - 1; ++x) {
                    w.put(x, y, z0, palette.wall);
                    w.put(x, y, z1, palette.wall);
                    for (int z = z0 + 1; z <= z1 - 1; ++z) {
                        w.put(x, y, z, BlockId::Air);
                    }
                }
            } else if (hi == lo) {
                for (int z = z0 - 1; z <= z1 + 1; ++z) {
                    w.put(lo, y, z, slabAt(slabs, false));
                }
            }
        }
    }
}

/// A flat roof with a parapet, which is what a desert village has instead.
void flatRoof(const Writer& w, const Palette& palette, int x0, int z0, int x1, int z1, int base) {
    const int slabs = slabFamilyOf(palette.roof);
    w.box(x0, base, z0, x1, base, z1, palette.roofFill);
    // A one-cell overhang all the way round, laid as slabs so the edge reads.
    for (int x = x0 - 1; x <= x1 + 1; ++x) {
        w.put(x, base, z0 - 1, slabAt(slabs, false));
        w.put(x, base, z1 + 1, slabAt(slabs, false));
    }
    for (int z = z0; z <= z1; ++z) {
        w.put(x0 - 1, base, z, slabAt(slabs, false));
        w.put(x1 + 1, base, z, slabAt(slabs, false));
    }
    // The parapet, broken at the corners so it looks built rather than extruded.
    for (int x = x0; x <= x1; ++x) {
        if ((x - x0) % 2 == 0) {
            w.put(x, base + 1, z0, palette.wallAlt);
            w.put(x, base + 1, z1, palette.wallAlt);
        }
    }
    for (int z = z0; z <= z1; ++z) {
        if ((z - z0) % 2 == 0) {
            w.put(x0, base + 1, z, palette.wallAlt);
            w.put(x1, base + 1, z, palette.wallAlt);
        }
    }
}

/// Fills the hole under a building and clears whatever the hill left inside it.
///
/// This is our stand-in for the reference's `beard_thin` and its visible support
/// platform at once. `beard_thin` modifies the density field before the surface
/// exists, which we have no lattice for; pouring a foundation afterwards is the
/// honest substitute and it is what the reference's platform does anyway.
/// **Circular rather than square**, which is Bedrock's own shape.
void foundation(const Writer& w, const Palette& palette, const Building& building, int clearance) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const float halfX = static_cast<float>(building.width) * 0.5f + 0.9f;
    const float halfZ = static_cast<float>(building.depth) * 0.5f + 0.9f;
    const float midX = static_cast<float>(x0 + x1) * 0.5f;
    const float midZ = static_cast<float>(z0 + z1) * 0.5f;

    for (int z = z0 - 2; z <= z1 + 2; ++z) {
        for (int x = x0 - 2; x <= x1 + 2; ++x) {
            const bool inside = x >= x0 && x <= x1 && z >= z0 && z <= z1;
            if (!inside) {
                const float u = (static_cast<float>(x) - midX) / halfX;
                const float v = (static_cast<float>(z) - midZ) / halfZ;
                if (u * u + v * v > 1.0f) {
                    continue;
                }
            }

            const int ground = surfaceHeightAt(w.seed, x, z);
            const int floorBase = building.floorY - 1;
            // **A skirt column over a big drop is not filled at all.** The loop
            // below stops after `kMaxFill` blocks, so filling one anyway would
            // leave a seven-tall pillar hanging in the air beside the house —
            // which is exactly the floating structure the plot test is careful
            // to avoid inside the footprint.
            if (!inside && floorBase - ground > kMaxFill) {
                continue;
            }
            for (int y = floorBase; y > std::max(ground, floorBase - kMaxFill - 1); --y) {
                w.put(x, y, z, y == floorBase && !inside ? palette.path : palette.foundation, true);
            }

            // Clear what the hill left standing. Inside the walls that is the
            // whole room; outside it only the first cell, so a house cut into a
            // slope keeps its bank instead of sitting in a trench.
            const int top = inside ? building.floorY + clearance : building.floorY + 1;
            for (int y = building.floorY; y <= top; ++y) {
                w.put(x, y, z, BlockId::Air);
            }
        }
    }
}

/// Where a wall's door goes, as an offset along that wall.
int doorSlot(int wallLength, std::uint32_t style) {
    const int slot = wallLength / 2;
    return slot + ((style >> 3) & 1u) * ((wallLength >= 7) ? 1 : 0) - ((wallLength >= 9) ? 1 : 0);
}

/// The one house builder. Everything that differs between the seven dwelling
/// shapes is a number passed in or a bit of `style`.
void buildHouse(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int floorY = building.floorY;
    const std::uint32_t style = building.style;

    int wallHeight = 4;
    switch (building.design) {
    case Design::SmallHouseC:
        wallHeight = 4 + static_cast<int>(style & 1u);
        break;
    case Design::MediumHouse:
        wallHeight = 5;
        break;
    case Design::LargeHouse:
        wallHeight = 7;
        break;
    case Design::Library:
        wallHeight = 6;
        break;
    case Design::Temple:
        wallHeight = 7;
        break;
    default:
        wallHeight = 4;
        break;
    }

    const bool stonePlinth = (style & 2u) != 0 || building.design == Design::Temple;
    const bool cornerPosts = (style & 4u) != 0 || building.design != Design::SmallHouseC;
    const int roofBase = floorY + wallHeight;

    foundation(w, palette, building, wallHeight + 4);

    // Floor, one below the walking surface, plus a course of the same under the
    // whole footprint so a room never opens onto a cave.
    w.box(x0, floorY - 1, z0, x1, floorY - 1, z1, palette.floor);

    // Walls.
    for (int y = floorY; y < roofBase; ++y) {
        const int rel = y - floorY;
        BlockId course = palette.wall;
        if (stonePlinth && rel == 0) {
            course = palette.stone;
        } else if (rel == wallHeight - 1 && (style & 8u) != 0) {
            course = palette.wallAlt;
        }
        for (int x = x0; x <= x1; ++x) {
            const BlockId here = (stonePlinth && rel == 0) ? weathered(palette, w.seed, x, z0)
                                                           : course;
            w.put(x, y, z0, here);
            w.put(x, y, z1, (stonePlinth && rel == 0) ? weathered(palette, w.seed, x, z1) : course);
        }
        for (int z = z0 + 1; z <= z1 - 1; ++z) {
            const BlockId here = (stonePlinth && rel == 0) ? weathered(palette, w.seed, x0, z)
                                                           : course;
            w.put(x0, y, z, here);
            w.put(x1, y, z, (stonePlinth && rel == 0) ? weathered(palette, w.seed, x1, z) : course);
        }
        // Interior air, which also carves whatever the hill left behind.
        w.box(x0 + 1, y, z0 + 1, x1 - 1, y, z1 - 1, BlockId::Air);
    }

    // Corner posts, which is what makes a plank box read as a framed building.
    if (cornerPosts) {
        for (int y = floorY; y < roofBase; ++y) {
            w.put(x0, y, z0, palette.post);
            w.put(x1, y, z0, palette.post);
            w.put(x0, y, z1, palette.post);
            w.put(x1, y, z1, palette.post);
        }
    }

    // Windows: glass panes at eye height, spaced so they never land on a corner.
    const BlockId panes = paneAt(0);
    const int windowY = floorY + 1 + static_cast<int>((style >> 4) & 1u);
    const int pitch = 2 + static_cast<int>((style >> 5) & 1u);
    for (int x = x0 + 2; x <= x1 - 2; x += pitch) {
        w.put(x, windowY, z0, panes);
        w.put(x, windowY, z1, panes);
    }
    for (int z = z0 + 2; z <= z1 - 2; z += pitch) {
        w.put(x0, windowY, z, panes);
        w.put(x1, windowY, z, panes);
    }
    if (wallHeight >= 6) {
        for (int x = x0 + 2; x <= x1 - 2; x += pitch) {
            w.put(x, windowY + 3, z0, panes);
            w.put(x, windowY + 3, z1, panes);
        }
    }

    // The door. Its facing is the outward normal of the wall it sits in, which
    // is what `facingToward` would have produced for someone standing outside.
    int doorX = 0;
    int doorZ = 0;
    switch (building.facing) {
    case FaceDirection::NegZ:
        doorX = x0 + doorSlot(building.width, style);
        doorZ = z0;
        break;
    case FaceDirection::PosZ:
        doorX = x0 + doorSlot(building.width, style);
        doorZ = z1;
        break;
    case FaceDirection::NegX:
        doorX = x0;
        doorZ = z0 + doorSlot(building.depth, style);
        break;
    default:
        doorX = x1;
        doorZ = z0 + doorSlot(building.depth, style);
        break;
    }
    doorX = std::clamp(doorX, x0 + 1, x1 - 1);
    doorZ = std::clamp(doorZ, z0 + 1, z1 - 1);
    // Snap back onto the wall the facing named — clamping moved it off a corner
    // and could otherwise have moved it off the wall entirely.
    if (building.facing == FaceDirection::NegZ) {
        doorZ = z0;
    } else if (building.facing == FaceDirection::PosZ) {
        doorZ = z1;
    } else if (building.facing == FaceDirection::NegX) {
        doorX = x0;
    } else {
        doorX = x1;
    }

    w.put(doorX, floorY, doorZ, doorAt(palette.opening, building.facing, false, false, false));
    w.put(doorX, floorY + 1, doorZ, doorAt(palette.opening, building.facing, false, false, true));

    // A doorstep, so a hillside house is not entered by jumping.
    const int stepX = doorX + (building.facing == FaceDirection::PosX    ? 1
                               : building.facing == FaceDirection::NegX ? -1
                                                                        : 0);
    const int stepZ = doorZ + (building.facing == FaceDirection::PosZ    ? 1
                               : building.facing == FaceDirection::NegZ ? -1
                                                                        : 0);
    w.put(stepX, floorY - 1, stepZ, palette.floor);
    w.put(stepX, floorY, stepZ, BlockId::Air);
    w.put(stepX, floorY + 1, stepZ, BlockId::Air);

    // A light beside the door rather than loose on the ground somewhere. The
    // reference lights a village from lamp posts, walls and doorsteps, and
    // nowhere else - torches standing about in the open is what reads as a
    // scattering rather than as a village.
    {
        const bool acrossX = building.facing == FaceDirection::NegZ ||
                             building.facing == FaceDirection::PosZ;
        const int lampX = stepX + (acrossX ? 1 : 0);
        const int lampZ = stepZ + (acrossX ? 0 : 1);
        w.put(lampX, floorY - 1, lampZ, palette.floor);
        w.put(lampX, floorY, lampZ, palette.lamp);
    }

    // Roof.
    if (palette.flatRoofs || (style & 16u) != 0) {
        flatRoof(w, palette, x0, z0, x1, z1, roofBase);
    } else {
        gableRoof(w, palette, x0, z0, x1, z1, roofBase);
    }

    // A chimney on about half of them, which is the cheapest silhouette break
    // there is. **Starts at the top wall course, not at the floor** — run down
    // to the hearth it would stand in the middle of the room the workstation
    // and the bed already share.
    if ((style & 32u) != 0 && building.design != Design::Temple) {
        const int cx = x0 + 1;
        const int cz = z1 - 1;
        for (int y = roofBase - 1; y <= roofBase + 3; ++y) {
            w.put(cx, y, cz, weathered(palette, w.seed, cx, cz + y));
        }
    }

    // Interior. A bed, a light, and whatever this building is for.
    const int bedX = x1 - 1;
    const int bedZ = z0 + 1;
    const int colour = (style & 64u) != 0 ? palette.bedColour : palette.bedColourAlt;
    w.put(bedX, floorY, bedZ, bedAt(colour, FaceDirection::PosZ, false));
    w.put(bedX, floorY, bedZ + 1, bedAt(colour, FaceDirection::PosZ, true));

    if (building.design == Design::Library) {
        // Shelves down one long wall, which is the whole of what makes a
        // library legible from the doorway.
        for (int z = z0 + 1; z <= z1 - 2; ++z) {
            w.put(x0 + 1, floorY, z, BlockId::Bookshelf);
            w.put(x0 + 1, floorY + 1, z, BlockId::Bookshelf);
        }
    } else if (building.design == Design::LargeHouse) {
        // The upper storey gets a floor of its own and a ladder to reach it.
        const int upper = floorY + 4;
        w.box(x0 + 1, upper, z0 + 1, x1 - 1, upper, z1 - 1, palette.wallAlt);
        w.put(x1 - 1, upper, z1 - 1, BlockId::Air);
        for (int y = floorY; y < upper; ++y) {
            w.put(x1 - 1, y, z1 - 1, ladderFacing(FaceDirection::NegX));
        }
        w.put(x0 + 1, upper + 1, z0 + 1, bedAt(colour, FaceDirection::PosX, false));
        w.put(x0 + 2, upper + 1, z0 + 1, bedAt(colour, FaceDirection::PosX, true));
        w.put(x1 - 1, upper + 1, z1 - 2, BlockId::Torch);
    } else if (building.design == Design::Temple) {
        // A stair climbing one wall, and a light at the head of it, so the tall
        // building stands over the roofs around it.
        const int stairs = stairFamilyOf(palette.roof);
        for (int k = 0; k < 4 && z0 + 1 + k <= z1 - 1; ++k) {
            w.put(x0 + 1, floorY + k, z0 + 1 + k, stairsAt(stairs, eaveFacing(0, 1), false));
        }
        w.put(x0 + 1, roofBase + 2, std::min(z0 + 2, z1 - 1), palette.lamp);
    } else if (building.design == Design::Workshop) {
        // An open counter facing the street, which is what separates a shop from
        // a house with a job block in it.
        const int slabs = slabFamilyOf(palette.roof);
        if (building.facing == FaceDirection::NegZ || building.facing == FaceDirection::PosZ) {
            const int counterZ = building.facing == FaceDirection::NegZ ? z0 + 1 : z1 - 1;
            for (int x = x0 + 1; x <= x1 - 1; ++x) {
                if (x != doorX) {
                    w.put(x, floorY, counterZ, slabAt(slabs, true));
                }
            }
        } else {
            const int counterX = building.facing == FaceDirection::NegX ? x0 + 1 : x1 - 1;
            for (int z = z0 + 1; z <= z1 - 1; ++z) {
                if (z != doorZ) {
                    w.put(counterX, floorY, z, slabAt(slabs, true));
                }
            }
        }
    }

    // **The job block goes in after the fittings, not before.** The workshop's
    // counter runs along whichever wall faces the street, and for two of the
    // four facings that is the same cell the workstation sits in — placing it
    // first left a shop with a slab where its trade should be, which is exactly
    // the kind of fault a build and a soak have nothing to say about.
    if (building.workstation != BlockId::Air) {
        BlockId job = building.workstation;
        if (isFurnace(job)) {
            job = cookerAt(job, oppositeDirection(building.facing), false);
        }
        w.put(x0 + 1, floorY, z1 - 1, job);
    } else if ((style & 128u) != 0) {
        w.put(x0 + 1, floorY, z1 - 1, BlockId::CraftingTable);
    }

    // The light goes in last, so nothing above can land on top of it.
    w.put(x0 + 1, floorY, z0 + 1, BlockId::Torch);
}

/// A well or a market square, whichever the roll picked. Both are nine across.
void buildTownCentre(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + 8;
    const int z1 = z0 + 8;
    const int y = building.floorY;
    const int stairs = stairFamilyOf(palette.roof);
    const int fences = fenceFamilyOf(palette.wall);

    foundation(w, palette, building, 8);

    // The apron: a paved square with its corners cut off, which is what stops it
    // reading as a slab dropped on the grass.
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = std::abs(x - (x0 + 4));
            const int dz = std::abs(z - (z0 + 4));
            if (dx + dz > 6) {
                continue;
            }
            const bool inner = dx <= 2 && dz <= 2;
            w.put(x, y - 1, z, inner ? weathered(palette, w.seed, x, z) : palette.path);
            w.box(x, y, z, x, y + 3, z, BlockId::Air);
        }
    }

    // The well itself: a rim of stone round a column of water, four posts and a
    // roof over it.
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        for (int x = x0 + 3; x <= x1 - 3; ++x) {
            w.put(x, y, z, weathered(palette, w.seed, x, z));
        }
    }
    w.put(x0 + 4, y, z0 + 4, BlockId::Water0);
    w.put(x0 + 4, y - 1, z0 + 4, BlockId::Water0);
    w.put(x0 + 4, y - 2, z0 + 4, palette.stone);

    for (int k = 1; k <= 2; ++k) {
        w.put(x0 + 3, y + k, z0 + 3, fenceAt(fences));
        w.put(x1 - 3, y + k, z0 + 3, fenceAt(fences));
        w.put(x0 + 3, y + k, z1 - 3, fenceAt(fences));
        w.put(x1 - 3, y + k, z1 - 3, fenceAt(fences));
    }
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        for (int x = x0 + 3; x <= x1 - 3; ++x) {
            w.put(x, y + 3, z, palette.roofFill);
        }
    }
    for (int x = x0 + 3; x <= x1 - 3; ++x) {
        w.put(x, y + 3, z0 + 2, stairsAt(stairs, eaveFacing(0, -1), false));
        w.put(x, y + 3, z1 - 2, stairsAt(stairs, eaveFacing(0, 1), false));
    }
    for (int z = z0 + 3; z <= z1 - 3; ++z) {
        w.put(x0 + 2, y + 3, z, stairsAt(stairs, eaveFacing(-1, 0), false));
        w.put(x1 - 2, y + 3, z, stairsAt(stairs, eaveFacing(1, 0), false));
    }

    // The bell hangs at the meeting point, because that is what a villager
    // gathers at.
    w.put(x0 + 4, y + 4, z0 + 4, BlockId::Bell);

    // Four market stalls on the apron, a job block apiece under a plank awning
    // on a post. **This is where most of a village's trades come from**, and
    // that is deliberate: the plot roll on its own left five villages in six
    // with a single profession in them, which is not what a village is.
    struct Stall {
        int dx;
        int dz;
        int backX;
        int backZ;
    };
    constexpr std::array<Stall, 4> kStalls{
        {{1, 4, 1, 0}, {7, 4, -1, 0}, {4, 1, 0, 1}, {4, 7, 0, -1}}};
    for (int i = 0; i < 4; ++i) {
        const Stall& stall = kStalls[static_cast<std::size_t>(i)];
        const int sx = x0 + stall.dx;
        const int sz = z0 + stall.dz;
        BlockId job = kJobSites[(building.style >> (i * 4)) % kJobSiteCount];
        if (isFurnace(job)) {
            // Turned to face the square, so its mouth is the side a customer
            // stands at.
            job = cookerAt(job,
                           stall.backX > 0   ? FaceDirection::NegX
                           : stall.backX < 0 ? FaceDirection::PosX
                           : stall.backZ > 0 ? FaceDirection::NegZ
                                             : FaceDirection::PosZ,
                           false);
        }
        w.put(sx, y, sz, job);
        // The post stands behind the counter and the awning leans out over it.
        const int px = sx + stall.backX;
        const int pz = sz + stall.backZ;
        w.put(px, y, pz, fenceAt(fences));
        w.put(px, y + 1, pz, fenceAt(fences));
        w.put(px, y + 2, pz, palette.roofFill);
        w.put(sx, y + 2, sz, stairsAt(stairs, eaveFacing(-stall.backX, -stall.backZ), false));
    }

    // Lights at the four corners of the square, **on posts**. Stood loose on
    // the paving they read as torches dropped at random, which is what a
    // village is not - the reference only ever puts one on a lamp post, on a
    // wall, or beside a door.
    constexpr std::array<std::array<int, 2>, 4> kCorners{{{1, 1}, {7, 1}, {1, 7}, {7, 7}}};
    for (const std::array<int, 2>& corner : kCorners) {
        const int cx = x0 + corner[0];
        const int cz = z0 + corner[1];
        w.put(cx, y, cz, palette.stone);
        w.put(cx, y + 1, cz, fenceAt(fences));
        w.put(cx, y + 2, cz, palette.lamp);
    }
}

/// A worked field: farmland either side of a water channel, fenced, with a
/// composter at the near corner.
void buildFarm(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int y = building.floorY;
    const int fences = fenceFamilyOf(palette.wall);
    const int gates = gateFamilyOf(palette.wall);

    foundation(w, palette, building, 3);

    w.box(x0, y - 1, z0, x1, y - 1, z1, BlockId::Dirt);
    w.box(x0, y, z0, x1, y + 2, z1, BlockId::Air);

    const bool channelAlongX = building.width >= building.depth;
    const int midX = (x0 + x1) / 2;
    const int midZ = (z0 + z1) / 2;

    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        for (int x = x0 + 1; x <= x1 - 1; ++x) {
            const bool channel = channelAlongX ? z == midZ : x == midX;
            if (channel) {
                w.put(x, y - 1, z, BlockId::Water0);
                continue;
            }
            w.put(x, y - 1, z, BlockId::FarmlandMoist);
            // Four crops mixed in the reference's own proportions, and every
            // one of them at a rolled age so a field is not a parade ground.
            const float pick = dress(w.seed, x, z, 3u);
            const BlockId family = pick < 0.40f   ? BlockId::WheatCrop0
                                   : pick < 0.70f ? BlockId::CarrotCrop0
                                   : pick < 0.90f ? BlockId::PotatoCrop0
                                                  : BlockId::BeetrootCrop0;
            const int age = static_cast<int>(dress(w.seed, x, z, 4u) * 8.0f) & 7;
            w.put(x, y, z, cropAt(family, age));
        }
    }

    // The fence, with a gate on the street side.
    for (int x = x0; x <= x1; ++x) {
        w.put(x, y, z0, fenceAt(fences));
        w.put(x, y, z1, fenceAt(fences));
    }
    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        w.put(x0, y, z, fenceAt(fences));
        w.put(x1, y, z, fenceAt(fences));
    }
    switch (building.facing) {
    case FaceDirection::NegZ:
        w.put(midX, y, z0, gateAt(gates, FaceDirection::NegZ, false));
        break;
    case FaceDirection::PosZ:
        w.put(midX, y, z1, gateAt(gates, FaceDirection::PosZ, false));
        break;
    case FaceDirection::NegX:
        w.put(x0, y, midZ, gateAt(gates, FaceDirection::NegX, false));
        break;
    default:
        w.put(x1, y, midZ, gateAt(gates, FaceDirection::PosX, false));
        break;
    }

    w.put(x0, y, z0, composterAt(0));
    w.put(x1, y, z1, BlockId::HayBlock);
    w.put(x1, y + 1, z1, palette.lamp);
}

/// A fenced paddock with a gate and a bale, which is where a village's animals
/// end up.
void buildPen(const Writer& w, const Palette& palette, const Building& building) {
    const int x0 = building.minX;
    const int z0 = building.minZ;
    const int x1 = x0 + building.width - 1;
    const int z1 = z0 + building.depth - 1;
    const int y = building.floorY;
    const int fences = fenceFamilyOf(palette.wall);
    const int gates = gateFamilyOf(palette.wall);

    foundation(w, palette, building, 3);

    w.box(x0, y - 1, z0, x1, y - 1, z1, BlockId::Grass);
    w.box(x0, y, z0, x1, y + 2, z1, BlockId::Air);
    for (int x = x0; x <= x1; ++x) {
        w.put(x, y, z0, fenceAt(fences));
        w.put(x, y, z1, fenceAt(fences));
    }
    for (int z = z0 + 1; z <= z1 - 1; ++z) {
        w.put(x0, y, z, fenceAt(fences));
        w.put(x1, y, z, fenceAt(fences));
    }

    const int midX = (x0 + x1) / 2;
    const int midZ = (z0 + z1) / 2;
    switch (building.facing) {
    case FaceDirection::NegZ:
        w.put(midX, y, z0, gateAt(gates, FaceDirection::NegZ, false));
        break;
    case FaceDirection::PosZ:
        w.put(midX, y, z1, gateAt(gates, FaceDirection::PosZ, false));
        break;
    case FaceDirection::NegX:
        w.put(x0, y, midZ, gateAt(gates, FaceDirection::NegX, false));
        break;
    default:
        w.put(x1, y, midZ, gateAt(gates, FaceDirection::PosX, false));
        break;
    }

    w.put(midX, y, midZ, BlockId::HayBlock);
    w.put(midX, y + 1, midZ, BlockId::HayBlock);
}

/// Streets: terrain-matching, per column, which is the one thing a village does
/// that a building must never do.
///
/// The reference reaches the same result twice over — it slides the whole piece
/// onto the heightmap when it places it, then drops each column individually
/// through a gravity processor. Ours is the second half only, which is all that
/// is needed when the road is not made of pieces.
void buildRoad(const Writer& w, const Palette& palette, const Road& road) {
    const int dx = road.x1 == road.x0 ? 0 : (road.x1 > road.x0 ? 1 : -1);
    const int dz = road.z1 == road.z0 ? 0 : (road.z1 > road.z0 ? 1 : -1);
    const int length = std::max(std::abs(road.x1 - road.x0), std::abs(road.z1 - road.z0));

    for (int t = 0; t <= length; ++t) {
        for (int side = -1; side <= 1; ++side) {
            const int x = road.x0 + dx * t + dz * side;
            const int z = road.z0 + dz * t - dx * side;
            const int ground = surfaceHeightAt(w.seed, x, z);

            if (ground <= kSeaLevel) {
                // Over water the street becomes a plank bridge, which is the
                // reference's own substitution.
                w.put(x, kSeaLevel + 1, z, palette.bridge);
                w.box(x, kSeaLevel + 2, z, x, kSeaLevel + 4, z, BlockId::Air);
                continue;
            }

            // One cell in ten stays grass. It is the cheapest possible detail
            // and it is most of why a village road reads as worn rather than
            // stamped.
            const bool bare = dress(w.seed, x, z, 7u) < 0.12f;
            w.put(x, ground, z, bare ? BlockId::Grass : palette.path);
            w.put(x, ground - 1, z, palette.foundation, true);
            w.box(x, ground + 1, z, x, ground + 3, z, BlockId::Air);
        }
    }
}

void buildDecor(const Writer& w, const Palette& palette, const Decor& item) {
    const int ground = surfaceHeightAt(w.seed, item.x, item.z);
    if (ground <= kSeaLevel) {
        return;
    }
    const int fences = fenceFamilyOf(palette.wall);
    switch (item.kind) {
    case 0: {
        // A lamp post: three fence sections and a light on top, which is the
        // reference's `<type>_lamp_1` in every village it has.
        w.put(item.x, ground, item.z, palette.stone);
        for (int k = 1; k <= 3; ++k) {
            w.put(item.x, ground + k, item.z, fenceAt(fences));
        }
        w.put(item.x, ground + 4, item.z, palette.lamp);
        break;
    }
    default:
        break;
    }
}

/// Snow settles on whatever the sky can see, which is the whole of what makes a
/// snowy village different from a taiga one.
void snowOver(const Writer& w, const Plan& plan, ChunkCoord coord) {
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int lz = 0; lz < Chunk::kSize; ++lz) {
        const int z = baseZ + lz;
        if (z < plan.minZ || z > plan.maxZ) {
            continue;
        }
        for (int lx = 0; lx < Chunk::kSize; ++lx) {
            const int x = baseX + lx;
            if (x < plan.minX || x > plan.maxX) {
                continue;
            }
            // Top down, so the first solid block found is the exposed one.
            for (int ly = Chunk::kSize - 1; ly >= 1; --ly) {
                const BlockId here = w.chunk.at(lx, ly, lz);
                const BlockId above = w.chunk.at(lx, ly + 1, lz);
                if (here == BlockId::Air || above != BlockId::Air) {
                    continue;
                }
                if (!occludesFace(here, 1) || isSnowLayer(here)) {
                    break;
                }
                if (baseY + ly + 1 <= plan.maxY) {
                    w.put(x, baseY + ly + 1, z, snowLayerAt(0), true);
                }
                break;
            }
        }
    }
}

} // namespace

Plan solveCell(std::uint32_t seed, int cellX, int cellZ) { return solve(seed, cellX, cellZ); }

Nearby plansNear(std::uint32_t seed, int chunkX, int chunkZ) {
    Nearby out;

    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    const int firstX = floorDivInt(baseX - kReach, kCellBlocks);
    const int lastX = floorDivInt(baseX + Chunk::kSize + kReach, kCellBlocks);
    const int firstZ = floorDivInt(baseZ - kReach, kCellBlocks);
    const int lastZ = floorDivInt(baseZ + Chunk::kSize + kReach, kCellBlocks);

    for (int cz = firstZ; cz <= lastZ && out.count < 4; ++cz) {
        for (int cx = firstX; cx <= lastX && out.count < 4; ++cx) {
            // Reject on arithmetic before any noise work: the origin is one
            // hash, and most candidates are hundreds of blocks away.
            Roll probe{noise::hash2D(seed ^ kLayoutSalt, cx, cz)};
            const int span = kCellBlocks - kSeparationBlocks;
            const int originX = cx * kCellBlocks + kSeparationBlocks / 2 + probe.range(span);
            const int originZ = cz * kCellBlocks + kSeparationBlocks / 2 + probe.range(span);
            if (originX + kReach < baseX || originX - kReach >= baseX + Chunk::kSize ||
                originZ + kReach < baseZ || originZ - kReach >= baseZ + Chunk::kSize) {
                continue;
            }

            Plan plan = solve(seed, cx, cz);
            if (plan.valid) {
                out.plans[out.count++] = plan;
            }
        }
    }

    return out;
}

bool occupies(const Nearby& near, int worldX, int worldZ) {
    for (int i = 0; i < near.count; ++i) {
        const Plan& plan = near.plans[i];
        for (int b = 0; b < plan.buildingCount; ++b) {
            const Building& building = plan.buildings[b];
            if (worldX >= building.minX - 3 && worldX < building.minX + building.width + 3 &&
                worldZ >= building.minZ - 3 && worldZ < building.minZ + building.depth + 3) {
                return true;
            }
        }
        for (int r = 0; r < plan.roadCount; ++r) {
            const Road& road = plan.roads[r];
            const int lowX = std::min(road.x0, road.x1) - 2;
            const int highX = std::max(road.x0, road.x1) + 2;
            const int lowZ = std::min(road.z0, road.z1) - 2;
            const int highZ = std::max(road.z0, road.z1) + 2;
            if (worldX >= lowX && worldX <= highX && worldZ >= lowZ && worldZ <= highZ) {
                return true;
            }
        }
    }
    return false;
}

void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord, const Nearby& near) {
    const Writer writer{chunk, coord, seed};
    const int baseY = coord.y * Chunk::kSize;

    for (int i = 0; i < near.count; ++i) {
        const Plan& plan = near.plans[i];
        // Two chunk layers in three are entirely below or above any village.
        if (baseY > plan.maxY || baseY + Chunk::kSize <= plan.minY) {
            continue;
        }

        const Palette& palette = paletteFor(plan.type);

        // Streets first, so a building's own doorstep wins where the two meet.
        for (int r = 0; r < plan.roadCount; ++r) {
            buildRoad(writer, palette, plan.roads[r]);
        }

        for (int b = 0; b < plan.buildingCount; ++b) {
            const Building& building = plan.buildings[b];
            switch (building.design) {
            case Design::TownCentre:
                buildTownCentre(writer, palette, building);
                break;
            case Design::Farm:
                buildFarm(writer, palette, building);
                break;
            case Design::AnimalPen:
                buildPen(writer, palette, building);
                break;
            default:
                buildHouse(writer, palette, building);
                break;
            }
        }

        for (int d = 0; d < plan.decorCount; ++d) {
            buildDecor(writer, palette, plan.decor[d]);
        }

        if (palette.snowy) {
            snowOver(writer, plan, coord);
        }
    }
}

int residentsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max) {
    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    int written = 0;

    for (int i = 0; i < near.count && written < max; ++i) {
        const Plan& plan = near.plans[i];
        for (int r = 0; r < plan.residentCount && written < max; ++r) {
            const Resident& who = plan.residents[r];
            if (who.x < baseX || who.x >= baseX + Chunk::kSize || who.z < baseZ ||
                who.z >= baseZ + Chunk::kSize) {
                continue;
            }
            out[written++] = who;
        }
    }
    return written;
}

int guardsIn(const Nearby& near, int chunkX, int chunkZ, Resident* out, int max) {
    const int baseX = chunkX * Chunk::kSize;
    const int baseZ = chunkZ * Chunk::kSize;
    int written = 0;

    for (int i = 0; i < near.count && written < max; ++i) {
        const Plan& plan = near.plans[i];
        // A village with almost nobody in it gets no guard, which is the spirit
        // of the reference's villager-count rule without the bookkeeping.
        if (plan.residentCount < 3) {
            continue;
        }
        // Just clear of the well, so it does not spawn inside the roof over it.
        const int x = plan.originX + 6;
        const int z = plan.originZ;
        if (x < baseX || x >= baseX + Chunk::kSize || z < baseZ || z >= baseZ + Chunk::kSize) {
            continue;
        }
        out[written++] = {x, plan.centreY, z, false, false};
    }
    return written;
}

} // namespace game::village
