#include "world/WorldStore.hpp"

// For `effects::kMaxActive` alone, to prove the saved effect list is still wide
// enough for the live one. Nothing else here reads it.
#include "world/Effects.hpp"

// For `kSmeltSeconds` alone, which bounds a furnace's cook timer coming off a
// disk. It already arrives transitively through `Campfire.hpp` - which is also
// where `kCampfireCookSeconds` comes from - and is named here anyway, because a
// clamp that silently depends on somebody else's include is one edit away from
// not compiling for a reason that has nothing to do with it.
#include "item/Smelting.hpp"

#include <engine/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>

namespace game {
namespace {

/// Identifies the file and pins the format. A chunk saved by an older build
/// would otherwise be read as garbage blocks rather than rejected.
///
/// **What these seven headers do not do, stated so it is a decision rather than
/// an oversight.** Each file carries its own magic, its own independent version
/// and the world seed - and nothing else. The seed is what refuses a file from a
/// *different* world, and it does that correctly. What no header can see is a
/// file from the *same seed at a different generation*: a `chests.dat` written
/// by another save of the same seed is accepted without a word, contents and
/// all, proven on bytes rather than argued.
///
/// So a partial restore from a backup, or a crash between two of the seven
/// writes, gives a mixed-generation world with no diagnostic - most visibly
/// chests standing where the terrain no longer has chests. A save-generation
/// counter, minted once when the directory is created and written identically
/// into all seven headers, would make it detectable in one comparison.
///
/// **Not done, and here is the price of doing it.** It is new bytes in every
/// header, so it is seven simultaneous version bumps - and the six that are not
/// the chunk version are the ones deliberately kept independent *so that
/// regenerating terrain never costs a player their inventory, their chests or
/// their animals*. Buying detection of a hand-restored backup at the cost of
/// wiping every inventory in existence is the wrong trade for a single-player
/// game with an expendable world. Revisit it when a save format changes for
/// another reason anyway and the bump is already being paid for.
constexpr std::array<char, 4> kMagic{'V', 'X', 'C', 'H'};
constexpr std::array<char, 4> kPlayerMagic{'V', 'X', 'P', 'L'};
constexpr std::array<char, 4> kFurnaceMagic{'V', 'X', 'F', 'N'};
constexpr std::array<char, 4> kCampfireMagic{'V', 'X', 'C', 'F'};
constexpr std::array<char, 4> kChestMagic{'V', 'X', 'C', 'T'};
constexpr std::array<char, 4> kStowboxMagic{'V', 'X', 'S', 'B'};
constexpr std::array<char, 4> kCreatureMagic{'V', 'X', 'C', 'R'};
constexpr std::array<char, 4> kDropMagic{'V', 'X', 'D', 'R'};

/// **The eight signatures above must stay pairwise distinct, and until now
/// nothing said so.** Written 2026-08-19.
///
/// Every reader in this file identifies a save by comparing the four bytes it
/// read against one of these. Two that matched would not fail: a stowbox file
/// would pass the chest reader's check and be parsed as a chest, taking a
/// different record size off the same bytes. Four of the eight already share
/// `VXC` and differ only in the last character, which is exactly the shape that
/// invites a copied line.
///
/// The count is **derived from the list** rather than written down - class
/// template argument deduction sizes `kAllMagics` from its initialiser - so
/// this table cannot be short. That matters here specifically: `std::array`
/// accepts a *short* initialiser silently and zero-fills the tail, so a
/// hand-written size would let a missing entry become four NUL bytes that
/// compare equal to nothing and quietly pass this very check.
///
/// **Falsified by** a `constexpr std::array<char, 4> k...Magic` in this file
/// that does not appear in `kAllMagics`. Re-run that search rather than
/// trusting this paragraph; it read 8 declared and 8 listed, all distinct, on
/// 2026-08-19.
constexpr std::array kAllMagics{kMagic,         kPlayerMagic, kFurnaceMagic,
                                kCampfireMagic, kChestMagic,  kStowboxMagic,
                                kCreatureMagic, kDropMagic};

constexpr bool sameSignature(const std::array<char, 4>& a, const std::array<char, 4>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

constexpr bool magicsAreDistinct() {
    for (std::size_t a = 0; a < kAllMagics.size(); ++a) {
        for (std::size_t b = a + 1; b < kAllMagics.size(); ++b) {
            if (sameSignature(kAllMagics[a], kAllMagics[b])) {
                return false;
            }
        }
    }
    return true;
}

static_assert(magicsAreDistinct(),
              "two save signatures are identical - one file type will be parsed as another; "
              "give the new format its own four bytes rather than copying a neighbour's");

/// The negative half, and the reason it is here: `magicsAreDistinct` passes
/// **vacuously** if `sameSignature` can never answer "same". A comparison
/// written the wrong way round, or one that returns early on the first
/// character, would leave the assert above green while checking nothing. So the
/// detector is required to fire on a known pair and to stay silent on another -
/// a known-answer control, stated where a compiler holds it rather than in a
/// paragraph that can rot.
static_assert(sameSignature(kMagic, kMagic) && !sameSignature(kMagic, kPlayerMagic),
              "sameSignature no longer distinguishes two four-byte signatures, so the "
              "distinctness assert above is passing without testing anything");
/// Versions `player.dat` alone, and is deliberately **not** the chunk version:
/// bumping that one regenerates stale terrain, and doing so must never cost the
/// player their inventory or position. Bumped to 2 at M21, when the record grew
/// health and hunger.
///
/// **Bumped to 4 on 2026-08-18, and version 4 means two things at once** - the
/// record grew an ender chest, *and* eighteen duplicate item ids were deleted
/// from the catalogue, sliding every id above them down. Both landed the same
/// day and both need a version 3 file read differently, so they share one
/// number and one migration rather than each claiming 4 and quietly disagreeing
/// about what a 4 contains.
///
/// Every older version is *upgraded* rather than rejected - there is nothing in
/// a version 2 or 3 file that a version 4 file cannot say.
///
/// **Bumped to 5 on 2026-08-18, when the record grew the bed the player last
/// slept in.** Until then the respawn point was a plain local in `main()` that
/// no save path could see, so sleeping in a bed and quitting lost it silently;
/// see `SavedPlayer::respawnBed`. A version 4 file is upgraded and given no bed,
/// which is exactly the behaviour it had when it was written - nothing about
/// such a world regresses, it simply stops getting worse.
///
/// **Bumped to 8 on 2026-08-19, when the record grew the time of day and the
/// weather.** Same shape of gap as the bed and the ender chest before it: both
/// were plain locals nothing could save, so every launch began at morning under
/// a clear sky however long the world had been played. A version 7 file is
/// upgraded and given exactly that - `0.18` and no rain - which is what it had
/// when it was written.
///
/// **This is not the `kChunkFormatVersion` 7 -> 8 landing in the same session,
/// and the coincidence of numbers is worth naming before it misleads someone.**
/// The two constants are deliberately independent - see the paragraph above -
/// so that regenerating terrain never costs a player their inventory. They
/// arrived at 8 together by accident, one because bee nests changed what
/// generates and one because the player record grew five fields, and neither
/// implies the other.
constexpr std::uint32_t kFormatVersion = 8;
constexpr std::uint32_t kPlayerVersionV7 = 7;
constexpr std::uint32_t kPlayerVersionV6 = 6;
constexpr std::uint32_t kPlayerVersionV5 = 5;
constexpr std::uint32_t kPlayerVersionV4 = 4;
constexpr std::uint32_t kPlayerVersionV3 = 3;
constexpr std::uint32_t kPlayerVersionV2 = 2;

/// Version 2's field list, kept so a world saved before the inventory was
/// written down still resumes where it was standing rather than at spawn.
///
/// **Spelled out as its own record rather than read as a prefix of the current
/// one.** A prefix read is a silent promise that nobody reorders the first
/// seven fields, and the rule at `SavedPlayer` is that a save record is a
/// stated list of fields - so the fields are copied across by name below and
/// the compiler is told about both shapes.
struct LegacyPlayerV2 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
};

/// Version 3's field list - everything the current record has except the ender
/// chest. Stated in full for the same reason version 2 is, even though it
/// happens to be a prefix of the current struct today: a prefix read is a
/// promise that nobody ever inserts a field, and this file's whole job is to
/// stop a struct changing shape quietly.
struct LegacyPlayerV3 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    std::array<ItemStack, kInventorySlots> inventory{};
    std::int32_t selectedSlot = 0;
};

static_assert(sizeof(LegacyPlayerV2) == 36,
              "the version 2 player record is a fact on disk and cannot change size");
static_assert(sizeof(LegacyPlayerV3) == 472,
              "the version 3 player record is a fact on disk and cannot change size");

/// Version 4's field list - everything the current record has except the bed
/// the player last slept in.
///
/// **This is the shape that was actually shipped, so it is spelled out here and
/// never read as a prefix of `SavedPlayer`.** A prefix read costs nothing today
/// and is a standing promise that nobody ever inserts a field above the tail;
/// the day someone does, every version 4 file on disk starts loading as a
/// plausible-looking world with the wrong inventory in it, and no check in this
/// file would notice. Restating it means the compiler holds both shapes at once
/// and the migration below has to name every field it carries across.
struct LegacyPlayerV4 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    std::array<ItemStack, kInventorySlots> inventory{};
    std::int32_t selectedSlot = 0;
    Chest enderChest{};
};

static_assert(sizeof(LegacyPlayerV4) == 796,
              "the version 4 player record is a fact on disk and cannot change size");

/// Version 5's field list: version 4 plus the bed, and nothing after it.
///
/// Restated for the same reason as every rung above - so that the compiler
/// holds this shape and today's shape at once, and the migration below has to
/// name every field it carries across rather than trusting a prefix read.
struct LegacyPlayerV5 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    std::array<ItemStack, kInventorySlots> inventory{};
    std::int32_t selectedSlot = 0;
    Chest enderChest{};
    glm::ivec3 respawnBed{0, -1, 0};
};

static_assert(sizeof(LegacyPlayerV5) == 808,
              "the version 5 player record is a fact on disk and cannot change size");

/// The shape shipped as version 6: version 5 plus the effect list and the
/// absorption pool, and still missing both the grant's clock and the armour
/// actually being worn.
///
/// Restated for the same reason as every rung above - so that the compiler
/// holds this shape and today's shape at once, and the migration below has to
/// name every field it carries across rather than trusting a prefix read.
struct LegacyPlayerV6 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    std::array<ItemStack, kInventorySlots> inventory{};
    std::int32_t selectedSlot = 0;
    Chest enderChest{};
    glm::ivec3 respawnBed{0, -1, 0};
    std::array<SavedEffect, kSavedEffectSlots> effects{};
    float absorption = 0.0f;
};

static_assert(sizeof(LegacyPlayerV6) == 1196,
              "the version 6 player record is a fact on disk and cannot change size");

/// Version 7's field list: everything the record held before the time of day
/// and the weather joined it.
///
/// Restated for the same reason as every rung above - so that the compiler
/// holds this shape and today's shape at once, and the migration below has to
/// name every field it carries across rather than trusting a prefix read.
struct LegacyPlayerV7 {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::int32_t health = 20;
    std::int32_t food = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    std::array<ItemStack, kInventorySlots> inventory{};
    std::int32_t selectedSlot = 0;
    Chest enderChest{};
    glm::ivec3 respawnBed{0, -1, 0};
    std::array<SavedEffect, kSavedEffectSlots> effects{};
    float absorption = 0.0f;
    float absorptionSeconds = 0.0f;
    std::array<ItemStack, kArmourSlots> armour{};
};

static_assert(sizeof(LegacyPlayerV7) == 1248,
              "the version 7 player record is a fact on disk and cannot change size");

/// **The saved effect list has to be at least as wide as the live one.**
///
/// `effects::kMaxActive` is derived from `effectInfo` and has moved before, 8
/// to 22. If it ever passes `kSavedEffectSlots`, a player carrying a full set
/// of effects would have the tail of the list silently dropped at save time -
/// no warning, no crash, just the last few potions missing on the next launch.
/// The saved width is a fact on disk and cannot follow it automatically, so the
/// build stops here instead and the widening becomes a version bump, which is
/// what it always was.
///
/// > Fails on: adding storable effect ids past 32 without widening the record.
static_assert(effects::kMaxActive <= kSavedEffectSlots,
              "more live effect slots than the save format has room for; widen "
              "kSavedEffectSlots and bump the player version");

/// Fails on: adding a field to `SavedPlayer` for version 7 without bumping the
/// version again. The current record must be strictly larger than the last one
/// shipped, or the two are the same layout wearing different numbers - which is
/// a bump that changes nothing, and this project has paid for one of those.
static_assert(sizeof(SavedPlayer) > sizeof(LegacyPlayerV7),
              "version 8 must say something version 7 could not");

/// **The writer and the reader, checked against each other rather than each
/// against itself.**
///
/// Nothing else here proves that the five shapes above are the *same* record at
/// five ages. The `sizeof` assert beside each one pins it individually, and an
/// individually-correct set of five numbers is exactly what a mis-stated legacy
/// struct looks like: `loadPlayer` would read the right number of bytes into the
/// wrong field list, every value would be plausible, and the file would load.
///
/// Each rung differs from the next by **precisely the members the migration
/// beside it names, measured off `SavedPlayer` itself**. That is what makes
/// this a check and not a restatement: the right-hand side is the live struct's
/// answer and the left-hand side is a number frozen on disk, so the two cannot
/// be wrong together. Insert a field anywhere above the tail and every line
/// below the insertion fails at compile time, naming the rung that has stopped
/// being true.
///
/// > Fails on: adding a field to `SavedPlayer` in the middle rather than at the
/// > tail - the one edit that makes every version 4 file on disk load as a
/// > plausible world with the wrong inventory in it.
static_assert(sizeof(LegacyPlayerV7) ==
                  sizeof(SavedPlayer) - sizeof(SavedPlayer::timeOfDay) -
                      sizeof(SavedPlayer::weatherRaining) -
                      sizeof(SavedPlayer::weatherThundering) -
                      sizeof(SavedPlayer::weatherRainSeconds) -
                      sizeof(SavedPlayer::weatherThunderSeconds),
              "version 8 is version 7 plus the time of day and the four weather numbers and "
              "nothing else");
static_assert(sizeof(LegacyPlayerV6) ==
                  sizeof(LegacyPlayerV7) - sizeof(SavedPlayer::absorptionSeconds) -
                      sizeof(SavedPlayer::armour),
              "version 7 is version 6 plus a reserved absorption clock - a field that is always "
              "zero, derived on load and deliberately never populated, see SavedPlayer - plus "
              "the worn armour, and nothing else");
static_assert(sizeof(LegacyPlayerV5) == sizeof(LegacyPlayerV6) - sizeof(SavedPlayer::effects) -
                                            sizeof(SavedPlayer::absorption),
              "version 6 is version 5 plus the effect list and the absorption pool and nothing "
              "else");
static_assert(sizeof(LegacyPlayerV4) == sizeof(LegacyPlayerV5) - sizeof(SavedPlayer::respawnBed),
              "version 5 is version 4 plus the bed and nothing else");
static_assert(sizeof(LegacyPlayerV3) ==
                  sizeof(LegacyPlayerV4) - sizeof(SavedPlayer::enderChest),
              "version 4 is version 3 plus the ender chest and nothing else");
static_assert(sizeof(LegacyPlayerV2) == sizeof(LegacyPlayerV3) - sizeof(SavedPlayer::inventory) -
                                            sizeof(SavedPlayer::selectedSlot),
              "version 3 is version 2 plus the inventory and the held slot and nothing else");

/// **The ladder has to be contiguous, and this is what makes the next bump
/// impossible to get wrong.**
///
/// `loadPlayer` reads exactly the five numbers named here. Bump
/// `kFormatVersion` to 8 for a new field and every one of those five still
/// compiles, still runs, and **refuses every version 7 file on disk** - the
/// player's inventory, ender chest and bed, gone at the next launch, with one
/// warning line that says the file "does not match this world". The struct-size
/// asserts do not catch it: they only ever describe the shapes that exist.
///
/// So the rungs are required to run 2, 3, 4, 5, 6, 7, current with no gap.
/// Bumping the version now fails to build until `kPlayerVersionV8 = 8` and a
/// `LegacyPlayerV8` spelling out today's fields have been written beside it -
/// which is the work the bump was always supposed to include.
///
/// The ladder bottoms out at 2 on purpose; version 1 predates the record having
/// a stated field list at all and is a stranger like any other.
static_assert(kFormatVersion == kPlayerVersionV7 + 1 &&
                  kPlayerVersionV7 == kPlayerVersionV6 + 1 &&
                  kPlayerVersionV6 == kPlayerVersionV5 + 1 &&
                  kPlayerVersionV5 == kPlayerVersionV4 + 1 &&
                  kPlayerVersionV4 == kPlayerVersionV3 + 1 &&
                  kPlayerVersionV3 == kPlayerVersionV2 + 1 && kPlayerVersionV2 == 2,
              "every shipped player version needs a rung in loadPlayer; a gap here is an "
              "inventory silently thrown away on the launch after a version bump");

/// Chunks carry their own version, separate from the player file's.
///
/// **Bumped to 2 when waterlogging arrived**, and to 3 when `BlockId` was
/// widened to sixteen bits. Version 2 was read and upgraded in place for a
/// while, because the id *numbers* did not change when the type did.
///
/// **That upgrade path is gone, and removing it is what fixed a real bug.** A
/// playtest found water hanging in mid-air; a probe put it at a 32x2 sheet at
/// y=64 in the chunk at the origin, left there by a long-deleted test harness
/// that wrote into the real world. Generating the same chunk from the same seed
/// produced none of it - so the damage was a fact on disk, and the only thing
/// that can undo a fact on disk is refusing to read it. Bumping this number was
/// tried first and did nothing, because a file that old is not version 3 at all.
///
/// The cost is that anything built in a chunk untouched since the id widening
/// goes with it. That is the trade this number exists to make.
///
/// **Bumped to 4 after the 2026-08-10 ground-cover change, and a playtest is
/// why.** It was held at 3 first, on the reasoning that patchier grass is
/// cosmetic and not worth rejecting every chunk on disk: new chunks would get
/// the new rule and already-visited ones would keep the old cover.
///
/// **That reasoning was wrong, and the failure does not look like a save bug.**
/// A world played across the change is a patchwork - here, 32 chunks loaded and
/// 186 rejected and regenerated - and the damage is all at the *seams*. What
/// gets reported is sugar cane hanging in the air and a river that stops dead,
/// because the ground a plant grew from and the channel a river ran down were
/// regenerated differently one chunk over. It reads as a worldgen fault, the
/// generator is innocent, and the world has to be thrown away regardless - so
/// holding the number back saved nothing and cost a playtest.
///
/// **So: anything that changes the shape of terrain bumps this.** Only a change
/// that cannot disagree with a neighbouring chunk may leave it alone. Losing
/// what was built is the smaller cost, because the alternative is losing it
/// anyway with a day of confusion first.
///
/// **Bumped to 5 on 2026-08-17, by that rule and without a second playtest to
/// argue about it.** Ground cover used to refuse the column whose surface is the
/// last block of the chunk below - `plantY == 0` was read as out of range when
/// it is a real placement, the plant belonging to this chunk and the ground it
/// stands on to the one underneath. Every world therefore had two bare bands
/// running through it, at y 32 and y 64, wherever a surface landed exactly on a
/// chunk floor. It does not now.
///
/// That is a block present or absent, not a shade of green: a chunk saved before
/// the change keeps its bare band while the neighbour regenerated beside it
/// grows the plants, and the seam between them is the same "the generator has
/// gone wrong" report the version-4 bump was paid for once already.
///
/// **Bumped to 6 on 2026-08-18, for two owners at once and eleven reasons.**
/// Terrain: the biome table's hot-and-wet hole is closed and Desert widened
/// over it, multi-block plants are no longer truncated at the chunk ceiling,
/// the ore table is genuinely rarest-first with two inversions corrected,
/// `veinHeight` is the vanilla sum rather than a 50/50 mixture,
/// `kShallowBedDepth` went 6 to 1 - a reference y-distance that had never been
/// converted into our 96-block world - the coastline factor is ramped rather
/// than stepped, and `kSteepDropOverTwoColumns` went 2 to 4. Structures: the
/// town square moved up one block, four interior collisions in village
/// buildings are fixed, plot bounds are accumulated from what is actually
/// written rather than from a probe lattice, and tree height ranges were
/// re-derived for our world height.
///
/// Every one of those changes which blocks a column contains, so every one of
/// them can disagree with a neighbour, so the rule above settles it on its own.
///
/// **There is no migration for this number and there cannot be one.** A record
/// version can be migrated because a record is a list of fields and a field can
/// be remapped; terrain is not a field, it is the whole chunk, and there is no
/// function from "the ore column version 5 generated" to "the one version 6
/// would have". So this version means **discard and regenerate**, and the check
/// in `load` is a strict `!=` for that reason. **Never widen it to
/// `version != kChunkFormatVersion && version != 5`** to "keep what people
/// built": that is a bump that changes nothing, which this project has already
/// paid for once, and the file it lets through is exactly the one that produces
/// the seam.
///
/// **6 to 7**: a chunk now carries one more per-cell bit array after the
/// waterlogged one - `Chunk::stateBitData`, which holds Bedrock's
/// `persistent_bit` on a leaf and its `age_bit` on a sapling. A version 6 file
/// is simply shorter, so it would load with every bit clear and be *correct*;
/// the bump is here because the same release re-derives nothing about terrain
/// but does change which leaves are allowed to exist - a canopy generated
/// before leaf decay existed is a canopy nothing ever checked - and the cheap
/// answer is to regenerate rather than to reason about it.
///
/// **7 to 8**: trees in meadow, plains, forest and dense forest can now carry a
/// bee nest (`Structures.cpp`, `beeNestChance` per biome, placed under the
/// lowest leaf). **Nothing about the file format changed** - a version 7 chunk
/// loads and is internally consistent - but it was generated by a decorator
/// that could not emit a nest, so every tree in it is nestless and no bee will
/// ever have a home. That is the "discard and regenerate" case exactly: there
/// is no function from the trees version 7 grew to the ones version 8 would
/// have.
///
/// It is also the failure that does not look like a stale save. Without the
/// bump a playtester loads the world they already have, walks a meadow, finds
/// no nests and reports **the feature as not working** - while the biome table,
/// the probabilities and the placement are all correct. `CLAUDE.md`'s new
/// session note 7 is this bug verbatim, and merely standing near water is
/// enough to have written chunks to disk, so "I have not built anything yet" is
/// not a reason to think `saves/` is empty.
///
/// **The same 8 also carries the lava, and this paragraph exists so that nobody
/// bumps it to 9 for that.** `TerrainGenerator.cpp` now fills carver-emptied
/// cells at or below `kLavaLevel` (6) with `BlockId::Lava0`, which is the same
/// class of change as the bee nests - the file format is untouched, and what
/// moved is what the generator emits - so it wants a regenerate and one
/// regenerate is enough for both. A second bump would be a bump that changes
/// nothing, which is the exact mistake the paragraphs above are written against,
/// and it would cost the player a second world reset for no gain.
///
/// **Measured on the bytes rather than reasoned about, because that is what
/// this project's session note 7 demands.** Every chunk file under `build/` on
/// 2026-08-19 reads `VXCH` with version **1, 3 or 4** - the newest written
/// 2026-08-11, days before either change - so there is no version 8 chunk
/// anywhere for the lava change to have missed, and there cannot be one: the
/// hardware throttle means no build has been produced since the bee-nest bump
/// landed, so no executable that writes an 8 has ever run.
///
/// > **Falsified by** a `*.chunk` file whose bytes 4..7 read 8 and whose
/// > modified time predates the lava landing. Read the header rather than
/// > trusting this note - four bytes of magic, then the version - and if one
/// > turns up, 9 is correct after all. Re-run that check rather than believing
/// > this paragraph; it read 0 such files on 2026-08-19.
constexpr std::uint32_t kChunkFormatVersion = 8;

/// Everything `save` writes after the header, and everything `load` reads after
/// it - blocks, then the waterlogged bits, then the state bits, in that order.
constexpr std::size_t kChunkPayloadBytes =
    Chunk::kBlockBytes + Chunk::kWaterloggedBytes + Chunk::kStateBitBytes;

/// **What proves the file format still describes the whole chunk.**
///
/// A `Chunk` is four arrays and the format names three of them. The fourth is
/// the light, one byte per cell, and it is left out because it is *derived* -
/// it crosses chunk boundaries, so it cannot be recomputed from one chunk alone
/// and is instead relit by `World` after a load. Every other byte of a `Chunk`
/// is state a player made and has to be on disk.
///
/// So the accounting is: the sections the format lists, plus the light, is the
/// whole struct. Add a fifth per-cell array - the next blockstate bit, a biome
/// id, a per-cell tint - and this fails, naming the file format as the thing
/// that has to grow with it and `kChunkFormatVersion` as the number that has to
/// move. Nothing else would notice: a chunk simply saves without the new array
/// and reloads with it cleared, which reads as the feature not working rather
/// than as a save bug.
///
/// **Both sides are `constexpr` and neither restates the other**, so this
/// cannot rot: the left is the compiler's answer for `sizeof(Chunk)` and the
/// right is the format's own list.
///
/// > Fails on: widening the light array to two bytes a cell, or adding a fifth
/// > array to `Chunk`, or deleting a `write` from `save`.
static_assert(sizeof(Chunk) == kChunkPayloadBytes + Chunk::kBlockCount,
              "every byte of a Chunk is either written to the chunk file or is the light array, "
              "which is derived and deliberately not stored");

/// **What a chunk bump does to the block-entity tables, written down because it
/// is a decision and not an accident.**
///
/// `chests.dat`, `furnaces.dat`, `stowboxes.dat` and `creatures.dat` are keyed
/// by position or by handle and are versioned separately, so **they survive a
/// chunk bump untouched** while every chunk under them is regenerated. That is
/// right for two of the four and wrong for two:
///
/// - **Stowboxes are correct as they stand.** A stowbox's contents are keyed by
///   a handle riding in an item's `damage`, and that item is in the player's
///   bag - which `player.dat` still holds. Nothing about it refers to terrain,
///   so discarding it would destroy something the player can still reach.
/// - **Creatures are acceptable.** One may reload inside new rock; the physics
///   pushes it out or it dies and the spawner replaces it, which is a bad
///   minute rather than a broken world.
/// - **Chests and furnaces are orphaned, every one of them.** A chunk file is
///   only written when the player *modified* it, so rejecting it throws away
///   every block they placed - including the chest block itself. The contents
///   stay in `chests.dat` at a position that is now whatever the generator put
///   there: unreachable, invisible, and rewritten to disk on every save for the
///   life of the world.
///
/// **And the orphan is not merely dead weight.** Place a new chest on that
/// exact cell and the restored entry is still there waiting, so the player
/// opens an empty-looking chest full of their old gear - or, for a village loot
/// chest whose "already rolled" mark lived in the discarded block id, rolls the
/// same loot a second time. That is duplication, not loss.
///
/// It is left alone here on purpose, and the fix is not a save-format rule,
/// because this file cannot see the world. **But the guard this comment used to
/// propose does not work, and the correction matters more than the original
/// note.** It said: one guard where a container is restored or first opened,
/// *if the block at this position is not a container, forget the entry*. That
/// catches an ordinary orphan - a stale entry under new terrain - and it does
/// **not** catch the loot chest, because `isChest` deliberately includes
/// `isLootChest`, so a regenerated marker passes the test and the entry is
/// kept. Implementing it as written would leave finding 871 wide open while
/// looking like it had been closed.
///
/// **What actually distinguishes the two is whether a record exists at all**,
/// since `Chest.hpp` gives a never-opened chest *no* entry rather than an empty
/// one. So the guard is: at `materialise`, if `chests` already holds this cell,
/// treat that as proof of the roll - overwrite the marker with
/// `plainChestFor(id)` and do **not** call `rollInto`.
///
/// **That was two thirds of a fix when it was written, and the last third
/// landed on 2026-08-19 - so what follows is a correction, not a request.**
/// The gap was real: `Main.cpp` wrote a chest only `if (!chest.empty())`, so a
/// chest the player had emptied completely had no record either, was
/// indistinguishable from one never opened, and rolled again on the next chunk
/// discard. That is the *ordinary* way a chest gets looted, so it was the
/// common case rather than the corner, and it was measured at a full 11 items
/// handed back after a single bump.
///
/// > **What closed it, named by symbol, because a negative claim in a header
/// > outlives every ledger entry and this one nearly did.** `Main.cpp` keeps a
/// > `rolledLoot` set of cells (:3545), seeds it at load from the saved records
/// > (:3553), consults it in the roll gate as
/// > `chests.find(at) == chests.end() && rolledLoot.count(at) == 0` (:3834),
/// > and writes a **deliberately empty** `PlacedChest` for every rolled cell
/// > with nothing left to carry - both for cells still in the map (:4963) and
/// > for cells whose entry is gone entirely because the chest was broken
/// > (:5007). The empty record *is* the roll flag, and unlike the block id it
/// > survives a chunk discard. Proved on real bytes rather than assumed: two
/// > records in, two out, the empty one seeding `rolledLoot` on the next
/// > launch, with three controls firing.
/// >
/// > **So `saveChests` writes whatever it is handed and must keep doing so.**
/// > Adding an `if (!chest.empty())` here - which looks like an obvious saving,
/// > since an empty chest is nothing - silently reopens finding 1262 from the
/// > other end, because it would strip exactly the records that carry no items
/// > by design. `Chest.hpp` documents the same exception from its side.
/// >
/// > **Falsified by** `rolledLoot` disappearing from `Main.cpp`, or by
/// > `saveChests` growing a per-record emptiness test.
///
/// **What is still open here is the orphan, not the duplicate.** Chest and
/// furnace records outlive the chunk they belong to; `Main.cpp`'s
/// `stillHasItsBlock` drops an entry whose block is gone, but only where the
/// column is loaded, since an absent chunk reads as air and dropping on that
/// would delete every container the player has walked away from. So a record
/// orphaned by a bump in terrain nobody has revisited is still written out on
/// every save. That is bloat rather than loss, and the developer's standing
/// position covers the rest: nothing in any save is precious, and worlds
/// regenerate freely.

/// Versioned separately from chunks and the player, because they store
/// `ItemStack`s and so have to be invalidated whenever those change **shape or
/// numbering**. A shared version would throw away every saved chunk for the
/// same reason.
///
/// Each has a three-rung ladder rather than a flag, and the middle rung is the
/// one that keeps getting forgotten:
///
/// - the **current** version, read as-is;
/// - the version written **before the duplicate runs were deleted** on
///   2026-08-18, whose ids above the block/item boundary have all slid down by
///   up to eighteen;
/// - the version written **before the block/item boundary moved** from 256 to
///   4096, which needs that shift as well - and only the furnaces and chests
///   are old enough to have one.
///
/// Previous versions are **upgraded rather than rejected** throughout: no id
/// was ever reused, so every one of them still says exactly what it meant.
constexpr std::uint32_t kNoSuchVersion = 0;

constexpr std::uint32_t kFurnaceVersion = 4;
constexpr std::uint32_t kFurnaceDuplicateRunVersion = 3;
constexpr std::uint32_t kFurnaceLegacyItemVersion = 2;
constexpr std::uint32_t kChestVersion = 3;
constexpr std::uint32_t kChestDuplicateRunVersion = 2;
constexpr std::uint32_t kChestLegacyItemVersion = 1;
/// **New table, so it starts at 1 and has no ladder underneath it.**
///
/// `campfires.dat` did not exist before campfires cooked, so there is no older
/// numbering for it to have been written in and nothing to migrate from: a
/// world saved by any previous build has no file, which reads as no campfire
/// holding anything - which is what was true, since nothing could be put on
/// one. The `eraForVersion` ladder the furnaces and chests carry is deliberately
/// absent rather than stubbed, because a rung that never existed is not the same
/// as a rung that is empty, and an empty one invites the next reader to add a
/// migration to a version that was never written.
constexpr std::uint32_t kCampfireVersion = 1;
/// New at M29c, so it never saw the 256 boundary and has only two rungs.
constexpr std::uint32_t kStowboxVersion = 2;
constexpr std::uint32_t kStowboxDuplicateRunVersion = 1;
/// **Bumped to 4 when a villager's three claimed cells joined the record**, and
/// version 3 is *rejected* rather than upgraded.
///
/// The upgrade was written out and thrown away, because reading an old record
/// and defaulting the new fields produces exactly the broken state this change
/// exists to fix: a villager that is employed but claims nothing can never work
/// again, can never re-claim - the claim gate is closed to anyone with a
/// profession - and leaves its workstation reading as free for the next
/// villager to take, which is where the duplicate armourer came from. An
/// upgraded file would therefore load, look right, and be wrong in the one way
/// the version bump is meant to end.
///
/// Rejecting costs a herd, and that is all it costs: creatures are respawned by
/// the continuous spawner and a village's residents by the same deterministic
/// generator that laid the village out, so what comes back is a world with
/// animals in it and villagers who claim beds on the first morning. That is the
/// trade the chunk-format comment above makes in the other direction, and the
/// asymmetry is the point - terrain cannot be regenerated without losing what
/// was built on it, and a population can.
///
/// **Bumped to 5 on 2026-08-18, and this one adds a section rather than changing
/// a record.** The chunk columns whose one-off group has already been placed now
/// follow the creatures. Without them that set was session-only, rebuilt on load
/// from whichever creatures happened to come back - so quitting far from a
/// village, whose residents were not in memory to mark it, repopulated it on the
/// next load, and the population grew every session until the village stood
/// shoulder to shoulder with itself.
///
/// **There is no rung for version 4, deliberately, and it is worth saying why
/// because one was written and then removed.** Version 4 was never shipped: the
/// last commit writes 3, with a `SavedCreature` whose `kind` is still a single
/// byte, and 4 was created and superseded inside this one working tree. Reading
/// it looked free - `SavedCreature` did not change between 4 and 5, so such a
/// file is simply a v5 that stops early - but that reasoning rests on 4 having
/// meant one layout, and an unshipped number is not a format. It is a label that
/// was never frozen, and it moved at least once inside this tree without a bump,
/// which is the very reason it cannot be trusted to have moved only once.
///
/// So 4 is rejected like any other stranger. The cost is one herd, on a
/// developer's machine, once - and the file that replaces it is written by this
/// build, which is the only kind whose meaning is known.
///
/// **Bumped to 6 to put the populated-column markers in FRONT of the creature
/// array, which is a fix rather than a tidy-up.** In version 5 the two records
/// shared one file with the markers last, reachable only by seeking blindly
/// across `creatureCount * sizeof(SavedCreature)` bytes. A file torn anywhere
/// inside the creature array therefore landed that seek short, the marker count
/// read failed, and `loadPopulatedColumns` returned empty - so every column the
/// player had already explored was marked unpopulated again and the initial
/// spawn pass refilled all of it with a fresh herd. That is exactly the failure
/// this record was introduced to stop, resurrected by one torn write.
///
/// The two sections are not equally worth protecting, which is what decides the
/// order. Losing creatures costs one herd and the spawner replaces them;
/// losing the markers re-breeds every place the player has ever been. A tear
/// takes whatever is nearest the end of the file, so the cheap section goes
/// last and the expensive one is read straight out of the header with no seek
/// at all.
constexpr std::uint32_t kCreatureVersion = 6;

/// **New table, so it starts at 1 and has no ladder underneath it**, for the
/// same reason `kCampfireVersion` has none: `drops.dat` did not exist before
/// this, so no older build wrote one and there is no numbering to migrate from.
/// A world saved before it simply has no file, which reads as nothing lying on
/// the ground - which is what every previous launch handed back anyway, because
/// the floor was cleared at quit.
///
/// **It carries no `eraForVersion` rung either**, and that is a decision rather
/// than an omission. The furnace and chest tables need one because they hold
/// item ids written before the catalogue was renumbered; this table is younger
/// than every renumbering, so the only ids it can contain are today's.
constexpr std::uint32_t kDropVersion = 1;

/// Sanity bound on a file the game did not write this run. Far more furnaces
/// than anyone would place. It bounds what a header may *claim*, not what gets
/// allocated - that is `reserveFor`, and the difference is measured there.
constexpr std::uint32_t kMaxFurnaces = 1u << 20;
constexpr std::uint32_t kMaxChests = 1u << 20;
/// Its own bound for the reason `kMaxStowboxes` has its own: campfires and
/// furnaces are unrelated quantities and a borrowed name retunes both.
constexpr std::uint32_t kMaxCampfires = 1u << 20;
/// **Its own bound, though it holds the same number as the chests' today.**
/// `loadStowboxes` used `kMaxChests`, which was harmless only for as long as the
/// two agreed - and they are unrelated quantities, one bounded by how many
/// blocks a player places and the other by how many box *items* exist. The day
/// either is retuned the borrowed name silently retunes the other table too.
constexpr std::uint32_t kMaxStowboxes = 1u << 20;
/// Far more than the population cap could ever reach. Like the bounds above it
/// limits what a header may claim; `reserveFor` limits what is allocated.
constexpr std::uint32_t kMaxCreatures = 1u << 16;
/// A column is 32 blocks square, so this is every column inside a world some
/// sixty-five thousand blocks across - far more than a player will ever walk.
/// (2048 columns to a side at 32 blocks each; the comment this replaces said
/// forty thousand, which was the arithmetic done once and never checked - and
/// it also claimed this bound kept the allocation small, which it does not: see
/// `reserveFor`, where that arithmetic is finally done.)
constexpr std::uint32_t kMaxPopulatedColumns = 1u << 22;
/// **Its own bound, and a much tighter one than the block tables above.**
///
/// Drops are transient by design - five minutes and gone - so a world holding
/// a million of them is a world where something has gone wrong, not a world
/// where somebody has been busy. 65536 is far past any plausible floor (a
/// player emptying a double chest makes 54) while still bounding the read.
constexpr std::uint32_t kMaxDrops = 1u << 16;

/// **How many records to reserve up front, whatever the header claims - which
/// is a different question from how many are allowed.**
///
/// The bounds above are *acceptance* limits: a file claiming more than one of
/// them is rejected whole. Three of them additionally claimed to be "small
/// enough that a corrupt length cannot ask for an enormous allocation", and
/// that was arithmetic nobody had done. Measured against the record sizes this
/// file already asserts: 1<<20 chests at 336 bytes each is **352 MB**, 1<<20
/// stowboxes at 328 is **344 MB**, 1<<20 campfires at 76 is 80 MB, 1<<20
/// furnaces at 60 is 63 MB, and 1<<22 populated columns at 8 is 34 MB. A single
/// flipped bit in a four-byte header therefore asked for a third of a gigabyte
/// - on the one path whose entire purpose is surviving a file this build did
/// not write.
///
/// **And if that allocation throws it does not cost the table, it costs the
/// session**: `reserve` throws `std::bad_alloc`, no loader here catches it, and
/// unwinding out of a load is not something this game is written to survive. A
/// bad four bytes should degrade to "that file is ignored", which is what every
/// other check in these loaders already achieves.
///
/// So the bound gates acceptance and this gates the *guess*. `reserve` is a
/// capacity hint and nothing more, so clamping it cannot change what is read or
/// how much: a table genuinely larger than this simply grows the way any vector
/// does, at the cost of a few reallocations on a path that already touches the
/// disk. 4096 is far past every population these files hold in practice and
/// costs 1.4 MB at the widest record.
constexpr std::uint32_t kReserveCeiling = 4096;

/// The number of records a loader should reserve for a header claiming `count`.
///
/// A free function rather than `std::min` spelled out at seven call sites,
/// because seven copies of a clamp is seven chances for one of them to be
/// written against the wrong ceiling - and because the name says *why* at the
/// point of use, where `std::min` would only say what.
constexpr std::size_t reserveFor(std::uint32_t count) {
    return count < kReserveCeiling ? static_cast<std::size_t>(count)
                                   : static_cast<std::size_t>(kReserveCeiling);
}

// These records are written and read as raw bytes. If anything in `Furnace`
// ever gains a pointer, a string or a virtual, that stops being valid and this
// is where it will be caught rather than in a corrupt save.
//
// **The list is the six records the loaders `file.read` into, and it was five
// until 2026-08-19.** `PlacedCampfire` was missing, which is the one direction
// this can fail in silently: the *writing* side is covered wherever it goes,
// because `AtomicSave::writeValue` carries its own trivially-copyable assert,
// but every loader here reads with a bare
// `file.read(reinterpret_cast<char*>(&record), sizeof(record))` and there is no
// assert inside a `reinterpret_cast`. A record that stopped being trivially
// copyable would therefore still fail the build - on the writer - and a reader
// that had *no* writer would not. Counted rather than eyeballed: six record
// types are read raw here, six are named below.
static_assert(std::is_trivially_copyable_v<PlacedFurnace>,
              "PlacedFurnace is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<SavedCreature>,
              "SavedCreature is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<PlacedChest>,
              "PlacedChest is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<PlacedCampfire>,
              "PlacedCampfire is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<StowedBox>,
              "StowedBox is written as raw bytes and must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<SavedItem>,
              "SavedItem is written as raw bytes and must stay trivially copyable");

/// Which numbering a file's item ids are written in.
enum class ItemEra {
    /// This build's numbering. Nothing to do.
    Current,
    /// Before the eighteen duplicate ids were deleted on 2026-08-18.
    BeforeDuplicateRuns,
    /// Older still: block ids were eight bits and items began at 256.
    BeforeItemBoundary,
    /// **Not an era, a count** - and it is here because the `switch` in
    /// `migrateItemId` cannot be trusted to complain. MSVC's C4062, the warning
    /// for a `switch` over an enum missing an enumerator, is **off at `/W4`**,
    /// and the `return stored;` that C4715 forces onto the end of that function
    /// is a `default:` wearing a different hat: it hands the id back
    /// unmigrated. So a fourth era added above this line and forgotten below
    /// would load every chest, furnace and stowbox in the world in whatever
    /// numbering its file happened to be written in, silently, and the assert
    /// beside `migrateItemId` is what now stops that.
    kCount,
};

/// The numbering a file of this version is in, or nothing if this build cannot
/// read that version at all.
///
/// **One ladder for all four tables.** The alternative was the same three-way
/// test written out four times with the numbers changed, and the rung that gets
/// forgotten in the fourth copy is always the middle one - which is silent,
/// because a file that is merely mis-numbered still loads and still looks like a
/// file.
constexpr std::optional<ItemEra> eraForVersion(std::uint32_t version, std::uint32_t current,
                                               std::uint32_t beforeDuplicateRuns,
                                               std::uint32_t beforeItemBoundary) {
    if (version == current) {
        return ItemEra::Current;
    }
    if (beforeDuplicateRuns != kNoSuchVersion && version == beforeDuplicateRuns) {
        return ItemEra::BeforeDuplicateRuns;
    }
    if (beforeItemBoundary != kNoSuchVersion && version == beforeItemBoundary) {
        return ItemEra::BeforeItemBoundary;
    }
    return std::nullopt;
}

/// Reads one stored id in the numbering its file was written in.
///
/// **Each rung names the single stage it needs.** `Item.hpp` exposes the two
/// halves of the upgrade separately - `upgradeDuplicateRuns` for the deleted
/// runs, `upgradeLegacyItemId` for the composed whole - and the composed one is
/// safe *only* on a file written when items began at 256. Its first stage lifts
/// everything in `[256, 4096)` into the tool run, which is right for those files
/// and catastrophically wrong for any later one: there are 580 appended blocks,
/// so most block-item ids sit squarely inside that window and a chest of stone
/// would come back a chest of tools.
///
/// **An earlier version of this rung called the composed function behind a
/// boundary test**, which worked but left the file arguing with `Item.hpp` about
/// whether that test was load-bearing - and the wrong answer invites deleting
/// it. Asking for the stage by name removes the question: `upgradeDuplicateRuns`
/// needs no guard, because both deleted runs sat above the block/item boundary
/// so every block item takes its first branch and comes straight back out. The
/// asserts below pin that fact and pin the rung to the single stage.
constexpr ItemId migrateItemId(ItemId stored, ItemEra era) {
    switch (era) {
    case ItemEra::Current:
        return stored;
    case ItemEra::BeforeDuplicateRuns:
        return upgradeDuplicateRuns(stored);
    case ItemEra::BeforeItemBoundary:
        return upgradeLegacyItemId(stored);
    case ItemEra::kCount:
        break;
    }
    return stored;
}

/// Fails on: adding an era to the enum without giving it a rung in the `switch`
/// above. See `ItemEra::kCount` for why nothing else would notice - C4062 is off
/// at `/W4`, so a missing enumerator is silent, and the trailing `return`
/// C4715 demands would quietly hand back every id unmigrated.
static_assert(static_cast<int>(ItemEra::kCount) == 3,
              "migrateItemId handles three eras; a fourth needs a rung of its own, not the "
              "fall-through that would load a world's containers in the wrong numbering");

/// One stack, brought across from an older file.
///
/// **Takes and returns the whole stack and edits one field of it**, rather than
/// building a new one out of the parts it happens to care about. That is the
/// entire defence against the migration bug that does not look like one: a
/// format upgrade promises that old *damage* survives, not just old data, and
/// `ItemStack::damage` is overloaded - it is a tool's wear **and** the handle a
/// stowbox uses to find its contents in `stowboxes.dat`. A migration that
/// faithfully remapped every id and count but rebuilt the stack from
/// `{item, count}` would empty every container in the world and repair every
/// tool, and the file would still load and still look right.
///
/// `constexpr` so the promise can be asserted rather than believed.
constexpr ItemStack migrateStack(ItemStack stack, ItemEra era) {
    stack.item = migrateItemId(stack.item, era);
    return stack;
}

/// A saved blaze rod that a player had worn down and a stowbox handle riding in
/// the same field, put through the upgrade together.
constexpr ItemStack kMigrationProbe =
    migrateStack(ItemStack{static_cast<ItemId>(kLegacyBlazeRod), 3, 47},
                 ItemEra::BeforeDuplicateRuns);

/// Fails on: rebuilding the stack inside `migrateStack` as
/// `ItemStack{migrateItemId(stack.item, era), stack.count}` - the single edit
/// that empties every stowbox in the world while still loading cleanly.
static_assert(kMigrationProbe.item == ItemId::CinderRod && kMigrationProbe.count == 3 &&
                  kMigrationProbe.damage == 47,
              "a migration carries count and damage across untouched; damage is a stowbox's "
              "handle as well as a tool's wear");

/// Fails on: deleting a duplicate *block* id, which would put a moved id below
/// the boundary and break the no-guard property `upgradeDuplicateRuns` rests
/// on. `Item.hpp` states that property and points here for the proof, so this
/// is the assert it means.
static_assert(kLegacyShadowDiscFirst >= static_cast<int>(ItemId::kFirstToolItem) &&
                  kLegacyBlazeRod >= static_cast<int>(ItemId::kFirstToolItem),
              "upgradeDuplicateRuns passes block items straight through on the strength of both "
              "deleted runs being above the block/item boundary");

/// A block id above the *old* 256 boundary - most of the 580 appended blocks
/// are - which the duplicate-run stage must not touch and the boundary stage
/// would wreck.
constexpr ItemId kBlockItemAboveOldBoundary = static_cast<ItemId>(kLegacyFirstToolItem + 1);

/// Fails on: writing `upgradeLegacyItemId` in the `ItemEra::BeforeDuplicateRuns`
/// rung above, in place of `upgradeDuplicateRuns`. The second half is the proof
/// that the two are not interchangeable there: the composed function is exactly
/// what would move a block item that must not move.
static_assert(migrateItemId(kBlockItemAboveOldBoundary, ItemEra::BeforeDuplicateRuns) ==
                      kBlockItemAboveOldBoundary &&
                  upgradeLegacyItemId(kBlockItemAboveOldBoundary) != kBlockItemAboveOldBoundary,
              "a block item in a post-boundary file must survive the duplicate-run upgrade, and "
              "the composed function is exactly what would move it");

/// Fails on: an era being added to the enum without a rung here.
static_assert(eraForVersion(kChestVersion, kChestVersion, kChestDuplicateRunVersion,
                            kChestLegacyItemVersion) == ItemEra::Current &&
                  eraForVersion(kChestDuplicateRunVersion, kChestVersion,
                                kChestDuplicateRunVersion, kChestLegacyItemVersion) ==
                      ItemEra::BeforeDuplicateRuns &&
                  eraForVersion(kChestLegacyItemVersion, kChestVersion, kChestDuplicateRunVersion,
                                kChestLegacyItemVersion) == ItemEra::BeforeItemBoundary &&
                  !eraForVersion(kChestVersion + 1, kChestVersion, kChestDuplicateRunVersion,
                                 kChestLegacyItemVersion).has_value(),
              "every readable chest version must name an era, and nothing else may");

void migrateChest(Chest& chest, ItemEra era) {
    for (ItemStack& slot : chest.slots) {
        slot = migrateStack(slot, era);
    }
}

/// Blanks the two bytes `ItemStack` leaves between its sixteen-bit `item` and
/// its thirty-two-bit `count`.
///
/// Those bytes belong to the compiler, are never written by anything, and go
/// straight to disk with the rest of the record - 54 per chest, 108 per player
/// file, of whatever the stack happened to be holding. Blanking them makes a
/// save byte-identical for identical state, which is the difference between
/// "the file changed" being a fact and being noise.
///
/// **Written as address arithmetic rather than a field-by-field copy on
/// purpose.** A `scrubbed(record)` helper that named every field would be a
/// second list of fields to keep in step, and the failure when it drifted would
/// be a *new field silently written as zero* - which is the same shape as the
/// bug it was meant to prevent, only quieter. This one is blind to what the
/// record contains and only knows the shape of a stack, and the asserts beside
/// `ItemStack` in the header are what keep that knowledge honest.
///
/// **Self-neutralising**: close the hole in `Item.hpp` and `kStackHoleBytes`
/// becomes zero and this becomes a no-op, rather than needing to be found and
/// deleted.
constexpr std::size_t kStackHoleBytes = offsetof(ItemStack, count) - sizeof(ItemId);

void blankStackPadding(ItemStack& stack) {
    std::memset(reinterpret_cast<char*>(&stack) + sizeof(ItemId), 0, kStackHoleBytes);
}

void blankChestPadding(Chest& chest) {
    for (ItemStack& slot : chest.slots) {
        blankStackPadding(slot);
    }
}

struct Header {
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::uint32_t blockCount = 0;
};

/// Fails on: narrowing `blockCount` to `std::uint16_t`.
static_assert(sizeof(Header) == 28 &&
                  sizeof(Header) == sizeof(Header::magic) + sizeof(Header::version) +
                                        sizeof(Header::seed) + sizeof(Header::x) +
                                        sizeof(Header::y) + sizeof(Header::z) +
                                        sizeof(Header::blockCount),
              "the chunk header must have no padding, or uninitialised bytes go to disk");

/// A save that only becomes the real file once every byte of it is on disk.
///
/// **This exists because the obvious version of it is wrong, and was.** Every
/// writer here used to open a temporary, write into it, ask `if (!file)` while
/// the stream was *still open*, and then rename. An `ofstream` buffers: the
/// last few kilobytes are still in memory at that check and are only handed to
/// the operating system when the destructor runs, on the way out of the block
/// - after the check, and with nowhere to report a failure to. A disk that
/// filled up on that final flush therefore produced a **truncated temporary
/// renamed over a complete save**, silently, and the player lost the file they
/// were trying to protect.
///
/// So the order is: write, `close()` - which flushes and sets `failbit` if the
/// flush failed - check, and only then rename. `rename` replaces atomically on
/// Windows and POSIX alike, so an interrupted save leaves either the old
/// complete file or the new complete file and never a mixture of the two.
///
/// The destructor removes the temporary unless `commit()` succeeded, so the
/// early returns cannot leave a `.tmp` behind for the next run to trip over.
///
/// **What this does not promise** is survival of a power cut: `close()` gets
/// the bytes to the operating system, not onto the platter, and only
/// `FlushFileBuffers` would do that. It costs a platform-specific handle and
/// buys protection against a strictly rarer event than the two this does cover
/// - a full disk and a crashing process - so it is knowingly not done here.
class AtomicSave {
public:
    explicit AtomicSave(std::filesystem::path target)
        : m_target(std::move(target)), m_temporary(m_target.string() + ".tmp"),
          m_file(m_temporary, std::ios::binary | std::ios::trunc) {}

    ~AtomicSave() {
        if (!m_committed) {
            m_file.close();
            std::error_code error;
            std::filesystem::remove(m_temporary, error);
        }
    }

    AtomicSave(const AtomicSave&) = delete;
    AtomicSave& operator=(const AtomicSave&) = delete;

    bool open() const { return static_cast<bool>(m_file); }

    void write(const void* data, std::size_t bytes) {
        m_file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    }

    template <typename T>
    void writeValue(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "only plain data may be written as raw bytes");
        write(&value, sizeof(value));
    }

    /// Flushes, checks, and swaps the temporary in for the real file. Returns
    /// false with the old file untouched if anything at all went wrong.
    bool commit() {
        m_file.close();
        if (!m_file) {
            engine::logError("Failed writing " + m_target.string() +
                             "; the previous save is untouched");
            return false;
        }

        std::error_code error;
        std::filesystem::rename(m_temporary, m_target, error);
        if (error) {
            engine::logError("Could not replace " + m_target.string() + ": " + error.message() +
                             "; the previous save is untouched");
            return false;
        }
        m_committed = true;
        return true;
    }

    const std::filesystem::path& target() const { return m_target; }
    const std::filesystem::path& temporary() const { return m_temporary; }

private:
    std::filesystem::path m_target;
    std::filesystem::path m_temporary;
    std::ofstream m_file;
    bool m_committed = false;
};

/// Deletes a table's file, for a table that is now empty.
///
/// An empty table removes its file rather than leaving a stale one, which would
/// otherwise restore containers the player has already broken. **Reported when
/// it fails**, because a removal that quietly did nothing is the same bug
/// wearing a different hat: chests come back from the dead on the next load.
bool removeTable(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        engine::logError("Could not remove stale save file " + path.string() + ": " +
                         error.message());
        return false;
    }
    return true;
}

/// Whether a path names something that is actually there.
///
/// **The `error_code` overload, never the throwing one.** A loader that threw
/// out of a `std::filesystem` call would abort the load of a world that is
/// merely unreadable for a moment, and the whole point of asking is to be
/// careful. An error answers "not present", which routes to the ordinary
/// absent-file path rather than to a refusal - the direction that cannot make
/// a bad situation worse, since the refusal path is the one that writes.
bool filePresent(const std::filesystem::path& path) {
    std::error_code error;
    const bool present = std::filesystem::exists(path, error);
    return present && !error;
}

/// Whether every float in a record is a real number.
///
/// A NaN on disk is not a transient fault, it is a permanent one: it is read
/// in, used, and written straight back out on the next save, so the world stays
/// broken for every launch after the first. `static_cast<int>` of one is
/// undefined behaviour and yields `INT_MIN` on x86, which then feeds chunk
/// coordinate arithmetic.
bool allFinite(std::initializer_list<float> values) {
    for (const float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

void sanitiseStack(ItemStack& stack) {
    if (stack.item <= ItemId::None || stack.item > ItemId::kLastItem || stack.count <= 0) {
        stack = {};
        return;
    }
    stack.count = std::min(stack.count, maxStackFor(stack.item));
    // Floored, never capped: see the declaration. `damage` carries a stowbox's
    // handle as well as a tool's wear.
    stack.damage = std::max(0, stack.damage);
}

void sanitiseChest(Chest& chest) {
    for (ItemStack& slot : chest.slots) {
        sanitiseStack(slot);
    }
}

bool plausibleStowHandle(std::int32_t handle) {
    return handle > 0 && handle < kMaxStowHandle;
}

bool plausibleBlockPosition(const glm::ivec3& position) {
    // Far past anywhere a player will walk, and small enough that no chunk
    // coordinate derived from it can overflow.
    constexpr int kHorizontalLimit = 1 << 24;
    constexpr int kWorldTop = kWorldHeightChunks * Chunk::kSize;
    return position.y >= 0 && position.y < kWorldTop && position.x > -kHorizontalLimit &&
           position.x < kHorizontalLimit && position.z > -kHorizontalLimit &&
           position.z < kHorizontalLimit;
}

WorldStore::WorldStore(std::filesystem::path root, std::uint32_t seed)
    : m_directory(std::move(root) / ("world_" + std::to_string(seed)) / "chunks"), m_seed(seed) {
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (error) {
        engine::logError("Could not create save directory " + m_directory.string() + ": " + error.message());
    }
}

std::filesystem::path WorldStore::pathFor(const ChunkCoord& coord) const {
    return m_directory / ("c" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + "_" +
                          std::to_string(coord.z) + ".chunk");
}

std::filesystem::path WorldStore::tablePath(Table table) const {
    // Seven enumerators declared, seven names written down. **The assert is not
    // decoration.** A `std::array` with too few initialisers is not an error -
    // it zero-fills - so a table added to the enum and forgotten here would get
    // a null name, and every path built from it would silently be the save
    // directory itself. Too *many* is a hard error the compiler already
    // catches, which is why only the short direction needs saying, and why the
    // last row is the one worth asserting on.
    static constexpr std::array<const char*, static_cast<std::size_t>(Table::Count)> kNames{
        {"furnaces.dat", "chests.dat", "campfires.dat", "stowboxes.dat", "drops.dat",
         "creatures.dat", "player.dat"}};
    static_assert(kNames[static_cast<std::size_t>(Table::Count) - 1] != nullptr,
                  "every Table enumerator needs a file name in kNames; a short initialiser "
                  "zero-fills rather than failing to build, so the last row is the tell");
    return m_directory.parent_path() / kNames[static_cast<std::size_t>(table)];
}

void WorldStore::refuseTable(Table table, const std::filesystem::path& path) const {
    const auto slot = static_cast<std::size_t>(table);
    if (m_refused[slot]) {
        // Already recorded, already copied. `creatures.dat` has two loaders
        // applying the same header test, so without this the player would be
        // told twice about one file.
        return;
    }
    m_refused[slot] = true;

    // Keep the bytes. This file is about to spend a session in front of a build
    // that cannot read it, and both of the things that happen to such a file
    // are destructive: the autosave deletes it thirty seconds later when the
    // live table is empty, or the first container the player places overwrites
    // it with a table holding one entry. A copy costs a few hundred kilobytes,
    // once, and is the only thing between the player and a world's worth of
    // chests.
    //
    // **A copy rather than a rename, and the direction is the whole point.**
    // Renaming would hide the original from the build that *can* read it - run
    // yesterday's exe once and today's would find nothing where the world used
    // to be - so the original stays exactly where it is and the spare takes the
    // new name. Nothing in `game/src` enumerates this directory (checked, not
    // assumed: zero uses of `directory_iterator` anywhere in the game), so an
    // extra file here is inert - every reader opens a fixed name.
    //
    // **`skip_existing`, never `overwrite_existing`, and this is the one line
    // here most worth arguing about.** Alternating builds can walk a world
    // through refuse, overwrite, accept, refuse - and the second refusal would
    // then be copying the *thinner* file over the rich snapshot the first one
    // saved. A thing built to stop silent data loss must not be capable of it,
    // so this can only ever create and never destroy: the first snapshot wins,
    // and the log says plainly when an older one was kept instead.
    std::error_code error;
    const std::filesystem::path keep = path.string() + ".rejected";
    const bool copied = std::filesystem::copy_file(
        path, keep, std::filesystem::copy_options::skip_existing, error);
    if (error) {
        engine::logError("Could not keep a copy of the unreadable " + path.string() + " as " +
                         keep.string() + ": " + error.message() +
                         "; the original is still there and will not be deleted");
        return;
    }
    if (!copied) {
        engine::logWarn("Could not read " + path.string() + ", and " + keep.string() +
                        " already exists from an earlier refusal - keeping the older copy, "
                        "which is the richer one. The original is untouched");
        return;
    }
    engine::logWarn("Kept a copy of the unreadable " + path.string() + " as " + keep.string() +
                    "; the original is untouched and this build will not delete it");
}

void WorldStore::acceptTable(Table table) const {
    m_refused[static_cast<std::size_t>(table)] = false;
}

bool WorldStore::removeTableUnlessRefused(Table table, const std::filesystem::path& path) const {
    if (!m_refused[static_cast<std::size_t>(table)]) {
        return removeTable(path);
    }
    // **The emptiness is this build's ignorance, not an empty world.** Deleting
    // here is how a single launch of a binary with older version constants used
    // to erase every container, every stowbox, every dropped item and the whole
    // creature roster thirty seconds after startup - on the autosave timer, not
    // at quit - while the log line read "saved 0 chests" and called it a
    // success.
    //
    // Returning false rather than true is deliberate: the caller's `wrote`
    // lambda collects the name into `unwritten`, `saveEverything` returns
    // false, and the frame loop's `saveFailing` hands the retry to the autosave
    // timer instead of the next frame. So the player gets one honest error line
    // per interval naming the table, which is exactly the cadence that error
    // machinery was built for.
    engine::logError("Not deleting " + path.string() +
                     ": this build refused to read that file, so an empty table means it was "
                     "never loaded rather than emptied. Nothing was written for it");
    return false;
}

std::optional<Chunk> WorldStore::load(const ChunkCoord& coord) const {
    const std::filesystem::path path = pathFor(coord);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        engine::logWarn("Truncated chunk file, ignoring: " + path.string());
        return std::nullopt;
    }

    // Every field is checked rather than trusted: this is the one place the game
    // reads bytes it did not produce this run.
    //
    // **Strict equality, and it must stay that way.** There is no migration from
    // an older chunk - see `kChunkFormatVersion` - so this is the discard, and
    // the caller regenerates from `(seed, coord)` the moment it gets nothing
    // back. Adding `|| header.version == <previous>` here is precisely the bump
    // that changes nothing.
    if (header.version != kChunkFormatVersion) {
        // Silent, unlike the checks below. An out-of-date chunk is not a fault -
        // it is the version number doing exactly the job it exists for, and a
        // warning here would print once per chunk in the world.
        return std::nullopt;
    }
    if (header.magic != kMagic || header.seed != m_seed || header.x != coord.x ||
        header.y != coord.y || header.z != coord.z ||
        header.blockCount != Chunk::kBlockCount) {
        engine::logWarn("Chunk file does not match this world, ignoring: " + path.string());
        return std::nullopt;
    }

    Chunk chunk;
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(Chunk::kBlockBytes));
    if (file.gcount() != static_cast<std::streamsize>(Chunk::kBlockBytes)) {
        engine::logWarn("Truncated chunk data, ignoring: " + path.string());
        return std::nullopt;
    }

    // Appended after the blocks, and **a short one is damage rather than age.**
    //
    // It was written as a forward-compatible short read - "a file from before
    // waterlogging existed simply runs out here" - and that stopped being true
    // the moment `kChunkFormatVersion`'s check became a strict `!=`. Every file
    // reaching this line is a version 7 file, and a version 7 file has both
    // sections in it; anything shorter is a torn write. `loadCreatures`
    // already states that exact rule about its own tail - only this build's
    // version reaches it, so a file ending at its count is damage, never age -
    // and a rule living in only one of the two places that need it is
    // this project's most expensive bug shape. This was the other place.
    //
    // **Kept rather than refused, and warned about rather than swallowed.**
    // Refusing costs the whole chunk, and a chunk file only exists because the
    // player built something in it - so that is their build thrown away to
    // protect some waterlogging. What is missing reads clear, which is the safe
    // direction in both meanings: no phantom water, and a leaf that decays
    // rather than one that never does. What it must not be is invisible: a
    // chunk that quietly lost its waterlogging looks like an ocean full of
    // holes and reads as a generator fault, which is the confusion
    // `kChunkFormatVersion`'s own comment has already been paid for once.
    file.read(reinterpret_cast<char*>(chunk.waterloggedData()),
              static_cast<std::streamsize>(Chunk::kWaterloggedBytes));
    const std::streamsize waterloggedRead = file.gcount();

    // And the single blockstate bit after that: which leaves a player placed
    // and how far a sapling has got.
    file.read(reinterpret_cast<char*>(chunk.stateBitData()),
              static_cast<std::streamsize>(Chunk::kStateBitBytes));
    const std::streamsize stateBitsRead = file.gcount();

    if (waterloggedRead != static_cast<std::streamsize>(Chunk::kWaterloggedBytes) ||
        stateBitsRead != static_cast<std::streamsize>(Chunk::kStateBitBytes)) {
        const std::streamsize missing =
            static_cast<std::streamsize>(Chunk::kWaterloggedBytes) +
            static_cast<std::streamsize>(Chunk::kStateBitBytes) - waterloggedRead - stateBitsRead;
        engine::logWarn("Chunk file is " + std::to_string(missing) +
                        " bytes short; its blocks are kept, but waterlogging and player-placed "
                        "leaves in it are lost: " +
                        path.string());
    }

    return chunk;
}

bool WorldStore::save(const ChunkCoord& coord, const Chunk& chunk) const {
    AtomicSave out(pathFor(coord));
    if (!out.open()) {
        engine::logError("Could not open chunk file for writing: " + out.temporary().string());
        return false;
    }

    Header header{};
    header.magic = kMagic;
    header.version = kChunkFormatVersion;
    header.seed = m_seed;
    header.x = coord.x;
    header.y = coord.y;
    header.z = coord.z;
    header.blockCount = Chunk::kBlockCount;

    out.writeValue(header);
    out.write(chunk.data(), Chunk::kBlockBytes);
    out.write(chunk.waterloggedData(), Chunk::kWaterloggedBytes);
    out.write(chunk.stateBitData(), Chunk::kStateBitBytes);

    return out.commit();
}

std::optional<SavedPlayer> WorldStore::loadPlayer() const {
    // Sits beside the chunks directory, not inside it, so a chunk sweep never
    // has to filter it out.
    const std::filesystem::path path = tablePath(Table::Player);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // **The worst payload of the whole refusal family, so it is recorded
        // even though `player.dat` is never deleted.** There is no empty case
        // here for an autosave to mistake - `savePlayer` always writes - which
        // is exactly the problem: `Main.cpp:2661` takes `has_value()` and
        // otherwise keeps the freshly constructed spawn character, and the next
        // autosave writes *that* over the real one. Inventory, position,
        // health, food, XP, ender chest, bed, effects and armour, thirty
        // seconds after a file this build could not open.
        if (filePresent(path)) {
            engine::logError("Player file exists but could not be opened: " + path.string());
            refuseTable(Table::Player, path);
        }
        return std::nullopt;
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    SavedPlayer player;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));

    if (!file || magic != kPlayerMagic || seed != m_seed ||
        (version != kFormatVersion && version != kPlayerVersionV7 &&
         version != kPlayerVersionV6 && version != kPlayerVersionV5 &&
         version != kPlayerVersionV4 && version != kPlayerVersionV3 &&
         version != kPlayerVersionV2)) {
        engine::logWarn("Player file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Player, path);
        return std::nullopt;
    }

    if (version == kFormatVersion) {
        file.read(reinterpret_cast<char*>(&player), sizeof(player));
    } else if (version == kPlayerVersionV7) {
        // A world saved before the time of day and the weather joined the
        // record. Every field is carried across by name.
        //
        // The five new fields keep their defaults, which is the honest answer
        // rather than a convenient one: `timeOfDay` stays at `0.18`, the value
        // every launch used to begin at, and the weather stays clear. A version
        // 7 world genuinely had no opinion about either - nothing was writing
        // them down - so this hands back exactly the state that build did, and
        // the first save after loading starts recording them.
        LegacyPlayerV7 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
        player.inventory = legacy.inventory;
        player.selectedSlot = legacy.selectedSlot;
        player.enderChest = legacy.enderChest;
        player.respawnBed = legacy.respawnBed;
        player.effects = legacy.effects;
        player.absorption = legacy.absorption;
        player.absorptionSeconds = legacy.absorptionSeconds;
        player.armour = legacy.armour;
    } else if (version == kPlayerVersionV6) {
        // A world saved before the absorption clock and the worn armour joined
        // the record. Every field is carried across by name.
        //
        // `armour` stays empty, which is the honest answer: nothing was writing
        // it down, so a version 6 file never held a worn set to begin with and
        // the player resumes wearing nothing - exactly what that build handed
        // back. `absorptionSeconds` stays zero for a subtler reason worth
        // stating, because zero is not automatically safe here: it means the
        // next tick sees the Absorption effect's clock as a fresh grant and
        // tops the pool back up. That is the *generous* direction, it can only
        // happen once, and it only happens to a player who had a grant running
        // when they quit - which is why this rung does not try to reconstruct
        // a clock it has no bytes for.
        LegacyPlayerV6 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
        player.inventory = legacy.inventory;
        player.selectedSlot = legacy.selectedSlot;
        player.enderChest = legacy.enderChest;
        player.respawnBed = legacy.respawnBed;
        player.effects = legacy.effects;
        player.absorption = legacy.absorption;
    } else if (version == kPlayerVersionV5) {
        // A world saved before status effects joined the record. Every field is
        // carried across by name; `effects` stays empty and `absorption` stays
        // zero, which is exactly the state such a world was in when it was
        // written - nothing was writing either one down, so the player resumes
        // with no effects running, which is what they got before this existed.
        //
        // Whole stacks again, for the reason spelled out on the version 4 rung:
        // the ids did not move between 5 and 6, so rebuilding the stacks would
        // do nothing but risk dropping `damage` and stranding every stowbox.
        LegacyPlayerV5 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
        player.inventory = legacy.inventory;
        player.selectedSlot = legacy.selectedSlot;
        player.enderChest = legacy.enderChest;
        player.respawnBed = legacy.respawnBed;
    } else if (version == kPlayerVersionV4) {
        // A world saved before the bed joined the record. Every field is
        // carried across by name and `respawnBed` keeps its default - `y` below
        // zero, meaning no bed - which is precisely the state such a world was
        // in when it was written, since nothing was writing one down.
        //
        // **Whole stacks, not `{item, count}` pairs.** The ids did not move
        // between 4 and 5, so there is no renumbering to do here at all, and a
        // migration that rebuilt the stacks anyway would drop `damage` and
        // strand every stowbox in the world behind a handle nothing points at.
        // Copying the array wholesale is what makes that impossible rather than
        // merely unlikely.
        LegacyPlayerV4 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
        player.inventory = legacy.inventory;
        player.selectedSlot = legacy.selectedSlot;
        player.enderChest = legacy.enderChest;
    } else if (version == kPlayerVersionV3) {
        // A world saved before **both** halves of version 4: before the ender
        // chest joined the record, and before the eighteen duplicate item ids
        // were deleted. One migration does both, because one version number
        // covers both.
        //
        // Copied across by name - `count` and `damage` included, because a
        // stowbox's handle rides in `damage` and an upgrade that dropped it
        // would strand every stowbox's contents in `stowboxes.dat` with nothing
        // left pointing at them. `migrateStack` is what guarantees that, and it
        // is asserted rather than believed.
        LegacyPlayerV3 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
        for (std::size_t i = 0; i < kInventorySlots; ++i) {
            player.inventory[i] = migrateStack(legacy.inventory[i], ItemEra::BeforeDuplicateRuns);
        }
        player.selectedSlot = legacy.selectedSlot;
        // The ender chest keeps its default: empty, which is the honest answer
        // for a world that never had one. So does `respawnBed`, for the same
        // reason - a file this old was written by a build with no bed in it.
    } else {
        // An older world still: everything but what it was carrying, which it
        // never stored. The inventory and the selected slot keep their defaults,
        // and with no ids on disk there is nothing to renumber.
        LegacyPlayerV2 legacy;
        file.read(reinterpret_cast<char*>(&legacy), sizeof(legacy));
        player.position = legacy.position;
        player.yaw = legacy.yaw;
        player.pitch = legacy.pitch;
        player.health = legacy.health;
        player.food = legacy.food;
        player.saturation = legacy.saturation;
        player.exhaustion = legacy.exhaustion;
    }

    if (!file) {
        engine::logWarn("Player file is truncated, ignoring: " + path.string());
        return std::nullopt;
    }

    // **Refused rather than repaired.** Where the player is standing is the one
    // field with no safe substitute here - only the caller knows where spawn is
    // - and `nullopt` is exactly how this function already says "start fresh".
    // A NaN position is permanent damage rather than a bad launch: it is read
    // in, floored into a chunk coordinate, and written straight back out on the
    // next save, so the world stays broken forever.
    if (!allFinite({player.position.x, player.position.y, player.position.z, player.yaw,
                    player.pitch, player.saturation, player.exhaustion, player.absorption,
                    player.absorptionSeconds})) {
        engine::logWarn("Player file holds values that are not numbers, ignoring: " + path.string());
        return std::nullopt;
    }

    // Wrapped and clamped rather than refused: an out-of-range angle costs the
    // player nothing but a moment's disorientation, so throwing away a world
    // over one would be the greater harm.
    player.yaw = std::fmod(player.yaw, 360.0f);
    if (player.yaw < 0.0f) {
        player.yaw += 360.0f;
    }
    player.pitch = std::clamp(player.pitch, -90.0f, 90.0f);

    // The trust boundary is here, not at the four places that read what this
    // returns. A container restored without this is a door left open beside
    // three that are locked.
    for (ItemStack& slot : player.inventory) {
        sanitiseStack(slot);
    }
    // Same door for the worn set. A second array reached by a second loop is
    // exactly the shape where a rule gets written once and not twice.
    for (ItemStack& worn : player.armour) {
        sanitiseStack(worn);
    }
    sanitiseChest(player.enderChest);
    // **Forgotten rather than refused, and for a sharper reason than the rest.**
    //
    // A duration that is not a number is not merely odd, it is *permanent*:
    // every expiry test is a comparison, and every comparison against a NaN is
    // false, so such an effect neither counts down nor ever runs out. The
    // player would be left with an effect no clock and no milk bucket can
    // remove, saved back out intact on every quit.
    //
    // The whole world is not worth refusing over one of these - unlike the
    // position above, an effect has a safe substitute, which is not having it -
    // so the slot is cleared and everything else loads.
    //
    // > Fails on: nothing at compile time. This is the reader's half of the
    // > same rule the finiteness check above states for the player's own
    // > floats, written here because `effects` is a second array and a rule
    // > that lives in only one of the two places that need it is this
    // > project's most expensive recurring shape.
    for (SavedEffect& effect : player.effects) {
        if (!allFinite({effect.secondsLeft})) {
            effect = SavedEffect{};
        }
    }
    player.selectedSlot =
        std::clamp<std::int32_t>(player.selectedSlot, 0, static_cast<std::int32_t>(kHotbarSlots) - 1);

    // **The clock and the sky, repaired rather than refused - and this is the
    // finiteness rule travelling to the third place that needs it.**
    //
    // A NaN here is the same permanent damage the position check above refuses
    // over: `timeOfDay` drives the sun angle and the moon phase, it is written
    // straight back out on the next save, and every comparison against it is
    // false - so a day that is not a number is a sky frozen forever. The
    // difference is that it *has* a safe substitute, which is morning, so this
    // repairs where the position refuses. That is the same split the effect
    // loop above makes for the same reason.
    //
    // Wrapped rather than clamped because a day is a cycle: `0.18` and `3.18`
    // name the same moment, and a save that had been running long enough to
    // accumulate whole days should not be dragged back to dawn by the reader.
    if (!allFinite({player.timeOfDay})) {
        player.timeOfDay = 0.18f;
    }
    player.timeOfDay = std::fmod(player.timeOfDay, 1.0f);
    if (player.timeOfDay < 0.0f) {
        player.timeOfDay += 1.0f;
    }

    // **A number off a disk is not a `bool`**, exactly as `SavedCreature::kind`
    // is not an enumerator. These are written as 0 or 1 and anything else is a
    // file this build did not write, so they are normalised to the two states
    // `Weather` can actually be in rather than passed through.
    player.weatherRaining = player.weatherRaining != 0 ? 1 : 0;
    player.weatherThundering = player.weatherThundering != 0 ? 1 : 0;
    // **There is deliberately no "thunder implies rain" normalisation here, and
    // adding one destroys a legitimate state.** This file carried exactly that
    // clause for part of a day, arguing that thunder without rain is a state the
    // live model cannot hold. That was simply false, and it is worth recording
    // how it was settled rather than merely removing it.
    //
    // `Weather::update` runs **two independent countdowns**. Read them at
    // `Weather.cpp` - `m_rainSeconds` decrements, and on reaching zero flips
    // `m_rainOn` and rolls a fresh duration; `m_thunderSeconds` then does the
    // identical thing to `m_thunderOn`. **Neither consults the other.** So
    // thunder running over a dry sky happens every time the thunder timer flips
    // on during the rain timer's off phase, and it is ordinary rather than
    // corrupt. The only place the two are set together is the *forced* path
    // behind the settings override, which is re-read each launch and never
    // reaches this record.
    //
    // **Clearing the flag here does not lose a bit, it inverts the phase**,
    // which is why it is worth more than a line of comment. The thunder timer is
    // restored whatever the flag says, so a cleared flag keeps counting and
    // flips thunder *on* at the moment it should have stayed off - the world
    // comes back in the opposite weather to the one it was saved in, drifting
    // further from the truth the longer the timer had left.
    //
    // The order is what made it invisible: this function runs at load, and
    // `Weather::restore` is called by `Main.cpp` afterwards, so the restore was
    // handed a value that had already been destroyed and could not tell. Both
    // halves read correctly on their own.
    //
    // `storming()` is `raining() && thundering()` and remains the right question
    // for anything wanting the full storm; it is not a claim that the other
    // three combinations are unreachable.
    // A timer that is not a number never counts down, so the weather it belongs
    // to never ends. Zeroed rather than trusted, which is the rule
    // `loadCampfires` already applies to a cook timer - and zero here simply
    // means the next update rolls a fresh duration, which is what a world with
    // no weather written down already does.
    if (!allFinite({player.weatherRainSeconds, player.weatherThunderSeconds}) ||
        player.weatherRainSeconds < 0.0f || player.weatherThunderSeconds < 0.0f) {
        player.weatherRainSeconds = 0.0f;
        player.weatherThunderSeconds = 0.0f;
    }

    // **`health`, `food`, `saturation` and `exhaustion` leave here unbounded on
    // purpose, and that is the one thing in this function a reader should not
    // "fix".** Their bounds are `survival::kMaxHealth`, `kMaxFood`,
    // `clampSaturation` and `kExhaustionPerLevel`, and the caller clamps every
    // one of them against exactly those before the first frame. A second bound
    // written here would be a copy of a table this file does not own, free to
    // drift the day survival is retuned - which is the same defect as deriving
    // a value away from the table that owns it. Only the finiteness check above
    // belongs to this file, because a NaN is not a survival question: it is
    // written straight back out on the next save and breaks the world forever.

    // **Forgotten rather than refused**, and through the same predicate every
    // block entity here is checked with. A cell off a disk that is not a place
    // in this world cannot be a bed, and the fallback - world spawn - is the
    // one thing the caller can always do. Refusing the whole record over it
    // would throw away an inventory to protect a convenience.
    //
    // `plausibleBlockPosition` already rejects `y < 0`, which is the encoding
    // for "no bed", so this is one test and not two: an unset point and a
    // corrupt one leave by the same door and land in the same state.
    if (!plausibleBlockPosition(player.respawnBed)) {
        player.respawnBed = glm::ivec3{0, -1, 0};
    }

    // Read through, every rung of the ladder taken, the record handed back. Any
    // refusal recorded against this file belonged to a previous load.
    acceptTable(Table::Player);
    return player;
}

bool WorldStore::savePlayer(const SavedPlayer& player) const {
    const std::filesystem::path path = tablePath(Table::Player);

    // **The one write in this file that cannot be skipped and cannot be
    // deferred**, so the refusal is announced rather than acted on. There is no
    // empty case here to hold back: the game always has a player, and refusing
    // to write would mean a session that can never be saved at all - worse than
    // the loss it would be preventing, since the bytes it is protecting have
    // already been copied aside by `refuseTable`.
    //
    // So the honest thing is to say what is about to happen and where the old
    // record went. Once: the successful write clears the flag, because after it
    // the file is one this build wrote.
    if (m_refused[static_cast<std::size_t>(Table::Player)]) {
        engine::logError("Overwriting the unreadable " + path.string() +
                         " with this session's player. The old record was kept as " +
                         path.string() + ".rejected - restore it with the build that wrote it");
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open player file for writing: " + out.temporary().string());
        return false;
    }

    // Copied so the two indeterminate bytes inside every stack can be blanked;
    // the caller's record is `const` and is none of this function's business.
    SavedPlayer clean = player;
    for (ItemStack& slot : clean.inventory) {
        blankStackPadding(slot);
    }
    // The worn set goes through the same blanking as everything else being
    // carried. It is a separate array on `Inventory`, so the loop above cannot
    // reach it, and an armour stack has the identical two indeterminate bytes
    // that made this function necessary in the first place.
    for (ItemStack& worn : clean.armour) {
        blankStackPadding(worn);
    }
    blankChestPadding(clean.enderChest);

    out.write(kPlayerMagic.data(), kPlayerMagic.size());
    out.writeValue(kFormatVersion);
    out.writeValue(m_seed);
    out.writeValue(clean);

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Player);
    return true;
}

std::vector<PlacedFurnace> WorldStore::loadFurnaces() const {
    const std::filesystem::path path = tablePath(Table::Furnaces);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // **Absent and unopenable are different answers and used to share a
        // return.** No file is the ordinary case - a world that has never had a
        // furnace - and the empty vector is the truth. A file that is *there*
        // and will not open is a virus scanner, a cloud sync, a backup agent or
        // a permissions change, all of which pass; answering "empty world" to
        // that is what let the next autosave delete it.
        if (filePresent(path)) {
            engine::logWarn("Furnace file exists but could not be opened: " + path.string());
            refuseTable(Table::Furnaces, path);
        }
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    const std::optional<ItemEra> era =
        eraForVersion(version, kFurnaceVersion, kFurnaceDuplicateRunVersion,
                      kFurnaceLegacyItemVersion);
    if (!file || magic != kFurnaceMagic || seed != m_seed || !era.has_value()) {
        engine::logWarn("Furnace file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Furnaces, path);
        return {};
    }
    // A corrupt count is not trusted, but note that this check is not what
    // protects the allocation: `kMaxFurnaces` records is 63 MB. What protects
    // it is `reserveFor` below. This bounds what the file may *claim*.
    if (count > kMaxFurnaces) {
        engine::logWarn("Furnace file claims " + std::to_string(count) + " entries, ignoring: " + path.string());
        refuseTable(Table::Furnaces, path);
        return {};
    }
    acceptTable(Table::Furnaces);

    std::vector<PlacedFurnace> furnaces;
    furnaces.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        PlacedFurnace placed;
        file.read(reinterpret_cast<char*>(&placed), sizeof(placed));
        if (!file) {
            engine::logWarn("Truncated furnace file, keeping what was read: " + path.string());
            break;
        }
        placed.furnace.input = migrateStack(placed.furnace.input, *era);
        placed.furnace.fuel = migrateStack(placed.furnace.fuel, *era);
        placed.furnace.output = migrateStack(placed.furnace.output, *era);
        // After the id migration, never before: an id in an older numbering is
        // out of range until it has been brought across, and sanitising first
        // would empty the world.
        sanitiseStack(placed.furnace.input);
        sanitiseStack(placed.furnace.fuel);
        sanitiseStack(placed.furnace.output);
        if (!allFinite({placed.furnace.burnRemaining, placed.furnace.burnTotal,
                        placed.furnace.cookElapsed})) {
            placed.furnace.burnRemaining = 0.0f;
            placed.furnace.burnTotal = 0.0f;
            placed.furnace.cookElapsed = 0.0f;
        }
        // **The range half of the same rule, which lived in `loadCampfires`
        // alone until 2026-08-19.** That reader states it in full - "a timer out
        // of a file is as suspect as a count out of one", a NaN or a negative or
        // an enormous value all reset to zero - and this one only ever checked
        // the NaN. A rule that exists, is correct, is commented, and is in only
        // one of the two places that need it is this project's most expensive
        // recurring shape; this was the other place.
        //
        // Written as the campfire writes it rather than as a third variant, so
        // the two cannot drift: out of range means back to zero.
        //
        // What it costs when it fires is at most ten seconds of cooking. What it
        // buys is that a `cookElapsed` of 1e30 cannot survive the load - such a
        // furnace stays past its threshold after every `-= kSmeltSeconds`, so it
        // smelts one item per *frame* for the rest of the world's life, which
        // reads as "smelting is instant here" and never resolves itself.
        //
        // **`burnRemaining` is deliberately only floored, not capped against
        // `burnTotal`.** An over-long burn out of a corrupt file is free fuel,
        // which is not a loss, and a cap computed from a second field on the
        // same suspect record could destroy a legitimately burning furnace's
        // fuel to protect against something that costs the player nothing. Only
        // the sign is unambiguous: a negative one draws a negative flame and can
        // never light.
        // **`kSmeltSeconds` is the right ceiling for every cooker, not just the
        // slow one**, and that is worth saying because it looks like a bug.
        // `Smelting.hpp` gives a smoker and a blast furnace
        // `cookSeconds = kSmeltSeconds / cookSpeed`, so their thresholds are
        // *below* this, and a legitimate record from one can never approach it.
        // Using the per-cooker figure would need the block id, which a
        // `PlacedFurnace` does not carry and which this file could not read
        // anyway - `WorldStore` cannot see the world, by design. So the bound is
        // deliberately the loosest of the three, which is the safe direction: it
        // clamps nothing legitimate and still catches the value that matters.
        if (placed.furnace.cookElapsed < 0.0f || placed.furnace.cookElapsed > kSmeltSeconds) {
            placed.furnace.cookElapsed = 0.0f;
        }
        placed.furnace.burnRemaining = std::max(0.0f, placed.furnace.burnRemaining);
        placed.furnace.burnTotal = std::max(0.0f, placed.furnace.burnTotal);
        // A block entity outside the world is attached to nothing: invisible,
        // unbreakable, and written back out on every save for the life of the
        // world. Dropping it is the only way it ever goes away.
        if (!plausibleBlockPosition(placed.position)) {
            engine::logWarn("Furnace saved outside the world, dropping it: " + path.string());
            continue;
        }
        furnaces.push_back(placed);
    }
    return furnaces;
}

bool WorldStore::saveFurnaces(const std::vector<PlacedFurnace>& furnaces) const {
    const std::filesystem::path path = tablePath(Table::Furnaces);

    // Nothing to keep: remove the file rather than leaving a stale one that
    // would restore furnaces the player has already broken - unless the load
    // refused it, in which case "nothing to keep" is a statement about this
    // build rather than about the world.
    if (furnaces.empty()) {
        return removeTableUnlessRefused(Table::Furnaces, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open furnace file for writing: " + out.temporary().string());
        return false;
    }

    const auto count = static_cast<std::uint32_t>(furnaces.size());
    out.write(kFurnaceMagic.data(), kFurnaceMagic.size());
    out.writeValue(kFurnaceVersion);
    out.writeValue(m_seed);
    out.writeValue(count);
    for (const PlacedFurnace& placed : furnaces) {
        PlacedFurnace clean = placed;
        blankStackPadding(clean.furnace.input);
        blankStackPadding(clean.furnace.fuel);
        blankStackPadding(clean.furnace.output);
        out.writeValue(clean);
    }

    if (!out.commit()) {
        return false;
    }
    // **The file on disk is now one this build wrote.** Whatever made the old
    // one unreadable is gone, so a later delete of it is safe again - and
    // without this a refusal recorded at startup would block the legitimate
    // deletion of a table the player really has emptied, leaving a stale file
    // to restore a furnace they broke.
    acceptTable(Table::Furnaces);
    return true;
}

std::vector<PlacedChest> WorldStore::loadChests() const {
    const std::filesystem::path path = tablePath(Table::Chests);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (filePresent(path)) {
            engine::logWarn("Chest file exists but could not be opened: " + path.string());
            refuseTable(Table::Chests, path);
        }
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    const std::optional<ItemEra> era = eraForVersion(
        version, kChestVersion, kChestDuplicateRunVersion, kChestLegacyItemVersion);
    if (!file || magic != kChestMagic || seed != m_seed || !era.has_value()) {
        engine::logWarn("Chest file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Chests, path);
        return {};
    }
    if (count > kMaxChests) {
        engine::logWarn("Chest file claims " + std::to_string(count) + " entries, ignoring: " + path.string());
        refuseTable(Table::Chests, path);
        return {};
    }
    acceptTable(Table::Chests);

    std::vector<PlacedChest> chests;
    chests.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        PlacedChest placed;
        file.read(reinterpret_cast<char*>(&placed), sizeof(placed));
        if (!file) {
            engine::logWarn("Truncated chest file, keeping what was read: " + path.string());
            break;
        }
        migrateChest(placed.chest, *era);
        // After the id migration, for the same reason the furnaces do it in
        // that order.
        sanitiseChest(placed.chest);
        if (!plausibleBlockPosition(placed.position)) {
            engine::logWarn("Chest saved outside the world, dropping it: " + path.string());
            continue;
        }
        chests.push_back(placed);
    }
    return chests;
}

bool WorldStore::saveChests(const std::vector<PlacedChest>& chests) const {
    const std::filesystem::path path = tablePath(Table::Chests);

    // An empty table deletes its file rather than leaving a stale one, which
    // would restore chests the player has already broken - unless the load
    // refused it, in which case the emptiness is not the player's doing.
    if (chests.empty()) {
        return removeTableUnlessRefused(Table::Chests, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open chest file for writing: " + out.temporary().string());
        return false;
    }

    const auto count = static_cast<std::uint32_t>(chests.size());
    out.write(kChestMagic.data(), kChestMagic.size());
    out.writeValue(kChestVersion);
    out.writeValue(m_seed);
    out.writeValue(count);
    for (const PlacedChest& placed : chests) {
        PlacedChest clean = placed;
        blankChestPadding(clean.chest);
        out.writeValue(clean);
    }

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Chests);
    return true;
}

std::vector<PlacedCampfire> WorldStore::loadCampfires() const {
    const std::filesystem::path path = tablePath(Table::Campfires);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (filePresent(path)) {
            engine::logWarn("Campfire file exists but could not be opened: " + path.string());
            refuseTable(Table::Campfires, path);
        }
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    // **No `eraForVersion` here, and its absence is the statement.** This table
    // is younger than every id renumbering the others have to climb, so the only
    // version it can carry is its own; anything else is a file from a build that
    // does not exist.
    if (!file || magic != kCampfireMagic || seed != m_seed || version != kCampfireVersion) {
        engine::logWarn("Campfire file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Campfires, path);
        return {};
    }
    if (count > kMaxCampfires) {
        engine::logWarn("Campfire file claims " + std::to_string(count) +
                        " entries, ignoring: " + path.string());
        refuseTable(Table::Campfires, path);
        return {};
    }
    acceptTable(Table::Campfires);

    std::vector<PlacedCampfire> campfires;
    campfires.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        PlacedCampfire placed;
        file.read(reinterpret_cast<char*>(&placed), sizeof(placed));
        if (!file) {
            engine::logWarn("Truncated campfire file, keeping what was read: " + path.string());
            break;
        }
        for (std::size_t slot = 0; slot < kCampfireSlots; ++slot) {
            sanitiseStack(placed.campfire.items[slot]);
            // **A timer out of a file is as suspect as a count out of one.** A
            // NaN here never reaches `kCampfireCookSeconds`, so the item on it
            // would cook forever and be neither usable nor recoverable except by
            // breaking the block; an enormous one finishes instantly on the next
            // frame. Both start again from zero, which costs at most thirty
            // seconds and cannot round trip.
            const float elapsed = placed.campfire.elapsed[slot];
            if (!allFinite({elapsed}) || elapsed < 0.0f || elapsed > kCampfireCookSeconds) {
                placed.campfire.elapsed[slot] = 0.0f;
            }
            // A timer with nothing under it is a value nothing will ever read
            // and the next item placed there would inherit. Zeroed rather than
            // trusted, which is the same rule `tickCampfire` applies live.
            if (placed.campfire.items[slot].empty()) {
                placed.campfire.elapsed[slot] = 0.0f;
            }
        }
        if (!plausibleBlockPosition(placed.position)) {
            engine::logWarn("Campfire saved outside the world, dropping it: " + path.string());
            continue;
        }
        campfires.push_back(placed);
    }
    return campfires;
}

bool WorldStore::saveCampfires(const std::vector<PlacedCampfire>& campfires) const {
    const std::filesystem::path path = tablePath(Table::Campfires);

    if (campfires.empty()) {
        return removeTableUnlessRefused(Table::Campfires, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open campfire file for writing: " + out.temporary().string());
        return false;
    }

    const auto count = static_cast<std::uint32_t>(campfires.size());
    out.write(kCampfireMagic.data(), kCampfireMagic.size());
    out.writeValue(kCampfireVersion);
    out.writeValue(m_seed);
    out.writeValue(count);
    for (const PlacedCampfire& placed : campfires) {
        PlacedCampfire clean = placed;
        for (ItemStack& slot : clean.campfire.items) {
            blankStackPadding(slot);
        }
        out.writeValue(clean);
    }

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Campfires);
    return true;
}

std::vector<StowedBox> WorldStore::loadStowboxes() const {
    const std::filesystem::path path = tablePath(Table::Stowboxes);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (filePresent(path)) {
            engine::logWarn("Stowbox file exists but could not be opened: " + path.string());
            refuseTable(Table::Stowboxes, path);
        }
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    const std::optional<ItemEra> era = eraForVersion(version, kStowboxVersion,
                                                     kStowboxDuplicateRunVersion, kNoSuchVersion);
    if (!file || magic != kStowboxMagic || seed != m_seed || !era.has_value()) {
        engine::logWarn("Stowbox file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Stowboxes, path);
        return {};
    }
    if (count > kMaxStowboxes) {
        engine::logWarn("Stowbox file claims " + std::to_string(count) +
                        " entries, ignoring: " + path.string());
        refuseTable(Table::Stowboxes, path);
        return {};
    }
    acceptTable(Table::Stowboxes);

    std::vector<StowedBox> boxes;
    boxes.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        StowedBox stowed;
        file.read(reinterpret_cast<char*>(&stowed), sizeof(stowed));
        if (!file) {
            engine::logWarn("Truncated stowbox file, keeping what was read: " + path.string());
            break;
        }
        // **The one bounds check that has to happen here rather than at the
        // caller.** The handle is how a stowbox *item* finds its contents, and
        // the item side carries it in `damage`, which is floored at zero. A
        // negative handle therefore reaches the map and can never be looked up
        // again: the contents are stranded, unreachable forever, and still
        // taking up room in the save file. An enormous one is no better - the
        // caller allocates the next handle as `handle + 1`.
        if (!plausibleStowHandle(stowed.handle)) {
            engine::logWarn("Stowbox handle " + std::to_string(stowed.handle) +
                            " cannot be reached from any item, dropping it: " + path.string());
            continue;
        }
        // **`handle` is deliberately not migrated.** It is an index into this
        // file's own table, not an item id, and it is the same number the
        // stowbox item in the player's bag carries in its `damage` - so
        // renumbering one side of that pair would strand every box.
        migrateChest(stowed.contents, *era);
        sanitiseChest(stowed.contents);
        boxes.push_back(stowed);
    }
    return boxes;
}

bool WorldStore::saveStowboxes(const std::vector<StowedBox>& boxes) const {
    const std::filesystem::path path = tablePath(Table::Stowboxes);

    if (boxes.empty()) {
        return removeTableUnlessRefused(Table::Stowboxes, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open stowbox file for writing: " + out.temporary().string());
        return false;
    }

    const auto count = static_cast<std::uint32_t>(boxes.size());
    out.write(kStowboxMagic.data(), kStowboxMagic.size());
    out.writeValue(kStowboxVersion);
    out.writeValue(m_seed);
    out.writeValue(count);
    for (const StowedBox& stowed : boxes) {
        StowedBox clean = stowed;
        blankChestPadding(clean.contents);
        out.writeValue(clean);
    }

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Stowboxes);
    return true;
}

std::vector<SavedItem> WorldStore::loadDrops() const {
    const std::filesystem::path path = tablePath(Table::Drops);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (filePresent(path)) {
            engine::logWarn("Drop file exists but could not be opened: " + path.string());
            refuseTable(Table::Drops, path);
        }
        return {};
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    std::uint32_t count = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    // No `eraForVersion`, for the reason stated at `kDropVersion`: this table is
    // younger than every id renumbering, so the only version it can carry is its
    // own.
    if (!file || magic != kDropMagic || seed != m_seed || version != kDropVersion) {
        engine::logWarn("Drop file does not match this world, ignoring: " + path.string());
        refuseTable(Table::Drops, path);
        return {};
    }
    if (count > kMaxDrops) {
        engine::logWarn("Drop file claims " + std::to_string(count) +
                        " entries, ignoring: " + path.string());
        refuseTable(Table::Drops, path);
        return {};
    }
    acceptTable(Table::Drops);

    std::vector<SavedItem> drops;
    drops.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        SavedItem saved;
        file.read(reinterpret_cast<char*>(&saved), sizeof(saved));
        if (!file) {
            engine::logWarn("Truncated drop file, keeping what was read: " + path.string());
            break;
        }
        // The same trust boundary every other table here draws: what comes back
        // has been through `sanitiseStack`, so no caller has to.
        sanitiseStack(saved.stack);
        // An empty stack is a drop of nothing - invisible, uncollectable, and
        // ticking forever until it despawns. `sanitiseStack` produces one from
        // any id this build does not know, which is exactly what a file from a
        // future build would be full of.
        if (saved.stack.empty()) {
            continue;
        }
        if (!allFinite({saved.position.x, saved.position.y, saved.position.z, saved.velocity.x,
                        saved.velocity.y, saved.velocity.z, saved.age, saved.pickupDelay})) {
            engine::logWarn("Item saved at a position that is not a number, dropping it: " +
                            path.string());
            continue;
        }
        // **A float too large to floor is undefined behaviour before
        // `plausibleBlockPosition` ever sees it**, so the cast is guarded rather
        // than the rule restated: this bound exists only to make the conversion
        // legal, and the question of whether a position is *in the world* keeps
        // its one owner. Two billion is inside `int`'s range and enormously
        // outside the real limit that owner applies.
        constexpr float kFloorable = 2.0e9f;
        if (std::fabs(saved.position.x) >= kFloorable || std::fabs(saved.position.y) >= kFloorable ||
            std::fabs(saved.position.z) >= kFloorable) {
            engine::logWarn("Item saved outside the world, dropping it: " + path.string());
            continue;
        }
        const glm::ivec3 cell{static_cast<int>(std::floor(saved.position.x)),
                              static_cast<int>(std::floor(saved.position.y)),
                              static_cast<int>(std::floor(saved.position.z))};
        if (!plausibleBlockPosition(cell)) {
            engine::logWarn("Item saved outside the world, dropping it: " + path.string());
            continue;
        }
        // **A negative age is an immortal drop**, because the despawn is a
        // comparison against a rising number - the one direction of corruption
        // this record can produce that never resolves itself. A wildly *large*
        // age is left alone on purpose: it simply despawns on the next update,
        // which is honest.
        saved.age = std::max(0.0f, saved.age);
        // Same for the pickup delay, and for the same asymmetry: a negative one
        // is collectable a frame early, which nobody can perceive, while an
        // enormous one is self-correcting because the drop despawns while it is
        // still counting down. Only the sign needs a rule.
        saved.pickupDelay = std::max(0.0f, saved.pickupDelay);
        saved.onGround = saved.onGround != 0 ? 1 : 0;
        drops.push_back(saved);
    }
    return drops;
}

bool WorldStore::saveDrops(const std::vector<SavedItem>& drops) const {
    const std::filesystem::path path = tablePath(Table::Drops);

    // **An empty floor removes the file rather than writing a header with zero
    // in it**, which is what every table here does - and it matters more for
    // this one than for the others, because an empty floor is the common case.
    //
    // Which is also why the refusal guard matters most here: the common case is
    // indistinguishable from the failure case by looking at the list alone.
    if (drops.empty()) {
        return removeTableUnlessRefused(Table::Drops, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open drop file for writing: " + out.temporary().string());
        return false;
    }

    // **The loader's ceiling, honoured on the writing side - because without
    // this the whole floor is lost rather than the tail of it.** `loadDrops`
    // refuses a count above `kMaxDrops` and returns *nothing at all*, so a world
    // with 70,000 items on the ground would write a perfectly well-formed file
    // that the next load discards whole. That is the failure `saveCreatures`
    // already warns about for its own bound, in the same words: a reader's
    // ceiling that is invisible from the writer is a rule living in one of the
    // two places that need it.
    //
    // **Trimmed here where `saveCreatures` only warns, and the difference is
    // the record rather than a change of mind.** A populated-column marker is
    // one of a set that is only meaningful whole - dropping the tail there is
    // the same silent loss one level earlier, which is what that function says.
    // Drops are independent of each other, so keeping 65,536 of 70,000 is
    // strictly better for the player than keeping none, and the warning is what
    // stops it being silent.
    //
    // `kMaxDrops` is the only ceiling here a real world can reach: it is 65,536
    // against 1,048,576 for the four block-entity tables, and it bounds
    // *transient entities* rather than blocks somebody placed by hand. Nothing
    // caps the live list - `ItemEntities` has no population limit - so a mass
    // break, a settling collapse or a long chain of explosions is the way there.
    //
    // > **Falsified by** `kMaxFurnaces`, `kMaxChests`, `kMaxCampfires` or
    // > `kMaxStowboxes` being lowered to anything a player could reach by
    // > placing blocks, at which point those four writers need this too.
    const std::size_t kept = std::min<std::size_t>(drops.size(), kMaxDrops);
    if (kept < drops.size()) {
        engine::logWarn("Saving " + std::to_string(kept) + " of " +
                        std::to_string(drops.size()) +
                        " dropped items, which is all the loader will accept (" +
                        std::to_string(kMaxDrops) + "); the rest are lost on the next load: " +
                        path.string());
    }

    const auto count = static_cast<std::uint32_t>(kept);
    out.write(kDropMagic.data(), kDropMagic.size());
    out.writeValue(kDropVersion);
    out.writeValue(m_seed);
    out.writeValue(count);
    // Indexed rather than ranged, because the count in the header has to be the
    // number of records that follow it and a ranged loop over `drops` would
    // write more of them than the header claims - which reads back as a table
    // with rubbish appended.
    for (std::size_t i = 0; i < kept; ++i) {
        SavedItem clean = drops[i];
        // `ItemStack` has two bytes of compiler-owned hole between `item` and
        // `count`; blanked on the way out so two identical saves are byte
        // identical. See `blankStackPadding`.
        blankStackPadding(clean.stack);
        out.writeValue(clean);
    }

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Drops);
    return true;
}

namespace {

/// What `openCreatureTable` found - because "no" used to mean two different
/// things and the difference decides whether the file gets deleted.
enum class TableOpen {
    /// There is no file. A world that has never saved a creature, which is not
    /// a fault and whose honest answer is an empty roster.
    Absent,
    /// There is a file and this build will not read it: wrong magic, wrong
    /// version, wrong seed, an impossible count, or it would not open at all.
    /// **This is the answer that must never reach a delete.**
    Refused,
    /// Header accepted, stream sitting on the first byte after it.
    Ready,
};

/// Opens `creatures.dat` and validates its header, leaving the stream sitting on
/// the first creature.
///
/// **One owner for the header, because two readers need it.** `loadCreatures`
/// and `loadPopulatedColumns` both parse magic, version, seed and count, and a
/// rule restated in two places is the shape that has cost this project the most:
/// the day one of them learns about a version the other does not, they disagree
/// about the same bytes and neither says anything.
///
/// The count in the header is the **populated-column** count as of version 6,
/// because that section now comes first. Both readers need it: one to read the
/// columns, the other to step over them.
TableOpen openCreatureTable(const std::filesystem::path& path, std::ifstream& file,
                            std::uint32_t expectedSeed, std::uint32_t& populatedCount) {
    file.open(path, std::ios::binary);
    if (!file) {
        // Absent is ordinary; present-and-unopenable is a refusal. Answering
        // "no creatures" to a locked file is what let the autosave delete the
        // roster **and every populated-column marker with it**, which is the
        // one loss in this file that re-breeds the whole explored world.
        if (filePresent(path)) {
            engine::logWarn("Creature file exists but could not be opened: " + path.string());
            return TableOpen::Refused;
        }
        return TableOpen::Absent;
    }

    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t seed = 0;
    populatedCount = 0;

    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&populatedCount), sizeof(populatedCount));

    // Exact equality, the same discipline the chunk header keeps: there is no
    // creature migration, so every version but this one is a stranger and the
    // population is regenerated. See `kCreatureVersion` for why 4 in particular
    // gets no exemption.
    if (!file || magic != kCreatureMagic || version != kCreatureVersion || seed != expectedSeed) {
        engine::logWarn("Creature file does not match this world, ignoring: " + path.string());
        return TableOpen::Refused;
    }
    if (populatedCount > kMaxPopulatedColumns) {
        engine::logWarn("Creature file claims " + std::to_string(populatedCount) +
                        " populated columns, ignoring: " + path.string());
        return TableOpen::Refused;
    }
    return TableOpen::Ready;
}

} // namespace

std::vector<SavedCreature> WorldStore::loadCreatures() const {
    const std::filesystem::path path = tablePath(Table::Creatures);

    std::ifstream file;
    std::uint32_t populatedCount = 0;
    const TableOpen opened = openCreatureTable(path, file, m_seed, populatedCount);
    if (opened == TableOpen::Refused) {
        refuseTable(Table::Creatures, path);
    }
    if (opened != TableOpen::Ready) {
        return {};
    }
    acceptTable(Table::Creatures);

    // Straight past the markers. Seeking rather than reading them a second
    // time: `PopulatedColumn` is fixed-width and the assert beside it says so,
    // which is the property that makes this legal at all.
    //
    // This is the seek that used to sit in `loadPopulatedColumns`, and it is
    // here now because a blind seek can only be safe over the section the game
    // can afford to lose. If a tear lands inside the markers this comes up
    // short and one herd is regenerated, which the spawner replaces. The other
    // way round it re-bred every column the player had ever visited.
    file.seekg(static_cast<std::streamoff>(populatedCount) *
                   static_cast<std::streamoff>(sizeof(PopulatedColumn)),
               std::ios::cur);

    std::uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file) {
        // Only this build's version reaches here, and it always writes the
        // count - so the file ending at this point is damage, never age.
        engine::logWarn("Creature file ends before its creatures: " + path.string());
        refuseTable(Table::Creatures, path);
        return {};
    }
    if (count > kMaxCreatures) {
        engine::logWarn("Creature file claims " + std::to_string(count) +
                        " entries, ignoring: " + path.string());
        refuseTable(Table::Creatures, path);
        return {};
    }

    std::vector<SavedCreature> creatures;
    creatures.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        SavedCreature saved;
        file.read(reinterpret_cast<char*>(&saved), sizeof(saved));
        if (!file) {
            engine::logWarn("Truncated creature file, keeping what was read: " + path.string());
            break;
        }
        // A creature at a coordinate that is not a number is worse than a
        // missing creature: it is floored into a chunk index every tick.
        // Dropping it costs one animal, which the spawner replaces.
        if (!allFinite({saved.x, saved.y, saved.z, saved.yaw, saved.scale})) {
            engine::logWarn("Creature saved at a position that is not a number, dropping it: " +
                            path.string());
            continue;
        }
        creatures.push_back(saved);
    }
    return creatures;
}

std::vector<PopulatedColumn> WorldStore::loadPopulatedColumns() const {
    const std::filesystem::path path = tablePath(Table::Creatures);

    std::ifstream file;
    std::uint32_t count = 0;
    const TableOpen opened = openCreatureTable(path, file, m_seed, count);
    if (opened == TableOpen::Refused) {
        // **The refusal that costs the most in this file.** An empty marker set
        // is indistinguishable from a world nobody has explored, so the next
        // save writes a roster with zero markers and every column the player
        // has ever visited breeds a fresh herd - the exact loss the markers
        // were added to prevent, applied to the whole world at once.
        refuseTable(Table::Creatures, path);
    }
    if (opened != TableOpen::Ready) {
        return {};
    }
    // **No `acceptTable` here, and the omission is the point.** This function
    // validates the header and stops; `loadCreatures` reads the file through to
    // its end, so it is the only one of the two entitled to say the file is
    // sound. Clearing the flag here would let a header-good, tail-damaged file
    // cancel the refusal `loadCreatures` had just recorded, and which of the
    // two happened to run first would decide whether the file survived.

    // **No seek, and that absence is the whole point of version 6.** The marker
    // count is the header's own field and the markers start at the very next
    // byte, so nothing this function needs can be put out of reach by damage
    // further down the file. The ceiling is checked in `openCreatureTable`
    // alongside every other header field, which is the one place that owns it.

    std::vector<PopulatedColumn> populated;
    populated.reserve(reserveFor(count));
    for (std::uint32_t i = 0; i < count; ++i) {
        PopulatedColumn column;
        file.read(reinterpret_cast<char*>(&column), sizeof(column));
        if (!file) {
            engine::logWarn("Truncated populated columns, keeping what was read: " + path.string());
            break;
        }
        // No bounds check, and the omission is deliberate: every `std::int32_t`
        // pair is a column somewhere, there is no invalid one, and the only harm
        // a wrong pair can do is suppress one chunk's herd. Inventing a world
        // limit here would be a second owner of a bound `World` already has.
        populated.push_back(column);
    }
    return populated;
}

bool WorldStore::saveCreatures(const std::vector<SavedCreature>& creatures,
                               const std::vector<PopulatedColumn>& populated) const {
    const std::filesystem::path path = tablePath(Table::Creatures);

    // An empty population removes the file rather than leaving a stale one, the
    // same reasoning the furnaces use: a leftover would repopulate a world the
    // player has already cleared.
    //
    // **Both lists have to be empty, and testing only the creatures was a real
    // bug for the minute this held two tables and one condition.** A player who
    // has cleared every animal out of a hundred explored columns has an empty
    // creature list and a hundred markers; deleting the file there throws the
    // markers away and every one of those columns breeds a fresh herd on the
    // next load - which is precisely what this record was added to stop.
    //
    // **And a third way into that same loss, which no test of the two lists can
    // see: both are empty because the load REFUSED the file.** One launch of a
    // binary whose `kCreatureVersion` differs empties both, and thirty seconds
    // later the autosave deleted the roster and every marker with it. That is
    // the same damage as the two-list bug above, arriving from the other side.
    if (creatures.empty() && populated.empty()) {
        return removeTableUnlessRefused(Table::Creatures, path);
    }

    AtomicSave out(path);
    if (!out.open()) {
        engine::logError("Could not open creature file for writing: " + out.temporary().string());
        return false;
    }

    // **Markers first, creatures last, and the order is load-bearing rather
    // than cosmetic.** A torn write takes the tail of the file, so whichever
    // section is written last is the one damage reaches first. Creatures are
    // the section the game can afford to lose - the spawner makes more - and
    // the markers are the one it cannot, because losing them tells the next
    // load that every column the player has explored has never been populated
    // and the whole explored world breeds again. See `kCreatureVersion`.
    //
    // `loadPopulatedColumns` reads its count out of the header and its columns
    // from the very next byte, with no seek at all; `loadCreatures` steps over
    // the markers to reach its own count. Both are the same order as this.
    //
    // **`loadPopulatedColumns` refuses a count above `kMaxPopulatedColumns` and
    // throws away every column when it does - and until this warning existed,
    // nothing on the writing side knew that bound at all.** That is the rule
    // living in one of the two places that need it: the reader's ceiling was
    // invisible from here, so a world that grew past it would write a
    // well-formed file that the next load discarded whole, re-populating every
    // explored column - the exact failure the comment at the top of this
    // function says this record exists to stop, applied to all of them at once
    // instead of one.
    //
    // It is not clamped, deliberately. Dropping the tail here would be the same
    // silent loss one level earlier, and 2^22 columns is not a number ordinary
    // play reaches - so this says so out loud rather than pretending to fix it.
    if (populated.size() > kMaxPopulatedColumns) {
        engine::logWarn("Saving " + std::to_string(populated.size()) +
                        " populated columns, which is more than the loader accepts (" +
                        std::to_string(kMaxPopulatedColumns) +
                        "); every one of them will be discarded on the next load and the "
                        "world will repopulate: " +
                        path.string());
    }

    out.write(kCreatureMagic.data(), kCreatureMagic.size());
    out.writeValue(kCreatureVersion);
    out.writeValue(m_seed);
    out.writeValue(static_cast<std::uint32_t>(populated.size()));
    for (const PopulatedColumn& column : populated) {
        out.writeValue(column);
    }

    // The count is written even when it is zero, so the section is always
    // present in a v6 file and its absence always means truncation.
    const auto count = static_cast<std::uint32_t>(creatures.size());
    out.writeValue(count);
    // No blanking pass: `SavedCreature` holds no `ItemStack`, and the assert
    // beside it proves it has no hole of its own.
    for (const SavedCreature& saved : creatures) {
        out.writeValue(saved);
    }

    if (!out.commit()) {
        return false;
    }
    acceptTable(Table::Creatures);
    return true;
}

} // namespace game
