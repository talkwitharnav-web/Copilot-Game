#include "world/Creature.hpp"

#include "item/SpriteModel.hpp"
#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/Explosion.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/Village.hpp"
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

/// The radius every population number below is quoted against, in blocks.
/// Nothing is fixed at it any more — it is the *reference* point that the
/// runtime radius scales from, so the tuning that was done at 90 m still means
/// what it meant.
constexpr float kBaseRadius = 90.0f;

/// The shell a creature may appear in, as fractions of the active radius. The
/// near edge keeps them from popping into view; the far edge stays inside the
/// retirement radius, or a herd would be placed and immediately dropped.
constexpr float kSpawnNearFraction = 0.27f;
constexpr float kSpawnFarFraction = 0.85f;
constexpr std::size_t kMaxCreatures = 14;

/// Hard ceiling on the population however far the world is drawn. A 9-chunk
/// render distance is ten times the area of the old fixed 90 m, and a hundred
/// and forty animals is a frame-rate problem rather than a busy world.
constexpr std::size_t kCreatureCeiling = 44;
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

/// The death fall. `ANIMATION.md` §7.1: a quarter turn onto its side, then the
/// body lies there for the rest of the reference's own twenty-tick life before
/// it is retired. It falls sideways; it does not spin, and it does not sink.
constexpr float kDeathTipSeconds = 0.625f;
constexpr float kDeathSeconds = 1.0f;
constexpr float kDeathTipAngle = 1.5707963f;

/// How far over a dying creature has tipped, in radians. Zero while it lives.
///
/// **Smoothstep, not the reference's `sqrt`.** Java's easing is fastest at the
/// very first tick - a quarter of the whole turn is spent in the first fiftieth
/// of a second - which reads as the body being swatted flat. The user asked for
/// a fall that accelerates as it goes over and decelerates as it settles, so it
/// *lands* instead of snapping, and that is exactly the ease-in-out curve. The
/// timings stay the reference's.
float deathTipAngle(const Creature& creature) {
    if (creature.health > 0) {
        return 0.0f;
    }
    const float t = std::clamp(creature.deathTimer / kDeathTipSeconds, 0.0f, 1.0f);
    return kDeathTipAngle * t * t * (3.0f - 2.0f * t);
}
/// The shove a player's blow carries.
constexpr float kStrikeKnockback = 5.0f;
constexpr float kStrikeLift = 4.0f;

/// How far outside its collision box a creature can still be hit. The reference
/// pads its pick box too, and here it also covers the fact that `halfWidth` is
/// deliberately narrower than the model it is drawn with.
constexpr float kAimPadding = 0.15f;
/// How hard a blast throws what it catches. The reference works in blocks per
/// tick and we work in blocks per second, so it is twenty times over.
constexpr float kBlastThrow = 20.0f;

/// A lightning bolt's reach, and what it does. The reference's 6x12x6 box read
/// as half-extents, and its five points of damage.
constexpr float kLightningReach = 3.0f;
constexpr float kLightningRise = 9.0f;
constexpr int kLightningDamage = 5;

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

/// Which row a villager's outfit sits on.
///
/// Profession 0 keeps the plain skin the sheet has always carried, and the
/// fourteen trades are composited copies of it further up. **That is what makes
/// taking a job visible**: an unemployed villager and a librarian differ by one
/// number here and by nothing else in the rig.
float villagerSkinRow(std::uint8_t profession) {
    return profession == 0
               ? kVillagerSkin
               : static_cast<float>(kVillagerProfessionSkinRow + (profession - 1) * 64);
}

/// The golem's own hundred-and-twenty-eight rows.
constexpr float kIronGolemSkin = static_cast<float>(kIronGolemSkinRow);
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
constexpr float kBeeSkin = 3552.0f;
/// The angry bee, on the wolf's arrangement: a whole second skin whose only
/// difference is **22 texels in the face**, measured - the pale cyan eyes
/// (124,201,209) go bright red (228,0,24). Cheaper than tinting and it is what
/// the artist will already be working from.
constexpr float kBeeAngrySkin = 3616.0f;
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

/// How the reference holds an item in a hand.
///
/// **Read off its own item models rather than guessed** -
/// `models/item/handheld.json` for tools and weapons, `models/item/bow.json`
/// for the bow, `models/item/generated.json` for everything else flat - and
/// every number is that model's `thirdperson_righthand` display transform in
/// the reference's own units: rotation in **degrees**, applied X then Y then Z;
/// translation in **sixteenths of a block**; scale a plain multiplier.
///
/// The left hand is this same row with the Y and Z rotations and the sideways
/// translation negated. That is the reference's own mirroring, and it is a
/// derivation rather than a second row precisely so the two cannot drift.
struct HeldTransform {
    float rotation[3];
    float translation[3];
    float scale;
};
constexpr HeldTransform kHeldHandheld{{0.0f, -90.0f, 55.0f}, {0.0f, 4.0f, 0.5f}, 0.85f};
constexpr HeldTransform kHeldBow{{-80.0f, 260.0f, -40.0f}, {-1.0f, -2.0f, 2.5f}, 0.9f};
constexpr HeldTransform kHeldFlat{{0.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 1.0f}, 0.55f};

const HeldTransform& heldTransform(ItemId item) {
    if (item == ItemId::Bow) {
        return kHeldBow;
    }
    return isTool(item) ? kHeldHandheld : kHeldFlat;
}

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

/// Burning in daylight: one point a second, which is the reference's own fire
/// damage rate, above a sky light of twelve. A zombie therefore takes twenty
/// seconds to die of the sun and can still reach shade in that time - the
/// reference's behaviour, and the whole reason it reads as burning rather than
/// as vanishing.
constexpr float kBurnInterval = 1.0f;
constexpr int kBurnDamage = 1;
constexpr int kBurnSkyLight = 12;

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
/// And the two distances that go with it. `max_flee` is how far clear counts as
/// safe, `sprint_distance` is how close is close enough to bolt rather than
/// walk away. **These are what end a flight** - before they existed, fleeing
/// stopped on a fixed timer, which meant an animal still sprinting long after
/// nothing was chasing it and one that gave up while the wolf was still on it.
constexpr float kFleeClear = 10.0f;
constexpr float kFleeSprint = 7.0f;

/// Ticks of fuse bought per block fallen, and the ceiling it stops at. Both are
/// the reference's: a long drop lands a creeper already most of the way through
/// its countdown, but never past it, so there is always a moment to react.
constexpr float kFallFuseTicksPerBlock = 1.5f;
constexpr float kFallFuseHeadroomTicks = 5.0f;
constexpr float kTicksPerSecond = 20.0f;

/// Grazing. `time_until_eat` is the reference's, and so are the odds - a per
/// tick chance of 0.001 for an adult and 0.02 for a lamb, converted to the per
/// second rates a delta-time loop needs. A sheep therefore crops the grass
/// about once a minute and a lamb rather often, which is exactly the reference
/// and is why a flock leaves a trail of dirt behind it.
constexpr float kGrazeSeconds = 1.8f;
constexpr float kGrazeChancePerSecond = 0.001f * kTicksPerSecond;
constexpr float kGrazeBabyChancePerSecond = 0.02f * kTicksPerSecond;
/// How far the head drops while it eats, in radians. The reference's grazing
/// animation puts the muzzle on the floor; ours clamps head pitch at 0.7, so
/// this simply asks for everything the neck has.
constexpr float kGrazeHeadPitch = 0.7f;

/// How long a creature keeps believing it can see the player after the ray last
/// got through.
///
/// **This is a frame-rate correction, not a mercy.** The reference samples
/// sight twenty times a second; we sample it every frame, so at 120 fps a
/// fence post clipped for a single frame is six times likelier to be caught,
/// and each catch reverses the fuse. A tick's worth of memory puts the sampling
/// back on the reference's footing.
constexpr float kSightGrace = 0.05f;

/// How often a creature says something unprompted. The reference's
/// `ambient_sound` fires about once every eight seconds per mob.
constexpr float kAmbientVoiceChancePerSecond = 1.0f / 8.0f;

/// The chicken's wing beat, in the reference's per-tick numbers. `flapSpeed`
/// climbs by 1.2 a tick airborne and falls by 0.3 on the ground, `flapping` is
/// refreshed to 1 while airborne and decays by a tenth a tick, and the phase
/// advances by twice `flapping`. All three are converted to per-second rates,
/// because a frame is not a tick.
constexpr float kFlapOpenRate = 1.2f / 0.05f;
constexpr float kFlapCloseRate = 0.3f / 0.05f;
constexpr float kFlapDecayPerTick = 0.9f;
constexpr float kFlapPhaseRate = 2.0f / 0.05f;

/// An insect's wing, which is a different thing entirely: it never opens, never
/// folds and never stops. The reference drives the bee's from its age at 2.1
/// radians a tick, so that is the rate, converted per second like the rest.
constexpr float kInsectWingPhaseRate = 2.1f / 0.05f;

/// How far an insect wing swings either side of flat. The reference's bee uses
/// 0.15pi, which is shallow on purpose - at this beat rate anything wider reads
/// as flapping rather than buzzing.
constexpr float kInsectWingSwing = 0.15f * 3.14159265f;

/// Where the bee's body centre sits above its feet. The net is 7 texels tall,
/// so the box spans 0.03 to 0.47 of a half-block-tall animal - it fills its own
/// hitbox almost exactly, which is what a bee should do.
constexpr float kBeeBodyUp = 0.25f;

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
/// How many Brambles arrive already charged. **A second source, not the only
/// one**: a lightning strike charges one, which is where the reference gets
/// them from and what M27 built. This keeps a few in the world between storms.
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

/// Flying. Same shape as the swimmer's numbers above and deliberately gentler:
/// a bee drifts rather than darts, and a wide dive angle at this speed reads as
/// a dropped stone.
constexpr float kFlyPitchMax = 0.22f;
/// Far steeper, because a cruise is a drift and a chase is a dive. A bee has to
/// come down to the player's own footing before the melee gate will let it
/// sting, and the gentle cruising angle cannot cover four blocks of descent
/// inside a decision.
constexpr float kFlyChasePitchMax = 0.7f;
constexpr float kFlyProbe = 1.2f;
constexpr float kFlyDecisionMin = 1.5f;
constexpr float kFlyDecisionSpan = 3.0f;

/// The band a flier holds above whatever is beneath it. Below the floor it
/// climbs outright rather than merely being forbidden to sink - a bee spawns on
/// the ground, and a clamp alone would leave it there whenever its rolled angle
/// happened to be level. The ceiling is only a clamp, because drifting down is
/// already what it does when it stops climbing.
constexpr int kFlyFloorBlocks = 2;
constexpr int kFlyCeilingBlocks = 6;

/// How far a bee looks for a flower, in blocks. It runs on the wander timer
/// rather than per frame, which is what keeps a box this size affordable.
constexpr int kFlowerSearchXZ = 6;
constexpr int kFlowerSearchY = 3;

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
constexpr int kChunkSize = 1 << kChunkShift;
/// The reference uses 0.10 for most biomes. Most chunks getting nothing is what
/// makes finding a herd worth anything.
constexpr float kChunkSpawnChance = 0.10f;

/// How many creatures the active radius should hold.
///
/// Scaled by **area**, because area is what a bigger radius actually buys. A
/// population scaled by radius instead would thin out visibly as the render
/// distance grew, which is the opposite of what asking for a wider world means.
std::size_t capacityFor(float radius) {
    const float ratio = (radius * radius) / (kBaseRadius * kBaseRadius);
    const auto scaled = static_cast<std::size_t>(static_cast<float>(kMaxCreatures) * ratio);
    return std::clamp(scaled, kMaxCreatures, kCreatureCeiling);
}
/// A generated group ignores the ordinary population cap, as the reference's
/// does, but not this. Without a ceiling a walk across open grassland would
/// stack herds without limit.
std::size_t generatedCeiling(float radius) {
    return capacityFor(radius) * 2;
}

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
     .maxBlockLight = 15, .weight = 1.0f, .babyChance = 0.05f, .groupSize = 4, .grazes = true,
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
     .swellStopRange = 6.0f, .avoids = tagMask(CreatureTag::Feline), .avoidRange = 6.0f,
     .chaseSpeedScale = 1.25f},
    // Falls slowly and flaps the whole way down, which is the reference's own
    // reason chickens need no fall-damage exemption - they never land hard.
    {.name = "Chicken", .halfWidth = 0.20f, .height = 0.80f, .gaitRate = 14.0f, .gaitSwing = 0.49f,
     .modelScale = 1.00f, .health = 4, .walkSpeed = 1.5f, .runSpeed = 3.0f, .senseRange = 6.0f,
     .maxBlockLight = 15, .weight = 1.1f, .babyChance = 0.05f, .groupSize = 4, .fallDrag = 0.6f,
     .alertRange = kHerdAlertRange},
    {.name = "Cat", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 6, .walkSpeed = 1.8f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .attackDamage = 1, .maxBlockLight = 15, .weight = 0.8f, .babyChance = 0.25f, .groupSize = 2,
     .hunts = tagMask(CreatureTag::Critter), .retaliates = false, .avoidsWater = true,
     .alertRange = kHerdAlertRange},
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
     .avoids = CreatureTag::Canine | CreatureTag::Ursine, .avoidRange = 8.0f,
     .avoidPlayerRange = 4.0f, .avoidsWater = true, .alertRange = kHerdAlertRange,
     .panicSpeedScale = 1.5f},
    // Neutral rather than hostile: it has a bite but no interest in using it
    // until struck. See `CreatureSpecies::attackDamage`. Twenty-five seconds of
    // grudge is the reference's `wolf_angry`, and it is four times what every
    // animal on the roster used to carry.
    {.name = "Wolf", .halfWidth = 0.30f, .height = 0.80f, .gaitRate = 9.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.8f, .runSpeed = 4.2f, .senseRange = 12.0f,
     .attackDamage = 4, .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.10f, .groupSize = 4,
     .hunts = CreatureTag::Grazer | CreatureTag::Critter | CreatureTag::Vulpine |
              CreatureTag::Skeletal,
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
     .attackDamage = 2, .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 3,
     .avoids = CreatureTag::Canine | CreatureTag::Ursine, .avoidRange = 10.0f,
     .hunts = CreatureTag::Fowl | CreatureTag::Critter | CreatureTag::Fish, .retaliates = false,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Shares the cat's net and model outright, the way the mule shares the
    // horse's - the reference draws them from one rig too.
    {.name = "Ocelot", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.8f, .runSpeed = 3.9f, .senseRange = 9.0f,
     .attackDamage = 1, .maxBlockLight = 15, .weight = 0.6f, .babyChance = 0.25f, .groupSize = 2,
     .hunts = tagMask(CreatureTag::Fowl), .retaliates = false, .avoidsWater = true,
     .alertRange = kHerdAlertRange},
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
     .babyChance = 0.10f, .groupSize = 2, .hunts = tagMask(CreatureTag::Vulpine),
     .leashRange = 48.0f, .angerSeconds = 500.0f, .alertRange = 41.0f},
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
    {.name = "Small Slime", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.90f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.5f, .runSpeed = 1.1f,
     .hostile = true, .senseRange = 16.0f, .nocturnal = true, .maxBlockLight = 15, .weight = 0.6f,
     .burnsInDay = false, .avoidsWater = true},
    {.name = "Slime", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.85f, .modelScale = 2.08f, .health = 4, .walkSpeed = 0.8f, .runSpeed = 1.8f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true,
     .maxBlockLight = 15, .weight = 0.5f, .burnsInDay = false,
     .splitInto = CreatureKind::SlimeSmall, .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    {.name = "Large Slime", .halfWidth = 0.75f, .height = 2.08f, .hops = true, .hopLaunch = 7.21f,
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
     .avoids = tagMask(CreatureTag::Canine), .avoidRange = 6.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f, .shootsArrows = true, .heldMainHand = ItemId::Bow},
    // Model and texture only for now: it wanders and nothing more. Trading, the
    // schedule and villages are their own milestone entirely. It bolts at its
    // run speed and gets no bonus on top, which leaves it the least athletic
    // fleer on the roster against a rabbit's 1.5 - the reference's timid
    // villager surviving the change of unit intact.
    {.name = "Villager", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f, .gaitSwing = 0.62f,
     .modelScale = 0.92f, .health = 20, .walkSpeed = 0.9f, .runSpeed = 1.6f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 0.5f, .avoids = tagMask(CreatureTag::Undead),
     .avoidRange = 8.0f, .avoidsWater = true, .alertRange = kHerdAlertRange,
     .panicSpeedScale = 1.0f, .keepsHouse = true},
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
     .weight = 0.35f, .burnsInDay = false, .avoids = tagMask(CreatureTag::Canine),
     .avoidRange = 6.0f, .floats = false, .amphibious = true,
     .breathesWater = true, .avoidsWater = true, .chaseSpeedScale = 1.25f, .swingsArms = true,
     .heldMainHand = ItemId::StoneSword},
    // Stray and bogged are the skeleton's rig again, unchanged. What separates
    // them is where they live and how hard they are: the stray holds the cold
    // uplands and is the tougher, the bogged swarms the lowlands and is the
    // weaker. The reference also tells them apart by the arrows they fire -
    // `aux_val` 19 slowness and 26 poison - which needs a tipped-arrow launch
    // kind we do not have; both fire plain ones.
    {.name = "Stray", .halfWidth = 0.28f, .height = 1.99f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.2f, .runSpeed = 2.8f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.5f,
     .avoids = tagMask(CreatureTag::Canine), .avoidRange = 6.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f, .shootsArrows = true, .heldMainHand = ItemId::Bow},
    {.name = "Bogged", .halfWidth = 0.28f, .height = 1.99f, .gaitRate = 6.5f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.3f, .runSpeed = 3.0f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.6f,
     .avoids = tagMask(CreatureTag::Canine), .avoidRange = 6.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f, .shootsArrows = true, .rangedInterval = 3.5f,
     .heldMainHand = ItemId::Bow},
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
    //
    // **It throws and does not touch you.** Until now it set neither ranged
    // flag, so it fell through to `MeleeAttack` and dealt its two damage by
    // walking into you with no animation at all - the `attackDamage` below is
    // now what a bottle of Harming is worth rather than what its hands are.
    // `heldMainHand` stays empty on purpose: the reference's witch has nothing
    // in its hands until it reaches for a bottle, and the bottle is put there
    // by the behaviour through `heldOverride`.
    {.name = "Witch", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f, .gaitSwing = 0.62f,
     .modelScale = 0.92f, .health = 26, .walkSpeed = 0.9f, .runSpeed = 1.8f, .hostile = true,
     .senseRange = 10.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.3f,
     .burnsInDay = false, .leashRange = 64.0f, .throwsPotions = true},
    // Passive, rare and found anywhere: the one thing on the roster that is
    // meant to read as a traveller rather than a resident. Trading is a
    // milestone of its own, so for now it only wanders.
    {.name = "Wandering Trader", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 4.5f,
     .gaitSwing = 0.62f, .modelScale = 0.92f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 1.8f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.2f,
     .avoids = tagMask(CreatureTag::Undead), .avoidRange = 8.0f, .avoidsWater = true,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.0f},
    // The first hostile that spawns in **daylight**, which is most of the point
    // of it: the uplands stop being safe at noon. Its body is the biped rig at
    // the standard limb offsets; only the head is its own. Thirty seconds of
    // anger and a 64 m leash are the reference's piglin, and its kind hear a
    // blow at 16 m - the one hostile on the roster that answers a call.
    //
    // It has no float goal in the shipped data, which is not an oversight: a
    // piglin genuinely will not swim up, so deep water drowns it.
    //
    // ⚠ **Named divergence: it carries a stone sword, not a golden one.** The
    // reference arms all three of these in gold and we have no gold tier at
    // all - wood, stone, iron, diamond, emberite. A stone blade is the nearest
    // thing that exists rather than a choice, and it is one word to change on
    // the day a golden sword does.
    {.name = "Princepin", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.1f, .runSpeed = 2.4f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 5, .maxBlockLight = 15, .weight = 0.3f,
     .burnsInDay = false, .floats = false, .avoidsWater = true, .leashRange = 64.0f,
     .angerSeconds = 30.0f, .alertRange = 16.0f, .swingsArms = true,
     .heldMainHand = ItemId::StoneSword},
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
    // The brute carries an **axe** where the other two carry swords, which is
    // the reference's own distinction and the only thing that tells the three
    // apart at a glance once they are all wearing the same skin family.
    {.name = "Princepin Brute", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 1.00f, .health = 50, .walkSpeed = 1.2f, .runSpeed = 2.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 7, .maxBlockLight = 15,
     .weight = 0.12f, .burnsInDay = false, .floats = false, .avoidsWater = true,
     .leashRange = 64.0f, .angerSeconds = 600.0f, .alertRange = 16.0f, .swingsArms = true,
     .heldMainHand = ItemId::StoneAxe},
    {.name = "Zombie Princepin", .halfWidth = 0.28f, .height = 1.95f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f,
     .senseRange = 16.0f, .attackDamage = 5, .nocturnal = true, .maxBlockLight = 7,
     .weight = 0.35f, .burnsInDay = false, .floats = false, .amphibious = true,
     .breathesWater = true, .angerSeconds = 30.0f, .alertRange = 20.0f, .swingsArms = true,
     .heldMainHand = ItemId::StoneSword},
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
    {.name = "Small Magma Cube", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.75f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.8f, .runSpeed = 1.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 3, .maxBlockLight = 15,
     .weight = 0.35f, .burnsInDay = false, .avoidsWater = true},
    {.name = "Magma Cube", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.70f, .modelScale = 2.08f, .health = 4, .walkSpeed = 1.1f, .runSpeed = 2.2f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 4, .maxBlockLight = 15,
     .weight = 0.3f, .burnsInDay = false, .splitInto = CreatureKind::MagmaCubeSmall,
     .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    {.name = "Large Magma Cube", .halfWidth = 0.75f, .height = 2.08f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.65f, .modelScale = 4.16f, .health = 16, .walkSpeed = 1.5f, .runSpeed = 3.0f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 6, .maxBlockLight = 15,
     .weight = 0.2f, .burnsInDay = false, .splitInto = CreatureKind::MagmaCubeMedium,
     .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    // The first flier, and `flies` is the whole archetype - the exact mirror of
    // `swims`: no gravity, a heading in three dimensions, and no pathfinder,
    // because there are no floors to plan a route across.
    //
    // **Neutral, not hostile.** It has a sting and no interest in using it
    // until struck, which is the wolf's arrangement and needs no flag of its
    // own - `attackDamage` above zero is already what sends a provoked animal
    // to `hurtByTarget` instead of `panic`. The generous `alertRange` is the
    // point of a bee rather than a detail: swatting one calls the rest.
    //
    // Ten health, 0.55 x 0.5 and two points of sting are Bedrock's. It keeps
    // `walksOnLand`, because a bee settles on a flower rather than hovering
    // over it forever.
    {.name = "Bee", .halfWidth = 0.275f, .height = 0.50f, .gaitRate = 0.0f, .modelScale = 1.00f,
     .health = 10, .walkSpeed = 2.4f, .runSpeed = 4.0f, .senseRange = 12.0f, .attackDamage = 2,
     .maxBlockLight = 15, .weight = 0.6f, .groupSize = 3, .stepHeight = 0.0f, .jumpHeight = 0.0f,
     .floats = false, .sinks = false, .flies = true, .maxLoaded = 12, .angerSeconds = 25.0f,
     .alertRange = 20.0f},

    // The village's guard. Neutral, never spawns on its own, and every number
    // here is Bedrock's except the two speeds — **those are authored natively**,
    // because our roster runs at about a third of the reference's and porting
    // its `movement 0.25` into a metres-per-second field is the wrong-unit trap.
    // 0.25 sits beside the zombie's 0.23, which is 1.0/2.2 here.
    //
    // `sinks`, `amphibious` and `breathesWater` together are the drowned's own
    // combination: it walks the bottom rather than swimming, and it does not
    // drown. There is no anger timer because Bedrock's golem has no
    // `minecraft:angry` component at all — `angerSeconds` is set long, like the
    // silverfish's, to mean "never forgets".
    {.name = "Iron Golem", .halfWidth = 0.70f, .height = 2.90f, .gaitRate = 2.60f,
     .gaitSwing = 0.66f, .modelScale = 1.00f, .health = 100, .walkSpeed = 1.0f, .runSpeed = 1.9f,
     .senseRange = 24.0f, .attackDamage = 7, .maxBlockLight = 15, .weight = 0.0f,
     .burnsInDay = false, .hunts = tagMask(CreatureTag::Monster), .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .floats = false, .amphibious = true, .breathesWater = true,
     .avoidsWater = true, .angerSeconds = 600.0f, .swingsArms = true, .attackDamageMax = 21,
     .knockbackScale = 3.15f, .knockbackLiftScale = 3.71f},
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

/// The flier's planner, which is the swimmer's with the second test removed. A
/// heading is good when the body still fits, and there is nothing else to ask:
/// air is simply everywhere the world is not solid, so the cell it lands in
/// needs no confirming the way a water cell does.
bool steerInAir(const World& world, const Creature& creature, float desiredYaw, float& chosenYaw) {
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

    /// What hour of the day it is, and therefore what a villager should be
    /// doing. **The phase sits above the behaviour table rather than inside
    /// it**: in the reference it is a `minecraft:scheduler` component that
    /// swaps whole component groups, which has no equivalent here, so it is
    /// computed once per tick beside the other per-tick world facts and each
    /// row's `canStart` reads it.
    VillagerPhase phase;

    /// The whole population, for the one question that genuinely needs it:
    /// whether a bed or a job block already belongs to somebody.
    ///
    /// **Derived, never stored.** A `cell -> creature` map on `Creatures` would
    /// be a second copy of a fact the creatures already hold, would have to be
    /// saved, and would need repairing every time a bed was broken - which is
    /// exactly the argument the double chest's pairing already settled.
    const std::vector<Creature>& population;

    /// Bearing to the nearest thing this species runs from, and whether there
    /// is one at all. Measured once per tick alongside everything else, and
    /// only for a species that actually fears something.
    ///
    /// Two booleans rather than one because starting to flee and carrying on
    /// fleeing are different distances: the reference's `avoid_mob_type` starts
    /// at its per-entry `max_dist` and only stops once `max_flee` blocks clear.
    bool feared;
    bool fearedNear;
    float yawFromFeared;
    float fearedDistance;

    /// The creature this one is fighting, or null when it is fighting the
    /// player or nothing. Resolved before the table runs, from `targetId`, so
    /// every consumer sees the same answer.
    const Creature* foe;
    glm::vec3 toFoe;
    float foeDistance;
    float yawToFoe;

    /// Bearing directly away from whatever last hurt this creature, and how far
    /// off that is. The threat is the player unless `threatId` names something,
    /// which is what lets a sheep run from the wolf that bit it rather than
    /// from whoever happens to be watching.
    float yawFromThreat;
    float threatDistance;

    /// Where a blow on the player is reported. Behaviours never touch the
    /// player themselves.
    CreatureAttack& attack;
    /// And where a blow on another creature is reported, for the stronger
    /// version of the same reason: the population is being walked right now.
    std::vector<CreatureHit>& hits;
    /// Where a loosed arrow is reported, for the same reason: `Creatures` has
    /// no idea projectiles exist.
    std::vector<Creatures::Launch>& launches;
    /// And a block a grazing animal has eaten, because only the main thread may
    /// write to the world.
    std::vector<glm::ivec3>& grazed;
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
    // walks on floors. A flier is the same case for the same reason - there is
    // no floor to plan a route across.
    if (ctx.species.swims || ctx.species.flies) {
        if (ctx.species.flies) {
            // **A flier must aim in three dimensions here or it never arrives.**
            // The dive angle belongs to `FlyWander`, and `FlyWander` is not
            // running while something else holds the movement controller - so a
            // chasing bee kept its cruising altitude and sailed over the player
            // forever, close enough to look interested and never once in reach.
            const glm::vec3 away = goal - ctx.self.position;
            const float flat = std::sqrt(away.x * away.x + away.z * away.z);
            ctx.self.targetPitch = std::clamp(-std::atan2(away.y, std::max(flat, 0.001f)),
                                              -kFlyChasePitchMax, kFlyChasePitchMax);
        }
        return walkToward(ctx, yawTo(ctx.self, goal));
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
    if (ctx.self.provokedTimer <= 0.0f || ctx.species.attackDamage <= 0) {
        return false;
    }
    // **A golem you built yourself never turns on you, and there is no timer on
    // that.** Bedrock gives a player-made golem its own `hurt_by_target` row
    // with the player filtered out — it carries no `minecraft:angry` component
    // at all, so this is structural rather than an anger duration set to zero.
    // `threatId == 0` means the player did it.
    if (ctx.self.playerBuilt && ctx.self.threatId == 0) {
        return false;
    }
    // A predator that is passive toward people still fights whatever bit it.
    // `retaliates` is the reference simply not giving a cat a `hurt_by_target`
    // goal - it kills rabbits and it runs from you, so "can it bite" and "will
    // it bite *you*" had to stop being one question.
    return ctx.species.retaliates || ctx.self.threatId != 0;
}

void hurtByTargetTick(const BehaviourContext& ctx) {
    if (ctx.self.threatId != 0) {
        ctx.self.target = CreatureTarget::Creature;
        ctx.self.targetId = ctx.self.threatId;
        return;
    }
    ctx.self.target = CreatureTarget::Player;
}

/// Hunting another creature. The mirror of `NearestAttackableTarget`, and it is
/// a separate row for the same reason that one is separate from `MeleeAttack`:
/// it produces a target and never asks what will be done with it.
///
/// **It is listed before the player's row on purpose.** Both claim no
/// controller, so both always run, and the later writer wins - which means a
/// wolf halfway through eating a sheep still turns on you the moment you are
/// worth turning on.
bool nearestPreyStart(const BehaviourContext& ctx) {
    return ctx.foe != nullptr;
}

void nearestPreyTick(const BehaviourContext& ctx) {
    ctx.self.target = CreatureTarget::Creature;
    ctx.self.targetId = ctx.foe->id;
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

/// Bolting. Prey only: anything willing to fight back does that instead.
///
/// It runs from whatever actually hurt it rather than always from the player,
/// which is what a sheep bitten by a wolf needs - `threatId` carries who, and
/// the context turns that into a bearing.
bool panicStart(const BehaviourContext& ctx) {
    if (ctx.self.provokedTimer <= 0.0f || ctx.species.hostile) {
        return false;
    }
    return ctx.species.attackDamage <= 0 ||
           (!ctx.species.retaliates && ctx.self.threatId == 0);
}

/// **It stops when it is actually clear, not when a timer says so.** The
/// reference's `avoid_mob_type` gives up at `max_flee`, ten blocks, and panic
/// deserves the same answer: bolting until a countdown expires means an animal
/// still sprinting long after nothing is chasing it, and one that stops dead
/// while the wolf is still on top of it.
bool panicContinue(const BehaviourContext& ctx) {
    return panicStart(ctx) && ctx.threatDistance < kFleeClear;
}

void panicTick(const BehaviourContext& ctx) {
    ctx.self.running = true;
    ctx.self.speedScale = ctx.species.panicSpeedScale;
    walkToward(ctx, ctx.yawFromThreat);
}

/// Closes on whatever the producers picked and bites it. It never asks *why*
/// there is a target, which is exactly why retaliation, pack anger and hunting
/// on sight all reach it through the same slot.
bool meleeAttackStart(const BehaviourContext& ctx) {
    // An archer never closes, and neither does a thrower. Excluded here as well
    // as being outranked by `RangedAttack`, so the two can never both want the
    // same controllers - and **the witch is the reason this is now two tests**:
    // it set neither flag, so it fell straight through to melee and dealt two
    // damage by walking into you, with no animation and nothing thrown.
    return ctx.self.target != CreatureTarget::None && !ctx.species.shootsArrows &&
           !ctx.species.throwsPotions;
}

void meleeAttackTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    self.running = true;
    self.speedScale = ctx.species.chaseSpeedScale;

    // **Everything below is measured against whatever is being fought**, which
    // is the player unless the target slot names a creature. Resolved once into
    // one set of locals rather than branching at each use, because the whole
    // point of the target slot is that a consumer never asks who filled it.
    const bool onCreature = self.target == CreatureTarget::Creature && ctx.foe != nullptr;
    if (self.target == CreatureTarget::Creature && ctx.foe == nullptr) {
        // The target died or walked out of range between the producer writing
        // it and this row reading it. Nothing to close on.
        return;
    }
    const CreatureSpecies& foeSpecies =
        onCreature ? speciesInfo(ctx.foe->kind) : ctx.species;
    const glm::vec3 toTarget = onCreature ? ctx.toFoe : ctx.toPlayer;
    const float distance = onCreature ? ctx.foeDistance : ctx.distance;
    const float yawToTarget = onCreature ? ctx.yawToFoe : ctx.yawToPlayer;
    const float targetHalfWidth =
        onCreature ? foeSpecies.halfWidth * ctx.foe->scale : player_constants::kWidth * 0.5f;
    const float targetHeight =
        onCreature ? foeSpecies.height * ctx.foe->scale : player_constants::kHeight;
    const float pitchToTarget =
        onCreature ? -std::atan2(toTarget.y + targetHeight * 0.5f -
                                     ctx.species.height * self.scale * 0.85f,
                                 std::max(distance, 0.001f))
                   : ctx.pitchToPlayer;

    // It holds the look controller as well as the movement one, so nothing
    // lower down can pull its gaze off what it is chasing.
    self.targetHeadYaw = yawToTarget;
    self.targetHeadPitch = pitchToTarget;

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
    const float contact = halfWidth + targetHalfWidth;
    // Arriving means arriving on the same **footing**. Anything more than a
    // step up or down is something to climb rather than something reached, and
    // both the step-up and the jump read `walking` - so a zombie at the foot of
    // a one-block ledge has to keep walking into it or it stands there forever.
    //
    // A flier is exempt for the same reason the hopper is: it has no footing to
    // share and closes in three dimensions, so proximity alone *is* arrival.
    // Without this a bee never stops, sails through the player, and has to come
    // all the way about for another pass.
    const bool arrived =
        distance <= contact &&
        (ctx.species.flies || std::abs(toTarget.y) <= ctx.species.stepHeight);
    if (arrived && !ctx.species.hops) {
        // `walking` is sticky - it survives from whichever behaviour set it
        // last - so standing still has to be said outright. Claiming the
        // movement controller only stops anything else *steering*. This is the
        // trap the Bramble's fuse already fell into once.
        self.walking = false;
        self.targetYaw = yawToTarget;
        self.route.clear();
    } else {
        // A searched route rather than a bearing, which is the whole of why a
        // wall is now something to walk around rather than something to press
        // against until the player happens to come back into the open.
        walkTo(ctx, self.position + toTarget);
    }

    const float length = std::max(distance, 0.001f);
    const glm::vec3 out{-toTarget.x / length, 0.0f, -toTarget.z / length};

    // Whether the two boxes overlap vertically, which is a real AABB test and
    // **not symmetric**: the creature spans `[0, height]` and the target spans
    // `[toTarget.y, toTarget.y + its own height]`, so a slime sailing over your
    // boots is still hitting you and so is a bee at your shoulder.
    const bool verticalOverlap = toTarget.y < ctx.species.height * self.scale &&
                                 toTarget.y > -targetHeight;

    // Whether the two bodies genuinely overlap, which is a different question
    // from having arrived.
    const bool touching = distance <= contact && verticalOverlap;

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
    const float reach = halfWidth + kMeleeHorizontalReach + targetHalfWidth;
    // And it has to be facing you. `melee_fov` is 90 degrees in the reference,
    // which turns walking round a creature into a real half-second of grace
    // while it comes about rather than a cosmetic detail.
    const float offAxis = std::abs(std::remainder(yawToTarget - self.yaw, kTwoPi));

    if (ctx.species.attackDamage <= 0 || distance >= reach || self.attackTimer > 0.0f ||
        offAxis > kMeleeHalfFov || !verticalOverlap) {
        return;
    }
    self.attackTimer = kAttackInterval;
    self.swingTimer = kAttackSwingSeconds;

    // A blow is a range where a species states one, and exactly `attackDamage`
    // where it does not. **Defaulting the top of the range to zero is what
    // leaves every existing row provably untouched** rather than requiring
    // fifty-seven of them to restate a number they already carry.
    int damage = ctx.species.attackDamage;
    if (ctx.species.attackDamageMax > damage) {
        damage += static_cast<int>(nextRandom(ctx.random) *
                                   static_cast<float>(ctx.species.attackDamageMax - damage + 1));
        damage = std::min(damage, ctx.species.attackDamageMax);
    }

    const glm::vec3 push = -out * (kKnockbackSpeed * ctx.species.knockbackScale) +
                           glm::vec3{0.0f, kKnockbackLift * ctx.species.knockbackLiftScale, 0.0f};
    if (onCreature) {
        // Reported rather than applied: the population is being walked right
        // now, and writing into a neighbour mid-walk leaves half of it reading
        // this tick and half the last one.
        ctx.hits.push_back({ctx.foe->id, self.id, damage, push});
        return;
    }

    ctx.attack.damage += damage;
    // Only the hardest blow moves you. Summing them lets a pack launch the
    // player clear across the world in one frame, which is what emptied the map
    // on the first night the hostiles worked.
    if (!ctx.attack.landed || glm::dot(push, push) > glm::dot(ctx.attack.push, ctx.attack.push)) {
        ctx.attack.push = push;
    }
    ctx.attack.landed = true;
}

/// A point on a creature's body, in the same model units `buildMesh`'s `place`
/// takes: forward from the middle, up from the feet, and out to its **left**.
struct ModelPoint {
    float alongForward = 0.0f;
    float up = 0.0f;
    float alongSide = 0.0f;
};

/// The biped rig's arm, as `biped` itself authors it.
///
/// Lifted to file scope because **two things now read these numbers**: the mesh
/// hangs a bow on the hand, and the archer below looses an arrow from it. A
/// hand worked out twice is a hand in two places, which is the first entry in
/// `CLAUDE.md`'s box of bug shapes.
constexpr float kBipedArmRestUp = 1.125f;
constexpr float kBipedShoulderGap = 0.25f;
constexpr float kBipedArmLength = 12.0f;
constexpr float kBipedArmGrow = 0.005f;

/// Where a biped's hand ends up, in model units.
///
/// **`legBox` derives its pivot from the box's own extent** - half the thinner
/// cross-section below the top face - and the hand is that pivot swung by the
/// whole length of the limb, so this reproduces `legBox`'s arithmetic rather
/// than approximating it. At `pitch` and `roll` of zero it returns the bottom
/// of an arm hanging at rest, which is the same "reduces to what it replaced"
/// property `legBox` and `beginHead` both have.
///
/// `mainHand` is the creature's **right**, which is `-side`: the player skin
/// net this rig uses paints the right arm at (40,16) and the left at (32,48),
/// and the Princepin's two arms - the one model here with a different net per
/// side - already sit that way round. That is the check, not a guess.
///
/// Swell is deliberately absent: the one species that swells has no hands.
ModelPoint bipedHandPoint(float limbWidth, float pitch, float roll, bool mainHand) {
    const float halfHeight = kBipedArmLength * kTexel * 0.5f + kBipedArmGrow;
    const float overhang = limbWidth * kTexel * 0.5f + kBipedArmGrow;
    const float hang = std::max(halfHeight - overhang, 0.0f);
    const float reach = hang + halfHeight;
    const float cosRoll = std::cos(roll);
    const float shoulder = (mainHand ? -1.0f : 1.0f) *
                           (kBipedShoulderGap + limbWidth * kTexel * 0.5f);
    return {-std::sin(pitch) * cosRoll * reach,
            kBipedArmRestUp + hang - std::cos(pitch) * cosRoll * reach,
            shoulder + std::sin(roll) * reach};
}

/// The villager rig's folded hands, in model units.
///
/// **Derived from the fold's own pivot and the arm's own length**, not measured
/// off a picture. `villagerRig` places three boxes as one rigid group pitched
/// `kVillagerFold`, and solving its two stated centres back for the point they
/// turn about gives 1.384 from both to within the sixteenth of a texel the
/// forearm block is deliberately nudged by. The hands are then that pivot
/// carried one arm's length along the way the fold points.
constexpr float kVillagerFold = -0.75f;
constexpr float kVillagerArmPivotUp = 1.384f;
constexpr float kVillagerArmLength = 8.0f;

ModelPoint villagerHandPoint() {
    const float reach = kVillagerArmLength * kTexel;
    return {-std::sin(kVillagerFold) * reach,
            kVillagerArmPivotUp - std::cos(kVillagerFold) * reach, 0.0f};
}

/// A model point in world space, for the two things outside `buildMesh` that
/// need one: an arrow leaves an archer's bow and a bottle leaves a witch's
/// hands, and both have to start where the thing being thrown is *drawn*.
///
/// Reads `position`, never `renderPosition`: the step-up smoothing is a fact
/// about the picture and nothing that reasons about the world may see it.
glm::vec3 modelPointToWorld(const Creature& creature, const CreatureSpecies& species,
                            const ModelPoint& point) {
    const float scale = species.modelScale * creature.scale;
    const float sinYaw = std::sin(creature.yaw);
    const float cosYaw = std::cos(creature.yaw);
    return creature.position + glm::vec3{sinYaw, 0.0f, cosYaw} * (point.alongForward * scale) +
           glm::vec3{cosYaw, 0.0f, -sinYaw} * (point.alongSide * scale) +
           glm::vec3{0.0f, point.up * scale, 0.0f};
}

/// How an archer fights: at a distance, on a cadence, and never by touching you.
///
/// The reference's `behavior.ranged_attack` in miniature - engage inside
/// `kArcherRange`, back off inside `kArcherTooClose`, close up outside
/// `kArcherPreferred`, and loose on `species.rangedInterval`.
constexpr float kArcherRange = 15.0f;
constexpr float kArcherPreferred = 9.0f;
constexpr float kArcherTooClose = 4.0f;
/// Bedrock's `mob_arrow` power, in blocks per tick.
constexpr float kArcherPower = 1.6f;
/// `uncertainty_base` 16 less `uncertainty_multiplier` 4 times a Normal
/// difficulty of 2. **Skeletons get more accurate as difficulty rises**, which
/// is the opposite of the obvious guess; we have one difficulty, so this is the
/// Normal row.
constexpr float kArcherSpread = 8.0f;
/// Degrees per unit of that spread, expressed as an offset on a unit aim
/// vector.
constexpr float kSpreadPerUnit = 0.0172275f;

/// How far off straight ahead a target may be and still be shot at, in radians.
///
/// **Asked of the body, never the head.** The bow is in a hand, the hand hangs
/// off the body, and the arrow's direction is worked out from where the target
/// actually is - so an archer that fires before it has finished turning sends
/// an arrow sideways out of a bow pointing somewhere else. Twenty degrees is
/// tight enough that the two always agree and loose enough that a `kTurnRate`
/// of 4 rad/s reaches it in a fraction of the reload.
constexpr float kArcherFov = 0.35f;

/// How long the loose itself takes, and it is the **tail of the reload** rather
/// than time added to it - the cadence is still `rangedInterval`.
///
/// Once it starts, nothing stops it but dying: an archer plants where it
/// stands, holds what it has, and the arrow goes. That is also what guarantees
/// it is never retreating at the moment of release, whatever it was doing when
/// it committed.
constexpr float kArcherRelease = 0.4f;

/// How fast the weapon comes up and goes back down, in fractions of the pose
/// per second. A sixth of a second either way - fast enough that an archer
/// looks ready rather than slow, slow enough that the bow is not simply *at*
/// the shoulder on the frame the target is acquired.
///
/// **The easing is ours; the reference has none.** Its controller is two states
/// with no blend time at all, so a Bedrock skeleton's bow snaps up. A sixth of
/// a second is below the threshold where that reads as a delay and above the
/// one where it reads as a pop.
constexpr float kAimRate = 6.0f;

/// `animation.humanoid.bow_and_arrow`, and it is a far simpler pose than the
/// obvious guess. **Both arms go straight out to -90 degrees**; the only thing
/// that separates the bow hand from the string hand is a third of a turn of
/// **yaw** - the reference's own +28.65 on the left against -5.73 on the right,
/// which brings the string hand in across the chest. There is no draw-back, no
/// second pitch, and no keyframes: the entry has two bones and nothing else.
///
/// Our `legBox` takes a pitch and a roll rather than a yaw, and for an arm
/// already pointing straight forward those are the same motion - the hand runs
/// along `forward * cos(roll) + side * sin(roll)`, so the roll *is* the yaw
/// once the arm is level. Positive swings toward `side`, which is the
/// creature's left, so the left arm's inward turn is the negative one.
///
/// 28.65 degrees is 0.5 radians and 5.73 is 0.1: the reference's numbers are
/// radian constants that have been through a conversion, which is a good sign
/// they are being read the right way round.
constexpr float kArcherArmPitch = -1.5708f;
constexpr float kArcherBowArmRoll = 0.10f;
constexpr float kArcherStringArmRoll = -0.50f;

/// Every archer on the roster is the skeleton rig, at a two-texel limb.
/// **This is asserted rather than assumed**, because the hand the arrow leaves
/// from is derived from that limb width: giving a four-texel biped
/// `shootsArrows` would otherwise fire its arrows out of a shoulder, silently,
/// and nothing about the shot would look wrong enough to notice.
constexpr float kArcherLimbWidth = 2.0f;

constexpr bool archersShareOneRig() {
    for (std::size_t i = 0; i < std::size(kSpecies); ++i) {
        if (!kSpecies[i].shootsArrows) {
            continue;
        }
        const auto kind = static_cast<CreatureKind>(i);
        if (kind != CreatureKind::Skeleton && kind != CreatureKind::Stray &&
            kind != CreatureKind::Bogged) {
            return false;
        }
    }
    return true;
}
static_assert(archersShareOneRig(),
              "every archer must be the skeleton rig, or the arrow leaves the wrong place");

/// How a witch fights, and it is the archer's shape with different numbers.
///
/// `attack_radius` 10 rather than 15, because a bottle arcs where an arrow
/// flies and has to be lobbed from closer in.
constexpr float kWitchRange = 10.0f;
constexpr float kWitchPreferred = 7.0f;
constexpr float kWitchTooClose = 3.0f;
/// The reference's own throw: power 0.75 blocks per tick, inaccuracy 8, aimed
/// at the target's eye **less 1.1** so the bottle bursts at their feet rather
/// than over their head. Same 8-unit spread the archer uses, which is why the
/// two share the wobble below.
constexpr float kWitchThrowPower = 0.75f;
constexpr float kWitchAimDrop = 1.1f;
/// Far enough away that a bottle of Slowness is worth more than damage. The
/// reference's own threshold; its companion at three blocks is in the table
/// above `witchBrewFor` and is unreachable until player health arrives here.
constexpr float kWitchSlownessRange = 8.0f;
/// How long before a throw a witch reaches for the bottle, in seconds.
///
/// **Ours.** The reference has no witch throw animation to port, and without
/// some tell a potion appears out of a motionless villager. Long enough to be
/// seen coming and short enough that the other two and a half seconds of the
/// reload still read as a witch walking at you.
constexpr float kWitchReach = 0.6f;
/// How far the folded arms swing up over a throw, in radians. Taken with the
/// fold's own -0.75 it ends just past straight out, which is an overhand lob
/// rather than a punch - and it is applied to the assembly **as one rigid
/// group**, because that fold is closed and may only move without coming apart.
///
/// **Ours, not the reference's.** Bedrock's witch has no throw animation in the
/// shipped pack at all; without some tell, a thrown potion appears out of a
/// motionless villager.
constexpr float kWitchArmRaise = 0.85f;

/// The row in `kPotions` carrying an effect at a given strength.
///
/// **Scanned rather than written down.** The forty-one brews are a table in
/// `Item.hpp` and their indices are an implementation detail of it; a literal
/// 23 here would be a second copy of that ordering, and inserting one brew
/// would silently turn every witch into a thrower of something else.
constexpr int potionRow(effects::Effect effect, int amplifier) {
    for (std::size_t i = 0; i < kPotions.size(); ++i) {
        if (kPotions[i].effect == effect && kPotions[i].amplifier == amplifier &&
            kPotions[i].second == effects::Effect::None) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

/// The splash form of a brew, which is the only form a witch ever holds.
constexpr ItemId witchBrew(effects::Effect effect) {
    return potionAt(potionRow(effect, 0), 1);
}

static_assert(isSplashPotion(witchBrew(effects::Effect::Slowness)) &&
                  isSplashPotion(witchBrew(effects::Effect::Poison)) &&
                  isSplashPotion(witchBrew(effects::Effect::Weakness)) &&
                  isSplashPotion(witchBrew(effects::Effect::InstantDamage)),
              "a witch must be holding a splash potion, not a bottle it would drink");
static_assert(potionKind(witchBrew(effects::Effect::InstantDamage)).effect ==
                      effects::Effect::InstantDamage &&
                  potionKind(witchBrew(effects::Effect::Poison)).effect == effects::Effect::Poison,
              "the scan must find the brew it was asked for");

/// Which bottle to reach for.
///
/// **This is a first-match list in the reference's shipped `minecraft:shooter`,
/// not engine code**, and the order is Mojang's own:
///
/// | | Potion | Condition |
/// |---|---|---|
/// | 1 | Healing | target is a raider on 4 health or less |
/// | 2 | Regeneration | target is any raider |
/// | 3 | Slowness | target 8 blocks or further, not already slowed |
/// | 4 | Poison | target on 8 health or more, not already poisoned |
/// | 5 | Weakness | target within 3 blocks, not weakened, one chance in four |
/// | - | Harming | when nothing above matched |
///
/// **Two of the six are dead here and two more are unreachable, and both
/// reasons are worth stating rather than hiding.** We have no raiders, so rows
/// 1 and 2 cannot fire at all. And nothing in this system can see how hurt the
/// player is - `Creatures` is handed their feet and whether they are sneaking,
/// and nothing else - so row 4's health test is taken as satisfied, which is
/// exactly what the reference does for anyone on eight health or more. That
/// leaves rows 5 and 6 unreachable until player health arrives here, at which
/// point they are two lines and the brews are already named below.
///
/// The result is faithful for a healthy player, which is nearly always: at
/// range a witch slows you so it can keep the range, and in close it poisons
/// you.
ItemId witchBrewFor(const BehaviourContext& ctx) {
    if (ctx.distance >= kWitchSlownessRange) {
        return witchBrew(effects::Effect::Slowness);
    }
    return witchBrew(effects::Effect::Poison);
}

bool rangedAttackStart(const BehaviourContext& ctx) {
    return (ctx.species.shootsArrows || ctx.species.throwsPotions) &&
           ctx.self.target != CreatureTarget::None;
}

/// Lets the shot go. Split out of the tick because the tick decides *whether*
/// and this decides *where*, and a committed release has to be able to run it
/// from a branch that has already stopped thinking about anything else.
void loose(const BehaviourContext& ctx, bool thrower) {
    Creature& self = ctx.self;

    // Where the shot leaves, and it is **the hand holding the weapon** rather
    // than the middle of the chest it used to be. Both points come from the
    // same two functions the mesh hangs the item on, so the arrow can only ever
    // leave the bow and the bottle can only ever leave the fingers.
    const glm::vec3 from =
        thrower ? modelPointToWorld(self, ctx.species, villagerHandPoint())
                : modelPointToWorld(self, ctx.species,
                                    bipedHandPoint(kArcherLimbWidth,
                                                   kArcherArmPitch + self.headPitch,
                                                   kArcherBowArmRoll, true));

    // A bottle is aimed **below** the eye and an arrow above the waist: the one
    // is meant to burst at your feet and the other to hit your chest.
    const float aimHeight = thrower ? player_constants::kEyeHeight - kWitchAimDrop
                                    : player_constants::kEyeHeight * 0.66f;
    const glm::vec3 to = ctx.self.position + ctx.toPlayer + glm::vec3{0.0f, aimHeight, 0.0f};

    glm::vec3 aim = to - from;
    const float flat = std::sqrt(aim.x * aim.x + aim.z * aim.z);
    // Aim above the target by a fifth of the ground distance. The reference's
    // own lead, and without it an archer's arrows all land at your feet -
    // gravity is not something a straight aim can survive over fifteen metres.
    // The reference gives a witch's throw the identical fifth, which is why one
    // line serves both.
    aim.y += flat * 0.2f;

    const float length = glm::length(aim);
    if (length < 1e-4f) {
        return;
    }
    aim /= length;

    // Triangular rather than uniform - the sum of two rolls, so a shot clusters
    // near true and only rarely goes wide.
    const auto wobble = [&ctx]() {
        return (nextRandom(ctx.random) - nextRandom(ctx.random)) * kSpreadPerUnit * kArcherSpread;
    };
    aim += glm::vec3{wobble(), wobble(), wobble()};

    if (thrower) {
        ctx.launches.push_back({from, aim * kWitchThrowPower,
                                Creatures::LaunchKind::SplashPotion, self.heldOverride});
        // Out of its hands the instant it leaves them.
        self.heldOverride = ItemId::None;
        return;
    }
    ctx.launches.push_back({from, aim * kArcherPower});
}

void rangedAttackTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    self.targetHeadYaw = ctx.yawToPlayer;
    self.targetHeadPitch = ctx.pitchToPlayer;
    self.targetYaw = ctx.yawToPlayer;
    self.speedScale = ctx.species.chaseSpeedScale;

    // One behaviour, two weapons. They engage at different distances and throw
    // different things, and everything between those two facts - closing,
    // backing off, reloading, leading the shot, the spread - is identical, so
    // it is written once. A second row in the table would have been a second
    // copy of all of it.
    const bool thrower = ctx.species.throwsPotions;
    const float range = thrower ? kWitchRange : kArcherRange;
    const float preferred = thrower ? kWitchPreferred : kArcherPreferred;
    const float tooClose = thrower ? kWitchTooClose : kArcherTooClose;
    const float release = thrower ? kWitchReach : kArcherRelease;

    // **The weapon comes up on sight of a target and stays up.** The
    // reference's `controller.animation.humanoid.bow_and_arrow` is two states
    // transitioning on `query.has_target` and on nothing else - no charge gate,
    // no blend, no animation length. So a skeleton with a target is an archer
    // with a raised bow whether or not it can shoot this second, and the three
    // seconds between shots are a *reload happening behind a drawn bow* rather
    // than a wind-up. This behaviour only runs with a target, so saying it here
    // says exactly that.
    //
    // **A witch is ours and is deliberately not that.** Bedrock ships no throw
    // animation for her at all, and holding the arms up for the whole
    // engagement would leave a villager brandishing a bottle at you for three
    // seconds at a stretch. She reaches for the bottle only while she is
    // actually committed to throwing it, which is what makes a throw read as a
    // throw - and it is the reason `aimWanted` exists apart from `aiming`
    // rather than being the same flag twice.
    self.aiming = true;
    self.aimWanted = (!thrower || self.releaseTimer > 0.0f) ? 1.0f : 0.0f;

    // **A shot, once started, finishes.** Nothing below cancels a release: not
    // losing sight of you, not stepping out of range, not being shoved. Only
    // dying does, and it does so by the corpse branch never reaching this
    // behaviour at all. The archer plants where it stands for the length of it,
    // which is also what guarantees the second half of the rule below - that it
    // is never retreating at the moment the arrow leaves.
    if (self.releaseTimer > 0.0f) {
        self.walking = false;
        self.route.clear();
        self.releaseTimer -= ctx.deltaSeconds;
        if (self.releaseTimer > 0.0f) {
            return;
        }
        self.releaseTimer = 0.0f;
        self.drawTimer = 0.0f;
        loose(ctx, thrower);
        return;
    }

    if (ctx.distance > preferred) {
        self.running = true;
        walkTo(ctx, ctx.self.position + ctx.toPlayer);
    } else if (ctx.distance < tooClose) {
        // Backing away is a bearing rather than a place, like panic: there is
        // nowhere in particular it wants to be, only somewhere further off.
        self.running = true;
        walkToward(ctx, ctx.yawToPlayer + 3.14159265f);
    } else {
        self.walking = false;
        self.route.clear();
    }

    // Clamped rather than left to run on, so a target that ducks behind a wall
    // for a minute is shot at once when it steps out and not four times.
    self.drawTimer = std::min(self.drawTimer + ctx.deltaSeconds, ctx.species.rangedInterval);
    if (self.drawTimer < ctx.species.rangedInterval) {
        return;
    }

    // **Four things have to be true before it will even start a shot**, and the
    // reload sitting at full while they are not is the point: it looses the
    // moment they come true rather than losing its turn.
    //
    // The first two are the old ones - in range, and it can actually see you.
    // The third is that the **body** is pointed at you: the arrow's direction
    // is worked out from where you are, so a skeleton that fires while still
    // turning sends an arrow sideways out of a bow that is not facing it. The
    // body yaw is the right one to ask because the bow is in a hand and the
    // hand follows the body - the head turns on its own and must not count.
    // And the fourth is that it is not **backing away**: it may shoot standing
    // still or while closing, never while retreating.
    const float offAim = std::abs(std::remainder(ctx.yawToPlayer - self.yaw, kTwoPi));
    const bool retreating = ctx.distance < tooClose;
    if (ctx.distance > range || !ctx.seesPlayer || offAim > kArcherFov || retreating) {
        return;
    }

    // Committed. From here the release runs to its end on its own.
    //
    // The bottle is chosen **here**, at the moment she reaches for it, and held
    // until it leaves her hand - so what she is visibly holding is what arrives.
    // It has to be this side of the commit rather than during the release,
    // because the release branch above does nothing but count down and throw.
    if (thrower) {
        self.heldOverride = witchBrewFor(ctx);
    }
    self.releaseTimer = release;
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

/// The nearest flower to a point, inside a small box, and a bee's whole reason
/// for leaving the hive. Returns false when there is none, which is the common
/// case and costs the same scan either way.
bool nearestFlower(const World& world, const glm::vec3& from, glm::vec3& found) {
    const int cx = static_cast<int>(std::floor(from.x));
    const int cy = static_cast<int>(std::floor(from.y));
    const int cz = static_cast<int>(std::floor(from.z));
    float best = 0.0f;
    bool any = false;
    for (int dy = -kFlowerSearchY; dy <= kFlowerSearchY; ++dy) {
        for (int dz = -kFlowerSearchXZ; dz <= kFlowerSearchXZ; ++dz) {
            for (int dx = -kFlowerSearchXZ; dx <= kFlowerSearchXZ; ++dx) {
                if (!isFlower(world.blockAt(cx + dx, cy + dy, cz + dz))) {
                    continue;
                }
                const glm::vec3 at{static_cast<float>(cx + dx) + 0.5f,
                                   static_cast<float>(cy + dy) + 0.5f,
                                   static_cast<float>(cz + dz) + 0.5f};
                const glm::vec3 away = at - from;
                const float distance = glm::dot(away, away);
                if (!any || distance < best) {
                    best = distance;
                    found = at;
                    any = true;
                }
            }
        }
    }
    return any;
}

/// Cruising, for anything that flies - the airborne twin of `SwimWander`, and
/// it claims the same controller as both, so the three are mutually exclusive:
/// a flier never ambles, a walker never cruises and neither swims.
///
/// Two things separate it from the swimmer's version. **A flier has no surface
/// to hold it down**, so it keeps a height band above the ground rather than
/// roaming freely in the vertical - without that, a bee climbs on its first
/// upward roll and never comes back. And **it is drawn to flowers**: the scan
/// simply replaces the rolled heading, so there is no goal to store and nothing
/// to repair when the flower is picked before it arrives.
bool flyWanderStart(const BehaviourContext& ctx) {
    return ctx.species.flies;
}

void flyWanderTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;

    if (self.decisionTimer <= 0.0f) {
        self.decisionTimer = kFlyDecisionMin + nextRandom(ctx.random) * kFlyDecisionSpan;
        self.targetYaw = nextRandom(ctx.random) * kTwoPi;
        self.targetPitch = (nextRandom(ctx.random) * 2.0f - 1.0f) * kFlyPitchMax;

        glm::vec3 flower{0.0f};
        if (nearestFlower(ctx.world, self.position, flower)) {
            self.targetYaw = yawTo(self, flower);
            self.targetPitch = 0.0f;
        }
    }

    self.walking = true;
    float routed = self.targetYaw;
    if (!steerInAir(ctx.world, self, self.targetYaw, routed)) {
        // Boxed in. Turn about and decide again immediately rather than grind
        // into the wall for the rest of the timer.
        self.targetYaw += 3.14159265f;
        self.decisionTimer = 0.0f;
        return;
    }
    self.targetYaw = routed;

    // The height band. Positive pitch is nose-down, so a floor under the angle
    // is a clamp toward zero and a ceiling over it is the same clamp the other
    // way. Counting clear cells downward stops at the ceiling height, because
    // the only question is which side of the band it is on.
    const int hx = static_cast<int>(std::floor(self.position.x));
    const int hz = static_cast<int>(std::floor(self.position.z));
    const int feet = static_cast<int>(std::floor(self.position.y));
    int clearance = 0;
    while (clearance < kFlyCeilingBlocks && !ctx.world.isSolid(hx, feet - 1 - clearance, hz)) {
        ++clearance;
    }
    if (clearance < kFlyFloorBlocks) {
        self.targetPitch = -kFlyPitchMax;
    } else if (clearance >= kFlyCeilingBlocks) {
        self.targetPitch = std::max(self.targetPitch, 0.0f);
    }

    // And anything directly overhead, which the horizontal fan cannot see.
    const float top = self.position.y + ctx.species.height * self.scale;
    if (ctx.world.isSolid(hx, static_cast<int>(std::floor(top + kFlyProbe)), hz)) {
        self.targetPitch = std::max(self.targetPitch, 0.0f);
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

/// Running from something. The reference's `avoid_mob_type`, generalised from
/// the one pairing it started as — a Bramble fleeing a cat — to any species
/// naming any family in `avoids`.
///
/// **Priority 3 is load-bearing.** It sits *below* `Swell`, so a fuse already
/// lit is not called off by a cat wandering past - the reference's own rule -
/// and *above* `MeleeAttack`, so a skeleton would rather back away from a wolf
/// than shoot at you.
///
/// The start and continue distances are genuinely different numbers, which is
/// the whole of "a flight that ends properly": it begins at the species' own
/// `max_dist` and only stops once `max_flee` blocks clear.
bool avoidStart(const BehaviourContext& ctx) {
    return ctx.fearedNear;
}

bool avoidContinue(const BehaviourContext& ctx) {
    return ctx.feared && ctx.fearedDistance < kFleeClear;
}

void avoidTick(const BehaviourContext& ctx) {
    // Bolt while it is close, walk once it is merely nearby. The reference's
    // `sprint_distance`, and it is what stops an animal sprinting flat out for
    // the whole ten blocks.
    ctx.self.running = ctx.fearedDistance < kFleeSprint;
    ctx.self.speedScale = kAvoidSpeedScale;
    ctx.self.targetHeadYaw = ctx.yawFromFeared + 3.14159265f;
    walkToward(ctx, ctx.yawFromFeared);
}

/// The cell a grazing animal's mouthful comes out of.
///
/// **One owner for both halves**: `eatBlockStart` asks whether there is
/// anything to eat here and `eatBlockTick` reports what was eaten, and two
/// separate derivations of the same cell would eventually disagree about which
/// block the animal had its head in.
///
/// Nudged up a tenth of a block before flooring, because a body resting exactly
/// on a surface sits on the boundary: `position.y` lands a hair under the
/// integer as often as on it, and `floor` then names the solid block the
/// creature is standing *on* rather than the cell it occupies.
glm::ivec3 grazeCell(const Creature& self) {
    return glm::ivec3{static_cast<int>(std::floor(self.position.x)),
                      static_cast<int>(std::floor(self.position.y + 0.1f)),
                      static_cast<int>(std::floor(self.position.z))};
}

/// Whether there is a mouthful here at all. The reference's
/// `eat_and_replace_block_pairs` names what may be eaten and what it turns
/// into, so a sheep standing on snow or sand never starts the animation - which
/// is the difference between an animal grazing and one miming it.
bool hasGrazeBlock(const World& world, const Creature& self) {
    const glm::ivec3 cell = grazeCell(self);
    return world.blockAt(cell.x, cell.y, cell.z) == BlockId::TallGrass ||
           world.blockAt(cell.x, cell.y - 1, cell.z) == BlockId::Grass;
}

/// Stopping to crop the grass. Bedrock's `behavior.eat_block`, which on the
/// shipped roster belongs to the sheep alone.
///
/// It claims **both** controllers, because an animal with its muzzle in the
/// ground is neither walking nor looking at you - and because otherwise
/// `Wander` would keep steering it while it ate.
bool eatBlockStart(const BehaviourContext& ctx) {
    const Creature& self = ctx.self;
    if (!ctx.species.grazes || !self.onGround || self.inWater) {
        return false;
    }
    if (!hasGrazeBlock(ctx.world, self)) {
        return false;
    }
    // A lamb grazes twenty times as often as a ewe, which is the reference's
    // own `query.is_baby ? 0.02 : 0.001` and is why a flock with young in it
    // visibly strips the ground.
    const float chance = self.scale < 1.0f ? kGrazeBabyChancePerSecond : kGrazeChancePerSecond;
    return nextRandom(ctx.random) < chance * ctx.deltaSeconds;
}

bool eatBlockContinue(const BehaviourContext& ctx) {
    return ctx.self.eatTimer > 0.0f;
}

void eatBlockTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    if (self.eatTimer <= 0.0f) {
        self.eatTimer = kGrazeSeconds;
    }

    // `walking` is sticky, so standing still has to be said outright - the same
    // trap the fuse and the melee arrival both fell into.
    self.walking = false;
    self.route.clear();
    self.targetHeadPitch = kGrazeHeadPitch;

    self.eatTimer -= ctx.deltaSeconds;
    if (self.eatTimer > 0.0f) {
        return;
    }

    // **The mouthful is taken at the end of the animation, not the start**, so
    // a sheep interrupted half way through gets nothing - which is the
    // reference's behaviour and the reason `time_until_eat` is a countdown
    // rather than a cooldown. Checked again here because it may have wandered
    // or the block may have gone in the meantime. Reported rather than done:
    // only the main thread may write to the world.
    if (hasGrazeBlock(ctx.world, self)) {
        ctx.grazed.push_back(grazeCell(self));
    }
}

// ---------------------------------------------------------------------------
// The villager's day
// ---------------------------------------------------------------------------

/// Bedrock's `dweller` scan: sixteen blocks out, four up and down.
constexpr int kPoiReach = 16;
constexpr int kPoiHeight = 4;
/// `update_interval_base` 60 ticks, `variant` 40, so three to five seconds.
constexpr float kPoiScanMin = 3.0f;
constexpr float kPoiScanSpan = 2.0f;
/// `behavior.work`: 250 ticks standing at the block, 200 ticks of cooldown.
constexpr float kWorkSeconds = 12.5f;
constexpr float kWorkCooldown = 10.0f;
/// How close counts as arriving at a claimed cell.
constexpr float kClaimArrival = 1.8f;

/// The scheduler's `max_delay_secs`, as a fraction of a day.
///
/// Bedrock staggers every schedule change by 0 to 10 seconds per villager,
/// which is one line and is the whole of why a village does not change shift in
/// lockstep. Ten seconds against our default ten-minute day is this fraction;
/// it is deliberately not read from the day-length setting, because it is a
/// scatter rather than a duration and a longer day should scatter no wider.
constexpr float kSchedulePhaseStagger = 0.017f;

glm::vec3 cellCentre(const glm::ivec3& cell) {
    return {static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y),
            static_cast<float>(cell.z) + 0.5f};
}

/// Whether somebody else already owns this cell. Linear over a population that
/// is measured in tens, and it is the *only* record of a claim — see the note
/// on `BehaviourContext::population`.
bool alreadyClaimed(const BehaviourContext& ctx, const glm::ivec3& cell) {
    for (const Creature& other : ctx.population) {
        if (other.id == ctx.self.id || other.health <= 0) {
            continue;
        }
        if (other.bedCell == cell || other.jobCell == cell) {
            return true;
        }
    }
    return false;
}

/// Claiming a bed, a job site and a bell.
///
/// **A producer, exactly like the targeting rows**: it claims no controller, so
/// it always runs and never argues with whatever the villager is doing. Taking
/// a job *is* gaining a profession — there is no separate application step,
/// which is the reference's own arrangement and the reason a freshly generated
/// village has no professionals in it at all.
bool claimPoiStart(const BehaviourContext& ctx) {
    if (!ctx.species.keepsHouse) {
        return false;
    }
    const Creature& self = ctx.self;
    // Once everything is claimed there is nothing to scan for, and the scan is
    // ten thousand block reads.
    return self.bedCell.y < 0 || self.meetCell.y < 0 ||
           (self.jobCell.y < 0 && self.profession == 0);
}

void claimPoiTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    self.poiTimer -= ctx.deltaSeconds;
    if (self.poiTimer > 0.0f) {
        return;
    }
    self.poiTimer = kPoiScanMin + nextRandom(ctx.random) * kPoiScanSpan;

    const int cx = static_cast<int>(std::floor(self.position.x));
    const int cy = static_cast<int>(std::floor(self.position.y));
    const int cz = static_cast<int>(std::floor(self.position.z));

    glm::ivec3 bestBed{0, -1, 0};
    glm::ivec3 bestJob{0, -1, 0};
    glm::ivec3 bestBell{0, -1, 0};
    int bedDistance = 0;
    int jobDistance = 0;
    int bellDistance = 0;

    for (int dy = -kPoiHeight; dy <= kPoiHeight; ++dy) {
        for (int dz = -kPoiReach; dz <= kPoiReach; ++dz) {
            for (int dx = -kPoiReach; dx <= kPoiReach; ++dx) {
                const glm::ivec3 cell{cx + dx, cy + dy, cz + dz};
                const BlockId block = ctx.world.blockAt(cell.x, cell.y, cell.z);
                const int reach = dx * dx + dy * dy + dz * dz;

                // **A bed is two blocks and only the head end is claimable**,
                // so two villagers cannot end up owning the same bed through
                // its two halves.
                if (self.bedCell.y < 0 && isBed(block) && bedIsHead(block) &&
                    (bestBed.y < 0 || reach < bedDistance) && !alreadyClaimed(ctx, cell)) {
                    bestBed = cell;
                    bedDistance = reach;
                    continue;
                }
                if (self.jobCell.y < 0 && self.profession == 0 &&
                    professionForJobSite(block) != 0 &&
                    (bestJob.y < 0 || reach < jobDistance) && !alreadyClaimed(ctx, cell)) {
                    bestJob = cell;
                    jobDistance = reach;
                    continue;
                }
                if (self.meetCell.y < 0 && block == BlockId::Bell &&
                    (bestBell.y < 0 || reach < bellDistance)) {
                    bestBell = cell;
                    bellDistance = reach;
                }
            }
        }
    }

    if (bestBed.y >= 0) {
        self.bedCell = bestBed;
    }
    if (bestBell.y >= 0) {
        self.meetCell = bestBell;
    }
    // **The bed comes first, and that gate is Bedrock's own.** A villager with
    // nowhere to sleep will not take a job, which is what stops a lone lectern
    // in a field turning a passing villager into a librarian.
    if (bestJob.y >= 0 && self.bedCell.y >= 0 && self.profession == 0) {
        self.jobCell = bestJob;
        self.profession = professionForJobSite(
            ctx.world.blockAt(bestJob.x, bestJob.y, bestJob.z));
    }

    // A claim that no longer answers is dropped, which is the whole of "break
    // the block and the villager loses its trade". Nothing has to be repaired
    // because nothing else records it.
    if (self.bedCell.y >= 0 &&
        !isBed(ctx.world.blockAt(self.bedCell.x, self.bedCell.y, self.bedCell.z))) {
        self.bedCell = {0, -1, 0};
    }
    if (self.jobCell.y >= 0 &&
        professionForJobSite(ctx.world.blockAt(self.jobCell.x, self.jobCell.y, self.jobCell.z)) ==
            0) {
        self.jobCell = {0, -1, 0};
        self.profession = 0;
    }
}

/// Going home, and staying there.
///
/// Sits at priority 3 **above** `Avoid`, which is not an accident: the reference
/// gives `sleep` priority 3 and `avoid_mob_type` priority 4, so a sleeping
/// villager does not bolt from a zombie standing over it. That is real,
/// observable reference behaviour and the ordering is what produces it.
bool sleepStart(const BehaviourContext& ctx) {
    return ctx.species.keepsHouse && ctx.self.bedCell.y >= 0 &&
           (ctx.phase == VillagerPhase::Sleep || ctx.phase == VillagerPhase::Home);
}

void sleepTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    const glm::vec3 goal = cellCentre(self.bedCell);
    if (flatDistance(self.position, goal) <= kClaimArrival) {
        // Arrived. **Said outright**, because `walking` is sticky and the trap
        // has already been paid for by a swelling Bramble and a charging zombie.
        self.walking = false;
        self.route.clear();
        self.targetHeadPitch = 0.0f;
        return;
    }
    if (!walkTo(ctx, goal)) {
        self.walking = false;
        self.route.clear();
    }
}

/// Standing at the job block for twelve and a half seconds, then a ten-second
/// cooldown. There is nothing to restock yet, so the standing *is* the feature:
/// a librarian at its lectern all morning is what makes a profession legible.
bool workStart(const BehaviourContext& ctx) {
    return ctx.species.keepsHouse && ctx.phase == VillagerPhase::Work &&
           ctx.self.jobCell.y >= 0 && ctx.self.workTimer <= 0.0f;
}

bool workContinue(const BehaviourContext& ctx) {
    return ctx.species.keepsHouse && ctx.phase == VillagerPhase::Work && ctx.self.jobCell.y >= 0;
}

void workTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    const glm::vec3 goal = cellCentre(self.jobCell);
    if (flatDistance(self.position, goal) > kClaimArrival) {
        if (!walkTo(ctx, goal)) {
            self.walking = false;
            self.route.clear();
        }
        return;
    }

    self.walking = false;
    self.route.clear();
    self.targetHeadYaw = yawTo(self, goal);
    self.targetHeadPitch = 0.4f;

    if (self.workTimer <= 0.0f) {
        self.workTimer = kWorkSeconds;
    }
    self.workTimer -= ctx.deltaSeconds;
    if (self.workTimer <= 0.0f) {
        // Negative through the cooldown, so `canStart` reads false until it
        // climbs back to zero. One field rather than two.
        self.workTimer = -kWorkCooldown;
    }
}

/// The two hours of the afternoon a village spends round its bell.
bool mingleStart(const BehaviourContext& ctx) {
    return ctx.species.keepsHouse && ctx.phase == VillagerPhase::Gather &&
           ctx.self.meetCell.y >= 0;
}

void mingleTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;
    const glm::vec3 goal = cellCentre(self.meetCell);
    // Three blocks rather than the bell itself, or a dozen villagers try to
    // stand in the same cell and shove each other across the square.
    if (flatDistance(self.position, goal) <= 3.5f) {
        self.walking = false;
        self.route.clear();
        self.targetHeadYaw = yawTo(self, goal);
        return;
    }
    if (!walkTo(ctx, goal)) {
        self.walking = false;
        self.route.clear();
    }
}

/// The table. One list shared by every species: the differences between animals
/// are already in `kSpecies`, and the predicates read them, so a per-species
/// list would be a second place for a species to disagree with itself.
///
/// **Must stay sorted by priority** - the selector walks it in order and relies
/// on having already seen everything that outranks the row it is looking at.
constexpr Behaviour kBehaviours[] = {
    {"Panic", 1, ControlMove, panicStart, panicTick, panicContinue},
    {"HurtByTarget", 1, 0, hurtByTargetStart, hurtByTargetTick},
    // Listed **before** the player's row and claiming nothing, so both run and
    // the later writer wins: a wolf halfway through eating a sheep still turns
    // on you the moment you come into range.
    {"NearestPreyTarget", 2, 0, nearestPreyStart, nearestPreyTick},
    {"NearestAttackableTarget", 2, 0, nearestTargetStart, nearestTargetTick, nearestTargetContinue},
    // Ties with the row above on purpose, and must stay below it: it reads the
    // target slot that one writes, and within a tie the array order decides.
    {"Swell", 2, ControlMove | ControlLook, swellStart, swellTick, swellContinue},
    // Claims nothing, like the targeting rows: a pufferfish inflates while it
    // goes on swimming, and neither behaviour needs to know about the other.
    {"Puff", 2, 0, puffStart, puffTick},
    // Also a producer claiming nothing. Taking a job *is* gaining a trade, so
    // this is where every profession in the world comes from.
    {"ClaimPoi", 2, 0, claimPoiStart, claimPoiTick},
    // **Above `Avoid` on purpose, and it ties with it.** The reference gives
    // `sleep` priority 3 and `avoid_mob_type` priority 4, so a villager in bed
    // does not flee the zombie at the window. Within a tie the array order
    // decides, so this row must stay above the next one.
    {"Sleep", 3, ControlMove | ControlLook, sleepStart, sleepTick},
    {"Avoid", 3, ControlMove | ControlLook, avoidStart, avoidTick, avoidContinue},
    // Ties with `MeleeAttack` and must stay above it: an archer keeps its
    // distance where a biter closes, and both want the same two controllers.
    // `meleeAttackStart` also refuses an archer outright, so neither ordering
    // nor the tie is load-bearing on its own.
    {"RangedAttack", 4, ControlMove | ControlLook, rangedAttackStart, rangedAttackTick},
    {"MeleeAttack", 4, ControlMove | ControlLook, meleeAttackStart, meleeAttackTick},
    // Above `Wander` and claiming both controllers, so an animal with its
    // muzzle in the ground is neither steered nor looking anywhere else.
    {"EatBlock", 5, ControlMove | ControlLook, eatBlockStart, eatBlockTick, eatBlockContinue},
    // Above `Wander` and claiming the same controller, which is what keeps the
    // three mutually exclusive: a swimmer never ambles, a flier never swims and
    // a walker never cruises.
    {"SwimWander", 5, ControlMove, swimWanderStart, swimWanderTick},
    {"FlyWander", 5, ControlMove, flyWanderStart, flyWanderTick},
    // **Priority 5, not the reference's 7.** Bedrock puts `work` and `mingle`
    // at 7 and `random_stroll` at 11; ours strolls at 6, so the two schedule
    // rows have to sit above it or a villager would amble through its own
    // working day. It is the *order* that is being ported, not the number.
    {"Work", 5, ControlMove | ControlLook, workStart, workTick, workContinue},
    {"Mingle", 5, ControlMove | ControlLook, mingleStart, mingleTick},
    {"Wander", 6, ControlMove, wanderStart, wanderTick},
    {"LookAtPlayer", 7, ControlLook, lookAtPlayerStart, lookAtPlayerTick, lookAtPlayerContinue},
};

static_assert(std::size(kBehaviours) <= 32, "runningBehaviours is a 32-bit mask");

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
    std::uint32_t running = 0;

    for (std::size_t i = 0; i < std::size(kBehaviours); ++i) {
        const Behaviour& behaviour = kBehaviours[i];
        if (behaviour.flags & claimed) {
            continue;
        }

        const auto bit = static_cast<std::uint32_t>(1u << i);
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

std::uint32_t creatureTags(CreatureKind kind) {
    // One grouped switch rather than a column on all fifty-seven rows, because
    // the great majority carry no tag at all and a `case` list reads as the
    // family it is naming. **Deliberately narrow families**: `Grazer` is the
    // sheep alone, because a wolf hunts sheep and leaves cattle be, and a tag
    // that lumped the two together would buy a divergence rather than save a
    // line.
    switch (kind) {
    // The rotting humanoids are also what a golem hunts, which is why the two
    // masks are combined rather than the list being written out twice. **Both
    // users of this function test a bit rather than the whole word**, so adding
    // a tag to a species cannot change an existing answer.
    case CreatureKind::Zombie:
    case CreatureKind::Husk:
    case CreatureKind::Drowned:
    case CreatureKind::ZombieVillager:
    case CreatureKind::ZombiePrincepin:
        return CreatureTag::Undead | CreatureTag::Monster;
    // The two undead mounts are undead and are not monsters: neither hunts, so
    // a golem has nothing to answer them for.
    case CreatureKind::ZombieHorse:
        return tagMask(CreatureTag::Undead);
    case CreatureKind::Skeleton:
    case CreatureKind::Stray:
    case CreatureKind::Bogged:
    case CreatureKind::Blackbone:
        return CreatureTag::Skeletal | CreatureTag::Monster;
    case CreatureKind::SkeletonHorse:
        return tagMask(CreatureTag::Skeletal);
    case CreatureKind::Cat:
    case CreatureKind::Ocelot:
        return tagMask(CreatureTag::Feline);
    case CreatureKind::Wolf:
        return tagMask(CreatureTag::Canine);
    case CreatureKind::PolarBear:
        return tagMask(CreatureTag::Ursine);
    case CreatureKind::Sheep:
        return tagMask(CreatureTag::Grazer);
    case CreatureKind::Rabbit:
        return tagMask(CreatureTag::Critter);
    case CreatureKind::Chicken:
        return tagMask(CreatureTag::Fowl);
    case CreatureKind::Fox:
        return tagMask(CreatureTag::Vulpine);
    case CreatureKind::Cod:
    case CreatureKind::Salmon:
    case CreatureKind::TropicalFish:
    case CreatureKind::Pufferfish:
        return tagMask(CreatureTag::Fish);
    case CreatureKind::Villager:
    case CreatureKind::WanderingTrader:
        return tagMask(CreatureTag::Trader);
    // What an iron golem hunts. **The Bramble is deliberately absent**: the
    // reference excludes creepers from every one of the golem's four targeting
    // rows, and an absent tag says that once where a negative filter would say
    // it four times. The slimes are absent for the same reason the reference
    // leaves them out of `monster` - they are hostile without being people.
    case CreatureKind::Spider:
    case CreatureKind::CaveSpider:
    case CreatureKind::Silverfish:
    case CreatureKind::Voidmite:
    case CreatureKind::Witch:
    case CreatureKind::Princepin:
    case CreatureKind::PrincepinBrute:
        return tagMask(CreatureTag::Monster);
    default:
        return 0;
    }
}

const Creature* Creatures::creatureById(std::uint32_t id) const {
    if (id == 0) {
        return nullptr;
    }
    for (const Creature& creature : m_creatures) {
        if (creature.id == id) {
            return &creature;
        }
    }
    return nullptr;
}

Creature* Creatures::creatureById(std::uint32_t id) {
    return const_cast<Creature*>(std::as_const(*this).creatureById(id));
}

std::vector<glm::ivec3> Creatures::takeGrazed() {
    return std::exchange(m_grazed, {});
}

std::vector<CreatureVoiceEvent> Creatures::takeVoices() {
    return std::exchange(m_voices, {});
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
    // **These ask what a place is, not what it is called.** They used to name
    // biomes outright, which meant that growing the roster from seven regions to
    // twenty-seven would have required editing fifty rules and that forgetting
    // one would be silent. A rule now asks for a property and a new biome
    // inherits every rule it qualifies for - the reference's `has_biome_tag`.
    const auto any = [biome](auto mask) { return biomeHasAny(biome, static_cast<std::uint32_t>(mask)); };

    const bool water = any(BiomeTag::Ocean | BiomeTag::River);
    const bool land = !water;

    switch (kind) {
    case CreatureKind::Sheep:
        // Grass, warm open ground, and the scrub at a desert's edge - a land
        // biome with nothing grazing it by day reads as broken rather than
        // harsh.
        return land && any(BiomeTag::Grassland | BiomeTag::Forest | BiomeTag::Beach | BiomeTag::Sandy);
    case CreatureKind::Cow:
        // Wants real grazing, so it keeps off the sand.
        return land && any(BiomeTag::Grassland | BiomeTag::Forest);
    case CreatureKind::Pig:
        return land && any(BiomeTag::Grassland | BiomeTag::Forest | BiomeTag::Beach);
    case CreatureKind::Bramble:
        // Anywhere it can stand. Darkness is the limit that matters, not region.
        return land;
    case CreatureKind::Chicken:
        return land && any(BiomeTag::Grassland | BiomeTag::Forest | BiomeTag::Beach);
    case CreatureKind::Cat:
        return land && any(BiomeTag::Grassland | BiomeTag::Beach | BiomeTag::Sandy);
    case CreatureKind::Camel:
        return land && any(BiomeTag::Sandy | BiomeTag::Badlands);
    case CreatureKind::Horse:
        return land && any(BiomeTag::Grassland);
    case CreatureKind::Mule:
        return land && any(BiomeTag::Highland | BiomeTag::Stony);
    case CreatureKind::Llama:
        return land && any(BiomeTag::Highland | BiomeTag::Peak | BiomeTag::Stony);
    case CreatureKind::Donkey:
        return land && any(BiomeTag::Grassland | BiomeTag::Highland);
    case CreatureKind::Goat:
        return land && any(BiomeTag::Highland | BiomeTag::Peak | BiomeTag::Stony);
    case CreatureKind::Rabbit:
        return land && any(BiomeTag::Grassland | BiomeTag::Sandy | BiomeTag::Beach | BiomeTag::Snowy);
    case CreatureKind::Wolf:
        // Taiga and forest in the reference, and both finally exist.
        return land && any(BiomeTag::Forest | BiomeTag::Highland | BiomeTag::Grassland);
    case CreatureKind::Frog:
        // Swamp in the reference, and that exists now too.
        return land && any(BiomeTag::Swamp | BiomeTag::Wet | BiomeTag::Beach);
    case CreatureKind::Fox:
        return land && any(BiomeTag::Forest | BiomeTag::Snowy);
    case CreatureKind::Ocelot:
        // Jungle in the reference, and there is none - the wettest forest we
        // have is the closest thing.
        return land && any(BiomeTag::Forest | BiomeTag::Wet);
    case CreatureKind::PolarBear:
        return land && any(BiomeTag::Peak | BiomeTag::Snowy) && any(BiomeTag::Cold | BiomeTag::Frozen);
    case CreatureKind::Panda:
        // Also a jungle animal with nowhere to live yet.
        return land && any(BiomeTag::Forest | BiomeTag::Wet);
    case CreatureKind::SlimeSmall:
    case CreatureKind::SlimeMedium:
    case CreatureKind::SlimeLarge:
        return land && any(BiomeTag::Swamp | BiomeTag::Wet);
    case CreatureKind::Spider:
    case CreatureKind::CaveSpider:
        // Anywhere it can stand. Darkness is the limit that matters.
        return land;
    case CreatureKind::Zombie:
    case CreatureKind::Skeleton:
        return land;
    case CreatureKind::Villager:
        // Villages do not exist yet, so it simply lives on open grassland.
        return land && any(BiomeTag::Grassland);
    case CreatureKind::Husk:
        // The reference's desert zombie, and the only hostile that is still
        // about at midday - which is what makes the desert feel different.
        return land && any(BiomeTag::Sandy | BiomeTag::Badlands) && any(BiomeTag::Hot);
    case CreatureKind::Silverfish:
        // Stone-dwelling in the reference, where it hides inside blocks. We
        // have no infested block, so the stony regions stand in for it.
        return land && any(BiomeTag::Stony | BiomeTag::Peak | BiomeTag::Highland);
    case CreatureKind::Blackbone:
        return land;
    case CreatureKind::Stray:
        // The reference wants snowy ground, and now there is some.
        return land && any(BiomeTag::Snowy | BiomeTag::Frozen | BiomeTag::Peak);
    case CreatureKind::Bogged:
        return land && any(BiomeTag::Swamp | BiomeTag::Wet);
    case CreatureKind::ZombieVillager:
        // Wherever a zombie turns one, which is anywhere it can stand.
        return land;
    case CreatureKind::Witch:
        return land;
    case CreatureKind::WanderingTrader:
        // A traveller rather than a resident, so nowhere is its home and
        // everywhere is its route.
        return land;
    case CreatureKind::Princepin:
        // The Nether in the reference, which we do not have. Barren stony
        // ground is the nearest thing, and keeping it off the grassland is what
        // stops a daylight hostile making the whole surface unsafe.
        return land && any(BiomeTag::Stony | BiomeTag::Badlands | BiomeTag::Peak);
    // The aquatics, which want open sea rather than a river channel.
    case CreatureKind::Drowned:
    case CreatureKind::Cod:
    case CreatureKind::Salmon:
    case CreatureKind::Pufferfish:
    case CreatureKind::Squid:
    case CreatureKind::GlowSquid:
    case CreatureKind::Dolphin:
    case CreatureKind::TropicalFish:
        return any(BiomeTag::Ocean);
    case CreatureKind::Turtle:
        // The reference hatches them on beach sand, and so do we - it is the
        // one aquatic here that starts its life out of the water.
        return any(BiomeTag::Beach);
    case CreatureKind::Axolotl:
        // Lush caves in the reference, which we have none of. Ocean water is
        // the honest stand-in until underwater caves exist.
        return any(BiomeTag::Ocean);
    case CreatureKind::MushroomCow:
        // The reference confines it to a mushroom biome we do not have, so it
        // grazes where a cow does and is simply scarce.
        return land && any(BiomeTag::Grassland | BiomeTag::Forest);
    case CreatureKind::SkeletonHorse:
    case CreatureKind::ZombieHorse:
        // The reference only produces these from a lightning trap, which needs
        // weather. Until then they are rare night grazers on open ground.
        return land && any(BiomeTag::Grassland);
    case CreatureKind::TraderLlama:
        return land && any(BiomeTag::Highland | BiomeTag::Peak | BiomeTag::Stony);
    case CreatureKind::PrincepinBrute:
    case CreatureKind::ZombiePrincepin:
        // Same barren stand-in for the Nether the Princepin already uses.
        return land && any(BiomeTag::Stony | BiomeTag::Badlands | BiomeTag::Peak);
    case CreatureKind::Voidmite:
        // The reference spawns it from an enderman's teleport, which does not
        // exist here. It keeps the silverfish's stony home instead.
        return land && any(BiomeTag::Stony | BiomeTag::Peak | BiomeTag::Highland);
    case CreatureKind::MagmaCubeSmall:
    case CreatureKind::MagmaCubeMedium:
    case CreatureKind::MagmaCubeLarge:
        // Nether again, so the same barren stand-in - and being underground and
        // dark is what actually gates them.
        return land && any(BiomeTag::Stony | BiomeTag::Badlands | BiomeTag::Peak);
    case CreatureKind::Bee:
        // Where the flowers are. The reference hangs its spawn off bee nests
        // generating in trees, which is a worldgen feature we do not have, so
        // the biomes that actually grow flowers stand in for it.
        return land && any(BiomeTag::Grassland | BiomeTag::Forest | BiomeTag::Swamp);
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
                      float deltaSeconds, bool night, bool playerSneaking, float timeOfDay,
                      CreatureAttack& attack, std::vector<CreatureExplosion>& blasts) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    creature.hurtTimer = std::max(0.0f, creature.hurtTimer - deltaSeconds);
    creature.attackTimer = std::max(0.0f, creature.attackTimer - deltaSeconds);
    creature.swingTimer = std::max(0.0f, creature.swingTimer - deltaSeconds);
    creature.provokedTimer = std::max(0.0f, creature.provokedTimer - deltaSeconds);
    creature.scanTimer = std::max(0.0f, creature.scanTimer - deltaSeconds);
    creature.decisionTimer -= deltaSeconds;

    // **Caught in the sun, and it burns to death rather than being deleted.**
    // `manage` used to retire one outright the moment the sky reached twelve,
    // which is why a zombie from a spawn egg at noon simply vanished on the
    // frame it appeared - no fire, no body, no drop, and nothing to tell the
    // player what had happened. The reference sets an undead alight and lets
    // ordinary damage finish it, so it staggers, flashes, falls over and leaves
    // what it was carrying. That also means **anything can now be spawned in
    // daylight**; the ones the sun kills just take the twenty seconds it takes.
    //
    // Water and shade both stop it, which is the reference's rule and the only
    // reason a drowned is not extinct by breakfast.
    if (!night && species.nocturnal && species.burnsInDay && !creature.inWater &&
        creature.health > 0 &&
        world.skyLightAt(static_cast<int>(std::floor(creature.position.x)),
                         static_cast<int>(std::floor(creature.position.y)),
                         static_cast<int>(std::floor(creature.position.z))) >= kBurnSkyLight) {
        creature.burnTimer += deltaSeconds;
        while (creature.burnTimer >= kBurnInterval) {
            creature.burnTimer -= kBurnInterval;
            creature.health -= kBurnDamage;
            creature.hurtTimer = kHurtSeconds;
        }
    } else {
        // Stepping into shade puts it out rather than pausing it, so a zombie
        // that reaches a doorway with one point left keeps that point.
        creature.burnTimer = 0.0f;
    }

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

    // The nearest thing this species runs from, and the nearest thing it hunts.
    // Both are quadratic across the population, which is why each is guarded by
    // the species wanting it at all - most of the roster pays for neither.
    //
    // **Two fear ranges, not one.** Starting to flee and carrying on fleeing are
    // different distances in the reference, so the search records the nearest
    // regardless and the predicates decide which threshold applies.
    bool feared = false;
    bool fearedNear = false;
    float yawFromFeared = 0.0f;
    float fearedDistance = kFleeClear;
    if (species.avoids != 0 || species.avoidPlayerRange > 0.0f) {
        float nearest = kFleeClear * kFleeClear;
        const auto consider = [&](const glm::vec3& at, float startRange) {
            const glm::vec3 offset = at - creature.position;
            const float flat = offset.x * offset.x + offset.z * offset.z;
            if (flat >= nearest) {
                return;
            }
            nearest = flat;
            feared = true;
            fearedDistance = std::sqrt(flat);
            fearedNear = fearedDistance < startRange;
            yawFromFeared = std::atan2(-offset.x, -offset.z);
        };

        if (species.avoids != 0) {
            for (const Creature& other : m_creatures) {
                if (other.health <= 0 || (creatureTags(other.kind) & species.avoids) == 0) {
                    continue;
                }
                consider(other.position, species.avoidRange);
            }
        }
        if (species.avoidPlayerRange > 0.0f) {
            consider(playerFeet, species.avoidPlayerRange);
        }
    }

    // What it is fighting, resolved once so that every consumer sees the same
    // answer. A remembered quarry outranks a fresh scan, which is what stops a
    // wolf abandoning a wounded sheep for a nearer healthy one on every tick.
    const Creature* foe = nullptr;
    if (creature.targetId != 0) {
        const Creature* held = creatureById(creature.targetId);
        const float leash = species.leashRange > 0.0f ? species.leashRange : species.senseRange;
        if (held != nullptr && held->health > 0 &&
            glm::distance(held->position, creature.position) <= leash) {
            foe = held;
        } else {
            creature.targetId = 0;
        }
    }
    if (foe == nullptr && species.hunts != 0 && creature.scanTimer <= 0.0f) {
        float nearest = species.senseRange * species.senseRange;
        for (const Creature& other : m_creatures) {
            // Never its own kind, which is checked rather than encoded - a fox
            // is `Vulpine` and hunts `Critter`, and without this a tag that
            // happened to cover both would have it eating itself.
            if (other.health <= 0 || other.kind == creature.kind ||
                (creatureTags(other.kind) & species.hunts) == 0) {
                continue;
            }
            const glm::vec3 offset = other.position - creature.position;
            const float flat = offset.x * offset.x + offset.z * offset.z;
            if (flat >= nearest) {
                continue;
            }
            nearest = flat;
            foe = &other;
        }
    }

    glm::vec3 toFoe{0.0f};
    float foeDistance = 0.0f;
    float yawToFoe = 0.0f;
    if (foe != nullptr) {
        toFoe = foe->position - creature.position;
        foeDistance = std::sqrt(toFoe.x * toFoe.x + toFoe.z * toFoe.z);
        yawToFoe = std::atan2(toFoe.x, toFoe.z);
    }

    // Where to run from. The player unless something else did it, which is what
    // lets a sheep bolt from the wolf that bit it rather than from whoever
    // happens to be standing nearby.
    float yawFromThreat = yawToPlayer + kPi;
    float threatDistance = distance;
    if (creature.threatId != 0) {
        const Creature* threat = creatureById(creature.threatId);
        if (threat != nullptr && threat->health > 0) {
            const glm::vec3 offset = threat->position - creature.position;
            threatDistance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
            yawFromThreat = std::atan2(-offset.x, -offset.z);
        } else {
            // Whatever it was is gone, so there is nothing left to run from.
            creature.threatId = 0;
            creature.provokedTimer = 0.0f;
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
    creature.aiming = false;
    creature.aimWanted = 0.0f;
    creature.targetHeadYaw = creature.yaw;
    creature.targetHeadPitch = 0.0f;

    const BehaviourContext context{world,
                                   creature,
                                   species,
                                   deltaSeconds,
                                   toPlayer,
                                   distance,
                                   yawToPlayer,
                                   pitchToPlayer,
                                   huntsHere,
                                   seesPlayer,
                                   playerSneaking,
                                   // The scheduler's 0-10 s stagger, added to
                                   // the clock rather than to a countdown, so a
                                   // village changes shift raggedly instead of
                                   // in lockstep. One day is `kDaySeconds`
                                   // long, so the offset is a fraction of it.
                                   villagerPhaseAt(std::fmod(
                                       timeOfDay + creature.phaseDelay + 1.0f, 1.0f)),
                                   m_creatures,
                                   feared,
                                   fearedNear,
                                   yawFromFeared,
                                   fearedDistance,
                                   foe,
                                   toFoe,
                                   foeDistance,
                                   yawToFoe,
                                   yawFromThreat,
                                   threatDistance,
                                   attack,
                                   m_hits,
                                   m_launches,
                                   m_grazed,
                                   m_random,
                                   m_pathfinder,
                                   m_pathBudget};
    runBehaviours(context);

    // The weapon comes up and goes back down. Eased rather than switched, so a
    // bow is raised over a sixth of a second instead of appearing at the
    // shoulder, and **held across the release** - the reference starts the next
    // draw rather than lowering the bow between shots, which is why `aiming`
    // stays true through a shot and only a lost target puts it down.
    //
    // The wind-up unwinds only once nothing is holding it, the same way the
    // Bramble's fuse does: breaking line of sight is then a real defence rather
    // than a pause, and a half-drawn bow cannot be left drawn for ever.
    const float aimWanted = creature.aimWanted;
    if (creature.aim < aimWanted) {
        creature.aim = std::min(aimWanted, creature.aim + deltaSeconds * kAimRate);
    } else {
        creature.aim = std::max(aimWanted, creature.aim - deltaSeconds * kAimRate);
    }
    if (!creature.aiming) {
        creature.drawTimer = std::max(0.0f, creature.drawTimer - deltaSeconds);
        creature.heldOverride = ItemId::None;
        // Losing the target outright is the one thing that abandons a committed
        // shot, and only because there is nothing left to shoot at - it takes
        // `forgetSeconds`, so it cannot happen inside a release that lasts a
        // fraction of a second. Left set, it would fire the instant you came
        // back into view.
        creature.releaseTimer = 0.0f;
    }

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

    // An occasional noise. The reference rolls `minecraft:ambient_sound`
    // roughly once every eight seconds per mob, which at a population of forty
    // is a voice every fifth of a second somewhere in the world - so it is
    // sparse per animal and constant overall, which is exactly how a inhabited
    // world sounds.
    if (nextRandom(m_random) < kAmbientVoiceChancePerSecond * deltaSeconds) {
        m_voices.push_back({creature.kind, CreatureSound::Idle, creature.position, creature.scale});
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
    } else if (species.flies) {
        // A flier holds itself up, so `step`'s locomotion branch owns
        // `velocity.y` outright the way the swimmer's does. Applying gravity
        // here and overwriting it there is exactly the mistake the water branch
        // above documents, and it costs the same offset. Nothing that never
        // falls can accumulate a fall distance either.
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
    if (creature.health <= 0) {
        // A corpse asks for nothing. It has to come first: a stranded fish flops
        // and a slime hops on their own timers rather than on `walking`, so
        // clearing the intent is not enough to stop either of them.
    } else if (species.flies) {
            // Where it points is where it goes, in all three axes - the
            // swimmer's arrangement exactly, and for the same reason: nothing to
            // fall toward and no floor to want.
            //
            // A flier standing still **holds its height** rather than sinking,
            // which is the whole difference between flying and falling slowly,
            // and it falls out for free: with no drive there is no vertical
            // component either, so `velocity.y` lands on zero.
            const float drive = creature.walking ? speed : 0.0f;
            wish = glm::vec3{std::sin(creature.yaw), 0.0f, std::cos(creature.yaw)} *
                   (drive * std::cos(creature.pitch));
            creature.velocity.y = -std::sin(creature.pitch) * drive;
    } else if (species.swims && creature.inWater) {
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
    // A shove from being hit overrides its own intent while it lasts - but not
    // once it is dead, or the killing blow's knockback drags the body through
    // its whole fall.
    if (creature.health > 0 && creature.hurtTimer > 0.0f) {
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
        // A corpse is the one exception, at the user's call: dying means
        // stopping where you died, and a body sliding downstream while it tips
        // over reads as exactly the bug that rule exists to prevent.
        if (creature.health > 0) {
            wish += water.flow * fluid::kCurrentSpeed;
        }
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
    // The reference forces the limb swing to zero outright while dying. Ours
    // asks the existing ease for zero instead, so an animal killed mid-stride
    // winds its legs down over the same third of a second rather than locking.
    const float wanted =
        creature.health > 0 ? std::min(travelSpeed / species.runSpeed, 1.0f) : 0.0f;
    creature.limbSwingAmount +=
        (wanted - creature.limbSwingAmount) *
        (1.0f - std::pow(kLimbSwingLagPerTick, deltaSeconds / 0.05f));

    // The wing beat, for anything that flaps. Opens while airborne and folds
    // back on the ground, and the beat carries on for a moment after landing -
    // which is the whole reason this is three numbers rather than one.
    //
    // An insect wants none of that machinery: its wings never fold, so only the
    // phase moves. This is a **separate branch rather than a widened
    // `fallDrag < 1` test** on purpose - that same predicate is what grants slow
    // descent a few hundred lines above, and a bee wants the beat without the
    // drag.
    if (species.flies) {
        creature.flapSpeed = 1.0f;
        creature.flapping = 1.0f;
        creature.flap += kInsectWingPhaseRate * deltaSeconds;
    } else if (species.fallDrag < 1.0f) {
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
                                 bool night, bool playerSneaking, float timeOfDay,
                                 std::vector<CreatureExplosion>& blasts) {
    CreatureAttack attack;
    // Refilled once a frame, then spent by whichever creatures ask to search.
    // A cap here rather than a cap per creature is what bounds the cost against
    // the whole population instead of against each animal.
    m_pathBudget = kPathsPerFrame;
    for (Creature& creature : m_creatures) {
        // A corpse stops deciding things but keeps falling, so it settles on
        // whatever it was standing over instead of hanging where it died. The
        // flags are cleared rather than left alone because `walking` is sticky
        // - it survives from whichever behaviour set it last, and a dead animal
        // that keeps its last intent walks away while it topples.
        if (creature.health <= 0) {
            // The death cry fires once, on the tick the fall starts, rather
            // than every tick a corpse is lying there.
            if (creature.deathTimer <= 0.0f) {
                m_voices.push_back({creature.kind, CreatureSound::Death, creature.position,
                                    creature.scale});
            }
            creature.deathTimer += deltaSeconds;
            creature.walking = false;
            creature.running = false;
            creature.swelling = false;
            creature.target = CreatureTarget::None;
            // **It stops where it died.** The killing blow assigns knockback
            // like any other, and carrying it would slide the body along the
            // ground while it tips over - so the horizontal is dropped outright
            // rather than decayed. Downward motion is kept, because a corpse in
            // mid-air still has to reach the floor; upward is not, or the same
            // blow's lift tosses it.
            creature.velocity.x = 0.0f;
            creature.velocity.z = 0.0f;
            creature.velocity.y = std::min(creature.velocity.y, 0.0f);
            step(world, creature, deltaSeconds);
            continue;
        }
        think(world, creature, playerFeet, deltaSeconds, night, playerSneaking, timeOfDay, attack,
              blasts);
        step(world, creature, deltaSeconds);
    }
    applyHits();
    separate(world, deltaSeconds);
    return attack;
}

/// Lands every blow one creature aimed at another this tick.
///
/// Deferred out of the update loop rather than done inline, because that loop
/// holds a reference into the very vector this writes to - and because a
/// behaviour that mutated its neighbour on the spot would leave half the
/// population deciding against this tick's state and half against the last.
void Creatures::applyHits() {
    for (const CreatureHit& hit : m_hits) {
        Creature* target = creatureById(hit.targetId);
        if (target == nullptr || target->health <= 0) {
            continue;
        }
        target->health -= hit.damage;
        target->hurtTimer = kHurtSeconds;
        m_voices.push_back({target->kind, CreatureSound::Hurt, target->position, target->scale});
        target->provokedTimer = speciesInfo(target->kind).angerSeconds;
        target->threatId = hit.fromId;
        // Assigned, never accumulated - the oldest bug in this file, and a pack
        // all landing on one animal in the same tick is exactly the case that
        // exposes it. A flier owns every axis of its own velocity, so a shove
        // is skipped rather than reduced.
        if (!speciesInfo(target->kind).flies) {
            target->velocity = hit.push;
            target->onGround = false;
        }

        // A clean kill starts no war, the same exemption a player's blow gets.
        if (target->health > 0) {
            const std::size_t index =
                static_cast<std::size_t>(target - m_creatures.data());
            alertNeighbours(*target, index, hit.fromId);
        }
    }
    m_hits.clear();
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

void Creatures::alertNeighbours(const Creature& struck, std::size_t struckIndex,
                                std::uint32_t threatId) {
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
        other.threatId = threatId;
    }
}

void Creatures::add(Creature creature) {
    creature.id = m_nextId++;
    m_creatures.push_back(creature);
}

void Creatures::place(CreatureKind kind, const glm::vec3& feet, float yaw, bool charged,
                      float puff, bool playerBuilt) {
    Creature creature;
    creature.kind = kind;
    creature.health = speciesInfo(kind).health;
    creature.charged = charged;
    creature.playerBuilt = playerBuilt;
    creature.puff = puff;
    creature.variant = rollVariant(kind);
    creature.position = feet;
    creature.yaw = yaw;
    creature.targetYaw = yaw;
    creature.headYaw = yaw;
    creature.phaseDelay = random01() * kSchedulePhaseStagger;
    pickWanderGoal(creature, m_random);
    add(creature);
}

void Creatures::placeVillager(const glm::vec3& feet, float yaw, bool baby, bool nitwit) {
    Creature creature;
    creature.kind = CreatureKind::Villager;
    creature.health = speciesInfo(CreatureKind::Villager).health;
    creature.position = feet;
    creature.yaw = yaw;
    creature.targetYaw = yaw;
    creature.headYaw = yaw;
    creature.scale = baby ? kBabyScale : 1.0f;
    creature.profession = nitwit ? kNitwit : 0;
    creature.phaseDelay = random01() * kSchedulePhaseStagger;
    pickWanderGoal(creature, m_random);
    add(creature);
}

void Creatures::restore(CreatureKind kind, const glm::vec3& feet, float yaw, int health, float scale,
                        bool charged, bool playerBuilt, std::uint8_t profession) {
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
    creature.playerBuilt = playerBuilt;
    creature.profession = profession;
    creature.phaseDelay = random01() * kSchedulePhaseStagger;
    pickWanderGoal(creature, m_random);
    add(creature);
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

    // Reaches as far as creatures are allowed to live, less one chunk, so a herd
    // is never placed in the ring that `manage` is about to retire.
    const int populateRadius =
        std::max(1, static_cast<int>(m_activeRadius * kSpawnFarFraction) / kChunkSize);

    for (int cz = centreZ - populateRadius; cz <= centreZ + populateRadius; ++cz) {
        for (int cx = centreX - populateRadius; cx <= centreX + populateRadius; ++cx) {
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

            // A generated village comes with its own people. They are placed
            // here rather than by the generator for the reason everything else
            // about the population is: only the main thread owns this list, and
            // a chunk is built on a worker. The layout is a pure function of
            // the seed, so the same column always produces the same villagers
            // however you approach it — the same guarantee the herd below has.
            {
                const village::Nearby villages = village::plansNear(world.seed(), cx, cz);
                village::Resident residents[village::kMaxResidents];
                const int born = village::residentsIn(villages, cx, cz, residents,
                                                      village::kMaxResidents);
                for (int i = 0; i < born; ++i) {
                    const village::Resident& who = residents[i];
                    placeVillager({static_cast<float>(who.x) + 0.5f, static_cast<float>(who.y),
                                   static_cast<float>(who.z) + 0.5f},
                                  hashUnit(chunkHash(world.seed(), who.x, who.z)) * kTwoPi,
                                  who.baby, who.nitwit);
                }

                village::Resident guards[2];
                const int guarding = village::guardsIn(villages, cx, cz, guards, 2);
                for (int i = 0; i < guarding; ++i) {
                    const village::Resident& where = guards[i];
                    place(CreatureKind::IronGolem,
                          {static_cast<float>(where.x) + 0.5f, static_cast<float>(where.y),
                           static_cast<float>(where.z) + 0.5f},
                          0.0f);
                }
            }

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
                if (m_creatures.size() >= generatedCeiling(m_activeRadius)) {
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
                add(creature);
            }
        }
    }
}

/// What a species leaves behind when killed.
///
/// A switch beside the table rather than three more columns on all sixty rows:
/// plenty of species drop nothing, and the rest would be `None, 0, 0` repeated.
/// Anything not named here drops nothing.
///
/// **Two drops, because most of the roster has two**: an animal leaves its meat
/// and its hide, a spider its string and its eye. A third has never been needed.
struct LootRule {
    ItemId item;
    int min;
    int max;
    ItemId extra = ItemId::None;
    int extraMin = 0;
    int extraMax = 0;
};

LootRule lootFor(CreatureKind kind) {
    switch (kind) {
    case CreatureKind::Pig:
        return {ItemId::RawPorkchop, 1, 3};
    case CreatureKind::Cow:
    case CreatureKind::MushroomCow:
        return {ItemId::RawBeef, 1, 3, ItemId::Leather, 1, 2};
    case CreatureKind::Chicken:
        return {ItemId::RawChicken, 1, 1, ItemId::Feather, 1, 2};
    case CreatureKind::Sheep:
        return {ItemId::RawMutton, 1, 2, itemForBlock(BlockId::WhiteWool), 1, 1};
    case CreatureKind::Rabbit:
        return {ItemId::RawRabbit, 1, 1, ItemId::RabbitHide, 1, 1};
    case CreatureKind::Panda:
        return {itemForBlock(BlockId::Bamboo), 1, 2};
    case CreatureKind::Horse:
    case CreatureKind::Donkey:
    case CreatureKind::Mule:
    case CreatureKind::Llama:
    case CreatureKind::TraderLlama:
        return {ItemId::Leather, 1, 2};

    // The water half. **The salmon drops salmon** - it shared the cod's row
    // until the audit noticed, so the one fish with its own meat item in the
    // catalogue was unobtainable.
    case CreatureKind::Cod:
    case CreatureKind::Dolphin:
        return {ItemId::RawCod, 1, 1};
    case CreatureKind::Salmon:
        return {ItemId::RawSalmon, 1, 1};
    case CreatureKind::TropicalFish:
        return {ItemId::RawTropicalFish, 1, 1};
    case CreatureKind::Pufferfish:
        return {ItemId::RawPufferfish, 1, 1};
    case CreatureKind::Squid:
        return {ItemId::InkSac, 1, 3};
    case CreatureKind::GlowSquid:
        return {ItemId::GlowInkSac, 1, 3};
    case CreatureKind::Turtle:
        return {ItemId::Scute, 1, 1};

    // The village guard. Three to five ingots always, and nought to two poppies
    // — the reference's own two pools, and the only mob whose drop is worth
    // more than it cost to build.
    case CreatureKind::IronGolem:
        return {ItemId::IronIngot, 3, 5, itemForBlock(BlockId::Poppy), 0, 2};

    // The hostiles.
    case CreatureKind::Spider:
    case CreatureKind::CaveSpider:
        return {ItemId::String, 1, 2, ItemId::SpiderEye, 1, 1};
    case CreatureKind::Zombie:
    case CreatureKind::Husk:
    case CreatureKind::Drowned:
    case CreatureKind::ZombieVillager:
    case CreatureKind::ZombiePrincepin:
        return {ItemId::RottenFlesh, 1, 2};
    case CreatureKind::Skeleton:
    case CreatureKind::Stray:
    case CreatureKind::Bogged:
    case CreatureKind::Blackbone:
    case CreatureKind::SkeletonHorse:
        return {ItemId::Bone, 1, 2};
    case CreatureKind::Bramble:
        return {ItemId::Gunpowder, 1, 2};
    // Only the two larger sizes, which is the reference's rule: a small slime
    // is what a bigger one broke into and leaves nothing more behind.
    case CreatureKind::SlimeMedium:
    case CreatureKind::SlimeLarge:
        return {ItemId::Slimeball, 1, 2};
    case CreatureKind::MagmaCubeMedium:
    case CreatureKind::MagmaCubeLarge:
        return {ItemId::MagmaCream, 1, 1};
    case CreatureKind::Witch:
        return {ItemId::Redstone, 1, 2, ItemId::GlassBottle, 1, 1};
    default:
        return {ItemId::None, 0, 0};
    }
}

std::vector<Creatures::Loot> Creatures::takeLoot() {
    return std::exchange(m_loot, {});
}

std::vector<Creatures::Launch> Creatures::takeLaunches() {
    return std::exchange(m_launches, {});
}

void Creatures::setActiveRadius(float blocks) {
    // Floored at the old fixed radius so a low render distance cannot make the
    // world emptier than it already was.
    m_activeRadius = std::max(blocks, kBaseRadius);
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
        const bool tooFar = glm::dot(offset, offset) > m_activeRadius * m_activeRadius;
        // Not simply "out of health": the body stays while it tips over. Every
        // other reason to go is immediate, which is right - retiring at a
        // distance is the world forgetting about something, not it dying in
        // front of you.
        //
        // **Daylight is no longer one of those reasons.** A nocturnal creature
        // caught in the sun used to be deleted here; it now catches fire in
        // `think` and dies of it, which is what lets one be spawned at noon and
        // watched rather than blinking out of existence.
        const bool killed = creature.health <= 0 && creature.deathTimer >= kDeathSeconds;

        if (tooFar || killed || creature.position.y < -8.0f) {
            // Only a kill leaves anything behind. Retiring at a distance or
            // burning off is the world forgetting about an animal, and a trail
            // of meat at the edge of the render distance is not that.
            if (killed) {
                const LootRule loot = lootFor(creature.kind);
                const auto leave = [&](ItemId item, int low, int high) {
                    if (item == ItemId::None) {
                        return;
                    }
                    const int span = high - low + 1;
                    const int count = low + static_cast<int>(random01() * static_cast<float>(span));
                    m_loot.push_back({creature.position, item, std::min(count, high)});
                };
                leave(loot.item, loot.min, loot.max);
                leave(loot.extra, loot.extraMin, loot.extraMax);
                // Chainmail has no recipe in the reference either - it is
                // armour worn by the undead and taken off them. **This is its
                // only source**, so without it four catalogue entries would be
                // unreachable rather than merely rare.
                if (creature.kind == CreatureKind::Zombie ||
                    creature.kind == CreatureKind::Skeleton ||
                    creature.kind == CreatureKind::Husk ||
                    creature.kind == CreatureKind::Stray) {
                    if (random01() < 0.04f) {
                        const int piece = static_cast<int>(random01() * 4.0f);
                        m_loot.push_back({creature.position,
                                          static_cast<ItemId>(
                                              static_cast<int>(ItemId::ChainmailHelmet) +
                                              std::min(piece, 3)),
                                          1});
                    }
                }
            }
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
        add(child);
    }

    m_spawnTimer -= deltaSeconds;
    if (m_spawnTimer > 0.0f || m_creatures.size() >= capacityFor(m_activeRadius)) {
        return;
    }
    m_spawnTimer = kSpawnInterval;

    // A handful of attempts rather than a search: failing is cheap and the next
    // try comes along in a couple of seconds anyway.
    const float spawnNear = m_activeRadius * kSpawnNearFraction;
    const float spawnFar = m_activeRadius * kSpawnFarFraction;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const float angle = random01() * kTwoPi;
        const float distance = spawnNear + random01() * (spawnFar - spawnNear);
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
        add(creature);
        // One per interval. Without this the loop keeps going and a single call
        // can add eight, which overshoots the cap.
        return;
    }
}

std::size_t Creatures::findAimed(const glm::vec3& eye, const glm::vec3& forward, float reach) const {
    // Nearest first, so a creature behind another cannot be hit through it.
    float nearest = reach;
    std::size_t found = m_creatures.size();

    // Ray against a box, returning where it enters. One axis pair at a time,
    // keeping the overlap of the three intervals - the standard slab test.
    const auto entersBox = [&](const Aabb& box, float& entry) {
        float near = 0.0f;
        float far = reach;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(forward[axis]) < 1e-6f) {
                // Parallel to this pair of faces: either always between them or
                // never, and dividing would give an infinity.
                if (eye[axis] < box.min[axis] || eye[axis] > box.max[axis]) {
                    return false;
                }
                continue;
            }
            float first = (box.min[axis] - eye[axis]) / forward[axis];
            float second = (box.max[axis] - eye[axis]) / forward[axis];
            if (first > second) {
                std::swap(first, second);
            }
            near = std::max(near, first);
            far = std::min(far, second);
            if (near > far) {
                return false;
            }
        }
        entry = near;
        return true;
    };

    for (std::size_t i = 0; i < m_creatures.size(); ++i) {
        const Creature& creature = m_creatures[i];
        // A body part-way through falling over is scenery. Letting it answer
        // would have it swallow the next swing and shield whatever is behind
        // it for a second after it is already dead.
        if (creature.health <= 0) {
            continue;
        }
        const CreatureSpecies& species = speciesInfo(creature.kind);
        // **Against the creature's own box**, which is the same shape its
        // physics and its spawning use. This was a 0.6 m sphere at the body's
        // middle, and a sphere cannot describe an animal: it covered most of a
        // chicken and a fraction of a camel, so a camel's legs simply could not
        // be hit - only a band around its belly could. The padding is because
        // `halfWidth` is deliberately narrower than the model, so aiming at a
        // limb that is visibly there should still land.
        Aabb box = bodyBox(species, creature.position, creature.scale);
        box.min -= glm::vec3{kAimPadding};
        box.max += glm::vec3{kAimPadding};

        float entry = 0.0f;
        if (!entersBox(box, entry) || entry > nearest) {
            continue;
        }
        nearest = entry;
        found = i;
    }
    return found;
}

bool Creatures::aimedAt(const glm::vec3& eye, const glm::vec3& forward, float reach) const {
    return findAimed(eye, forward, reach) != m_creatures.size();
}

std::size_t Creatures::findMilkable(const glm::vec3& eye, const glm::vec3& forward,
                                    float reach) const {
    const std::size_t index = findAimed(eye, forward, reach);
    if (index == m_creatures.size()) {
        return kNoCreature;
    }
    const CreatureKind kind = m_creatures[index].kind;
    if (kind != CreatureKind::Cow && kind != CreatureKind::MushroomCow) {
        return kNoCreature;
    }
    return m_creatures[index].health > 0 ? index : kNoCreature;
}

bool Creatures::strike(const glm::vec3& eye, const glm::vec3& forward, float reach, int damage) {
    const std::size_t index = findAimed(eye, forward, reach);
    if (index == m_creatures.size()) {
        return false;
    }

    Creature& target = m_creatures[index];
    target.health -= damage;
    target.hurtTimer = kHurtSeconds;
    m_voices.push_back({target.kind, CreatureSound::Hurt, target.position, target.scale});
    // Assigned, not added. Accumulating means a second blow before the first has
    // worn off launches the creature twice as far, and a third further still.
    //
    // **A flier is not shoved.** Nothing it does is anchored to the ground, so
    // there is nothing for a knock to act against and no gravity to bring it
    // back - it simply reads as the animal being thrown across the sky. Its own
    // locomotion owns every axis of its velocity, which is exactly why the
    // assignment below would fight it rather than move it.
    if (!speciesInfo(target.kind).flies) {
        target.velocity = glm::vec3{forward.x, 0.0f, forward.z} * kStrikeKnockback +
                          glm::vec3{0.0f, kStrikeLift, 0.0f};
        target.onGround = false;
    }

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
    // Zero names the player, which is why the id counter starts at one.
    target.threatId = 0;

    // A clean kill starts no war. The reference has the same exemption, and it
    // is what stops one-shotting a lone animal turning its whole species on
    // you - the neighbours never saw anything happen.
    if (target.health > 0) {
        alertNeighbours(target, index, 0);
    }
    return true;
}

int Creatures::applyLightning(const glm::vec3& at) {
    int caught = 0;
    for (Creature& creature : m_creatures) {
        if (creature.health <= 0) {
            continue;
        }
        if (std::abs(creature.position.x - at.x) > kLightningReach ||
            std::abs(creature.position.z - at.z) > kLightningReach ||
            creature.position.y < at.y - kLightningReach ||
            creature.position.y > at.y + kLightningRise) {
            continue;
        }

        if (creature.kind == CreatureKind::Bramble && !creature.charged) {
            // Charged rather than killed. The reference does not damage what it
            // transforms, and a Bramble that dies to the bolt that charged it
            // would be a joke rather than a threat.
            creature.charged = true;
            ++caught;
            continue;
        }

        creature.health -= kLightningDamage;
        creature.hurtTimer = kHurtSeconds;
        ++caught;
    }
    return caught;
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

engine::MeshData Creatures::buildMesh(const World& world, engine::MeshData& translucent,
                                      const DrawRange& range, const SpriteMask* sprites) const {
    engine::MeshData mesh;

    // Where the next quad goes and how see-through it is. Everything defaults
    // to the opaque mesh at full alpha; a slime's shell flips both for the one
    // box that needs it and flips them straight back.
    engine::MeshData* target = &mesh;
    float quadAlpha = 1.0f;

    for (const Creature& creature : m_creatures) {
        if (!range.contains(creature.position)) {
            continue;
        }
        const CreatureSpecies& species = speciesInfo(creature.kind);

        // Where the body is *drawn*, which trails the collision box up a step
        // so a climb reads as one rather than as a teleport. Only geometry may
        // use it; everything that reasons about the world wants the real one.
        //
        // A body tipping over is also raised as it goes, because the fall
        // pivots at the feet: at a quarter turn everything that was a body
        // half-width to one side is now that far *below* the floor. The lift is
        // `halfWidth`, which is the collision box and deliberately narrower
        // than the model - so a broad animal still settles slightly into the
        // ground rather than floating above it, which is the right way round.
        const float fallAngle = deathTipAngle(creature);
        const glm::vec3 renderPosition =
            creature.position -
            glm::vec3{0.0f, creature.stepSmooth - species.halfWidth * creature.scale * std::sin(fallAngle),
                      0.0f};

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
        // Struck creatures are tinted by the shader, not lit by it. **A corpse
        // keeps the tint for the whole fall**: fading back to its own colours
        // half way over reads as the animal recovering, which is precisely the
        // wrong thing to say while it topples.
        //
        // A lit fuse outranks that and strobes: the reference alternates every
        // tenth of the countdown, at a constant rate that does *not* speed up -
        // only the bulge accelerates, which is what makes the last half-second
        // read as sudden rather than as a build-up.
        float skinLayer = (creature.hurtTimer > 0.0f || creature.health <= 0) ? kSkinHurtLayer
                                                                             : kSkinTextureLayer;
        if (species.explodePower > 0.0f && creature.fuseTimer > 0.0f) {
            const float s = creature.fuseTimer / species.fuseSeconds;
            if (static_cast<int>(std::round(s * 10.0f)) % 2 != 0) {
                skinLayer = kSkinFlashLayer;
            }
        }

        const float sinYaw = std::sin(creature.yaw);
        const float cosYaw = std::cos(creature.yaw);

        // **The death fall, and it is one rule for all fifty-six species.**
        // The whole model rolls a quarter turn about its own forward axis,
        // pivoting where it stands, so it tips onto its side rather than
        // spinning or sinking. Every box, limb and head group in this function
        // is placed along the three local axes below, so rotating *those* is
        // the entire animation - there is no per-species table, and a model
        // added tomorrow gets it without knowing.
        //
        // Applied to the forward axis too, where it is a no-op, because a
        // rotation written once for every axis is visibly rigid.
        const glm::vec3 fallAxis{sinYaw, 0.0f, cosYaw};
        const float sinFall = std::sin(fallAngle);
        const float cosFall = std::cos(fallAngle);
        const auto tipped = [&](const glm::vec3& v) {
            if (fallAngle <= 0.0f) {
                return v;
            }
            return v * cosFall + glm::cross(fallAxis, v) * sinFall +
                   fallAxis * (glm::dot(fallAxis, v) * (1.0f - cosFall));
        };

        // Local axes: forward is where it faces, side is to its left.
        const glm::vec3 bodyForward = tipped({sinYaw, 0.0f, cosYaw});
        const glm::vec3 bodySide = tipped({cosYaw, 0.0f, -sinYaw});

        const float sinHead = std::sin(creature.headYaw);
        const float cosHead = std::cos(creature.headYaw);
        // The **model's** up, which is world up right until the fall tips it.
        const glm::vec3 modelUp = tipped({0.0f, 1.0f, 0.0f});
        const glm::vec3 headLevel = tipped({sinHead, 0.0f, cosHead});
        const glm::vec3 headSide = tipped({cosHead, 0.0f, -sinHead});
        const float sinTilt = std::sin(creature.headPitch);
        const float cosTilt = std::cos(creature.headPitch);
        const glm::vec3 headForward = headLevel * cosTilt - modelUp * sinTilt;
        const glm::vec3 headUp = headLevel * sinTilt + modelUp * cosTilt;

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
        glm::vec3 upAxis = modelUp;
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
                          modelUp * (neckUp * modelScale * swellTall);
        };
        const auto endHead = [&]() {
            forward = bodyForward;
            side = bodySide;
            upAxis = modelUp;
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
                                                              engine::packVertexColor(sky, block, shade, quadAlpha),
                                                              {uvs[i].x, uvs[i].y},
                                                              skinLayer,
                                                              engine::kVertexSurfaceDefault});
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
            // The box's own three axes after both rotations, and the one place
            // they are worked out: `uprightBox` measures its extents along
            // them, `legBox` hangs a limb from the height axis and a held item
            // rides on all three. Any two of those disagreeing would put
            // something somewhere its own box is not.
            struct BoxAxes {
                glm::vec3 depth;
                glm::vec3 side;
                glm::vec3 height;
            };
            const auto boxAxes = [&](float pitch, float roll) {
                const float cosPitch = std::cos(pitch);
                const float sinPitch = std::sin(pitch);
                const glm::vec3 pitched = forward * sinPitch + upAxis * cosPitch;
                return BoxAxes{forward * cosPitch - upAxis * sinPitch,
                               side * std::cos(roll) + pitched * std::sin(roll),
                               pitched * std::cos(roll) - side * std::sin(roll)};
            };
            const auto boxHeightAxis = [&](float pitch, float roll) {
                return boxAxes(pitch, roll).height;
            };

            const auto uprightBox = [&](const glm::vec3& centre, float netW, float netH, float netD,
                                        float u, float v, float vBase, float grow, float pitch = 0.0f,
                                        float growSide = 0.0f, float roll = 0.0f,
                                        bool mirror = false) {
                const BoxAxes axes = boxAxes(pitch, roll);
                const glm::vec3 depthAxis = axes.depth;
                const glm::vec3 sideAxis = axes.side;
                const glm::vec3 heightAxis = axes.height;

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

            // Where a limb's far end came to rest, and the frame it got there
            // on. `up` runs from that end back toward the joint, so a hand's
            // three axes are the arm's own.
            struct LimbEnd {
                glm::vec3 tip;
                glm::vec3 up;
                glm::vec3 forward;
                glm::vec3 side;
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
            //
            // **It returns where the far end of the limb ended up**, because
            // that is where a hand is and there is nowhere else to get one.
            // Working a fist out from the shoulder and the pitch a second time
            // is this repo's oldest bug shape - a value derived somewhere other
            // than the one place that owns it - and it would show up as a bow
            // floating a few centimetres off an arm at only some angles.
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
                const BoxAxes axes = boxAxes(pitch, roll);
                uprightBox(pivot - axes.height * hang, netW, netH, netD, u, v, vBase,
                           grow, pitch, 0.0f, roll, mirror);
                return LimbEnd{pivot - axes.height * (hang + halfHeight), axes.height, axes.depth,
                               axes.side};
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

            // An item in a hand: the picture extruded and walled in, through
            // the same `appendSpriteModel` a dropped item and a thrown one
            // already go through. **Third caller rather than a fourth path** -
            // a bow in a fist and a bow on the floor cannot then disagree.
            //
            // **The hand frame is composed, not authored.** The reference moves
            // to the arm's pivot, turns a quarter about X and a half about Y,
            // and only then steps to the fist. Working that composition through
            // its own model space - x the mob's left, y *down*, z backward -
            // leaves three axes: the mob's right, the arm's own forward face,
            // and one running back up the arm toward the shoulder. Those three
            // are what the item's rotation and translation are expressed in,
            // and getting them from `legBox` rather than re-deriving them is
            // what keeps a held item welded to the arm at every angle.
            //
            // The step to the fist is `legBox`'s own far end rather than the
            // reference's flat -10/16, because that number is only true of a
            // 4-wide arm: our overhang rule puts a skeleton's 2-wide arm on a
            // pivot a texel lower, and a constant would push a bow through the
            // hand on one of the two.
            const auto heldItem = [&](ItemId item, const LimbEnd& hand, bool leftHand) {
                // **A behaviour's own choice outranks the species' standing
                // weapon.** That is the whole of the witch's bottle: her row
                // carries nothing, because the reference's witch has empty
                // hands until she reaches for one, and the bottle exists only
                // for the two seconds she is winding up to throw it.
                if (creature.heldOverride != ItemId::None) {
                    item = creature.heldOverride;
                }
                // No mask means the caller has no item pictures to hand, which
                // is a state rather than a fault: everything else about the
                // creature is still drawn.
                if (item == ItemId::None || sprites == nullptr) {
                    return;
                }
                // A block in a hand would need the miniature-cube path a
                // dropped block takes, not a sprite. Nothing on the roster
                // holds one, and drawing it as a flat picture would be worse
                // than drawing nothing.
                const int layer = itemTextureLayer(item);
                if (layer < 0) {
                    return;
                }

                const HeldTransform& held = heldTransform(item);
                const float mirror = leftHand ? -1.0f : 1.0f;

                // `side` is the creature's *left*, so its right is the negative.
                const glm::vec3 gripRight = -hand.side;
                const glm::vec3 gripUp = hand.forward;
                const glm::vec3 gripBack = hand.up;
                const auto intoWorld = [&](const glm::vec3& v) {
                    return gripRight * v.x + gripUp * v.y + gripBack * v.z;
                };

                // The reference's own offset from the arm's end to the grip: a
                // texel **outboard** and two forward. Outboard rather than "to
                // the right", which is what makes one expression serve both
                // hands instead of two numbers free to drift apart.
                const glm::vec3 fist =
                    hand.tip + (gripRight * mirror + gripUp * 2.0f) * (kTexel * modelScale);
                const glm::vec3 centre =
                    fist + intoWorld({held.translation[0] * mirror, held.translation[1],
                                      held.translation[2]}) *
                               (kTexel * modelScale);

                constexpr float kDegrees = kPi / 180.0f;
                const float rx = held.rotation[0] * kDegrees;
                const float ry = held.rotation[1] * kDegrees * mirror;
                const float rz = held.rotation[2] * kDegrees * mirror;
                // Composed X then Y then Z, which is the reference's own order
                // and the whole of what makes a bow stand upright when the arm
                // comes level. Written out rather than remembered: a rotation
                // recalled from memory comes out inverted about a third of the
                // time, and this one has no symmetry to expose it.
                const auto turned = [&](glm::vec3 v) {
                    v = {v.x * std::cos(rz) - v.y * std::sin(rz),
                         v.x * std::sin(rz) + v.y * std::cos(rz), v.z};
                    v = {v.x * std::cos(ry) + v.z * std::sin(ry), v.y,
                         -v.x * std::sin(ry) + v.z * std::cos(ry)};
                    v = {v.x, v.y * std::cos(rx) - v.z * std::sin(rx),
                         v.y * std::sin(rx) + v.z * std::cos(rx)};
                    return intoWorld(v);
                };

                // `appendSpriteModel` wants half-extents of equal length, and
                // the item is one block cubed in its own space before its
                // display scale.
                const float half = 0.5f * held.scale * modelScale;
                appendSpriteModel(mesh, *sprites, layer, centre, turned({1.0f, 0.0f, 0.0f}) * half,
                                  turned({0.0f, 1.0f, 0.0f}) * half,
                                  turned({0.0f, 0.0f, 1.0f}) * half, sky, block);
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

            if (creature.kind == CreatureKind::Bee) {
                // Measured off the reference's 64x64 net rather than recalled.
                // Alpha rows 0-9 give a band 14 wide starting at x=10, which is
                // `2w` over `d` rows; rows 10-16 give sides 34 wide from x=0,
                // which is `2(w+d)` over `h`. That solves to **7x7x10 at net
                // origin (0,0)** and no second box.
                //
                // The wings are **not a box**. They are two wing-shaped cutouts
                // at (8,18) and (15,24), each 7x6 and mirror images of one
                // another, so they are drawn as flat double-sided quads with
                // their own rects. `finBox` was the obvious tool and is wrong
                // here: its second rect is derived from the first, would land on
                // empty sheet, and would leave each wing invisible from below.
                //
                // Named divergence: no legs, antennae or stinger. Every one of
                // them is one or two texels on a body half a block long.
                const float sinTip = std::sin(creature.pitch);
                const float cosTip = std::cos(creature.pitch);
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = kBeeBodyUp;
                frameOrigin = renderPosition + modelUp * (kBeeBodyUp * modelScale * swellTall);

                const float beeSkin =
                    creature.target != CreatureTarget::None ? kBeeAngrySkin : kBeeSkin;
                uprightBox(place(0.0f, kBeeBodyUp, 0.0f), 7.0f, 7.0f, 10.0f, 0.0f, 0.0f, beeSkin,
                           0.0f);

                // The beat. `flap` advances at `kInsectWingPhaseRate`, and the
                // swing is deliberately shallow - at that rate anything wider
                // reads as a bird flapping rather than an insect blurring.
                const float wingRoll = std::sin(creature.flap) * kInsectWingSwing;
                const auto beeWing = [&](float sideSign, float rectX, float rectY) {
                    const float roll = wingRoll * sideSign;
                    const glm::vec3 spanAxis =
                        (side * std::cos(roll) + upAxis * std::sin(roll)) * sideSign;
                    const glm::vec3 root = place(-0.0625f, 0.4375f, sideSign * 0.0625f);
                    const glm::vec3 span = spanAxis * (7.0f * kTexel * modelScale * swellWide);
                    const glm::vec3 chord = forward * (3.0f * kTexel * modelScale * swellWide);
                    skinQuad(root - chord, root + span - chord, root + span + chord, root + chord,
                             0.95f, beeSkin, rectX, rectY, 7.0f, 6.0f);
                };
                beeWing(1.0f, 8.0f, 18.0f);
                beeWing(-1.0f, 15.0f, 24.0f);
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
            const auto biped = [&](float skin, float limb, bool armsOut, float inflate = 0.0f,
                                   ItemId held = ItemId::None) {
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
                //
                // **The swinging arm is the mob's right**, because that is the
                // main hand and it is the hand an item is drawn in. It was the
                // left until held items arrived, which nothing could see while
                // both hands were empty and which would have read as a
                // Blackbone swinging one arm and carrying its sword in the
                // other.
                LimbEnd mainHand{};
                float mainPitch = 0.0f;
                float mainRoll = 0.0f;
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
                    legBox(place(0.0f, kBipedArmRestUp, shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow, -1.5708f + swayPitch + zombieChop, swayRoll);
                    mainPitch = -1.5708f - swayPitch + zombieChop;
                    mainRoll = -swayRoll;
                } else {
                    // **Aiming a bow.** `animation.humanoid.bow_and_arrow` is
                    // two bones and nothing else: both arms straight out at -90
                    // degrees, differing **only in yaw** - the reference's
                    // +28.65 on the left against -5.73 on the right, which
                    // brings the string hand in across the chest while the bow
                    // hand stays pointed at you. There is no draw-back and no
                    // second pitch, and the three drawn-bow pictures live on
                    // the *item*, not on the arms.
                    //
                    // **The pose replaces the walk rather than adding to it.**
                    // The reference's channels end in `- this`, which overrides
                    // whatever the walk cycle put there, so an aiming
                    // skeleton's arms stop swinging. Blending toward it does
                    // exactly that at full aim - and at zero every expression
                    // below is precisely the walking pose it replaces, which is
                    // what makes this safe for the fifty-odd species that will
                    // never hold anything.
                    //
                    // The sway is carried *through* the pose rather than
                    // blended away, because the reference's aiming entry has
                    // its own jitter and it is the same jitter: 0.05 radians of
                    // pitch out of phase between the arms and a mirrored 0.05
                    // of roll, which is `swayPitch` and `swayRoll` to the
                    // constant.
                    const auto blend = [&](float rest, float aimed) {
                        return rest + (aimed - rest) * creature.aim;
                    };
                    const float aimPitch = kArcherArmPitch + creature.headPitch;

                    legBox(place(0.0f, kBipedArmRestUp, shoulder), limb, 12.0f, limb,
                           40.0f, 16.0f, skin, grow,
                           blend(-swing + swayPitch, aimPitch + swayPitch),
                           blend(swayRoll, kArcherStringArmRoll + swayRoll));
                    mainPitch = blend(swing - swayPitch - humanoidRaise, aimPitch - swayPitch);
                    mainRoll = blend(-swayRoll, kArcherBowArmRoll - swayRoll);
                }
                mainHand = legBox(place(0.0f, kBipedArmRestUp, -shoulder), limb, 12.0f, limb,
                                  40.0f, 16.0f, skin, grow, mainPitch, mainRoll);
                // **The anchor is `bipedHandPoint`, not the limb's own far
                // end.** The two agree - it reproduces `legBox`'s pivot rule
                // exactly - but the arrow leaves from that same call, and a
                // weapon and the shot it fires derived separately would be the
                // first entry in `CLAUDE.md`'s box of bug shapes. The limb
                // still supplies the three axes, because only it has them.
                const ModelPoint grip = bipedHandPoint(limb, mainPitch, mainRoll, true);
                mainHand.tip = place(grip.alongForward, grip.up, grip.alongSide);
                heldItem(held, mainHand, false);
            };

            if (creature.kind == CreatureKind::Zombie) {
                biped(kZombieSkin, 4.0f, true, 0.0f, species.heldMainHand);
                continue;
            }

            if (creature.kind == CreatureKind::Husk) {
                biped(kHuskSkin, 4.0f, true, 0.0f, species.heldMainHand);
                continue;
            }

            if (creature.kind == CreatureKind::Drowned) {
                // The zombie rig twice over: the body, then the reference's
                // `drowned_outer_layer` as a second shell a quarter of a texel
                // larger. That layer is an ordinary alpha-tested cutout like
                // every skin here, so it needs no new rendering path - the same
                // arrangement the sheep's fleece has used since M19.
                //
                // **Only the first call carries the held item**, or the shell
                // would build a second one inside the same fist.
                biped(kDrownedSkin, 4.0f, true, 0.0f, species.heldMainHand);
                biped(kDrownedOuterSkin, 4.0f, true, 0.25f * kTexel);
                continue;
            }

            if (creature.kind == CreatureKind::Skeleton) {
                biped(kSkeletonSkin, 2.0f, false, 0.0f, species.heldMainHand);
                continue;
            }

            if (creature.kind == CreatureKind::Blackbone) {
                biped(kBlackboneSkin, 2.0f, false, 0.0f, species.heldMainHand);
                continue;
            }

            if (creature.kind == CreatureKind::Stray) {
                biped(kStraySkin, 2.0f, false, 0.0f, species.heldMainHand);
                continue;
            }

            if (creature.kind == CreatureKind::Bogged) {
                biped(kBoggedSkin, 2.0f, false, 0.0f, species.heldMainHand);
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
            const auto villagerRig = [&](float skin, bool armsOut, float armRaise = 0.0f) {
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
                // separately. In the reference both upper arms and the crossed
                // forearms sit in a single group pitched 0.75 rad as a unit,
                // and the forearm block spans the gap between the two upper
                // arms so the whole thing is one continuous sleeve from
                // shoulder to shoulder.
                //
                // So all three share the pitch, and their centres are the
                // reference's local offsets carried through that rotation
                // rather than guessed. Tilting the arms while leaving the
                // forearms level - which is how this first shipped - is exactly
                // what left the hands floating unattached.
                //
                // **`armRaise` swings the whole group and cannot break it.**
                // The one way this assembly is allowed to move is rigidly: the
                // same extra pitch on every part *and* every centre carried
                // round one pivot, so the three keep exactly the relative
                // geometry the user closed the book on. The pivot is not
                // invented either - solving the two stated centres above back
                // for the point they turn about gives `kVillagerArmPivotUp`
                // from both, agreeing to within the sixteenth of a texel the
                // forearm block is deliberately nudged by. At an `armRaise` of
                // zero every expression below reduces to the numbers it
                // replaced, so a villager and a trader are untouched.
                constexpr float kFold = kVillagerFold;
                const float turn = -armRaise;
                const float cosTurn = std::cos(turn);
                const float sinTurn = std::sin(turn);
                const auto folded = [&](float alongForward, float up, float alongSide) {
                    const float dy = up - kVillagerArmPivotUp;
                    return place(alongForward * cosTurn - dy * sinTurn,
                                 kVillagerArmPivotUp + alongForward * sinTurn + dy * cosTurn,
                                 alongSide);
                };

                uprightBox(folded(0.1477f, 1.221f, 0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold + armRaise);
                uprightBox(folded(0.1477f, 1.221f, -0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold + armRaise);
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
                uprightBox(folded(0.2332f, 1.1381f, 0.0f), 8.0f, 4.0f, 4.0f,
                           40.0f, 38.0f, skin, 0.005f, kFold + armRaise, 0.08f);

                // The bottle, held where the hands are. `villagerHandPoint` is
                // the same function the thrown potion leaves from, turned by
                // the same rigid swing, so what is in her hands and what
                // arrives at your feet start from one place.
                const ModelPoint grip = villagerHandPoint();
                const float pitch = kFold + armRaise;
                const glm::vec3 gripUp = boxHeightAxis(pitch, 0.0f);
                const LimbEnd hands{folded(grip.alongForward, grip.up, 0.0f), gripUp,
                                    forward * std::cos(pitch) - upAxis * std::sin(pitch), side};
                heldItem(ItemId::None, hands, false);
            };

            if (creature.kind == CreatureKind::Villager) {
                villagerRig(villagerSkinRow(creature.profession), false);
                continue;
            }

            if (creature.kind == CreatureKind::ZombieVillager) {
                villagerRig(kZombieVillagerSkin, true);
                continue;
            }

            if (creature.kind == CreatureKind::IronGolem) {
                // Its own rig, and it had to be: it shares nothing with the
                // biped helper. Eight bones at eight distinct net origins, on a
                // 128x128 sheet of its own — the largest single allocation the
                // creature sheet has.
                //
                // Every number is `iron_golem.geo.json` verbatim, converted the
                // usual way: Bedrock's origin is a cube's minimum corner with Y
                // up from the feet and forward at -Z, so a centre is
                // `origin + size/2` and `alongForward` is that centre's Z
                // negated.
                beginHead(0.125f, 1.9375f);
                uprightBox(place(0.21875f, 2.375f, 0.0f), 8.0f, 10.0f, 8.0f,
                           0.0f, 0.0f, kIronGolemSkin, 0.006f);
                // The nose is a 2x4x2 block standing proud of the face, and it
                // is the whole of the golem's expression.
                uprightBox(place(0.53125f, 2.125f, 0.0f), 2.0f, 4.0f, 2.0f,
                           24.0f, 0.0f, kIronGolemSkin, 0.0f);
                endHead();

                uprightBox(place(0.03125f, 1.6875f, 0.0f), 18.0f, 12.0f, 11.0f,
                           0.0f, 40.0f, kIronGolemSkin, 0.0f);
                // **The waist bridges a real gap.** The legs stop at 16 texels
                // and the chest starts at 21, so leaving this out puts a
                // five-texel hole straight through the middle of the animal.
                // The reference inflates it by half a texel, which is exactly
                // what closes the join at both ends.
                uprightBox(place(0.0f, 1.15625f, 0.0f), 9.0f, 5.0f, 6.0f,
                           0.0f, 70.0f, kIronGolemSkin, 0.5f * kTexel);

                // The two thirty-texel arms. **Named divergence:** the
                // reference hangs both from the body's own centreline at
                // (0, 31), so the fists sweep from ankle height up past the
                // head. `legBox` derives a joint from the top of the box it is
                // given, which puts the hinge at the shoulder instead — the
                // rest pose is identical and the arc of a blow is smaller.
                //
                // Both arms swing **together and in the same sense**, which is
                // the golem's signature: `animation.iron_golem.attack` gives
                // them one expression with no sign flip, so it is a two-handed
                // overhead heave rather than a punch. The walk keeps them in
                // opposition, at a third of the legs' amplitude, because the
                // reference's `move` animation does.
                const float golemHeave = humanoidRaise * 1.6f;
                legBox(place(0.0f, 1.15625f, -0.6875f), 4.0f, 30.0f, 6.0f,
                       60.0f, 21.0f, kIronGolemSkin, 0.0f, -swing * 0.34f - golemHeave);
                legBox(place(0.0f, 1.15625f, 0.6875f), 4.0f, 30.0f, 6.0f,
                       60.0f, 58.0f, kIronGolemSkin, 0.0f, swing * 0.34f - golemHeave);

                legBox(place(0.03125f, 0.5f, -0.28125f), 6.0f, 16.0f, 5.0f,
                       37.0f, 0.0f, kIronGolemSkin, 0.0f, swing);
                legBox(place(0.03125f, 0.5f, 0.28125f), 6.0f, 16.0f, 5.0f,
                       60.0f, 0.0f, kIronGolemSkin, 0.0f, -swing);
                continue;
            }

            if (creature.kind == CreatureKind::Witch) {
                // The arms come up as she winds up, and that is the whole tell
                // that a bottle is coming - the folded assembly is closed by
                // the user's own instruction, so it swings rigidly or not at
                // all. `aim` is the eased 0 to 1 the archer's bow rides on, so
                // the two wind-ups are the same clock.
                villagerRig(kWitchSkin, false, creature.aim * kWitchArmRaise);
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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + modelUp * (bodyUp * modelScale * swellTall);

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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + modelUp * (bodyUp * modelScale * swellTall);

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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = kBellUp;
                frameOrigin = renderPosition + modelUp * (kBellUp * modelScale * swellTall);

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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = kSpineUp;
                frameOrigin = renderPosition + modelUp * (kSpineUp * modelScale * swellTall);

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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = kBodyUp;
                frameOrigin =
                    renderPosition + modelUp * ((kBodyUp + kLift) * modelScale * swellTall);

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
                forward = bodyForward * cosTip - modelUp * sinTip;
                upAxis = bodyForward * sinTip + modelUp * cosTip;
                pivotUp = bodyUp;
                frameOrigin = renderPosition + modelUp * (bodyUp * modelScale * swellTall);

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
