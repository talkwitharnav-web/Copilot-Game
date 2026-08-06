#include "world/Creature.hpp"

#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/Explosion.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace game {
namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPi = 3.14159265f;

constexpr float kGravity = 26.0f;
constexpr float kTerminalVelocity = 60.0f;
constexpr float kTurnRate = 4.0f;
/// Drops deeper than this are treated as a wall. Without it, wandering animals
/// walk off every cliff in the world and the population drains downhill.
constexpr float kMaxDropHeight = 3.0f;

/// How far a creature may be from the player before it is retired, and the ring
/// it is allowed to appear in. The near edge keeps them from popping into view.
constexpr float kDespawnDistance = 90.0f;
/// The reference's spawn shell, 24 to 44 blocks at its lowest simulation
/// distance. Ours was 12 to 30, which is close enough to watch one appear.
constexpr float kSpawnNear = 24.0f;
constexpr float kSpawnFar = 44.0f;
constexpr std::size_t kMaxCreatures = 14;
constexpr float kSpawnInterval = 2.5f;

/// How far up a body buried in terrain will climb to free itself, and in what
/// steps. Upward only: it is the direction that works for something covered
/// over, and a sideways nudge would just as often find another block.
constexpr float kUnstickReach = 2.0f;
constexpr float kUnstickStep = 0.25f;

/// A struck creature is shoved back and flashes for this long. How long it
/// stays roused afterwards is `CreatureSpecies::angerSeconds`, which used to be
/// a flat six seconds here for every animal in the world.
constexpr float kHurtSeconds = 0.35f;
/// The shove a player's blow carries.
constexpr float kStrikeKnockback = 5.0f;
constexpr float kStrikeLift = 4.0f;
/// How hard a blast throws what it catches. The reference works in blocks per
/// tick and we work in blocks per second, so it is twenty times over.
constexpr float kBlastThrow = 20.0f;

/// The skin sheet is one column of nets. Each creature names the row it starts
/// at rather than an index, because they are not all the same height - a sheep
/// needs 32 rows and a cow 64.
constexpr float kSkinWidth = static_cast<float>(kCreatureSheetWidth);
constexpr float kSkinSheet = static_cast<float>(kCreatureSheetHeight);
constexpr float kSheepHide = 0.0f;
constexpr float kSheepFleece = 32.0f;
constexpr float kBrambleSkin = 64.0f;
constexpr float kCowSkin = 96.0f;
constexpr float kPigSkin = 160.0f;
constexpr float kChickenSkin = 192.0f;
constexpr float kCatSkin = 224.0f;
constexpr float kCamelSkin = 256.0f;
constexpr float kHorseSkin = 384.0f;
constexpr float kMuleSkin = 448.0f;
constexpr float kLlamaSkin = 512.0f;
constexpr float kDonkeySkin = 576.0f;
constexpr float kGoatSkin = 640.0f;
constexpr float kRabbitSkin = 704.0f;
constexpr float kWolfSkin = 768.0f;
/// The same net again with red eyes and a lowered brow - the reference's only
/// difference between a wolf and an angry one is forty pixels on the face. It
/// fits inside the wolf's own 64-row allocation, so the sheet does not grow.
constexpr float kWolfAngrySkin = 800.0f;
constexpr float kFrogSkin = 832.0f;
constexpr float kFoxSkin = 896.0f;
constexpr float kOcelotSkin = 928.0f;
constexpr float kPolarBearSkin = 960.0f;
constexpr float kPandaSkin = 1024.0f;
constexpr float kSlimeSkin = 1088.0f;
constexpr float kSpiderSkin = 1120.0f;
constexpr float kZombieSkin = 1152.0f;
constexpr float kSkeletonSkin = 1216.0f;
constexpr float kCaveSpiderSkin = 1248.0f;
constexpr float kVillagerSkin = 1280.0f;
constexpr float kHuskSkin = 1344.0f;
constexpr float kSilverfishSkin = 1408.0f;
constexpr float kBlackboneSkin = 1440.0f;
constexpr float kStraySkin = 1472.0f;
constexpr float kBoggedSkin = 1504.0f;
constexpr float kZombieVillagerSkin = 1536.0f;
constexpr float kWitchSkin = 1600.0f;
constexpr float kWanderingTraderSkin = 1728.0f;
constexpr float kPrincepinSkin = 1792.0f;
/// The charged Bramble's energy shell. **Not a skin** - it is the reference's
/// `creeper_armor` overlay, the same net as the Bramble itself so the same box
/// UVs read it, mostly transparent with the blue splotches that survive the
/// cutout test. Drawn as a second, larger shell exactly the way the sheep's
/// fleece is.
constexpr float kChargeSkin = 1856.0f;
constexpr float kDrownedSkin = 1888.0f;
/// The drowned's clothing, on the same net as the body underneath it - the
/// reference's `drowned_outer_layer`. An alpha-tested cutout like every other
/// skin here, so it needs no new rendering path: the same rig is drawn twice,
/// the second pass a fraction of a texel larger.
constexpr float kDrownedOuterSkin = 1952.0f;
constexpr float kCodSkin = 2016.0f;
constexpr float kSalmonSkin = 2048.0f;
constexpr float kPufferfishSkin = 2080.0f;
constexpr float kSquidSkin = 2112.0f;
constexpr float kGlowSquidSkin = 2144.0f;
constexpr float kTurtleSkin = 2176.0f;
constexpr float kDolphinSkin = 2240.0f;
/// Five liveries at 64 rows each, chosen per individual.
constexpr float kAxolotlSkin = 2304.0f;
constexpr float kAxolotlVariantRows = 64.0f;
/// Two body shapes, each followed by its six pattern overlays. **The pattern is
/// a second alpha-cutout shell over the body**, exactly as the drowned's
/// clothing and the sheep's fleece are - which is what buys a reef's worth of
/// fish from fourteen skins instead of authoring every combination.
constexpr float kTropicalASkin = 2624.0f;
constexpr float kTropicalBSkin = 2848.0f;
/// The Tier 1 batch. Each of the first four shares its whole rig with a species
/// already here and differs only in which rows of the sheet it reads.
constexpr float kMushroomCowSkin = 3072.0f;
constexpr float kMagmaCubeSkin = 3136.0f;
constexpr float kSkeletonHorseSkin = 3200.0f;
constexpr float kZombieHorseSkin = 3264.0f;
/// Not a species: the trader llama's pack, on the same net as the llama under
/// it, drawn as a second cutout shell. The sheep's fleece arrangement.
constexpr float kTraderLlamaDecorSkin = 3328.0f;
constexpr float kVoidmiteSkin = 3392.0f;
constexpr float kPrincepinBruteSkin = 3424.0f;
constexpr float kZombiePrincepinSkin = 3488.0f;
constexpr float kTropicalRows = 32.0f;
constexpr int kTropicalPatterns = 6;
constexpr float kSkinTextureLayer = -3.0f;
/// Same sheet, but the fragment shader tints it red. A struck creature has to
/// signal somehow and every vertex colour channel already carries something.
constexpr float kSkinHurtLayer = -4.0f;
/// Same again, washed toward white. A lit fuse strobes between this and the
/// ordinary layer, which is the reference's own tell that something is about to
/// go off.
constexpr float kSkinFlashLayer = -5.0f;
/// One texel is 1/16 of a block, matching the block textures.
constexpr float kTexel = 1.0f / 16.0f;

/// Half the thickness given to a fin the reference authors with none at all.
/// A box of zero extent puts its two large faces on exactly one plane, and
/// every creature quad here is double-sided, so they would fight for every
/// pixel. About a fifth of a texel: invisible, and twenty times the depth
/// precision at normal viewing range.
constexpr float kFinHalfThickness = 0.012f;

/// How far past its own body a blow lands, and how long before the next.
///
/// Bedrock's `melee_box_attack.horizontal_reach`: the mob's own box grown by
/// this much on the horizontal axes, which the target has to overlap. It is
/// **not** a flat distance, and using one gave a silverfish a polar bear's
/// reach and a polar bear a silverfish's. The two older components pick reach
/// differently again - `melee_attack` multiplies body size by 2 and
/// `delayed_attack` by 1.5 - but every modern mob in the reference uses the box
/// form, so that is the one to copy.
constexpr float kMeleeHorizontalReach = 0.8f;
constexpr float kAttackInterval = 1.0f;
/// How long one swing of the arm takes, against that one-second gap between
/// blows. Six ticks, the reference's own swing duration - so a mob strikes,
/// recovers, and stands there for most of a second before doing it again.
constexpr float kAttackSwingSeconds = 0.3f;

/// What a hopper does when it arrives on you. **Ours, not the reference's** -
/// Bedrock makes a slime `pushable` and lets entity collision separate the two,
/// and we have no push between a creature and the player at all.
///
/// So the player's body is treated as a wall instead: the part of the slime's
/// velocity heading into you is sent back out at `kReboundRestitution` of its
/// speed, and it is lifted clear of the ground so the recoil is a real hop
/// rather than something the next tick flattens. It lands, gathers, and comes
/// again, which is the cycle a slime is supposed to have.
constexpr float kReboundRestitution = 0.6f;
constexpr float kReboundLift = 0.6f;

/// How much the gap between hops closes once a slime has something to chase.
/// Bedrock swaps `movement.jump.jump_delay` from 0.5-1.5 s to 0.16-0.5 when it
/// turns aggressive, so roughly a third of the wait.
constexpr float kChaseHopGather = 0.33f;
/// `melee_fov`, halved because the test is against a bearing rather than a
/// cone. Ninety degrees means a creature has to be turned toward you to land a
/// blow, so walking round one buys the moment it takes to come about.
constexpr float kMeleeHalfFov = 0.7853982f;

/// Bedrock's `scan_interval`: ten ticks between attempts at noticing something,
/// rather than one. The docs say plainly that anything under ten hurts
/// performance, and it buys two things beyond the cost - a target that cannot
/// flicker on and off frame by frame at the exact edge of the sense range, and
/// a line-of-sight ray paid for twice a second instead of at the frame rate.
constexpr float kTargetScanSeconds = 0.5f;

/// Bedrock's `look_at_player`: notices you within 8 m, rolls **0.02 per tick**
/// and holds the glance for two to four seconds.
///
/// The probability is converted to a per-second rate, because our tick is a
/// frame and the reference's is a fixed twentieth of a second - used verbatim,
/// a chicken at 120 fps would roll six times as often as one at 20.
constexpr float kLookDistance = 8.0f;
constexpr float kLookChancePerSecond = 0.02f / 0.05f;
constexpr float kLookSecondsMin = 2.0f;
constexpr float kLookSecondsMax = 4.0f;
/// A head snaps round quicker than a body turns, and stops well short of
/// behind - a head that could rotate all the way round reads as a bug rather
/// than as attention.
constexpr float kHeadTurnRate = 6.0f;
constexpr float kMaxHeadTurn = 0.9f;
/// Bedrock does not publish a numeric limit; 40 degrees is the reference's
/// documented default for how far a head tips, and a neck that could fold
/// further would read as broken rather than attentive.
constexpr float kMaxHeadPitch = 0.7f;

/// Bedrock's `target_sneak_visibility_multiplier`, with the floor that goes
/// with it: a crouched player is noticed at four fifths the range, but **two
/// metres is always close enough** however carefully they move.
constexpr float kSneakDetection = 0.8f;
constexpr float kMinDetection = 2.0f;
/// Bedrock's `avoid_mob_type` sprint multiplier. A creeper runs from a cat
/// faster than it ever chases you, which is the whole joke.
constexpr float kAvoidSpeedScale = 1.2f;
/// Ticks of fuse bought per block fallen, and the ceiling it stops at. Both are
/// the reference's: a long drop lands a creeper already most of the way through
/// its countdown, but never past it, so there is always a moment to react.
constexpr float kFallFuseTicksPerBlock = 1.5f;
constexpr float kFallFuseHeadroomTicks = 5.0f;
constexpr float kTicksPerSecond = 20.0f;

/// How long a creature keeps believing it can see the player after the ray last
/// got through.
///
/// **This is a frame-rate correction, not a mercy.** The reference samples
/// sight twenty times a second; we sample it every frame, so at 120 fps a
/// fence post clipped for a single frame is six times likelier to be caught,
/// and each catch reverses the fuse. A tick's worth of memory puts the sampling
/// back on the reference's footing.
constexpr float kSightGrace = 0.05f;

/// The chicken's wing beat, in the reference's per-tick numbers. `flapSpeed`
/// climbs by 1.2 a tick airborne and falls by 0.3 on the ground, `flapping` is
/// refreshed to 1 while airborne and decays by a tenth a tick, and the phase
/// advances by twice `flapping`. All three are converted to per-second rates,
/// because a frame is not a tick.
constexpr float kFlapOpenRate = 1.2f / 0.05f;
constexpr float kFlapCloseRate = 0.3f / 0.05f;
constexpr float kFlapDecayPerTick = 0.9f;
constexpr float kFlapPhaseRate = 2.0f / 0.05f;

/// What is left of the gap between the wanted limb amplitude and the current
/// one after one tick. The reference closes 40% of it per tick, a lag of about
/// 0.3 s, which is what makes a creature's legs spin up and wind down rather
/// than snapping between still and striding.
constexpr float kLimbSwingLagPerTick = 0.6f;

/// What is left of a step-up's visual lag after one tick, and the most of it
/// that can ever be owed. Half a tick puts a stride's worth of climb away in
/// about a sixth of a second - long enough to read as stepping up, short enough
/// that the drawn body is never far from the box that is actually colliding.
constexpr float kStepSmoothPerTick = 0.5f;
constexpr float kMaxStepSmooth = 1.6f;

/// A cancelled fuse runs back down at this multiple of real time. The reference
/// deflates over the same span it swelled, so one is right and anything faster
/// would make backing off feel like a switch rather than a retreat.
constexpr float kDeflateRate = 1.0f;
/// A charged blast is exactly twice the ordinary one - power 3 becomes 6, which
/// is the reference's own doubling and takes point-blank damage from 27.5 to
/// well past anything on the roster.
constexpr float kChargedPowerScale = 2.0f;
/// How many Brambles arrive already charged. A stand-in: the reference makes
/// them with lightning and we have no weather until M27, so without this the
/// whole thing would be unreachable outside the debug key.
constexpr float kChargedChance = 0.05f;
/// The shove a blow carries. Enough to break your footing, not enough to throw
/// you somewhere you cannot recognise.
constexpr float kKnockbackSpeed = 4.5f;
constexpr float kKnockbackLift = 3.0f;

/// The local planner probes this far ahead, and gives up after this many
/// candidate headings either side of the one it wanted.
constexpr float kProbeDistance = 0.9f;
constexpr int kSteerFanSteps = 6;
constexpr float kSteerFanStep = 0.42f;
/// How far ahead the jump looks. Much shorter than the steering probe on
/// purpose: steering wants to know what is coming, a jump wants to know what it
/// is already up against, and launching a metre early clears nothing.
constexpr float kJumpProbe = 0.45f;

/// Pathfinding. `kRepathSeconds` is the reference's own cadence for a chase -
/// it recomputes every four to ten ticks rather than every tick - and
/// `kRepathMoved` is the other half of the same rule: a target that has walked
/// a block away invalidates the route early.
constexpr float kRepathSeconds = 0.5f;
constexpr float kRepathMoved = 1.0f;
/// How close counts as having reached a waypoint. The reference advances "when
/// it is close enough" and skips a node when heading straight at a later one is
/// safe; this is that distance.
constexpr float kWaypointReached = 0.55f;
/// How long a creature may make no headway before its route is thrown away.
/// The reference stops a path whose mob has stopped advancing, and without it a
/// route into somewhere the legs cannot manage is walked at forever.
constexpr float kProgressWindow = 1.0f;
constexpr float kProgressDistance = 0.25f;
/// Searches allowed across the whole population in one frame. A ceiling on the
/// spike rather than a rate: the repath cadence already holds the population to
/// about two searches a second each, so this only ever binds when several
/// animals are boxed in at once - which is exactly the case worth bounding.
constexpr int kPathsPerFrame = 2;
/// How far an ambling creature picks a spot. The reference's `random_stroll`
/// reaches ten blocks.
constexpr float kWanderRange = 10.0f;

/// Swimming. A fish carries a heading and a dive angle rather than a goal/// point: the reference's `random_swim` aims 16 blocks out and up to 4 up or
/// down, which is an angle of about 14 degrees, so rolling the angle directly
/// reproduces it without storing a destination. The turn rate is slower than a
/// walker's because a fish banks rather than pivots.
constexpr float kSwimPitchMax = 0.28f;
constexpr float kSwimTurnRate = 1.8f;
/// How far ahead a swimmer looks for the edge of its own water. Longer than the
/// walker's probe because there is no floor to stop it - a fish that misses the
/// surface simply leaves.
constexpr float kSwimProbe = 1.4f;

/// A stranded fish flops rather than lying still: a small hop and a new heading
/// on a fixed cadence. It cannot right itself, which is the point of it.
constexpr float kFlopInterval = 0.55f;
constexpr float kFlopLaunch = 3.2f;

/// What counts as a body of water big enough to live in.
///
/// **The reference has no size test at all** - its `spawns_underwater` asks
/// only that the cell is water, and what actually keeps fish out of puddles is
/// the `biome_filter` on the `ocean` and `river` tags. Every aquatic row here is
/// ocean-only for that reason.
///
/// This depth is ours, and it is deliberately small: our biome map is coarser
/// than the reference's, so an ocean region can hug a shoreline more tightly
/// than theirs does. Two cells is what the reference implies anyway - water at
/// the position and water above it - and this is one more for margin.
constexpr int kOpenWaterDepth = 3;

/// The pufferfish. It inflates while something is inside `kPuffRange` and lets
/// go once nothing has been within `kPuffHoldRange` for `kPuffDeflateSeconds` -
/// the reference's 2.5 m, 2.9 m and 3 s, with its two-second stage timers
/// expressed as the rate a single number climbs through the three stages.
constexpr float kPuffRange = 2.5f;
constexpr float kPuffHoldRange = 2.9f;
constexpr float kPuffStageSeconds = 2.0f;
constexpr float kPuffDeflateSeconds = 3.0f;
constexpr float kPuffMax = 2.0f;

/// The squid's jet. **It does not walk and has no gait**: the tentacles open
/// wide and then snap shut, and the snap is the push.
///
/// The curve is the reference's own - `sin(f^2 * pi)` over the first half of
/// the cycle - and the squared term is the whole point of it: the opening takes
/// seven tenths of that half and the closing the remaining three, so it flares
/// slowly and shuts suddenly. Thrust is applied only past three quarters of the
/// way through, which lands it squarely inside the snap. The second half of the
/// cycle holds the tentacles still and coasts.
constexpr float kJetRate = 4.0f;
constexpr float kJetSplay = 0.7853982f;
constexpr float kJetThrustFrom = 0.75f;
constexpr float kJetOpenDecay = 0.8f;
constexpr float kJetCoastDecay = 0.9f;
/// How fast the body swings round to line up with where it is going. The
/// reference's own 0.1 a tick, which is slow enough to see it happen.
constexpr float kBodyTiltLagPerTick = 0.9f;
/// Above this much push, a jetting creature stops steering. The squeeze is what
/// moves it, so it has to be pointing somewhere before it starts - which is the
/// whole reason one has to turn around before it can go the other way.
constexpr float kJetCommitted = 0.5f;

/// How big a baby is against its parent, and how much quicker it moves. The
/// reference gives young a smaller body, an oversized head and a faster walk;
/// we take the first and the last, since one scale cannot do the middle.
constexpr float kBabyScale = 0.55f;
constexpr float kBabySpeedBonus = 1.15f;

/// How hard two creatures standing in each other push apart. A soft impulse
/// rather than a hard constraint, which is the reference's own choice - a hard
/// one would need entities to resolve against each other as well as the world.
constexpr float kSeparationPush = 2.2f;
/// How far up and down a call carries. The horizontal reach is per species now,
/// because the reference's broadcast ranges run from nothing at all for the
/// undead to forty-one metres for a bear.
constexpr float kAlertHeight = 10.0f;

/// Chunks are 32 blocks, so a coordinate shifts by five.
constexpr int kChunkShift = 5;
/// How far out chunks are given their one-off group, in chunks. Kept inside the
/// 90 m retirement radius, or a herd would be placed and immediately dropped.
constexpr int kPopulateRadius = 2;
/// The reference uses 0.10 for most biomes. Most chunks getting nothing is what
/// makes finding a herd worth anything.
constexpr float kChunkSpawnChance = 0.10f;
/// A generated group ignores the ordinary population cap, as the reference's
/// does, but not this. Without a ceiling a walk across open grassland would
/// stack herds without limit.
constexpr std::size_t kGeneratedCeiling = kMaxCreatures * 2;

/// Deterministic mixing for the chunk-generation spawn pass. Multipliers stay
/// small enough to keep the arithmetic inside 32 bits.
std::uint32_t chunkHash(std::uint32_t seed, int x, int z) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 73856093u) ^
                      (static_cast<std::uint32_t>(z) * 19349663u);
    h ^= h >> 13;
    h *= 83492791u;
    h ^= h >> 16;
    return h;
}

float hashUnit(std::uint32_t hash) {
    return static_cast<float>(hash & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/// How far a struck animal's own kind hears it. **Ours, not the reference's** -
/// Bedrock gives farm animals no alerting whatsoever, and a herd that scatters
/// together is worth more than fidelity here.
constexpr float kHerdAlertRange = 16.0f;

/// One row per species. Adding an animal is adding a row.
///
/// Heights for the nine most recent species are Bedrock's hitboxes rather than
/// the model's silhouette: a head or a pair of ears is allowed to rise above
/// the box it collides with. The first four keep the boxes they were playtested
/// with. Half-widths are ours throughout and are deliberately narrower than the
/// reference's - a horse is 0.9 across here against Bedrock's 1.4 - because a
/// wide box catches on doorways and tree trunks far more than it reads as bulk.
///
/// Order must match `CreatureKind`.
///
/// **Fields are named rather than positional.** It used to be a wall of bare
/// values, which meant every new field had to go at the end with a default, and
/// setting one on an old row meant restating a dozen numbers nobody wanted to
/// change. Naming them costs nothing at runtime, lets a row say only what makes
/// it different, and turns a mis-ordered value from a silent wrong animal into
/// a compile error. Designated initialisers must appear in declaration order,
/// so the struct itself is the checklist.
///
/// **`gaitSwing` is radians of limb rotation about the joint**, not a distance.
/// Each value is the species' old hand-tuned foot travel converted back to the
/// angle that produces it, then opened up by half again and held to 0.70 rad -
/// half the reference's 1.4. The cap is not timidity: rotating lifts a foot by
/// `L(1 - cos angle)`, so a short-legged animal at full amplitude prances. The
/// several species that sit on the cap are the short-legged and the fast, which
/// is exactly the set the reference would give one flat amplitude anyway.
constexpr CreatureSpecies kSpecies[] = {
    {.name = "Sheep", .halfWidth = 0.32f, .height = 1.40f, .gaitRate = 6.0f, .gaitSwing = 0.36f,
     .modelScale = 1.00f, .health = 6, .walkSpeed = 1.6f, .runSpeed = 3.4f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 1.0f, .babyChance = 0.05f, .groupSize = 4,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Cow", .halfWidth = 0.45f, .height = 1.50f, .gaitRate = 5.5f, .gaitSwing = 0.36f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.3f, .runSpeed = 2.8f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.9f, .babyChance = 0.05f, .groupSize = 4,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Pig", .halfWidth = 0.32f, .height = 1.05f, .gaitRate = 7.0f, .gaitSwing = 0.66f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.4f, .runSpeed = 3.0f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.9f, .babyChance = 0.05f, .groupSize = 4,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // The one thing in the world that detonates. `attackDamage` stays at 3 so a
    // fuse that is cancelled still leaves something dangerous, but the blast is
    // the point: power 3 clears about four blocks of soil and barely marks
    // stone. The 16 m follow range and the 20 health are Bedrock's.
    //
    // **2.5 m to light and 6 m to cancel come from `creeper.json` itself**, and
    // they were briefly "corrected" to the wiki's 3 and 7 on the reasoning that
    // the wiki was the Bedrock source. It is not: `Mojang/bedrock-samples` is
    // the shipped behaviour pack, its `behavior.swell` reads
    // `start_distance 2.5, stop_distance 6`, and its `target_nearby_sensor`
    // agrees exactly. Do not flip these again without opening that file.
    //
    // The run speed is the speed attribute converted at the low end of the
    // documented x17-19 band and sits just under a walking player, so a head
    // start still saves you and standing still does not.
    //
    // It spawns in the dark and does **not** burn off at dawn. `RESEARCH.md`
    // §13.4 names the burn list exactly and this is not on it, which is why one
    // caught out at sunrise is still following you at noon.
    {.name = "Bramble", .halfWidth = 0.30f, .height = 1.70f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 2.0f, .runSpeed = 4.25f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.9f,
     .burnsInDay = false, .explodePower = 3.0f, .fuseSeconds = 1.5f, .swellStartRange = 2.5f,
     .swellStopRange = 6.0f, .avoidFelineRange = 6.0f, .chaseSpeedScale = 1.25f},
    // Falls slowly and flaps the whole way down, which is the reference's own
    // reason chickens need no fall-damage exemption - they never land hard.
    {.name = "Chicken", .halfWidth = 0.20f, .height = 0.80f, .gaitRate = 14.0f, .gaitSwing = 0.49f,
     .modelScale = 1.00f, .health = 4, .walkSpeed = 1.5f, .runSpeed = 3.0f, .senseRange = 6.0f,
     .maxBlockLight = 15, .weight = 1.1f, .babyChance = 0.05f, .groupSize = 4, .fallDrag = 0.6f,
     .alertRange = kHerdAlertRange},
    {.name = "Cat", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 6, .walkSpeed = 1.8f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.8f, .babyChance = 0.25f, .groupSize = 2,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Long-legged enough to walk up a full block without jumping, which is the
    // reference's own list: the horse family, the llama and the camel all have
    // a step height of 1 or more and so never hop a ledge.
    {.name = "Camel", .halfWidth = 0.48f, .height = 2.375f, .gaitRate = 3.5f, .gaitSwing = 0.35f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.3f, .runSpeed = 2.8f, .senseRange = 10.0f,
     .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 1, .stepHeight = 1.5f,
     .jumpHeight = 0.0f, .alertRange = kHerdAlertRange, .panicSpeedScale = 1.2f},
    {.name = "Horse", .halfWidth = 0.45f, .height = 1.60f, .gaitRate = 5.0f, .gaitSwing = 0.49f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 2.0f, .runSpeed = 4.5f, .senseRange = 10.0f,
     .maxBlockLight = 15, .weight = 0.8f, .babyChance = 0.20f, .groupSize = 4, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Mule", .halfWidth = 0.44f, .height = 1.60f, .gaitRate = 5.2f, .gaitSwing = 0.47f,
     .modelScale = 0.92f, .health = 16, .walkSpeed = 1.8f, .runSpeed = 4.0f, .senseRange = 10.0f,
     .maxBlockLight = 15, .weight = 0.55f, .babyChance = 0.20f, .groupSize = 3, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Llama", .halfWidth = 0.43f, .height = 1.87f, .gaitRate = 5.5f, .gaitSwing = 0.31f,
     .modelScale = 1.00f, .health = 12, .walkSpeed = 1.6f, .runSpeed = 3.5f, .senseRange = 10.0f,
     .maxBlockLight = 15, .weight = 0.65f, .babyChance = 0.10f, .groupSize = 4, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .alertRange = kHerdAlertRange},
    {.name = "Donkey", .halfWidth = 0.42f, .height = 1.60f, .gaitRate = 5.5f, .gaitSwing = 0.44f,
     .modelScale = 0.87f, .health = 15, .walkSpeed = 1.7f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.65f, .babyChance = 0.20f, .groupSize = 3, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Goat", .halfWidth = 0.38f, .height = 1.30f, .gaitRate = 8.0f, .gaitSwing = 0.36f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.7f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 3,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // The gait numbers are zero on purpose for a hopper: its legs are driven by
    // the jump, so there is no cycle to tune. Sat upright the rabbit is tall
    // and narrow, which is what the much smaller scale is paying for.
    {.name = "Rabbit", .halfWidth = 0.18f, .height = 0.60f, .hops = true, .hopLaunch = 4.2f,
     .hopGather = 0.18f, .modelScale = 0.48f, .health = 4, .walkSpeed = 2.0f, .runSpeed = 4.2f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.9f, .babyChance = 0.05f, .groupSize = 3,
     .avoidsWater = true, .alertRange = kHerdAlertRange, .panicSpeedScale = 1.5f},
    // Neutral rather than hostile: it has a bite but no interest in using it
    // until struck. See `CreatureSpecies::attackDamage`. Twenty-five seconds of
    // grudge is the reference's `wolf_angry`, and it is four times what every
    // animal on the roster used to carry.
    {.name = "Wolf", .halfWidth = 0.30f, .height = 0.80f, .gaitRate = 9.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.8f, .runSpeed = 4.2f, .senseRange = 12.0f,
     .attackDamage = 4, .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.10f, .groupSize = 4,
     .angerSeconds = 25.0f, .alertRange = 20.0f},
    // A far higher, lazier arc than the rabbit's: about 0.8 m up with a long
    // pause between hops, which is most of what makes a frog read as a frog.
    // Step height 1 is the reference's, and hopping already is its jump.
    //
    // The one genuinely amphibious animal here: it breathes underwater, walks
    // the bottom rather than bobbing, and `sinks = false` holds it at whatever
    // depth it is at instead of settling.
    {.name = "Frog", .halfWidth = 0.25f, .height = 0.50f, .hops = true, .hopLaunch = 6.5f,
     .hopGather = 0.55f, .modelScale = 1.00f, .health = 10, .walkSpeed = 1.0f, .runSpeed = 2.2f,
     .senseRange = 7.0f, .maxBlockLight = 15, .weight = 0.8f, .groupSize = 4, .stepHeight = 1.0f,
     .floats = false, .sinks = false, .amphibious = true, .breathesWater = true,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.2f},
    {.name = "Fox", .halfWidth = 0.25f, .height = 0.70f, .gaitRate = 11.0f, .gaitSwing = 0.66f,
     .modelScale = 0.93f, .health = 10, .walkSpeed = 1.9f, .runSpeed = 4.0f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 3,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Shares the cat's net and model outright, the way the mule shares the
    // horse's - the reference draws them from one rig too.
    {.name = "Ocelot", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.8f, .runSpeed = 3.9f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.6f, .babyChance = 0.25f, .groupSize = 2,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // The heaviest thing in the world and the second neutral: it ignores you
    // until struck, and then hits for six. Slow, so backing off actually works -
    // except that it now holds a grudge for the reference's five hundred
    // seconds and follows to 48 m, so backing off has to mean leaving.
    // Scaled well past its hitbox on purpose - a bear that does not tower over
    // the roster is not doing its job, and the horse already sets the precedent
    // for a model much taller than the box it collides with.
    {.name = "Polar Bear", .halfWidth = 0.50f, .height = 1.40f, .gaitRate = 4.5f,
     .gaitSwing = 0.64f, .modelScale = 1.25f, .health = 30, .walkSpeed = 1.2f, .runSpeed = 3.0f,
     .senseRange = 12.0f, .attackDamage = 6, .maxBlockLight = 15, .weight = 0.4f,
     .babyChance = 0.10f, .groupSize = 2, .leashRange = 48.0f, .angerSeconds = 500.0f,
     .alertRange = 41.0f},
    {.name = "Panda", .halfWidth = 0.45f, .height = 1.25f, .gaitRate = 4.5f, .gaitSwing = 0.54f,
     .modelScale = 1.10f, .health = 20, .walkSpeed = 0.8f, .runSpeed = 1.8f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.35f, .babyChance = 0.05f, .groupSize = 2,
     .avoidsWater = true, .angerSeconds = 500.0f, .alertRange = 41.0f},
    // The three slimes are one animal at three sizes, and the size is the whole
    // design: health is the size squared, damage is the size, and killing one
    // leaves two to four of the next size down. A small one does no damage at
    // all and still hunts you, exactly as the reference has it.
    //
    // They hop rather than walk, and the launch is identical for all three
    // because the reference jumps one block high whatever the size - only the
    // distance covered scales, which falls out of the speed.
    {.name = "Slime", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.90f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.5f, .runSpeed = 1.1f,
     .hostile = true, .senseRange = 16.0f, .nocturnal = true, .maxBlockLight = 15, .weight = 0.6f,
     .burnsInDay = false, .avoidsWater = true},
    {.name = "Slime", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.85f, .modelScale = 2.08f, .health = 4, .walkSpeed = 0.8f, .runSpeed = 1.8f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true,
     .maxBlockLight = 15, .weight = 0.5f, .burnsInDay = false,
     .splitInto = CreatureKind::SlimeSmall, .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    {.name = "Slime", .halfWidth = 0.75f, .height = 2.08f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.80f, .modelScale = 4.16f, .health = 16, .walkSpeed = 1.3f, .runSpeed = 2.8f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 4, .nocturnal = true,
     .maxBlockLight = 15, .weight = 0.3f, .burnsInDay = false,
     .splitInto = CreatureKind::SlimeMedium, .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    // Neutral by temperament and hostile by circumstance: it ignores you in the
    // light and hunts on sight in the dark, which is the reference's rule and a
    // far better use of our light system than another always-hostile mob.
    //
    // Ten seconds of anger is `spider_angry`, and it is what makes the light
    // rule survive contact with a torch: once it has you, brightening the room
    // no longer calls it off.
    //
    // **Both spiders run the reference's own gait numbers rather than ours**,
    // because `animation.spider.walk` is ported whole: 2.665 radians per metre
    // is `ANIMATION.md` §2.2's rate for the whole of Minecraft, and 0.4 is the
    // 22.92 degrees the animation swings. Driving it by distance is what makes
    // a chase scuttle and a stroll amble with no second animation.
    {.name = "Spider", .halfWidth = 0.45f, .height = 0.90f, .gaitRate = 2.665f, .gaitSwing = 0.40f,
     .modelScale = 1.10f, .health = 16, .walkSpeed = 1.6f, .runSpeed = 4.0f, .senseRange = 16.0f,
     .attackDamage = 2, .nocturnal = true, .maxBlockLight = 15, .weight = 0.8f,
     .burnsInDay = false, .huntsBelowLight = 11, .climbs = true, .angerSeconds = 10.0f},
    // Same rig as the spider - their reference skins have byte-identical alpha -
    // at two thirds the size, which is the whole difference.
    {.name = "Cave Spider", .halfWidth = 0.30f, .height = 0.50f, .gaitRate = 2.665f,
     .gaitSwing = 0.40f, .modelScale = 0.70f, .health = 12, .walkSpeed = 1.8f, .runSpeed = 4.2f,
     .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 15,
     .weight = 0.6f, .burnsInDay = false, .huntsBelowLight = 11, .climbs = true,
     .angerSeconds = 10.0f},
    // The first bipeds. Movement is the cheap half of a humanoid - two legs and
    // two counter-swinging arms - and these carry none of the villager's
    // economy, so they cost a rig and a row each.
    //
    // **35 m to notice and 25 m to give up are the reference's, and they are
    // the right way round**: a zombie sees you across a field and then loses
    // interest closer in than it started. Seventeen seconds of memory is its
    // own override of the three everything else gets, which is why a corner
    // shakes off a skeleton and does nothing about a zombie.
    //
    // The undead do not swim. `is_amphibious` with no float goal means they
    // walk the seabed, and `breathes_water` is why that costs them nothing -
    // so a lake is a road to a zombie rather than a wall.
    {.name = "Zombie", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f, .hostile = true,
     .senseRange = 35.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 1.0f,
     .floats = false, .amphibious = true, .breathesWater = true,
     .forgetSeconds = 17.0f, .leashRange = 25.0f, .swingsArms = true},
    // No bow yet, so it closes and hits rather than shooting. Faster and
    // lighter than the zombie to keep the two feeling different, and it sprints
    // the last stretch - `melee_box_attack.speed_multiplier` is 1.25 on every
    // skeleton in the reference and 1 on every zombie.
    //
    // **It does not swing its arms**, and that is deliberate rather than
    // missing: a skeleton is an archer. The reference only drops one to melee
    // when it has no bow, and until we have arrows a punching animation would
    // tell the player it is something it is not.
    {.name = "Skeleton", .halfWidth = 0.28f, .height = 1.99f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.2f, .runSpeed = 2.8f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.9f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f},
    // Model and texture only for now: it wanders and nothing more. Trading, the
    // schedule and villages are their own milestone entirely. It flees slower
    // than it walks, which is the reference's 0.6 and reads as panic rather
    // than athleticism.
    {.name = "Villager", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f, .gaitSwing = 0.62f,
     .modelScale = 0.92f, .health = 20, .walkSpeed = 0.9f, .runSpeed = 1.6f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.5f, .avoidsWater = true, .alertRange = kHerdAlertRange,
     .panicSpeedScale = 0.65f},
    // The zombie's rig verbatim - the two reference skins have byte-identical
    // alpha - and the one thing that makes it a different animal is that it
    // does **not** burn off at dawn. So the desert is the one region where
    // something is still hunting you at noon. It forgets in three seconds where
    // a zombie takes seventeen, which is the reference's own divergence and
    // easy to miss.
    {.name = "Husk", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f, .hostile = true,
     .senseRange = 35.0f, .attackDamage = 4, .nocturnal = true, .maxBlockLight = 6, .weight = 0.9f,
     .burnsInDay = false, .floats = false, .amphibious = true, .breathesWater = true,
     .leashRange = 25.0f, .swingsArms = true},
    // The smallest thing in the world: seven tapering segments, no legs, and a
    // serpentine wriggle. It has no limb to turn, so `gaitSwing` is zero and
    // the wriggle's own amplitude - a sideways distance, not an angle - lives
    // beside the segments in `buildMesh` where the rest of its shape does.
    // Lives underground and does not burn, so a cave is its home rather than a
    // night shift.
    //
    // **The one species that needs no line of sight**, which is the reference's
    // own omission and why it comes at you through stone. It is also the only
    // one whose `hurt_by_target` sets `alert_same_type`, and its anger never
    // expires - so ten minutes stands in for never.
    {.name = "Silverfish", .halfWidth = 0.20f, .height = 0.30f, .gaitRate = 11.0f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.3f, .runSpeed = 2.6f, .hostile = true,
     .senseRange = 8.0f, .attackDamage = 1, .nocturnal = true, .maxBlockLight = 4, .weight = 0.8f,
     .burnsInDay = false, .mustSee = false, .angerSeconds = 600.0f, .alertRange = 20.0f},
    // The skeleton's rig at 1.20 scale, which is what takes a 2.00 model to the
    // reference's 2.4 hitbox. Hits far harder than the skeleton and is rarer
    // for it. Also not on §13.4's burn list, and for the same reason as the
    // Bramble: only the ordinary undead catch fire, and staying out past dawn
    // is what makes it the rare one worth being afraid of.
    //
    // **The one skeleton that does swing**: it carries a stone sword in the
    // reference rather than a bow, so melee is its real attack.
    {.name = "Blackbone", .halfWidth = 0.30f, .height = 2.40f, .gaitRate = 5.5f, .gaitSwing = 0.70f,
     .modelScale = 1.20f, .health = 20, .walkSpeed = 1.1f, .runSpeed = 2.6f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 6, .nocturnal = true, .maxBlockLight = 6,
     .weight = 0.35f, .burnsInDay = false, .floats = false, .amphibious = true,
     .breathesWater = true, .avoidsWater = true, .chaseSpeedScale = 1.25f, .swingsArms = true},
    // Stray and bogged are the skeleton's rig again, unchanged. What separates
    // them is where they live and how hard they are: the stray holds the cold
    // uplands and is the tougher, the bogged swarms the lowlands and is the
    // weaker. The reference tells them apart by the arrows they fire, which we
    // have no bow for yet.
    {.name = "Stray", .halfWidth = 0.28f, .height = 1.99f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.2f, .runSpeed = 2.8f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.5f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f},
    {.name = "Bogged", .halfWidth = 0.28f, .height = 1.99f, .gaitRate = 6.5f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.3f, .runSpeed = 3.0f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.6f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f},
    // The villager's rig - its nets are identical row for row - but posed with
    // the arms held out rather than folded, which is the reference's own
    // distinction between a villager and one that has turned.
    {.name = "Zombie Villager", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 0.92f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f,
     .hostile = true, .senseRange = 35.0f, .attackDamage = 3, .nocturnal = true,
     .maxBlockLight = 6, .weight = 0.4f, .floats = false, .amphibious = true,
     .breathesWater = true, .leashRange = 25.0f, .swingsArms = true},
    // The villager rig again, under a four-box pointed hat and with a wart on
    // the nose. Tough and slow, and like the husk it does not burn - so one
    // caught out at dawn keeps coming. It notices you at only 10 m, which is
    // the reference's, and then follows to 64: a witch is hard to provoke and
    // very hard to shake.
    {.name = "Witch", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f, .gaitSwing = 0.62f,
     .modelScale = 0.92f, .health = 26, .walkSpeed = 0.9f, .runSpeed = 1.8f, .hostile = true,
     .senseRange = 10.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.3f,
     .burnsInDay = false, .leashRange = 64.0f},
    // Passive, rare and found anywhere: the one thing on the roster that is
    // meant to read as a traveller rather than a resident. Trading is a
    // milestone of its own, so for now it only wanders.
    {.name = "Wandering Trader", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f,
     .gaitSwing = 0.62f, .modelScale = 0.92f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 1.8f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.2f, .avoidsWater = true,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 0.65f},
    // The first hostile that spawns in **daylight**, which is most of the point
    // of it: the uplands stop being safe at noon. Its body is the biped rig at
    // the standard limb offsets; only the head is its own. Thirty seconds of
    // anger and a 64 m leash are the reference's piglin, and its kind hear a
    // blow at 16 m - the one hostile on the roster that answers a call.
    //
    // It has no float goal in the shipped data, which is not an oversight: a
    // piglin genuinely will not swim up, so deep water drowns it.
    {.name = "Princepin", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.1f, .runSpeed = 2.4f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 5, .maxBlockLight = 15, .weight = 0.3f,
     .burnsInDay = false, .floats = false, .avoidsWater = true, .leashRange = 64.0f,
     .angerSeconds = 30.0f, .alertRange = 16.0f, .swingsArms = true},
    // The zombie's rig and the zombie's stats, living in the sea. It is
    // amphibious with no float goal, so it walks the seabed exactly as its dry
    // cousins do - and `breathes_water` is why that costs it nothing.
    //
    // It notices you at 20 m, which is the reference's `max_dist` rather than
    // the zombie's 35, and keeps the zombie's seventeen seconds of memory. Its
    // land speed is 0.23 against a zombie's 0.23, but `underwater_movement` is
    // only 0.06 - a quarter - which is why one is slow to reach you and then
    // alarming once you are both on the beach.
    {.name = "Drowned", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f, .hostile = true,
     .senseRange = 20.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 7, .weight = 1.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .spawnsInWater = true,
     .maxSpawnY = kSeaLevel, .maxLoaded = 5, .forgetSeconds = 17.0f, .leashRange = 25.0f,
     .swingsArms = true},
    // The first two things that swim. `swims` is the whole archetype: no
    // gravity while submerged, a heading in three dimensions rather than two,
    // and no ability to walk at all - so out of water they flop and suffocate.
    //
    // Three health and a 0.1 speed attribute are the reference's, converted at
    // the same x17 the rest of the roster uses. The group size, the ceiling and
    // the population cap are its `herd`, `height_filter` and `density_limit`
    // from `spawn_rules/cod.json` - 4 to 7 a shoal, anywhere at or below the
    // surface, and twenty at a time.
    {.name = "Cod", .halfWidth = 0.25f, .height = 0.30f, .gaitRate = 0.0f, .modelScale = 1.00f,
     .health = 3, .walkSpeed = 1.7f, .runSpeed = 3.4f, .senseRange = 6.0f, .maxBlockLight = 15,
     .weight = 1.0f, .groupSize = 7, .stepHeight = 0.0f, .jumpHeight = 0.0f, .floats = false,
     .sinks = false, .breathesWater = true, .swims = true, .breathesAir = false,
     .spawnsInWater = true, .maxSpawnY = kSeaLevel, .maxLoaded = 20, .walksOnLand = false,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.5f},
    // Bigger, faster and a longer two-part body. The reference rolls three
    // sizes at spawn - 0.5, 1.0 and 1.5 - and ours reuses the baby scale for
    // the small one rather than adding a second size mechanism.
    //
    // **Ocean only.** The reference's second home for a salmon is a *river*,
    // which we do not have, and a beach was standing in for one - but a beach
    // here is the shallow edge of the sea rather than a channel, so it put
    // salmon in the surf.
    {.name = "Salmon", .halfWidth = 0.22f, .height = 0.50f, .gaitRate = 0.0f, .modelScale = 1.00f,
     .health = 3, .walkSpeed = 2.0f, .runSpeed = 4.0f, .senseRange = 6.0f, .maxBlockLight = 15,
     .weight = 0.8f, .babyChance = 0.30f, .groupSize = 5, .stepHeight = 0.0f, .jumpHeight = 0.0f,
     .floats = false, .sinks = false, .breathesWater = true, .swims = true, .breathesAir = false,
     .spawnsInWater = true, .maxSpawnY = kSeaLevel, .maxLoaded = 10, .walksOnLand = false,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.5f},
    // Inflates in three stages when something comes within 2.5 m and lets go
    // three seconds after it leaves. The stages are three separate *models* -
    // the spines are real geometry, not a scale - which is why this is a flag
    // rather than a size. The reference's own `scale` of 1.2 is folded into
    // `modelScale`.
    //
    // **Its collision box does not grow with it.** The reference swaps the box
    // per stage, and one species owns one shape here; a per-stage box is a
    // change to the shape table rather than to this row. Nothing collides with
    // a fish but terrain, so the cost is that a puffed one is easier to swim
    // past than it should be.
    {.name = "Pufferfish", .halfWidth = 0.28f, .height = 0.60f, .gaitRate = 0.0f,
     .modelScale = 1.20f, .health = 3, .walkSpeed = 2.2f, .runSpeed = 4.4f, .senseRange = 6.0f,
     .maxBlockLight = 15, .weight = 0.5f, .groupSize = 5, .stepHeight = 0.0f, .jumpHeight = 0.0f,
     .floats = false, .sinks = false, .breathesWater = true, .swims = true, .breathesAir = false,
     .spawnsInWater = true, .maxSpawnY = kSeaLevel, .maxLoaded = 3, .puffs = true,
     .walksOnLand = false, .panicSpeedScale = 1.5f},
    // The first thing built from a ring of limbs rather than a spine and legs:
    // a bell with eight tentacles hanging off it at forty-five degree spacing.
    // Ten health and a 0.2 speed attribute are the reference's, and 5% of them
    // arrive as young, which is its own `entity_spawned` roll rather than the
    // baby share the land roster uses.
    {.name = "Squid", .halfWidth = 0.35f, .height = 0.80f, .gaitRate = 0.0f, .modelScale = 0.80f,
     .health = 10, .walkSpeed = 1.6f, .runSpeed = 3.4f, .senseRange = 8.0f, .maxBlockLight = 15,
     .weight = 0.6f, .babyChance = 0.05f, .groupSize = 4, .stepHeight = 0.0f, .jumpHeight = 0.0f,
     .floats = false, .sinks = false, .breathesWater = true, .swims = true, .breathesAir = false,
     .spawnsInWater = true, .maxSpawnY = kSeaLevel, .maxLoaded = 4, .jets = true,
     .walksOnLand = false, .panicSpeedScale = 1.5f},
    // The squid's rig outright - the two reference skins are the same net at
    // the same size - lit by itself rather than by the water around it.
    //
    // **It is the one aquatic that wants depth.** Its `height_filter` is -64 to
    // 30 against a sea level of 63, so thirty-three blocks down and lower; our
    // world is a quarter as tall, which puts it six below ours.
    {.name = "Glow Squid", .halfWidth = 0.35f, .height = 0.80f, .gaitRate = 0.0f,
     .modelScale = 0.80f, .health = 10, .walkSpeed = 1.6f, .runSpeed = 3.4f, .senseRange = 8.0f,
     .maxBlockLight = 0, .weight = 0.35f, .babyChance = 0.05f, .groupSize = 4, .stepHeight = 0.0f,
     .jumpHeight = 0.0f, .floats = false, .sinks = false, .breathesWater = true, .swims = true,
     .breathesAir = false, .spawnsInWater = true, .maxSpawnY = kSeaLevel - 6, .maxLoaded = 4,
     .glows = true, .jets = true, .walksOnLand = false, .panicSpeedScale = 1.5f},
    // Spawns on the sand like the reference does rather than in the water, and
    // swims only once it gets there - `can_swim` *and* `can_walk`, which is the
    // first time those two have been true together here.
    //
    // Thirty health is the reference's and makes it far the toughest passive in
    // the sea. **What it does not have yet is a home beach**: the reference
    // remembers where it hatched and returns there to lay, which needs
    // per-creature state that survives a save and is a milestone of its own.
    {.name = "Turtle", .halfWidth = 0.50f, .height = 0.40f, .gaitRate = 6.0f, .gaitSwing = 0.30f,
     .modelScale = 1.00f, .health = 30, .walkSpeed = 1.0f, .runSpeed = 2.0f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.5f, .babyChance = 0.10f, .groupSize = 2, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .floats = false, .sinks = false, .breathesWater = true, .swims = true,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.2f},
    // Fast, and the only thing in the sea that hits back without being hostile:
    // its `angry` duration is 25 seconds and its kind hear it at 16 m.
    //
    // **Named divergence: it does not drown.** The reference gives it a
    // 240-second air supply and a goal that sends it up to breathe, and without
    // that goal a drowning dolphin would simply die at the bottom - so it
    // breathes water and the land timer is what can kill it.
    {.name = "Dolphin", .halfWidth = 0.40f, .height = 0.60f, .gaitRate = 0.0f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 2.6f, .runSpeed = 5.2f, .senseRange = 12.0f,
     .attackDamage = 3, .maxBlockLight = 15, .weight = 0.4f, .babyChance = 0.10f, .groupSize = 4,
     .stepHeight = 0.0f, .jumpHeight = 0.0f, .floats = false, .sinks = false,
     .breathesWater = true, .swims = true, .spawnsInWater = true, .maxSpawnY = kSeaLevel,
     .maxLoaded = 4, .walksOnLand = false, .dryOutSeconds = 120.0f, .angerSeconds = 25.0f,
     .alertRange = 16.0f},
    // Amphibious like the turtle, and the first species with a livery: the
    // reference rolls four of its five colours at 25% each and keeps the fifth
    // for breeding, which we have none of, so all five are equally likely.
    // Underwater it is twice the speed it manages on land - 0.2 against 0.1 -
    // and it dries out in five minutes ashore.
    {.name = "Axolotl", .halfWidth = 0.32f, .height = 0.42f, .gaitRate = 9.0f, .gaitSwing = 0.30f,
     .modelScale = 1.00f, .health = 14, .walkSpeed = 1.7f, .runSpeed = 3.4f, .senseRange = 8.0f,
     .attackDamage = 2, .maxBlockLight = 15, .weight = 0.4f, .babyChance = 0.10f, .groupSize = 3,
     .stepHeight = 0.6f, .jumpHeight = 0.0f, .floats = false, .sinks = false,
     .breathesWater = true, .swims = true, .spawnsInWater = true, .maxSpawnY = kSeaLevel,
     .maxLoaded = 5, .dryOutSeconds = 300.0f, .variantCount = 5,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.2f},
    // Two body shapes crossed with six patterns. The reference gets its
    // twenty-two named fish by tinting a base and an overlay at runtime; ours
    // draws the overlay as a second cutout shell instead, because runtime
    // tinting is exactly what produced the washed-out grass at M19a.
    {.name = "Tropical Fish", .halfWidth = 0.20f, .height = 0.40f, .gaitRate = 0.0f,
     .modelScale = 1.00f, .health = 3, .walkSpeed = 1.8f, .runSpeed = 3.6f, .senseRange = 6.0f,
     .maxBlockLight = 15, .weight = 0.7f, .groupSize = 5, .stepHeight = 0.0f, .jumpHeight = 0.0f,
     .floats = false, .sinks = false, .breathesWater = true, .swims = true, .breathesAir = false,
     .spawnsInWater = true, .maxSpawnY = kSeaLevel, .maxLoaded = 8, .walksOnLand = false,
     .variantCount = 2 * kTropicalPatterns, .alertRange = kHerdAlertRange,
     .panicSpeedScale = 1.5f},
    // --- Tier 1. Six of the ten are an existing rig plus a skin, which is what
    // --- makes them a row here rather than a model: the geometry each one uses
    // --- was read out of `Mojang/bedrock-samples`' own entity files.

    // The cow's rig and the cow's stats. The reference confines it to one
    // biome we do not have, so it spawns wherever a cow does and is simply
    // scarce - a named divergence rather than an oversight.
    {.name = "Mushroom Cow", .halfWidth = 0.45f, .height = 1.50f, .gaitRate = 5.5f,
     .gaitSwing = 0.36f, .modelScale = 1.00f, .health = 10, .walkSpeed = 1.3f, .runSpeed = 2.8f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.18f, .babyChance = 0.05f,
     .groupSize = 2, .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Both undead horses are `geometry.horse` unchanged. They are **passive**,
    // as the reference has them, and neither burns off - so a pale horse
    // standing in a field at noon is correct rather than a bug.
    {.name = "Skeleton Horse", .halfWidth = 0.45f, .height = 1.60f, .gaitRate = 5.0f,
     .gaitSwing = 0.49f, .modelScale = 1.00f, .health = 15, .walkSpeed = 2.0f, .runSpeed = 4.5f,
     .senseRange = 10.0f, .nocturnal = true, .maxBlockLight = 7, .weight = 0.15f,
     .burnsInDay = false, .stepHeight = 1.0f, .jumpHeight = 0.0f, .floats = false,
     .amphibious = true, .breathesWater = true},
    {.name = "Zombie Horse", .halfWidth = 0.45f, .height = 1.60f, .gaitRate = 5.0f,
     .gaitSwing = 0.49f, .modelScale = 1.00f, .health = 15, .walkSpeed = 1.8f, .runSpeed = 4.0f,
     .senseRange = 10.0f, .nocturnal = true, .maxBlockLight = 7, .weight = 0.15f,
     .burnsInDay = false, .stepHeight = 1.0f, .jumpHeight = 0.0f, .floats = false,
     .amphibious = true, .breathesWater = true},
    // The llama's rig with its pack drawn over it. Travels in smaller groups
    // than a wild llama, which is the whole idea of a trader's string.
    {.name = "Trader Llama", .halfWidth = 0.43f, .height = 1.87f, .gaitRate = 5.5f,
     .gaitSwing = 0.31f, .modelScale = 1.00f, .health = 12, .walkSpeed = 1.6f, .runSpeed = 3.5f,
     .senseRange = 10.0f, .maxBlockLight = 15, .weight = 0.2f, .babyChance = 0.10f,
     .groupSize = 2, .stepHeight = 1.0f, .jumpHeight = 0.0f, .alertRange = kHerdAlertRange},
    // The Princepin's rig at both ends of its family. The brute is the
    // reference's own outlier - fifty health and seven damage, and it never
    // calms down - while the zombified one is **neutral**, which needs no flag
    // at all: not hostile with damage above zero is already "ignores you until
    // struck", and its thirty-second grudge is the reference's `angry` duration.
    {.name = "Princepin Brute", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 1.00f, .health = 50, .walkSpeed = 1.2f, .runSpeed = 2.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 7, .maxBlockLight = 15,
     .weight = 0.12f, .burnsInDay = false, .floats = false, .avoidsWater = true,
     .leashRange = 64.0f, .angerSeconds = 600.0f, .alertRange = 16.0f, .swingsArms = true},
    {.name = "Zombie Princepin", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f,
     .senseRange = 16.0f, .attackDamage = 5, .nocturnal = true, .maxBlockLight = 7,
     .weight = 0.35f, .burnsInDay = false, .floats = false, .amphibious = true,
     .breathesWater = true, .angerSeconds = 30.0f, .alertRange = 20.0f, .swingsArms = true},
    // The silverfish's cousin, and it keeps the one thing that makes a
    // silverfish frightening: no line of sight needed.
    {.name = "Voidmite", .halfWidth = 0.20f, .height = 0.30f, .gaitRate = 11.0f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.4f, .runSpeed = 2.8f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 4,
     .weight = 0.5f, .burnsInDay = false, .mustSee = false, .angerSeconds = 600.0f},
    // Three sizes that split, exactly as the slimes do - the machinery is
    // already there and none of it needed touching. The launch is shared for
    // the same reason theirs is: the reference jumps one block whatever the
    // size, and only the ground covered scales.
    {.name = "Magma Cube", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.75f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.8f, .runSpeed = 1.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 3, .maxBlockLight = 15,
     .weight = 0.35f, .burnsInDay = false, .avoidsWater = true},
    {.name = "Magma Cube", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.70f, .modelScale = 2.08f, .health = 4, .walkSpeed = 1.1f, .runSpeed = 2.2f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 4, .maxBlockLight = 15,
     .weight = 0.3f, .burnsInDay = false, .splitInto = CreatureKind::MagmaCubeSmall,
     .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    {.name = "Magma Cube", .halfWidth = 0.75f, .height = 2.08f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.65f, .modelScale = 4.16f, .health = 16, .walkSpeed = 1.5f, .runSpeed = 3.0f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 6, .maxBlockLight = 15,
     .weight = 0.2f, .burnsInDay = false, .splitInto = CreatureKind::MagmaCubeMedium,
     .splitMin = 2, .splitMax = 4, .avoidsWater = true},
};

static_assert(std::size(kSpecies) == static_cast<std::size_t>(CreatureKind::Count),
              "every CreatureKind needs a row");

Aabb bodyBox(const CreatureSpecies& species, const glm::vec3& feet, float scale) {
    const float half = species.halfWidth * scale;
    return Aabb{{feet.x - half, feet.y, feet.z - half},
                {feet.x + half, feet.y + species.height * scale, feet.z + half}};
}

/// Reads `collisionBoxes` through the shared helper, exactly as the player
/// does. Testing whole cells here instead left creatures hovering half a block
/// over every slab and stair, and refused them a fence's cell entirely.
bool bodyOverlapsSolid(const World& world, const CreatureSpecies& species, const glm::vec3& feet,
                       float scale = 1.0f) {
    return overlapsSolid(world, bodyBox(species, feet, scale));
}

/// Ground within a step's reach below, so a heading that walks into open air is
/// rejected before it is taken rather than after the fall.
bool hasFooting(const World& world, const glm::vec3& feet) {
    const int x = static_cast<int>(std::floor(feet.x));
    const int z = static_cast<int>(std::floor(feet.z));
    const int y = static_cast<int>(std::floor(feet.y));
    const int lowest = y - static_cast<int>(kMaxDropHeight);
    for (int probe = y - 1; probe >= lowest; --probe) {
        if (world.isSolid(x, probe, z)) {
            return true;
        }
    }
    return false;
}

/// What the route planner is allowed to walk through, built from the species
/// row rather than written down twice.
///
/// **`climbHeight` is deliberately the same `max(stepHeight, jumpHeight)` the
/// steering fan probes with.** A planner and the legs that follow it have to
/// agree on what counts as passable - the fan learned that once already, when
/// rejecting anything blocked at foot height sent creatures around every rise
/// and the jump could never fire.
path::Agent pathAgentFor(const CreatureSpecies& species, float scale) {
    return path::Agent{.halfWidth = species.halfWidth * scale,
                       .height = species.height * scale,
                       .climbHeight = std::max(species.stepHeight, species.jumpHeight),
                       .maxDrop = kMaxDropHeight,
                       .avoidsWater = species.avoidsWater,
                       // The undead walk the bottom, so water is a road rather
                       // than a hazard to route around.
                       .amphibious = species.amphibious || species.breathesWater};
}

/// xorshift32: small, fast, and good enough for wandering. Creatures are not
/// part of generation, so this needs no determinism guarantee.
float nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/// A cell somewhere inside the water column over the seabed, or -1 where there
/// is no room for one.
///
/// A swimmer wants the *middle* of the water, not the floor of it - which is
/// all `highestSolid` can offer, and is why the surface-based spawn path cannot
/// serve one. Two cells of water are required so the body is genuinely under
/// rather than bobbing in the one wet block of a shoreline.
int waterColumnY(const World& world, int x, int z, float roll) {    const int bed = world.highestSolid(x, z);
    if (bed < 0) {
        return -1;
    }
    int top = bed + 1;
    while (top <= kSeaLevel + 1 && isWater(world.blockAt(x, top, z))) {
        ++top;
    }
    const int lowest = bed + 1;
    const int highest = top - 2;
    if (highest < lowest) {
        return -1;
    }
    return std::min(lowest + static_cast<int>(roll * static_cast<float>(highest - lowest + 1)),
                    highest);
}

/// Whether the water here is deep enough to live in.
///
/// Measured over the whole column between the bed and the surface rather than
/// around the candidate, because a fish one block off the floor of a deep sea
/// is in open water and the same fish in a two-deep dip is not.
bool inOpenWater(const World& world, int x, int z) {
    const int bed = world.highestSolid(x, z);
    if (bed < 0) {
        return false;
    }
    int top = bed + 1;
    while (top <= kSeaLevel + 1 && isWater(world.blockAt(x, top, z))) {
        ++top;
    }
    return top - (bed + 1) >= kOpenWaterDepth;
}

/// Picks somewhere to amble to, and how long to spend on it.
void pickWanderGoal(Creature& creature, std::uint32_t& random) {
    creature.walking = nextRandom(random) > 0.35f;
    creature.decisionTimer =
        creature.walking ? 2.0f + nextRandom(random) * 4.0f : 1.5f + nextRandom(random) * 4.5f;
    if (creature.walking) {
        creature.targetYaw = nextRandom(random) * kTwoPi;
        // A place as well as a bearing, because only a place can be pathed to.
        // The reference picks a spot within ten blocks and paths to that.
        const float reach = kWanderRange * (0.4f + nextRandom(random) * 0.6f);
        creature.wanderGoal =
            creature.position +
            glm::vec3{std::sin(creature.targetYaw), 0.0f, std::cos(creature.targetYaw)} * reach;
        creature.route.clear();
    }
}

/// The local planner: turns the creature toward the first walkable heading near
/// the one it wants. Returns false when every candidate is blocked.
bool steerAround(const World& world, const Creature& creature, float desiredYaw, float& chosenYaw) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    // Straight on first, then alternating to either side. Trying the desired
    // heading before its neighbours is what makes this hug an obstacle's edge
    // instead of orbiting it.
    for (int step = 0; step <= kSteerFanSteps; ++step) {
        for (int sign = 0; sign < (step == 0 ? 1 : 2); ++sign) {
            const float offset = static_cast<float>(step) * kSteerFanStep * (sign == 0 ? 1.0f : -1.0f);
            const float yaw = desiredYaw + offset;
            const glm::vec3 ahead =
                creature.position + glm::vec3{std::sin(yaw), 0.0f, std::cos(yaw)} * kProbeDistance;

            glm::vec3 landing = ahead;
            if (bodyOverlapsSolid(world, species, landing, creature.scale)) {
                // Blocked at foot height. A rise it could step onto is still
                // passable - and so is one it could **jump** onto, which is the
                // part that matters: reject those here and the fan simply
                // steers around every hill, the creature never walks into the
                // rise, and the jump below never gets a chance to fire.
                landing.y += std::max(species.stepHeight, species.jumpHeight);
                if (bodyOverlapsSolid(world, species, landing, creature.scale)) {
                    continue;
                }
            }
            // Open air below is a fall and gets rejected - unless it is already
            // swimming, in which case there is no floor to want and demanding
            // one would pin it in the middle of the lake.
            if (!creature.inWater && !hasFooting(world, landing)) {
                continue;
            }
            // `avoid_water` is a pathing preference and nothing more: it routes
            // around a pond it can see, and is perfectly capable of being
            // knocked into one. Most farm animals have it; a chicken, a wolf, a
            // llama and a polar bear do not.
            if (species.avoidsWater &&
                isWater(world.blockAt(static_cast<int>(std::floor(landing.x)),
                                      static_cast<int>(std::floor(landing.y)),
                                      static_cast<int>(std::floor(landing.z))))) {
                continue;
            }
            chosenYaw = yaw;
            return true;
        }
    }
    return false;
}

/// The swimmer's planner. The same fan as `steerAround`, looking for a
/// different thing: a heading is good when the body still fits *and* the cell
/// it lands in is water. There is no floor to want and no rise to step onto,
/// and asking for either would pin a fish in the middle of the sea.
bool steerInWater(const World& world, const Creature& creature, float desiredYaw, float& chosenYaw) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    for (int step = 0; step <= kSteerFanSteps; ++step) {
        for (int sign = 0; sign < (step == 0 ? 1 : 2); ++sign) {
            const float offset = static_cast<float>(step) * kSteerFanStep * (sign == 0 ? 1.0f : -1.0f);
            const float yaw = desiredYaw + offset;
            const glm::vec3 ahead =
                creature.position + glm::vec3{std::sin(yaw), 0.0f, std::cos(yaw)} * kSwimProbe;
            if (bodyOverlapsSolid(world, species, ahead, creature.scale)) {
                continue;
            }
            const float mid = species.height * creature.scale * 0.5f;
            if (!isWater(world.blockAt(static_cast<int>(std::floor(ahead.x)),
                                       static_cast<int>(std::floor(ahead.y + mid)),
                                       static_cast<int>(std::floor(ahead.z))))) {
                continue;
            }
            chosenYaw = yaw;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Behaviours//
// Bedrock's shape rather than Bedrock's format: a behaviour is a priority, a
// set of controllers it claims, and three function pointers. There is no JSON,
// no component groups and no filter language - the table is `constexpr`, so the
// compiler checks it and it costs nothing to load. `RESEARCH.md` §8.7 records
// why each of those was skipped.
// ---------------------------------------------------------------------------

/// Which controller a behaviour holds while it runs.
///
/// **This is the whole difference between a behaviour system and a state
/// machine.** Two behaviours claiming *disjoint* flags run at the same time;
/// two claiming the *same* flag are exclusive and the higher priority wins. It
/// is what lets a creature walk somewhere and watch something else at once
/// without either behaviour knowing the other exists.
///
/// A behaviour claiming *nothing* always runs when it can. That is not a
/// degenerate case - it is how a targeting behaviour works, since writing down
/// what to attack takes no controller at all.
enum ControlFlag : std::uint8_t {
    ControlMove = 1,
    ControlLook = 2,
    ControlJump = 4,
};

/// Everything a behaviour is allowed to see, gathered once per creature per
/// tick. One struct rather than nine arguments, which is what keeps every
/// behaviour's signature identical enough to sit in a table.
struct BehaviourContext {
    const World& world;
    Creature& self;
    const CreatureSpecies& species;
    float deltaSeconds;

    /// Offset to the player, its horizontal length, and the bearing along it.
    /// Computed once because three behaviours want them.
    glm::vec3 toPlayer;
    float distance;
    float yawToPlayer;
    /// Eye to eye, so a creature looks at the player's face rather than their
    /// feet - which at close range is the difference between attention and a
    /// stare at the floor.
    float pitchToPlayer;

    /// Whether this species' `huntsBelowLight` is satisfied where it stands.
    /// A per-frame fact about the world, so it is measured before any
    /// behaviour runs rather than by whichever one happens to ask first.
    bool huntsHere;

    /// Whether the player is in sight. Measured once per tick for the same
    /// reason, and only for a species that has any use for it - it costs a ray
    /// through the world, and asking it twice in one frame from two predicates
    /// would pay for it twice.
    bool seesPlayer;

    /// Whether the player is crouched. Detection range is cut for a sneaking
    /// target, but **only while acquiring** - see `nearestTargetContinue`.
    bool playerSneaking;

    /// Bearing to the nearest thing this species runs from, and whether there
    /// is one at all. Measured once per tick alongside everything else, and
    /// only for a species that actually fears something.
    bool feared;
    float yawFromFeared;

    /// Where a blow on the player is reported. Behaviours never touch the
    /// player themselves.
    CreatureAttack& attack;
    std::uint32_t& random;

    /// The one route planner, and what is left of this frame's search budget.
    /// Shared across the population rather than held per creature, so that one
    /// animal boxed into a maze cannot cost a frame on its own - the same
    /// reasoning as the per-frame budgets on light and water.
    path::Pathfinder& pathfinder;
    int& pathBudget;
};

/// One row of the behaviour table.
struct Behaviour {
    const char* name;
    /// **Lower is higher**, matching Bedrock, where 0 is both the top priority
    /// and the default.
    std::uint8_t priority;
    /// `ControlFlag` mask. Zero means it claims nothing and can never conflict.
    std::uint8_t flags;

    bool (*canStart)(const BehaviourContext&);
    void (*tick)(const BehaviourContext&);
    /// Why a behaviour keeps running is usually a looser question than why it
    /// started - a hunter gives up further out than it engages. `nullptr` means
    /// the two tests are the same.
    bool (*canContinue)(const BehaviourContext&) = nullptr;
};

/// Point the body at a heading, routing around whatever is in the way. Returns
/// false when the planner found nowhere to go at all.
bool walkToward(const BehaviourContext& ctx, float desiredYaw) {
    ctx.self.walking = true;
    float routed = desiredYaw;
    if (!steerAround(ctx.world, ctx.self, desiredYaw, routed)) {
        return false;
    }
    ctx.self.targetYaw = routed;
    return true;
}

/// The bearing from a creature to a place, in this codebase's yaw convention.
float yawTo(const Creature& self, const glm::vec3& goal) {
    return std::atan2(goal.x - self.position.x, goal.z - self.position.z);
}

float flatDistance(const glm::vec3& a, const glm::vec3& b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

/// Walk to a **place**, following a searched route rather than a bearing.
///
/// **Falling back to `walkToward` is what made this safe to roll out**: a
/// creature with no route steers at its goal exactly as it always did, so every
/// case the search declines to answer behaves as it did before. The route only
/// ever replaces *which way to face this instant*; the step-up, the jump, the
/// water avoidance and the final 0.9 m of obstacle-hugging are all still the
/// steering fan's, and none of them changed.
bool walkTo(const BehaviourContext& ctx, const glm::vec3& goal) {
    Creature& self = ctx.self;

    // A swimmer has its own planner and moves in three dimensions; this one
    // walks on floors.
    if (ctx.species.swims) {
        return walkToward(ctx, yawTo(self, goal));
    }

    // Close enough that a route would be two waypoints of noise.
    if (flatDistance(self.position, goal) <= kWaypointReached * 2.0f) {
        self.route.clear();
        return walkToward(ctx, yawTo(self, goal));
    }

    // No headway for a while means the route is asking for something the legs
    // cannot do. Throw it away rather than grinding into the same block.
    self.progressTimer += ctx.deltaSeconds;
    if (self.progressTimer >= kProgressWindow) {
        if (flatDistance(self.position, self.progressFrom) < kProgressDistance) {
            self.route.clear();
            self.repathTimer = 0.0f;
        }
        self.progressFrom = self.position;
        self.progressTimer = 0.0f;
    }

    self.repathTimer = std::max(0.0f, self.repathTimer - ctx.deltaSeconds);
    // Two reasons to want a new route, and **both are gated behind the same
    // cadence**. Without that gate a chaser dancing in and out of contact
    // clears its route on the frame it arrives and searches again on the next
    // one, forever. Waiting simply falls back to steering in the meantime,
    // which is what this did before there were routes at all.
    const bool wantNew =
        self.route.finished() || flatDistance(goal, self.routeGoal) > kRepathMoved;
    if (wantNew && self.repathTimer <= 0.0f && ctx.pathBudget > 0) {
        --ctx.pathBudget;
        self.repathTimer = kRepathSeconds;
        self.routeGoal = goal;
        if (!ctx.pathfinder.find(ctx.world, pathAgentFor(ctx.species, self.scale), self.position,
                                 goal, self.route)) {
            self.route.clear();
        }
    }

    // Advance past everything already reached. Walking through two waypoints in
    // one frame is normal at speed, and doubling back to touch one would read
    // as a stutter.
    while (!self.route.finished() &&
           flatDistance(self.position, path::waypoint(self.route, self.route.next)) <=
               kWaypointReached) {
        ++self.route.next;
    }

    if (self.route.finished()) {
        return walkToward(ctx, yawTo(self, goal));
    }
    return walkToward(ctx, yawTo(self, path::waypoint(self.route, self.route.next)));
}

// --- Producers: these write the target slot and claim no controller at all, so
// --- they can never be crowded out by something that is merely moving.

/// Being struck, or seeing one of its own kind struck. The reference's
/// `hurt_by_target`; `alertNeighbours` is its `alert_same_type` flag.
///
/// A species that cannot bite gets nothing from this and panics instead, which
/// is what keeps the wolf and the sheep on the same table with no flag telling
/// them apart - `attackDamage` already does.
bool hurtByTargetStart(const BehaviourContext& ctx) {
    return ctx.self.provokedTimer > 0.0f && ctx.species.attackDamage > 0;
}

void hurtByTargetTick(const BehaviourContext& ctx) {
    ctx.self.target = CreatureTarget::Player;
}

/// Hunting on sight. `huntsBelowLight` is now simply part of this predicate
/// rather than a special case in the mood logic - a spider is a hunter wherever
/// the light is low enough and an ordinary neutral everywhere else.
///
/// Three of the reference's rules land here at once.
///
/// **It only asks twice a second.** Bedrock scans on a `scan_interval` of ten
/// ticks, and that is what the old distance hysteresis was really standing in
/// for: without it, something sitting exactly on the sense boundary is noticed
/// and forgotten on alternate frames.
///
/// **It needs to see you.** `must_see` is set on every hostile in the shipped
/// data, and we did not have it at all outside the Bramble's fuse - so a zombie
/// on the far side of a wall knew exactly where you were.
///
/// **A crouched target is noticed later.** The floor matters as much as the
/// multiplier: nothing hides at point-blank range however carefully it moves.
bool nearestTargetStart(const BehaviourContext& ctx) {
    if (!(ctx.species.hostile || ctx.huntsHere) || ctx.self.scanTimer > 0.0f) {
        return false;
    }
    const float range = ctx.playerSneaking
                            ? std::max(ctx.species.senseRange * kSneakDetection, kMinDetection)
                            : ctx.species.senseRange;
    return ctx.distance < range && (!ctx.species.mustSee || ctx.seesPlayer);
}

/// Why it keeps coming is a different question from why it set off, and the
/// reference answers it with two different numbers rather than one widened one.
///
/// **Distance is the leash, not the sense range**, and the two are not ordered
/// the way instinct says: a zombie notices you at 35 m and loses interest at
/// 25. **Sight becomes a memory** - `must_see_forget_duration`, three seconds
/// for almost everything and seventeen for a zombie - so ducking behind a tree
/// does not shake anything off instantly.
///
/// **Being roused outranks the reason it started.** That is what makes the
/// spider's light rule survive contact with a torch: it acquires you in the
/// dark, `nearestTargetTick` sets the grudge, and brightening the room no
/// longer calls it off until the grudge runs out.
///
/// **Sneaking is deliberately not applied here.** The reference only uses the
/// modifier while a mob is acquiring, so crouching keeps you from being noticed
/// and does nothing at all once you have been - which is the difference between
/// stealth and an escape button.
bool nearestTargetContinue(const BehaviourContext& ctx) {
    if (!(ctx.species.hostile || ctx.huntsHere || ctx.self.provokedTimer > 0.0f)) {
        return false;
    }
    const float leash =
        ctx.species.leashRange > 0.0f ? ctx.species.leashRange : ctx.species.senseRange;
    return ctx.distance < leash && (!ctx.species.mustSee || ctx.self.forgetTimer > 0.0f);
}

void nearestTargetTick(const BehaviourContext& ctx) {
    ctx.self.target = CreatureTarget::Player;
    // A neutral that turns hunter by circumstance stays roused for a while
    // afterwards, which is the reference's `spider_angry`. A species that is
    // hostile outright needs nothing: it never stops being a reason on its own.
    if (ctx.huntsHere && !ctx.species.hostile) {
        ctx.self.provokedTimer = std::max(ctx.self.provokedTimer, ctx.species.angerSeconds);
    }
}

// --- Consumers and locomotion.

/// The fuse. Bedrock's `behavior.swell`: inside 3 m with an unbroken line of
/// sight it stops dead and starts counting; past 7 m, or the instant it loses
/// sight of you, it gives up and the countdown reverses.
///
/// It claims **both** controllers, which is what keeps anything else from
/// steering it, and it sits above `MeleeAttack` so an exploder that is about to
/// go off stops trying to bite you.
bool swellStart(const BehaviourContext& ctx) {
    return ctx.species.explodePower > 0.0f && ctx.self.target != CreatureTarget::None &&
           ctx.distance <= ctx.species.swellStartRange && ctx.seesPlayer;
}

/// Looser than starting, and deliberately so: it will keep counting out to the
/// wider cancel range, which is what makes backing away a real defence rather
/// than a step to one side.
bool swellContinue(const BehaviourContext& ctx) {
    return ctx.species.explodePower > 0.0f && ctx.self.target != CreatureTarget::None &&
           ctx.distance <= ctx.species.swellStopRange && ctx.seesPlayer;
}

void swellTick(const BehaviourContext& ctx) {
    ctx.self.swelling = true;
    // Claiming the movement controller stops anything else *steering* it, but
    // `walking` is sticky - it survives from whichever behaviour set it last -
    // so without this a swelling creeper kept running at the player at full
    // speed. The reference stops dead, and the stopping is most of what makes
    // the fuse readable.
    ctx.self.walking = false;
    ctx.self.targetHeadYaw = ctx.yawToPlayer;
    ctx.self.targetHeadPitch = ctx.pitchToPlayer;
    ctx.self.fuseTimer += ctx.deltaSeconds;
}

/// Bolting. Prey only: anything that can bite fights back instead.
bool panicStart(const BehaviourContext& ctx) {
    return ctx.self.provokedTimer > 0.0f && !ctx.species.hostile && ctx.species.attackDamage <= 0;
}

void panicTick(const BehaviourContext& ctx) {
    ctx.self.running = true;
    ctx.self.speedScale = ctx.species.panicSpeedScale;
    walkToward(ctx, ctx.yawToPlayer + 3.14159265f);
}

/// Closes on whatever the producers picked and bites it. It never asks *why*
/// there is a target, which is exactly why retaliation, pack anger and hunting
/// on sight all reach it through the same slot.
bool meleeAttackStart(const BehaviourContext& ctx) {
    return ctx.self.target != CreatureTarget::None;
}

void meleeAttackTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    self.running = true;
    self.speedScale = ctx.species.chaseSpeedScale;
    // It holds the look controller as well as the movement one, so nothing
    // lower down can pull its gaze off what it is chasing.
    self.targetHeadYaw = ctx.yawToPlayer;
    self.targetHeadPitch = ctx.pitchToPlayer;

    const float halfWidth = ctx.species.halfWidth * self.scale;
    // **It closes until the two bodies meet, and then stands there and
    // swings.** The reference paths to a node beside its target and the path
    // simply ends, which is what a walker arriving at you looks like.
    //
    // Without that it kept walking for as long as it had a target. Nothing
    // here collides with the player, so it walked *through*, overshot, and then
    // had to come all the way about - which at a limited turn rate is a circle,
    // with the player on the inside of it taking a hit on every pass.
    //
    // A hopper is exempt, and that is the reference's own split rather than a
    // convenience: a slime has no melee goal at all, it has `slime_attack` and
    // an `area_attack` that hurts whatever it lands on, so arriving on top of
    // you *is* its attack and stopping short would leave it sitting in front of
    // you doing nothing.
    //
    // So is anything whose target is not on its own level. Two bodies at
    // different heights have not met, and standing still there would mean a
    // zombie at the foot of a one-block ledge never climbing it - the step-up
    // and the jump both read `walking`.
    const float contact = halfWidth + player_constants::kWidth * 0.5f;
    // Arriving means arriving on the same **footing**. Anything more than a
    // step up or down is something to climb rather than something reached, and
    // both the step-up and the jump read `walking` - so a zombie at the foot of
    // a one-block ledge has to keep walking into it or it stands there forever.
    const bool arrived =
        ctx.distance <= contact && std::abs(ctx.toPlayer.y) <= ctx.species.stepHeight;
    if (arrived && !ctx.species.hops) {
        // `walking` is sticky - it survives from whichever behaviour set it
        // last - so standing still has to be said outright. Claiming the
        // movement controller only stops anything else *steering*. This is the
        // trap the Bramble's fuse already fell into once.
        self.walking = false;
        self.targetYaw = ctx.yawToPlayer;
        self.route.clear();
    } else {
        // A searched route rather than a bearing, which is the whole of why a
        // wall is now something to walk around rather than something to press
        // against until the player happens to come back into the open.
        walkTo(ctx, self.position + ctx.toPlayer);
    }

    const float length = std::max(ctx.distance, 0.001f);
    const glm::vec3 out{-ctx.toPlayer.x / length, 0.0f, -ctx.toPlayer.z / length};

    // Whether the two bodies genuinely overlap, which is a different question
    // from having arrived and is **not symmetric**: the player is 1.8 m tall,
    // so a slime sailing over their boots is still hitting them.
    const bool touching = ctx.distance <= contact &&
                          ctx.toPlayer.y < ctx.species.height * self.scale &&
                          ctx.toPlayer.y > -player_constants::kHeight;

    // A hopper rebounds instead of stopping, because it has nowhere to stop:
    // arriving on you *is* its attack. So the player is a wall, and the part of
    // its velocity heading into that wall comes back out - which is what turns
    // "it keeps jumping into me" into a real cycle of hit, recoil, gather,
    // come again.
    //
    // **Contact, not the blow.** A small slime does no damage at all in the
    // reference, and a wall does not care: it would look wrong for the big ones
    // to bounce and the small one to pass straight through.
    //
    // It fires at most once per approach without needing a timer, because after
    // it the slime is heading away and `inward` is negative.
    if (ctx.species.hops && touching) {
        const float inward = -(self.velocity.x * out.x + self.velocity.z * out.z);
        if (inward > 0.0f) {
            // Whatever it had sideways is kept; only the inward part reverses.
            const float tangentX = self.velocity.x + out.x * inward;
            const float tangentZ = self.velocity.z + out.z * inward;
            self.velocity.x = tangentX + out.x * inward * kReboundRestitution;
            self.velocity.z = tangentZ + out.z * inward * kReboundRestitution;
            // Assigned as the stronger of the two, never accumulated - several
            // sources each adding their own push is the oldest bug in here.
            self.velocity.y =
                std::max(self.velocity.y, ctx.species.hopLaunch * kReboundLift);
            // It has just been knocked off the floor, and saying so is what
            // makes the rebound survive: the hop branch zeroes horizontal
            // velocity on any creature it still believes is standing, so a
            // bounce landed at ground level would come out as a hop straight
            // up with the recoil thrown away.
            self.onGround = false;
        }
    }

    // Reach is wider than contact, deliberately: it is this creature's own box
    // grown horizontally, plus the target's half-width, because what the
    // reference tests is whether the two boxes overlap once one of them has
    // been inflated. So a blow still lands on someone who has just been knocked
    // back out of contact. A flat number gave every animal on the roster the
    // same bite.
    const float reach = halfWidth + kMeleeHorizontalReach + player_constants::kWidth * 0.5f;
    // And it has to be facing you. `melee_fov` is 90 degrees in the reference,
    // which turns walking round a creature into a real half-second of grace
    // while it comes about rather than a cosmetic detail.
    const float offAxis = std::abs(std::remainder(ctx.yawToPlayer - self.yaw, kTwoPi));

    if (ctx.species.attackDamage <= 0 || ctx.distance >= reach || self.attackTimer > 0.0f ||
        offAxis > kMeleeHalfFov || std::abs(ctx.toPlayer.y) >= ctx.species.height) {
        return;
    }
    self.attackTimer = kAttackInterval;
    self.swingTimer = kAttackSwingSeconds;
    ctx.attack.damage += ctx.species.attackDamage;

    const glm::vec3 push = -out * kKnockbackSpeed + glm::vec3{0.0f, kKnockbackLift, 0.0f};
    // Only the hardest blow moves you. Summing them lets a pack launch the
    // player clear across the world in one frame, which is what emptied the map
    // on the first night the hostiles worked.
    if (!ctx.attack.landed || glm::dot(push, push) > glm::dot(ctx.attack.push, ctx.attack.push)) {
        ctx.attack.push = push;
    }
    ctx.attack.landed = true;
}

bool wanderStart(const BehaviourContext&) {
    return true;
}
void wanderTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    if (self.decisionTimer <= 0.0f) {
        pickWanderGoal(self, ctx.random);
    }
    if (!self.walking) {
        return;
    }
    if (!walkTo(ctx, self.wanderGoal)) {
        // Boxed in with nowhere sensible to go: stand still and try again
        // shortly rather than grind into the wall for the rest of the timer.
        self.walking = false;
        self.decisionTimer = 0.5f;
        self.route.clear();
    }
}

/// An occasional glance at the player, claiming the **look controller alone**.
///
/// This row exists as much to prove the arbitration as to be a feature: it sits
/// below `Wander` in priority and claims a controller `Wander` does not, so the
/// two must run *together*. A creature that walks and watches at the same time
/// means disjoint flags really do coexist; one that stops walking means they do
/// not. It is also, as it happens, the single change that most makes an animal
/// look alive.
bool lookAtPlayerStart(const BehaviourContext& ctx) {
    return ctx.distance <= kLookDistance &&
           nextRandom(ctx.random) < kLookChancePerSecond * ctx.deltaSeconds;
}

bool lookAtPlayerContinue(const BehaviourContext& ctx) {
    return ctx.self.lookTimer > 0.0f && ctx.distance <= kLookDistance;
}

void lookAtPlayerTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    if (self.lookTimer <= 0.0f) {
        self.lookTimer =
            kLookSecondsMin + nextRandom(ctx.random) * (kLookSecondsMax - kLookSecondsMin);
    }
    self.lookTimer -= ctx.deltaSeconds;
    self.targetHeadYaw = ctx.yawToPlayer;
    self.targetHeadPitch = ctx.pitchToPlayer;
}

/// Cruising, for anything that swims. The reference splits this across three
/// goals - `random_swim` picks a point 16 out and up to 4 above or below,
/// `swim_wander` nudges the heading, `swim_idle` does nothing - and one row
/// covers all three here, because the only thing separating them is how far
/// ahead the point is and nothing here stores a point at all. Rolling the dive
/// *angle* instead reproduces the same 14-degree spread for one float.
///
/// It outranks `Wander` and claims the same controller, so a swimmer never runs
/// the walking version and a walker never runs this one.
bool swimWanderStart(const BehaviourContext& ctx) {
    return ctx.species.swims;
}

void swimWanderTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    if (!self.inWater) {
        // Out of its element there is nothing to steer: `step` flops it, and a
        // heading would only make it slide.
        self.walking = false;
        self.targetPitch = 0.0f;
        return;
    }

    if (self.decisionTimer <= 0.0f) {
        self.decisionTimer = 2.0f + nextRandom(ctx.random) * 4.0f;
        self.targetYaw = nextRandom(ctx.random) * kTwoPi;
        self.targetPitch = (nextRandom(ctx.random) * 2.0f - 1.0f) * kSwimPitchMax;
    }

    self.walking = true;
    float routed = self.targetYaw;
    if (!steerInWater(ctx.world, self, self.targetYaw, routed)) {
        // Boxed in. Turn about and decide again immediately rather than grind
        // into the seabed for the rest of the timer.
        self.targetYaw += 3.14159265f;
        self.decisionTimer = 0.0f;
        return;
    }
    self.targetYaw = routed;

    // The surface and the seabed are both walls to a fish, and neither is
    // something the horizontal fan can see. Positive pitch is nose-down, so
    // refusing to rise is a floor under the angle and refusing to dive is a
    // ceiling over it.
    const int hx = static_cast<int>(std::floor(self.position.x));
    const int hz = static_cast<int>(std::floor(self.position.z));
    const float top = self.position.y + ctx.species.height * self.scale;
    if (!isWater(ctx.world.blockAt(hx, static_cast<int>(std::floor(top + kSwimProbe)), hz))) {
        self.targetPitch = std::max(self.targetPitch, 0.0f);
    }
    if (ctx.world.isSolid(hx, static_cast<int>(std::floor(self.position.y - kSwimProbe)), hz)) {
        self.targetPitch = std::min(self.targetPitch, 0.0f);
    }
}

/// A pufferfish inflating. It claims **no controller at all**, because swelling
/// is a state rather than a way of moving - so it runs alongside whatever is
/// steering, exactly as the targeting rows do.
bool puffStart(const BehaviourContext& ctx) {
    return ctx.species.puffs;
}

void puffTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    // Two ranges rather than one, which is the reference's own arrangement: it
    // starts inflating at 2.5 m and only lets go once nothing has been within
    // 2.9 m for three seconds. A single threshold would flicker for anyone
    // circling at exactly that distance.
    if (ctx.distance <= kPuffRange) {
        self.puff = std::min(self.puff + ctx.deltaSeconds / kPuffStageSeconds, kPuffMax);
    } else if (ctx.distance > kPuffHoldRange) {
        self.puff = std::max(self.puff - ctx.deltaSeconds / kPuffDeflateSeconds, 0.0f);
    }
}

/// Running from a cat. The reference's `avoid_mob_type`, and the one behaviour
/// on the table that is about another creature rather than the player.
///
/// **Priority 3 is load-bearing.** It sits *below* `Swell`, so a fuse already
/// lit is not called off by a cat wandering past - the reference's own rule -
/// and *above* `MeleeAttack`, so an unlit creeper would rather flee than fight.
bool avoidFelineStart(const BehaviourContext& ctx) {
    return ctx.species.avoidFelineRange > 0.0f && ctx.feared;
}

void avoidFelineTick(const BehaviourContext& ctx) {
    ctx.self.running = true;
    ctx.self.speedScale = kAvoidSpeedScale;
    ctx.self.targetHeadYaw = ctx.yawFromFeared + 3.14159265f;
    walkToward(ctx, ctx.yawFromFeared);
}

/// The table. One list shared by every species: the differences between animals
/// are already in `kSpecies`, and the predicates read them, so a per-species
/// list would be a second place for a species to disagree with itself.
///
/// **Must stay sorted by priority** - the selector walks it in order and relies
/// on having already seen everything that outranks the row it is looking at.
constexpr Behaviour kBehaviours[] = {
    {"Panic", 1, ControlMove, panicStart, panicTick},
    {"HurtByTarget", 1, 0, hurtByTargetStart, hurtByTargetTick},
    {"NearestAttackableTarget", 2, 0, nearestTargetStart, nearestTargetTick, nearestTargetContinue},
    // Ties with the row above on purpose, and must stay below it: it reads the
    // target slot that one writes, and within a tie the array order decides.
    {"Swell", 2, ControlMove | ControlLook, swellStart, swellTick, swellContinue},
    // Claims nothing, like the targeting rows: a pufferfish inflates while it
    // goes on swimming, and neither behaviour needs to know about the other.
    {"Puff", 2, 0, puffStart, puffTick},
    {"AvoidFeline", 3, ControlMove | ControlLook, avoidFelineStart, avoidFelineTick},
    {"MeleeAttack", 4, ControlMove | ControlLook, meleeAttackStart, meleeAttackTick},
    // Above `Wander` and claiming the same controller, which is what keeps the
    // two mutually exclusive: a swimmer never ambles and a walker never cruises.
    {"SwimWander", 5, ControlMove, swimWanderStart, swimWanderTick},
    {"Wander", 6, ControlMove, wanderStart, wanderTick},
    {"LookAtPlayer", 7, ControlLook, lookAtPlayerStart, lookAtPlayerTick, lookAtPlayerContinue},
};

static_assert(std::size(kBehaviours) <= 16, "runningBehaviours is a 16-bit mask");

constexpr bool behavioursSorted() {
    for (std::size_t i = 1; i < std::size(kBehaviours); ++i) {
        if (kBehaviours[i].priority < kBehaviours[i - 1].priority) {
            return false;
        }
    }
    return true;
}
static_assert(behavioursSorted(), "kBehaviours must be ordered by priority, lowest number first");

/// Chooses which behaviours run this tick and runs them.
///
/// The whole of the arbitration, and it is deliberately short. Walk the table
/// from the highest priority down; a row runs if it can and if nothing above it
/// already claimed a controller it needs. Rows claiming *nothing* never
/// conflict, so a targeting behaviour always gets its say - which is what keeps
/// "decide what to attack" and "attack it" from competing.
void runBehaviours(const BehaviourContext& ctx) {
    std::uint8_t claimed = 0;
    std::uint16_t running = 0;

    for (std::size_t i = 0; i < std::size(kBehaviours); ++i) {
        const Behaviour& behaviour = kBehaviours[i];
        if (behaviour.flags & claimed) {
            continue;
        }

        const auto bit = static_cast<std::uint16_t>(1u << i);
        const bool wasRunning = (ctx.self.runningBehaviours & bit) != 0;
        const bool allowed = (wasRunning && behaviour.canContinue != nullptr)
                                 ? behaviour.canContinue(ctx)
                                 : behaviour.canStart(ctx);
        if (!allowed) {
            continue;
        }

        claimed |= behaviour.flags;
        running |= bit;
        behaviour.tick(ctx);
    }

    ctx.self.runningBehaviours = running;
}

} // namespace

const CreatureSpecies& speciesInfo(CreatureKind kind) {
    return kSpecies[static_cast<std::size_t>(kind)];
}

static_assert(kSpawnEggItems == static_cast<int>(CreatureKind::Count),
              "one spawn egg sprite layer per species, in the same order - across both runs");

const char* spawnEggName(ItemId item) {
    // Built once and handed out as stable pointers, because a tooltip keeps the
    // pointer rather than copying the text.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> built;
        built.reserve(static_cast<std::size_t>(CreatureKind::Count));
        for (int i = 0; i < static_cast<int>(CreatureKind::Count); ++i) {
            built.emplace_back(std::string{kSpecies[i].name} + " Spawn Egg");
        }
        return built;
    }();

    const int index = spawnEggIndex(item);
    return index >= 0 ? names[static_cast<std::size_t>(index)].c_str() : "Spawn Egg";
}

bool spawnsIn(CreatureKind kind, BiomeId biome) {
    switch (kind) {
    case CreatureKind::Sheep:
        // Grass, warm open ground, and the scrub at a desert's edge - a land
        // biome with nothing grazing it by day reads as broken rather than
        // harsh.
        return biome == BiomeId::Plains || biome == BiomeId::Beach || biome == BiomeId::Desert;
    case CreatureKind::Cow:
        // Wants real grazing, so it keeps off the sand.
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::Pig:
        return biome == BiomeId::Plains || biome == BiomeId::Beach;
    case CreatureKind::Bramble:
        // Anywhere it can stand. Darkness is the limit that matters, not region.
        return biome != BiomeId::Ocean;
    case CreatureKind::Chicken:
        return biome == BiomeId::Plains || biome == BiomeId::Beach || biome == BiomeId::Rocky;
    case CreatureKind::Cat:
        return biome == BiomeId::Plains || biome == BiomeId::Beach || biome == BiomeId::Desert;
    case CreatureKind::Camel:
        return biome == BiomeId::Desert || biome == BiomeId::Beach;
    case CreatureKind::Horse:
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::Mule:
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains;
    case CreatureKind::Llama:
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains || biome == BiomeId::SnowyPeaks;
    case CreatureKind::Donkey:
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::Goat:
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains || biome == BiomeId::SnowyPeaks;
    case CreatureKind::Rabbit:
        return biome == BiomeId::Plains || biome == BiomeId::Beach || biome == BiomeId::Desert ||
               biome == BiomeId::SnowyPeaks;
    case CreatureKind::Wolf:
        // The reference puts wolves in taiga and forest, neither of which exists
        // here yet. Upland and cold ground is the nearest thing we have.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains ||
               biome == BiomeId::SnowyPeaks || biome == BiomeId::Plains;
    case CreatureKind::Frog:
        // Wants swamp, which does not exist yet either; damp low ground is the
        // closest stand-in until it does.
        return biome == BiomeId::Beach || biome == BiomeId::Plains;
    case CreatureKind::Fox:
        // Reference range is taiga and forest. Trees only grow on plains and
        // rocky ground here, and the snow line is the nearest thing to a taiga.
        return biome == BiomeId::Plains || biome == BiomeId::Rocky || biome == BiomeId::SnowyPeaks;
    case CreatureKind::Ocelot:
        // Jungle in the reference, and there is none - warm lowland is the
        // closest we have.
        return biome == BiomeId::Plains || biome == BiomeId::Beach;
    case CreatureKind::PolarBear:
        return biome == BiomeId::SnowyPeaks || biome == BiomeId::Mountains;
    case CreatureKind::Panda:
        // Also a jungle animal with nowhere to live yet. Kept off the beach so
        // it does not share every spawn with the ocelot.
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::SlimeSmall:
    case CreatureKind::SlimeMedium:
    case CreatureKind::SlimeLarge:
        // The reference wants swamp, which we do not have. Damp low ground is
        // the nearest thing, same stand-in the frog already uses.
        return biome == BiomeId::Plains || biome == BiomeId::Beach;
    case CreatureKind::Spider:
    case CreatureKind::CaveSpider:
        // Anywhere it can stand. Darkness is the limit that matters.
        return biome != BiomeId::Ocean;
    case CreatureKind::Zombie:
    case CreatureKind::Skeleton:
        return biome != BiomeId::Ocean;
    case CreatureKind::Villager:
        // Villages do not exist yet, so it simply lives on open grassland.
        return biome == BiomeId::Plains;
    case CreatureKind::Husk:
        // The reference's desert zombie, and the only hostile that is still
        // about at midday - which is what makes the desert feel different.
        return biome == BiomeId::Desert;
    case CreatureKind::Silverfish:
        // Stone-dwelling in the reference, where it hides inside blocks. We
        // have no infested block, so the stony regions stand in for it.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains ||
               biome == BiomeId::SnowyPeaks;
    case CreatureKind::Blackbone:
        return biome != BiomeId::Ocean;
    case CreatureKind::Stray:
        // The reference wants snowy ground, and here that is the high cold.
        return biome == BiomeId::SnowyPeaks || biome == BiomeId::Mountains;
    case CreatureKind::Bogged:
        // Swamp in the reference, which we do not have - damp low ground is the
        // same stand-in the frog and the slimes already use.
        return biome == BiomeId::Plains || biome == BiomeId::Beach;
    case CreatureKind::ZombieVillager:
        // Wherever a zombie turns one, which is anywhere it can stand.
        return biome != BiomeId::Ocean;
    case CreatureKind::Witch:
        return biome != BiomeId::Ocean;
    case CreatureKind::WanderingTrader:
        // A traveller rather than a resident, so nowhere is its home and
        // everywhere is its route.
        return biome != BiomeId::Ocean;
    case CreatureKind::Princepin:
        // The Nether in the reference, which we do not have. Barren stony
        // ground is the nearest thing, and keeping it off the plains is what
        // stops a daylight hostile making the whole surface unsafe.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains;
    // The only three that want the ocean, which until now nothing did.
    case CreatureKind::Drowned:
    case CreatureKind::Cod:
        return biome == BiomeId::Ocean;
    case CreatureKind::Salmon:
    case CreatureKind::Pufferfish:
    case CreatureKind::Squid:
    case CreatureKind::GlowSquid:
    case CreatureKind::Dolphin:
    case CreatureKind::TropicalFish:
        return biome == BiomeId::Ocean;
    case CreatureKind::Turtle:
        // The reference hatches them on beach sand, and so do we - it is the
        // one aquatic here that starts its life out of the water.
        return biome == BiomeId::Beach;
    case CreatureKind::Axolotl:
        // Lush caves in the reference, which we have none of. Ocean water is
        // the honest stand-in until underwater caves exist.
        return biome == BiomeId::Ocean;
    case CreatureKind::MushroomCow:
        // The reference confines it to a mushroom biome we do not have, so it
        // grazes where a cow does and is simply scarce.
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::SkeletonHorse:
    case CreatureKind::ZombieHorse:
        // The reference only produces these from a lightning trap, which needs
        // weather. Until then they are rare night grazers on open ground.
        return biome == BiomeId::Plains || biome == BiomeId::Rocky;
    case CreatureKind::TraderLlama:
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains ||
               biome == BiomeId::SnowyPeaks;
    case CreatureKind::PrincepinBrute:
    case CreatureKind::ZombiePrincepin:
        // Same barren stand-in for the Nether the Princepin already uses.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains;
    case CreatureKind::Voidmite:
        // The reference spawns it from an enderman's teleport, which does not
        // exist here. It keeps the silverfish's stony home instead.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains ||
               biome == BiomeId::SnowyPeaks;
    case CreatureKind::MagmaCubeSmall:
    case CreatureKind::MagmaCubeMedium:
    case CreatureKind::MagmaCubeLarge:
        // Nether again, so the same barren stand-in - and being underground and
        // dark is what actually gates them.
        return biome == BiomeId::Rocky || biome == BiomeId::Mountains;
    case CreatureKind::Count:
        break;
    }
    return false;
}

Creatures::Creatures(std::uint32_t seed) : m_random(seed | 1u) {}

float Creatures::random01() {
    return nextRandom(m_random);
}

/// Which of a species' skins an individual wears. Zero for the thirty-eight
/// that have only one, so the roll costs nothing where it means nothing.
std::uint8_t Creatures::rollVariant(CreatureKind kind) {
    const int count = speciesInfo(kind).variantCount;
    if (count <= 1) {
        return 0;
    }
    return static_cast<std::uint8_t>(
        std::min(static_cast<int>(random01() * static_cast<float>(count)), count - 1));
}

std::size_t Creatures::hunting() const {
    std::size_t count = 0;
    for (const Creature& creature : m_creatures) {
        if (creature.target != CreatureTarget::None) {
            ++count;
        }
    }
    return count;
}

std::array<std::size_t, static_cast<std::size_t>(CreatureKind::Count)> Creatures::census() const {
    std::array<std::size_t, static_cast<std::size_t>(CreatureKind::Count)> counts{};
    for (const Creature& creature : m_creatures) {
        ++counts[static_cast<std::size_t>(creature.kind)];
    }
    return counts;
}

bool Creatures::canSpawnAt(const World& world, const CreatureSpecies& species, int x, int y, int z) {
    // **The body's own box, through the shared helper.** This used to scan a
    // column of whole cells `ceil(height)` tall, which is a second copy of what
    // `collisionBoxes` already owns - and it could not see a slab, a fence, or a
    // body wider than the cell it stands in, so anything bulky was dropped
    // straight into the terrain beside it and had no way back out.
    //
    // Measured at full size even for a baby: an animal that grows into its box
    // should not find itself inside a wall when it does.
    const glm::vec3 feet{static_cast<float>(x) + 0.5f, static_cast<float>(y),
                         static_cast<float>(z) + 0.5f};
    if (overlapsSolid(world, bodyBox(species, feet, 1.0f))) {
        return false;
    }

    const bool wet = isWater(world.blockAt(x, y, z));
    // Bedrock's `height_filter`, which is the reference's only expression of
    // depth: its ocean fish sit anywhere at or below the surface and its glow
    // squid well under it.
    if (species.maxSpawnY > 0 && y > species.maxSpawnY) {
        return false;
    }
    if (species.swims) {
        // Nothing under it and nothing above it matters - only that there is
        // enough water to be inside of. Asking for the cell above as well is
        // what stops a fish appearing in the single wet block of a shoreline.
        return wet && isWater(world.blockAt(x, y + 1, z)) && inOpenWater(world, x, z);
    }
    if (species.spawnsInWater) {
        return wet && world.isSolid(x, y - 1, z) && inOpenWater(world, x, z);
    }
    if (wet) {
        return false;
    }

    if (!world.isSolid(x, y - 1, z)) {
        return false;
    }
    const BlockId ground = world.blockAt(x, y - 1, z);
    return ground == BlockId::Grass || ground == BlockId::Sand || ground == BlockId::Snow ||
           ground == BlockId::Stone || ground == BlockId::Gravel;
}

void Creatures::think(const World& world, Creature& creature, const glm::vec3& playerFeet,
                      float deltaSeconds, bool night, bool playerSneaking, CreatureAttack& attack,
                      std::vector<CreatureExplosion>& blasts) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    creature.hurtTimer = std::max(0.0f, creature.hurtTimer - deltaSeconds);
    creature.attackTimer = std::max(0.0f, creature.attackTimer - deltaSeconds);
    creature.swingTimer = std::max(0.0f, creature.swingTimer - deltaSeconds);
    creature.provokedTimer = std::max(0.0f, creature.provokedTimer - deltaSeconds);
    creature.scanTimer = std::max(0.0f, creature.scanTimer - deltaSeconds);
    creature.decisionTimer -= deltaSeconds;

    const glm::vec3 toPlayer = playerFeet - creature.position;
    const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
    const float yawToPlayer = std::atan2(toPlayer.x, toPlayer.z);
    // Negated because a positive head pitch is nose-down, so a player standing
    // above wants a negative one.
    const float pitchToPlayer =
        -std::atan2(toPlayer.y + player_constants::kEyeHeight - species.height * 0.85f,
                    std::max(distance, 0.001f));

    // Stored sky light does not fall at night - the shader applies the time of
    // day - so darkness has to be asked for as "night, and no lamp reaching
    // here". Getting that wrong would leave a spider permanently placid under
    // an open sky, which is the one place it most needs to hunt.
    bool huntsHere = false;
    if (species.huntsBelowLight > 0) {
        const int cx = static_cast<int>(std::floor(creature.position.x));
        const int cy = static_cast<int>(std::floor(creature.position.y + species.height * 0.5f));
        const int cz = static_cast<int>(std::floor(creature.position.z));
        const int block = world.blockLightAt(cx, cy, cz);
        const int sky = night ? 0 : world.skyLightAt(cx, cy, cz);
        huntsHere = std::max(block, sky) <= species.huntsBelowLight;
    }

    // Sight, eye to eye, so lying behind a one-block wall genuinely hides you.
    //
    // Measured once here rather than inside the predicates that want it: it
    // costs a ray through the world and several predicates ask for it. **It is
    // no longer only the exploders that pay** - `must_see` is set on every
    // hostile in the reference, and until now nothing but the fuse read it, so
    // a zombie behind a wall tracked you through it.
    //
    // How often the ray is cast is the interesting part. An exploder needs an
    // answer every tick, because its fuse reverses the instant the line breaks.
    // Everything else only needs one on the tick it scans, and the answer holds
    // until the next scan - which is exactly Bedrock's own sampling, and takes
    // the cost from one ray per creature per frame to two per second.
    creature.sightTimer = std::max(0.0f, creature.sightTimer - deltaSeconds);
    const float sightRange = std::max(species.senseRange, species.leashRange);
    const bool caresAboutSight =
        species.hostile || species.huntsBelowLight > 0 || species.attackDamage > 0;
    if (caresAboutSight && distance <= sightRange) {
        const bool recheck = species.explodePower > 0.0f || creature.scanTimer <= 0.0f ||
                             creature.forgetTimer > 0.0f;
        const glm::vec3 eye = creature.position + glm::vec3{0.0f, species.height * 0.85f, 0.0f};
        const glm::vec3 playerEye = playerFeet + glm::vec3{0.0f, player_constants::kEyeHeight, 0.0f};
        if (recheck && hasLineOfSight(world, eye, playerEye)) {
            creature.sightTimer = species.explodePower > 0.0f ? kSightGrace : kTargetScanSeconds;
        }
    } else {
        creature.sightTimer = 0.0f;
    }
    const bool seesPlayer = creature.sightTimer > 0.0f;

    // And the longer memory on top of it. Sight answers "can it see you now";
    // this answers "how long ago did it last", which is the question a chase
    // actually turns on.
    creature.forgetTimer = seesPlayer ? species.forgetSeconds
                                      : std::max(0.0f, creature.forgetTimer - deltaSeconds);

    // The nearest thing this species runs from. Quadratic across the whole
    // population, which is why it is guarded by the range being non-zero at
    // all - only the Bramble pays for it.
    bool feared = false;
    float yawFromFeared = 0.0f;
    if (species.avoidFelineRange > 0.0f) {
        float nearest = species.avoidFelineRange * species.avoidFelineRange;
        for (const Creature& other : m_creatures) {
            if (other.kind != CreatureKind::Cat && other.kind != CreatureKind::Ocelot) {
                continue;
            }
            const glm::vec3 offset = other.position - creature.position;
            const float flat = offset.x * offset.x + offset.z * offset.z;
            if (flat >= nearest) {
                continue;
            }
            nearest = flat;
            feared = true;
            // Straight away from it, which is what the heading is for.
            yawFromFeared = std::atan2(-offset.x, -offset.z);
        }
    }

    // Both are re-derived from nothing every tick, so neither can go stale: a
    // target survives only as long as some producer keeps asserting it, and the
    // run is only claimed by whichever behaviour actually holds the movement
    // controller this frame.
    creature.target = CreatureTarget::None;
    creature.running = false;
    creature.speedScale = 1.0f;
    creature.swelling = false;
    creature.targetHeadYaw = creature.yaw;
    creature.targetHeadPitch = 0.0f;

    const BehaviourContext context{world,     creature,  species,        deltaSeconds,
                                   toPlayer,  distance,  yawToPlayer,    pitchToPlayer,
                                   huntsHere,
                                   seesPlayer,           playerSneaking, feared,
                                   yawFromFeared,        attack,         m_random,
                                   m_pathfinder,         m_pathBudget};
    runBehaviours(context);

    // Rearmed after the table has run, not before, so the acquisition predicate
    // gets to see the tick on which it expired. A scan that finds nothing costs
    // the same wait as one that finds you, which is the reference's behaviour
    // and is what keeps the cadence steady.
    if (creature.scanTimer <= 0.0f) {
        creature.scanTimer = kTargetScanSeconds;
    }

    // The fuse runs back down whenever nothing is holding it, which is what
    // makes breaking line of sight or stepping back an actual defence rather
    // than a pause. Detonating is reported rather than done: rewriting terrain
    // is the caller's business, not a creature's.
    if (!creature.swelling) {
        creature.fuseTimer = std::max(0.0f, creature.fuseTimer - deltaSeconds * kDeflateRate);
    } else if (creature.fuseTimer >= species.fuseSeconds) {
        const float power =
            species.explodePower * (creature.charged ? kChargedPowerScale : 1.0f);
        blasts.push_back(CreatureExplosion{creature.position, power});
        // Retired by the next `manage`, the same way anything killed is.
        creature.health = 0;
        creature.fuseTimer = 0.0f;
        creature.swelling = false;
    }

    // The head is eased on its own clock and then clamped to what a neck can
    // manage. Doing it here rather than in `step` is deliberate: watching
    // something is a decision, and it must keep happening even when the ground
    // underneath has streamed out and the physics has stopped.
    const float headTurn = std::remainder(creature.targetHeadYaw - creature.headYaw, kTwoPi);
    creature.headYaw +=
        std::clamp(headTurn, -kHeadTurnRate * deltaSeconds, kHeadTurnRate * deltaSeconds);
    creature.headYaw = creature.yaw + std::clamp(std::remainder(creature.headYaw - creature.yaw, kTwoPi),
                                                 -kMaxHeadTurn, kMaxHeadTurn);

    // Pitch needs no wrapping and no body-follow, so it is the same easing
    // without the bookkeeping.
    const float headTilt = creature.targetHeadPitch - creature.headPitch;
    creature.headPitch +=
        std::clamp(headTilt, -kHeadTurnRate * deltaSeconds, kHeadTurnRate * deltaSeconds);
    creature.headPitch = std::clamp(creature.headPitch, -kMaxHeadPitch, kMaxHeadPitch);
}

void Creatures::step(const World& world, Creature& creature, float deltaSeconds) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    // Clamped exactly as the player's is. The resolver snaps out of at most one
    // block, so a frame long enough to fall further than that lands the body on
    // the wrong side of the floor - and a chunk-loading hitch is long enough.
    deltaSeconds = std::min(deltaSeconds, 0.05f);

    // Ground it cannot see is ground it would fall through: an absent chunk
    // reads as air, so simulating over one drops the creature out of the world
    // and it reappears buried when the chunk returns. Standing still until the
    // terrain is back is the only safe answer.
    if (!world.columnResident(static_cast<int>(std::floor(creature.position.x)),
                              static_cast<int>(std::floor(creature.position.z)))) {
        creature.velocity = glm::vec3{0.0f};
        return;
    }

    // A body inside terrain climbs out rather than staying there forever.
    // Nothing *should* put one there, but a shove from a herd, a spawn over
    // ground that had not finished loading, or a player filling in the block it
    // was standing in all can - and once inside, every move it tries overlaps
    // something, so it is stuck for good. The reference has its own push-out
    // procedure for exactly this (`RESEARCH.md` §1.6).
    if (bodyOverlapsSolid(world, species, creature.position, creature.scale)) {
        for (float lift = kUnstickStep; lift <= kUnstickReach; lift += kUnstickStep) {
            glm::vec3 freed = creature.position;
            freed.y += lift;
            if (!bodyOverlapsSolid(world, species, freed, creature.scale)) {
                creature.position = freed;
                break;
            }
        }
        // Whatever it was doing, it was not doing it from in there.
        creature.velocity = glm::vec3{0.0f};
        creature.onGround = false;
    }

    // **Floating is a behaviour, not buoyancy.** Bedrock gives almost every
    // land animal `minecraft:behavior.float`, which swims up while its head is
    // under - so a cow bobs, and the undead, which have no such goal, walk the
    // bottom. Ours applies it continuously rather than rolling the reference's
    // 0.8 per tick; the bob still comes out, because clearing the surface is
    // what stops it.
    const glm::vec3 eye = creature.position + glm::vec3{0.0f, species.height * creature.scale, 0.0f};
    const bool headUnder = isWater(world.blockAt(static_cast<int>(std::floor(eye.x)),
                                                 static_cast<int>(std::floor(eye.y - 0.1f)),
                                                 static_cast<int>(std::floor(eye.z))));
    const fluid::FluidContact water =
        fluid::sampleFluid(world, bodyBox(species, creature.position, creature.scale));
    creature.inWater = water.inWater;

    if (creature.inWater) {
        // Water **replaces** gravity rather than being applied after it. Doing
        // both leaves the settling speed offset by `g dt k / (1 - k)`, which at
        // 120 fps is over 5 m/s - so a cow would have sunk like a stone while
        // every constant still read correct.
        const float terminal = !species.sinks                ? 0.0f
                               : (species.floats && headUnder) ? fluid::kSwimUpSpeed
                                                               : -fluid::kSinkSpeed;
        creature.velocity.y =
            fluid::approach(creature.velocity.y, terminal, fluid::kWaterDrag, deltaSeconds);
        creature.fallDistance = 0.0f;
    } else {
        creature.velocity.y =
            std::max(creature.velocity.y - kGravity * deltaSeconds, -kTerminalVelocity);
    }

    // Breath. Every land animal on the roster carries the reference's 15-second
    // supply and takes two health points a second once it runs out; the undead
    // and the frog breathe water and never start the clock.
    //
    // A fish runs the **same** counter for being out of water. Bedrock spells
    // that as `breathes_air: false` on the very component that carries
    // `breathes_water`, so it is one mechanism read from either end rather than
    // a second system for drying out.
    const bool suffocating = (headUnder && !species.breathesWater) ||
                             (!creature.inWater && !species.breathesAir);
    if (suffocating) {
        creature.air = std::max(0.0f, creature.air - deltaSeconds);
        if (creature.air <= 0.0f) {
            creature.drownTimer += deltaSeconds;
            while (creature.drownTimer >= fluid::kDrownInterval) {
                creature.drownTimer -= fluid::kDrownInterval;
                creature.health -= fluid::kDrownDamage;
                creature.hurtTimer = kHurtSeconds;
            }
        }
    } else {
        creature.air =
            std::min(fluid::kAirSeconds,
                     creature.air + deltaSeconds * fluid::kAirSeconds / fluid::kInhaleSeconds);
        creature.drownTimer = 0.0f;
    }

    // Drying out, which is a separate clock running the other way. A dolphin
    // does both: held under it would drown, kept out it dries, and the two have
    // nothing to do with each other beyond sharing an animal.
    if (species.dryOutSeconds > 0.0f) {
        if (creature.inWater) {
            creature.dryTimer = 0.0f;
        } else {
            creature.dryTimer += deltaSeconds;
            if (creature.dryTimer >= species.dryOutSeconds) {
                creature.dryTimer -= 1.0f;
                creature.health -= 1;
                creature.hurtTimer = kHurtSeconds;
            }
        }
    }

    // A chicken flaps and drifts down instead of dropping. The reference scales
    // a descent by 0.6 every tick, which settles at 1.95 m/s against the 60
    // everything else reaches - and it is why one never needs fall damage
    // exempting, because it simply never lands hard.
    //
    // The factor is re-solved for our timestep rather than used verbatim.
    // `v = (v - g dt) k` settles at `g dt k / (1 - k)`, so applying 0.6 per
    // *frame* would land somewhere else entirely - 2.55 m/s at 120 fps. Working
    // backwards from the speed the reference's own constant produces keeps the
    // descent identical at any frame rate, and comes out at exactly 0.6 when a
    // frame happens to be a tick.
    if (species.fallDrag < 1.0f && !creature.inWater && !creature.onGround && creature.velocity.y < 0.0f) {
        const float terminal = kGravity * 0.05f * species.fallDrag / (1.0f - species.fallDrag);
        creature.velocity.y *= terminal / (terminal + kGravity * deltaSeconds);
    }

    // Turning is eased, so a new heading reads as the animal turning to face it.
    // A jetting creature holds its heading while it is actually pushing: it
    // moves along the way it points, so steering mid-squeeze would slew it
    // sideways instead of making it turn about and go again.
    if (!species.jets || creature.jetPower <= kJetCommitted) {
        const float turn = std::remainder(creature.targetYaw - creature.yaw, kTwoPi);
        creature.yaw += std::clamp(turn, -kTurnRate * deltaSeconds, kTurnRate * deltaSeconds);
    }

    // And the body's own tilt, which only a swimmer ever asks for. Its own rate
    // because a fish banks into a climb rather than pivoting into one.
    const float tilt = creature.targetPitch - creature.pitch;
    creature.pitch += std::clamp(tilt, -kSwimTurnRate * deltaSeconds, kSwimTurnRate * deltaSeconds);

    const bool running = creature.running;
    const float speed = (running ? species.runSpeed : species.walkSpeed) * creature.speedScale *
                        (creature.scale < 1.0f ? kBabySpeedBonus : 1.0f);

    // Whether something is genuinely in the way, asked by probing ahead rather
    // than by checking which axes moved.
    //
    // **The axis test does not work and it is worth saying why**: a creature
    // walking along a wall is blocked on one axis and slides freely on the
    // other, so "neither axis moved" is false and it slides forever instead of
    // climbing. Worse, a heading near an axis makes one component tiny, and a
    // tiny step rarely overlaps anything, so that axis reports success while
    // the creature is pressed flat against a block.
    bool wallAhead = false;
    bool stepBlocked = false;
    bool jumpClear = false;
    if (creature.walking) {
        const glm::vec3 heading{std::sin(creature.yaw), 0.0f, std::cos(creature.yaw)};
        const glm::vec3 foot = creature.position + heading * kJumpProbe;
        wallAhead = bodyOverlapsSolid(world, species, foot, creature.scale);
        if (wallAhead) {
            glm::vec3 probe = foot;
            probe.y = foot.y + species.stepHeight;
            stepBlocked = bodyOverlapsSolid(world, species, probe, creature.scale);
            probe.y = foot.y + species.jumpHeight;
            jumpClear = !bodyOverlapsSolid(world, species, probe, creature.scale);
        }
    }
    const bool mustJump = wallAhead && stepBlocked && jumpClear && species.jumpHeight > 0.0f;

    glm::vec3 wish{0.0f};
    if (species.swims && creature.inWater) {
            // Where it points is where it goes, in all three axes. Bedrock says
            // this three times over - `physics.has_gravity: false`,
            // `can_sink: false` and `can_walk: false` - and one flag covers all
            // three: no buoyancy to fight and no floor to want.
            float drive = creature.walking ? speed : 0.0f;

            if (species.jets) {
                // Seeded on the first tick rather than at construction, so a
                // group of them is out of phase with itself.
                if (creature.age <= 0.0f) {
                    creature.jetPhase = random01() * kTwoPi;
                }
                creature.jetPhase += kJetRate * deltaSeconds;
                if (creature.jetPhase > kTwoPi) {
                    creature.jetPhase -= kTwoPi;
                }
                const float half = kTwoPi * 0.5f;
                const float ticks = deltaSeconds / 0.05f;
                const bool closing =
                    creature.jetPhase < half && creature.jetPhase / half > kJetThrustFrom;
                if (closing) {
                    creature.jetPower = 1.0f;
                } else {
                    creature.jetPower *= std::pow(
                        creature.jetPhase < half ? kJetOpenDecay : kJetCoastDecay, ticks);
                }
                // The push and the pose are the same number, so they can never
                // drift apart into a squid that glides while its arms flap.
                drive *= creature.jetPower;
            }

            wish = glm::vec3{std::sin(creature.yaw), 0.0f, std::cos(creature.yaw)} *
                   (drive * std::cos(creature.pitch));
            creature.velocity.y = -std::sin(creature.pitch) * drive;
    } else if (species.swims && !species.walksOnLand) {
            // Stranded, and it cannot walk. So it flops: a shove and a new
            // heading on a fixed cadence, going nowhere in particular, which is
            // the point of it. Anything amphibious falls past this and walks.
            if (creature.onGround) {
                creature.velocity.x = 0.0f;
                creature.velocity.z = 0.0f;
                creature.hopTimer -= deltaSeconds;
                if (creature.hopTimer <= 0.0f) {
                    creature.hopTimer = kFlopInterval;
                    creature.velocity.y = kFlopLaunch;
                    creature.yaw = random01() * kTwoPi;
                    creature.targetYaw = creature.yaw;
                    creature.velocity.x = std::sin(creature.yaw) * species.walkSpeed;
                    creature.velocity.z = std::cos(creature.yaw) * species.walkSpeed;
                }
            }
            wish = glm::vec3{creature.velocity.x, 0.0f, creature.velocity.z};
    } else if (species.hops && !creature.inWater) {
        // A hop is a real jump rather than a bob: the launch sets a velocity and
        // the world's gravity brings it back down, so the arc, the airtime and
        // the distance covered all fall out of the physics. All the ground it
        // makes is made in the air, so the burst has to be faster than the
        // species' speed by exactly the fraction of time it spends landed.
        if (creature.onGround && creature.hurtTimer <= 0.0f) {
            creature.velocity.x = 0.0f;
            creature.velocity.z = 0.0f;
            creature.hopTimer -= deltaSeconds;
            if (creature.walking && creature.hopTimer <= 0.0f) {
                // Bedrock shortens the wait between hops the moment a slime has
                // something to chase - `jump_delay` goes from 0.5-1.5 s to
                // 0.16-0.5. Worked out once and used for both the cadence and
                // the burst, or the two disagree about how much ground a hop
                // has to cover.
                const float gather =
                    species.hopGather *
                    (creature.target != CreatureTarget::None ? kChaseHopGather : 1.0f);
                const float airSeconds = 2.0f * species.hopLaunch / kGravity;
                const float burst = speed * (airSeconds + gather) / airSeconds;
                // A rabbit's ordinary hop reaches about a third of a metre, so
                // left alone it nudges into every ledge it meets forever. When
                // one is in the way it launches hard enough to clear it
                // instead - the same height a walker's jump reaches, and the
                // only place a hopper's arc is ever anything but its own.
                creature.velocity.y =
                    mustJump ? std::max(species.hopLaunch,
                                        std::sqrt(2.0f * kGravity * species.jumpHeight))
                             : species.hopLaunch;
                creature.velocity.x = std::sin(creature.yaw) * burst;
                creature.velocity.z = std::cos(creature.yaw) * burst;
                creature.hopTimer = gather;
            }
        }
        // Nothing steers mid-air, which is the point.
        wish = glm::vec3{creature.velocity.x, 0.0f, creature.velocity.z};
    } else if (creature.walking) {
        wish = glm::vec3{std::sin(creature.yaw), 0.0f, std::cos(creature.yaw)} * speed;
    }
    // A shove from being hit overrides its own intent while it lasts.
    if (creature.hurtTimer > 0.0f) {
        wish = glm::vec3{creature.velocity.x, 0.0f, creature.velocity.z};
    }

    if (creature.inWater) {
        // A land animal is hobbled by water; a fish is not. `kSwimSpeedRatio` is
        // the penalty for swimming badly, and something whose entire movement
        // model *is* swimming should not be paying it.
        if (!species.swims) {
            wish *= fluid::kSwimSpeedRatio;
        }
        // A flowing cell carries whatever is standing in it, walking or not -
        // which is the whole of why a river is a river and not a long puddle.
        wish += water.flow * fluid::kCurrentSpeed;
    }

    // A jetting body lines itself up with where it is going. Taken from the
    // movement vector rather than from the heading, which is what makes a squid
    // hang upright when it is drifting and lie right over when it is swimming -
    // bell first, tentacles trailing. `atan2` of the two speeds is the
    // reference's own expression, and it gives every case for free: nothing
    // moving reads as upright, flat out reads as a right angle, and straight
    // down reads as a half turn.
    if (species.jets) {
        const float flat = std::sqrt(wish.x * wish.x + wish.z * wish.z);
        const float wanted = std::atan2(flat, creature.velocity.y);
        creature.bodyTilt += (wanted - creature.bodyTilt) *
                             (1.0f - std::pow(kBodyTiltLagPerTick, deltaSeconds / 0.05f));
    }

    const glm::vec3 before = creature.position;

    // One axis at a time, vertical first, exactly as the player resolves - so a
    // corner cannot push it diagonally through a wall.
    creature.position.y += creature.velocity.y * deltaSeconds;
    creature.onGround = false;
    if (creature.velocity.y < 0.0f) {
        creature.fallDistance -= creature.velocity.y * deltaSeconds;
    }
    if (bodyOverlapsSolid(world, species, creature.position, creature.scale)) {
        if (creature.velocity.y < 0.0f) {
            // A slab's top is halfway up its cell, so the resting height comes
            // from the shape table rather than from the cell boundary. Checked
            // before it is taken: the surface found is at or below where the
            // body already was, and a lower position can meet something the
            // higher one cleared.
            const float surface = highestSurfaceBelow(
                world, bodyBox(species, creature.position, creature.scale), before.y);
            const float landed = std::isfinite(surface) ? surface + kCollisionSkin : before.y;
            glm::vec3 settled = creature.position;
            settled.y = landed;
            creature.position.y =
                bodyOverlapsSolid(world, species, settled, creature.scale) ? before.y : landed;
            creature.onGround = true;

            // A hard landing lights most of an exploder's fuse. The reference
            // buys 1.5 ticks per block and stops five short of the end, so a
            // creeper dropped on your head goes off almost at once - but never
            // instantly, and only if it is swelling anyway.
            if (species.explodePower > 0.0f && creature.fallDistance > 0.5f) {
                const float ceiling =
                    std::max(0.0f, species.fuseSeconds - kFallFuseHeadroomTicks / kTicksPerSecond);
                const float bought =
                    creature.fallDistance * kFallFuseTicksPerBlock / kTicksPerSecond;
                creature.fuseTimer = std::min(std::max(creature.fuseTimer, bought), ceiling);
            }
            creature.fallDistance = 0.0f;
        } else {
            creature.position.y = before.y;
        }
        creature.velocity.y = 0.0f;
    }

    const auto tryMove = [&](float dx, float dz) {
        const glm::vec3 previous = creature.position;
        creature.position.x += dx;
        creature.position.z += dz;
        if (!bodyOverlapsSolid(world, species, creature.position, creature.scale)) {
            return true;
        }
        // Blocked. A rise within its step height is walked straight up, with no
        // jump and no airtime - which is why a horse flows over a ledge that a
        // chicken has to hop.
        creature.position = previous;
        if (!creature.onGround) {
            return false;
        }
        glm::vec3 raised = previous;
        raised.y += species.stepHeight;
        raised.x += dx;
        raised.z += dz;
        if (bodyOverlapsSolid(world, species, raised, creature.scale)) {
            return false;
        }

        // Rise only as far as the thing actually being stepped onto. Lifting by
        // the whole step height overshoots every slab and stair by the
        // difference and then drops back under gravity, which is a stutter on
        // every single step - and for a camel, whose step height is a block and
        // a half, it is a launch.
        Aabb reach = bodyBox(species, raised, creature.scale);
        reach.min.y = previous.y;
        const float surface = highestSurfaceBelow(world, reach, raised.y);
        if (std::isfinite(surface)) {
            glm::vec3 settled = raised;
            settled.y = std::max(previous.y, surface + kCollisionSkin);
            if (!bodyOverlapsSolid(world, species, settled, creature.scale)) {
                raised = settled;
            }
        }

        creature.position = raised;
        creature.stepSmooth =
            std::min(creature.stepSmooth + (raised.y - previous.y), kMaxStepSmooth);
        return true;
    };

    tryMove(wish.x * deltaSeconds, 0.0f);
    tryMove(0.0f, wish.z * deltaSeconds);

    // A climber treats the side of whatever stopped it as a ladder, but only
    // while it is actually going somewhere - otherwise an idle spider scales
    // every wall it happens to amble into and ends up on the roof.
    if (species.climbs && wallAhead && creature.running) {
        creature.velocity.y = std::max(creature.velocity.y, 3.0f);
        creature.onGround = false;
    } else if (mustJump && !species.hops && creature.onGround) {
        // Too tall to step over and low enough to clear. A jump is the
        // reference's answer, and it is a real one: the launch speed is
        // whatever reaches the species' jump height under creature gravity, so
        // the arc, the airtime and the landing all come out of the physics
        // rather than being animated.
        //
        // `jumpClear` is what stops a herd pinned against a cliff face bouncing
        // on the spot forever, which reads far worse than standing still does.
        creature.velocity.y = std::sqrt(2.0f * kGravity * species.jumpHeight);
        creature.onGround = false;
    }

    const glm::vec3 travelled = creature.position - before;
    const float ground = std::sqrt(travelled.x * travelled.x + travelled.z * travelled.z);
    creature.gait += ground * species.gaitRate;
    creature.age += deltaSeconds;

    // The drawn body catching up to a step the box already took.
    creature.stepSmooth *= std::pow(kStepSmoothPerTick, deltaSeconds / 0.05f);
    if (creature.stepSmooth < 0.001f) {
        creature.stepSmooth = 0.0f;
    }

    // The reference's eased amplitude: chase the speed the creature actually
    // achieved rather than the speed it wanted, so being blocked, shoved or
    // knocked back winds the legs down instead of leaving them cycling.
    //
    // The lag is a *drag* on the gap, not a rate, so it converts by raising the
    // per-tick retention to `dt/0.05` - and unlike the chicken's fall that is
    // exact here rather than approximate, because the target holds still for
    // the length of the step instead of being pushed by gravity underneath it.
    const float travelSpeed = deltaSeconds > 0.0f ? ground / deltaSeconds : 0.0f;
    const float wanted = std::min(travelSpeed / species.runSpeed, 1.0f);
    creature.limbSwingAmount +=
        (wanted - creature.limbSwingAmount) *
        (1.0f - std::pow(kLimbSwingLagPerTick, deltaSeconds / 0.05f));

    // The wing beat, for anything that flaps. Opens while airborne and folds
    // back on the ground, and the beat carries on for a moment after landing -
    // which is the whole reason this is three numbers rather than one.
    if (species.fallDrag < 1.0f) {
        creature.flapSpeed = std::clamp(
            creature.flapSpeed +
                (creature.onGround ? -kFlapCloseRate : kFlapOpenRate) * deltaSeconds,
            0.0f, 1.0f);
        if (!creature.onGround) {
            creature.flapping = 1.0f;
        }
        creature.flapping *= std::pow(kFlapDecayPerTick, deltaSeconds / 0.05f);
        creature.flap += creature.flapping * kFlapPhaseRate * deltaSeconds;
    }
}

CreatureAttack Creatures::update(const World& world, const glm::vec3& playerFeet, float deltaSeconds,
                                 bool night, bool playerSneaking,
                                 std::vector<CreatureExplosion>& blasts) {
    CreatureAttack attack;
    // Refilled once a frame, then spent by whichever creatures ask to search.
    // A cap here rather than a cap per creature is what bounds the cost against
    // the whole population instead of against each animal.
    m_pathBudget = kPathsPerFrame;
    for (Creature& creature : m_creatures) {
        think(world, creature, playerFeet, deltaSeconds, night, playerSneaking, attack, blasts);
        step(world, creature, deltaSeconds);
    }
    separate(world, deltaSeconds);
    return attack;
}

void Creatures::separate(const World& world, float deltaSeconds) {
    // A soft horizontal shove rather than a hard constraint, which is what the
    // reference does too: entities never resolve against each other properly,
    // they just push apart. There is deliberately no vertical component, so one
    // standing on another stays there rather than being squeezed out.
    //
    // Quadratic, and that is fine at a cap of fourteen plus whatever a slime
    // splits into. If the cap ever grows past a hundred this wants a grid.
    for (std::size_t i = 0; i < m_creatures.size(); ++i) {
        for (std::size_t j = i + 1; j < m_creatures.size(); ++j) {
            Creature& a = m_creatures[i];
            Creature& b = m_creatures[j];

            const float reach = (speciesInfo(a.kind).halfWidth * a.scale +
                                 speciesInfo(b.kind).halfWidth * b.scale);
            glm::vec3 offset = b.position - a.position;
            offset.y = 0.0f;
            const float distance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
            if (distance >= reach) {
                continue;
            }

            // Exactly coincident gives no direction to push along, so pick one
            // from their indices rather than dividing by zero.
            glm::vec3 push{0.0f};
            if (distance > 0.0001f) {
                push = offset / distance;
            } else {
                const float angle = static_cast<float>(i * 31 + j) * 0.7f;
                push = glm::vec3{std::sin(angle), 0.0f, std::cos(angle)};
            }

            const float strength = (reach - distance) * kSeparationPush * deltaSeconds;
            // A shove may not put a body inside terrain. It could, and once
            // inside a block every move a creature tries overlaps something, so
            // it is stuck for good - which is how a herd crowding a wall buried
            // its own members.
            const glm::vec3 nextA = a.position - push * strength;
            const glm::vec3 nextB = b.position + push * strength;
            if (!bodyOverlapsSolid(world, speciesInfo(a.kind), nextA, a.scale)) {
                a.position = nextA;
            }
            if (!bodyOverlapsSolid(world, speciesInfo(b.kind), nextB, b.scale)) {
                b.position = nextB;
            }
        }
    }
}

void Creatures::alertNeighbours(const Creature& struck, std::size_t struckIndex) {
    // One mechanism, two behaviours: a neighbour that can bite turns on the
    // player, and one that cannot bolts with its herd. Which of the two it is
    // stopped being decided here at M20c - this only records that the
    // neighbour heard something, and `Panic` and `HurtByTarget` read the
    // species row and disagree about what that means.
    //
    // **How far the call carries is per species, and for most hostiles it does
    // not carry at all.** The reference does this through
    // `minecraft:angry.broadcast_anger` rather than `alert_same_type`, which is
    // off on everything except the silverfish - so a zombie or a skeleton hears
    // nothing, and a horde has to be walked into rather than summoned by
    // hitting one of them. A wolf pack hears at twenty metres, a bear at
    // forty-one, and a herd of sheep at sixteen, which is ours rather than the
    // reference's.
    const CreatureSpecies& species = speciesInfo(struck.kind);
    if (species.alertRange <= 0.0f) {
        return;
    }

    for (std::size_t i = 0; i < m_creatures.size(); ++i) {
        if (i == struckIndex) {
            continue;
        }
        Creature& other = m_creatures[i];
        if (other.kind != struck.kind) {
            continue;
        }

        const glm::vec3 offset = other.position - struck.position;
        if (std::abs(offset.y) > kAlertHeight ||
            offset.x * offset.x + offset.z * offset.z > species.alertRange * species.alertRange) {
            continue;
        }
        other.provokedTimer = species.angerSeconds;
    }
}

void Creatures::place(CreatureKind kind, const glm::vec3& feet, float yaw, bool charged,
                      float puff) {
    Creature creature;
    creature.kind = kind;
    creature.health = speciesInfo(kind).health;
    creature.charged = charged;
    creature.puff = puff;
    creature.variant = rollVariant(kind);
    creature.position = feet;
    creature.yaw = yaw;
    creature.targetYaw = yaw;
    creature.headYaw = yaw;
    pickWanderGoal(creature, m_random);
    m_creatures.push_back(creature);
}

void Creatures::restore(CreatureKind kind, const glm::vec3& feet, float yaw, int health, float scale,
                        bool charged) {
    Creature creature;
    creature.kind = kind;
    creature.health = health;
    creature.variant = rollVariant(kind);
    creature.position = feet;
    creature.yaw = yaw;
    creature.targetYaw = yaw;
    creature.headYaw = yaw;
    creature.scale = scale;
    creature.charged = charged;
    pickWanderGoal(creature, m_random);
    m_creatures.push_back(creature);
}

void Creatures::populateChunks(const World& world, const glm::vec3& playerFeet) {
    // The reference runs *two* spawn systems: the continuous cycle around the
    // player, which `manage` below is, and a one-off pass when a chunk is
    // generated that ignores the population cap entirely. This is the second.
    // It is a pure function of the seed and the chunk coordinate, so a given
    // chunk always produces the same herd however you approach it - the same
    // rule the terrain generator already follows.
    const int centreX = static_cast<int>(std::floor(playerFeet.x)) >> kChunkShift;
    const int centreZ = static_cast<int>(std::floor(playerFeet.z)) >> kChunkShift;

    for (int cz = centreZ - kPopulateRadius; cz <= centreZ + kPopulateRadius; ++cz) {
        for (int cx = centreX - kPopulateRadius; cx <= centreX + kPopulateRadius; ++cx) {
            const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
                             static_cast<std::uint32_t>(cz);
            if (m_populated.count(key) != 0) {
                continue;
            }

            const int originX = cx << kChunkShift;
            const int originZ = cz << kChunkShift;
            if (!world.columnResident(originX, originZ)) {
                // Not loaded yet, so leave it unmarked and try again later.
                continue;
            }
            m_populated.insert(key);

            // Most chunks get nothing at all, which is what keeps a herd worth
            // finding. One hash answers that before any biome work is done.
            std::uint32_t seed = chunkHash(world.seed(), cx, cz);
            if (hashUnit(seed) > kChunkSpawnChance) {
                continue;
            }

            const BiomeId biome = sampleBiome(world.seed(), originX + 16, originZ + 16).dominant;

            // Everything that could be generated here, weighted as usual. Only
            // species with a group size take part.
            const auto loaded = census();
            float weights[static_cast<std::size_t>(CreatureKind::Count)]{};
            float total = 0.0f;
            for (std::size_t i = 0; i < static_cast<std::size_t>(CreatureKind::Count); ++i) {
                const CreatureSpecies& species = kSpecies[i];
                if (species.groupSize <= 0 || !spawnsIn(static_cast<CreatureKind>(i), biome)) {
                    continue;
                }
                if (species.maxLoaded > 0 &&
                    loaded[i] >= static_cast<std::size_t>(species.maxLoaded)) {
                    continue;
                }
                weights[i] = species.weight;
                total += species.weight;
            }
            if (total <= 0.0f) {
                continue;
            }

            seed = chunkHash(seed, cx, cz);
            float roll = hashUnit(seed) * total;
            auto chosen = CreatureKind::Count;
            for (std::size_t i = 0; i < static_cast<std::size_t>(CreatureKind::Count); ++i) {
                roll -= weights[i];
                if (weights[i] > 0.0f && roll <= 0.0f) {
                    chosen = static_cast<CreatureKind>(i);
                    break;
                }
            }
            if (chosen == CreatureKind::Count) {
                continue;
            }

            const CreatureSpecies& species = speciesInfo(chosen);
            seed = chunkHash(seed, cx, cz);
            const int wanted = 1 + static_cast<int>(hashUnit(seed) * static_cast<float>(species.groupSize));

            for (int n = 0; n < wanted; ++n) {
                if (m_creatures.size() >= kGeneratedCeiling) {
                    return;
                }
                seed = chunkHash(seed, n, cz);
                const int x = originX + static_cast<int>(hashUnit(seed) * 32.0f);
                seed = chunkHash(seed, n, cx);
                const int z = originZ + static_cast<int>(hashUnit(seed) * 32.0f);

                const int surface = world.highestSolid(x, z);
                if (surface < 0) {
                    continue;
                }
                // Ground that is not fully loaded reads as air, so a herd placed
                // over it starts life falling through the world.
                if (!world.columnResident(x, z)) {
                    continue;
                }
                seed = chunkHash(seed, z, x);
                const int y =
                    species.swims ? waterColumnY(world, x, z, hashUnit(seed)) : surface + 1;
                if (y < 0 || !canSpawnAt(world, species, x, y, z)) {
                    continue;
                }

                Creature creature;
                creature.kind = chosen;
                creature.health = species.health;
                creature.variant = rollVariant(chosen);
                creature.position = glm::vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y),
                                              static_cast<float>(z) + 0.5f};
                seed = chunkHash(seed, x, z);
                creature.yaw = hashUnit(seed) * kTwoPi;
                creature.targetYaw = creature.yaw;
                creature.headYaw = creature.yaw;
                if (hashUnit(chunkHash(seed, n, n)) < species.babyChance) {
                    creature.scale = kBabyScale;
                }
                pickWanderGoal(creature, m_random);
                m_creatures.push_back(creature);
            }
        }
    }
}

void Creatures::manage(const World& world, const glm::vec3& playerFeet, float deltaSeconds, bool night) {
    populateChunks(world, playerFeet);

    // Anything a dying creature leaves behind. Collected rather than pushed
    // straight on, because appending to the vector being walked would move it
    // out from under the loop.
    std::vector<Creature> spawned;

    // Retire the distant ones first, so a player walking away frees room for
    // the ones ahead of them rather than hitting the cap behind them.
    for (std::size_t i = m_creatures.size(); i-- > 0;) {
        const Creature& creature = m_creatures[i];
        const CreatureSpecies& species = speciesInfo(creature.kind);
        const glm::vec3 offset = creature.position - playerFeet;
        const bool tooFar = glm::dot(offset, offset) > kDespawnDistance * kDespawnDistance;
        // Nocturnal creatures caught in daylight retire, which is what makes a
        // night different from a day rather than merely darker. Slimes and
        // spiders are exempt: they spawn in the dark and simply stay. So is
        // anything still in the water, which is the reference's rule and the
        // only reason a drowned is not extinct by breakfast.
        const int sky = world.skyLightAt(static_cast<int>(std::floor(creature.position.x)),
                                        static_cast<int>(std::floor(creature.position.y)),
                                        static_cast<int>(std::floor(creature.position.z)));
        const bool burnedOff =
            !night && species.nocturnal && species.burnsInDay && sky >= 12 && !creature.inWater;
        const bool killed = creature.health <= 0;

        if (tooFar || burnedOff || killed || creature.position.y < -8.0f) {
            // Only a kill splits it. Retiring at distance or burning off must
            // not, or walking away from a large slime quietly breeds a swarm
            // out of view.
            if (killed && species.splitInto != CreatureKind::Count) {
                const int span = species.splitMax - species.splitMin + 1;
                const int count = species.splitMin + static_cast<int>(random01() * static_cast<float>(span));
                for (int n = 0; n < std::min(count, species.splitMax); ++n) {
                    Creature child;
                    child.kind = species.splitInto;
                    child.health = speciesInfo(child.kind).health;
                    // Scattered off the parent's centre, or the children spawn
                    // inside one another and shove each other apart.
                    const float angle = random01() * kTwoPi;
                    child.position = creature.position +
                                     glm::vec3{std::sin(angle) * 0.4f, 0.1f, std::cos(angle) * 0.4f};
                    child.yaw = angle;
                    child.targetYaw = angle;
                    child.headYaw = angle;
                    // Nothing to set: the player is standing right there, and
                    // `NearestAttackableTarget` picks them up on the first tick.
                    spawned.push_back(child);
                }
            }
            m_creatures[i] = m_creatures.back();
            m_creatures.pop_back();
        }
    }

    // Deliberately not held to the population cap: a split is the payoff for
    // killing the thing, and suppressing it would make large slimes pointless.
    for (Creature& child : spawned) {
        m_creatures.push_back(child);
    }

    m_spawnTimer -= deltaSeconds;
    if (m_spawnTimer > 0.0f || m_creatures.size() >= kMaxCreatures) {
        return;
    }
    m_spawnTimer = kSpawnInterval;

    // A handful of attempts rather than a search: failing is cheap and the next
    // try comes along in a couple of seconds anyway.
    for (int attempt = 0; attempt < 8; ++attempt) {
        const float angle = random01() * kTwoPi;
        const float distance = kSpawnNear + random01() * (kSpawnFar - kSpawnNear);
        const int x = static_cast<int>(std::floor(playerFeet.x + std::sin(angle) * distance));
        const int z = static_cast<int>(std::floor(playerFeet.z + std::cos(angle) * distance));
        const int surface = world.highestSolid(x, z);
        if (surface < 0) {
            continue;
        }

        const int y = surface + 1;
        const BiomeId biome = sampleBiome(world.seed(), x, z).dominant;

        // Never spawn over ground that is not fully loaded, or the creature
        // starts its life falling through the world.
        if (!world.columnResident(x, z)) {
            continue;
        }

        // A swimmer wants a cell in open water rather than the seabed the rest
        // of the roster stands on, so the column is measured once here and the
        // shortlist below picks whichever height each species actually wants.
        const int swimY = waterColumnY(world, x, z, random01());

        // Bedrock's `density_limit`, which is per species rather than global -
        // without it a shoal of cod crowds every squid out of the same water.
        const auto loaded = census();

        // Everything that could live here, weighted. Building the shortlist and
        // then rolling once is what keeps the table the only place rules live.
        float weights[static_cast<std::size_t>(CreatureKind::Count)]{};
        float total = 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t>(CreatureKind::Count); ++i) {
            const auto kind = static_cast<CreatureKind>(i);
            const CreatureSpecies& species = kSpecies[i];
            if (!spawnsIn(kind, biome)) {
                continue;
            }
            if (species.maxLoaded > 0 &&
                loaded[i] >= static_cast<std::size_t>(species.maxLoaded)) {
                continue;
            }
            const int spawnY = species.swims ? swimY : y;
            if (spawnY < 0) {
                continue;
            }
            // Fish are the reference's `water_ambient` category, which carries
            // neither a light rule nor a time-of-day one - the sea is dark
            // enough at depth and they are there at noon regardless.
            if (!species.swims) {
                if (species.nocturnal != night) {
                    continue;
                }
                if (world.blockLightAt(x, spawnY, z) > species.maxBlockLight) {
                    continue;
                }
            }
            if (!canSpawnAt(world, species, x, spawnY, z)) {
                continue;
            }
            weights[i] = species.weight;
            total += species.weight;
        }
        if (total <= 0.0f) {
            continue;
        }

        float roll = random01() * total;
        auto chosen = CreatureKind::Sheep;
        for (std::size_t i = 0; i < static_cast<std::size_t>(CreatureKind::Count); ++i) {
            roll -= weights[i];
            if (weights[i] > 0.0f && roll <= 0.0f) {
                chosen = static_cast<CreatureKind>(i);
                break;
            }
        }

        Creature creature;
        creature.kind = chosen;
        creature.health = speciesInfo(chosen).health;
        creature.variant = rollVariant(chosen);
        const int spawnY = speciesInfo(chosen).swims ? swimY : y;
        creature.position = glm::vec3{static_cast<float>(x) + 0.5f, static_cast<float>(spawnY),
                                      static_cast<float>(z) + 0.5f};
        creature.yaw = random01() * kTwoPi;
        creature.targetYaw = creature.yaw;
        creature.headYaw = creature.yaw;
        // A share of every natural spawn is young, which is most of what makes
        // a group read as a family rather than as a set of identical adults.
        if (random01() < speciesInfo(chosen).babyChance) {
            creature.scale = kBabyScale;
        }
        // Standing in for lightning until weather exists.
        if (speciesInfo(chosen).explodePower > 0.0f && random01() < kChargedChance) {
            creature.charged = true;
        }
        pickWanderGoal(creature, m_random);
        m_creatures.push_back(creature);
        // One per interval. Without this the loop keeps going and a single call
        // can add eight, which overshoots the cap.
        return;
    }
}

std::size_t Creatures::findAimed(const glm::vec3& eye, const glm::vec3& forward, float reach) const {
    // Nearest first, so a creature behind another cannot be hit through it.
    float nearest = reach;
    std::size_t found = m_creatures.size();

    for (std::size_t i = 0; i < m_creatures.size(); ++i) {
        const Creature& creature = m_creatures[i];
        const CreatureSpecies& species = speciesInfo(creature.kind);
        const glm::vec3 centre = creature.position + glm::vec3{0.0f, species.height * 0.5f, 0.0f};
        const glm::vec3 offset = centre - eye;
        const float along = glm::dot(offset, forward);
        if (along <= 0.0f || along > nearest) {
            continue;
        }
        // Distance from the creature's centre to the aim ray. Generous, because
        // a box model is wider than a point and swinging should not feel fussy.
        const glm::vec3 closest = offset - forward * along;
        if (glm::dot(closest, closest) > 0.6f * 0.6f) {
            continue;
        }
        nearest = along;
        found = i;
    }
    return found;
}

bool Creatures::aimedAt(const glm::vec3& eye, const glm::vec3& forward, float reach) const {
    return findAimed(eye, forward, reach) != m_creatures.size();
}

bool Creatures::strike(const glm::vec3& eye, const glm::vec3& forward, float reach, int damage) {
    const std::size_t index = findAimed(eye, forward, reach);
    if (index == m_creatures.size()) {
        return false;
    }

    Creature& target = m_creatures[index];
    target.health -= damage;
    target.hurtTimer = kHurtSeconds;
    // Assigned, not added. Accumulating means a second blow before the first has
    // worn off launches the creature twice as far, and a third further still.
    target.velocity = glm::vec3{forward.x, 0.0f, forward.z} * kStrikeKnockback +
                      glm::vec3{0.0f, kStrikeLift, 0.0f};
    target.onGround = false;

    // Prey bolts; a hunter that is already hunting you is unimpressed; and a
    // neutral turns on you, which is the whole of what makes a wolf a wolf.
    // None of which this function decides any more - it records that something
    // happened, and `Panic` and `HurtByTarget` read the species and disagree
    // about what to do with it.
    //
    // **How long it keeps mattering is the species' own number.** A flat six
    // seconds meant a polar bear you shot forgot about it before you had
    // finished backing away; the reference gives it five hundred.
    target.provokedTimer = speciesInfo(target.kind).angerSeconds;

    // A clean kill starts no war. The reference has the same exemption, and it
    // is what stops one-shotting a lone animal turning its whole species on
    // you - the neighbours never saw anything happen.
    if (target.health > 0) {
        alertNeighbours(target, index);
    }
    return true;
}

int Creatures::applyExplosion(const World& world, const glm::vec3& centre, float power) {
    int caught = 0;
    for (Creature& creature : m_creatures) {
        // Whatever set this off is already at zero and waiting to be retired.
        // Flinging its corpse is not worth the one frame it would show for.
        if (creature.health <= 0) {
            continue;
        }

        const CreatureSpecies& species = speciesInfo(creature.kind);
        const Aabb body = bodyBox(species, creature.position, creature.scale);
        // Distance is measured to the feet and shelter across the whole box,
        // which is the reference's split: where you stand decides how much of
        // the blast reaches you, and how much of you is behind cover decides
        // how much of that lands.
        const float exposure = explosionExposure(world, centre, body);
        const float impact = explosionImpact(centre, power, creature.position, exposure);
        if (impact <= 0.0f) {
            continue;
        }

        creature.health -= explosionDamage(power, impact);
        creature.hurtTimer = kHurtSeconds;
        // Deliberately **not** provoked. `provokedTimer` only ever means "the
        // player did this", because the target slot has no way to name anything
        // else - so rousing a goat a Bramble blew up would send it after the
        // player for something they did not do. Revisit when a creature can
        // target another creature.

        const glm::vec3 away = (body.min + body.max) * 0.5f - centre;
        const float reach = glm::length(away);
        if (reach > 0.001f) {
            // Aimed at the body's middle rather than its feet, which is what
            // gives a close blast its upward throw without a special case.
            //
            // Assigned, not added - two blasts in one frame each adding their
            // own throw is the accumulator bug that has already emptied the map
            // once.
            creature.velocity = away / reach * impact * kBlastThrow;
            creature.onGround = false;
        }
        ++caught;
    }
    return caught;
}

engine::MeshData Creatures::buildMesh(const World& world, engine::MeshData& translucent) const {
    engine::MeshData mesh;

    // Where the next quad goes and how see-through it is. Everything defaults
    // to the opaque mesh at full alpha; a slime's shell flips both for the one
    // box that needs it and flips them straight back.
    engine::MeshData* target = &mesh;
    float quadAlpha = 1.0f;

    for (const Creature& creature : m_creatures) {
        const CreatureSpecies& species = speciesInfo(creature.kind);

        // Where the body is *drawn*, which trails the collision box up a step
        // so a climb reads as one rather than as a teleport. Only geometry may
        // use it; everything that reasons about the world wants the real one.
        const glm::vec3 renderPosition =
            creature.position - glm::vec3{0.0f, creature.stepSmooth, 0.0f};

        const int lx = static_cast<int>(std::floor(creature.position.x));
        const int ly = static_cast<int>(std::floor(creature.position.y + species.height * 0.5f));
        const int lz = static_cast<int>(std::floor(creature.position.z));
        float sky = static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        float block = static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        // Lit by itself rather than by the water around it. A proper emissive
        // material waits for the renderer rebuild; feeding the skin full light
        // is most of what one looks like in a dark ocean and costs nothing.
        if (species.glows) {
            sky = 1.0f;
            block = 1.0f;
        }
        // Struck creatures are tinted by the shader, not lit by it. A lit fuse
        // outranks that and strobes: the reference alternates every tenth of
        // the countdown, at a constant rate that does *not* speed up - only the
        // bulge accelerates, which is what makes the last half-second read as
        // sudden rather than as a build-up.
        float skinLayer = creature.hurtTimer > 0.0f ? kSkinHurtLayer : kSkinTextureLayer;
        if (species.explodePower > 0.0f && creature.fuseTimer > 0.0f) {
            const float s = creature.fuseTimer / species.fuseSeconds;
            if (static_cast<int>(std::round(s * 10.0f)) % 2 != 0) {
                skinLayer = kSkinFlashLayer;
            }
        }

        const float sinYaw = std::sin(creature.yaw);
        const float cosYaw = std::cos(creature.yaw);
        // Local axes: forward is where it faces, side is to its left.
        const glm::vec3 bodyForward{sinYaw, 0.0f, cosYaw};
        const glm::vec3 bodySide{cosYaw, 0.0f, -sinYaw};

        const float sinHead = std::sin(creature.headYaw);
        const float cosHead = std::cos(creature.headYaw);
        const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
        const glm::vec3 headLevel{sinHead, 0.0f, cosHead};
        const glm::vec3 headSide{cosHead, 0.0f, -sinHead};
        const float sinTilt = std::sin(creature.headPitch);
        const float cosTilt = std::cos(creature.headPitch);
        const glm::vec3 headForward = headLevel * cosTilt - worldUp * sinTilt;
        const glm::vec3 headUp = headLevel * sinTilt + worldUp * cosTilt;

        // Every box below is placed along these three, so switching them is the
        // whole of the head turn - no box builder changes and no per-part
        // rotation. `beginHead(...)` before the parts that ride on the neck and
        // `endHead()` straight after; a species that never calls them renders
        // exactly as it did before, which is what let this be rolled out one
        // animal at a time.
        //
        // **The head turns about the neck joint, not about the creature.** It
        // used to rotate the frame about the body's own origin, which meant a
        // head placed forward of centre *orbited* - it physically swung out
        // sideways and lunged as it looked, dragging the neck with it. A pivot
        // at the base of the head group leaves the neck planted and turns the
        // head on top of it, which is what an animal actually does.
        glm::vec3 forward = bodyForward;
        glm::vec3 side = bodySide;
        glm::vec3 upAxis = worldUp;
        glm::vec3 frameOrigin = renderPosition;
        float pivotForward = 0.0f;
        float pivotUp = 0.0f;

        // The species' own scale times this individual's, which is 1 for an
        // adult and a fraction for a baby. Not const: a pufferfish multiplies it
        // to inflate, which is the one place a species changes its own size
        // mid-frame, and doing it here means every box picks it up for free.
        float modelScale = species.modelScale * creature.scale;

        // A lit fuse swells the whole animal. The curve is the reference's and
        // it matters that it is `s^4`: nothing visible happens for the first two
        // thirds of the countdown, then it bloats fast. It grows wide (1.4x)
        // far more than tall (1.1x), so it reads as bulging rather than rising.
        float swellWide = 1.0f;
        float swellTall = 1.0f;
        if (species.explodePower > 0.0f && creature.fuseTimer > 0.0f) {
            // The reference normalises against two ticks short of the fuse, so
            // the model reaches full size just before it goes off.
            const float s = std::clamp(creature.fuseTimer / (species.fuseSeconds - 0.1f), 0.0f, 1.0f);
            const float bulge = s * s * s * s;
            // A tiny volume-preserving shimmy - wide and tall move in opposite
            // directions - which is what stops the swell reading as a smooth
            // balloon.
            const float wobble = 1.0f + std::sin(s * 100.0f) * s * 0.01f;
            swellWide = (1.0f + bulge * 0.4f) * wobble;
            swellTall = (1.0f + bulge * 0.1f) / wobble;
        }

        const auto place = [&](float alongForward, float up, float alongSide) {
            return frameOrigin + forward * ((alongForward - pivotForward) * modelScale * swellWide) +
                   side * (alongSide * modelScale * swellWide) +
                   upAxis * ((up - pivotUp) * modelScale * swellTall);
        };

        // The neck joint, in the same model units `place` takes. Taken from the
        // head box's own numbers rather than invented: the rear face for a head
        // carried out in front, the bottom face for one sat on top, and the base
        // of the neck for anything long-necked, where the neck is part of what
        // turns. Split in two so a group that forgets its pivot cannot compile.
        const auto beginHead = [&](float neckForward, float neckUp) {
            forward = headForward;
            side = headSide;
            upAxis = headUp;
            pivotForward = neckForward;
            pivotUp = neckUp;
            frameOrigin = renderPosition +
                          bodyForward * (neckForward * modelScale * swellWide) +
                          worldUp * (neckUp * modelScale * swellTall);
        };
        const auto endHead = [&]() {
            forward = bodyForward;
            side = bodySide;
            upAxis = worldUp;
            pivotForward = 0.0f;
            pivotUp = 0.0f;
            frameOrigin = renderPosition;
        };

        {
            // A box face reads one rectangle out of the unwrapped net. The net
            // is the standard cross: depth-sized flaps either side of the
            // width, top and bottom sitting above.
            const auto skinQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                      const glm::vec3& d, float shade, float vBase, float rx, float ry,
                                      float rw, float rh) {
                const auto base = static_cast<std::uint32_t>(target->vertices.size());
                const glm::vec3 corners[4]{a, b, c, d};
                const float u0 = rx / kSkinWidth;
                const float u1 = (rx + rw) / kSkinWidth;
                const float v0 = (vBase + ry) / kSkinSheet;
                const float v1 = (vBase + ry + rh) / kSkinSheet;
                const glm::vec2 uvs[4]{{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
                for (int i = 0; i < 4; ++i) {
                    target->vertices.push_back(engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                                              {sky, block, shade, quadAlpha},
                                                              {uvs[i].x, uvs[i].y},
                                                              skinLayer});
                }
                target->indices.insert(target->indices.end(),
                                       {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                        base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
            };

            // Head and legs stand upright: the net's width is the creature's
            // side, its height is up, its depth runs forward. A positive
            // `pitch` tips the box nose-down about its side axis, which is the
            // only way an axis-aligned box gets a sloping face or a lowered
            // muzzle without a second geometry path.
            // `grow` swells a box on every axis at once, which is right for
            // breaking a shared plane and wrong for reaching toward a
            // neighbour: it thickens the box as much as it lengthens it.
            // `growSide` extends the side axis alone, for a part that has to
            // span between two others without getting fatter than them.
            // `roll` tips the box about its own forward axis, which is the only
            // way an axis-aligned box gets a wing that lifts.
            //
            // The box's own up axis after both rotations, and the one place it
            // is worked out: `uprightBox` measures its height along it and
            // `legBox` hangs a limb from it, and those two disagreeing would
            // put every limb somewhere its own box is not.
            const auto boxHeightAxis = [&](float pitch, float roll) {
                const glm::vec3 pitched = forward * std::sin(pitch) + upAxis * std::cos(pitch);
                return pitched * std::cos(roll) - side * std::sin(roll);
            };

            const auto uprightBox = [&](const glm::vec3& centre, float netW, float netH, float netD,
                                        float u, float v, float vBase, float grow, float pitch = 0.0f,
                                        float growSide = 0.0f, float roll = 0.0f,
                                        bool mirror = false) {
                const float cosPitch = std::cos(pitch);
                const float sinPitch = std::sin(pitch);
                const glm::vec3 depthAxis = forward * cosPitch - upAxis * sinPitch;
                const glm::vec3 pitched = forward * sinPitch + upAxis * cosPitch;

                const glm::vec3 sideAxis = side * std::cos(roll) + pitched * std::sin(roll);
                const glm::vec3 heightAxis = boxHeightAxis(pitch, roll);

                const glm::vec3 f = depthAxis * ((netD * kTexel * 0.5f + grow) * modelScale * swellWide);
                const glm::vec3 s =
                    sideAxis * ((netW * kTexel * 0.5f + grow + growSide) * modelScale * swellWide);
                const glm::vec3 up = heightAxis * ((netH * kTexel * 0.5f + grow) * modelScale * swellTall);

                skinQuad(centre + f + s - up, centre + f - s - up, centre + f - s + up, centre + f + s + up,
                         0.86f, vBase, u + netD, v + netD, netW, netH);
                skinQuad(centre - f - s - up, centre - f + s - up, centre - f + s + up, centre - f - s + up,
                         0.72f, vBase, u + netD + netW + netD, v + netD, netW, netH);
                // The two side rects, and **which one faces outward is a fact
                // about the limb rather than about the net**. A pair of legs
                // shares one net, so drawing both identically puts the painted
                // flap outward on one and inward on the other - invisible while
                // a texture is symmetric, and glaring the moment one is not.
                // The skeleton horse paints bone on a single flap and leaves
                // the other bare, which is what finally exposed it.
                skinQuad(centre + s - f - up, centre + s + f - up, centre + s + f + up, centre + s - f + up,
                         0.80f, vBase, mirror ? u + netD + netW : u, v + netD, netD, netH);
                skinQuad(centre - s + f - up, centre - s - f - up, centre - s - f + up, centre - s + f + up,
                         0.66f, vBase, mirror ? u : u + netD + netW, v + netD, netD, netH);
                // Top and bottom run **forward toward the far edge of their
                // rect**, because the top rect folds down to meet the front
                // face - so its last row is the front, not its first. These two
                // were reversed for the whole roster until a model with
                // asymmetric artwork up there finally rendered it backwards.
                skinQuad(centre + up + f - s, centre + up + f + s, centre + up - f + s, centre + up - f - s,
                         1.0f, vBase, u + netD, v, netW, netD);
                skinQuad(centre - up - f - s, centre - up - f + s, centre - up + f + s, centre - up + f - s,
                         0.55f, vBase, u + netD + netW, v, netW, netD);
            };

            // A limb that hangs from a joint and turns about it, instead of
            // sliding back and forth. The foot then traces an arc - it lifts,
            // it eases into each extreme, and the top of the limb stays put
            // rather than sliding out from under the body.
            //
            // **The pivot is derived from the box's own extent**, never given
            // by the caller and never a column in the species row, because a
            // pivot written down twice is free to drift from the geometry it
            // belongs to. It sits below the top face by half the limb's
            // *thinner* cross-section: tilting swings the top corners by half
            // that extent times the sine of the angle, so that much material
            // has to stay above the joint or a wedge opens at the hip on every
            // stride. `ANIMATION.md` §1.2 is where this comes from and names
            // the overhang as what stops the shoulder gapping - and the rule
            // reproduces the reference on both shapes it can be checked
            // against, a 4x12x4 biped arm pivoting 2 texels down from its top
            // and a 1x4x6 wing pivoting at its top face.
            //
            // `restCentre` is where the box sits at rest, so an angle of zero
            // rebuilds precisely the box `uprightBox` would have. That is what
            // lets the roster convert one species at a time.
            const auto legBox = [&](const glm::vec3& restCentre, float netW, float netH, float netD,
                                    float u, float v, float vBase, float grow, float pitch,
                                    float roll = 0.0f, bool mirror = false) {
                const float halfHeight = (netH * kTexel * 0.5f + grow) * modelScale * swellTall;
                // `grow` belongs in the overhang as much as in the extent,
                // because the overhang is measured against the box that is
                // actually drawn. Leaving it out puts the far end of the limb at
                // exactly `-overhang` whatever the box's size - so two limbs
                // sharing a net and differing only in `grow`, which is precisely
                // a body and the clothing shell over it, land their far faces on
                // one plane and fight over every pixel of it.
                const float overhang =
                    (std::min(netW, netD) * kTexel * 0.5f + grow) * modelScale * swellTall;
                const float hang = std::max(halfHeight - overhang, 0.0f);

                const glm::vec3 pivot = restCentre + upAxis * hang;
                uprightBox(pivot - boxHeightAxis(pitch, roll) * hang, netW, netH, netD, u, v, vBase,
                           grow, pitch, 0.0f, roll, mirror);
            };

            // A limb that runs *sideways* out of the body and pivots at its
            // inner end rather than hanging from a joint above it - the
            // spider's eight legs, which `ANIMATION.md` §1.2 puts at the body
            // attachment, mid-height. Its swing is therefore a yaw about the
            // vertical, not a tilt, so it turns the local axes the way the head
            // turn does instead of tipping the box.
            //
            // `splay` then droops it about its **own** forward axis, so the
            // outer end drops however the leg happens to be fanned. That is
            // what stands a spider off the ground rather than leaving it lying
            // on its belly with eight level bars sticking out. At zero for both
            // it rebuilds exactly the box `uprightBox` would have.
            const auto barBox = [&](float alongForward, float up, float alongSide, float netW,
                                    float netH, float netD, float u, float v, float vBase, float grow,
                                    float yaw, float splay = 0.0f) {
                const float outward = alongSide < 0.0f ? -1.0f : 1.0f;
                const float reach = (netW * kTexel * 0.5f + grow) * modelScale * swellWide * outward;
                const glm::vec3 pivot = place(alongForward, up, alongSide) - side * reach;

                const glm::vec3 restForward = forward;
                const glm::vec3 restSide = side;
                const glm::vec3 restUp = upAxis;
                forward = restForward * std::cos(yaw) - restSide * std::sin(yaw);
                side = restSide * std::cos(yaw) + restForward * std::sin(yaw);

                // Rotating both axes rather than only tilting the long one, so
                // the box stays a rigid body and its cut ends stay square to it.
                const glm::vec3 fanned = side;
                const float drop = std::sin(splay) * outward;
                side = fanned * std::cos(splay) - restUp * drop;
                upAxis = restUp * std::cos(splay) + fanned * drop;

                uprightBox(pivot + side * reach, netW, netH, netD, u, v, vBase, grow);
                forward = restForward;
                side = restSide;
                upAxis = restUp;
            };

            // The body is authored standing and then laid down a quarter turn,
            // so the net's height runs forward and its depth becomes upright.
            // Getting this backwards scrambles every body face even when the
            // sizes are right.
            const auto lyingBox = [&](const glm::vec3& centre, float netW, float netH, float netD, float u,
                                      float v, float vBase, float grow) {
                const glm::vec3 f = forward * ((netH * kTexel * 0.5f + grow) * modelScale * swellWide);
                const glm::vec3 s = side * ((netW * kTexel * 0.5f + grow) * modelScale * swellWide);
                const glm::vec3 up = upAxis * ((netD * kTexel * 0.5f + grow) * modelScale * swellTall);

                // Laid down a quarter turn about the side axis. Working it
                // through: the net's back rect ends up on world top and its
                // front rect underneath, while the net's top and bottom become
                // world front and back. Having top and bottom the wrong way
                // round is invisible on a sheep, whose belly matches its back,
                // and paints a cow's pink underside across its spine.
                skinQuad(centre + up - f - s, centre + up - f + s, centre + up + f + s, centre + up + f - s,
                         1.0f, vBase, u + netD + netW + netD, v + netD, netW, netH);
                skinQuad(centre - up + f - s, centre - up + f + s, centre - up - f + s, centre - up - f - s,
                         0.55f, vBase, u + netD, v + netD, netW, netH);
                skinQuad(centre + f + s - up, centre + f - s - up, centre + f - s + up, centre + f + s + up,
                         0.86f, vBase, u + netD, v, netW, netD);
                skinQuad(centre - f - s - up, centre - f + s - up, centre - f + s + up, centre - f - s + up,
                         0.72f, vBase, u + netD + netW, v, netW, netD);
                skinQuad(centre + s - f - up, centre + s + f - up, centre + s + f + up, centre + s - f + up,
                         0.80f, vBase, u, v + netD, netD, netH);
                skinQuad(centre - s + f - up, centre - s - f - up, centre - s - f + up, centre - s + f + up,
                         0.66f, vBase, u + netD + netW, v + netD, netD, netH);
            };

            // A fin or a spine, which the reference authors as a cube with one
            // dimension of zero. Only the two faces carrying artwork are
            // emitted, a fraction of a texel apart: a real box would put those
            // two on one plane, and its four edge slivers would each sample a
            // rect of zero width - which for a dorsal means a *negative* V,
            // reading rows belonging to another species entirely.
            //
            // Whichever net dimension is zero decides which pair of rects the
            // two faces take.
            const auto finBox = [&](const glm::vec3& centre, float netW, float netH, float netD,
                                    float u, float v, float vBase, float pitch = 0.0f,
                                    float roll = 0.0f) {
                const float cosPitch = std::cos(pitch);
                const float sinPitch = std::sin(pitch);
                const glm::vec3 depthAxis = forward * cosPitch - upAxis * sinPitch;
                const glm::vec3 pitched = forward * sinPitch + upAxis * cosPitch;
                const glm::vec3 sideAxis = side * std::cos(roll) + pitched * std::sin(roll);
                const glm::vec3 heightAxis = pitched * std::cos(roll) - side * std::sin(roll);

                const float thick = kFinHalfThickness * modelScale;
                const glm::vec3 f =
                    depthAxis * (netD <= 0.0f ? thick
                                              : netD * kTexel * 0.5f * modelScale * swellWide);
                const glm::vec3 s =
                    sideAxis * (netW <= 0.0f ? thick
                                             : netW * kTexel * 0.5f * modelScale * swellWide);
                const glm::vec3 up =
                    heightAxis * (netH <= 0.0f ? thick
                                               : netH * kTexel * 0.5f * modelScale * swellTall);

                if (netW <= 0.0f) {
                    skinQuad(centre + s - f - up, centre + s + f - up, centre + s + f + up,
                             centre + s - f + up, 0.80f, vBase, u, v + netD, netD, netH);
                    skinQuad(centre - s + f - up, centre - s - f - up, centre - s - f + up,
                             centre - s + f + up, 0.80f, vBase, u + netD, v + netD, netD, netH);
                    return;
                }
                if (netH <= 0.0f) {
                    skinQuad(centre + up + f - s, centre + up + f + s, centre + up - f + s,
                             centre + up - f - s, 0.95f, vBase, u + netD, v, netW, netD);
                    skinQuad(centre - up - f - s, centre - up - f + s, centre - up + f + s,
                             centre - up + f - s, 0.72f, vBase, u + netD + netW, v, netW, netD);
                    return;
                }
                skinQuad(centre + f + s - up, centre + f - s - up, centre + f - s + up,
                         centre + f + s + up, 0.86f, vBase, u + netD, v + netD, netW, netH);
                skinQuad(centre - f - s - up, centre - f + s - up, centre - f + s + up,
                         centre - f - s + up, 0.72f, vBase, u + netD + netW + netD, v + netD, netW,
                         netH);
            };

            // Runs `body` with the local frame turned about the vertical. The
            // spider's legs and the head turn do the same thing; this is the
            // form for a part that needs its *orientation* rotated rather than
            // its position, so the centre is worked out before it is called.
            const auto yawed = [&](float yaw, auto&& body) {
                const glm::vec3 restForward = forward;
                const glm::vec3 restSide = side;
                forward = restForward * std::cos(yaw) - restSide * std::sin(yaw);
                side = restSide * std::cos(yaw) + restForward * std::sin(yaw);
                body();
                forward = restForward;
                side = restSide;
            };

            // How far a limb has turned about its joint this frame, in
            // radians. **Negated because a positive pitch is nose-down**, which
            // swings a foot backward - so `swing` still reads as "this limb
            // forward" at every call site, exactly as it did when it was a
            // distance.
            //
            // A hunted animal moves its legs further, not faster: the rate is
            // already tied to how far it actually travelled, and the amplitude
            // is what separates an amble from a bolt.
            const float swing =
                -std::sin(creature.gait) * species.gaitSwing * creature.limbSwingAmount;

            // The one idle motion the reference gives every biped, and it is
            // the difference between a person standing there and a statue: the
            // arms rock outward by up to 5.7 degrees and forward and back by
            // 2.9, on two deliberately incommensurate periods - 3.5 s and 4.7 s
            // - so it never visibly repeats. The rates are the reference's own
            // 0.09 and 0.067 radians per tick, converted to per second.
            //
            // The roll carries a constant offset as well as a swing, which is
            // what leaves the arms resting slightly splayed rather than flush
            // against the torso - and incidentally off its face planes.
            const float swayRoll = std::cos(creature.age * 1.8f) * 0.05f + 0.05f;
            const float swayPitch = std::sin(creature.age * 1.34f) * 0.05f;

            // A blow, straight out of the two shipped animations. Bedrock runs
            // `variable.attack_time` from 0 to 1 across the swing; ours counts
            // the other way, so the phase is one minus what is left.
            //
            // **No "am I swinging" test is needed anywhere.** Both curves are
            // zero at both ends by construction, so a creature that is not
            // swinging has a timer of zero, a phase of one, and an angle of
            // nothing.
            const float attackPhase = 1.0f - creature.swingTimer / kAttackSwingSeconds;
            const float back = 1.0f - attackPhase;

            // `animation.zombie.attack_bare_hand`: `sin(t*180)*1.2 -
            // sin((1-(1-t)^2)*180)*0.4`, in radians once the 57.3 that turns
            // them into degrees is taken back out. It peaks a little over half
            // a radian halfway through and is never negative, so an arm already
            // held out in front swings **down** and comes back up.
            const float rawChop =
                std::sin(attackPhase * kPi) * 1.2f -
                std::sin((1.0f - back * back) * kPi) * 0.4f;

            // `animation.humanoid.attack.rotations`: `sin((1-(1-t)^3)*180)`,
            // which is a different shape - it peaks a fifth of the way in and
            // eases back over the rest, so the arm flicks up and falls. The
            // 1.2 radian amplitude is the reference's.
            //
            // The two run in opposite senses and that is not a sign slip: a
            // zombie starts with its arms up and chops down, while anything
            // holding a weapon starts with them hanging and swings up.
            //
            // Both are gated on the species rather than on the rig, because
            // the skeleton family share a rig with the Blackbone and are not
            // the same kind of fighter - they are archers.
            const float zombieChop = species.swingsArms ? rawChop : 0.0f;
            const float humanoidRaise =
                species.swingsArms
                    ? std::sin((1.0f - back * back * back) * kPi) * 1.2f
                    : 0.0f;

            if (creature.kind == CreatureKind::Chicken) {
                // Reference net measurements: head 4x6x3, beak 4x2x2,
                // wattle 2x2x2, body 6x8x6, legs 3x5x3 and wings 1x4x6.
                // The body is authored upright and laid forward, like the
                // other quadruped torsos; every appendage stays upright.
                lyingBox(place(0.0f, 0.50f, 0.0f), 6.0f, 8.0f, 6.0f,
                         0.0f, 9.0f, kChickenSkin, 0.0f);
                beginHead(0.21875f, 0.46875f);
                uprightBox(place(0.3125f, 0.65625f, 0.0f), 4.0f, 6.0f, 3.0f,
                           0.0f, 0.0f, kChickenSkin, 0.0f);
                // Beak and wattle sit low on the face. Level with the middle of
                // the head they crowd the eyes, which live on the front rect
                // two texels down from the crown and need clear space above.
                uprightBox(place(0.46875f, 0.625f, 0.0f), 4.0f, 2.0f, 2.0f,
                           14.0f, 0.0f, kChickenSkin, 0.0f);
                uprightBox(place(0.46875f, 0.51f, 0.0f), 2.0f, 2.0f, 2.0f,
                           14.0f, 4.0f, kChickenSkin, 0.0f);
                endHead();

                const auto leg = [&](float alongForward, float alongSide, float angle) {
                    constexpr float halfStrip = 0.03125f;
                    constexpr float halfFoot = 0.09375f;
                    // No box to hand to `legBox`, so the same pivot is applied
                    // by hand: the hip is the top of the strip and everything
                    // below it swings on the arc.
                    const glm::vec3 hip = place(alongForward, 0.3225f, alongSide);
                    const glm::vec3 shank =
                        boxHeightAxis(angle, 0.0f) * (0.3125f * modelScale * swellTall);
                    const glm::vec3 high = hip;
                    const glm::vec3 low = hip - shank;

                    // The current reference uses a one-texel leg strip rather
                    // than a box net. Two crossed quads keep that thin leg
                    // visible from every direction.
                    skinQuad(low - side * halfStrip, low + side * halfStrip,
                             high + side * halfStrip, high - side * halfStrip,
                             0.75f, kChickenSkin, 36.0f, 3.0f, 1.0f, 5.0f);
                    skinQuad(low - forward * halfStrip, low + forward * halfStrip,
                             high + forward * halfStrip, high - forward * halfStrip,
                             0.75f, kChickenSkin, 36.0f, 3.0f, 1.0f, 5.0f);

                    const glm::vec3 foot =
                        low + forward * (0.04f * modelScale * swellWide) +
                        glm::vec3{0.0f, 0.002f * modelScale * swellTall, 0.0f};
                    skinQuad(foot - forward * halfFoot - side * halfFoot,
                             foot + forward * halfFoot - side * halfFoot,
                             foot + forward * halfFoot + side * halfFoot,
                             foot - forward * halfFoot + side * halfFoot,
                             0.75f, kChickenSkin, 32.0f, 0.0f, 3.0f, 3.0f);
                };
                leg(-0.08f, 0.10f, swing);
                leg(-0.08f, -0.10f, -swing);

                // Wings open as the bird leaves the ground and fold as it
                // settles. The angle is the reference's exactly - `(sin(phase)
                // + 1) x openness` - so the beat is fastest just after takeoff
                // and dies away a moment after landing rather than snapping shut.
                const float wingAngle =
                    (std::sin(creature.flap) + 1.0f) * creature.flapSpeed;
                const auto chickenWing = [&](float sideSign) {
                    legBox(place(0.0f, 0.5f, sideSign * 0.21875f), 1.0f, 4.0f, 6.0f, 24.0f, 13.0f,
                           kChickenSkin, 0.0f, 0.0f, wingAngle * sideSign);
                };
                chickenWing(1.0f);
                chickenWing(-1.0f);
                continue;
            }

            // Cat and ocelot are one rig. Their reference sheets have
            // byte-identical alpha, so every net matches and only the pixels
            // differ - the same arrangement the horse family already uses.
            const auto feline = [&](float skin) {
                // A low, narrow quadruped: 4x16x6 body, 5x4x5 head,
                // long front legs, short rear legs and a raised two-part tail.
                lyingBox(place(-0.08f, 0.50f, 0.0f), 4.0f, 16.0f, 6.0f,
                         20.0f, 0.0f, skin, 0.0f);
                beginHead(0.364f, 0.67f);
                uprightBox(place(0.52f, 0.67f, 0.0f), 5.0f, 4.0f, 5.0f,
                           0.0f, 0.0f, skin, 0.0f);

                // Each ear has its own net, and they are barely more than a
                // texel: 1 across, 1 up, 2 forward.
                uprightBox(place(0.47f, 0.83f, 0.09f), 1.0f, 1.0f, 2.0f,
                           0.0f, 10.0f, skin, 0.0f);
                uprightBox(place(0.47f, 0.83f, -0.09f), 1.0f, 1.0f, 2.0f,
                           6.0f, 10.0f, skin, 0.0f);
                // The muzzle's back face is buried in the head, so its net's
                // side row measures 7 wide rather than the 10 the formula gives.
                uprightBox(place(0.70f, 0.62f, 0.0f), 3.0f, 2.0f, 2.0f,
                           0.0f, 24.0f, skin, 0.0f);
                endHead();

                const float frontY = 0.3125f;
                legBox(place(0.27f, frontY, 0.10f), 2.0f, 10.0f, 2.0f,
                       40.0f, 0.0f, skin, 0.0f, swing);
                legBox(place(0.27f, frontY, -0.10f), 2.0f, 10.0f, 2.0f,
                       40.0f, 0.0f, skin, 0.0f, -swing);

                // Grown a third of a texel: the rear leg net is 6 tall and the
                // body 6 deep, so level with each other their half-extents match
                // exactly and the overlapping faces fight over one depth.
                const float rearY = 0.1875f;
                legBox(place(-0.43f, rearY, 0.10f), 2.0f, 6.0f, 2.0f,
                       8.0f, 13.0f, skin, 0.005f, -swing);
                legBox(place(-0.43f, rearY, -0.10f), 2.0f, 6.0f, 2.0f,
                       8.0f, 13.0f, skin, 0.005f, swing);

                uprightBox(place(-0.57f, 0.34f, 0.0f), 1.0f, 8.0f, 1.0f,
                           0.0f, 15.0f, skin, 0.0f);
                uprightBox(place(-0.57f, 0.78f, 0.0f), 1.0f, 8.0f, 1.0f,
                           4.0f, 15.0f, skin, 0.0f);
            };

            if (creature.kind == CreatureKind::Cat) {
                feline(kCatSkin);
                continue;
            }

            if (creature.kind == CreatureKind::Ocelot) {
                feline(kOcelotSkin);
                continue;
            }

            if (creature.kind == CreatureKind::Camel) {
                // Read off Mojang's own `geometry.camel`: 15x12x27 body, 9x5x11
                // hump, a 7x8x19 neck running forward from the chest, a 7x14x7
                // head standing on the far end of it, a 5x5x6 muzzle and 5x21x5
                // legs. The body is authored horizontally already and needs no
                // quarter turn.
                //
                // **The neck is the long low bar and the head is the tall box
                // on its end.** This shipped the other way round - the tall box
                // sat back at the shoulder, where it read as a slab bolted onto
                // the animal rather than as any part of a camel.
                uprightBox(place(-0.05f, 1.69f, 0.0f), 15.0f, 12.0f, 27.0f,
                           0.0f, 25.0f, kCamelSkin, 0.0f);
                uprightBox(place(-0.05f, 2.12f, 0.0f), 9.0f, 5.0f, 11.0f,
                           74.0f, 0.0f, kCamelSkin, 0.0f);
                uprightBox(place(0.96875f, 1.625f, 0.0f), 7.0f, 8.0f, 19.0f,
                           60.0f, 24.0f, kCamelSkin, 0.0f);

                // The reference keeps the neck inside the head bone, so a camel
                // there swings its whole neck to look. Ours does not: over a
                // 19-texel neck that is a lunge rather than a glance, and the
                // eyes are painted on the head box anyway. The joint is where
                // the head sits down onto the neck's front end.
                beginHead(1.34375f, 1.875f);
                // Sunk a hundredth into the neck and grown to match. Level with
                // it the two would share a face plane, and every quad here is
                // double-sided, so the join would flicker.
                uprightBox(place(1.34375f, 2.30f, 0.0f), 7.0f, 14.0f, 7.0f,
                           21.0f, 0.0f, kCamelSkin, 0.01f);
                uprightBox(place(1.75f, 2.59375f, 0.0f), 5.0f, 5.0f, 6.0f,
                           50.0f, 0.0f, kCamelSkin, 0.0f);

                uprightBox(place(1.21875f, 2.6875f, 0.28125f), 3.0f, 1.0f, 2.0f,
                           45.0f, 0.0f, kCamelSkin, 0.0f);
                uprightBox(place(1.21875f, 2.6875f, -0.28125f), 3.0f, 1.0f, 2.0f,
                           67.0f, 0.0f, kCamelSkin, 0.0f);
                endHead();

                const float legY = 0.65625f;
                legBox(place(0.58f, legY, 0.32f), 5.0f, 21.0f, 5.0f,
                       0.0f, 0.0f, kCamelSkin, 0.0f, swing);
                legBox(place(0.58f, legY, -0.32f), 5.0f, 21.0f, 5.0f,
                       0.0f, 26.0f, kCamelSkin, 0.0f, -swing);
                legBox(place(-0.65f, legY, 0.32f), 5.0f, 21.0f, 5.0f,
                       58.0f, 16.0f, kCamelSkin, 0.0f, -swing);
                legBox(place(-0.65f, legY, -0.32f), 5.0f, 21.0f, 5.0f,
                       94.0f, 16.0f, kCamelSkin, 0.0f, swing);

                const glm::vec3 tailLow = place(-0.91f, 1.28f, 0.0f);
                const glm::vec3 tailHigh = place(-0.91f, 2.10f, 0.0f);
                skinQuad(tailLow - side * 0.09375f, tailLow + side * 0.09375f,
                         tailHigh + side * 0.09375f, tailHigh - side * 0.09375f,
                         0.70f, kCamelSkin, 122.0f, 0.0f, 3.0f, 14.0f);
                continue;
            }

            const auto equine = [&](float skin, bool longEars, bool bony = false) {
                // One model at three sizes, as the original does it - the
                // donkey and mule differ from the horse only by `modelScale`.
                //
                // Neck, head and muzzle are tilted rather than stacked square.
                // Axis-aligned they read as a fat blunt slab with the pale
                // muzzle sitting beside the eye like a cheek patch; tilting
                // them puts the nose where a nose belongs.
                constexpr float kNeckPitch = 0.50f;
                constexpr float kHeadPitch = 0.32f;

                uprightBox(place(-0.05f, 1.00f, 0.0f), 10.0f, 10.0f, 22.0f,
                           0.0f, 32.0f, skin, 0.0f);
                beginHead(0.40f, 1.11f);
                uprightBox(place(0.58f, 1.44f, 0.0f), 4.0f, 12.0f, 7.0f,
                           0.0f, 35.0f, skin, 0.0f, kNeckPitch);
                uprightBox(place(0.90f, 1.75f, 0.0f), 6.0f, 5.0f, 7.0f,
                           0.0f, 13.0f, skin, 0.0f, kHeadPitch);
                uprightBox(place(1.23f, 1.64f, 0.0f), 4.0f, 5.0f, 5.0f,
                           0.0f, 25.0f, skin, 0.0f, kHeadPitch);
                // The mane runs the length of the neck, so it shares its tilt.
                uprightBox(place(0.46f, 1.34f, 0.0f), 2.0f, 16.0f, 2.0f,
                           56.0f, 36.0f, skin, 0.0f, kNeckPitch);

                const float earHeight = longEars ? 7.0f : 3.0f;
                const float earU = longEars ? 0.0f : 19.0f;
                const float earV = longEars ? 12.0f : 16.0f;
                const float earY = longEars ? 2.19f : 2.06f;
                uprightBox(place(0.77f, earY, 0.13f), 2.0f, earHeight, 1.0f,
                           earU, earV, skin, 0.0f, -0.20f);
                uprightBox(place(0.77f, earY, -0.13f), 2.0f, earHeight, 1.0f,
                           earU, earV, skin, 0.0f, -0.20f, 0.0f, 0.0f, true);
                endHead();

                // Everything that comes in a left-and-right pair mirrors the
                // one on the far side, so the painted flap faces outward on
                // both. Without it a skeleton horse wears its leg bones on the
                // inside down one flank.
                //
                // **The grow is not cosmetic.** A leg's top lands at exactly
                // `0.34375 + 11/32` and the body's underside at exactly
                // `1.00 - 10/32` - the same plane - and every creature quad
                // here is double-sided, so the two fight over every pixel of
                // the hip. On a brown horse both surfaces are opaque and much
                // the same colour so it never showed; on a skeleton the body's
                // underside is barely a third painted and the fight is visible
                // through the gaps. Pushing the leg a few thousandths into the
                // body separates them, which is §14.5b's rule: overlap, never
                // touch.
                constexpr float kHipOverlap = 0.006f;
                const float legY = 0.34375f;
                legBox(place(0.53f, legY, 0.25f), 4.0f, 11.0f, 4.0f,
                       48.0f, 21.0f, skin, kHipOverlap, swing);
                legBox(place(0.53f, legY, -0.25f), 4.0f, 11.0f, 4.0f,
                       48.0f, 21.0f, skin, kHipOverlap, -swing, 0.0f, true);
                legBox(place(-0.55f, legY, 0.25f), 4.0f, 11.0f, 4.0f,
                       48.0f, 21.0f, skin, kHipOverlap, -swing);
                legBox(place(-0.55f, legY, -0.25f), 4.0f, 11.0f, 4.0f,
                       48.0f, 21.0f, skin, kHipOverlap, swing, 0.0f, true);
                // Tilted so the tail trails behind the rump instead of hanging
                // flat against it. **The skeleton horse paints nothing at all
                // in this rect** - measured at 0% coverage - so drawing the box
                // there contributes stray slivers and no tail.
                if (!bony) {
                    uprightBox(place(-0.78f, 0.78f, 0.0f), 3.0f, 14.0f, 4.0f,
                               42.0f, 36.0f, skin, 0.0f, 0.30f);
                }
            };

            if (creature.kind == CreatureKind::Horse) {
                equine(kHorseSkin, false);
                continue;
            }

            if (creature.kind == CreatureKind::Mule) {
                equine(kMuleSkin, true);
                continue;
            }

            // `geometry.horse` unchanged, which is what the reference uses for
            // both. Their skins differ from a horse's on most rows and share
            // its net exactly - those are skeletal holes and a ragged mane in
            // the artwork, not net boundaries, which is precisely why a
            // whole-sheet alpha diff cannot decide a rig.
            if (creature.kind == CreatureKind::SkeletonHorse) {
                equine(kSkeletonHorseSkin, false, true);
                continue;
            }

            if (creature.kind == CreatureKind::ZombieHorse) {
                equine(kZombieHorseSkin, false);
                continue;
            }

            if (creature.kind == CreatureKind::Llama || creature.kind == CreatureKind::TraderLlama) {
                // Independently measured 128x64 layout. The 18-texel body
                // dimension runs forward, while its 10-texel depth becomes
                // vertical after the quarter turn.
                lyingBox(place(-0.08f, 1.1875f, 0.0f), 12.0f, 18.0f, 10.0f,
                         29.0f, 0.0f, kLlamaSkin, 0.0f);
                // Long neck, but not a giraffe: at the previous height the ear
                // tips reached 2.57 m against the horse's 2.15, and a llama is
                // the shorter animal. The neck's lower half stays buried in the
                // chest, which is where a llama's neck actually starts.
                beginHead(0.50f, 0.8875f);
                uprightBox(place(0.50f, 1.45f, 0.0f), 8.0f, 18.0f, 6.0f,
                           0.0f, 14.0f, kLlamaSkin, 0.0f);
                // The muzzle hangs **below** the eyes, and where it sits is
                // measured rather than judged: the eyes are painted on the
                // neck's front rect one to two texels down from its top, and
                // the reference muzzle spans two to six texels down - so its
                // top edge meets the underside of the eyes. Centred any higher
                // it covers them outright, which is what it did until it was
                // measured. It also stands exactly four texels proud of the
                // neck's face, with its own back buried a texel inside.
                uprightBox(place(0.65625f, 1.7625f, 0.0f), 4.0f, 4.0f, 9.0f,
                           0.0f, 0.0f, kLlamaSkin, 0.0f);
                // Sat on the crown rather than hovering over it. The reference
                // puts the ear's base exactly on the neck's top face, which we
                // cannot copy literally - two double-sided quads on one plane
                // fight over it - so each ear is sunk half a texel in and drawn
                // a quarter texel narrower than the head. Both differences are
                // invisible; the shared plane would not have been.
                uprightBox(place(0.5f, 2.075f, 0.1406f), 3.0f, 3.0f, 2.0f,
                           17.0f, 0.0f, kLlamaSkin, 0.0f);
                uprightBox(place(0.5f, 2.075f, -0.1406f), 3.0f, 3.0f, 2.0f,
                           17.0f, 0.0f, kLlamaSkin, 0.0f);
                endHead();

                const float legY = 0.4375f;
                legBox(place(0.38f, legY, 0.28f), 4.0f, 14.0f, 4.0f,
                       29.0f, 29.0f, kLlamaSkin, 0.0f, swing);
                legBox(place(0.38f, legY, -0.28f), 4.0f, 14.0f, 4.0f,
                       29.0f, 29.0f, kLlamaSkin, 0.0f, -swing);
                legBox(place(-0.46f, legY, 0.28f), 4.0f, 14.0f, 4.0f,
                       29.0f, 29.0f, kLlamaSkin, 0.0f, -swing);
                legBox(place(-0.46f, legY, -0.28f), 4.0f, 14.0f, 4.0f,
                       29.0f, 29.0f, kLlamaSkin, 0.0f, swing);

                // The trader's pack is a second cutout shell over the body, on
                // the same net as the llama under it - the reference draws its
                // decor over an ordinary llama rather than replacing the skin,
                // which is the sheep-fleece arrangement exactly.
                if (creature.kind == CreatureKind::TraderLlama) {
                    lyingBox(place(-0.08f, 1.1875f, 0.0f), 12.0f, 18.0f, 10.0f,
                             29.0f, 0.0f, kTraderLlamaDecorSkin, 0.25f * kTexel);
                }
                continue;
            }

            if (creature.kind == CreatureKind::Donkey) {
                equine(kDonkeySkin, true);
                continue;
            }

            if (creature.kind == CreatureKind::Goat) {
                // Measured off goat.png. The animal is a body carrying a woolly
                // neck ruff wider and taller than it is, with only a narrow face
                // poking out the front - which is why the head net is 5 across
                // and 10 deep, with an eye on each *side* rect rather than on
                // the front. The ruff's bottom rect is transparent because it
                // sits down inside the body, so it must overlap far enough to
                // keep that hole out of sight; its side rects are cut into a fur
                // fringe along the last five rows.
                uprightBox(place(-0.10f, 0.71875f, 0.0f), 9.0f, 11.0f, 16.0f,
                           1.0f, 1.0f, kGoatSkin, 0.0f);
                uprightBox(place(0.30f, 0.875f, 0.0f), 11.0f, 14.0f, 11.0f,
                           0.0f, 28.0f, kGoatSkin, 0.0f);
                // The ruff is shoulders rather than neck, so it stays with the
                // body; everything forward of it rides on the head.
                beginHead(0.5375f, 1.05f);
                uprightBox(place(0.85f, 1.05f, 0.0f), 5.0f, 8.0f, 10.0f,
                           34.0f, 46.0f, kGoatSkin, 0.0f);

                // Horns and ears each share one net between the two sides.
                uprightBox(place(0.62f, 1.45f, 0.12f), 2.0f, 7.0f, 2.0f,
                           12.0f, 55.0f, kGoatSkin, 0.0f);
                uprightBox(place(0.62f, 1.45f, -0.12f), 2.0f, 7.0f, 2.0f,
                           12.0f, 55.0f, kGoatSkin, 0.0f);
                uprightBox(place(0.80f, 1.18f, 0.22f), 3.0f, 2.0f, 1.0f,
                           2.0f, 61.0f, kGoatSkin, 0.0f);
                uprightBox(place(0.80f, 1.18f, -0.22f), 3.0f, 2.0f, 1.0f,
                           2.0f, 61.0f, kGoatSkin, 0.0f);
                // The beard is a short tuft whose lower rows are cut into loose
                // strands, and whose own top and bottom rects are unused - so
                // it has to overlap the jaw or that hole shows from above.
                uprightBox(place(1.00f, 0.62f, 0.0f), 4.0f, 7.0f, 1.0f,
                           23.0f, 56.0f, kGoatSkin, 0.0f);
                endHead();

                // Front and hind legs are different lengths and have separate
                // nets. The front pair is four texels longer and its top is
                // buried in the chest so every hoof still meets the ground.
                legBox(place(0.28f, 0.3125f, 0.19f), 3.0f, 10.0f, 3.0f,
                       35.0f, 2.0f, kGoatSkin, 0.0f, swing);
                legBox(place(0.28f, 0.3125f, -0.19f), 3.0f, 10.0f, 3.0f,
                       49.0f, 2.0f, kGoatSkin, 0.0f, -swing);
                legBox(place(-0.42f, 0.1875f, 0.19f), 3.0f, 6.0f, 3.0f,
                       36.0f, 29.0f, kGoatSkin, 0.0f, -swing);
                legBox(place(-0.42f, 0.1875f, -0.19f), 3.0f, 6.0f, 3.0f,
                       49.0f, 29.0f, kGoatSkin, 0.0f, swing);
                continue;
            }

            if (creature.kind == CreatureKind::Rabbit) {
                // Measured off rabbit_brown.png: an 8x6x10 body, a 5x5x5 head
                // carrying the face, ears on their own 2x5x1 nets, a 4x4x4
                // haunch shared by both sides, 2x4x2 front legs, and hind feet
                // that are 2x1x6 planks lying flat on the ground.
                //
                // The torso is stood almost on end, and rotating it that far
                // rotates its net with it - which lands right rather than wrong:
                // the belly rect ends up facing forward and the rump rect
                // downward, which is how a rabbit sitting on its haunches is
                // actually arranged. Upright it is far taller than it is long,
                // which is what the much smaller `modelScale` pays for.
                constexpr float kSitPitch = -1.4835f;  // 85 degrees off level

                // The hop is real, so the pose is read back out of the physics
                // instead of driven by a sine: +1 at push-off, 0 at the apex,
                // -1 on the way down.
                const float phase = std::clamp(creature.velocity.y / species.hopLaunch, -1.0f, 1.0f);
                // Hind legs are the whole gait, and they behave like springs:
                // extended at both ends of the arc and folded at the top,
                // trailing behind on push-off and swung forward to catch the
                // landing. The forelegs take no part at all - a rabbit does not
                // walk on them.
                const float tuck = creature.onGround ? 0.0f : 1.0f - std::abs(phase);
                const float reach = creature.onGround ? 0.0f : -phase;
                // Compressed at the moment it lands, extending into the launch.
                const float squash =
                    std::clamp(creature.hopTimer / species.hopGather, 0.0f, 1.0f) * 0.05f;

                uprightBox(place(-0.02f, 0.50f - squash, 0.0f), 8.0f, 6.0f, 10.0f,
                           0.0f, 0.0f, kRabbitSkin, 0.0f, kSitPitch);
                beginHead(0.06f, 0.74375f - squash);
                uprightBox(place(0.06f, 0.90f - squash, 0.0f), 5.0f, 5.0f, 5.0f,
                           0.0f, 16.0f, kRabbitSkin, 0.0f, -0.15f);
                // Set well apart on the crown: at a narrower spacing the pair
                // meet over the brow and read as a helmet rather than as ears.
                uprightBox(place(0.01f, 1.18f - squash, 0.10f), 2.0f, 5.0f, 1.0f,
                           26.0f, 0.0f, kRabbitSkin, 0.0f, -0.10f);
                uprightBox(place(0.01f, 1.18f - squash, -0.10f), 2.0f, 5.0f, 1.0f,
                           32.0f, 0.0f, kRabbitSkin, 0.0f, -0.10f);
                endHead();

                // The hip travels with the foot, but through a shorter arc.
                uprightBox(place(-0.06f + reach * 0.08f, 0.19f + tuck * 0.07f, 0.15f), 4.0f, 4.0f, 4.0f,
                           20.0f, 16.0f, kRabbitSkin, 0.0f);
                uprightBox(place(-0.06f + reach * 0.08f, 0.19f + tuck * 0.07f, -0.15f), 4.0f, 4.0f, 4.0f,
                           20.0f, 16.0f, kRabbitSkin, 0.0f);

                // Held against the chest and never moved.
                uprightBox(place(0.17f, 0.36f - squash, 0.10f), 2.0f, 4.0f, 2.0f,
                           36.0f, 18.0f, kRabbitSkin, 0.0f);
                uprightBox(place(0.17f, 0.36f - squash, -0.10f), 2.0f, 4.0f, 2.0f,
                           44.0f, 18.0f, kRabbitSkin, 0.0f);

                uprightBox(place(0.04f + reach * 0.18f, 0.03125f + tuck * 0.12f, 0.115f), 2.0f, 1.0f, 6.0f,
                           20.0f, 24.0f, kRabbitSkin, 0.0f);
                uprightBox(place(0.04f + reach * 0.18f, 0.03125f + tuck * 0.12f, -0.115f), 2.0f, 1.0f, 6.0f,
                           36.0f, 24.0f, kRabbitSkin, 0.0f);
                continue;
            }

            if (creature.kind == CreatureKind::Wolf) {
                // The reference keeps a whole second skin for an angry wolf and
                // the only difference is forty pixels: the eyes go red and the
                // brow drops. Cheaper than a shader trick and it is what the
                // artist will already be working from.
                const float wolfSkin =
                    creature.target != CreatureTarget::None ? kWolfAngrySkin : kWolfSkin;
                // Measured off wolf.png: head 6x6x4 at (0,0), snout 3x3x4 at
                // (0,10), a shared 2x2x1 ear net at (16,14), the shaggy
                // shoulder mane 8x6x7 at (21,0), body 6x9x6 at (18,14), and one
                // 2x8x2 net serving all four legs with a second for the tail.
                //
                // The mane is the piece that makes it a wolf rather than a dog:
                // it is wider than both the head and the body, so it reads as a
                // ruff standing proud of the neck.
                // The mane is laid down the same quarter turn as the torso, not
                // stood upright: on end it is a broad slab that swallows the
                // head, and turned it becomes the tall narrow ruff that makes
                // the animal read as a wolf rather than a dog. It is also wider
                // than both the head and the body, so it stands proud of them.
                lyingBox(place(-0.05f, 0.6875f, 0.0f), 6.0f, 9.0f, 6.0f,
                         18.0f, 14.0f, wolfSkin, 0.0f);
                lyingBox(place(0.28f, 0.66f, 0.0f), 8.0f, 6.0f, 7.0f,
                         21.0f, 0.0f, wolfSkin, 0.0f);
                beginHead(0.455f, 0.74f);
                uprightBox(place(0.58f, 0.74f, 0.0f), 6.0f, 6.0f, 4.0f,
                           0.0f, 0.0f, wolfSkin, 0.0f);
                uprightBox(place(0.82f, 0.68f, 0.0f), 3.0f, 3.0f, 4.0f,
                           0.0f, 10.0f, wolfSkin, 0.0f);

                // Two texels either side of centre, as the reference has them.
                // Closer together they meet over the brow and read as one block.
                uprightBox(place(0.52f, 0.94f, 0.125f), 2.0f, 2.0f, 1.0f,
                           16.0f, 14.0f, wolfSkin, 0.0f);
                uprightBox(place(0.52f, 0.94f, -0.125f), 2.0f, 2.0f, 1.0f,
                           16.0f, 14.0f, wolfSkin, 0.0f);
                endHead();

                const float legY = 0.25f;
                legBox(place(0.22f, legY, 0.10f), 2.0f, 8.0f, 2.0f,
                       0.0f, 18.0f, wolfSkin, 0.0f, swing);
                legBox(place(0.22f, legY, -0.10f), 2.0f, 8.0f, 2.0f,
                       0.0f, 18.0f, wolfSkin, 0.0f, -swing);
                legBox(place(-0.24f, legY, 0.10f), 2.0f, 8.0f, 2.0f,
                       0.0f, 18.0f, wolfSkin, 0.0f, -swing);
                legBox(place(-0.24f, legY, -0.10f), 2.0f, 8.0f, 2.0f,
                       0.0f, 18.0f, wolfSkin, 0.0f, swing);

                // Held out straight behind when it is coming for you, drooping
                // otherwise - the reference reads a wolf's mood off its tail and
                // so should we.
                const float tailPitch = creature.target != CreatureTarget::None ? 1.30f : 0.45f;
                uprightBox(place(-0.40f, 0.70f, 0.0f), 2.0f, 8.0f, 2.0f,
                           9.0f, 18.0f, wolfSkin, 0.0f, tailPitch);
                continue;
            }

            if (creature.kind == CreatureKind::Frog) {
                // Measured off frog_temperate.png. The two big nets are the same
                // 7x9 footprint and stack rather than sitting end to end: the
                // 7x3x9 at (3,1) is the patterned back and the 7x2x9 at (0,13)
                // the pale belly under it. Read as head-and-body instead they
                // make an animal twice as long as it is wide, which a frog is
                // not. Being the same width they would also share a side plane
                // and z-fight, so the belly is inset and the back wins.
                //
                // The eyes are their own 3x2x3 boxes sunk halfway into the skull,
                // which is what gives a frog its bulge. Legs share two nets and
                // the four webbed feet are flat 4x4 sprites, like the chicken's.
                const float phase = std::clamp(creature.velocity.y / species.hopLaunch, -1.0f, 1.0f);
                // A frog folds its legs right up under itself in the air and
                // splays them to land. Same spring as the rabbit, far more of it.
                const float tuck = creature.onGround ? 0.0f : 1.0f - std::abs(phase);
                const float reach = creature.onGround ? 0.0f : -phase;
                const float squash =
                    std::clamp(creature.hopTimer / species.hopGather, 0.0f, 1.0f) * 0.04f;

                // The two big nets are not interchangeable, and the alpha says
                // which way up before any judgement does: the (3,1) net has no
                // TOP rect and the (0,13) net has no BOTTOM one, so they
                // interlock with (0,13) resting on (3,1). Read the other way
                // round - which is how this shipped - the missing top faces the
                // sky and the frog is hollow from above. Colour agrees: the
                // exposed (0,13) top is the saturated back at rgb(209,121,75),
                // the exposed (3,1) underside the paler belly at (203,147,93).
                //
                // Being the same width they would share a side plane and
                // z-fight, so the belly is inset and the patterned back wins.
                uprightBox(place(0.0f, 0.11f - squash, 0.0f), 7.0f, 3.0f, 9.0f,
                           3.0f, 1.0f, kFrogSkin, -0.008f);
                uprightBox(place(0.0f, 0.2575f - squash, 0.0f), 7.0f, 2.0f, 9.0f,
                           0.0f, 13.0f, kFrogSkin, 0.0f);

                // Grown so the eyes' 2-texel height does not match the back's
                // exactly where the two meet.
                uprightBox(place(0.18f, 0.375f - squash, 0.13f), 3.0f, 2.0f, 3.0f,
                           0.0f, 0.0f, kFrogSkin, 0.005f);
                uprightBox(place(0.18f, 0.375f - squash, -0.13f), 3.0f, 2.0f, 3.0f,
                           0.0f, 5.0f, kFrogSkin, 0.005f);

                const auto webbedFoot = [&](float alongForward, float alongSide, float rx, float ry) {
                    constexpr float half = 0.125f;
                    const glm::vec3 c = place(alongForward, 0.012f, alongSide);
                    skinQuad(c - forward * half - side * half, c + forward * half - side * half,
                             c + forward * half + side * half, c - forward * half + side * half,
                             0.75f, kFrogSkin, rx, ry, 4.0f, 4.0f);
                };

                const float thighY = 0.09f + tuck * 0.08f;
                uprightBox(place(0.16f + reach * 0.04f, thighY, 0.24f), 2.0f, 3.0f, 3.0f,
                           0.0f, 32.0f, kFrogSkin, 0.0f);
                uprightBox(place(0.16f + reach * 0.04f, thighY, -0.24f), 2.0f, 3.0f, 3.0f,
                           0.0f, 32.0f, kFrogSkin, 0.0f);
                webbedFoot(0.26f + reach * 0.08f, 0.24f, 12.0f, 35.0f);
                webbedFoot(0.26f + reach * 0.08f, -0.24f, 28.0f, 35.0f);

                // The hind pair does the work, so it swings through twice the arc.
                uprightBox(place(-0.20f + reach * 0.08f, thighY, 0.26f), 2.0f, 3.0f, 3.0f,
                           0.0f, 38.0f, kFrogSkin, 0.0f);
                uprightBox(place(-0.20f + reach * 0.08f, thighY, -0.26f), 2.0f, 3.0f, 3.0f,
                           0.0f, 38.0f, kFrogSkin, 0.0f);
                webbedFoot(-0.30f + reach * 0.18f, 0.26f, 13.0f, 42.0f);
                webbedFoot(-0.30f + reach * 0.18f, -0.26f, 27.0f, 42.0f);
                continue;
            }

            if (creature.kind == CreatureKind::Fox) {
                // Measured off fox.png (48x32): head 8x6x6 at (1,5), ears 2x2x1
                // at (8,1) and (15,1), snout 4x2x3 at (6,18), body 6x11x6 at
                // (24,15) laid down, tail 4x9x5 at (30,0), and two 2x6x2 leg
                // nets at (4,24) and (13,24) shared front to back.
                //
                // Long and low, with the head carried level with the spine
                // rather than above it - that line is most of what separates a
                // fox from a small dog.
                lyingBox(place(-0.02f, 0.5625f, 0.0f), 6.0f, 11.0f, 6.0f,
                         24.0f, 15.0f, kFoxSkin, 0.0f);
                // Grown a sixth of a texel: level with the body the head's
                // half-height matches the body's exactly, and two double-sided
                // quads sharing a plane flicker along their diagonal.
                beginHead(0.2325f, 0.5625f);
                uprightBox(place(0.42f, 0.5625f, 0.0f), 8.0f, 6.0f, 6.0f,
                           1.0f, 5.0f, kFoxSkin, 0.01f);
                uprightBox(place(0.68f, 0.50f, 0.0f), 4.0f, 2.0f, 3.0f,
                           6.0f, 18.0f, kFoxSkin, 0.0f);
                uprightBox(place(0.38f, 0.80f, 0.14f), 2.0f, 2.0f, 1.0f,
                           8.0f, 1.0f, kFoxSkin, 0.0f);
                uprightBox(place(0.38f, 0.80f, -0.14f), 2.0f, 2.0f, 1.0f,
                           15.0f, 1.0f, kFoxSkin, 0.0f);
                endHead();

                // Pitched back past vertical so the brush trails behind and
                // dips, and swept side to side at half the stride - a fox's
                // tail is the part of it that reads as alive.
                const float sway = std::sin(creature.gait * 0.5f) * 0.10f;
                uprightBox(place(-0.58f, 0.50f, sway), 4.0f, 9.0f, 5.0f,
                           30.0f, 0.0f, kFoxSkin, 0.0f, -1.75f);

                // Legs are 6 tall against a body 6 deep, so they take the same
                // nudge the head does.
                const float legY = 0.1875f;
                legBox(place(0.20f, legY, 0.11f), 2.0f, 6.0f, 2.0f,
                       13.0f, 24.0f, kFoxSkin, 0.005f, swing);
                legBox(place(0.20f, legY, -0.11f), 2.0f, 6.0f, 2.0f,
                       4.0f, 24.0f, kFoxSkin, 0.005f, -swing);
                legBox(place(-0.24f, legY, 0.11f), 2.0f, 6.0f, 2.0f,
                       13.0f, 24.0f, kFoxSkin, 0.005f, -swing);
                legBox(place(-0.24f, legY, -0.11f), 2.0f, 6.0f, 2.0f,
                       4.0f, 24.0f, kFoxSkin, 0.005f, swing);
                continue;
            }

            if (creature.kind == CreatureKind::PolarBear) {
                // Measured off polarbear.png (128x64): head 7x7x7 at (0,0), a
                // shared 2x2x1 ear net at (26,0), snout 5x3x3 at (0,44), body
                // 14x14x11 at (0,19), and a second 12x12x10 slab at (39,0).
                //
                // That second slab is the shoulder hump: the reference builds
                // the torso from two stacked boxes rather than one, and the
                // hump is the entire silhouette of a bear. Reading it as a head
                // would have been the obvious mistake - the real head is the
                // 7x7x7, which is far smaller than it looks like it should be.
                //
                // Front legs are 8 deep against the hind pair's 6, which is
                // what gives a bear its heavy front end.
                // The two torso boxes sit END TO END along the length, not
                // stacked. In the reference they are contiguous on the axis the
                // quarter turn maps to *forward*, so together they make a
                // 26-texel body - the 12x12x10 is the chest and shoulders in
                // front, the 14x14x11 the haunches behind. Stacking them
                // vertically, which is how this first shipped, buries one inside
                // the other and the bear comes out stubby.
                //
                // Front legs are 8 deep against the hind pair's 6, which is what
                // gives a bear its heavy front end.
                const float lumber = std::sin(creature.gait * 0.5f) * 0.02f;
                lyingBox(place(-0.42f, 0.96875f + lumber, 0.0f), 14.0f, 14.0f, 11.0f,
                         0.0f, 19.0f, kPolarBearSkin, 0.0f);
                lyingBox(place(0.375f, 0.9375f + lumber, 0.0f), 12.0f, 12.0f, 10.0f,
                         39.0f, 0.0f, kPolarBearSkin, 0.0f);
                beginHead(0.69125f, 1.05f + lumber);
                uprightBox(place(0.91f, 1.05f + lumber, 0.0f), 7.0f, 7.0f, 7.0f,
                           0.0f, 0.0f, kPolarBearSkin, 0.0f);
                uprightBox(place(1.19f, 1.00f + lumber, 0.0f), 5.0f, 3.0f, 3.0f,
                           0.0f, 44.0f, kPolarBearSkin, 0.0f);
                uprightBox(place(0.86f, 1.31f + lumber, 0.14f), 2.0f, 2.0f, 1.0f,
                           26.0f, 0.0f, kPolarBearSkin, 0.0f);
                uprightBox(place(0.86f, 1.31f + lumber, -0.14f), 2.0f, 2.0f, 1.0f,
                           26.0f, 0.0f, kPolarBearSkin, 0.0f);
                endHead();

                const float legY = 0.3125f;
                legBox(place(0.45f, legY, 0.28f), 4.0f, 10.0f, 8.0f,
                       50.0f, 22.0f, kPolarBearSkin, 0.005f, swing);
                legBox(place(0.45f, legY, -0.28f), 4.0f, 10.0f, 8.0f,
                       50.0f, 22.0f, kPolarBearSkin, 0.005f, -swing);
                legBox(place(-0.62f, legY, 0.28f), 4.0f, 10.0f, 6.0f,
                       50.0f, 40.0f, kPolarBearSkin, 0.005f, -swing);
                legBox(place(-0.62f, legY, -0.28f), 4.0f, 10.0f, 6.0f,
                       50.0f, 40.0f, kPolarBearSkin, 0.005f, swing);
                continue;
            }

            if (creature.kind == CreatureKind::Panda) {
                // Measured off panda.png (64x64): head 13x10x9 at (0,6), nose
                // 7x5x2 at (45,16), a shared 5x4x1 ear net at (52,25), body
                // 19x26x13 at (0,25) laid down, and one 6x9x6 leg net at (40,0)
                // serving all four. A barrel on stubby legs - the body alone is
                // wider than the head is tall.
                //
                // A waddle rather than a bob: the torso shifts side to side at
                // half the stride and the head follows it a little late, which
                // is what a panda's walk actually reads as. Rolling the model
                // would be the real answer and there is no roll axis.
                const float waddle = std::sin(creature.gait * 0.5f) * 0.05f;
                lyingBox(place(-0.15f, 0.85625f, waddle), 19.0f, 26.0f, 13.0f,
                         0.0f, 25.0f, kPandaSkin, 0.0f);
                beginHead(0.51875f, 0.85f);
                uprightBox(place(0.80f, 0.85f, waddle * 0.6f), 13.0f, 10.0f, 9.0f,
                           0.0f, 6.0f, kPandaSkin, 0.0f);
                uprightBox(place(1.11f, 0.80f, waddle * 0.6f), 7.0f, 5.0f, 2.0f,
                           45.0f, 16.0f, kPandaSkin, 0.0f);
                uprightBox(place(0.73f, 1.2575f, waddle * 0.6f + 0.28f), 5.0f, 4.0f, 1.0f,
                           52.0f, 25.0f, kPandaSkin, 0.0f);
                uprightBox(place(0.73f, 1.2575f, waddle * 0.6f - 0.28f), 5.0f, 4.0f, 1.0f,
                           52.0f, 25.0f, kPandaSkin, 0.0f);
                endHead();

                // Legs sit under the ends of a 26-texel body, not tucked toward
                // its middle - a panda's hind legs are right at the rump.
                const float legY = 0.28125f;
                legBox(place(0.45f, legY, 0.30f), 6.0f, 9.0f, 6.0f,
                       40.0f, 0.0f, kPandaSkin, 0.005f, swing);
                legBox(place(0.45f, legY, -0.30f), 6.0f, 9.0f, 6.0f,
                       40.0f, 0.0f, kPandaSkin, 0.005f, -swing);
                legBox(place(-0.72f, legY, 0.30f), 6.0f, 9.0f, 6.0f,
                       40.0f, 0.0f, kPandaSkin, 0.005f, -swing);
                legBox(place(-0.72f, legY, -0.30f), 6.0f, 9.0f, 6.0f,
                       40.0f, 0.0f, kPandaSkin, 0.005f, swing);
                continue;
            }

            if (creature.kind == CreatureKind::MagmaCubeSmall ||
                creature.kind == CreatureKind::MagmaCubeMedium ||
                creature.kind == CreatureKind::MagmaCubeLarge) {
                // `geometry.lavaslime`, and it is **not** a scaled slime, which
                // is the obvious guess: the reference builds the shell from
                // eight separate 8x1x8 slabs stacked on a 4x4x4 core, and pulls
                // them apart as it jumps so the glow shows between them. One
                // box could never do that, and the gaps are the whole animal.
                //
                // Every slab reads the same band. The reference gives each its
                // own row, but our reference texture is Java's and only the
                // first row carries a full 32-wide band there - the other seven
                // measure half covered, which would render four faces of each
                // slab as nothing. Measured, not assumed.
                // Every slab and the core read Java's own 64x64 layout, taken
                // off the alpha rather than from Bedrock's geometry - **the two
                // genuinely disagree here**, and Bedrock's is a 64x32 sheet we
                // do not have. Four slabs run down the left column at u=0 and
                // four down the right at u=32, nine rows apart.
                //
                // **Index 0 is the top slab**, and that ordering is what puts
                // the eyes where they belong: they are painted on the front
                // faces of the third and fourth rows down, which is just above
                // the middle. Reading one shared row for all eight is what lost
                // them entirely.
                static constexpr float kSlabU[8]{0.0f, 0.0f, 0.0f, 0.0f, 32.0f, 32.0f, 32.0f, 32.0f};
                static constexpr float kSlabV[8]{0.0f, 9.0f, 18.0f, 27.0f, 0.0f, 9.0f, 18.0f, 27.0f};

                // Shut on the ground and open in the air. The reference parts
                // the slabs only while it is jumping, so a resting magma cube
                // is a solid cube exactly the size of a slime - which is why a
                // creature that has never ticked defaults to standing, or the
                // showcase would only ever show it mid-leap.
                const float spread = creature.onGround ? 0.0f : 0.55f;
                for (int i = 0; i < 8; ++i) {
                    const int row = 7 - i;
                    const float rest = (static_cast<float>(i) + 0.5f) * kTexel;
                    // Fanned about the middle of the stack so it swells evenly
                    // rather than lifting off its own feet.
                    const float lift = (static_cast<float>(i) - 3.5f) * kTexel * spread;
                    uprightBox(place(0.0f, rest + lift, 0.0f), 8.0f, 1.0f, 8.0f,
                               kSlabU[row], kSlabV[row], kMagmaCubeSkin, 0.0f);
                }
                // Only ever seen through the gaps, so it sits inside the stack
                // rather than under it.
                uprightBox(place(0.0f, 0.25f, 0.0f), 4.0f, 4.0f, 4.0f,
                           24.0f, 40.0f, kMagmaCubeSkin, 0.0f);
                continue;
            }

            if (creature.kind == CreatureKind::SlimeSmall || creature.kind == CreatureKind::SlimeMedium ||
                creature.kind == CreatureKind::SlimeLarge) {
                // Measured off slime.png (64x32): an 8x8x8 outer shell at (0,0),
                // a 6x6x6 inner core at (0,16), two 2x2x2 eyes at (32,0) and
                // (32,4), and a 1x1x1 mouth at (32,8). All three sizes are the
                // same model; only `modelScale` differs, which is why the sizes
                // are three rows rather than a field.
                //
                // Two layers, and the layering is the whole animal: an opaque
                // core carrying the face, inside a translucent gel shell. The
                // face therefore sits *within* the body and is read through the
                // shell, which is why the eyes are inset rather than stuck on
                // the outside - mounted proud of the shell they read as blobs
                // glued to a green box.
                //
                // Squash is the animation: it flattens and spreads on landing,
                // then rounds out into the launch, which is most of what makes a
                // cube read as alive.
                const float squash = std::clamp(creature.hopTimer / species.hopGather, 0.0f, 1.0f);
                const float flat = 1.0f - squash * 0.22f;
                const float wide = 1.0f + squash * 0.14f;
                const float coreY = 0.25f * flat;

                uprightBox(place(0.0f, coreY, 0.0f), 6.0f * wide, 6.0f * flat, 6.0f * wide,
                           0.0f, 16.0f, kSlimeSkin, 0.0f);

                // Offsets taken from the reference model rather than eyeballed.
                // Both the eyes and the mouth stand half a texel proud of the
                // core's face, but they are 2 and 1 texels deep, so they need
                // *different* centres to do it - giving them the same one puts
                // the mouth's front face exactly on the core's front plane and
                // the pair flicker over each other.
                //
                // The reference lets the eyes overhang the core's sides by a
                // quarter texel. Pulled in to sit wholly inside it instead:
                // spilling over an edge reads as a mistake at this size, and
                // flush would be another shared plane.
                const float coreFront = 0.1875f * wide;
                const float eyeSide = 0.115f * wide;
                uprightBox(place(coreFront - 0.03125f, coreY + 0.0625f * flat, eyeSide), 2.0f, 2.0f, 2.0f,
                           32.0f, 0.0f, kSlimeSkin, 0.0f);
                uprightBox(place(coreFront - 0.03125f, coreY + 0.0625f * flat, -eyeSide), 2.0f, 2.0f, 2.0f,
                           32.0f, 4.0f, kSlimeSkin, 0.0f);
                uprightBox(place(coreFront, coreY - 0.03125f * flat, 0.03125f * wide), 1.0f, 1.0f, 1.0f,
                           32.0f, 8.0f, kSlimeSkin, 0.0f);

                // Only one of the eye's four texels is actually black; the rest
                // are a dark green socket. So how black an eye reads is set by
                // how much shell green is laid over it, and this alpha is the
                // dial for that - not the texture.
                target = &translucent;
                quadAlpha = 0.40f;
                uprightBox(place(0.0f, coreY, 0.0f), 8.0f * wide, 8.0f * flat, 8.0f * wide,
                           0.0f, 0.0f, kSlimeSkin, 0.0f);
                target = &mesh;
                quadAlpha = 1.0f;
                continue;
            }

            if (creature.kind == CreatureKind::Spider || creature.kind == CreatureKind::CaveSpider) {
                // Rebuilt from Mojang's `geometry.spider.v1.8`. **One height
                // carries the whole animal**: thorax, head, abdomen and all
                // eight leg joints sit at texel 9 of 16, and the legs droop
                // from there to the floor. The body used to sit at 0.40 with
                // the legs as level bars either side of it, which read as a
                // spider lying on its belly rather than standing on its legs.
                //
                // `cave_spider.png` has byte-identical alpha, so it is the same
                // rig at a smaller `modelScale` - the arrangement the ocelot
                // shares with the cat.
                const float skin = creature.kind == CreatureKind::Spider ? kSpiderSkin : kCaveSpiderSkin;
                constexpr float kJoint = 9.0f * kTexel;

                // The reference butts all three body boxes face to face. We
                // cannot: every quad here is double-sided, so a shared plane
                // flickers. Each is nudged a hundredth into its neighbour and
                // given its own grow, so nothing lines up with anything.
                uprightBox(place(-0.5525f, kJoint, 0.0f), 10.0f, 8.0f, 12.0f,
                           0.0f, 12.0f, skin, 0.004f);
                uprightBox(place(0.0f, kJoint, 0.0f), 6.0f, 6.0f, 6.0f,
                           0.0f, 0.0f, skin, 0.0f);
                beginHead(0.1875f, kJoint);
                uprightBox(place(0.4275f, kJoint, 0.0f), 8.0f, 8.0f, 8.0f,
                           32.0f, 4.0f, skin, 0.006f);
                endHead();

                // `animation.spider.default_leg_pose`, rear pair first. The fan
                // sweeps the back pair behind and the front pair ahead; the
                // splay is the droop, 45 degrees on the outer pairs and 33.3 on
                // the two inner ones - and **the splay is the whole reason the
                // body is off the ground**.
                constexpr float kFan[4]{0.7854f, 0.3927f, -0.3927f, -0.7854f};
                constexpr float kSplay[4]{0.7854f, 0.5812f, 0.5812f, 0.7854f};
                // Where each pair meets the body, in the reference's own
                // one-texel steps from back to front.
                constexpr float kAlong[4]{-0.125f, -0.0625f, 0.0f, 0.0625f};

                const float amplitude = species.gaitSwing * creature.limbSwingAmount;
                for (int pair = 0; pair < 4; ++pair) {
                    // `animation.spider.walk`. Three things about it are worth
                    // knowing, because none of them is what you would write.
                    //
                    // **A quarter turn of phase per pair** is what sets
                    // neighbours against each other - one reaching while the
                    // one behind it is planted - instead of all eight rowing
                    // together, which is what the single shared sine did.
                    //
                    // **The sweep runs at twice the rate of the lift**, so a
                    // leg goes fore and aft twice for every time it picks up.
                    //
                    // **Both are absolute values**, so a leg only ever moves to
                    // one side of its rest pose. That is what keeps the travel
                    // small enough that no tip ever reaches its neighbour's.
                    // Java takes the cosine unsigned; Bedrock does not, and
                    // Bedrock is the reference.
                    const float phase = static_cast<float>(pair) * 1.5708f;
                    const float sweep =
                        -std::abs(std::cos(creature.gait * 2.0f + phase)) * amplitude;
                    const float lift = std::abs(std::sin(creature.gait + phase)) * amplitude;
                    const float yaw = kFan[pair] + sweep;
                    // Subtracted, so the leg straightens toward level as it
                    // picks up and droops again as it plants.
                    const float splay = kSplay[pair] - lift;

                    // The same yaw mirrors on the far side for free - `barBox`
                    // measures it against whichever way the leg points out.
                    barBox(kAlong[pair], kJoint, 0.6875f, 16.0f, 2.0f, 2.0f,
                           18.0f, 0.0f, skin, 0.0f, yaw, splay);
                    barBox(kAlong[pair], kJoint, -0.6875f, 16.0f, 2.0f, 2.0f,
                           18.0f, 0.0f, skin, 0.0f, -yaw, splay);
                }
                continue;
            }

            // Zombie and skeleton are one rig at two limb thicknesses: head
            // 8x8x8 at (0,0), body 8x12x4 at (16,16), and arm and leg nets at
            // (40,16) and (0,16) that are 4 wide for the zombie and 2 for the
            // skeleton. Measured off both sheets; the head and body match
            // exactly, which is what makes one lambda serve both.
            //
            // Legs swing opposite each other and each arm swings opposite its
            // own leg, which is the whole of a walk cycle. Every part is grown a
            // third of a texel because a 12-tall limb against a 12-tall torso
            // otherwise shares a face plane exactly.
            const auto biped = [&](float skin, float limb, bool armsOut, float inflate = 0.0f) {
                const float half = limb * kTexel * 0.5f;
                const float grow = 0.005f + inflate;
                beginHead(0.0f, 1.5f);
                uprightBox(place(0.0f, 1.75f, 0.0f), 8.0f, 8.0f, 8.0f,
                           0.0f, 0.0f, skin, grow);
                endHead();
                uprightBox(place(0.0f, 1.125f, 0.0f), 8.0f, 12.0f, 4.0f,
                           16.0f, 16.0f, skin, inflate);

                // Hips are a fixed spacing, not derived from limb width: at
                // half a 2-wide skeleton limb the legs meet in the middle and
                // read as one column. A shade wider than the reference's exact
                // touch, because our quads are double-sided and two legs sharing
                // an inner plane would fight over it - and the gap has to open
                // with the shell, or an inflated pair of trousers overlaps in
                // the middle with every extent identical, which is the same
                // fight one step further out.
                const float hip = 0.133f + inflate;
                legBox(place(0.0f, 0.375f, hip), limb, 12.0f, limb,
                       0.0f, 16.0f, skin, grow, swing);
                legBox(place(0.0f, 0.375f, -hip), limb, 12.0f, limb,
                       0.0f, 16.0f, skin, grow, -swing);

                const float shoulder = 0.25f + half;
                // A zombie's arms both come down together - the reference's
                // zombie animations mirror the left arm onto the right
                // throughout. Anything holding a weapon swings only the hand it
                // is holding it in, and the other arm just keeps walking.
                if (armsOut) {
                    // Held straight out in front. The pitch is **negative** so
                    // the net's top - the sleeve - stays at the shoulder and the
                    // bare hand ends up at the far end. Positive puts the sleeve
                    // on the hands, which is exactly how this shipped first.
                    //
                    // The rest height is the torso centre, exactly as the
                    // arms-down branch below uses: `legBox` derives the joint
                    // from it, so raising it raises the shoulder. It sat at
                    // 1.42, which put the joint at 1.68 - inside a head that
                    // spans 1.5 to 2.0 - and the arms hinged from the ears.
                    //
                    // The chop is **added**, which swings them down from here.
                    legBox(place(0.0f, 1.125f, shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow, -1.5708f + swayPitch + zombieChop, swayRoll);
                    legBox(place(0.0f, 1.125f, -shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow, -1.5708f - swayPitch + zombieChop, -swayRoll);
                } else {
                    legBox(place(0.0f, 1.125f, shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow, -swing + swayPitch - humanoidRaise, swayRoll);
                    legBox(place(0.0f, 1.125f, -shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow, swing - swayPitch, -swayRoll);
                }
            };

            if (creature.kind == CreatureKind::Zombie) {
                biped(kZombieSkin, 4.0f, true);
                continue;
            }

            if (creature.kind == CreatureKind::Husk) {
                biped(kHuskSkin, 4.0f, true);
                continue;
            }

            if (creature.kind == CreatureKind::Drowned) {
                // The zombie rig twice over: the body, then the reference's
                // `drowned_outer_layer` as a second shell a quarter of a texel
                // larger. That layer is an ordinary alpha-tested cutout like
                // every skin here, so it needs no new rendering path - the same
                // arrangement the sheep's fleece has used since M19.
                biped(kDrownedSkin, 4.0f, true);
                biped(kDrownedOuterSkin, 4.0f, true, 0.25f * kTexel);
                continue;
            }

            if (creature.kind == CreatureKind::Skeleton) {
                biped(kSkeletonSkin, 2.0f, false);
                continue;
            }

            if (creature.kind == CreatureKind::Blackbone) {
                biped(kBlackboneSkin, 2.0f, false);
                continue;
            }

            if (creature.kind == CreatureKind::Stray) {
                biped(kStraySkin, 2.0f, false);
                continue;
            }

            if (creature.kind == CreatureKind::Bogged) {
                biped(kBoggedSkin, 2.0f, false);
                continue;
            }

            if (creature.kind == CreatureKind::Silverfish ||
                creature.kind == CreatureKind::Voidmite) {
                // Seven tapering segments read off the alpha, front to back.
                // There are no legs at all: the wriggle is a wave travelling
                // down the body as a sideways offset, its amplitude growing
                // toward the tail so the head leads and the tail whips.
                struct Segment {
                    float netW, netH, netD;
                    float u, v;
                    float forward, up, grow;
                };
                // The two hindmost are both one texel tall at the same height,
                // and the two before them are both two texels across, so their
                // grows differ - equal extents on an overlapping pair put faces
                // on one plane and every creature quad is double-sided.
                static constexpr Segment kSegments[]{
                    {3.0f, 2.0f, 2.0f, 0.0f, 0.0f, 0.46875f, 0.0625f, 0.005f},
                    {4.0f, 3.0f, 2.0f, 0.0f, 4.0f, 0.34375f, 0.09375f, 0.005f},
                    {6.0f, 4.0f, 3.0f, 0.0f, 9.0f, 0.1875f, 0.125f, 0.005f},
                    {3.0f, 3.0f, 3.0f, 0.0f, 16.0f, 0.0f, 0.09375f, 0.005f},
                    {2.0f, 2.0f, 3.0f, 0.0f, 22.0f, -0.1875f, 0.0625f, 0.005f},
                    {2.0f, 1.0f, 2.0f, 11.0f, 0.0f, -0.34375f, 0.03125f, 0.007f},
                    {1.0f, 1.0f, 2.0f, 13.0f, 4.0f, -0.46875f, 0.03125f, 0.009f},
                };
                // `endermite.geo.json`, four sections rather than seven, with
                // Bedrock's -Z forward negated into ours. Distinct grows
                // because neighbouring sections overlap and every quad here is
                // double-sided.
                static constexpr Segment kVoidmiteSegments[]{
                    {4.0f, 3.0f, 2.0f, 0.0f, 0.0f, 0.2125f, 0.09375f, 0.006f},
                    {6.0f, 4.0f, 5.0f, 0.0f, 5.0f, -0.00625f, 0.125f, 0.0f},
                    {3.0f, 3.0f, 1.0f, 0.0f, 14.0f, -0.1875f, 0.09375f, 0.007f},
                    {1.0f, 2.0f, 1.0f, 0.0f, 18.0f, -0.25f, 0.0625f, 0.009f},
                };
                const bool voidmite = creature.kind == CreatureKind::Voidmite;
                const float vermin = voidmite ? kVoidmiteSkin : kSilverfishSkin;
                const Segment* body = voidmite ? kVoidmiteSegments : kSegments;
                const int bodyCount =
                    voidmite ? static_cast<int>(std::size(kVoidmiteSegments))
                             : static_cast<int>(std::size(kSegments));
                const auto tail = static_cast<float>(bodyCount - 1);
                for (int index = 0; index < bodyCount; ++index) {
                    const Segment& segment = body[index];
                    // A sideways distance rather than a joint angle: there is no
                    // limb here to turn, so this is the one place the swing stays
                    // a translation, and its amplitude belongs beside the shape
                    // it belongs to rather than in the species row.
                    constexpr float kWriggle = 0.10f;
                    const float amplitude =
                        kWriggle * creature.limbSwingAmount * (static_cast<float>(index) / tail);
                    const float wave =
                        std::sin(creature.gait - static_cast<float>(index) * 0.8f) * amplitude;
                    uprightBox(place(segment.forward, segment.up, wave), segment.netW, segment.netH,
                               segment.netD, segment.u, segment.v, vermin, segment.grow);
                }
                continue;
            }

            // Its own rig rather than the biped one: a taller 8x10x8 head with
            // a 2x4x2 nose on it, a deeper 8x12x6 body, upper arms at (44,22)
            // and the crossed forearms at (40,38) that are most of what makes a
            // villager read as a villager. The zombie villager's nets are
            // identical row for row, and the reference tells the two apart by
            // the pose alone - folded arms against a zombie's reach.
            const auto villagerRig = [&](float skin, bool armsOut) {
                beginHead(0.0f, 1.5f);
                uprightBox(place(0.0f, 1.8125f, 0.0f), 8.0f, 10.0f, 8.0f,
                           0.0f, 0.0f, skin, 0.005f);
                // Four texels below the head's centre, matching the reference:
                // the eyes sit in the upper half of the face and the nose hangs
                // from between them past the chin. Level with the eyes - which
                // is where this first sat - it covers them instead.
                uprightBox(place(0.3125f, 1.5625f, 0.0f), 2.0f, 4.0f, 2.0f,
                           24.0f, 0.0f, skin, 0.0f);
                endHead();
                uprightBox(place(0.0f, 1.125f, 0.0f), 8.0f, 12.0f, 6.0f,
                           16.0f, 20.0f, skin, 0.0f);

                // Legs all but touching, as the reference has them. A hair of
                // gap rather than a shared plane, because two double-sided
                // quads on one plane fight over it - 1/32 of a texel, invisible.
                legBox(place(0.0f, 0.375f, 0.131f), 4.0f, 12.0f, 4.0f,
                       0.0f, 22.0f, skin, 0.005f, swing);
                legBox(place(0.0f, 0.375f, -0.131f), 4.0f, 12.0f, 4.0f,
                       0.0f, 22.0f, skin, 0.005f, -swing);

                if (armsOut) {
                    // The arm net is the one place the two sheets differ, and
                    // only the last four rows say so: the villager's side rows
                    // stop at 33 and the zombie villager's run to 37, so this
                    // is a full 12-long zombie limb rather than the villager's
                    // short folded 8. Same offsets as the biped's arms for
                    // exactly that reason - including the torso-centre rest
                    // height, which is what keeps the joint on the shoulder
                    // rather than in the head, and the chop that brings them
                    // down on you. **Only this branch animates**: the folded
                    // assembly below is closed and must stay still.
                    legBox(place(0.0f, 1.125f, 0.375f), 4.0f, 12.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, -1.5708f + swayPitch + zombieChop, swayRoll);
                    legBox(place(0.0f, 1.125f, -0.375f), 4.0f, 12.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, -1.5708f - swayPitch + zombieChop, -swayRoll);
                    return;
                }

                // The arms are **one folded assembly**, not three parts placed
                // separately, and they never animate. In the reference both
                // upper arms and the crossed forearms sit in a single group
                // pitched 0.75 rad as a unit, and the forearm block spans the
                // gap between the two upper arms so the whole thing is one
                // continuous sleeve from shoulder to shoulder.
                //
                // So all three share the pitch, and their centres are the
                // reference's local offsets carried through that rotation
                // rather than guessed. Tilting the arms while leaving the
                // forearms level - which is how this first shipped - is exactly
                // what left the hands floating unattached.
                constexpr float kFold = -0.75f;
                uprightBox(place(0.1477f, 1.221f, 0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold);
                uprightBox(place(0.1477f, 1.221f, -0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold);
                // Cross-section matched to the sleeves exactly - same 0.005
                // grow, so both cut ends are 4.16 texels square - and stretched
                // sideways to reach well into them.
                //
                // The reference lines up *three* pairs of faces between these
                // parts: the undersides, the fronts and the backs. Identical
                // sizes therefore put all three on shared planes, and our quads
                // are double-sided so they fight. Nudging the block a
                // sixteenth of a texel along its own height and depth axes
                // separates every one of them while leaving it the same size to
                // the eye - which sizing it up or down cannot do.
                uprightBox(place(0.2332f, 1.1381f, 0.0f), 8.0f, 4.0f, 4.0f,
                           40.0f, 38.0f, skin, 0.005f, kFold, 0.08f);
            };

            if (creature.kind == CreatureKind::Villager) {
                villagerRig(kVillagerSkin, false);
                continue;
            }

            if (creature.kind == CreatureKind::ZombieVillager) {
                villagerRig(kZombieVillagerSkin, true);
                continue;
            }

            if (creature.kind == CreatureKind::Witch) {
                villagerRig(kWitchSkin, false);
                // Four boxes stacked off the crown, every one measured: brim
                // 10x2x10 at (0,64), then 7x4x7, 4x4x4 and a 1x2x1 tip. Each
                // takes its own small grow so the stack overlaps instead of
                // butting, which would put a shared plane at every joint.
                beginHead(0.0f, 1.5f);
                uprightBox(place(0.0f, 2.1875f, 0.0f), 10.0f, 2.0f, 10.0f,
                           0.0f, 64.0f, kWitchSkin, 0.005f);
                uprightBox(place(0.0f, 2.375f, 0.0f), 7.0f, 4.0f, 7.0f,
                           0.0f, 76.0f, kWitchSkin, 0.004f);
                uprightBox(place(0.0f, 2.625f, 0.0f), 4.0f, 4.0f, 4.0f,
                           0.0f, 87.0f, kWitchSkin, 0.003f);
                uprightBox(place(0.0f, 2.8125f, 0.0f), 1.0f, 2.0f, 1.0f,
                           0.0f, 95.0f, kWitchSkin, 0.002f);
                // The wart, a 1x1x1 sharing the sheet's very first corner with
                // nothing else - the head net's band starts eight texels in.
                // A negative grow shrinks the geometry below a full texel while
                // leaving the net honest, since grow never touches the UVs.
                uprightBox(place(0.395f, 1.545f, 0.022f), 1.0f, 1.0f, 1.0f,
                           0.0f, 0.0f, kWitchSkin, -0.008f);
                endHead();
                continue;
            }

            if (creature.kind == CreatureKind::WanderingTrader) {
                villagerRig(kWanderingTraderSkin, false);
                // The head wrap is a second layer over the same 8x10x8 net at
                // (32,0), inflated a third of a texel. It is transparent
                // everywhere but a band across the middle, which is why it
                // reads as a turban rather than a cap - and why it needs the
                // shader's alpha test, exactly as the sheep's fleece does.
                beginHead(0.0f, 1.5f);
                uprightBox(place(0.0f, 1.8125f, 0.0f), 8.0f, 10.0f, 8.0f,
                           32.0f, 0.0f, kWanderingTraderSkin, 0.02f);
                endHead();
                continue;
            }

            if (creature.kind == CreatureKind::Princepin ||
                creature.kind == CreatureKind::PrincepinBrute ||
                creature.kind == CreatureKind::ZombiePrincepin) {
                // Body, arms and legs are the biped rig at the standard
                // player-skin offsets - right limbs at (0,16) and (40,16), left
                // at (16,48) and (32,48), which is the first model here to use
                // separate nets per side. Only the head is its own: 10 wide
                // rather than 8, carrying two ears, a snout and two tusks.
                //
                // All three are `geometry.piglin` in the shipped data, so the
                // cousins are a skin row apiece and nothing else.
                const float skin = creature.kind == CreatureKind::PrincepinBrute
                                       ? kPrincepinBruteSkin
                                   : creature.kind == CreatureKind::ZombiePrincepin
                                       ? kZombiePrincepinSkin
                                       : kPrincepinSkin;
                beginHead(0.0f, 1.5f);
                uprightBox(place(0.0f, 1.75f, 0.0f), 10.0f, 8.0f, 8.0f,
                           0.0f, 0.0f, skin, 0.005f);
                uprightBox(place(-0.03f, 1.82f, 0.34375f), 1.0f, 5.0f, 4.0f,
                           39.0f, 6.0f, skin, 0.0f);
                uprightBox(place(-0.03f, 1.82f, -0.34375f), 1.0f, 5.0f, 4.0f,
                           51.0f, 6.0f, skin, 0.0f);
                uprightBox(place(0.28125f, 1.72f, 0.0f), 4.0f, 4.0f, 1.0f,
                           31.0f, 1.0f, skin, 0.0f);
                uprightBox(place(0.28f, 1.655f, 0.15625f), 1.0f, 2.0f, 1.0f,
                           2.0f, 0.0f, skin, 0.004f);
                uprightBox(place(0.28f, 1.655f, -0.15625f), 1.0f, 2.0f, 1.0f,
                           2.0f, 4.0f, skin, 0.004f);
                endHead();

                uprightBox(place(0.0f, 1.125f, 0.0f), 8.0f, 12.0f, 4.0f,
                           16.0f, 16.0f, skin, 0.0f);
                constexpr float kPigHip = 0.133f;
                legBox(place(0.0f, 0.375f, kPigHip), 4.0f, 12.0f, 4.0f,
                       16.0f, 48.0f, skin, 0.005f, swing);
                legBox(place(0.0f, 0.375f, -kPigHip), 4.0f, 12.0f, 4.0f,
                       0.0f, 16.0f, skin, 0.005f, -swing);
                constexpr float kPigShoulder = 0.375f;
                legBox(place(0.0f, 1.125f, kPigShoulder), 4.0f, 12.0f, 4.0f,
                       32.0f, 48.0f, skin, 0.005f, -swing + swayPitch - humanoidRaise,
                       swayRoll);
                legBox(place(0.0f, 1.125f, -kPigShoulder), 4.0f, 12.0f, 4.0f,
                       40.0f, 16.0f, skin, 0.005f, swing - swayPitch, -swayRoll);
                continue;
            }

            if (creature.kind == CreatureKind::Cod || creature.kind == CreatureKind::Salmon) {
                // Both are Mojang's `geometry.cod` and `geometry.salmon` read
                // straight off `bedrock-samples`: every cube's centre is its
                // origin plus half its size, converted from Bedrock's axes -
                // forward is -Z there, so our `alongForward` is the negated one.
                //
                // **The body tips to the angle it is swimming at**, about its
                // own mid-height rather than the point between its feet. A
                // pivot at the feet would swing the nose through an arc every
                // time it changed depth - the same fault the head turn had
                // before it was given a neck to turn about.
                const bool cod = creature.kind == CreatureKind::Cod;
                const float skin = cod ? kCodSkin : kSalmonSkin;
                const float bodyUp = cod ? 0.125f : 0.375f;

                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + worldUp * (bodyUp * modelScale * swellTall);

                // A pectoral fin is rotated 35 degrees in the reference, which
                // bakes the turn into an odd cube origin so the bone rotation
                // lands it right. Ours applies the angle as a roll instead and
                // places the fin against the flank, which is the same fin
                // without reproducing a pivot we have no use for.
                constexpr float kFinRoll = 0.6109f;

                if (cod) {
                    uprightBox(place(-0.28125f, 0.125f, 0.0f), 2.0f, 4.0f, 7.0f,
                               0.0f, 0.0f, skin, 0.0f);
                    uprightBox(place(0.03125f, 0.125f, 0.0f), 2.0f, 4.0f, 3.0f,
                               11.0f, 0.0f, skin, 0.0f);
                    uprightBox(place(0.15625f, 0.15625f, 0.0f), 2.0f, 3.0f, 1.0f,
                               0.0f, 0.0f, skin, 0.0f);
                    finBox(place(-0.6875f, 0.125f, 0.0f), 0.0f, 4.0f, 6.0f, 20.0f, 1.0f, skin, 0.0f);
                    finBox(place(-0.1875f, 0.28125f, 0.0f), 0.0f, 1.0f, 6.0f, 20.0f, -6.0f, skin, 0.0f);
                    finBox(place(-0.25f, -0.03125f, 0.0f), 0.0f, 1.0f, 2.0f, 22.0f, -1.0f, skin, 0.0f);
                    uprightBox(place(-0.0625f, 0.03125f, 0.125f), 2.0f, 1.0f, 2.0f,
                               24.0f, 4.0f, skin, 0.002f, 0.0f, 0.0f, kFinRoll);
                    uprightBox(place(-0.0625f, 0.03125f, -0.125f), 2.0f, 1.0f, 2.0f,
                               24.0f, 1.0f, skin, 0.002f, 0.0f, 0.0f, -kFinRoll);
                    continue;
                }

                // Two body halves rather than one long box, which is what lets
                // the reference bend a salmon in the middle. Ours draws them
                // straight - the bend is an animation we do not have - but the
                // nets are the reference's either way.
                uprightBox(place(0.0f, 0.375f, 0.0f), 3.0f, 5.0f, 8.0f, 0.0f, 0.0f, skin, 0.0f);
                uprightBox(place(-0.5f, 0.375f, 0.0f), 3.0f, 5.0f, 8.0f, 0.0f, 13.0f, skin, 0.0f);
                uprightBox(place(0.34375f, 0.40625f, 0.0f), 2.0f, 4.0f, 3.0f, 22.0f, 0.0f, skin, 0.0f);
                finBox(place(-0.1875f, 0.59375f, 0.0f), 0.0f, 2.0f, 2.0f, 4.0f, 2.0f, skin, 0.0f);
                finBox(place(-0.34375f, 0.59375f, 0.0f), 0.0f, 2.0f, 3.0f, 2.0f, 3.0f, skin, 0.0f);
                finBox(place(-0.9375f, 0.375f, 0.0f), 0.0f, 5.0f, 6.0f, 20.0f, 10.0f, skin, 0.0f);
                finBox(place(0.1875f, 0.2417f, 0.15625f), 2.0f, 0.0f, 2.0f, 2.0f, 0.0f, skin,
                       0.0f, kFinRoll);
                finBox(place(0.1875f, 0.2417f, -0.15625f), 2.0f, 0.0f, 2.0f, 2.0f, 0.0f, skin,
                       0.0f, -kFinRoll);
                continue;
            }

            if (creature.kind == CreatureKind::Pufferfish) {
                // Three separate geometries, not one model scaled: the spines
                // are real parts that only exist once it is inflated, which is
                // why `puff` picks a model rather than a size.
                //
                // **But the changeover happens where the two shapes measure the
                // same.** The three bodies are 3, 5 and 8 texels wide, so
                // swapping them outright trebles the fish between one frame and
                // the next. Instead the model is scaled continuously across the
                // transition and handed over at the geometric mean of the two
                // widths - the size both shapes agree on - so what the eye sees
                // is one animal swelling, with only the spines arriving.
                constexpr float kPuffWidth[3]{3.0f, 5.0f, 8.0f};
                const float skin = kPufferfishSkin;
                const int lower = creature.puff < 1.0f ? 0 : 1;
                const float span = std::clamp(creature.puff - static_cast<float>(lower), 0.0f, 1.0f);
                // Eased rather than linear, so it swells and settles.
                const float eased = span * span * (3.0f - 2.0f * span);
                const float cross = std::sqrt(kPuffWidth[lower] * kPuffWidth[lower + 1]);
                const int stage = eased < 0.5f ? lower : lower + 1;
                const float reach = cross / kPuffWidth[stage];
                modelScale *= eased < 0.5f ? 1.0f + (reach - 1.0f) * (eased * 2.0f)
                                           : reach + (1.0f - reach) * ((eased - 0.5f) * 2.0f);

                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                const float bodyUp = stage == 0 ? 0.0625f : (stage == 1 ? 0.21875f : 0.25f);
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + worldUp * (bodyUp * modelScale * swellTall);

                // A spine sits flush against the body in the reference, which
                // shares a face plane with it - and both of ours are drawn from
                // both sides, so each one would flicker. A sixteenth of a texel
                // outward separates every one of them and is invisible.
                constexpr float kSpineGap = 0.006f;
                constexpr float kSpineTilt = 0.7853982f;

                if (stage == 0) {
                    uprightBox(place(0.0f, 0.0625f, 0.0f), 3.0f, 2.0f, 3.0f, 0.0f, 27.0f, skin, 0.0f);
                    uprightBox(place(0.0625f, 0.15625f, -0.0625f), 1.0f, 1.0f, 1.0f,
                               24.0f, 6.0f, skin, 0.002f);
                    uprightBox(place(0.0625f, 0.15625f, 0.0625f), 1.0f, 1.0f, 1.0f,
                               28.0f, 6.0f, skin, 0.002f);
                    finBox(place(-0.1875f, 0.0625f, 0.0f), 3.0f, 0.0f, 3.0f, -3.0f, 0.0f, skin);
                    uprightBox(place(0.03125f, 0.03125f, -0.125f), 1.0f, 1.0f, 2.0f,
                               25.0f, 0.0f, skin, 0.002f);
                    uprightBox(place(0.03125f, 0.03125f, 0.125f), 1.0f, 1.0f, 2.0f,
                               25.0f, 0.0f, skin, 0.002f);
                    continue;
                }

                if (stage == 1) {
                    uprightBox(place(0.0f, 0.21875f, 0.0f), 5.0f, 5.0f, 5.0f,
                               12.0f, 22.0f, skin, 0.0f);
                    uprightBox(place(0.03125f, 0.28125f, -0.21875f), 2.0f, 1.0f, 2.0f,
                               24.0f, 3.0f, skin, 0.002f);
                    uprightBox(place(0.03125f, 0.28125f, 0.21875f), 2.0f, 1.0f, 2.0f,
                               24.0f, 0.0f, skin, 0.002f);
                    // Eight flat spines, each hinged 45 degrees off the face it
                    // grows from - the reference's `bind_pose_rotation`.
                    finBox(place(0.15625f + kSpineGap, 0.40625f + kSpineGap, 0.0f), 5.0f, 1.0f, 0.0f,
                           19.0f, 17.0f, skin, kSpineTilt);
                    finBox(place(-0.15625f - kSpineGap, 0.40625f + kSpineGap, 0.0f), 5.0f, 1.0f, 0.0f,
                           11.0f, 17.0f, skin, -kSpineTilt);
                    finBox(place(0.15625f + kSpineGap, 0.03125f - kSpineGap, 0.0f), 5.0f, 1.0f, 0.0f,
                           18.0f, 20.0f, skin, -kSpineTilt);
                    finBox(place(-0.15625f - kSpineGap, 0.03125f - kSpineGap, 0.0f), 5.0f, 1.0f, 0.0f,
                           18.0f, 20.0f, skin, kSpineTilt);
                    // The four side spines turn about the vertical instead, so
                    // their centres are worked out before the frame is turned.
                    const glm::vec3 leftFront = place(0.15625f + kSpineGap, 0.21875f, -0.1875f - kSpineGap);
                    const glm::vec3 leftBack = place(-0.15625f - kSpineGap, 0.21875f, -0.1875f - kSpineGap);
                    const glm::vec3 rightFront = place(0.15625f + kSpineGap, 0.21875f, 0.1875f + kSpineGap);
                    const glm::vec3 rightBack = place(-0.15625f - kSpineGap, 0.21875f, 0.1875f + kSpineGap);
                    yawed(kSpineTilt, [&]() {
                        finBox(leftFront, 1.0f, 5.0f, 0.0f, 1.0f, 17.0f, skin);
                        finBox(rightBack, 1.0f, 5.0f, 0.0f, 9.0f, 17.0f, skin);
                    });
                    yawed(-kSpineTilt, [&]() {
                        finBox(leftBack, 1.0f, 5.0f, 0.0f, 1.0f, 17.0f, skin);
                        finBox(rightFront, 1.0f, 5.0f, 0.0f, 5.0f, 17.0f, skin);
                    });
                    continue;
                }

                uprightBox(place(0.0f, 0.25f, 0.0f), 8.0f, 8.0f, 8.0f, 0.0f, 0.0f, skin, 0.0f);
                uprightBox(place(0.1244f, 0.40625f, -0.3125f), 2.0f, 1.0f, 2.0f,
                           24.0f, 3.0f, skin, 0.002f);
                uprightBox(place(0.1245f, 0.40625f, 0.3123f), 2.0f, 1.0f, 2.0f,
                           24.0f, 0.0f, skin, 0.002f);
                // Twelve spikes: three along the top, three underneath, three
                // down each flank. The end ones are canted 45 degrees off the
                // body and the middle ones are square to it, which is what
                // gives the silhouette its points.
                const float top = 0.53125f + kSpineGap;
                const float bottom = -0.03125f - kSpineGap;
                uprightBox(place(0.21875f, top, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 16.0f, skin, 0.0f,
                           kSpineTilt);
                uprightBox(place(-0.03125f, top, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 16.0f, skin, 0.0f);
                uprightBox(place(-0.28125f, top, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 16.0f, skin, 0.0f,
                           -kSpineTilt);
                uprightBox(place(0.21875f, bottom, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 19.0f, skin, 0.0f,
                           -kSpineTilt);
                uprightBox(place(-0.03125f, bottom, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 19.0f, skin, 0.0f);
                uprightBox(place(-0.28125f, bottom, 0.0f), 8.0f, 1.0f, 1.0f, 14.0f, 19.0f, skin, 0.0f,
                           kSpineTilt);

                const float flank = 0.28125f + kSpineGap;
                const glm::vec3 sideSpines[6]{place(0.21875f, 0.25f, -flank),
                                              place(-0.03125f, 0.25f, -flank),
                                              place(-0.28125f, 0.25f, -flank),
                                              place(0.21875f, 0.25f, flank),
                                              place(-0.03125f, 0.25f, flank),
                                              place(-0.28125f, 0.25f, flank)};
                const float sideU[6]{0.0f, 4.0f, 8.0f, 4.0f, 8.0f, 8.0f};
                const float sideYaw[6]{kSpineTilt, 0.0f, -kSpineTilt, -kSpineTilt, 0.0f, kSpineTilt};
                for (int i = 0; i < 6; ++i) {
                    yawed(sideYaw[i], [&]() {
                        uprightBox(sideSpines[i], 1.0f, 8.0f, 1.0f, sideU[i], 16.0f, skin, 0.0f);
                    });
                }
                continue;
            }

            if (creature.kind == CreatureKind::Squid || creature.kind == CreatureKind::GlowSquid) {
                // A bell with eight tentacles hanging off it at forty-five
                // degree spacing - the first thing here that is a ring of limbs
                // rather than a spine with legs. The glow squid is the same rig
                // and the same net; only the skin row and its own light differ.
                const float skin = creature.kind == CreatureKind::Squid ? kSquidSkin : kGlowSquidSkin;
                constexpr float kBellUp = 0.95f;

                // **`bodyTilt`, not `pitch`.** The dive angle says where it is
                // headed; this says how the animal is lying while it gets
                // there, and for something that swims bell-first with its
                // tentacles streaming behind those are not the same number.
                const float sinTip = std::sin(creature.bodyTilt);
                const float cosTip = std::cos(creature.bodyTilt);
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = kBellUp;
                frameOrigin = renderPosition + worldUp * (kBellUp * modelScale * swellTall);

                uprightBox(place(0.0f, kBellUp, 0.0f), 12.0f, 16.0f, 12.0f, 0.0f, 0.0f, skin, 0.0f);

                // They flare and snap rather than swinging: the reference's own
                // `sin(f^2 * pi)` over the first half of the cycle, still over
                // the second. **Negated** because a positive pitch swings a
                // hanging limb toward the ring's centre, and a flare is
                // outward. `legBox` hangs each one from its top face and the
                // derived pivot lands exactly on the underside of the bell, so
                // the ring hinges where it meets the body.
                const float half = kTwoPi * 0.5f;
                const float phase = creature.jetPhase / half;
                const float splay =
                    creature.jetPhase < half ? std::sin(phase * phase * half) * kJetSplay : 0.0f;
                for (int i = 0; i < 8; ++i) {
                    const float around = static_cast<float>(i) * 0.7853982f;
                    yawed(around, [&]() {
                        legBox(place(0.3125f, kBellUp - 1.0f, 0.0f), 2.0f, 18.0f, 2.0f,
                               48.0f, 0.0f, skin, 0.002f, -splay);
                    });
                }
                continue;
            }

            if (creature.kind == CreatureKind::Turtle) {
                // The shell and the belly plate are authored upright and laid
                // down a quarter turn - the reference's own `bind_pose_rotation`
                // of 90 degrees, which is exactly what `lyingBox` is for. The
                // head and the four flippers are already flat in the data and
                // take no rotation at all.
                //
                // The shell is centred against the *flippers* rather than by
                // eye: the front pair sit at z -6..-1 and the rear at 11..21,
                // so a body spanning -8..12 meets both.
                lyingBox(place(-0.125f, 0.1875f, 0.0f), 19.0f, 20.0f, 6.0f,
                         6.0f, 37.0f, kTurtleSkin, 0.0f);
                lyingBox(place(-0.125f, 0.046875f, 0.0f), 11.0f, 18.0f, 3.0f,
                         30.0f, 1.0f, kTurtleSkin, 0.0f);

                // **Sunk into the shell rather than butted against it, and a
                // shade lower than its roof.** Its crown used to land on
                // exactly the shell's top plane, and two coincident faces on
                // double-sided quads is a flicker right where the neck is.
                //
                // The net origin is 3, not the geometry's 2: measured off the
                // texture, whose painted band runs x 9-20, and at 2 the top face
                // sampled a transparent column and showed a slot across it.
                beginHead(0.5f, 0.20f);
                uprightBox(place(0.66f, 0.20f, 0.0f), 6.0f, 5.0f, 6.0f,
                           3.0f, 0.0f, kTurtleSkin, 0.004f);
                endHead();

                // Flippers row rather than stride: the front pair are broad
                // paddles and the rear pair long and narrow, and both sweep
                // about their inner end the way the spider's legs do.
                const float row = swing * 0.6f;
                barBox(0.21875f, 0.15625f, 0.71875f, 13.0f, 1.0f, 5.0f,
                       26.0f, 30.0f, kTurtleSkin, 0.0f, row);
                barBox(0.21875f, 0.15625f, -0.71875f, 13.0f, 1.0f, 5.0f,
                       26.0f, 24.0f, kTurtleSkin, 0.0f, -row);
                barBox(-1.0f, 0.09375f, 0.21875f, 4.0f, 1.0f, 10.0f,
                       0.0f, 23.0f, kTurtleSkin, 0.0f, -row);
                barBox(-1.0f, 0.09375f, -0.21875f, 4.0f, 1.0f, 10.0f,
                       0.0f, 12.0f, kTurtleSkin, 0.0f, row);
                continue;
            }

            if (creature.kind == CreatureKind::Dolphin) {
                // Body, head and beak in a line, then the tail and its fluke.
                // It tips with the swim angle like the fish do - it has a clear
                // nose, so where it points is obvious without the squid's
                // separate body angle.
                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                constexpr float kSpineUp = 0.21875f;
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = kSpineUp;
                frameOrigin = renderPosition + worldUp * (kSpineUp * modelScale * swellTall);

                // **These net origins are measured off the texture, not taken
                // from the Bedrock geometry.** Our reference art is Java's, and
                // for this one mob the two disagree: Bedrock puts the tail at
                // (0,33) and the tail fin at (0,49), where the Java sheet has
                // nothing at all - which is why the whole back half rendered as
                // empty. Head and nose happen to agree; body, tail and fin do
                // not. `tools/alpha-runs.ps1` on `dolphin.png` shows it.
                uprightBox(place(-0.21875f, 0.21875f, 0.0f), 8.0f, 7.0f, 13.0f,
                           22.0f, 0.0f, kDolphinSkin, 0.0f);
                // **The head is grown and pushed back into the body.** Its
                // cross-section is the body's exactly - 8 by 7 - and the two met
                // at a plane, so their touching faces were the same rectangle in
                // the same place, drawn from both sides. That is the flicker at
                // the neck; the beak had it against the head for the same
                // reason.
                uprightBox(place(0.355f, 0.21875f, 0.0f), 8.0f, 7.0f, 6.0f,
                           0.0f, 0.0f, kDolphinSkin, 0.005f);
                uprightBox(place(0.665f, 0.0625f, 0.0f), 2.0f, 2.0f, 4.0f,
                           0.0f, 13.0f, kDolphinSkin, 0.009f);
                uprightBox(place(-0.955f, 0.15625f, 0.0f), 4.0f, 5.0f, 11.0f,
                           0.0f, 19.0f, kDolphinSkin, 0.004f);
                uprightBox(place(-1.36f, 0.15625f, 0.0f), 10.0f, 1.0f, 6.0f,
                           19.0f, 20.0f, kDolphinSkin, 0.006f);
                // Swept back thirty degrees, which is the reference's own
                // rotation - negative because a positive pitch tips a box's top
                // forward and a dorsal leans the other way.
                uprightBox(place(-0.1875f, 0.546875f, 0.0f), 1.0f, 5.0f, 4.0f,
                           52.0f, 0.0f, kDolphinSkin, 0.002f, -0.5236f);
                uprightBox(place(0.03125f, 0.09375f, 0.4375f), 8.0f, 1.0f, 4.0f,
                           44.0f, 27.0f, kDolphinSkin, 0.002f, 0.0f, 0.0f, 0.3491f);
                uprightBox(place(0.03125f, 0.09375f, -0.4375f), 8.0f, 1.0f, 4.0f,
                           44.0f, 27.0f, kDolphinSkin, 0.002f, 0.0f, 0.0f, -0.3491f);
                continue;
            }

            if (creature.kind == CreatureKind::Axolotl) {
                // Five liveries, each a whole net on its own rows.
                const float skin =
                    kAxolotlSkin + static_cast<float>(creature.variant) * kAxolotlVariantRows;
                // The model sits below its own feet in the reference - the
                // limbs hang to -4 - so the whole thing is lifted to stand in
                // its box rather than through it.
                constexpr float kBodyUp = 0.125f;
                constexpr float kLift = 0.14f;

                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = kBodyUp;
                frameOrigin =
                    renderPosition + worldUp * ((kBodyUp + kLift) * modelScale * swellTall);

                uprightBox(place(0.0f, 0.125f, 0.0f), 8.0f, 4.0f, 10.0f,
                           0.0f, 11.0f, skin, 0.0f);
                // The ridge along its back. **In water the tail does all the
                // work and the legs hang still**; on land it is the other way
                // about, which is the whole difference between the two gaits.
                finBox(place(0.03125f, 0.15625f, 0.0f), 0.0f, 5.0f, 9.0f, 2.0f, 17.0f, skin);

                // The tail sways about where it meets the body rather than
                // about the animal's middle, or its root would swing clear of
                // the hips on every beat.
                const float sway = creature.inWater
                                       ? std::sin(creature.age * 5.0f) * 0.45f
                                       : std::sin(creature.age * 1.4f) * 0.08f;
                const glm::vec3 tailRoot = place(-0.25f, 0.15625f, 0.0f);
                const glm::vec3 tailCentre =
                    tailRoot - forward * (0.375f * std::cos(sway) * modelScale) +
                    side * (0.375f * std::sin(sway) * modelScale);
                yawed(sway, [&]() {
                    finBox(tailCentre, 0.0f, 5.0f, 12.0f, 2.0f, 19.0f, skin);
                });

                // **No `beginHead` here.** It rebuilds the frame from the
                // creature's own position, which would throw away `kLift` and
                // drop the head below the body it is attached to. A swimmer
                // pitches its whole body anyway, so there is no separate head
                // turn to want.
                uprightBox(place(0.46875f, 0.15625f, 0.0f), 8.0f, 5.0f, 5.0f,
                           0.0f, 1.0f, skin, 0.0f);
                // **The gills are three real boxes cut by alpha, not decoration.**
                // Two feathered fronds off the cheeks and one across the crown,
                // and they are what makes the animal read as an axolotl at all.
                finBox(place(0.375f, 0.21875f, -0.34375f), 3.0f, 7.0f, 0.0f, 11.0f, 40.0f, skin);
                finBox(place(0.375f, 0.21875f, 0.34375f), 3.0f, 7.0f, 0.0f, 0.0f, 40.0f, skin);
                finBox(place(0.375f, 0.40625f, 0.0f), 8.0f, 3.0f, 0.0f, 3.0f, 37.0f, skin);

                // Four flat paddles, each turned a quarter about the vertical -
                // the reference bakes that into the cube origin, so the centres
                // here are worked out after the turn and only the orientation
                // is rotated. Seated a little higher than the reference's exact
                // offset: its plates have no thickness and ours do, so a single
                // texel of overlap left them reading as detached.
                const glm::vec3 limbs[4]{place(0.28125f, -0.03f, 0.25f),
                                         place(-0.21875f, -0.03f, 0.25f),
                                         place(0.28125f, -0.03f, -0.25f),
                                         place(-0.21875f, -0.03f, -0.25f)};
                const float limbYaw[4]{-1.5708f, 1.5708f, 1.5708f, -1.5708f};
                // Splayed out and down rather than hanging straight, which is
                // what a sprawling amphibian's legs do and what lets them meet
                // the floor at all. Only the walk moves them - in water they
                // are held still and the tail drives.
                constexpr float kSprawl = 0.75f;
                const float paddle = creature.inWater ? 0.0f : swing * 0.7f;
                const float lean[4]{-kSprawl + paddle, kSprawl - paddle, -kSprawl - paddle,
                                    kSprawl + paddle};
                for (int i = 0; i < 4; ++i) {
                    yawed(limbYaw[i], [&]() {
                        finBox(limbs[i], 3.0f, 5.0f, 0.0f, 2.0f, 13.0f, skin, lean[i]);
                    });
                }
                continue;
            }

            if (creature.kind == CreatureKind::TropicalFish) {
                // Two body shapes crossed with six patterns. The pattern rides
                // over the body as a second cutout shell a fraction larger,
                // which is the sheep's fleece arrangement and is what avoids
                // authoring every combination as its own skin.
                const bool tall = creature.variant >= kTropicalPatterns;
                const int pattern = creature.variant % kTropicalPatterns;
                const float base = tall ? kTropicalBSkin : kTropicalASkin;
                const float overlay = base + static_cast<float>(pattern + 1) * kTropicalRows;
                constexpr float kOverBody = 0.006f;

                const float bodyUp = tall ? 0.1875f : 0.09375f;
                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                forward = bodyForward * cosTip - worldUp * sinTip;
                upAxis = bodyForward * sinTip + worldUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + worldUp * (bodyUp * modelScale * swellTall);

                // Both skins share one net, so the shell is the same call twice
                // at two row offsets and a hair of extra size.
                for (int pass = 0; pass < 2; ++pass) {
                    const float sheet = pass == 0 ? base : overlay;
                    const float grow = pass == 0 ? 0.0f : kOverBody;
                    if (tall) {
                        uprightBox(place(-0.1875f, 0.1875f, 0.0f), 2.0f, 6.0f, 6.0f,
                                   0.0f, 20.0f, sheet, grow);
                        finBox(place(-0.1875f, -0.15625f, 0.0f), 0.0f, 5.0f, 6.0f,
                               20.0f, 21.0f, sheet);
                        finBox(place(-0.1875f, 0.53125f, 0.0f), 0.0f, 5.0f, 6.0f,
                               20.0f, 10.0f, sheet);
                        finBox(place(-0.53125f, 0.1875f, 0.0f), 0.0f, 6.0f, 5.0f,
                               21.0f, 16.0f, sheet);
                    } else {
                        uprightBox(place(0.0f, 0.09375f, 0.0f), 2.0f, 3.0f, 6.0f,
                                   0.0f, 0.0f, sheet, grow);
                        finBox(place(0.0f, 0.3125f, 0.0f), 0.0f, 4.0f, 6.0f,
                               10.0f, -6.0f, sheet);
                        finBox(place(-0.3125f, 0.09375f, 0.0f), 0.0f, 3.0f, 4.0f,
                               24.0f, -4.0f, sheet);
                    }
                }

                // Pectorals, swept thirty-five degrees like every other fish
                // here. Body art only - the pattern sheet does not paint them.
                constexpr float kFinTurn = 0.6109f;
                const float finForward = tall ? -0.14688f : 0.00662f;
                const float finSide = tall ? 0.19104f : 0.0835f;
                const glm::vec3 fins[2]{place(finForward, 0.0625f, finSide),
                                        place(finForward, 0.0625f, -finSide)};
                yawed(kFinTurn, [&]() { finBox(fins[0], 2.0f, 2.0f, 0.0f, 2.0f, 12.0f, base); });
                yawed(-kFinTurn, [&]() { finBox(fins[1], 2.0f, 2.0f, 0.0f, 2.0f, 16.0f, base); });
                continue;
            }

            if (creature.kind == CreatureKind::Pig) {
                // Short and low: the whole animal tops out at one block, so the
                // head sits level with the body rather than above it.
                lyingBox(place(0.0f, 0.625f, 0.0f), 10.0f, 16.0f, 8.0f, 28.0f, 8.0f, kPigSkin, 0.0f);
                beginHead(0.375f, 0.75f);
                uprightBox(place(0.625f, 0.75f, 0.0f), 8.0f, 8.0f, 8.0f, 0.0f, 0.0f, kPigSkin, 0.0f);
                // The snout is one texel deep and its back face is unused, so
                // its net band is narrower than the formula predicts.
                uprightBox(place(0.90625f, 0.65625f, 0.0f), 4.0f, 3.0f, 1.0f, 16.0f, 16.0f, kPigSkin, 0.0f);
                endHead();

                const float legY = 0.1875f;
                legBox(place(0.3125f, legY, 0.1875f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, kPigSkin, 0.0f, swing);
                legBox(place(0.3125f, legY, -0.1875f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, kPigSkin, 0.0f, -swing);
                legBox(place(-0.4375f, legY, 0.1875f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, kPigSkin, 0.0f, -swing);
                legBox(place(-0.4375f, legY, -0.1875f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, kPigSkin, 0.0f, swing);
                continue;
            }

            if (creature.kind == CreatureKind::Bramble) {
                // Upright and legless-looking: a tall slab of a body on four
                // stubby legs, with the head sat straight on top. Nothing is
                // laid down, so every box takes the upright mapping.
                const auto brambleBoxes = [&](float skin, float grow) {
                    uprightBox(place(0.0f, 0.75f, 0.0f), 8.0f, 12.0f, 4.0f, 16.0f, 16.0f, skin, grow);
                    beginHead(0.0f, 1.125f);
                    uprightBox(place(0.0f, 1.375f, 0.0f), 8.0f, 8.0f, 8.0f, 0.0f, 0.0f, skin, grow);
                    endHead();

                    const float legY = 0.1875f;
                    legBox(place(0.25f, legY, 0.125f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, skin, grow, swing);
                    legBox(place(0.25f, legY, -0.125f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, skin, grow, -swing);
                    legBox(place(-0.25f, legY, 0.125f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, skin, grow, -swing);
                    legBox(place(-0.25f, legY, -0.125f), 4.0f, 6.0f, 4.0f, 0.0f, 16.0f, skin, grow, swing);
                };

                brambleBoxes(kBrambleSkin, 0.0f);

                // The charge is the reference's own overlay net rather than a
                // tint: the same boxes again, a little larger, reading the
                // `creeper_armor` rows. It is mostly transparent and the skin
                // path already discards anything under half alpha, so only the
                // blue survives - the same arrangement that makes the sheep's
                // fleece a shell rather than a repaint.
                //
                // Translucent, because an energy shell has to read *over* the
                // animal. Opaque it would simply be a bigger creeper.
                if (creature.charged) {
                    target = &translucent;
                    quadAlpha = 0.45f;
                    brambleBoxes(kChargeSkin, 0.75f * kTexel);
                    target = &mesh;
                    quadAlpha = 1.0f;
                }
                continue;
            }

            if (creature.kind == CreatureKind::Cow || creature.kind == CreatureKind::MushroomCow) {
                // Heavier than the sheep and on a 64-row net rather than 32.
                // `geometry.mooshroom.v2` is the cow's bones exactly - the
                // mushrooms are not in the skin at all, they are separate block
                // models, which is why one is grown below rather than boxed.
                const float skin =
                    creature.kind == CreatureKind::MushroomCow ? kMushroomCowSkin : kCowSkin;
                lyingBox(place(-0.0625f, 1.0625f, 0.0f), 12.0f, 18.0f, 10.0f, 18.0f, 4.0f, skin, 0.0f);
                beginHead(0.5f, 1.25f);
                uprightBox(place(0.6875f, 1.25f, 0.0f), 8.0f, 8.0f, 6.0f, 0.0f, 0.0f, skin, 0.0f);
                endHead();

                // The udder hangs under the rear and belongs to the body group,
                // so it is laid down with it. Its net is at (52,0) - the pink
                // one. The brown box at (0,32) is not an udder, and using it put
                // a slab of hide through the animal's back.
                lyingBox(place(-0.4375f, 0.71875f, 0.0f), 4.0f, 6.0f, 1.0f, 52.0f, 0.0f, skin, 0.0f);

                // Both horns share one net.
                beginHead(0.5f, 1.25f);
                uprightBox(place(0.71875f, 1.46875f, 0.28125f), 1.0f, 3.0f, 1.0f, 22.0f, 0.0f, skin, 0.0f);
                uprightBox(place(0.71875f, 1.46875f, -0.28125f), 1.0f, 3.0f, 1.0f, 22.0f, 0.0f, skin, 0.0f);
                endHead();

                const float legY = 0.375f;
                legBox(place(0.375f, legY, 0.25f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, skin, 0.0f, swing);
                legBox(place(0.375f, legY, -0.25f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, skin, 0.0f, -swing);
                legBox(place(-0.4375f, legY, 0.25f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, skin, 0.0f, -swing);
                legBox(place(-0.4375f, legY, -0.25f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, skin, 0.0f, swing);

                if (creature.kind == CreatureKind::MushroomCow) {
                    // A crossed pair of flat quads, which is how a plant is
                    // drawn everywhere else here - the reference puts actual
                    // mushroom *blocks* on its back, and a block model is a
                    // crossed sprite. The sprite is stamped into an empty
                    // corner of this species' own rows rather than given a
                    // texture layer of its own, which would have moved
                    // `TextureLayer::SpawnEggFirst` and slid every egg sprite.
                    constexpr float kCapU = 32.0f;
                    constexpr float kCapV = 48.0f;
                    const auto mushroom = [&](float alongForward, float alongSide, float size) {
                        const glm::vec3 base = place(alongForward, 1.34375f, alongSide);
                        const float half = size * 0.5f * modelScale;
                        const glm::vec3 tall = upAxis * (size * modelScale);
                        const glm::vec3 a = (forward + side) * (half * 0.70710678f);
                        const glm::vec3 b = (forward - side) * (half * 0.70710678f);
                        skinQuad(base - a, base + a, base + a + tall, base - a + tall, 1.0f, skin,
                                 kCapU, kCapV, 16.0f, 16.0f);
                        skinQuad(base - b, base + b, base + b + tall, base - b + tall, 1.0f, skin,
                                 kCapU, kCapV, 16.0f, 16.0f);
                    };
                    mushroom(-0.28f, 0.0f, 0.42f);
                    mushroom(0.06f, 0.20f, 0.34f);
                    mushroom(0.10f, -0.22f, 0.30f);
                }
                continue;
            }

            lyingBox(place(0.0f, 0.9375f, 0.0f), 8.0f, 16.0f, 6.0f, 28.0f, 8.0f, kSheepHide, 0.0f);
            beginHead(0.375f, 1.1875f);
            uprightBox(place(0.625f, 1.1875f, 0.0f), 6.0f, 6.0f, 8.0f, 0.0f, 0.0f, kSheepHide, 0.0f);
            endHead();

            const float legY = 0.375f;
            legBox(place(0.3125f, legY, 0.1875f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, kSheepHide, 0.0f, swing);
            legBox(place(0.3125f, legY, -0.1875f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, kSheepHide, 0.0f, -swing);
            legBox(place(-0.4375f, legY, 0.1875f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, kSheepHide, 0.0f, -swing);
            legBox(place(-0.4375f, legY, -0.1875f), 4.0f, 12.0f, 4.0f, 0.0f, 16.0f, kSheepHide, 0.0f, swing);

            // The fleece is a second, slightly larger shell one net down the
            // sheet. That separation is what makes a sheep read as woolly
            // rather than as an animal that happens to be painted cream.
            lyingBox(place(0.0f, 0.9375f, 0.0f), 8.0f, 16.0f, 6.0f, 28.0f, 8.0f, kSheepFleece, 1.75f * kTexel);
            // The fleece skull is its own box - 6 deep against the bare head's
            // 8 - and where its front edge lands is the whole game. The ears
            // are painted 1-3 texels back from the front of the head and the
            // face is the plane at the very front, so the fleece has to stop
            // between them: past the ear, short of the face. Too far forward
            // masks the face, too far back leaves half an ear showing.
            beginHead(0.375f, 1.1875f);
            uprightBox(place(0.625f, 1.1875f, 0.0f), 6.0f, 6.0f, 6.0f, 0.0f, 0.0f, kSheepFleece, 0.6f * kTexel);
            endHead();
        }
    }

    return mesh;
}

} // namespace game
