#pragma once

#include "world/Effects.hpp"
#include "world/Fluid.hpp"
#include "world/Survival.hpp"

#include <glm/glm.hpp>

namespace game {

class World;

/// Player dimensions and motion, in metres and metres per second.
///
/// One block is one cubic metre, so these are directly comparable to real human
/// proportions. Values match the genre's established feel as a starting point;
/// they are tuning numbers, not part of the game's identity, and are expected to
/// change once there is real content to move through.
namespace player_constants {

constexpr float kWidth = 0.6f;
constexpr float kHeight = 1.8f;
constexpr float kEyeHeight = 1.62f;

/// Crouching shrinks the box from the top down, so the feet stay put.
constexpr float kSneakHeight = 1.5f;
constexpr float kSneakEyeHeight = 1.27f;

/// How fast the camera slides between standing and crouched eye level. The
/// collision box switches instantly; only the view is eased, because a camera
/// that teleports vertically reads as a glitch.
constexpr float kEyeAdjustSpeed = 8.0f;

/// Ledges up to this high are climbed automatically. Without it, every
/// one-block rise stops you dead and uneven ground is miserable to walk on.
constexpr float kStepHeight = 0.6f;

constexpr float kWalkSpeed = 4.317f;
constexpr float kSprintSpeed = 5.612f;
constexpr float kSneakSpeed = 1.295f;

/// What honey leaves you of your walking speed. The reference's own factor.
constexpr float kStickySpeedScale = 0.4f;

/// How much of an impact a slime block returns, and the speed below which it
/// simply stops you - without a floor, resting on slime jitters for ever.
constexpr float kSlimeBounce = 0.8f;
/// A bed returns less than slime does - the reference's own two thirds against
/// slime's four fifths - and cancels the fall outright, which is why dropping
/// onto one never hurts.
constexpr float kBedBounce = 0.66f;
constexpr float kBounceThreshold = 1.5f;
constexpr float kFlySpeed = 11.0f;
/// Flying with sprint held. Fast enough to cross terrain quickly, but opt-in
/// rather than the default, which made ordinary flying uncontrollable.
constexpr float kFlySprintSpeed = 22.0f;

/// Flight eases into and out of motion rather than snapping to full speed.
/// Deceleration is the gentler of the two, which is what reads as gliding.
constexpr float kFlyAcceleration = 38.0f;
constexpr float kFlyDeceleration = 26.0f;

/// Walking ramps up and down too. Stopping is quicker than starting, so the
/// player still feels planted rather than skating.
///
/// These are the **dry-land** figures. `SurfaceMotion` scales both, along with
/// the top speed, by the slipperiness of whatever is underfoot, so ice is the
/// same two numbers seen through one block property rather than a rule of its
/// own. Ordinary ground scales by exactly 1.
constexpr float kGroundAcceleration = 30.0f;
constexpr float kGroundDeceleration = 42.0f;

/// Mid-air steering is deliberately feeble, and air drag is close to nothing:
/// that is what makes a jump commit to its arc instead of being flown. It is
/// also what carries a glide off the edge of an ice sheet.
constexpr float kAirAcceleration = 9.0f;
constexpr float kAirDeceleration = 2.0f;

constexpr float kGravity = 32.0f;
constexpr float kTerminalVelocity = 78.4f;

/// Chosen so the jump apex is ~1.25 blocks: high enough to clear one block,
/// not high enough to clear two.
constexpr float kJumpVelocity = 8.944f;

/// The apex the line above claims, checked rather than asserted in prose:
/// `v² / 2g` against the reference's own 1.2522 m (`RESEARCH.md` §1.5).
/// **Changing either constant on its own fires this** - taking `kGravity` down
/// to the 26 m/s² creatures currently use puts the apex at 1.54 blocks, which
/// clears a block-and-a-half and turns every one-block wall into a step.
static_assert(kJumpVelocity * kJumpVelocity / (2.0f * kGravity) > 1.24f &&
                  kJumpVelocity * kJumpVelocity / (2.0f * kGravity) < 1.26f,
              "the jump must clear one block and not two: apex ~1.25 m");

} // namespace player_constants

/// Position is the centre of the player's feet, which is the natural anchor for
/// standing on a surface.
struct Player {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    bool onGround = false;
    bool flying = false;

    /// Held across frames rather than read from input, because standing back up
    /// is refused when there is no headroom.
    bool sneaking = false;

    /// True while any part of the body is in water. The reference switches its
    /// entire movement model on this, not on being fully under, so wading in
    /// the shallows is already swimming.
    bool inWater = false;

    /// True while the eye is under the surface. Separate from `inWater`,
    /// because breath and the swim state both ask about the head alone.
    bool underwater = false;

    /// Sprint-swimming: faster, slipperier, and it holds depth with no input
    /// because water gravity is skipped outright while it is on.
    ///
    /// Starting it needs the head under; keeping it only needs to be in water,
    /// which is what lets you sprint-swim along the surface.
    bool swimming = false;

    /// Seconds since any part of the body was last in water. Only the airborne
    /// horizontal reads it - see `fluid::kSwimGrace` for what it is for.
    float sinceWater = 0.0f;

    /// Mid-stroke while treading water. Latched across `fluid::kStroke` rather
    /// than recomputed, because a drive that fades out as the head clears is
    /// first-order and settles dead - the latch is what keeps the bob going.
    bool treading = false;

    /// Seconds of breath left, counting down only while the eye is submerged.
    float air = fluid::kAirSeconds;

    /// Counts on past empty, and every whole second of it is two health points.
    float drowningSeconds = 0.0f;

    // --- Survival. M21. `world/Survival.hpp` owns every constant behind these.

    /// Half-hearts, 0 to 20. Zero is dead.
    int health = survival::kMaxHealth;

    /// The hunger bar, and the two numbers behind it that never appear on
    /// screen. **Saturation is spent before food and is capped at the food
    /// level**; exhaustion is an accumulator that costs a saturation point
    /// every time it fills.
    int food = survival::kMaxFood;
    float saturation = 5.0f;
    float exhaustion = 0.0f;

    /// **The rule that makes melee survivable.** While this is running a blow
    /// no larger than `lastDamage` is ignored and a larger one deals only the
    /// difference - so a creature standing inside you cannot kill in a frame,
    /// and damage is capped at two hits a second from any one source.
    float invulnerableSeconds = 0.0f;
    int lastDamage = 0;

    /// Cosmetic, and deliberately shorter than the invulnerability so the two
    /// are never mistaken for one another.
    float hurtFlash = 0.0f;

    /// **Durability owed to each worn armour piece, waiting to be collected.**
    ///
    /// The damage rule knows how much wear a blow caused; it does not know what
    /// the player is wearing, because the armour lives in an `Inventory` and
    /// `world/` has no business reaching into `item/` storage. So this is the
    /// project's own "separate computing the result from applying it" rule made
    /// concrete: the simulation banks what is owed, and whoever owns the
    /// inventory drains it with `Inventory::wearArmour` and zeroes this.
    ///
    /// **A point here is per piece, not shared between them** - four pieces each
    /// pay this whole number, which is the reference's rule and which
    /// `wearArmour` implements.
    ///
    /// **Drained, and this said the opposite until 2026-08-19 evening.** It read
    /// "Nothing drains it yet, so it climbs and is harmless: `wearArmour` is the
    /// only consumer and it has no caller until the inventory screen can put
    /// armour on." All three clauses are false. `Main.cpp` runs
    /// `if (inventory.wearArmour(player.armourWear) > 0)` every frame, guarded on
    /// positive, and armour goes on either by clicking an armour cell or by
    /// right-clicking a held piece, both through `inventory.equipArmour`.
    /// Cited by symbol rather than by line, deliberately - `Survival.hpp`'s armour
    /// block explains why, but the short version is that the line numbers that
    /// stood here had drifted by roughly +50 within hours of being written.
    ///
    /// **Still deliberately not saved.** Wear is banked between one frame and the
    /// next and collected the same frame; there is nothing to persist. The
    /// durability itself lives on the stacks, which are saved.
    int armourWear = 0;

    /// How far it has dropped since it last stood on something. Fall damage is
    /// **change in Y, not speed**, which is why this is a distance.
    float fallDistance = 0.0f;

    /// Cadences for the hazards that tick rather than land once, and the
    /// counters that heal and starve. Separate timers because they run at
    /// different rates and one shared counter would make them interfere.
    float hazardTimer = 0.0f;
    float burnTimer = 0.0f;
    float burningSeconds = 0.0f;
    float regenTimer = 0.0f;
    float starveTimer = 0.0f;

    /// **How frozen, in seconds of exposure, and it is not a countdown.**
    ///
    /// Rises at real time while any cell the body occupies holds powder snow and
    /// falls at `survival::kFreezeRecoveryRate` times real time once out, capped
    /// at `survival::kFreezeOnsetSeconds` - which is Bedrock's `TicksFrozen`
    /// counted in this file's own unit rather than in ticks (minecraft.wiki,
    /// *Powder Snow*: "+1 every tick, to a maximum of 140 ... decreases at a
    /// rate of 2 per tick after the entity leaves"). Damage starts at the cap,
    /// so re-entering before it has drained resumes where it left off, which is
    /// the sentence immediately after that one.
    ///
    /// **A ratio of this to the onset is what the frosty vignette and the cyan
    /// hearts are drawn from**, and neither exists yet - both are the HUD's, and
    /// this is the field they will want.
    ///
    /// **Not saved, deliberately.** Bedrock persists `TicksFrozen` on the
    /// entity; quitting here thaws you. That is a divergence of at most seven
    /// seconds of exposure, in the player's favour, and adding it to
    /// `SavedPlayer` would cost a format rung for it.
    float freezeSeconds = 0.0f;
    /// The damage cadence once fully frozen. Its own timer rather than the
    /// shared `hazardTimer`, because freezing is 40 ticks and every contact
    /// hazard is 10 - `Survival.hpp` asserts the two apart.
    ///
    /// **It rests at `survival::kFreezeInterval`, not at zero**, so that the
    /// first point lands *at* the seven-second onset rather than two seconds
    /// after it. Anything that resets it to zero - the Creative early-out,
    /// `respawnPlayer` - is resetting `freezeSeconds` to zero at the same time,
    /// and the next frame primes this from the same branch, so zero is a
    /// transient rather than a state the damage path can ever see.
    float freezeTimer = 0.0f;

    /// Everything a potion has put on the player.
    ///
    /// **Saved since 2026-08-19, and this note used to say the opposite.** It
    /// read "Not saved, and that is a divergence rather than a match", went on
    /// for three paragraphs about what the save record would need, and every
    /// word of it is now false: `SavedPlayer` (`WorldStore.hpp`) carries
    /// `std::array<SavedEffect, kSavedEffectSlots>` with `kSavedEffectSlots` =
    /// 32, plus `absorption` and `absorptionSeconds` as plain floats, and
    /// `Main.cpp` fills all three. Drink an eight-minute potion, quit, and it is
    /// still running when you come back, with its amplifier and its remaining
    /// duration - which is Bedrock's behaviour.
    ///
    /// **Clearing on death is still right and is a different rule.**
    /// minecraft.wiki, *Death*, no edition tag: "The player also loses all
    /// effects upon respawning, even when the game rule
    /// `keep_inventory`/`keepinventory` is set to `true`." `respawnPlayer`
    /// below does that; the save path does not, and the two must not be
    /// confused for one another.
    ///
    /// **Nothing on this side is measured on disk, but this class's *count* is.**
    /// The round trip runs on the public API - `Effects::all()` out,
    /// `Effects::apply()` back in - so no field here is a record field. What
    /// does cross over is `effects::kMaxActive`: the save site carries
    /// `static_assert(effects::kMaxActive <= game::kSavedEffectSlots)`, so
    /// widening `Effect` past 32 storable ids fails the build rather than
    /// silently dropping effects from every save. `Effects.hpp` says so at
    /// `kMaxActive`.
    effects::Effects effects;
    /// Their own cadences, because regeneration, poison and wither each run at
    /// an interval that depends on how strong they are - a shared timer would
    /// make Regeneration II tick at Regeneration I's rate.
    float effectHealTimer = 0.0f;
    float effectHurtTimer = 0.0f;

    /// Absorption points still held, and the effect's remaining seconds as this
    /// file last saw them.
    ///
    /// **A balance, not a level**, which is the whole reason it needs a field
    /// rather than a call: `effects::absorptionPoints` returns what a grant is
    /// *worth*, and the wiki says these points "cannot be replenished by
    /// natural regeneration or other effects" (`minecraft.wiki/w/Absorption`).
    /// So they are spent once and only a fresh grant restores them - which a
    /// function of the running effects cannot express, because the effect is
    /// still running at full level when the pool is empty.
    ///
    /// The second field is how a *fresh grant* is recognised without a hook at
    /// the eat site (which lives in `Main.cpp` and is not ours to reach into):
    /// `Effects::apply` is the only thing that can make the remaining seconds
    /// go **up**, and the tick is the only thing that brings them down. A rise
    /// is therefore exactly "it was applied again", and it catches the upgrade
    /// (I to IV) and the top-up at the same level with one comparison.
    float absorption = 0.0f;
    float absorptionSeconds = 0.0f;

    /// How long the meal in hand has been going. Reset the moment the button
    /// comes up or the stack changes.
    float eatingSeconds = 0.0f;

    /// How long it has been dead. The world keeps running underneath, which is
    /// what lets the body settle before the screen takes over.
    float deathSeconds = 0.0f;

    bool alive() const { return health > 0; }

    float eyeOffset = player_constants::kEyeHeight;

    /// Metres the camera still trails the feet after stepping up.
    ///
    /// The collision box snaps to the new height and only the *view* eases up
    /// after it. Ramping the box instead would leave a part-way body inside the
    /// block it is climbing, which is a whole family of stuck states.
    /// **Only the camera may read this** - reach, targeting and knockback all
    /// want the true eye.
    float stepSmooth = 0.0f;

    glm::vec3 eyePosition() const { return position + glm::vec3{0.0f, eyeOffset, 0.0f}; }

    /// Where the camera actually sits: the eye, trailing briefly after a step.
    glm::vec3 renderEyePosition() const {
        return eyePosition() - glm::vec3{0.0f, stepSmooth, 0.0f};
    }

    float height() const {
        return sneaking ? player_constants::kSneakHeight : player_constants::kHeight;
    }
};

/// What the player is asking to do this frame. Produced from input by game code,
/// so the physics itself never reads the keyboard.
struct PlayerInput {
    /// Desired horizontal direction in world space; need not be normalised.
    glm::vec3 moveDirection{0.0f, 0.0f, 0.0f};
    /// How much of full speed to move at, 0 to 1.
    ///
    /// **A separate field because `moveDirection` is normalised**, so a stick
    /// pushed halfway and one pushed to the stop are the same vector by the
    /// time the physics sees them - and the difference between a stroll and a
    /// walk is most of what an analogue stick is for. A keyboard leaves it at 1.
    float moveScale = 1.0f;
    bool jump = false;
    bool sprint = false;
    bool sneak = false;
    /// Only used while flying.
    float verticalWish = 0.0f;
    /// Vertical component of where the camera is pointing, positive up.
    /// **Only sprint-swimming reads it**: `moveDirection` is flattened so that
    /// looking down cannot drive you into the ground, and this is what puts the
    /// pitch back for the one case that wants it.
    float lookY = 0.0f;
    /// Creative. Nothing hurts and nothing is spent - and it arrives here
    /// rather than being read from the settings, because the physics has no
    /// business knowing what a game mode is.
    bool invulnerable = false;

    /// **What the player is wearing, for the frame's hazards.** Same reasoning
    /// as `invulnerable` directly above, and it is the reason this is an input
    /// rather than a field on `Player`: the physics has no business knowing what
    /// an `Inventory` is, and a copy living on `Player` would be saved to
    /// `player.dat` and could come back stale against the armour actually worn.
    ///
    /// Refreshed from `Inventory::armourSet()` every frame by whoever owns the
    /// inventory, exactly as `invulnerable` is refreshed from the game mode.
    /// **Left at `kNoArmour` it means "naked", which is today's behaviour
    /// exactly** - so nothing changes until it is filled in.
    ///
    /// Only the hazards armour actually reduces read it - lava, standing in
    /// fire, magma and cactus. Suffocation, drowning, burning-over-time,
    /// falling, starvation, freezing and the void ignore it by passing
    /// `survival::kNoArmour` at their own call sites rather than by being
    /// filtered here.
    survival::ArmourSet armour{};

    /// **Whether any worn piece is leather, and whether the boots specifically
    /// are.** Two flags rather than one, because powder snow states two
    /// different rules against two different sets and they are not
    /// interchangeable - a leather cap with iron boots is immune to freezing and
    /// still falls in. minecraft.wiki, *Powder Snow*: "Wearing any piece of
    /// leather armor stops the freezing effect", and separately "Entities
    /// wearing leather boots ... do not fall through powder snow."
    ///
    /// **Why they are not derivable from `armour` above.** That carries defence
    /// points and toughness - a number, not a material - so the set that gives 3
    /// defence could be leather boots or gold ones, and `ArmourSet` must not
    /// grow a material field for the same reason it has none now: the physics
    /// has no business knowing what an `ItemId` is. These are two booleans for
    /// the same reason `invulnerable` is one.
    ///
    /// **Boots imply leather, and the reader ORs them rather than trusting a
    /// filler to know that** - `Player.cpp` tests `leatherBoots || leatherArmour`
    /// for freezing immunity, so a caller that sets only the narrower flag still
    /// gets the wider rule. Belt as well as braces, and it costs one `||`.
    ///
    /// **Filled since 2026-08-19 11:27, and this note used to say they were
    /// not.** It read *"nothing changes until whoever owns the inventory fills
    /// them"* - true when written, and a trap the moment it stopped being.
    /// `Main.cpp` now sets both in the same frame as `armour` above; grep
    /// `move.leatherArmour` for the pair. **Do not add a second filling site** -
    /// an unmet-looking spec in a header is exactly how one field acquires two
    /// writers, and this paragraph was one reader away from causing it.
    ///
    /// **The filler is careful in a way worth not undoing.** It walks all
    /// `kArmourSlots` and compares `armourMaterial(worn.item)` against
    /// `armourMaterial(ItemId::LeatherHelmet)` rather than against `0`, guarding
    /// that test with `isArmour` - because `armourMaterial` is
    /// `(item - LeatherHelmet) / 4` and C++ truncates a negative quotient toward
    /// zero, so any id in the three slots below `LeatherHelmet` would otherwise
    /// read as leather out of a hand-edited `player.dat`.
    ///
    /// **What would falsify this note:** more than one assignment to
    /// `move.leatherArmour`, or `Player.cpp` ceasing to test
    /// `leatherBoots || leatherArmour`.
    bool leatherArmour = false;
    bool leatherBoots = false;
};

/// Advances the player by one frame against the world.
///
/// Reads the world, writes only the player. `deltaSeconds` is clamped
/// internally, because a long stall must not let the player move far enough in
/// one step to pass straight through a wall.
void updatePlayer(Player& player, const PlayerInput& input, const World& world, float deltaSeconds);

/// **The one way the player takes damage**, and the only place the half-second
/// invulnerability window is applied.
///
/// **`true` means "a hit landed", not "health went down"**, and the two have
/// genuinely come apart: Resistance can round a small blow to nothing, and
/// Absorption can swallow a large one whole, while both still flash, still cost
/// exhaustion and still arm the window. That is the reference's own split
/// between deciding a hit happened and deciding what it costs (`RESEARCH.md`
/// §2.4), and it is the answer every plausible consumer wants - knockback,
/// aggro, hit sounds and statistics all follow the *hit*, which is why the
/// reference knocks you back under Resistance. A caller that genuinely needs
/// "did the health bar move" must compare `player.health` across the call;
/// nothing does today, so this returns one value rather than two.
///
/// `bypassInvulnerability` is for the handful of sources the reference exempts
/// - starvation and the void - which are not blows and must not be shrugged off
/// by having just taken one.
/// `armour` is **what protects against this particular blow**, not simply what
/// is worn: a source armour does not reduce passes `survival::kNoArmour`, and
/// so does a naked player. See `survival::ArmourSet` for why those two are
/// deliberately the same value. Passing a real set also charges durability into
/// `player.armourWear` for the inventory's owner to collect.
bool damagePlayer(Player& player, int amount, bool bypassInvulnerability = false,
                  survival::ArmourSet armour = survival::kNoArmour);

void healPlayer(Player& player, int amount);

/// Eats one of something. Saturation is clamped to the food bar on the way in,
/// which is what stops a rich meal on an empty stomach banking more than it
/// should.
void feedPlayer(Player& player, const survival::FoodValue& value);

/// Puts the player back on their feet with everything reset.
void respawnPlayer(Player& player, const glm::vec3& at);

/// True if a block at these coordinates would intersect the player's box.
/// Placing there would seal the player inside solid geometry.
bool playerOverlapsBlock(const Player& player, const glm::ivec3& block);

} // namespace game
