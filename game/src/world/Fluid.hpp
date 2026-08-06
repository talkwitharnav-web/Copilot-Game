#pragma once

#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <limits>

namespace game {

/// How an entity moves through water, and how flowing water pushes it.
///
/// **This is the one owner of the water constants**, shared by the player,
/// creatures and dropped items, because a second copy of a physics number is
/// the recurring bug in this project.
///
/// The reference does not cap speed in water; it *multiplies* velocity by a
/// drag factor every tick and adds a fixed impulse. That single difference is
/// most of why our old fudge - gravity x 0.22 with a 3 m/s sink cap - felt like
/// falling slowly rather than swimming. See `RESEARCH.md` 9.1.
namespace fluid {

/// The reference integrates at a fixed 20 Hz. Every constant below is quoted at
/// that rate and converted for our variable frame time.
constexpr float kTickSeconds = 0.05f;

/// Fraction of velocity surviving each tick in water, on every axis.
/// Sprint-swimming is slipperier, which is the whole of why it is faster.
constexpr float kWaterDrag = 0.8f;
constexpr float kSprintSwimDrag = 0.9f;

/// Every speed below is a *terminal* velocity: the impulse divided by `1 - drag`
/// and converted to metres per second. Quoting them this way rather than as
/// per-tick impulses is what makes the frame-rate conversion exact - see
/// `approach`.
///
/// Doing nothing at all, from water gravity 0.005 b/t^2 (a sixteenth of the
/// air value) against drag 0.8.
constexpr float kSinkSpeed = 0.5f;
/// Holding jump, and holding sneak: +/-0.04 b/t of impulse every tick.
///
/// **Each of these is `(impulse * drag - waterGravity) / (1 - drag)`, in blocks
/// per tick, times twenty.** The whole set shipped roughly a quarter too fast
/// because the water gravity term was left out of the vertical ones - which is
/// what made bobbing at the surface throw the player further out of the water
/// than it should. Two checks that the arithmetic is right: the same expression
/// with no impulse gives `kSinkSpeed` exactly, and the sprint figure lands
/// within 3% of the 6.98 m/s the wiki measures.
constexpr float kSwimUpSpeed = 2.7f;
constexpr float kSwimDownSpeed = 3.7f;
/// The same impulses under sprint-swimming's lighter drag, which also skips
/// water gravity outright - so these are simply `0.04 * 0.9 / 0.1`.
constexpr float kSprintSwimUpSpeed = 7.2f;
constexpr float kSprintSwimDownSpeed = 7.2f;

/// Horizontal, from the in-water input acceleration 0.02 b/t^2 scaled by the
/// reference's 0.98 input impulse. These reproduce the published 1.96 and
/// 3.918 m/s exactly, which is the check that the whole model is right.
constexpr float kSwimSpeed = 1.96f;
constexpr float kSprintSwimSpeed = 3.92f;

/// What fraction of its speed on land anything covers while swimming.
///
/// Not a fudge and not per species: land and water are the *same* acceleration
/// under a different drag, so the ratio belongs to the water. 1.96 / 4.317 for
/// the player, and the same for any animal whose land speed comes from the same
/// attribute - which on this roster is all of them.
constexpr float kSwimSpeedRatio = kSwimSpeed / 4.317f;

/// What a flowing cell carries you at. Measured in the reference as "25 blocks
/// every 18 seconds"; the constants give 1.4 exactly.
constexpr float kCurrentSpeed = 1.4f;

/// Set, not added, when a swimmer is pressed against a ledge with headroom.
/// This is what climbs you out of a pool instead of leaving you scrabbling.
constexpr float kSwimOutSpeed = 6.0f;
/// How much clearance above the head that climb-out needs.
constexpr float kSwimOutHeadroom = 0.6f;

/// Water shallower than this under your feet jumps like dry land rather than
/// starting a swim, so ankle-deep puddles do not feel like a pool. Anything
/// deeper is a **climb** off the bottom at the water's own speed, not a leap:
/// a single block of water used to launch you off the floor like solid ground,
/// which read as nothing like swimming.
constexpr float kShallowDepth = 0.4f;

/// Treading water: where a floating swimmer's eyes sit above the surface, and
/// how far the stroke carries them either side of that.
///
/// **`kSwimUpSpeed` is the speed you *climb* at, not the speed you *tread* at,
/// and using it for both is what threw the player a body length clear of the
/// water on every bob.** A treading kick only has to beat the sink; the *ratio*
/// between the two is what sets the rhythm - a quick stroke against a slow
/// drift back down.
///
/// The band is a latch rather than a taper, and that is deliberate: a drive
/// that fades out as the head clears is first-order, so it settles dead and the
/// player ends up floating motionless. Kicking below the low mark and drifting
/// above the high one cannot settle.
///
/// The three knobs do separate jobs, which is worth knowing before touching any
/// of them: **`kStroke` is the amplitude, `kFloatEye - kStroke` is the trough,
/// and `kTreadSpeed` against `kSinkSpeed` is the period.** Widening the stroke
/// alone sinks the low point rather than lifting the high one.
///
/// The height is set so that **floating in deep water and standing in a single
/// block of it come out the same** - measured, not judged: both give a 0.31 m
/// bob every 1.4 s, with the eyes 0.74 to 1.04 above the surface, half the body
/// out of it at the trough and just over two thirds at the crest.
/// `tools/simulate-swim.ps1` is what says so, and it reads these constants
/// rather than copying them.
///
/// Ours, not the reference's: Bedrock drives the impulse off *any part of the
/// body* being wet, at full climb speed, which balances with the **feet** at
/// the surface - a player stood on top of the sea with water physics running.
constexpr float kFloatEye = 0.84f;
constexpr float kStroke = 0.09f;
constexpr float kTreadSpeed = kSinkSpeed * 2.8f;

/// How long after leaving water a body still moves horizontally like a swimmer.
///
/// A bob at the surface lifts the whole box clear for about a sixth of a
/// second, and **our airborne model eases toward a target speed where the
/// reference's has none at all** - it only keeps your velocity under drag. So
/// without this, every bounce spent its airtime accelerating toward the walking
/// speed and dropping back on splashdown, which read as a lurch on each bob
/// rather than as swimming. Longer than a bob's airtime and shorter than a real
/// jump's, so leaping onto a bank still hands control back.
constexpr float kSwimGrace = 0.3f;

/// Air supply, in seconds. The reference's `total_supply` is 15 for the player
/// and for every mob that breathes.
constexpr float kAirSeconds = 15.0f;
/// A player gets one second of grace past empty before the first hit
/// (`suffocate_time: -1`); a mob gets none (`suffocate_time: 0`).
constexpr float kPlayerSuffocateGrace = 1.0f;
/// Then two health points every second, forever - the interval never shortens.
constexpr float kDrownInterval = 1.0f;
constexpr int kDrownDamage = 2;
/// Empty to full once your head is out, from `inhale_time: 3.75`.
constexpr float kInhaleSeconds = 3.75f;

/// A per-tick drag factor, raised to however much of a tick this frame was.
inline float dragOver(float perTick, float deltaSeconds) {
    return std::pow(perTick, deltaSeconds / kTickSeconds);
}

// --- How water *looks* from inside it. Appearance rather than physics, but it
// --- lives here because a second home for a water constant is how the same
// --- number ends up written down twice.

/// What everything fades to underwater.
///
/// **Measured off reference screenshots, not taken from the fog JSON.** The
/// shipped `water_fog_color` for an ocean is `#1165b0`, and using it directly
/// comes out visibly grey - because what reaches the screen is that colour
/// blended with the water's own, not the raw value. A fully fogged region of a
/// bright ocean measures `#146FFF` and of a deeper one `#1762CD`; red and green
/// agree with the JSON almost exactly and only the blue is far off, which is
/// what read as "a weird blue".
constexpr glm::vec3 kFogColour{20 / 255.0f, 101 / 255.0f, 231 / 255.0f};

/// `fog_end`, and `render_distance_type: "fixed"` - so it is sixty **metres**,
/// not a fraction of the render distance. `fog_start` is 0, which is why even
/// something close up carries a little of the colour.
///
/// Held constant. The reference ramps this up from a quarter over thirty
/// seconds as your eyes adjust, and that was built and taken back out: bobbing
/// at a shoreline restarts it on every dip, so the density never settles and
/// the whole effect reads as unstable.
constexpr float kFogDistance = 60.0f;

/// One frame of "shed some velocity, then take a fixed push toward `terminal`".
///
/// **Terminal velocity is exact at any frame rate**, which the naive conversion
/// is not: `v = (v - g dt) k` moves its settling point when both `dt` and `k`
/// change, and the chicken's slow fall already proved that once. Quoting the
/// settling speed instead of the impulse removes the problem rather than
/// correcting for it, and at a 50 ms frame this reduces to the reference's own
/// arithmetic.
inline float approach(float velocity, float terminal, float perTickDrag, float deltaSeconds) {
    const float k = dragOver(perTickDrag, deltaSeconds);
    return velocity * k + terminal * (1.0f - k);
}

inline glm::vec3 approach(const glm::vec3& velocity, const glm::vec3& terminal, float perTickDrag,
                          float deltaSeconds) {
    const float k = dragOver(perTickDrag, deltaSeconds);
    return velocity * k + terminal * (1.0f - k);
}

/// How full a cell is, as a fraction of a block. A source is 8/9 and each
/// flowing level below it loses a ninth - the reference's own scale, and what
/// makes the flow gradient below come out in whole ninths.
inline float fluidHeight(BlockId id) {
    return isWater(id) ? static_cast<float>(kMaxWaterLevel + 1 - waterLevel(id)) / 9.0f : 0.0f;
}

/// Which way a single water cell is flowing, as a unit vector, or zero.
///
/// It is the gradient of the fill-height field over the four horizontal
/// neighbours - each one contributes its signed height difference along its own
/// axis. A still lake pushes nothing, because every neighbour matches.
///
/// The one special case earns its place: a neighbour that holds no water but
/// does not block movement is checked one cell *down*, and a drop found there
/// contributes more than any ordinary gradient could. That is what aims a
/// stream at the cliff edge it is about to fall over.
inline glm::vec3 flowVector(const World& world, int x, int y, int z) {
    const BlockId own = world.blockAt(x, y, z);
    if (!isWater(own)) {
        return glm::vec3{0.0f};
    }
    // A falling cell's water is going down, not sideways. It is full, so a
    // gradient would read it as a mound and shove you out from under a
    // waterfall - which is the opposite of what standing in one feels like.
    if (isFallingWater(own)) {
        return glm::vec3{0.0f};
    }

    const float here = fluidHeight(own);
    constexpr int kStepX[4] = {1, -1, 0, 0};
    constexpr int kStepZ[4] = {0, 0, 1, -1};

    glm::vec3 flow{0.0f};
    for (int i = 0; i < 4; ++i) {
        const int nx = x + kStepX[i];
        const int nz = z + kStepZ[i];
        const BlockId neighbour = world.blockAt(nx, y, nz);

        float gradient = 0.0f;
        if (isWater(neighbour)) {
            gradient = here - fluidHeight(neighbour);
        } else if (!isSolid(neighbour)) {
            const BlockId below = world.blockAt(nx, y - 1, nz);
            if (isWater(below)) {
                gradient = here - (fluidHeight(below) - 8.0f / 9.0f);
            }
        }

        if (gradient != 0.0f) {
            flow.x += static_cast<float>(kStepX[i]) * gradient;
            flow.z += static_cast<float>(kStepZ[i]) * gradient;
        }
    }

    const float lengthSq = glm::dot(flow, flow);
    return lengthSq > 0.0f ? flow / std::sqrt(lengthSq) : glm::vec3{0.0f};
}

/// What the water around a body adds up to.
struct FluidContact {
    /// Any part of the box is in water. This is what the reference switches its
    /// whole movement model on, so a player wading is already swimming.
    bool inWater = false;
    /// How far the water surface stands above the underside of the box. Zero
    /// when dry. Used to tell a puddle from a pool.
    float depth = 0.0f;
    /// Averaged unit push from every water cell the box touches.
    glm::vec3 flow{0.0f};
};

/// Samples every cell a body occupies.
///
/// Averaging over the occupied cells rather than sampling one point is what
/// stops the push flickering as a body straddles a boundary.
inline FluidContact sampleFluid(const World& world, const Aabb& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCollisionSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kCollisionSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCollisionSkin));

    FluidContact contact;
    glm::vec3 sum{0.0f};
    int cells = 0;
    float surface = -std::numeric_limits<float>::infinity();

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockId block = world.blockAt(x, y, z);
                if (!isWater(block)) {
                    continue;
                }
                contact.inWater = true;
                surface = std::max(surface, static_cast<float>(y) + fluidHeight(block));
                sum += flowVector(world, x, y, z);
                ++cells;
            }
        }
    }

    if (contact.inWater) {
        contact.depth = std::max(0.0f, surface - box.min.y);
        const float lengthSq = glm::dot(sum, sum);
        if (lengthSq > 0.0f) {
            contact.flow = sum / std::sqrt(lengthSq);
        }
    }
    return contact;
}

} // namespace fluid
} // namespace game
