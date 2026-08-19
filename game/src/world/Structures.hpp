#pragma once

#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/Village.hpp"

#include <cstdint>
#include <vector>

namespace game {

/// Places trees and anything else that spans more than one block.
///
/// The whole problem here is that a structure straddles chunk boundaries while
/// generation has to stay a **pure function of `(seed, chunkCoord)`** — a chunk
/// may not ask its neighbours what they decided. So nothing is ever "pushed"
/// into a neighbouring chunk. Instead each chunk works out which structures
/// could reach it, rebuilds every one of them from the seed, and keeps only the
/// blocks that land inside its own bounds. Two chunks generating the same tree
/// independently reach the same answer, which is what makes it seamless.
///
/// Candidates live on a fixed grid so the search is bounded: only cells within
/// `kStructureReach` of the chunk can possibly matter.
namespace structures {

/// Side of one placement cell, in blocks. At most one structure per cell, which
/// is what stops trees growing out of each other.
constexpr int kCellSize = 8;

/// How far a structure may reach outside the cell it belongs to, in blocks.
///
/// A branching oak throws clusters about `height * 0.4` sideways and each one is
/// a blob two blocks wide, so 6 rather than the 3 a fixed canopy needed. Too
/// small and a tree straddling a chunk border is built by one chunk and not the
/// other, which is a seam you can walk up to; too large and every chunk pays for
/// cells that can never touch it.
///
/// **`Structures.cpp` asserts this against the canopies that justify it and
/// against `village::kClaimReach`**, so neither number can move on its own.
constexpr int kReach = 6;

/// Writes every structure overlapping this chunk into it.
///
/// `villages` is passed in rather than looked up because the caller has already
/// solved it, and a tree cell asking for itself would rebuild the same village
/// plan once per candidate cell.
void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord,
                  const village::Nearby& villages);

/// One block a planned tree wants to write, in **world** coordinates.
///
/// `onlyIntoAir` carries the same meaning it has inside the generator: leaves
/// give way to anything already standing, logs do not.
struct PlannedBlock {
    int x = 0;
    int y = 0;
    int z = 0;
    BlockId block = BlockId::Air;
    bool onlyIntoAir = false;
};

/// A tree, worked out but not yet placed.
///
/// **This is the "compute the result, then apply the result" split** that lets
/// a sapling use the generator's own tree builder: the plan is a pure function
/// of its arguments, and the caller checks it against the real world before
/// writing a single block. A sapling can therefore never leave half a tree
/// behind, and a tree grown from a sapling is the same code as a tree grown by
/// the generator - which is the point, because two tree builders drift.
struct SaplingPlan {
    std::vector<PlannedBlock> blocks;

    /// The box the reference clears before it grows anything, measured from the
    /// sapling (the **northwest** one when `trunkSpan` is 2). Horizontally
    /// `[-clearRadius, clearRadius + trunkSpan - 1]`, vertically `[1,
    /// clearHeight]` above the sapling.
    ///
    /// **Derived from the height that was rolled, not from a constant**, which
    /// is the reference's own rule: it draws a height first and then asks
    /// whether that tree fits, so a sapling in a cramped spot fails some
    /// attempts and succeeds at others rather than always growing short.
    int clearRadius = 0;
    int clearHeight = 0;

    /// 1 for an ordinary tree, 2 for the two-by-two giant.
    int trunkSpan = 1;

    /// False when this sapling names no tree this generator can build.
    bool valid = false;

    /// The half-width the caller must find free `dy` blocks above the sapling.
    ///
    /// **The taper is the reference's own**, out of its tree feature: the
    /// trunk's column near the ground so a tree may grow beside a wall, and
    /// the species' full published footprint only over the top two rows where
    /// the canopy actually is. A straight box for the whole height would
    /// refuse to grow a sapling in a two-block-wide gap, which the reference
    /// grows quite happily.
    ///
    /// **This lives here, on the plan, because two readers need it** - the
    /// caller that checks the world and the planner that decides which of its
    /// own blocks are allowed to overwrite anything. Written out twice it is
    /// `CLAUDE.md` bug shape #14 exactly: a rule that is right in one of the
    /// two places that need it, and the probe that caught it the first time
    /// only exists because the two disagreed.
    constexpr int clearedRadiusAt(int dy) const {
        return dy >= clearHeight - 1 ? clearRadius : 1;
    }

    /// Whether an offset from the sapling (the northwest one when `trunkSpan`
    /// is 2) is somewhere the caller has proved is free.
    ///
    /// `dy == 0` is the sapling's own cell (or the four of them), which the
    /// caller clears outright. `dy < 0` is the soil the tree roots itself into
    /// and is deliberately **not** part of the box - the reference writes dirt
    /// under a trunk whatever was there.
    constexpr bool insideClearedBox(int dx, int dy, int dz) const {
        if (dy == 0) {
            return dx >= 0 && dx < trunkSpan && dz >= 0 && dz < trunkSpan;
        }
        if (dy < 0) {
            return true;
        }
        if (dy > clearHeight) {
            return false;
        }
        const int r = clearedRadiusAt(dy);
        return dx >= -r && dx <= r + trunkSpan - 1 && dz >= -r && dz <= r + trunkSpan - 1;
    }
};

/// What a sapling of this species would grow into, standing at `(x, y, z)`.
///
/// `roll` seeds every shape draw, so the caller owns the randomness and this
/// stays pure. `twoByTwo` says the caller has already found four matching
/// saplings in a square with this one at the northwest corner - the check
/// belongs to the caller because it needs the world.
///
/// **Dark oak returns an invalid plan unless `twoByTwo`**, which is the
/// reference's Bedrock rule: a lone dark oak sapling never grows.
/// <https://minecraft.wiki/w/Sapling>
SaplingPlan planSaplingTree(BlockId sapling, int x, int y, int z, std::uint32_t roll, bool twoByTwo);

/// Whether four of this sapling in a square can become one big tree, and so
/// whether the caller should look for the other three. Spruce, jungle and dark
/// oak in the reference; **dark oak only ever grows this way**.
bool saplingHasGiantForm(BlockId sapling);

/// Whether this sapling refuses to grow on its own. Dark oak, and nothing else.
bool saplingNeedsGiantForm(BlockId sapling);

} // namespace structures
} // namespace game
