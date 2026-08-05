#pragma once

#include "world/Biome.hpp"
#include "world/Block.hpp"
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
constexpr int kCreatureSheetHeight = 1888;

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
};

const CreatureSpecies& speciesInfo(CreatureKind kind);

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

    /// How long it still believes it can see the player. Refreshed whenever the
    /// ray gets through and counted down otherwise, so a fence post clipped for
    /// one frame does not reverse a countdown.
    float sightTimer = 0.0f;

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

    bool onGround = false;
    int health = 6;
    /// Brief flash after being struck.
    float hurtTimer = 0.0f;
    /// Stops a hostile landing a blow every frame it is touching you.
    float attackTimer = 0.0f;

    /// This individual's size against its species. One for an adult; a baby is
    /// smaller, and it multiplies the collision box as well as the model, so a
    /// calf fits through gaps its mother cannot.
    float scale = 1.0f;

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

    /// Placed only where one could stand, so a spawn never lands inside rock.
    static bool canStandAt(const World& world, const CreatureSpecies& species, int x, int y, int z);

    /// Places one creature outright, ignoring biome, light and the population
    /// cap. Only the showcase setting and the debug spawn key use this.
    void place(CreatureKind kind, const glm::vec3& feet, float yaw, bool charged = false);

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
    void separate(float deltaSeconds);
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
};

} // namespace game
