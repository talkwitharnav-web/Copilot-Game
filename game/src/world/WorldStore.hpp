#pragma once

#include "world/Chunk.hpp"
#include "world/Campfire.hpp"
#include "world/Furnace.hpp"
#include "world/Chest.hpp"
#include "world/TerrainGenerator.hpp"
#include "item/Inventory.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <vector>

namespace game {

/// Where the player was when they last quit, so they resume rather than respawn.
///
/// Widened at M21 to carry survival state. **`kFormatVersion` was bumped with
/// it**, so an older file is refused and the player resumes at full health at
/// spawn rather than being read as garbage - a save record is a stated list of
/// fields, and growing one silently is how a struct starts lying about itself.
/// **How many effect slots a player record has room for - a fact on disk, and
/// deliberately not `effects::kMaxActive`.**
///
/// `kMaxActive` is derived (`storableEffectCount()`, one slot per storable id)
/// and it has *already moved once*, from 8 to 22, when the comment beside it
/// was made true. Sizing a saved record by it would have silently changed the
/// on-disk layout of every player file that day, with no version bump and no
/// diagnostic - the reader would have read the right number of bytes into the
/// wrong fields. So the file's width is stated here, in the file that owns the
/// format, and `WorldStore.cpp` asserts it is still wide enough. Widening
/// `Effects.hpp` past this fails the build instead of corrupting saves.
inline constexpr std::size_t kSavedEffectSlots = 32;

/// One running status effect, as written down.
///
/// **A record of its own rather than `effects::ActiveEffect` written raw**, for
/// the same reason the legacy player rungs are spelled out rather than read as
/// prefixes: `ActiveEffect` belongs to `Effects.hpp` and may gain a field for
/// reasons that have nothing to do with saving. If it did, and this were a raw
/// copy, every player file on disk would change shape without anything here
/// changing at all.
///
/// **`secondsLeft` is a remaining duration and must never become a timestamp.**
/// A `steady_clock::time_point` is measured from an arbitrary origin that the
/// operating system is free to reset at boot, so one written down before a quit
/// means nothing at all after it - the effect would come back already expired,
/// or running for years. `ActiveEffect` already stores a duration, so this is a
/// copy rather than a conversion, and that is the whole reason it is safe.
///
/// `id` is the raw `effects::Effect` number. **That enum's numbering is now a
/// fact on disk**, exactly as item ids are, and renumbering it turns every
/// saved potion into a different one - see `migrateItemId` for the machinery
/// this project already has when that becomes necessary.
struct SavedEffect {
    std::int32_t id = 0;
    std::int32_t amplifier = 0;
    float secondsLeft = 0.0f;
};

/// **BEFORE YOU ADD, MOVE OR REORDER A MEMBER OF THIS STRUCT, read this.**
/// Written 2026-08-19. It states a constraint, not a state of the world, so it
/// does not rot the way a "not yet done" note does.
///
/// **`Main.cpp` builds one of these with a POSITIONAL aggregate initialiser.**
/// In its `saveEverything` lambda it writes `SavedPlayer saved{...}` with seven
/// values and no member names, then assigns the rest by name. The seven, in
/// order, are `position`, `yaw`, `pitch`, `health`, `food`, `saturation`,
/// `exhaustion` - which is exactly the order they are declared in below, and it
/// is the declaration order that binds them, not the names.
///
/// **The constraint: new members go at the TAIL. Never inserted, never
/// reordered.** C++20 lets a designated initialiser be skipped but not
/// reordered, and that brace list uses no designators at all, so an insertion
/// shifts every value after it by one slot.
///
/// **Some of those shifts are a hard error and some are silent, which is the
/// dangerous part.** A shift that lands a `float` in an `int32` slot is a
/// narrowing conversion and fails the build, so inserting anything before
/// `health` is caught. A shift within the float run is not: put a new `float`
/// between `saturation` and `exhaustion` and the initialiser quietly hands
/// `exhaustion`'s value to the new member, leaves `exhaustion` at its default,
/// and writes both to disk with no diagnostic from any tool this project runs.
///
/// **If you add a member you must also**: update `static_assert(sizeof(
/// SavedPlayer) == 1268)` below to the new size, add the next `kPlayerVersion`
/// rung in `WorldStore.cpp`, and give the previous layout a `LegacyPlayer`
/// struct - the rung asserts are written so that bumping the version fails to
/// build until you do. That chain is deliberate and is the only part of this
/// warning a compiler can enforce for you.
///
/// **Falsified by**: `Main.cpp` no longer containing a braced `SavedPlayer`
/// initialiser without designators. If someone converts it to designated
/// initialisers - `saved{.position = ..., .yaw = ...}` - insertion becomes safe
/// and this whole paragraph can go. That is the fix worth making; it just is
/// not mine to make, because that file has another owner.
struct SavedPlayer {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    /// Everything being carried, hotbar first - the same order `Inventory`
    /// keeps, so this is a copy rather than a translation.
    ///
    /// **Widened again at M29m, and the version bumped with it**, because until
    /// then nothing the player was carrying was written down anywhere: every
    /// launch handed back an empty inventory, which in survival is the whole
    /// game. A version 2 file is still read - it simply has no inventory in it -
    /// so a world saved before this keeps where it was standing.
    std::array<ItemStack, kInventorySlots> inventory{};
    /// Which hotbar slot is in hand.
    std::int32_t selectedSlot = 0;
    /// **The ender chest, which belongs to the player and not to any block** -
    /// which is exactly why it was missed. Every other container is keyed by a
    /// block position and rides in `chests.dat`; this one is a single inventory
    /// shared by every ender chest in the world, so it has no position to be
    /// keyed by and no entry in that map. It was therefore a plain local that
    /// no save path ever saw, and a player who filled the one block the
    /// reference *promises* is safe came back to it empty, silently.
    ///
    /// Appended at the tail rather than beside `inventory`, so the v3 record is
    /// still a stated prefix-free struct of its own (`LegacyPlayerV3`) and the
    /// migration is a field-by-field copy - `damage` included, because a
    /// stowbox's handle rides in it and an upgrade that reset it would empty
    /// every stowbox in the world.
    Chest enderChest{};
    /// **Where the bed they last slept in stands**, `y` below zero for "no bed"
    /// - the same encoding `SavedCreature::bedCell` already uses for a
    /// villager's, and the same one `plausibleBlockPosition` already rejects,
    /// so "unset" and "not a place in this world" are one test rather than two.
    ///
    /// A separate `bool` was the obvious shape and is the wrong one twice over:
    /// it opens three bytes of padding at the tail of a record whose whole
    /// discipline is having none, and it makes "no respawn point" and "a
    /// respawn point at a nonsense coordinate" two states that the reader has
    /// to keep in step. One sentinel cannot drift from itself.
    ///
    /// **Added at M29p with a version bump, because until then it was a plain
    /// local in `main()` that no save path could see.** Sleep in a bed, quit,
    /// come back, die - and you woke at world spawn, potentially thousands of
    /// blocks from everything you own, with nothing on screen to say why. It is
    /// the worst-feeling class of bug a survival game has: invisible until it
    /// has already cost the player the session, and indistinguishable from
    /// "beds do not work" from the outside.
    ///
    /// **Only where the bed *was*.** Whether it is still there, and whether
    /// anything has been built on top of it, is asked at respawn against the
    /// live world and never cached here - the reference clears the point when
    /// the bed is missing or blocked, and a `hasRespawnPoint` written down at
    /// save time would be a second, staler answer to a question the world can
    /// always answer for itself.
    glm::ivec3 respawnBed{0, -1, 0};
    /// **Status effects, and the absorption hearts that are not one.**
    ///
    /// Until this landed, every running effect was thrown away on quit: drink a
    /// potion of Fire Resistance, save in a place only that potion makes
    /// survivable, and come back standing in lava with nothing on screen to say
    /// what changed. A beacon's Haste, a golden apple's Regeneration, a
    /// Poison that the player was waiting out - all gone, and the last one in
    /// the player's favour, which is why this reads as a bug in both directions.
    ///
    /// **Absorption rides here as a plain number and not as effect 22.** The
    /// two are genuinely different things: `Effect::Absorption` is the *grant*,
    /// and `Player::absorption` is the pool of extra hearts it filled, which
    /// the player then spends. Writing only the effect down would hand back a
    /// full pool to someone who had already taken the hits; writing only the
    /// pool would drop the grant that keeps topping it up. `Player.cpp` already
    /// keeps them as two values for exactly that reason, and a save that
    /// collapsed them would be re-deriving one from the other in the one place
    /// with no live data to derive it from.
    ///
    /// Appended at the tail, so version 5 stays a stated prefix-free record of
    /// its own (`LegacyPlayerV5`) and the rung-difference asserts below measure
    /// this addition rather than describe it.
    ///
    /// > **Landed 2026-08-19, both halves. This note previously said the
    /// > opposite and was believed.** `Main.cpp` fills the array in its
    /// > `saveEverything` lambda, immediately before it writes `absorption`,
    /// > and reads it back in the `savedPlayer.has_value()` block that restores
    /// > the player, looping each record into `player.effects`.
    /// >
    /// > For hours after the wiring landed, this note still asserted the
    /// > opposite - that nobody had written these call sites yet. It was read,
    /// > believed, and repeated into a briefing, and two
    /// > finished features were within one step of being re-queued as gaps in
    /// > `GAPS.md`. **The danger is not the wasted reading** - a note saying a
    /// > feature is one call site short invites the reader to go and write that
    /// > call site, which here would have produced two writers for one field.
    /// >
    /// > **The retracted wording is deliberately not quoted here.** Reproducing
    /// > it put the alarm phrase back into the file, where a search for stale
    /// > notes hits the retraction and reports it as the claim - which happened
    /// > within minutes of this paragraph being written, to a reader who was
    /// > right to look. A correction that restates what it corrects is not a
    /// > correction; it is the same string with a preface.
    /// >
    /// > **Falsified by**: `saved.effects` or `savedPlayer->effects` matching
    /// > nothing in `Main.cpp`. That is the entire test, it takes one search,
    /// > and it is the test that was never run on the sentence this replaced.
    std::array<SavedEffect, kSavedEffectSlots> effects{};
    float absorption = 0.0f;
    /// **The grant's clock - and it is deliberately NOT restored from this
    /// field. Do not wire it up.**
    ///
    /// The hazard the field was added for is real. `Player.cpp` recognises a
    /// *fresh* grant by watching the Absorption effect's remaining seconds rise
    /// above the value it last saw, so a pool restored beside a zeroed clock
    /// reads as a rise on the very first tick after loading and refills to full
    /// - spend your absorption hearts, quit, come back, and they are all there
    /// again.
    ///
    /// **It is closed by derivation rather than by storage.** `Main.cpp` sets
    /// the live value from `player.effects.secondsLeft(Effect::Absorption)`
    /// right after restoring the pool, because what the tick would have left
    /// there is exactly the restored effect's own remaining seconds. That comes
    /// off the one table that owns it, so there is nothing to keep in step.
    ///
    /// Which leaves this field **never populated and never applied**: it goes to
    /// disk as the 0.0f below, every time. **That is not an oversight to fix.**
    /// Populating it would put a second answer next to a settled one, and the
    /// two would disagree the moment the effect list is sanitised on load and
    /// this number is not.
    ///
    /// **Say "never populated", never "written by nobody".** That wording was
    /// wrong here until 2026-08-19 11:56 and the imprecision is load-bearing,
    /// because the field *is* moved across the disk boundary twice over:
    /// `loadPlayer` bulk-reads the whole record with one
    /// `file.read(reinterpret_cast<char*>(&player), sizeof(player))`,
    /// `savePlayer` bulk-writes its `clean` copy the same way, and the version 7
    /// rung assigns it **by name** alongside every other field. A reader who
    /// checks "is it written?" finds three writers and concludes the note is
    /// stale; a reader who greps the member name finds none of them, because a
    /// bulk byte move names no field at all. Both readers are then wrong about
    /// different things. What is absent is narrower and is the only part that
    /// matters: **nothing fills it from the live player, and nothing hands it
    /// back to one.**
    ///
    /// **This has been filed as a bug three times, so here is the evidence and
    /// not just the assertion.** It is an absence claim, so no same-symbol
    /// control exists - there is nothing there to vary. What can be shown is
    /// that the search would have found the wiring had it existed. Measured in
    /// `Main.cpp`, 2026-08-19 11:58, code lines counted apart from comment
    /// lines, and **both accessor spellings searched** - the record is a plain
    /// `saved` where it is filled and an `optional`, so `savedPlayer->`, where
    /// it is applied:
    ///
    ///                            filled   applied
    ///     absorptionSeconds        0         0     <- the claim
    ///     absorption               1         1     <- sibling, both ways
    ///     armour                   1         1     <- sibling, both ways
    ///     absorptionMilliseconds   0         0     <- invented, cannot exist
    ///
    /// Non-uniform, both directions represented, and the zero side contains a
    /// name that *cannot* be found, so a zero here means absent rather than
    /// blind. The one place `absorptionSeconds` appears in code there is
    /// `player.` and not the record - that is the live field taking the
    /// derivation described above, and mistaking it for disk wiring is
    /// precisely how this became a bug report three times.
    ///
    /// **Search both spellings or the measurement is worthless.** An earlier
    /// version of this table probed `saved.` alone. It reported the two
    /// siblings as filled-but-never-applied, because every apply site is
    /// spelled `savedPlayer->` - a false reading of *working* code, produced by
    /// a probe that was right about this field for the wrong reason. The
    /// accessor is part of the spelling, and one dot cost the whole load side.
    ///
    /// That control is sound but it is a token sweep, and a token sweep cannot
    /// settle a question about *behaviour* - which is exactly what the bulk read
    /// above demonstrates. The claim rests on the population sites, not on the
    /// count: `SavedPlayer` is built in one place, positionally, from seven
    /// initialisers that stop nine members short of this one, so this field
    /// takes the 0.0f below and reaches disk as a deterministic zero rather than
    /// as anything the player did.
    ///
    /// **Falsified by**: any `saved.absorptionSeconds` **or**
    /// `savedPlayer->absorptionSeconds` appearing in `Main.cpp`, or an eighth
    /// initialiser joining that braced construction. If either happens it is a
    /// second writer for a derived value, and the bug is the new line rather
    /// than this comment.
    ///
    /// **Proved, and it is the clause every earlier version of this note was
    /// missing: this field has an owner, and the owner is not the disk.**
    /// `Main.cpp:2670-2672`, inside the restore branch, does
    /// `player.absorptionSeconds = player.effects.secondsLeft(Absorption)` -
    /// it is **derived on every load** from `effects` at :209, which does
    /// persist, each `SavedEffect` carrying its own `secondsLeft` at :64. The
    /// comment there states the rule outright: *"It needs no field on disk:
    /// what the tick would have left here is exactly the restored effect's own
    /// remaining seconds, so it comes off the one table that owns it."*
    ///
    /// **It is NOT inert, and do not read the paragraph above as saying so.**
    /// `Player::update` reconciles the pool by comparing the effect's remaining
    /// seconds against this field and **treats a rise as a fresh grant**. Leave
    /// it at zero across a load and the first tick reads as a new golden apple
    /// and refills the pool: spend your absorption hearts, quit, return, and
    /// they are all back. `WorldStore.cpp:1294` documents the same mechanism
    /// from the version 6 rung's side. **The derivation is what prevents that,
    /// so it is load-bearing - deleting it as redundant installs the exploit.**
    ///
    /// So there are two opposite wrong edits and this note exists to block
    /// both: **populating this field from the live player and applying it back**
    /// (a second writer for a value that already has an owner - see 9770), and
    /// **deleting the derivation** because a disk field of the same name and
    /// unit sits here looking authoritative. The field is the *previous tick's*
    /// value held for edge detection, not a duration store; same unit, same
    /// name, different meaning.
    ///
    /// I got that wrong myself at 12:24 and it is worth recording why: I
    /// matched the field's **name and unit** against `SavedEffect::secondsLeft`
    /// and concluded "duplicate", without reading the consumer. The standing
    /// rule is *decode what the consumer receives, do not trace the accessor* -
    /// and the consumer here uses it as a rise detector, which no amount of
    /// staring at the declaration would reveal.
    ///
    /// It stays because removing it is not free: it is a *version 7* field, so
    /// deleting it changes `LegacyPlayerV7`'s stated size and every rung assert
    /// measured against it. Filed to be dropped at the next bump that has to
    /// rebase them anyway.
    float absorptionSeconds = 0.0f;
    /// **What is being worn**, in `ArmourSlot`'s own order - head, chest, legs,
    /// feet - so this is a copy of `Inventory::m_armour` rather than a
    /// translation of it.
    ///
    /// `Inventory` grew a second array beside `m_slots` and nothing wrote it
    /// down, so a full set of diamond survived until the next launch and was
    /// then silently gone: it is not in `inventory`, so no amount of care over
    /// *that* array could have saved it. The pieces go to disk as whole stacks
    /// for the same reason every other rung copies them wholesale - `damage`
    /// carries the wear, and a migration that rebuilt the stacks would hand
    /// back a repaired set, which is the "old damage stays" half of a format
    /// bump that is easy to lose.
    std::array<ItemStack, kArmourSlots> armour{};
    /// **What time it is, and what the sky is doing - world state, knowingly
    /// filed under the player.**
    ///
    /// Until this landed, every launch began at `0.18` - morning - however long
    /// the player had played and whenever they had quit. Stop at dusk, come
    /// back, and it is dawn again, so a night never survives a session and the
    /// day counter that drives the moon phase always reads the same.
    ///
    /// **A `level.dat` would be the pure answer and it is the wrong one here.**
    /// The category was already crossed deliberately, one field up: `respawnBed`
    /// is world state by exactly the same argument and rides here because it
    /// belongs to *this* player. The reason other games must split player state
    /// from world state is that N players share one world - and multiplayer is
    /// permanently cut (`CLAUDE.md`), so N is 1 forever, `player.dat` is already
    /// per-world, and the distinction has no consequence it can ever cash out
    /// into. A second file costs a new magic, a new version, a new load path,
    /// new corruption handling and a new `wrote(...)` line to hold five numbers.
    ///
    /// > Revisit when the *second* piece of true world state arrives - a game
    /// > rule table, a difficulty, a world border. At that point `level.dat`
    /// > earns its keep and these five fields move, which is a migration this
    /// > file's rung machinery already knows how to do.
    ///
    /// **The two flags are `std::int32_t` and not `bool` on purpose.** A `bool`
    /// here is one byte followed by three the compiler owns, which is the exact
    /// hole `SavedEffect::id` and `respawnBed` are each documented for avoiding -
    /// and uninitialised padding is what stops two identical saves being
    /// byte-identical.
    ///
    /// **These are the four values `Weather` cannot recompute, and no more.**
    /// It keeps eleven members. Two are not weather at all - `m_forced` is the
    /// `settings.startWeather` override, re-read from settings on every launch,
    /// and `m_strikes` is the list of lightning bolts currently in the air,
    /// which last about a second. Five more - `m_rainLevel`, `m_thunderLevel`,
    /// `m_cloudLevel`, `m_windSpeed` and `m_flash` - are *ramps* that chase the
    /// two flags at `kLevelRampPerSecond` (0.2/s, so five seconds end to end).
    /// Saving a ramp would freeze a presentation detail into the format and
    /// give the next reader two sources for one fact; not saving it costs a
    /// five-second fade-in on load, once. That leaves `m_rainOn`, `m_thunderOn`,
    /// `m_rainSeconds` and `m_thunderSeconds` - the state that genuinely cannot
    /// be derived, and what the reference itself stores.
    ///
    /// > Named rather than cited by line, deliberately. An earlier draft of
    /// > this paragraph pointed at `Weather.hpp:329-337`; that file was edited
    /// > by somebody else the same hour and the members moved to 349-357, so
    /// > the citation was stale before it was ever read. A member name survives
    /// > an edit that a line number does not.
    ///
    /// > **Landed 2026-08-19, all five, both halves.** `Main.cpp` writes
    /// > `timeOfDay` from the live accumulator and the four weather fields from
    /// > `Weather::raining`, `thundering`, `rainSeconds` and `thunderSeconds`,
    /// > all in its `saveEverything` lambda; it reads them back by wrapping the
    /// > hour into the local clock and handing the four to `Weather::restore`.
    /// > The save side calls `thundering()` and never `storming()`, for the
    /// > reason given above.
    /// >
    /// > For hours after all five landed, this paragraph still asserted that
    /// > none of them had been written. **Negative claims rot fastest**:
    /// > they are true only until
    /// > somebody acts, and nothing tells the comment when they do. The one
    /// > above `effects` failed the same way in the same file on the same day,
    /// > which is why both now carry a date and a test rather than a state.
    /// > The retracted wording is not quoted here for the reason given there -
    /// > a retraction that repeats the phrase keeps answering to searches for it.
    /// >
    /// > **Falsified by**: `saved.timeOfDay` or `weather.restore` matching
    /// > nothing in `Main.cpp`.
    float timeOfDay = 0.18f;
    std::int32_t weatherRaining = 0;
    std::int32_t weatherThundering = 0;
    float weatherRainSeconds = 0.0f;
    float weatherThunderSeconds = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SavedPlayer>,
              "the player record is written as bytes and must stay plain data");

/// A furnace and the block it belongs to.
struct PlacedFurnace {
    glm::ivec3 position{0};
    Furnace furnace;
};

/// A chest and the block it belongs to.
struct PlacedChest {
    glm::ivec3 position{0};
    Chest chest;
};

/// A campfire and the block it belongs to.
///
/// **`campfires.dat` is a new table, so it has no legacy record beside it and
/// wants none.** A world saved before this existed simply has no file, which
/// reads as "no campfire anywhere has anything on it" - which is exactly what
/// was true, because nothing could be put on one. That is why this costs no
/// `kFormatVersion` bump: the player record did not change shape.
struct PlacedCampfire {
    glm::ivec3 position{0};
    Campfire campfire;
};

/// What is inside a stowbox that is currently **an item** rather than a block.
///
/// Keyed by a handle that rides in the item stack's `damage`, which is the one
/// per-stack number every save format and every drop already carries. That is
/// the whole trick: contents that travel with the item need no new field, no
/// format bump and no migration.
struct StowedBox {
    std::int32_t handle = 0;
    Chest contents;
};

/// One item lying on the ground as it goes to disk.
///
/// **The gap this closes is the worst kind of data loss in the tree.**
/// Everything on the floor at quit was simply destroyed: die in a cave, quit
/// before the five-minute despawn rather than sprinting back, and the entire
/// inventory is gone from where it fell - with nothing on screen to say so, and
/// no way to tell it from "a mob picked it up". `settleFallersBeforeFinalSave`
/// makes it certain rather than likely, because a falling cube that lands on a
/// torch or a slab *pays an item* at the terminal save, and that item was
/// created and destroyed in the same second.
///
/// **Its own record rather than `ItemEntity`'s private `Drop` written raw**,
/// for the reason every record here is: `Drop` belongs to the simulation and
/// may gain a field - a bob phase, a merge cooldown - for reasons that have
/// nothing to do with saving, and if this were a raw copy every drops file on
/// disk would change shape without anything here changing at all. The stack is
/// spelled as one `ItemStack` where `Drop` keeps `item`, `count` and `damage`
/// loose, because that is the shape every other record in this file stores and
/// the shape `sanitiseStack` and `blankStackPadding` already know how to clean.
///
/// **`age` is saved, and that is the correct answer rather than the comfortable
/// one.** The five-minute despawn is a real mechanic; an unsaved age makes the
/// floor immortal, so quitting and reloading becomes a way to keep a dropped
/// stack forever and the reference's own rule stops applying. The cost is real
/// and worth stating so nobody "fixes" it: a world left overnight loses its
/// drops on the first update after loading, because they were already four
/// minutes old when it was closed. That is the reference's behaviour and it is
/// intended here.
///
/// **`pickupDelay` is saved for a sharper reason.** It is what stops a death
/// drop being sucked straight back up by the player standing in it. Drop it and
/// every reload hands back the items that were meant to be walked to.
///
/// `onGround` is an `std::int32_t` rather than a `bool` for the padding reason
/// documented on `SavedCreature` - one byte followed by three the compiler
/// owns, going to disk uninitialised.
struct SavedItem {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    ItemStack stack{};
    float age = 0.0f;
    float pickupDelay = 0.0f;
    std::int32_t onGround = 0;
};

/// One creature as it goes to disk.
///
/// Deliberately its own record rather than the live `Creature`. A save format
/// has to be an explicit list of fields, or it changes silently every time the
/// struct gains one - and everything transient (gait, timers, what it was
/// thinking) is exactly what a reload should throw away.
///
/// **Every byte of this is written, padding included**, so the layout is chosen
/// to have none: an `std::uint8_t kind` left a three-byte hole behind it and a
/// fourth at the end, and four bytes of whatever the stack happened to contain
/// went into every save file. Widening `kind` and naming the tail byte costs
/// three bytes a creature and makes the record say exactly what it means.
struct SavedCreature {
    /// Wider than the `CreatureKind` it holds, to close the hole that followed
    /// it. The reader still has to bounds-check it - a number off a disk is not
    /// an enumerator.
    std::int32_t kind = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    std::int32_t health = 0;
    float scale = 1.0f;
    /// Whether a Bramble is carrying a charge. Kept because it is the one
    /// per-individual property that is not transient and not derivable - a
    /// charged one that reloaded as ordinary would look like the charge simply
    /// wearing off.
    std::uint8_t charged = 0;
    /// Whether an iron golem was built by the player rather than found in a
    /// village. **Not cosmetic**: it is the whole of the golem's temperament,
    /// so one that forgot and started hitting you after a reload would be a
    /// nasty bug rather than a blemish.
    std::uint8_t playerBuilt = 0;
    /// A villager's trade. Derivable in principle from the block it claimed,
    /// but only while that block is still there and still unclaimed - so it is
    /// stored, exactly as the reference stores it.
    std::uint8_t profession = 0;
    /// Named padding, not a spare field. The three bytes above leave one, and a
    /// hole the compiler owns is a byte of stack going to disk; a member with an
    /// initialiser is a byte of zero. Anything that needs a `std::uint8_t` next
    /// may take it - and must bump the version, because an old file's value here
    /// is whatever was in memory.
    std::uint8_t reserved = 0;
    /// A villager's claimed bed, workstation and meeting point, `y` below zero
    /// for unclaimed - the same encoding the live creature uses.
    ///
    /// **A villager without these is not a villager**: `workStart` needs
    /// `jobCell.y >= 0` and re-claiming is refused to anyone who already has a
    /// profession, so an armourer that reloaded without its anvil could neither
    /// work nor claim again for the rest of that world's life. Worse, the scan
    /// that decides whether a workstation is taken reads every restored villager
    /// as claiming nothing, so the next one to wake takes the same anvil and the
    /// village ends up with two armourers.
    glm::ivec3 bedCell{0, -1, 0};
    glm::ivec3 jobCell{0, -1, 0};
    glm::ivec3 meetCell{0, -1, 0};
};

/// Proof there is nowhere for the compiler to hide a byte in the records above:
/// each one's size is checked against **the sum of its own members' sizes**, so
/// a hole opened by narrowing or reordering a field shows up as a mismatch.
/// **Costs nothing and cannot rot** - both sides are `constexpr`.
///
/// **The sizes are read off the members rather than restated**, which is the
/// whole point and was the defect in the version this replaces. That one spelled
/// the right-hand side as `sizeof(std::int32_t) * 2 + ...`, so narrowing
/// `SavedCreature::kind` back to `std::uint8_t` - the exact regression it was
/// written to catch - left both sides at 68 and passed while three bytes of
/// stack went to disk. A `static_assert` comparing one side of a derivation
/// against itself proves nothing.
///
/// The literal totals beside them pin the on-disk record size, so growing a
/// record cannot happen without touching a number here and thinking about the
/// version it needs.
///
/// **A sum only proves the outer record is hole-free**, so the nested types get
/// the same treatment - a hole inside `Furnace` is invisible from
/// `PlacedFurnace`, whose sum would simply grow with it.
static_assert(sizeof(ItemStack) == 12 && sizeof(ItemId) == 2,
              "ItemStack changed shape; every record below is sized in terms of it");
/// **The one member that is knowingly not hole-free.** `ItemId` is sixteen bits
/// and `count` is thirty-two, so bytes 2 and 3 of every stack belong to the
/// compiler - 54 per chest, 108 per player record. They are blanked on the way
/// out by `blankStackPadding` in the writer rather than left to whatever the
/// stack happened to contain.
///
/// The real cure is one edit in `Item.hpp`: give `ItemId` a 32-bit underlying
/// type, or move `item` below `count` and `damage`. Either closes the hole,
/// makes the blanking a no-op, and changes the size of every record here - so
/// it costs a bump of `kFormatVersion`, `kChestVersion`, `kFurnaceVersion` and
/// `kStowboxVersion` together, which is why it has not been done in passing.
static_assert(offsetof(ItemStack, item) == 0 && offsetof(ItemStack, count) == 4 &&
                  offsetof(ItemStack, damage) == 8,
              "ItemStack's hole moved; blankStackPadding in WorldStore.cpp assumes it is "
              "the gap between `item` and `count`");

/// Fails on: adding a `std::uint8_t rows` to `Chest`.
static_assert(sizeof(Chest) == 324 && sizeof(Chest) == sizeof(Chest::slots),
              "Chest must have no padding, or uninitialised bytes go to disk");
/// Fails on: adding a `bool lit` to `Furnace`.
static_assert(sizeof(Furnace) == 48 &&
                  sizeof(Furnace) == sizeof(Furnace::input) + sizeof(Furnace::fuel) +
                                         sizeof(Furnace::output) + sizeof(Furnace::burnRemaining) +
                                         sizeof(Furnace::burnTotal) + sizeof(Furnace::cookElapsed),
              "Furnace must have no padding, or uninitialised bytes go to disk");
/// Fails on: adding a `bool lit` to `Campfire`, or spelling its four timers as
/// a `double`.
static_assert(sizeof(Campfire) == 64 &&
                  sizeof(Campfire) == sizeof(Campfire::items) + sizeof(Campfire::elapsed),
              "Campfire must have no padding, or uninitialised bytes go to disk");

/// **Every member of `SavedPlayer` must carry a default member initialiser, and
/// the assert below is the tripwire for it.** Written 2026-08-19 11:57.
///
/// `loadPlayer` declares its record as a bare `SavedPlayer player;` and only
/// the current-version rung fills it wholesale. Every legacy rung assigns a
/// subset by name and leaves the rest to their declared defaults - which is
/// what each of those rungs already says in its own words, that `timeOfDay`
/// "stays at 0.18" and that the worn set "stays empty". Those sentences are
/// true only because all twenty members are declared with an initialiser.
///
/// So a member added *without* one is not merely untidy: it is indeterminate
/// stack memory handed back as player state on every legacy load, and then
/// bulk-written to disk by the next save. No warning fires, because the member
/// is unambiguously "assigned" from the compiler's point of view - by the bulk
/// read, on the one path that does not need it.
///
/// This comment sits here rather than beside the struct because **this is where
/// the breaking edit is already guaranteed to stop.** Adding a member changes
/// `sizeof(SavedPlayer)`, the assert below fails, and whoever is updating 1268
/// has to read this. There is no trait that can test the invariant directly -
/// a struct whose other members have initialisers is not trivially default
/// constructible either way - so the existing tripwire is the whole mechanism.
///
/// **Falsified by** any member declared in `SavedPlayer` as a bare
/// `type name;`. Re-run that search rather than trusting this paragraph; it
/// read 20 with an initialiser and 0 without on 2026-08-19 11:57.
///
/// Fails on: narrowing `selectedSlot` to `std::int16_t`, and on spelling
/// `respawnBed` as a `glm::ivec3` plus a `bool`, which opens three bytes at the
/// tail. Fails too on spelling either weather flag as a `bool`, which opens
/// three bytes in the middle of the new tail.
static_assert(sizeof(SavedPlayer) == 1268 &&
                  sizeof(SavedPlayer) ==
                      sizeof(SavedPlayer::position) + sizeof(SavedPlayer::yaw) +
                          sizeof(SavedPlayer::pitch) + sizeof(SavedPlayer::health) +
                          sizeof(SavedPlayer::food) + sizeof(SavedPlayer::saturation) +
                          sizeof(SavedPlayer::exhaustion) + sizeof(SavedPlayer::inventory) +
                          sizeof(SavedPlayer::selectedSlot) + sizeof(SavedPlayer::enderChest) +
                          sizeof(SavedPlayer::respawnBed) + sizeof(SavedPlayer::effects) +
                          sizeof(SavedPlayer::absorption) + sizeof(SavedPlayer::absorptionSeconds) +
                          sizeof(SavedPlayer::armour) + sizeof(SavedPlayer::timeOfDay) +
                          sizeof(SavedPlayer::weatherRaining) +
                          sizeof(SavedPlayer::weatherThundering) +
                          sizeof(SavedPlayer::weatherRainSeconds) +
                          sizeof(SavedPlayer::weatherThunderSeconds),
              "SavedPlayer must have no padding, or uninitialised bytes go to disk");

/// **The order-sensitive half of the same guard, and the assert above cannot do
/// this job.** Written 2026-08-19.
///
/// That one is a SUM, so it is blind to order: insert a member in the middle,
/// add its name to the sum, update 1268, and it passes - while `Main.cpp`'s
/// positional brace-init (see the warning above `SavedPlayer`) silently hands
/// every value after the insertion to the wrong member. Every one of them is a
/// `float`, so nothing else in the toolchain says a word.
///
/// This one pins the offset of the LAST positionally-initialised member, which
/// is exactly the trigger condition: any insertion or reorder before
/// `exhaustion` moves it and fails the build, while appending after it - the
/// one edit that is actually safe - leaves it alone. It is the same `offsetof`
/// idiom the `ItemStack` assert below uses, so it is known to compile here.
///
/// **The right-hand side is derived rather than written**, so it does not
/// encode a guess about `glm::vec3` being 12 bytes; if glm ever aligns to 16
/// both sides move together and the assert keeps testing order alone.
static_assert(offsetof(SavedPlayer, exhaustion) ==
                  sizeof(SavedPlayer::position) + sizeof(SavedPlayer::yaw) +
                      sizeof(SavedPlayer::pitch) + sizeof(SavedPlayer::health) +
                      sizeof(SavedPlayer::food) + sizeof(SavedPlayer::saturation),
              "The first seven members of SavedPlayer moved. Main.cpp brace-initialises them "
              "positionally, so appending is safe but inserting or reordering silently "
              "mis-assigns - read the warning above the struct before changing this number");

/// Fails on: spelling `SavedEffect::id` as the `effects::Effect` enum itself,
/// which is a `std::uint8_t` and opens three bytes of padding before
/// `amplifier` - 96 uninitialised bytes per player file, different on every
/// launch, in a record this file goes to some trouble to keep deterministic.
static_assert(sizeof(SavedEffect) == 12 &&
                  sizeof(SavedEffect) == sizeof(SavedEffect::id) +
                                             sizeof(SavedEffect::amplifier) +
                                             sizeof(SavedEffect::secondsLeft),
              "SavedEffect must have no padding, or uninitialised bytes go to disk");

/// Fails on: adding a `std::uint8_t facing` to `PlacedFurnace`.
static_assert(sizeof(PlacedFurnace) == 60 &&
                  sizeof(PlacedFurnace) ==
                      sizeof(PlacedFurnace::position) + sizeof(PlacedFurnace::furnace),
              "PlacedFurnace must have no padding, or uninitialised bytes go to disk");

/// Fails on: adding a `bool doubled` to `PlacedChest`.
static_assert(sizeof(PlacedChest) == 336 &&
                  sizeof(PlacedChest) ==
                      sizeof(PlacedChest::position) + sizeof(PlacedChest::chest),
              "PlacedChest must have no padding, or uninitialised bytes go to disk");

/// Fails on: adding a `std::uint8_t lit` to `PlacedCampfire`.
static_assert(sizeof(PlacedCampfire) == 76 &&
                  sizeof(PlacedCampfire) ==
                      sizeof(PlacedCampfire::position) + sizeof(PlacedCampfire::campfire),
              "PlacedCampfire must have no padding, or uninitialised bytes go to disk");

/// Fails on: narrowing `handle` to `std::int16_t`.
static_assert(sizeof(StowedBox) == 328 &&
                  sizeof(StowedBox) == sizeof(StowedBox::handle) + sizeof(StowedBox::contents),
              "StowedBox must have no padding, or uninitialised bytes go to disk");

/// Fails on: spelling `onGround` as a `bool`, and on adding a bob phase or a
/// merge cooldown to the record without a `kDropVersion` bump beside it.
static_assert(sizeof(SavedItem) == 48 &&
                  sizeof(SavedItem) == sizeof(SavedItem::position) + sizeof(SavedItem::velocity) +
                                           sizeof(SavedItem::stack) + sizeof(SavedItem::age) +
                                           sizeof(SavedItem::pickupDelay) +
                                           sizeof(SavedItem::onGround),
              "SavedItem must have no padding, or uninitialised bytes go to disk");

/// Fails on: narrowing `kind` back to `std::uint8_t`, which is what the record
/// this replaces let through.
static_assert(sizeof(SavedCreature) == 68 &&
                  sizeof(SavedCreature) ==
                      sizeof(SavedCreature::kind) + sizeof(SavedCreature::x) +
                          sizeof(SavedCreature::y) + sizeof(SavedCreature::z) +
                          sizeof(SavedCreature::yaw) + sizeof(SavedCreature::health) +
                          sizeof(SavedCreature::scale) + sizeof(SavedCreature::charged) +
                          sizeof(SavedCreature::playerBuilt) + sizeof(SavedCreature::profession) +
                          sizeof(SavedCreature::reserved) + sizeof(SavedCreature::bedCell) +
                          sizeof(SavedCreature::jobCell) + sizeof(SavedCreature::meetCell),
              "SavedCreature must have no padding, or uninitialised bytes go to disk");

/// A chunk column whose one-off creature group has already been placed.
///
/// **Its own record, and deliberately not a field on `SavedCreature`.** The
/// marker `Creatures::m_populated` holds is keyed by chunk *column* - a packed
/// `(chunkX, chunkZ)` - and describes ground, not an animal. Bolting it onto
/// the creature record would make it per-creature, which is a different thing
/// with a different lifetime: a column stays populated after everything in it
/// has been eaten by a wolf, and that is the whole point of the marker. A
/// village is a region too, spanning many columns and owning none of them, so
/// there is no per-village record to hang it on either.
///
/// **Stored unpacked, though `Creatures` keys it packed.** `populatedKey` is
/// private to `Creature.cpp` and its comment already says it must have one
/// owner "or the producer and the consumer are free to pack it differently and
/// the set silently never matches". Writing the packed `std::uint64_t` here
/// would make this file that second owner: change the packing on their side
/// and every marker on disk quietly names a different column, with no error and
/// no warning - the world simply repopulates forever. Two `std::int32_t` cost
/// the same eight bytes and know nothing about the packing.
struct PopulatedColumn {
    std::int32_t x = 0;
    std::int32_t z = 0;
};

/// Fails on: widening either field to `std::int64_t` to match the packed key,
/// which would put an eight-byte member after a four-byte one and open a hole.
static_assert(sizeof(PopulatedColumn) == 8 &&
                  sizeof(PopulatedColumn) ==
                      sizeof(PopulatedColumn::x) + sizeof(PopulatedColumn::z),
              "PopulatedColumn must have no padding, or uninitialised bytes go to disk");
static_assert(std::is_trivially_copyable_v<PopulatedColumn>,
              "PopulatedColumn goes to disk as raw bytes");

/// The one rule for a stack that came off a disk, in one place.
///
/// **Every reader below already calls this**, so what `WorldStore` hands back is
/// clean whether or not its caller checks. It is exported because the caller has
/// containers of its own - the ender chest, a screen's carried stack - and the
/// rule being correctly restated at one of four call sites is exactly how a
/// corrupt `count` gets laundered into the validated side after validation.
///
/// An out-of-range id or a non-positive count empties the slot; a count above
/// what that item may stack to is clamped. **`damage` is only floored at zero,
/// never capped**, because it is overloaded: it is a tool's wear *and* a
/// stowbox's handle into the stowed map, and capping it against a tool's
/// durability would strand a stowbox's contents forever.
void sanitiseStack(ItemStack& stack);

/// The same rule across a whole container.
void sanitiseChest(Chest& chest);

/// The largest handle a stowbox may carry. Bounded rather than merely positive
/// because the caller allocates the next one as `handle + 1`, which on a corrupt
/// `INT_MAX` overflows into undefined behaviour and then hands out a duplicate.
constexpr std::int32_t kMaxStowHandle = 1 << 24;

/// Zero means "this stack is not a stowbox", so a real handle starts at one.
bool plausibleStowHandle(std::int32_t handle);

/// Whether a block position off a disk names a cell the world could contain.
///
/// `y` is the hard one: the world is a fixed three chunks tall, so a furnace
/// restored above or below it is a block entity attached to nothing, invisible
/// and unbreakable, that is written back out on every save forever.
bool plausibleBlockPosition(const glm::ivec3& position);

/// Stores only the chunks the player actually changed.
///
/// Generation is a pure function of `(seed, chunkCoord)`, so an untouched chunk
/// is already perfectly reproducible and writing it down would be redundant. A
/// save is therefore the *difference* between the generated world and the played
/// one, which keeps it small no matter how far the player travels.
///
/// One file per chunk. Crude, but it makes a partial write damage exactly one
/// chunk instead of the whole world, and it needs no index to stay consistent.
/// A packed region format belongs with the rest of the M13 optimisation work, if
/// file count ever becomes the problem.
class WorldStore {
public:
    /// `root` is created if missing. Worlds with different seeds are kept apart,
    /// because a saved chunk is only meaningful against the terrain it was
    /// edited from.
    WorldStore(std::filesystem::path root, std::uint32_t seed);

    /// Returns the stored chunk, or nothing if this chunk was never modified.
    std::optional<Chunk> load(const ChunkCoord& coord) const;

    /// Overwrites any previous copy, returning whether the save actually landed.
    ///
    /// Writes to a temporary file, **flushes and closes it, checks that every
    /// byte reached the operating system**, and only then renames it over the
    /// real one - so a crash or a full disk mid-write leaves either the old
    /// complete file or the new complete file, never a hybrid.
    ///
    /// **`[[nodiscard]]` on purpose, and it is the only writer here that has
    /// it.** The caller clears the chunk's modified flag after this returns; a
    /// chunk that failed to write and is then marked clean is never retried, so
    /// the player's edits are gone at exit with no warning. Clearing the flag
    /// must be conditional on this being `true`. Silencing this with a cast to
    /// `void` reinstates that bug.
    [[nodiscard]] bool save(const ChunkCoord& coord, const Chunk& chunk) const;

    /// Nothing on a world that has never been played.
    ///
    /// **What comes back has already been through `sanitiseStack`**, and a
    /// record whose position, orientation or survival numbers are not finite is
    /// refused outright rather than repaired - `nullopt` puts the caller on its
    /// fresh-spawn path, which is the only correct answer when the one field
    /// that says where the player is cannot be trusted. A NaN position round
    /// trips: it is written back out on every save, so it bricks a world
    /// permanently rather than for one launch.
    std::optional<SavedPlayer> loadPlayer() const;
    bool savePlayer(const SavedPlayer& player) const;

    /// Block entities, in one file for the whole world rather than split across
    /// the chunk files.
    ///
    /// Deliberate: a chunk is loaded and saved by worker threads, and threading
    /// block-entity data through that path buys a whole class of ordering bugs
    /// for something a player only ever has a handful of. The cost is that these
    /// are written when the world is saved rather than when a chunk unloads.
    ///
    /// Every loader drops records whose position is outside the world and
    /// sanitises every stack, so nothing here needs the caller's trust.
    ///
    /// **A saver can now return false without any disk error, and a caller that
    /// treats that as "the disk is broken" will be wrong.** It also means *this
    /// build refused to read that file, so it will not delete it either* -
    /// which is a bug that was live until 2026-08-19 and cost, in the worst
    /// case, every container in a world.
    ///
    /// The shape: a loader that would not accept a file returned the same empty
    /// vector as a world that genuinely has none, the live table came up empty,
    /// and thirty seconds later - on `kAutosaveInterval`, not at quit -
    /// `saveX({})` deleted the file and the log line said the save was
    /// complete. **One launch of a binary whose version constants are older
    /// than the file on disk was enough**, which in a tree where several people
    /// bump those constants is an ordinary Tuesday rather than an exotic
    /// scenario. For `creatures.dat` it took the populated-column markers with
    /// it, so every explored column bred again.
    ///
    /// What happens now, in order: the loader records the refusal, keeps a copy
    /// of the file as `<name>.rejected` (created, never overwritten), and the
    /// saver refuses to delete and returns false. `saveEverything`'s existing
    /// `unwritten` list then names the table and the save honestly reports
    /// incomplete. A successful write of that table clears the refusal, because
    /// the file on disk is then one this build wrote.
    ///
    /// > **Falsified by** any `saveX` in `WorldStore.cpp` calling `removeTable`
    /// > directly instead of `removeTableUnlessRefused`, or by any loader
    /// > returning an empty result on a path that does not first call either
    /// > `refuseTable` or `acceptTable`.
    std::vector<PlacedFurnace> loadFurnaces() const;
    bool saveFurnaces(const std::vector<PlacedFurnace>& furnaces) const;

    std::vector<PlacedChest> loadChests() const;
    bool saveChests(const std::vector<PlacedChest>& chests) const;

    std::vector<PlacedCampfire> loadCampfires() const;
    bool saveCampfires(const std::vector<PlacedCampfire>& campfires) const;

    std::vector<StowedBox> loadStowboxes() const;
    bool saveStowboxes(const std::vector<StowedBox>& boxes) const;

    /// Everything lying on the ground, in one flat file for the whole world.
    ///
    /// **Flat rather than per-chunk, which is a divergence from the reference
    /// and the lower-risk answer here.** Bedrock keys item entities to the chunk
    /// they float in, which is right when chunks stream independently and wrong
    /// for this store: nothing else here is chunk-keyed, so a per-chunk table
    /// would be the only record type that has to survive `kChunkFormatVersion`
    /// moving underneath it - and that number is bumped whenever *terrain*
    /// changes, which has nothing to do with what is on the floor. `chests.dat`
    /// and `furnaces.dat` are position-keyed and flat for the same reason. The
    /// cost is that drops far from the player are written on every world save
    /// rather than when their chunk unloads, which is the same trade every
    /// other table here already makes.
    ///
    /// **`restore` must append rather than clear** on the entity side, or a
    /// load that happens after anything has already been dropped destroys it.
    ///
    /// **Both of these are LIVE, and this paragraph exists because a work queue
    /// still says they are not.** Measured here 2026-08-19 15:45, not taken on
    /// report: `Main.cpp` calls `world.store().loadDrops()` in the restore block
    /// and `world.store().saveDrops(savedDrops)` in `saveEverything`, two live
    /// calls among sixteen mentions of the drop machinery, against a control of
    /// three for `saveChests` in the same file and zero for an invented name.
    ///
    /// > **Why it is worth a paragraph rather than a ledger entry.** Anyone who
    /// > implements this a *second* time on the strength of a stale queue entry
    /// > gives the player a duplicate of every item on the ground at each load,
    /// > and this header is the file they must open to do it. A negative claim
    /// > outlives every ledger entry, because the next reader has the file.
    /// >
    /// > **Re-measure rather than believing this line** - a reachability result
    /// > is a measurement with a timestamp, not a property of the code, and this
    /// > one was true at the moment it was written and nothing more. The search
    /// > is the symbol, not a line number: `saveDrops` and `loadDrops` in
    /// > `game/src/Main.cpp`. **Falsified by** either returning no call site
    /// > outside a comment.
    std::vector<SavedItem> loadDrops() const;
    bool saveDrops(const std::vector<SavedItem>& drops) const;

    std::vector<SavedCreature> loadCreatures() const;

    /// Which chunk columns have already had their one-off creature group
    /// placed, so a reload does not place a second one.
    ///
    /// **Reads the same file `loadCreatures` does, on purpose.** Two tables that
    /// must agree do not go in two files: two files means two renames, and a
    /// crash between them leaves either creatures with no markers - which
    /// repopulates, the bug this exists to fix - or markers with no creatures,
    /// an empty world that never repopulates. One atomic rename cannot produce
    /// either. It re-opens rather than handing back both at once because most of
    /// what reads creatures does not want the columns, and a struct or an
    /// out-parameter would put them in the way of every caller that does not;
    /// the header parse has one owner in `openCreatureTable`, so the two readers
    /// cannot drift apart. (It also let the wiring land one call site at a time,
    /// which mattered while `Main.cpp` was catching up and does not now.)
    ///
    /// **Survives a torn file that `loadCreatures` does not, and that asymmetry
    /// is deliberate.** As of `kCreatureVersion` 6 the markers sit immediately
    /// after the header, so any tear that leaves the header intact still returns
    /// every marker written before the damage. They used to sit last, behind a
    /// blind seek across the creature array, so a tear anywhere in that array
    /// returned no markers at all and the next load re-populated every column
    /// the player had ever visited. Creatures are now the section at the tail,
    /// because losing them costs one herd the spawner replaces.
    std::vector<PopulatedColumn> loadPopulatedColumns() const;

    /// Writes both creature tables, always together.
    ///
    /// **`populated` is not optional and there is deliberately no overload that
    /// omits it.** A creature list saved without its columns is what made
    /// villages breed a second population on every load: the marker set was
    /// rebuilt from whichever creatures happened to be in memory, so quitting
    /// far from a village left it unmarked and it repopulated on the next load,
    /// every session, until it stood shoulder to shoulder with itself. A
    /// one-argument convenience form would compile at every existing call site
    /// and put that bug straight back, silently, which is why the caller has to
    /// say what it means. One such form existed briefly as a `[[deprecated]]`
    /// shim, so the then-unwired call site would raise C4996 naming the fix; the
    /// wiring landed, it went to zero callers, and it was deleted rather than
    /// left as an API whose only remaining use is the mistake.
    bool saveCreatures(const std::vector<SavedCreature>& creatures,
                       const std::vector<PopulatedColumn>& populated) const;

    const std::filesystem::path& directory() const { return m_directory; }
    std::uint32_t seed() const { return m_seed; }

private:
    std::filesystem::path pathFor(const ChunkCoord& coord) const;

    /// The side tables that live beside the chunks, and the one place their
    /// file names exist.
    ///
    /// **Both halves of a table used to spell its own file name**, so a loader
    /// and its saver held the same literal twice - and a rename that reached
    /// only one of them would have left the saver deleting a file nobody reads
    /// while the loader read a stale one, with a clean build and no warning.
    /// `tablePath` is now the only speaker, and every caller names a table
    /// rather than a string.
    ///
    /// `Player` is here for the refusal record alone: `player.dat` is never
    /// deleted, because `savePlayer` has no empty case to mistake for one.
    enum class Table : std::size_t {
        Furnaces,
        Chests,
        Campfires,
        Stowboxes,
        Drops,
        Creatures,
        Player,
        Count,
    };

    std::filesystem::path tablePath(Table table) const;

    /// Records that a table's file is **present but unusable by this build**,
    /// and keeps a copy of it before anything can overwrite it.
    ///
    /// The distinction this exists to draw is between *there is no file* and
    /// *there is a file and I will not read it*. Every loader used to answer
    /// both with an empty vector, and an empty vector is indistinguishable from
    /// a world whose last chest was just broken - so thirty seconds later the
    /// autosave handed that emptiness to `saveX({})`, which deleted the file
    /// and reported the save complete.
    void refuseTable(Table table, const std::filesystem::path& path) const;

    /// The other half of `refuseTable`: this build has just read that file and
    /// found it sound, so any refusal recorded for it is history.
    ///
    /// **Called where the header is accepted, not where the loader returns.** A
    /// loader that salvages a truncated tail still accepted the header, and the
    /// file it accepted is one this build can write over safely.
    void acceptTable(Table table) const;

    /// `removeTable` for an emptiness that might be our own ignorance.
    ///
    /// Deletes as before when the table was genuinely read and is genuinely
    /// empty; **refuses, loudly, when the last load of that file was a
    /// refusal.** Returns false in that case so the caller's existing
    /// `unwritten` list names the table and the save reports incomplete, which
    /// is the honest answer: this build did not write that table and must not
    /// pretend the absence of data is the absence of a world.
    bool removeTableUnlessRefused(Table table, const std::filesystem::path& path) const;

    /// Set by a loader that refused a file, cleared by one that read it and by
    /// any successful write of it.
    ///
    /// **`mutable` because every load and save on this class is `const`** - the
    /// object is handed out as `const WorldStore&` by `World::store()` - and
    /// this is bookkeeping about the files, not about the world. One object per
    /// world, held by `shared_ptr`, so the loader and the saver see the same
    /// flags.
    mutable std::array<bool, static_cast<std::size_t>(Table::Count)> m_refused{};

    std::filesystem::path m_directory;
    std::uint32_t m_seed;
};

} // namespace game
