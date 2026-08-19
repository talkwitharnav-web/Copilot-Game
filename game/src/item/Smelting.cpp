#include "item/Smelting.hpp"

#include <array>
#include <cstddef>

namespace game {
namespace {

struct SmeltRule {
    ItemId input;
    ItemStack output;
};

/// **The row count, named once.** Five separate places used to spell `37` - the
/// table, the two checkers, the negative-pin function and its local copy - so
/// adding one rule meant five edits in step and a missed one is a compile
/// error at best. It is one constant now: bump this, add the row, done. The
/// initialiser list is still checked against it by the language, so a count
/// that disagrees with the rows cannot build.
constexpr std::size_t kSmeltRuleCount = 38;

/// Ratios and burn times come from the reference recipe data - see
/// `CRAFTABLE.md`.
///
/// **Audited row by row against the primary source on 2026-08-19, and it is
/// complete for every id this game has.** `behavior_pack/recipes` holds 1756
/// files; 149 of them are smelting recipes - 146 carrying a `furnace_` prefix
/// and three named `<output>_furnace`, which is the naming trap that hid the
/// deepslate rows for a while. **Do not read the prefix as the input either**:
/// `furnace_smooth_basalt`, `furnace_sponge` and both cracked-brick files are
/// named after what comes *out*. Every one of the 149 is now answered, by this
/// table or by one of the four family arms in `smeltResult`: the five raw/cooked
/// pairs through `isRawFood`, 29 wood rows through `charsIntoCharcoal`, the
/// sixteen terracottas through the glazed offset, the eight deepslate ores
/// through `stoneOreFor`, and iron, gold and chainmail gear through `nuggetFor`.
///
/// **The remainder are absent ids, not absent rows**, which is why none of them
/// is a hole in this table and none should be written until `Block.hpp` or the
/// `ItemId` run grows:
///   - 9 leaves -> `leaf_litter` (no `LeafLitter` block)
///   - red sand -> glass (**there is no `RedSand` id at all**)
///   - resin clump -> resin brick (neither item exists)
///   - 11 copper tools and armour pieces, 3 horse armours, 3 nautilus armours
///     and 3 spears -> nuggets (none of those items exists)
///   - **5 golden tools -> gold nuggets, and this is the one that costs a
///     player something.** `nuggetFor` already handles golden *armour*; there
///     is no `GoldenPickaxe` family in `ItemId` at all, so gold is a dead-end
///     metal for tools. Filed, and the smelt rows follow the ids for free.
///
/// **Green dye is named, not counted to.** This row said
/// `static_cast<ItemId>(static_cast<int>(kFirstDye) + 13)` - a value derived
/// somewhere other than the table that owns it, which is this project's most
/// expensive bug shape. `ItemId::GreenDye` is an enumerator; nothing has to
/// stay true for it to keep meaning green, whereas the arithmetic quietly
/// became "purple" the moment a colour was inserted into the run.
///
/// > **CROSS-FILE: this table is also the campfire's menu, and the campfire's
/// > owner sees no edit when you add a row here.** `world/Campfire.hpp` asks
/// > `campfireCooks(item)`, which is `isFood(smeltResult(item).item)` - it does
/// > not carry a list of its own. **So every food-output row added here becomes
/// > campfire-cookable the same instant, with nothing in `Campfire.hpp` to
/// > review.**
/// >
/// > **That derivation is deliberate and correct** - the reference's campfire
/// > does cook a strict subset of the furnace's, and deriving it is what keeps
/// > the two from drifting apart. But it means **this table is not free to
/// > grow.** Before adding a row, ask whether a campfire should cook it; if the
/// > answer is no *and* the output is food, the derivation is wrong for that
/// > row and the campfire needs an exclusion rather than the row needing a
/// > different output.
/// >
/// > **The live control is the chorus row below.** `ChorusFruit ->
/// > PoppedChorusFruit` is a furnace-only recipe in the reference -
/// > `furnace_chorus_fruit.json` carries the `furnace` tag alone, where
/// > `furnace_potato.json` carries `["furnace","smoker","campfire",
/// > "soul_campfire"]` - and our campfire excludes it **for the right reason
/// > and by accident of a good design**: popped chorus fruit is not food, so
/// > `isFood` returns false. It is a control precisely because it would have
/// > gone wrong silently: had popped chorus fruit been edible, adding that row
/// > would have taught the campfire a recipe the reference forbids, and no
/// > assert in either file would have said so.
/// >
/// > Recorded 2026-08-19 11:20. **Falsified by** `campfireCooks` gaining a
/// > table of its own, or by any campfire tag appearing in a per-row field
/// > here - either would make the coupling explicit and this note obsolete.
constexpr std::array<SmeltRule, kSmeltRuleCount> kSmelting{{
    {itemForBlock(BlockId::Cobblestone), ItemStack{itemForBlock(BlockId::Stone), 1}},
    {itemForBlock(BlockId::Sand), ItemStack{itemForBlock(BlockId::Glass), 1}},
    {itemForBlock(BlockId::Stone), ItemStack{itemForBlock(BlockId::SmoothStone), 1}},
    {ItemId::RawIron, ItemStack{ItemId::IronIngot, 1}},
    {ItemId::RawGold, ItemStack{ItemId::GoldIngot, 1}},
    {ItemId::RawCopper, ItemStack{ItemId::CopperIngot, 1}},
    {itemForBlock(BlockId::AncientDebris), ItemStack{ItemId::EmberiteScrap, 1}},
    {itemForBlock(BlockId::StoneBricks), ItemStack{itemForBlock(BlockId::CrackedStoneBricks), 1}},
    {itemForBlock(BlockId::Sandstone), ItemStack{itemForBlock(BlockId::SmoothSandstone), 1}},
    // **The rule that did not travel to its twin.** The plain sandstone one
    // line up has smelted to smooth since this table was written; the red one
    // never did, so a player building in red desert stone could not make the
    // smooth variant at all. `behavior_pack/recipes/furnace_red_sandstone.json`
    // gives it the same smooth form (its input names `"data": 0`, the plain
    // red sandstone rather than the chiselled or cut one).
    {itemForBlock(BlockId::RedSandstone),
     ItemStack{itemForBlock(BlockId::SmoothRedSandstone), 1}},
    {itemForBlock(BlockId::WetSponge), ItemStack{itemForBlock(BlockId::Sponge), 1}},
    // The two raw foods that sit in the appended run rather than the first one.
    // Named rather than folded into `isRawFood`, because that run is not pairs
    // all the way through - the tropical fish is followed by the pufferfish.
    {ItemId::RawRabbit, ItemStack{ItemId::CookedRabbit, 1}},
    {ItemId::RawSalmon, ItemStack{ItemId::CookedSalmon, 1}},
    {itemForBlock(BlockId::Kelp), ItemStack{ItemId::DriedKelp, 1}},
    // **The potato, which `smokerAccepts` already believed was here.** Its
    // comment names "the five raw meats, the four fish, the potato and the
    // kelp" - written from the reference's food list rather than from this
    // table, so it described a row that did not exist and every reader since
    // has trusted it. `behavior_pack/recipes/furnace_potato.json` is tagged
    // `[furnace, smoker, campfire, soul_campfire]`, and because the smoker and
    // the campfire both gate on `isFood` of the **result**, this one row
    // reaches all four cookers with nothing else to change.
    {ItemId::Potato, ItemStack{ItemId::BakedPotato, 1}},
    // The chains the new recipes need a first link for.
    {itemForBlock(BlockId::Clay), ItemStack{itemForBlock(BlockId::Terracotta), 1}},
    {ItemId::ClayBall, ItemStack{ItemId::Brick, 1}},
    {itemForBlock(BlockId::Cactus), ItemStack{ItemId::GreenDye, 1}},
    // The other plant that cooks into a dye.
    // `behavior_pack/recipes/furnace_sea_pickle.json` writes the output as the
    // legacy `dye:10`, and 10 is lime - named here rather than derived,
    // because an aux number is exactly the kind of value that quietly means a
    // different colour once the run it indexes changes.
    {itemForBlock(BlockId::SeaPickle), ItemStack{ItemId::LimeDye, 1}},
    {itemForBlock(BlockId::NetherQuartzOre), ItemStack{ItemId::Quartz, 1}},
    {itemForBlock(BlockId::QuartzBlock), ItemStack{itemForBlock(BlockId::SmoothQuartz), 1}},
    {itemForBlock(BlockId::Basalt), ItemStack{itemForBlock(BlockId::SmoothBasalt), 1}},
    {itemForBlock(BlockId::NetherBricks), ItemStack{itemForBlock(BlockId::CrackedNetherBricks), 1}},
    // The fifth member of the cracked family, which had four. Its reference
    // file is named after its **output** -
    // `behavior_pack/recipes/furnace_cracked_polished_blackstone_bricks.json` -
    // which is why a search by input name finds nothing and the row looked
    // absent from the reference rather than absent from us.
    {itemForBlock(BlockId::PolishedBlackstoneBricks),
     ItemStack{itemForBlock(BlockId::CrackedPolishedBlackstoneBricks), 1}},
    {itemForBlock(BlockId::Netherrack), ItemStack{ItemId::NetherBrickItem, 1}},
    // **The deepslate family's way back, and its two unreachable members.**
    // Mining deepslate yields cobbled deepslate and there is no silk touch in
    // this game, so without the first row the deepslate a player stands in is
    // gone the moment they dig it. Primary source, not the wiki:
    // `behavior_pack/recipes/deepslate_furnace.json` is `minecraft:cobbled_deepslate`
    // -> `minecraft:deepslate`, tag `[furnace]`. **It is named after its output,
    // which is why a search for `furnace_deepslate*` finds nothing** - three of
    // the pack's smelting recipes use that convention and all three are in this
    // family (`cracked_deepslate_bricks_furnace.json` and
    // `cracked_deepslate_tiles_furnace.json` are the other two, and they are the
    // cracked pair below). Search the recipe list by substring, never by a
    // guessed prefix: this row stood on a wiki citation for want of that.
    {itemForBlock(BlockId::CobbledDeepslate), ItemStack{itemForBlock(BlockId::Deepslate), 1}},
    {itemForBlock(BlockId::DeepslateBricks),
     ItemStack{itemForBlock(BlockId::CrackedDeepslateBricks), 1}},
    {itemForBlock(BlockId::DeepslateTiles),
     ItemStack{itemForBlock(BlockId::CrackedDeepslateTiles), 1}},
    // Every ore block into what it refines to, wiki `[[Ore]]` "Smelting
    // ingredient". **The eight deepslate halves are deliberately absent**: they
    // are answered in `smeltResult` by the stone ore they mirror, because
    // `stoneOreFor` already owns that mapping and eight more rows here would be
    // a second copy of it, free to disagree.
    //
    // Only creative reaches these today - no silk touch means a mined ore drops
    // its resource rather than itself - but a blast furnace already *accepts*
    // every one of them, so without these rows it takes them and does nothing.
    {itemForBlock(BlockId::CoalOre), ItemStack{ItemId::Coal, 1}},
    {itemForBlock(BlockId::IronOre), ItemStack{ItemId::IronIngot, 1}},
    {itemForBlock(BlockId::CopperOre), ItemStack{ItemId::CopperIngot, 1}},
    {itemForBlock(BlockId::GoldOre), ItemStack{ItemId::GoldIngot, 1}},
    {itemForBlock(BlockId::RedstoneOre), ItemStack{ItemId::Redstone, 1}},
    {itemForBlock(BlockId::LapisOre), ItemStack{ItemId::LapisLazuli, 1}},
    {itemForBlock(BlockId::DiamondOre), ItemStack{ItemId::Diamond, 1}},
    {itemForBlock(BlockId::EmeraldOre), ItemStack{ItemId::Emerald, 1}},
    // The nether's two ores sit outside `isOre` - that predicate answers
    // "anything a vein places" - so each is named. Gold, not a nugget: wiki
    // `[[Nether Gold Ore]]` smelts to a whole ingot, which is the one reason
    // the reference gives for ever using silk touch on it.
    {itemForBlock(BlockId::NetherGoldOre), ItemStack{ItemId::GoldIngot, 1}},
    // **The furnace is the only producer of a popped chorus fruit anywhere,
    // and two live recipes were eating one.** `PoppedChorusFruit` is the four
    // corners of a purpur block (`Recipe.cpp:1517`) and half of an end rod
    // (`Recipe.cpp:1592`), and nothing in the game could make one - bug shape
    // #15, a complete feature one row short of being reachable. `ChorusFruit`
    // itself is not orphaned: `BlockDrops.hpp:1301` drops 0-1 of it from a
    // chorus plant, pinned by a `static_assert` at 2129, so the chain is whole
    // the moment this row exists.
    //
    // **Precisely: this unblocks one of those two, not both.** The purpur
    // block becomes craftable outright. The end rod also wants a `CinderRod`,
    // which is a Nether drop we have no source for, so it stays unreachable
    // for a reason that has nothing to do with this row. Said plainly because
    // "two recipes fixed" is the sort of confident half-truth bug shape #16 is
    // about.
    //
    // **DORMANT BY DESIGN, ADJUDICATED 2026-08-19 - the row is correct and
    // deliberately unreachable, and it stays.** Finding 2019 (fx-recipe) first
    // closed this `wontfix`; I landed the row anyway and escalated rather than
    // silently overriding, and the scope owner has now ruled on both halves.
    //
    // **2019 was right about obtainability and wrong about the remedy.**
    // Chorus plants generate only in The End, The End is out of scope by direct
    // user ruling - *"our main focus is limited to the overworld, not nether or
    // end... (doesn't exist on timeline, leave it like that)"* - and its absence
    // from `TIMELINE.md` is the decision rather than an oversight. So
    // `ChorusPlant` is unreachable in play and is expected to stay so.
    //
    // **That does not make the row wrong, because this project already carries
    // dormant ids as an accepted category and does so deliberately.** Roughly
    // four hundred redstone ids are dormant for want of a signal engine, and
    // thirty-nine blocks are unobtainable because Silk Touch is dormant. A
    // correct table row for an unobtainable block is the house pattern; a
    // *hole* shaped exactly like a published row is not, and deleting this one
    // would make this file the single place that treats dormancy differently.
    //
    // **What would make this false**, so the next reader does not re-derive the
    // whole argument: The End arriving. That is not planned and is not on the
    // timeline. Until then the row costs one line, changes no behaviour a
    // player can observe, and keeps the recipe chain coherent on paper.
    //
    // **Do not re-file this as an unreachable feature.** It has been found and
    // resolved twice now, once from the recipe side and once from the scope
    // side, and it is bug shape #16's "negative claim rots fastest" - so the
    // date and the falsifier above are load-bearing, not decoration.
    //
    // Primary source `behavior_pack/recipes/furnace_chorus_fruit.json`:
    // `minecraft:chorus_fruit` -> `minecraft:chorus_fruit_popped`, tag
    // `[furnace]` alone - so unlike the potato it must **not** reach the
    // smoker or the campfire, and it does not: both gate on `isFood` of the
    // result, and a popped chorus fruit is not food.
    {ItemId::ChorusFruit, ItemStack{ItemId::PoppedChorusFruit, 1}},
}};

/// Whether the table maps one input to exactly one of something, asked of the
/// table **passed in** rather than of `kSmelting` directly - which is the only
/// reason the negative pin below can exist. A checker wired to the real table
/// can never be shown to say no, and this project has had eleven
/// `static_assert`s pass while pointing at the wrong texture.
constexpr bool smeltsTo(const std::array<SmeltRule, kSmeltRuleCount>& table, ItemId in,
                        ItemId out) {
    for (const SmeltRule& rule : table) {
        if (rule.input == in) {
            return rule.output.item == out && rule.output.count == 1;
        }
    }
    return false;
}

/// **Four rules that had a sibling in the table and never travelled to it.**
/// All four were found by diffing this table against the reference recipe pack
/// rather than by reading it, and that is the point: a wrong row looks exactly
/// like a right one, and a *missing* row looks like nothing at all. Plain
/// sandstone smelted smooth while red did not; four blocks cracked while
/// polished blackstone did not; the cactus gave a dye while the sea pickle did
/// not; every other food cooked while the potato did not.
///
/// Written as pairs rather than as a row count, because a count cannot say
/// *which* row is present - `kSmelting.size() == kSmeltRuleCount` is a
/// tautology, and a hand-written row count would have passed on the
/// day all four were missing and four others were duplicated.
constexpr bool everyTwinTravelled(const std::array<SmeltRule, kSmeltRuleCount>& t) {
    return smeltsTo(t, itemForBlock(BlockId::Sandstone), itemForBlock(BlockId::SmoothSandstone)) &&
           smeltsTo(t, itemForBlock(BlockId::RedSandstone),
                    itemForBlock(BlockId::SmoothRedSandstone)) &&
           smeltsTo(t, itemForBlock(BlockId::StoneBricks),
                    itemForBlock(BlockId::CrackedStoneBricks)) &&
           smeltsTo(t, itemForBlock(BlockId::PolishedBlackstoneBricks),
                    itemForBlock(BlockId::CrackedPolishedBlackstoneBricks)) &&
           smeltsTo(t, itemForBlock(BlockId::Cactus), ItemId::GreenDye) &&
           smeltsTo(t, itemForBlock(BlockId::SeaPickle), ItemId::LimeDye) &&
           smeltsTo(t, ItemId::Potato, ItemId::BakedPotato);
}

/// The negative pin: this table with the red twin taken back out. If
/// `everyTwinTravelled` ever loses the ability to say no, this stops building.
constexpr std::array<SmeltRule, kSmeltRuleCount> aTableMissingTheRedTwin() {
    std::array<SmeltRule, kSmeltRuleCount> copy = kSmelting;
    for (SmeltRule& rule : copy) {
        if (rule.input == itemForBlock(BlockId::RedSandstone)) {
            rule = SmeltRule{ItemId::None, ItemStack{}};
        }
    }
    return copy;
}

static_assert(everyTwinTravelled(kSmelting),
              "every smelt rule whose sibling is already in this table is in it too");
static_assert(!everyTwinTravelled(aTableMissingTheRedTwin()),
              "and the check above can fail, which is the only thing that makes it worth having");

/// **The silent direction of the count, which nothing above could see.**
/// `kSmeltRuleCount` sizes the array, so the two ways it can disagree with the
/// initialiser list are not symmetric. **Too many initialisers is a hard
/// compiler error** and needs no help from anyone. **Too few is silent**: the
/// trailing rows value-initialise to `SmeltRule{ItemId::None, ItemStack{}}`,
/// the table still compiles, `size()` still reports the count, and every
/// existing check here still passes - `everyTwinTravelled` asks whether named
/// pairs both appear, which a blank row at the end does not disturb.
///
/// That is `CLAUDE.md`'s aggregate-initialiser trap, and the guard below is
/// the cheap half of it. It cannot be written as `kSmelting.size() ==
/// kSmeltRuleCount`, which is the vacuous form warned about above: the array
/// is *sized by* that constant, so the comparison is one side of a derivation
/// against itself and passes no matter how wrong the table is.
constexpr bool noRuleIsBlank(const std::array<SmeltRule, kSmeltRuleCount>& t) {
    for (const SmeltRule& rule : t) {
        // `output.empty()` rather than `item == None || count <= 0` spelled out
        // again: that is `ItemStack`'s own definition of empty, and restating it
        // here would be a second copy free to drift from the first.
        if (rule.input == ItemId::None || rule.output.empty()) {
            return false;
        }
    }
    return true;
}

/// **Its control is not new machinery, and that is the point.**
/// `aTableMissingTheRedTwin()` already plants exactly the fault this checker
/// must catch - it overwrites one row with `SmeltRule{ItemId::None,
/// ItemStack{}}`, which is byte-for-byte what an under-filled initialiser
/// leaves behind. So the negative below is a *real* planted fault rather than
/// a second assertion of the same thing, and it differs from the positive in
/// exactly one variable: one row blanked.
static_assert(noRuleIsBlank(kSmelting),
              "a smelt rule is blank - kSmeltRuleCount is larger than the number of rows "
              "actually written, and the trailing rows value-initialised to None");
static_assert(!noRuleIsBlank(aTableMissingTheRedTwin()),
              "and that check can fail on a table with one blanked row, which is the only "
              "thing that makes it worth having");

/// The potato reaches the smoker and the campfire because both gate on `isFood`
/// of the **result** rather than on a second list of their own, so the one row
/// above is the whole change. The second half is what keeps that from being an
/// accident: the three non-food rows added beside it must *not* pass this, or a
/// smoker would start cooking sandstone.
static_assert(isFood(ItemId::BakedPotato) &&
                  !isFood(itemForBlock(BlockId::SmoothRedSandstone)) &&
                  !isFood(ItemId::LimeDye) &&
                  !isFood(itemForBlock(BlockId::CrackedPolishedBlackstoneBricks)),
              "the potato reaches the smoker and the campfire; the other three do not");

struct FuelRule {
    ItemId item;
    float seconds;
};

/// The reference measures fuel in items smelted; at ten seconds each that makes
/// charcoal worth eight, wood one and a half, and a stick a half.
///
/// **Every number here is one item in a plain furnace, and none of it is scaled
/// for a smoker or a blast furnace.** Those cook twice as fast *and* burn their
/// fuel twice as fast, so a fuel is worth the same number of items in all three
/// and only the wall clock differs - which is why the doubling is a single
/// multiplier on the whole tick, applied outside this file where `Main.cpp`
/// passes `deltaSeconds * cookSpeed(present)` into `tickFurnace`. Halving a
/// burn time here to "match" a smoker would double that smoker's yield, and
/// scaling it the other way would quarter it; `Smelting.hpp`'s `cookSeconds` is
/// where the wall-clock answer lives.
///
/// **Wood is answered by shape below rather than listed here**, because there
/// are now eleven planks, eleven fences and dozens of wooden stairs, slabs and
/// gates - and the reference burns every one of them.
///
/// **The coal block is worth ten coal, not nine**, and that is the reference's
/// own number rather than an arithmetic guess: wiki `[[Block of Coal]]` gives
/// it 16000 ticks - 800 seconds, 80 items - and says so outright, "ten times
/// the duration of a single piece of coal and 1 1/9 times as efficient as nine
/// individual pieces". Without this row the block was a **pure sink**: nine
/// coal went in, a block came out, and nothing in the game would burn it. The
/// unpack recipe added in `Recipe.cpp` is the other half of the same hole.
///
/// **The nine plants below are the two things a player ends up with hundreds of
/// spares of**, and the furnace refused both. Wiki
/// `[[Template:Smelting table]]`: any sapling 5 s, azalea and flowering azalea
/// 5 s, bamboo the plant 2.5 s. Named one row each rather than answered by a
/// range, because nothing about `OakSapling..DarkOakSapling` being contiguous is
/// checkable by eye and a seventh sapling would sit wherever the enum has room.
///
/// **Wool and wool carpet are not here on purpose.** The reference's fuel table
/// marks both `{{only|je}}` and each says so on its own page too, and Bedrock is
/// what this project follows. Leaving them out of this list was never enough on
/// its own - the sixteen carpets reached the furnace through `blockFuelSeconds`
/// instead, and the assert beside that function is what now holds them out.
///
/// **What this table is for, now that a second one exists.** A row here is a
/// *named one-off*: one id, one published number, and nothing about it that a
/// family predicate could get right. `blockFuelSeconds` below answers the
/// families. The block of coal has always sat here for that reason and the
/// three blocks below join it - a dried kelp block, scaffolding and a dead bush
/// are each the only member of their own row in the reference's table.
///
/// **The three odd numbers, each from wiki `[[Template:Smelting table]]`.**
///
/// * *Dried Kelp Block - 200 s, 4000 ticks.* This is the reference's renewable
///   fuel and the one thing on the list a player can farm without a tree, and
///   at nought it was not merely missing a row, it was a **sink**: the block
///   costs nine smelting operations to make and returned none of them.
/// * *Scaffolding - 2.5 s, 50 ticks*, the same as the bamboo it is cut from.
/// * *Dead Bush - 5 s, 100 ticks*, beside the saplings.
///
/// **And the seven wooden tools and tackle, all of which this game already has
/// items for.** Three of them are a Java/Bedrock split and the Bedrock number
/// is the one taken, per the project's reference edition:
///
/// * *Wooden pickaxe, axe, shovel, hoe and sword - 10 s, 200 ticks*, both
///   editions.
/// * *Fishing rod - 15 s, 300 ticks*, both editions.
/// * *Bow and crossbow - Java 15 s, **Bedrock 10 s**.* The table gives each of
///   them two rows, tagged `{{only|je}}` and `{{only|be}}`.
/// * *Bowl - Java 5 s, **Bedrock 10 s**.* Same two-row shape.
///
/// **The lava bucket is here and the bucket comes back.** 1000 s, 20000 ticks,
/// far and away the longest row on the reference's table, and its note is the
/// reason this looked unsafe to add: "If a lava bucket is used as fuel, an empty
/// bucket remains in the fuel slot." That half is already built - `Furnace.cpp`
/// asks `fuelRemainder` before it decrements and puts the empty bucket back, and
/// asserts that lava returns one. Without this row that whole path is
/// unreachable: `fuelBurnSeconds` answers nought, the furnace never lights, and
/// a correct piece of machinery sits behind a table that never sends it
/// anything.
///
/// **The cinder rod is our name for the reference's blaze rod, and it is fuel
/// even though nothing drops one yet.** 120 s, 2400 ticks, 12 items - stated
/// twice over, by wiki `[[Template:Smelting table]]` and in prose on
/// `[[Blaze Rod]]` itself ("When used in a furnace, a blaze rod lasts 120
/// seconds (12 items)"), neither carrying an edition marker. Written now
/// because the row costs one line and the alternative is finding it missing on
/// the day a creature finally drops one, with the drop, the item and the fuel
/// all new at once and no way to tell which of the three is wrong.
constexpr std::array<FuelRule, 27> kFuels{{
    {ItemId::Coal, 80.0f},
    {ItemId::Charcoal, 80.0f},
    {itemForBlock(BlockId::CoalBlock), 800.0f},
    {ItemId::Stick, 5.0f},
    {itemForBlock(BlockId::OakSapling), 5.0f},
    {itemForBlock(BlockId::SpruceSapling), 5.0f},
    {itemForBlock(BlockId::BirchSapling), 5.0f},
    {itemForBlock(BlockId::JungleSapling), 5.0f},
    {itemForBlock(BlockId::AcaciaSapling), 5.0f},
    {itemForBlock(BlockId::DarkOakSapling), 5.0f},
    {itemForBlock(BlockId::Azalea), 5.0f},
    {itemForBlock(BlockId::FloweringAzalea), 5.0f},
    {itemForBlock(BlockId::Bamboo), 2.5f},
    {itemForBlock(BlockId::DriedKelpBlock), 200.0f},
    {itemForBlock(BlockId::Scaffolding), 2.5f},
    {itemForBlock(BlockId::DeadBush), 5.0f},
    {ItemId::WoodenPickaxe, 10.0f},
    {ItemId::WoodenAxe, 10.0f},
    {ItemId::WoodenShovel, 10.0f},
    {ItemId::WoodenHoe, 10.0f},
    {ItemId::WoodenSword, 10.0f},
    {ItemId::FishingRod, 15.0f},
    {ItemId::Bow, 10.0f},
    {ItemId::Crossbow, 10.0f},
    {ItemId::Bowl, 10.0f},
    {ItemId::LavaBucket, 1000.0f},
    {ItemId::CinderRod, 120.0f},
}};

/// Where a fuel sits in `kFuels`, so a proof about two rows names the rows
/// rather than counting to them.
constexpr int fuelIndexOf(ItemId item) {
    for (std::size_t i = 0; i < kFuels.size(); ++i) {
        if (kFuels[i].item == item) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

constexpr int kCoalFuel = fuelIndexOf(ItemId::Coal);
constexpr int kCoalBlockFuel = fuelIndexOf(itemForBlock(BlockId::CoalBlock));

static_assert(kCoalFuel >= 0 && kCoalBlockFuel >= 0,
              "both halves of the coal ratio must still be in kFuels for the next assert to be "
              "about anything");
/// **Derive the index, then assert the identity.** This read `kFuels[2]` and
/// `kFuels[0]` outright, which was a proof by position: insert a fuel row above
/// index 2 and it would have silently retargeted a different pair and gone on
/// passing. Same fix, and same reason, as the non-wood switch families in
/// `Recipe.cpp`.
static_assert(kFuels[kCoalBlockFuel].seconds == kFuels[kCoalFuel].seconds * 10.0f,
              "the coal block's burn time is the reference's stated ten coal, not the nine it is "
              "crafted from - change either number and this is what says the other moved");

// **The renewable fuel, and the only row on this table with a progression
// attached to it.** A dried kelp block is nine dried kelp, each of which is one
// smelting operation, so at anything under 90 s it is a net loss and at nought
// it is a pure sink - a player who farms kelp to stop needing coal gets nothing
// back. Wiki `[[Template:Smelting table]]` gives it 4000 ticks and footnotes
// exactly that arithmetic: "Requires 9 smelting operations to obtain, so net
// output is 11 operations".
//
// > Fails if: delete the dried kelp block row, or retype 200 as the 20
// > *operations* the column beside it holds. Both are one edit and neither
// > shows up anywhere else in this file.
constexpr int kDriedKelpFuel = fuelIndexOf(itemForBlock(BlockId::DriedKelpBlock));
static_assert(kDriedKelpFuel >= 0 && kFuels[kDriedKelpFuel].seconds == 200.0f,
              "the dried kelp block is the reference's renewable fuel at 4000 ticks, and it costs "
              "nine smelting operations to make - without the row it is a sink");

// **Scaffolding is bamboo, and burns for exactly what bamboo burns for.** Wiki
// `[[Template:Smelting table]]` gives both 50 ticks. Written as a comparison
// rather than as two 2.5s so the pair cannot drift.
//
// > Fails if: give scaffolding the 15 s that everything else made of a plant
// > block gets, or move bamboo off 2.5 s without moving this.
constexpr int kBambooFuel = fuelIndexOf(itemForBlock(BlockId::Bamboo));
constexpr int kScaffoldingFuel = fuelIndexOf(itemForBlock(BlockId::Scaffolding));
static_assert(kBambooFuel >= 0 && kScaffoldingFuel >= 0 &&
                  kFuels[kScaffoldingFuel].seconds == kFuels[kBambooFuel].seconds,
              "scaffolding is cut from bamboo and the reference burns the two for the same 50 "
              "ticks - one number, asked twice");

// **Bow, crossbow and bowl are the three rows where Java and Bedrock disagree
// and Bedrock is shorter**, which is the direction that makes a wrong copy
// silently generous rather than obviously broken. Wiki
// `[[Template:Smelting table]]` gives each of them two rows: bow and crossbow
// `{{only|je}}` 300 ticks against `{{only|be}}` 200, and the bowl
// `{{only|je}}` 100 ticks against `{{only|be}}` 200 - the one row where Bedrock
// is the *longer* of the two.
//
// > Fails if: take the first number off any of those three rows. The Java one
// > is written first in every case, which is exactly how the wool and carpet
// > divergence got in.
constexpr int kBowFuel = fuelIndexOf(ItemId::Bow);
constexpr int kCrossbowFuel = fuelIndexOf(ItemId::Crossbow);
constexpr int kBowlFuel = fuelIndexOf(ItemId::Bowl);
static_assert(kBowFuel >= 0 && kCrossbowFuel >= 0 && kBowlFuel >= 0 &&
                  kFuels[kBowFuel].seconds == 10.0f && kFuels[kCrossbowFuel].seconds == 10.0f &&
                  kFuels[kBowlFuel].seconds == 10.0f,
              "this game follows Bedrock, where a bow, a crossbow and a bowl are all 200 ticks - "
              "Java's are 300, 300 and 100 and none of the three is right here");

// **The two halves of the lava bucket, asserted together because either one
// alone is a bug.** A row without a remainder destroys the player's bucket; a
// remainder without a row is a branch in `Furnace.cpp` that nothing can ever
// reach, which is the state this was found in - the container return and its own
// assert were written and live, and the fuel table had no lava in it, so the
// furnace simply refused the longest-burning item in the game.
//
// > Fails if: delete the lava bucket row. `fuelIndexOf` returns -1 and the first
// > clause goes, which is the point - the `fuelRemainder` half is in
// > `Smelting.hpp` and would keep passing its own assert in `Furnace.cpp` with
// > nothing to remain from.
constexpr int kLavaBucketFuel = fuelIndexOf(ItemId::LavaBucket);
static_assert(kLavaBucketFuel >= 0 && kFuels[kLavaBucketFuel].seconds == 1000.0f &&
                  fuelRemainder(ItemId::LavaBucket) == ItemId::Bucket,
              "a lava bucket is 20000 ticks and leaves the bucket behind - the burn time and the "
              "container that comes back are one feature and neither is any use alone");

// **The one row in this table that no amount of playing can check.** Nothing in
// the game drops a cinder rod yet, so an absent or wrong row here is invisible
// to the playtester who found every other fuel bug in this file - it would
// surface on the day a creature finally drops one, alongside a new drop, a new
// item and a new recipe, with four suspects and no way to tell them apart.
// Wiki `[[Blaze Rod]]` states it in prose - "a blaze rod lasts 120 seconds (12
// items)" - and `[[Template:Smelting table]]` gives the same 2400 ticks with no
// edition marker on either.
//
// > Fails if: delete the cinder rod row, or retime it. `fuelIndexOf` returns -1
// > and the first clause goes. This is the whole of its defence, which is the
// > point of writing it down.
constexpr int kCinderRodFuel = fuelIndexOf(ItemId::CinderRod);
static_assert(kCinderRodFuel >= 0 && kFuels[kCinderRodFuel].seconds == 120.0f,
              "a cinder rod is 2400 ticks, twelve items - the reference's fuel between coal and a "
              "block of it, and the only row here that no playtest can miss the absence of");

/// Wood that chars into charcoal: the reference's charcoal input, which wiki
/// `[[Charcoal]]` states as **"Any Log, Any Stripped Log, Any Wood, Any
/// Stripped Wood"** and nothing else.
///
/// **The published recipe pack has 29 wood charcoal rows and not one of them is
/// cherry or pale oak - and that is a hole in the source, not a rule.** Audited
/// 2026-08-19: `behavior_pack/recipes` carries `furnace_oak_wood`,
/// `furnace_log_oak`, `furnace_stripped_oak_log`, `furnace_stripped_oak_wood`
/// and the matching set for spruce, birch, jungle, acacia, dark oak and
/// mangrove - and for cherry and pale oak it publishes **only**
/// `furnace_cherry_leaves` and `furnace_pale_oak_leaves`. That is a listed
/// result rather than a silence: a content search of the whole recipe folder
/// for `cherry_log` returns six files and none is a furnace recipe.
///
/// **Do not narrow this predicate to match it.** Cherry logs burn to charcoal
/// in the game itself; the family predicate is the rule and the pack's
/// per-wood files are a snapshot that has not kept up. This note exists because
/// "the reference does not publish it" is exactly the argument that would
/// delete two working woods, and this project has been misled about an edition
/// split four times in one night already.
///
/// **`isNetherWoodBlock` is `Block.hpp`'s and is asked, not copied.** This file
/// carried its own ten-id `isNetherWood` beside it - the same list, forwarded
/// the same way, with nothing comparing the two. Wiki `[[Log]]` states both
/// halves of why it matters outright: "Stems cannot be smelted into charcoal"
/// and "Logs, but not stems, can be used as a fuel in furnaces", so the list
/// that decides whether a crimson stem burns in a fire is the same list that
/// decides whether it chars in a furnace. A rule that exists in only one of the
/// two places that need it is exactly what cost this project its
/// water-versus-lava afternoon.
///
/// **`isLogBlock` and `isPlanksBlock` both include them**, so asking either one
/// directly - which both ends of this file did - quietly made a crimson stem
/// into charcoal and a crimson fence into fuel.
///
/// **Two families the fuel test below takes and this one must not**, and until
/// now one predicate answered both questions - which is this project's
/// widened-family bug shape wearing its usual hat.
///
/// * *Planks are fuel and are not charcoal input.* They were only ever in here
///   because the fuel half needs them.
/// * *A block of bamboo is fuel and is not charcoal input either.* Wiki
///   `[[Block of Bamboo]]` says so outright - "Unlike logs, blocks of bamboo
///   cannot be smelted into charcoal" - and the reference tracked it as
///   MC-257428 and closed it Works As Intended. `isLogBlock` includes both
///   bamboo blocks, so they arrived here through the family predicate and made
///   bamboo, which regrows faster than any tree, an infinite charcoal farm.
///
/// The bark blocks are the other half of the same correction: `isLogBlock` does
/// not cover them, so twenty ids that the reference chars and burns did
/// neither.
constexpr bool charsIntoCharcoal(BlockId block) {
    return (isLogBlock(block) || isBarkBlock(block) || isStrippedBarkBlock(block)) &&
           !isNetherWoodBlock(block) && block != BlockId::BambooBlock &&
           block != BlockId::StrippedBambooBlock;
}

/// Wood that burns in a furnace at the reference's flat 15 s - one and a half
/// items - which is the charcoal set plus the two families it excludes. Wiki
/// `[[Template:Smelting table]]` gives logs, stripped logs, wood, stripped
/// wood, planks and both bamboo blocks the same 300 ticks.
constexpr bool burnsAsFuel(BlockId block) {
    return (isLogBlock(block) || isPlanksBlock(block) || isBarkBlock(block) ||
            isStrippedBarkBlock(block)) &&
           !isNetherWoodBlock(block);
}

static_assert(isLogBlock(BlockId::CrimsonStem) && isLogBlock(BlockId::WarpedStem) &&
                  !charsIntoCharcoal(BlockId::CrimsonStem) &&
                  !charsIntoCharcoal(BlockId::WarpedStem) && !burnsAsFuel(BlockId::CrimsonStem) &&
                  !burnsAsFuel(BlockId::StrippedCrimsonStem) &&
                  !burnsAsFuel(BlockId::CrimsonPlanks) && !burnsAsFuel(BlockId::WarpedPlanks) &&
                  !burnsAsFuel(BlockId::CrimsonHyphae) && !burnsAsFuel(BlockId::WarpedHyphae) &&
                  !burnsAsFuel(BlockId::StrippedCrimsonHyphae) &&
                  !burnsAsFuel(BlockId::StrippedWarpedHyphae) && charsIntoCharcoal(BlockId::Log) &&
                  charsIntoCharcoal(BlockId::StrippedOakLog) && burnsAsFuel(BlockId::Planks),
              "the nether woods must be the only wood that neither chars into charcoal nor burns "
              "as fuel - delete the isNetherWoodBlock guard and a crimson stem makes charcoal, "
              "which the reference refuses outright");

/// **The twenty bark blocks char and burn exactly as the logs they are made
/// of.** Wiki `[[Charcoal]]` names "Any Wood, Any Stripped Wood" beside the
/// logs, and the fuel table gives Overworld wood the same 15 s. Delete either
/// bark clause from `charsIntoCharcoal` and this is the line that fires.
static_assert(charsIntoCharcoal(BlockId::OakWood) && burnsAsFuel(BlockId::OakWood) &&
                  charsIntoCharcoal(BlockId::DarkOakWood) &&
                  charsIntoCharcoal(BlockId::StrippedOakWood) &&
                  charsIntoCharcoal(BlockId::StrippedDarkOakWood) &&
                  burnsAsFuel(BlockId::StrippedDarkOakWood),
              "a bark block is made of four logs and the reference treats it as one - neither "
              "isLogBlock nor isPlanksBlock covers the run, so it needs naming here");

/// **The two families that burn and must not char.** Fold these two predicates
/// back into the single `charsAndBurns` they were written as - the one edit -
/// and this is what fires: a block of bamboo would make charcoal, and so would
/// a plank.
static_assert(burnsAsFuel(BlockId::BambooBlock) && !charsIntoCharcoal(BlockId::BambooBlock) &&
                  burnsAsFuel(BlockId::StrippedBambooBlock) &&
                  !charsIntoCharcoal(BlockId::StrippedBambooBlock) &&
                  burnsAsFuel(BlockId::Planks) && !charsIntoCharcoal(BlockId::Planks) &&
                  burnsAsFuel(BlockId::BambooMosaic) && !charsIntoCharcoal(BlockId::BambooMosaic),
              "the reference's charcoal input is any log, stripped log, wood or stripped wood and "
              "nothing else - planks and both bamboo blocks are fuel only");

/// The four thin shapes the reference burns and `burnsAsFuel` does not reach,
/// each at its **own** published time rather than at the flat 15 s the general
/// branch hands everything else.
///
/// > **This function exists because of a live regression, not a tidy-up.** The
/// > general branch used to ask `isShapedBlock(block) && isFlammable(block)`,
/// > and that worked for as long as `isFlammable` happened to mean *"is this
/// > wood?"*. It has since been narrowed to what it actually says - *"does fire
/// > spread on this?"* - and a wooden button, a wooden pressure plate, a sign
/// > and a banner all sit in the reference's **non-flammable** table while
/// > remaining perfectly good fuel. 524 ids stopped burning in a furnace in a
/// > single diff, with nothing to catch it: two other consumers of that
/// > predicate (`soundMaterialFor` and `blastResistance`) were moved off it in
/// > the same round, and this third one was missed because it reads as a
/// > sentence about wood rather than about fire. That branch now asks
/// > `shapedParent` instead, so nothing about fuel in this file reads it.
///
/// Burn times are wiki `[[Template:Smelting table]]`, which lists all four
/// separately and pointedly does **not** give them one shared number:
///
/// * Overworld Wooden Button - **5 s**, 100 ticks.
/// * Overworld Wooden Pressure Plate - **15 s**, 300 ticks.
/// * Overworld Sign - **10 s**, 200 ticks, and Overworld Hanging Sign the same.
/// * Any Banner - **15 s**, 300 ticks. Bedrock added it in 1.2.0 beside the
///   jukebox; it is wool and a stick rather than planks, which is why it is
///   answered before the plank test below.
constexpr float thinWoodFuelSeconds(BlockId block) {
    // The nether woods burn in none of these families for the same reason they
    // burn in none of the others, and the guard lives here rather than at the
    // call site so this function is a complete answer on its own - the wiki
    // writes "Overworld" in front of three of the four rows above.
    if (isNetherWoodBlock(block)) {
        return 0.0f;
    }
    // A banner is six wool and a stick, so it is the one of the four not cut
    // from planks and has to be answered before the plank test.
    if (isBanner(block)) {
        return 15.0f;
    }
    // The other three are the same shapes in rock and metal as well - a stone
    // button, a stone plate and the two weighted plates are 60 of these ids and
    // the reference burns none of them. `shapedParent` already knows what each
    // was cut from, and asking it is what keeps this from becoming a list of
    // families that a twelfth wood would have to be added to.
    if (!isPlanksBlock(shapedParent(block))) {
        return 0.0f;
    }
    if (isButton(block)) {
        return 5.0f;
    }
    if (isPressurePlate(block)) {
        return 15.0f;
    }
    if (isSign(block) || isHangingSign(block)) {
        return 10.0f;
    }
    return 0.0f;
}

/// **The crafted wooden things the reference burns that no shape predicate
/// reaches**, all at the same flat 15 s as the planks they are made of. Wiki
/// `[[Template:Smelting table]]` gives every one of them 300 ticks: chest,
/// trapped chest, barrel, ladder, composter, note block, jukebox, daylight
/// detector, bookshelf, chiseled bookshelf, lectern, crafting table,
/// cartography table, fletching table, smithing table and loom.
///
/// > **None of these is `isShapedBlock`**, so the general branch below has never
/// > seen one, and none is `isPlanksBlock` either - a bookshelf is its own id,
/// > not a cut of oak. They were nought seconds each from the day the furnace
/// > was written, which is 132 ids and the reason a player with a chest, a
/// > ladder and no coal could not smelt.
///
/// **The count is 132, not the 564 this doc carried until 2026-08-19.** 564 is
/// the union of *both* branches added that day - `openingFuelSeconds > 0` for
/// 432 ids plus `burnsAsCraftedWood` for 132, two disjoint sets - so the claim
/// at `blockFuelSeconds` ("564 added, 0 removed, 0 retimed") is correct and
/// only this per-branch copy of it was not. Measured over all 3269 ids. A
/// union pasted into a per-branch doc is a 4.3x mis-attribution sitting in the
/// one place a reader would look to decide whether this branch earns its keep.
///
/// **`isChest` is wider than "a wooden box" and that is the trap here.** It also
/// covers the ender chest and the seventeen stowboxes, because everything
/// downstream wants one predicate for "opens the twenty-seven slot panel" - and
/// its own comment in `Block.hpp` says so outright. An ender chest is obsidian
/// and a stowbox is a shell, and the reference burns neither. Subtracting the
/// two by name is deliberate: it means a *future* widening of `isChest` shows up
/// here as a new fuel rather than being silently correct, and the assert below
/// is what catches it. The loot chest is left in - it is a plain chest that has
/// not rolled yet, and it can never be an item anyway.
constexpr bool burnsAsCraftedWood(BlockId block) {
    if (isChest(block)) {
        return !isEnderChest(block) && !isStowbox(block);
    }
    return isLadder(block) || isComposter(block) || isNoteBlock(block) ||
           isDaylightDetector(block) || block == BlockId::Bookshelf ||
           block == BlockId::ChiseledBookshelf || block == BlockId::Lectern ||
           block == BlockId::CraftingTable || block == BlockId::CartographyTable ||
           block == BlockId::FletchingTable || block == BlockId::SmithingTable ||
           block == BlockId::Loom || block == BlockId::Jukebox;
}

/// Two names compared character by character, so a family can be pinned by what
/// it is called rather than by where it sits. `kDoorFamilies` holds a `name` and
/// no parent block, so this is the only anchor available that is not another
/// spelling of the index being anchored.
constexpr bool sameName(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/// **Doors and trapdoors, which are their own shape system and reach none of the
/// tests above.** `shapedParent` is the identity for both - a door's parent is
/// the door, not the planks - so `isNetherWoodBlock` answers false for a crimson
/// door and every plank-parent test misses all 576 ids.
///
/// The two published times differ and the wiki writes "Overworld" in front of
/// both rows:
///
/// * Overworld Wooden Door - **10 s**, 200 ticks.
/// * Overworld Wooden Trapdoor - **15 s**, 300 ticks.
///
/// **The three families to leave out are the same three in both tables**, and
/// they are reached by index because `OpeningFamily` carries a name, two texture
/// layers and a `metal` flag but no parent block. Crimson is eight, warped is
/// nine and iron is eleven in `kDoorFamilies` and `kTrapdoorFamilies` alike -
/// asserted by name below, because comparing the layer numbers would be
/// comparing the index against itself.
constexpr bool openingIsOverworldWood(int family) {
    return family != 8 && family != 9 && family != 11;
}

constexpr float openingFuelSeconds(BlockId block) {
    if (isDoor(block)) {
        return openingIsOverworldWood(doorFamily(block)) ? 10.0f : 0.0f;
    }
    if (isTrapdoor(block)) {
        return openingIsOverworldWood(trapdoorFamily(block)) ? 15.0f : 0.0f;
    }
    return 0.0f;
}

// The three families `openingIsOverworldWood` names by number, pinned by the
// only thing about them that is not the number. `kDoorFamilies` is `Block.hpp`'s
// and is declared in `kWoods` order, so inserting a wood ahead of crimson would
// slide all three.
//
// > Fails if: add a twelfth wood to `kWoods` and to the two opening tables. That
// > is the single edit, and without this line it would quietly make a crimson
// > door fuel and stop a bamboo one being fuel, with nothing on screen but a
// > furnace that had changed its mind.
static_assert(sameName(kDoorFamilies[8].name, "Crimson Door") &&
                  sameName(kDoorFamilies[9].name, "Warped Door") &&
                  sameName(kDoorFamilies[11].name, "Iron Door") &&
                  sameName(kTrapdoorFamilies[8].name, "Crimson Trapdoor") &&
                  sameName(kTrapdoorFamilies[9].name, "Warped Trapdoor") &&
                  sameName(kTrapdoorFamilies[11].name, "Iron Trapdoor"),
              "the two families the reference refuses as fuel are eight and nine in both opening "
              "tables and the metal one is eleven - insert a wood and every index here moves");

/// **The whole answer for a block put in a fuel slot, in one `constexpr`
/// function.** `fuelBurnSeconds` reads it after the `kFuels` lookup, so between
/// them the two own every answer: `kFuels` the named one-offs, this the
/// families. Every assert below evaluates the expression a furnace really runs
/// rather than a shorter copy of it that could stay true while the live path
/// broke.
constexpr float blockFuelSeconds(BlockId block) {
    const float thin = thinWoodFuelSeconds(block);
    if (thin > 0.0f) {
        return thin;
    }
    // Ordering here is not cosmetic and both of the new branches were checked
    // against the old answers before they went in: a door and a trapdoor are
    // reached by no other test in this function, and `burnsAsCraftedWood` feeds
    // the same 15 s the branch below hands out, so neither can retime anything
    // that already burned. Measured over all 3269 ids: 564 added, 0 removed,
    // 0 retimed.
    const float opening = openingFuelSeconds(block);
    if (opening > 0.0f) {
        return opening;
    }
    // Everything else wooden burns for the reference's flat 15 s, except the
    // nether woods. A cut shape is asked what it was cut **from** rather than
    // whether fire spreads on it, so a birch fence gate and an oak stair are
    // fuel without being named - and Bedrock gives a wooden slab the same 15 s
    // as its planks rather than Java's 7.5, which the wiki footnotes as
    // MCPE-94368 and which is why there is no second number here.
    //
    // > **This branch used to ask `isFlammable`, and that is exactly what broke
    // > it.** That predicate reads as a sentence about wood and is actually a
    // > sentence about fire; when it was narrowed to what it says, 524 ids fell
    // > out of the furnace overnight. `shapedParent` answers the question this
    // > branch is really asking, it cannot drift with the fire tables, and it is
    // > the same call `thinWoodFuelSeconds` above already makes - so the fuel
    // > path in this file now depends on `isFlammable` nowhere at all.
    //
    // The `!isNetherWoodBlock` is no longer belt and braces either: crimson and
    // warped planks **are** planks, so it is the only thing standing between a
    // warped fence gate and the furnace. It is `Block.hpp`'s predicate rather
    // than a copy of its ten ids, which is what makes the fire tables and the
    // furnace state one rule instead of two that may drift.
    if (burnsAsFuel(block) || burnsAsCraftedWood(block) ||
        (isShapedBlock(block) && isPlanksBlock(shapedParent(block)) &&
         !isNetherWoodBlock(block))) {
        return 15.0f;
    }
    return 0.0f;
}

/// The block a *placed* block becomes when broken and put back: the item it
/// drops, and then the block that item sets down. A shaped family spends most of
/// its ids on placement states and only the canonical one is ever an item, so an
/// assert that names a run's first id has to travel this before it is about
/// anything the furnace evaluates.
///
/// Exposed separately from the seconds so a negative assert can say *which*
/// block it found nothing for. `placedFuelSeconds(x) == 0` is true of air, of a
/// typo and of an id that does not round-trip, which would be an assert that
/// cannot fail.
constexpr BlockId placedBlock(BlockId block) {
    return blockForItem(itemForBlock(shapedCanonical(block)));
}

constexpr float placedFuelSeconds(BlockId block) { return blockFuelSeconds(placedBlock(block)); }

// **The four families the `isFlammable` narrowing silently took out of the
// furnace**, each at the number the reference publishes for it rather than at
// the flat 15 s the general branch hands everything else.
//
// > Fails if: delete the `thinWoodFuelSeconds` call from `blockFuelSeconds`.
// > All five drop straight to 0 s, because `isFlammable` answers false for
// > every one of these shapes and the `isShapedBlock && isFlammable` branch is
// > the only other thing that could have caught them. That is precisely the
// > state this file was found in.
static_assert(placedFuelSeconds(BlockId::ButtonRunFirst) == 5.0f &&
                  placedFuelSeconds(BlockId::PressurePlateRunFirst) == 15.0f &&
                  placedFuelSeconds(BlockId::SignRunFirst) == 10.0f &&
                  placedFuelSeconds(BlockId::HangingSignRunFirst) == 10.0f &&
                  placedFuelSeconds(BlockId::BannerRunFirst) == 15.0f,
              "a wooden button, plate, sign, hanging sign and banner are all furnace fuel at four "
              "different published times - 100, 300, 200 and 300 ticks");

// The three non-wooden ids the two asserts below name by family index. Block.hpp
// owns those tables, so this is what says an inserted family moved them.
static_assert(kButtonFamilies[11].parent == BlockId::Stone &&
                  kPressurePlateFamilies[11].parent == BlockId::Stone &&
                  kPressurePlateFamilies[13].parent == BlockId::IronBlock &&
                  kButtonFamilies[8].parent == BlockId::CrimsonPlanks,
              "the fuel asserts under this one reach the stone button, the stone plate, the heavy "
              "weighted plate and the crimson button by index - put a family in front of any of "
              "them and this fires first");

// **The same four shapes cut from something that is not wood.** A stone button,
// a stone plate and the two weighted plates are rock, gold and iron.
//
// > Fails if: drop the `isPlanksBlock(shapedParent(block))` test from
// > `thinWoodFuelSeconds` and let the family predicates answer alone - a stone
// > button becomes 5 s of fuel and an iron plate 15 s, which is a furnace
// > burning metal.
static_assert(placedFuelSeconds(buttonAt(11, 0, false)) == 0.0f &&
                  placedFuelSeconds(pressurePlateAt(11, 0)) == 0.0f &&
                  placedFuelSeconds(pressurePlateAt(13, 0)) == 0.0f,
              "a button and a plate are shapes, not a material - the stone and weighted ones burn "
              "in no edition");

// **And the same four cut from nether wood**, which the reference refuses as
// fuel in every form.
//
// > Fails if: delete the `isNetherWoodBlock` early-out from
// > `thinWoodFuelSeconds`. A crimson button becomes 5 s of fuel. It cannot be
// > satisfied by the general branch's own `!isNetherWoodBlock` instead, because
// > that branch is never reached for a button once the thin one answers.
static_assert(placedFuelSeconds(buttonAt(8, 0, false)) == 0.0f,
              "the wiki writes \"Overworld\" in front of the button, the plate and both sign "
              "rows - a crimson one burns in neither edition");

// **The families that did *not* regress, kept here so a fix to the four above
// cannot quietly cost them.** A fence gate and a stair still reach the general
// branch through `isFlammable`'s `shapedParent` forwarding, and a plank is fuel
// without any shape at all.
//
// > Fails if: make `thinWoodFuelSeconds` return a real number for every shaped
// > block rather than for the four families it names - the gate and the stair
// > would take the thin path and come away with the wrong time, or with none.
static_assert(blockFuelSeconds(BlockId::Planks) == 15.0f &&
                  thinWoodFuelSeconds(BlockId::Planks) == 0.0f &&
                  thinWoodFuelSeconds(BlockId::Stone) == 0.0f,
              "planks are the general branch's own answer and must not be claimed by the thin "
              "one, and rock is fuel in neither");

// **Wool and wool carpet burn in Java and in no other edition, and Bedrock is
// what this project follows.** Each says so on its own page rather than only in
// the fuel table: wiki `[[Carpet]]` under its own *Fuel* heading - *"Carpet can
// be used as a fuel in furnaces, smelting 0.335 items per carpet item"*, tagged
// `{{only|java}}` - and wiki `[[Wool]]` - *"{{IN|Java}}, wool can be used as
// fuel in furnaces and their variants, smelting 0.5 items per wool block"*.
// `[[Template:Smelting table]]` marks both rows `{{only|je}}` to match.
//
// `kFuels` deliberately left wool out from the start and the comment there says
// so. **The sixteen carpets were arriving anyway**, through a branch nobody had
// re-read: a carpet is `isShapedBlock`, and `isFlammable` forwarded it to the
// wool it is woven from. Asking `isPlanksBlock(shapedParent(block))` instead
// takes them out as a consequence of the branch finally saying what it means,
// rather than as a special case bolted on the end.
//
// > Fails if: put `isFlammable(block)` back in place of the plank test in
// > `blockFuelSeconds`. All sixteen carpets become fuel again - and at 15 s
// > rather than even Java's 3.35, which is the second thing that was wrong with
// > them.
static_assert(placedFuelSeconds(BlockId::CarpetRunFirst) == 0.0f &&
                  blockFuelSeconds(BlockId::WhiteWool) == 0.0f,
              "wool and wool carpet are Java-only fuel and this game follows Bedrock - both of "
              "their own pages say so under a Fuel heading");

// **A banner is the one of the five thin families that is not wood**, so the
// general branch above can no longer reach it at all - `shapedParent` of a
// banner is the wool it is sewn from, and that has just stopped being fuel.
// Its 15 s now rests entirely on `thinWoodFuelSeconds` naming it.
//
// > Fails if: delete the `isBanner` clause from `thinWoodFuelSeconds` and trust
// > the general branch to catch it the way it used to. It cannot: the carpet
// > fix above removed the only route wool had into this function, so a banner
// > would read 0 s while the sixteen carpets it shares a material with also
// > read 0 s - one right, one wrong, and nothing to tell them apart.
static_assert(placedFuelSeconds(BlockId::BannerRunFirst) == 15.0f &&
                  isWoolBlock(shapedParent(BlockId::BannerRunFirst)),
              "Bedrock added the banner as fuel in 1.2.0 beside the jukebox, and it is the only "
              "wool-parented thing in this file that burns");

// **The sixteen crafted wooden blocks, every one of them at the reference's
// 300 ticks.** None is a shaped block and none is a plank, so before
// `burnsAsCraftedWood` existed the furnace refused all 132 of their ids - a
// chest, a ladder, a bookshelf and a composter are among the first things a
// player builds and none of them would burn.
//
// > Fails if: delete the `burnsAsCraftedWood` call from `blockFuelSeconds`. All
// > sixteen drop to 0 s together, which is the state this file was found in.
static_assert(placedFuelSeconds(BlockId::Chest) == 15.0f &&
                  placedFuelSeconds(BlockId::TrappedChest) == 15.0f &&
                  placedFuelSeconds(BlockId::Barrel) == 15.0f &&
                  placedFuelSeconds(BlockId::LadderNorth) == 15.0f &&
                  placedFuelSeconds(BlockId::Composter0) == 15.0f &&
                  placedFuelSeconds(BlockId::NoteBlock) == 15.0f &&
                  placedFuelSeconds(BlockId::Jukebox) == 15.0f &&
                  placedFuelSeconds(BlockId::DaylightDetectorRunFirst) == 15.0f &&
                  placedFuelSeconds(BlockId::Bookshelf) == 15.0f &&
                  placedFuelSeconds(BlockId::ChiseledBookshelf) == 15.0f &&
                  placedFuelSeconds(BlockId::Lectern) == 15.0f &&
                  placedFuelSeconds(BlockId::CraftingTable) == 15.0f &&
                  placedFuelSeconds(BlockId::CartographyTable) == 15.0f &&
                  placedFuelSeconds(BlockId::FletchingTable) == 15.0f &&
                  placedFuelSeconds(BlockId::SmithingTable) == 15.0f &&
                  placedFuelSeconds(BlockId::Loom) == 15.0f,
              "every crafted wooden block on the reference's fuel table burns for 300 ticks, and "
              "no shape or plank predicate in this file reaches a single one of them");

// **The two members of `isChest` that are not wood.** Both halves are asserted:
// that they are still inside the predicate `burnsAsCraftedWood` opens with, and
// that they still come away with nothing. Without the first half this would pass
// just as happily if `isChest` had stopped covering them, which would make it an
// assert about nothing.
//
// > Fails if: drop the `!isEnderChest(block) && !isStowbox(block)` subtraction
// > and let `isChest` answer alone - a furnace would burn obsidian and a shell,
// > 21 ids of it.
static_assert(isChest(BlockId::EnderChest) && isChest(BlockId::Stowbox) &&
                  isChest(BlockId::StowboxDyedFirst) &&
                  placedFuelSeconds(BlockId::EnderChest) == 0.0f &&
                  placedFuelSeconds(BlockId::Stowbox) == 0.0f &&
                  placedFuelSeconds(BlockId::StowboxDyedFirst) == 0.0f,
              "an ender chest is obsidian and a stowbox is a shell - they are chests to every "
              "screen in the game and fuel to none of them");

// **Doors and trapdoors, the last 576 ids and the two published times that are
// not 15 s.** A door is 200 ticks and a trapdoor 300, and nothing else in this
// file could have told them apart: `shapedParent` is the identity for both, so
// they reach neither the thin path nor the plank branch.
//
// > Fails if: give `openingFuelSeconds` one number for both families. The door
// > line is what says the reference does not.
static_assert(placedFuelSeconds(BlockId::DoorRunFirst) == 10.0f &&
                  placedFuelSeconds(BlockId::TrapdoorRunFirst) == 15.0f &&
                  isDoor(placedBlock(BlockId::DoorRunFirst)) &&
                  isTrapdoor(placedBlock(BlockId::TrapdoorRunFirst)),
              "the wiki gives an Overworld wooden door 200 ticks and an Overworld wooden trapdoor "
              "300 - the two are not one row and never have been");

// **The bamboo door and trapdoor are Overworld and burn**, and the nether and
// iron ones do not. Each negative names the family it landed in as well as the
// zero, because `placedFuelSeconds(x) == 0` on its own is also what air says.
//
// > Fails if: replace `openingIsOverworldWood` with a plain
// > `family < kDoorFamilyCount - 1` to catch the iron one - bamboo is family ten
// > and would keep working, while crimson at eight and warped at nine would both
// > become fuel and this is what says so.
static_assert(placedFuelSeconds(doorAt(10, FaceDirection::NegZ, false, false, false)) == 10.0f &&
                  placedFuelSeconds(trapdoorAt(10, FaceDirection::NegZ, false, false)) == 15.0f,
              "bamboo is an Overworld wood and the reference burns its door and trapdoor like any "
              "other");

static_assert(doorFamily(placedBlock(doorAt(8, FaceDirection::NegZ, false, false, false))) == 8 &&
                  doorFamily(placedBlock(doorAt(9, FaceDirection::NegZ, false, false, false))) == 9 &&
                  doorFamily(placedBlock(doorAt(11, FaceDirection::NegZ, false, false, false))) ==
                      11 &&
                  placedFuelSeconds(doorAt(8, FaceDirection::NegZ, false, false, false)) == 0.0f &&
                  placedFuelSeconds(doorAt(9, FaceDirection::NegZ, false, false, false)) == 0.0f &&
                  placedFuelSeconds(doorAt(11, FaceDirection::NegZ, false, false, false)) == 0.0f &&
                  trapdoorFamily(placedBlock(trapdoorAt(8, FaceDirection::NegZ, false, false))) ==
                      8 &&
                  trapdoorFamily(placedBlock(trapdoorAt(9, FaceDirection::NegZ, false, false))) ==
                      9 &&
                  trapdoorFamily(placedBlock(trapdoorAt(11, FaceDirection::NegZ, false, false))) ==
                      11 &&
                  placedFuelSeconds(trapdoorAt(8, FaceDirection::NegZ, false, false)) == 0.0f &&
                  placedFuelSeconds(trapdoorAt(9, FaceDirection::NegZ, false, false)) == 0.0f &&
                  placedFuelSeconds(trapdoorAt(11, FaceDirection::NegZ, false, false)) == 0.0f,
              "the crimson and warped openings burn in no edition and the iron ones are metal - "
              "the family index each one landed in is asserted beside its nought so this cannot "
              "quietly become an assert about air");

/// The metal a tool or a piece of armour goes back to, one nugget at a time.
///
/// **Seventeen enumerators named rather than a run walked**, for the reason the
/// green dye row at the top of this file already carries: iron, gold and
/// chainmail gear is not one contiguous family, and arithmetic across it would
/// quietly mean a different metal the moment a tier is inserted - which finding
/// 941 says is coming, because there is no gold tool tier yet.
///
/// Primary source is `behavior_pack/recipes/furnace_*.json`, one file per row:
/// every iron tool and every iron piece gives `iron_nugget`, **chainmail gives
/// `iron_nugget` as well** rather than a chain of its own, and golden armour
/// gives `gold_nugget`. All seventeen are tagged `[furnace, blast_furnace]`
/// and **not** smoker or campfire - which needs nothing done about it, because
/// both of those gate on `isFood` of the *result* and a nugget is not food.
///
/// **Bedrock smelts nine golden tools we cannot name**, because there is no
/// gold tier in `kTools` at all; when finding 941 lands, its five ids belong
/// here. The blast furnace refuses all seventeen today - `blastFurnaceAccepts`
/// asks `isBlockItem` and gear is not a block - so these smelt in the plain
/// furnace alone until finding 2017's other half lands in `Furnace.cpp`. That
/// is a missing *speed*, not a missing recipe, and it is the reason this half
/// is worth landing on its own.
constexpr ItemId nuggetFor(ItemId item) {
    switch (item) {
    case ItemId::IronSword:
    case ItemId::IronPickaxe:
    case ItemId::IronAxe:
    case ItemId::IronShovel:
    case ItemId::IronHoe:
    case ItemId::IronHelmet:
    case ItemId::IronChestplate:
    case ItemId::IronLeggings:
    case ItemId::IronBoots:
    case ItemId::ChainmailHelmet:
    case ItemId::ChainmailChestplate:
    case ItemId::ChainmailLeggings:
    case ItemId::ChainmailBoots:
        return ItemId::IronNugget;
    case ItemId::GoldenHelmet:
    case ItemId::GoldenChestplate:
    case ItemId::GoldenLeggings:
    case ItemId::GoldenBoots:
        return ItemId::GoldNugget;
    default:
        return ItemId::None;
    }
}

static_assert(nuggetFor(ItemId::IronSword) == ItemId::IronNugget &&
                  nuggetFor(ItemId::IronHoe) == ItemId::IronNugget &&
                  nuggetFor(ItemId::IronBoots) == ItemId::IronNugget &&
                  nuggetFor(ItemId::ChainmailHelmet) == ItemId::IronNugget &&
                  nuggetFor(ItemId::ChainmailBoots) == ItemId::IronNugget &&
                  nuggetFor(ItemId::GoldenHelmet) == ItemId::GoldNugget &&
                  nuggetFor(ItemId::GoldenBoots) == ItemId::GoldNugget,
              "every iron and chainmail piece goes back to iron and every golden one to gold - "
              "chainmail is the one that reads wrong and is right, and it is pinned at both ends "
              "of its run so a future edit cannot give it a metal of its own");
/// **The negative half, and it is the half that rots.** A `default:` returning
/// a real value is this project's bug shape #10; this one returns the empty
/// answer, so the way it fails is by quietly *growing*. Diamond and wood have
/// no furnace recipe in any edition, and an ingot is not gear - the reference
/// takes an ingot apart on a crafting grid, nine nuggets at a time, not in a
/// furnace. Widen the switch and one of these four stops holding.
static_assert(nuggetFor(ItemId::DiamondSword) == ItemId::None &&
                  nuggetFor(ItemId::WoodenPickaxe) == ItemId::None &&
                  nuggetFor(ItemId::IronIngot) == ItemId::None &&
                  nuggetFor(ItemId::Stick) == ItemId::None,
              "only iron, gold and chainmail gear smelts - a diamond sword, a wooden pickaxe, a "
              "bare ingot and a stick must all come back with nothing");

} // namespace

ItemStack smeltResult(ItemId input) {
    // Every raw food cooks into the item that follows it, so the pairing lives
    // in the `ItemId` order rather than in five more table rows that could
    // disagree with it.
    if (isRawFood(input)) {
        return ItemStack{cookedForm(input), 1};
    }
    // Two families answered from the block an item came from rather than from a
    // row each.
    if (isBlockItem(input)) {
        const BlockId block = blockForItem(input);
        // **Every log chars, not just the oak one** - but not the nether ones,
        // not the two bamboo blocks and not planks. The rule named one id where
        // a family predicate already existed, so ten of the eleven woods could
        // not make charcoal at all; widening it to `isLogBlock` then swept in
        // the crimson and warped stems and the bamboo, which the reference
        // refuses. `charsIntoCharcoal` owns the answer, and `burnsAsFuel`
        // below owns the different one.
        if (charsIntoCharcoal(block)) {
            return ItemStack{ItemId::Charcoal, 1};
        }
        // The sixteen dyed terracottas fire into their glazed forms. Both runs
        // are declared white-first in the same order, so this is one offset
        // rather than sixteen rows.
        if (block >= BlockId::WhiteTerracotta && block <= BlockId::BlackTerracotta) {
            return ItemStack{itemForBlock(static_cast<BlockId>(
                                 static_cast<int>(BlockId::WhiteGlazedTerracotta) +
                                 static_cast<int>(block) - static_cast<int>(BlockId::WhiteTerracotta))),
                             1};
        }
        // **The eight deepslate ores are the eight stone ores' own answer.**
        // Both runs are declared in the same order and `stoneOreFor` is where
        // that mapping already lives, held in step by `Block.hpp`'s asserts;
        // eight more table rows would be a second copy of it, free to drift.
        // One level of recursion and no deeper - a stone ore is never a
        // deepslate one.
        if (isDeepslateOre(block)) {
            return smeltResult(itemForBlock(stoneOreFor(block)));
        }
    }
    // Iron, gold and chainmail gear goes back to one nugget. Asked as a family
    // above the table for the same reason the logs and the terracottas are -
    // seventeen more rows would be a second copy of `nuggetFor`, free to drift
    // from it. Gear is neither raw food nor a block item, so none of the
    // branches above can have answered already.
    if (const ItemId nugget = nuggetFor(input); nugget != ItemId::None) {
        return ItemStack{nugget, 1};
    }
    for (const SmeltRule& rule : kSmelting) {
        if (rule.input == input) {
            return rule.output;
        }
    }
    return ItemStack{};
}

float fuelBurnSeconds(ItemId item) {
    for (const FuelRule& rule : kFuels) {
        if (rule.item == item) {
            return rule.seconds;
        }
    }
    // Two owners and no third: `kFuels` above holds the named one-offs, block
    // items included, and one `constexpr` function holds every family. The
    // asserts beside that function drive this exact path through `placedBlock`
    // rather than a shorter copy of it - which is what the four thin families
    // and the sixteen crafted ones went missing for want of.
    if (isBlockItem(item)) {
        return blockFuelSeconds(blockForItem(item));
    }
    return 0.0f;
}

} // namespace game
