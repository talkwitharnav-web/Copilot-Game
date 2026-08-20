#pragma once

#include "world/Block.hpp"
#include "world/Creature.hpp"
#include "world/Material.hpp"

#include <engine/audio/AudioEngine.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace game {

/// Everything the game knows how to make a noise about.
///
/// Names an *event*, not a file - which is the whole reason this enum exists
/// rather than paths scattered through the loop. Most events have several
/// recordings and one is picked at random, so a row of blocks broken in a line
/// does not read as a machine.
///
/// **`kStems[i]` in the .cpp must be the stem for `SoundEvent(i)`**, and that
/// is the whole invariant: inserting an enumerator here without inserting its
/// stem at the same index shifts every event after it onto the wrong
/// recordings. `Sounds.cpp` anchors that with three `static_assert`s rather
/// than trusting it. The staging script's *order* does not matter at all - the
/// loader addresses files by stem name - but a stem this table names and the
/// script never writes is an event that silently never sounds, which is what
/// `sweep()` is for.
///
/// **Adding an enumerator takes three edits and this header is only the
/// first.** Written down here because the audit partition hands this file and
/// `Sounds.cpp` to different owners often enough that it has already blocked
/// one piece of work (the armour equip sound), and an owner of the header alone
/// has no way to see the other two from here:
///
///   1. the enumerator, here, before `Count`;
///   2. its stem in `kStems` at the same index, in `Sounds.cpp`, **plus an arm
///      in the `eventUse` switch beside it** - that switch is what tells
///      `sweep()` whether a silent event is a fault or deliberate. It has no
///      `default:` and names all 61 tokens, falling through to `Unclassified`,
///      which is the right shape - but do not expect the compiler to enforce
///      it. `-Wswitch` would; MSVC's equivalent C4062 is off by default and
///      this build sets only `/W4` (CMakeLists.txt:90, no `/w14062`, no
///      `/WX`), so on this toolchain a missing arm is caught by `sweep()` at
///      startup and by nothing earlier;
///   3. a row in `tools/make-reference-sounds.ps1`, which is where the audio
///      comes from - this project never authors sound.
///
/// Only the first two are enforced. Since 2026-08-19 a fourth `static_assert`,
/// `everyStemIsFilled()`, makes step 2's *stem* half a compile error rather
/// than a crash: the three anchors catch insertion and reordering but cannot
/// catch an enumerator **appended after the last one**, because nothing shifts
/// below it - it just leaves `kStems` a row short, and a short `std::array`
/// value-initialises the tail to `nullptr`, which `load` builds a
/// `std::string` from. Step 2's *switch* half and step 3 are still caught only
/// at run time, by `sweep()`, which is why `sweep()` runs at startup.
enum class SoundEvent : std::uint8_t {
    DigStone,
    DigWood,
    DigGrass,
    DigGravel,
    DigSand,
    DigCloth,
    DigSnow,
    DigGlass,
    DigCoral,
    DigWet,

    StepStone,
    StepWood,
    StepGrass,
    StepGravel,
    StepSand,
    StepCloth,
    StepSnow,
    StepLadder,
    StepCoral,
    StepWet,

    Hurt,
    FallBig,
    FallSmall,
    Breath,

    Bow,
    BowHit,
    HitLand,
    Explode,
    Fuse,

    Eat,
    Burp,
    Pop,
    Orb,
    Click,
    WoodClick,
    ItemBreak,
    Fizz,
    Splash,
    SplashBig,
    Swim,
    Drink,
    LevelUp,
    ChestOpen,
    ChestClose,
    DoorOpen,
    DoorClose,

    BucketFill,
    BucketEmpty,
    BucketFillLava,
    BucketEmptyLava,

    Fire,
    Ignite,
    Lava,
    LavaPop,
    Water,

    Cave,
    Music,

    Rain,
    Thunder,

    Bell,

    Count,
};

/// Which voice a species speaks with.
///
/// **A family rather than a species**, for the same reason blocks are grouped
/// by material: fifty-seven creatures share far fewer voices, and a new one
/// inherits an existing family by naming it rather than by needing recordings
/// of its own. The staging script's `$voices` table is the other half of this
/// and the two must name the same families in the same order.
enum class CreatureVoice : std::uint8_t {
    Sheep,
    Cow,
    Pig,
    Chicken,
    Horse,
    Llama,
    Cat,
    Wolf,
    Fox,
    Panda,
    Bear,
    Rabbit,
    Goat,
    Bee,
    Turtle,
    Dolphin,
    Squid,
    Villager,
    Trader,
    Zombie,
    Husk,
    Drowned,
    ZombieVillager,
    Skeleton,
    Stray,
    Bogged,
    Blackbone,
    Spider,
    Creeper,
    Slime,
    Magma,
    Silverfish,
    Princepin,
    Fish,
    Golem,
    Count,
    /// Says nothing at all. The frog and the axolotl have voices in the
    /// reference that nothing here has been mapped to yet, and silence is a
    /// better answer than the wrong animal.
    None = Count,
};

/// What a creature is doing when it makes a noise.
enum class VoiceState : std::uint8_t {
    Idle,
    Hurt,
    Death,
    Count,
};

CreatureVoice voiceFamilyFor(CreatureKind kind);

/// What a block is made of, as far as the ears are concerned.
///
/// **Grouped by material rather than by block**, which is the reference's own
/// arrangement and the only one that scales: there are eleven hundred blocks
/// and ten sounds, so a new stone variant inherits stone without a line being
/// added anywhere.
enum class SoundMaterial : std::uint8_t {
    Stone,
    Wood,
    Grass,
    Gravel,
    Sand,
    Cloth,
    Snow,
    Glass,
    /// A ladder is wood to break but has its own five-recording step bank, which
    /// is why it needs a material of its own rather than answering `Wood`.
    Ladder,
    Coral,
    Wet,
    /// Makes no noise at all - air, water, and anything else with no surface.
    None,
};

/// Every block the reference gives the **wood** sound group, asked as families.
///
/// **Not `isFlammable`, and never again.** That is what the bottom of
/// `soundMaterialFor` used to lean on, and fuel and timbre are two different
/// questions: a barrel, a jukebox, a note block and a mushroom block all break
/// and step as wood in the reference while their infoboxes read
/// `flammable = No`, and a block of coal, TNT, a hay bale and dried kelp all
/// burn while sounding like rock or grass. So widening `isFlammable` to buy a
/// sound would trade a sound bug for a block-destruction bug - which is live
/// today, because `Block.hpp` is narrowing that predicate right now and every
/// answer keyed off it would have moved underneath this table.
///
/// **It lives in the header rather than an anonymous namespace in the .cpp so
/// that `soundMaterialFor` can be `constexpr`** - a `constexpr` function has to
/// be visible in every translation unit that constant-evaluates it, which is
/// the same reason `digSoundFor` is here, and the sweep at the bottom of this
/// file is what it buys.
///
/// Source for the whole list: <https://minecraft.wiki/w/Block_sound_type>,
/// `wood` row, **Bedrock** column.
constexpr bool soundsLikeWood(BlockId block) {
    // A wooden door or trapdoor is wood; the iron pair is the reference's
    // `iron` group, which has no bank here, so those two keep the rock default.
    // Asked as the family's own `metal` flag rather than by naming the iron ids,
    // because `kDoorFamilies` is the one table that owns which material a door
    // is made of.
    if (isDoor(block)) {
        return !kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal;
    }
    if (isTrapdoor(block)) {
        return !kTrapdoorFamilies[static_cast<std::size_t>(trapdoorFamily(block))].metal;
    }
    // Timber, and the four families that are all the same tree: logs, bark
    // blocks, their stripped forms and planks. **`isPlanksBlock`, not
    // `BlockId::Planks`** - that named oak alone and the other eleven woods were
    // rescued only by happening to burn, which is exactly the coupling this
    // function exists to cut.
    if (isLogBlock(block) || isBarkBlock(block) || isStrippedBarkBlock(block) ||
        isPlanksBlock(block)) {
        return true;
    }
    // The workbenches and containers. `isChest` covers the barrel and every loot
    // chest, and the two that are **not** wood in the reference are named out:
    // an ender chest and a stowbox are obsidian and stone.
    if (isChest(block)) {
        return !isEnderChest(block) && !isStowbox(block);
    }
    // The workbenches and containers, plus **the hives, which this rewrite very
    // nearly lost.** Cutting the `isFlammable` tail dropped every block that was
    // reaching the wood bank by burning rather than by being named, and the
    // beehive and bee nest were the eight ids nobody re-listed: measured after
    // the cut, `isFlammable(block) && soundMaterialFor(block) == Stone` was
    // exactly nine ids - a block of coal, which is correctly rock, and these
    // eight, which were silently demoted.
    //
    // **The hives are a third documented divergence, and the note that used to
    // sit here was wrong on its facts.** It cited `Beehive/Sounds` and said
    // "wood in the reference" flat; the `wood` row marks Beehive and Bee Nest
    // `{{Only|JE}}`, and Bedrock files both under `normal` instead. But
    // `normal` is not a Bedrock *decision* - the same page footnotes it as
    // "the default category in BE", the bucket for blocks Mojang never
    // classified, exactly what `return SoundMaterial::Stone` is at the bottom
    // of this file. Following an absent assignment is not following Bedrock, a
    // hive is planks and comb hanging in a tree, and this is the same class of
    // call already made knowingly for coral and for `wet_grass` below. Java's
    // answer, kept on purpose and now written down.
    // <https://minecraft.wiki/w/Block_sound_type>, `wood` and `normal` rows.
    //
    // **A chiseled bookshelf is a bookshelf, and only the plain one was named.**
    // It is six planks and three books, it sits beside the ordinary bookshelf in
    // every build, and it was breaking and being walked on as cobblestone while
    // the shelf next to it sounded like planks. It has a `chiseled_bookshelf`
    // group of its own in *both* editions - Bedrock added it in 1.19.60 - so it
    // is not in `stone` or `normal` under either reading, and with no bank of
    // its own staged the wood row it was cut from is the near neighbour. This is
    // the "rule that did not travel" shape: `Bookshelf` was listed here, and the
    // second bookshelf added five hundred ids later was not.
    // <https://minecraft.wiki/w/Block_sound_type>, `chiseled_bookshelf` row.
    if (block == BlockId::Bookshelf || block == BlockId::ChiseledBookshelf ||
        block == BlockId::CraftingTable || block == BlockId::Loom ||
        block == BlockId::Lectern || block == BlockId::CartographyTable ||
        block == BlockId::FletchingTable || block == BlockId::SmithingTable ||
        isComposter(block) || isBeehive(block) || block == BlockId::Jukebox ||
        isNoteBlock(block) || isDaylightDetector(block) ||
        block == BlockId::Campfire || block == BlockId::SoulCampfire) {
        return true;
    }
    // **Scaffolding, which is the one this whole helper nearly lost.** It has no
    // wood in it at all, so no family test above catches it, and it was reaching
    // the wood bank purely by being fuel - which is exactly the dependency this
    // rewrite removes. The reference gives it a bamboo group of its own
    // (`scaffolding` in one edition, the bamboo banks in the other), and with no
    // bamboo recordings staged, wood is the near neighbour and rock is not.
    // <https://minecraft.wiki/w/Block_sound_type>, `scaffolding` / `bamboo_wood`.
    if (block == BlockId::Scaffolding) {
        return true;
    }
    // Furniture cut from wool and timber. A bed and a banner are both wood in
    // the reference despite being made of six wool - which only the *sound*
    // table disagrees about, so both have to be named here, above the
    // `shapedParent` forwarding that would otherwise hand a banner its wool's
    // answer. Signs are named for the opposite reason: they reach wood through
    // their planks today and would flip to rock the moment planks stop burning.
    if (isBed(block) || isSignLike(block)) {
        return true;
    }
    // The gourds and the pods. Every one of these is `flammable = No` in the
    // reference and wood to the ear anyway.
    if (block == BlockId::Pumpkin || block == BlockId::Melon || isCarvedPumpkin(block) ||
        isJackOLantern(block) || isCocoa(block) || isStemBlock(block)) {
        return true;
    }
    // A huge mushroom is wood, cap and stem alike - the reference's own
    // {{Sound table/Block/Wood}}, filed as MCPE-19717 and marked working as
    // intended.
    if (block == BlockId::BrownMushroomBlock || block == BlockId::RedMushroomBlock ||
        block == BlockId::MushroomStem) {
        return true;
    }
    // Sticks on a wall: every torch including the redstone one, the end rod and
    // the tripwire hook.
    return isTorchBlock(block) || block == BlockId::EndRod || isTripwireHook(block);
}

/// Which of those ten a block is - the table itself, mapping all 3,269 ids.
///
/// **`constexpr`, and defined here rather than in the .cpp so it can be swept.**
/// It was the last per-block table in the game with no compile-time guard at
/// all: `blockName` has `everyBlockNamed`, `blockTextureLayer` has
/// `everyBlockTextured`, mining has `MiningSweep`, drops have `DropSweep` and
/// blast resistance has `BlastSweep`. That last one is the cautionary tale
/// rather than the precedent - it was a plain runtime float in a .cpp for
/// exactly the reason this was, and a hundred and twenty blocks sat on the
/// wrong number through three clean builds and a soak. Nothing in here needed
/// the runtime: every test is a `constexpr` predicate over `Block.hpp`'s family
/// tables plus `shapedParent`, so the move costs one header and buys
/// `sound::everySoundPassSwept` at the bottom of this file.
constexpr SoundMaterial soundMaterialFor(BlockId block) {
    // Asked as families rather than as a list of ids, so eleven hundred blocks
    // are covered by a dozen tests and a new one inherits the right sound the
    // day it is added.
    //
    // **This is a run of ordered tests and a new one near the top shadows every
    // row below it.** Several families here sit deliberately *above* a broader
    // shape question - `isCrossBlock` in particular, which was widened for
    // geometry and answers `true` for a conduit, a cobweb, a candle and an
    // amethyst bud. Anything inserted must say which broader test it is jumping.
    if (block == BlockId::Air || isFluid(block)) {
        return SoundMaterial::None;
    }
    if (isLeafBlock(block)) {
        return SoundMaterial::Grass;
    }
    if (soundsLikeWood(block)) {
        return SoundMaterial::Wood;
    }
    // **`isGlassBlock`, not the one id.** Tinted glass and the sixteen stained
    // ones are glass by every question anyone asks of them; naming only the
    // clear block dropped all seventeen through to the catch-all, while the
    // seventeen *panes* beside them tinkled correctly - which is what hid it.
    // The same fix was made in `Material.hpp`'s `materialFamilyFor` and did not
    // travel here.
    //
    // The rest of the reference's `glass` row that we have ids for: the three
    // ices, the redstone lamp lit and unlit, the sea lantern, the end portal
    // frame and (Bedrock only) the beacon.
    // <https://minecraft.wiki/w/Block_sound_type>, `glass` row.
    if (isGlassBlock(block) || isPane(block) || isIce(block) ||
        block == BlockId::Glowstone || block == BlockId::RedstoneLamp ||
        block == BlockId::RedstoneLampLit || block == BlockId::SeaLantern ||
        block == BlockId::EndPortalFrame || block == BlockId::Beacon) {
        return SoundMaterial::Glass;
    }
    // Coral, and **live only**: dead coral - block, fan and plant - is stone.
    // Asked before the cross-shape test below, which owns coral plants and fans
    // and was answering `Grass` for them, and before the fallthrough, which was
    // answering `Stone` for the blocks.
    //
    // **A documented divergence from Bedrock, on purpose.** Bedrock has no live
    // coral group at all: MCPE-76182 puts every coral, coral block and coral fan
    // in `stone`. That is an open bug rather than a design, following it would
    // silence ten staged recordings that nothing else can name, and a reef that
    // clunks like cobblestone is worse for the player. So this is Java's split,
    // kept knowingly. <https://minecraft.wiki/w/Block_sound_type>,
    // `coral_block` and `stone` rows.
    //
    // **The honeycomb block is not a divergence - it is the one member the
    // group actually has in Bedrock, and it was answering rock.** The previous
    // note here said so and then did nothing about it, which is the whole of
    // the bug: `coral_block`(JE)/`coral`(BE) lists the honeycomb block
    // unmarked, so it is in this group under *both* editions, and the bank it
    // needs is already staged and loaded. Named rather than folded into
    // `isCoralBlock`, because that predicate is `Block.hpp`'s and owns the reef
    // geometry - a comb is not coral to anything except the sound table.
    if (block == BlockId::HoneycombBlock) {
        return SoundMaterial::Coral;
    }
    if (isCoralBlock(block) || isCoralPlant(block) || isCoralFan(block)) {
        return isDeadCoral(block) ? SoundMaterial::Stone : SoundMaterial::Coral;
    }
    // **Sandstone is not in this group and concrete powder is.** The reference's
    // `sand` row is sand, red sand and the sixteen concrete powders; sandstone
    // is rock that used to be sand and sits in the `stone` row with every cut,
    // smooth and chiseled form - which is why red sandstone was already right
    // and the two halves of one family disagreed. Naming sandstone here also
    // handed the wrong answer to its eleven stairs, slabs and walls through
    // `shapedParent`. Soul sand and soul soil have groups of their own with no
    // bank here, and loose grit is the nearest honest answer for both.
    // <https://minecraft.wiki/w/Block_sound_type>, `sand` and `stone` rows.
    if (block == BlockId::Sand || isConcretePowder(block) ||
        block == BlockId::SuspiciousSand || block == BlockId::SoulSand ||
        block == BlockId::SoulSoil) {
        return SoundMaterial::Sand;
    }
    // **The whole dirt family, not two of it.** The reference's `gravel` row is
    // exactly clay, coarse dirt, dirt, farmland, gravel and podzol - so plain
    // dirt was actively wrong at `Grass` rather than merely missing, and
    // farmland, which is under every farm a player builds, was rock. Rooted
    // dirt, mud and muddy mangrove roots each have a group of their own that no
    // bank here can play, and loose wet ground is the nearest of the ten.
    // <https://minecraft.wiki/w/Block_sound_type>, `gravel` row.
    if (block == BlockId::Gravel || block == BlockId::Clay || block == BlockId::Dirt ||
        block == BlockId::CoarseDirt || block == BlockId::Podzol || isFarmland(block) ||
        block == BlockId::RootedDirt || block == BlockId::Mud ||
        block == BlockId::MuddyMangroveRoots || block == BlockId::SuspiciousGravel) {
        return SoundMaterial::Gravel;
    }
    // **The layers, both cubes and the powder.** Settled snow is a different
    // block from a snow block, it is what a snowy biome is actually surfaced
    // with, and asking only for the cube left every layer sounding like rock.
    // `BlockId::SnowBlock` is a second id for the same solid cube - the duplicate
    // is `Block.hpp`'s to collapse and `Mining.hpp`'s `isSolidSnow` is the
    // predicate that owns the pair, so this must be changed with it. Powder snow
    // has its own group in the reference and the snow bank is the nearest.
    if (block == BlockId::Snow || block == BlockId::SnowBlock || isSnowLayer(block) ||
        block == BlockId::PowderSnow) {
        return SoundMaterial::Snow;
    }
    // Anything that lives in water squelches rather than crunches.
    //
    // **A second documented divergence.** `wet_grass` is Java's group; Bedrock
    // has no such event and puts kelp, seagrass and the lily pad in `grass`, and
    // gives the sea pickle the `slime` group, which we have no bank for either.
    // Keeping the four here is a deliberate choice for the same reason as coral:
    // the recordings are staged and loaded, a seabed that rustles like a meadow
    // is worse, and dropping them would orphan two banks.
    // <https://minecraft.wiki/w/Block_sound_type>, `wet_grass`, `grass` and
    // `slime` rows.
    if (block == BlockId::Kelp || block == BlockId::Seagrass || block == BlockId::LilyPad ||
        block == BlockId::SeaPickle) {
        return SoundMaterial::Wet;
    }
    // **Wool, carpet, and the three the reference puts in the same row.** Wool
    // is flammable, so it was falling through to the old wood test and the
    // sixteen dyed blocks broke and were walked on as planks. A cactus is
    // literally in the reference's `wool` row; a cobweb's own group plays the
    // wool samples; sponge and wet sponge have groups of their own and a soft
    // porous thud is nearer cloth than rock. The cobweb is also the first of
    // the four tests that must sit above `isCrossBlock`, which claims it.
    // <https://minecraft.wiki/w/Block_sound_type>, `wool`, `cobweb`, `sponge`
    // and `wet_sponge` rows.
    if (isWoolBlock(block) || isCarpet(block) || block == BlockId::Cactus ||
        block == BlockId::Cobweb || block == BlockId::Sponge ||
        block == BlockId::WetSponge) {
        return SoundMaterial::Cloth;
    }
    // **Cross-shaped and not a plant.** `isCrossBlock` is the geometry owner and
    // was widened to cover these for their shape; reading it as "this rustles"
    // gave a conduit, a stone spike, seventeen candles and five amethyst
    // crystals a leafy rustle. Each has a group of its own in the reference with
    // no bank here, and the chorus pair is plain `stone` in Bedrock, so rock is
    // both the right answer and this table's documented default for an unknown.
    // Glass was the other candidate for the amethysts; stone keeps them with the
    // amethyst block they are cut from.
    // <https://minecraft.wiki/w/Block_sound_type>, `stone` row.
    if (block == BlockId::Conduit || block == BlockId::PointedDripstone ||
        block == BlockId::ChorusPlant || block == BlockId::ChorusFlower ||
        isCandle(block) || block == BlockId::AmethystCluster ||
        (block >= BlockId::SmallAmethystBud && block <= BlockId::LargeAmethystBud)) {
        return SoundMaterial::Stone;
    }
    // The reference's `grass` row, which is far wider than "a plant": the grass
    // block, the dirt path, mycelium, dried kelp, a hay bale, a target block and
    // TNT are all in it, alongside every cross-shaped growing thing. **TNT is in
    // it twice over** - the placed charge and the primed one were answering
    // different materials because only one of them burned, so lighting a charge
    // changed what it was made of to the ears.
    //
    // Nearest-available, each with a group of its own and no bank: a vine (which
    // was answering `Wood` purely because vines burn), moss, moss carpet, sculk
    // vein, azalea, flowering azalea and the two wart blocks.
    // <https://minecraft.wiki/w/Block_sound_type>, `grass` row.
    if (block == BlockId::Grass || isCrossBlock(block) || isVine(block) ||
        block == BlockId::DirtPath || block == BlockId::Mycelium ||
        block == BlockId::DriedKelpBlock || block == BlockId::HayBlock || isTarget(block) ||
        block == BlockId::Tnt || block == BlockId::TntPrimed ||
        block == BlockId::MossBlock || block == BlockId::MossCarpet ||
        block == BlockId::SculkVein || block == BlockId::Azalea ||
        block == BlockId::FloweringAzalea || block == BlockId::NetherWartBlock ||
        block == BlockId::WarpedWartBlock) {
        return SoundMaterial::Grass;
    }

    // **A ladder has its own footsteps.** `SoundEvent::StepLadder` and its five
    // recordings were staged and loaded from the start, and nothing could name
    // them: a ladder matched no family above and fell through to the catch-all
    // at the bottom, so it was climbed sounding like *rock*. (An earlier note
    // here said "flammable, so it fell through to the wood test" - wrong on its
    // facts twice over: `isFlammable(Ladder)` is false, a ladder is
    // `BlockShape::Ladder` rather than `Cross`, and the bug was the worse of the
    // two answers. The same note then said `Explosion.cpp` still hands a ladder
    // the rock default; **both halves of that are now stale** - blast resistance
    // moved into `Explosion.hpp`, and the ladder was fixed there and pinned by
    // that file's sweep invariant 2, which fails outright if any thin shape
    // reaches stone's 6.) It still *breaks* as wood - the reference has no dig
    // bank of its own for it - which is why only the step sound differs.
    if (isLadder(block)) {
        return SoundMaterial::Ladder;
    }

    // A cut shape sounds like whatever it was cut from, which falls out of
    // `shapedParent` rather than needing six hundred rows of its own. Asked
    // last, so every family above wins over a parent's answer - which is what a
    // banner needs, its parent being wool.
    const BlockId parent = shapedParent(block);
    if (parent != block) {
        return soundMaterialFor(parent);
    }

    // Every remaining block is some kind of rock, which is also the safest thing
    // for an unknown to be. **No second test here.** This used to read
    // `isFlammable(block) ? Wood : Stone`, and that one clause is what made a
    // block of coal clunk like planks, a ladder unreachable, eleven of the
    // twelve plank types correct only by luck, and every wooden family above a
    // hostage to a fire rule that is being narrowed in another file this week.
    return SoundMaterial::Stone;
}

/// Which recording a material breaks and is walked on with.
///
/// **Defined here rather than in the .cpp so both can be `constexpr`**, which is
/// the only way the coverage assert below can exist at all: a `constexpr`
/// function has to be visible in every translation unit that constant-evaluates
/// it. Neither switch has a `default:`, which is deliberate - but that alone
/// catches nothing on this build, because MSVC's unhandled-enumerator warning
/// C4062 is off even at `/W4` (measured: turning on `/w44062` produced 23,594
/// warnings from one deliberately-partial switch elsewhere and was rejected).
/// So a `default:`-less switch here is a silent `SoundEvent::Count`, i.e.
/// silence rather than a wrong sound - the safe half of the failure, and not a
/// compile error. `soundBanksAreComplete` below is what makes it one.
constexpr SoundEvent digSoundFor(SoundMaterial material) {
    switch (material) {
    case SoundMaterial::Stone:
        return SoundEvent::DigStone;
    case SoundMaterial::Wood:
        return SoundEvent::DigWood;
    case SoundMaterial::Grass:
        return SoundEvent::DigGrass;
    case SoundMaterial::Gravel:
        return SoundEvent::DigGravel;
    case SoundMaterial::Sand:
        return SoundEvent::DigSand;
    case SoundMaterial::Cloth:
        return SoundEvent::DigCloth;
    case SoundMaterial::Snow:
        return SoundEvent::DigSnow;
    case SoundMaterial::Glass:
        return SoundEvent::DigGlass;
    // A ladder breaks as wood in the reference and has no dig bank of its own;
    // only its footsteps differ, which is what earned it a material.
    case SoundMaterial::Ladder:
        return SoundEvent::DigWood;
    case SoundMaterial::Coral:
        return SoundEvent::DigCoral;
    case SoundMaterial::Wet:
        return SoundEvent::DigWet;
    case SoundMaterial::None:
        break;
    }
    return SoundEvent::Count;
}

constexpr SoundEvent stepSoundFor(SoundMaterial material) {
    switch (material) {
    case SoundMaterial::Stone:
        return SoundEvent::StepStone;
    case SoundMaterial::Wood:
        return SoundEvent::StepWood;
    case SoundMaterial::Grass:
        return SoundEvent::StepGrass;
    case SoundMaterial::Gravel:
        return SoundEvent::StepGravel;
    case SoundMaterial::Sand:
        return SoundEvent::StepSand;
    case SoundMaterial::Cloth:
        return SoundEvent::StepCloth;
    case SoundMaterial::Snow:
        return SoundEvent::StepSnow;
    // Glass has no footstep of its own in the reference either; it walks like
    // stone.
    case SoundMaterial::Glass:
        return SoundEvent::StepStone;
    // The whole reason `Ladder` is a material rather than plain wood: five
    // `step_ladder` recordings that nothing could reach before 2026-08-17.
    case SoundMaterial::Ladder:
        return SoundEvent::StepLadder;
    case SoundMaterial::Coral:
        return SoundEvent::StepCoral;
    case SoundMaterial::Wet:
        return SoundEvent::StepWet;
    case SoundMaterial::None:
        break;
    }
    return SoundEvent::Count;
}

/// Every audible material must name a real dig bank and a real step bank.
///
/// **This is the compile error the missing `default:` was wrongly credited with
/// giving.** It walks the enumerators straight through rather than over the
/// handful a reader thought to name, so it cannot go stale the way a hand-kept
/// list does. `None` is the one exemption and is asked for by name: it is the
/// deliberate silence of air and water, not a hole.
///
/// **The single edit that makes it fail** is adding an enumerator to
/// `SoundMaterial` above `None` without giving it a `case` in both switches -
/// which is exactly how `SoundEvent::StepLadder` sat staged, loaded and
/// unreachable for twenty milestones, only from the other end.
constexpr bool soundBanksAreComplete() {
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(SoundMaterial::None); ++i) {
        const auto material = static_cast<SoundMaterial>(i);
        if (material == SoundMaterial::None) {
            continue;
        }
        if (digSoundFor(material) == SoundEvent::Count ||
            stepSoundFor(material) == SoundEvent::Count) {
            return false;
        }
    }
    return true;
}

static_assert(soundBanksAreComplete(),
              "every SoundMaterial but None must map to a dig bank and a step bank; a new "
              "enumerator without a case in both switches is silence, not a wrong sound, and "
              "nothing else on this build would tell you");
static_assert(digSoundFor(SoundMaterial::None) == SoundEvent::Count &&
                  stepSoundFor(SoundMaterial::None) == SoundEvent::Count,
              "and None must stay silent, or the exemption above is hiding a real hole");

// ---------------------------------------------------------------------------
// The compile-time sweep over `soundMaterialFor`, which is why it is `constexpr`
// and in this header at all.
//
// Modelled on `everyBlockNamed` in `Block.hpp`, `MiningSweep` in `Mining.hpp`
// and `BlastSweep` in `Explosion.hpp`: strided so no single `static_assert`
// blows MSVC's constexpr step budget, generated rather than hand-listed so a
// deleted line cannot silently stop checking 512 ids, and with a coverage
// assert at the end so ids appended past the last stride cannot fall out of it.
//
// Every other per-block table already had one. This was the last that did not,
// and its whole tail was rewritten in a single pass - which is exactly the
// circumstance `Explosion.hpp` describes for blast resistance, where a plain
// runtime float in a .cpp let a hundred and twenty blocks sit on the wrong
// number through three clean builds and a soak.
// ---------------------------------------------------------------------------

/// 512 for the reason it is 512 in `Block.hpp`, `Mining.hpp` and
/// `Explosion.hpp`: it splits the enum into few enough chunks to sweep and
/// small enough ones that Debug's `constexpr` step limit is never in sight.
/// **The stride is the fixed half of this pair and the pass count below is
/// derived from it**, so no number here has to be revisited when blocks are
/// added.
constexpr int kSoundSweepStride = 512;

/// How many strides the generated sweep instantiates - **derived, so it cannot
/// fall behind the block table.** Whatever `kBlockIdCount` becomes, this is the
/// number of `kSoundSweepStride` chunks needed to reach the end of it, so
/// coverage holds by construction rather than by somebody remembering to raise
/// a number. Deriving the *passes* and fixing the *stride* is the right way
/// round here for the same reason as in `Copper.hpp`: the stride bounds
/// per-pass `constexpr` work, which is what Debug's step limit measures, so it
/// must not be allowed to grow with the block count.
constexpr int kSoundSweepPasses =
    (static_cast<int>(kBlockIdCount) + kSoundSweepStride - 1) / kSoundSweepStride;

namespace sound {

/// The invariants the rewrite established, asked of every block id at once.
///
/// None of these restates the table - each is a rule the table can break, and
/// every one of them **was** broken by the version this replaced.
constexpr bool soundTableSound(int stride) {
    const int first = stride * kSoundSweepStride;
    const int end = first + kSoundSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId block = static_cast<BlockId>(i);
        const SoundMaterial material = soundMaterialFor(block);

        // 1. **No ladder is left on the rock default.** `Stone` is this table's
        //    catch-all, so a family that matches nothing lands on it looking
        //    entirely plausible - which is how four ladder ids were climbed
        //    sounding like cobblestone while five `step_ladder` recordings sat
        //    loaded and unnameable for twenty milestones.
        //    > Fails if: delete the `isLadder` branch. Nothing above it claims a
        //    > ladder and its `shapedParent` is itself, so all four fall to the
        //    > final `return SoundMaterial::Stone`.
        if (blockShape(block) == BlockShape::Ladder && material == SoundMaterial::Stone) {
            return false;
        }

        // 2. **Anything cut from a plank is heard as wood, all 212 of them.**
        //    Asked through `shapedParent` rather than of the plank itself,
        //    because the failure was never the plank - it was the stairs, slabs,
        //    fences, gates, buttons and plates hanging off it.
        //    > Fails if: narrow `isPlanksBlock` back to `block ==
        //    > BlockId::Planks` in `soundsLikeWood`. The eleven non-oak species
        //    > and everything cut from them fall past the forwarding to rock -
        //    > which is precisely what the old `isFlammable` tail was hiding,
        //    > since they were rescued only by happening to burn.
        if (isPlanksBlock(shapedParent(block)) && material == SoundMaterial::Stone) {
            return false;
        }

        // 3. **Every glass block tinkles, not just the clear one.** Tinted glass
        //    and the sixteen stained blocks are glass by every question anyone
        //    asks of them, and naming only `BlockId::Glass` dropped all
        //    seventeen through to the catch-all while the seventeen *panes*
        //    beside them were right - which is what hid it for so long.
        //    > Fails if: replace `isGlassBlock(block)` with `block ==
        //    > BlockId::Glass` in the glass branch.
        if (isGlassBlock(block) && material != SoundMaterial::Glass) {
            return false;
        }
    }
    return true;
}

/// One `static_assert` per stride, generated rather than written out - the
/// pattern `MiningSweep`, `DropSweep` and `BlastSweep` already use, and for the
/// reason those give: a hand-written list can have a middle line deleted and
/// 512 ids stop being checked while the build stays green.
template <int Pass>
struct SoundSweep {
    static_assert(soundTableSound(Pass),
                  "a block's sound material breaks one of the three invariants above - the "
                  "numbered comments name the single edit that causes each");
    static constexpr bool swept = true;
};

/// Which materials one stride of ids can actually produce, a bit per
/// enumerator.
///
/// **The fourth invariant, and the only one that cannot be asked of a single
/// id.** `SoundMaterial::Coral` spent its whole life unreachable *by
/// construction* - ten recordings staged and loaded that no code path could
/// name, because the function tested leaves, logs, glass, sand, gravel, snow,
/// wet, grass and cloth and never once asked about coral - and `Ladder` was
/// the same, five `step_ladder` recordings and no branch. Neither is visible
/// from any one block: every id had a plausible answer, and the hole was in
/// what the *range* of the function did not contain. Strided and OR-folded so
/// it reuses the seven passes rather than adding a 3,269-step evaluation of
/// its own.
constexpr unsigned soundMaterialsReached(int stride) {
    const int first = stride * kSoundSweepStride;
    const int end = first + kSoundSweepStride;
    unsigned mask = 0u;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        mask |= 1u << static_cast<unsigned>(soundMaterialFor(static_cast<BlockId>(i)));
    }
    return mask;
}

template <int... Pass>
constexpr unsigned everyMaterialReached(std::integer_sequence<int, Pass...>) {
    return (soundMaterialsReached(Pass) | ...);
}

/// Derived from the enum's own last enumerator rather than written as 12, so
/// adding a material to `SoundMaterial` widens what has to be reachable
/// instead of quietly leaving the new one out.
constexpr unsigned kEverySoundMaterial =
    (1u << (static_cast<unsigned>(SoundMaterial::None) + 1u)) - 1u;

template <int... Pass>
constexpr bool everySoundPassSwept(std::integer_sequence<int, Pass...>) {
    return (SoundSweep<Pass>::swept && ...);
}

} // namespace sound

static_assert(sound::everySoundPassSwept(std::make_integer_sequence<int, kSoundSweepPasses>{}),
              "the sound material table is wrong somewhere - the failing SoundSweep "
              "instantiation above names which stride");
/// **True by construction since `kSoundSweepPasses` became derived, and kept
/// anyway.** What it catches is no longer a block family - it is a future edit
/// that reverts the derivation to a hand-written count or gets its rounding
/// wrong.
static_assert(kSoundSweepPasses * kSoundSweepStride >= static_cast<int>(kBlockIdCount),
              "kSoundSweepPasses is derived from kBlockIdCount, so if this fires the derivation "
              "itself has been edited - restore the ceiling division rather than raising a number");

// **No half-stride early warning here, and after the derivation above there is
// nothing left for one to warn about.** A warning exists to give notice before a
// hand-written pass count runs out; this one cannot run out, so a guard here
// could never fire - which is worse than no guard, because it reads as
// protection.
//
// **This replaced an earlier comment that was already wrong.** It argued no
// warning was needed here because `Copper.hpp` was the tightest sweep and would
// fire first - true when written, false an hour later once copper's count
// became derived and could no longer fire at all. Sound reasoning, expired
// premise: the argument for dissolving a coupling rather than describing it.
//
// **Do not rebuild the list of sweeps that used to be here.** It was wrong
// twice within one hour - it missed `ChunkMesher.cpp`'s `kBoxFaceSweepPasses`,
// then missed `Block.hpp`'s `kModelSweepStride`. **Search `*SweepPasses` and
// `*SweepStride` instead**; the search finds sweeps nobody has told you about
// and a list never will. As of 2026-08-19 the only one still carrying a
// hand-written pass count is `ChunkMesher.cpp`'s, and `Copper.hpp` holds the
// assert that warns for it.

// **No bank may be orphaned.** Delete the `isLadder` branch, or the
// `HoneycombBlock` line above the coral test *and* the coral branch, and this
// is the only thing in the build that says so - every individual block still
// has a plausible answer, and the recordings simply stop being reachable.
static_assert(sound::everyMaterialReached(
                  std::make_integer_sequence<int, kSoundSweepPasses>{}) ==
                  sound::kEverySoundMaterial,
              "some SoundMaterial is returned for no block at all - its recordings are staged, "
              "loaded and unnameable, which is how Coral and Ladder both sat dead for twenty "
              "milestones");

// ---------------------------------------------------------------------------
// The named checks: one per finding whose fix is a *value* rather than an
// invariant, each written as the whole expression a reader evaluates rather
// than one side of it compared against itself.
// ---------------------------------------------------------------------------

// **Restore `return isFlammable(block) ? Wood : Stone` as the catch-all and the
// first of these fails.** All four burn, none of them is timber, and the block
// of coal is the one that reached the wood bank that way. The other three are
// named from the other side so the check also fires if anyone "tidies" them out
// of the grass row - a hay bale and dried kelp are grass in the reference, and
// **TNT is in it twice over**, the placed charge and the primed one, which were
// answering different materials for as long as only one of them burned.
// <https://minecraft.wiki/w/Block_sound_type>, `stone` and `grass` rows.
static_assert(soundMaterialFor(BlockId::CoalBlock) == SoundMaterial::Stone &&
                  soundMaterialFor(BlockId::HayBlock) == SoundMaterial::Grass &&
                  soundMaterialFor(BlockId::DriedKelpBlock) == SoundMaterial::Grass &&
                  soundMaterialFor(BlockId::Tnt) == SoundMaterial::Grass &&
                  soundMaterialFor(BlockId::TntPrimed) == SoundMaterial::Grass,
              "burning is not the same question as being made of wood, and lighting a charge must "
              "not change what it is made of");

// **Delete `isBeehive` from `soundsLikeWood` and this fails.** The eight hive
// ids were the one family the `isFlammable` cut dropped: they were reaching the
// wood bank by burning, nothing named them afterwards, and rock is a plausible
// enough answer that only a sweep would ever have said so. It is Java's
// assignment kept knowingly - Bedrock leaves both in its unclassified `normal`
// bucket - and the branch's own comment is where that is argued.
// <https://minecraft.wiki/w/Block_sound_type>, `wood` and `normal` rows.
static_assert(soundMaterialFor(BlockId::Beehive) == SoundMaterial::Wood,
              "a beehive and a bee nest are wood, which is Java's call and this table's, and the "
              "branch comment says why Bedrock's default bucket does not override it");

// **The two bookshelves, asked together, because that is the shape of the
// bug.** One was named and the other was not, so a chiseled bookshelf broke
// and was walked on as cobblestone beside a shelf that sounded like planks.
// Both sides are here so it also fires if anyone moves the plain one - the
// point is that they agree, not that either is wood on its own.
// <https://minecraft.wiki/w/Block_sound_type>, `chiseled_bookshelf` row.
static_assert(soundMaterialFor(BlockId::ChiseledBookshelf) ==
                      soundMaterialFor(BlockId::Bookshelf) &&
                  soundMaterialFor(BlockId::ChiseledBookshelf) == SoundMaterial::Wood,
              "the two bookshelves must sound like each other, and like the planks they are cut "
              "from");

// **The honeycomb block is the reference's `coral` group, and it was rock.**
// Named from both sides: it must be `Coral`, and the reef it borrows the bank
// from must still be `Coral` too, so folding either into the other's branch
// fails here rather than silently.
static_assert(soundMaterialFor(BlockId::HoneycombBlock) == SoundMaterial::Coral &&
                  soundMaterialFor(BlockId::TubeCoralBlock) == SoundMaterial::Coral &&
                  soundMaterialFor(BlockId::HoneyBlock) != SoundMaterial::Coral,
              "the honeycomb block is in the reference's coral group in both editions; the honey "
              "block beside it in Block.hpp's isHoneyLike is not");

// **Drop the `isEnderChest`/`isStowbox` exclusion and the second and third
// halves fail.** Both are named, so it also fires if the ordinary chest is
// moved to their answer - the three must be decided together or not at all.
static_assert(soundMaterialFor(BlockId::Chest) == SoundMaterial::Wood &&
                  soundMaterialFor(BlockId::EnderChest) == SoundMaterial::Stone &&
                  soundMaterialFor(BlockId::Stowbox) == SoundMaterial::Stone,
              "a chest is planks; an ender chest is obsidian and a stowbox is stone");

// **Move the `isBed || isSignLike` branch below the `shapedParent` forwarding
// and both halves fail together.** A bed and a banner are built from six wool
// and the reference still files them under `wood`, so they are the one place
// the parent's answer must lose - and the wool beside them proves the
// forwarding itself is still intact.
static_assert(soundMaterialFor(BlockId::BedRunFirst) == SoundMaterial::Wood &&
                  soundMaterialFor(BlockId::BannerRunFirst) == SoundMaterial::Wood &&
                  soundMaterialFor(BlockId::WhiteWool) == SoundMaterial::Cloth,
              "a bed and a banner are wood in the reference despite being made of wool");

// ---------------------------------------------------------------------------
// The seam: the two per-block tables that were both keyed off `isFlammable`,
// asked together in the one place that can see both.
//
// **`MaterialTable::conflicts` cannot see this and its own comment says why** -
// it counts two *families* claiming one texture layer, so a block that is wrong
// in both tables, or wrong in one and unremarkable in the other, scores zero.
// It stayed at zero the whole time a block of coal was shaded as a plank.
// `Sounds.hpp` is the only file that includes both, which is why the check
// lives here rather than beside either table; `core/` already depends on
// `world/` and not the other way round, so nothing is inverted by it.
//
// **It is deliberately not a blanket "the two must agree" sweep, and that is a
// measured decision rather than caution.** A throwaway probe over all 3,269 ids
// found 877 blocks where one table calls something timber and the other does
// not, and almost every one of them is right: a pumpkin, a melon, a cocoa pod,
// a stem, a jukebox and an end rod are all `wood` to the ear and none of them
// is made of wood, and a bed is wood to the ear and wool to the eye. An assert
// demanding agreement would need an exception list longer than either table.
// What *can* be pinned is the narrow class that actually went wrong: the blocks
// both tables got wrong together, because both were asking the same question -
// "does it burn?" - in place of "what is it made of?".
//
// > **The single edit that makes this fail:** put `if (isFlammable(id)) return
// > MaterialFamily::Wood;` back on the end of `materialFamilyFor`, or
// > `isFlammable(block) ? Wood : Stone` back on the end of `soundMaterialFor`.
// > Either one alone breaks it, from its own side.
// ---------------------------------------------------------------------------
static_assert(soundMaterialFor(BlockId::CoalBlock) == SoundMaterial::Stone &&
                  materialFamilyFor(BlockId::CoalBlock) == MaterialFamily::Stone &&
                  soundMaterialFor(BlockId::HayBlock) == SoundMaterial::Grass &&
                  materialFamilyFor(BlockId::HayBlock) == MaterialFamily::Organic &&
                  soundMaterialFor(BlockId::DriedKelpBlock) == SoundMaterial::Grass &&
                  materialFamilyFor(BlockId::DriedKelpBlock) == MaterialFamily::Organic &&
                  soundMaterialFor(BlockId::Tnt) == SoundMaterial::Grass &&
                  materialFamilyFor(BlockId::Tnt) == MaterialFamily::Organic,
              "the four blocks that burn without being timber must be timber to neither table - "
              "an isFlammable tail on either one shades or sounds them like planks");

// **The two ids of one block, in both tables at once.** Each table had this
// split separately and for the same reason: only the placed charge burned, so
// the primed one answered something else. Fires the moment either stops naming
// both.
static_assert(soundMaterialFor(BlockId::Tnt) == soundMaterialFor(BlockId::TntPrimed) &&
                  materialFamilyFor(BlockId::Tnt) == materialFamilyFor(BlockId::TntPrimed),
              "lighting a charge must not change what it is made of, to the eye or the ear");

// **The two that moved *underneath* these tables inside a single diff**, which
// is the whole shape of this finding. Azalea and flowering azalea were added to
// `isFlammable` and became planks to look at; nether wart block was removed from
// it and became rock. Nobody edited either table, and nothing failed.
static_assert(materialFamilyFor(BlockId::Azalea) == MaterialFamily::Leaves &&
                  materialFamilyFor(BlockId::FloweringAzalea) == MaterialFamily::Leaves &&
                  soundMaterialFor(BlockId::Azalea) == SoundMaterial::Grass &&
                  materialFamilyFor(BlockId::NetherWartBlock) == MaterialFamily::Organic &&
                  soundMaterialFor(BlockId::NetherWartBlock) == SoundMaterial::Grass,
              "an azalea is foliage and a wart block is neither wood nor rock; both answers used "
              "to be a side effect of what Block.hpp thinks burns");


/// The cues that are a **continuing condition rather than a moment**: standing
/// in a burning room, swimming with no air left, being underwater at all.
///
/// They exist as their own thing because the mixer has no loop point, so each
/// one is retriggered on its own timer - and the timer is the only part of it
/// the call site would otherwise have to own, which is what turned four
/// ambient sounds into four hand-rolled floats and a nest of `if`s. Here the
/// interval and the volume live beside the stems they name and each call site
/// is one line.
enum class AmbientCue : std::uint8_t {
    Fire,
    Lava,
    LavaPop,
    Water,
    /// The gasp of drowning. Not ambient in the scenery sense, but the same
    /// shape exactly: a condition that holds, sounding at a steady cadence.
    Breath,
    Count,
};

/// Loads the staged sound bank and turns game events into voices.
///
/// **It owns the mapping and the engine owns the mixing**, which is the same
/// split every other system here follows: `engine::AudioEngine` has no idea
/// what a block is, and this has no idea what a sample rate is.
class Sounds {
public:
    /// `directory` is `sounds-reference/` beside the executable. A missing
    /// folder is not an error - every event simply has no recordings and every
    /// call becomes a no-op, because a silent game is far better than one that
    /// refuses to start over a missing sound.
    void load(engine::AudioEngine& audio, const std::filesystem::path& directory);

    /// Plays one of an event's recordings at a place in the world.
    void play(engine::AudioEngine& audio, SoundEvent event, const glm::vec3& at,
              float volume = 1.0f, float pitch = 1.0f);

    /// Plays it without a position: something that happened *to you* rather
    /// than near you.
    void playGlobal(engine::AudioEngine& audio, SoundEvent event, float volume = 1.0f,
                    float pitch = 1.0f);

    /// A creature's own voice. **A baby speaks higher and quicker**, which the
    /// reference gets from the same pitch multiplier rather than from a second
    /// set of recordings, so `scale` is all this needs to know about the
    /// individual.
    void playVoice(engine::AudioEngine& audio, CreatureKind kind, VoiceState state,
                   const glm::vec3& at, float scale = 1.0f);

    /// Starts a random track if none is playing. Called every frame; it keeps
    /// its own long timer, so music comes and goes rather than running
    /// continuously.
    void tickMusic(engine::AudioEngine& audio, float deltaSeconds);

    /// Sounds a continuing condition at its own cadence, or lets it lapse.
    ///
    /// `sounding` is the game's judgement - "there is fire within earshot",
    /// "my air has run out" - and `at` is where it is coming from, ignored for
    /// the cues that happen to you rather than near you. Call it every frame
    /// whether or not the condition holds: a cue that stops holding forgets its
    /// timer, so the next one is immediate rather than up to an interval late.
    ///
    /// **Retriggered, not looped**, because the mixer has no loop point. The
    /// interval is the recording's own length rounded down a little, so a long
    /// one overlaps by a hair rather than leaving a gap.
    void tickAmbient(engine::AudioEngine& audio, AmbientCue cue, bool sounding,
                     const glm::vec3& at, float deltaSeconds);

    /// How many recordings were found. Zero means the bank is missing, which is
    /// worth one line in the log rather than a warning per event.
    std::size_t loaded() const { return m_loaded; }

    /// Checks the loaded bank against what the game can actually *name*, and
    /// returns one line per problem - empty when all is well.
    ///
    /// **A query: the caller logs what comes back.** Three separate faults, all
    /// of which have happened here and none of which produces a warning, a
    /// validation error or a wrong-looking line of code:
    ///
    ///  - an event the game plays with no recordings staged, which is a stem
    ///    this file spells one way and the staging script spells another;
    ///  - a creature voice family or state with nothing behind it;
    ///  - **and the one `has()` alone cannot see** - a bank that loaded
    ///    perfectly and that nothing in `game/` ever names. A ladder's
    ///    footsteps sat like that for twenty milestones and thirteen more banks
    ///    were found the same way, because a healthy-looking bank and a played
    ///    one are not the same question. The answer to the second is a
    ///    hand-kept table in the .cpp, and an event with no row in it is
    ///    reported here by name at startup - **not** caught by the compiler,
    ///    because MSVC's unhandled-enumerator warning is off even at `/W4`.
    ///
    /// **Wiring what it found paid for it twice over.** Giving `ItemBreak` a
    /// call site turned up a fourth hand-rolled tool-wear path - the bow,
    /// reading the durability constant and clearing its own slot instead of
    /// calling the owner that `Mining.hpp` already listed it in. A completeness
    /// check over one system found a correctness bug in another, because the
    /// missing noise *was* the missing call.
    std::vector<std::string> sweep() const;

    /// Whether an event found any recordings at all. The stem list and the
    /// staging script are two places that must agree, and a mismatch fails
    /// silently as an event that never makes a noise. Read by `sweep`.
    bool has(SoundEvent event) const {
        return event != SoundEvent::Count && !m_banks[static_cast<std::size_t>(event)].empty();
    }
    bool hasVoice(CreatureVoice family, VoiceState state) const {
        return family != CreatureVoice::None &&
               !m_voices[static_cast<std::size_t>(family)][static_cast<std::size_t>(state)].empty();
    }

private:
    engine::SoundHandle pick(SoundEvent event);
    engine::SoundHandle pickVoice(CreatureVoice family, VoiceState state);

    std::array<std::vector<engine::SoundHandle>, static_cast<std::size_t>(SoundEvent::Count)>
        m_banks;
    std::array<std::array<std::vector<engine::SoundHandle>,
                          static_cast<std::size_t>(VoiceState::Count)>,
               static_cast<std::size_t>(CreatureVoice::Count)>
        m_voices;
    std::size_t m_loaded = 0;
    std::uint32_t m_random = 0x9E3779B9u;
    float m_musicTimer = 0.0f;
    /// Counts *down* to the next retrigger, one per cue. Zero means due now,
    /// which is also what a lapsed cue is reset to.
    std::array<float, static_cast<std::size_t>(AmbientCue::Count)> m_ambientTimers{};
};

} // namespace game
