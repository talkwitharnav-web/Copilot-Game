# Reference Asset Library

> **Reading a texture's layout, or authoring one of ours? Read `TEXTURING.md` first.** This file says what is *in* `reference/`; that one says what to *do* with it — how to derive a box net from an image, which measurements to take, and the traps that make a skin look broken.

Map of `reference/minecraft-assets-26.2/` — a complete extraction of Minecraft 26.2's assets and data, ~553 MB.

> **This folder is gitignored and nothing in it may ever ship.** It is reference material only. Mechanics, dimensions and recipe shapes are functional designs and may be reimplemented freely; textures, models, sounds and names are expressive work and must be ours. See `CLAUDE.md` → "Architecture Rules".

The real root is nested one level down: `reference/minecraft-assets-26.2/minecraft-assets-26.2/`. Everything below is relative to that.

---

## Also in `reference/`, outside the dump

Loose files sitting directly under `reference/`, not part of the 26.2 extraction:

| File | What it is |
|---|---|
| `crafting-ui.avif` | **A 1280×720 console capture of the crafting-table screen.** The single most useful UI reference we have — it is vanilla's layout at **exactly 3×** (slot pitch measures 54 px = 18 × 3), so every measurement divides back to whole units. `INTERFACE.md` §5.7 lists what it settled. |
| `crafting-ui.png` | The same, converted. **System.Drawing cannot open AVIF**; `tools/convert-image.ps1` reads it through WIC. |
| `ui-icons/` | **21 regions cropped out of that capture**, written by `tools/extract-ui-icons.ps1`. See below. |
| `inv-concept.png`, `concept art for inv*.webp` | The user's own inventory mock-ups, which the current panel art was built from |
| `inv-9col.png`, `inv-native.png`, `hud-native.png`, `hud-source.png`, `furnace-ui.png` | Sources for `tools/make-hud-sheet.ps1` |

### `reference/ui-icons/` — the extracted UI set

Regenerate with `powershell -File tools\extract-ui-icons.ps1`. Every region is written **twice**: `<name>@3x.png` at the capture's own resolution, which is what you look at, and `<name>.png` divided by three, which is the native pixel grid and what you measure. The 1× is *indicative* — the source is lossy AVIF, so it recovers the grid, not clean texels.

| Group | Files | Size (units) |
|---|---|---|
| **Category tabs** | `tab-construction`, `tab-equipment`, `tab-items`, `tab-nature`, `tab-search` | 22 × 25 each |
| **Layout switch** | `layout-book-inventory` (recipe book **and** inventory), `layout-inventory-only` | 29 × 19, 27 × 19 |
| **Toolbar** | `button-help`, `button-close`, `bumper-zl`, `bumper-zr`, `toolbar-strip` | |
| **Craftable filter** | `toggle-craftable` | 28 × 19 |
| **Widgets** | `scrollbar`, `slot-empty`, `slot-red-uncraftable`, `slot-output-red`, `panel-corner`, `armour-slot-glyphs` | slots are 18 × 18 |
| **Whole cards** | `card-left-full`, `card-right-full` | 149 × 193, 178 × 169 |

**The tab icons are not sprites and never were.** Each is a *rendered item composition* — Equipment is a sword crossed with a helmet, Nature is a grass block carrying a sapling and a flower, Items is a bed with a bucket. There is no artwork to port, which means our own tabs can be built from `appendBlockIcon` and cost nothing.

**The extractor refuses to write under `assets/`**, mechanically, the same way `tools/make-reference-creature-atlas.ps1` does.

### Staging these as proof art

`tools/make-reference-hud.ps1` composites the five tab crops over a copy of our own `assets/textures/hud.png` and writes **`hud-reference.png` beside each `game.exe`**, which the game prefers whenever it exists. It carries the same `assets/` guard, never resizes a crop (the 22 × 25 check *is* the proof that the capture is a clean 3×), and `run.ps1` rebuilds it whenever the sheet it was composited from is newer.

`tools/make-reference-blocks.ps1` does the same for the world: **110 block and item textures into `blocks-reference/`**, again beside the exe and never under `assets/`. It **never rescales** — anything not 16 × 16 after frame extraction throws, because a texture array needs one size and a silent resize would blur exactly one layer. Two details it has to handle: `water_still.png` is a 16 × 512 strip of **32 animation frames**, cropped to frame 0 for the plain water layer and extracted whole as `water00.png` … `water31.png` for the animated surface; and five textures ship **greyscale** because the original tints them at runtime (`grass_block_top`, `grass_block_side_overlay`, `oak_leaves`, `short_grass`, `water_still`) — so the tool applies our plains tints on the way through. `grass_side` is two reference images composited into one of ours. Textures with no counterpart, like `white.png` and `sun.png`, keep ours.

Both exist so a layout or a world can be judged against pixels known to be right. **Author our own art after the layout is settled, not while it is being settled** — otherwise a tab that looks wrong could equally be bad geometry or bad art, which is the same two-unknowns trap as debugging a box net alongside its skin.

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
| **The other seven ores** | ✅ **in use** | `textures/block/{iron,copper,gold,redstone,lapis,diamond,emerald}_ore.png` |
| **Ingots and gems** | ✅ **in use** | `textures/item/{raw_iron,iron_ingot,raw_gold,gold_ingot,raw_copper,copper_ingot,diamond,emerald,lapis_lazuli,redstone}.png` |
| Wooden tools ×5 | ✅ | `textures/item/wooden_{pickaxe,axe,shovel,sword,hoe}.png` |
| Stone tools ×5 | ✅ | `textures/item/stone_{...}.png` |
| **Chest** | ✅ | `textures/entity/chest/normal.png` — **64×64 UV sheet**, not four faces |
| **Spawn eggs ×46** | ✅ **in use as placeholders** | `textures/item/<mob>_spawn_egg.png`, 16×16 each |
| **Creature skins ×42** | ✅ **in use as placeholders** | `textures/entity/<family>/<mob>.png` |
| **Charged creeper shell** | ✅ **in use** | `textures/entity/creeper/creeper_armor.png` — the same net as the creeper, mostly transparent |
| **Catalogue tabs ×5** | ✅ **in use as placeholders** | cropped from `crafting-ui.avif` into `ui-icons/`, 22×25 each |

**Four sets are staged beside the executable rather than copied into `assets/`**, which is the rule for reference art: `make-spawn-egg-sprites.ps1` writes `spawn-eggs/`, `make-reference-creature-atlas.ps1` writes `creatures-reference.png`, `make-reference-hud.ps1` writes `hud-reference.png`, and `make-reference-blocks.ps1` writes `blocks-reference/`. All four refuse to write under `assets/`. See `START-HERE.md` §5.

The chest remains the awkward one: it is a single 64×64 unwrapped sheet for a non-cube model, so it needs a real model pipeline rather than a per-face texture. That is a bigger job than the texture.

### Recipes — all verified against source

Every recipe in `CRAFTABLE.md` was confirmed by reading its JSON directly, which is more reliable than the wiki (whose tables are collapsed behind JavaScript and cannot be fetched).

### Still genuinely missing

Nothing on any current path. The chest is the one asset that is *present* but unusable, because it needs a model pipeline before its texture means anything.

**Three folders have earned their keep and are worth knowing about:** `models/entity/` and the Bedrock `.geo.json` equivalents behind `ANIMATION.md`'s pivot tables; `data/minecraft/loot_table/entities/` for what a creature drops, which is the first thing M21 will want; and `data/minecraft/worldgen/` — `configured_feature/ore_*.json` carries the exact vein sizes and counts that our own ore table currently approximates from wiki depth bands.

### ⛔ This dump is **Java's**, and our model geometry is **Bedrock's**

That combination is fine for nearly every mob — the two sheets agree — and it is a
trap for the ones where it is not. A UV transcribed out of a Bedrock `.geo.json`
and pointed at a Java texture can land on **empty pixels**, and the part then
renders as **nothing at all**: no error, no warning, no log line. The dolphin lost
its entire back half to exactly this (Bedrock puts its tail at `(0,33)`, the Java
sheet at `(0,19)`), and the turtle's head net starts one texel further right than
its geometry claims.

**So after transcribing any UV, dump the alpha of that rect and check coverage.**
`tools/analyze-alpha.ps1` and `tools/alpha-runs.ps1` answer it in one command:
full coverage means the origin is right, partial means it has slid, empty means
the part is about to vanish. Doing it costs a minute; not doing it costs a render,
a bug report and a re-measure.

**A fourth lives in `Mojang/bedrock-samples` rather than this dump:** `resource_pack/fogs/*_fog_setting.json`, one per biome, which is the only published answer to "what does being underwater look like". It gave the fog's distance and the fact that `render_distance_type: "fixed"` makes that 60 a number of **metres**. ⛔ **It did not give a usable colour.** `fog_color` is an input the game blends with the water's own colour, so shipping it raw reads grey; ours is measured off screenshots instead, and `SYSTEM_MEMORY.md` "Water" carries the numbers and a do-not-revert box. *A colour in shipped data is not a pixel.*

**A fifth is `resource_pack/animations/*.animation.json`, and it settles arguments a still image cannot.** `spider.animation.json` gave the whole walk cycle — a quarter turn of phase per leg pair, a sweep at twice the rate of the lift, absolute values on both — and one genuine edition difference in a formula that otherwise matches term for term (Java takes the cosine signed, Bedrock does not). `zombie.animation.json` settled the arm swing, which **runs the opposite way round** from the humanoid one and would have been a coin flip from memory. **Fetch the file rather than recalling the behaviour**: the same call also disproved a remembered detail — the shipped animation uses a flat −90° base rather than an aggressive-versus-calm split, so a zombie does *not* raise its arms on sighting you.

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
