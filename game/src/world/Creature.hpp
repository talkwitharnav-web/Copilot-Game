#pragma once

#include "world/Biome.hpp"
#include "world/Block.hpp"
#include "world/Fluid.hpp"
#include "world/Pathfinder.hpp"
#include "item/Item.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace game {

class World;

/// The creature skin sheet's exact size. **Every net origin in `Creature.cpp` is
/// measured against these**, so a sheet of any other size does not fail - it
/// silently slides every species' UVs and scrambles the whole roster at once.
/// `tools/make-creature-skins.ps1` owns the file; this is the one place the
/// number is written down in code, and `Main.cpp` checks the PNG against it.
constexpr int kCreatureSheetWidth = 128;
constexpr int kCreatureSheetHeight = 3552;

/// Which animal this is.
enum class CreatureKind : std::uint8_t {
    Sheep,
    Cow,
    Pig,
    Bramble,
    Chicken,
    Cat,
    Camel,
    Horse,
    Mule,
    Llama,
    Donkey,
    Goat,
    Rabbit,
    Wolf,
    Frog,
    Fox,
    Ocelot,
    PolarBear,
    Panda,
    SlimeSmall,
    SlimeMedium,
    SlimeLarge,
    Spider,
    CaveSpider,
    Zombie,
    Skeleton,
    Villager,
    // Appended rather than inserted: the showcase setting addresses a species by
    // this index, and every skin row offset is written against it.
    Husk,
    Silverfish,
    /// Our name for the reference's tall blackened skeleton. *Wither* is a
    /// coined name and needs ours; *skeleton* is an ordinary English word.
    Blackbone,
    Stray,
    Bogged,
    ZombieVillager,
    Witch,
    WanderingTrader,
    /// Our name for the reference's gilded swine-folk. *Piglin* is coined and
    /// needs ours; the user named this one and its larger cousin.
    Princepin,
    /// The first three that belong in water. The drowned walks the seabed on
    /// the zombie's rig; the two fish are the first things here that swim, and
    /// they are the reason a second movement model exists at all.
    Drowned,
    Cod,
    Salmon,
    /// The pufferfish inflates in three stages and the squid is the first thing
    /// here built from a ring of limbs rather than a spine and legs.
    Pufferfish,
    Squid,
    GlowSquid,
    /// The amphibious three and the one that comes in a dozen liveries. All
    /// four swim, and unlike the fish the first three can walk out of it.
    Turtle,
    Dolphin,
    Axolotl,
    TropicalFish,
    /// Six that share a rig with something already here, which is what makes
    /// them a table row and a skin rather than a model. Confirmed against
    /// `Mojang/bedrock-samples`' own `<mob>.entity.json`, which names the
    /// geometry outright - a whole-sheet alpha diff calls the skeleton horse a
    /// different model, and it is not: those are holes in the artwork.
    MushroomCow,
    SkeletonHorse,
    ZombieHorse,
    TraderLlama,
    /// Our names. *Piglin* is coined and already answers to Princepin here, so
    /// its two cousins join the same family; *brute* and *zombie* are ordinary
    /// English and stay.
    PrincepinBrute,
    ZombiePrincepin,
    /// Our name for the reference's tiny end-vermin. *Endermite* is coined;
    /// *void* and *mite* are both ordinary words.
    Voidmite,
    /// Not a scaled slime, which is the obvious guess and wrong: the reference
    /// builds it from **eight stacked slabs** around a glowing core, and pulls
    /// them apart as it hops. Three sizes, splitting like the slime.
    MagmaCubeSmall,
    MagmaCubeMedium,
    MagmaCubeLarge,
    Count,
};

/// What a creature currently wants to attack.
///
/// **Written by the targeting behaviours and read by the attacking ones.** That
/// split is the whole of why "hunts on sight", "fights back when struck" and
/// "joins in when its own kind is attacked" are three rows in the behaviour
/// table rather than three branches inside one function - the producers never
/// know who consumes what they wrote, and the consumers never know who wrote it.
///
/// There is exactly one thing worth attacking so far. When creatures fight each
/// other this becomes an index into the population.
enum class CreatureTarget : std::uint8_t {
    None,
    Player,
};

/// Everything that varies between species, one row each.
///
/// Same reasoning as the biome table: adding an animal should mean adding a row
/// here, never another branch in the update loop. The behaviour code reads this
/// and never asks which kind it is holding.
///
/// The *model* is the one exception: box layouts live in `buildMesh`, because a
/// creature's shape is not a number.
struct CreatureSpecies {
    const char* name;

    /// Collision box. Deliberately narrower than the body looks, so a creature
    /// never wedges in a gap the player can walk through.
    float halfWidth;
    float height;

    /// How the animal moves. `gaitRate` is how fast the legs cycle per metre
    /// travelled, so a short-legged bird takes quicker steps than a camel;
    /// `gaitSwing` is **how far a limb turns about its joint, in radians**, at
    /// full stride. It is an angle rather than a distance because a limb
    /// rotates about its hip rather than sliding back and forth - which is what
    /// lifts the foot, eases the ends of the stride, and keeps the top of the
    /// leg welded to the body. The reference swings a leg 1.4 rad; the roster
    /// tops out at half that on purpose, because rotating lifts a foot by
    /// `L(1 - cos angle)` and a full-amplitude animal prances.
    ///
    /// `hops` replaces both with a real jump - the launch sets a velocity and
    /// gravity does the rest - because a rabbit does not walk, and a bob faked
    /// over a constant glide does not look like one either.
    float gaitRate;
    float gaitSwing;
    bool hops;
    /// Only read when `hops`. The launch speed alone fixes the arc height and
    /// the airtime; the gather is the pause between hops. A frog's hop is far
    /// higher and lazier than a rabbit's, which is the whole difference between
    /// how the two read.
    float hopLaunch;
    float hopGather;

    /// Uniform render scale. The horse family shares one model at three sizes,
    /// which is how the original does it too.
    float modelScale;

    int health;
    float walkSpeed;
    /// Used when fleeing or chasing. Prey run slightly faster than the hunter,
    /// so a chase can end in escape rather than a guaranteed kill.
    float runSpeed;

    bool hostile;
    /// How far off it notices the player, in metres.
    float senseRange;
    /// Zero means it cannot bite at all. A species that is not `hostile` but
    /// can bite is *neutral*: it ignores you until struck, and then fights back
    /// rather than bolting. That is the whole of the wolf's temperament, and it
    /// needed no flag of its own.
    int attackDamage;

    /// Spawning. Nocturnal species appear only when the sun is down and refuse
    /// to spawn near a light source, which is what makes torches worth placing.
    bool nocturnal;
    int maxBlockLight;
    /// Relative likelihood among the species allowed at a candidate spot.
    float weight;

    // Everything below has a default, so the nineteen rows written before these
    // existed are still correct without restating them.

    /// Retires at dawn wherever the sky can see it. Separate from `nocturnal`
    /// because spawning in the dark and burning by day are different rules: the
    /// spider and the slimes do the first and not the second, so meeting one in
    /// daylight is a real possibility rather than a bug.
    bool burnsInDay = true;
    /// A neutral that turns hunter in the dark. Zero means never - otherwise it
    /// hunts on sight wherever the light reaching it is at or below this, and
    /// drops back to ordinary neutrality in the light. That is the spider, and
    /// it is what makes a torch a defence rather than only a lamp.
    int huntsBelowLight = 0;
    /// What it leaves behind when killed. `Count` means nothing: a large slime
    /// becomes mediums, a medium becomes smalls, and a small is the end of it.
    CreatureKind splitInto = CreatureKind::Count;
    int splitMin = 0;
    int splitMax = 0;
    /// Treats the side of a solid block as a ladder while hunting. The spider's
    /// one trick, and the reason a wall alone is not a defence against it.
    bool climbs = false;
    /// Odds that one spawning here is a baby. Zero for everything that has no
    /// business having young - the hostiles, the villager and the trader. A
    /// baby is a scale multiplier and a slightly quicker walk, nothing more,
    /// which is what makes a herd read as a family for almost no code.
    float babyChance = 0.0f;
    /// Largest group generated *with a chunk*, as opposed to the continuous
    /// spawn cycle around the player. Zero means this species is never placed
    /// that way, which is every hostile - the reference only does this for its
    /// passive "creature" category, and that is what makes a fresh world feel
    /// inhabited on arrival rather than filling in behind you.
    int groupSize = 0;

    /// Blast power, or zero for anything that does not detonate. Three is the
    /// reference's creeper: it clears about four blocks of soil and barely
    /// dents stone. **This is the whole of what makes a species explosive** -
    /// the fuse, the swell and the blast all read it, and nothing else has to
    /// know which animal it is holding.
    float explodePower = 0.0f;
    /// Seconds from the fuse lighting to the blast, and the two distances that
    /// start and cancel it. Bedrock's numbers, which are **not** Java's: it
    /// swells at 2.5 m and gives up at 6, where Java uses 3 and 7.
    float fuseSeconds = 1.5f;
    float swellStartRange = 2.5f;
    float swellStopRange = 6.0f;

    /// Runs from cats and ocelots inside this, or zero to ignore them. The
    /// reference's `avoid_mob_type`, narrowed to the one pairing that exists
    /// here - and it is the whole reason a cat is worth keeping around.
    float avoidFelineRange = 0.0f;

    /// How high a rise it simply walks up, and how high it can jump when one is
    /// taller than that. **These are two different mechanisms and the reference
    /// keeps them apart deliberately**: `step_height` is 0.6 for almost
    /// everything, 1.0 for the long-legged, and 1.5 for a camel, and anything
    /// whose step height already clears a full block never jumps at all. So a
    /// horse glides up a ledge and a chicken hops it, which is exactly how the
    /// two read in the original.
    ///
    /// A jump is real physics: the launch speed is derived from this height
    /// against creature gravity, and the arc, the airtime and the landing all
    /// fall out of it. Zero means it never jumps - either because its step
    /// height covers the job, or because `hops` already makes jumping its
    /// entire way of moving.
    float stepHeight = 0.6f;
    float jumpHeight = 1.2522f;

    /// What a descent is multiplied by each tick while airborne, or 1 for a
    /// normal fall. **This is the chicken**, and it is the whole of why one can
    /// be dropped off a cliff and walk away: 0.6 per tick gives a terminal
    /// descent under 2 m/s against the 60 everything else reaches. It flaps
    /// while it does it, and the flapping is driven by the same airborne state
    /// rather than being a separate animation.
    ///
    /// Applies to chicks exactly as it does to adults - `scale` never enters
    /// into it, which is the reference's behaviour too.
    float fallDrag = 1.0f;

    // --- Water. Bedrock keeps these on two different components and they mean
    // --- genuinely different things, so they are four flags rather than one
    // --- "aquatic" bit. `RESEARCH.md` §9.1 has the shipped values.

    /// Bedrock's `minecraft:behavior.float`, which almost every land animal
    /// carries: while its head is under, it swims up. **This is what makes a
    /// cow bob at the surface, and it is a behaviour rather than buoyancy** -
    /// nothing pushes it, it swims. Turning it off is how the reference lets
    /// the undead walk the seabed, and how the Princepin drowns.
    bool floats = true;

    /// Bedrock's `can_sink`. False is neutral buoyancy: it holds whatever depth
    /// it is at instead of settling. Only the frog on this roster.
    bool sinks = true;

    /// Bedrock's `is_amphibious`: it walks on the bottom rather than swimming.
    /// Paired with `floats = false` on every mob that has it, which is what
    /// makes a drowned horde cross a lake along the floor.
    bool amphibious = false;

    /// Never runs out of air. Bedrock's `breathable.breathes_water`, and the
    /// list is exactly the undead plus the frog.
    bool breathesWater = false;

    /// Bedrock's `avoid_water`, which is a **pathing** preference and nothing
    /// more: it routes around water when it can, and is perfectly capable of
    /// being knocked into it. Most farm animals have it; a chicken, a wolf, a
    /// llama and a polar bear do not.
    bool avoidsWater = false;

    /// Lives in water and moves in three dimensions there. Bedrock spreads this
    /// across `physics.has_gravity: false`, `navigation.can_swim` and
    /// `can_walk: false`; one flag covers all three here because nothing on
    /// this roster wants them apart. **It is not `amphibious`** - that is a
    /// walker that copes underwater, and this is something that cannot walk.
    bool swims = false;

    /// Bedrock's `breathable.breathes_air`. False is a fish: out of water it
    /// runs the same fifteen-second supply down and then suffocates, which is
    /// the exact mirror of what drowning does to everything else.
    bool breathesAir = true;

    /// Wants a water cell to spawn in. Separate from `swims` because the
    /// drowned needs both water *and* solid ground under it - it walks the
    /// bottom rather than swimming over it.
    bool spawnsInWater = false;

    /// Highest cell it will spawn in, or 0 for no ceiling. Bedrock's
    /// `height_filter`, which is how the reference expresses depth: its ocean
    /// fish take 0-64 against a sea level of 63 - anywhere at or below the
    /// surface - while a glow squid takes -64 to 30, thirty-three blocks down.
    /// Ours is a quarter the height of theirs, so a depth below sea level
    /// converts at the same 0.19 the ore bands use.
    int maxSpawnY = 0;

    /// How many of this species may be loaded at once, or 0 for no limit.
    /// Bedrock's `density_limit`, and it is per species rather than global -
    /// which is what keeps a shoal of cod from crowding out every squid in the
    /// sea when both want the same water.
    int maxLoaded = 0;

    /// Inflates when something gets close. Three stages, and they are three
    /// separate models rather than one model scaled, so this drives geometry
    /// rather than a size. **The collision box does not grow with it** - one
    /// species has one shape here, and a per-stage row is a change to the shape
    /// table rather than to this row.
    bool puffs = false;

    /// Lit by itself rather than by the world. We have no emissive materials
    /// until the renderer is rebuilt, so this simply feeds the skin full light
    /// instead of the cell's - which is most of what one looks like in a dark
    /// ocean, and costs nothing.
    bool glows = false;

    /// Moves by pulsing rather than by swimming steadily. **A squid has no
    /// legs and does not walk**: its tentacles flare wide, snap shut, and the
    /// snap is what pushes it. So its speed is not a constant to be steered -
    /// it surges on the close and coasts in between, and the animation and the
    /// movement are the same number rather than two that have to be kept in
    /// step.
    bool jets = false;

    /// Whether it can get about on land at all. **A swimmer is not necessarily
    /// a fish**: Bedrock's turtle, axolotl and dolphin all set `can_swim` *and*
    /// `can_walk`, so they move in three dimensions in the water and walk out
    /// of it. A cod sets `can_walk: false` and can only flop.
    bool walksOnLand = true;

    /// Seconds out of water before it starts taking damage, or 0 for never.
    /// Bedrock's `drying_out_timer`, and it is **not** the breath counter: a
    /// dolphin drowns if it is held under *and* dries out if it is kept out,
    /// which are two timers running in opposite directions.
    float dryOutSeconds = 0.0f;

    /// How many skins this species has, each one a full net on its own rows of
    /// the sheet. One means the single skin at `skinRow`. Bedrock's
    /// `minecraft:variant`, which is how it gets five axolotls and a whole reef
    /// of tropical fish out of one model.
    int variantCount = 1;

    // --- Aggression. Bedrock's `nearest_attackable_target` and
    // --- `minecraft:angry`, cut down to the fields that mean anything while
    // --- the player is still the only thing worth attacking.

    /// Whether noticing the player needs a clear line of sight. Bedrock's
    /// `must_see`, and **every hostile in the shipped data sets it** - which is
    /// the whole reason a wall is a defence. It defaults to `false` there and
    /// to `true` here, because the one species that genuinely leaves it off is
    /// easier to name than the thirty-five that do not: the silverfish comes at
    /// you through stone.
    bool mustSee = true;

    /// How long it keeps coming after losing sight of you. Bedrock's
    /// `must_see_forget_duration`, whose default is three seconds. The zombie
    /// is the one that overrides it, at **seventeen** - which is why ducking
    /// round a corner shakes off a skeleton and does nothing at all about a
    /// zombie.
    float forgetSeconds = 3.0f;

    /// How far it will follow something it already has, or zero to use
    /// `senseRange`. Bedrock's `within_radius` / `follow_range`, and it is
    /// **not always wider than the range it notices you at**: a zombie spots
    /// you at 35 m and gives up at 25.
    float leashRange = 0.0f;

    /// How long being struck - or noticing you in the dark - keeps mattering.
    /// Bedrock's `minecraft:angry.duration`, and the spread is enormous and
    /// entirely deliberate: a llama sulks for four seconds, a wolf for
    /// twenty-five, a polar bear for five hundred, a silverfish forever. Ours
    /// was a flat six for every animal on the roster.
    float angerSeconds = 6.0f;

    /// How far striking one rouses its own kind, or zero for none.
    ///
    /// The reference does this through `minecraft:angry.broadcast_anger` rather
    /// than `hurt_by_target.alert_same_type`, which is off on everything except
    /// the silverfish - and **the undead do not do it at all**, so a horde has
    /// to be walked into rather than summoned by hitting one of them.
    ///
    /// The passive rows are ours rather than the reference's, which gives farm
    /// animals no alerting whatsoever. A herd that scatters together is worth
    /// keeping.
    float alertRange = 0.0f;

    /// Bedrock's `melee_box_attack.speed_multiplier`: how much quicker it moves
    /// while closing on something than while going anywhere else. The skeleton
    /// family and the Bramble sprint the last stretch; a zombie does not.
    float chaseSpeedScale = 1.0f;
    /// And `panic.speed_multiplier`. Expressed against the roster rather than
    /// against the reference's own stroll speed, so only the rows that
    /// genuinely stand out move - a fleeing villager is slower than it walks,
    /// a rabbit is far faster.
    float panicSpeedScale = 1.0f;

    /// Whether a blow from this species swings its arms.
    ///
    /// **It is not simply "has arms", and that is the whole point of the
    /// field.** The skeleton family are archers - the reference gives them
    /// `ranged_attack` at priority 0 and only drops them to melee when they
    /// have no bow - so miming a sword swing tells the player exactly the wrong
    /// thing about what they are. They still deal contact damage here, because
    /// we have no arrows yet and a harmless skeleton is worse than an
    /// unconvincing one, but they do not wind up to it.
    ///
    /// Defaults false, which is safe: most of the roster has no arms at all, so
    /// the rows that want this are the handful that opt in rather than the
    /// majority that would have to remember to opt out.
    bool swingsArms = false;
};

const CreatureSpecies& speciesInfo(CreatureKind kind);

static_assert(kSpawnEggItems == static_cast<int>(CreatureKind::Count),
              "the two spawn egg runs together must cover every species");

/// The egg that produces this species, and the species an egg produces. The two
/// runs are deliberately the same order, so this is arithmetic rather than a
/// thirty-six row table nobody would keep in step.
constexpr ItemId spawnEggFor(CreatureKind kind) {
    return spawnEggForIndex(static_cast<int>(kind));
}

/// Only meaningful when `isSpawnEgg` holds.
constexpr CreatureKind creatureForSpawnEgg(ItemId item) {
    return static_cast<CreatureKind>(spawnEggIndex(item));
}

/// "Sheep Spawn Egg". Lives here because this is the one place that knows what
/// a species is called, and returns a stable pointer so the tooltip can hold it.
const char* spawnEggName(ItemId item);

/// Whether this species will spawn in that region.
bool spawnsIn(CreatureKind kind, BiomeId biome);

/// A blow landed on the player this frame. Returned rather than applied,
/// because the creature system has no business writing to the player.
struct CreatureAttack {
    bool landed = false;
    glm::vec3 push{0.0f};
    int damage = 0;
};

/// A blast a creature set off this frame, for the caller to apply.
///
/// Same reasoning as `CreatureAttack` and rather more important: an explosion
/// rewrites terrain, and **only the main thread may mutate the world**. So the
/// creature decides *that* it went off and where, and somebody else decides
/// what that does to the blocks.
struct CreatureExplosion {
    glm::vec3 centre{0.0f};
    float power = 0.0f;
};

/// A living thing in the world.
///
/// The **general entity** dropped items deliberately were not: it has size, so
/// it collides on every axis rather than only vertically, it faces a direction,
/// and it decides where to go.
struct Creature {
    CreatureKind kind = CreatureKind::Sheep;

    /// What it is trying to attack right now. Recomputed from scratch every
    /// tick, so it can never go stale: a producer that stops running stops
    /// asserting the target, and the consumers simply find nothing there.
    CreatureTarget target = CreatureTarget::None;

    /// Centre of the feet, matching the player, so standing on a surface is the
    /// same arithmetic for both.
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};

    /// Where it is pointing, and where it wants to point. Turning is eased
    /// toward the target so a change of direction is a turn rather than a snap.
    float yaw = 0.0f;
    float targetYaw = 0.0f;

    /// How far the **body** is tipped nose-down, and where it wants to be.
    /// Only a swimmer uses these: a walker's body is level by definition, and
    /// its head tips on its own pair below. Positive is nose-down, matching
    /// every other pitch here.
    float pitch = 0.0f;
    float targetPitch = 0.0f;

    /// How inflated a pufferfish is, from 0 to 2. A float rather than the
    /// reference's three component groups, because the stages are separated by
    /// timers either way and one number carries both the stage and how far
    /// through it this individual is.
    float puff = 0.0f;

    /// Where a jetting creature is in its pulse, from 0 to two pi, and how much
    /// push it still has from the last one. Only `jets` species use them. The
    /// phase is seeded at random on the first tick so a group does not pulse in
    /// unison, which is the one thing that would make eight squid read as one
    /// machine.
    float jetPhase = 0.0f;
    float jetPower = 0.0f;

    /// How far a jetting creature's body is tipped away from hanging upright,
    /// in radians, eased. **Derived from where it is actually moving, not from
    /// where it wants to go**: a squid hangs vertically when still and lies
    /// right over when swimming flat out, so the bell leads and the tentacles
    /// trail. Zero is upright, a right angle is horizontal, and a half turn is
    /// diving straight down.
    float bodyTilt = 0.0f;

    /// Where the **head** points, which is deliberately not where the body
    /// walks. Two separate controllers means a behaviour can claim one without
    /// the other, and this pair is what that buys: a creature that ambles one
    /// way while watching another.
    ///
    /// `targetHeadYaw` is reset to the body's heading every tick, so a look
    /// behaviour that stops running hands the head straight back.
    float headYaw = 0.0f;
    float targetHeadYaw = 0.0f;
    /// And how far it is tipped up or down, which is the other half of looking
    /// at something rather than merely facing it. Far simpler than the yaw:
    /// the body never follows it and it cannot wrap, so it is one angle eased
    /// and clamped. Zero is level, positive is nose-down.
    float headPitch = 0.0f;
    float targetHeadPitch = 0.0f;
    /// How long the current glance has left. Bedrock's `look_time`.
    float lookTimer = 0.0f;

    /// Counts down to the next decision while wandering.
    float decisionTimer = 0.0f;
    bool walking = false;
    /// Whether it is going somewhere in earnest rather than ambling - set by
    /// whichever behaviour holds the movement controller. Decides run speed
    /// against walk speed, how far the legs swing, and whether a climber
    /// bothers with a wall.
    bool running = false;
    /// What the running behaviour wants to multiply that speed by. Bedrock's
    /// `speed_multiplier`, and it is per-behaviour rather than per-species
    /// because the same animal flees faster than it chases.
    float speedScale = 1.0f;

    /// The route it is walking, and the place that route was built to reach.
    ///
    /// **This is a plan, where `targetYaw` is an instinct.** The steering fan
    /// looks 0.9 m ahead and takes the first heading that is not blocked, which
    /// can never choose to go *away* from where it wants in order to get round
    /// something - so a wall longer than it can see is hugged rather than
    /// rounded, and a dead end is oscillated in. A route is searched before a
    /// step is taken, so it can commit to the detour.
    path::Route route;
    glm::vec3 routeGoal{0.0f};
    /// Counts down to the next search. The reference recomputes a chase path
    /// every four to ten ticks rather than every tick, and paying for that
    /// across a whole population is the only expensive part of pathfinding.
    float repathTimer = 0.0f;
    /// Where it stood when progress was last checked, and how long it has made
    /// none. The reference stops a path whose mob is not advancing; without
    /// that, a route asking for something the legs cannot actually manage is
    /// walked into forever.
    glm::vec3 progressFrom{0.0f};
    float progressTimer = 0.0f;
    /// Where an ambling creature has decided to go. A **place** rather than a
    /// bearing, because a bearing is not something that can be pathed to.
    glm::vec3 wanderGoal{0.0f};

    /// How long it still believes it can see the player. Refreshed whenever the
    /// ray gets through and counted down otherwise, so a fence post clipped for
    /// one frame does not reverse a countdown.
    float sightTimer = 0.0f;

    /// Counts down to the next attempt at noticing something.
    ///
    /// Bedrock scans on a `scan_interval` of ten ticks rather than every tick,
    /// and copying that does two separate jobs. It stops a target flickering on
    /// and off frame by frame at the exact edge of the sense range, which is
    /// what the old distance hysteresis was standing in for. And it pays for
    /// the line-of-sight ray twice a second instead of a hundred and twenty
    /// times, which is what makes sight affordable for the whole roster rather
    /// than only for the exploders.
    float scanTimer = 0.0f;
    /// How long it still remembers something it can no longer see. Bedrock's
    /// `must_see_forget_duration`, refreshed for as long as the ray gets
    /// through - so a creature that has you does not lose you the instant a
    /// tree passes between the two of you.
    float forgetTimer = 0.0f;

    /// How far it has dropped since it last stood on something. Only the
    /// exploders read it - a hard landing shortens their fuse - but fall damage
    /// at M21 wants exactly the same number.
    float fallDistance = 0.0f;

    /// How long it stays roused after being struck, or after a neighbour of its
    /// own kind was. **The six-second grudge**, and the one thing `strike` now
    /// writes: it records that something happened and leaves the behaviour
    /// table to decide whether that means fighting back or bolting.
    float provokedTimer = 0.0f;

    /// How far through its fuse an exploder is, in seconds. Counts up while the
    /// swell behaviour holds and **runs back down when it does not**, so
    /// breaking line of sight or backing away genuinely defuses it rather than
    /// merely pausing it.
    float fuseTimer = 0.0f;
    /// Whether the fuse is being held this tick. Re-derived every tick like
    /// `target` and `running`, which is what lets the countdown reverse without
    /// the behaviour needing to know it stopped.
    bool swelling = false;

    /// Drives the leg swing. Advanced only while moving, so a standing creature
    /// has its legs still.
    float gait = 0.0f;

    /// How much of the species' full swing the limbs are using, 0 to 1, eased.
    /// The reference's `limbSwingAmount`, and it does two jobs at once: it
    /// stops the leg cycle popping on and off as a creature starts and stops,
    /// and it is the whole of the difference between a walk and a run, so one
    /// amplitude covers both without a second animation.
    ///
    /// Saturation is against the species' own `runSpeed` rather than the
    /// reference's flat 5 m/s, because our roster moves at about a third of
    /// Minecraft's speeds - measured against theirs, everything here would
    /// barely lift a foot.
    float limbSwingAmount = 0.0f;

    /// Wing beat, for anything with `fallDrag`. `flapSpeed` is how far the
    /// wings open - it climbs while airborne and falls back on the ground -
    /// `flapping` is the beat's own energy, refreshed while airborne and dying
    /// away after landing, and `flap` is the phase those two advance. Three
    /// fields because that is what the reference uses, and the reason it needs
    /// three is that the wings keep beating for a moment after touchdown.
    float flap = 0.0f;
    float flapSpeed = 0.0f;
    float flapping = 0.0f;

    /// Counts down the gather a hopper spends on the ground between jumps.
    /// Unused by everything that walks.
    float hopTimer = 0.0f;

    /// How long this individual has been alive, for idle motion that runs
    /// whether or not it is going anywhere. Per creature rather than a world
    /// clock so a crowd does not sway in unison.
    float age = 0.0f;

    /// How far the **drawn** body is still lagging below where the collision
    /// box already is, in metres, after walking up a step.
    ///
    /// A step-up has to move the box in one go - anything gradual is a new way
    /// to get wedged half inside a block - so what eases instead is only what
    /// is rendered. Nothing else may read this: it is a lie told to the eye,
    /// and physics, targeting and reach all want the truth.
    float stepSmooth = 0.0f;

    /// Any part of the body is in water, which is what switches its whole
    /// movement model over - the same test the reference uses.
    bool inWater = false;

    /// Seconds of breath left. Counts down only while the head is under, and
    /// refills over `fluid::kInhaleSeconds` once it is out.
    float air = fluid::kAirSeconds;

    /// Time since the air ran out, so drowning lands two health points on the
    /// second rather than once a frame.
    float drownTimer = 0.0f;

    /// Standing until something says otherwise. `step` recomputes it every tick
    /// before anything reads it, so the default only matters for a creature
    /// that has never ticked - which is exactly the frozen showcase, where
    /// *standing* is the pose worth reviewing.
    bool onGround = true;
    int health = 6;
    /// Brief flash after being struck.
    float hurtTimer = 0.0f;
    /// Stops a hostile landing a blow every frame it is touching you.
    float attackTimer = 0.0f;
    /// Time left in the arm swing of a blow that has just landed. Far shorter
    /// than `attackTimer`, because the arm is moving for well under a third of
    /// the cycle and rests visibly in between.
    float swingTimer = 0.0f;

    /// This individual's size against its species. One for an adult; a baby is
    /// smaller, and it multiplies the collision box as well as the model, so a
    /// calf fits through gaps its mother cannot.
    float scale = 1.0f;

    /// Which of its species' skins this one wears, below `variantCount`. Rolled
    /// at spawn. **Not saved yet** - a reloaded fish comes back in a different
    /// livery, which is cosmetic and waits on a save-format version bump.
    std::uint8_t variant = 0;

    /// How long it has been out of water, for anything with `dryOutSeconds`.
    float dryTimer = 0.0f;

    /// Struck by lightning, in the reference. **Doubles the blast power and
    /// nothing else** - a charged creeper has the same twenty health as any
    /// other, which is worth stating because the obvious guess is that it is
    /// tougher. It is not; it is louder.
    ///
    /// We have no weather until M27, so for now a small share of Brambles
    /// arrive this way instead of being made by a storm.
    bool charged = false;

    /// Which behaviours were running last tick, one bit per row of the table
    /// in `Creature.cpp`. Opaque outside the selector, and it exists for one
    /// reason: a behaviour that is already running is asked whether it may
    /// *continue*, which is deliberately a looser question than whether it may
    /// *start*. A hunter gives up further out than it engages.
    std::uint16_t runningBehaviours = 0;
};

/// Every creature currently loaded, plus the rules that spawn and retire them.
class Creatures {
public:
    explicit Creatures(std::uint32_t seed);

    /// Moves, decides and animates. Creatures are few and touch the world only
    /// to read it, so this stays on the main thread with the drops. Returns any
    /// blow landed on the player for the caller to apply, and appends any blast
    /// that went off to `blasts` for the same reason.
    CreatureAttack update(const World& world, const glm::vec3& playerFeet, float deltaSeconds, bool night,
                          bool playerSneaking, std::vector<CreatureExplosion>& blasts);

    /// Adds and retires creatures around the player. Kept apart from `update`
    /// because it runs on its own slower clock - trying every frame would spend
    /// most of its time failing to find a spot.
    void manage(const World& world, const glm::vec3& playerFeet, float deltaSeconds, bool night);

    /// Strikes the first creature the aim ray reaches. Returns true if one was
    /// hit, so the caller can spend a swing on it instead of the block behind.
    bool strike(const glm::vec3& eye, const glm::vec3& forward, float reach, int damage);

    /// Damages and throws everything caught in a blast. Returns how many were
    /// hit, which is the number that makes "did the explosion reach anything?"
    /// answerable from the log rather than by standing next to one.
    ///
    /// Lives here rather than in the caller because `Creatures` owns the
    /// population; the caller owns the world and rewrites the blocks. Anything
    /// reduced to zero health is retired by the next `manage`, so a slime
    /// caught in a blast still splits.
    int applyExplosion(const World& world, const glm::vec3& centre, float power);

    /// Whether a creature stands in the way of the aim ray. Asked every frame,
    /// where `strike` only lands once a swing is ready - the block behind a
    /// creature must stay protected during the swing's cooldown too.
    bool aimedAt(const glm::vec3& eye, const glm::vec3& forward, float reach) const;

    /// Built fresh each frame, for the same reason dropped items are: world
    /// meshes are drawn with an identity model matrix, so vertex positions have
    /// to *be* world positions.
    ///
    /// Anything see-through goes into `translucent` instead of the return value,
    /// because blending depends on draw order and the renderer draws every
    /// opaque mesh before any translucent one. Only a slime's outer shell uses
    /// it so far.
    engine::MeshData buildMesh(const World& world, engine::MeshData& translucent) const;

    std::size_t count() const { return m_creatures.size(); }
    /// How many are currently hunting the player. This is the number that makes
    /// "is the hostile behaviour actually running?" answerable from the log.
    std::size_t hunting() const;
    /// How many of each kind are loaded, indexed by `CreatureKind`. Answers
    /// "why is nothing spawning here" without attaching a debugger.
    std::array<std::size_t, static_cast<std::size_t>(CreatureKind::Count)> census() const;

    /// Placed only where this species could actually live: on ground for a
    /// walker, in water for a swimmer, and on the seabed *under* water for the
    /// drowned. One test rather than three call sites deciding for themselves.
    static bool canSpawnAt(const World& world, const CreatureSpecies& species, int x, int y, int z);

    /// Rolls which skin an individual wears, or 0 for a species with only one.
    std::uint8_t rollVariant(CreatureKind kind);

    /// Places one creature outright, ignoring biome, light and the population
    /// cap. Only the showcase setting and the debug spawn key use this. `puff`
    /// is for the showcase alone: the roster is frozen there, so a pufferfish
    /// can never inflate itself and has to be handed a stage.
    void place(CreatureKind kind, const glm::vec3& feet, float yaw, bool charged = false,
               float puff = 0.0f);

    /// Every creature currently loaded, for the caller to write to disk. The
    /// store owns what a save record contains; this only hands over the live
    /// list, so the two can change independently.
    const std::vector<Creature>& all() const { return m_creatures; }
    /// Puts one back exactly as it was saved, bypassing every spawn rule. The
    /// population cap still applies from the next `manage` onward.
    void restore(CreatureKind kind, const glm::vec3& feet, float yaw, int health, float scale,
                 bool charged = false);

private:
    float random01();
    void think(const World& world, Creature& creature, const glm::vec3& playerFeet, float deltaSeconds,
               bool night, bool playerSneaking, CreatureAttack& attack,
               std::vector<CreatureExplosion>& blasts);
    void step(const World& world, Creature& creature, float deltaSeconds);
    /// Pushes overlapping creatures apart horizontally. Soft, so it never
    /// fights the world collision that runs before it.
    void separate(const World& world, float deltaSeconds);
    /// Rouses the struck creature's own kind nearby: fighters join in, prey
    /// bolts with it. The reference's `alert_same_type`, and the same mechanism
    /// serves pack anger and herd flight.
    void alertNeighbours(const Creature& struck, std::size_t struckIndex);
    /// Places a chunk's own group of animals the first time the player comes
    /// near it. Derived entirely from the seed and the chunk coordinate, so a
    /// given chunk always produces the same herd.
    void populateChunks(const World& world, const glm::vec3& playerFeet);
    /// Index of the nearest creature on the aim ray, or `size()` for none.
    std::size_t findAimed(const glm::vec3& eye, const glm::vec3& forward, float reach) const;

    std::vector<Creature> m_creatures;
    /// Chunks whose one-off group has already been placed, keyed by packed
    /// coordinate. Only grows, which is correct: a chunk gets its animals once
    /// per session, and re-populating on re-entry would breed a herd out of
    /// walking back and forth.
    std::unordered_set<std::uint64_t> m_populated;
    std::uint32_t m_random;
    float m_spawnTimer = 0.0f;

    /// The one route planner, shared by the whole population so that repeated
    /// searches reuse its buffers instead of allocating.
    path::Pathfinder m_pathfinder;
    /// How many searches are left this frame. One animal boxed into a maze must
    /// not be able to cost a frame on its own, so the population shares a
    /// budget exactly as light propagation and water flow already do.
    int m_pathBudget = 0;
};

} // namespace game
