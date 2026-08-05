# INTERFACE.md

How the inventory and crafting screens should work, measured from primary sources, written for **this** codebase — a mesh-builder HUD with no widgets, no text input and no clipping.

**Bedrock is the reference.** Java values are marked `[JE]`. Where the two differ I say which one we are taking and why.

Companion documents: `SYSTEM_MEMORY.md` (what the HUD actually is today), `CRAFTABLE.md` (the recipe list), `TEXTURING.md` (§14 formulas, and the HUD sheet's construction).

**There is a reference screenshot at `reference/crafting-ui.avif`** — a console (Switch) capture of the crafting-table screen, 1280 × 720. It is **exactly 3× vanilla's unit layout**, so every number in this document is confirmed against a real frame rather than only against the UI data. Windows cannot hand an AVIF to `System.Drawing`; `tools/convert-image.ps1` reads it through WIC and writes a PNG beside it. Like everything in `reference/`, it may be **measured from** and must **never** be copied under `assets/`.

> **▶ Where we stand (2026-08-04).** **Slices 1–3 are built.** `engine::Window` has a character callback and a `consumeTypedText()` queue; `allItems()`, `categoryFor` and per-recipe `category` / `fitsInTwoByTwo` exist and report **67 items and 15 recipes, 4 of which fit a 2×2**; and the catalogue card is on screen beside the inventory with five clickable tabs, a static 7-wide grid and hover tooltips. The signed-off click/drag/shift-click/double-click model is untouched — the inventory card only *translated*, through the one function every position comes from.
>
> What still does not exist: scrolling, the craftable colouring, the filter toggle, the search field, click-to-fill and groups — slices 4 to 9. **The live scroll bug in §6.2 is still live**; slice 4 owns it.

---

## 0. The five things worth knowing before designing anything

1. **"White backdrop = craftable, red = not" is close but incomplete.** There are **six** slot-background states, not two, and the pale one is a neutral parchment rather than white. Full table in §3.
2. **The colouring and the "Show Craftable" toggle are two separate features.** The colours are always on and always per-item. The toggle *hides* entries; it does not recolour them. Bedrock ships both because they answer different questions — "why can't I make this?" versus "stop showing me things I can't make".
3. **In Bedrock the search bar only exists on the Search tab.** The four category tabs have no search field at all. That is a real structural difference from Java, where search is always present.
4. **The crafting-table screen is the same screen with one region swapped.** Mojang implement it as a single UI object with a `$top_half_variant` variable. Everything else — the catalogue, the tabs, the filter, the 27+9 slots — is byte-identical. We already have this instinct: `tools/make-hud-sheet.ps1` derives the crafting panel from the inventory panel.
5. **"Can I craft this?" cannot be answered greedily.** It is bipartite matching. §4.2 has the counter-example that breaks the obvious implementation, and it fails silently for only some players.

---

# 1. What the screen actually is

## 1.1 Two cards, side by side

Bedrock's survival inventory is **one screen made of two independent cards**, laid out in a 326 × 166 region (UI units; 1 unit ≈ 1 art pixel).

```
┌─ tab strip (sits ABOVE the card's top edge) ─┐
│ [Constr][Equip][Items][Nature]      [Search] │        ┌──────────────────────────┐
├──────────────────────────────────────────────┤        │  armour │ doll │ 2x2     │
│  tab name              [craftable toggle ▮▯] │   ║    │   x4    │      │ crafting│
│  [🔍 search field           ] (Search tab only)│  ║    │         │      │  → out  │
│  ┌────────────────────────────────────────┐  │   ║    ├──────────────────────────┤
│  │ ▤ ▤ ▤ ▤ ▤ ▤ ▤   7 wide, scrolling      │▓ │   ║    │  storage 9 x 3           │
│  │ ▤ ▤ ▤ ▤ ▤ ▤ ▤   18x18 cells            │▓ │   ║    │  hotbar  9 x 1           │
│  └────────────────────────────────────────┘  │   ║    └──────────────────────────┘
└──────────────────────────────────────────────┘  fold
            recipe book  (146 x 166)                        player inventory (176 x 166)
```

| Region | Size | Notes |
|---|---|---|
| Recipe book card | 146 × 166 | its own framed panel |
| Centre fold | 4 × 166 | the gap that separates the two cards — **measured at 12 px = 4 units** |
| Player inventory card | 176 × 166 | **exactly what we already draw** |
| Tab strip | 146 × 27 | anchored *above* the card, tabs overlap its top edge |
| Inner padding | 6 units all round | |
| Item grid | **18 × 18 cells, 7 columns × 7 full rows + an 8th clipped** | measured off the screenshot |

When the recipe book is hidden, a 75-unit spacer keeps the inventory card centred. That is worth copying — it is why toggling the book does not make the inventory jump.

**Three things the screenshot settled that the UI data could not.**

- **The catalogue cell pitch is 18 units, identical to an inventory slot** — not the 25 × 25 that the `slot_craftable` sprite's dimensions imply. That sprite is an overhanging highlight, not the cell. So **one cell pitch serves the whole screen**, which is a real simplification.
- **The eighth row is clipped mid-cell by the card's bottom edge**, not hidden. The reference genuinely masks the list to a viewport. We have no scissor rectangle anywhere (§6.2), so this has to be solved in the builder — either by culling whole rows and accepting a shorter list, or by emitting a partial quad with partial UVs for the last row. **The second is the faithful one and it is not much harder**, because `appendSprite` already takes an arbitrary pixel rect.
- **The two cards are genuinely separate**, each with its own frame and drop shadow, divided by a 12 px gap. "Fold" names a gap, not a graphic joining them.

## 1.2 The right-hand card

Two 88 × 83 panels side by side above the storage rows.

- **Left:** armour column (1 × 4, on the *left* of the doll, helmet→boots), the character preview (52 × 70 black recess, and the doll **tracks the mouse**), and the **offhand as a separate 1 × 1 slot at the bottom-right of the doll** — not part of the armour column.
- **Right:** "Crafting" label, the 2 × 2 grid, an arrow, the output slot.
- **A quirk worth stealing:** if the selected recipe needs a 3 × 3 grid, the arrow is *replaced by a crafting-table icon*. That is Bedrock telling you "go find a table" in place, with no text.

## 1.3 The crafting table screen

Identical, except the **entire top-right region** is swapped for:

- a 3 × 3 grid (54 × 54) offset 29 units right, roughly centring it in the space the doll vacated;
- a larger arrow (22 × 15 against 16 × 13);
- a **larger output slot — 26 × 26 against 18 × 18**;
- **no doll, no armour slots, no offhand.** You cannot equip armour at a crafting table in Bedrock. Easy to miss, and it is a real behavioural difference.

The recipe book is present and **open by default** on Bedrock (collapsed by default on Java `[JE]`). Only the recipe *set* differs: the inventory shows recipes that fit in 2 × 2, the table shows all of them.

## 1.4 Creative

The recipe book is **replaced** by the item catalogue, not extended. Same card, same tab strip, same grid, no craft semantics, **no craftable filter**, and a hotbar strip drawn under the catalogue so you can drag straight to it. The 2 × 2 crafting grid stays — Bedrock briefly made it 3 × 3 in beta and changed it back to match survival.

---

# 2. Tabs

**Five, in order: Construction · Equipment · Items · Nature · … Search.** The first four pack left; a filler then pushes Search hard right. It is not an evenly spaced strip.

| Tab | Contents | Icon |
|---|---|---|
| Construction | building materials | bricks |
| Equipment | tools, weapons, armour | sword |
| Items | ores and miscellany | bed |
| Nature | plants, soil, food | grass block |
| Search | everything, plus the search field | magnifier |

Three facts:

- **Survival and creative use the same five tabs.** Bedrock's deliberate design: one taxonomy, two screens, so an item is in the same place in both. Java has 13–14 creative tabs and 5 recipe-book tabs and they no longer correspond `[JE]`.
- **Unselected tabs render at 20% alpha**, and the leftmost and rightmost tabs have squared-off outer edges (different art from the middle ones).
- **A selected tab has no glow or accent.** In the reference art the *only* differences are that its interior switches from the recessed grey to the panel's own face colour, and it grows 2–4 px toward the panel. It reads as selected because it has become part of the panel.

**How an item gets its category.** Bedrock puts it on the *item* (`menu_category.category`), and the recipe book inherits it from the creative menu. Java moved to declaring it *per recipe* in 1.19.3 `[JE]`.

> **Our choice: per recipe, following Java.** One item can be produced by recipes that belong in different places, and a per-recipe enum has no dependency on a creative catalogue. It costs one field. **We keep Bedrock's four category names**, which are better than Java's.

---

# 3. The craftable indication — the part to get right

## 3.1 Per-cell background: six states, always on

Every catalogue cell picks one background sprite from an index. Vanilla's mapping:

| Index | Meaning | Looks like |
|---|---|---|
| 0 | **Default / craftable** | pale neutral slot — the one described as "white" |
| 1 | Group header, collapsed | light button |
| 2 | Group header, expanded | dark button, pressed |
| 3 | Child item inside an expanded group | dark button |
| 4 | **Selected recipe** | highlight — neither pale nor red |
| 5 | **Not craftable** | a dedicated red button texture |

Two corrections to the common understanding, both worth writing down:

- The "red" is **its own texture**, not a red tint laid over the normal slot. Authoring it as a tint will not look right.
- There is a **selected** state that is neither of the two obvious ones, and two greys for expandable groups. A design that only has "white" and "red" has nowhere to put "this is the one you clicked".

Each cell also carries a **count label**. Whether that is "how many you could make" or "the recipe's output quantity" is unresolved — the binding is named `recipe_craftable_count`, which suggests the former, but every other stack label in the game says the latter. **Decide deliberately; do not inherit by accident.**

## 3.1b The red state is not confined to the catalogue

Measured off the screenshot, and this is the most useful thing it gave up:

| Slot | Colour | Meaning |
|---|---|---|
| Catalogue cell, uncraftable | `243,74,63` | the red state |
| **Crafting output slot** | `239,92,70` | **same red** — the selected recipe cannot be made |
| Empty 2×2 grid slot | `139,139,139` | ordinary vanilla recessed slot |
| Storage / hotbar slot | `139,139,139` | unchanged |

So selecting a recipe you cannot afford **turns the output slot red too**, and the grid fills with a ghost of the recipe. The red is one shared state meaning "this is not going to happen", used in two places — not a catalogue-only decoration.

Also worth knowing so it is not mistaken for a bug: in the screenshot **every cell in the Construction tab is red at once**, because the player's inventory is empty. That is the correct and normal early-game appearance.

## 3.2 The filter toggle — a different feature

- A **two-cell segmented control** at the right-hand end of the tab-name row, measured at 76 × 48 px = **26 × 16 units**, which is the shipped sprite's size exactly. The active cell carries a crafting-grid glyph and a bright green border; the inactive one is a plain dark-green square. It is *not* a sliding knob, despite "switch" being the natural word for it.
- The label beside it flips between **"All recipes"** and **"Craftable recipes"**.
- It **hides entries. It does not recolour them.** The red backgrounds stay on regardless.
- **Hidden entirely in creative.**

**The console prompt, confirmed from the shipped language file:**

```
controller.buttonTip.recipes.showAll=Show All
controller.buttonTip.recipes.showCraftable=Show Craftable
```

These name **the action the button will perform**, not the current state. Showing everything → the prompt reads "Show Craftable"; press it and it flips to "Show All". So the `A: Show Craftable` prompt is the filter, and it has nothing to do with the red backdrops.

**Ship both.** The colour answers "why can't I make this?"; the toggle answers "stop showing me things I can't make".

---

# 4. Mechanics

## 4.1 Two questions, two pieces of code

Conflating these is the main design trap.

| Question | Shape matters? | Runs when |
|---|---|---|
| **Does this grid layout produce a result?** | yes | the grid changes |
| **Does my inventory contain enough for this recipe?** | **no** — shape is ignored entirely | the inventory changes |

Mojang's own doc comment on the matcher says it outright: *"This specifically does not check patterns."* Whether you *have* nine cobblestone has nothing to do with where they go.

## 4.2 The craftable check is bipartite matching, not a greedy count

Build a `map<ItemId, int>` of everything in the inventory, summed across stacks. Then pair up *ingredient slots* against *distinct item types you own*, backtracking when a pairing gets stuck (Kuhn's algorithm — "when stuck, walk back and reassign an earlier pair to free something up").

**Why greedy is wrong, and why it fails silently:**

> Recipe needs `any plank` + `oak plank`. You hold one oak plank and one spruce plank.
> Greedy assigns the oak plank to `any plank` first — it matches — then fails on `oak plank` and reports "not craftable". Wrong.
> The matcher reassigns `any plank` to the spruce plank, freeing the oak one. Craftable.

This only bites once an ingredient can be a *set* ("any plank") **and** a recipe mentions both the set and a member. Until then greedy is correct. **So: ship greedy while every ingredient is a single item, and put the counter-example in a comment at the call site**, because the day we add a wood type it becomes wrong for some players and nobody will connect the two.

**Related rules:**

- **An ingredient always consumes exactly one item from its slot.** There is no "this slot needs 3 sticks" in vanilla. Eight cobblestone means eight grid slots. Keeping this is a large simplification.
- The *result* count is per recipe.
- Ingredient matching is on item **type** — a damaged pickaxe still matches "pickaxe".
- Anything needing per-instance state (repairing two damaged tools, dyeing a container while keeping its contents) gets its **own recipe type and is not shown in the book at all**. Minecraft does exactly this. Do not try to make the data format handle it.

## 4.3 Which recipes appear where

> *"Recipes appear in the inventory's recipe book if they are 2×2 in size, otherwise they will only appear if the player is using a crafting table."*

The filter is on the recipe's **pattern dimensions**, not on the item. Precompute one bool per recipe at load.

## 4.4 Unlocking — and why we can ignore it for now

Bedrock got progressive recipe unlocking in 1.20.30, with a `recipesunlock` game rule (default on) to disable it. **By default the known-recipes set is purely a catalogue filter — it does not gate crafting.** A player who knows the layout can always place items by hand.

> **Our choice: everything visible from the start.** It is a predicate on the display list, addable later with no change to the crafting code. Bedrock declares the trigger in the recipe file (`"unlock": [...]`, an OR over items/tags) which is far cleaner than Java's advancement machinery if we ever want it.

Worth knowing for later: Java persists `recipes`, `toBeDisplayed` (unlocked but not yet seen — drives the "new!" highlight), and **the UI toggle states themselves** — the book remembers whether it was open and whether the filter was on.

## 4.5 Selecting a recipe

| Input | Bedrock | Java `[JE]` |
|---|---|---|
| Left click | **fills the grid** with available materials | shows ghost items |
| Right click | **crafts one** | shows all alternates |
| Shift | **crafts as many as materials allow** | fills the grid |

Bedrock's is the more direct design and maps onto the click/right-click/shift handling we already have — it costs a switch, not a system.

**The auto-fill algorithm**, in order:

1. If the grid already holds exactly this recipe, do nothing.
2. **Empty the grid back into the inventory first** — and if it will not fit, **abort the whole operation** rather than partially filling or dropping items.
3. Build the item→count map.
4. Craft count = 1, or for craft-all `min(possible crafts, smallest ingredient's stack limit, result stack limit)`.
5. Fill each grid cell from the first matching inventory stack found in a linear scan.

**The matcher must tell the placer which concrete item it chose.** With set ingredients, "any plank" has to resolve to a specific plank before anything can be placed, and the placer must not re-decide — it would pick a different assignment and get stuck.

## 4.5b Clicking a catalogue entry — the catalogue is a source, not a container

Measured from `Mojang/bedrock-samples`. **A catalogue cell is not a container slot.** The right card's slots are built from `container_slot_button_prototype`; catalogue cells are built from `creative_no_coalesce_container_slot_button`, whose three actions are `recipe_select` / `recipe_secondary` / `recipe_tertiary`. The recipe book and the creative catalogue are literally the same control. It follows that an entry **never depletes, can never be placed into, and has no merge or double-click-gather path.**

| Input on a catalogue entry | Bedrock | Java `[JE]` |
|---|---|---|
| Left click | **a full stack** on the cursor (the item's own max, so 1 for a tool) | one item, accumulating on repeat |
| Right click | **exactly one** on the cursor | one, second click puts it back |
| Shift + left click | a full stack straight to the inventory | a full stack on the cursor |
| Double click | nothing — the gather mapping is deliberately deleted | — |
| Clicking with a full cursor | replaced by a fresh stack; the old one is gone | same |

**The empty space around the entries is the bin.** Bedrock overrides the shared scroll panel specifically to add destruction: `menu_select → destroy_selection`, `menu_secondary_select → container_reset_held`. So **left click destroys the carried stack, right click puts it back where it came from.** The cells, the tabs, the search field and the filter toggle are all *outside* the destroy zone — only the gaps between cells and the empty area below the last row. Java has no such zone; it has an explicit trash slot instead, and **we should not build one**.

> **Ours:** left click destroys; right click leaves the stack on the cursor, because returning it needs an origin slot we do not track. Gated on creative — in survival the left card is a recipe book and conjuring items would be a cheat.

## 4.6 Ordering and grouping

**Neither edition sorts alphabetically and neither has a display-order field.** Order is the order recipes are declared. That is what we already do for creature species, block shapes and biomes, so it needs no new mechanism — and alphabetical would scatter the wood tiers, which nobody wants.

**Grouping** collapses several recipes producing the same result into one cell with an expand/collapse glyph. Java does it with a `group` string on the recipe; Bedrock's recipe-level `group` field is documented as having *"no known use"* — it groups items instead, via the creative menu. **Take Java's here**: one string field, no coupling, and we have no creative catalogue to inherit from. Note that Java keys on `(group, category)` specifically so a group cannot straddle two tabs.

## 4.7 Search

Bedrock, since 1.20.30, matches **the beginning of any word in the item's display name** — so `stone` finds "Stone Bricks" and "Smooth **Stone**", but a mid-word substring does not hit. Not tags, not ids. Search also **bypasses the unlock gate** where the tabs do not.

Java uses a suffix array over names and ids `[JE]`; at our scale (a few hundred items) a linear scan over display names is free and we should not build an index.

---

# 5. What the reference art measures

Measured from the local reference dump. **These are measurements, not assets** — the art must be ours.

## 5.1 The whole visual language is six greys

`inventory.png` contains **exactly six opaque colours** and nothing else:

| RGB | Role |
|---|---|
| `198,198,198` | panel face |
| `139,139,139` | recessed fill — slot interiors |
| `255,255,255` | raised highlight |
| `85,85,85` | panel drop shadow |
| `55,55,55` | recessed shadow |
| `0,0,0` | outline |

**Raised things are lit from the top-left** (white above/left, `85` below/right). **Recessed things are the inverse** (`55` above/left, white below/right). Everything in the entire GUI is one of those two moves at a different size. That is a remarkably small palette to reproduce and it is most of why the art reads as coherent.

## 5.2 Panel frame, identical on every panel

```
x=0        1px  0,0,0        outline
x=1..2     2px  255,255,255  highlight
x=3..172        198,198,198  face
x=173..174 2px  85,85,85     shadow
x=175      1px  0,0,0        outline
```

Two details that are easy to miss: **the first interior row and column get 3 px of white, not 2** — that extra pixel is what makes the corner read as a mitre rather than a step. And **the corners are chamfered by transparency**, 2 px top-left and 3 px bottom-right.

## 5.3 The slot tile — 18 × 18, packed on an 18 px pitch with no gap

```
row 0 / col 0   : 55,55,55  (17 px)
interior        : 139,139,139  16 x 16 at (1,1)
row 17 / col 17 : 255,255,255 (17 px)
```

Pixel-count check in the style of `TEXTURING.md` §14.7: `55` appears 33 times, `255` appears 33, `139` appears 258. Adjacent slots **share their bevel edge**.

## 5.4 Key rectangles

| Sheet | Element | Rect |
|---|---|---|
| inventory 176×166 | armour ×4 | (7,7) (7,25) (7,43) (7,61) |
| | offhand | (76,61) |
| | 2×2 grid | (97,17) (115,17) (97,35) (115,35) |
| | result | (153,27) **18×18** |
| | arrow | 16×13 at (135,29), flat `139`, no outline |
| | player preview | (25,7) 51×72, interior pure black |
| | storage 3×9 | x = 7,25,…,151; y = 83,101,119 |
| | hotbar | same x; y = **141** (a 4 px gap) |
| crafting 176×166 | 3×3 grid | x = 29,47,65; y = 16,34,52 |
| | result | (119,30) **26×26** |
| | arrow | 22×15 at (90,35) |
| | storage + hotbar | **byte-identical to the inventory sheet** |
| catalogue 195×136 | item grid 5×9 | x = 8,26,…,152; y = 17,35,53,71,89 |
| | hotbar | same x; y = 111 |
| | scrollbar track | (174,17) 14×12 outer, interior 12×110 |
| | scrollbar thumb | 12×15, travels 95 px |
| | search field | **(80,4) 90×12** |

The search field uses a **softer bevel than a slot** — `85` on its top/left where a slot uses `55`. A deliberately shallower recess.

## 5.5 Recipe-book sprites

`slot_craftable` and `slot_uncraftable` are **25 × 25**, not 18 × 18 — the catalogue cell is bigger than an inventory slot. Tabs are 35 × 27, the filter toggle 26 × 16, the search icon 12 × 12.

## 5.6 There are no tab icons

**Zero tab-icon textures exist.** The icon on a tab is a *rendered item stack*, drawn by the same code that draws items in slots. There is no artwork to port — and we already have `appendBlockIcon`, so a Construction tab showing a brick block is free.

## 5.7 What the screenshot measures, at 3×

`reference/crafting-ui.avif` is 1280 × 720 and the slot pitch measures **54 px**, which is 18 × 3 exactly — so it is vanilla's layout at a clean 3× and every measurement divides back to whole units.

| Measured | px | ÷ 3 = units | Matches |
|---|---|---|---|
| Slot pitch (storage, hotbar, catalogue) | 54 | **18** | vanilla slot |
| Layout block, left edge → right edge | 978 | **326** | the researched content stack |
| Left card | 438 | **146** | recipe book |
| Gap between cards | 12 | **4** | the "fold" |
| Right card | 528 | **176** | our existing panel, unchanged |
| Card height | ~498 | **166** | our existing panel, unchanged |
| Storage block | 162 tall | 54 = 3 rows | |
| Gap, storage → hotbar | ~9–12 | **3–4** | |

The layout block is centred horizontally, which is why it starts at x ≈ 151 on a 1280-wide frame.

**Two cautions about this particular capture.** It is a **console** screenshot — the `ZL`/`ZR` strip, the `L`/`R` bumper hints beside the tabs and the `A Show Craftable` / `B Exit` prompt bar along the bottom are all controller chrome and are **not** part of the PC layout. The two layout buttons themselves *are* part of the PC layout; only their shoulder-button bindings are console-specific.

## 5.8 The extracted icon set

`tools/extract-ui-icons.ps1` crops 21 regions out of the capture into **`reference/ui-icons/`**, each written twice: `<name>@3x.png` as captured (what you look at) and `<name>.png` divided by three (the native grid, what you measure). The script **refuses to write under `assets/`**, the same mechanical guard the creature atlas tool carries.

| Group | Files | Measured |
|---|---|---|
| Category tabs | `tab-construction` `tab-equipment` `tab-items` `tab-nature` `tab-search` | 66 × 74 px = **22 × 25 units**, on a **75 px (25 unit) pitch**; Search pushed right by an extra 63 px |
| Layout switch | `layout-book-inventory` · `layout-inventory-only` | 29 × 19 and 27 × 19 units |
| Toolbar | `button-help` `button-close` `bumper-zl` `bumper-zr` `toolbar-strip` | |
| Craftable filter | `toggle-craftable` | 28 × 19 units |
| Widgets | `scrollbar` `slot-empty` `slot-red-uncraftable` `slot-output-red` `panel-corner` `armour-slot-glyphs` | slots **exactly 18 × 18 units**, which is the check that the 3× assumption holds |
| Whole cards | `card-left-full` `card-right-full` | 149 × 193 and 178 × 169 units |

**What the layout switch actually means**, since it is not obvious from the glyphs: the left button shows a **green book overlapping a barrel** and selects *recipe book **and** inventory*; the right shows the **barrel alone** and selects *inventory only*. On console they are bound to `ZL` and `ZR`, which is why the bumper glyphs bracket the strip — but the buttons themselves are part of the PC layout too.

**The tab icons are compositions, not sprites.** Equipment is a sword crossed with a helmet; Nature is a grass block carrying a sapling and a flower; Items is a bed with a bucket. §5.6 says there is no tab-icon artwork in the asset dump and this is why — the game renders item stacks into the tab. **We already have `appendBlockIcon`**, so our tabs can be built the same way and cost nothing to author.

---

# 6. What we have, and what is missing

## 6.1 Reusable unchanged

| Thing | Why |
|---|---|
| `game::slots::*` | pure two-stack operations, zero UI knowledge |
| `Recipe` matching (`craftResult`, `consumeIngredients`) | takes `(ItemStack*, size)` |
| All of `HudPrimitives` | quads, sprites, text, block icons, tooltips |
| The negative-layer convention | **no shader change needed to add panels** |
| `tools/make-hud-sheet.ps1`'s derivation | a new panel is ~15 lines: derive from the inventory art, stamp cells, append to the stack |
| The whole click/drag/shift/double-click model | already correct and already better than most |

`InventoryScreen::build` is a **pure function returning a mesh** — no state, no scroll offset, no selection. That is the most reusable fact in the module, and it is also the thing a scrolling, tabbed, searchable panel will force us to change: the screen will need state, and it should live in **one struct owned by the caller**, passed in, rather than becoming file-scope statics.

## 6.2 Genuinely missing, in order of how much they hurt

1. **Text input does not exist.** No letters beyond `WASDEFQ`, no Backspace, no character callback. `Window` needs a GLFW char callback and a small typed-text queue. **This is a platform change and it gates the search bar entirely.**
2. **No clip rectangle anywhere.** Nothing can be masked to a viewport. The reference **clips its last row mid-cell** (§1.1), so a faithful list needs the builder to emit a partial quad with partial UVs for the bottom row — `appendSprite` already takes an arbitrary pixel rect, so this is arithmetic rather than a new primitive.
3. **No item enumeration.** There is no `allItems()`, no count, no iteration order. A catalogue needs one.
4. **No recipe enumeration.** `recipes()` is in an anonymous namespace, and there is no result→recipe index.
5. **Scroll input is not routed to the UI.** `consumeScrollDelta()` unconditionally drives hotbar selection — it is not gated on a screen being open, so scrolling with the inventory open *already* changes the hotbar selection invisibly. **That is a live bug, not just a gap.**
6. **`appendStack` is file-private** in `InventoryScreen.cpp`. A second panel needs it promoted into `HudPrimitives`.
7. **No per-item category**, and no display-name path that handles spawn eggs uniformly (`spawnEggName` lives in `Creature.hpp` because `Item.hpp` cannot know what a creature is called — every catalogue label needs the same two-branch fallback the tooltip already uses).

## 6.3 Two traps specific to us

- **Depth.** Two overlapping cards, tabs that sit *above* a card's edge, a scroll shadow, cell backgrounds under icons under count labels. The hotbar-highlight incident is exactly this failure. **Assign every layer an explicit band up front** rather than interleaving.
- **The HUD rebuilds every frame while a screen is open** (the held stack follows the cursor), so a caret, hover highlighting and live filtering are free. But `hudDirty` doubles as a console-log trigger — typing in a search box would spam the log until that is separated.

---

# 7. The build order

Each slice ends in something runnable, per rule 1.

| # | Slice | Why here |
|---|---|---|
| **1** | **Typed text in `Window`** — a GLFW char callback, Backspace, and a `consumeTypedText()` queue. No UI yet. | Everything else can be designed around it; nothing else can start without it. Smallest possible platform change. |
| **1** | **Typed text in `Window`** — a GLFW char callback, Backspace, and a `consumeTypedText()` queue. No UI yet. ✅ | Everything else can be designed around it; nothing else can start without it. Smallest possible platform change. |
| **2** | **Enumerate items and recipes** — `allItems()`, make `recipes()` public, add `category` and `fitsInTwoByTwo` per recipe. Still no UI. ✅ | Pure data. Provable by logging counts. |
| **3** | **The second card, static** — draw a 146 × 166 panel beside the inventory, with the five tabs and a fixed, unscrolling grid of every item in the selected category. No search, no craftable colouring. ✅ | This is the layout risk. Get the two cards, the fold and the tab strip right while there is nothing else to blame. |
| **4** | **Scrolling** — route the wheel to the panel when a screen is open (**fixing the live hotbar bug**), per-item culling, a scrollbar. | Makes slice 3 actually usable and removes the F10 egg/block-kit toggle's reason to exist. |
| **5** | **Craftable colouring** — the six-state background, the greedy check with the bipartite counter-example written at the call site. | The first thing that needs the recipe data rather than the item list. |
| **6** | **The filter toggle** — hide non-craftable entries, with the "All recipes" / "Craftable recipes" label. | Trivial once 5 exists. Deliberately separate, because they are separate features. |
| **7** | **The search tab** — the field, the caret, beginning-of-word matching, and the clear button. | Needs slice 1 and nothing else. |
| **8** | **Click to fill the grid** — left fills, right crafts one, shift crafts all, with the empty-the-grid-first rule. | The payoff. Needs 2 and 5. |
| **9** | **Groups** — collapse recipes sharing a result, expand/collapse glyph, the two grey backgrounds. | Only worth it once there are enough recipes to need it. |

**Deferred deliberately:** recipe unlocking (§4.4 — a display predicate, addable any time), the armour and offhand slots (there is no armour system yet, and the `Region` enumerators for them are already dead code), the character doll, and the furnace's separate tab set.

**The smallest thing that already looks like Bedrock** is slices 1–5: two cards, five tabs, an 18 × 18 grid with pale and red backgrounds. The filter, search and groups can all follow without changing the layout.

---

## Sources

**Primary:** [`Mojang/bedrock-samples`](https://github.com/Mojang/bedrock-samples) — `resource_pack/ui/inventory_screen.json`, `inventory_screen_pocket.json`, and `resource_pack/texts/en_US.lang`. This is the shipped vanilla UI data, so every layout number in §1–§3 is read rather than eyeballed.

**Secondary:** minecraft.wiki — Recipe book, Inventory, Creative inventory, Crafting, Crafting Table, 2×2 grid. Java algorithm names and structures via the Fabric Yarn mappings (class, field and method names plus doc comments — not method bodies), marked `[JE]` throughout.

**Measured locally:** `reference/minecraft-assets-26.2/` for §5.1–§5.6, and `reference/crafting-ui.avif` for §5.7 and every measured number elsewhere. Reference art may be **measured from**, must **never live under `assets/`**, and must **never ship**.

**Not confirmed, rather than guessed:** whether red backgrounds apply to the whole visible list or only the selected entry (the per-item binding implies the whole list; the wiki's phrasing implies otherwise — **the screenshot shows an entire tab red at once, which settles it in favour of the whole list**); and what the per-cell count label actually counts.
