#pragma once

#include "world/Block.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <limits>

namespace game {

/// Keeps a box a hair away from surfaces it rests against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kCollisionSkin = 0.001f;

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// The one connection answer all three box questions ask.
///
/// **Extracted because "kept beside its twin so the two cannot drift apart" was
/// not enough and they drifted anyway** (2026-08-19): this expression stood
/// written out three times below, and the *other* fact a box answer needs -
/// what is in the cell overhead - reached only one of the three. Adjacency is
/// not a binding; a shared function is. Takes the shape rather than reading the
/// id again, so it costs no extra `blockAt` on `overlapsSolid`'s hot loop.
///
/// **Proven behaviour-identical to the three copies it replaced - by reading
/// what each callee does with a zero, rather than by trusting that a zero means
/// nothing** (2026-08-19). The connecting branch is character-identical in all
/// three, so the entire question is what a *non*-connecting shape now does, and
/// on every one of those paths the `connections` argument is **never read**:
///
/// - `collisionBoxesWith` switches on `blockShape(id)`, and its three
///   non-`default` cases are Fence, Wall and Pane - *exactly* the three
///   `connectsToNeighbours` returns true for. A non-connecting id therefore
///   reaches `default:` and returns `collisionBoxes(id)`, which is the early
///   return this replaced.
/// - `selectionBoxesWith` asks `connectsToNeighbours` **itself**, as its first
///   act, and returns `selectionBoxes(id)` when it is false. The early return
///   here was duplicating a test the callee already performs.
/// - `worldDrawnBoxes` never had an early return: this function *is* the
///   ternary it already wrote out inline.
///
/// So zero is not "the value that happens to agree" - it is dead on every path
/// that changed, which is the stronger claim and the reason no per-id runtime
/// comparison is owed. **Falsified by a fourth connecting shape**, and worth
/// knowing that neither way of half-adding one breaks this: added to
/// `connectsToNeighbours` but not to the switch, old and new both reach
/// `default:` and still agree; added to the switch but not to
/// `connectsToNeighbours`, the new form routes to the new case while the old
/// short-circuited past it - the more correct of the two. That is why there is
/// no `static_assert` pinning the three-shape agreement here: this call is
/// robust to that drift in both directions, and `Block.hpp` owns the shapes.
inline std::uint8_t worldConnectionBits(const World& world, BlockShape shape, int x, int y, int z) {
    if (!connectsToNeighbours(shape)) {
        return std::uint8_t{0};
    }
    return connectionBits(shape, world.blockAt(x, y, z - 1), world.blockAt(x, y, z + 1),
                          world.blockAt(x - 1, y, z), world.blockAt(x + 1, y, z));
}

/// A block's collision geometry **where it stands**.
///
/// `collisionBoxes` can only see an id, so it has to assume a fence, wall or
/// pane grows every arm. That is right for a fence in a line and wrong for a
/// lone pane of glass, which is drawn as a two-texel post and was collided with
/// as a full cross - an invisible shell round it, which is exactly the
/// complaint the ladder had answered.
///
/// **A collision box that left its own cell would be invisible to both scans
/// below, and no id did when this was run** - measured 2026-08-19 through the
/// real `collisionBoxes` / `collisionBoxesWith`: 2,186 collidable, 0 overflowing
/// their cell up, down or sideways, and 0 of the 55 connecting ids overflowing
/// under any of the 16 connection patterns. **The sweep's denominator was 3,285,
/// which was the block count that morning rather than `kBlockIdCount` now** -
/// the bee nest's twenty-four landed the same day - so read the result as "no id
/// below 3,285", not as "no id". It is falsified by a single box declaring a
/// corner outside `[0, 1]` on any axis, which is the one thing worth checking
/// when adding one, and re-running the sweep is what re-earns the sentence.
/// The 1,781 that stop
/// *exactly* at y = 1.0 are not at risk - `overlapsSolid` subtracts
/// `kCollisionSkin` from its upper bound precisely so a body resting on a
/// boundary does not claim the next cell along. So this is a **constraint on
/// what a block may declare**, not a bug: raising a fence or wall to the
/// reference's 1.5 blocks does nothing until the scans start one cell lower,
/// because a body standing at 1.5 floors to cell 1 and never visits the post in
/// cell 0. That extra row costs a `worldCollisionBoxes` call per column on the
/// hottest path in the game and should be paid for by a measurement.
inline BlockBoxes worldCollisionBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    return collisionBoxesWith(id, worldConnectionBits(world, blockShape(id), x, y, z));
}

/// The same question for the crosshair.
///
/// **Had `worldDrawnBoxes`' third fact missing until 2026-08-19, and that was a
/// live defect rather than a difference** (finding 1253). A vine's drawn extent
/// depends on whether the cell above it is opaque - it grows a ceiling panel
/// when it is - and `drawnBoxes` was handed that answer while `selectionBoxes`
/// was not, so the crack overlay sat on a vine's ceiling panel you could not
/// aim at. **Both bodies now ask the same three questions**, and that is the
/// whole fix: the two must be read as a pair, because a fact given to one and
/// not the other is exactly what produced this.
///
/// The blocking half was `Block.hpp`'s, and it landed: a three-argument
/// `selectionBoxesWith(BlockId, std::uint8_t, bool opaqueAbove)` now sits
/// beside the two-argument form, which is retained and load-bearing. **Do not
/// add a second overload** - the instruction that used to stand here has been
/// carried out, and an instruction reads as unmet whether or not it has been
/// met, which is why it is written in the past tense now rather than left to be
/// honoured twice.
inline BlockBoxes worldSelectionBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    return selectionBoxesWith(id, worldConnectionBits(world, blockShape(id), x, y, z),
                              isOpaque(world.blockAt(x, y + 1, z)));
}

/// What the mesher actually put on screen for this block, in world terms.
///
/// The one to use when drawing **onto** a block rather than colliding with it,
/// and the only one of the three that is told what stands overhead.
inline BlockBoxes worldDrawnBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    return drawnBoxes(id, worldConnectionBits(world, blockShape(id), x, y, z),
                      isOpaque(world.blockAt(x, y + 1, z)));
}

/// True if any block's collision geometry overlaps the box.
///
/// **Everything that collides with the world goes through here**, because
/// `collisionBoxes` is the single source of truth for a block's extent and a
/// second copy of that answer is the recurring bug in this project. Testing
/// whole cells is only correct while every solid block fills its cell, which
/// stopped being true at M17b.
///
/// Subtracting the skin from the upper bound stops a box resting exactly on a
/// boundary from counting the next block along.
inline bool overlapsSolid(const World& world, const Aabb& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCollisionSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kCollisionSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCollisionSkin));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = worldCollisionBoxes(world, x, y, z);
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    if (box.min.x < static_cast<float>(x) + b.maxX &&
                        box.max.x > static_cast<float>(x) + b.minX &&
                        box.min.y < static_cast<float>(y) + b.maxY &&
                        box.max.y > static_cast<float>(y) + b.minY &&
                        box.min.z < static_cast<float>(z) + b.maxZ &&
                        box.max.z > static_cast<float>(z) + b.minZ) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// Highest surface under `box` that its underside may come to rest on, or
/// negative infinity if there is none.
///
/// Landing is the one case where the blocking plane is not a block boundary: a
/// slab's top is halfway up its cell. Snapping to the boundary leaves the body
/// on thin air, the ground probe finds nothing, and it falls again - a bounce
/// that repeats forever.
inline float highestSurfaceBelow(const World& world, const Aabb& box, float notAbove) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCollisionSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCollisionSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(notAbove));

    float best = -std::numeric_limits<float>::infinity();
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = worldCollisionBoxes(world, x, y, z);
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    // Only boxes actually under the footprint can be landed on.
                    if (box.min.x >= static_cast<float>(x) + b.maxX ||
                        box.max.x <= static_cast<float>(x) + b.minX ||
                        box.min.z >= static_cast<float>(z) + b.maxZ ||
                        box.max.z <= static_cast<float>(z) + b.minZ) {
                        continue;
                    }
                    const float top = static_cast<float>(y) + b.maxY;
                    if (top <= notAbove + kCollisionSkin && top > best) {
                        best = top;
                    }
                }
            }
        }
    }
    return best;
}

/// How far up a body that has ended up **inside** terrain will climb to free
/// itself, and in what steps. Upward only: it is the direction that works for
/// something covered over, and a sideways nudge would as often find another
/// block. The reach is a convenience and not the answer to being buried -
/// anything deeper stays put, and suffocation is what ends it.
///
/// **One owner, because the pair stopped being decorative.** The player and
/// every creature run the identical loop, both files had written the identical
/// two numbers with no cross-reference, and then the creatures' suffocation
/// clock was defined *relative to the reach*: a creature dies in stone exactly
/// when it is buried deeper than this pass can lift it. A published number with
/// two owners is the shape this project has paid for most, and one of these two
/// now decides when something dies. They live here, beside `overlapsSolid`,
/// because that is the question the loop actually asks.
///
/// **Qualified rather than sitting in `game` directly, deliberately.** While a
/// file still carries its own copy in an anonymous namespace, an unqualified
/// `game::kUnstickReach` would be *ambiguous* at every `game`-scope use site
/// rather than quietly shadowed - so a bare name here could not be adopted one
/// file at a time without breaking the build of a file mid-edit. It also
/// matches how `fluid::`, `survival::` and `effects::` name their constants.
namespace collision {

constexpr float kUnstickReach = 2.0f;
constexpr float kUnstickStep = 0.25f;

// The loop every caller writes is
//   `for (float lift = kUnstickStep; lift <= kUnstickReach; lift += kUnstickStep)`.
// Raise the step past the reach and it runs zero times: the unstick pass
// silently stops existing, every caller still compiles, and the only symptom is
// a body stuck in rock. Drop the step to zero or below and it never ends.
static_assert(kUnstickStep > 0.0f && kUnstickStep <= kUnstickReach,
              "the unstick pass must take at least one step and must terminate");

} // namespace collision

} // namespace game
