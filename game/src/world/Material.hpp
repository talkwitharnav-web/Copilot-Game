#pragma once

#include "world/Block.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace game {

/// What a surface is made of, for lighting purposes.
///
/// Written on `soundMaterialFor`'s pattern and for the same reason: a dozen
/// family questions cover eleven hundred blocks, and a new block inherits the
/// right answer the day it is added rather than needing a row.
///
/// **There is no PBR art and none can be staged.** The reference dump contains
/// zero normal, roughness, metallic or emissive maps - checked across all 25,981
/// files - and Java has never shipped any. So these values are authored here,
/// which is the path Mojang's own `texture_set.json` offers: it accepts a single
/// value in place of a map, described in their docs as "the equivalent to
/// referencing a texture image filled uniformly with that value".
enum class MaterialFamily : std::uint8_t {
    Stone,
    /// Anything named Polished, Smooth, Cut or Bricks: worked rock, which takes
    /// a sheen that a broken face does not.
    PolishedStone,
    Dirt,
    Sand,
    Gravel,
    Wood,
    Leaves,
    Plant,
    Wool,
    Metal,
    Glass,
    Ice,
    Fluid,
    /// Water is nearly a mirror; lava is not, and sharing a family with it made
    /// a lava lake reflect like a swimming pool.
    Lava,
    /// Terracotta, concrete and the glazed set: fired, so smoother than rock.
    Ceramic,
    Snow,
    /// The fallback. Nothing looks wrong being slightly rough and non-metallic.
    Organic,
    Count,
};

/// **Roughness here is perceptual, not the `alpha` a GGX distribution wants.**
/// The shader squares it. A value copied from a LabPBR source is already alpha
/// and must not be squared twice.
struct MaterialProperties {
    float roughness = 0.85f;
    float metallic = 0.0f;
};

/// Hand-tuned, **seventeen rows** - one per `MaterialFamily` enumerator, which
/// is what the three anchoring asserts below exist to keep true. (This said
/// "sixteen" for as long as there have been seventeen; a stale count in the one
/// comment a reader checks the list against is exactly the drift those asserts
/// are watching for, so it is worth keeping right.) Reflectance is deliberately
/// not a column: 0.04 is physically right for every dielectric here, and the
/// two that are not - glass and ice - differ by less than the eye can find
/// without a reference beside it.
inline constexpr std::array<MaterialProperties, static_cast<std::size_t>(MaterialFamily::Count)> kMaterials{{
    {0.88f, 0.0f}, // Stone
    {0.45f, 0.0f}, // PolishedStone
    {0.95f, 0.0f}, // Dirt
    {0.90f, 0.0f}, // Sand
    {0.92f, 0.0f}, // Gravel
    {0.72f, 0.0f}, // Wood
    {0.82f, 0.0f}, // Leaves
    {0.86f, 0.0f}, // Plant
    {0.98f, 0.0f}, // Wool
    {0.30f, 1.0f}, // Metal
    {0.06f, 0.0f}, // Glass
    {0.12f, 0.0f}, // Ice
    {0.04f, 0.0f}, // Fluid
    {0.55f, 0.0f}, // Lava
    {0.38f, 0.0f}, // Ceramic
    {0.85f, 0.0f}, // Snow
    {0.90f, 0.0f}, // Organic
}};

/// The table is **positional**, and being sized by `Count` is exactly what
/// hides that: add an enumerator without a row and the array is still "full",
/// every row below the new one has shifted up by one, and the last row quietly
/// takes `MaterialProperties`' own defaults - 0.85 and non-metallic, which is a
/// real and plausible pair. C++ has no designated initialiser for an array, so
/// what stands in for one is three anchors that a shift of any size moves.
///
/// The single edit that fails them: adding an enumerator to `MaterialFamily`
/// without adding its row here, wherever in the list it goes.
constexpr int metallicRowCount() {
    int rows = 0;
    for (const MaterialProperties& row : kMaterials) {
        if (row.metallic > 0.5f) {
            ++rows;
        }
    }
    return rows;
}

static_assert(metallicRowCount() == 1 &&
                  kMaterials[static_cast<std::size_t>(MaterialFamily::Metal)].metallic == 1.0f,
              "Metal must be the only metallic row and must still be at Metal's own index - if it "
              "is not, an enumerator was added without its row and every row below it has shifted");
static_assert(static_cast<std::size_t>(MaterialFamily::Organic) + 1 == kMaterials.size() &&
                  kMaterials.back().roughness == 0.90f && kMaterials.back().metallic == 0.0f,
              "the last row must still be Organic's own pair; a row appended past it would take "
              "the struct's defaults instead and nothing else here would notice");

/// Set on a layer that is vegetation, so a later slice can move it in the wind
/// without asking what block it came from. Reserved now because retrofitting a
/// bit into a packed word is expensive and this one is free.
inline constexpr std::uint8_t kMaterialFlagFoliage = 1u << 0;
inline constexpr std::uint8_t kMaterialFlagTranslucent = 1u << 1;

/// True for the handful of blocks that are worked metal rather than rock with
/// metal in it. **An ore is not metallic** - it is stone with specks, and a
/// binary metallic flag on the whole face would turn the stone into chrome.
///
/// **The name test below can only see an *extra* block, and that guard used to
/// be the whole function's ceiling** (2026-08-19, found by a probe that dumped
/// `materialFamilyFor` for every id beside each name). Every metal thing
/// whose id is a plain enumerator or a packed run - all four rail kinds, the
/// hopper, the cauldron, both iron openings and `Block of Emberite` - reached
/// `if (!isExtraBlock(id)) return false;` and answered *no*, so 95 ids were
/// shaded as rough rock at 0.88 instead of metal at 0.30 with `metallic` set.
/// (That count read 94 until it was re-measured on 2026-08-20. Every count this
/// file stated was checked that day and **five of five were wrong** - none by
/// more than a rounding of attention, and none catchable by reading.)
/// `Block of Emberite` is the proof: it is **named in the list below** and the
/// list never ran for it. `BlockId::IronBars` sitting alone above the guard is
/// the same bug found once and patched for one id instead of for the guard.
///
/// So the predicates come first and the name test is the *fallback*, not the
/// rule. What would make this note false: `isExtraBlock` growing to cover the
/// packed runs, at which point the two halves could merge again.
constexpr bool isMetalBlock(BlockId id) {
    // **Openings ask the table that owns which of them are metal**, exactly as
    // `Sounds.hpp`'s `soundsLikeWood` does and for the reason it writes down
    // there: `kDoorFamilies` is the one place a door's material is decided, so
    // naming the iron ids here would be a second copy of it.
    if (isDoor(id)) {
        return kDoorFamilies[static_cast<std::size_t>(doorFamily(id))].metal;
    }
    if (isTrapdoor(id)) {
        return kTrapdoorFamilies[static_cast<std::size_t>(trapdoorFamily(id))].metal;
    }
    // Worked metal that carries no name the test below could read, because the
    // test below never sees it. A lantern is an iron cage, a bell is cast
    // metal, and `EmberiteBlock` is the reference's dark alloy under our name.
    if (isRail(id) || isHopper(id) || isCauldron(id) || isAnvil(id)) {
        return true;
    }
    if (id == BlockId::IronBars || id == BlockId::EmberiteBlock || id == BlockId::Bell ||
        id == BlockId::Lantern || id == BlockId::SoulLantern) {
        return true;
    }
    // **Diamond and emerald, because the reference says so and nothing else
    // here would have.** `Mojang/bedrock-samples`
    // `resource_pack/blocks.json` publishes `diamond_block` and
    // `emerald_block` with `"sound": "metal"`, while `coal_block` and
    // `lapis_block` on either side of them publish `"stone"` and all three
    // `raw_*_block` publish `"stone"` too. So this is not "gem blocks are
    // shiny" applied evenly - it is four rows of the same shape where the
    // source splits two off, which is why reading could not have found it.
    // These are plain enumerators, so the name list below never sees them; it
    // is the same reach problem the note above this function describes.
    // What would make this note false: those two rows in `blocks.json`
    // changing away from `metal`.
    if (id == BlockId::DiamondBlock || id == BlockId::EmeraldBlock) {
        return true;
    }
    // **The lightning rod, which this predicate denied while the family granted
    // it** (2026-08-20, found by a probe comparing the two answers id by id,
    // not by reading). `materialFamilyFor` returns `Metal` for it from its own
    // `isLightningRod` arm, several tests above the one that asks this
    // function - so the rod has always *rendered* as metal and no player has
    // ever seen this. What was wrong is that a `constexpr` predicate in a
    // header called `isMetalBlock` answered *no* about a copper rod, which is
    // the CLAUDE.md shape "a rule that exists, is correct, and is commented in
    // only one of the two places that need it".
    //
    // It is a plain enumerator run, so the name list below never reaches it,
    // and "Lightning Rod" contains none of the words that list matches even if
    // it did. Sourced: `resource_pack/blocks.json` publishes `lightning_rod`
    // with `"sound": "copper"`, the same metal group as the copper blocks.
    //
    // **Adding it here cannot move any block's family**, because the arm that
    // already answers for the rod runs first - this makes the two agree rather
    // than changing what is drawn. That is the point: the next reader to call
    // this predicate should not have to know which of the two to trust.
    if (isLightningRod(id)) {
        return true;
    }
    if (!isExtraBlock(id)) {
        return false;
    }
    const std::string_view name = extraBlockInfo(id).name;
    // The rule above, enforced. A bare `find("Copper")` matched "Deepslate
    // Copper Ore" and "Block of Raw Copper" - so the ore this comment exists to
    // exclude was being rendered as chrome, and raw ore with it.
    if (name.find("Ore") != std::string_view::npos || name.find("Raw ") != std::string_view::npos) {
        return false;
    }
    return name.find("Block of Iron") != std::string_view::npos ||
           name.find("Block of Gold") != std::string_view::npos ||
           name.find("Block of Copper") != std::string_view::npos ||
           name.find("Block of Emberite") != std::string_view::npos ||
           name.find("Copper") != std::string_view::npos || name.find("Anvil") != std::string_view::npos ||
           name.find("Rail") != std::string_view::npos || name.find("Chain") != std::string_view::npos;
}

/// True for a name the reference gives to rock that has been worked smooth.
constexpr bool isWorkedStoneName(std::string_view name) {
    return name.find("Polished") != std::string_view::npos || name.find("Smooth") != std::string_view::npos ||
           name.find("Cut ") != std::string_view::npos || name.find("Chiseled") != std::string_view::npos ||
           name.find("Bricks") != std::string_view::npos || name.find("Quartz") != std::string_view::npos ||
           name.find("Tiles") != std::string_view::npos;
}

inline constexpr MaterialFamily materialFamilyFor(BlockId id) {
    // A cut shape is made of whatever it was cut from. Six hundred and forty
    // blocks are covered by this one line.
    const BlockId parent = shapedParent(id);
    if (parent != id) {
        return materialFamilyFor(parent);
    }

    if (isLava(id)) {
        return MaterialFamily::Lava;
    }
    if (isFluid(id)) {
        return MaterialFamily::Fluid;
    }
    if (isLeafBlock(id)) {
        return MaterialFamily::Leaves;
    }
    // **The azalea bush is foliage and is not a leaf block.** Both ids were
    // added to `isFlammable` in the same round that this function's tail still
    // keyed off it, so they were shaded at a plank's 0.72 - and worse, a plank
    // does not carry `kMaterialFlagFoliage`, so the one bush in the game that
    // should move in the wind was the one thing pinned still. `Leaves` rather
    // than `Plant` because that is what the block is: leaves on a stem, drawn
    // with the reference's own azalea leaf art.
    if (id == BlockId::Azalea || id == BlockId::FloweringAzalea) {
        return MaterialFamily::Leaves;
    }
    // Before the plant test, and that is the point: a torch is drawn as a
    // crossed pair like a flower, so it would otherwise be tagged as foliage and
    // sway in the wind the moment anything reads that flag.
    //
    // **`isTorchBlock`, not the three ids by name.** The redstone torch grew to
    // ten states, and the nine new ones share the lit one's texture layer - so
    // naming only `RedstoneTorch` left that layer claimed by two different
    // families, which the startup check reports and one of which is then
    // silently ignored.
    if (isTorchBlock(id)) {
        return MaterialFamily::Wood;
    }
    // The rest of the redstone family. A rail already answers Metal through its
    // name, and a button and a plate are cut shapes and answered above.
    if (isRedstoneWire(id) || isRepeater(id) || isComparator(id) || isObserver(id) ||
        isDispenserLike(id) || isPiston(id) || isPistonHead(id) || isLever(id) ||
        isTripwireHook(id) || isTripwire(id) || isTarget(id) || isNoteBlock(id) ||
        isDaylightDetector(id) || isRedstoneLamp(id)) {
        return isNoteBlock(id) || isDaylightDetector(id) ? MaterialFamily::Wood
                                                         : MaterialFamily::Stone;
    }
    if (isLightningRod(id)) {
        return MaterialFamily::Metal;
    }
    if (isCrossBlock(id) || isVine(id) || isCocoa(id) || id == BlockId::Kelp || id == BlockId::Seagrass ||
        id == BlockId::LilyPad) {
        return MaterialFamily::Plant;
    }
    // **Doors, trapdoors and beds - 704 ids that were being shaded as rock**
    // (2026-08-19, probe over every id). Nothing above catches them: a door is
    // its own packed run rather than a shaped child of planks, so
    // `shapedParent` returns it unchanged, `isPlanksBlock` says no, and it fell
    // the whole length of this function to the rock catch-all at the bottom.
    // **576 doors and trapdoors - 528 wooden, 48 iron - plus 128 bed ids.** The
    // wooden ones had a plank's 0.72 replaced by rock's 0.88, and the iron pair
    // sat at 0.88 non-metallic instead of 0.30 metallic.
    //
    // **Both numbers in this paragraph were wrong until they were re-measured
    // on 2026-08-20**, and each was wrong in a way worth recognising: the total
    // said 736 because it was assembled from the categories a *fix* touched
    // rather than the ones the sentence names, and it swept in metal ids that
    // are not doors; and 576 was labelled "wooden" when it is the whole family,
    // wood and iron together, counted under one of its two halves. Neither is a
    // typo and neither could be caught by reading. **Re-measure a count rather
    // than carrying it** - a comment that states one is a claim, not a label.
    //
    // **Which of them are metal is `isMetalBlock`'s question, and it now asks
    // `kDoorFamilies`' own flag** - the same table `Sounds.hpp` asks, which is
    // the point: the rule already existed, was correct, and was commented, in
    // one of the two places that needed it.
    //
    // A bed is `Wool` rather than `Wood`: the reference files it under the
    // `wood` **sound** group, but this table sets how a surface takes light and
    // the face you look at is a blanket. That is the same split this file
    // already makes at the `isFlammable` note below - what a thing is made of
    // and what it sounds like are two questions with two answers.
    //
    // **A banner gets the same answer and there is deliberately no arm for it
    // here, because one would be dead code.** The `shapedParent` forward at the
    // top of this function runs first, and a banner's parent is its wool, so all
    // 128 of them are already `Wool` before this line is reached - `isBanner`
    // tested below would never see one. That answer is *right* by the rule the
    // bed states above: `blocks.json` publishes `standing_banner` and
    // `wall_banner` as `wood`, and the face you look at is still cloth.
    //
    // **But it is right by derivation rather than by rule, which is what the
    // assert beside the bed's now pins.** The parenting exists to give a banner
    // its *colour*; the material falls out of it. Re-parent banners to their
    // planks - for a crafting or a burn question, and `isFlammable` already has
    // to special-case `isSignLike` for exactly that reason - and 128 ids change
    // how they take light with nothing else in the build to say so. Their sign
    // and hanging-sign siblings are the counter-case and the proof this is a
    // real fork: same predicate `isSignLike`, but they parent to Oak Planks and
    // so come out `Wood`, which is also what the reference calls them.
    //
    // Checked 2026-08-19 against `resource_pack/blocks.json`; falsified by
    // `shapedParent(BannerRunFirst)` ceasing to be a wool id.
    if (isDoor(id) || isTrapdoor(id)) {
        return isMetalBlock(id) ? MaterialFamily::Metal : MaterialFamily::Wood;
    }
    if (isBed(id)) {
        return MaterialFamily::Wool;
    }
    if (isLogBlock(id) || isPlanksBlock(id) || id == BlockId::Bookshelf || id == BlockId::CraftingTable ||
        id == BlockId::SmithingTable || isChest(id) || isBeehive(id) || isLadder(id)) {
        return MaterialFamily::Wood;
    }
    if (isWoolBlock(id) || isCarpet(id)) {
        return MaterialFamily::Wool;
    }
    // **`isGlassBlock`, not the one id.** Tinted glass and the sixteen stained
    // ones are glass by every question anyone asks of them, and naming only the
    // clear block dropped all seventeen through to the catch-all at the bottom
    // of this function - which returns Stone, a real and plausible value, so
    // they were shaded at roughness 0.88 like rock instead of 0.06 like glass.
    //
    // **`MaterialTable::conflicts` cannot see this and stayed at zero
    // throughout** - measured, before and after. Each stained block owns its own
    // texture layer, so no second family ever contests it; the counter only
    // catches two families claiming *one* layer, never one family claiming a
    // layer it has no business with.
    if (isGlassBlock(id) || isPane(id)) {
        return MaterialFamily::Glass;
    }
    if (isIce(id)) {
        return MaterialFamily::Ice;
    }
    if (isMetalBlock(id)) {
        return MaterialFamily::Metal;
    }
    // **`Sandstone` is not sand, and this line used to say it was** - while the
    // extra-block tail below deliberately excludes every *other* sandstone from
    // the sand test and says so in its own comment. So plain Sandstone answered
    // Sand and Red Sandstone answered Stone: one block in two colours in two
    // families, and the file contradicting itself four hundred lines apart.
    // Rock is the answer both comments intend (2026-08-19).
    if (id == BlockId::Sand) {
        return MaterialFamily::Sand;
    }
    if (id == BlockId::Gravel || id == BlockId::Clay) {
        return MaterialFamily::Gravel;
    }
    // **Both ids for the cube, plus the powder.** `BlockId::SnowBlock` is a
    // second id for the same solid snow cube as `BlockId::Snow`, so naming one
    // of them shaded the other like rock beside the layers it is buried in;
    // `Mining.hpp`'s `isSolidSnow` is the predicate that owns the pair and must
    // move with this if the duplicate is ever collapsed. Powder snow is its own
    // block and its own sound group in the reference, and it is snow to look at
    // by every measure this family exists to set.
    if (id == BlockId::Snow || id == BlockId::SnowBlock || isSnowLayer(id) ||
        id == BlockId::PowderSnow) {
        return MaterialFamily::Snow;
    }
    // **`SoulSoil` is here because its own pair is already handled and it was
    // not** - the CLAUDE.md #14 shape, a rule that reached one of two. Soul
    // sand answers `Sand` through the name test below; soul soil carries
    // neither "Sand" nor "Dirt" in its name, so it fell all the way to the rock
    // catch-all, and the two blocks that generate side by side were shaded
    // 0.90 and 0.88 from different families for no reason anyone chose.
    // `blocks.json` gives each its own sound group (`soul_sand`, `soul_soil`),
    // so the source does not name a family for either; having its own group has
    // never disqualified anything here, mycelium and podzol sit in `Dirt` on
    // the same terms. It is soil, it is dug with a shovel, `Dirt` is the
    // nearest family this table has. What would make this note false: a
    // dedicated soul family, at which point both ids move together.
    if (id == BlockId::Grass || id == BlockId::Dirt || id == BlockId::CoarseDirt || id == BlockId::Podzol ||
        id == BlockId::Mycelium || isFarmland(id) || id == BlockId::DirtPath || id == BlockId::SoulSoil) {
        return MaterialFamily::Dirt;
    }
    if (id == BlockId::Terracotta) {
        return MaterialFamily::Ceramic;
    }

    if (isExtraBlock(id)) {
        const std::string_view name = extraBlockInfo(id).name;
        if (name.find("Terracotta") != std::string_view::npos ||
            name.find("Concrete") != std::string_view::npos || name.find("Glazed") != std::string_view::npos) {
            return MaterialFamily::Ceramic;
        }
        if (isWorkedStoneName(name)) {
            return MaterialFamily::PolishedStone;
        }
        // **Sandstone is rock that used to be sand**, so it is excluded before
        // the sand test rather than after it - and the worked-stone test above
        // has to stay in front of both, because "Smooth Sandstone" is neither.
        // Soul sand and suspicious sand are the two this catches, and both were
        // falling to the Stone catch-all at the bottom.
        if (name.find("Sand") != std::string_view::npos &&
            name.find("Sandstone") == std::string_view::npos) {
            return MaterialFamily::Sand;
        }
        // **And gravel beside it, which this had no test for at all**: the core
        // `Gravel` id answers Gravel two dozen lines above and Suspicious Gravel
        // answered rock, the same one-of-a-pair split the sand line was written
        // to close (2026-08-19).
        if (name.find("Gravel") != std::string_view::npos) {
            return MaterialFamily::Gravel;
        }
        // Rooted dirt, mud and moss, for the same reason. "Mud Bricks" is
        // already gone by here, taken by the worked-stone test.
        if (name.find("Dirt") != std::string_view::npos || name.find("Mud") != std::string_view::npos ||
            name.find("Moss") != std::string_view::npos) {
            return MaterialFamily::Dirt;
        }
        // The rotated carved pumpkins and the jack o'lantern, which are extra
        // ids and so never reach the core pumpkin test further down. Same
        // `blocks.json` `"wood"` group as the plain pumpkin, and skipping them
        // would leave one gourd shaded as timber and five as rock. "Pumpkin
        // Stem" is already gone by here - it answered `Plant` far above - and
        // the `Stem` exclusion keeps it that way if that ever changes.
        if ((name.find("Pumpkin") != std::string_view::npos || name.find("Jack o'") != std::string_view::npos) &&
            name.find("Stem") == std::string_view::npos) {
            return MaterialFamily::Wood;
        }
    }

    // **Not `isFlammable`, and for the same reason `soundMaterialFor` stopped
    // asking it in the same round this file did not.** Fuel and *substance* are
    // two different questions: a block of coal, TNT, a hay bale and dried kelp
    // all burn and not one of them is timber, so all four were being shaded at
    // a plank's 0.72. It is also a live dependency rather than a tidy-up -
    // `Block.hpp` is narrowing that predicate right now, and every answer keyed
    // off it moves underneath this table when it does. Nether wart block proved
    // it inside one diff: it was dropped from `isFlammable` and silently went
    // Wood -> Stone here without anybody editing this function.
    //
    // What the tail was actually rescuing, measured rather than guessed - 33
    // ids, and every one of them is named below or falls to rock on purpose.

    // **The timber no family above catches.** Bark and its stripped form are
    // the log's own six-sided cousins (`isLogBlock` stops at the stripped
    // *logs*, which is why sixteen ids needed the fuel test). A lectern and a
    // composter are the two wooden workstations the named benches above miss.
    // Scaffolding has no wood in it at all - the reference gives it a bamboo
    // group of its own, and with nothing bamboo staged, wood is the near
    // neighbour and rock is not; that is the same call `soundMaterialFor` makes
    // and cites, so the two consumers agree about it by argument rather than by
    // both happening to burn.
    if (isBarkBlock(id) || isStrippedBarkBlock(id) || id == BlockId::Lectern ||
        isComposter(id) || id == BlockId::Scaffolding) {
        return MaterialFamily::Wood;
    }
    // **Gourds, fungal flesh and both campfires, all four sourced rather than
    // guessed** (2026-08-19). `Mojang/bedrock-samples`
    // `resource_pack/blocks.json` publishes `"sound": "wood"` for `pumpkin`,
    // `carved_pumpkin`, `lit_pumpkin`, `melon_block`, `brown_mushroom_block`,
    // `red_mushroom_block`, `mushroom_stem`, `campfire` and `soul_campfire` -
    // the same group it gives `lectern` and `composter` on the line above,
    // which is the precedent this file already follows.
    //
    // **This corrects a comment of mine at the bottom of this function**, which
    // named pumpkin, melon and the mushroom blocks among ids that "would go to
    // `Organic`" and dismissed the move as two hundredths of roughness. The
    // primary source says wood, not organic, and that is 0.72 against rock's
    // 0.88 - eight times the difference the note argued about, and visible.
    // The wiki would not have shown it; the *resource* pack states it.
    //
    // **Say which pack, because one of them does not contain blocks at all.**
    // `behavior_pack/` has entities, items, loot tables, recipes, spawn rules
    // and trading, and **no `blocks/` directory** - Bedrock block behaviour is
    // engine-side and unpublished (verified 2026-08-19:
    // `behavior_pack/blocks/stone.json` is a 404 while
    // `behavior_pack/entities/wolf.json` is a 200, so that is the repository
    // saying no rather than a broken fetch). An earlier draft of this comment
    // said "behaviour pack", which would have sent the next reader to a
    // directory that does not exist and left them concluding a correct fix had
    // been invented. Block *sound groups* live in `resource_pack/blocks.json`;
    // block *behaviour numbers* - tick rates, growth odds, flow distance -
    // cannot be sourced from this repository at all, and minecraft.wiki remains
    // the best available authority for those.
    // What would make this note false: those rows changing group in
    // `blocks.json`.
    if (id == BlockId::Pumpkin || id == BlockId::Melon || id == BlockId::BrownMushroomBlock ||
        id == BlockId::RedMushroomBlock || id == BlockId::MushroomStem || id == BlockId::Campfire ||
        id == BlockId::SoulCampfire) {
        return MaterialFamily::Wood;
    }
    // **Soft organic matter: not timber, not foliage, not cloth.** `Organic` is
    // 0.90 against Wood's 0.72, and it deliberately does *not* carry
    // `kMaterialFlagFoliage` - a hay bale is dried grass but it is a solid bale
    // and must not sway, which is the trap `Plant` and `Leaves` set here.
    //
    // **Both TNT ids, and that is the point.** The placed charge burned and the
    // primed one did not, so the two halves of one block were answering Wood and
    // Stone - the identical split `soundMaterialFor` had and fixed. The two wart
    // blocks join them: they are fungal flesh, they have a sound group of their
    // own in the reference that nothing here can play, and rock is the one thing
    // they are certainly not.
    // <https://minecraft.wiki/w/Block_sound_type>, `grass` row for the charge,
    // the bale and the dried kelp; `nether_wart` for the pair.
    if (id == BlockId::HayBlock || id == BlockId::DriedKelpBlock || id == BlockId::Tnt ||
        id == BlockId::TntPrimed || id == BlockId::NetherWartBlock ||
        id == BlockId::WarpedWartBlock) {
        return MaterialFamily::Organic;
    }

    // **What is left in rock on purpose, measured rather than assumed**
    // (2026-08-19, probe over every id; re-run against a primary source
    // the same day and *corrected*, see below). What still reaches here and is
    // visibly not rock: sponge, slime, honey, honeycomb, the sculk family, the
    // froglights, shroomlight, bone block, both nylium, ancient debris, the
    // levers and the tripwire hooks. Every one of those is a block
    // `Mojang/bedrock-samples` `resource_pack/blocks.json` gives a *sound group
    // of its own* - `sponge`, `slime`, `honey_block`, `nylium`,
    // `ancient_debris`, `shroomlight`, `bone_block`, `lever` - so the source
    // names no neighbouring family for them and rock stays the safest answer an
    // unknown can have. A block of coal is among them and is compressed
    // mineral, published as `stone`.
    //
    // **The earlier version of this note was wrong and is worth reading as a
    // warning.** It listed pumpkin, melon, both mushroom blocks and the stem
    // here too, claimed they "would go to `Organic`", and dismissed the whole
    // question as two hundredths of roughness not worth spending. The
    // resource pack publishes all five as `"wood"` - 0.72, not 0.90 - so the
    // family was wrong *and* the gap was eight times what the note asserted,
    // and the note's confidence is exactly what would have stopped the next
    // reader looking. They are handled above now. The lesson is that the list
    // was walked honestly and still came out wrong, because it was walked
    // against judgement rather than against a source that simply states the
    // answer.
    //
    // **Where this source stops, measured 2026-08-19.**
    // `resource_pack/blocks.json` names a sound group for 1,192 of its 1,231
    // blocks - 96.8% - and **39 carry none at all**, `flower_pot`, `cauldron`,
    // `enchanting_table`, `brick_block`, `beehive` and `bee_nest` among them.
    // So an absent group is a real answer here: it means *ask something else*,
    // not "the source disagrees". A flower pot was very nearly moved to
    // `Ceramic` on this round for looking like fired clay, and the only thing
    // that stopped it was noticing its group is the empty string rather than a
    // value. **A missing field and a field that says rock are not the same
    // evidence**, and the difference is invisible if you read the value without
    // checking it exists.
    //
    // The companion trap, which does not bite here but will next door:
    // `metadata/vanilladata_modules/mojang-blocks.json` publishes state
    // *domains* shared across many blocks, so a value list there describes the
    // field rather than any one block's range. `sound` is not like that - it is
    // one scalar per block, 124 distinct values over 1,231 blocks - so a value
    // read here is authoritative for that block and nothing wider. That file
    // carries no material, sound or render field at all, so it cannot answer
    // this function's question and is the wrong place to look for it.
    //
    // Two the source does *not* settle, recorded so they are not re-derived:
    // `cactus` publishes `"cloth"` and `hay_block` publishes `"grass"`. Neither
    // moves. This family drives roughness and the metallic flag, and sound
    // group is evidence for that rather than an answer to it - a cactus is not
    // cloth to look at, and the hay bale is already argued into `Organic` above
    // on its own terms. Where the two questions disagree, say which one is
    // being asked.
    return MaterialFamily::Stone;
}

/// The two numbers a block's family actually puts in front of the shader.
///
/// **They exist for the asserts below and are written as `buildMaterialTable`
/// evaluates them**, base and all: this project has had eleven asserts that
/// compared one side of a derivation against itself, so a check that restates
/// `materialFamilyFor`'s enum answer would prove only that the enum is the enum.
/// A roughness is the thing a wrong family is actually felt through.
///
/// **So neither of these has a runtime caller, on purpose, and that is not a
/// stranded feature - the live path does not go through them.** It is
/// `buildMaterialTable` below, which asks `materialFamilyFor` itself and packs
/// the same two numbers into a row per texture layer; `Main.cpp` calls it and
/// hands `.rows` straight to `Renderer::setMaterialTable`. Said here because a
/// reader who greps these two names finds every reference inside this file and
/// can reasonably conclude the material never reaches the GPU - that was filed
/// as a bug on 2026-08-19 and it is wrong. **What would make it true:** the
/// `buildMaterialTable` call in `Main.cpp` disappearing, or `packMaterial`
/// ceasing to read `kMaterials`. Measured the day this was written, by decoding
/// the row the shader receives rather than by reading: an iron door arrives at
/// roughness 0.302 and metallic 1.000, an oak door at 0.722 and 0.0.
constexpr float familyRoughness(BlockId id) {
    return kMaterials[static_cast<std::size_t>(materialFamilyFor(id))].roughness;
}
constexpr float familyMetallic(BlockId id) {
    return kMaterials[static_cast<std::size_t>(materialFamilyFor(id))].metallic;
}

// **The 704 ids a probe found sitting in the wrong family on 2026-08-19**
// (count re-measured 2026-08-20; it read 736 until then),
// pinned against absolute numbers nobody is free to move quietly. `DoorRunFirst`
// is the first oak door and `DoorRunLast` the last iron one, so one line covers
// both ends of the run and the family flag that separates them.
static_assert(familyRoughness(BlockId::DoorRunFirst) == 0.72f &&
                  familyMetallic(BlockId::DoorRunFirst) == 0.0f &&
                  familyRoughness(BlockId::TrapdoorRunFirst) == 0.72f,
              "a wooden door or trapdoor must take a plank's roughness; both runs fell to rock's "
              "0.88 for as long as this function has existed");
static_assert(familyMetallic(BlockId::DoorRunLast) == 1.0f &&
                  familyRoughness(BlockId::DoorRunLast) == 0.30f &&
                  familyMetallic(BlockId::TrapdoorRunLast) == 1.0f,
              "the iron opening must be metal, and it must be metal because kDoorFamilies says so "
              "rather than because an id was named here");
static_assert(familyMetallic(BlockId::RailRunFirst) == 1.0f &&
                  familyMetallic(BlockId::EmberiteBlock) == 1.0f,
              "a rail and a block of Emberite are worked metal; both answered rock because the "
              "name test they are named in could only ever see an extra block");
static_assert(familyRoughness(BlockId::BedRunFirst) == 0.98f,
              "a bed's blanket takes wool's roughness, not rock's");
// **The banner, which reaches the same answer without being named anywhere.**
// Each of these three can fail on its own, which is the property a self-
// comparing assert lacks: the first is the outcome, the second is the family
// that outcome encodes, and the third is the derivation that produces it. A
// reader who re-parents banners to their planks trips the third and is told
// which of the two questions - colour or material - they have just moved.
//
// The sign clauses are not decoration either. They are the counter-case: the
// same `isSignLike` group, parented to planks instead of wool, coming out
// `Wood` - so a change that flattened all sign-like blocks to one material
// would break here rather than passing quietly.
static_assert(familyRoughness(BlockId::BannerRunFirst) == 0.98f &&
                  materialFamilyFor(BlockId::BannerRunFirst) == MaterialFamily::Wool &&
                  isWoolBlock(shapedParent(BlockId::BannerRunFirst)),
              "a banner is cloth to the eye and gets there through its wool parent, not through an "
              "arm of materialFamilyFor - the shapedParent forward at the top of that function runs "
              "first, so an isBanner arm below it would never be reached");
static_assert(materialFamilyFor(BlockId::SignRunFirst) == MaterialFamily::Wood &&
                  materialFamilyFor(BlockId::HangingSignRunFirst) == MaterialFamily::Wood &&
                  isPlanksBlock(shapedParent(BlockId::SignRunFirst)),
              "a sign is sign-like exactly as a banner is, and is wood - blocks.json publishes all "
              "three as the wood sound group and only the banner's face disagrees");

// **The genuine negative test, and it is one because it can fail on its own** -
// the construct `FaceGeometry.hpp` uses, because a rule that only ever says yes
// cannot be shown to be a rule. **An ore is the one this file most needs to
// keep saying no to**: it is the case the `Ore` exclusion above exists for, and
// turning stone to chrome is far uglier than the bug just fixed.
static_assert(!isMetalBlock(BlockId::IronOre) && familyMetallic(BlockId::IronOre) == 0.0f &&
                  familyRoughness(BlockId::IronOre) == 0.88f,
              "iron ore is stone with specks in it and must never be metallic");

// **The door pair below is a restatement, and it was advertised as a negative
// test like the ore above until 2026-08-19.** Neither of its first two clauses
// can fail while the two asserts fifty lines up hold: `metallicRowCount() == 1`
// makes `Metal` the only metallic row, so `familyMetallic(DoorRunLast) == 1.0f`
// already forces that family to `Metal` - hence not `Wood` - and
// `familyMetallic(DoorRunFirst) == 0.0f` already forces the other end away from
// `Metal`, which for a door is the whole of `isMetalBlock`, since the arm reads
// `isMetalBlock(id) ? Metal : Wood`. Kept, because saying out loud what the
// numbers mean is worth two lines; relabelled, because a negative test is the
// single construct this codebase trusts most and calling a restatement one buys
// false confidence - eleven asserts once passed while pointing at the wrong
// texture for exactly that reason.
//
// **The third and fourth clauses are the ones that can fail alone, and they are
// why this assert is still worth having.** `materialFamilyFor` forwards a
// shaped child to its parent on its first line, so every arm below that forward
// is reachable only while the id is its own parent - which is precisely why
// there is deliberately no `isBanner` arm. Make a door a shaped child of its
// planks and this whole branch becomes dead code: the wooden end would keep
// answering 0.72 through the parent and nothing else here would notice.
static_assert(!isMetalBlock(BlockId::DoorRunFirst) &&
                  materialFamilyFor(BlockId::DoorRunLast) != MaterialFamily::Wood &&
                  shapedParent(BlockId::DoorRunFirst) == BlockId::DoorRunFirst &&
                  shapedParent(BlockId::DoorRunLast) == BlockId::DoorRunLast,
              "an oak door must not be metal and an iron one must not be wood - and the arm that "
              "decides it is only reached while a door is its own shapedParent");
static_assert(familyRoughness(BlockId::Sandstone) == familyRoughness(BlockId::Stone) &&
                  familyRoughness(BlockId::Sand) == 0.90f,
              "sandstone is rock and sand is sand; the two disagreed with each other and with "
              "this file's own comment about which is which");

// **The 2026-08-19 primary-source round, pinned the same way.** Each pairs a
// relation against an absolute anchor, because `Pumpkin == Lectern` alone would
// still hold if both slid to rock together.
static_assert(familyRoughness(BlockId::Pumpkin) == familyRoughness(BlockId::Lectern) &&
                  familyRoughness(BlockId::Pumpkin) == 0.72f &&
                  familyRoughness(BlockId::Melon) == 0.72f &&
                  familyRoughness(BlockId::MushroomStem) == 0.72f &&
                  familyRoughness(BlockId::Campfire) == 0.72f &&
                  familyRoughness(BlockId::SoulCampfire) == 0.72f,
              "blocks.json publishes all of these in the same \"wood\" group as the lectern; they "
              "were shaded as rock at 0.88 and an earlier comment here argued they were organic");
static_assert(isMetalBlock(BlockId::DiamondBlock) && familyMetallic(BlockId::EmeraldBlock) == 1.0f &&
                  familyRoughness(BlockId::DiamondBlock) == 0.30f,
              "blocks.json gives diamond and emerald blocks the \"metal\" sound group; both are "
              "plain enumerators, so the name list that would have caught them never runs");
static_assert(familyRoughness(BlockId::SoulSoil) == familyRoughness(BlockId::Dirt) &&
                  familyRoughness(BlockId::SoulSoil) == 0.95f,
              "soul soil is soil; its own pair, soul sand, was already handled and it was not");

// **The lightning rod, added 2026-08-20 after a probe caught the two answers
// disagreeing.** Both ends of the run, because it is twelve ids and a fix that
// reached only the first would look identical from here. The second clause is
// the one that was already true and stayed true - it is included so that a
// future reader can see the pair this assert exists to keep together, which is
// the whole content of the bug.
static_assert(isMetalBlock(BlockId::LightningRodRunFirst) &&
                  isMetalBlock(BlockId::LightningRodRunLast) &&
                  materialFamilyFor(BlockId::LightningRodRunFirst) == MaterialFamily::Metal &&
                  materialFamilyFor(BlockId::LightningRodRunLast) == MaterialFamily::Metal,
              "a lightning rod is copper - blocks.json publishes it in the \"copper\" sound group - "
              "and isMetalBlock denied it for as long as it existed while materialFamilyFor "
              "granted it from a different arm; the two must not be allowed to drift apart again");

// The reversed claims for that round. **Coal and lapis are the control**: they
// sit beside diamond and emerald in every other table in this project, and
// blocks.json still calls them stone - so a change that made "gem blocks are
// shiny" true in general would break here, which is the whole point.
static_assert(!isMetalBlock(BlockId::CoalBlock) && !isMetalBlock(BlockId::LapisBlock) &&
                  familyMetallic(BlockId::CoalBlock) == 0.0f,
              "the metal answer for diamond and emerald is those two rows of blocks.json, not a "
              "rule about gem blocks; coal and lapis are published as stone and must stay rock");
static_assert(materialFamilyFor(BlockId::Cactus) != MaterialFamily::Wool &&
                  materialFamilyFor(BlockId::HayBlock) != MaterialFamily::Plant &&
                  familyRoughness(BlockId::Cactus) == 0.88f,
              "sound group is evidence for a PBR family, not an answer to it - cactus is published "
              "as cloth and the hay bale as grass, and neither is shaded that way on purpose");

/// One row of the table the shader reads, packed exactly as it unpacks it.
///
/// R roughness, G metallic, B emissive, A flags - four bytes, one word, and
/// deliberately **no ambient occlusion column**: AO already has an owner in
/// `ChunkMesher`'s `kOcclusionSteps`, and a second copy of a value is this
/// project's most repeated bug.
constexpr std::uint32_t packMaterial(float roughness, float metallic, float emissive, std::uint8_t flags) {
    const auto quantise = [](float value) {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    };
    return quantise(roughness) | (quantise(metallic) << 8) | (quantise(emissive) << 16) |
           (static_cast<std::uint32_t>(flags) << 24);
}

struct MaterialTable {
    std::vector<std::uint32_t> rows;
    /// **Distinct layers** two different families both claim. Should be zero;
    /// anything else means one of them is being silently ignored, and which one
    /// wins is decided by whichever block id happens to come first.
    ///
    /// Counted per layer, not per claim: the walk asks every face, facing and
    /// chest half, so one disagreeing layer used to be reported forty-five
    /// times over and the number looked like a catastrophe.
    int conflicts = 0;
    /// The first layer two families both claimed, and which two. **A count on
    /// its own is not a diagnostic** - it says something is wrong and gives you
    /// nothing to look at, which cost a round trip the first time a redstone
    /// block shared a picture with the block it was named after.
    int firstConflictLayer = -1;
    int firstConflictHeld = -1;
    int firstConflictWanted = -1;
    /// Layers no block ever asks for - item sprites, the white utility layer,
    /// animation frames. They keep the default row.
    int unclaimed = 0;
};

/// Walks every block, face and facing, and writes what each texture layer is
/// made of.
///
/// **Keyed on the texture layer, not on the block.** A block does not have one
/// material: grass is soil underneath and living surface on top, and a lit
/// furnace glows on exactly one of its six faces. The layer is already in the
/// vertex, already a `flat` varying, and already part of the greedy merge key -
/// so this costs no vertex change, no re-mesh, and cannot fragment merging.
inline MaterialTable buildMaterialTable(std::size_t layerCount) {
    MaterialTable table;
    const MaterialProperties fallback = kMaterials[static_cast<std::size_t>(MaterialFamily::Organic)];
    table.rows.assign(layerCount, packMaterial(fallback.roughness, fallback.metallic, 0.0f, 0));

    std::vector<int> claimedBy(layerCount, -1);
    std::vector<bool> conflicted(layerCount, false);
    std::vector<float> emission(layerCount, -1.0f);

    const auto faces = std::array{BlockFace::Top, BlockFace::Bottom, BlockFace::Side};
    const auto directions = std::array{FaceDirection::Unknown, FaceDirection::PosX, FaceDirection::NegX,
                                       FaceDirection::PosZ, FaceDirection::NegZ};
    const auto halves = std::array{ChestHalf::Single, ChestHalf::Left, ChestHalf::Right};

    for (int raw = 0; raw <= static_cast<int>(kLastBlock); ++raw) {
        const auto id = static_cast<BlockId>(raw);
        if (id == BlockId::Air) {
            continue;
        }
        const MaterialFamily family = materialFamilyFor(id);
        const MaterialProperties& properties = kMaterials[static_cast<std::size_t>(family)];
        const float emissive = static_cast<float>(blockLightEmission(id)) / static_cast<float>(kMaxLight);

        std::uint8_t flags = 0;
        if (family == MaterialFamily::Plant || family == MaterialFamily::Leaves) {
            flags |= kMaterialFlagFoliage;
        }
        if (isTranslucent(id)) {
            flags |= kMaterialFlagTranslucent;
        }

        for (BlockFace face : faces) {
            for (FaceDirection direction : directions) {
                for (ChestHalf half : halves) {
                    const auto layer = static_cast<int>(blockTextureLayer(id, face, direction, half));
                    if (layer < 0 || static_cast<std::size_t>(layer) >= layerCount) {
                        continue;
                    }

                    if (claimedBy[static_cast<std::size_t>(layer)] < 0) {
                        claimedBy[static_cast<std::size_t>(layer)] = static_cast<int>(family);
                        table.rows[static_cast<std::size_t>(layer)] =
                            packMaterial(properties.roughness, properties.metallic, 0.0f, flags);
                    } else if (claimedBy[static_cast<std::size_t>(layer)] != static_cast<int>(family) &&
                               !conflicted[static_cast<std::size_t>(layer)]) {
                        conflicted[static_cast<std::size_t>(layer)] = true;
                        if (table.firstConflictLayer < 0) {
                            table.firstConflictLayer = layer;
                            table.firstConflictHeld = claimedBy[static_cast<std::size_t>(layer)];
                            table.firstConflictWanted = static_cast<int>(family);
                        }
                        ++table.conflicts;
                    }

                    // **The lowest claim wins, and that is what gets the furnace
                    // right.** `FurnaceTop` is drawn by the unlit furnace as
                    // well as the lit one, so it scores zero and stays dark;
                    // `FurnaceFrontLit` is only ever drawn by the lit one, so it
                    // keeps its 13 and only the mouth glows. A block-keyed
                    // emissive would light the whole box.
                    float& stored = emission[static_cast<std::size_t>(layer)];
                    stored = stored < 0.0f ? emissive : std::min(stored, emissive);
                }
            }
        }
    }

    for (std::size_t layer = 0; layer < layerCount; ++layer) {
        if (claimedBy[layer] < 0) {
            ++table.unclaimed;
            continue;
        }
        const auto& properties = kMaterials[static_cast<std::size_t>(claimedBy[layer])];
        const std::uint8_t flags = static_cast<std::uint8_t>((table.rows[layer] >> 24) & 0xffu);
        table.rows[layer] =
            packMaterial(properties.roughness, properties.metallic, std::max(emission[layer], 0.0f), flags);
    }

    return table;
}

} // namespace game
