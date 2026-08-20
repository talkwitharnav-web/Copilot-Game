#include "world/Creature.hpp"

#include "item/SpriteModel.hpp"
#include "world/Block.hpp"
// For `isCampfire`, which the occupied-cell scan in `think` reads. Its own
// comment says it belongs in `Block.hpp` beside `isFurnace` and is parked in
// `Campfire.hpp` only because that file had another owner; when it moves, this
// include goes and nothing else here changes.
#include "world/Campfire.hpp"
#include "world/Collision.hpp"
#include "world/Explosion.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/Survival.hpp"
#include "world/Tick.hpp"
#include "world/Village.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace game {
namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPi = 3.14159265f;

/// The two rates a standing biped's idle sway runs at, in radians a second -
/// the reference's own 0.09 and 0.067 radians a *tick*, converted. Deliberately
/// incommensurate, so the motion never visibly repeats.
constexpr float kIdleSwayRollRate = 1.8f;
constexpr float kIdleSwayPitchRate = 1.34f;
/// A full period of the slowest thing `age` drives, in seconds.
///
/// **`add` scatters every creature's `age` across this**, which is the whole
/// reason the field is per creature rather than a world clock. Nothing seeded
/// it: every creature started at zero, so a herd placed in one call swayed in
/// exact phase - and a reloaded world, where the entire population is placed in
/// one call, swayed in phase from end to end. The squid's `jetPhase` had the
/// right idea and is now seeded beside it.
constexpr float kAgePhaseSpan = kTwoPi / kIdleSwayPitchRate;
static_assert(kIdleSwayPitchRate < kIdleSwayRollRate,
              "the span has to cover the slower rate, or the scatter is not a whole cycle");

/// **The player's pair, read rather than restated.** Bedrock puts players and
/// mobs on one row of its constant table - `RESEARCH.md` §1.3, gravity
/// `0.08 b/t²` and terminal `3.92 b/t`, which convert to 32 m/s² and 78.4 m/s
/// at 20 Hz - so a creature falling at its own 26 was a published constant with
/// two owners and a 19% divergence, and the animals visibly fell more slowly
/// than the player did.
///
/// **The proof it was an error rather than a feel choice is `species.fallDrag`
/// below.** That line re-solves the *reference's* own per-tick 0.6 against
/// whatever gravity is in scope, and only the reference's gravity reproduces
/// the reference's own answer: 32 x 0.05 x 0.6 / 0.4 = 2.4 m/s, where 26 gave
/// 1.95 and the comment then attributed 1.95 to the reference.
///
/// What had to travel with it: every arc solved against gravity. A jump is
/// `sqrt(2 g h)` off `species.jumpHeight`, so its apex is unchanged and only
/// the airtime shortens to the reference's. A hop's `hopLaunch` is a *speed*,
/// so each of the three distinct launch speeds in the table was re-derived by
/// `v' = v sqrt(32/26)` to hold its arc height exactly where it was - which is
/// how a slime's 7.21 came out at 8.00, exactly one block under the new pair.
constexpr float kGravity = player_constants::kGravity;
constexpr float kTerminalVelocity = player_constants::kTerminalVelocity;

/// A single frame may not advance a creature further than this. **The same 0.05
/// the player and the item drops clamp to** (`ItemEntity.cpp` names all three);
/// they resolve against the same world with the same one-block-deep assumption,
/// so a frame too long for one is too long for all of them.
///
/// Owned by `update`, which clamps once before anything reads it, so the
/// per-tick timers in `think` and the per-tick timers in `step` cannot advance
/// at different rates during a hitch - they used to, `think` taking the raw
/// delta and `step` the clamped one.
///
/// **Seconds, and deliberately *not* derived from `tick::kSeconds` even though
/// it reads the same today.** This is a collision guard - how far a body may
/// travel before the sweep stops being able to see a floor - and the tick rate
/// is a simulation rate; they are equal by coincidence and would have to be
/// re-decided separately, not silently rescaled together. The assert below
/// exists so that decision has to be made out loud rather than by nobody.
constexpr float kMaxDeltaSeconds = 0.05f;
static_assert(kMaxDeltaSeconds == tick::kSeconds && kMaxDeltaSeconds == 0.05f,
              "the frame clamp happens to equal one tick; if the tick rate moves, decide "
              "what the sweep clamp should be rather than letting it follow");

/// Longest slice of a vertical move that may be tested in one go.
///
/// Consecutive test boxes have to overlap or a floor thinner than the gap
/// between two of them falls straight between them, so this may never exceed
/// the shortest body in the roster. The `static_assert` that proves it lives
/// under `kSpecies`, which is the only place the table is in scope.
constexpr float kMaxSweepStep = 0.15f;

constexpr float kTurnRate = 4.0f;
/// Drops deeper than this are treated as a wall. Without it, wandering animals
/// walk off every cliff in the world and the population drains downhill.
constexpr float kMaxDropHeight = 3.0f;

/// The radius every population number below is quoted against, in blocks.
/// Nothing is fixed at it any more — it is the *reference* point that the
/// runtime radius scales from, so the tuning that was done at 90 m still means
/// what it meant.
///
/// **This is ours, and it stands in for a despawn model Bedrock states in three
/// numbers rather than one.** Checked against minecraft.wiki's *Mob spawning*
/// on 2026-08-19: Bedrock despawns a mob outright past **128 blocks** on
/// simulation distance 6 and above, past **44** on simulation distance 4 (and
/// fish past 40), and rolls a **1-in-800 chance per tick** for anything past
/// **32 blocks** that has not been hurt for thirty seconds. We have no
/// simulation distance and no per-creature damage clock, so `manage` retires on
/// one radius instead - which sits inside Bedrock's band and is scaled by the
/// render distance in `setActiveRadius`. **It retires only what it can afford
/// to lose**, since 2026-08-19: a villager, anything `playerBuilt` and an iron
/// golem sleep past this radius instead (`Creature::dormant`). **Do not
/// "correct" this to 128**: the
/// number it would have to match depends on a setting this engine does not
/// have, and 90 is the figure every population constant below was tuned at.
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
///
/// **The numbers now live in `collision::`**, beside `overlapsSolid`, which is
/// the only question they ask. They were written out here *and* in `Player.cpp`
/// with identical values and structurally identical loops - a published constant
/// with two owners, which is one of the two shapes this file has paid for most.
///
/// **This reach is a convenience, not the answer to being buried.** Anything
/// deeper than it simply stays inside the terrain, and before the suffocation
/// clock below existed that meant *stuck alive, permanently* - every move it
/// tried overlapped something and nothing ever subtracted health. Widening the
/// reach would only have moved the threshold. Suffocation is the reference's own
/// answer and it has no threshold.
///
/// **On the step size, since three agents have now looked at this pair and the
/// next reader should not have to re-derive it.** The candidates are
/// `y + k * step`, phased on the *body's own* `y` rather than on the world, so a
/// uniform grid of spacing `s` is guaranteed to find a valid landing only when
/// `s <= slack`, where `slack` is the pocket's height less the body's. Measured
/// across all 58 rows against each species' own minimal pocket: **37 are safe at
/// today's 0.25; six more - chicken, wolf, squid, glow squid and the two llamas,
/// slack 0.13 to 0.20 - would become safe at 0.125; and fifteen stay
/// phase-dependent whatever the step**, because the villager family's slack is
/// 0.05 and a skeleton's is 0.01. Dropping to 0.125 is a real but partial fix,
/// and its sample set is a strict superset of today's - verified here rather
/// than taken on report, so it can rescue more bodies and never fewer.
///
/// **The unstick pass no longer depends on any of that**, because a grid was
/// the wrong instrument. It now tries the **block floors** above the body first
/// - `position` is the feet, so a body at rest stands on a block boundary and
/// `floor(y) + 1, + 2, ...` are exactly the valid resting heights, in two or
/// three exact samples with no slack condition at all. The fine grid still runs
/// behind them for slabs, stairs and carpets, whose standing heights are not on
/// a boundary; but nothing lethal now hangs on its phase. **The recommendation
/// to drop the shared constant to 0.125 therefore still stands for
/// `Player.cpp`**, whose loop is still a bare grid, and no longer matters much
/// here.
///
/// The reach is a different question and it *is* load-bearing: a fully entombed
/// body has to clear its own volume, so a lift shorter than the body's height
/// can never free it. `Player.cpp` asserts exactly that with
/// `kUnstickReach >= kHeight`. We cannot mirror the assert - our height is a
/// runtime field and five rows exceed 2.0 m - so `step` honours the same rule
/// at runtime by taking the larger of the shared reach and the body's height.

/// Suffocation: **one health point every half second, unreduced by armour**
/// (`RESEARCH.md` §3.1's damage table, the `Suffocation` row - **not** the
/// `Lava` row two lines above it, which reads 4 HP per 0.5 s and is what a
/// line-numbered citation here used to land on). Taken from
/// `survival::kSuffocationDamage` and `survival::kSuffocationInterval` rather
/// than written again here, because the player already pays exactly this and a
/// second copy of a published constant is the bug this file has paid for more
/// than any other. `Survival.hpp` already asserts the interval against the
/// shared hazard cadence.
///
/// **Read the row, not the line number.** The pointer was four rows off for a
/// day, and the shape of that mistake is worse than it sounds: the prose was
/// right, the constant was right, and the only wrong thing was where it said to
/// look - so an agent checking the constant against §3.1 would have read
/// "4 HP per 0.5 s", concluded the code understated suffocation fourfold, and
/// quadrupled a figure the player shares.
///
/// **What is ours is the trigger, and it is narrower than the player's.** The
/// player suffocates when the block at its *eye* is opaque and solid. A
/// creature suffocates only when it is inside terrain **and the unstick pass
/// above could not free it** - because ours teleports a shallowly-buried body
/// out instantly, so charging on the overlap alone would bill it for a burial
/// the same frame already ended. What is left is the case that had no answer at
/// all: no free space anywhere within the body's own height above it.
///
/// **The trigger is only as honest as the search that feeds it**, and when this
/// clock was first written it was not honest enough - the pass sampled a fixed
/// grid that could step straight over a legitimate pocket, and five rows were
/// taller than the lift could reach. Both were written down as known gaps,
/// which was fine while failing merely meant "stuck", and stopped being fine
/// the moment failing meant "dead". Both are closed at the pass itself; see the
/// note there. Charging a body for a pocket the sampling missed would have been
/// a search bug wearing a death message.
///
/// This also gives sand falling on a mob, a piston pushing one into a wall and
/// a player bricking one up somewhere to end, all of which were survivable
/// forever.

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
/// The shove a player's blow carries, in metres per second - **the player-hits-
/// creature direction only**, applied in `damageCreature`'s caller below.
///
/// There is a second, deliberately separate pair further down this file,
/// `kKnockbackSpeed` / `kKnockbackLift` at 4.5 and 3.0, which is the
/// creature-hits-you direction. **They are not a duplicated constant and must
/// not be unified** (2026-08-19): they are two ends of the same exchange and
/// the reference tunes them apart too - a mob's blow is scaled per species by
/// `knockbackScale`, a player's blow is scaled by the Knockback enchantment,
/// and neither scale exists on the other side. What would make this note false
/// is one pair losing its own scaling factor, at which point they really would
/// be saying the same thing twice.
constexpr float kStrikeKnockback = 5.0f;
constexpr float kStrikeLift = 4.0f;

/// How far outside its collision box a creature can still be hit. The reference
/// pads its pick box too, and here it also covers the fact that `halfWidth` is
/// deliberately narrower than the model it is drawn with.
constexpr float kAimPadding = 0.15f;
/// How hard a blast throws what it catches, in blocks per second at full
/// impact.
///
/// **This was 20 and that was the wrong end of a conversion.** The reference's
/// knockback is one block per *tick*, which is twenty a second, so 20 is what
/// the arithmetic gives - and the arithmetic is not the whole rule. The
/// reference decays velocity by 9% every tick and **this engine does not
/// anywhere**: every write to `creature.velocity` in this file was enumerated on
/// 2026-08-19 and not one of them damps it, while the movement step carries the
/// horizontal component straight back into the next tick's wish. So a creature
/// thrown off the ground keeps all twenty blocks per second until it lands,
/// which reads as a rocket rather than a blast.
///
/// **7 is the sustained-speed equivalent of that impulse**, and it is not a new
/// number: `Main.cpp`'s `kBlastKnockback` is 7.0f, has been since the same
/// question was worked through for the player, and states the whole argument
/// above in its own comment. That argument is about *our physics*, not about
/// what is being thrown, so it always applied here word for word - and this
/// file only converted the units and stopped.
///
/// **`CLAUDE.md` bug shape #14 exactly**: a rule that exists, is correct, and is
/// commented, in only one of the two places that need it. The symptom was one
/// blast throwing a cow 2.9 times as far as it threw the player, from the
/// identical impact - visible in play, invisible in review, and reported by
/// nothing.
///
/// **These are two copies of one number and now there is one literal.** This
/// is `blast::kKnockbackSpeed` from `Explosion.hpp` - the constant published
/// for exactly this, carrying the whole argument above and a `static_assert`
/// pinning it away from the reference's 20 - rather than a second 7.0f that
/// merely agrees with it. `Explosion.hpp` was already included here for
/// `withinBlast` and `explosionImpact`, so adopting it cost nothing, and it
/// gave a constant that had been *published, reasoned about and read by nobody*
/// its first consumer.
///
/// **The local name is kept rather than the definition**, which is the cheapest
/// rung of `CLAUDE.md`'s ladder that works here: derive one side from the other
/// so the coupling cannot break. A tuner editing `Explosion.hpp` moves this
/// automatically and no comment has to be believed.
///
/// **`Main.cpp`'s `kBlastKnockback = 7.0f` is the one copy left**, and it is
/// another agent's file - finding 9690 routes it. Until that lands, a change to
/// `blast::kKnockbackSpeed` moves creatures and not the player, which is the
/// smaller half of the fork that used to exist and the same direction of
/// travel.
constexpr float kBlastThrow = blast::kKnockbackSpeed;

/// A lightning bolt's reach, and what it does. The reference's 6x12x6 box read
/// as half-extents, and its five points of damage.
constexpr float kLightningReach = 3.0f;
constexpr float kLightningRise = 9.0f;
constexpr int kLightningDamage = 5;

/// **Freezing damage for the reference's fire mobs, which take five a tick
/// rather than one.** Bedrock's list is blaze, magma cube and strider; the
/// magma cube is the only one of the three on this roster, and it carries
/// `freezesHarder` for it.
///
/// A bare 5 rather than a multiple of `survival::kFreezeDamage`: the reference
/// publishes the two rates independently and one is not derived from the other,
/// so writing `kFreezeDamage * 5` would manufacture a coupling that does not
/// exist. Source is minecraft.wiki, *Powder Snow* - **secondary**, by way of
/// `Survival.hpp`'s freezing block, which names this rule as this file's to
/// implement and records that Bedrock's own JSON does not publish it.
constexpr int kFireMobFreezeDamage = 5;

/// **How long a skeleton stands in powder snow before it becomes a stray**, and
/// this one is primary: `Mojang/bedrock-samples`,
/// `behavior_pack/entities/skeleton.json`, the `in_powder_snow` component group
/// carries `"minecraft:timer": { "looping": false, "time": 20 }` whose
/// `time_down_event` is `become_stray_event`. Bedrock's `minecraft:timer.time`
/// is in **seconds**, so this is 20 s and not 20 ticks.
///
/// **Read against the wiki, which says seven**, because that is the Java
/// figure and it is the freeze *onset* rather than the conversion - the two
/// mechanics genuinely differ and reading the wiki's 7 into here would have
/// converted a skeleton the instant the first point of freezing damage landed.
/// Primary source wins, and this project has already paid for that four times.
constexpr float kStrayConversionSeconds = 20.0f;

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
///
/// `kAttackInterval` is **seconds**, and it is that component's other field:
/// `melee_box_attack.cooldown_time`, whose published default is 1 second (the
/// Bedrock creator docs, *Entity Documentation - minecraft:behavior.melee_box_-*
/// *attack*). Seconds is the reference's own unit here, unusually - most of
/// what this file copies is published in ticks - so **do not "convert" it**;
/// multiplying by `tick::kPerSecond` would give a mob one blow every twenty
/// seconds and every review step would pass.
///
/// **This is the mob's cooldown and it has no player counterpart.** Java gives
/// the player a charge-up meter that scales damage with how long since the last
/// swing; Bedrock has none, and Bedrock is the reference, so nothing on the
/// player's side should ever be derived from this number.
constexpr float kMeleeHorizontalReach = 0.8f;
constexpr float kAttackInterval = 1.0f;
/// How long one swing of the arm takes, against that one-second gap between
/// blows. Six ticks - so a mob strikes, recovers, and stands there for most of
/// a second before doing it again. **Seconds, written as six ticks so the six
/// stays legible.**
///
/// **Six is Java's, and it is the one number on this page that is.** Checked
/// 2026-08-19: Bedrock does not publish a swing duration anywhere. Its arm
/// swing is driven by the engine-supplied Molang `variable.attack_time`, which
/// is a normalised 0-to-1 progress value - `resource_pack/animations/`
/// `humanoid.animation.json` in `Mojang/bedrock-samples` declares no
/// `animation_length` on the attack animation, and the controllers beside it
/// only ever test its sign. Six ticks is `LivingEntity`'s swing in Java, used
/// here because nothing better exists and because this drives an *animation*
/// rather than a hit. **What would make this note false: an `animation_length`
/// appearing on that animation, or a `{{only|bedrock}}` duration on
/// minecraft.wiki's *Melee attack* page.**
constexpr float kAttackSwingSeconds = 6.0f * tick::kSeconds;
static_assert(kAttackSwingSeconds < kAttackInterval && kAttackSwingSeconds == 0.3f,
              "the swing has to finish inside the one-second cooldown, and six ticks is "
              "0.3 s only at a twenty-tick second");

/// Burning in daylight starts **at** a sky light of twelve, not above it - the
/// test at the call site is `>=`, and this sentence said "above" until
/// 2026-08-19, which is a one-level disagreement between prose and code in the
/// direction that gets code "corrected". **The threshold is all that is written
/// down here** - the rate is `survival::kBurnDamage` and
/// `survival::kBurnInterval`, one point a second, which the player already
/// spends on being alight after leaving a flame. A zombie therefore takes
/// twenty seconds to die of the sun and can still reach shade in that time -
/// the reference's behaviour, and the whole reason it reads as burning rather
/// than as vanishing.
///
/// **Twelve is the best-sourced number and the source is not edition-tagged.**
/// minecraft.wiki's *Sun* page: "sunlight can be considered as the presence of
/// both an internal sky light level of at least 12 and a sky light level of
/// 15" - so the reference's real rule is a *pair* of tests, and ours keeps only
/// the first because we have one sky-light value rather than two. Re-checked
/// 2026-08-19 against the Bedrock behaviour packs and no Bedrock-only statement
/// of it exists, so **do not "restore" this to a Bedrock citation** it never
/// had; what would make that claim false is a `{{only|bedrock}}` tag appearing
/// on the *Sun* or *Zombie* page's burning section.
///
/// **This file used to carry its own `kBurnDamage` and `kBurnInterval` beside
/// this line, and they were the *unqualified* names**, so the sun branch below
/// silently read the local pair while the lava and fire branches beside it read
/// `survival::` - no ambiguity, no warning, and a comment at the call site
/// asserting outright that both rates come from `Survival.hpp`. They only
/// agreed because both pairs happened to read 1 and 1.0; retuning the player's
/// afterburn would have moved one and not the other. The sky light stays
/// because it has no second owner: nothing in `Survival.hpp` publishes it, the
/// player does not burn in daylight, and it is a *trigger* rather than a rate.
constexpr int kBurnSkyLight = 12;

/// Who walks through fire and lava unharmed.
///
/// "Most of the Nether mobs (blazes, ghasts, magma cubes, striders, wither
/// skeletons, zoglins, and zombified piglins), agents, NPCs, vexes, ender
/// dragons, shulkers, wardens, withers, and players or mobs affected by the
/// Fire Resistance effect are not damaged when touching lava" (minecraft.wiki,
/// *Lava*). Three of that roster exist here: the magma cube in all three sizes,
/// the blackbone - our name for the reference's wither skeleton - and the
/// zombie princepin, its zombified piglin.
///
/// **The princepin and the princepin brute are not on it**, and neither is the
/// plain skeleton. That list names its own exceptions, and inverting them would
/// make the brute that guards a bastion immune to the one thing the terrain
/// around it is made of - so the roster is quoted rather than inferred from
/// "lives in the Nether".
///
/// This is a `switch` and not a table column because the column belongs in
/// `CreatureSpecies`, which lives in `Creature.hpp` and has an owner other than
/// this session. **The single edit that makes the assertion below fail is
/// adding a species**, which is exactly when this roster needs re-reading -
/// a `default:` alone would have quietly answered "not immune" for it.
[[nodiscard]] constexpr bool fireImmune(CreatureKind kind) {
    switch (kind) {
    case CreatureKind::Blackbone:
    case CreatureKind::ZombiePrincepin:
    case CreatureKind::MagmaCubeSmall:
    case CreatureKind::MagmaCubeMedium:
    case CreatureKind::MagmaCubeLarge:
        return true;
    default:
        return false;
    }
}
static_assert(static_cast<int>(CreatureKind::Count) == 58,
              "a species was added or removed - check it against the fire-immune roster in "
              "fireImmune() above, then bump this number");

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
///
/// **Seconds, from the published ten ticks.** Written as the conversion rather
/// than as 0.5 so the ten is still legible against the reference.
constexpr float kTargetScanSeconds = 10.0f * tick::kSeconds;

/// Bedrock's `look_at_player`: notices you within 8 m, rolls **0.02 per tick**
/// and holds the glance for two to four seconds.
///
/// The probability is converted to a per-second rate, because our tick is a
/// frame and the reference's is a fixed twentieth of a second - used verbatim,
/// a chicken at 120 fps would roll six times as often as one at 20.
///
/// **The conversion is `* tick::kPerSecond`, not `/ 0.05f`.** It was the
/// literal for a long time, which made this the only per-tick-odds conversion
/// in the file that did not go through `Tick.hpp` - `kGrazeChancePerSecond`
/// and `kGrazeBabyChancePerSecond` below do the identical sum correctly. Same
/// value today (0.4 either way), so nothing moved; what changed is that the
/// tick rate now has one owner here as well as everywhere else.
constexpr float kLookDistance = 8.0f;
constexpr float kLookChancePerSecond = 0.02f * tick::kPerSecond;
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
/// From `Tick.hpp`, the one owner. Only the name is local: everything around it
/// is published per tick and reads as a multiplier, not as a duration.
constexpr float kTicksPerSecond = tick::kPerSecond;

/// The two spellings of the tick have to agree **at the site that divides by
/// one of them** - the creeper's fuse headroom is a quarter of a second either
/// way round, and the second clause is the general rule the first is an
/// instance of.
///
/// > Fails if: `kTicksPerSecond` is re-literalised here rather than derived, or
/// > either half of `Tick.hpp` moves without the other.
static_assert(kFallFuseHeadroomTicks / kTicksPerSecond == kFallFuseHeadroomTicks * tick::kSeconds &&
                  kTicksPerSecond * tick::kSeconds == 1.0f,
              "the fuse headroom is five ticks whichever way the tick rate is spelled");

/// Grazing. **`time_until_eat` is 1.8 and its published unit is *seconds*, not
/// ticks** - `Mojang/bedrock-samples` `behavior_pack/entities/sheep.json` reads
/// `"time_until_eat": 1.8`, and Microsoft's schema for
/// `minecraft:behavior.eat_block` says "the amount of time (**in seconds**) it
/// takes for the block to be eaten". So this one is *not* converted, and
/// multiplying it by the tick rate would give a sheep a thirty-six-second
/// mouthful. Java's `EatBlockGoal` uses 40 ticks (2 s) instead, so the two
/// editions genuinely differ here and Bedrock is the shorter.
///
/// The odds beside it *are* per tick and are converted: 0.001 for an adult and
/// 0.02 for a lamb, the same file's
/// `"success_chance": "query.is_baby ? 0.02 : 0.001"`, which happens to match
/// Java exactly. A sheep therefore crops the grass about once a minute and a
/// lamb rather often, which is exactly the reference and is why a flock leaves
/// a trail of dirt behind it.
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
/// them from and what M27 built (`Main.cpp` drives `applyLightning` off the
/// storm). This keeps a few in the world between storms.
///
/// **Ours, and the reference publishes no counterpart** - checked 2026-08-19.
/// minecraft.wiki/w/Creeper states only that "a charged creeper is created when
/// lightning strikes within four blocks of a normal creeper", and
/// `behavior_pack/spawn_rules/creeper.json` in `Mojang/bedrock-samples` carries
/// no powered roll at all. So it is a probability per *spawn*, not per tick or
/// per second, and **nobody should go looking for a source for the 0.05**;
/// what would make this note false is a `powered` entry appearing in that
/// spawn-rules file.
constexpr float kChargedChance = 0.05f;
/// The shove a *creature's* blow carries, in metres per second. Enough to break
/// your footing, not enough to throw you somewhere you cannot recognise.
///
/// **The mirror of `kStrikeKnockback` / `kStrikeLift` (5.0 / 4.0) near the top
/// of this file, and deliberately a different number.** That pair is the
/// player-hits-creature direction; this one is multiplied per species by
/// `knockbackScale` and `knockbackLiftScale`, which is how an iron golem
/// launches you and a silverfish does not. See the longer note over
/// `kStrikeKnockback` before merging them.
constexpr float kKnockbackSpeed = 4.5f;
constexpr float kKnockbackLift = 3.0f;

/// **This name exists twice in this translation unit, at two different values
/// and two different meanings, and only a namespace qualifier keeps them
/// apart.** The one above is a creature's punch, 4.5 m/s. `Explosion.hpp`
/// publishes `blast::kKnockbackSpeed` at 7.0 m/s for a blast, and its own
/// comment says it "named this differently on purpose" after a past C2872 -
/// the name it picked was already taken here, at a different value, in one of
/// the two files it names. Nothing clashes only because that one is namespaced
/// and this one is in this file's anonymous namespace, where the inner
/// declaration wins every unqualified lookup.
///
/// **So `kBlastThrow`'s `blast::` prefix is load-bearing, not decoration**: drop
/// it and the constant silently becomes 4.5, a 36% weaker blast throw, on a
/// clean build with no diagnostic possible. That is the failure this fires on -
/// it is sited here, beside the declaration that would capture the lookup,
/// rather than beside `kBlastThrow`, because this is the one a reader has to be
/// looking at to make the mistake.
///
/// If a tuner ever moves `blast::kKnockbackSpeed` to exactly 4.5 this fires as
/// a false alarm; that is the direction to fail in, and the message says which
/// to check first. Filed 2026-08-19.
static_assert(kBlastThrow != kKnockbackSpeed,
              "kBlastThrow has picked up this file's kKnockbackSpeed (4.5 m/s, a creature's "
              "punch) instead of blast::kKnockbackSpeed (7.0 m/s, an explosion). Check the "
              "blast:: qualifier on kBlastThrow before changing either number.");

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
/// Searches allowed across the whole population **in one second**, and the unit
/// is the fix: it was `kPathsPerFrame = 2`, so AI responsiveness was a function
/// of frame rate. At 240 fps that bought 480 searches a second and at 30 fps
/// only 60, against a full population wanting about 88 - so on a weaker machine
/// some animals never pathed at all while others chased normally. Everything
/// else in this file is per tick or per second; this was the odd one out
/// (failure shape #3, a number measured in a different unit).
///
/// Two per tick at the reference's 20 Hz, which is exactly what the old
/// constant delivered on a frame that happened to be a tick. A ceiling on the
/// spike rather than a rate: the repath cadence already holds the population to
/// about two searches a second each, so this only ever binds when several
/// animals are boxed in at once - which is exactly the case worth bounding.
constexpr float kPathsPerSecond = 40.0f;
/// Most a hitch may bank, so a two-second stall does not buy eighty searches on
/// the frame it ends. Two ticks' worth.
constexpr float kMaxPathBurst = 4.0f;
/// How far an ambling creature picks a spot. The reference's `random_stroll`
/// reaches ten blocks.
constexpr float kWanderRange = 10.0f;

/// Swimming. A fish carries a heading and a dive angle rather than a goal
/// point: the reference's `random_swim` aims 16 blocks out and up to 4 up or
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

/// How far a bee looks for a flower, in **blocks**, and both halves are the
/// reference's: `Mojang/bedrock-samples` `behavior_pack/entities/bee.json`
/// gives its `minecraft:behavior.move_to_block` a `"search_range": 6` and a
/// `"search_height": 4`. **The height read 3 until 2026-08-19** - a
/// hand-rounded number sitting beside a sourced one, which is the shape that
/// survives review because the pair looks deliberate. It runs on the wander
/// timer rather than per frame, which is what keeps a box this size affordable.
constexpr int kFlowerSearchXZ = 6;
constexpr int kFlowerSearchY = 4;

/// The rest of the pollination cycle, every number off
/// `Mojang/bedrock-samples` `behavior_pack/entities/bee.json` unless it says
/// otherwise. **Read the unit note on `kPollinateSeconds` before changing any
/// of them.**
///
/// How far a bee looks for its hive, in **blocks**: `find_hive`'s
/// `minecraft:behavior.move_to_block` carries `"search_range": 16` and
/// `"search_height": 10`, four times the flower box's volume and the reason
/// this one is gated behind `hiveCell.y < 0` and a timer rather than run on the
/// wander cadence like the flower scan.
constexpr int kHiveSearchXZ = 16;
constexpr int kHiveSearchY = 10;

/// How long a bee stays on a flower before it has nectar.
///
/// **`look_for_food`'s `"stay_duration": 20.0`, read as SECONDS, and the unit
/// is genuinely disputed - which is why both readings are written down.**
/// Microsoft's own component reference for `minecraft:behavior.move_to_block`
/// (learn.microsoft.com, read 2026-08-19) says "Number of **ticks** needed to
/// complete a stay at the block", which would make this 1.0 s. Three things
/// argue for seconds and are why that is what is used here: the field is typed
/// "Decimal number" and every tick count in the same file is an integer;
/// Java's `PollinateGoal` holds a bee on its flower for a minimum of 400 ticks,
/// which is exactly 20 s, so 20.0 s is the value the two editions agree on; and
/// play accounts of Bedrock describe tens of seconds of circling, not one.
///
/// **The two readings differ by twenty times**, so this is the constant to
/// re-derive first if hives ever fill absurdly fast. What would settle it: a
/// stopwatch on a Bedrock bee, or an engine-side source for the unit. This is
/// `CLAUDE.md` bug shape #3 caught before it was written rather than after.
///
/// **The cross-entity check was run on 2026-08-19 and came back empty, which is
/// itself the answer to "why is this still open".** The technique that settled
/// `honey_level` was counting the property's users - it has two, `beehive` and
/// `bee_nest`, and they agree. `stay_duration` has **exactly one user in the
/// whole vanilla behaviour pack, and it is `bee.json` itself**. So there is no
/// second entity whose value could disambiguate the unit, and the only
/// documentary statement of it is the Microsoft page above - which carries
/// `ai-usage: ai-assisted` metadata, so the one source is also the weak one. A
/// search for a second source returns that same page, which is agreement with
/// itself rather than corroboration.
///
/// **Left at 20 s deliberately, because the two errors are not symmetric.** Too
/// slow is a hive that takes longer than it should to fill, which is a pacing
/// complaint. Too fast is honeycomb going from scarce to trivial and a wax farm
/// that needs no farm - it would quietly undo the scarcity the whole chain is
/// built on. When a factual question cannot be closed, the conservative arm is
/// the one that fails visibly rather than the one that fails silently.
///
/// **This is a feel decision with a factual dispute under it, so it belongs to
/// the playtester**, and flipping it is one edit: 20.0f -> 1.0f, nothing else
/// in the cycle reads the unit.
constexpr float kPollinateSeconds = 20.0f;

/// How close counts as arrived, in blocks, measured in three dimensions because
/// a bee approaches from above as often as from the side. The flower's is
/// `look_for_food`'s `"goal_radius": 1.0`; the hive's is `go_home`'s
/// `"goal_radius": 1.2` rather than `find_hive`'s 0.633, because `go_home` is
/// the higher-priority path home in the reference and 0.633 is a hard target
/// for something that steers by heading rather than by waypoint.
constexpr float kFlowerArrival = 1.0f;
constexpr float kHiveArrival = 1.2f;

/// How often a homeless bee re-runs the hive scan, in seconds.
///
/// **The reference's own retry cadence for the adjacent case, not a guess.**
/// `bee.json` gives `find_hive` a `"tick_interval": 1` - once per tick - which
/// over a 33x21x33 box is not affordable here; its `hive_full` group waits
/// `"time": [5, 20]` with `randomInterval` before trying for a hive again, and
/// that is the number used. A bee that has a home never runs this at all, so
/// the cost is paid only where there is nothing to find.
constexpr float kHiveSearchMin = 5.0f;
constexpr float kHiveSearchSpan = 15.0f;

/// The chance, per delivery, that the hive gains **two** levels instead of one.
/// "When the bee exits, the hive increments its honey level by 1 and has a 1%
/// chance to increment it by 2" - minecraft.wiki/w/Beehive/Usage, and Java's
/// `nextInt(100) == 0 ? 2 : 1` says the same thing, so it is two *instead of*
/// one rather than three.
constexpr float kDoubleHoneyChance = 0.01f;

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

/// **Where a creature looks from: one owner, because eight sites derived it and
/// three of them forgot the scale (2026-08-19).**
///
/// The eye sits `kEyeFraction` of the way up the body, and the body is
/// `species.height * creature.scale` - **the same `creature.scale` above**, so
/// a cub's eye must come down with the rest of it. Three sites multiplied the
/// species height by 0.85 alone: the player line-of-sight ray in `think`,
/// `pitchToPlayer` beside it, and the shooter's own eye inside `pitchToTarget`
/// - which was inconsistent *within one expression*, since the target's eye one
/// line above already scaled. At `kBabyScale` an unscaled eye is **1.82x too
/// high**: a polar bear cub sighted from 1.19 m against a true 0.654 m, so it
/// saw over cover it should have been blind behind and went blind in a
/// crawlspace because the ray started inside the ceiling. Eight roster rows
/// carry `babyChance` and pass the sight gate - cat, wolf, fox, ocelot, polar
/// bear, panda, dolphin, axolotl.
///
/// It is `CLAUDE.md` bug shape #5, a derivation applied to one of a pair and
/// not the other, and **the creature-vs-creature ray fifteen lines from one of
/// the broken ones carried a comment naming that exact shape** - the pair was
/// written to mirror each other and one of them had already stopped. So the
/// three call sites are not the fix; **having one place that knows the
/// expression is the fix**, which is why this is a function rather than a
/// corrected literal in three places.
///
/// 0.85 is ours rather than the reference's, which publishes a per-entity
/// `minecraft:collision_box` and a separate eye offset the behaviour packs do
/// not carry. What would make this note false: a species wanting its own eye
/// fraction, at which point this becomes a column on the row and every caller
/// already reads it through here.
constexpr float kEyeFraction = 0.85f;

/// How far above its feet a body looks from, in metres.
constexpr float eyeHeight(const CreatureSpecies& species, float bodyScale) {
    return species.height * bodyScale * kEyeFraction;
}

/// Where a body looks from, in world space.
inline glm::vec3 eyeOf(const glm::vec3& feet, const CreatureSpecies& species, float bodyScale) {
    return feet + glm::vec3{0.0f, eyeHeight(species, bodyScale), 0.0f};
}

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

/// Records who landed a blow, for the drop table to read a second later.
///
/// **One function for both blow sites**, because "zero names the player" is
/// exactly the kind of rule that gets written correctly in one place and
/// forgotten in the other. Overwrites rather than accumulates: the reference
/// decides a mob's drops by the *killing* blow, so whoever hit last wins.
void recordBlow(Creature& target, std::uint32_t fromId) {
    if (target.death.latched) {
        return;
    }
    target.death.cause = fromId == 0 ? DeathCause::Player : DeathCause::Creature;
    target.death.killerId = fromId;
}

/// The same, for the four ways the world itself kills something. Clears
/// `killerId` as well, or a creature the player wounded and the sea finished
/// would still be paying out a player kill's rare drops.
void recordHazard(Creature& target, DeathCause cause) {
    if (target.death.latched) {
        return;
    }
    target.death.cause = cause;
    target.death.killerId = 0;
}

/// **One function for every blow that lands on a creature**, and it is a new
/// rule here rather than a tidied-up old one. Read the next paragraph before
/// judging a time-to-kill regression.
///
/// **Creatures never had a damage window.** At HEAD `applyHits` (creature on
/// creature) and `strike` (the player's swing and every arrow) each subtracted
/// health straight out of the struct, unconditionally, and `Creature.hpp`
/// carried neither `invulnerableSeconds` nor `lastDamage` - both fields arrive
/// in this change. Only the player had the rule, in `damagePlayer`, since M21.
/// So this is **not** a de-duplication: `Survival.hpp` is the shared home the
/// player's copy moved into, and creatures are a **new** caller of it.
///
/// **The behavioural consequence is real and it is not a refactor.** Within
/// `survival::kInvulnerableSeconds` of a hit landing, a second blow no larger
/// than the first now does nothing at all, and only the *difference* lands when
/// it is larger (`RESEARCH.md` §2.4). Concretely: fast weapons lose roughly
/// half their effect against mobs, a wolf pack or two skeletons converging on
/// one target have most of their hits vanish, and because `chargeFall` routes
/// through here too, a mob that has just landed is briefly hard to hit. That is
/// the reference's behaviour - Bedrock mobs carry the same ten-tick hurt
/// resistance with the same "only the difference lands" rule - and it is the
/// point of the change rather than a side effect of it. It also fixes what the
/// unconditional sites did: a wolf pack all landing in one tick deleted a sheep
/// between two frames, and a fall ending on the same tick as a sword swing
/// charged for both.
///
/// **The three hazard ticks are deliberately outside it** - burning, drowning
/// and drying out each carry their own cadence in `think`, one point every half
/// second or every second, and running those through a half-second window would
/// silently halve rates that were taken from the reference. They are the same
/// split the reference makes, where suffocation and fire have their own clocks
/// rather than sharing the melee one. Suffocation joins them, for the same
/// reason and on the same terms. **Lightning and explosions do not** - they are
/// ordinary blows in the reference and go through here, which is what stops a
/// creeper detonating on the frame of a sword swing from charging twice.
///
/// Returns whether health actually moved, so a caller can hang the things that
/// should only happen on a real hit - the hurt cry, the knockback, the anger,
/// the alerting of neighbours - off the answer rather than off having tried.
bool damageCreature(Creature& target, int amount) {
    if (amount <= 0 || target.health <= 0) {
        return false;
    }

    // **The window rule lives in `Survival.hpp`**, which is where the player's
    // copy moved to so that this file could call it rather than write a second
    // one. There was no creature copy to merge - see above - so what this line
    // does is *adopt* a published rule, not reconcile two of them.
    //
    // No `bypass` here: a creature has no void death and nothing else that must
    // outrun the window. The returned amount needs no scaling either, which is
    // why the shared function hands back the raw figure and leaves scaling to
    // the caller - `effects::damageTakenScale` reads a `Player`'s effects and a
    // creature has no effects at all.
    const int landed =
        survival::chargeDamageWindow(target.invulnerableSeconds, target.lastDamage, amount);
    if (landed <= 0) {
        return false;
    }

    target.health = std::max(0, target.health - landed);
    target.hurtTimer = kHurtSeconds;
    return true;
}

// **A note about `damageCreature` above, deliberately not a `///` block** - it
// documents no function, and attached to the one below it would read as a claim
// about `hazardDamage`.
//
// **Why `survival::armourDamageTaken` is not called from this file, measured
// 2026-08-19 rather than assumed.** Finding 9662 reported "armoured creatures
// take full damage" off a bare-name sweep that found the function spent in
// `Player.cpp` and `Survival.hpp` and nowhere here, and asked that the damage
// path be traced before anything was wired - which is the right order, because
// the sweep cannot see a value that arrives through a struct or a table.
//
// Traced: **there is no armour on a creature to read.** Every blow reaches
// `damageCreature` above, which spends `chargeDamageWindow` and subtracts;
// nothing between a swing and `health` consults a defence value, and a
// case-insensitive search of `armo(u)r|defence|defense|helmet|chestplate|
// leggings|boots|equipment` across `Creature.hpp` and `Creature.cpp` returns
// only prose - the villager *armourer* profession, comments about drops, and
// `CreatureSpecies::heldMainHand`, which is render-only and holds no armour.
// Neither `Creature` nor `CreatureSpecies` carries a defence, a toughness or a
// worn set. Controls for that search: `chargeDamageWindow` returns 1 in this
// file, so it does reach here, and an invented name returns 0.
//
// So the finding is **structural rather than a gap**: the reference dresses
// its zombies and skeletons in random armour on the harder difficulties, we
// model no mob equipment at all, and until a mob can *wear* something there is
// no argument to pass. Filing it as "creatures ignore armour" would have led
// the next reader to add a defence column and a second owner for a number
// nothing produces.
//
// **What would make this false:** a defence or armour field appearing on
// either type, or a species row naming a worn item. The day that lands,
// `damageCreature` is where the call goes - one site, because every blow
// already comes through there.

/// The world killing something, on its own clock and **outside** the damage
/// window `damageCreature` enforces.
///
/// Burning, drowning and drying out each carry their own cadence - one point
/// every half second or every second - and routing those through a half-second
/// window would silently halve rates taken from the reference. That split is
/// deliberate and is the reference's own; what was not deliberate is that the
/// three sites then had to agree about everything else by inspection, **and one
/// of them did not**. Burning tested `health > 0`; drowning and drying out did
/// not, and both live in `step`, which is the one function that *does* run on a
/// corpse. So a cow the player killed at the water's edge kept drowning after
/// it died, and `recordHazard` overwrites `cause` and clears `killerId` - so
/// the kill became the sea's, and the player's rare drops were gone by the time
/// the body paid out a second later. A dolphin left on a beach did the same
/// thing through `dryTimer`.
///
/// One function, so the guard cannot travel to two of the three again.
void hazardDamage(Creature& target, int amount, DeathCause cause) {
    if (target.health <= 0) {
        return;
    }
    // Clamped, exactly as `damageCreature` clamps. Not cosmetic: drowning takes
    // two points at a time against a health that can be 1, so this was the last
    // path in the file that could leave the field negative - and it is written
    // to the save. Every reader tests `health <= 0` so nothing behaves
    // differently today, which is precisely why the two would have drifted
    // apart unnoticed.
    target.health = std::max(0, target.health - amount);
    target.hurtTimer = kHurtSeconds;
    recordHazard(target, cause);
}

/// Rousing one creature: the anger clock and who it blames, set together.
///
/// **One owner, because the pair had four writers** - `strike`, where the blow
/// itself provokes; `applyHits`, the same thing for creature on creature;
/// `alertNeighbours`, where the call carries to the herd; and `provokeNear`,
/// where something in the world angers a group without touching it. Each set
/// both fields by hand, and `provokedTimer` without `threatId` is a creature
/// angry at whoever it was angry at last - which, since zero names the player,
/// defaults to blaming them for someone else's arrow.
///
/// **How long it lasts is the species' own number**, never a flat constant: a
/// bear you shot is angry for five hundred seconds and a bee for twenty-five.
void rouse(Creature& target, std::uint32_t threatId) {
    target.provokedTimer = speciesInfo(target.kind).angerSeconds;
    target.threatId = threatId;
}


/// partner, and the two swing cooldowns.
///
/// **Split out because it has two callers, and the second one is why it
/// exists.** These sat at the top of `think`, and `update` skips `think`
/// entirely for a corpse - so all four froze at whatever the killing blow left
/// and never counted down again. `Player.cpp`'s `tickPassiveTimers` is the same
/// extraction made at the same time, and there it was load-bearing: its early
/// return skipped `deathSeconds` and soft-locked the respawn screen.
///
/// **Here it is hygiene, and saying so is the point.** Nothing visible depends
/// on it today: the corpse tint is `hurtTimer > 0 || health <= 0` on purpose,
/// `damageCreature` refuses a corpse so the window cannot matter, and the
/// `wish` chain gives a corpse its own branch before either swing timer is
/// read. What it buys is that a clock measuring elapsed time elapses, so the
/// next thing to read one of these off a body - a revive, a longer death
/// animation, a corpse that can be hit again - finds a number rather than a
/// fossil.
///
/// **The AI clocks deliberately stay in `think`.** `provokedTimer`, `scanTimer`
/// and `decisionTimer` are inputs to decisions, and a corpse makes none - where
/// these four are outputs of things that already happened, and an event in the
/// past does not stop expiring because the thing it happened to has died.
void tickPassiveTimers(Creature& creature, float deltaSeconds) {
    creature.hurtTimer = std::max(0.0f, creature.hurtTimer - deltaSeconds);
    // **The other half of the shared window rule.** The pair is cleared
    // together or `lastDamage` outlives the window it belongs to and suppresses
    // the next real hit - and that clearing is no longer a rule this function
    // has to remember on its own, which is the whole point of the move.
    survival::tickDamageWindow(creature.invulnerableSeconds, creature.lastDamage, deltaSeconds);
    creature.attackTimer = std::max(0.0f, creature.attackTimer - deltaSeconds);
    creature.swingTimer = std::max(0.0f, creature.swingTimer - deltaSeconds);
}

/// Fall damage, from **`survival::fallDamage`, which owns the curve** -
/// `RESEARCH.md` §1.10, `floor((distance - 3) * 1)`, measured in change in Y
/// rather than in speed. Called once per resolved landing by `step`.
///
/// **This used to re-derive the curve inline and it cost the same bug the
/// player's copy cost, for the same reason (2026-08-19).** The two constants
/// were read by name and multiplied out here, which looks like using the owner
/// and is not: it silently dropped `survival::kFallFloorTolerance`, the third
/// term. `fallDistance` is an accumulator - `+= before.y - position.y` once per
/// resolved sweep - so a true 23-block drop arrives as 22.999980926513672 and
/// `floor` takes the point away. That file measured it on the player: **669 of
/// 1332 whole-block drops, 50.2%, charged one point light, and which ones is
/// frame-rate dependent.** Creatures accumulate the same way, so half of every
/// mob fall was light and a published anchor - 23 blocks kills - did not hold
/// on a grinder built from it.
///
/// So: **do not re-derive this curve.** `survival::fallDamage` is `constexpr`,
/// its anchors and its drift cases are `static_assert`ed beside it, and a
/// checker that cannot rot is worth more than three constants read by name.
/// What would make this note false: the tolerance moving into the callers, at
/// which point every caller wants revisiting together.
///
/// **Creatures took none at all until now.** `fallDistance` was a complete,
/// correct accumulator with exactly one consumer - the exploder fuse - and
/// nothing anywhere subtracted health, so a skeleton pushed off a cliff walked
/// away and no document recorded an exemption. Bedrock applies fall damage to
/// every mob that is not explicitly excepted, which is why this is the general
/// rule here and the exceptions are named on the rows.
///
/// The exceptions are the reference's own two lists, both in §1.10: six of our
/// species are on its "fully immune" line, and the goat and the frog take ten
/// and five points less. **Subtracted from the damage, not from the distance**,
/// because that is what "takes 10 HP less, always" says - a goat still takes
/// nothing at all under 13 blocks, and above that it pays ten less than a sheep
/// beside it. That is also why the two scale arguments are left at their
/// defaults: `distanceScale` is what a surface leaves of the *fall* and
/// `damageScale` is what it leaves of the *damage*, both multipliers, and a
/// species reduction is neither - it is points off, after the curve.
///
/// Fliers and swimmers never reach here with anything accumulated, because both
/// clear `fallDistance` where they take over `velocity.y`.
void chargeFall(Creature& creature, const CreatureSpecies& species) {
    if (species.ignoresFallDamage) {
        return;
    }
    const int damage = survival::fallDamage(creature.fallDistance) - species.fallDamageReduction;
    if (damage > 0 && damageCreature(creature, damage)) {
        recordHazard(creature, DeathCause::Falling);
    }
}

/// How far a struck animal's own kind hears it. **Ours, not the reference's** -
/// Bedrock gives farm animals no alerting whatsoever, and a herd that scatters
/// together is worth more than fidelity here.
constexpr float kHerdAlertRange = 16.0f;

/// One row per species. Adding an animal is adding a row.
///
/// Heights are Bedrock's collision box throughout, rather than the model's
/// silhouette: a head or a pair of ears is allowed to rise above the box it
/// collides with. **The first four rows - sheep, cow, pig, bramble - used to be
/// a blanket exception that "kept the boxes they were playtested with", and
/// that sentence is now too broad to be useful: today's height sweep moved the
/// sheep and the cow onto the published figures, so the exception is down to
/// two numbers.** Measured against `Mojang/bedrock-samples`
/// `behavior_pack/entities/*.json` on 2026-08-19 and stated per row, so the
/// next reader can see which of the four is still ours:
///
///   - **Sheep** 0.9 x 1.3 published; ours 1.30 tall - agrees.
///   - **Cow** 0.9 x 1.3 published; ours 1.30 tall and 0.90 across - agrees on
///     **both** axes, which no comment here had noticed.
///   - **Mushroom cow** 0.9 x 1.3 published; ours 1.30 and 0.90 - agrees, and
///     it tracks the cow as it must, because two animals that look identical
///     must not stand different heights. The note that had them both at 1.5 was
///     stale by two corrections.
///   - **Pig** 0.9 x 0.9 published (`pig.json`, format 1.26.30, fetched
///     2026-08-19); ours is **1.05** tall. Genuinely ours - Java's pig is 0.9
///     too, so this agrees with neither edition. **Left standing deliberately**:
///     finding 1041 rules that a systematic height change is the playtester's
///     call, not a sweep's.
///   - **Bramble** 0.6 x **1.8** published (`creeper.json`); ours is 0.60 across
///     - the published width exactly - and **1.70** tall, which is *Java's*
///     figure to the digit. So it is not "neither edition" either; it is the
///     other one. Same ruling as the pig: the playtester decides.
///
/// Everything else was measured against that source the same day and 30 of 51
/// already agreed exactly. **Fifteen were corrected**: the twelve humanoid
/// and undead rows carried Java's 1.95 and 1.99 where Bedrock publishes a
/// single 1.9 for all of them - drowned was already right, which is what gave
/// the cluster away - the frog was 0.50 against 0.55, and the pufferfish and
/// tropical fish had both lost their scale factor. To falsify any of this,
/// re-run the dump against that source; a row that disagrees is either a
/// new upstream value or a missing scale factor.
///
/// Half-widths are ours throughout and are deliberately narrower than the
/// reference's - a horse is 0.9 across here against Bedrock's 1.4 - because a
/// wide box catches on doorways and tree trunks far more than it reads as bulk.
///
/// **The three widths finding 10213 flagged were measured on 2026-08-19 and
/// deliberately left alone**, because every one of them is that policy working
/// rather than a value that was missed: sheep 0.64 across against a published
/// 0.9, pufferfish 0.56 against 0.8, Blackbone 0.60 against 0.72
/// (`sheep.json`, `pufferfish.json`, `wither_skeleton.json`). All three are
/// **narrower** than the reference, which is the direction this paragraph
/// promises; a row that came out *wider* would be the bug. The cow at 0.90 and
/// the Bramble at 0.60 are the two that happen to land on the published number
/// exactly, and they are not evidence the policy has been abandoned.
///
/// **Why width is not swept in with height**: it decides what fits through a
/// one-block gap and what the pathfinder will attempt - `narrowestBody()` reads
/// `halfWidth` - so it is a gameplay change, not a fidelity correction, and it
/// wants the playtester rather than a table dump.
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
/// **Health is in health points, never hearts, everywhere in this table.** Two
/// points to a heart, so a row copied off a wiki infobox's heart *picture*
/// rather than its `{{hp|n}}` number comes out at half - and half of a correct
/// number is the one wrong value that never looks wrong. Four rows were found
/// that way on 2026-08-19 by dumping the whole column and reading it against
/// `RESEARCH.md` §6.1 and the wiki infoboxes, which is the only instrument that
/// finds this: every one of them built, validated and played fine.
///
/// **Source order for anything in this table, settled 2026-08-19.** Use
/// `Mojang/bedrock-samples`, `behavior_pack/entities/<mob>.json`, first. That
/// repository is Mojang's own shipped Bedrock behaviour pack - a *primary*
/// source stating `minecraft:health`, `minecraft:attack`,
/// `minecraft:collision_box` and `minecraft:movement` in the engine's own
/// units - and where it covers a field it beats minecraft.wiki outright. The
/// wiki is secondary, documents Java freely, and marks the edition split
/// unevenly; it misled this project three separate times in one night, twice
/// into nearly "correcting" values that were already right.
///
/// Two things to know when reading those files. **A value can live in a
/// `component_group` rather than in `components`**, and the group is usually a
/// state this project does not have - `wolf_tame` publishes a different attack
/// and a different health from the wild wolf, and taking the group's number
/// for the base is how this table ended up with a wild wolf biting like a
/// tamed one. **And `minecraft:health.value` is sometimes a `{range_min,
/// range_max}` object**, which the four living equines all use; this table
/// stores one health per species, so any figure inside the published range is
/// a legitimate pick and only a figure outside it is a defect.
///
/// **And third, the one that nearly cost two correct rows: `collision_box` is
/// stored pre-divided by `minecraft:scale`, so the real box is the product of
/// the two.** Mojang annotate it themselves in `rabbit.json` - `"width":
/// 0.81666666, // 0.49/0.6` beside `"scale": 0.6` - and reading the raw box
/// alone says a rabbit is 1.0 tall when it is 0.6, exactly what this table
/// already had. The same trap sits under blackbone, whose raw 2.01 times its
/// 1.2 scale is the 2.412 that the 2.4 here was taken from; "correcting" it to
/// 2.01 would have shortened it by four-tenths of a metre. Four rows of the
/// fifty-one carry a scale other than 1 - rabbit, blackbone, pufferfish and
/// tropical fish - and a scale is only in force when it sits in the same block
/// as the box. **Search for it with a first-match regex and you will get the
/// wrong one**: `zombie.json` answers 0.5, which belongs to its
/// `minecraft:zombie_baby` group, and believing that would halve every
/// humanoid in this table; rabbit answers 0.4, its baby, where the adult is
/// 0.6. Read the top-level `components` block, or the group the box is in.
/// This is `CLAUDE.md` bug shape #3, a number in a different unit,
/// and nothing about the file's appearance warns you.
///
/// **This note was overridden on the day it was written, and the row it names
/// is the one that broke.** Blackbone was edited to 2.01 anyway, along with
/// pufferfish 0.96 -> 0.80 and tropical fish 0.52 -> 0.40, on the reasoning
/// that a value equal to `box x scale` must be a render scale wrongly
/// multiplied in. All three were reverted 2026-08-19 16:15 after re-fetching
/// the JSON. **The trap is that the arithmetic looks identical from both
/// sides**: `0.96 = 0.8 x 1.2` is exactly what a *correct* effective box looks
/// like, so the product alone can never tell you which way the error runs.
/// Only what the engine consumes can, and `CreatureSpecies::height`'s doc
/// records that measurement. `squid.json` is the shortest proof that Mojang
/// multiply: its baby group carries `"scale": 0.5` beside `"height": 1, //
/// 0.5/0.5`, the division written out in their own file.
///
/// Four rows were corrected against that source on 2026-08-19 - bogged attack
/// 2 to 3, wolf attack 4 to 3, zombie horse health 15 to 25, trader llama
/// health 12 to 15 - after fifty-three entity files were downloaded and their
/// health, attack, hitbox and movement dumped in one pass. Reading finds what
/// looks wrong; a wrong row looks exactly like a right one, and there are
/// fifty-seven of them.
constexpr CreatureSpecies kSpecies[] = {
    // Eight, not six: `{{hp|8}}` on minecraft.wiki/w/Sheep, no edition split,
    // and `RESEARCH.md` §6.1's master table says the same. Six was two points
    // light, which is one whole hit off a wooden sword.
    {.name = "Sheep", .halfWidth = 0.32f, .height = 1.30f, .gaitRate = 6.0f, .gaitSwing = 0.36f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.6f, .runSpeed = 3.4f, .senseRange = 8.0f,
     .maxBlockLight = 15, .weight = 1.0f, .babyChance = 0.05f, .groupSize = 4, .grazes = true,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Cow", .halfWidth = 0.45f, .height = 1.30f, .gaitRate = 5.5f, .gaitSwing = 0.36f,
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
     .ignoresFallDamage = true, .alertRange = kHerdAlertRange},
    // **Ten health and three damage, both from the reference.** Six and one
    // were ours and nothing recorded where they came from: minecraft.wiki/w/Cat
    // publishes `Wild: {{hp|10}} / Tamed: {{hp|20}}` for Bedrock specifically
    // (Java has a flat 10), `RESEARCH.md` §6.1 records the same pair, and the
    // same infobox gives `damage = {{hp|3}} against rabbits and baby turtles
    // only`. We have no taming, so the wild figure is the one that applies.
    // Damage matters more than the health does: a rabbit is 3 health, so one
    // pounce now settles it the way the reference has it, instead of three.
    {.name = "Cat", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.8f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .attackDamage = 3, .maxBlockLight = 15, .weight = 0.8f, .babyChance = 0.25f, .groupSize = 2,
     .hunts = tagMask(CreatureTag::Critter), .retaliates = false, .ignoresFallDamage = true,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Long-legged enough to walk up a full block without jumping, which is the
    // reference's own list: the horse family, the llama and the camel all have
    // a step height of 1 or more and so never hop a ledge.
    //
    // **Thirty-two health, not sixteen** - `{{hp|32|mob=1}}` on
    // minecraft.wiki/w/Camel, and `RESEARCH.md` §6.1 bolds the same number
    // precisely because it is the surprising one. The height beside it (2.375)
    // is off the same infobox and was already right, which is what made the
    // halved health invisible: the row looked sourced.
    {.name = "Camel", .halfWidth = 0.48f, .height = 2.375f, .gaitRate = 3.5f, .gaitSwing = 0.35f,
     .modelScale = 1.00f, .health = 32, .walkSpeed = 1.3f, .runSpeed = 2.8f, .senseRange = 10.0f,
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
    // **The one row in the table whose health the reference randomises.**
    // `minecraft.wiki/w/Llama` publishes `{{hp|15}} to {{hp|30}}`, no edition
    // split - a llama rolls its own maximum on spawn, which is why a wild herd
    // has weak and tough members. This row holds one int, so it takes the
    // published **floor**: 15 health points. It was 12, which is not a figure
    // the reference contains anywhere and is below the weakest llama that can
    // exist (2026-08-19, found by a probe that dumped all 58 rows). What would
    // make this note false: `health` becoming a range the way `attackDamage`
    // already has `attackDamageMax`, at which point this should be 15..30.
    //
    // It has no `attackDamage` and that is correct rather than missing - a
    // llama does not bite, it spits, for {{hp|1}} on Easy and Normal.
    {.name = "Llama", .halfWidth = 0.43f, .height = 1.87f, .gaitRate = 5.5f, .gaitSwing = 0.31f,
     .modelScale = 1.00f, .health = 15, .walkSpeed = 1.6f, .runSpeed = 3.5f, .senseRange = 10.0f,
     .maxBlockLight = 15, .weight = 0.65f, .babyChance = 0.10f, .groupSize = 4, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .alertRange = kHerdAlertRange},
    {.name = "Donkey", .halfWidth = 0.42f, .height = 1.60f, .gaitRate = 5.5f, .gaitSwing = 0.44f,
     .modelScale = 0.87f, .health = 15, .walkSpeed = 1.7f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.65f, .babyChance = 0.20f, .groupSize = 3, .stepHeight = 1.0f,
     .jumpHeight = 0.0f, .avoidsWater = true, .alertRange = kHerdAlertRange},
    {.name = "Goat", .halfWidth = 0.38f, .height = 1.30f, .gaitRate = 8.0f, .gaitSwing = 0.36f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.7f, .runSpeed = 3.8f, .senseRange = 9.0f,
     .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 3,
     .fallDamageReduction = 10,
     .avoidsWater = true, .alertRange = kHerdAlertRange},
    // The gait numbers are zero on purpose for a hopper: its legs are driven by
    // the jump, so there is no cycle to tune. Sat upright the rabbit is tall
    // and narrow, which is what the much smaller scale is paying for.
    //
    // `hopLaunch` is a **speed**, so it is the one number in this table that
    // gravity is measured against: the arc it reaches is `v^2 / 2g`. It was
    // re-derived when `kGravity` was unified with the player's, by
    // `v' = v sqrt(32/26)`, which holds the arc at the 0.34 m `RESEARCH.md`
    // §1.4 records for an ordinary rabbit hop and shortens only the airtime.
    //
    // **Three health, not four** - `{{hp|3}}` on minecraft.wiki/w/Rabbit and
    // `RESEARCH.md` §6.1, no edition split. Four made it the same to kill as a
    // chicken, which it is not: a rabbit is the frailest thing in the roster.
    {.name = "Rabbit", .halfWidth = 0.18f, .height = 0.60f, .hops = true, .hopLaunch = 4.66f,
     .hopGather = 0.18f, .modelScale = 0.48f, .health = 3, .walkSpeed = 2.0f, .runSpeed = 4.2f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.9f, .babyChance = 0.05f, .groupSize = 3,
     .avoids = CreatureTag::Canine | CreatureTag::Ursine, .avoidRange = 8.0f,
     .avoidPlayerRange = 4.0f, .avoidsWater = true, .alertRange = kHerdAlertRange,
     .panicSpeedScale = 1.5f, .sinksInPowderSnow = false},
    // Neutral rather than hostile: it has a bite but no interest in using it
    // until struck. See `CreatureSpecies::attackDamage`. Twenty-five seconds of
    // grudge is the reference's `wolf_angry`, and it is four times what every
    // animal on the roster used to carry.
    //
    // **The bite was 4 and is 3** (2026-08-19). `wolf.json` in
    // `Mojang/bedrock-samples` - the shipped Bedrock behaviour pack, which
    // beats the wiki wherever it covers something - carries **two** attack
    // components: `"damage": 3` in `components`, which is every wild wolf, and
    // `"damage": 4` inside the `minecraft:wolf_tame` group, which is a wolf
    // that belongs to somebody. The 4 was the tamed number sitting in a table
    // that has no taming in it, so a wild pack bit a third harder than the
    // reference - and a pack is `groupSize = 4`. The same file puts tamed
    // health at 40 in `minecraft:wolf_increased_max_health` against the wild 8
    // this row already has, so the wild/tame split is real and this row is
    // unambiguously the wild side of it.
    //
    // What would make this note false: taming arriving, at which point a tamed
    // wolf wants 4 and 40, and this row still wants 3 and 8.
    {.name = "Wolf", .halfWidth = 0.30f, .height = 0.80f, .gaitRate = 9.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 8, .walkSpeed = 1.8f, .runSpeed = 4.2f, .senseRange = 12.0f,
     .attackDamage = 3, .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.10f, .groupSize = 4,
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
    {.name = "Frog", .halfWidth = 0.25f, .height = 0.55f, .hops = true, .hopLaunch = 7.21f,
     .hopGather = 0.55f, .modelScale = 1.00f, .health = 10, .walkSpeed = 1.0f, .runSpeed = 2.2f,
     .senseRange = 7.0f, .maxBlockLight = 15, .weight = 0.8f, .groupSize = 4, .stepHeight = 1.0f,
     .fallDamageReduction = 5,
     .floats = false, .sinks = false, .amphibious = true, .breathesWater = true,
     .alertRange = kHerdAlertRange, .panicSpeedScale = 1.2f},
    {.name = "Fox", .halfWidth = 0.25f, .height = 0.70f, .gaitRate = 11.0f, .gaitSwing = 0.66f,
     .modelScale = 0.93f, .health = 10, .walkSpeed = 1.9f, .runSpeed = 4.0f, .senseRange = 8.0f,
     .attackDamage = 2, .maxBlockLight = 15, .weight = 0.7f, .babyChance = 0.05f, .groupSize = 3,
     .avoids = CreatureTag::Canine | CreatureTag::Ursine, .avoidRange = 10.0f,
     .hunts = CreatureTag::Fowl | CreatureTag::Critter | CreatureTag::Fish, .retaliates = false,
     .avoidsWater = true, .alertRange = kHerdAlertRange, .sinksInPowderSnow = false},
    // Shares the cat's net and model outright, the way the mule shares the
    // horse's - the reference draws them from one rig too.
    {.name = "Ocelot", .halfWidth = 0.24f, .height = 0.70f, .gaitRate = 10.0f, .gaitSwing = 0.32f,
     .modelScale = 1.00f, .health = 10, .walkSpeed = 1.8f, .runSpeed = 3.9f, .senseRange = 9.0f,
     // **Three, off `RESEARCH.md` 7.1's land-quadruped table, the `Ocelot`
     // row** - which also carries the 10 health beside it. This cited a line
     // number until 2026-08-19, and the line it named was a paragraph about the
     // creeper's stats, nowhere near a cat; 7.1 is a table, and a table's rows
     // move while its heading does not. **The claim that went with it - "not
     // the cat's one" - was wrong too**, and in the more expensive direction:
     // the reference gives a cat 3 as well (`damage = {{hp|3}}`,
     // minecraft.wiki/w/Cat), so the cat's row has been corrected to match
     // rather than this one being treated as special. A chicken has 4 health,
     // so either of them settles one in two pounces.
     //
     // **The primary source has no attack component for either animal at all**
     // (2026-08-19). `Mojang/bedrock-samples` is the shipped Bedrock behaviour
     // pack and beats the wiki where it covers something: `ocelot.json` and
     // `cat.json` each contain **zero** `"minecraft:attack"` entries and zero
     // melee-attack behaviours. That is a real zero and not a bad search - the
     // identical scan over `bogged.json`, `wolf.json`, `fox.json` and
     // `panda.json` returns 1, 2, 1 and 2, so the check can still find
     // something when there is something to find. Health is confirmed
     // exactly - wild 10 in both files, tamed 20, which is the 10 this row and
     // the Cat row carry.
     //
     // The 3 is therefore **ours**, off `RESEARCH.md` 7.1, and it is being
     // left: both animals are `hunts`-only with `retaliates = false` and no
     // `hostile`, so this number never reaches the player and decides one
     // thing only - how many pounces a rabbit or a chicken survives. Changing
     // it to 0 would stop cats hunting at all, since the melee attempt returns
     // early on `attackDamage <= 0`.
     .attackDamage = 3, .maxBlockLight = 15, .weight = 0.6f, .babyChance = 0.25f, .groupSize = 2,
     .hunts = tagMask(CreatureTag::Fowl), .retaliates = false, .ignoresFallDamage = true,
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
     .babyChance = 0.10f, .groupSize = 2, .hunts = tagMask(CreatureTag::Vulpine),
     .leashRange = 48.0f, .angerSeconds = 500.0f, .alertRange = 41.0f,
     .immuneToFreezing = true},
    // Neutral like the bear above and tuned like it - the reference gives a
    // provoked panda the same long grudge and the same wide alert - but it was
    // copied without the one field that lets any of it run: `hurtByTargetStart`
    // returns false outright on `attackDamage <= 0`, so a panda could not
    // retaliate and both numbers beside it were dead. Two is `RESEARCH.md`
    // 7.1's own figure; the aggressive variant's six is not modelled, because
    // there is no personality field here to hang it on.
    {.name = "Panda", .halfWidth = 0.45f, .height = 1.25f, .gaitRate = 4.5f, .gaitSwing = 0.54f,
     .modelScale = 1.10f, .health = 20, .walkSpeed = 0.8f, .runSpeed = 1.8f, .senseRange = 8.0f,
     .attackDamage = 2, .maxBlockLight = 15, .weight = 0.35f, .babyChance = 0.05f, .groupSize = 2,
     .avoidsWater = true, .angerSeconds = 500.0f, .alertRange = 41.0f},
    // The three slimes are one animal at three sizes, and the size is the whole
    // design: health is the size squared, damage is the size, and killing one
    // leaves two to four of the next size down. A small one does no damage at
    // all and still hunts you, exactly as the reference has it.
    //
    // They hop rather than walk, and the launch is identical for all three
    // because the reference jumps one block high whatever the size - only the
    // distance covered scales, which falls out of the speed.
    //
    // **A slime is a CUBE, and the largest of each family stopped being one.**
    // `slime.json` publishes `collision_box` width == height at every size -
    // 0.52, 1.04, 2.08 - with **no `minecraft:scale` component anywhere in the
    // file**, so the published box is the effective box and there is no
    // multiplier to argue about. Both Large rows carried `halfWidth = 0.75`,
    // width 1.50, against a height of 2.08, and **the same 0.75 sat in both
    // families**, which is what makes it a copy rather than a per-species
    // decision. Corrected to 1.04 on 2026-08-19 (fetched that day).
    //
    // The render says which side was wrong: the model is an 8 x 8 x 8 outer
    // shell, so at `modelScale` 4.16 it draws 2.08 m across against a 1.50 m
    // box - **0.29 m of visible body outside the box on every side**, and
    // `kAimPadding` is 0.15, so even the padded pick box missed the outer
    // 0.14 m of a flank you can plainly see. Swinging at the edge of a large
    // slime missed.
    //
    // **The counter-hypothesis, ruled on rather than ignored:** 1.50 fits a
    // two-cell corridor where 2.08 needs three, so 0.75 could have been a
    // deliberate pathing concession. Rejected - nothing in the row or its
    // comment said so, the duplication across two families says copy, and the
    // concession would have been incoherent anyway, because `halfWidth` feeds
    // the collision box AND the `path::Agent` from the same field: at 0.75 the
    // body genuinely FIT the two-wide corridor it was pathing down. Raising it
    // moves both together, so a large slime is now 2.08 wide to the world and
    // to the pathfinder alike, and it will refuse a two-wide gap - correctly.
    //
    // **A deliberate 0.02 divergence, so nobody "tidies" it later:**
    // `magma_cube.json` publishes its MEDIUM box as height 1.02 against width
    // 1.04, alone among the six boxes in the two files; every other size in
    // both is an exact cube. Ours stays 1.04 because the medium shares that
    // same 8 x 8 x 8 shell at `modelScale` 2.08 and draws 1.04 tall, so porting
    // 1.02 would put 2 cm of body above its own box - a smaller copy of the
    // bug being fixed above - to gain 2 cm of fidelity to what looks like a
    // typo in the reference. Falsified by Mojang publishing 1.02 for any other
    // size, which would make it a rule rather than a slip.
    {.name = "Small Slime", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 8.0f,
     .hopGather = 0.90f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.5f, .runSpeed = 1.1f,
     .hostile = true, .senseRange = 16.0f, .nocturnal = true, .maxBlockLight = 15, .weight = 0.6f,
     .burnsInDay = false, .avoidsWater = true},
    {.name = "Slime", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 8.0f,
     .hopGather = 0.85f, .modelScale = 2.08f, .health = 4, .walkSpeed = 0.8f, .runSpeed = 1.8f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 2, .nocturnal = true,
     .maxBlockLight = 15, .weight = 0.5f, .burnsInDay = false,
     .splitInto = CreatureKind::SlimeSmall, .splitMin = 2, .splitMax = 4, .avoidsWater = true},
    {.name = "Large Slime", .halfWidth = 1.04f, .height = 2.08f, .hops = true, .hopLaunch = 8.0f,
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
    {.name = "Zombie", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
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
    {.name = "Skeleton", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
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
    //
    // **The shuffle, and it is half a biped's stride on purpose.** `ANIMATION.md`
    // §1 gives villager legs a 40 degree amplitude against the biped's 80 - the
    // reference animates a villager's walk as a short shuffle, not a march. Our
    // whole roster is already halved to 0.70 rad, so it is the *ratio* that
    // ports, not the number: 0.70 / 2 = 0.35. It was 0.62, which is a biped's
    // stride with a rounding error, and a villager strode about its village
    // like a zombie. The same three rows share the rig and the number: this
    // one, `Witch` and `Wandering Trader`. `Zombie Villager` deliberately does
    // **not** - it is a zombie wearing a villager's skin and marches like one.
    {.name = "Villager", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 4.5f, .gaitSwing = 0.35f,
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
    //
    // **The blow is the zombie's, and it used to be a point harder** (2026-08-19,
    // probe over all 58 rows). `minecraft.wiki/w/Husk` gives Easy 2.5, Normal 3,
    // Hard 4.5 with **no edition split**, and opens by saying a husk "functions
    // similarly to zombies, except that they do not burn in sunlight" - the same
    // one difference the paragraph above claims. The row said 4 while `Zombie`
    // two rows down said 3, so the comment and the row disagreed about how many
    // differences there are, and 4 is the reference's *Hard* figure in a table
    // that is Normal everywhere else. Health points, Normal difficulty. What
    // would make this note false: this project growing a difficulty setting, at
    // which point every hostile row needs three numbers, not one.
    {.name = "Husk", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 2.2f, .hostile = true,
     .senseRange = 35.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.9f,
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
     .burnsInDay = false, .mustSee = false, .angerSeconds = 600.0f, .alertRange = 20.0f,
     .sinksInPowderSnow = false},
    // The skeleton's rig at 1.20 scale, which is what takes a 2.00 model to the
    // reference's 2.4 hitbox. Hits far harder than the skeleton and is rarer
    // for it. Also not on §13.4's burn list, and for the same reason as the
    // Bramble: only the ordinary undead catch fire, and staying out past dawn
    // is what makes it the rare one worth being afraid of.
    //
    // **The one skeleton that does swing**: it carries a stone sword in the
    // reference rather than a bow, so melee is its real attack.
    //
    // **The 6 is ours and is deliberately left alone** (2026-08-19). Because the
    // paragraph above cites the reference for the sword, the next reader will
    // look the damage up, so here it is in full: `minecraft.wiki/w/Wither_
    // Skeleton` gives **unarmed** Easy 3 / Normal 4 / Hard 6, and **armed with
    // the stone sword** Java 5/8/12 against **Bedrock 5.5/9/13.5** - one of the
    // handful of places the two editions genuinely differ, so Bedrock's Normal
    // 9 is the faithful figure. Ours sits between unarmed-Hard and armed-Normal.
    // That is a *feel* number on the rarest hostile in the game and the user is
    // the playtester, so it is not being moved from a desk; it is written down
    // so nobody "corrects" it to 9 or to 4 believing either is obviously right.
    // The reference also applies Wither for ten seconds on hit, which this
    // project does not - filed, not silently absorbed into the 6.
    //
    // **Primary source, added 2026-08-19, and it moves the goalposts without
    // moving the number.** Every figure in the paragraph above came from the
    // wiki, which is a secondary source that documents Java freely and marks
    // the split unevenly. `Mojang/bedrock-samples`
    // `behavior_pack/entities/wither_skeleton.json` - the shipped Bedrock
    // behaviour pack - publishes `"minecraft:attack": {"damage": 4,
    // "effect_name": "wither", "effect_duration": 10}`. So Bedrock's stated
    // component is **4**, not the 9 written above; 9 is what the wiki reports
    // *after* the stone sword is counted, and **our engine gives held items no
    // damage at all** - the blow is `species.attackDamage` and nothing else,
    // and `heldMainHand` is drawn, never added. The wither-on-hit clause is
    // confirmed by the same file.
    //
    // The 6 still stands and is still the user's call, but the question is now
    // "6 or 4", not "6 or 9". Note the trap this defuses: a reader who opens
    // that JSON, sees 4, sees `heldMainHand = StoneSword` and assumes the sword
    // makes up the difference would set this to 4 and quietly cut the mob's
    // threat by a third, because in *this* engine the sword contributes
    // nothing. Every other armed mob in this table - Princepin 5, Princepin
    // Brute 7, Zombie Princepin 5 - already carries its JSON figure exactly,
    // so this row is the only one that does not, which is the argument for 4
    // and is written here rather than acted on.
    // **Height 2.412, and it is not the 2.01 the JSON prints.**
    // `wither_skeleton.json` publishes `"minecraft:collision_box": {"height":
    // 2.01, "width": 0.72}` *and*, in the same `components` block,
    // `"minecraft:scale": {"value": 1.2}` - re-fetched from
    // `Mojang/bedrock-samples` 2026-08-19 16:15 to settle this. Bedrock's boxes
    // are stored pre-divided by that scale, so the standing mob is 2.01 x 1.2 =
    // **2.412** m, which is also the figure the wiki's Bedrock infobox prints.
    // This row briefly read 2.01 on 2026-08-19 - the raw box taken without its
    // scale - which shortened a hostile mob by four-tenths of a metre and
    // changed what it fits under, what can hit it and every arrow arc against
    // it. Reverted the same day. The third note above `kSpecies` warned about
    // exactly this row by name, before it happened.
    {.name = "Blackbone", .halfWidth = 0.30f, .height = 2.412f, .gaitRate = 5.5f,
     .gaitSwing = 0.70f, .modelScale = 1.20f, .health = 20, .walkSpeed = 1.1f,
     .runSpeed = 2.6f, .hostile = true, .senseRange = 16.0f, .attackDamage = 6,
     .nocturnal = true, .maxBlockLight = 6,
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
    //
    // **The bogged's melee was 2 and is 3** (2026-08-19). The pair is *not*
    // told apart by damage in the reference: `stray.json` and `bogged.json`
    // both put `"minecraft:attack": {"damage": 3}` in their
    // `minecraft:melee_attack` group, and the bogged's weakness is spelt
    // entirely in its **health**, 16 against the stray's 20 - which this table
    // already had right. The 2 was the plain `skeleton.json` figure, which is
    // genuinely 2, sitting in the wrong row: the stray beside it was read and
    // the bogged was assumed. Source is `Mojang/bedrock-samples`
    // `behavior_pack/entities/`, the shipped behaviour pack, not the wiki.
    {.name = "Stray", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 6.0f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 20, .walkSpeed = 1.2f, .runSpeed = 2.8f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.5f,
     .avoids = tagMask(CreatureTag::Canine), .avoidRange = 6.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f, .shootsArrows = true, .heldMainHand = ItemId::Bow,
     .immuneToFreezing = true},
    {.name = "Bogged", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 6.5f, .gaitSwing = 0.70f,
     .modelScale = 1.00f, .health = 16, .walkSpeed = 1.3f, .runSpeed = 3.0f, .hostile = true,
     .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true, .maxBlockLight = 6, .weight = 0.6f,
     .avoids = tagMask(CreatureTag::Canine), .avoidRange = 6.0f,
     .floats = false, .amphibious = true, .breathesWater = true, .avoidsWater = true,
     .chaseSpeedScale = 1.25f, .shootsArrows = true, .rangedInterval = 3.5f,
     .heldMainHand = ItemId::Bow, .immuneToFreezing = true},
    // The villager's rig - its nets are identical row for row - but posed with
    // the arms held out rather than folded, which is the reference's own
    // distinction between a villager and one that has turned.
    {.name = "Zombie Villager", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f,
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
    {.name = "Witch", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 4.5f, .gaitSwing = 0.35f,
     .modelScale = 0.92f, .health = 26, .walkSpeed = 0.9f, .runSpeed = 1.8f, .hostile = true,
     .senseRange = 10.0f, .attackDamage = 2, .nocturnal = true, .maxBlockLight = 6, .weight = 0.3f,
     .burnsInDay = false, .leashRange = 64.0f, .throwsPotions = true},
    // Passive, rare and found anywhere: the one thing on the roster that is
    // meant to read as a traveller rather than a resident. Trading is a
    // milestone of its own, so for now it only wanders.
    {.name = "Wandering Trader", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 4.5f,
     .gaitSwing = 0.35f, .modelScale = 0.92f, .health = 20, .walkSpeed = 1.0f, .runSpeed = 1.8f,
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
    {.name = "Princepin", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f, .gaitSwing = 0.70f,
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
    // `modelScale` for the drawing, and **also into the height**: the JSON's
    // 0.8 box times its 1.2 scale is the 0.96 below, because the reference
    // stores boxes pre-divided by scale (see the third note above `kSpecies`).
    // 0.60 sat here until 2026-08-19, matching neither figure; 0.80 - the raw
    // box read without its scale - sat here for part of the same day and is the
    // mistake that note names. Re-fetched from `pufferfish.json` 16:15:
    // `collision_box` 0.8, `minecraft:scale` 1.2.
    //
    // **Its collision box does not grow with it.** One species owns one shape
    // here, so a per-stage box would be a change to the shape table rather
    // than to this row. Worth knowing before assuming the reference does
    // otherwise: none of its seven component groups carries a `collision_box`
    // at all, so if it swaps one it is doing it in code and not in data.
    // Nothing collides with a fish but terrain, so the cost is that a puffed
    // one is easier to swim past than it should be.
    {.name = "Pufferfish", .halfWidth = 0.28f, .height = 0.96f, .gaitRate = 0.0f,
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
    // Its height is the JSON's 0.4 box times its `minecraft:scale` of 1.3, so
    // 0.52 - re-fetched from `tropicalfish.json` 2026-08-19 16:15. The 0.40
    // that sat here for part of that day was the stored figure read without the
    // scale, this table's clearest instance of that trap, and this very comment
    // said so while the row below it carried 0.40 anyway. `modelScale` carries
    // the 1.3 as well, which this comment used to forbid. RETRACTED, quoted
    // and not asserted: "`modelScale` stays 1 deliberately" - RETRACTED. That
    // was written because our model is sized from its texture net rather than
    // from the reference's box. It is now 1.30 and it is a
    // *visual* change nobody has judged on screen - the drawn fish is 30%
    // bigger than it was. Flagged for the playtester rather than reverted,
    // since 1.3 is at least consistent with the box and with pufferfish and
    // blackbone beside it. None of its sixty component groups carries a box or
    // a scale, so the pair above is the whole story.
    {.name = "Tropical Fish", .halfWidth = 0.20f, .height = 0.52f, .gaitRate = 0.0f,
     .modelScale = 1.30f, .health = 3, .walkSpeed = 1.8f, .runSpeed = 3.6f, .senseRange = 6.0f,
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
    {.name = "Mushroom Cow", .halfWidth = 0.45f, .height = 1.30f, .gaitRate = 5.5f,
     .gaitSwing = 0.36f, .modelScale = 1.00f, .health = 10, .walkSpeed = 1.3f, .runSpeed = 2.8f,
     .senseRange = 8.0f, .maxBlockLight = 15, .weight = 0.18f, .babyChance = 0.05f,
     .groupSize = 2, .avoidsWater = true, .alertRange = kHerdAlertRange},
    // Both undead horses are `geometry.horse` unchanged. They are **passive**,
    // as the reference has them, and neither burns off - so a pale horse
    // standing in a field at noon is correct rather than a bug.
    //
    // **They do not share a health, and they did** (2026-08-19). The two rows
    // are otherwise identical and the zombie one had been given the skeleton
    // one's 15. `Mojang/bedrock-samples` is unambiguous and the two files
    // disagree with each other on purpose: `skeleton_horse.json` says
    // `"minecraft:health": {"value": 15, "max": 15}` and `zombie_horse.json`
    // says `{"value": 25, "max": 25}`. Neither is a range, unlike the four
    // living equines, so there is no judgement to make here - 25 is simply the
    // number. Player consequence: a zombie horse died in three hits of a stone
    // sword instead of five.
    {.name = "Skeleton Horse", .halfWidth = 0.45f, .height = 1.60f, .gaitRate = 5.0f,
     .gaitSwing = 0.49f, .modelScale = 1.00f, .health = 15, .walkSpeed = 2.0f, .runSpeed = 4.5f,
     .senseRange = 10.0f, .nocturnal = true, .maxBlockLight = 7, .weight = 0.15f,
     .burnsInDay = false, .stepHeight = 1.0f, .jumpHeight = 0.0f, .floats = false,
     .amphibious = true, .breathesWater = true},
    {.name = "Zombie Horse", .halfWidth = 0.45f, .height = 1.60f, .gaitRate = 5.0f,
     .gaitSwing = 0.49f, .modelScale = 1.00f, .health = 25, .walkSpeed = 1.8f, .runSpeed = 4.0f,
     .senseRange = 10.0f, .nocturnal = true, .maxBlockLight = 7, .weight = 0.15f,
     .burnsInDay = false, .stepHeight = 1.0f, .jumpHeight = 0.0f, .floats = false,
     .amphibious = true, .breathesWater = true},
    // The llama's rig with its pack drawn over it. Travels in smaller groups
    // than a wild llama, which is the whole idea of a trader's string.
    //
    // **Fifteen, and this is the second half of a fix that only landed on the
    // first half** (2026-08-19). The wild Llama row carried 12 until earlier
    // today, when it went to 15 because the published health is a 15..30 range
    // and 12 is not in it. `trader_llama.json` in `Mojang/bedrock-samples`
    // carries the **identical** `"minecraft:health": {"value": {"range_min":
    // 15, "range_max": 30}}`, and this row kept the 12 - so the pair was left
    // disagreeing, which is exactly the shape the wild llama had just been
    // fixed out of. Same 12, same source, one of the two edited: `CLAUDE.md`
    // bug shape #5.
    //
    // On the range: this table stores one health per species and the four
    // living equines all share that same 15..30 roll, so any figure inside it
    // is a legitimate pick and 15 (the floor, matching the Llama and Donkey
    // rows) is the one this file uses. Horse and Mule sit at 16, also inside
    // it, also fine. **Twelve was the only one outside it**, which is what made
    // it a defect rather than a preference.
    {.name = "Trader Llama", .halfWidth = 0.43f, .height = 1.87f, .gaitRate = 5.5f,
     .gaitSwing = 0.31f, .modelScale = 1.00f, .health = 15, .walkSpeed = 1.6f, .runSpeed = 3.5f,
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
    {.name = "Princepin Brute", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f,
     .gaitSwing = 0.70f, .modelScale = 1.00f, .health = 50, .walkSpeed = 1.2f, .runSpeed = 2.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 7, .maxBlockLight = 15,
     .weight = 0.12f, .burnsInDay = false, .floats = false, .avoidsWater = true,
     .leashRange = 64.0f, .angerSeconds = 600.0f, .alertRange = 16.0f, .swingsArms = true,
     .heldMainHand = ItemId::StoneAxe},
    {.name = "Zombie Princepin", .halfWidth = 0.28f, .height = 1.90f, .gaitRate = 5.0f,
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
     .weight = 0.5f, .burnsInDay = false, .mustSee = false, .angerSeconds = 600.0f,
     .sinksInPowderSnow = false},
    // Three sizes that split, exactly as the slimes do - the machinery is
    // already there and none of it needed touching. The launch is shared for
    // the same reason theirs is: the reference jumps one block whatever the
    // size, and only the ground covered scales.
    //
    // **`nocturnal` and the light gate are ours, and the row says so.** These
    // three had neither - no `nocturnal`, `maxBlockLight = 15` - while the
    // comment above them claimed "being underground and dark is what actually
    // gates them", which was wrong twice over: `nocturnal == false` means *day
    // only* in the spawner, not "any time", and a ceiling of 15 is a light gate
    // that can never fire. A Large Magma Cube hitting for 6 hopped across a lit
    // mountainside at noon and none appeared at night, the exact reverse of
    // what was written. Nothing here is a reference number, because the
    // reference's magma cube lives in the Nether and spawns at any light level;
    // relocating it to Overworld stone and badlands is ours, so its spawn rule
    // has to be ours too, and 6 is the band every other Overworld hostile on
    // this table already uses.
    //
    // **Boxes: same correction as the slimes, same source.** `magma_cube.json`
    // publishes 0.52, 1.04 and 2.08 with no `minecraft:scale`, so the Large row
    // is a 2.08 cube and its `halfWidth` moved 0.75 -> 1.04 on 2026-08-19. The
    // 0.75 was the same literal the Large Slime carried, in a family that
    // shares nothing else numerically, which is what identified it as a copy.
    // The medium's published height of 1.02 is discussed on the slime rows and
    // deliberately not ported.
    {.name = "Small Magma Cube", .halfWidth = 0.26f, .height = 0.52f, .hops = true, .hopLaunch = 8.0f,
     .hopGather = 0.75f, .modelScale = 1.04f, .health = 1, .walkSpeed = 0.8f, .runSpeed = 1.6f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 3, .nocturnal = true,
     .maxBlockLight = 6, .weight = 0.35f, .burnsInDay = false, .ignoresFallDamage = true,
     .avoidsWater = true, .freezesHarder = true},
    {.name = "Magma Cube", .halfWidth = 0.52f, .height = 1.04f, .hops = true, .hopLaunch = 8.0f,
     .hopGather = 0.70f, .modelScale = 2.08f, .health = 4, .walkSpeed = 1.1f, .runSpeed = 2.2f,
     .hostile = true, .senseRange = 16.0f, .attackDamage = 4, .nocturnal = true,
     .maxBlockLight = 6, .weight = 0.3f, .burnsInDay = false,
     .splitInto = CreatureKind::MagmaCubeSmall,
     .splitMin = 2, .splitMax = 4, .ignoresFallDamage = true, .avoidsWater = true,
     .freezesHarder = true},
    {.name = "Large Magma Cube", .halfWidth = 1.04f, .height = 2.08f, .hops = true,
     .hopLaunch = 8.0f, .hopGather = 0.65f, .modelScale = 4.16f, .health = 16,
     .walkSpeed = 1.5f, .runSpeed = 3.0f, .hostile = true, .senseRange = 16.0f,
     .attackDamage = 6, .nocturnal = true,
     .maxBlockLight = 6, .weight = 0.2f, .burnsInDay = false,
     .splitInto = CreatureKind::MagmaCubeMedium,
     .splitMin = 2, .splitMax = 4, .ignoresFallDamage = true, .avoidsWater = true,
     .freezesHarder = true},
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
     .maxBlockLight = 15, .weight = 0.6f, .groupSize = 3, .pollinates = true, .stepHeight = 0.0f,
     .jumpHeight = 0.0f,
     .ignoresFallDamage = true,
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
     .jumpHeight = 0.0f, .ignoresFallDamage = true,
     .floats = false, .amphibious = true, .breathesWater = true,
     .avoidsWater = true, .angerSeconds = 600.0f, .swingsArms = true, .attackDamageMax = 21,
     .knockbackScale = 3.15f, .knockbackLiftScale = 3.71f},
};

static_assert(std::size(kSpecies) == static_cast<std::size_t>(CreatureKind::Count),
              "every CreatureKind needs a row");

/// **The order is declared load-bearing at the top of the table and only its
/// *length* was ever checked.** `speciesInfo` is a bare index, so swapping two
/// adjacent rows compiles clean at `/W4`, passes the soak, and hands each
/// species the other's health, damage, speed, spawn weight, population cap and
/// despawn rules for good. The rows most at risk are the ones that look alike
/// and sit next to each other by design: Cat and Ocelot differ in four numbers,
/// Stray and Bogged are adjacent, and the three slimes and the three magma
/// cubes are near-identical triples where a swap is invisible from any angle
/// except the health bar.
///
/// Names rather than numbers, because a name is unique across the table and a
/// health value is not - and one anchor per look-alike run rather than all
/// fifty-eight, because the point is to catch a *neighbouring* swap and a run
/// of one has nothing to swap with. `archersShareOneRig` already pins
/// Skeleton/Stray/Bogged from the other end.
///
/// The single edit that makes it fail: exchanging any two rows inside one of
/// the named runs - the two slime lines, say, or Cat and Ocelot.
constexpr bool speciesNamed(CreatureKind kind, std::string_view name) {
    return name == kSpecies[static_cast<std::size_t>(kind)].name;
}
constexpr bool speciesRowsAnchored() {
    return speciesNamed(CreatureKind::Sheep, "Sheep") &&
           speciesNamed(CreatureKind::Cat, "Cat") &&
           speciesNamed(CreatureKind::Ocelot, "Ocelot") &&
           speciesNamed(CreatureKind::Horse, "Horse") &&
           speciesNamed(CreatureKind::Mule, "Mule") &&
           speciesNamed(CreatureKind::Donkey, "Donkey") &&
           speciesNamed(CreatureKind::SlimeSmall, "Small Slime") &&
           speciesNamed(CreatureKind::SlimeMedium, "Slime") &&
           speciesNamed(CreatureKind::SlimeLarge, "Large Slime") &&
           speciesNamed(CreatureKind::Spider, "Spider") &&
           speciesNamed(CreatureKind::CaveSpider, "Cave Spider") &&
           speciesNamed(CreatureKind::Skeleton, "Skeleton") &&
           speciesNamed(CreatureKind::Stray, "Stray") &&
           speciesNamed(CreatureKind::Bogged, "Bogged") &&
           speciesNamed(CreatureKind::Cod, "Cod") &&
           speciesNamed(CreatureKind::Salmon, "Salmon") &&
           speciesNamed(CreatureKind::Turtle, "Turtle") &&
           speciesNamed(CreatureKind::MagmaCubeSmall, "Small Magma Cube") &&
           speciesNamed(CreatureKind::MagmaCubeMedium, "Magma Cube") &&
           speciesNamed(CreatureKind::MagmaCubeLarge, "Large Magma Cube") &&
           speciesNamed(CreatureKind::IronGolem, "Iron Golem");
}
static_assert(speciesRowsAnchored(),
              "kSpecies rows must stay in CreatureKind order - a swap is silent and permanent");

/// And the one relationship in the table that points at another row, which a
/// swap of the slime lines would invert rather than merely scramble: a slime
/// must split into something smaller than itself, or killing a large one
/// produces two larger ones and the population never stops growing.
///
/// The single edit that makes it fail: pointing `SlimeLarge.splitInto` at
/// `SlimeLarge`, or swapping the small and large rows.
static_assert(kSpecies[static_cast<std::size_t>(CreatureKind::SlimeLarge)].splitInto ==
                      CreatureKind::SlimeMedium &&
                  kSpecies[static_cast<std::size_t>(CreatureKind::SlimeMedium)].splitInto ==
                      CreatureKind::SlimeSmall &&
                  kSpecies[static_cast<std::size_t>(CreatureKind::SlimeSmall)].splitInto ==
                      CreatureKind::Count,
              "a slime must split into a smaller slime, and the smallest into nothing");

/// The fall-damage immunity list, pinned by count and by its one trap.
///
/// `RESEARCH.md` §1.10 names exactly eight of our fifty-eight rows as fully
/// immune - the three magma cubes, the bee, the cat, the chicken, the iron
/// golem and the ocelot - and the trap is that **the slime is not on it** while
/// the magma cube is. The two families are otherwise line-for-line copies of
/// each other in this table, so a later editor tidying the pair into agreement
/// would be undoing a real reference difference rather than a typo.
///
/// The single edit that makes it fail: giving any slime row
/// `ignoresFallDamage`, dropping it from a magma cube, or adding a ninth row to
/// the list without a §1.10 line to hang it on.
constexpr int fallImmuneRows() {
    int count = 0;
    for (const CreatureSpecies& species : kSpecies) {
        if (species.ignoresFallDamage) {
            ++count;
        }
    }
    return count;
}
static_assert(fallImmuneRows() == 8, "RESEARCH.md 1.10 lists exactly eight of our rows as immune");
static_assert(kSpecies[static_cast<std::size_t>(CreatureKind::MagmaCubeLarge)].ignoresFallDamage &&
                  !kSpecies[static_cast<std::size_t>(CreatureKind::SlimeLarge)].ignoresFallDamage,
              "a magma cube is immune to falling and a slime is not - that difference is real");

/// The shortest body the roster can put in the world, in metres.
///
/// **Babies count, and every row is measured as though it could have one.**
/// `babyChance` is not asked: a row gaining one later is exactly the edit that
/// would otherwise reopen the hole below without touching anything the compiler
/// can see.
constexpr float shortestBody() {
    float shortest = kSpecies[0].height;
    for (const CreatureSpecies& row : kSpecies) {
        shortest = row.height < shortest ? row.height : shortest;
    }
    return shortest * kBabyScale;
}

/// And the narrowest, which is the horizontal sweep's version of the same
/// question. Written as a separate function rather than one returning both,
/// because the pair "derive one of these and not the other" is its own recorded
/// failure shape and two names is the cheapest way to make the omission visible.
constexpr float narrowestBody() {
    float narrowest = kSpecies[0].halfWidth;
    for (const CreatureSpecies& row : kSpecies) {
        narrowest = row.halfWidth < narrowest ? row.halfWidth : narrowest;
    }
    return narrowest * 2.0f * kBabyScale;
}

/// **The three that keep `step`'s sweep honest**, and they live here rather
/// than beside `kMaxSweepStep` because this is the first point at which the
/// table is in scope.
///
/// Consecutive test boxes in a swept move must overlap or a body passes clean
/// between two of them, which is how creatures fell through the floor for
/// twenty milestones. **Strict, matching `Player.cpp`'s
/// `kMaxStepDistance < kWidth`** - the same non-tunnelling condition written
/// against the same kind of quantity, and equality is a touch rather than an
/// overlap.
///
/// **The single edit that makes the first fail:** adding a species shorter than
/// 0.273 m - a 0.25 m mob, say - or lowering `kBabyScale`. Today's margin is
/// 0.15 against 0.165.
static_assert(kMaxSweepStep < shortestBody(),
              "sweep slices must overlap, or the shortest body falls between two of them");

/// The horizontal half, and it is the one that had no sweep at all until the
/// knockback numbers were held up against the frame clamp: `kKnockbackSpeed`
/// times the iron golem's 3.15 `knockbackScale` is 14.175 m/s, which is 0.709 m
/// inside one clamped frame against a narrowest body of 0.198 m.
///
/// **The single edit that makes it fail:** adding a species narrower than 0.136
/// half-width, or lowering `kBabyScale`.
static_assert(kMaxSweepStep < narrowestBody(),
              "sweep slices must overlap sideways too, or a shove goes through a wall");

/// And the budget, so the slice count stays sane. **The single edit that makes
/// it fail:** raising `kTerminalVelocity`, or loosening the frame clamp, far
/// enough that a clamped fall needs more slices than the sweep was written for.
static_assert(kMaxDeltaSeconds * kTerminalVelocity / kMaxSweepStep <= 32.0f,
              "a clamped fall would need more sweep slices than intended");

/// How many rows stand taller than the unstick pass's shared reach.
///
/// **`Player.cpp` asserts `kUnstickReach >= kHeight` and we cannot mirror it**,
/// which is worth stating rather than quietly omitting. The heights *are*
/// available at compile time - `kSpecies` is `constexpr` and `shortestBody`
/// above already walks it - so the obstacle is not reach into the table. The
/// obstacle is that **the assert would be false**: the shared reach is 2.0 m and
/// five rows exceed it, the iron golem at 2.9 m by nearly half again.
///
/// **So the rule is honoured at runtime instead of dropped.** Their assert is
/// not really about a constant, it is the statement *a body must be able to
/// clear its own volume* - and a fully entombed body lifted by less than its own
/// height never can. `step` therefore lifts by the larger of the shared reach
/// and the body's own height, which satisfies the same rule for all 58 rows and
/// for any future one. This count is what remains: the rows for which that
/// widening is what saves them, and which would silently go back to being
/// unrescuable - now fatally, since suffocation charges what the pass cannot
/// free - if the widening were ever tidied away as redundant.
///
/// **The single edit that makes this fail:** adding a sixth species over 2.0 m
/// tall, or changing `collision::kUnstickReach` in either direction - each of
/// which should be a deliberate act rather than something noticed later in a
/// ravine.
constexpr int bodiesTallerThanUnstickReach() {
    int count = 0;
    for (const CreatureSpecies& row : kSpecies) {
        if (row.height > collision::kUnstickReach) {
            ++count;
        }
    }
    return count;
}
static_assert(bodiesTallerThanUnstickReach() == 5,
              "iron golem, blackbone, camel and the two large cubes out-reach the shared lift");

/// **Blackbone clears `kUnstickReach` by 0.412 m, and for part of 2026-08-19 it
/// cleared it by 0.01.** Its height was edited 2.412 -> 2.01, taking the raw
/// `collision_box` out of `wither_skeleton.json` without the
/// `"minecraft:scale": {"value": 1.2}` published four lines below it, and the
/// margin over the 2.0 m lift collapsed from 0.412 to 0.01. A paragraph here
/// then defended the 0.01 as load-bearing and told readers not to "simplify"
/// it. Both are gone: the row is back to 2.412 = 2.01 x 1.2.
///
/// **What that near-miss is worth keeping.** The count above stayed 5 through
/// the wrong value, so this assert never fired - it cannot see a body that is
/// still tall enough to be counted but is four-tenths of a metre shorter than
/// it should be. An assert on a *count* proves nothing about the values that
/// produced it, which is why the row itself now carries its arithmetic and its
/// source, and why `CreatureSpecies::height`'s own doc states the convention.

/// Whether a spawn attempt for this species is placed **in the water column**
/// rather than on the ground above it.
///
/// **One owner for a rule that was written three times and keyed on the wrong
/// field.** `canSpawnAt` branched on `swims`, and so did both spawners' choice
/// of Y - so `spawnsInWater`, which reads exactly like the field that decides
/// this, was dead on all nine swimmers and the turtle was unspawnable. Its row
/// and `spawnsIn` both say it hatches on beach sand like the reference's; the
/// code asked for three blocks of open water on a beach column, `waterColumnY`
/// returned -1, and every attempt was discarded on both paths. The one shape
/// the slime row already records as paid for: the rule lived in a comment and
/// the comment said the opposite of the code.
///
/// `swims` is now only "how deep" - a body that moves in three dimensions is
/// dropped anywhere in the column, the Drowned walks the seabed at `surface+1`
/// - and `spawnsInWater` alone decides wet or dry.
constexpr bool spawnsInOpenWater(const CreatureSpecies& species) {
    return species.swims && species.spawnsInWater;
}

/// **A swimmer that cannot walk must say where it spawns.** Without this the
/// turtle's fix is one field away from silently becoming a fish's bug: a new
/// row with `swims` and no `spawnsInWater` now takes the *land* branch, so it
/// would be placed on dry ground and suffocate there.
///
/// The two species this deliberately allows through are the turtle and the
/// axolotl, both of which set `walksOnLand`; the turtle picks land and the
/// axolotl picks water, and that is a real choice rather than an omission.
///
/// The single edit that makes it fail: adding a fish - `swims`,
/// `walksOnLand = false` - and forgetting `spawnsInWater = true`.
constexpr bool swimmersSpawnWet() {
    for (const CreatureSpecies& row : kSpecies) {
        if (row.swims && !row.walksOnLand && !row.spawnsInWater) {
            return false;
        }
    }
    return true;
}
static_assert(swimmersSpawnWet(),
              "a species that swims and cannot walk must spawn in water, not on a beach");
static_assert(!spawnsInOpenWater(kSpecies[static_cast<std::size_t>(CreatureKind::Turtle)]) &&
                  kSpecies[static_cast<std::size_t>(CreatureKind::Turtle)].swims,
              "the turtle swims and still hatches ashore, which is the whole point of the split");

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

/// Whether the terrain under a body has actually arrived.
///
/// **One owner for a rule that had one caller and needed two.** Ground a
/// creature cannot see is ground it would fall through - an absent chunk reads
/// as air, so moving a body over one drops it out of the world and it reappears
/// buried when the chunk returns. `step` refused to simulate over an unloaded
/// column and said so in a paragraph; `separate`, a hundred lines below, wrote
/// `position` with no such test, and `bodyOverlapsSolid` over an unloaded
/// column answers "clear" - so a shove could nudge a body into terrain that had
/// not landed yet. Both ask this now.
bool onLoadedColumn(const World& world, const glm::vec3& feet) {
    return world.columnResident(static_cast<int>(std::floor(feet.x)),
                                static_cast<int>(std::floor(feet.z)));
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

    /// Whether it is night where the whole world is concerned - the same flag
    /// the spawner and the daylight burners already run on, passed in rather
    /// than re-derived, because a second answer to "is it dark" is a second
    /// place for it to be wrong.
    ///
    /// One reader today: a bee stops looking for flowers after dark. Bedrock
    /// spells that as `bee.json`'s `shelter_detection` environment sensor,
    /// which sends a bee home when `is_daytime` is false **or** the weather is
    /// precipitation; only the first half is expressible here, because nothing
    /// hands this file the weather.
    bool night;

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

    /// The thing that last hurt it, **resolved on exactly the leash `foe` is
    /// held on** and null otherwise.
    ///
    /// This is the one owner of "can I still fight what hit me". `threatId`
    /// alone answers "who", and answering "and is it still reachable" a second
    /// time somewhere else is how an iron golem spent ten minutes as a statue:
    /// `foe` was dropped past the leash, `threatId` was not, and the producer
    /// re-asserted the unreachable target every tick for the full 600 s.
    ///
    /// **Null does not mean forgiven.** `provokedTimer` and `threatId` both
    /// survive an excursion out of range, so a golem that loses a zombie round
    /// a corner is still angry and re-engages the moment it comes back inside -
    /// what stops is the *targeting*, not the anger.
    const Creature* threat;

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
    /// And a hive a bee has just delivered nectar to, for exactly the same
    /// reason. See `Creatures::takePollinated` for what the drain must do and
    /// why a repeated cell is meaningful rather than a duplicate.
    std::vector<glm::ivec3>& pollinated;
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
    if (ctx.self.threatId != 0) {
        // **Only while it is still there to be fought.** `ctx.threat` is the
        // single owner of that question and applies the same leash `foe` does,
        // so this row cannot assert a quarry that `think` has already given up
        // on - which is precisely how the freeze happened.
        return ctx.threat != nullptr;
    }
    // **A golem you built yourself never turns on you, and there is no timer on
    // that.** Bedrock gives a player-made golem its own `hurt_by_target` row
    // with the player filtered out — it carries no `minecraft:angry` component
    // at all, so this is structural rather than an anger duration set to zero.
    // `threatId == 0` means the player did it.
    if (ctx.self.playerBuilt) {
        return false;
    }
    // A predator that is passive toward people still fights whatever bit it.
    // `retaliates` is the reference simply not giving a cat a `hurt_by_target`
    // goal - it kills rabbits and it runs from you, so "can it bite" and "will
    // it bite *you*" had to stop being one question.
    return ctx.species.retaliates;
}

void hurtByTargetTick(const BehaviourContext& ctx) {
    // **The bootstrap that used to force this row to write blind is gone.**
    // `ctx.foe` is resolved from the *previous* tick's `targetId`, so it is
    // always null on the first tick after a blow, and a producer keyed on it
    // would never set the slot that makes it resolve. `ctx.threat` is resolved
    // from `threatId` in the same pass, so it is non-null immediately - the
    // attacker is standing right there, it just swung - and the row can be
    // honest about reachability without refusing to start a fight.
    if (ctx.threat != nullptr) {
        ctx.self.target = CreatureTarget::Creature;
        ctx.self.targetId = ctx.threat->id;
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
    if (ctx.self.target == CreatureTarget::None || ctx.species.shootsArrows ||
        ctx.species.throwsPotions) {
        return false;
    }
    // **A row that cannot act must not claim a controller**, because
    // `runBehaviours` claims *before* it ticks. The target slot names a
    // creature that `think` could not resolve - dead, despawned, or past the
    // leash - so `meleeAttackTick` would take `ControlMove | ControlLook`,
    // return on its first line and leave the animal standing still for as long
    // as the producer keeps re-asserting the target. `HurtByTarget` re-asserts
    // it every tick for `species.angerSeconds`, which is **six hundred seconds**
    // for an iron golem: one shot from out of reach froze it mid-village while
    // zombies walked past. Refusing here hands the controllers to `Wander` and
    // `Panic`, and the moment the quarry is resolvable again this row takes
    // them straight back.
    return !(ctx.self.target == CreatureTarget::Creature && ctx.foe == nullptr);
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
        // Belt only: `meleeAttackStart` refuses this case outright now, so the
        // row never claims the two controllers it would then sit on. Kept
        // because the two would otherwise have to agree by inspection, and this
        // is the branch that would read `ctx.foe` through a null if they ever
        // stopped.
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
                                     eyeHeight(ctx.species, self.scale),
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
    //
    // **`heldMainHand` contributes nothing here and that is deliberate**
    // (2026-08-19). Four species carry a weapon - Blackbone and Princepin a
    // stone sword, Princepin Brute an axe, Zombie Princepin a sword - and the
    // field is read only by the model builder, so the item is drawn in the fist
    // and never enters this sum. `attackDamage` is therefore the **whole** blow
    // a species lands, not a base that equipment is added to.
    //
    // This matters when checking a row against `Mojang/bedrock-samples`,
    // because the reference works the other way round: there,
    // `minecraft:attack.damage` is a base and a held weapon really does raise
    // it, which is why the wiki's published figure for an armed mob is larger
    // than the number in its JSON. Reading our 6 beside `wither_skeleton.json`'s
    // 4 and concluding the 2 must be the sword would be wrong twice over - the
    // sword is worth nothing here, and setting the row to 4 would just make the
    // mob weaker. What would make this note false: a weapon-damage term
    // arriving in this function, at which point every armed row wants revisiting
    // together.
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
/// The pig-family torso, in texels across the side axis. **Named because the
/// `static_assert` beside the arms measures against it** - the arm hangs flush
/// on the torso's side, so the two cannot be edited apart.
constexpr float kPigTorsoWidth = 8.0f;
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

/// A point on that folded assembly, carried rigidly round its own pivot.
///
/// **One owner for the rotation.** `villagerRig` hangs three boxes on it and
/// the witch's bottle leaves from it, and the two used to turn the arm in two
/// separate places - which is how the thrown potion ended up leaving from the
/// *unturned* hand, about 0.38 blocks from where the mesh was drawing it.
///
/// A positive `turn` carries a point below the pivot **forward and up**, which
/// is the opposite sense to `uprightBox`'s `pitch`, whose positive direction
/// leans a limb's far end backwards. Any caller turning the assembly therefore
/// pairs `turn` with `-turn` of pitch, and that pairing is the whole of what
/// keeps the sleeve from coming apart.
ModelPoint foldedArmPoint(const ModelPoint& rest, float turn) {
    const float dy = rest.up - kVillagerArmPivotUp;
    return {rest.alongForward * std::cos(turn) - dy * std::sin(turn),
            kVillagerArmPivotUp + rest.alongForward * std::sin(turn) + dy * std::cos(turn),
            rest.alongSide};
}

/// `armRaise` is the wind-up the rig is drawing, so this is the hands where
/// they *are* rather than where they rest. At zero it reduces exactly to the
/// resting fold, which is what leaves a villager and a trader untouched.
ModelPoint villagerHandPoint(float armRaise = 0.0f) {
    const float reach = kVillagerArmLength * kTexel;
    const ModelPoint rest{-std::sin(kVillagerFold) * reach,
                          kVillagerArmPivotUp - std::cos(kVillagerFold) * reach, 0.0f};
    return foldedArmPoint(rest, armRaise);
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

/// How far her arms are up **right now**, which both the rig and the throw need
/// and neither may work out for itself. `aim` is the eased 0-to-1 the archer's
/// bow rides on, so the two wind-ups run off one clock.
float witchArmRaise(const Creature& self) { return self.aim * kWitchArmRaise; }

/// The row in `kPotions` carrying an effect at a given strength.
///
/// **Scanned rather than written down.** The forty-one brews are a table in
/// `Item.hpp` and their indices are an implementation detail of it; a literal
/// 23 here would be a second copy of that ordering, and inserting one brew
/// would silently turn every witch into a thrower of something else.
///
/// **-1 on a miss, not 0.** It returned 0, which is a plausible answer and
/// therefore the worst kind: `kPotions[0]` is a *water bottle*, so deleting or
/// re-amplifying the Slowness row would have handed the witch a splash bottle
/// of water - she winds up, throws, and nothing whatsoever happens - with every
/// assert below still green, because `isSplashPotion` is true of row 0 exactly
/// as it is of row 23. `potionAt(-1, 1)` lands one below `SplashPotionFirst`,
/// which `isSplashPotion` rejects, so the miss now fails the build instead.
constexpr int potionRow(effects::Effect effect, int amplifier) {
    for (std::size_t i = 0; i < kPotions.size(); ++i) {
        if (kPotions[i].effect == effect && kPotions[i].amplifier == amplifier &&
            kPotions[i].second == effects::Effect::None) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The splash form of a brew, which is the only form a witch ever holds.
constexpr ItemId witchBrew(effects::Effect effect) {
    return potionAt(potionRow(effect, 0), 1);
}

/// **The single edit that makes these fail:** deleting
/// `{effects::Effect::Slowness, 0, 90.0f}` from `kPotions`, or changing its
/// amplifier. Both asserts used to survive that - the first because a water
/// bottle is a splash potion too, the second because it only checked two of the
/// four brews and Slowness was one of the two it left out, which is the one she
/// actually throws at range.
static_assert(isSplashPotion(witchBrew(effects::Effect::Slowness)) &&
                  isSplashPotion(witchBrew(effects::Effect::Poison)) &&
                  isSplashPotion(witchBrew(effects::Effect::Weakness)) &&
                  isSplashPotion(witchBrew(effects::Effect::InstantDamage)),
              "a witch must be holding a splash potion, not a bottle it would drink");
static_assert(potionKind(witchBrew(effects::Effect::Slowness)).effect ==
                      effects::Effect::Slowness &&
                  potionKind(witchBrew(effects::Effect::Poison)).effect ==
                      effects::Effect::Poison &&
                  potionKind(witchBrew(effects::Effect::Weakness)).effect ==
                      effects::Effect::Weakness &&
                  potionKind(witchBrew(effects::Effect::InstantDamage)).effect ==
                      effects::Effect::InstantDamage,
              "the scan must find the brew it was asked for - all four of them");

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
///
/// **The distance is passed in rather than read off the context**, because the
/// context's is the distance to the *player* and a witch can be angered by
/// something that is not one - an iron golem does it in any village she walks
/// into. The caller has already resolved who is being thrown at, and this is
/// the last place in the ranged path that could have disagreed with it.
ItemId witchBrewFor(float targetDistance) {
    if (targetDistance >= kWitchSlownessRange) {
        return witchBrew(effects::Effect::Slowness);
    }
    return witchBrew(effects::Effect::Poison);
}

bool rangedAttackStart(const BehaviourContext& ctx) {
    if (!(ctx.species.shootsArrows || ctx.species.throwsPotions) ||
        ctx.self.target == CreatureTarget::None) {
        return false;
    }
    // **The same rule `meleeAttackStart` keeps, and it was missing here.** A
    // row that cannot act must not claim a controller: this one takes
    // `ControlMove | ControlLook`, so a skeleton holding an unresolvable
    // creature target would stand drawing a bow at nothing for the whole of
    // `angerSeconds`. The `HurtByTarget` freeze reached this row too - it was
    // only ever reported against melee because a skeleton is excluded there.
    return !(ctx.self.target == CreatureTarget::Creature && ctx.foe == nullptr);
}

/// Lets the shot go. Split out of the tick because the tick decides *whether*
/// and this decides *where*, and a committed release has to be able to run it
/// from a branch that has already stopped thinking about anything else.
///
/// `aimAt` is a world point rather than a bearing, because the lead below is
/// applied to the *flat distance* and needs the real geometry to do it. The
/// caller resolves it, so this function never asks who is being shot at - the
/// same contract `meleeAttackTick` keeps.
void loose(const BehaviourContext& ctx, bool thrower, const glm::vec3& aimAt) {
    Creature& self = ctx.self;

    // Where the shot leaves, and it is **the hand holding the weapon** rather
    // than the middle of the chest it used to be. Both points come from the
    // same two functions the mesh hangs the item on, so the arrow can only ever
    // leave the bow and the bottle can only ever leave the fingers.
    //
    // The witch's hand is turned by her wind-up, and this used to ask for the
    // resting one: the mesh drew the bottle on a raised arm and the throw left
    // from where her hands hang, 0.38 blocks apart at full raise.
    const glm::vec3 from =
        thrower ? modelPointToWorld(self, ctx.species, villagerHandPoint(witchArmRaise(self)))
                : modelPointToWorld(self, ctx.species,
                                    bipedHandPoint(kArcherLimbWidth,
                                                   kArcherArmPitch + self.headPitch,
                                                   kArcherBowArmRoll, true));

    glm::vec3 aim = aimAt - from;
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
        // **Named rather than left to default.** `fromId` is who the hit is
        // blamed on, and a bottle or an arrow that arrives anonymous is blamed
        // on the player - which is how a skeleton's stray shot used to make an
        // enemy of you out of whatever it clipped.
        ctx.launches.push_back({from, aim * kWitchThrowPower,
                                Creatures::LaunchKind::SplashPotion, self.heldOverride, self.id});
        // Out of its hands the instant it leaves them.
        self.heldOverride = ItemId::None;
        return;
    }
    ctx.launches.push_back(
        {from, aim * kArcherPower, Creatures::LaunchKind::Arrow, ItemId::None, self.id});
}

void rangedAttackTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;

    // **Everything below is measured against whatever is being shot at**, and
    // until now none of it was: every bearing, the closing distance, the
    // backing-off heading and the shot itself all read `ctx.*Player`
    // unconditionally, so a skeleton that a zombie had angered raised its bow,
    // walked toward *you* and put the arrow in *you*. Resolved into one set of
    // locals exactly as `meleeAttackTick` does, because the whole point of the
    // target slot is that a consumer never asks who filled it.
    const bool onCreature = self.target == CreatureTarget::Creature && ctx.foe != nullptr;
    const glm::vec3 toTarget = onCreature ? ctx.toFoe : ctx.toPlayer;
    const float distance = onCreature ? ctx.foeDistance : ctx.distance;
    const float yawToTarget = onCreature ? ctx.yawToFoe : ctx.yawToPlayer;

    // A bottle is aimed **below** the eye and an arrow above the waist: the one
    // is meant to burst at your feet and the other to hit your chest. Against a
    // creature the same two fractions are taken of *its* height, so a shot at a
    // chicken is not aimed at the air above it.
    const bool thrower = ctx.species.throwsPotions;
    const float targetEye =
        onCreature ? eyeHeight(speciesInfo(ctx.foe->kind), ctx.foe->scale)
                   : player_constants::kEyeHeight;
    const float aimHeight = thrower ? std::max(targetEye - kWitchAimDrop, 0.0f)
                                    : targetEye * 0.66f;
    const glm::vec3 aimAt = self.position + toTarget + glm::vec3{0.0f, aimHeight, 0.0f};

    // Negated because a positive head pitch is nose-down, so something standing
    // above wants a negative one. The player's copy of this lives in `think`;
    // this is the same expression against the resolved target. **Both eyes
    // through `eyeHeight`**: the target's, one block above, always scaled - and
    // the shooter's own, here, which did not until 2026-08-19, so the two
    // halves of a single expression disagreed and a cub aimed from an adult's
    // head.
    const float pitchToTarget =
        onCreature ? -std::atan2(toTarget.y + targetEye - eyeHeight(ctx.species, self.scale),
                                 std::max(distance, 0.001f))
                   : ctx.pitchToPlayer;

    self.targetHeadYaw = yawToTarget;
    self.targetHeadPitch = pitchToTarget;
    self.targetYaw = yawToTarget;
    self.speedScale = ctx.species.chaseSpeedScale;

    // One behaviour, two weapons. They engage at different distances and throw
    // different things, and everything between those two facts - closing,
    // backing off, reloading, leading the shot, the spread - is identical, so
    // it is written once. A second row in the table would have been a second
    // copy of all of it.
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
        loose(ctx, thrower, aimAt);
        return;
    }

    if (distance > preferred) {
        self.running = true;
        walkTo(ctx, self.position + toTarget);
    } else if (distance < tooClose) {
        // Backing away is a bearing rather than a place, like panic: there is
        // nowhere in particular it wants to be, only somewhere further off.
        self.running = true;
        walkToward(ctx, yawToTarget + kPi);
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
    const float offAim = std::abs(std::remainder(yawToTarget - self.yaw, kTwoPi));
    const bool retreating = distance < tooClose;
    if (distance > range || offAim > kArcherFov || retreating) {
        return;
    }
    // **The sight test is the one thing that is not free, so it is asked last
    // and only here** - once per `rangedInterval` at the moment of committing,
    // rather than every tick. `ctx.seesPlayer` is a cached answer `think`
    // maintains for the player alone; a creature target has no such cache, and
    // a remembered quarry is not re-filtered for sight, so this is the only
    // place the question can be asked about one at all.
    if (onCreature) {
        const glm::vec3 eye = eyeOf(self.position, ctx.species, self.scale);
        if (!hasLineOfSight(ctx.world, eye, aimAt)) {
            return;
        }
    } else if (!ctx.seesPlayer) {
        return;
    }

    // Committed. From here the release runs to its end on its own.
    //
    // The bottle is chosen **here**, at the moment she reaches for it, and held
    // until it leaves her hand - so what she is visibly holding is what arrives.
    // It has to be this side of the commit rather than during the release,
    // because the release branch above does nothing but count down and throw.
    if (thrower) {
        self.heldOverride = witchBrewFor(distance);
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

/// The middle of a cell, on all three axes.
///
/// **Deliberately not `cellCentre`**, which the villager section defines and
/// which leaves `y` on the block's bottom face because a villager stands *on*
/// its bed. A bee flies *to* the middle of a flower, so the two are different
/// questions and share no answer.
glm::vec3 blockCentre(const glm::ivec3& cell) {
    return {static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
            static_cast<float>(cell.z) + 0.5f};
}

/// Nothing, on the `y < 0` convention `hiveCell`, `flowerCell` and the three
/// village cells all share. Spelled once so that clearing one is a named thing
/// rather than a literal repeated at five sites.
constexpr glm::ivec3 kNoCell{0, -1, 0};

/// The nearest flower to a point, inside a small box, and a bee's whole reason
/// for leaving the hive. Returns false when there is none, which is the common
/// case and costs the same scan either way.
bool nearestFlower(const World& world, const glm::vec3& from, glm::ivec3& found) {
    const int cx = static_cast<int>(std::floor(from.x));
    const int cy = static_cast<int>(std::floor(from.y));
    const int cz = static_cast<int>(std::floor(from.z));
    float best = 0.0f;
    bool any = false;
    for (int dy = -kFlowerSearchY; dy <= kFlowerSearchY; ++dy) {
        for (int dz = -kFlowerSearchXZ; dz <= kFlowerSearchXZ; ++dz) {
            for (int dx = -kFlowerSearchXZ; dx <= kFlowerSearchXZ; ++dx) {
                const glm::ivec3 cell{cx + dx, cy + dy, cz + dz};
                if (!isFlower(world.blockAt(cell.x, cell.y, cell.z))) {
                    continue;
                }
                const glm::vec3 away = blockCentre(cell) - from;
                const float distance = glm::dot(away, away);
                if (!any || distance < best) {
                    best = distance;
                    found = cell;
                    any = true;
                }
            }
        }
    }
    return any;
}

/// The nearest hive or nest, and the other half of a bee's day.
///
/// **Its own function rather than a predicate parameter on `nearestFlower`**,
/// because the two differ in more than what they are looking for: this box is
/// `find_hive`'s 16 by 10 against the flower's 6 by 4, fifteen times the
/// volume, which is the whole reason one runs on the wander cadence and this
/// one only runs when a bee has no home at all.
bool nearestHive(const World& world, const glm::vec3& from, glm::ivec3& found) {
    const int cx = static_cast<int>(std::floor(from.x));
    const int cy = static_cast<int>(std::floor(from.y));
    const int cz = static_cast<int>(std::floor(from.z));
    float best = 0.0f;
    bool any = false;
    for (int dy = -kHiveSearchY; dy <= kHiveSearchY; ++dy) {
        for (int dz = -kHiveSearchXZ; dz <= kHiveSearchXZ; ++dz) {
            for (int dx = -kHiveSearchXZ; dx <= kHiveSearchXZ; ++dx) {
                const glm::ivec3 cell{cx + dx, cy + dy, cz + dz};
                if (!isBeehive(world.blockAt(cell.x, cell.y, cell.z))) {
                    continue;
                }
                const glm::vec3 away = blockCentre(cell) - from;
                const float distance = glm::dot(away, away);
                if (!any || distance < best) {
                    best = distance;
                    found = cell;
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

        glm::ivec3 flower{0, -1, 0};
        if (nearestFlower(ctx.world, self.position, flower)) {
            self.targetYaw = yawTo(self, blockCentre(flower));
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
    // **Clearance is a collision question, and both probes were asking
    // `isSolid`.** `isSolid` reports a cell *occupied*, and it says yes for
    // `BlockShape::Flat` - a rail, a redstone line, a tripwire - none of which
    // has any collision box at all, so a bird would not climb over a powered
    // rail and read a track on the ground as its floor a block early. One
    // lambda for both, because a floor rule and a ceiling rule that disagree is
    // this project's most expensive bug shape. A lantern still counts: its box
    // is small but real, so the half of the finding about hanging blocks was
    // already right.
    const auto blocked = [&ctx](int x, int y, int z) {
        return worldCollisionBoxes(ctx.world, x, y, z).count != 0;
    };
    int clearance = 0;
    while (clearance < kFlyCeilingBlocks && !blocked(hx, feet - 1 - clearance, hz)) {
        ++clearance;
    }
    if (clearance < kFlyFloorBlocks) {
        self.targetPitch = -kFlyPitchMax;
    } else if (clearance >= kFlyCeilingBlocks) {
        self.targetPitch = std::max(self.targetPitch, 0.0f);
    }

    // And anything directly overhead, which the horizontal fan cannot see.
    const float top = self.position.y + ctx.species.height * self.scale;
    if (blocked(hx, static_cast<int>(std::floor(top + kFlyProbe)), hz)) {
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
    // **Deliberately has no `dormant` guard, and must never grow one.** Every
    // other scan of the population in this file skips a dormant record, because
    // one is ninety metres away and out of play - but a claim is not a thing a
    // creature is *doing*, it is a thing it *owns*, and this loop is the only
    // place that ownership is written down. Skip the sleeping villagers of a
    // village the player walked away from and their beds, job sites and bells
    // all read as free: come back and a second villager takes the bed you
    // already had one in, and the village ends up with two armourers. That has
    // been shipped twice already. The distance argument that makes the other
    // guards safe says nothing here, because the question is not "can it reach
    // me" but "does it still hold this cell", and the answer is yes from any
    // distance.
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

// ---------------------------------------------------------------------------
// The bee's day
// ---------------------------------------------------------------------------

/// Where home is and which flower is being worked, kept up to date.
///
/// **A producer claiming no controller, exactly like `ClaimPoi`**, and for the
/// identical reason: remembering a cell is not a way of moving, so it must not
/// be able to crowd out - or be crowded out by - anything that is. It is also
/// what keeps a bee that cannot find a hive from freezing: the search runs
/// here, the flying stays with `FlyWander`.
///
/// **This is where every scan a bee does is paid for, and both are gated.** The
/// hive box is fifteen times the flower box's volume, so it only runs while
/// there is no home at all, on the reference's own retry cadence; the flower
/// box only runs while there is a home to carry nectar to and daylight to work
/// in. A bee that has settled into a nest costs one `blockAt` a tick.
bool beeScanStart(const BehaviourContext& ctx) {
    return ctx.species.pollinates;
}

void beeScanTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;

    // Home first, and **forgetting comes before finding**. A hive that has been
    // mined, burnt or blown up stops being one, and a bee steering at a cell
    // that no longer holds a hive would neither arrive nor give up. One lookup,
    // so it is checked every tick rather than on the search cadence.
    if (self.hiveCell.y >= 0 &&
        !isBeehive(ctx.world.blockAt(self.hiveCell.x, self.hiveCell.y, self.hiveCell.z))) {
        self.hiveCell = kNoCell;
    }
    if (self.hiveCell.y < 0) {
        self.hiveSearchTimer -= ctx.deltaSeconds;
        if (self.hiveSearchTimer <= 0.0f) {
            self.hiveSearchTimer = kHiveSearchMin + nextRandom(ctx.random) * kHiveSearchSpan;
            glm::ivec3 hive{kNoCell};
            if (nearestHive(ctx.world, self.position, hive)) {
                self.hiveCell = hive;
            }
        }
    }

    // Then the flower, on the same two rules in the same order.
    if (self.flowerCell.y >= 0 &&
        !isFlower(ctx.world.blockAt(self.flowerCell.x, self.flowerCell.y, self.flowerCell.z))) {
        self.flowerCell = kNoCell;
        // Losing the flower loses the visit with it, so a bee that arrives at
        // the next one starts its stay from the top rather than finishing a
        // countdown it began somewhere else.
        self.pollinateTimer = 0.0f;
    }
    // Three ways to have no use for a flower: already carrying nectar, nowhere
    // to put it, or after dark. The last is `bee.json`'s `shelter_detection`,
    // which sends a bee home when `is_daytime` is false - its other half tests
    // for precipitation, and nothing hands this file the weather.
    if (self.hasNectar || self.hiveCell.y < 0 || ctx.night || self.flowerCell.y >= 0) {
        return;
    }
    self.flowerSearchTimer -= ctx.deltaSeconds;
    if (self.flowerSearchTimer > 0.0f) {
        return;
    }
    // The wander cadence, because this is the scan `FlyWander` already runs at
    // that rate and one box should not be affordable at two different prices.
    self.flowerSearchTimer = kFlyDecisionMin + nextRandom(ctx.random) * kFlyDecisionSpan;
    glm::ivec3 flower{kNoCell};
    if (nearestFlower(ctx.world, self.position, flower)) {
        self.flowerCell = flower;
    }
}

/// The round trip: out to a flower, twenty seconds on it, home with the nectar.
/// **The only thing in the game that puts honey in a hive**, and therefore the
/// only source of honeycomb, the honeycomb block, candles and every waxed
/// copper stage.
///
/// One row where Bedrock has four goals - `look_for_food`, `go_home`,
/// `find_hive` and the `has_nectar` component group. They are a state machine
/// with one bit of state between them, and writing it as one behaviour is the
/// same choice `Work` makes against the villager's several: our table arbitrates
/// by controller, so four rows that can never run together would buy nothing and
/// would give the bit four owners.
///
/// **Claims both controllers**, like `EatBlock`: a bee settled on a flower is
/// neither wandering nor looking anywhere else.
///
/// Two of the reference's numbers are deliberately not ported.
/// `look_for_food`'s `"start_chance": 0.5` is a coin flip per `tick_interval`,
/// and `tick_interval` is 1 - so it starts within a tick or two either way, and
/// rolling it here would only add jitter to a decision that is already made.
/// `find_hive`'s `"goal_radius": 0.633` is likewise replaced by `go_home`'s
/// 1.2, which is the reference's own arrival distance for the higher-priority
/// path home and is reachable by something that steers on a heading.
bool pollinateStart(const BehaviourContext& ctx) {
    const Creature& self = ctx.self;
    if (!ctx.species.pollinates || self.hiveCell.y < 0) {
        return false;
    }
    // Carrying nectar, the only thing that matters is getting home - and that
    // runs after dark, because a bee heading for shelter is what the reference
    // does at nightfall rather than what it stops doing.
    if (self.hasNectar) {
        return true;
    }
    return !ctx.night && self.flowerCell.y >= 0;
}

void pollinateTick(const BehaviourContext& ctx) {
    Creature& self = ctx.self;

    if (self.hasNectar) {
        const glm::vec3 home = blockCentre(self.hiveCell);
        if (glm::distance(self.position, home) > kHiveArrival) {
            if (!walkTo(ctx, home)) {
                self.walking = false;
                self.route.clear();
            }
            return;
        }

        // Home. **Reported rather than done**: only the main thread may write to
        // the world, so the cell is queued and `Main.cpp` raises the level.
        //
        // Bedrock increments as the bee *exits* again, after converting the
        // nectar inside; we have no inside, so it lands on arrival. That is a
        // difference of one hive-conversion delay and nothing else - the number
        // of levels a delivery is worth is identical.
        //
        // **Twice for the 1% is the whole of the double-honey rule**, and it is
        // rolled here because this is where the random stream is. The drain
        // adds one level per entry, so two entries are two levels; see
        // `Creatures::takePollinated` for why that list must never be
        // de-duplicated.
        ctx.pollinated.push_back(self.hiveCell);
        if (nextRandom(ctx.random) < kDoubleHoneyChance) {
            ctx.pollinated.push_back(self.hiveCell);
        }
        self.hasNectar = false;
        self.walking = false;
        self.route.clear();
        return;
    }

    // No nectar, so this is the outward half. `beeScanTick` owns which flower.
    const glm::vec3 flower = blockCentre(self.flowerCell);
    if (glm::distance(self.position, flower) > kFlowerArrival) {
        if (!walkTo(ctx, flower)) {
            self.walking = false;
            self.route.clear();
        }
        // Arriving is what starts the stay, so a bee crossing a meadow does not
        // bank the flight time.
        self.pollinateTimer = kPollinateSeconds;
        return;
    }

    // Settled. Said outright because `walking` is sticky - the same trap the
    // grazing sheep, the swelling Bramble and the arriving zombie all paid for.
    self.walking = false;
    self.route.clear();
    self.targetHeadYaw = yawTo(self, flower);

    self.pollinateTimer -= ctx.deltaSeconds;
    if (self.pollinateTimer > 0.0f) {
        return;
    }
    self.pollinateTimer = 0.0f;
    self.hasNectar = true;
    self.flowerCell = kNoCell;
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
    // The same shape for the same reason: remembering where home and the
    // flower are is not a way of moving, and a bee that cannot find a hive must
    // still be able to fly while it looks.
    {"BeeScan", 2, 0, beeScanStart, beeScanTick},
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
    // **Must stay above `FlyWander`.** Both want the movement controller and
    // both are priority 5, so the array order alone decides - and below it a
    // bee would cruise instead of working, which is the whole honey chain gone
    // with the priorities still reading as sorted and nothing warning.
    {"Pollinate", 5, ControlMove | ControlLook, pollinateStart, pollinateTick},
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

/// Where a row sits in the table, by name. `std::size(kBehaviours)` when there
/// is no such row, which is itself the failure a rename should produce.
constexpr std::size_t behaviourIndex(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kBehaviours); ++i) {
        if (name == kBehaviours[i].name) {
            return i;
        }
    }
    return std::size(kBehaviours);
}

/// **The half `behavioursSorted` cannot see.** That one proves priorities are
/// non-decreasing, and the single edit that makes it fail is swapping `Wander`
/// (6) and `LookAtPlayer` (7) - but not one of the four invariants this table
/// actually leans on is a priority. All four are *ties*, where the array order
/// alone decides, and every one of them was written down in a comment and
/// nowhere else. Swap any pair below and the priorities are still sorted, the
/// old assert still passes, and the behaviour is silently wrong.
///
/// The single edit that makes each line fail, in order:
///   1. Move `NearestPreyTarget` below `NearestAttackableTarget` - both claim
///      nothing so both run, and the later writer wins, so a wolf eating a
///      sheep would stop turning on the player who walked up to it.
///   2. Move `Swell` above `NearestAttackableTarget` - `Swell` reads the target
///      slot that row writes, so a creeper would swell against last tick's
///      answer and hesitate a frame every time it acquired you.
///   3. Move `Sleep` below `Avoid` - both want the same two controllers, and
///      the reference gives `sleep` priority 3 against `avoid_mob_type` 4, so a
///      villager in bed would leap up at the zombie outside the window.
///   4. Move `RangedAttack` below `MeleeAttack` - both want the same two
///      controllers, so an archer would close to bite instead of keeping its
///      distance.
///   5. Give `Wander` priority 5 to match `Work` and `Mingle` and leave it
///      where it is - `behavioursSorted` is still satisfied, and a villager
///      strolls straight through its own working day.
///   6. Move `Pollinate` below `FlyWander` - both claim the movement
///      controller at priority 5, so a bee would cruise instead of working, no
///      honey would ever reach a hive, and honeycomb would go back to being
///      unobtainable with every priority in the table still sorted.
constexpr bool behaviourTiesOrdered() {
    return behaviourIndex("NearestPreyTarget") < behaviourIndex("NearestAttackableTarget") &&
           behaviourIndex("NearestAttackableTarget") < behaviourIndex("Swell") &&
           behaviourIndex("Sleep") < behaviourIndex("Avoid") &&
           behaviourIndex("RangedAttack") < behaviourIndex("MeleeAttack") &&
           behaviourIndex("Work") < behaviourIndex("Wander") &&
           behaviourIndex("Mingle") < behaviourIndex("Wander") &&
           behaviourIndex("Pollinate") < behaviourIndex("FlyWander");
}
static_assert(behaviourTiesOrdered(),
              "kBehaviours ties are load-bearing: within one priority the array order decides");
/// A misspelt or renamed row would make every comparison above compare two
/// copies of `std::size(kBehaviours)`, which is false, so the assert would fire
/// - except for the one pair where *both* names went missing together. Pinning
/// the count is what closes that: a row that cannot be found is not a row.
static_assert(behaviourIndex("NearestPreyTarget") < std::size(kBehaviours) &&
                  behaviourIndex("NearestAttackableTarget") < std::size(kBehaviours) &&
                  behaviourIndex("Swell") < std::size(kBehaviours) &&
                  behaviourIndex("Sleep") < std::size(kBehaviours) &&
                  behaviourIndex("Avoid") < std::size(kBehaviours) &&
                  behaviourIndex("RangedAttack") < std::size(kBehaviours) &&
                  behaviourIndex("MeleeAttack") < std::size(kBehaviours) &&
                  behaviourIndex("Work") < std::size(kBehaviours) &&
                  behaviourIndex("Mingle") < std::size(kBehaviours) &&
                  behaviourIndex("Pollinate") < std::size(kBehaviours) &&
                  behaviourIndex("FlyWander") < std::size(kBehaviours) &&
                  behaviourIndex("BeeScan") < std::size(kBehaviours) &&
                  behaviourIndex("Wander") < std::size(kBehaviours),
              "a tie-order assert that names a row which no longer exists proves nothing");

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

/// Draws one species from a weighted shortlist, or `Count` when the roll finds
/// nothing - which it genuinely can, because subtracting a shortlist's weights
/// back off a `total` that was summed in the same order still leaves floating
/// point residue.
///
/// **One function rather than two loops**, because the two disagreed and only
/// one of them was right: the chunk spawner started at `Count` and returned
/// nothing when it missed, and the continuous one started at `Sheep` and kept
/// it - so a missed roll put a sheep in an ocean cell picked for a cod, or at
/// night where only a hostile had qualified, with `canSpawnAt` never asked
/// about it. Naming the failure is what makes the caller handle it.
CreatureKind drawSpecies(const float (&weights)[static_cast<std::size_t>(CreatureKind::Count)],
                         float roll) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(CreatureKind::Count); ++i) {
        roll -= weights[i];
        if (weights[i] > 0.0f && roll <= 0.0f) {
            return static_cast<CreatureKind>(i);
        }
    }
    return CreatureKind::Count;
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

/// **The rule every dormant guard in this file is an instance of.** A dormant
/// record is a villager, an iron golem or something `playerBuilt` that the
/// player has walked more than `kBaseRadius` - ninety metres - away from. It is
/// kept so the village is still there when they come back, and it is by
/// construction outside the drawn world, so it cannot be a melee target, an aim
/// target, a flee or a hunt target, or inside a blast, a bolt or an anvil's
/// hurt box. A loop that answers one of those questions about one is paying for
/// a record nobody can see.
///
/// **The flag was honoured by `update`, `separate` and `buildMesh` and by
/// nothing else when it landed**, which is why keeping a village still cost
/// something - and cost a little more with every village the player had ever
/// visited, which is exactly the "the longer I play the worse it gets" shape.
///
/// **This site is the expensive one**, and it is why the rest were found:
/// `think` asks it twice per awake creature per tick (the held foe and the held
/// threat), `applyHits` once per blow and `emitLoot` once per death, and each
/// ask walks the whole vector - so the remembered villages turn a scan that is
/// "linear across tens" into a quadratic one across hundreds.
///
/// Every caller already handles a miss, and a miss is the *right* answer here
/// rather than merely a tolerable one: the widest `leashRange` in the species
/// table is 64 m, so a held foe that has gone dormant is already past every
/// leash, and `think` clearing `targetId` on the null is the same thing the
/// leash test one line below would have done. A hundred-metre memory is stale,
/// not small.
const Creature* Creatures::creatureById(std::uint32_t id) const {
    if (id == 0) {
        return nullptr;
    }
    for (const Creature& creature : m_creatures) {
        if (creature.dormant) {
            continue;
        }
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

std::vector<glm::ivec3> Creatures::takePollinated() {
    return std::exchange(m_pollinated, {});
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
        // **Villages exist now**, and `populateChunks` places their people from
        // the village plan rather than from this table - so this rule is the
        // *stray* villager on open grassland, not the village's own population.
        // It used to say villages did not exist yet, which is the first thing
        // anyone porting the declarative spawn-rule table would have read.
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
        // Nether again, so the same barren stand-in. **The gate is darkness at
        // night**, which is now what the row says as well - it read "underground
        // and dark" while carrying neither, and the spawner only ever places at
        // `surface + 1`, so underground is unreachable by construction and no
        // comment here can make it otherwise.
        return land && any(BiomeTag::Stony | BiomeTag::Badlands | BiomeTag::Peak);
    case CreatureKind::Bee:
        // Where the flowers are, and **deliberately not off the nests**.
        //
        // The reference hangs its spawn off bee nests: a nest generates with
        // 2-3 bees already inside it, and there is no ambient bee spawn at all.
        // Nests now generate here too (`Structures.cpp`, 2026-08-19, which is
        // what makes this comment worth re-reading rather than trusting - it
        // said "a worldgen feature we do not have" until that landed), but a
        // nest in this game holds no occupants, because there is no inside. So
        // the only way to copy the reference exactly would be for the spawner
        // to hunt for nest blocks, which is a scan over the world at spawn time
        // for a creature that has a perfectly good biome rule.
        //
        // The biome rule reaches the same place from the other side, and
        // `Biome.cpp` is what makes that true rather than luck: Meadow carries
        // `Grassland`, the highest `flowerShare` in the table at 0.55, and a
        // 100% nest chance on every tree it grows - so the one biome where a
        // nest is certain is also a biome where a bee is common and a flower is
        // never far. `Creature.cpp`'s own hive search (16 blocks out, 10 up)
        // does the pairing that the reference does at generation time.
        //
        // **What would make this wrong:** a nest gaining occupants of its own,
        // or `Meadow` losing `Grassland`. Neither is true on 2026-08-19.
        return land && any(BiomeTag::Grassland | BiomeTag::Forest | BiomeTag::Swamp);
    case CreatureKind::IronGolem:
        // **Not a natural spawn, and now it says so.** A golem is placed by
        // `populateChunks` from the village plan, next to whichever residents
        // are guarding - so falling through to `false` here is right, but it
        // was silent, and silent is indistinguishable from forgotten.
    case CreatureKind::Count:
        break;
    }
    return false;
}

Creatures::Creatures(std::uint32_t seed) : m_seed(seed), m_random(seed | 1u) {}

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
        // A dormant record is not in play - see `Creature::dormant`. It cannot
        // be hunting anything a hundred metres away, and if it went dormant
        // mid-chase it kept whatever target it had at that instant, which is a
        // stale answer rather than a small one.
        if (creature.dormant) {
            continue;
        }
        if (creature.target != CreatureTarget::None) {
            ++count;
        }
    }
    return count;
}

std::array<std::size_t, static_cast<std::size_t>(CreatureKind::Count)> Creatures::census() const {
    std::array<std::size_t, static_cast<std::size_t>(CreatureKind::Count)> counts{};
    for (const Creature& creature : m_creatures) {
        // **The density limit is about the water and the field in front of the
        // player**, so the villagers and golems being kept for villages the
        // player is nowhere near must not fill it. Counting them would let one
        // remembered village suppress the iron golems of the next one - the
        // same shape as the spawn caps, and `activeCount` is the same answer
        // for the whole population.
        if (creature.dormant) {
            continue;
        }
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
    if (spawnsInOpenWater(species)) {
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
    // **Asked of the block table rather than listed here.** This was a
    // hand-kept whitelist of five ids - Grass, Sand, Snow, Stone, Gravel - and
    // it was a second copy of knowledge `Biome.cpp` already owns. Two biome top
    // blocks are not on it: the Badlands is Terracotta over Clay patches, so
    // **100% of its surface could never spawn anything at all**, day or night,
    // and Frozen Peaks is PackedIce saved only by its 10% snow patches. Camel,
    // Husk, Princepin and all three magma cubes name the Badlands as home; each
    // also carries `Sandy`, `Stony` or `Peak`, which is why no species vanished
    // from the game and this stayed invisible for the life of the biome.
    //
    // `isOpaque` is "a full cube that hides what is behind it", which is the
    // reference's own spawn-surface test - it lets a mob stand on any natural
    // ground and on player-built stone the way Bedrock does, and still refuses
    // leaves, glass, cross-shaped plants, slabs, stairs and fences. The next
    // biome given a new top block is spawnable the day it is added rather than
    // silently dead.
    return isOpaque(world.blockAt(x, y - 1, z));
}

/// **The proof the line above actually covers the terrain**, because "it
/// compiles" says nothing about which blocks a biome puts on top. Every `.top`
/// and `.patch` block in `Biome.cpp` is listed, and the negative half pins the
/// things that must stay unspawnable - without it, widening `isOpaque` to mean
/// merely "solid" would pass the first assert and quietly let mobs spawn on
/// leaves and fence posts.
///
/// The single edit that makes it fail: giving a biome a top block that is not a
/// full opaque cube - a moss carpet, say - or marking Terracotta a cutout.
static_assert(isOpaque(BlockId::Grass) && isOpaque(BlockId::Sand) && isOpaque(BlockId::Snow) &&
                  isOpaque(BlockId::Stone) && isOpaque(BlockId::Gravel) &&
                  isOpaque(BlockId::Terracotta) && isOpaque(BlockId::PackedIce) &&
                  isOpaque(BlockId::Clay),
              "every biome's top and patch block must be ground something can spawn on");
static_assert(!isOpaque(BlockId::Leaves) && !isOpaque(BlockId::Glass) &&
                  !isOpaque(BlockId::TallGrass),
              "and foliage and glass must not be");

void Creatures::think(const World& world, Creature& creature, const glm::vec3& playerFeet,
                      float deltaSeconds, bool night, bool playerSneaking, float timeOfDay,
                      CreatureAttack& attack, std::vector<CreatureExplosion>& blasts) {
    const CreatureSpecies& species = speciesInfo(creature.kind);

    tickPassiveTimers(creature, deltaSeconds);
    creature.provokedTimer = std::max(0.0f, creature.provokedTimer - deltaSeconds);
    creature.scanTimer = std::max(0.0f, creature.scanTimer - deltaSeconds);
    creature.decisionTimer -= deltaSeconds;

    // **Standing in fire or lava, which until this landed did nothing at all to
    // a creature.** `BlockId::Fire` and `isLava` appeared nowhere in this file:
    // the only heat in the game was the sun on the undead, so a burning forest
    // killed no animals, "set it on fire" was not a strategy, and the
    // cooked-drop rule the loot table has carried for milestones - raw beef to
    // steak - was unreachable, because nothing could ever set `death.burning`.
    //
    // "An entity standing inside a fire block takes 1 HP per tick, capped by
    // the damage-immunity window to once every half second"; lava is the same
    // clock at four (minecraft.wiki, *Fire* and *Lava*). Both rates are already
    // published in `Survival.hpp` and spent by the player, so they are read
    // from there rather than written down a second time here.
    //
    // Sampled over the cells the body occupies, the same scan the player's
    // contact hazards use, so a corner clipping a flame counts and standing
    // beside one does not. **Skipped outright for a fire-immune species** -
    // that is what makes it cheap for a magma cube sitting in a lava lake,
    // which is the one creature that spends its whole life inside this test.
    const bool immuneToHeat = fireImmune(creature.kind);
    // **Powder snow rides the same occupied-cell walk rather than a second one
    // of its own**, which is the shape `Player.cpp` used for campfire contact:
    // a flag filled beside `inFire` in the scan that is already paying for the
    // block reads. `canFreeze` is false for the species the reference exempts
    // and for the four that never get inside the block at all, so for those the
    // walk costs exactly what it did before this landed - and for a magma cube,
    // which is heat-immune, it is the only reason the walk happens.
    const bool canFreeze = !species.immuneToFreezing && species.sinksInPowderSnow;
    bool inFire = false;
    bool inLava = false;
    bool inPowderSnow = false;
    // **Its own flag beside `inFire`, not a widened `inFire`** - `CLAUDE.md`
    // bug shape #2, and `Player.cpp` made exactly this call at its own copy of
    // this scan. `inFire` is read twice below (the burn cascade and, through
    // `DeathCause::Burning`, the cooked-drop test) and a campfire wants only
    // the first of them charged at a different rate, so widening the predicate
    // would have changed a reader that nobody was looking at.
    //
    // **Every campfire in this game is lit.** `BlockId` carries `Campfire` and
    // `SoulCampfire` and no unlit twin, and the reference places them lit, so
    // an unconditional test is right *today*; the day an unlit state is
    // modelled, this line and `Player.cpp`'s are the two that go wrong
    // together, which is why the same warning sits at both.
    bool inCampfire = false;
    if ((!immuneToHeat || canFreeze) && creature.health > 0) {
        const Aabb body = bodyBox(species, creature.position, creature.scale);
        if (!immuneToHeat) {
            inLava = fluid::sampleFluid(world, body, fluid::FluidKind::Lava).inFluid;
        }
        // **The early-out is widened, not dropped.** It used to be `!inFire`;
        // leaving it that way would have stopped the walk on the first flame
        // and missed the snow beyond it, and dropping it would have made every
        // burning creature pay for the whole box. This stops as soon as none of
        // the three flags can still change - which for a fire-immune species is
        // the first cell of snow, and for everything else the first cell that
        // has answered fire, campfire and snow together.
        const auto stillLooking = [&] {
            return ((!inFire || !inCampfire) && !immuneToHeat) || (!inPowderSnow && canFreeze);
        };
        for (int x = static_cast<int>(std::floor(body.min.x));
             x <= static_cast<int>(std::floor(body.max.x)) && stillLooking(); ++x) {
            for (int y = static_cast<int>(std::floor(body.min.y));
                 y <= static_cast<int>(std::floor(body.max.y)) && stillLooking(); ++y) {
                for (int z = static_cast<int>(std::floor(body.min.z));
                     z <= static_cast<int>(std::floor(body.max.z)) && stillLooking(); ++z) {
                    // **Accumulated rather than assigned**, which the old line
                    // could get away with only because it stopped the instant it
                    // was true. Now that the walk can continue past a flame, a
                    // plain assignment would let the next cell clear a flag that
                    // was correctly set.
                    const BlockId here = world.blockAt(x, y, z);
                    inFire = inFire || (!immuneToHeat && here == BlockId::Fire);
                    inCampfire = inCampfire || (!immuneToHeat && isCampfire(here));
                    inPowderSnow = inPowderSnow || (canFreeze && here == BlockId::PowderSnow);
                }
            }
        }
    }

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
    //
    // **Water is sampled here rather than read off `creature.inWater`, which is
    // a frame stale.** That flag is written by `step`, and `step` runs after
    // `think` in `update`, so a zombie stepping out of water took a tick of
    // burn it had not earned and one stepping in was spared a tick it had. The
    // sample is placed last in the condition on purpose: only a nocturnal
    // burner already standing in daylight ever pays for it.
    //
    // **Four sources, one clock, and the fastest of them owns it** - lava beats
    // fire beats a campfire beats the sun, each charging its own published
    // rate. Running four accumulators side by side would interleave them into a
    // stream faster than any one is meant to be, which is the mistake
    // `survival::kHazardInterval` exists to stop the player's hazards making.
    //
    // **All four rates are `survival::`-qualified, and that is load-bearing
    // rather than tidy.** The sun branch alone used the bare names while a
    // file-local `kBurnDamage` / `kBurnInterval` pair sat at the top of this
    // TU, so unqualified lookup took the copy - no ambiguity, no warning, and
    // the two branches either side of it honouring the rule the copy broke.
    // Leaving the qualifier off any of them makes the same hole the moment
    // someone writes a constant of that name here again.
    int heatDamage = 0;
    float heatInterval = 0.0f;
    if (inLava) {
        heatDamage = survival::kLavaDamage;
        heatInterval = survival::kLavaInterval;
    } else if (inFire) {
        heatDamage = survival::kFireDamage;
        heatInterval = survival::kFireInterval;
    } else if (inCampfire) {
        // **A campfire hurts what stands in it, and until now it hurt only the
        // player.** `Campfire.hpp`'s handoff asked for this by name and
        // `survival::kCampfireDamage` has been waiting for it; the asymmetry
        // was visible in one screenful, since a pig could stand in the fire you
        // are cooking on and take nothing.
        //
        // **Chained onto the same `else` rather than charged beside it**, which
        // is the choice `Player.cpp` makes for magma and campfire both: one
        // clock, the fastest source owning it, so standing in lava over a
        // campfire cannot cost the sum of the two.
        //
        // **`kHazardInterval` rather than `kFireInterval`, though both read
        // 0.5.** `Survival.hpp` says campfire "needs no interval of its own"
        // and charges it on the shared contact cadence, so that is the constant
        // that owns its pace - naming fire's would make this line silently
        // depend on a number that documents a different hazard.
        //
        // **Two of the reference's campfire rules cost nothing to honour here.**
        // It is not armour-reduced - no creature wears anything, which is the
        // whole of the note sitting between `damageCreature` and
        // `hazardDamage` - and it does not set you alight, which this file gets
        // for free because `Creature` carries no afterburn countdown at all
        // (the burn cascade's own `else` explains that gap).
        //
        // It does count as fire for the drop, which `LootDrop::cooked`'s own
        // comment already promised in a sentence naming campfires ("lava, a
        // campfire and an undead caught at dawn all cook it alike") while no
        // campfire could reach a creature: a pig killed in one leaves cooked
        // porkchop, through `DeathCause::Burning` exactly as fire and lava do.
        heatDamage = survival::kCampfireDamage;
        heatInterval = survival::kHazardInterval;
    } else if (!night && species.nocturnal && species.burnsInDay && creature.health > 0 &&
               world.skyLightAt(static_cast<int>(std::floor(creature.position.x)),
                                static_cast<int>(std::floor(creature.position.y)),
                                static_cast<int>(std::floor(creature.position.z))) >=
                   kBurnSkyLight &&
               !fluid::sampleFluid(world, bodyBox(species, creature.position, creature.scale))
                    .inFluid) {
        heatDamage = survival::kBurnDamage;
        heatInterval = survival::kBurnInterval;
    }

    if (heatDamage > 0) {
        creature.burnTimer += deltaSeconds;
        while (creature.burnTimer >= heatInterval) {
            creature.burnTimer -= heatInterval;
            hazardDamage(creature, heatDamage, DeathCause::Burning);
        }
    } else {
        // Stepping into shade or out of the flames puts it out rather than
        // pausing it, so a zombie that reaches a doorway with one point left
        // keeps that point.
        //
        // **The reference's eight- and fifteen-second afterburn is not modelled
        // here**, and it is the one part of *Fire* this cannot reach: a mob's
        // `Fire` tag jumps to 160 ticks the moment it is exposed and keeps
        // burning after it leaves. That needs a countdown on the record -
        // `Player` carries exactly one, `burningSeconds`, beside the same
        // `burnTimer` this uses - and `Creature` has no such field. Adding one
        // is a `Creature.hpp` edit and a saved-record change, both owned
        // elsewhere; `survival::kFireBurnSeconds` and `kLavaBurnSeconds` are
        // already published and waiting for it. Until then a creature stops
        // burning the instant it is clear, which undercharges the escape and
        // never overcharges it.
        creature.burnTimer = 0.0f;
    }

    // --- Freezing. **The last hazard the player had and creatures did not**,
    // and it is the player's own clock adopted rather than a second one written
    // here: every constant is `survival::`-qualified, the accumulate-and-decay
    // shape is `Player.cpp`'s line for line, and the reasoning for each is at
    // `Player::freezeSeconds` and `Player::freezeTimer`. A cow freezing at a
    // different rate to the player standing in the same drift would be
    // `CLAUDE.md` bug shape #14 - a rule that exists, is correct and travels to
    // only one of the two places that need it - and it is the shape that put
    // the player through not-yet-loaded chunks for twenty milestones.
    //
    // **Outside the damage window on purpose**, exactly as burning, drowning,
    // drying out and suffocation are: this carries its own two-second cadence
    // and `survival::kFreezeInterval != kHazardInterval` is asserted over
    // there, so folding it onto anything else fails the build rather than
    // quietly changing the rate. `hazardDamage` is also the one entry point
    // that refuses a corpse, which is what stops a body in a drift going on
    // freezing until it is retired and stealing the kill from whoever earned
    // it.
    //
    // **Armour does not enter into it and cannot.** The reference's exemption
    // is leather, worn - a creature on this roster wears nothing at all, so the
    // whole question is the exempt-species column and nothing else.
    if (inPowderSnow) {
        creature.freezeSeconds =
            std::min(survival::kFreezeOnsetSeconds, creature.freezeSeconds + deltaSeconds);
        creature.powderSnowSeconds += deltaSeconds;
    } else {
        creature.freezeSeconds =
            std::max(0.0f, creature.freezeSeconds - deltaSeconds * survival::kFreezeRecoveryRate);
        // **Reset rather than decayed, and the two counters differ here on
        // purpose.** `freezeSeconds` is Bedrock's `TicksFrozen`, which drains;
        // the conversion timer is a component group that is *removed* the
        // moment the skeleton is out of the block, so a skeleton that steps out
        // for a second and back in starts its twenty seconds again.
        creature.powderSnowSeconds = 0.0f;
    }
    if (creature.freezeSeconds >= survival::kFreezeOnsetSeconds) {
        creature.freezeTimer += deltaSeconds;
        while (creature.freezeTimer >= survival::kFreezeInterval) {
            creature.freezeTimer -= survival::kFreezeInterval;
            hazardDamage(creature,
                         species.freezesHarder ? kFireMobFreezeDamage : survival::kFreezeDamage,
                         DeathCause::Freezing);
        }
    } else {
        // Primed at the interval, never zeroed - the published 140 ticks is
        // when damage *begins*, so starting a fresh 40-tick wait here would
        // make the onset read as 180. `Player.cpp` says the same at its own
        // copy of this line.
        creature.freezeTimer = survival::kFreezeInterval;
    }

    // **A skeleton left in the snow turns into a stray**, which is the
    // reference's own use for this block and the only conversion on the roster.
    // `skeleton.json`'s `in_powder_snow` group is a twenty-second non-looping
    // timer firing `become_stray_event`; `kStrayConversionSeconds` carries the
    // sourcing and why it is not the wiki's seven.
    //
    // **In place rather than replaced.** Bedrock's `minecraft:transformation`
    // destroys the skeleton and spawns a stray, which would hand back a full
    // twenty health; ours keeps the body, its id, its anger and whatever the
    // freezing already cost it, clamped to the new species' maximum. That is a
    // deliberate divergence and it is the conservative direction - a free heal
    // for standing in snow would be a farm, not a mechanic.
    //
    // **The `species` reference above is now one row stale for the rest of this
    // call**, which is stated rather than fixed: the two rows share a box, a
    // health, a speed and a rig, and differ only in bite and weight, so the one
    // frame costs a point of damage at most. Re-binding a `const&` mid-function
    // would mean restructuring `think` around a case that happens once in a
    // creature's life.
    if (creature.kind == CreatureKind::Skeleton &&
        creature.powderSnowSeconds >= kStrayConversionSeconds) {
        creature.kind = CreatureKind::Stray;
        creature.health = std::min(creature.health, speciesInfo(CreatureKind::Stray).health);
        creature.freezeSeconds = 0.0f;
        creature.freezeTimer = survival::kFreezeInterval;
        creature.powderSnowSeconds = 0.0f;
        // The reference plays `convert_to_stray` here. `CreatureSound` has
        // three values - idle, hurt, death - and none of them is a conversion,
        // so this is silent and that gap belongs to the sound table rather than
        // to a fourth enumerator invented at a call site.
    }

    const glm::vec3 toPlayer = playerFeet - creature.position;
    const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
    const float yawToPlayer = std::atan2(toPlayer.x, toPlayer.z);
    // Negated because a positive head pitch is nose-down, so a player standing
    // above wants a negative one. Through `eyeHeight` since 2026-08-19; this
    // was one of the three that dropped `creature.scale`.
    const float pitchToPlayer =
        -std::atan2(toPlayer.y + player_constants::kEyeHeight - eyeHeight(species, creature.scale),
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
    // A creature that has never seen you pays one ray per scan - twice a
    // second - and the answer holds until the next scan, which is Bedrock's own
    // sampling.
    //
    // **A creature that *is* chasing you pays one every frame, and that is the
    // case that costs.** `forgetTimer` is refilled to `species.forgetSeconds`
    // on every tick the player is seen - seventeen for a zombie - so `recheck`
    // is true for the whole of a chase and for seventeen seconds after it. The
    // comment here used to promise two rays a second flatly, which was true
    // only of the creatures that never look at you. It is deliberate rather
    // than an oversight: a chase is exactly when the answer has to be current,
    // and it is bounded by how many things can be chasing at once.
    creature.sightTimer = std::max(0.0f, creature.sightTimer - deltaSeconds);
    const float sightRange = std::max(species.senseRange, species.leashRange);
    const bool caresAboutSight =
        species.hostile || species.huntsBelowLight > 0 || species.attackDamage > 0;
    if (caresAboutSight && distance <= sightRange) {
        const bool recheck = species.explodePower > 0.0f || creature.scanTimer <= 0.0f ||
                             creature.forgetTimer > 0.0f;
        const glm::vec3 eye = eyeOf(creature.position, species, creature.scale);
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
                // Nothing ninety metres away is frightening. This scan runs per
                // awake creature per tick, so a dormant record joins it once
                // for every animal on screen - see `creatureById` for the rule.
                if (other.dormant) {
                    continue;
                }
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
    //
    // **One leash, used twice**, for the foe below and for the threat further
    // down. Two copies of "how far can I still care about this" is exactly what
    // let the two disagree and freeze a golem for ten minutes.
    const float leash = species.leashRange > 0.0f ? species.leashRange : species.senseRange;
    const Creature* foe = nullptr;
    if (creature.targetId != 0) {
        const Creature* held = creatureById(creature.targetId);
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
            // **The widest `senseRange` in the table is well inside ninety
            // metres**, so a dormant record could never have won this scan - it
            // was walked, measured and rejected on distance, once per hunter per
            // scan. The same rule as `creatureById`, and this is the other half
            // of the quadratic it names.
            if (other.dormant) {
                continue;
            }
            // Never its own kind, which is checked rather than encoded - a fox
            // is `Vulpine` and hunts `Critter`, and without this a tag that
            // happened to cover both would have it eating itself.
            if (other.health <= 0 || other.kind == creature.kind ||
                (creatureTags(other.kind) & species.hunts) == 0) {
                continue;
            }
            // **Three dimensions, matching the retention test above.**
            // Acquisition measured flat and retention measured in 3-D meant a
            // foe picked up a slope away was already out of leash on the very
            // next tick, so a fox on a hillside twitched between chasing and
            // idling on alternate scans.
            const glm::vec3 offset = other.position - creature.position;
            const float span = glm::dot(offset, offset);
            if (span >= nearest) {
                continue;
            }
            nearest = span;
            foe = &other;
        }

        // **`mustSee` travelled to one of the two targeting rows and not the
        // other.** `nearestTargetStart`, which picks the player, gates on it
        // correctly; this scan cast no ray at all, so an iron golem set off
        // through a village wall after a zombie it had never seen and a wolf
        // tracked a sheep through a hill. The flag defaults to true, so this is
        // the whole roster of hunters: cat, wolf, fox, ocelot and the golem.
        //
        // **One ray, after the scan, not one per candidate.** A ray through the
        // world is the expensive thing in this function; asking it of every
        // grazer in range would have cost the population's worth of them per
        // scan to reject all but one. Losing the nearest to a wall means going
        // without a quarry until the next scan rather than falling through to
        // the second nearest, which is the reference's behaviour too - its
        // `nearest_attackable_target` picks first and filters after.
        if (foe != nullptr && species.mustSee) {
            // Both eyes through `eyeOf`, which is now the only place the
            // expression exists. The pair used to be written to mirror each
            // other and this comment named the shape they were guarding against
            // - "a derivation applied to one of a pair and not the other" - but
            // mirroring is a promise a reader has to keep, and three OTHER eye
            // sites in this file had already broken it. One owner keeps it
            // instead.
            const glm::vec3 eye = eyeOf(creature.position, species, creature.scale);
            const CreatureSpecies& foeSpecies = speciesInfo(foe->kind);
            const glm::vec3 foeEye = eyeOf(foe->position, foeSpecies, foe->scale);
            if (!hasLineOfSight(world, eye, foeEye)) {
                foe = nullptr;
            }
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
    const Creature* threat = nullptr;
    if (creature.threatId != 0) {
        const Creature* held = creatureById(creature.threatId);
        if (held == nullptr || held->health <= 0) {
            // Whatever it was is gone, so there is nothing left to run from.
            creature.threatId = 0;
            creature.provokedTimer = 0.0f;
        } else {
            const glm::vec3 offset = held->position - creature.position;
            threatDistance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
            yawFromThreat = std::atan2(-offset.x, -offset.z);
            // **Alive but out of reach is a third answer, and it used to be
            // missing.** Gone cleared both fields, near kept both, and there
            // was no case in between - so an attacker that simply backed off
            // stayed the target for the whole of `angerSeconds`, up to 600 s.
            // The anger deliberately survives: `provokedTimer` and `threatId`
            // are untouched here, so it re-engages the instant the thing comes
            // back inside the leash. What stops is the targeting.
            if (glm::distance(held->position, creature.position) <= leash) {
                threat = held;
            }
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
                                   night,
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
                                   threat,
                                   attack,
                                   m_hits,
                                   m_launches,
                                   m_grazed,
                                   m_pollinated,
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
        // **This is the eighth site that lowers `health`, and it records like
        // the other seven.** It did not, and that was a real drop bug rather
        // than tidiness: `applyExplosion` skips anything already at zero, so
        // nothing else was ever going to write this death down, and the context
        // kept whatever had last hurt this Bramble *at any point in its life*.
        // A skeleton that grazed it half a minute ago left `cause` reading
        // `Creature` with that skeleton's id, so walking up to the player and
        // detonating paid out a music disc - the creeper-shot-by-skeleton drop,
        // through a door nobody had thought to shut.
        //
        // `Explosion` rather than a `SelfDestruct` of its own, deliberately: it
        // *is* a blast, and one enumerator is one thing for a future "did this
        // die in an explosion?" test to ask about. A second one is a rule that
        // has to travel, and rules that have to travel are the ones that do not.
        recordHazard(creature, DeathCause::Explosion);
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
    //
    // **Belt, not the owner.** `update` clamps once before anything reads the
    // frame, so this is already true for every caller it has today; it stays
    // because `step` is a `private` member with three call sites and the clamp
    // is a precondition of the sweep below rather than of the caller.
    deltaSeconds = std::min(deltaSeconds, kMaxDeltaSeconds);

    // Ground it cannot see is ground it would fall through: an absent chunk
    // reads as air, so simulating over one drops the creature out of the world
    // and it reappears buried when the chunk returns. Standing still until the
    // terrain is back is the only safe answer. `separate` asks the same
    // question through the same helper.
    if (!onLoadedColumn(world, creature.position)) {
        creature.velocity = glm::vec3{0.0f};
        return;
    }

    // A body inside terrain climbs out rather than staying there forever.
    // Nothing *should* put one there, but a shove from a herd, a spawn over
    // ground that had not finished loading, or a player filling in the block it
    // was standing in all can - and once inside, every move it tries overlaps
    // something, so it is stuck for good. The reference has its own push-out
    // procedure for exactly this (`RESEARCH.md` §1.6).
    //
    // **The search had to get better the moment this could kill.** The clock
    // below charges a body this pass fails to free, so every way the pass can
    // fail *spuriously* became a way to die of a search bug rather than of
    // being buried. Two were already written down as known gaps, and both are
    // closed here rather than excused:
    //
    // 1. **A fixed 0.25 grid phased on the body's own `y` steps over legitimate
    //    free space.** It only guarantees a hit when the step is no larger than
    //    the pocket's slack, and 21 of the 58 rows have under 0.25 m of it in a
    //    one-block pocket - a skeleton has 0.01. **Block floors have no phase
    //    condition at all**: `position` is the feet, so a body at rest stands
    //    with its feet on a block boundary, and `floor(y) + 1, + 2, ...` are
    //    precisely the valid resting heights. Two or three samples, exact.
    // 2. **Five rows are taller than the shared 2.0 m reach**, so a fully
    //    entombed iron golem could never clear its own volume however fine the
    //    sampling got. `Player.cpp` asserts `kUnstickReach >= kHeight` and that
    //    assert *is* the rule; it could not be mirrored here only because our
    //    height is a runtime field. So the rule is honoured at runtime instead
    //    of dropped - the reach covers the body's own height when that is the
    //    larger. It cannot misplace anything: every candidate is still rejected
    //    unless it is clear.
    //
    // What is left when both loops fail is a body with no free space anywhere
    // within its own height above it, which is a defensible meaning for
    // "buried" and the one the clock below is allowed to charge for.
    bool buried = false;
    if (bodyOverlapsSolid(world, species, creature.position, creature.scale)) {
        buried = true;
        const float startY = creature.position.y;
        const float reach = std::max(collision::kUnstickReach, species.height * creature.scale);

        const auto tryLift = [&](float lift) {
            if (lift <= 0.0f || lift > reach) {
                return false;
            }
            glm::vec3 freed = creature.position;
            freed.y = startY + lift;
            if (bodyOverlapsSolid(world, species, freed, creature.scale)) {
                return false;
            }
            creature.position = freed;
            buried = false;
            return true;
        };

        for (float surface = std::floor(startY) + 1.0f; surface <= startY + reach;
             surface += 1.0f) {
            if (tryLift(surface - startY)) {
                break;
            }
        }
        // The fine grid still runs behind them, because a slab, a stair or a
        // carpet gives a valid standing height that is not on a block boundary
        // and the block floors alone would miss it.
        if (buried) {
            for (float lift = collision::kUnstickStep; lift <= reach;
                 lift += collision::kUnstickStep) {
                if (tryLift(lift)) {
                    break;
                }
            }
        }
        // Whatever it was doing, it was not doing it from in there.
        creature.velocity = glm::vec3{0.0f};
        creature.onGround = false;
    }

    // **Still inside it**, with nothing clear within its own height overhead,
    // and until this clock existed that was a creature stuck alive for ever -
    // `step` has no other way to end it, because every move it attempts
    // overlaps and none of the three hazards it does run asks about solid
    // terrain.
    //
    // Worth the cost right now in particular: `kChunkFormatVersion` keeps
    // moving - it was 5 -> 6 when this was written and is **8 as of 2026-08-19
    // 11:09**, read out of `WorldStore.cpp:436` rather than remembered - so
    // every modified chunk in every existing save is being discarded
    // and regenerated while creature records load unconditionally from their own
    // file. A creature materialising inside fresh terrain is the normal case
    // this round, not the edge case. **That is also exactly why the search above
    // had to be made exact before this was allowed to be lethal** - a villager
    // dies to this in ten seconds and an iron golem in twenty, with no attacker
    // to blame it on, so charging one for a pocket the sampling merely missed
    // would have been the worst kind of silent regression.
    //
    // **Deliberately not an early return.** Everything below - the water sample,
    // the breath counter, the fall accumulator - goes on running exactly as it
    // did before this clock was added, so the only change here is that health
    // now moves. Returning would have read more tidily and would have quietly
    // frozen four other timers on a buried body, which is the "what else does
    // this early return skip" shape that has already cost this project a
    // soft-locked respawn screen. The moves below all overlap and fail on their
    // own, exactly as they did before, and need no help from here.
    if (buried) {
        creature.suffocateTimer += deltaSeconds;
        if (creature.suffocateTimer >= survival::kSuffocationInterval) {
            creature.suffocateTimer -= survival::kSuffocationInterval;
            hazardDamage(creature, survival::kSuffocationDamage, DeathCause::Suffocation);
        }
    } else {
        // Free, so the clock starts again from nothing rather than resuming - a
        // body clipped by a closing door for a frame should never accumulate
        // toward a hit. `burnTimer` is reset by shade for the same reason.
        creature.suffocateTimer = 0.0f;
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
    creature.inWater = water.inFluid;

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
                hazardDamage(creature, fluid::kDrownDamage, DeathCause::Drowning);
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
                hazardDamage(creature, 1, DeathCause::DryingOut);
            }
        }
    }

    // A chicken flaps and drifts down instead of dropping. The reference scales
    // a descent by 0.6 every tick, which settles at 2.4 m/s against the 78.4
    // everything else reaches - and it is why one never needs fall damage
    // exempting, because it simply never lands hard.
    //
    // **That 2.4 is the reference's own number, and reaching it is what proved
    // `kGravity` had drifted.** This line re-solves the reference's per-tick
    // constant against whatever gravity is in scope, so with the old creature
    // gravity of 26 it produced 1.95 and the comment here reported 1.95 as
    // though the reference had said it.
    //
    // The factor is re-solved for our timestep rather than used verbatim.
    // `v = (v - g dt) k` settles at `g dt k / (1 - k)`, so applying 0.6 per
    // *frame* would land somewhere else entirely - it is linear in `dt`, so at
    // 120 fps it would settle at a sixth of the speed, 0.4 m/s. Working
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
                // **Seeded in `add` with every other phase**, not on the first
                // tick here. This asked for `age <= 0.0f`, which quietly stopped
                // being true the day `age` itself was scattered - a guard a fix
                // three thousand lines away could switch off without touching it.
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
    //
    // **Swept, not destination-only.** `ItemEntity.cpp` carries the identical
    // loop under the identical heading, and a creature needed it at least as
    // badly. The shortest body on the roster is a 0.30 m silverfish, 0.165 m as
    // a baby, and a clamped frame at terminal velocity moves 3.92 m - so a test
    // of only where the move *ended* found clear air under every floor the body
    // had jumped clean over, and the animal fell out of the world after an
    // ordinary 13 m cliff. The move is walked in slices no longer than
    // `kMaxSweepStep`, and the first one to hit stops it; the two
    // `static_assert`s under `kSpecies` are what keep the slice short enough
    // for consecutive test boxes to overlap.
    const float verticalStep = creature.velocity.y * deltaSeconds;
    const int verticalSlices =
        std::max(1, static_cast<int>(std::ceil(std::abs(verticalStep) / kMaxSweepStep)));
    const float verticalSlice = verticalStep / static_cast<float>(verticalSlices);
    creature.onGround = false;
    for (int i = 0; i < verticalSlices; ++i) {
        // Where *this slice* began, and the ceiling a landing may not be above.
        // Handing the whole frame's start to `highestSurfaceBelow` would let a
        // later slice settle on a surface the body had already fallen past,
        // which is a teleport upward.
        const float from = creature.position.y;
        creature.position.y += verticalSlice;
        if (!bodyOverlapsSolid(world, species, creature.position, creature.scale)) {
            continue;
        }
        if (creature.velocity.y < 0.0f) {
            // A slab's top is halfway up its cell, so the resting height comes
            // from the shape table rather than from the cell boundary. Checked
            // before it is taken: the surface found is at or below where the
            // body already was, and a lower position can meet something the
            // higher one cleared.
            const float surface = highestSurfaceBelow(
                world, bodyBox(species, creature.position, creature.scale), from);
            const float landed = std::isfinite(surface) ? surface + kCollisionSkin : from;
            glm::vec3 settled = creature.position;
            settled.y = landed;
            creature.position.y =
                bodyOverlapsSolid(world, species, settled, creature.scale) ? from : landed;
            creature.onGround = true;
        } else {
            creature.position.y = from;
        }
        creature.velocity.y = 0.0f;
        break;
    }

    // **How far it fell, measured as ground actually lost rather than as speed
    // multiplied by time.** Taken after the sweep has resolved, so a slice that
    // landed contributes only the part of its travel that happened - the old
    // accumulator added the whole intended step first and let the landing throw
    // it away, which credited a fall with up to a frame of travel it never made.
    if (creature.position.y < before.y) {
        creature.fallDistance += before.y - creature.position.y;
    }

    // The two consumers of that one number, so it is read in one place and
    // cleared in one place. It used to be charged inside the landing branch,
    // where a landing resolved anywhere else - a swim, a flier's hover - left
    // it to grow without bound.
    if (creature.onGround) {
        // A hard landing lights most of an exploder's fuse. The reference buys
        // 1.5 ticks per block and stops five short of the end, so a creeper
        // dropped on your head goes off almost at once - but never instantly,
        // and only if it is swelling anyway.
        if (species.explodePower > 0.0f && creature.fallDistance > 0.5f) {
            const float ceiling =
                std::max(0.0f, species.fuseSeconds - kFallFuseHeadroomTicks / kTicksPerSecond);
            const float bought = creature.fallDistance * kFallFuseTicksPerBlock / kTicksPerSecond;
            creature.fuseTimer = std::min(std::max(creature.fuseTimer, bought), ceiling);
        }
        chargeFall(creature, species);
        creature.fallDistance = 0.0f;
    }

    const auto trySlice = [&](float dx, float dz) {
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
        // **The rise alone is tested first**, exactly as `Player.cpp` does
        // before its own step-up. Combining the rise and the horizontal move
        // into one destination test accepts a place reachable only by cutting
        // the corner - the body passes through the ceiling over its head on the
        // way up and arrives somewhere it could never have walked to. Same rule
        // as the sweep above and for the same reason: a single test of where a
        // move *ended* says nothing about what it went through.
        if (bodyOverlapsSolid(world, species, raised, creature.scale)) {
            return false;
        }
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

    // **Swept, exactly as the vertical is forty lines above, and it was not.**
    // The same rule, the same function, twelve lines apart, correct in one of
    // the two places that needed it - which is the shape this file is the
    // project's worst offender for. The vertical was swept because gravity
    // reaches 78.4 m/s; the horizontal was left destination-only on the
    // reasoning that a creature walks at 4 m/s, and that reasoning forgot
    // knockback. An iron golem's punch is `kKnockbackSpeed` x its 3.15
    // `knockbackScale`, 14.175 m/s, which is 0.709 m inside one clamped frame -
    // against a 0.40 m-wide chicken and a 0.198 m baby silverfish. A single
    // test of where the shove *ended* found open air on the far side of the
    // village wall, and the bird went through it.
    //
    // Partial progress is kept rather than thrown away, which is also a fix:
    // stopping the whole move on a blocked slice left a body up to a slice
    // short of the wall it was walking into, and the gap showed.
    const auto tryMove = [&](float dx, float dz) {
        // One axis at a time by construction, so the sum is the length.
        const float travel = std::abs(dx) + std::abs(dz);
        const int slices = std::max(1, static_cast<int>(std::ceil(travel / kMaxSweepStep)));
        const float sliceX = dx / static_cast<float>(slices);
        const float sliceZ = dz / static_cast<float>(slices);
        for (int i = 0; i < slices; ++i) {
            // Each slice re-reads the world from wherever the last one left the
            // body, and a step-up taken by one is a step-up the next measures
            // from. Nothing is decided once and re-applied - that is the defect
            // the player's bounce latch exists to fix, and the reason the
            // vertical hands every slice its own `from`.
            if (!trySlice(sliceX, sliceZ)) {
                // **The stopped axis loses its speed, exactly as the vertical
                // one does on landing and as `ItemEntity`'s `sweepAxis` does.**
                // Position alone was already right - the body is clamped short
                // of the wall every frame - but `velocity.xz` is not scratch
                // here: a hop, a flop, a mid-air drift and a knockback all read
                // `wish` straight back out of it. Left alone, a slime that
                // clips a fence keeps its whole launch speed pressed into it
                // for the rest of the arc, and a creature knocked into a wall
                // spends its entire `hurtTimer` shoving at the stone. Only the
                // blocked axis is cleared, so sliding along the wall - which is
                // what calling this once per axis buys - still works.
                if (dx != 0.0f) {
                    creature.velocity.x = 0.0f;
                }
                if (dz != 0.0f) {
                    creature.velocity.z = 0.0f;
                }
                return false;
            }
        }
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

    // **Clamped once, here, for everything downstream.** `step` clamped and
    // `think` did not, so during a chunk-loading hitch the burn, anger and fuse
    // timers in `think` advanced at the raw rate while the drown, dry-out and
    // air timers in `step` advanced at the clamped one - two families of
    // per-tick clock running at different speeds through the same stutter.
    // `separate` took the raw delta too, and with `kSeparationPush` that turned
    // a one-second hitch into a three-metre shove tested only at its
    // destination, i.e. straight through a wall.
    deltaSeconds = std::min(deltaSeconds, kMaxDeltaSeconds);

    // Refilled at a rate, then spent by whichever creatures ask to search. A
    // cap here rather than a cap per creature is what bounds the cost against
    // the whole population instead of against each animal.
    //
    // The fractional part is carried, so a rate that does not divide evenly
    // into a frame is still delivered on average: at 240 fps this hands out a
    // single search every sixth frame rather than rounding to none.
    m_pathCredit = std::min(m_pathCredit + kPathsPerSecond * deltaSeconds, kMaxPathBurst);
    m_pathBudget = static_cast<int>(m_pathCredit);
    m_pathCredit -= static_cast<float>(m_pathBudget);
    for (Creature& creature : m_creatures) {
        // **Kept, but not thought about.** A dormant creature is a villager or
        // a player-built golem the player has walked away from: it survives the
        // retirement test in `manage` so the village is still there when you
        // come back, and it is skipped here so that surviving costs a
        // comparison rather than a full `think` and `step` every frame for
        // every village ever visited. See `Creature::dormant`.
        //
        // **Nothing decays while it is skipped**, which is the point: no
        // gravity, no anger clock, no hunger. It stands where it stood, which
        // is exactly what a creature the world had forgotten used to do by not
        // existing at all.
        if (creature.dormant) {
            continue;
        }
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
                // **The same one-shot gate latches the death context**, rather
                // than a second gate beside it that could disagree. Everything
                // it needs is destroyed within a tick or two: `think` never
                // runs on a corpse, so `burnTimer` freezes here whatever the
                // last live tick left - and loot is not paid out until a full
                // second later, by which time nothing on the creature still
                // says it died on fire.
                creature.death.burning =
                    creature.burnTimer > 0.0f || creature.death.cause == DeathCause::Burning;
                // **And who did it, latched by the same gate for the same
                // reason.** `recordHazard` overwrites `cause` and clears
                // `killerId` outright, and `step` runs on a corpse - so a cow
                // the player killed at the water's edge went on drowning while
                // it toppled, and the drop that was paid out a second later was
                // the sea's kill with no killer and no rare drops. The hazards
                // now refuse a corpse outright, which is the real fix;
                // this is the belt, and it is what makes the next hazard
                // somebody adds harmless rather than a repeat.
                creature.death.latched = true;
                // And where it died, in sixteenths of a block, for the same
                // reason and in the same breath. `position` does **not** hold
                // still for the second the body lies there: `separate` shoves
                // corpses exactly as it shoves the living, so an animal killed
                // inside a herd drifts several sixteenths before it is retired
                // - and that shove is per-frame explicit Euler, which put the
                // frame-rate dependence the whole hash exists to remove back
                // into the commonest kill there is. Latched here, the drop is
                // decided by the death and nothing after it.
                creature.death.place =
                    glm::ivec2{static_cast<int>(std::floor(creature.position.x * 16.0f)),
                               static_cast<int>(std::floor(creature.position.z * 16.0f))};
            }
            creature.deathTimer += deltaSeconds;
            // **The clocks that expire keep expiring**, which they did not:
            // `think` is skipped entirely for a corpse and it owned all four.
            // Hygiene rather than a visible fix - see `tickPassiveTimers` for
            // what does and does not depend on it - and the same extraction
            // `Player.cpp` made for the same shape at the same time, where it
            // *was* load-bearing.
            tickPassiveTimers(creature, deltaSeconds);
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
        // **The damage window decides whether this is a hit at all**, and
        // everything below hangs off the answer. A pack all swinging in the
        // same tick used to land every blow, which is the one case the window
        // exists for.
        if (!damageCreature(*target, hit.damage)) {
            continue;
        }
        recordBlow(*target, hit.fromId);
        m_voices.push_back({target->kind, CreatureSound::Hurt, target->position, target->scale});
        rouse(*target, hit.fromId);
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
    // they just push apart. There is deliberately no vertical *push*, so one
    // standing on another stays there rather than being squeezed out - but
    // there is a vertical *bound*, which is a different thing and was missing.
    //
    // **This loop was the file's worst case of a rule that exists twelve lines
    // away.** `step` clamps the frame, refuses to move a body over an unloaded
    // column and skips corpses; `alertNeighbours`, immediately below, bounds
    // its pair test in Y. `separate` had none of the four, so a zombie in a
    // cave thirty metres under a pen shoved the cows above it, a corpse drifted
    // out of the herd while it toppled, and a chunk-loading hitch pushed an
    // animal several metres through a wall in one destination-only test. The
    // frame clamp now has one owner in `update`; the other three are here.
    //
    // Quadratic, and that is fine at a cap of fourteen plus whatever a slime
    // splits into. If the cap ever grows past a hundred this wants a grid.
    for (std::size_t i = 0; i < m_creatures.size(); ++i) {
        // **Skipped in the outer loop, so a dormant one costs one test rather
        // than a row of the quadratic.** Villagers kept for the player at every
        // village they have ever visited are exactly the population that would
        // turn this loop's own "fine at a cap of fourteen" comment false.
        if (m_creatures[i].dormant) {
            continue;
        }
        for (std::size_t j = i + 1; j < m_creatures.size(); ++j) {
            Creature& a = m_creatures[i];
            Creature& b = m_creatures[j];
            // And the other half of the pair: a shove is between two bodies
            // that both exist in the simulation, and a dormant one is a hundred
            // metres away in any case.
            if (b.dormant) {
                continue;
            }

            const CreatureSpecies& speciesA = speciesInfo(a.kind);
            const CreatureSpecies& speciesB = speciesInfo(b.kind);
            const float reach = speciesA.halfWidth * a.scale + speciesB.halfWidth * b.scale;
            glm::vec3 offset = b.position - a.position;
            // **Bodies that do not overlap in Y are not crowding each other.**
            // `alertNeighbours` twelve lines down has carried this bound all
            // along; without it the test was flat, so two squid at different
            // depths and two bees at different altitudes shoved each other at
            // over a metre a second. Measured against the taller of the two
            // rather than a constant, so a slime stack and a bee agree.
            const float span = std::max(speciesA.height * a.scale, speciesB.height * b.scale);
            if (std::abs(offset.y) >= span) {
                continue;
            }
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
            //
            // **Tested per body**, so a corpse or a body over an unloaded
            // column still pushes the living without being pushed itself.
            // `update` zeroes a corpse's horizontal velocity on the tick it
            // dies, with a comment saying that carrying it would slide the body
            // along the ground while it tips over - and then called this two
            // statements later, which did exactly that.
            const glm::vec3 nextA = a.position - push * strength;
            const glm::vec3 nextB = b.position + push * strength;
            if (a.health > 0 && onLoadedColumn(world, nextA) &&
                !bodyOverlapsSolid(world, speciesA, nextA, a.scale)) {
                a.position = nextA;
            }
            if (b.health > 0 && onLoadedColumn(world, nextB) &&
                !bodyOverlapsSolid(world, speciesB, nextB, b.scale)) {
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
        // **Correct as well as cheaper.** `rouse` sets an anger clock that only
        // `update` winds down, and `update` skips a dormant record - so rousing
        // one stores anger that never expires and wakes it, minutes or hours
        // later, hunting something that is long gone. The bound above is 41 m
        // at its widest and the struck creature is awake, so the only records
        // this can reach at all are ones sitting just past the ninety-metre
        // line. See `creatureById` for the rule.
        if (other.dormant) {
            continue;
        }
        if (other.kind != struck.kind) {
            continue;
        }

        const glm::vec3 offset = other.position - struck.position;
        if (std::abs(offset.y) > kAlertHeight ||
            offset.x * offset.x + offset.z * offset.z > species.alertRange * species.alertRange) {
            continue;
        }
        // Same kind by the filter above, so the species row `rouse` looks up is
        // the one already in hand.
        rouse(other, threatId);
    }
}
void Creatures::add(Creature creature) {
    creature.id = m_nextId++;
    // **Every animation phase is scattered here, at the one door in.** Both of
    // these are read only by the renderer and neither is a duration, so a fresh
    // roll per session is enough and there is nothing to save; what matters is
    // that no two individuals share one. `age` was seeded nowhere at all, which
    // left a reloaded world of bipeds swaying as one; `jetPhase` was seeded on
    // the squid's first tick, off a test on `age` that scattering `age` would
    // have silently disabled.
    creature.age = random01() * kAgePhaseSpan;
    creature.jetPhase = random01() * kTwoPi;
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
                        bool charged, bool playerBuilt, std::uint8_t profession,
                        const glm::ivec3& bedCell, const glm::ivec3& jobCell,
                        const glm::ivec3& meetCell) {
    // **A corrupt id is rejected here rather than trusted.** The record's kind
    // arrives as a plain number off the disk, and one out of range would index
    // the species table past its end - `speciesInfo` immediately below being the
    // first of a hundred readers to do it.
    //
    // The rule itself lives in `isKnownCreatureKind` beside the enum, so the
    // record reader can ask it *before* casting to `CreatureKind` and skip or
    // report the row, rather than casting blind and relying on this to catch
    // what it made. Same test, one owner, two callers.
    if (!isKnownCreatureKind(static_cast<std::int32_t>(kind))) {
        return;
    }
    Creature creature;
    creature.kind = kind;
    creature.health = health;
    creature.variant = rollVariant(kind);
    creature.position = feet;
    creature.yaw = yaw;
    creature.targetYaw = yaw;
    creature.headYaw = yaw;
    // **`kind` was treated as a value off the disk and these two were not**, and
    // the difference is not a matter of degree. `villagerSkinRow` computes
    // `kVillagerProfessionSkinRow + (profession - 1) * 64`, so a byte of 200
    // addresses row sixteen thousand of a sheet that has four thousand seven
    // hundred - and `professionName`'s `default:` answers "Unemployed" and hides
    // it. `scale` is worse: it multiplies straight into `bodyBox`, so a NaN or a
    // 1e30 off a truncated save produces a collision box that swallows the
    // world and shoves every creature in it.
    //
    // Clamped rather than rejected, because a save with one odd byte should
    // cost you a villager's coat and not the animal. `kNitwit` is the last
    // outfit in the sheet, so it is also the largest legal index.
    creature.scale = std::isfinite(scale) ? std::clamp(scale, kBabyScale, 1.0f) : 1.0f;
    creature.charged = charged;
    creature.playerBuilt = playerBuilt;
    creature.profession = std::min(profession, kNitwit);
    // A villager's whole working life. Without these three a reloaded armourer
    // could never reach `workStart`, which needs `jobCell.y >= 0`, and could
    // never re-claim either - the claim gate is closed to anyone who already has
    // a profession. Worse, the free-station scan reads every restored villager
    // as claiming nothing, so the next one to wake up takes the same anvil and
    // the village ends up with two armourers and no bed between them.
    creature.bedCell = bedCell;
    creature.jobCell = jobCell;
    creature.meetCell = meetCell;
    creature.phaseDelay = random01() * kSchedulePhaseStagger;
    pickWanderGoal(creature, m_random);
    add(creature);

    // **What comes back off the disk is what that chunk's one-off pass already
    // produced.** `m_populated` is session-only, so without this every reload
    // re-ran `populateChunks` over chunks whose animals had just been restored:
    // a second herd beside the first, and - because the village block answered
    // to no cap at all - two of every villager and two iron golems per village
    // after one load, six of each after five, standing in pairs and contending
    // for the same beds.
    //
    // The three claim cells are marked as well as the body, because a villager
    // walks and its bed does not. A resident restored at the far end of its
    // village would otherwise leave its own birth chunk unmarked and be born
    // again beside its own bed.
    markPopulated(feet);
    if (bedCell.y >= 0) {
        markPopulated(glm::vec3{bedCell});
    }
    if (jobCell.y >= 0) {
        markPopulated(glm::vec3{jobCell});
    }
    if (meetCell.y >= 0) {
        markPopulated(glm::vec3{meetCell});
    }
}

namespace {

/// The `m_populated` key, packed and unpacked, as one pair so the inverse
/// cannot drift from the forward direction. Both go through `std::uint32_t`:
/// the coordinates are signed, and letting either half sign-extend turns every
/// negative chunk - half the world - into a key that never matches.
constexpr std::uint64_t packColumn(int chunkX, int chunkZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
           static_cast<std::uint32_t>(chunkZ);
}

constexpr glm::ivec2 unpackColumn(std::uint64_t key) {
    return glm::ivec2{static_cast<int>(static_cast<std::uint32_t>(key >> 32)),
                      static_cast<int>(static_cast<std::uint32_t>(key))};
}

/// **The single edit that makes these fail is dropping either `std::uint32_t`
/// cast**, which is the whole hazard: a save written before the change reloads
/// with every negative column missing, the one-off animal pass runs again over
/// all of them, and the herd doubles on every reload west or north of the
/// origin. Nothing else in the system would report it.
static_assert(unpackColumn(packColumn(0, 0)) == glm::ivec2{0, 0});
static_assert(unpackColumn(packColumn(-1, -1)) == glm::ivec2{-1, -1});
static_assert(unpackColumn(packColumn(-7, 12)) == glm::ivec2{-7, 12});
static_assert(unpackColumn(packColumn(12, -7)) == glm::ivec2{12, -7});
static_assert(packColumn(-7, 12) != packColumn(12, -7), "and the two halves are not transposable");

} // namespace

std::uint64_t Creatures::populatedKey(int chunkX, int chunkZ) {
    return packColumn(chunkX, chunkZ);
}

void Creatures::markPopulated(const glm::vec3& feet) {
    m_populated.insert(populatedKey(static_cast<int>(std::floor(feet.x)) >> kChunkShift,
                                    static_cast<int>(std::floor(feet.z)) >> kChunkShift));
}

std::vector<glm::ivec2> Creatures::populatedColumns() const {
    std::vector<glm::ivec2> columns;
    columns.reserve(m_populated.size());
    for (const std::uint64_t key : m_populated) {
        // **The only place the packing is undone**, through the inverse that
        // sits beside the forward direction and is asserted against it.
        columns.push_back(unpackColumn(key));
    }
    return columns;
}

void Creatures::restorePopulatedColumn(int chunkX, int chunkZ) {
    m_populated.insert(populatedKey(chunkX, chunkZ));
}

void Creatures::restorePopulatedColumn(const glm::ivec2& column) {
    restorePopulatedColumn(column.x, column.y);
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
            const auto key = populatedKey(cx, cz);
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
            //
            // **Under the same ceiling the herd honours, which it was not.**
            // This block sat above the population guard as well as above the
            // `kChunkSpawnChance` roll, so village population answered to
            // nothing at all: every reload re-ran it over chunks whose people
            // had just been restored from disk, and after five loads a village
            // held six of every resident and six iron golems, contending in
            // pairs for the same beds. `restore` marking what it brings back is
            // what closes the duplication; this is what bounds whatever still
            // slips past it - a resident who wandered out of the chunk it was
            // born in before the save, which nothing in this file can see.
            //
            // **`activeCount`, not the raw size**, and here it matters most:
            // the creatures this ceiling is protecting the frame from are the
            // ones near the player, while the dormant records it would
            // otherwise count are villagers from *other* villages the player is
            // nowhere near. Counting those would mean the third village you
            // find generates nobody, which is the same empty village the
            // retirement bug produced, arriving by a different road.
            if (activeCount() < generatedCeiling(m_activeRadius)) {
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

                // The animals in the paddocks. Every pen in the world was a
                // fenced empty square until 2026-08-19 — the fences were built
                // and nothing ever put anything inside them, so a village was
                // no source of starting livestock at all.
                //
                // **This sits here, and not in the generator, for all four of
                // the reasons the two blocks above it do.** `village::
                // livestockIn` only *derives* where the animals stand, which
                // keeps chunk generation a pure function of `(seed,
                // chunkCoord)`; the spawn is a world mutation and so belongs to
                // the one owner of this list, on the main thread. Sitting
                // inside the `m_populated` guard is what stops a pen restocking
                // itself on every reload — the same defect that once left a
                // village holding six of every resident after five loads — and
                // sitting inside `generatedCeiling` is what keeps a herd of
                // cows from eating the creature budget.
                village::Livestock penned[village::kMaxLivestock];
                const int stock =
                    village::livestockIn(villages, cx, cz, penned, village::kMaxLivestock);
                for (int i = 0; i < stock; ++i) {
                    const village::Livestock& animal = penned[i];
                    place(animal.kind,
                          {static_cast<float>(animal.x) + 0.5f, static_cast<float>(animal.y),
                           static_cast<float>(animal.z) + 0.5f},
                          hashUnit(chunkHash(world.seed(), animal.x, animal.z)) * kTwoPi);
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
            const CreatureKind chosen = drawSpecies(weights, hashUnit(seed) * total);
            if (chosen == CreatureKind::Count) {
                continue;
            }

            const CreatureSpecies& species = speciesInfo(chosen);
            seed = chunkHash(seed, cx, cz);
            const int wanted = 1 + static_cast<int>(hashUnit(seed) * static_cast<float>(species.groupSize));

            for (int n = 0; n < wanted; ++n) {
                if (activeCount() >= generatedCeiling(m_activeRadius)) {
                    return;
                }
                seed = chunkHash(seed, n, cz);
                // **`kChunkSize`, not a 32 that happens to agree with it.** The
                // origin two lines up derives from `cx << kChunkShift`, so a
                // literal here agrees only while the shift is 5. Nothing would
                // change type if the chunk size moved, so the compiler could not
                // have caught it - spawns would silently cluster in one corner
                // of every chunk (failure shape #1).
                const int x =
                    originX + static_cast<int>(hashUnit(seed) * static_cast<float>(kChunkSize));
                seed = chunkHash(seed, n, cx);
                const int z =
                    originZ + static_cast<int>(hashUnit(seed) * static_cast<float>(kChunkSize));

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
                // `spawnsInOpenWater`, not `swims` - the turtle swims and is
                // still placed on the sand. One owner, three call sites.
                const int y = spawnsInOpenWater(species)
                                  ? waterColumnY(world, x, z, hashUnit(seed))
                                  : surface + 1;
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

namespace {

/// Per-slot conditions the reference states in words rather than in numbers.
///
/// A flag rather than an `if` beside the table for the reason the slime paid
/// for: its rule lived in a comment, the comment said the opposite of the code,
/// and every audit read the comment and moved on. A flag is checked by the one
/// emitter and cannot disagree with itself.
enum class LootFlag : std::uint8_t {
    None = 0,
    /// Bedrock's "only when killed by a player". Among the **common** drops the
    /// spider's eye alone; every rare drop is player-only as well and says so by
    /// being in the rare pool rather than by carrying this.
    PlayerKill = 1u << 0,
    /// Only when the killing blow was a bolt - the turtle's bowl, and nothing
    /// else in the roster.
    Lightning = 1u << 1,
    /// Fires **only if the slot above it did not**. The polar bear alone:
    /// Bedrock rolls raw cod at three-quarters *or* raw salmon at one quarter,
    /// and two independent chances would leave some bears carrying both and
    /// some carrying neither.
    Otherwise = 1u << 2,
};

constexpr std::uint8_t lootFlag(LootFlag flag) {
    return static_cast<std::uint8_t>(flag);
}

/// Chances are in tenths of a percent, so that the reference's own numbers go
/// in unrounded. Whole percents cannot hold the standard 2.5% rare drop, and a
/// denominator of forty - which can - cannot hold the drowned's 11%.
constexpr int kChanceScale = 1000;
constexpr std::uint16_t kAlways = static_cast<std::uint16_t>(kChanceScale);

/// One stack a death may leave behind.
struct LootDrop {
    ItemId item = ItemId::None;
    /// What `item` becomes when the body was burning. Bedrock's whole rule is
    /// the single bit `DeathContext::burning` carries - *"only when on fire"* -
    /// so lava, a campfire and an undead caught at dawn all cook it alike.
    /// Java additionally counts a Fire Aspect weapon; there is no enchanting
    /// here, which makes the simpler rule the complete one. `None` means this
    /// drop has no cooked form - a tropical fish has none.
    ///
    /// **Reachable at last**: `think` charges fire and lava contact, so a cow
    /// driven into a flame now drops steak. The sun still cooks the undead's
    /// drops too, which is the reference's rule - one bit, every source alike.
    ItemId cooked = ItemId::None;
    /// Uniform and inclusive. **A minimum of zero is the reference's own number
    /// for most of the roster**, and is the whole reason a kill can pay nothing.
    std::uint8_t min = 0;
    std::uint8_t max = 0;
    /// Out of `kChanceScale`.
    std::uint16_t chance = kAlways;
    std::uint8_t flags = 0;
};

/// One entry of the witch's weighted pool.
struct LootPoolEntry {
    ItemId item = ItemId::None;
    /// Relative, against whatever else the row lists. The reference gives the
    /// stick two shares and the other six one apiece, so a roll picks a stick
    /// **2 in 8 - a quarter**, and each of the rest an eighth. It is worth
    /// writing the denominator down: the redstone entry was missing here for a
    /// while, which made the same stick 2 in 7, and a pool's odds are the one
    /// thing you cannot read off a single row.
    std::uint8_t weight = 0;
    std::uint8_t min = 0;
    std::uint8_t max = 0;
};

/// Three, because the turtle - seagrass, scute and a bowl - is the widest row
/// in the roster today. Widening this is one number; the asserts below prove
/// nothing overflowed it.
constexpr std::size_t kLootSlots = 3;
/// Three, because the zombie's iron ingot, carrot and potato is the largest
/// shared pool the reference has for anything here.
constexpr std::size_t kLootRareSlots = 3;
/// Seven, which is the witch's pool exactly: stick, glass bottle, glowstone
/// dust, gunpowder, spider eye, sugar **and redstone**. It read six until the
/// redstone was found sitting outside the pool as a guaranteed drop.
constexpr std::size_t kLootPoolSlots = 7;

/// Everything one species leaves behind, one row each.
///
/// **The twin of `kSpecies`, deliberately.** It used to be a `switch`, which
/// meant a species could be added to the enum and simply drop nothing, silently
/// and for ever - which is how five of them came to. Adding an animal is now
/// adding a row in each of two adjacent arrays, and forgetting the second is a
/// build failure rather than an animal nobody notices is empty.
struct LootRow {
    std::array<LootDrop, kLootSlots> common{};

    /// One shared roll picks **one** of these. The reference's own rule, and
    /// the reason a zombie's iron ingot, carrot and potato are 0.833% each
    /// rather than 2.5% each - they share the 2.5%, they do not each get one.
    ///
    /// **Always player-only**, which every rare drop in the reference is. That
    /// is what `DeathCause::Player` exists for: reading `threatId == 0` instead
    /// would pay an iron ingot for every zombie that burned off at dawn.
    std::array<ItemId, kLootRareSlots> rare{};
    std::uint8_t rareCount = 0;
    /// Out of `kChanceScale`. 25 is the reference's flat 2.5%.
    std::uint16_t rareChance = 0;

    /// The witch alone. One to three rolls, each picking a single item by
    /// weight and paying nought to two of it, repeats allowed - which is how
    /// one witch can leave six sticks and another none at all.
    std::array<LootPoolEntry, kLootPoolSlots> pool{};
    std::uint8_t poolRollsMin = 0;
    std::uint8_t poolRollsMax = 0;
};

/// Every number below is Bedrock's, from `RESEARCH.md` §5.1 (the drop rules)
/// and §5.2 (the roster table), with the per-species loot tables themselves
/// read from Mojang's own published behaviour pack:
/// <https://learn.microsoft.com/en-us/minecraft/creator/reference/source/vanillabehaviorpack_snippets/loot_tables/entities/>
/// (one JSON per mob - `witch`, `spider`, `zombie_pigman` and the rest).
///
/// **That citation is load-bearing and it used to be a lie**: this comment
/// named `ml-04-mobdrops.md` §4.2 and §4.4, a file that has never existed in
/// this repository, so not one of these fifty-eight rows could be checked
/// against anything without leaving the tree. Cite something a reader can open.
///
/// **Where a minimum is zero it is zero in the reference**; this table used to
/// read `1` in **twenty-nine** places, so every kill paid out and nothing could
/// ever roll empty. Two of the twenty-nine turned out to be a *chance* rather
/// than a range - see the spider - which leaves twenty-seven reading `0` today.
///
/// Deliberately **not** sized `[Count]`: an explicit size would zero-fill a
/// forgotten row into a silently empty animal, where an unsized array trips the
/// assert underneath.
constexpr LootRow kLoot[] = {
    // Sheep. Its wool is the one drop the reference exempts from Looting in
    // both directions, death and shearing alike - which will matter when there
    // is shearing to exempt.
    {.common = {{{ItemId::RawMutton, ItemId::CookedMutton, 1, 2},
                 {itemForBlock(BlockId::WhiteWool), ItemId::None, 1, 1}}}},
    // Cow.
    {.common = {{{ItemId::RawBeef, ItemId::CookedBeef, 1, 3},
                 {ItemId::Leather, ItemId::None, 0, 2}}}},
    // Pig.
    {.common = {{{ItemId::RawPorkchop, ItemId::CookedPorkchop, 1, 3}}}},
    // Bramble. The reference's creeper also drops a music disc when a skeleton
    // kills it, which is handled beside the table because it needs to know what
    // killed it and a row cannot ask.
    {.common = {{{ItemId::Gunpowder, ItemId::None, 0, 2}}}},
    // Chicken. Exactly one bird, never two - the only meat in the roster with
    // no range at all.
    {.common = {{{ItemId::RawChicken, ItemId::CookedChicken, 1, 1},
                 {ItemId::Feather, ItemId::None, 0, 2}}}},
    // Cat. Dropped nothing until now.
    {.common = {{{ItemId::String, ItemId::None, 0, 2}}}},
    // Camel. Nothing: a saddle is only ever there because a player put it there.
    {},
    // Horse.
    {.common = {{{ItemId::Leather, ItemId::None, 0, 2}}}},
    // Mule.
    {.common = {{{ItemId::Leather, ItemId::None, 0, 2}}}},
    // Llama.
    {.common = {{{ItemId::Leather, ItemId::None, 0, 2}}}},
    // Donkey.
    {.common = {{{ItemId::Leather, ItemId::None, 0, 2}}}},
    // Goat. Nothing. Its horn is a ramming drop, not a death drop.
    {},
    // Rabbit. The foot is the one rare in the roster at ten per cent rather
    // than the standard two and a half.
    {.common = {{{ItemId::RawRabbit, ItemId::CookedRabbit, 0, 1},
                 {ItemId::RabbitHide, ItemId::None, 0, 1}}},
     .rare = {{ItemId::RabbitFoot}},
     .rareCount = 1,
     .rareChance = 100},
    // Wolf. Nothing - its armour drops only if a player put it on.
    {},
    // Frog.
    {},
    // Fox. Nothing of its own: Bedrock drops whatever it spawned holding, and
    // nothing here spawns a fox holding anything.
    {},
    // Ocelot.
    {},
    // PolarBear. Cod at three-quarters *or* salmon at one quarter, never both -
    // see `LootFlag::Otherwise`. Dropped nothing until now.
    {.common = {{{ItemId::RawCod, ItemId::CookedCod, 0, 2, 750},
                 {ItemId::RawSalmon, ItemId::CookedSalmon, 0, 2, kAlways,
                  lootFlag(LootFlag::Otherwise)}}}},
    // Panda. Nought to two is Bedrock's; Java gives exactly one.
    {.common = {{{itemForBlock(BlockId::Bamboo), ItemId::None, 0, 2}}}},
    // SlimeSmall. **The small one is the only size that drops slimeballs**, and
    // this table had it exactly backwards for twenty milestones behind a comment
    // that stated the rule confidently and stated it inverted. A large slime
    // still pays more than a small one, because it breaks into several of them.
    {.common = {{{ItemId::Slimeball, ItemId::None, 0, 2}}}},
    // SlimeMedium. Nothing - it splits, and the pieces pay.
    {},
    // SlimeLarge. Nothing, for the same reason.
    {},
    // Spider. The eye is player-only in the reference; a spider that burned to
    // death or drowned leaves string and nothing else.
    //
    // **One eye at 33.3%, not nought-to-one at fifty.** Bedrock writes it as
    // `set_count {min: -1, max: 1}` and clamps at zero, so the three outcomes
    // are -1, 0 and 1 and only the last pays - the widely quoted one-in-three.
    // A uniform 0-1 here reads identically at a glance and pays half again as
    // many eyes, which is the whole fermented-spider-eye economy off by 50%.
    {.common = {{{ItemId::String, ItemId::None, 0, 2},
                 {ItemId::SpiderEye, ItemId::None, 1, 1, 333,
                  lootFlag(LootFlag::PlayerKill)}}}},
    // CaveSpider. The reference groups it with the spider on one row.
    {.common = {{{ItemId::String, ItemId::None, 0, 2},
                 {ItemId::SpiderEye, ItemId::None, 1, 1, 333,
                  lootFlag(LootFlag::PlayerKill)}}}},
    // Zombie. One 2.5% roll picks one of the three, so each is 0.833% - not
    // three separate 2.5% rolls, which would be three times as generous.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 2}}},
     .rare = {{ItemId::IronIngot, ItemId::Carrot, ItemId::Potato}},
     .rareCount = 3,
     .rareChance = 25},
    // Skeleton. **Arrows are new**: three species carry them in the reference
    // and none of them dropped one here, which left arrows with no mob source.
    {.common = {{{ItemId::Bone, ItemId::None, 0, 2}, {ItemId::Arrow, ItemId::None, 0, 2}}}},
    // Villager.
    {},
    // Husk.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 2}}},
     .rare = {{ItemId::IronIngot, ItemId::Carrot, ItemId::Potato}},
     .rareCount = 3,
     .rareChance = 25},
    // Silverfish.
    {},
    // Blackbone. Outside the Overworld scope, but it is a row today and it was
    // wrong today. Its skull is not in the catalogue, so that 2.5% is left out.
    {.common = {{{ItemId::Coal, ItemId::None, 0, 1}, {ItemId::Bone, ItemId::None, 0, 2}}}},
    // Stray. Its arrow of Slowness needs a named tipped arrow and is left out.
    {.common = {{{ItemId::Bone, ItemId::None, 0, 2}, {ItemId::Arrow, ItemId::None, 0, 2}}}},
    // Bogged. Its arrow of Poison is left out for the same reason.
    {.common = {{{ItemId::Bone, ItemId::None, 0, 2}, {ItemId::Arrow, ItemId::None, 0, 2}}}},
    // ZombieVillager. The reference gives it the zombie's rare pool entire,
    // which is why its iron ingot is quoted at the same 0.833%.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 2}}},
     .rare = {{ItemId::IronIngot, ItemId::Carrot, ItemId::Potato}},
     .rareCount = 3,
     .rareChance = 25},
    // Witch. One to three rolls over a pool of **seven entries carrying eight
    // shares** - the stick counts twice - each paying nought to two of whatever
    // it picked, and **there is no guaranteed drop at all**.
    //
    // This row read "redstone 4-8 guaranteed, then a six-entry pool", which was
    // by a wide margin the largest fidelity error in the roster and the only
    // one that mattered economically. The reference's redstone is the seventh
    // entry of the same pool - weight 1, nought to two - so the expected yield
    // is 2 rolls x 1/8 x 1 = **0.25 dust per witch**. Guaranteed 4-8 pays 6.0
    // every single time: twenty-four times the reference, on the most valuable
    // common material in the game, which turns a lottery mob into a redstone
    // farm and every redstone recipe into a witch hunt.
    //
    // Her 8.5% potion needs her to be killed mid-drink, and nothing records
    // that yet.
    {.pool = {{{ItemId::Stick, 2, 0, 2},
               {ItemId::GlassBottle, 1, 0, 2},
               {ItemId::GlowstoneDust, 1, 0, 2},
               {ItemId::Gunpowder, 1, 0, 2},
               {ItemId::SpiderEye, 1, 0, 2},
               {ItemId::Sugar, 1, 0, 2},
               {ItemId::Redstone, 1, 0, 2}}},
     .poolRollsMin = 1,
     .poolRollsMax = 3},
    // WanderingTrader. Two leads, always, however it died. Dropped nothing
    // until now.
    //
    // **A deliberate divergence, and it is the "however it died" half.** The
    // reference drops the two leads because they are the ones tying its pair of
    // trader llamas, so a trader that never had llamas drops nothing - and ours
    // never has any, because llamas are not on the roster. Dropping them
    // unconditionally is the closest thing to the reference's *outcome* that
    // can be written before the llamas exist; the day they do, this becomes a
    // count of the leashed animals it is still holding.
    {.common = {{{ItemId::Lead, ItemId::None, 2, 2}}}},
    // Princepin. No common drops in the reference; its sword is equipment.
    {},
    // Drowned. Copper at 11% on a player kill. Its nautilus shell is 100% *if
    // it spawned holding one*, and nothing here records what a creature spawned
    // holding, so that is left out rather than guessed at a flat rate.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 2}}},
     .rare = {{ItemId::CopperIngot}},
     .rareCount = 1,
     .rareChance = 110},
    // Cod. Every fish leaves a bone a quarter of the time in Bedrock; Java
    // gives bone *meal* at a twentieth, and that is not the reference here.
    {.common = {{{ItemId::RawCod, ItemId::CookedCod, 1, 1},
                 {ItemId::Bone, ItemId::None, 1, 1, 250}}}},
    // Salmon. **Its own meat**, which it did not have until an earlier audit -
    // it shared the cod's row, so raw salmon was unobtainable.
    {.common = {{{ItemId::RawSalmon, ItemId::CookedSalmon, 1, 1},
                 {ItemId::Bone, ItemId::None, 1, 1, 250}}}},
    // Pufferfish. No cooked form in the reference, so none here.
    {.common = {{{ItemId::RawPufferfish, ItemId::None, 1, 1},
                 {ItemId::Bone, ItemId::None, 1, 1, 250}}}},
    // Squid.
    {.common = {{{ItemId::InkSac, ItemId::None, 1, 3}}}},
    // GlowSquid.
    {.common = {{{ItemId::GlowInkSac, ItemId::None, 1, 3}}}},
    // Turtle. Seagrass is the reference's actual death drop and is new here.
    //
    // **The scute is a deliberate divergence and stays until it has somewhere
    // else to come from.** In the reference a scute comes off a baby turtle
    // *growing up* and never off a corpse - but there is no growth clock here,
    // and the scute is the sole ingredient of a five-scute recipe, so removing
    // it would make that recipe unreachable rather than merely rare. Delete
    // this slot on the day baby turtles grow.
    {.common = {{{itemForBlock(BlockId::Seagrass), ItemId::None, 0, 2},
                 {ItemId::Scute, ItemId::None, 1, 1},
                 {ItemId::Bowl, ItemId::None, 1, 1, kAlways,
                  lootFlag(LootFlag::Lightning)}}}},
    // Dolphin.
    {.common = {{{ItemId::RawCod, ItemId::CookedCod, 0, 1}}}},
    // Axolotl.
    {},
    // TropicalFish. No cooked form, same as the pufferfish.
    {.common = {{{ItemId::RawTropicalFish, ItemId::None, 1, 1},
                 {ItemId::Bone, ItemId::None, 1, 1, 250}}}},
    // MushroomCow. The cow's row exactly; its mushrooms come off shears.
    {.common = {{{ItemId::RawBeef, ItemId::CookedBeef, 1, 3},
                 {ItemId::Leather, ItemId::None, 0, 2}}}},
    // SkeletonHorse.
    {.common = {{{ItemId::Bone, ItemId::None, 0, 2}}}},
    // ZombieHorse. Dropped nothing until now.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 2}}}},
    // TraderLlama.
    {.common = {{{ItemId::Leather, ItemId::None, 0, 2}}}},
    // PrincepinBrute. Its axe is equipment, not a drop.
    {},
    // ZombiePrincepin. Nought to one of each, unlike the ordinary zombie's
    // nought to two - and the gold ingot is its rare, at the standard 2.5% on a
    // player kill. It was the one undead in the roster without one, which left
    // the four others paying a rare and this one paying none.
    {.common = {{{ItemId::RottenFlesh, ItemId::None, 0, 1},
                 {ItemId::GoldNugget, ItemId::None, 0, 1}}},
     .rare = {{ItemId::GoldIngot}},
     .rareCount = 1,
     .rareChance = 25},
    // Voidmite.
    {},
    // MagmaCubeSmall. Nothing, which is the slime's rule inverted - and it is
    // inverted in the reference too, so both are as written.
    {},
    // MagmaCubeMedium.
    {.common = {{{ItemId::MagmaCream, ItemId::None, 0, 1}}}},
    // MagmaCubeLarge.
    {.common = {{{ItemId::MagmaCream, ItemId::None, 0, 1}}}},
    // Bee. Nothing - honey comes off the hive rather than off the bee, and as
    // of 2026-08-19 it finally gets there: `Pollinate` above flies a bee to a
    // flower, holds it there for `look_for_food`'s `stay_duration`, and queues
    // its home hive on `takePollinated` when it returns. That queue is the last
    // rung of the honeycomb chain and the one that lives outside this file -
    // `Main.cpp` drains it beside `takeGrazed` and raises the level.
    //
    // The cycle, and where each half lives, so that a reader who finds one
    // half broken knows where the other one is:
    //
    //   * **Placement.** `Structures.cpp` hangs an **empty** nest under the
    //     south edge of an oak canopy at Bedrock's per-biome chances - meadow
    //     100%, plains 5%, forest and dense forest 0.035%. Level 0 is not an
    //     oversight and must not be "fixed": a nest that generated full would
    //     invert the mechanic and make the bee pointless.
    //   * **Filling.** Here. One level per delivery, two on a 1% roll, capped
    //     at five by the drain.
    //   * **Harvest.** `Main.cpp` already shears a full hive for exactly 3
    //     honeycomb and empties it, and has done all along - that half was
    //     written long before anything could reach it.
    //
    // **Corroborated by Mojang directly, and it widens the ask by one block.**
    // `metadata/vanilladata_modules/mojang-blocks.json` in
    // `Mojang/bedrock-samples` publishes `honey_level` with the value list
    // `[0,1,2,3,4,5]`, which settles the six levels above. Its **users are
    // `beehive` *and* `bee_nest`, both of them** - so whoever widens the state
    // has to widen the pair, and a fix that touches only the crafted hive
    // leaves the naturally-generated nest unable to hold honey. That is this
    // project's failure shape 14 waiting to happen, and it is the whole reason
    // the block list is written down here rather than left as "the hive".
    //
    // **The count-the-users rule applies to that file and this is a case where
    // it passes.** A property's published value list there is the *storage
    // domain* for every block that carries it, not any one block's range, so a
    // list is only authoritative when exactly one block uses it. `honey_level`
    // has two users and they are the pair that must agree, so `[0..5]` is right
    // for both. Contrast `growth [0-7]`, which thirteen blocks share and which
    // therefore proves nothing about any single crop.
    {},
    // IronGolem. Three to five ingots always, and nought to two poppies - the
    // reference's own two pools, and the only mob whose drop is worth more than
    // it cost to build.
    {.common = {{{ItemId::IronIngot, ItemId::None, 3, 5},
                 {itemForBlock(BlockId::Poppy), ItemId::None, 0, 2}}}},
};

static_assert(std::size(kLoot) == static_cast<std::size_t>(CreatureKind::Count),
              "every CreatureKind needs a loot row");

/// Proves every row is well formed, at compile time.
///
/// The same shape as `survival::foodTablesAgree`, and for the same reason: both
/// sides are `constexpr`, so **none of this can rot**. Every clause is a bug
/// that has already been paid for once - a transposed range, a half-filled
/// slot, an `Otherwise` with nothing above it to be otherwise *to*, a cooked
/// form that is not food, and the salmon that shared the cod's row until its
/// own meat turned out to be unobtainable.
constexpr bool lootTableIsSane() {
    for (const LootRow& row : kLoot) {
        for (std::size_t i = 0; i < row.common.size(); ++i) {
            const LootDrop& drop = row.common[i];
            if (drop.item == ItemId::None) {
                // A range, a cooked form or a condition with no item to attach
                // it to - a row half-edited and left.
                if (drop.min != 0 || drop.max != 0 || drop.cooked != ItemId::None ||
                    drop.flags != 0) {
                    return false;
                }
                continue;
            }
            if (drop.min > drop.max || drop.max == 0) {
                return false;
            }
            if (static_cast<int>(drop.max) > maxStackFor(drop.item)) {
                return false;
            }
            if (drop.chance == 0 || drop.chance > kAlways) {
                return false;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (row.common[j].item == drop.item) {
                    return false;
                }
            }
            // `Otherwise` asks "did the slot above me fire?", and the emitter
            // answers that from a `previousFired` that starts `false` - so an
            // `Otherwise` in slot 0, or one sitting under an empty slot, fires
            // **unconditionally** while reading like a conditional. The rule
            // above forbids a half-filled slot but says nothing about a gap.
            //
            // The single edit that trips this: move the polar bear's salmon
            // slot to index 0, or delete its cod slot.
            if ((drop.flags & lootFlag(LootFlag::Otherwise)) != 0 &&
                (i == 0 || row.common[i - 1].item == ItemId::None)) {
                return false;
            }
            if (drop.cooked != ItemId::None) {
                // **What these two clauses actually guarantee, and no more:**
                // the raw side is food, and the cooked side is not *worse* than
                // the raw side. That is a downgrade check, not a pairing check.
                //
                // It does not catch a mispairing, and the obvious example
                // passes: raw beef paired with cooked *chicken* clears both,
                // because cooked chicken's 6 hunger is happily above raw beef's
                // 3. The clause that would close it is
                // `smeltResult(drop.item).item == drop.cooked`, which is the
                // question actually being asked - but `smeltResult`
                // (`item/Smelting.hpp`) is not `constexpr`, so it cannot be
                // called from here. Make it `constexpr` and this becomes a real
                // check for one line.
                if (survival::foodValue(drop.item).hunger <= 0) {
                    return false;
                }
                if (survival::foodValue(drop.cooked).hunger <
                    survival::foodValue(drop.item).hunger) {
                    return false;
                }
            }
        }
        if (static_cast<std::size_t>(row.rareCount) > row.rare.size()) {
            return false;
        }
        for (std::size_t i = 0; i < row.rare.size(); ++i) {
            const bool filled = i < static_cast<std::size_t>(row.rareCount);
            if (filled != (row.rare[i] != ItemId::None)) {
                return false;
            }
        }
        if ((row.rareCount > 0) != (row.rareChance > 0) || row.rareChance > kAlways) {
            return false;
        }
        if (row.poolRollsMin > row.poolRollsMax) {
            return false;
        }
        int weight = 0;
        for (const LootPoolEntry& entry : row.pool) {
            if (entry.item == ItemId::None) {
                if (entry.weight != 0 || entry.min != 0 || entry.max != 0) {
                    return false;
                }
                continue;
            }
            if (entry.weight == 0 || entry.min > entry.max || entry.max == 0) {
                return false;
            }
            if (static_cast<int>(entry.max) > maxStackFor(entry.item)) {
                return false;
            }
            weight += entry.weight;
        }
        // A pool with nothing in it, or contents nothing ever rolls for.
        if ((weight > 0) != (row.poolRollsMax > 0)) {
            return false;
        }
    }
    return true;
}

static_assert(lootTableIsSane(), "a loot row is malformed - see lootTableIsSane for which rule");

/// Which roll of a death this is.
///
/// Folded into the hash so the rolls of one death are independent: without it a
/// cow's beef and its leather would draw the same number and always agree, and
/// a slot's chance and its count would be the same coin flipped twice.
enum class LootRoll : int {
    Count,
    Chance,
    Pick,
};

/// Which independent roll a slot index names, past the common slots.
constexpr std::size_t kRareSlot = kLootSlots;
constexpr std::size_t kChainmailSlot = kLootSlots + 1;
constexpr std::size_t kDiscSlot = kLootSlots + 2;
constexpr std::size_t kPoolCountSlot = kLootSlots + 3;
constexpr std::size_t kPoolFirstSlot = kLootSlots + 4;

/// A drop roll, as a pure function of the death rather than a draw from a
/// running stream.
///
/// `random01` advances one shared xorshift that wander goals, ambient voices,
/// variant rolls and animation phases all draw from, so **every extra frame an
/// animal lived for shifted what it dropped** - the same kill in the same place
/// paid differently depending on the frame rate, and a reload re-rolled it.
/// Block drops sidestep this by being fixed (`dropCountForBlock` takes the
/// middle of each range), which a creature cannot do: the whole point of a 0-2
/// range is that some kills pay nothing.
///
/// Position is quantised to a sixteenth of a block **on purpose** - hashing the
/// raw float would make the answer depend on bits a re-simulation is not
/// obliged to reproduce - and it is read from `death.place`, latched on the
/// tick health crossed zero, **never from `creature.position`**. That is not a
/// nicety: `separate` shoves corpses exactly as it shoves the living, so a
/// body killed inside a herd is pushed for the whole `kDeathSeconds` before it
/// pays out, and that push is per-frame explicit Euler. Hashing where the body
/// came to rest would therefore be hashing the frame rate, in precisely the
/// commonest kill there is.
///
/// `deathTimer` and every other clock are deliberately out, because including
/// one is exactly how the frame-rate dependence gets back in. The honest limit:
/// `id` restarts at one each session and is not saved, so this is
/// session-stable rather than save-stable, and the death position carries the
/// rest.
std::uint32_t lootHash(std::uint32_t worldSeed, const Creature& creature, std::size_t slot,
                       LootRoll roll) {
    const std::uint32_t seed = worldSeed ^ creature.id;
    const std::uint32_t place = chunkHash(seed, creature.death.place.x, creature.death.place.y);
    return chunkHash(place, static_cast<int>(slot), static_cast<int>(roll));
}

} // namespace

void Creatures::emitLoot(const Creature& creature) {
    // **A baby leaves nothing.** `RESEARCH.md` §5.1: *"Baby animals drop no
    // common drops at all"*, which is two lines above the very table this row
    // set was built from - and `emitLoot` never asked. Babies are not a corner
    // case either: `populateChunks` rolls `species.babyChance` on every animal
    // it places, which is 5% for most of the roster and 25-30% for rabbits,
    // ocelots and axolotls, so a herd has been paying adult meat all along.
    //
    // Blanket rather than common-only, and that is not a shortcut: the
    // reference exempts babies from common drops but still lets them drop
    // *equipment*, and nothing in this roster spawns holding anything - every
    // species carrying `babyChance` is a passive animal with no rare pool
    // either. Revisit the day a baby zombie exists.
    //
    // `scale < 1.0f` is the same test `graze` already uses for "is this a
    // baby", rather than a second spelling of the rule beside it.
    if (creature.scale < 1.0f) {
        return;
    }

    const LootRow& row = kLoot[static_cast<std::size_t>(creature.kind)];
    const DeathContext& death = creature.death;

    // Every roll below is one of these two, and both are pure functions of the
    // death - see `lootHash`.
    const auto unit = [&](std::size_t slot, LootRoll roll) {
        return hashUnit(lootHash(m_seed, creature, slot, roll));
    };
    const auto passes = [&](std::size_t slot, int chance) {
        return static_cast<int>(lootHash(m_seed, creature, slot, LootRoll::Chance) %
                                static_cast<std::uint32_t>(kChanceScale)) < chance;
    };
    const auto rollCount = [&](std::size_t slot, int low, int high) {
        const int span = high - low + 1;
        return std::min(low + static_cast<int>(unit(slot, LootRoll::Count) *
                                               static_cast<float>(span)),
                        high);
    };
    const auto leave = [&](ItemId item, int count) {
        if (count > 0) {
            m_loot.push_back({creature.position, item, count});
        }
    };

    // Walked in order, because `Otherwise` asks about the slot above it.
    bool previousFired = false;
    for (std::size_t slot = 0; slot < kLootSlots; ++slot) {
        const LootDrop& drop = row.common[slot];
        bool fires = drop.item != ItemId::None;
        if (fires && (drop.flags & lootFlag(LootFlag::PlayerKill)) != 0) {
            fires = death.cause == DeathCause::Player;
        }
        if (fires && (drop.flags & lootFlag(LootFlag::Lightning)) != 0) {
            fires = death.cause == DeathCause::Lightning;
        }
        if (fires && (drop.flags & lootFlag(LootFlag::Otherwise)) != 0) {
            fires = !previousFired;
        }
        if (fires && drop.chance < kAlways) {
            fires = passes(slot, drop.chance);
        }
        previousFired = fires;
        if (!fires) {
            continue;
        }
        // Bedrock's cooked-on-fire rule, and the only reader of the bit latched
        // when health crossed zero. Every burning source qualifies in the
        // reference, and now in this game too: the sun on the undead, a fire
        // block and lava all run through the one `burnTimer` in `think`, so all
        // three cook what they kill.
        const ItemId item =
            death.burning && drop.cooked != ItemId::None ? drop.cooked : drop.item;
        leave(item, rollCount(slot, drop.min, drop.max));
    }

    // The rare pool: **one** roll decides whether anything rare fell, and a
    // second decides which - never one roll each, or a zombie's three rares
    // would be three times as likely as the reference's one.
    if (row.rareCount > 0 && death.cause == DeathCause::Player &&
        passes(kRareSlot, row.rareChance)) {
        const int pick = std::min(static_cast<int>(unit(kRareSlot, LootRoll::Pick) *
                                                   static_cast<float>(row.rareCount)),
                                  static_cast<int>(row.rareCount) - 1);
        leave(row.rare[static_cast<std::size_t>(pick)], 1);
    }

    // The witch's pool. Each roll picks one item by weight and pays nought to
    // two of it, and the rolls are independent of each other - which is why one
    // witch can leave six sticks and the next one nothing.
    if (row.poolRollsMax > 0) {
        int weight = 0;
        for (const LootPoolEntry& entry : row.pool) {
            weight += entry.weight;
        }
        const int rolls = rollCount(kPoolCountSlot, row.poolRollsMin, row.poolRollsMax);
        for (int n = 0; n < rolls; ++n) {
            const std::size_t slot = kPoolFirstSlot + static_cast<std::size_t>(n);
            int ticket = std::min(
                static_cast<int>(unit(slot, LootRoll::Pick) * static_cast<float>(weight)),
                weight - 1);
            for (const LootPoolEntry& entry : row.pool) {
                if (entry.item == ItemId::None) {
                    continue;
                }
                ticket -= entry.weight;
                if (ticket < 0) {
                    leave(entry.item, rollCount(slot, entry.min, entry.max));
                    break;
                }
            }
        }
    }

    // The reference's only source of a music disc: a creeper killed by a
    // skeleton's arrow leaves one, every time. **The one drop in the roster
    // that needs a *mob* to have done it** - which is why the death context
    // carries who as well as whether, and why this cannot be a table row.
    //
    // **Which mobs can do it is derived from `shootsArrows`, not listed.** The
    // reference's rule is about the *arrow*, not about the family: the
    // `#minecraft:skeletons` tag also holds the wither skeleton - our
    // `Blackbone` - and it still cannot drop a disc, because it carries a sword
    // and never fires anything. (Minecraft Wiki, "Music Disc": a wither
    // skeleton only manages it if a command gives it a bow.) So the set that
    // qualifies is exactly the set that carries one, which `archersShareOneRig`
    // already pins, and a fourth archer species joins it for free rather than
    // being forgotten here - the failure this file pays for more than any
    // other.
    //
    // **The one divergence, stated at the site:** we do not check that the
    // killing blow was the arrow rather than a bite. Nothing records what a
    // blow was made with; when something does, this wants that test too.
    if (creature.kind == CreatureKind::Bramble && death.cause == DeathCause::Creature) {
        const Creature* killer = creatureById(death.killerId);
        if (killer != nullptr && speciesInfo(killer->kind).shootsArrows) {
            const int index = std::min(
                static_cast<int>(unit(kDiscSlot, LootRoll::Pick) *
                                 static_cast<float>(kMusicDiscs)),
                kMusicDiscs - 1);
            leave(musicDiscAt(index), 1);
        }
    }

    // Chainmail has no recipe in the reference either - it is armour worn by
    // the undead and taken off them. **This is its only source**, so without it
    // four catalogue entries would be unreachable rather than merely rare.
    //
    // **All four pieces drop here, and a name-based sweep cannot see three of
    // them.** Finding 940 reported the chestplate and the leggings as having no
    // producer of any kind; that is wrong, and it is wrong in an instructive
    // way. The only chainmail ids spelled out in this file are `ChainmailHelmet`
    // - the base the roll offsets from - and `ChainmailBoots`, which appears
    // solely inside the `static_assert` below. The other two are produced by
    // arithmetic and appear nowhere as a name, so a grep for
    // `ChainmailChestplate` returns nothing and reads as an absent feature.
    // That is `CLAUDE.md` bug shape #15's false-positive class exactly: a value
    // routed through an index cast is invisible to a call-site sweep. `piece`
    // runs 0 to 3 over `ChainmailHelmet + piece`, so helmet, chestplate,
    // leggings and boots are each drawn a quarter of the time this fires.
    //
    // Deliberately still outside the table, and still 4%: Bedrock's actual rule
    // is 25% of a piece the mob *spawned wearing*, and nothing here records
    // what anything spawned wearing. It belongs in the equipment rule the day
    // one exists; a row cannot hold it, because a zombie's rare pool is already
    // spoken for by iron, carrot and potato.
    //
    // **This list is deliberately not the archer list above.** It is "the
    // undead that wear armour", which is a different question with a different
    // answer - the `Bogged` is an archer and wears none, and the `Husk` wears
    // armour and shoots nothing. Written out rather than derived because there
    // is no flag for it yet, and the flag is the equipment rule's to add.
    //
    // **This list is deliberately not the archer list above.** It is "the
    // undead that wear armour", which is a different question with a different
    // answer - the `Bogged` is an archer and wears none, and the `Husk` wears
    // armour and shoots nothing. Written out rather than derived because there
    // is no flag for it yet, and the flag is the equipment rule's to add.
    if (creature.kind == CreatureKind::Zombie || creature.kind == CreatureKind::Skeleton ||
        creature.kind == CreatureKind::Husk || creature.kind == CreatureKind::Stray) {
        if (passes(kChainmailSlot, 40)) {
            // **The four pieces must be contiguous and in this order**, because
            // the roll indexes off the helmet. `Item.hpp` lists them helmet,
            // chestplate, leggings, boots; inserting anything between two of
            // them would silently start dropping whatever landed in the gap,
            // and nothing here changes type, so only this can catch it.
            static_assert(static_cast<int>(ItemId::ChainmailBoots) -
                                  static_cast<int>(ItemId::ChainmailHelmet) ==
                              3,
                          "chainmail pieces must stay contiguous, helmet first");
            const int piece =
                std::min(static_cast<int>(unit(kChainmailSlot, LootRoll::Pick) * 4.0f), 3);
            leave(static_cast<ItemId>(static_cast<int>(ItemId::ChainmailHelmet) + piece), 1);
        }
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
    //
    // **Counted rather than tracked**, so the dormant tally cannot drift from
    // the flags it is a count of: every pass sets every flag and adds them up.
    std::size_t dormant = 0;
    for (std::size_t i = m_creatures.size(); i-- > 0;) {
        Creature& creature = m_creatures[i];
        const CreatureSpecies& species = speciesInfo(creature.kind);
        const glm::vec3 offset = creature.position - playerFeet;
        const bool tooFar = glm::dot(offset, offset) > m_activeRadius * m_activeRadius;
        // **Two kinds of creature must survive the distance test, and until
        // today neither did.** A villager carries a profession, a bed, a job
        // site and a meeting point - the four fields `kCreatureVersion` was
        // bumped to 4 to save - and `populateChunks` marks a column populated
        // on first visit and never returns to it, so a retired villager is not
        // regenerated when you walk back. A golem is worse: the player spent
        // iron and a pumpkin on it, and `playerBuilt` is saved precisely
        // because who built it is not a cosmetic detail. Walking ninety metres
        // emptied your village permanently at the next autosave, with no loot,
        // no particle and no log line to say it had happened.
        //
        // **Both flags already existed and are already saved**, which is what
        // makes this a predicate rather than a new column: `keepsHouse` is the
        // villager row's own and `playerBuilt` is per individual.
        //
        // **The golem is named by kind and that is deliberate.** A village's
        // guards are placed by `populateChunks` from the plan, not by the
        // ambient spawner - `spawnsIn` returns false for `IronGolem` and says
        // so in a comment - so a retired one never returns either, and the two
        // ways a golem can exist (built by the player, generated with the
        // village) both produce something unrepeatable. `playerBuilt` alone
        // would have saved half of them.
        //
        // **Not derived from "cannot spawn naturally", tempting as that is.**
        // The same test is true of a slime's children, which are produced by
        // splitting rather than by the spawner: deriving the rule would have
        // made every small slime immortal and, with the cap exemption below,
        // let a walk past a swamp accumulate them without limit. Three named
        // cases beat a derivation that is right about two of them.
        //
        // `killed` and the void floor still retire them - dying and falling out
        // of the world are things that happen *to* a villager, not the world
        // forgetting about one.
        const bool persistent =
            species.keepsHouse || creature.playerBuilt || creature.kind == CreatureKind::IronGolem;
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

        // **Kept costs nothing beyond its bytes.** A persistent creature out of
        // range goes dormant instead of being simulated: `update` skips its
        // `think` and `step`, `separate` skips it in both halves of a quadratic
        // loop, `buildMesh` builds no geometry for it, and the spawn caps do
        // not count it. Without that, every village ever visited would be paying
        // full price every frame for animals nobody can see.
        //
        // **The verdict is taken once and read twice**, so the flag and the
        // count cannot disagree with the removal: a persistent villager that
        // falls out of the world is retired *and* must not be counted dormant
        // on the way out.
        //
        // The floor is `survival::kVoidDepth` rather than a bare -8, which is
        // the same constant `Player.cpp` reads and carries a long note on why
        // the reference's -64 must not be ported: that figure is Bedrock's
        // world floor and ours is Y = 0, so what transfers is the clearance
        // below the floor and not the number. This was the only site in the
        // engine still spelling it as a literal (2026-08-19).
        const bool retire =
            (tooFar && !persistent) || killed || creature.position.y < survival::kVoidDepth;
        creature.dormant = tooFar && persistent && !retire;
        if (creature.dormant) {
            ++dormant;
        }

        if (retire) {
            // Only a kill leaves anything behind. Retiring at a distance or
            // burning off is the world forgetting about an animal, and a trail
            // of meat at the edge of the render distance is not that.
            if (killed) {
                emitLoot(creature);
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

    // **Published after the additions, not before.** `add` and the split above
    // both push creatures that are by definition standing beside the player, so
    // every one of them is awake - the tally is still exactly the number of
    // dormant records in the vector, and `activeCount` stays honest.
    m_dormant = dormant;

    m_spawnTimer -= deltaSeconds;
    if (m_spawnTimer > 0.0f || activeCount() >= capacityFor(m_activeRadius)) {
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
            const int spawnY = spawnsInOpenWater(species) ? swimY : y;
            if (spawnY < 0) {
                continue;
            }
            // Fish are the reference's `water_ambient` category, which carries
            // neither a light rule nor a time-of-day one - the sea is dark
            // enough at depth and they are there at noon regardless. **Keyed on
            // `swims` rather than on where the body is put**, deliberately: the
            // turtle is placed on sand by the line above and is still an
            // aquatic, and the reference's turtle spawn rule names a biome and
            // a surface and no light or time condition either.
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

        // The same draw the chunk spawner makes, and now literally the same
        // function: this one fell back to `Sheep` when the roll missed, which
        // is a spawn nothing on the shortlist ever qualified for.
        const CreatureKind chosen = drawSpecies(weights, random01() * total);
        if (chosen == CreatureKind::Count) {
            continue;
        }

        Creature creature;
        creature.kind = chosen;
        creature.health = speciesInfo(chosen).health;
        creature.variant = rollVariant(chosen);
        const int spawnY = spawnsInOpenWater(speciesInfo(chosen)) ? swimY : y;
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
        // **A second source, not a stand-in, and this comment used to say the
        // opposite.**
        //
        // RETRACTED, not a claim: "standing in for lightning until weather exists"
        //
        // That removal condition has been met since M27, so a reader who
        // believed it would have deleted a path that
        // `kChargedChance`'s own definition calls permanent. Weather is real,
        // `Main.cpp` drives `applyLightning` off a live bolt and that is what
        // charges a Bramble in a storm; this roll is what keeps a few of them
        // in the world *between* storms, which is the reference's own reason
        // for having both. Corrected 2026-08-19 against finding 10369.
        //
        // The retracted sentence is quoted rather than dropped so the
        // correction explains itself - and flagged on its own line because a
        // token sweep cannot tell an assertion from a quotation of a withdrawn
        // one, which had this finding re-filed as live three times in a day.
        //
        // **What would make this wrong:** `applyLightning` losing its
        // `Main.cpp` caller, or `Weather.cpp` ceasing to emit bolts.
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

std::size_t Creatures::findAimed(const glm::vec3& eye, const glm::vec3& forward, float reach,
                                 std::uint32_t ignoreId, float* entryDistance) const {
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
        // **First, because this runs every frame and the ray is not free.** A
        // dormant record is past the active radius, so it is further than any
        // reach this is ever called with - the player's arm, a projectile's
        // step, the block-breaking test that asks whether something is in the
        // way. It was being fed to the slab test purely to be rejected on
        // distance, once per remembered villager per frame. See `creatureById`.
        if (creature.dormant) {
            continue;
        }
        // Whoever the shot is still inside. Ids start at one, so the usual zero
        // asks this of nobody and costs one comparison.
        if (creature.id == ignoreId && ignoreId != 0) {
            continue;
        }
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
    // Only meaningful when something was found; `nearest` still holds `reach`
    // otherwise, and reporting that as a hit distance would be a lie the caller
    // cannot tell from a graze at maximum range.
    if (entryDistance != nullptr && found != m_creatures.size()) {
        *entryDistance = nearest;
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

bool Creatures::strike(const glm::vec3& eye, const glm::vec3& forward, float reach, int damage,
                       std::uint32_t fromId, std::uint32_t ignoreId, float* hitDistance,
                       std::size_t* hitIndex) {
    float entry = 0.0f;
    const std::size_t index = findAimed(eye, forward, reach, ignoreId, &entry);
    if (index == m_creatures.size()) {
        return false;
    }
    // **Answered before the window, not after.** Every path below this either
    // returns true or falls through to returning true, so the ray's answer is
    // settled here and the two out-parameters are filled once, where the one
    // function that owns the question is standing. Filling them beside the
    // damage instead would have the absorbed-blow early return hand back a hit
    // with no distance and no index.
    if (hitDistance != nullptr) {
        *hitDistance = entry;
    }
    if (hitIndex != nullptr) {
        *hitIndex = index;
    }

    Creature& target = m_creatures[index];
    // **The same window `applyHits` goes through**, which is what stops a fast
    // weapon - or a swarm of arrows arriving in one frame - landing every blow.
    //
    // The caller is still told `true`, because what it asked was "did the swing
    // connect with a creature", not "did it hurt one": `Main.cpp` hangs the
    // swing cooldown and the tool's durability off this answer, and a player
    // whose blow the window absorbed would otherwise get a free swing at no
    // wear and no cooldown. Only the damage, the cry, the shove and the anger
    // are filtered.
    if (!damageCreature(target, damage)) {
        return true;
    }
    recordBlow(target, fromId);
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
    // **Whoever actually struck**, which is not always the player: this is the
    // path an arrow resolves through as well as the player's own swing, and it
    // used to write a flat zero. A skeleton shooting past you and clipping your
    // wolf therefore turned that wolf - and, through `alertNeighbours`, its
    // whole pack - on the one person in the fight who had not fired. Zero still
    // names the player, which is why the id counter starts at one, and it is
    // still what the melee caller passes.
    rouse(target, fromId);

    // A clean kill starts no war. The reference has the same exemption, and it
    // is what stops one-shotting a lone animal turning its whole species on
    // you - the neighbours never saw anything happen.
    if (target.health > 0) {
        alertNeighbours(target, index, fromId);
    }
    return true;
}

int Creatures::applyLightning(const glm::vec3& at) {
    int caught = 0;
    for (Creature& creature : m_creatures) {
        // **The one site where skipping changes an outcome, and it changes it
        // the right way.** Weather rolls strikes out to `kSimulationRadius`,
        // 128 m, which reaches past the ninety-metre active radius - so a bolt
        // could land within `kLightningReach` of a dormant villager. Killing
        // one is precisely what `Creature::dormant` exists to prevent: the
        // village empties while the player is away, off screen, with nothing to
        // see and nothing to have done about it. See `creatureById` for the
        // rule the rest of these guards follow.
        if (creature.dormant) {
            continue;
        }
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

        // **Through the window, not around it.** A bolt is an ordinary blow in
        // the reference, not a hazard tick with its own cadence, so it belongs
        // where every other blow goes. Writing `health -=` here also let the
        // value go negative, which `damageCreature`'s clamp does not.
        //
        // `recordHazard` is gated on the blow actually landing, and that guard
        // is the point: it overwrites `cause` and clears `killerId`, so a bolt
        // swallowed by the window of the player's own sword hit would have
        // taken the kill off them and with it their rare drops. That is the
        // same blame-stealing shape the drowning tick already paid for.
        if (damageCreature(creature, kLightningDamage)) {
            recordHazard(creature, DeathCause::Lightning);
        }
        // Counted either way: `caught` means the bolt reached it, which the
        // Bramble branch above already establishes by counting a charge that
        // deals no damage at all.
        ++caught;
    }
    return caught;
}

int Creatures::hurtInBox(const glm::vec3& low, const glm::vec3& high, int damage, DeathCause cause) {
    // **Nothing to do is answered here rather than at every call site.**
    // `anvilLandingDamage` returns 0 for gravel, for sand and for an anvil that
    // has not fallen far enough to hurt, so a caller draining a queue of
    // landings would otherwise need this test itself - and `damageCreature`
    // with a zero would still spend the immunity window and set the hurt flash,
    // which is a mob flashing red for a falling sand block.
    if (damage <= 0) {
        return 0;
    }

    int caught = 0;
    for (Creature& creature : m_creatures) {
        // Ninety metres from the player and out of play - and an anvil that
        // crushed a dormant villager would empty the village off screen, which
        // is the failure dormancy was introduced to stop. See `creatureById`.
        if (creature.dormant) {
            continue;
        }
        // Already at zero and waiting for the next `manage` to retire it. The
        // same guard `applyExplosion` opens with, and the reason is the one
        // `hazardDamage` was written for: a corpse that keeps being damaged has
        // its `cause` and `killerId` overwritten, and the kill - with its rare
        // drops - is taken off whoever earned it.
        if (creature.health <= 0) {
            continue;
        }

        const CreatureSpecies& species = speciesInfo(creature.kind);
        const Aabb body = bodyBox(species, creature.position, creature.scale);
        // **Written to match `Main.cpp`'s player test clause for clause**, with
        // the creature's body where the player's box goes. Strict on both sides,
        // so boxes that merely touch faces do not count as overlapping - a mob
        // standing exactly flush beside the cell is missed, exactly as a player
        // standing there is. The whole value of this entry point is that the
        // two answers cannot drift apart; writing a different inequality here
        // would be the drift arriving on day one.
        if (body.min.x >= high.x || body.max.x <= low.x || body.min.y >= high.y ||
            body.max.y <= low.y || body.min.z >= high.z || body.max.z <= low.z) {
            continue;
        }

        // **Through the window, not around it** - the same choice `applyLightning`
        // and `applyExplosion` make, and here it is load-bearing rather than
        // merely consistent. A tower of anvils collapses into one cell across
        // consecutive frames, and the player's path leans on `damagePlayer`'s
        // half-second window to make that land as a single hit. A creature going
        // around the window would take the whole stack, so the same trap would
        // kill a pig and bruise a player.
        //
        // `recordHazard` gated on the blow landing, for the reason it is gated
        // everywhere else: it overwrites `cause` and clears `killerId`, so an
        // anvil the window swallows must not take the kill off the player who
        // was mid-swing.
        if (damageCreature(creature, damage)) {
            recordHazard(creature, cause);
        }
        // Counted whether or not the damage landed, matching `applyLightning`:
        // the number answers "did it reach anything?", which is the question a
        // caller draining landings wants and which the window does not change.
        ++caught;

        // Deliberately **not** provoked, for the reason `applyExplosion` spells
        // out: `provokedTimer` can only ever mean "the player did this", so
        // angering a mob at an anvil that fell on its own would send it after
        // somebody who did nothing.
    }
    return caught;
}

int Creatures::provokeNear(const glm::vec3& centre, float radius, CreatureKind kind,
                           std::uint32_t threatId) {
    // **Nothing here damages, and that is the whole reason it exists.** The only
    // public way to anger a group was `strike`, which lands a blow to do it - so
    // "shearing a hive angers its bees" could not be written without also
    // wounding them. `rouse` sets the anger clock and the blame, and touches
    // health, knockback and the hurt flash not at all.
    const float reachSquared = radius * radius;
    int roused = 0;
    for (Creature& creature : m_creatures) {
        // Anger a dormant record cannot spend: `update` skips it, so the clock
        // `rouse` starts never winds down and the creature wakes hunting
        // something that stopped existing hours ago. The same argument as
        // `alertNeighbours`, and see `creatureById` for the rule.
        if (creature.dormant) {
            continue;
        }
        if (creature.kind != kind || creature.health <= 0) {
            continue;
        }
        const glm::vec3 offset = creature.position - centre;
        if (glm::dot(offset, offset) > reachSquared) {
            continue;
        }
        // The species' own anger duration, never a constant passed in: a bee
        // stays angry twenty-five seconds and a bear five hundred, and a caller
        // that had to know which would be a second owner of that number.
        rouse(creature, threatId);
        ++roused;
    }
    return roused;
}

int Creatures::applyExplosion(const World& world, const glm::vec3& centre, float power) {
    int caught = 0;
    for (Creature& creature : m_creatures) {
        // **Before `withinBlast`, not after.** The pre-filter below is the
        // cheap question, but "cheap" is per creature per detonation and a
        // dormant record is ninety metres out - further than `2 x power` for
        // any charge in this game, so it can only ever be rejected. See
        // `creatureById` for the rule.
        if (creature.dormant) {
            continue;
        }
        // Whatever set this off is already at zero and waiting to be retired.
        // Flinging its corpse is not worth the one frame it would show for.
        if (creature.health <= 0) {
            continue;
        }

        const CreatureSpecies& species = speciesInfo(creature.kind);
        // **The cheap question first.** `explosionExposure` fires about thirty-six
        // DDA rays through the world for one body, and every one of them is
        // wasted on something the blast cannot reach at all: outside twice the
        // power the impact is zero whatever the exposure says, so the work was
        // bought and then thrown away by the `impact <= 0` test below. This is
        // the pre-filter `Explosion.hpp` publishes for exactly that reason, and
        // calling it keeps the `2 x power` reach in one place instead of
        // growing a second copy here - which is the shape that has already cost
        // this project a blast radius that disagreed with its own crater.
        if (!withinBlast(centre, power, creature.position)) {
            continue;
        }
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

        // **Through the window, as the reference does.** A blast is a blow, not
        // a hazard on its own clock, and going around the window meant a creeper
        // detonating on the same frame as a sword swing charged the mob twice -
        // and a second creeper in a chain charged it a third time. That is
        // precisely the double-charge `damageCreature` exists to stop. It also
        // clamps, where `health -=` here could leave the value negative.
        //
        // `recordHazard` gated on the blow landing, so a blast the window
        // swallows cannot take the kill off the player who was mid-swing.
        if (damageCreature(creature, explosionDamage(power, impact))) {
            recordHazard(creature, DeathCause::Explosion);
        }
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
            // **Thrown even when the window swallowed the damage**, which is
            // the reference's split too: being caught in a blast moves you
            // whether or not your health bar does. Hanging the throw off the
            // damage instead would make a mob that had just been hit stand
            // impossibly still in an explosion.
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
        // **Not redundant with the range test below, and not a second owner of
        // it.** `DrawRange` answers "is it inside the drawn world", which is
        // another file's number; `dormant` answers "is this a record being kept
        // for the player rather than a creature in play", which is this file's.
        // They agree today, because the active radius is the render distance
        // floored at `kBaseRadius` - and if they ever stop agreeing, the
        // village you left behind must not appear as a lone figure standing in
        // fog, which is a thing it could never do while it was being deleted.
        if (creature.dormant) {
            continue;
        }
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

            // **Arms swing less than legs, and by a published amount.** The
            // reference's `animation.humanoid.move` drives `leftarm` with
            // `tcos0` against `leftleg`'s `tcos0 * -1.4`, and Java's
            // `HumanoidModel` sets 1.0 radian of arm against 1.4 of leg -
            // `ANIMATION.md` 2.4 states the ratio in bold. Both arms had been
            // reading the leg amplitude outright, which is a march.
            //
            // Derived from `swing` rather than given a species column of its
            // own: one number, two limbs, and no way for the pair to drift.
            constexpr float kArmSwingRatio = 1.0f / 1.4f;
            const float armSwing = swing * kArmSwingRatio;
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
            const float swayRoll = std::cos(creature.age * kIdleSwayRollRate) * 0.05f + 0.05f;
            const float swayPitch = std::sin(creature.age * kIdleSwayPitchRate) * 0.05f;

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
                    // **Scaled, like every other half-extent in this file.**
                    // These two were bare metres, so a chick - `kBabyScale`
                    // 0.55 - wore full-size legs and full-size feet under a
                    // half-size body, and a puffed-up chicken's legs did not
                    // grow with it. The bee's wing and the mooshroom's sprite
                    // are the precedent: a half-extent is always
                    // `* modelScale * swellWide`.
                    const float halfStrip = 0.03125f * modelScale * swellWide;
                    const float halfFoot = 0.09375f * modelScale * swellWide;
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
                // Half-width scaled with the model, like every other extent -
                // it was a bare metre, so a camel's tail stayed one width
                // whatever size the animal was drawn at.
                const float halfTail = 0.09375f * modelScale * swellWide;
                skinQuad(tailLow - side * halfTail, tailLow + side * halfTail,
                         tailHigh + side * halfTail, tailHigh - side * halfTail,
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
                    // Scaled with the model, like every other half-extent. A
                    // bare metre here meant a tadpole-scale frog stood on
                    // full-size feet.
                    const float half = 0.125f * modelScale * swellWide;
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
                           blend(-armSwing + swayPitch, aimPitch + swayPitch),
                           blend(swayRoll, kArcherStringArmRoll + swayRoll));
                    mainPitch = blend(armSwing - swayPitch - humanoidRaise, aimPitch - swayPitch);
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
                // **The wind-up's sign was inverted, and it dropped her hands
                // instead of lobbing.** `turn` is the assembly's own frame
                // rotation, whose positive direction carries the hands forward
                // and up; `pitch` is `uprightBox`'s, whose positive direction
                // leans a limb's far end backwards. They are the same rotation
                // in two conventions, so the rigid pair is `+armRaise` against
                // `kFold - armRaise` - and that pair reaches -1.60 rad at full
                // wind-up, which is `kWitchArmRaise`'s own documented "just past
                // straight out" (`kArcherArmPitch` is -1.5708). Written the
                // other way round it reached +0.10 - arms hanging down - and
                // put the hands 5 cm behind the pivot and 13 below their rest.
                const float turn = armRaise;
                const auto folded = [&](float alongForward, float up, float alongSide) {
                    const ModelPoint turned = foldedArmPoint({alongForward, up, alongSide}, turn);
                    return place(turned.alongForward, turned.up, turned.alongSide);
                };

                uprightBox(folded(0.1477f, 1.221f, 0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold - armRaise);
                uprightBox(folded(0.1477f, 1.221f, -0.375f), 4.0f, 8.0f, 4.0f,
                           44.0f, 22.0f, skin, 0.005f, kFold - armRaise);
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
                           40.0f, 38.0f, skin, 0.005f, kFold - armRaise, 0.08f);

                // The bottle, held where the hands are. `villagerHandPoint` now
                // applies the swing itself, so the thrown potion and the drawn
                // one are the *same call with the same argument* rather than two
                // places agreeing by hand - which they did not: the throw used
                // the resting hand and the mesh the turned one.
                const ModelPoint grip = villagerHandPoint(armRaise);
                const float pitch = kFold - armRaise;
                const glm::vec3 gripUp = boxHeightAxis(pitch, 0.0f);
                const LimbEnd hands{place(grip.alongForward, grip.up, grip.alongSide), gripUp,
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
                villagerRig(kWitchSkin, false, witchArmRaise(creature));
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

                uprightBox(place(0.0f, 1.125f, 0.0f), kPigTorsoWidth, 12.0f, 4.0f,
                           16.0f, 16.0f, skin, 0.0f);
                constexpr float kPigHip = 0.133f;
                legBox(place(0.0f, 0.375f, kPigHip), 4.0f, 12.0f, 4.0f,
                       16.0f, 48.0f, skin, 0.005f, swing);
                legBox(place(0.0f, 0.375f, -kPigHip), 4.0f, 12.0f, 4.0f,
                       0.0f, 16.0f, skin, 0.005f, -swing);
                // **Derived from `bipedHandPoint`'s own shoulder rule**, not
                // written down beside it: that function anchors the weapon, and
                // a shoulder measured twice is a weapon floating off the arm
                // holding it. Same for the rest height - `legBox` hangs the
                // joint off it and `bipedHandPoint` assumes it.
                constexpr float kPigArmWidth = 4.0f;
                constexpr float kPigShoulder = kBipedShoulderGap + kPigArmWidth * kTexel * 0.5f;
                // **The relationship, not the answer.** This used to pin
                // `kPigShoulder == 0.375f`, which proved only that nobody had
                // retyped the sum - it would have fired on a perfectly correct
                // widening of `kBipedShoulderGap` and stayed silent on the one
                // edit that matters. What actually has to hold is that the
                // shared shoulder gap *is* this torso's half-width, so the arm
                // hangs flush against its side rather than floating off it or
                // sinking into it. The single edit that makes it fail: widening
                // the torso above to a ten-texel brute without widening the
                // gap, or moving the gap without the torso.
                static_assert(kBipedShoulderGap == kPigTorsoWidth * kTexel * 0.5f,
                              "the biped shoulder gap must be this torso's half-width");
                // The **off** hand, on the creature's left: (32,48) is the left
                // arm on the player sheet, which `bipedHandPoint`'s own comment
                // cites as the check for which side is which.
                legBox(place(0.0f, kBipedArmRestUp, kPigShoulder), 4.0f, 12.0f, 4.0f,
                       32.0f, 48.0f, skin, 0.005f, -armSwing + swayPitch, swayRoll);
                // The main hand, on the right at (40,16) - and it is the arm the
                // blow now comes from. `humanoidRaise` was bolted to the left
                // arm above, so all three of these swung the empty hand at you
                // while the sword hand hung still; and the sword was never drawn
                // at all, because this rig never called `heldItem` despite every
                // one of the three carrying a weapon in `kSpecies`.
                const float mainPitch = armSwing - swayPitch - humanoidRaise;
                const float mainRoll = -swayRoll;
                LimbEnd mainHand = legBox(place(0.0f, kBipedArmRestUp, -kPigShoulder), 4.0f, 12.0f,
                                          4.0f, 40.0f, 16.0f, skin, 0.005f, mainPitch, mainRoll);
                // The anchor is `bipedHandPoint`, exactly as the biped rig does
                // it: the limb supplies the three axes, because only it has
                // them, and the grip comes from the one function that knows
                // where a hand ends up.
                const ModelPoint grip = bipedHandPoint(4.0f, mainPitch, mainRoll, true);
                mainHand.tip = place(grip.alongForward, grip.up, grip.alongSide);
                heldItem(species.heldMainHand, mainHand, false);
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
                // **The same sign on all four, and the sprawl alone alternates.**
                // The gait term used to alternate too - `{+p, -p, -p, +p}` - and
                // because `limbYaw` already flips the axis for indices 1 and 2,
                // that second alternation cancelled it: working each limb back
                // through its own yaw, all four displaced the *same* way in
                // world space, so the animal shuffled sideways with its feet in
                // lockstep instead of walking. With one sign, the yaw does the
                // alternating and the diagonals fall out correctly - `{0,3}`
                // (front one side, rear the other) swing together against
                // `{1,2}`, which is a trot.
                const float lean[4]{-kSprawl + paddle, kSprawl + paddle, -kSprawl + paddle,
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

            // **The sheep is named rather than reached by falling off the end,
            // and that is the whole of this guard.** Every other species above
            // tests its own kind and `continue`s; the sheep used to be whatever
            // was left, so a fifty-ninth species added without a branch of its
            // own would have been drawn silently **as a sheep** - right shape,
            // right skin, wrong animal - on a clean build. This is an if/else
            // chain rather than a `switch`, so no `-Wswitch` can see the gap,
            // and `C4062` is off at `/W4` anyway (`CLAUDE.md` bug shape #10,
            // filed as finding 10292).
            //
            // **Anything that reaches the `continue` renders nothing at all**,
            // which is the deliberate half: an invisible creature that still
            // walks, collides and can be hit is conspicuous the first time it
            // is spawned, where a plausible-looking sheep is not. Same choice
            // made for `TreeShape`'s terminal branch, which grows nothing
            // rather than an oak.
            if (creature.kind != CreatureKind::Sheep) {
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
