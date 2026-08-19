#pragma once

#include <glm/glm.hpp>

namespace game {

class World;

/// Where a look-ray met the world.
struct RaycastHit {
    bool hit = false;
    /// The solid block that was struck.
    glm::ivec3 block{0};
    /// The empty cell the ray was in immediately before, which is where a newly
    /// placed block goes.
    glm::ivec3 adjacent{0};

    // --- The three below are `SweepHit`'s three, deliberately spelled the same
    // --- way and meaning the same thing, so the two results read alike.
    //
    // **They were being computed and thrown away.** `hitsBlockGeometry` has
    // always returned the entry parameter and the face crossed - `sweepBlocks`
    // keeps both and `raycast` kept neither, deriving `adjacent` from the normal
    // and dropping the rest on the floor. Nothing new is calculated for these.

    /// The face crossed to get in, as a unit step out of the block: `{0,1,0}`
    /// for the top, `{-1,0,0}` for the west side.
    ///
    /// **`adjacent` is `block + normal` and that is not a coincidence** - it is
    /// how `adjacent` has always been computed. What this adds is the ability to
    /// tell *which* face without subtracting two cells, which is what a caller
    /// wants when the answer depends on the face rather than on the cell: the
    /// half a slab is placed in, which way a stair or a torch or a chest turns.
    ///
    /// **Zero when the ray began inside the thing it hit**, and zero when
    /// `stopAtFluid` stopped on a source the ray started inside. That is honest
    /// rather than a sentinel - there is no face to have crossed - and a caller
    /// that must have a direction should fall back on the view vector.
    glm::ivec3 normal{0};
    /// Where on the ray it landed, in world space, and how far along it that
    /// was in metres from the origin.
    ///
    /// **The point is what a top slab and an upside-down stair need**, and
    /// neither works without it: which half of a cell was struck is a question
    /// about the hit position's fractional Y, and knowing only the cell and the
    /// face cannot answer it - a ray hitting the *side* of a cell crosses the
    /// same face whether it lands in the top half or the bottom.
    ///
    /// **The distance is the parameter along the normalised direction**, so it
    /// is in the same units as the `maxDistance` argument and is directly
    /// comparable between two hits. For a `stopAtFluid` stop it is where the ray
    /// entered the fluid *cell*, not where it met a surface - fluids have no
    /// selection geometry to meet, which is the whole reason that branch exists.
    glm::vec3 point{0.0f};
    float distance = 0.0f;
};

/// Walks the ray cell by cell and returns the first solid block within range.
///
/// Steps block to block rather than sampling at fixed intervals, so it cannot
/// skip a block no matter how the ray is angled and costs the same regardless
/// of precision.
///
/// `stopAtFluid` makes a **source** of either fluid count as something to hit.
/// It is off by
/// default because water has no selection geometry - you aim *through* it at
/// the riverbed - and on only for a bucket, which is the reference's own
/// arrangement: fluids are invisible to a normal reach and solid to a bucket.
RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction,
                   float maxDistance, bool stopAtFluid = false);

/// Whether anything opaque stands between two points.
///
/// **Deliberately not `raycast`.** That one walks `selectionBoxes`, because it
/// answers "what am I aiming at" and you must be able to aim at a tuft of
/// grass. Vision is a different question and `Block.hpp` already owns the
/// predicate for it - `isOpaque`, which is documented as *blocks vision* and is
/// kept distinct from *blocks movement* for exactly this reason. Asking the
/// wrong one made tall grass hide the player from a creeper, which stuttered
/// its fuse every time it walked through a meadow.
bool hasLineOfSight(const World& world, const glm::vec3& from, const glm::vec3& to);

/// Where a segment first meets something's **collision** geometry.
struct SweepHit {
    bool hit = false;
    glm::ivec3 block{0};
    /// The face crossed to get in, as a unit step out of the block.
    glm::ivec3 normal{0};
    /// Where on the segment it landed, and how far along it that was.
    glm::vec3 point{0.0f};
    float distance = 0.0f;
};

/// Sweeps `from` to `to` and returns the first block the segment enters.
///
/// **A third question for the one walker, and deliberately not a flag on
/// `raycast`.** That one reads `selectionBoxes` and answers "what am I aiming
/// at", which is why you can pick out a tuft of grass; a projectile has to ask
/// what it would physically hit, which is `collisionBoxes`. Widening the
/// existing function would have quietly changed every caller of it.
///
/// The whole segment is tested rather than the endpoint cell, which is what
/// stops an arrow moving sixty blocks a second from passing through a wall - so
/// there is no substepping anywhere and no speed cap.
SweepHit sweepBlocks(const World& world, const glm::vec3& from, const glm::vec3& to);

} // namespace game
