# Reference Asset Library

Map of `reference/minecraft-assets-26.2/` — a complete extraction of Minecraft 26.2's assets and data, ~553 MB.

> **This folder is gitignored and nothing in it may ever ship.** It is reference material only. Mechanics, dimensions and recipe shapes are functional designs and may be reimplemented freely; textures, models, sounds and names are expressive work and must be ours. See `CLAUDE.md` → "Architecture Rules".

The real root is nested one level down: `reference/minecraft-assets-26.2/minecraft-assets-26.2/`. Everything below is relative to that.

---

## What this replaces

The old `reference/minecraft-1.19.3-block-textures/` was 925 block textures and nothing else. This dump adds **item icons, entity textures, every model's geometry, and all 1587 recipes as machine-readable JSON** — which means recipe shapes, block dimensions and drop tables no longer have to be read off a wiki page.

---

## Top level

```
assets/     art, models, blockstates, sounds, language files
data/       recipes, loot tables, tags, worldgen, structures
```

---

## `assets/minecraft/` — art and geometry

| Path | Count | What it is |
|---|---|---|
| `textures/block` | 1372 | Block face textures, 16×16 RGBA |
| `textures/item` | **797** | **Item icons — sticks, tools, coal, ingots** |
| `textures/entity` | — | Non-cube models: chests, mobs, signs, beds |
| `textures/gui` | 662 | Every HUD and menu sprite |
| `textures/particle` | 290 | Particle sprites |
| `textures/environment` | 5 | Sun, moon, clouds, rain |
| `textures/colormap` | 4 | Grass and foliage tint gradients by climate |
| `models/block` | **2659** | **Geometry as JSON — exact box dimensions** |
| `models/item` | 1273 | How each item renders in hand and inventory |
| `blockstates` | 1200 | Which model to use for which block state |
| `font` | 9 | Font definitions |
| `lang` | 146 | Translations — useful for naming conventions |

### `models/block` is the most valuable folder here

Every non-cube block's exact dimensions, in sixteenths of a block. Example — `fence_post.json`:

```json
{ "from": [ 6, 0, 6 ], "to": [ 10, 16, 10 ] }
```

That is a post spanning 0.375–0.625, which is **exactly what our `fenceBoxes()` already uses**. This folder is how to check any future shape without guessing.

### `blockstates` explains connected geometry

`oak_fence.json` uses a `multipart` list: always apply `fence_post`, then apply `fence_side` rotated 0/90/180/270 for each `when: { north: "true" }` condition. This is precisely the model our mesher implements — the arms are derived from neighbours, not stored.

---

## `data/minecraft/` — rules and tables

| Path | Count | What it is |
|---|---|---|
| `recipe` | **1587** | **Every recipe, machine-readable** |
| `loot_table/blocks` | 1115 | What each block drops, with tool conditions |
| `loot_table/entities` | 95 | Mob drops |
| `loot_table/chests` | 29 | Generated chest contents |
| `tags/item` | 193 | Ingredient equivalence groups |
| `tags/block` | 263 | Block groupings |
| `worldgen/biome` | 68 | Biome parameters |
| `worldgen/noise` | 63 | Noise generator settings |
| `worldgen/configured_feature` | 228 | Trees, ores, decorations |
| `worldgen/placed_feature` | 264 | Where features are placed |
| `structure/` | ~400 | Village and dungeon layouts, NBT |
| `enchantment` | 45 | Enchantment definitions |
| `damage_type` | 53 | Damage sources |

### Recipe format

```json
{
  "type": "minecraft:crafting_shaped",
  "key": { "#": "minecraft:stick", "W": "minecraft:oak_planks" },
  "pattern": [ "W#W", "W#W" ],
  "result": { "count": 3, "id": "minecraft:oak_fence" }
}
```

`pattern` is rows of the grid; `key` maps each character to an ingredient. A `#`-prefixed value like `#minecraft:planks` is a **tag** — a named set living in `tags/item/`, so `planks` resolves to all twelve wood types. This is exactly the indirection our recipe system will want for "any planks".

**Recipe types present:** 733 shaped, 323 shapeless, 319 stonecutting, 73 smelting, 25 blasting, plus smithing, campfire and special cases.

Smelting is simpler:

```json
{ "type": "minecraft:smelting", "experience": 0.15,
  "ingredient": "#minecraft:logs_that_burn",
  "result": { "id": "minecraft:charcoal" } }
```

---

## What we need, and whether it's here

### Textures — everything on the critical path is now covered

| Asset | Status | Path |
|---|---|---|
| **Stick** | ✅ **now available** | `textures/item/stick.png` |
| **Charcoal** | ✅ **now available** | `textures/item/charcoal.png` |
| Crafting Table | ✅ | `textures/block/crafting_table_{top,side,front}.png` |
| Furnace | ✅ | `textures/block/furnace_{front,front_on,side,top}.png` |
| Torch | ✅ | `textures/block/torch.png` |
| Glass | ✅ | `textures/block/glass.png` |
| Ladder | ✅ | `textures/block/ladder.png` |
| Coal + Coal Ore | ✅ | `textures/item/coal.png`, `textures/block/coal_ore.png` |
| Wooden tools ×5 | ✅ **now available** | `textures/item/wooden_{pickaxe,axe,shovel,sword,hoe}.png` |
| Stone tools ×5 | ✅ **now available** | `textures/item/stone_{...}.png` |
| **Chest** | ✅ | `textures/entity/chest/normal.png` — **64×64 UV sheet**, not four faces |

**Nothing on the critical path lacks reference any more.** The earlier conclusion that the artist was needed for Stick and the tool icons is now obsolete — those were only missing because the old dump held block textures exclusively.

The chest remains the awkward one: it is a single 64×64 unwrapped sheet for a non-cube model, so it needs a real model pipeline rather than a per-face texture. That is a bigger job than the texture.

### Recipes — all verified against source

Every recipe in `CRAFTABLE.md` was confirmed by reading its JSON directly, which is more reliable than the wiki (whose tables are collapsed behind JavaScript and cannot be fetched).

### Still genuinely missing

Nothing needed for M19. Longer term, `data/minecraft/loot_table/blocks/` will settle tool-tier drop rules when mining requirements land, and `worldgen/` is worth revisiting at ore generation.

---

## How to use this well

**Measure, do not describe.** The lesson from the bark texture stands: counting a reference's run lengths and palette spread settles an argument that adjectives cannot. `tools/probe-image.ps1` exists for this.

**Read the model JSON before inventing dimensions.** Every non-cube shape we add — stairs, panes, doors, buttons — has its exact extents recorded in `models/block/`. Guessing costs a rebuild cycle; reading costs one command.

**Read the recipe JSON before writing a recipe.** Pattern, key and yield are unambiguous there.

**Never copy pixels.** Derive statistics, generate originals. The whole premise of the project depends on this boundary being mechanical rather than remembered — which is why `reference/` sits outside `assets/` and is gitignored.

---

## Useful commands

```powershell
$ref = "reference\minecraft-assets-26.2\minecraft-assets-26.2"

# Read a recipe
Get-Content "$ref\data\minecraft\recipe\torch.json" -Raw

# Read a shape's exact dimensions
Get-Content "$ref\assets\minecraft\models\block\fence_post.json" -Raw

# Resolve an ingredient tag
Get-Content "$ref\data\minecraft\tags\item\planks.json" -Raw

# Find a texture
Get-ChildItem "$ref\assets\minecraft\textures" -Recurse -Filter "stick.png"

# What does a block drop?
Get-Content "$ref\data\minecraft\loot_table\blocks\stone.json" -Raw
```
