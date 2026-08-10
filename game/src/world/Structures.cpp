#include "world/Structures.hpp"

#include "world/Biome.hpp"
#include "world/Noise.hpp"
#include "world/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>

namespace game::structures {
namespace {

/// A deterministic per-tree stream. See `noise::Stream` for why every draw has
/// to come from one and happen in the same order both times a tree is built.
using Roll = noise::Stream;

/// What a tree actually is, rather than which of two silhouettes it copies.
///
/// The reference does not have a "round tree"; it has a trunk placer and a
/// foliage placer, and a biome picks between several with lopsided weights so
/// one species dominates and the others punctuate. These four are the cheapest
/// set that covers both of ours.
enum class TreeVariant : std::uint8_t {
    Oak,
    Branching,
    Spruce,
    Pine,
    /// A straight jungle trunk under an oak-shaped canopy, vined all over and
    /// sometimes carrying cocoa.
    JungleSmall,
    /// The two-by-two giant. Up to thirty-one blocks in the reference; ours is
    /// scaled for a ninety-six block world like every other height here.
    JungleMega,
    /// One log and a ball of **oak** leaves - the reference's own choice, and
    /// the reason a jungle floor is not uniformly jungle-green.
    JungleBush,
};

/// Where a tree stands inside its cell, and what it is. Everything is derived
/// from the cell coordinates alone, so any chunk that asks about this cell gets
/// the same tree.
struct Tree {
    int x = 0;
    int z = 0;
    /// The whole height for a branching oak, the log column for everything else.
    int height = 0;
    TreeVariant variant = TreeVariant::Oak;
    /// Re-seeds `Roll` in `buildTree`, so shape draws are repeatable.
    std::uint32_t shapeSeed = 0;
};

bool treeInCell(std::uint32_t seed, int cellX, int cellZ, Tree& out) {
    const float presence = noise::hashUnit2D(seed ^ 0x7ee50001u, cellX, cellZ);

    // Most cells are empty, and finding that out costs one hash. Sampling the
    // biome and surface height first would mean three noise evaluations per
    // candidate for a question already answered.
    if (presence >= maxTreeDensity()) {
        return false;
    }

    // Kept away from the cell edges so neighbouring trunks cannot collide across
    // a boundary. Canopies are free to overlap, and should: interpenetrating
    // crowns are most of what separates a forest from a plantation.
    constexpr int margin = 2;
    constexpr int span = kCellSize - 2 * margin;

    Roll roll{noise::hash2D(seed ^ 0x7ee50002u, cellX, cellZ)};

    out.x = cellX * kCellSize + margin + roll.range(span);
    out.z = cellZ * kCellSize + margin + roll.range(span);
    out.shapeSeed = roll.next();

    const BiomeSample sample = sampleBiome(seed, out.x, out.z);
    const Biome& biome = biomeInfo(sample.dominant);
    if (presence >= biome.treeDensity || biome.treeShape == TreeShape::None) {
        return false;
    }

    // Heights are the reference's, scaled for a 96-block world. The *ratios*
    // inside each builder — 0.618 for the trunk fraction, 0.381 for the branch
    // slope — are dimensionless and are deliberately not scaled with them.
    if (biome.treeShape == TreeShape::Tall) {
        out.variant = roll.unit() < 0.33f ? TreeVariant::Pine : TreeVariant::Spruce;
        out.height = out.variant == TreeVariant::Pine ? 6 + roll.range(3) : 5 + roll.range(3);
    } else if (biome.treeShape == TreeShape::Jungle) {
        // The reference's own `trees_jungle` mix, resolved: bush 45 %, mega
        // 15 %, fancy oak 10 %, and the small jungle tree taking the rest. The
        // bushes are what make a jungle floor impassable rather than shaded.
        const float pick = roll.unit();
        if (pick < 0.45f) {
            out.variant = TreeVariant::JungleBush;
            out.height = 1;
        } else if (pick < 0.60f) {
            out.variant = TreeVariant::JungleMega;
            out.height = 11 + roll.range(8);
        } else if (pick < 0.70f) {
            out.variant = TreeVariant::Branching;
            out.height = 6 + roll.range(5);
        } else {
            out.variant = TreeVariant::JungleSmall;
            out.height = 5 + roll.range(6);
        }
    } else {
        out.variant = roll.unit() < 0.12f ? TreeVariant::Branching : TreeVariant::Oak;
        out.height = out.variant == TreeVariant::Branching ? 6 + roll.range(5) : 4 + roll.range(3);
    }

    // Nothing grows in the sea.
    const int surface = surfaceHeightAt(seed, out.x, out.z);
    if (surface <= kSeaLevel + 1) {
        return false;
    }

    // The ground it would stand on has to still be there. A cave mouth removes
    // it, and the tree used to be built anyway - trunk, canopy and the dirt
    // block that roots it, all hanging over the hole.
    if (surfaceCarvedAt(seed, out.x, out.z)) {
        return false;
    }

    // The reference gates every tree on `would_survive`, which is a test on the
    // block below. We cannot see that block from here, so the case that
    // actually matters is tested directly: a **steep** face, where the surface
    // rules deliberately put bare rock and a tree would be standing on a cliff.
    constexpr int kSteepDrop = 4;
    return std::abs(surfaceHeightAt(seed, out.x - 1, out.z) - surface) < kSteepDrop &&
           std::abs(surfaceHeightAt(seed, out.x + 1, out.z) - surface) < kSteepDrop &&
           std::abs(surfaceHeightAt(seed, out.x, out.z - 1) - surface) < kSteepDrop &&
           std::abs(surfaceHeightAt(seed, out.x, out.z + 1) - surface) < kSteepDrop;
}

/// Writes one block if it falls inside this chunk. Out-of-range writes are
/// simply dropped, which is what lets the same tree be built by several chunks.
void place(Chunk& chunk, ChunkCoord coord, int worldX, int worldY, int worldZ, BlockId block, bool onlyIntoAir) {
    const int lx = worldX - coord.x * Chunk::kSize;
    const int ly = worldY - coord.y * Chunk::kSize;
    const int lz = worldZ - coord.z * Chunk::kSize;

    if (!Chunk::contains(lx, ly, lz)) {
        return;
    }
    if (onlyIntoAir && chunk.at(lx, ly, lz) != BlockId::Air) {
        return;
    }
    chunk.set(lx, ly, lz, block);
}

/// Adds one side to whatever vine already stands here, or starts a new one.
///
/// **Merging rather than overwriting is what lets a cell between two trunks
/// cling to both.** Sixteen ids exist for exactly that, and writing a fresh
/// single-sided vine over an existing one would throw half of them away.
void placeVine(Chunk& chunk, ChunkCoord coord, int worldX, int worldY, int worldZ, std::uint8_t side) {
    const int lx = worldX - coord.x * Chunk::kSize;
    const int ly = worldY - coord.y * Chunk::kSize;
    const int lz = worldZ - coord.z * Chunk::kSize;
    if (!Chunk::contains(lx, ly, lz)) {
        return;
    }
    const BlockId there = chunk.at(lx, ly, lz);
    if (there == BlockId::Air) {
        chunk.set(lx, ly, lz, vineWith(side));
    } else if (isVine(there)) {
        chunk.set(lx, ly, lz, vineWith(static_cast<std::uint8_t>(vineSides(there) | side)));
    }
}

/// Wraps a jungle tree in vines, and hangs cocoa on the small one.
///
/// **Computed from the tree's own numbers rather than by looking at what was
/// built**, because a tree straddling a chunk border is built once per chunk
/// and each of those only sees its own cells. Every draw comes from the shared
/// `Roll` in a fixed order, so both chunks lay the same vines.
void dressJungleTree(Chunk& chunk, ChunkCoord coord, const Tree& tree, int base, Roll& roll) {
    const bool mega = tree.variant == TreeVariant::JungleMega;
    const int span = mega ? 1 : 0;
    const int top = base + tree.height;

    // **Cocoa goes on before the vines**, because a vine already in the cell
    // refuses the pod and the trunk is 75 % vined - putting this second meant
    // no jungle tree in the world carried a single pod.
    //
    // Cocoa is the small tree's alone: the reference does not put it on a
    // giant, and a sapling-grown tree gets none either. A fifth of trees carry
    // any at all, and on those each face of the lowest three logs is its own
    // quarter chance.
    if (!mega && roll.unit() < 0.2f) {
        for (int y = base + 1; y <= base + 3; ++y) {
            struct Face {
                int dx;
                int dz;
                FaceDirection facing;
            };
            // The facing is the side the **log** is on, which is the opposite
            // of the offset the pod sits at.
            constexpr std::array<Face, 4> faces{{{-1, 0, FaceDirection::PosX},
                                                 {1, 0, FaceDirection::NegX},
                                                 {0, -1, FaceDirection::PosZ},
                                                 {0, 1, FaceDirection::NegZ}}};
            for (const Face& face : faces) {
                if (roll.unit() >= 0.25f) {
                    continue;
                }
                place(chunk, coord, tree.x + face.dx, y, tree.z + face.dz,
                      cocoaAt(face.facing, roll.range(3)), true);
            }
        }
    }

    // The reference's `trunk_vine`: a vine on **each side of each trunk block
    // at 75 %**. Implemented as anything less than that leaves jungle trees
    // looking bald - it is a sheath, not a garnish.
    for (int y = base + 1; y <= top; ++y) {
        for (int dz = 0; dz <= span; ++dz) {
            for (int dx = 0; dx <= span; ++dx) {
                const int x = tree.x + dx;
                const int z = tree.z + dz;
                if (dx == 0 && roll.unit() < 0.75f) {
                    placeVine(chunk, coord, x - 1, y, z, ConnectEast);
                }
                if (dx == span && roll.unit() < 0.75f) {
                    placeVine(chunk, coord, x + 1, y, z, ConnectWest);
                }
                if (dz == 0 && roll.unit() < 0.75f) {
                    placeVine(chunk, coord, x, y, z - 1, ConnectSouth);
                }
                if (dz == span && roll.unit() < 0.75f) {
                    placeVine(chunk, coord, x, y, z + 1, ConnectNorth);
                }
            }
        }
    }

    // The reference's `leave_vine`: a quarter of the canopy's outer positions
    // get a vine, and **each one is extended up to four blocks downward** -
    // which is the whole of why a jungle has curtains rather than fringes.
    const int radius = mega ? 3 : 2;
    for (int row = 0; row < 2; ++row) {
        const int y = top - row;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dz) != radius) {
                    continue;
                }
                if (roll.unit() >= 0.25f) {
                    continue;
                }
                // Which side of its own cell the vine hangs on: the one facing
                // back toward the canopy it is attached to.
                std::uint8_t side = ConnectNorth;
                if (std::abs(dx) >= std::abs(dz)) {
                    side = dx > 0 ? ConnectWest : ConnectEast;
                } else {
                    side = dz > 0 ? ConnectNorth : ConnectSouth;
                }
                for (int drop = 0; drop < 5; ++drop) {
                    placeVine(chunk, coord, tree.x + dx, y - drop, tree.z + dz, side);
                }
            }
        }
    }
}

void buildTree(Chunk& chunk, ChunkCoord coord, std::uint32_t seed, const Tree& tree) {
    const int base = surfaceHeightAt(seed, tree.x, tree.z);
    Roll roll{tree.shapeSeed};

    // Species wood. Conifers are spruce and the branching oaks stay oak; birch
    // rides on the plain round tree, so a forest is a mix rather than one model
    // repeated. The blocks are chosen once here and every placement below uses
    // them, or half a tree would come out the wrong wood.
    const bool conifer =
        tree.variant == TreeVariant::Spruce || tree.variant == TreeVariant::Pine;
    const bool jungle = tree.variant == TreeVariant::JungleSmall ||
                        tree.variant == TreeVariant::JungleMega ||
                        tree.variant == TreeVariant::JungleBush;
    const bool birch = tree.variant == TreeVariant::Oak && (tree.shapeSeed & 3u) == 0u;
    const BlockId logBlock = conifer  ? BlockId::SpruceLog
                             : jungle ? BlockId::JungleLog
                             : birch  ? BlockId::BirchLog
                                      : BlockId::Log;
    // **A jungle bush wears oak leaves**, which is the reference's own choice
    // and not an oversight: it is what stops a jungle floor being one flat
    // green. Only the two real jungle trees carry jungle leaves.
    const BlockId leafBlock = conifer                                ? BlockId::SpruceLeaves
                              : tree.variant == TreeVariant::JungleSmall ||
                                        tree.variant == TreeVariant::JungleMega
                                  ? BlockId::JungleLeaves
                              : birch ? BlockId::BirchLeaves
                                      : BlockId::Leaves;

    const auto leaf = [&](int x, int y, int z) {
        place(chunk, coord, x, y, z, leafBlock, true);
    };

    /// One squat layer of leaves. `cut` is rolled per corner rather than fixed,
    /// which is the single cheapest thing that stops two canopies being the
    /// same shape.
    const auto disc = [&](int cx, int y, int cz, int radius, bool cutCorners) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (cutCorners && std::abs(dx) == radius && std::abs(dz) == radius &&
                    radius > 0 && roll.range(2) == 0) {
                    continue;
                }
                leaf(cx + dx, y, cz + dz);
            }
        }
    };

    /// The reference's blob: two wide layers, two narrow, a plus on top.
    const auto blob = [&](int cx, int top, int cz, int radius) {
        disc(cx, top - 2, cz, radius, true);
        disc(cx, top - 1, cz, radius, true);
        disc(cx, top, cz, radius - 1, false);
        for (int d = -1; d <= 1; ++d) {
            leaf(cx + d, top + 1, cz);
            leaf(cx, top + 1, cz + d);
        }
    };

    /// Foliage carried by a branch, sized to how far that branch actually went.
    ///
    /// **A short branch gets a sheath, not a canopy.** The reference hangs
    /// full-size clusters off trees up to 16 blocks tall, so a cluster always
    /// lands well clear of the trunk and of its neighbours. Ours are a third
    /// that height, so anything more than enough leaves to cover the log reads
    /// as a second crown bolted onto the side of the first.
    const auto cluster = [&](int cx, int cy, int cz, int reach) {
        if (reach >= 3) {
            blob(cx, cy, cz, 2);
            return;
        }
        for (int d = -1; d <= 1; ++d) {
            leaf(cx + d, cy, cz);
            leaf(cx, cy, cz + d);
        }
        leaf(cx, cy + 1, cz);
        leaf(cx, cy - 1, cz);
    };

    /// The reference's line rasteriser, used for branches. Stepping the longest
    /// axis is what keeps a diagonal branch connected instead of dotted.
    const auto branch = [&](int x0, int y0, int z0, int x1, int y1, int z1) {
        const int dx = x1 - x0;
        const int dy = y1 - y0;
        const int dz = z1 - z0;
        const int steps = std::max(std::max(std::abs(dx), std::abs(dy)), std::abs(dz));
        for (int i = 0; i <= steps; ++i) {
            const float t = steps == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(steps);
            place(chunk, coord, x0 + static_cast<int>(std::lround(dx * t)),
                  y0 + static_cast<int>(std::lround(dy * t)),
                  z0 + static_cast<int>(std::lround(dz * t)), logBlock, false);
        }
    };

    switch (tree.variant) {
    case TreeVariant::Oak: {
        const int top = base + tree.height;
        blob(tree.x, top, tree.z, 2);
        for (int y = base + 1; y <= top; ++y) {
            place(chunk, coord, tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Branching: {
        // The reference's fancy oak: the bare trunk is 0.618 of the whole, and
        // clusters hang off branches that rise at 0.381 and never start below
        // 30% of the height. Those three ratios are the shape.
        const int trunkTop = base + std::max(3, static_cast<int>(tree.height * 0.618f));
        // Scaled to the height, because the number of places a cluster can go
        // without landing on top of another one is what the height buys.
        const int clusters = 1 + roll.range(1 + (tree.height - 5) / 2);

        blob(tree.x, base + tree.height, tree.z, 2);

        for (int i = 0; i < clusters; ++i) {
            const float angle = roll.unit() * 6.2831853f;
            const float reach = (0.328f + roll.unit()) * static_cast<float>(tree.height) * 0.30f;
            const int cx = tree.x + static_cast<int>(std::lround(std::sin(angle) * reach));
            const int cz = tree.z + static_cast<int>(std::lround(std::cos(angle) * reach));
            const int lowest = base + static_cast<int>(tree.height * 0.3f);
            const int cy = lowest + roll.range(std::max(1, base + tree.height - lowest));

            const int spread = std::max(std::abs(cx - tree.x), std::abs(cz - tree.z));
            if (spread == 0) {
                // Landed on the trunk. There is no branch to draw and the crown
                // already covers it, so foliage here is pure thickening.
                continue;
            }

            // Where the branch meets the trunk, from the reference's slope.
            const float run = std::sqrt(static_cast<float>((cx - tree.x) * (cx - tree.x) +
                                                           (cz - tree.z) * (cz - tree.z)));
            const int attach = std::min(trunkTop, cy - static_cast<int>(run * 0.381f));

            branch(tree.x, std::max(base + 1, attach), tree.z, cx, cy, cz);
            cluster(cx, cy + 1, cz, spread);
        }

        for (int y = base + 1; y <= trunkTop; ++y) {
            place(chunk, coord, tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Spruce: {
        // The saw-tooth skirt, straight from the reference: the radius climbs to
        // a cap, drops back, and the cap grows one step each time. That single
        // rule is what makes a spruce read as tiered rather than as a cone.
        const int bare = 1 + roll.range(2);
        const int maxRadius = 2 + roll.range(2);
        const int rows = tree.height - bare;

        int radius = roll.range(2);
        int cap = 1;
        int carry = 0;
        for (int row = 0; row <= rows; ++row) {
            disc(tree.x, base + tree.height - row, tree.z, radius, radius > 0);
            if (radius >= cap) {
                radius = carry;
                carry = 1;
                cap = std::min(cap + 1, maxRadius);
            } else {
                ++radius;
            }
        }

        for (int y = base + 1; y <= base + tree.height - roll.range(2); ++y) {
            place(chunk, coord, tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Pine: {
        // A bare pole under a flat crown, which is the reference's other conifer
        // and the reason a taiga is not all one silhouette.
        const int bare = std::max(2, tree.height - roll.range(2) - 3);
        const int maxRadius = 1 + roll.range(tree.height - bare + 1);

        int radius = 0;
        for (int y = base + tree.height; y >= base + bare; --y) {
            disc(tree.x, y, tree.z, radius, radius > 0);
            if (radius >= 1 && y == base + bare + 1) {
                --radius;
            } else if (radius < maxRadius) {
                ++radius;
            }
        }

        for (int y = base + 1; y <= base + tree.height - 1; ++y) {
            place(chunk, coord, tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::JungleBush: {
        // One log and a ball of oak leaves, which is the whole feature.
        place(chunk, coord, tree.x, base + 1, tree.z, logBlock, false);
        disc(tree.x, base + 2, tree.z, 2, true);
        disc(tree.x, base + 3, tree.z, 1, false);
        break;
    }

    case TreeVariant::JungleSmall:
    case TreeVariant::JungleMega: {
        const bool mega = tree.variant == TreeVariant::JungleMega;
        const int top = base + tree.height;
        // **A giant's trunk is two by two.** That is the whole of what separates
        // the two here, and it is what the vines then have to wrap round.
        const int span = mega ? 1 : 0;

        for (int dz = 0; dz <= span; ++dz) {
            for (int dx = 0; dx <= span; ++dx) {
                for (int y = base + 1; y <= top - (mega ? 1 : 0); ++y) {
                    place(chunk, coord, tree.x + dx, y, tree.z + dz, logBlock, false);
                }
                place(chunk, coord, tree.x + dx, base, tree.z + dz, BlockId::Dirt, false);
            }
        }

        if (mega) {
            // The reference caps the giant with a single block and hangs
            // branches off the top half, each carrying its own foliage.
            place(chunk, coord, tree.x, top, tree.z, logBlock, false);
            const int branches = 1 + roll.range(3);
            for (int i = 0; i < branches; ++i) {
                const float angle = roll.unit() * 6.2831853f;
                const int reach = 2 + roll.range(2);
                const int bx = tree.x + static_cast<int>(std::lround(std::sin(angle) * reach));
                const int bz = tree.z + static_cast<int>(std::lround(std::cos(angle) * reach));
                const int by = top - 2 - roll.range(std::max(1, tree.height / 3));
                branch(tree.x, by, tree.z, bx, by + 1, bz);
                blob(bx, by + 2, bz, 2);
            }
            // Rounder and wider than a small tree's crown: radii 3, 2, 1 rather
            // than the blob's 2, 2, 1.
            disc(tree.x, top, tree.z, 3, true);
            disc(tree.x, top + 1, tree.z, 2, true);
            disc(tree.x, top + 2, tree.z, 1, false);
        } else {
            blob(tree.x, top, tree.z, 2);
        }
        break;
    }
    }

    // Roots the tree, so it never appears to float over a one-block dip.
    place(chunk, coord, tree.x, base, tree.z, BlockId::Dirt, false);

    // Vines and cocoa, which only the two real jungle trees carry. A bush gets
    // neither, and neither does anything outside a jungle.
    if (tree.variant == TreeVariant::JungleSmall || tree.variant == TreeVariant::JungleMega) {
        dressJungleTree(chunk, coord, tree, base, roll);
    }
}

} // namespace

void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord) {
    const int baseX = coord.x * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // Every cell whose structure could reach into this chunk. The margin is what
    // makes a tree straddling the border come out identical from both sides.
    const int firstCellX = floorDivInt(baseX - kReach, kCellSize);
    const int lastCellX = floorDivInt(baseX + Chunk::kSize + kReach, kCellSize);
    const int firstCellZ = floorDivInt(baseZ - kReach, kCellSize);
    const int lastCellZ = floorDivInt(baseZ + Chunk::kSize + kReach, kCellSize);

    for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
        for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
            Tree tree;
            if (treeInCell(seed, cellX, cellZ, tree)) {
                buildTree(chunk, coord, seed, tree);
            }
        }
    }
}

} // namespace game::structures
