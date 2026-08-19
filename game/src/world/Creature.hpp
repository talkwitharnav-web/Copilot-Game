#pragma once

#include "world/Biome.hpp"
#include "world/Block.hpp"
#include "world/DrawRange.hpp"
#include "world/Fluid.hpp"
#include "world/Pathfinder.hpp"
#include "item/Item.hpp"
#include "item/SpriteMask.hpp"

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
constexpr int kCreatureSheetHeight = 4704;

/// Where the iron golem's own 128-row net begins. It is the largest single
/// allocation the sheet has — everything else is 32 or 64 tall.
constexpr int kIronGolemSkinRow = 3680;

/// The fourteen villager outfits, sixty-four rows each: the thirteen trades and
/// the nitwit, in the same order as the job-site table. **Unemployed is not one
/// of them** — it wears the plain villager skin that already exists, which is
/// what makes a villager visibly change the moment it takes a job.
constexpr int kVillagerProfessionSkinRow = kIronGolemSkinRow + 128;
constexpr int kVillagerProfessionCount = 14;

/// The nitwit: employable by nobody, and it is a profession index rather than a
/// flag because it wears an outfit like every other trade.
constexpr std::uint8_t kNitwit = 14;

/// Which trade a block offers, 1 to 13, or 0 for a block nobody claims.
///
/// **The family predicates answer, never a list of ids.** A blast furnace has
/// eight ids for its facing and lit state and a composter has nine for its
/// fill, so naming ids here would be a second copy of `Block.hpp`'s own
/// families and would go stale the moment one of them widened.
constexpr std::uint8_t professionForJobSite(BlockId id) {
    if (isComposter(id)) {
        return 1; // Farmer
    }
    if (id == BlockId::Barrel) {
        return 2; // Fisherman
    }
    if (id == BlockId::FletchingTable) {
        return 3; // Fletcher
    }
    if (id == BlockId::Loom) {
        return 4; // Shepherd
    }
    if (id == BlockId::CartographyTable) {
        return 5; // Cartographer
    }
    if (id == BlockId::Lectern) {
        return 6; // Librarian
    }
    if (id == BlockId::Stonecutter) {
        return 7; // Mason
    }
    if (id == BlockId::SmithingTable) {
        return 8; // Toolsmith
    }
    if (id == BlockId::Grindstone) {
        return 9; // Weaponsmith
    }
    // **The narrow families are asked before the wide one.** `isFurnace` covers
    // all three cookers, so testing it first would make every blast furnace and
    // smoker in the world an armourer's - the same early-out trap that made
    // eight `blockName` cases dead code when the smoker arrived.
    if (isBlastFurnace(id)) {
        return 10; // Armourer
    }
    if (isSmoker(id)) {
        return 11; // Butcher
    }
    if (isCauldron(id)) {
        return 12; // Leatherworker
    }
    if (id == BlockId::BrewingStand) {
        return 13; // Cleric
    }
    return 0;
}

/// What a villager is called.
///
/// **Nothing calls this, and in particular it is NOT in the debug overlay -
/// which is what this comment claimed until 2026-08-19.** The claim was false
/// and it is the dangerous kind of false, because it describes a feature a
/// reader can go and look for: `CLAUDE.md` bug shape #16, a comment that is
/// wrong while the code is right, which every review passes. Anyone checking
/// "can I tell a Farmer from a Toolsmith?" would have read this line, gone to
/// the overlay, found nothing, and been left doubting the overlay rather than
/// the note.
///
/// **The profession itself is entirely live** - which is why this is a missing
/// reader and not dead code. `professionForJobSite` resolves thirteen trades
/// plus Nitwit from the claimed job block, `Creature.cpp` assigns it when a
/// villager takes a station and clears it when the station goes, it is written
/// to and read from the save, and `villagerSkinRow` already dresses the
/// villager in the right outfit. So the game knows this one is a Cleric and
/// draws it as a Cleric, and has no way to say so in words.
///
/// **Player consequence:** you can tell professions apart by robe colour and by
/// nothing else - no name plate, no overlay line, no trade screen title.
///
/// The reader belongs in `Main.cpp`, which owns every screen; see the finding
/// filed against it. Kept here rather than deleted because this is the one file
/// that knows what a profession is called, and because deleting it would mean
/// writing the same fourteen strings again the day the overlay wants them.
///
/// **What would make this note wrong**, in one grep: any hit for
/// `professionName` outside this definition and the comment in `Creature.cpp`
/// that discusses the `default:` arm below. **Re-run 2026-08-19 11:29 against
/// `Main.cpp` with a two-sided control: still zero hits, so the claim holds.**
/// Re-run it rather than trusting this line — a negative claim is the kind that
/// rots, because the world only has to move once and nothing tells the comment.
///
/// **An absence claim can never have a claim control**, because the whole
/// content of the claim is that there is nothing there to measure. So the only
/// evidence available is *instrument* controls — probes with known answers,
/// proving the detector can see. Two are needed and neither alone is enough:
///   1. **Same symbol, different file** — search `professionName` where it is
///      known present, which is the definition below. Measured 1. This is the
///      strongest kind, because it is the only probe that proves the detector
///      can see *this exact token*; a different-symbol control cannot, and would
///      fire happily while a broken pattern for this name returned zero.
///   2. **Different symbol, same file** — search a name known present in
///      `Main.cpp`, proving the probe can read that file at all. `CreatureKind`
///      measured 12 over 12118 comment-stripped lines.
/// Control 2 alone is how a sister finding was wrongly refuted on 2026-08-19: a
/// true count of a *neighbouring* symbol is the most persuasive way to be wrong,
/// because every digit checks out. **Print the lines probed as well as the
/// hits**, and *compute* that figure rather than narrating it — a hardcoded
/// "3 of 4" was written here at 11:32 when the answer was 2 of 4.
///
/// ⚠️ **And ask one question before either control, because no control can
/// reach it: is this token the only way the concept could be spelled?** Measured
/// the same day: `BeeNest` returns 0 in `Structures.cpp` while both controls
/// fire correctly (45 in-file, 39 same-symbol elsewhere) — and the feature is
/// right there, spelled `beeNestChance` and `beeNestAtLevel`, 11 hits
/// case-insensitively. Claim false, instrument perfect, both controls green.
/// **Search case-insensitively first and narrow afterwards**, and list the hits
/// rather than counting them.
///
/// ⚠️ **And do not read "1 in the header, 0 in the .cpp" as a missing
/// definition** — that inference was drawn and retracted on 2026-08-19 11:30.
/// This is `constexpr` and defined inline right here, so a caller links fine;
/// the count cannot tell a declaration from an inline definition, and the
/// disambiguator is one character, `;` against `{`.
constexpr const char* professionName(std::uint8_t profession) {
    switch (profession) {
    case 1:
        return "Farmer";
    case 2:
        return "Fisherman";
    case 3:
        return "Fletcher";
    case 4:
        return "Shepherd";
    case 5:
        return "Cartographer";
    case 6:
        return "Librarian";
    case 7:
        return "Mason";
    case 8:
        return "Toolsmith";
    case 9:
        return "Weaponsmith";
    case 10:
        return "Armourer";
    case 11:
        return "Butcher";
    case 12:
        return "Leatherworker";
    case 13:
        return "Cleric";
    case kNitwit:
        return "Nitwit";
    default:
        return "Unemployed";
    }
}

/// What a villager is doing at this hour, as a fraction of the day.
///
/// The reference's `minecraft:scheduler`, and its five windows are quoted in
/// ticks out of 24000. Our `timeOfDay` runs 0 to 1 from sunrise and so does the
/// reference's tick count, so the mapping is a plain divide with no phase
/// offset - which is worth stating, because getting it wrong would put a
/// village to bed at lunchtime and nothing would report it.
enum class VillagerPhase : std::uint8_t {
    Work,
    Gather,
    Home,
    Sleep,
};

constexpr VillagerPhase villagerPhaseAt(float dayFraction) {
    // 0-8000 work, 8000-10000 gather, 10000-11000 work, 11000-12000 home,
    // 12000-24000 bed.
    if (dayFraction < 8000.0f / 24000.0f) {
        return VillagerPhase::Work;
    }
    if (dayFraction < 10000.0f / 24000.0f) {
        return VillagerPhase::Gather;
    }
    if (dayFraction < 11000.0f / 24000.0f) {
        return VillagerPhase::Work;
    }
    if (dayFraction < 12000.0f / 24000.0f) {
        return VillagerPhase::Home;
    }
    return VillagerPhase::Sleep;
}

/// Which animal this is.
///
/// ⚠️ **`Village.hpp` forward-declares this enum** — `enum class CreatureKind :
/// std::uint8_t;` — so a pen can name the species standing in it without
/// pulling this header into every translation unit that builds a tree.
/// `Structures.hpp` includes `Village.hpp`, so the include would cost far more
/// than the one field is worth.
///
/// **The constraint, stated rather than the world:** this enum must keep a
/// fixed underlying type identical to the one in that declaration, and must
/// stay directly in `namespace game`. Changing either is the edit that breaks
/// it — which is why the warning is here, in the file that would do the
/// breaking, rather than only over there where it would never be read.
///
/// **It cannot break silently.** `Village.cpp` includes both headers, so the
/// compiler sees the opaque declaration and this definition together and a
/// disagreement is a hard error. The reason to read this first is that the
/// error will name a file you did not edit.
///
/// **Falsified by:** `Village.hpp` losing its `CreatureKind` declaration, or
/// gaining `#include "world/Creature.hpp"` — either makes this note dead
/// weight and it should be deleted. Written 2026-08-19.
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
    /// The first thing here that flies. *Bee* is an ordinary English word for a
    /// real animal, so it keeps its name.
    Bee,
    /// The village's own guard. *Iron golem* is two ordinary English words and
    /// needs no rename.
    IronGolem,
    Count,
};

/// Whether a raw number off a save is a real `CreatureKind`.
///
/// **One owner for "is this id legal", because four readers each remembering
/// the range is four chances to forget it.** `SavedCreature::kind` is a plain
/// `std::int32_t` on purpose - a number off a disk is not an enumerator, and a
/// truncated, stale or hand-edited record can hold anything at all. Casting one
/// out of range to `CreatureKind` and handing it to `speciesInfo` indexes the
/// species table past its end, which is a read of whatever memory follows an
/// eight-hundred-row constant array: not a crash you can find, just a creature
/// with someone else's health and a body box out of nowhere.
///
/// Signed and widened deliberately. Taking `std::int32_t` is what lets this
/// catch a **negative** id, which is the half a naive `< Count` test on an
/// unsigned or enum-typed value silently converts into a very large positive
/// one and waves through.
///
/// The single edit that makes the asserts below fail is giving `CreatureKind` a
/// value after `Count`, or changing this to take the enum type - either turns
/// the negative case back into the hole it used to be.
constexpr bool isKnownCreatureKind(std::int32_t kind) {
    return kind >= 0 && kind < static_cast<std::int32_t>(CreatureKind::Count);
}

static_assert(isKnownCreatureKind(0), "the first species is a legal id");
static_assert(isKnownCreatureKind(static_cast<std::int32_t>(CreatureKind::Count) - 1),
              "the last species is a legal id");
static_assert(!isKnownCreatureKind(static_cast<std::int32_t>(CreatureKind::Count)),
              "one past the end is not");
static_assert(!isKnownCreatureKind(-1), "and neither is a negative off a corrupt record");

/// What a creature currently wants to attack.
///
/// **Written by the targeting behaviours and read by the attacking ones.** That
/// split is the whole of why "hunts on sight", "fights back when struck" and
/// "joins in when its own kind is attacked" are three rows in the behaviour
/// table rather than three branches inside one function - the producers never
/// know who consumes what they wrote, and the consumers never know who wrote it.
///
/// `Creature` names another animal, and *which* one is `Creature::targetId`
/// rather than an index: the population is a vector that retires by swapping
/// the last element down, so an index goes stale the moment anything dies.
enum class CreatureTarget : std::uint8_t {
    None,
    Player,
    Creature,
};

/// What a species *is*, as a bitmask, so that anything reacting to it asks for a
/// property rather than naming a list of kinds.
///
/// The same reasoning as `BiomeTag`, and it exists for the same reason: a wolf
/// hunts sheep, rabbits, foxes and the skeleton family, and a villager runs from
/// six kinds of undead. Written as lists of names, every one of those rows would
/// have to be revisited each time the roster grew, and forgetting one would be
/// silent. Written as tags, a new species inherits every rule it qualifies for
/// the moment it declares what it is.
///
/// Deliberately narrow families rather than broad ones: the reference names
/// entity types outright, so a tag that lumps two kinds together buys a
/// divergence. `Grazer` is the sheep alone because a wolf hunts sheep and not
/// cows, and that is the whole reason `Livestock` is not a tag.
enum class CreatureTag : std::uint32_t {
    None = 0,
    /// Rotting humanoids. What a villager runs from.
    Undead = 1u << 0,
    /// The bone family. What a wolf hunts and what runs from a wolf in turn.
    Skeletal = 1u << 1,
    /// Cat and ocelot. What a Bramble runs from.
    Feline = 1u << 2,
    Canine = 1u << 3,
    Ursine = 1u << 4,
    /// The sheep alone - a wolf hunts sheep and leaves cattle be.
    Grazer = 1u << 5,
    /// The rabbit alone.
    Critter = 1u << 6,
    /// The chicken alone.
    Fowl = 1u << 7,
    Vulpine = 1u << 8,
    /// Anything a fox would take out of the water.
    Fish = 1u << 9,
    /// Villager and wandering trader.
    Trader = 1u << 10,
    /// Anything that hunts people. **The Bramble is deliberately not in it**:
    /// its hostility comes from `hostile`, and leaving the tag off is how the
    /// reference's "an iron golem never attacks a creeper" rule is expressed
    /// here — as an absent tag rather than as a negative filter, which would be
    /// a second place for the same rule to live.
    Monster = 1u << 11,
};

constexpr std::uint32_t operator|(CreatureTag a, CreatureTag b) {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}
constexpr std::uint32_t operator|(std::uint32_t a, CreatureTag b) {
    return a | static_cast<std::uint32_t>(b);
}
/// One tag as a mask. `operator|` already covers two or more.
constexpr std::uint32_t tagMask(CreatureTag t) {
    return static_cast<std::uint32_t>(t);
}

/// Which families a species belongs to. One switch rather than a column on all
/// fifty-seven rows, because the great majority belong to none of them - and a
/// grouped `case` list reads as the family it is naming.
std::uint32_t creatureTags(CreatureKind kind);

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
    ///
    /// **`height` is `minecraft:collision_box.height` from `Mojang/bedrock-samples`,
    /// raw and unscaled. `modelScale` is `minecraft:scale`. The two are
    /// INDEPENDENT and neither is ever folded into the other** (2026-08-19).
    ///
    /// That is measured rather than asserted stylistically: `height` is read by
    /// the collision box, the eye at `height * 0.85f`, `kUnstickReach`, the
    /// cramming span and the light sample - always against `creature.scale`, the
    /// per-entity baby factor, and **never against `modelScale`**. `modelScale`
    /// appears only inside the model builder, on `netW/netH/netD * kTexel`
    /// geometry. **Villager is the proof they are independent**: height 1.9 with
    /// modelScale 0.92, and 1.9 is Bedrock's box exactly, so the scale plainly
    /// is not baked in.
    ///
    /// **Two rows had the scale multiplied into `height` and both were wrong**,
    /// found and fixed 2026-08-19: pufferfish carried 0.96 = 0.8 x 1.2 and
    /// tropical fish 0.52 = 0.4 x 1.3, against JSON boxes of 0.8 and 0.4. It is
    /// `CLAUDE.md` bug shape #3 - a number ported into a field measured in a
    /// different unit - and it is invisible, because a slightly tall hitbox
    /// looks like nothing at all. **Both rows' `halfWidth` was already correct
    /// (0.28 and 0.20), which is what made the pair diagnosable**: a scale folded
    /// into one axis and not the other cannot be a deliberate choice.
    ///
    /// So when adding or checking a row, read `collision_box` out of that
    /// entity's JSON and put it here unmultiplied. What would make this note
    /// false: a reader of `height` appearing that also multiplies by
    /// `modelScale`, at which point the fields are no longer independent and
    /// every row wants revisiting together.
    ///
    /// **Widths are NOT all Bedrock and that is a separate, open gap** - sheep
    /// is 0.64 against 0.9, pufferfish 0.56 against 0.8, Blackbone 0.60 against
    /// 0.72. Filed rather than swept in with the heights, because a width change
    /// moves what fits through a gap and wants its own decision.
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
    /// The brightest **block light**, 0-15, a species will still spawn in, and
    /// it is an **inclusive maximum** - `manage` rejects a candidate cell when
    /// `blockLightAt(...) > maxBlockLight`, so 6 means "spawns at 6, not at 7".
    /// 15 is the way a row says "no light rule at all".
    ///
    /// **The reference's own field is `minecraft:spawn_rules`'
    /// `brightness_filter`, whose hostile default is `{min: 0, max: 7}`**
    /// (`Mojang/bedrock-samples`, `behavior_pack/spawn_rules/zombie.json` and
    /// `creeper.json`; Microsoft's schema documents `max` as inclusive). Our
    /// hostile rows sit at 6 rather than 7, which is one level stricter.
    /// **That gap is deliberate only in the sense that it is unresolved**:
    /// minecraft.wiki's *Mob spawning* prose contradicts the schema, saying
    /// monsters cannot spawn when "the block light level is greater than 0" at
    /// all, which would be stricter still. Two sources, three answers - so the
    /// rows were left where they play well and this note records why, rather
    /// than one of the three being picked and dressed up as the reference.
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

    /// Which families it runs from, and from how far. The reference's
    /// `avoid_mob_type`, whose per-entry `max_dist` is what this range is - not
    /// the goal-level one, which is a different field with a different default
    /// on the same page.
    ///
    /// Zero range means it runs from nothing, whatever the mask says.
    std::uint32_t avoids = 0;
    float avoidRange = 0.0f;
    /// And whether the player is one of the things it runs from. Separate
    /// because the player is not a `CreatureKind` and never will be. The
    /// reference gives a rabbit a *shorter* range for the player than for a
    /// wolf, so this carries its own.
    float avoidPlayerRange = 0.0f;

    /// Which families it hunts on sight, or zero for none. This is the whole of
    /// creature-versus-creature aggression: the row says what it eats and the
    /// behaviour table works out the rest, exactly as `hostile` does for the
    /// player.
    ///
    /// **It never targets its own kind**, which is checked rather than encoded,
    /// so a fox tagged `Vulpine` may still hunt `Critter` without hunting foxes.
    std::uint32_t hunts = 0;

    /// Whether being struck makes it fight back rather than bolt.
    ///
    /// Until creatures hunted each other, "can it bite" and "will it fight you"
    /// were the same question and `attackDamage > 0` answered both. A cat is
    /// the case that separates them: it kills rabbits and it runs from *you*,
    /// so it needs a bite and no willingness to use it on a player. The
    /// reference says the same thing by simply not giving it a `hurt_by_target`
    /// goal.
    ///
    /// True by default, so every row written before this existed is unchanged.
    bool retaliates = true;

    /// Stops to eat the ground. Bedrock's `behavior.eat_block`, which on the
    /// shipped roster is the **sheep alone** - it is what regrows a shorn
    /// fleece there, and it is why a flock leaves bare dirt behind it.
    bool grazes = false;

    /// Works a flower for nectar and carries it home to a hive, which on the
    /// shipped roster is the **bee alone**. Bedrock spells this as four goals
    /// on `bee.json` - `look_for_food`, `go_home`, `find_hive` and the
    /// `has_nectar` component group - and ours is one behaviour holding the
    /// same state machine, for the same reason `Work` is one row rather than
    /// the villager's five.
    ///
    /// **This is the only thing in the game that puts honey in a hive.** Clear
    /// it and honeycomb becomes unobtainable again, taking the honeycomb block,
    /// the candle and all four waxed-copper stages with it.
    bool pollinates = false;

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
    /// descent of 2.4 m/s against the 78.4 everything else reaches. It flaps
    /// while it does it, and the flapping is driven by the same airborne state
    /// rather than being a separate animation.
    ///
    /// **Fall damage is `ignoresFallDamage` below, not this.** It used to be
    /// charged against `fallDrag < 1` on the reasoning that a parachute and an
    /// immunity should not be separable - which read well and was wrong, because
    /// the reference's immunity list has nine of ours on it and only one of them
    /// glides.
    ///
    /// Applies to chicks exactly as it does to adults - `scale` never enters
    /// into it, which is the reference's behaviour too.
    float fallDrag = 1.0f;

    /// Takes no fall damage at all, however far it drops.
    ///
    /// **A list, not a derivation**, because the reference's is: `RESEARCH.md`
    /// §1.10's "fully immune" line names magma cube, bee, cat, chicken, iron
    /// golem and ocelot among the species we have - and pointedly *not* the
    /// slime, which is the ordinary-looking neighbour a derivation would have
    /// swept in. Nine species, six rows, no rule connecting them.
    bool ignoresFallDamage = false;

    /// Health subtracted from a fall before it is charged, in points.
    ///
    /// The reference's two named softenings, `RESEARCH.md` §1.10: a goat takes
    /// ten less and a frog five, always. It is a subtraction from the *damage*
    /// rather than from the distance, which is what "takes 10 HP less" says and
    /// is not the same thing as the safe distance below it.
    int fallDamageReduction = 0;

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

    /// Flies, and is the airborne mirror of `swims`: no gravity, a heading in
    /// three dimensions, and no pathfinder because there are no floors to plan
    /// across. The two are deliberately **separate flags rather than one
    /// "moves in 3D"**, because everything else about them differs - a swimmer
    /// is confined to water and suffocates outside it, a flier is confined to
    /// air and is perfectly happy to land.
    ///
    /// It does not imply `walksOnLand = false`: a bee settles on a flower.
    bool flies = false;

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
    /// And `panic.speed_multiplier` - but **in our unit, which multiplies
    /// `runSpeed`.** The reference's is not interchangeable with it: a Bedrock
    /// mob has a single `movement` speed and scales that, while we author walk
    /// and run separately, so `runSpeed` already means "in earnest". A value
    /// below 1.0 here therefore says "flee slower than you run", which lands
    /// within a whisker of a stroll and reads as not fleeing at all. Copying
    /// the reference's 0.6 straight in is exactly that mistake, and it shipped
    /// on both humanoids until a playtest caught a struck trader ambling away.
    float panicSpeedScale = 1.0f;

    /// Whether a blow from this species swings its arms.
    ///
    /// **It is not simply "has arms", and that is the whole point of the
    /// field.** The skeleton family are archers - the reference gives them
    /// `ranged_attack` at priority 0 and only drops them to melee when they
    /// have no bow - so miming a sword swing tells the player exactly the wrong
    /// thing about what they are. They shoot instead, and keep their contact
    /// damage as well, which is the reference's own arrangement.
    ///
    /// Defaults false, which is safe: most of the roster has no arms at all, so
    /// the rows that want this are the handful that opt in rather than the
    /// majority that would have to remember to opt out.
    bool swingsArms = false;

    /// Fights at a distance instead of closing. **The other half of the same
    /// split `swingsArms` names**: the skeleton family were archers with
    /// nothing to shoot, so they were given contact damage as a stopgap and
    /// told not to mime a swing. This is the real thing, and it replaces that
    /// stopgap rather than adding to it - an archer does not also punch.
    bool shootsArrows = false;

    /// Fights at a distance by lobbing a bottle. The witch, and the third of
    /// the family `swingsArms` and `shootsArrows` began: same arbitration, same
    /// refusal to close, and a wholly different thing arriving at the far end.
    bool throwsPotions = false;

    /// Seconds between one ranged attack and the next.
    ///
    /// **`attack_interval` in `minecraft:behavior.ranged_attack`, and it is in
    /// seconds rather than ticks** - the whole of that component is, which is
    /// worth stating beside a field whose neighbours in this file are per-tick
    /// reference numbers. Three seconds for the skeleton, the stray and the
    /// witch on Normal difficulty; the bogged alone is slower, which pairs with
    /// its lower health and is the reference's own distinction.
    ///
    /// **It is a reload, not a wind-up.** The reference raises the bow the
    /// moment a target is acquired and holds it up, so nothing about this
    /// number is visible as a draw - it is the wait between shots taken behind
    /// an already-drawn bow.
    float rangedInterval = 3.0f;

    /// What it carries in its main hand, which is its **right** - the side the
    /// player's own skin net calls right, and the side every arm on this roster
    /// that swings a blow already uses. `None` for empty hands.
    ///
    /// A fact about the species rather than about the rig, for the same reason
    /// `swingsArms` is: the skeleton family and the Blackbone are one set of
    /// boxes and are not carrying the same thing. Anything a *behaviour* puts
    /// in a hand - the witch's bottle, which only exists while it is winding up
    /// - overrides this rather than being listed here.
    ///
    /// **It is drawn through `appendSpriteModel`**, the same owner a dropped
    /// item and a thrown one already go through, so a bow in a fist and a bow
    /// on the floor cannot disagree about what a bow looks like. Only items
    /// with a flat sprite are held: a block in a hand would need the miniature
    /// cube path instead, and nothing on the roster wants one.
    ItemId heldMainHand = ItemId::None;

    /// Top of a damage *range*, or zero to mean "exactly `attackDamage`".
    ///
    /// Defaulting to zero is what leaves all fifty-seven existing rows provably
    /// unchanged: only a row that states a maximum gets a roll at all.
    int attackDamageMax = 0;

    /// How hard a blow from this species throws its target, against the ordinary
    /// `kKnockbackSpeed`/`kKnockbackLift`.
    ///
    /// **Both numbers are ratios, not ported values.** Bedrock's iron golem
    /// carries `horizontal_power` 0.52 against the player's own 0.165, so what
    /// transfers is 3.15x — dropping 0.52 into our metres-per-second field would
    /// be the wrong-unit mistake this project has already paid for once.
    float knockbackScale = 1.0f;
    float knockbackLiftScale = 1.0f;

    /// Claims a bed and a job site, keeps a daily schedule, and gains a trade
    /// from whatever block it claimed. The villager alone.
    bool keepsHouse = false;
};

const CreatureSpecies& speciesInfo(CreatureKind kind);

static_assert(kSpawnEggItems == static_cast<int>(CreatureKind::Count),
              "the two spawn egg runs together must cover every species");

/// The egg that produces this species, and the species an egg produces. The two
/// runs are deliberately the same order, so this is arithmetic rather than a
/// thirty-six row table nobody would keep in step.
///
/// **That sentence used to be the only thing standing behind the pair**, and a
/// claim of correctness with nothing testing it is `CLAUDE.md` bug shape #11 -
/// the shape where eleven `static_assert`s passed while pointing at the wrong
/// texture. `spawnEggsRoundTrip` below is the test it was missing.
constexpr ItemId spawnEggFor(CreatureKind kind) {
    return spawnEggForIndex(static_cast<int>(kind));
}

/// Only meaningful when `isSpawnEgg` holds.
constexpr CreatureKind creatureForSpawnEgg(ItemId item) {
    return static_cast<CreatureKind>(spawnEggIndex(item));
}

/// **Every species' egg maps back to that species.** Walks all of
/// `CreatureKind` rather than sampling, because the failure this guards against
/// is a one-place slip that is invisible on either side of itself.
///
/// **Why the pair genuinely can break, which is what stops this being
/// ceremony.** The eggs are not one run: `kSpawnEggLayers` is 36 and
/// `kExtraSpawnEggLayers` is 22, and **thirteen ids sit between them** - eleven
/// resources from `Coal` to `Redstone`, then `Bucket` and `WaterBucket`. Both
/// directions therefore carry the same piecewise arithmetic written twice, in
/// opposite directions, in a file that cannot see this one. Add a species and
/// the second run grows; add a resource in the wrong place and the runs move
/// apart. Nothing in either file compares them.
///
/// **This asserts the expression the real readers evaluate**, not one side of
/// it against itself. `Item.hpp`'s creative-inventory build calls
/// `spawnEggForIndex`, and `Main.cpp`'s place path calls `creatureForSpawnEgg`;
/// composing them is exactly what the game does when you take an egg out of the
/// menu and put it on the ground. `isSpawnEgg` is in the loop for the same
/// reason - `creatureForSpawnEgg` is documented as meaningful only where that
/// holds, so a round trip through an id the game would not recognise as an egg
/// is not a round trip the player can make.
///
/// **A `constexpr` assert over two `constexpr` functions can never rot.** It is
/// the same argument `foodTablesAgree()` rests on, and it is why this is worth
/// more than the comment it replaces.
constexpr bool spawnEggsRoundTrip() {
    for (int i = 0; i < static_cast<int>(CreatureKind::Count); ++i) {
        const ItemId egg = spawnEggFor(static_cast<CreatureKind>(i));
        if (!isSpawnEgg(egg) || spawnEggIndex(egg) != i) {
            return false;
        }
    }
    return true;
}

/// **THE CONTROL, and it differs from the claim in exactly one variable**: the
/// forward run, and nothing else. Same loop, same `spawnEggIndex`, same
/// `isSpawnEgg`, same bound - only the two-run formula is replaced by the
/// single-run one somebody would write if they forgot the eggs are in two
/// pieces.
///
/// **It is not vacuous, and here is the arithmetic that says so.** It agrees
/// with the real formula for the first 36 species and disagrees for the
/// remaining 22, so it fires on 22 of 58 rather than on none or on all. The
/// first disagreement is at index 36, where the real run gives the drowned's
/// egg and this one gives `Coal`; by index 49 it has walked into the *second*
/// egg run and starts returning real eggs belonging to the wrong species, which
/// is the failure that would actually reach a player.
///
/// **A control that passed here would mean the second run is empty**, so the
/// bound is asserted separately below rather than left to be assumed.
constexpr ItemId spawnEggForIndexOneRun(int kindIndex) {
    return static_cast<ItemId>(static_cast<int>(ItemId::SpawnEggFirst) + kindIndex);
}

constexpr bool spawnEggsRoundTripOneRun() {
    for (int i = 0; i < static_cast<int>(CreatureKind::Count); ++i) {
        const ItemId egg = spawnEggForIndexOneRun(i);
        if (!isSpawnEgg(egg) || spawnEggIndex(egg) != i) {
            return false;
        }
    }
    return true;
}

static_assert(spawnEggsRoundTrip(),
              "every spawn egg must produce the species whose egg it is");
static_assert(!spawnEggsRoundTripOneRun(),
              "the control must FAIL - if a single-run formula round trips, this file is "
              "proving nothing about the two-run one the game actually uses");
static_assert(kExtraSpawnEggLayers > 0,
              "the control above is only non-vacuous while there is a second egg run");

/// The seam itself, named rather than merely covered by the loop. `Princepin`
/// is index 35 and the last egg of the first run; `Drowned` is 36 and the first
/// of the second. **A whole-range loop passes just as happily when the boundary
/// is off by one in both directions at once**, so the two ids either side of it
/// are worth stating outright - and stating them is what makes a later reader
/// who moves a species see which two rows they have to re-check.
static_assert(creatureForSpawnEgg(spawnEggFor(CreatureKind::Princepin)) ==
                      CreatureKind::Princepin &&
                  creatureForSpawnEgg(spawnEggFor(CreatureKind::Drowned)) ==
                      CreatureKind::Drowned,
              "the last egg of the first run and the first of the second both round trip");

/// "Sheep Spawn Egg". Lives here because this is the one place that knows what
/// a species is called, and returns a stable pointer so the tooltip can hold it.
const char* spawnEggName(ItemId item);

/// What to call an item on screen. **Use this rather than `itemDisplayName`** -
/// that one lives in `Item.hpp`, which cannot see the creature roster, so it
/// answers plain "Spawn Egg" for all fifty-six of them.
inline const char* displayNameOf(ItemId item) {
    return isSpawnEgg(item) ? spawnEggName(item) : itemDisplayName(item);
}

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
///
/// **DO NOT ADD `bool fromTnt` HERE. It was asked for, it is no longer needed,
/// and adding it now would be a second answer to a question that already has
/// one.** The history is worth two lines because the request is written down in
/// another file and a reader will meet it: a charge drops everything it breaks
/// and a creeper drops one block in `power`, so the roll site has to tell them
/// apart, and for a while the only way to say so was a field on this struct.
/// `Explosion.hpp`'s `explosionDropChance` still describes that as the missing
/// piece. **It is stale.** `Main.cpp` solved it without touching this type, and
/// solved it better: that file owns the vector, appends every creeper first and
/// every charge afterwards, records the boundary in `creeperBlasts`, and reads
/// `blastIndex >= creeperBlasts` at the one site that needs the answer.
///
/// So a field here would not be wired to anything. It would sit at its default,
/// look authoritative, and the next reader to set it at a `push_back` would
/// believe the roll had read it - while the roll went on reading the index.
/// That is `CLAUDE.md` bug shape #1 exactly, a value derived somewhere other
/// than the one place that owns it, and it is worse than the gap it closes.
///
/// **What would make this note wrong**, so it can be checked rather than
/// trusted: `creeperBlasts` disappearing from `Main.cpp`, or a second producer
/// appending to that vector after it is taken. Verified by enumeration on
/// 2026-08-19 - the vector has exactly one `push_back` in the file.
struct CreatureExplosion {
    glm::vec3 centre{0.0f};
    float power = 0.0f;
};

/// A blow one creature landed on another this tick.
///
/// Collected and applied after the update loop has finished walking the
/// population, for the same reason every other hand-off here exists: the loop
/// holds a reference into the vector it would otherwise be writing to, and a
/// behaviour that mutates its neighbour mid-walk leaves half the population
/// reading this tick and half reading the last one.
struct CreatureHit {
    std::uint32_t targetId = 0;
    std::uint32_t fromId = 0;
    int damage = 0;
    glm::vec3 push{0.0f};
};

/// A noise a creature made this tick, for whoever owns the speakers.
///
/// Handed over rather than played, for the same reason loot is handed over
/// rather than spawned: `Creatures` has no idea audio exists, and the loop that
/// owns it does.
enum class CreatureSound : std::uint8_t {
    Idle,
    Hurt,
    Death,
};

struct CreatureVoiceEvent {
    CreatureKind kind = CreatureKind::Sheep;
    CreatureSound sound = CreatureSound::Idle;
    glm::vec3 at{0.0f};
    /// Carried so a baby can be pitched up from the adult's own recording.
    float scale = 1.0f;
};

/// What took the last point of health.
///
/// **`Unknown` existing at all is the whole point of this enum.** `threatId`
/// below cannot answer "did a player do this?", because zero there means both
/// *the player* and *nobody ever hurt it* - so a sheep that drowned, a zombie
/// that burned off at dawn and a rabbit the player shot all end up looking
/// identical. Reading `threatId == 0` as a player kill would hand every
/// environmental death a rare-drop roll. Here the two are separate values, and
/// nothing that did not actually strike a blow can ever read as `Player`.
enum class DeathCause : std::uint8_t {
    /// Nothing has hurt it, or nothing has hurt it since it was created.
    Unknown,
    /// A player's own swing or a player's arrow. **Not** an id of zero: this is
    /// written positively, by the site that landed the blow.
    Player,
    /// Another creature - `DeathContext::killerId` names which.
    Creature,
    Burning,
    Drowning,
    DryingOut,
    /// Buried in solid terrain and unable to climb out. Distinct from drowning
    /// even though both are "cannot breathe", because the drops differ by cause
    /// and a body dug out of a hillside is not a body pulled from a lake.
    Suffocation,
    Lightning,
    Explosion,
    /// A landing hard enough to hurt. Recorded by `step`, which is the only
    /// site that knows how far the body actually fell.
    Falling,
    /// Crushed where it stood by a block that fell **on** it - an anvil, today.
    ///
    /// **Deliberately not `Falling`.** That one means the body's own landing and
    /// its comment names `step` as the only site that writes it; a second writer
    /// would have made that true sentence false, which is this project's most
    /// expensive kind of defect because every review passes it. The two are also
    /// genuinely different events - one is a mob hitting the ground, the other
    /// is the ground hitting a mob - and nothing that reads a cause should have
    /// to guess which happened.
    ///
    /// No loot flag tests this, so an anvil kill pays ordinary drops and no
    /// player rares, which is correct: `LootFlag::PlayerKill` fires only on
    /// `DeathCause::Player`.
    ///
    /// **Staged, and unreferenced apart from this line as of 2026-08-19** - a
    /// bare-name search finds exactly one hit, which is this definition. That is
    /// deliberate and is not the "one call site short of existing" shape three
    /// separate findings hit tonight: `hurtInBox` takes the cause as an
    /// *argument*, so the only place this name can appear is the caller, and
    /// the caller is `Main.cpp`'s falling-block drain, which belongs to another
    /// owner. **What would make this note false:** that one line landing. Until
    /// it does, an anvil hurts the player and nothing else - so if you are
    /// reading this because a pig survived an anvil, the gap is there and not
    /// here.
    CrushedByBlock,
};

/// The facts about a death that are gone by the time it pays out.
///
/// `cause` and `killerId` are overwritten by **every** blow, so the last one
/// wins - which is the reference's rule: what a mob drops is decided by the
/// killing blow and not by whatever hurt it first. `burning` and `place` are
/// latched once, on the tick health crosses zero, and read a full second later
/// when the body is retired (`kDeathSeconds`).
///
/// Latched rather than asked for, because by then there is nothing left to ask.
/// A corpse skips `think` entirely, so no clock on it still runs; the sun-burn
/// branch that set it alight is behind a `health > 0` test and would have put
/// it out; and the blow that killed it was resolved a second ago in a function
/// that has already returned.
struct DeathContext {
    DeathCause cause = DeathCause::Unknown;
    /// Which creature struck the last blow, when `cause` is `Creature`. Zero
    /// otherwise - and unlike `threatId`, a zero here is **never** the player,
    /// because `cause` says that instead.
    std::uint32_t killerId = 0;
    /// Where it died, in sixteenths of a block, x and z. **The drop hash reads
    /// this and never `position`**, and that is the whole reason a drop is a
    /// pure function of the death rather than of how long the body lay there.
    ///
    /// `separate` shoves corpses exactly as it shoves the living - a body in a
    /// herd drifts for the full `kDeathSeconds` before it is retired - and that
    /// shove is per-frame explicit Euler, so hashing the position at retirement
    /// put the frame-rate dependence straight back into the one case that
    /// matters most: killing one animal inside a herd.
    glm::ivec2 place{0};
    /// Was it on fire as it died? Bedrock's whole `burn` condition is this one
    /// bit - "only when on fire" - and it is what turns raw meat into cooked.
    /// Java additionally counts a Fire Aspect weapon; we have no enchanting, so
    /// the simpler rule is also the complete one.
    bool burning = false;

    /// Set on the tick health crosses zero, after which `cause` and `killerId`
    /// stop moving.
    ///
    /// **The last-blow-wins rule has an end, and it did not have one.** A body
    /// lies there for `kDeathSeconds` before it pays out, and `step` keeps
    /// running on it - so anything in `step` that records a cause was still
    /// rewriting the answer a second after the fight was over. Drowning was the
    /// live case: a cow killed at the water's edge toppled in, went on taking a
    /// point a second, and paid out as the sea's kill with `killerId` cleared,
    /// which costs the player every rare drop. The hazards refuse a corpse
    /// outright now; this makes the *record* immutable as well, so the next one
    /// added cannot reopen it.
    bool latched = false;
};

/// A living thing in the world.
///
/// The **general entity** dropped items deliberately were not: it has size, so
/// it collides on every axis rather than only vertically, it faces a direction,
/// and it decides where to go.
struct Creature {
    CreatureKind kind = CreatureKind::Sheep;

    /// Stable for this creature's whole life, and **never an index**: the
    /// population retires by swapping the last element down over the hole, so
    /// an index names a different animal the moment anything dies. Zero is
    /// "nobody", which is why the counter starts at one.
    std::uint32_t id = 0;

    /// What it is trying to attack right now. Recomputed from scratch every
    /// tick, so it can never go stale: a producer that stops running stops
    /// asserting the target, and the consumers simply find nothing there.
    CreatureTarget target = CreatureTarget::None;
    /// Which creature, when `target` names one. Unlike `target` this **does**
    /// persist between ticks, because it is the memory that lets a chase
    /// survive a moment out of sight - the same job `forgetTimer` does for the
    /// player.
    std::uint32_t targetId = 0;

    /// Who last hurt it. Zero means the player, which is the common case and
    /// the one the whole grudge system was built around. Only meaningful while
    /// `provokedTimer` is running.
    ///
    /// **Not what the drop table reads** - see `death` below and the note on
    /// `DeathCause::Unknown` for why zero cannot answer that question.
    std::uint32_t threatId = 0;

    /// What last hurt it, and how. Written by every one of the **eight** sites
    /// that reduces `health`, so the killing blow is still known a second later
    /// when the body is retired and pays out.
    ///
    /// The eighth was missed and cost a music disc: a Bramble that detonates
    /// sets its own health to zero, and recorded nothing - so a Bramble a
    /// skeleton had merely wounded, minutes earlier, still read as a skeleton
    /// kill when it blew itself up beside the player. **Any site that lowers
    /// `health` records, including the ones that do it to themselves.**
    DeathContext death{};

    /// Counts down the 1.8 s of a graze. The reference's `time_until_eat`, and
    /// the block is taken when it reaches zero rather than when it starts, so
    /// interrupting a sheep costs it the mouthful.
    float eatTimer = 0.0f;

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
    /// phase is seeded at random **when the creature enters the population**, so
    /// a group does not pulse in unison, which is the one thing that would make
    /// eight squid read as one machine.
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
    /// clock so a crowd does not sway in unison - which needs it **seeded**, not
    /// merely stored: `add` starts every creature at a random point in the sway
    /// cycle, because starting them all at zero is a world clock with extra
    /// steps. Nothing reads it as a duration, so it is not worth saving.
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

    /// The damage window, and the pair that make it work - **the same rule the
    /// player pays, and now literally the same code**: both go through
    /// `survival::chargeDamageWindow` and `survival::tickDamageWindow`, which
    /// own the rule so that neither entity has to restate it. Inside the window
    /// a blow no larger than `lastDamage` is ignored outright and a larger one
    /// lands only the difference; the timer is not restarted by either, or a
    /// stream of escalating blows holds the creature immune forever.
    ///
    /// Deliberately **not** `hurtTimer`, which is 0.35 s and cosmetic. The two
    /// were one number here for twenty milestones, which is why a pack could
    /// delete an animal in a single tick while the player standing beside it
    /// took two hits a second from the same pack.
    float invulnerableSeconds = 0.0f;
    /// The blow the running window is measured against. Cleared with the window
    /// by `survival::tickDamageWindow`, so it can never suppress a hit after it
    /// has expired.
    int lastDamage = 0;
    /// How long it has been dead, in seconds. `health <= 0` is what starts it
    /// running; the body tips over onto its side while it counts, and is
    /// retired by `manage` once it is done. Alive creatures leave it at zero.
    float deathTimer = 0.0f;
    /// Stops a hostile landing a blow every frame it is touching you.
    float attackTimer = 0.0f;
    /// Time left in the arm swing of a blow that has just landed. Far shorter
    /// than `attackTimer`, because the arm is moving for well under a third of
    /// the cycle and rests visibly in between.
    float swingTimer = 0.0f;

    /// How long this one has been drawing a bow or raising a bottle, in
    /// seconds. **Counts up, unlike every other timer here**, because what it
    /// measures is a wind-up and not a wait: the shot leaves the moment it is
    /// full, and the cadence is that duration rather than a separate cooldown.
    float drawTimer = 0.0f;
    /// Whether a ranged behaviour ran at all this tick. Re-derived from nothing
    /// every tick, exactly like `running`, so it cannot go stale and leave a
    /// reload ticking on a creature that has forgotten you.
    bool aiming = false;
    /// Where the weapon **should** be this tick, 0 down and 1 up. Also
    /// re-derived from nothing every tick.
    ///
    /// **Separate from `aiming`, because for a witch the two genuinely
    /// differ.** An archer's bow is up the whole time it has a target - the
    /// reference's controller transitions on `query.has_target` and on nothing
    /// else - so its two values agree. A witch spends most of the same three
    /// seconds reloading with her arms folded and reaches for the bottle only
    /// at the end, so hers do not.
    float aimWanted = 0.0f;
    /// How far the arms are into the aiming pose, 0 to 1. Eased, so a bow comes
    /// up and goes back down rather than appearing at the shoulder - and held
    /// **across** the release, because the reference keeps the bow up and
    /// starts the next draw rather than lowering it between shots.
    float aim = 0.0f;
    /// Seconds left in a **committed** shot. Nothing cancels one but death,
    /// which cancels it by a corpse never reaching the behaviour at all.
    float releaseTimer = 0.0f;
    /// What a *behaviour* has put in this one's hand, overriding the species'
    /// own `heldMainHand`. `None` means "whatever it always carries".
    ///
    /// **Not re-derived every tick, unlike `aiming` beside it**, and that is
    /// deliberate: a witch chooses its brew once at the start of the wind-up
    /// and has to keep holding that one, or the bottle changes colour while it
    /// winds up and lies about what is coming.
    ItemId heldOverride = ItemId::None;

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

    /// Seconds of sunlight accumulated toward the next point of fire damage.
    /// Reset by shade or water rather than paused, so reaching a doorway is a
    /// real escape.
    float burnTimer = 0.0f;

    /// Seconds accumulated toward the next point of suffocation damage, for a
    /// body that is inside solid terrain **and could not climb out of it**.
    ///
    /// Reset the moment it is free rather than paused, exactly as `burnTimer`
    /// is: being briefly clipped by a closing door should cost nothing, and
    /// only a burial that persists across the interval should ever land a hit.
    float suffocateTimer = 0.0f;

    /// Struck by lightning, in the reference. **Doubles the blast power and
    /// nothing else** - a charged creeper has the same twenty health as any
    /// other, which is worth stating because the obvious guess is that it is
    /// tougher. It is not; it is louder.
    ///
    /// We have no weather until M27, so for now a small share of Brambles
    /// arrive this way instead of being made by a storm.
    bool charged = false;

    /// Built by a player out of iron and a carved pumpkin, rather than found in
    /// a village.
    ///
    /// **Per individual, not per species**, exactly like `charged`, and it is
    /// the whole of the golem's temperament: a player-built one excludes the
    /// player from `hurt_by_target` outright, so it is passive to you for ever
    /// and there is no anger timer to expire. Bedrock has no `minecraft:angry`
    /// on the golem at all.
    ///
    /// **This one has to be saved.** A golem that forgot who made it and
    /// started hitting you after a reload is not a cosmetic bug.
    bool playerBuilt = false;

    /// What trade this villager took, or 0 for none. 1..13 index the job-site
    /// table; 14 is the nitwit, which can never take one.
    ///
    /// **A profession is a claimed block, not a state that was assigned.** A
    /// village generates nobody with a trade — every armourer in the world
    /// claimed a blast furnace on its first morning, which is the reference's
    /// own behaviour and much the simplest thing to implement.
    std::uint8_t profession = 0;

    /// The bed and the job site this villager claimed, and the bell it gathers
    /// at. `y < 0` means none.
    ///
    /// **The claim lives here and nowhere else.** "Is this bed taken" is a scan
    /// of the population rather than a map on `Creatures`, for the same reason
    /// a double chest re-derives its pairing: a second copy cannot be kept in
    /// step, needs saving, and has to be repaired when the block is broken.
    glm::ivec3 bedCell{0, -1, 0};
    glm::ivec3 jobCell{0, -1, 0};
    glm::ivec3 meetCell{0, -1, 0};

    /// Bedrock's `dweller.update_interval` — the point-of-interest scan runs
    /// every 3 to 5 seconds rather than every tick.
    float poiTimer = 0.0f;
    /// `behavior.work`: 12.5 s standing at the job block, then 10 s of cooldown.
    float workTimer = 0.0f;
    /// The scheduler's own 0-10 s stagger, so a village does not change shift in
    /// lockstep.
    float phaseDelay = 0.0f;

    /// A bee's home hive or nest, on the same `y < 0 means none` convention as
    /// the three village cells above - which is why it is spelled the same way
    /// rather than carrying a `bool` beside it.
    ///
    /// **Deliberately not saved.** `SavedCreature` is a fixed, trivially
    /// copyable record and widening it costs a `kCreatureVersion` bump that
    /// throws away every existing population; a reloaded bee simply searches
    /// again and re-latches within a few seconds, and the hive's honey level is
    /// in the chunk, which *is* saved. Nothing about the honey chain depends on
    /// this surviving a restart.
    glm::ivec3 hiveCell{0, -1, 0};
    /// The flower it is working, on the same convention. Remembered rather than
    /// re-scanned, so the box search runs on a cadence instead of every frame,
    /// and cleared the moment the flower is picked out from under it.
    glm::ivec3 flowerCell{0, -1, 0};
    /// Whether it is carrying nectar. Bedrock's `minecraft:has_nectar` boolean
    /// property, and it is the switch the whole cycle turns on: without it the
    /// bee looks for flowers, with it the bee looks for home.
    bool hasNectar = false;
    /// Counts down `look_for_food`'s `stay_duration` while the bee is settled on
    /// a flower. Like `eatTimer` the payoff lands when it **reaches zero**, so a
    /// bee knocked off its flower half way through gets no nectar.
    float pollinateTimer = 0.0f;
    /// Rate-limits the hive search, which is the one expensive scan a bee does.
    /// Only ever runs while `hiveCell.y < 0`, so a bee that has a home never
    /// pays for it at all.
    float hiveSearchTimer = 0.0f;
    /// And the flower search, on the wander cadence rather than the hive's - a
    /// far smaller box, and a bee that has lost its flower should find the next
    /// one in seconds rather than in tens of them.
    float flowerSearchTimer = 0.0f;

    /// Which behaviours were running last tick, one bit per row of the table
    /// in `Creature.cpp`. Opaque outside the selector, and it exists for one
    /// reason: a behaviour that is already running is asked whether it may
    /// *continue*, which is deliberately a looser question than whether it may
    /// *start*. A hunter gives up further out than it engages.
    ///
    /// **Thirty-two bits, not sixteen.** The villager's schedule took the table
    /// past sixteen rows; overflowing this silently would make behaviours
    /// forget they were running, and the symptom would read as a pathing bug.
    std::uint32_t runningBehaviours = 0;
};

/// Every creature currently loaded, plus the rules that spawn and retire them.
class Creatures {
public:
    explicit Creatures(std::uint32_t seed);

    /// Moves, decides and animates. Creatures are few and touch the world only
    /// to read it, so this stays on the main thread with the drops. Returns any
    /// blow landed on the player for the caller to apply, and appends any blast
    /// that went off to `blasts` for the same reason.
    ///
    /// **The `const World&` is not a wall, and two features have now been filed
    /// as blocked by it when neither was.** Work crossing this seam runs in one
    /// of two directions and they want different shapes:
    ///
    /// **Inbound - the world acts on a creature.** A falling anvil landing on a
    /// pig, a piston shoving one. This needs no `World` at all: it reads the
    /// roster and writes creature health, both of which live here. It is a plain
    /// mutating method, and `hurtInBox` below is the first one. Nothing about
    /// the const reference ever stood in its way; the anvil was filed as blocked
    /// by it because "damage something in the world" *sounds* like world work.
    ///
    /// **Outbound - a creature acts on the world.** A bee pollinating a crop,
    /// which is the one still open. This genuinely cannot take a mutable
    /// `World&`, and not for want of an entry point: the architecture rule is
    /// that exactly one owner mutates the world, on the main thread, and handing
    /// the roster a mutable reference is how that rule dies.
    ///
    /// **The shape it wants already exists here, twice.** This function returns
    /// `CreatureAttack` for the caller to apply and appends to `blasts` for the
    /// caller to drain - compute the result, hand it over, let the owner apply
    /// it. A bee's crop edit is a third of exactly that rather than a new seam;
    /// and it is the same pattern `FallingBlocks::update(world, dt, &landings)`
    /// uses to deliver the very landings `hurtInBox` was written to consume.
    ///
    /// So: **one entry point cannot serve both, and neither of them needs a
    /// mutable `World&`.** Written here because the question has now been asked
    /// twice and the second asker had no way to see the first answer.
    CreatureAttack update(const World& world, const glm::vec3& playerFeet, float deltaSeconds, bool night,
                          bool playerSneaking, float timeOfDay,
                          std::vector<CreatureExplosion>& blasts);

    /// Adds and retires creatures around the player. Kept apart from `update`
    /// because it runs on its own slower clock - trying every frame would spend
    /// most of its time failing to find a spot.
    void manage(const World& world, const glm::vec3& playerFeet, float deltaSeconds, bool night);

    /// What a killed creature left behind, waiting to be turned into drops.
    ///
    /// Handed over rather than spawned here, for the same reason `World` hands
    /// over the blocks a flow swept aside: `Creatures` has no idea dropped
    /// items exist, and the loop that owns them does.
    struct Loot {
        glm::vec3 position;
        ItemId item;
        int count;
    };
    std::vector<Loot> takeLoot();

    /// An arrow an archer has just loosed, waiting to be turned into a
    /// projectile.
    ///
    /// Handed over rather than fired here, for the same reason `takeLoot` hands
    /// drops over and `World` hands over the blocks a flow swept aside:
    /// `Creatures` has no idea projectiles exist, and the loop that owns them
    /// does.

    /// What a creature let go of, **in the creature's own vocabulary**.
    ///
    /// Deliberately not a `ProjectileKind`: this header does not know
    /// projectiles exist and the paragraph above is the reason. The loop that
    /// owns them maps this onto whatever it actually fires, which is one
    /// `switch` there against a dependency here that would never come back out.
    enum class LaunchKind : std::uint8_t {
        Arrow,
        SplashPotion,
    };

    struct Launch {
        glm::vec3 origin;
        /// Blocks per tick, already carrying the thrower's spread.
        glm::vec3 velocity;
        LaunchKind kind = LaunchKind::Arrow;
        /// Which brew, for a bottle. `None` for anything else, and an `ItemId`
        /// rather than a raw number because `Loot` above already hands one over
        /// and a second way of naming an item is a second way of getting it
        /// wrong. **The default is what keeps every existing archer shot
        /// compiling and meaning exactly what it did.**
        ItemId payload = ItemId::None;
        /// Who fired it. **Must be carried through to whatever resolves the
        /// hit**, or an arrow that clips a bystander blames the player for it -
        /// which turned a wolf, and its whole pack, on the one person who did
        /// not shoot it. Zero names the player, as everywhere else here.
        std::uint32_t fromId = 0;
    };
    std::vector<Launch> takeLaunches();

    /// A block a grazing animal has just eaten, waiting for the owner of the
    /// world to actually remove it.
    ///
    /// Handed over rather than done here for the hardest of the reasons in this
    /// file: **only the main thread may mutate the world**, and `Creatures`
    /// reads it and never writes it. Same arrangement as `takeLoot`,
    /// `takeLaunches` and the blast list.
    std::vector<glm::ivec3> takeGrazed();

    /// A hive a bee has just flown home to with nectar, waiting for the owner
    /// of the world to raise its honey level.
    ///
    /// **Same arrangement and the same reason as `takeGrazed` above**: this
    /// file is handed a `const World&` everywhere and never writes to it, so
    /// the only way a creature changes a block is to name the cell and let the
    /// main thread apply it. A drain that is never written is a complete
    /// feature one call site short of being reachable, which is why the
    /// requirement is spelled out here rather than left to be inferred:
    ///
    /// > For each cell, read the block. If `isBeehive` and
    /// > `beehiveHoneyLevel` is below `kBeehiveFullHoney`, write
    /// > `beeHomeAtLevel(id, level + 1)`. A cell that no longer holds a hive is
    /// > simply dropped.
    ///
    /// **THAT CALL SITE NOW EXISTS: `Main.cpp:10247`, `for (const glm::ivec3&
    /// cell : creatures.takePollinated())`, landed 2026-08-19.** Verified by
    /// reading the call site rather than by trusting this note, and recorded
    /// here because a spec that does not say it has been met is how a second
    /// writer gets built: the reader arrives, finds a requirement written in
    /// the imperative and no statement that anyone honoured it, and honours it
    /// again. **`WorldStore.hpp` carried "Not yet written by anyone" about two
    /// call sites that had landed hours earlier, and acting on it would have
    /// produced two writers for one field.** So if you are here to wire this
    /// up: it is wired. Check `Main.cpp` before adding anything.
    ///
    /// **`beeHomeAtLevel` and not `beehiveAtLevel(beehiveFacing(id), ...)`.**
    /// The second reads correctly and is wrong: a bare facing carries no record
    /// of which half of the family the block came from, so it quietly rebuilds
    /// every natural nest as a crafted hive. `Block.hpp` keeps that exact call
    /// as a named control and a `static_assert` rejects it.
    ///
    /// **A cell may appear twice in one list and both entries must be applied.**
    /// That is not a duplicate to be filtered: Bedrock's rule is "when the bee
    /// exits, the hive increments its honey level by 1 and has a 1% chance to
    /// increment it by 2" (minecraft.wiki/w/Beehive/Usage), and the 1% is rolled
    /// here, where the random stream already lives, by pushing the cell a second
    /// time. De-duplicating this list silently deletes that rule and also merges
    /// two different bees arriving in the same tick.
    std::vector<glm::ivec3> takePollinated();

    /// Every noise the population made this tick.
    std::vector<CreatureVoiceEvent> takeVoices();

    /// How far creatures live, appear and are retired, in blocks.
    /// **Follows the render distance rather than a number of its own.** A fixed
    /// 90 m meant an animal blinked out well inside the world you could see,
    /// which is the one place the illusion is easiest to break. Safe to change
    /// mid-session: the next `manage` places or retires the difference, and the
    /// population cap scales with the *area* so a wider world is not a thinner
    /// one.
    void setActiveRadius(float blocks);

    /// Strikes the first creature the aim ray reaches. Returns true if one was
    /// hit, so the caller can spend a swing on it instead of the block behind.
    ///
    /// `fromId` is who struck, and it is what the victim and its neighbours go
    /// after. **It defaults to the player**, because zero names the player and
    /// the melee caller has nothing else to say - but a projectile must name
    /// its shooter, or a skeleton's stray arrow makes an enemy of you on its
    /// behalf. `ignoreId` is who cannot be hit, for the launch window a shot
    /// spends inside its own thrower; zero skips nobody, since ids start at one.
    ///
    /// `hitDistance` and `hitIndex`, when given, name **where** and **who** -
    /// how far along the ray the body was entered, and its index in `all()`.
    /// Both are written whenever a creature was found, including the case where
    /// the immunity window absorbs the blow and no damage lands, because the
    /// caller asked what the ray hit and that is true either way.
    ///
    /// They exist so a projectile does not have to run `findAimed` itself to
    /// recover them. Doing that would walk the same ray twice and, worse, would
    /// decide *who was hit* in a second place - and a value derived somewhere
    /// other than the one function that owns it is this project's single most
    /// expensive bug shape. One ray, one answer, handed out.
    bool strike(const glm::vec3& eye, const glm::vec3& forward, float reach, int damage,
                std::uint32_t fromId = 0, std::uint32_t ignoreId = 0,
                float* hitDistance = nullptr, std::size_t* hitIndex = nullptr);

    /// Damages and throws everything caught in a blast. Returns how many were
    /// hit, which is the number that makes "did the explosion reach anything?"
    /// answerable from the log rather than by standing next to one.
    ///
    /// Lives here rather than in the caller because `Creatures` owns the
    /// population; the caller owns the world and rewrites the blocks. Anything
    /// reduced to zero health is retired by the next `manage`, so a slime
    /// caught in a blast still splits.
    int applyExplosion(const World& world, const glm::vec3& centre, float power);

    /// Damages everything a lightning bolt lands on, and **charges a Bramble**.
    /// Returns how many were hit.
    ///
    /// The reference's box rather than a radius, and the same one the player is
    /// judged against, so a bolt cannot hit you and spare the animal beside you.
    /// A charged Bramble is *supposed* to come from a storm; until there was
    /// weather, a share of them spawned that way instead.
    int applyLightning(const glm::vec3& at);

    /// **Damages every creature standing in a box.** Returns how many were hit.
    ///
    /// The entry point the world needs when something lands *on* a mob rather
    /// than a mob walking into something. Written for the falling anvil, whose
    /// damage path reached the player and stopped there - `Main.cpp` said so at
    /// its own call site rather than improvising a loop over the roster, which
    /// was the right call and is why this is here instead of there.
    ///
    /// **Takes no `World`, and that is the point of the shape.** `Creatures` is
    /// handed a `const World&` everywhere else by design, and the reason a
    /// falling anvil looked blocked by that is that it was assumed to need one.
    /// It does not: damaging a creature touches the roster and nothing else.
    /// The const-`World` seam is a real constraint for *outbound* work - a bee
    /// that wants to change a crop - and no constraint at all for inbound work
    /// like this one. See the note above `update` for why those two want
    /// different shapes rather than one shared one.
    ///
    /// **The box is the caller's, and the damage is the caller's**, because
    /// this must not learn the anvil's ladder. `anvilLandingDamage` already owns
    /// it, is `constexpr`, and answers 0 for every other block - so the caller
    /// passes what it computed and gravel stays harmless without this function
    /// knowing gravel exists.
    ///
    /// **Judge creatures against the same box the player is judged against.**
    /// `applyLightning` above carries the same warning for the same reason: a
    /// blow that hits you and spares the pig beside you is worse than one that
    /// misses both. Today `Main.cpp` tests the player against the unit cube at
    /// the landing cell, so pass exactly that.
    ///
    /// **Through the damage window, like every other blow.** Lightning and
    /// blasts go through `damageCreature` and so does this, which is what makes
    /// a collapsing stack of ten anvils land as one hit - precisely what the
    /// player's own path says it relies on. Routing it around the window would
    /// have charged a mob ten times for what charges the player once.
    int hurtInBox(const glm::vec3& low, const glm::vec3& high, int damage, DeathCause cause);

    /// **Angers every creature of one kind near a point, without touching a
    /// hair on any of them.** Returns how many were roused.
    ///
    /// The verb the world needs when something provokes a group rather than a
    /// blow doing it: shearing a hive angers its bees, and the only public route
    /// before this was `strike`, which would have *hurt* them. Rousing and
    /// hurting are different verbs and this is the one that was missing.
    ///
    /// **Takes a `CreatureKind` rather than provoking everything in range, and
    /// that is a decision rather than an oversight.** "Anger the bees at this
    /// hive" and "anger everything standing near this hive" are different
    /// features; the caller wants the first, and letting it filter the second
    /// afterwards would put the knowledge of *which* creatures a hive belongs to
    /// in the wrong file. There is no all-kinds mode because nothing asks for
    /// one, and an unused mode is a thing that is built, clean and never called.
    ///
    /// A true sphere, unlike `alertNeighbours`' cylinder. The caller supplies
    /// one radius and one radius means a sphere; that function's `kAlertHeight`
    /// exists because its horizontal reach comes from the species row and is
    /// about how far a *call* carries, which is a different question.
    ///
    /// `threatId` is who gets blamed, defaulting to the player exactly as
    /// `strike` does. Corpses are skipped - a dead bee is not angry.
    int provokeNear(const glm::vec3& centre, float radius, CreatureKind kind,
                    std::uint32_t threatId = 0);

    /// Whether a creature stands in the way of the aim ray. Asked every frame,
    /// where `strike` only lands once a swing is ready - the block behind a
    /// creature must stay protected during the swing's cooldown too.
    bool aimedAt(const glm::vec3& eye, const glm::vec3& forward, float reach) const;

    /// No creature, for `findMilkable`. A sentinel rather than an optional
    /// because every other index here is a plain `size_t` into the same list.
    static constexpr std::size_t kNoCreature = static_cast<std::size_t>(-1);

    /// The cow this ray hits, or `kNoCreature`. Milk comes from an animal
    /// rather than a block, so the bucket has to ask the roster before it asks
    /// the world.
    std::size_t findMilkable(const glm::vec3& eye, const glm::vec3& forward, float reach) const;

    /// Built fresh each frame, for the same reason dropped items are: world
    /// meshes are drawn with an identity model matrix, so vertex positions have
    /// to *be* world positions.
    ///
    /// Anything see-through goes into `translucent` instead of the return value,
    /// because blending depends on draw order and the renderer draws every
    /// opaque mesh before any translucent one. Only a slime's outer shell uses
    /// it so far.
    ///
    /// `range` is a **drawing** limit and must never be confused with
    /// `setActiveRadius`, which is the simulation one. A creature outside this
    /// still walks, still paths, still hunts, still despawns on its own terms
    /// and is still saved - it is only not built into triangles this frame.
    ///
    /// `sprites` is the silhouette of every item picture, needed for the same
    /// reason a dropped item needs it: anything held in a hand is that picture
    /// extruded and walled in, not a decal.
    ///
    /// **Last, and defaulted to null on purpose.** A required parameter breaks
    /// every existing caller on the frame it lands, and null simply means "no
    /// held items this frame" - nothing else in here depends on it, so a caller
    /// without a mask still draws every creature correctly.
    engine::MeshData buildMesh(const World& world, engine::MeshData& translucent,
                               const DrawRange& range = {},
                               const SpriteMask* sprites = nullptr) const;

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
               float puff = 0.0f, bool playerBuilt = false);

    /// Every creature currently loaded, for the caller to write to disk. The
    /// store owns what a save record contains; this only hands over the live
    /// list, so the two can change independently.
    const std::vector<Creature>& all() const { return m_creatures; }
    /// Puts one back exactly as it was saved, bypassing every spawn rule. The
    /// population cap still applies from the next `manage` onward.
    ///
    /// The three cells are a villager's claimed bed, workstation and meeting
    /// point. **They have to come back with it**: `workStart` needs
    /// `jobCell.y >= 0` and re-claiming is closed to anyone who already has a
    /// profession, so a reloaded armourer stood at no anvil for the rest of the
    /// world's life - and, because the claim scan saw the station as free, the
    /// next villager to wake up became a second one.
    void restore(CreatureKind kind, const glm::vec3& feet, float yaw, int health, float scale,
                 bool charged = false, bool playerBuilt = false, std::uint8_t profession = 0,
                 const glm::ivec3& bedCell = glm::ivec3{0, -1, 0},
                 const glm::ivec3& jobCell = glm::ivec3{0, -1, 0},
                 const glm::ivec3& meetCell = glm::ivec3{0, -1, 0});

    /// Every chunk column whose one-off animal pass has already run, as
    /// **unpacked** `(chunkX, chunkZ)` pairs, for the caller to write to disk.
    ///
    /// The pair is handed over rather than the packed key deliberately: the
    /// packing is `populatedKey`'s alone, and a store that wrote the key would
    /// become a second owner of it - so a later change to the packing would
    /// silently stop matching everything already on disk, which is precisely the
    /// failure `populatedKey` exists as one function to prevent.
    ///
    /// **This set is not derivable from the creatures.** A column stays
    /// populated after everything in it has been eaten, and a village spans
    /// many columns, so it belongs beside the creature list rather than on any
    /// record within it.
    std::vector<glm::ivec2> populatedColumns() const;

    /// Puts one such column back. The counterpart of `populatedColumns`, taking
    /// the same unpacked form, so the store never sees a key.
    ///
    /// The `glm::ivec2` overload is the one to reach for when feeding back
    /// exactly what `populatedColumns` returned - the round trip is then a loop
    /// with no unpacking at either end.
    void restorePopulatedColumn(int chunkX, int chunkZ);
    void restorePopulatedColumn(const glm::ivec2& column);

    /// Places the villagers a freshly generated village comes with.
    ///
    /// **None of them has a trade**, which is the reference's own arrangement:
    /// five per cent are babies, and of the adults one in ten is a nitwit and
    /// the rest are unemployed. Everything else is claimed on the first morning.
    void placeVillager(const glm::vec3& feet, float yaw, bool baby, bool nitwit);

private:
    float random01();
    void think(const World& world, Creature& creature, const glm::vec3& playerFeet, float deltaSeconds,
               bool night, bool playerSneaking, float timeOfDay, CreatureAttack& attack,
               std::vector<CreatureExplosion>& blasts);
    void step(const World& world, Creature& creature, float deltaSeconds);
    /// Pushes overlapping creatures apart horizontally. Soft, so it never
    /// fights the world collision that runs before it.
    void separate(const World& world, float deltaSeconds);
    /// Lands every creature-on-creature blow collected this tick.
    void applyHits();
    /// Fills `m_loot` with everything one killed creature leaves behind.
    ///
    /// Split out of `manage`'s retirement loop rather than left inline: it now
    /// reads a table, a death context and four kinds of roll, and none of that
    /// is about retiring a creature.
    void emitLoot(const Creature& creature);
    /// **The one way a creature enters the population**, and the only place an
    /// id is handed out. There were five creation sites and two of them were
    /// patched by hand when ids arrived, which left everything spawned by the
    /// world itself carrying id 0 - so a wolf could never be named as anything's
    /// threat. Routing all five through here makes that impossible rather than
    /// remembered.
    void add(Creature creature);
    /// Rouses the struck creature's own kind nearby: fighters join in, prey
    /// bolts with it. The reference's `alert_same_type`, and the same mechanism
    /// serves pack anger and herd flight.
    void alertNeighbours(const Creature& struck, std::size_t struckIndex, std::uint32_t threatId);
    /// Places a chunk's own group of animals the first time the player comes
    /// near it. Derived entirely from the seed and the chunk coordinate, so a
    /// given chunk always produces the same herd.
    void populateChunks(const World& world, const glm::vec3& playerFeet);
    /// Index of the nearest creature on the aim ray, or `size()` for none.
    /// `ignoreId` is skipped outright; zero skips nobody, because ids start at
    /// one.
    ///
    /// `entryDistance`, when given, receives how far along the ray the winning
    /// body was entered - the number the slab test already computes to decide
    /// "nearest" and used to throw away. A caller that needs it would otherwise
    /// have to run the ray a second time to recover it.
    std::size_t findAimed(const glm::vec3& eye, const glm::vec3& forward, float reach,
                          std::uint32_t ignoreId = 0, float* entryDistance = nullptr) const;
    /// The live creature carrying this id, or null. Linear across a population
    /// capped in the tens, which is cheaper than keeping a map in step.
    const Creature* creatureById(std::uint32_t id) const;
    Creature* creatureById(std::uint32_t id);

    std::vector<Creature> m_creatures;
    /// Chunks whose one-off group has already been placed, keyed by packed
    /// coordinate. Only grows, which is correct: a chunk gets its animals once
    /// per session, and re-populating on re-entry would breed a herd out of
    /// walking back and forth.
    ///
    /// **"Per session" was the bug, and it is now fixed.** This used never to be
    /// written to disk while every living creature was - so every reload re-ran
    /// the one-off pass over chunks whose animals had just been restored, and
    /// the herd doubled. `restore` marks what it brings back, which closed the
    /// common case; the set itself is now persisted by `WorldStore` in its own
    /// table in `creatures.dat`, through `populatedColumns` and
    /// `restorePopulatedColumn`, which closes the rest. It is keyed by column
    /// rather than hung off a creature record because a column stays populated
    /// after everything in it has been eaten.
    std::unordered_set<std::uint64_t> m_populated;
    /// Packs a chunk coordinate into an `m_populated` key. **One owner**, or the
    /// producer and the consumer are free to pack it differently and the set
    /// silently never matches.
    static std::uint64_t populatedKey(int chunkX, int chunkZ);
    /// Marks the chunk containing a world position as already populated.
    void markPopulated(const glm::vec3& feet);
    /// The world's own seed, kept as it arrived. **Separate from `m_random`**,
    /// which is `seed | 1` and is consumed by everything that draws: a drop
    /// roll has to be a pure function of the death, so it hashes this instead
    /// of drawing from a stream whose position depends on how many frames the
    /// animal happened to live for.
    ///
    /// That claim was **not** true until `DeathContext::place` existed - the
    /// hash read the corpse's live position, and `separate` shoves a corpse for
    /// the whole second it lies there, per frame. The seed was never the leak;
    /// the position was.
    std::uint32_t m_seed;
    std::uint32_t m_random;
    /// Handed out by `place` and `restore`. Starts at one so that zero can mean
    /// "nobody" everywhere it is stored.
    std::uint32_t m_nextId = 1;
    float m_spawnTimer = 0.0f;
    std::vector<Loot> m_loot;
    std::vector<Launch> m_launches;
    std::vector<glm::ivec3> m_grazed;
    std::vector<glm::ivec3> m_pollinated;
    std::vector<CreatureVoiceEvent> m_voices;
    std::vector<CreatureHit> m_hits;
    /// Overwritten at startup from the render distance; the default only covers
    /// the window before the first `setActiveRadius`.
    float m_activeRadius = 90.0f;

    /// The one route planner, shared by the whole population so that repeated
    /// searches reuse its buffers instead of allocating.
    path::Pathfinder m_pathfinder;
    /// How many searches are left this frame. One animal boxed into a maze must
    /// not be able to cost a frame on its own, so the population shares a
    /// budget exactly as light propagation and water flow already do.
    int m_pathBudget = 0;
    /// The fractional searches not yet handed out, carried between frames.
    /// **This is what makes the budget a rate rather than a per-frame quota** -
    /// see `kPathsPerSecond`.
    float m_pathCredit = 0.0f;
};

} // namespace game
