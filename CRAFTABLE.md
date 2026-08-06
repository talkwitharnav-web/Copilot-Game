# CRAFTABLE.md

Every recipe planned for this game, what it needs, and whether we can build it today.

Every recipe below was verified on 2026-07-31 against the **recipe JSON in `reference/minecraft-assets-26.2/`**, which is authoritative and unambiguous. Do not use the wiki for this — its recipe tables are collapsed behind JavaScript and cannot be fetched. See `reference/ASSETS-REFERENCE.md` for how to read them.

**Mechanics are free, assets are not.** Recipe shapes and yields are functional designs and are reimplemented here deliberately. Every texture must be ours. See `CLAUDE.md` → "Architecture Rules".

---

## Grid notation

Recipes are drawn as they sit in the grid. `.` is an empty cell.

A **2×2** recipe fits the player inventory's own grid. A **3×3** recipe needs a crafting table, which is why the table gates almost everything.

**Shapeless** recipes ignore position — the ingredients may sit anywhere in the grid.

---

## What exists right now

**Blocks (66):** the originals — Air, Stone, Dirt, Grass, Sand, Cobblestone, Gravel, Snow, Planks, Bricks, Glowstone, Log, Leaves, Water, Tall Grass, Stone Slab, Cobblestone Stairs, Plank Fence, Crafting Table, Furnace, Torch — plus **Andesite, Diorite, Granite, Smooth Stone, Stone Bricks, Mossy Cobblestone, Obsidian, Clay, Sandstone, Bookshelf, Glass, Dandelion, Poppy, Dead Bush, eight ores, Deepslate, Bedrock, Terracotta and Packed Ice.**

**Items (61):** Stick, Charcoal, ten tools, fifty-six spawn eggs, and **eleven resources** — coal, raw iron/gold/copper, iron/gold/copper ingots, diamond, emerald, lapis lazuli and redstone.

**Crafting:** the inventory's 2×2 and a crafting table's 3×3 both work, with shift-click and double-click. **Smelting works** with seven recipes, fuelled by coal, charcoal, logs, planks, fences or sticks.

**Mining is timed and tiered.** Stone-family blocks and every ore need a pickaxe or they drop nothing.

**Recipes shipped (20):** `Log → 4 Planks`, `2 Planks → 4 Sticks`, `4 Planks → Crafting Table`, `8 Cobblestone → Furnace`, `Charcoal + Stick → 4 Torches`, `Coal + Stick → 4 Torches`, `4 Stone → 4 Stone Bricks`, `4 Sand → Sandstone`, `Cobblestone + Tall Grass → Mossy Cobblestone`, `3 Iron Ingots in a V → Bucket`, and all ten tools. **Eight fit a 2×2**, so they are craftable without a table.

**Smelting shipped (7):** Cobblestone → Stone, Stone → Smooth Stone, Log → Charcoal, Sand → Glass, and the three raw metals → their ingots.

**Still missing:** wall torches (need a tilted shape), the chest (needs a non-cube model), and the whole of Tier 2's slab and stair variants.

---

## Tier 0 — works with zero new art

These need nothing that does not already exist.

### Planks
```
Log        →  4 Planks
```
Shapeless, 1 ingredient, fits 2×2. The first recipe the game ever shipped, and what M19 was built against.

### Stone Slab
```
Stone Stone Stone   →  6 Stone Slabs
.     .     .
.     .     .
```
3×3. Slab block already exists.

### Cobblestone Stairs
```
Cobble .      .        →  4 Cobblestone Stairs
Cobble Cobble .
Cobble Cobble Cobble
```
3×3, mirrorable. Stairs already exist in all 8 orientations.

### Stone (smelting)
```
Cobblestone  +  fuel  →  Stone
```
Furnace. Makes Stone renewable and needs no new texture at all — we own both blocks.

---

## Tier 1 — the critical path

Four assets unlock roughly twenty-five recipes. **All four shipped at M19c**; the ordering is kept because it is the reasoning, and because it is the shape any future "what unblocks the most?" question should take.

### 1. Stick
```
Planks .   →  4 Sticks
Planks .
```
2×2. **The single highest-value asset in the project.** Gates fences, all tools, and torches. It is the most-used ingredient in the original game (49 recipes).

### 2. Crafting Table · *reference: `crafting_table_top/_side/_front`*
```
Planks Planks   →  1 Crafting Table
Planks Planks
```
2×2. Gates every 3×3 recipe below, including the two Tier 0 ones.

### 3. Furnace · *reference: `furnace_front/_front_on/_side/_top`*
```
Cobble Cobble Cobble   →  1 Furnace
Cobble .      Cobble
Cobble Cobble Cobble
```
3×3. Opens smelting: charcoal, glass, and stone.

### 4. Torch · *reference: `torch.png`*
```
Charcoal .   →  4 Torches
Stick    .
```
2×2. Coal works too, but **charcoal comes from smelting a log**, so torches need no ore and no mining — wood alone is enough. This is why Coal Ore is *not* on the critical path.

---

## Tier 2 — free variants

Reuse textures we already own. Pure code once a crafting table exists.

| Recipe | Grid | Output |
|---|---|---|
| 3 Cobblestone in a row | 3×3 | 6 Cobblestone Slabs |
| 3 Planks in a row | 3×3 | 6 Wooden Slabs |
| 6 Planks, stair pattern | 3×3 | 4 Wooden Stairs |
| 6 Stone, stair pattern | 3×3 | 4 Stone Stairs |

### Plank Fence · *block already exists, needs only Sticks*
```
Planks Stick Planks   →  3 Fences
Planks Stick Planks
.      .     .
```
3×3.

---

## Tier 3 — smelting

Needs the furnace. **All shipped.**

| Input | Output |
|---|---|
| Log | Charcoal |
| Sand | Glass |
| Cobblestone | Stone |
| Stone | Smooth Stone |
| Raw iron / gold / copper | the matching ingot |

Fuel burn times worth copying: coal/charcoal smelts 8 items, a log smelts 1.5, planks 1.5, a stick 0.5.

---

## Tier 4 — tools

Every tool is `material` on top, `Stick` below. Wooden uses Planks; stone uses Cobblestone. **Five icon shapes, recoloured per material** — so ten sprites for two full tiers.

```
Pickaxe        Axe            Shovel      Sword       Hoe
M M M          M M .          . M .       . M .       M M .
. S .          M S .          . S .       . M .       . S .
. S .          . S .          . S .       . S .       . S .
```
All 3×3, all mirrorable. Reference icons exist for all ten — `textures/item/{wooden,stone}_*.png`.

---

## Tier 5 — cheap wins, low priority

### Ladder · *reference: `ladder.png`*
```
Stick .     Stick   →  3 Ladders
Stick Stick Stick
Stick .     Stick
```
3×3. Seven sticks, one texture, genuinely useful for caving.

### Fence Gate · *needs a new texture*
```
Stick Planks Stick   →  1 Fence Gate
Stick Planks Stick
```

### Chest · *entity model, not a cube — defer*
```
Planks Planks Planks   →  1 Chest
Planks .      Planks
Planks Planks Planks
```
3×3. Needs a UV-unwrapped sheet and a non-cube model, so it is meaningfully more work than any block above.

---

## Deliberately excluded

| Thing | Why |
|---|---|
| **Lever**, **Grindstone** | Craftable with what we would have, but there is no redstone circuitry and no enchanting, so they would do nothing |
| **Snowball, Flint** | Item icons for no payoff |
| **Bricks** | The block exists but has **no source**. The real chain is Clay Ball → smelt → Brick item → 4 Bricks, which needs two icons. Clay now generates, so this is cheaper than it was |
| **Sign** | `6 Planks + 1 Stick → 3 Signs` is simple, but rendering player text on a block is a whole feature |

---

## Asset status summary

**Every texture on the critical path has reference material, and all of it is currently staged in the running game.** `tools/make-reference-blocks.ps1` writes the reference block and item art to `blocks-reference/` beside each executable; our own 16×16s under `assets/textures/blocks/` are the fallback. Delete the folder to return to ours everywhere. See `START-HERE.md` §5 for why the reference goes in first and ours follows.

**Chest is the one real exception.** Its texture is a single 64×64 UV-unwrapped sheet for a non-cube model, not four square faces — so it needs model support before the texture matters at all.

Before inventing any shape's dimensions, read `models/block/*.json` in the reference dump. Our fence post at `0.375–0.625` already matches the reference's `[6,0,6]→[10,16,10]` exactly, and that was arrived at by guessing — next time it can be read.

---

## Update discipline

Add a recipe here **before** implementing it, with its grid drawn and its texture status named. When a recipe ships, note it in `TIMELINE.md` rather than adding a status column here — this file describes the target, not the progress.
