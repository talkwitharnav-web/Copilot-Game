#pragma once

#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/Tick.hpp"
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
///
/// **The name stays because it is already reached for from outside** -
/// `hud/StatusBars.cpp`'s `static_assert` on `kIconsPerRow * kTicksPerBubble *
/// fluid::kTickSeconds == fluid::kAirSeconds` ties the air-supply bar to it -
/// but the value now comes from `Tick.hpp`, which is the one owner. It was a
/// literal 0.05 here and `1.0f / 20.0f` in `Weather.hpp`: identical floats,
/// spelled differently, with nothing that would have failed if one of them had
/// drifted.
///
/// That reference used to name a line number, and the assert had long since
/// moved - `Tick.hpp` carries the same sentence and had already been corrected,
/// one copied from the other. Re-verified 2026-08-19: the assert is still there
/// and still says exactly this.
constexpr float kTickSeconds = tick::kSeconds;

/// Fraction of velocity surviving each tick in water, on every axis.
/// Sprint-swimming is slipperier, which is the whole of why it is faster.
constexpr float kWaterDrag = 0.8f;
constexpr float kSprintSwimDrag = 0.9f;

/// Water's own downward pull, in blocks per tick squared: **a sixteenth of the
/// air value's 0.08** (`RESEARCH.md` §9.1). Named rather than left in the prose
/// below, because `terminal` is what proves the four speeds came out of it and
/// a number only prose knows is a number nothing can check.
constexpr float kWaterGravity = 0.005f;

/// What holding jump or sneak is worth in water, in blocks per tick.
constexpr float kSwimImpulse = 0.04f;

/// Where "add the impulse, scale by the drag, then subtract gravity" settles -
/// the reference's own order within a tick - **in metres per second**.
///
/// Steady state is `v = (v + impulse) * drag - gravity`, so
/// `v = (impulse * drag - gravity) / (1 - drag)` blocks per tick, and a tick is
/// `kTickSeconds` of a second.
constexpr float terminal(float impulse, float drag, float gravity) {
    return (impulse * drag - gravity) / (1.0f - drag) / kTickSeconds;
}

/// Float comparison for the asserts below, which compare a written-down speed
/// against the expression it was derived from rather than against itself.
constexpr bool agrees(float a, float b, float tolerance) {
    const float difference = a - b;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

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

// **The two invariants the block above states in prose, stated to the
// compiler.** Both sides are `constexpr`, so neither can rot.
//
// The single edit that fails the first three: dropping the water-gravity term
// out of `terminal`, which is exactly what shipped once and made the whole set
// about a quarter too fast.
static_assert(agrees(terminal(0.0f, kWaterDrag, kWaterGravity), -kSinkSpeed, 0.001f),
              "the same expression with no impulse at all must give kSinkSpeed");
static_assert(agrees(terminal(kSwimImpulse, kWaterDrag, kWaterGravity), kSwimUpSpeed, 0.001f) &&
                  agrees(terminal(-kSwimImpulse, kWaterDrag, kWaterGravity), -kSwimDownSpeed,
                         0.001f),
              "holding jump and holding sneak are the same expression with the impulse signed");
// Sprint-swimming skips water gravity outright, which is why its pair is the
// plain `impulse * drag / (1 - drag)` - and it is the one figure here with an
// outside measurement to check against. `RESEARCH.md` §9.1 settles on **6.98
// m/s**, the wiki's own 2.23 being measured in a waterfall rather than in still
// water; ours is 3.2% over that, so the tolerance is 0.25 and not a percentage
// the prose could quietly round.
static_assert(agrees(terminal(kSwimImpulse, kSprintSwimDrag, 0.0f), kSprintSwimUpSpeed, 0.001f) &&
                  agrees(kSprintSwimUpSpeed, 6.98f, 0.25f),
              "the sprint climb must be the lighter drag with no gravity, and must stay within a "
              "quarter of a metre a second of the measured 6.98");

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

// --- Lava. `RESEARCH.md` §1.11's second half, which shipped empty: until this
// --- block existed the only lava constants in the codebase were the two fog
// --- ones, so a body in lava fell at the full 78.4 m/s with full walking
// --- control. Every figure here is the reference's, and the three speeds are
// --- *derived* from the drag/gravity pair by `terminal` rather than written
// --- down beside it - the same arrangement, and the same asserts, as water.

/// Fraction of velocity surviving each tick in lava, and lava's own downward
/// pull in blocks per tick squared. `RESEARCH.md` §1.11: "In lava, drag ≈ 0.5,
/// downward acceleration ≈ 0.02."
constexpr float kLavaDrag = 0.5f;
constexpr float kLavaGravity = 0.02f;

/// Doing nothing, holding jump, and holding sneak - the same three the water
/// block above publishes, under lava's pair.
///
/// **The rise comes out at exactly nothing, and that is the answer rather than
/// a missing number**: the published swim impulse under lava's heavier drag
/// balances its heavier gravity precisely, so holding jump holds you level and
/// never lifts you. That is why lava is a death trap and why you leave one at
/// its edge rather than by swimming up it.
///
/// **Which makes the ledge climb the only way out**, so it had better apply to
/// lava - see `kSwimOutSpeed` below, and the twenty milestones during which it
/// did not.
constexpr float kLavaSinkSpeed = 0.8f;
constexpr float kLavaRiseSpeed = 0.0f;
constexpr float kLavaDiveSpeed = 1.6f;

// The single edit that fails all three: changing `kLavaDrag` or `kLavaGravity`
// without re-solving the speeds, which is exactly how water's set shipped a
// quarter too fast once.
static_assert(agrees(terminal(0.0f, kLavaDrag, kLavaGravity), -kLavaSinkSpeed, 0.001f) &&
                  agrees(terminal(kSwimImpulse, kLavaDrag, kLavaGravity), kLavaRiseSpeed, 0.001f) &&
                  agrees(terminal(-kSwimImpulse, kLavaDrag, kLavaGravity), -kLavaDiveSpeed, 0.001f),
              "lava's three vertical speeds are its drag and gravity with the swim impulse "
              "signed, exactly as water's three are");

/// Walking in lava, in metres per second - `RESEARCH.md` §1.11's own table.
///
/// Quoted as a measured speed rather than as the wiki's "horizontal movement
/// speed reduced by 50%" (minecraft.wiki, *Lava*), because those two are not
/// the same number: the halving is a per-tick multiplier and 0.784 is what it
/// settles at once lava's drag has also had its say. Our model eases toward a
/// target speed, so the settled figure is the one that transfers.
constexpr float kLavaWalkSpeed = 0.784f;

/// What one tick inside lava leaves of the fall banked so far.
///
/// minecraft.wiki, *Lava*: "for each tick an entity spends inside of lava, its
/// fall distance is halved", which `RESEARCH.md` §1.10 lists as "lava (−50% of
/// accumulated distance per tick)". **Per tick, so it is a drag factor and not
/// a scale** - hand it to `dragOver` with the frame time or it becomes a
/// frame-rate rule, which is the unit mistake this project has paid for before.
constexpr float kLavaFallHalving = 0.5f;

/// What flowing lava carries you at.
///
/// **Derived, because the wiki publishes no lava figure.** It states the push
/// for water - "about 1.39 meters per second, or 25 blocks every 18 seconds"
/// (minecraft.wiki, *Water*) - and both spread *rates*: water advances a block
/// every 5 game ticks and lava one every 30 in the Overworld (minecraft.wiki,
/// *Lava*). A current is the flow going past you, so the push scales with the
/// rate: a sixth. Written as a fraction of `kCurrentSpeed` rather than as
/// 0.2333, so water's figure stays the only one anybody has to maintain.
constexpr float kLavaCurrentSpeed = kCurrentSpeed / 6.0f;

/// Set, not added, when a swimmer is pressed against a ledge with headroom.
/// This is what climbs you out of a pool instead of leaving you scrabbling.
///
/// **A fluid's rule, not water's**, and `Player.cpp`'s ledge climb now asks for
/// either. It asked only for water until 2026-08-19, and because
/// `kLavaRiseSpeed` above is exactly zero, a player who fell into a lava pool
/// with a one-block lip could not rise, could not climb and had no way out at
/// all. The reference calls that a bug by name: *Pocket Edition v0.12.1 alpha*
/// fixed **MCPE-3671, "You can't get out of lava"**, in the same version that
/// added swimming in lava and "Improved water climbing against a wall".
///
/// **Both fluids, and only the fluids.** minecraft.wiki, *Fluid*: "There are
/// only two fluid blocks: water and lava." Powder snow looks like a third and
/// is not - it is a slowdown with a leather-boot rule of its own, not a swim -
/// so the call site names water and lava rather than asking some wider
/// "am I in something thick" question.
///
/// Lava's drag eats into it: this is set at the end of a frame and the lava
/// branch eases it toward `kLavaRiseSpeed` on the next, which at 60 fps leaves
/// about 4.8 m/s against water's 5.8. Both clear a one-block lip several times
/// over.
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

/// Air supply, in seconds - and the **engine's own field is in seconds too**,
/// which is worth saying in a file where nearly everything else is a tick count
/// converted. `minecraft:breathable` in `player.json`
/// (`Mojang/bedrock-samples`, a published Bedrock behaviour pack and therefore
/// a primary source) reads `"total_supply": 15`, `"suffocate_time": -1`,
/// `"inhale_time": 3.75`. Every constant in this block is one of those fields
/// verbatim - re-verified against the file 2026-08-19.
constexpr float kAirSeconds = 15.0f;
/// A player gets one second of grace past empty before the first hit
/// (`suffocate_time: -1`); a mob gets none (`suffocate_time: 0`).
constexpr float kPlayerSuffocateGrace = 1.0f;
/// Then two health points every second, forever - the interval never shortens.
/// The amount is not in `breathable`; it is the engine's drowning cause.
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

/// The same two numbers for lava, and the reason you cannot see out of it.
/// The reference's `fog_lava` is a dark red at almost zero distance - which is
/// the whole point: inside lava you should see nothing but lava.
constexpr glm::vec3 kLavaFogColour{0.44f, 0.09f, 0.02f};
constexpr float kLavaFogDistance = 2.4f;

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
///
/// **Either fluid**, through `fluidLevel`, which is `Block.hpp`'s own owner for
/// "the level of whichever of the two this is". Lava spreads two levels a block
/// rather than one, so its steps are twice as deep; the gradient below only
/// cares that they descend.
inline float fluidHeight(BlockId id) {
    return isFluid(id) ? static_cast<float>(kMaxWaterLevel + 1 - fluidLevel(id)) / 9.0f : 0.0f;
}

/// **Changing `kMaxLavaLevel` alone fires this.** The height above divides by
/// one maximum for both fluids, which is only right while they share it.
static_assert(kMaxLavaLevel == kMaxWaterLevel,
              "one fill-height scale serves both fluids, so both must have the same top level");

// **Eight levels is right, and Mojang's own block metadata will look like it
// says sixteen. Read this before "correcting" it.**
//
// `metadata/vanilladata_modules/mojang-blocks.json` in `Mojang/bedrock-samples`
// publishes `liquid_depth` with the value list `[0 .. 15]`. That is the
// *storage domain of the property*, not the range any one block takes: the same
// file gives it **four users** - `water`, `flowing_water`, `lava` and
// `flowing_lava` - and a shared domain only ever tells you how wide the field
// is. (The one-user case is the one that proves a range; `cluster_count` has a
// single user, `sea_pickle`, so its `[0..3]` genuinely is that block's range.)
//
// What the sixteen actually are: the low three bits are the spread level, 0 for
// a source down to 7 for the thinnest, and **bit 8 is a "falling" flag** - a
// cell fed from above, which spreads only downward. Values 8-15 are that flag
// with the level bits along for the ride. So the reference has **eight spread
// levels and a flag**, which is exactly `kMaxWaterLevel = 7` plus a concept
// this engine expresses through the flow gradient instead of through the id.
// Sourced from the metadata above for the domain and the user count, and from
// minecraft.wiki *Water* for the bit layout - which is a secondary source, and
// is named as one because Bedrock publishes no `blocks/` behaviour at all.
//
// Widening these to 16 would double the fluid id run, change the `/9.0f` scale
// underneath `fluidHeight`, and make every source read as half-full.

/// Which of the two a sample is about.
///
/// **One gradient and one sampler for both, rather than a lava copy of each.**
/// A physics rule written down twice is the bug this whole file exists to
/// prevent, and the water half of it was already the only half that worked.
enum class FluidKind : std::uint8_t { Water, Lava };

constexpr bool isKind(BlockId id, FluidKind kind) {
    return kind == FluidKind::Water ? isWater(id) : isLava(id);
}

/// **Powder snow is a third case and must never become a `FluidKind`.**
///
/// minecraft.wiki, *Fluid*: "There are only two fluid blocks: water and lava."
/// Powder snow is a block entities fall *into* and are slowed by, not a fluid:
/// it has no flow, no source-and-level ladder, no waterlogging and no current,
/// and `Player.cpp` gives it its own branch with its own sink speed and its own
/// freezing clock. `sampleFluid` therefore never sees it, `FluidContact::inFluid`
/// is never true because of it, and `Player::inWater` cannot be set by it.
///
/// **This is the assert the ledge-climb comment in `Player.cpp` leans on.** That
/// rule - press against a wall while in a fluid and you climb out - is written
/// as `player.inWater || lava.inFluid`, and powder snow must not join it,
/// because its climb-out is a *different* mechanic: leather boots make it behave
/// like scaffolding, and without them there is no climb-out at all. The claim
/// there was structural ("there is no third value `sampleFluid` could be handed")
/// and until now nothing checked it. This is the check.
///
/// The second line is the control, and it is what stops the first from passing
/// vacuously: the same predicate must still answer **true** for a real source of
/// each fluid. A `isKind` that returned false for everything would satisfy the
/// negative claim on its own, which is exactly the "not vacuous on both sides"
/// failure this project has been caught by. **The single edit that fires this:
/// widening `isWater` or `isLava` - or `FluidKind` itself - to cover powder
/// snow.**
static_assert(!isKind(BlockId::PowderSnow, FluidKind::Water) &&
                  !isKind(BlockId::PowderSnow, FluidKind::Lava),
              "powder snow is not a fluid: it must not reach sampleFluid, inWater, or the "
              "ledge-climb rule that both of those feed");
static_assert(isKind(BlockId::Water0, FluidKind::Water) &&
                  isKind(BlockId::Lava0, FluidKind::Lava),
              "the control for the assert above - isKind still recognises the two fluids it "
              "does own, so the negative claim is about powder snow and not about isKind");

/// **What a fluid may move into: air, or a block the flow destroys.**
///
/// The one owner of that set. It exists because the same question was being
/// asked in four places and answered three different ways - `World.cpp`'s
/// `fluidCanEnter` and `fluidCanDrainFrom` each spelled it out longhand, and
/// `flowVector` below reached for the nearest predicate that looked similar and
/// got a different set entirely. That is this project's "constant with two
/// owners" wearing a predicate's clothes, and the cost is a stream that stops
/// at a carpet in one code path while flowing through it in another.
///
/// **It is `isWashedAway`, not `isReplaceable`, and the two are now different
/// sets on purpose** - see the two predicates in `Block.hpp` and the
/// `static_assert` that keeps them apart there, which requires a rail, a carpet
/// and redstone wire to be washed away while *not* being replaceable.
/// `isReplaceable` answers what a *player* may build into, and the reference
/// lets you build into neither a rail nor a carpet while happily washing both
/// away. Routing the flow through the placement question left rails, carpets,
/// moss carpet, redstone dust and snow deeper than one layer damming a stream -
/// which is the exact set the old `!isSolid` test also dammed, so the swap
/// fixed nothing until this landed.
///
/// That reference used to name a line number in `Block.hpp` and the line had
/// moved. Re-verified 2026-08-19: both predicates and the assert are still
/// there, and the sets are still different.
///
/// **Water is deliberately not in here.** Its two callers disagree about it and
/// that disagreement is a real rule rather than an oversight: a cell already
/// holding water is somewhere water can *be* (`fluidCanEnter`) but not
/// somewhere it can *fall to* (`fluidCanDrainFrom`, where counting it stops a
/// filled hole ever being full). Each caller adds it back if it wants it.
///
/// **All four callers now route through here**, which was the point: three
/// spellings of nearly-the-same question is how the bug below arose in the
/// first place. `flowVector` (this file), and in `World.cpp` `fluidCanEnter`,
/// `fluidCanDrainFrom` and - through the named `fluidRestsOn` in that file's
/// anonymous namespace - `fluidFeedsSideways`. That file needs
/// `#include "world/Fluid.hpp"`, which it has.
///
/// **The third one was a live bug and this is what it was.**
/// `fluidFeedsSideways` asked a bare `game::isSolid(below)`, and `isSolid` is
/// true for every flat block. So a carpet under a stream was drainable by one
/// predicate and solid support by another: water pooled on it *and* ran
/// through it. Support is now `isSolid(below) && !fluidMayOccupy(below)`, the
/// subtraction asserted at `fluidRestsOn` so the two can never disagree again.
constexpr bool fluidMayOccupy(BlockId id) {
    return id == BlockId::Air || isWashedAway(id);
}

// The families the reference washes away, named one at a time because every one
// of them dammed a flow before `isWashedAway` widened. The single edit that
// fails this: wiring `fluidMayOccupy` back to `isReplaceable`, which excludes
// all five by construction.
static_assert(fluidMayOccupy(BlockId::Air) && fluidMayOccupy(railRunFirst(0)) &&
                  fluidMayOccupy(BlockId::CarpetRunFirst) &&
                  fluidMayOccupy(BlockId::MossCarpet) &&
                  fluidMayOccupy(BlockId::RedstoneWireFirst) &&
                  fluidMayOccupy(snowLayerAt(3)) && fluidMayOccupy(BlockId::TallGrass),
              "a flow runs through everything it destroys, and destroys all of these");
// And the other side of it, which is what keeps a stream in its channel: a
// lily pad and a sculk vein are flat and are **not** washed away, so they dam
// like any wall. Fails the moment somebody writes this as `blockShape == Flat`.
static_assert(!fluidMayOccupy(BlockId::Stone) && !fluidMayOccupy(BlockId::LilyPad) &&
                  !fluidMayOccupy(BlockId::SculkVein),
              "the two flat blocks the reference keeps must still stop a flow");

/// Which way a single fluid cell is flowing, as a unit vector, or zero.
///
/// It is the gradient of the fill-height field over the four horizontal
/// neighbours - each one contributes its signed height difference along its own
/// axis. A still lake pushes nothing, because every neighbour matches.
///
/// The one special case earns its place: a neighbour that holds no fluid but
/// that **the fluid could still enter** is checked one cell *down*, and a drop
/// found there contributes more than any ordinary gradient could. That is what
/// aims a stream at the cliff edge it is about to fall over.
///
/// **Lava flows too, and it took `kind` to notice.** minecraft.wiki, *Lava*:
/// "Flowing lava can push entities, including those that do not take lava
/// damage." This used to return zero for anything that was not water, so
/// `FluidContact::flow` was permanently zero in lava.
inline glm::vec3 flowVector(const World& world, int x, int y, int z,
                            FluidKind kind = FluidKind::Water) {
    const BlockId own = world.blockAt(x, y, z);
    if (!isKind(own, kind)) {
        return glm::vec3{0.0f};
    }
    // **A falling cell's current is straight down, not nothing.** The reference
    // states it outright - "Falling water blocks have a downward current by
    // default" (minecraft.wiki, *Water*) - and the horizontal half stays zero
    // for the reason it always did: the cell is full, so a gradient reads it as
    // a mound and shoves you out from under a waterfall, which is the opposite
    // of what standing in one feels like.
    if (isFallingWater(own) || isFallingLava(own)) {
        return glm::vec3{0.0f, -1.0f, 0.0f};
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
        if (isKind(neighbour, kind)) {
            gradient = here - fluidHeight(neighbour);
        } else if (fluidMayOccupy(neighbour)) {
            // **"Somewhere the fluid could go", not "somewhere a body could
            // walk".** `isSolid` is a movement test and it answers *true* for
            // every `BlockShape::Flat` block - a rail, a carpet, a snow layer,
            // redstone dust - so a stream about to pour over a ledge with any
            // of those on it read the ledge as a wall and pushed nobody toward
            // the drop.
            //
            // It was first swapped for `isReplaceable`, which was wrong in a way
            // worth recording: that is the *placement* question, it excludes
            // every one of those families by design, and the two predicates
            // therefore agreed on the entire set the bug was about. The change
            // compiled, read plausibly, and moved nothing. `fluidMayOccupy` is
            // the flow's own question and the same one `World.cpp` should be
            // asking - see the two call sites named at its definition.
            const BlockId below = world.blockAt(nx, y - 1, nz);
            if (isKind(below, kind)) {
                gradient = here - (fluidHeight(below) - 8.0f / 9.0f);
            }
        }

        if (gradient != 0.0f) {
            flow.x += static_cast<float>(kStepX[i]) * gradient;
            flow.z += static_cast<float>(kStepZ[i]) * gradient;
        }
    }

    // **The downward half, which the model had no y component for at all.**
    // minecraft.wiki, *Water*: "A downward current in a water block is caused by
    // the block below it. Most blocks that do not have a solid upper face cause
    // downward current on above water blocks. Also, ice and falling water
    // blocks ... cause downward current on the water block above."
    //
    // "No solid upper face" is spelled here as `fluidMayOccupy` - this file's
    // own owner for "somewhere the fluid could go", and the same predicate the
    // ledge case above uses, so a drop and the pull toward it cannot disagree.
    // Ice is deliberately not modelled: it is a whole family here and the
    // gradient would have to name each member, which is the sort of second list
    // that rots.
    const BlockId under = world.blockAt(x, y - 1, z);
    if (fluidMayOccupy(under) || isFallingWater(under) || isFallingLava(under)) {
        flow.y -= 1.0f;
    }

    const float lengthSq = glm::dot(flow, flow);
    return lengthSq > 0.0f ? flow / std::sqrt(lengthSq) : glm::vec3{0.0f};
}

/// What the fluid around a body adds up to.
struct FluidContact {
    /// Any part of the box is in the sampled fluid. This is what the reference
    /// switches its whole movement model on, so a player wading is already
    /// swimming.
    ///
    /// **Named for the fluid rather than for water**, because the same sampler
    /// now answers for lava and a field called `inWater` that was true in lava
    /// is precisely how a rule ends up applied to the wrong one.
    bool inFluid = false;
    /// How far the fluid surface stands above the underside of the box. Zero
    /// when dry. Used to tell a puddle from a pool.
    float depth = 0.0f;
    /// Averaged unit push from every fluid cell the box touches.
    glm::vec3 flow{0.0f};
};

/// Samples every cell a body occupies.
///
/// Averaging over the occupied cells rather than sampling one point is what
/// stops the push flickering as a body straddles a boundary.
inline FluidContact sampleFluid(const World& world, const Aabb& box,
                                FluidKind kind = FluidKind::Water) {
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
                // A waterlogged cell is water for every purpose except what it
                // looks like, so it counts here as a full source block. Nothing
                // is ever lava-logged, so this half belongs to water alone.
                const bool logged = kind == FluidKind::Water && world.waterloggedAt(x, y, z);
                if (!isKind(block, kind) && !logged) {
                    continue;
                }
                contact.inFluid = true;
                // **Asked of the height function, not written out as 1.0.** A
                // source is 8/9 of a cell, not a whole one, so the literal made
                // a waterlogged slab stand 0.111 blocks deeper than the water
                // beside it while the comment above claimed the two were the
                // same - which is the "derived somewhere other than the table
                // that owns it" shape, and it moved where treading starts.
                surface = std::max(surface, static_cast<float>(y) +
                                                fluidHeight(logged ? BlockId::Water0 : block));
                sum += flowVector(world, x, y, z, kind);
                ++cells;
            }
        }
    }

    if (contact.inFluid) {
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
