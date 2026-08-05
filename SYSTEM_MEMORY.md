# SYSTEM_MEMORY.md

Current technical truth for this voxel sandbox project: what exists, where it lives, what version it is, and how to build and run it.

Narrative history, rejected approaches, and debugging lessons live in `CLAUDE.md`. **If you are new to this project, read `START-HERE.md` first** — one page covering the current state, the build, and the traps. The milestone route and the long-term vision live in `TIMELINE.md`. Every planned recipe lives in `CRAFTABLE.md`, `ASSETS-REFERENCE.md` maps the reference asset dump, `TEXTURING.md` covers box nets and creature models (its §14 is a formula reference), and **`RESEARCH.md` records how the original's mechanics actually work — exact physics constants, spawn rules, mob behaviour and the AI architecture worth copying.** **Bedrock Edition is our primary reference**; `RESEARCH.md` marks Java-only values `[JE]`. **This file is factual and current-state only** — when something changes, replace the old fact in place rather than appending.

> **Status:** Milestones 1–19 complete. **M20 is in progress** — split into M20a (entity foundation, done), M20b (roster and behaviour, *partly* done: thirty-six species on working machinery, with richer animation, stronger pathfinding and non-land archetypes still outstanding) and **M20c (the AI restructure, done 2026-08-04)**. See `TIMELINE.md` for what M20b still owes.
>
> The game is playable: an endless seeded world streams in behind a loading bar, and the player walks, jumps, sprints, crouches, flies, swims, and digs and places blocks. **Digging takes time** — how long depends on the block and the tool — and stone withholds its drop until you hold a pickaxe. **Thirty-six creatures share the world:** eighteen animals plus a villager and a wandering trader graze, wander or hop by day in the regions that suit them; the wolf and the polar bear are neutral and fight back when struck; Brambles, zombies, skeletons and Blackbones hunt after dark, and only the ordinary undead among them burn off at dawn — a Bramble or a Blackbone caught out at sunrise keeps coming; the husk hunts the desert through the day because it does not burn; slimes hop in three sizes and split when killed; the silverfish wriggles through the stony uplands; and the spider and cave spider climb walls, hunting in the dark and ignoring you in the light. They **turn their heads to watch you, jump ledges a step cannot clear, and lose sight of you behind anything opaque**. The **Bramble detonates**, and a charged one does it twice as hard. The first four wear art of ours; the other thirty-two are on placeholder reference art pending an artist. Broken blocks drop as items collected into a 36-slot inventory, opened with `E`, with the full set of click, right-click, click-drag, shift-click and double-click slot interactions and a name label on hover. **Thirty-six spawn eggs** place a creature on right-click. Crafting works in the inventory's 2×2 and a crafting table's 3×3, a furnace smelts with fuel over time, torches light caves, and ten tools wear out with use. Edits, player position, furnace contents and the creature population survive a restart, and `F5` shows per-frame diagnostics.
>
> Generation and meshing run on worker threads, mesh uploads are batched, geometry is greedily merged and frustum culled, the world is lit with sky light, block light, smooth lighting and ambient occlusion, and a placeholder sun crosses the sky. Terrain is divided into seven biomes with caves underneath, oceans that flow, trees whose leaves are alpha-tested, tall grass, slabs, stairs and fences. Every block texture and every non-cube shape has been measured against the reference dump.
>
> **Next:** M20b still owes **richer animation, stronger pathfinding and the water/flying archetypes** — `ANIMATION.md` is the reference for the first, and `TIMELINE.md` scopes the rest. **M21 (survival systems) is the first thing that gives the player health**, which is what every blast and blow already computes and currently throws away.

---

## Critical Invariants

- **`engine/` never references `game/`.** The engine is a static library with no knowledge of the game. Violating this breaks the ability to reuse the engine for tools/servers later.
- **Build only through CMake Presets.** Never hand-invoke `cl.exe` or write build commands by hand; the presets are the single source of truth for compiler flags and output paths.
- **A plain shell cannot build this project — the MSVC environment must be loaded first.** `cl.exe` is not on the global `PATH` by design, and CMake's Ninja generator finds the compiler through `PATH`/`INCLUDE`/`LIB`. Verified by direct test: configuring from a scrubbed shell fails with `No CMAKE_CXX_COMPILER could be found`. Run `. .\tools\dev-env.ps1` first (leading dot required), or build through VS Code's CMake Tools, which loads that environment itself.
- **`CMakePresets.json` must keep its `architecture`/`toolset` fields with `"strategy": "external"`.** That is the signal VS Code's CMake Tools uses to set up the MSVC x64 environment before invoking CMake. Plain CMake ignores them for the Ninja generator, so they cost nothing on the command line. Removing them breaks the entire in-editor build.
- **Validation layers are enabled in Debug builds and disabled in Release.** They are a correctness tool with a real performance cost. A Debug build that reports zero validation errors is the acceptance bar for any rendering change.
- **Every Vulkan object must be destroyed in reverse creation order.** Vulkan does not reference-count. Destroying a device before the things allocated from it is undefined behavior and typically manifests as a driver crash on exit, not at the point of the mistake.
- **PowerShell 5.1 on this machine.** Use `;` to chain, never `&&`. Never send multi-line scripts to the integrated terminal (see `CLAUDE.md`).
- **The Windows user account is not an administrator.** Anything requiring elevation must be launched via `Start-Process -Verb RunAs` for the user to authorize at Windows' own prompt.

---

## Verified Environment

Confirmed working on this machine as of 2026-07-30.

| Component | What it is, in plain terms | Version / Location |
|---|---|---|
| OS | — | Windows 11 Home, build 10.0.26200 |
| CPU | — | Intel Core Ultra 7 155H — 16 cores, 22 threads |
| GPU (discrete) | The fast graphics card | NVIDIA GeForce RTX 4070 Laptop, driver 32.0.16.1047 |
| GPU (integrated) | The power-saving graphics chip built into the CPU | Intel Arc Graphics, driver 32.0.101.8247 |
| Git | Version control | `%LOCALAPPDATA%\Programs\Git` |
| CMake | Describes *how* to build the project; generates the actual build files | 4.4.1, `%LOCALAPPDATA%\Programs\CMake` (portable, user-scope) |
| Ninja | The program that actually runs the compiler, in parallel, as fast as possible | 1.13.2, `%LOCALAPPDATA%\Programs\Ninja` (portable, user-scope) |
| MSVC | Microsoft's C++ compiler — turns source code into an `.exe` | Build Tools 2022 v17.14.37516.0, toolset 14.44.35207, compiler `cl.exe` 19.44.35228 |
| Windows SDK | Header files that let a program talk to Windows itself | 10.0.26100.0 |
| Vulkan runtime | The driver-side piece that actually talks to the GPU | `C:\Windows\System32\vulkan-1.dll` (shipped by GPU drivers, predates SDK) |
| Vulkan SDK | Developer tooling: error-checking layers + shader compiler | LunarG 1.4.357.0, `C:\VulkanSDK\1.4.357.0` |
| VS Code | Editor | CMake Tools 1.23.52, C/C++ (cpptools) 1.32.2 |

`CMake` and `Ninja` were installed as extracted archives with `%LOCALAPPDATA%\Programs\CMake\bin` and `%LOCALAPPDATA%\Programs\Ninja` appended to the **user** `Path` variable. There is nothing to uninstall; upgrading means replacing the folder.

### Two GPUs

This laptop has both a discrete NVIDIA GPU and an integrated Intel GPU, and **Vulkan will happily pick either one.** Physical-device selection therefore prefers a discrete GPU explicitly rather than taking whatever Vulkan lists first. This is not theoretical: the Intel Arc iGPU enumerates as `GPU0` and the RTX 4070 as `GPU1`, so "take the first device" would silently select the slow one. The startup log prints the chosen GPU — check it before investigating any performance complaint.

### Third-party Vulkan layers are present on this machine

`vulkaninfo` reports `VK_LAYER_OBS_HOOK` (OBS) and `VK_LAYER_RTSS` (RivaTuner/MSI Afterburner) installed system-wide, both built against Vulkan 1.3 while this project requests 1.4. The loader warns about the mismatch on every launch. They are harmless so far, but if a bizarre rendering or present-mode bug ever appears that cannot be reproduced logically, disabling these overlays is a cheap early test.

---

## Repository Layout

```
/
├── CMakeLists.txt          Root build script: project name, C++ standard, dependencies
├── CMakePresets.json       Named build configurations (debug/release)
├── .gitignore              Keeps build output and downloaded deps out of Git
├── .gitattributes          Line-ending normalization (Windows/CRLF safety)
├── CLAUDE.md               Why decisions were made (narrative)
├── SYSTEM_MEMORY.md        What currently exists (this file)
├── TIMELINE.md             Where this is going and in what order
├── CRAFTABLE.md            Every planned recipe, its grid, and what art it still needs
├── ASSETS-REFERENCE.md     Map of the reference asset dump and how to read it
├── run.ps1                 Build and launch in one command
├── .vscode/                Editor config + recommended extensions
├── tools/
│   ├── dev-env.ps1         Loads the MSVC environment into the current shell
│   ├── make-block-textures.ps1  Generates the block and item sprites
│   ├── make-creature-skins.ps1  Generates creatures.png rows 0-191, then calls the roster stage
│   ├── make-roster-skins.ps1     Rows 192-767: chicken, cat, camel, horse, mule, llama, donkey, goat, rabbit
│   ├── measure-skin.ps1          Colour count, luminance span, saturation and per-rect means
│   ├── make-reference-creature-atlas.ps1  Temporary proof atlas of reference skins, written beside the exe
│   ├── make-spawn-egg-sprites.ps1     The 36 reference spawn egg sprites, staged beside the exe
│   ├── analyze-alpha.ps1         Lists a texture's connected opaque islands - finds unwrapped model nets
│   ├── alpha-runs.ps1            Per-row opaque runs - step 1 of the model procedure, and what islands cannot do
│   ├── preview-grid.ps1          Magnifies one texture with coordinate lines, for reading net boundaries
│   ├── compare-skin.ps1          Our skins vs reference: nets, structure, patches, flatness, saturation, overlap
│   ├── compare-texture.ps1       Ours against the reference: palette, spread, saturation, run lengths
│   ├── preview-textures.ps1     Magnifies textures into a labelled sheet for review
│   ├── make-font.ps1            Regenerates the ASCII font atlas
│   ├── make-hud-sheet.ps1       Composites the HUD and inventory art into one sheet
│   ├── capture-window.ps1       Screenshots the running game, optionally after sending keys or placing the pointer
│   ├── dump-pixels.ps1          Prints a texture as a character grid with a luminance-sorted legend and counts
│   ├── benchmark.ps1            Sweeps a setting and restores settings.cfg afterwards
│   ├── convert-image.ps1        Any Windows-decodable image (incl. WebP and AVIF) to PNG, with crop and integer downscale
│   ├── extract-ui-icons.ps1     Crops the UI reference capture into reference/ui-icons/, at 3× and 1×. Refuses to write under assets/
│   └── probe-image.ps1          Dumps pixel runs along a row or column
├── engine/                 The reusable engine — a static library, knows nothing about the game
│   ├── CMakeLists.txt
│   ├── include/engine/     Public headers: what the game is allowed to use
│   └── src/                Implementation: private to the engine
└── game/                   The game executable — uses the engine
    ├── CMakeLists.txt
    └── src/
```

**Why the `include/` vs `src/` split:** headers under `engine/include/engine/` are the engine's public surface — anything the game can call. Everything in `engine/src/` is internal. This makes it structurally obvious when the game starts depending on engine internals it should not.

**What sits beside the built executable and is not in the tree.** `build/{debug,release}/bin/` holds `assets/` (copied), `settings.cfg`, `saves/`, `shaders/`, and **two placeholder art drops that must never be moved under `assets/`**: `creatures-reference.png` and `spawn-eggs/`. Both are regenerable, both are restored by `run.ps1`, and both are reference art — see `START-HERE.md` §5.

---

## Build System

### Targets

| Target | Type | Purpose |
|---|---|---|
| `engine` | Static library | All reusable engine code. Produces `engine.lib`, which is linked into the game. |
| `game` | Executable | The actual runnable program. |

A **static library** means the engine's compiled code is copied into the final `.exe` at link time — one self-contained executable, no separate `.dll` to ship. This is the right default until there is a reason (hot-reloading, plugins) to change it.

### Presets

Defined in `CMakePresets.json`. Run these from the repository root:

```powershell
. .\tools\dev-env.ps1        # once per terminal: load the MSVC environment
cmake --preset debug          # configure (only needed after changing CMakeLists.txt)
cmake --build --preset debug  # compile
.\build\debug\bin\game.exe    # run
```

Replace `debug` with `release` for an optimized build. In VS Code, CMake Tools does all of this from the status bar; select the `Debug (validation layers on)` configure preset once and it is remembered.

| Preset | Compiler flags | Validation layers | Use for |
|---|---|---|---|
| `debug` | `/Od /Zi` — no optimization, full debug info | On | Everyday development |
| `release` | `/O2` — optimized | Off | Measuring real performance |

All build output goes to `build/`, which is git-ignored. Deleting `build/` is always safe and is the correct first response to a build that has gone strange.

---

## Dependencies

Fetched by CMake at configure time via `FetchContent`, pinned to exact Git tags. Nothing is committed to the repository.

| Dependency | Version | Why it is here |
|---|---|---|
| GLFW | 3.4 | Creates the OS window, handles keyboard/mouse input, and creates the Vulkan drawing surface. Without it we would hand-write several hundred lines of raw Win32 code. |
| Vulkan | SDK 1.4.357.0 | Found via `find_package(Vulkan)`; not fetched. Provides the headers and the loader import library. |

**Adding a dependency requires a one-sentence justification of why the current milestone needs it.** "We'll want it eventually" is not sufficient.

### Dependency sources are nested Git repositories, and that is expected

`FetchContent` clones GLFW into `build/<preset>/_deps/glfw-src`, which is a real Git repository carrying GLFW's own upstream branches and sitting at `HEAD detached at 3.4`. It is **not** part of this project's history — `build/` is gitignored, so nothing there is ever committed or pushed. `.vscode/settings.json` adds `build` to `git.repositoryScanIgnoredFolders` so VS Code's Source Control panel does not list it alongside the real repository. This project has exactly one branch: `main`.

---

## Engine Architecture

Every type owns its Vulkan resources and destroys them in its destructor.

| Component | Header | Responsibility |
|---|---|---|
| `Log` | `engine/core/Log.hpp` | Minimal timestamped console output at info/warn/error levels. Deliberately trivial — replaced when there is a real need. |
| `Paths` | `engine/core/Paths.hpp` | `executableDirectory()`. Assets resolve relative to the `.exe`, not the working directory, which differs between terminal and editor launches. |
| `FrameLimiter` | `engine/core/FrameLimiter.hpp` | Paces the main loop to a target frame rate, adjustable at runtime. A target of `0` means uncapped. |
| `JobSystem` | `engine/core/JobSystem.hpp` | Fixed-size worker thread pool and job queue. Knows nothing about what a job is. Zero workers runs jobs inline. |
| `Window` | `engine/platform/Window.hpp` | Owns the GLFW window and its lifetime. Reports whether a close was requested, pumps OS events, and exposes a queue of key presses via the engine-level `Key` enum so the game never includes GLFW. |
| `VulkanContext` | `engine/render/VulkanContext.hpp` | Vulkan instance, debug messenger, window surface, physical device selection, logical device, and queues. The one-time setup that everything else needs. |
| `Swapchain` | `engine/render/Swapchain.hpp` | The set of images that get shown on screen, plus their views. Rebuilt when the window resizes. |
| `Vertex` | `engine/render/Vertex.hpp` | **The single definition of a vertex**, together with its Vulkan binding/attribute descriptions. Position, face shade, texture coordinate, and texture array layer. The shader's `layout(location = ...)` inputs must match it. Change the format here and in the shader, nowhere else. |
| `TextureArray` | `engine/render/TextureArray.hpp` | A stack of same-sized images in one GPU resource, sampled by layer index, with a generated mip chain and its own sampler. |
| `MeshPushConstants` | `engine/render/PushConstants.hpp` | The per-draw data block. Must match the shader's `layout(push_constant)` block field for field. |
| `Buffer` | `engine/render/Buffer.hpp` | A `VkBuffer` plus the `VkDeviceMemory` backing it, released together. Copy and move are deleted. `uploadBufferData()` fills device-local buffers via a temporary staging buffer. |
| `DepthImage` | `engine/render/DepthImage.hpp` | The depth attachment. Picks the best supported format (`D32_SFLOAT` preferred) and is rebuilt with the swapchain, since it must match the colour target's size. |
| `Camera` | `engine/render/Camera.hpp` | Position, yaw, pitch, and the view matrix. Pitch clamps just short of vertical, where the up vector becomes ambiguous and the view flips. **Which keys move it is game code's decision, not the engine's.** |
| `GraphicsPipeline` | `engine/render/GraphicsPipeline.hpp` | One complete draw configuration: both shader stages plus all fixed-function state. Loads SPIR-V from disk. Viewport and scissor are dynamic state, so resizing never rebuilds it. |
| `Renderer` | `engine/render/Renderer.hpp` | Command pool, command buffers, per-frame synchronization, and the per-frame record/submit/present cycle. Owns mesh slots addressed by `MeshHandle`, with a free list so removed chunks release their slot for reuse. |

**Vulkan vocabulary, briefly:**
- **Instance** — the connection between the program and the Vulkan library.
- **Physical device** — a GPU that exists on the machine.
- **Logical device** — the program's own handle onto that GPU, with the features it asked for.
- **Queue** — a lane for submitting work to the GPU. Graphics and presentation may or may not be the same lane.
- **Surface** — the bridge between Vulkan and the OS window.
- **Swapchain** — the small ring of images the GPU draws into and the OS displays, rotated each frame so drawing and displaying never touch the same image.
- **Command buffer** — a recorded list of GPU instructions, submitted in bulk rather than one call at a time.

---

## Game Architecture (current)

Everything below lives in `game/` and is invisible to the engine. The engine has no idea what a block is; it only ever receives finished mesh data.

| Component | Header | Responsibility |
|---|---|---|
| `Block` | `world/Block.hpp` | The block ID enum plus the three predicates that must stay distinct: solid (blocks movement), opaque (blocks vision), light-transparent. Also which texture layer each face uses — `BlockFace` is what lets grass differ on top, sides and bottom without special cases in the mesher — and `blockName`, the one place a block is named. Water encodes its depth in the id. |
| `Collision` | `world/Collision.hpp` | The box-versus-world tests, shared by the player and by creatures: `overlapsSolid` and `highestSurfaceBelow`, both reading `collisionBoxes`. **Anything that collides with the world goes through here** — a second copy of "where is this block" is the recurring bug in this project, and creature physics carried one until it was folded in. |
| `Chunk` | `world/Chunk.hpp` | A 32³ block of world as a flat array, indexed `x + z*32 + y*32*32`, plus a parallel light array. Reads outside the chunk return air rather than failing, so callers do not need bounds checks everywhere. |
| `noise` | `world/Noise.hpp` | Seeded value noise and fractal Brownian motion in 2D and 3D. An integer hash, so it is reproducible on any machine without storing anything. |
| `Biome` | `world/Biome.hpp` | The table that answers "which block goes here, and why", and the temperature/humidity selection that picks between rows. |
| `TerrainGenerator` | `world/TerrainGenerator.hpp` | `generateChunk(seed, coord)` — **a pure function**, and required to stay one. No neighbour reads, no global state, no clock. This is what makes the world deterministic and what lets generation run on a worker thread. |
| `ChunkMesher` | `world/ChunkMesher.hpp` | Turns a padded chunk volume into opaque and translucent mesh data, emitting only faces that can be seen. The volume is passed in rather than looked up, which keeps meshing pure. |
| `World` | `world/World.hpp` | Owns every loaded chunk, streams them around the player, and runs light propagation and water flow. The single owner of block state, and the only thing that mutates it. |
| `WorldStore` | `world/WorldStore.hpp` | Reads and writes the save directory. Stores only modified chunks, plus the player's position and view direction, the furnaces, and the creature population. Each side table is its own file with its own magic, version and seed, all checked on load. **A save record is always an explicit struct, never the live one** — `SavedCreature` names the six fields worth keeping, so the format cannot change silently when `Creature` gains a field, and everything transient is discarded on reload by construction. |
| `Raycast` | `world/Raycast.hpp` | Walks the view ray cell by cell to find the block being aimed at, and the empty cell in front of it where a new block goes. Steps block to block rather than sampling at intervals, so it cannot skip a block at any angle. **Two questions, one walker:** `raycast` reads `selectionBoxes` because aiming must be able to pick a tuft of grass, while `hasLineOfSight` reads `isOpaque` because sight must not be blocked by one. Using the aiming ray for vision is what made tall grass hide the player from a creeper. |
| `Sky` | `world/Sky.hpp` | Placeholder day cycle: sun direction over time, sky colour, and the billboarded sun quad. |
| `BlockOutline` | `world/BlockOutline.hpp` | The wireframe cage marking the targeted block. Built from thin solid bars so it needs no second pipeline or line-width support. Sized from the targeted block's selection box, and rebuilt only when that height changes. |
| `Settings` | `core/Settings.hpp` | `settings.cfg` next to the executable. Read once at startup. **`creative_mode` defaults to 1** — survival with no crafting lets you place only what you have already dug up, so flip it once progression exists. |
| `Item` | `item/Item.hpp` | `ItemId`, stacks, and what a block drops when broken. Block items share the block's numbering; anything else starts at `kFirstToolItem`. Non-block items name a sprite layer through `itemTextureLayer`. `itemDisplayName` names either kind, deferring to `blockName` for blocks — casting an item id straight to a block id works only for block items and reported everything else as "Air". |
| `Inventory` | `item/Inventory.hpp` | 36 slots, the first 9 being the hotbar. `add()` tops up matching stacks before using an empty slot. |
| `Recipe` | `item/Recipe.hpp` | Shaped and shapeless recipes in one struct, and the matcher. **Patterns are stored at their own size**, so the matcher slides them around a larger grid — a 1×2 recipe works anywhere in a 3×3 without changes. `craftResult` takes the grid size, which is what lets a crafting table reuse it unchanged. |
| `Smelting` | `item/Smelting.hpp` | What an item smelts into, and how long a fuel burns. Two separate questions, because a log is both an input and a fuel and most things are exactly one. Every recipe takes the same ten seconds. |
| `Furnace` | `world/Furnace.hpp` | One furnace's three slots and its burn and cook timers, plus the tick that advances them. **A block entity** — state belonging to a block that is not part of which block it is. Fuel is only lit when there is something worth cooking. |
| `Tool` | `item/Tool.hpp` | How mining works: what each tool is and how fast, how hard each block is, which tool suits it, and the tier it demands before it drops anything. `breakSeconds` and `yieldsDrop` are the two answers the game actually asks for. |
| `Creature` | `world/Creature.hpp` | Thirty-six species on one entity system: three-axis collision through `world/Collision.hpp`, step-up and jumping, a priority-sorted behaviour table with control flags, local steering, spawning both continuously and with a chunk, retiring, striking, splitting, herd alerting and soft separation. Species policy stays in `kSpecies` — how it moves (`gaitRate` plus `gaitSwing`, which is **radians of limb rotation about the joint**, or `hops` plus `hopLaunch`/`hopGather` for a real ballistic jump), how big it renders (`modelScale`, which is how one horse model serves the mule and donkey), whether it `climbs`, what it `splitInto`s when killed, its `babyChance`, and the `groupSize` it is generated with. **Temperament falls out of existing fields rather than a flag:** `hostile` hunts on sight; not hostile but `attackDamage > 0` is *neutral*, ignoring you until struck then fighting back for a six-second grudge; and `huntsBelowLight` makes a neutral hunt wherever the light reaching it is dark enough, which is the spider. `burnsInDay` is kept separate from `nocturnal` because spawning in the dark and burning at dawn are different rules, and the burn list is far shorter than instinct suggests — `RESEARCH.md` §13.4 names it, and **only the ordinary undead are on it**: zombie, zombie villager, skeleton, stray, bogged. The Bramble, Blackbone, husk, witch, spiders, slimes and silverfish all spawn dark and stay. **The field defaults to `true`, so a row that does not restate it burns** — which is exactly how the Bramble and the Blackbone were wrong until 2026-08-04. **Every field with a default sits at the end of the struct**, so rows written before it existed still compile; adding one in the middle silently shifts every brace-initialised row after it. `Creature::scale` is the one per-individual number and multiplies the collision box and the model together. Box layouts stay in `buildMesh`, where `uprightBox` takes an optional nose-down `pitch`: a level muzzle reads as a cheek patch and a level rabbit reads as a brick. It also takes `growSide`, which extends the side axis alone — `grow` swells every axis at once, so reaching toward a neighbour with it thickens the part as much as it lengthens it. Models sample the 128×1888 `assets/textures/creatures.png` net sheet through layer `-3.0` (`-4.0` while hurt); anything see-through goes to a second, translucent mesh, which so far is only a slime's shell. Every net origin is measured off the reference alpha. The per-second census is generated from the enum rather than hardcoded names. See `TEXTURING.md`. |
| `Explosion` | `world/Explosion.hpp` | What a blast removes, and what it does to whatever is standing in it. **`blastResistance` is not mining hardness and the two must never be conflated** — stone is 1.5 to a pickaxe and 6 to an explosion, obsidian 50 against 1200; `Tool.hpp` owns one number and this owns the other. `explosionBlocks` is the reference's algorithm exactly: 1352 rays toward the faces of a 16³ grid, each starting at `power × [0.7, 1.3)`, advancing 0.3 blocks and paying a flat 0.225 per step plus `(resistance + 0.3) × 0.3` for what it passes through — a block is taken only if the ray still has intensity *after* paying, which is what makes a crater ragged rather than spherical. `explosionExposure` is the share of sample points on a box the blast can see, so cover genuinely protects; `explosionImpact` combines it with distance, and both damage and knockback read that one number rather than recomputing it. Damage is `7 × power × (impact² + impact) + 1`, scaled by `27.5 / 43` to land on **Bedrock's** point-blank figure instead of Java's harsher one. The trailing `+1` means everything inside twice the power takes at least a point even fully shielded. |
| `slots` | `item/SlotOps.hpp` | What a click does to one slot given what the cursor holds: left takes or merges a whole stack, right takes half and places one, `distribute` spreads a stack over several slots, `quickMove` sends one elsewhere (shift-click) and `gather` pulls matching items in (double-click). Free functions over stacks, because the crafting grid is not part of the inventory but obeys the same rules. |
| `hud::HudPrimitives` | `hud/HudPrimitives.hpp` | Screen-space building blocks: quads, sprite-sheet regions, free-corner quads, text, isometric block icons, a slot's item-and-count, and the hover label. The label sizes its frame from the text and clamps itself to the window, flipping above the cursor rather than overflowing the bottom edge. `kSheetSize` is the **single owner** of the HUD sheet's dimensions (currently 185 × 773) and must follow whatever `tools/make-hud-sheet.ps1` reports; a stale copy silently skews every sprite on the sheet, so `Main.cpp` reads the PNG header and logs an error on mismatch. The game loads `hud-reference.png` from beside the executable in place of `assets/textures/hud.png` whenever it exists — the same proof-atlas arrangement the creature skins use, and it currently supplies the five catalogue tabs. |
| `Crosshair` | `hud/Crosshair.hpp` | The aiming reticle. |
| `Hotbar` | `hud/Hotbar.hpp` | The nine-slot bar, drawn from the HUD sprite sheet. |
| `InventoryScreen` | `hud/InventoryScreen.hpp` | **Every** container screen: the player's inventory, a crafting table and a furnace. One `Kind` picks a small layout table — which panel sprite to draw, where the slots sit, and how wide the grid is — so the slot interaction exists once rather than three times. A furnace borrows the crafting region for its input and fuel, which is why it needed no new click handling. Every position is measured off the art in its own pixels and scaled through one constant. The inventory and crafting-table screens also draw the **catalogue card** beside the panel: a 146 × 166 second card, a 4-unit fold and a 176-unit inventory card, centred as one 326-unit block. **`toScreen(Kind, x, y)` is the single owner of that horizontal offset** — every slot centre, hit test and panel bound comes through it, so widening the screen was a translation rather than a rewrite of the click handling. Its scroll position, search text and selection live in a `CatalogueState` the caller owns, keeping `build` a pure function of its arguments. |
| `DebugOverlay` | `hud/DebugOverlay.hpp` | The `F5` diagnostics panel: frame-time graph and labelled rows. |
| `Player` | `world/Player.hpp` | Player box, motion constants, and `updatePlayer()`, which reads the world and writes only the player. Input arrives as a `PlayerInput` struct, so the physics never touches the keyboard. |

---

## Frame Pacing

The loop is capped so it does not render frames nobody sees. **Default cap: 120 fps.** `F1` steps the cap down, `F2` steps it up, through `30 / 60 / 90 / 120 / 144 / 165 / 240 / uncapped`. Changes take effect immediately and are logged.

The cap list and key bindings are **game policy** and live in `game/src/Main.cpp`; `FrameLimiter` itself only knows about a target number. The keyboard control is a temporary stand-in until there is a real settings screen — it is not intended to be the permanent interface.

The limiter is deliberately independent of the swapchain present mode, which is `mailbox` (render as fast as possible, always show the newest finished frame, no tearing). Using vsync/`FIFO` instead would lock the frame rate to the monitor's refresh rate, which is not the same thing as a user-chosen cap.

**Implementation note:** the wait uses a Win32 high-resolution waitable timer plus a sub-millisecond spin, not a plain sleep — see `CLAUDE.md` for why a plain sleep produces roughly 64 fps when asked for 120.

---

## Shaders

Shader source lives in `engine/shaders/` as GLSL. GPUs cannot read GLSL, so `glslc` (from the Vulkan SDK) compiles each file to **SPIR-V** bytecode at build time, writing `<name>.spv` into `build/<preset>/bin/shaders/`. Each shader has its own CMake rule depending on its own source, so editing one shader and rebuilding recompiles only that shader — no C++ rebuild required.

At runtime the pipeline loads them via `executableDirectory() / "shaders"`. **Never load assets by a plain relative path**; the working directory differs between a terminal launch and an editor launch.

## Rendering Approach

**Dynamic rendering, not render passes.** `vkCmdBeginRendering` (core in Vulkan 1.3) names the target images directly, so there are no `VkRenderPass` or `VkFramebuffer` objects to create up front or keep synchronized with the swapchain across resizes.

This requires two things that must not be removed:
- `VkPhysicalDeviceVulkan13Features::dynamicRendering` enabled at device creation.
- Physical-device selection rejects anything reporting less than `VK_API_VERSION_1_3`, so an unsuitable GPU produces a clear "no suitable GPU" error rather than a confusing failure inside `vkCreateDevice`.

The swapchain image is transitioned `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL` before rendering and `→ PRESENT_SRC_KHR` afterwards, and the submit waits at `COLOR_ATTACHMENT_OUTPUT`. The depth image is transitioned `UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL` at `EARLY_FRAGMENT_TESTS` and cleared to `1.0` (the far plane) each frame; its contents are never needed after the frame, so `storeOp` is `DONT_CARE`.

### Winding and culling — do not "fix" this by reasoning

Geometry is wound **counter-clockwise seen from outside**, and the pipeline uses `VK_CULL_MODE_BACK_BIT` with **`VK_FRONT_FACE_COUNTER_CLOCKWISE`**, despite the projection flipping Y. This combination was established by looking at the screen, not derived — a hand derivation argued for `CLOCKWISE` and was wrong. Getting it backwards renders every closed object as a hollow shell viewed from inside, and **produces no validation error and no warning of any kind**. If this is ever touched, verify against a closed box viewed from outside. See `CLAUDE.md`.

### Camera and projection

Projection is built by the renderer from the **swapchain's** extent, not the window's, so the aspect ratio cannot disagree with what is actually drawn. **Vertical FOV is 70°**, set by the game (`kDefaultFov` in `Main.cpp`) and stepped by `F3`/`F4` between 50 and 110; `Renderer` clamps anything it is given to 30–130. It was 45° up to M3, when the camera sat close to a single cube and 60° visibly warped it — that is a near-subject artefact, not a limit, and it stopped applying once there was a world to stand in. `projection[1][1] *= -1` converts GLM's OpenGL-style Y-up clip space to Vulkan's Y-down. `GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined **PUBLIC** on the engine target because every translation unit doing matrix maths must agree on the depth convention.

Scene geometry is supplied in world space, so `Renderer::drawFrame` takes only a view matrix.

---

## Controls

Temporary, until there is a real settings and input-binding screen (`TIMELINE.md` M34).

| Input | Action |
|---|---|
| Mouse | Look (raw motion, bypassing OS pointer acceleration) |
| `W` `A` `S` `D` | Move horizontally, relative to facing |
| `Space` | Jump (walking) / rise (flying) / swim up (in water) |
| `Left Shift` | Sneak (walking) / descend (flying) |
| `Left Ctrl` | Sprint (also sprint-fly) |
| Double-tap `Space` | Toggle flight |
| Left click | Dig the targeted block. **Hold** — how long depends on the block and what you are holding, shown by a bar under the crosshair |
| Right click | Place against the targeted face (hold to repeat), **release a held spawn egg**, or open an interactive block such as a crafting table or furnace |
| `Left Shift` + right click | Place against an interactive block instead of opening it |
| `1` – `9` / scroll | Select a hotbar slot |
| `Q` | Throw one of the held item (hold to repeat); empties the cursor while a screen is open |
| `E` | Open and close the inventory |
| `Escape` | Close an open screen, or release the mouse cursor when none is open |
| Left click | Recapture the cursor when released |
| `F1` / `F2` | Lower / raise the frame cap |
| `F3` / `F4` | Narrow / widen the field of view (default 70°) |
| `F5` | Toggle the diagnostics overlay |
| `F6` / `F7` | Decrease / increase render distance (saved to `settings.cfg`) |
| `F8` / `F9` | Spawn a Bramble 12 m ahead / a charged one. **Debug** |
| `F10` | Swap the creative inventory between the 36 spawn eggs and the block kit. **Debug** |

**The debug keys are deliberately their own keys, not modifiers.** `Shift+F8` spawned a charged Bramble for about an hour, and because `Shift` is *sneak* the act of spawning one left the player crouched at 1.3 m/s against a creeper doing 2.4 — so backing away, which is the entire defence against an exploder, could not work. It read as a fuse bug and was an input one.

**Inventory screen**, following the rules the genre established — anyone who has played one of these already knows them, and getting them subtly wrong is more jarring than not having them at all:

| Input | Action |
|---|---|
| Left click a slot | Take or put down a whole stack; merges onto a matching stack and keeps the remainder; swaps if different |
| Right click a slot | Take **half**, rounded up (7 leaves 3); or place **one** at a time |
| Left click and sweep | Spread the carried stack **evenly** over every slot crossed |
| Right click and sweep | Place **one** in each slot crossed |
| `Left Shift` + left click | Send the stack to the other half: hotbar to storage, storage to hotbar, crafting grid to the inventory. On the result slot it crafts **as many as will fit** |
| Double left click | Pull every matching item in the inventory onto the cursor, smallest stacks first |
| Hover a slot | Shows the item's name in a floating label, whenever the cursor is not carrying anything |
| Click outside the panel | Left throws the whole carried stack into the world, right throws one |

A sweep only starts from a cursor that was *already* carrying something, and only counts slots that are empty or hold the same item. The distribution is applied live and replayed from scratch as the sweep grows, so what you see during the drag is what you get.

**Closing a screen always empties it back into the inventory** — the cursor stack and every crafting slot, whether you close with `E` or `Escape`. Anything that will not fit is thrown into the world rather than destroyed.

Movement is scaled by delta time and the direction vector is normalised, so diagonal movement is not faster than straight movement. Movement direction is flattened to the horizontal plane, so looking down does not drive the player into the ground.

---

## Player Physics

The player is an axis-aligned box, **0.6 m wide, 1.8 m tall**, with eyes at **1.62 m**. `Player::position` is the centre of the feet, because that is the natural anchor for standing on a surface. One block is one cubic metre, so these are directly comparable to real human proportions.

| Quantity | Value |
|---|---|
| Walk / sprint / sneak | 4.317 / 5.612 / 1.295 m/s |
| Fly / sprint-fly | 11 / 22 m/s |
| Fly acceleration / deceleration | 38 / 26 m/s² |
| Ground acceleration / deceleration | 30 / 42 m/s² |
| Air acceleration / deceleration | 9 / 2 m/s² |
| Swim rise / sink | 5.0 / 3.0 m/s |
| Gravity | 32 m/s² |
| Terminal velocity | 78.4 m/s |
| Jump apex | ~1.25 blocks |
| Automatic step-up | 0.6 m |

These are tuning numbers, not part of the game's identity, and are expected to change once there is real content to move through.

**All movement eases in and out**, as a single horizontal velocity vector rather than per axis, so changing direction curves through the turn instead of stopping one axis and starting another. Stopping is quicker than starting, which is what keeps the player feeling planted rather than skating. Airborne rates are far lower — steering mid-jump is deliberately feeble and air drag is nearly nothing, so a jump commits to its arc. Flight ends when a *downward* move collides — clipping a wall sideways does not land you, and hovering has no vertical movement to collide at all.

**In water**, buoyancy cancels most of gravity: holding jump climbs, releasing it drifts slowly down, and horizontal speed drops. Water is survivable rather than a pit to drown in — there is no breath meter or drowning damage, and there is no swimming animation.

**Collision resolves one axis at a time** — vertical first, then X, then Z. Resolving all three simultaneously leaves the maths unable to tell which direction to push out of a corner, which shows up as jitter or as sliding diagonally through walls. Vertical runs first so that "am I on the ground" is settled before the horizontal move decides whether a step-up is permitted.

**Resolution reads the same shape table the overlap test does, on every axis.** `blockingPlaneAlong` finds the face that actually stopped the box rather than assuming a cell boundary. Snapping to `floor()` is only correct for full cubes — a slab's top is halfway up its cell and a stair's step starts halfway across it, and both produced spectacular bugs before this existed. See `CLAUDE.md`; this one root cause has surfaced three times.

`updatePlayer` **clamps its own delta time to 50 ms**. A long stall must not let the player travel far enough in one step to pass straight through a wall; the collision test only looks at blocks the box overlaps, so it cannot see anything it skipped over.

A 1 mm skin is kept between the box and surfaces it rests against, so a resolved contact does not immediately re-report as a collision.

Movement is also **split into steps of at most 0.4 m**. The resolver snaps out of at most one block of penetration, and flying (22 m/s) or a long fall (up to ~64 m/s) covers more than a block per frame at low frame rates — the snap would then land on the far side of the wall.

**Crouching** drops the box to 1.5 m and the eyes to 1.27 m, shrinking from the top so the feet stay put. It is held on the player rather than read from the key each frame, because standing up is refused when there is no headroom. The collision box switches instantly; only the camera is eased, at 8 m/s.

While crouched and supported, any step that would leave nothing underfoot is undone. This is checked per axis, so an edge can still be slid along, plus once more at the end of the whole step as a backstop — auto step-up resolves its own position and would otherwise skip the per-axis checks entirely.

---

## Block Textures

Blocks are drawn from a **2D texture array**, one 16×16 layer per material, not from an atlas. Mipmapping an atlas averages across tile boundaries, so distant stone picks up the colour of whatever was packed beside it. Array layers share no edges, so mip generation is simply correct. The cost is that every layer must be the same size, which for block textures is wanted anyway.

| Setting | Value | Why |
|---|---|---|
| Format | `R8G8B8A8_SRGB` | The swapchain is `B8G8R8A8_SRGB`, so the hardware applies the sRGB curve on write. Sampling must undo it or everything looks washed out. |
| Magnification | `NEAREST` | Keeps a texel a crisp square up close. This *is* the blocky look. |
| Mip mode | `LINEAR` | Blends between mip levels, which stops distant terrain shimmering while moving. |
| Mips | Full chain, blitted | Built with `vkCmdBlitImage`, halving each level. |

A vertex carries a texture coordinate and a layer index. The layer is declared `flat` in the shaders — interpolating it would make a triangle sample a blend of two different textures across its surface.

`Vertex::color` is no longer material colour; it is **face shading** multiplied with the sampled texel, so white leaves a texture untouched.

**The reference's own block and item art is currently staged over ours**, in `blocks-reference/` beside each executable, chosen per texture so `white.png` and `sun.png` — which have no counterpart — keep ours. `tools/make-reference-blocks.ps1` writes it and **never rescales anything**: `water_still.png` is a 16×512 strip of 32 animation frames and is cropped to frame 0, and every output is asserted 16×16 or the script stops, because a texture array needs one size and a silent resize would blur exactly one layer. Five reference textures ship **greyscale** because the game tints them at runtime — `grass_block_top`, `grass_block_side_overlay`, `oak_leaves`, `short_grass` and `water_still` — so the plains tints are applied on the way through; stone, cobblestone and the furnace look grey to a naive test but are naturally grey and must not be tinted. `grass_side` is two reference images composited into one of ours. Delete the folder to return to our own art everywhere.

### Creature skins

All thirty-six models read their box faces out of `assets/textures/creatures.png`, one net per species at a named row offset. **Every net origin and box dimension is recorded in `TEXTURING.md` §13** and the code must agree with it exactly.

The sheet is generated in **two stages, by two scripts with disjoint row ownership**:

```powershell
powershell -NoProfile -File tools\make-creature-skins.ps1   # runs both stages
```

`make-creature-skins.ps1` draws rows 0-191 — sheep, cow, pig, Bramble — saves the sheet, then calls `make-roster-skins.ps1`, which reopens it and paints the rest. They are separate files because both grew a `Set-Solid` and a `Set-Vignette` with different signatures; merging them verbatim would silently redraw four species that are already signed off. Each stage prints which rows it wrote.

**Rows 0-767 are the only ones our scripts paint. Rows 768-1343 — wolf, frog, fox, ocelot, polar bear, panda, the slimes, both spiders, the zombie, the skeleton and the villager — carry no art of ours at all** and render entirely from the reference proof atlas, which is why `creatures-reference.png` sits beside each executable. **This is sanctioned and temporary, not a mistake to clean up** — see `START-HERE.md` §5 for the arrangement and why it exists. Do not re-author these skins; keeping `TEXTURING.md` §13 and §14 accurate is the useful contribution.

**The reference-art boundary:** reference art may be **measured from**, must **never live under `assets/`**, and must **never ship in a release**. `tools/make-reference-creature-atlas.ps1` enforces the second mechanically — it refuses to write under `assets/`. It composites over our own sheet from row 192 up, so **rows 0-191 (sheep, cow, pig, Bramble) always render our art, and the nine species on rows 192-767 have our art in the sheet but are covered while the atlas exists.** Deleting the atlas returns those nine to our skins and leaves everything above row 767 untextured. `assets/textures/hud.png` is the other sanctioned exception, also to be replaced before release.

Dropping in finished art means replacing `assets/textures/creatures.png` (or having `make-roster-skins.ps1` stamp the new pixels), then deleting `creatures-reference.png` from both `build/*/bin/`. The game prefers the atlas only while it exists, and `run.ps1` rebuilds it whenever the sheet is newer — so a stale atlas cannot silently serve old art, which it did once for a whole session.

### Reviewing a model

`creature_showcase` in `settings.cfg` freezes the spawner and lines the roster up in front of spawn: `1` is every species in a staggered grid, and **`2` and above is one species alone in three copies** — profile, facing the camera, facing away. The value is the `CreatureKind` index + 2, because 0 is off and 1 is the grid:

| value | species | value | species | value | species | value | species |
|---|---|---|---|---|---|---|---|
| 2 | Sheep | 9 | Horse | 16 | Frog | 23 | Slime, large |
| 3 | Cow | 10 | Mule | 17 | Fox | 24 | Spider |
| 4 | Pig | 11 | Llama | 18 | Ocelot | 25 | Cave Spider |
| 5 | Bramble | 12 | Donkey | 19 | Polar Bear | 26 | Zombie |
| 6 | Chicken | 13 | Goat | 20 | Panda | 27 | Skeleton |
| 7 | Cat | 14 | Rabbit | 21 | Slime, small | 28 | Villager |
| 8 | Camel | 15 | Wolf | 22 | Slime, medium | 29 | **Husk** |
| | | | | | | 30 | **Silverfish** |
| | | | | | | 31 | **Blackbone** |
| | | | | | | 32 | **Stray** |
| | | | | | | 33 | **Bogged** |
| | | | | | | 34 | **Zombie Villager** |
| | | | | | | 35 | **Witch** |
| | | | | | | 36 | **Wandering Trader** |
| | | | | | | 37 | **Princepin** |

Two things to know before using it. **Creatures are frozen**, so they never settle onto the terrain — a copy standing on a slope will look half-buried, and that is the showcase, not the model. And **the camera and creature yaw conventions are 90° apart** (`Camera::forward()` is `(cos yaw, sin pitch, sin yaw)`; a creature's is `(sin yaw, 0, cos yaw)`), which is why the showcase lays its rows out along its own axis rather than the camera's.

**Anything that writes `settings.cfg` must restore it.** A benchmark once left `frame_cap=0` behind and the user reported the frame limiter broken — twice. Back the file up and restore it in a `finally`.

### How a population arrives, and what happens to it

**Two spawn systems, not one**, which is how the reference does it too.

- **At chunk generation.** The first time the player comes within two chunks of one it has not populated, a chunk rolls for its own group of animals and places them, **ignoring the ordinary population cap**. It is a pure function of `(seed, chunkCoord)`, so a given chunk always produces the same herd however you approach it — the same rule the terrain generator follows. Ten percent of chunks get anything at all; which species and how many come from `groupSize` in the table. This is most of what makes a world feel inhabited on arrival rather than filling in behind you.
- **The continuous cycle**, every 2.5 s within 12–30 m of the player, held to a cap of 14. Unchanged.

`m_populated` records which chunks have had their one-off group and **only ever grows** — a chunk gets its animals once per session, and re-populating on re-entry would breed a herd out of walking back and forth. A separate `kGeneratedCeiling` stops a long walk across grassland stacking herds without limit.

**A share of every natural spawn is young.** `babyChance` is per species, matching the reference's rates — 5% for most farm animals, 10% wolf and llama, 20% the horse family, 25% cat. A baby is one number, `Creature::scale`, which multiplies the **collision box as well as the model**, plus a slightly quicker walk. So a calf fits through gaps its mother cannot, and a herd reads as a family.

**Striking one rouses its own kind nearby** — 16 m out and 10 m up. Neighbours that can bite turn on the player; those that cannot bolt with it. One mechanism, and it is both pack anger and herd flight. **A blow that kills outright propagates nothing**, which is the reference's own exemption and stops one-shotting a lone animal turning its whole species on you.

**Creatures push each other apart** with a soft horizontal impulse after the world collision has run, never vertically — again the reference's choice. It is a nudge, not a constraint, so one standing on another stays there rather than being squeezed out. The pass is quadratic, which is fine at this cap; past a hundred it would want a grid.

**The population survives a restart.** It is written to `creatures.dat` beside `player.dat` and `furnaces.dat` on save, as an explicit `SavedCreature` record rather than the live struct — a save format has to be a stated list of fields, and everything transient (gait, timers, what it was thinking) is exactly what a reload should throw away. Anything the player has since walked away from is retired by the first `manage`, so a stale saved position corrects itself.

### How a creature decides what to do

**A priority-sorted behaviour table with control flags, not a state machine.** Bedrock's shape, not Bedrock's format — the table is a `constexpr` array of structs in `Creature.cpp`, so the compiler checks it and it costs nothing to load. `RESEARCH.md` §8.7 records what was deliberately skipped: runtime JSON, component groups, a filter language, and navigation class hierarchies. There are six rows, not a hundred and ninety.

A row is a **priority** (lower is higher, matching Bedrock), a **control-flag mask**, and `canStart` / `tick` / `canContinue`. `canContinue` exists because *why a behaviour keeps running* is a looser question than *why it started* — a hunter gives up further out than it engages, which is the hysteresis that used to be an `if` inside the mood block.

**Control flags are the whole difference between this and a switch.** Three bits — `Move`, `Look`, `Jump`. Rows claiming *disjoint* flags run at the same time; rows claiming the *same* flag are exclusive and the higher priority wins. A row claiming **nothing** always runs when it can, which is not a degenerate case but the mechanism by which targeting works.

| Priority | Behaviour | Claims | What it does |
|---|---|---|---|
| 1 | `Panic` | Move | Prey bolting. Only species that cannot bite. |
| 1 | `HurtByTarget` | — | Struck, or a neighbour of its kind was. Writes the target. |
| 2 | `NearestAttackableTarget` | — | Hostile, or `huntsBelowLight` satisfied, and in range. Writes the target. |
| 3 | `MeleeAttack` | Move + Look | Closes on whatever is in the target slot and bites it. |
| 6 | `Wander` | Move | Ambling. Always able to start, so it is the floor. |
| 7 | `LookAtPlayer` | Look | An occasional glance. Bedrock's 8 m, 2–4 s numbers. |

**Target production is split from target consumption**, and that split is why retaliation, pack anger and hunting on sight are three rows rather than three branches. The producers write `Creature::target`; `MeleeAttack` reads it and never asks why it is set. `strike` and `alertNeighbours` no longer decide anything — they set `provokedTimer`, the six-second grudge, and the table works out whether that means fighting back or running.

**`target` and `running` are re-derived from nothing every tick**, so neither can go stale: a producer that stops running stops asserting the target, and the consumers simply find nothing there.

**A creature's head turns separately from its body.** `LookAtPlayer` claims `Look` alone and sits *below* `Wander`, which claims `Move` — so the two must run together, and a chicken that walks and watches at once is the proof that flag arbitration works. In `buildMesh` the head parts are drawn between `beginHead(neckForward, neckUp)` and `endHead()`, which swap the three local axes for the duration; a species that never calls them renders exactly as it did before, which is what let this be rolled out one animal at a time. The frog, the slimes and the silverfish have no separable head and do not turn one.

**The head turns about the neck joint, and getting that wrong is very visible.** It used to rotate the head's frame about the *creature's own origin*, so anything placed forward of centre **orbited** rather than turning — the head physically swung out sideways and lunged forward as it looked, dragging the neck with it. `beginHead` now takes the neck joint in model units and the frame turns about that, so the neck stays planted and the head turns on top of it. The pivot is taken from the head box's own numbers by one of three rules: the **rear face** for a head carried out in front, the **bottom face** for one sat on top, and the **base of the neck** for anything long-necked, where the neck is part of what turns. Splitting the old single `headParts(bool)` into two lambdas is what makes that safe — a head group that forgets its pivot does not compile.

**Heads pitch as well as yaw**, clamped to 40° against the yaw's 52°, eased on the same clock, and aimed eye to eye so a creature looks at the player's face rather than their feet. Pitch is far cheaper than yaw: no wrapping, no body-follow, no separate accumulator.

### What a Bramble's blast actually does

The creature system **reports** a detonation and never performs it: `update` fills a `blasts` list and the owning loop rewrites the world, because `Creatures` reads the world and must never write it. That split is why the damage arrives in three separate places.

1. **Blocks** — `explosionBlocks`, then `setBlock` to air. A furnace caught in it spills its contents exactly as breaking one does, and **one block in `power` survives as an item**, which is the reference's rule and the reason a blast is a net loss rather than a mining technique.
2. **Creatures** — `Creatures::applyExplosion`. Damage, a hurt flash, and a throw aimed at the body's middle so a close blast lifts as well as shoves. Anything reduced to zero is retired by the next `manage`, so **a slime caught in a blast still splits**.
3. **The player** — knockback only. There is no player health until M21, and a hostile that shoves you is honest feedback until then.

**Two traps this has already fallen into.** The damage maths was written and only ever run for the player, so the first playtest levelled the terrain around a completely unharmed goat — *a function that is only defined is untested; call it or it is not a feature.* And both throws are **assigned or taken as the strongest of the frame, never accumulated**: several blasts each adding their own is the same accumulator bug that once launched the player far enough to despawn the entire map.

**A blast does not provoke.** `provokedTimer` can only ever mean "the player did this", because the target slot has no way to name anything else — so rousing an animal a Bramble blew up would send it after the player for something they did not do. Revisit when a creature can target another creature.

**A charged Bramble doubles the blast and nothing else.** Power 3 becomes 6; health stays at twenty, which is worth writing down because the obvious guess is that it is tougher. Its energy shell is the reference's `creeper_armor` overlay on the **same net as the Bramble itself**, living in the sheet's last 32 rows — the same boxes drawn again three quarters of a texel larger, in the translucent pass at 45% alpha. It needed no shader change at all, because creature skins are already alpha-tested cutouts and the overlay is mostly transparent. That is the sheep's fleece arrangement exactly. **`charged` is the one per-individual property besides `scale` that persists**, so a charged one does not quietly reload as ordinary. The reference makes them with lightning; we have no weather until M27, so 5% of Brambles arrive charged instead.

### How limbs move

**A limb hangs from a joint and turns about it.** It used to be an axis-aligned box slid back and forth, which is why animals read as skating: a translated foot travels forward without ever leaving the ground, and the top of the leg slides out from under the body. Rotating fixes all of that at once — the foot lifts by `L(1 − cos θ)`, it eases into each end of the stride instead of shuttling at constant speed, and the near and far legs foreshorten differently, which is what separates the four legs of a quadruped visually.

**The pivot is derived from the box, never authored.** `legBox` puts it on the limb's own centre line, below the top face by half the limb's thinner cross-section. That overhang is the point rather than a detail: tilting swings the top corners, so material has to stay above the joint or a wedge opens at the hip every stride — which is exactly why the reference's biped arm sticks 2 texels up past its shoulder. A pivot written into `kSpecies` would be a second copy of a number the geometry already owns, and that is this codebase's most repeated bug.

**`barBox` is the second pivot location.** A spider's leg runs sideways and hinges at its inner end, so its swing is a yaw rather than a tilt — it turns the local axes the way the head turn does. The silverfish is the one genuine exception: it has no limb to turn, so its wriggle stays a sideways *distance* and lives in `buildMesh` beside the segments rather than in the species row.

**Amplitude is eased, and it does two jobs.** `limbSwingAmount` chases the speed the creature actually achieved — not the speed it wanted — with the reference's ~0.3 s lag, so legs spin up and wind down instead of popping between still and striding, and one amplitude covers both a walk and a run with no second animation. It saturates against the species' **own `runSpeed`** rather than the reference's flat 5 m/s, because our roster moves at about a third of Minecraft's speeds; measured against theirs, nothing here would lift a foot. Being driven by distance actually covered means being blocked, shoved or knocked back winds the legs down for free.

**Amplitudes are capped at 0.70 rad, half the reference's 1.4.** Rotating lifts a foot a long way — at the reference's 80° it is 83% of a leg length — so a full-amplitude roster prances. The several species sitting on the cap are the short-legged and the fast, which is the set the reference gives one flat amplitude anyway.

**Every biped but three carries the reference's idle arm sway**: a roll of 0…5.7° on a 3.5 s period and a pitch of ±2.9° on a 4.7 s one, deliberately incommensurate so it never visibly loops, against a per-creature age so a crowd does not sway in unison. The three left out are the villager, witch and wandering trader, whose arms are a folded three-box assembly that is closed rather than solved.

### Getting up a hill

**Two mechanisms, kept apart exactly as the reference keeps them.** `stepHeight` is how high a rise a creature simply walks up, with no airtime — 0.6 for almost everything, **1.0** for the horse family, the llama and the frog, **1.5** for the camel. `jumpHeight` is how high it can jump when a rise is taller than that, and it is `1.2522` m for everything else, derived from the reference's `jump_strength` of 0.42. **Anything whose step height already clears a full block never jumps**, which is why a horse flows over a ledge and a chicken hops it.

A jump is real physics: the launch speed is whatever reaches that height under creature gravity, so the arc, the airtime and the landing all fall out rather than being animated.

**A step rises to the surface, not by the step height.** Those are different numbers and using the second for the first is what made a step a teleport: `stepHeight` is how high a creature *can* climb, while how far it *should* rise is a fact about the block it is climbing onto, which the world owns. Lifting by the whole allowance overshot every slab and stair by the difference and then dropped back under gravity — a stutter on every step — and for a camel, whose allowance is a block and a half, it was a launch. The destination surface now comes from the same `highestSurfaceBelow` that landing uses.

**What eases is the drawn body, never the box.** A step-up has to move the collision box in one go; ramping it would leave a creature part-way inside a block and is a fresh source of stuck states. So `Creature::stepSmooth` records how far the *rendered* body still trails behind and decays over about a sixth of a second. It is deliberately a lie told to the eye: physics, targeting, reach and separation all read the true position, and only `buildMesh` may touch it. Descent needs no equivalent — walking off a ledge is already gravity.

Three things make it actually work, and each was a separate bug on the way:

- **The steering fan has to agree.** It rejected any heading blocked at foot height, so creatures routed *around* every rise and the jump could never fire. It now accepts anything clearable by a step or a jump.
- **"Which axis failed to move" is not a test for being blocked.** Walking along a wall blocks one axis and slides freely on the other. The trigger probes ahead instead — blocked at foot height, still blocked at step height, clear at jump height. The spiders' wall-climb had been on the same broken test since M20b and only worked diagonally.
- **Hoppers needed their own answer.** A rabbit's ordinary hop reaches 0.34 m, so it nudged into every ledge forever. A hop that meets something it cannot clear launches to the same 1.2522 m; every other hop is untouched.

**A chicken comes down slowly and flaps the whole way**, and the two are one mechanism rather than two — `fallDrag` scales the descent and the same airborne state drives the wing beat. Terminal descent is **1.95 m/s** against the 60 everything else reaches, which is exactly why the reference needs no fall-damage exemption for chickens: they never land hard. **Chicks are covered for free**, because `scale` never enters into it. The drag factor is re-solved for our timestep rather than applied per frame, since `v = (v - g·dt)·k` settles at a different speed when a frame is not a tick; the form used reduces to the reference's 0.6 at 20 Hz. The wings hinge at the shoulder using a **roll** axis on `uprightBox` — the first rotation in the model system about the forward axis — because rotating a box about its own centre swings the tip out and the root into the body.

### Spawn eggs

**One per species, right-click to place that creature.** The egg's item id, its sprite layer and the `CreatureKind` it produces are all **the same offset from their respective firsts**, so `spawnEggFor(kind)` and `creatureForSpawnEgg(item)` are arithmetic rather than a thirty-six row table that nobody would keep in step. `ItemId::SpawnEggFirst`, `TextureLayer::SpawnEggFirst` and `CreatureKind` are one order, and a `static_assert` ties the count to `CreatureKind::Count`.

**They are appended after every tool, deliberately.** `isTool` is a range test bounded by `StoneHoe`; an egg inserted among the tools would become a one-slot item that wears out and has mining power.

**The sprites are Mojang's, staged beside the exe** in `spawn-eggs/`, exactly like `creatures-reference.png` and for the same reason — reference art may be measured from and used as a placeholder, but must never live under `assets/` and must never ship. `tools/make-spawn-egg-sprites.ps1` refuses to write under `assets/`, and `run.ps1` restores them if the folder is missing. A sprite that cannot be found **falls back to blank rather than being skipped**, because the list index is the layer index and dropping one would shift every layer after it.

**Right-click reuses `placeTimer`.** Without a cooldown one click at 120 fps drops seven creatures on the same square — the same shape as the instant-dig bug that stripped a row of blocks per click.

**Thirty-six eggs fill all thirty-six inventory slots**, so the egg set and the block kit cannot coexist. Creative starts on eggs and **F10 swaps between the two** rather than either being quietly truncated.

### Creature sizes

Two different numbers, and conflating them is a bug either way. The **collision height** in `kSpecies` is the original's hitbox; the **model top** is where the tallest box actually reaches. Models are allowed to exceed their boxes — a horse's head is well above its 1.6 m hitbox in the original too — but an animal whose silhouette contradicts its neighbours is wrong. The llama once reached 2.57 m against the horse's 2.15 and towered over it, which is backwards.

| species | collision | model top | render scale | matches |
|---|---|---|---|---|
| Sheep / Cow / Pig / Bramble | 1.40 / 1.50 / 1.05 / 1.70 | ≈ as boxed | 1.00 | playtested, not the reference |
| Chicken | 0.80 | 0.84 | 1.00 | Bedrock |
| Cat | 0.70 | 1.03 (tail up) | 1.00 | both |
| Camel | 2.375 | 2.72 | 1.00 | both |
| Horse | 1.60 | 2.15 | 1.00 | both |
| Mule | 1.60 | 2.22 | **0.92** | both |
| Donkey | 1.60 | 2.10 | **0.87** | Bedrock |
| Llama | 1.87 | 2.31 | 1.00 | both |
| Goat | 1.30 | 1.67 (horn tips) | 1.00 | both |
| Rabbit | 0.60 | 0.64 (ear tips) | **0.48** | Bedrock |
| Wolf | 0.80 | 0.97 (ear tips) | 1.00 | Bedrock |
| Frog | 0.50 | 0.44 (eye tops) | 1.00 | both |
| Fox | 0.70 | 0.70 (crown) | **0.93** | both |
| Ocelot | 0.70 | 1.03 (tail up) | 1.00 | both |
| Polar Bear | 1.40 | 1.71 (ear tips) | **1.25** | both |
| Panda | 1.25 | 1.52 (ear tips) | **1.10** | both |
| Slime, small | 0.52 | 0.52 | **1.04** | both |
| Slime, medium | 1.04 | 1.04 | **2.08** | both |
| Slime, large | 2.08 | 2.08 | **4.16** | both |
| Spider | 0.90 | 0.74 (abdomen) | **1.10** | both |
| Cave Spider | 0.50 | 0.47 (abdomen) | **0.70** | both |
| Zombie | 1.95 | 2.00 (crown) | 1.00 | both |
| Skeleton | 1.99 | 2.00 (crown) | 1.00 | both |
| Villager | 1.95 | 1.96 (crown) | **0.92** | both |
| Husk | 1.95 | 2.00 (crown) | 1.00 | both |
| Silverfish | 0.30 | 0.26 (thorax) | 1.00 | both |
| Blackbone | 2.40 | 2.41 (crown) | **1.20** | both |
| Stray | 1.99 | 2.00 (crown) | 1.00 | both |
| Bogged | 1.99 | 2.00 (crown) | 1.00 | both |
| Zombie Villager | 1.95 | 1.96 (crown) | **0.92** | both |
| Witch | 1.95 | 2.65 (hat tip) | **0.92** | both |
| Wandering Trader | 1.95 | 1.96 (crown) | **0.92** | both |
| Princepin | 1.95 | 2.01 (crown) | 1.00 | both |

`modelScale` multiplies geometry and placement only, never UV rectangles. The rabbit's scale is far below the rest because it is the only model stood on end — upright it is more than twice as tall as it is long, so the same nets need a much smaller multiplier to stay rabbit-sized. The frog is the one model *shorter* than its hitbox, which is correct: the original's 0.5 cube is generous around a flat animal.

**Only heights come from the reference. Half-widths are ours and are deliberately narrower** — a horse is 0.9 across here against Bedrock's 1.4 — because a wide box catches on doorways and trunks far more than it reads as bulk. Do not "correct" the widths to match a wiki table; the divergence is the design.

Chicken, donkey, rabbit and wolf were moved from Java's heights to Bedrock's on 2026-08-03, so the chicken and rabbit grew, the donkey grew to match the horse (as Bedrock has it), and the wolf shrank slightly. Cow and Bramble stay on their playtested boxes deliberately.

### Adding a block type

1. Put a 16×16 PNG in `assets/textures/blocks/`.
2. Add its filename to the texture list in `game/src/Main.cpp`, in layer order.
3. Add an entry to `TextureLayer` in `game/src/world/Block.hpp`.
4. Return it from `blockTextureLayer()`.

Blocks whose faces differ are handled by the `BlockFace` parameter rather than by special cases in the mesher. Grass is the worked example: top, bottom and sides all resolve to different layers.

### Authoring

`tools/make-block-textures.ps1` generates the current set. The rules it follows, taken from real block-texture reference:

- **No interpolation, blending or gradients.** Every pixel is one palette entry chosen outright. Smooth noise reads as melted blobs even after quantization, and looks uncanny.
- **Tight value ranges.** Stone spans only a few near greys. Wide contrast makes terrain look like static.
- **Weighted palette selection**, so most pixels land on middle tones and extremes stay sparse.
- **Short horizontal runs**, never large patches.
- **Accent pixels**, such as the grey pebbles in soil, which are what stop brown reading as a blanket.
- **Direction beats noise where the material has a grain.** Bark is per-column stripes with occasional vertical breaks, not isotropic noise stretched vertically. Log end grain is **square** rings (Chebyshev distance), not circular ones — concentric squares are what reads as a cut log at 16×16.
- **Leaves are baked green.** Reference art stores them greyscale because the original tints them per biome at runtime; we have no tinting stage, so the colour goes into the texture.

The PNGs are ordinary files and may be edited by hand instead; the script is a starting point, not a pipeline step. `tools/preview-textures.ps1` magnifies textures into a labelled sheet, because 16×16 cannot be judged at actual size.

Third-party textures kept for visual reference live in `reference/`, which is gitignored and deliberately outside `assets/` so the build cannot copy them into the game.

### Items and drops

**`ItemId` is a separate type from `BlockId`.** Every placeable block has an item form, but not every item is a block — a tool never is. Block items share the block's numbering so there is only one list to maintain, and non-block items start at `kFirstToolItem`.

`dropForBlock` decides what breaking yields. It is a table, not an identity: stone gives cobblestone, grass gives dirt, and all eight stair orientations collapse to one item so an inventory cannot fill with rotations of the same thing.

**Dropped items are the smallest possible entity** — position, velocity, stack — and resolve only vertically against the ground. The general entity system is M20. Their geometry is **rebuilt every frame rather than transformed**, because world meshes are drawn with an identity model matrix and the shader recovers normals from screen-space derivatives of world position; that only holds while vertex positions *are* world positions. The mesh handle is reused, or every frame would retire a GPU buffer.

Drops are **thrown**, not released: without a forward impulse a dropped stack lands at your feet and is collected again immediately. Each drop carries **its own pickup delay** — 0.35 s from breaking, 1.2 s when thrown — because the delay has to outlast the flight or the item is pulled straight back inside the 2 m attraction radius.

Landing **bounces**, keeping 42% of the impact speed, and anything slower than 2 m/s settles outright. That threshold is what makes it terminate; removing it to get a bouncier feel turns the animation into the infinite loop described in `CLAUDE.md`.

**A drop nobody collects is gone after five minutes.** Without that a long session accumulates every item it ever made, and each one rebuilds its geometry every frame. The reference pauses the timer when a chunk stops ticking; ours runs on wall time, which is simpler and equivalent because drops are not saved.

`creative_mode` in `settings.cfg` keeps infinite blocks: placing never runs a stack down. **It changes nothing else.** Breaking drops, and drops are collected, in every mode — both halves of that were once suppressed in creative and both produced bugs, recorded in `CLAUDE.md`.

Stack counts are drawn in the **bottom-left** of a slot, and only when there is more than one — a count of one reads as simply having the thing.

### Crafting

`craftResult(slots, size)` returns what a grid currently makes, and `consumeIngredients` spends it. **They are separate calls because the result is a preview until it is taken** — the player sees what a grid would make, then decides.

A recipe's pattern is stored **at its own size**, not padded to the grid. The matcher finds the bounding box of the filled cells and compares the pattern against that, so a 1×2 recipe is craftable in either column of a 2×2 and anywhere in a 3×3. Since `craftResult` takes the grid size as an argument, a crafting table is a layout change rather than a matcher change.

Shapeless matching **counts leftovers**: after ticking off each ingredient it checks nothing else is in the grid, or a grid holding an extra item would still craft and quietly eat it.

Recipes live in one table in `item/Recipe.cpp`. Shapes and yields come from the reference recipe JSON — `CRAFTABLE.md` records every recipe, whether we can build it today, and what art it still needs.

Closing a screen **returns crafting ingredients** to the inventory, not just the cursor stack. Items left in a grid the player cannot see are items that quietly vanish.

### The crafting table

`BlockId::CraftingTable` answers `isInteractive`, which is what makes right-clicking it open a screen rather than place against it. Sneaking suppresses that, so a block can still be put down on top of one.

Its panel is **generated from the inventory panel** by `tools/make-hud-sheet.ps1`: same frame, same backdrop, same storage rows and hotbar, with the top section cleared and the *same 18×18 slot cell* stamped into a 3×3 and a result position. The cells are not a lookalike, they are the same pixels, so the two screens cannot drift apart. Positions came from measuring the reference GUI, which turned out to share our panel's rows exactly and differ by one pixel in its columns.

The matcher needed no changes at all — `craftResult` already took the grid size, and patterns are already stored at their own size.

### The furnace

Smelting is deliberately dull machinery: every recipe takes **ten seconds**, fuel is measured in how many items it will smelt (charcoal 8, wood 1½, a stick ½), and the two are unrelated tables. Recipes today are `Cobblestone → Stone` and `Log → Charcoal`; glass is missing because sand needs a transparent block we do not have, not because the recipe is unknown.

**Fuel is only lit when there is something worth cooking**, so a furnace loaded with charcoal and nothing else sits cold instead of quietly burning through it. Cook progress slides back rather than resetting, so pulling an item out for a moment does not throw away its cooking.

**Lit and unlit are two different blocks.** `Furnace` and `FurnaceLit` differ in their front texture and in the light they cast (13), and both of those are properties of the block itself — the same reasoning that puts water levels and stair facings in the id.

**The contents are a block entity**, which is the thing the id trick cannot do. They live in a map keyed by block position in `Main.cpp`, not in `World`: chunks are loaded and saved on worker threads, and there is no reason to route block-entity data through that. The cost is that they are written on world save rather than when a chunk unloads, and that entries outlive their chunk being unloaded — both cheap for something a player places a handful of. They persist to `furnaces.dat` beside `player.dat`, and the record is `static_assert`ed trivially copyable because it is written as raw bytes.

**A furnace's output slot is storage, not a preview.** Unlike a crafting result you can only take from it, never put into it, so the shared click handling branches there and nowhere else.

Both progress indicators are the **lit sprite drawn over the spent one** baked into the panel, clipped to how far along the furnace is: the flame is cut from the top because it burns downward, the arrow from the right because it fills rightward.

**Known gap:** a furnace's lit state lives in the block and its contents live in `furnaces.dat`, so if that file is lost or rejected the block stays lit forever with nothing behind it. Opening it creates a fresh entry and the next tick puts it out. Not worth architecture to prevent, but worth knowing when a furnace looks stuck.

### Mining and tools

**Digging takes time.** `breakSeconds(block, item)` multiplies the block's hardness by the tool's speed and by a penalty for being under-tiered; `yieldsDrop(block, item)` answers separately whether anything comes out. Creative pays no time cost and always drops — the one place mode is allowed to matter besides placing.

The multipliers are the reference's own pair, and so is the hardness table: **×1.5 with the kit a block demands, ×5 without**. The ratio is what makes a tool a decision rather than a convenience — a block still comes away bare-handed, it just takes long enough to be worth avoiding.

**The tier demand is the whole progression.** Stone, cobblestone, bricks, slabs, stairs and furnaces require a wooden pickaxe or better; without one they still come away, three times slower, and give nothing. Everything else drops bare-handed, or a fresh world would be unplayable.

Tools are wood (speed 2, 60 uses) and stone (speed 4, 132 uses). Wear is counted per block broken and only on blocks that resist — plants and torches cost nothing. **Tools do not stack**, because two with different wear are not interchangeable, which is why `maxStackFor` exists rather than a bare `kMaxStack`.

Dig progress is drawn as a bar under the crosshair. The HUD only rebuilds when asked, so the frame digging *stops* has to ask too — otherwise the last bar drawn stays on screen forever, which is exactly what happened first time.

### Torches

A torch is a **cutout cross**, the same shape tall grass uses, so it needed no new geometry: the texture is transparent apart from a two-pixel stick and its flame, and the existing alpha-tested pass does the rest. It emits light 14 and, like plants, needs something solid beneath it — placing without support is refused, and breaking that support drops it.

**Wall torches are deliberately absent.** They need either a tilted box or an offset cross plus four orientations in the block id, and floor torches light a cave perfectly well in the meantime.

---

## HUD

Screen-space geometry is a separate mesh drawn last, with no view or projection — only an aspect correction, so coordinates are relative to window **height** on both axes and a square stays square. Y is positive *downward*.

Everything is built from `hud::` primitives: axis-aligned quads, sprite-sheet regions, free-corner quads, text, and isometric block icons.

**Block icons follow the block's shape.** A slab is drawn at half height and a cross-shaped plant is drawn as a flat sprite, because wrapping a plant's artwork around a cube shows a box of grass rather than what actually gets placed. Only `Full` blocks get the three-quad isometric cube.

**Every HUD quad is emitted with both windings.** Backface culling is on, and screen geometry skips the projection that establishes which way is front. Deriving that has gone wrong before and fails completely silently, so two extra triangles per quad buys certainty.

### The sprite sheet

`assets/textures/hud.png` is **built, not hand-edited** — `tools/make-hud-sheet.ps1` stacks the widget art and the inventory panel into one image. One sheet because the HUD samples a single texture, and a texture array needs every layer the same size, which these are not. The widget art stays at the origin so its pixel coordinates survive the sheet growing.

The inventory art arrives as an **integer upscale** of its real pixel grid, so the tool samples every Nth pixel to recover the original exactly. Resizing would blur crisp pixel art. The artist's mock-up also contains a drawn character in the preview panel, which the tool paints out with the panel's own backdrop colour — the game renders its own there.

`assets/textures/hud.png` is loaded as a **second texture binding**, not a layer of the block array, because a texture array requires every layer to share one size and the sheet is 185×41 against the blocks' 16×16. A **negative vertex layer** is the agreed signal for the shader to sample it. Hearts, food and the XP bar are already in the sheet, unused.

> The sheet is recognisably Minecraft's HUD widget art and is a placeholder to replace before any release.

### Text

`assets/textures/font.png` is a **third texture binding**, selected by layer `-2.0` (the HUD sheet is `-1.0`). It is a 128×84 atlas: printable ASCII 32–126 in a 16×6 grid of 8×14 cells, rendered from Consolas with hinted 1-bit rasterisation so every glyph lands on whole pixels. `tools/make-font.ps1` regenerates it.

`hud::appendText` emits one textured quad per character at a fixed advance. Sizes should be chosen so a cell maps to whole pixels — `14.0f / 360.0f` is 1:1 at 720p — because the sampler is nearest-neighbour and any other ratio doubles some rows and not others.

### Hotbar

Cell frames are drawn as **four edge strips** rather than one quad. The artwork's slot interiors are solid black, so a single quad would hide the world behind them; leaving the middle out is what makes the slot see-through. A dark translucent quad then tints the interior so icons stay readable against bright sky.

The bevel is **three pixels on the top and left but two on the bottom and right** — that asymmetry is what makes a cell look raised, and it means the interior is *not* centred on the tile. Everything inside a cell positions from the interior centre; using the tile centre puts icons visibly high and left.

The selected cell uses a larger sprite and a **nearer depth band than every other cell**, so its oversized frame draws over its neighbours rather than being clipped by them.

Block icons are isometric: three quads (top, front, right) at 30°, using the same face shades as the world mesher so an icon reads like the block it places.

### Transparency

`Vertex::color` carries alpha and the pipeline blends. World geometry is opaque, so blending is a no-op for it; it exists only so HUD panels can sit over the scene.

### Startup

The world **streams in behind a loading screen** rather than blocking before the first frame. The window is created, then the normal per-frame streaming runs with a wider budget while a progress bar draws, and the game loop starts once `initialLoadProgress()` reaches 1.

This replaced a `loadImmediately` that queued all 2,187 chunks at once with no cap and no budget. That pinned every core, reached peak memory before anything was on screen, and left the window unresponsive long enough for Windows to mark it so. Progressive loading costs roughly a second of wall time and gives all of that back.

`initialLoadProgress()` weights generation against meshing and only returns 1 once **everything** has drained — chunk queues, light, and fluid. Light is the easy one to forget: propagating it dirties chunks, so a world with empty chunk queues can still have geometry about to change.

> The startup triangle count in the log is now a **snapshot**, not a determinism check. It varies by a few hundredths of a percent depending on exactly when it is read. Verifying that generation is independent of thread scheduling needs a different measurement.

---

## Terrain Generation

Generation is a **pure function of `(seed, chunkCoord)`** — no neighbour reads, no global state, no wall-clock time. That is what lets it run on a worker thread and what makes a world reproducible from one number.

### Biomes

A biome is the data structure that answers *"which block goes here, and why"*. Each is one row in a table carrying its surface block, filler, filler depth, terrain base height, amplitude, snow line, climate coordinates and tree density. **Adding a block type should mean adding or editing a row, never adding a branch to the generator.**

Selection uses **two independent low-frequency noise fields**: temperature and humidity. One field could only ever order biomes along a line, which is why a single "climate" value cannot separate desert from plains from tundra. Each biome sits at a point in that 2D space and claims ground by proximity.

**Blending falls out of the same weights.** Terrain height is a weighted average over every biome in range, so regions slope into each other. Surface *blocks* take the single strongest biome instead — a blend of two block types is not a thing — and the boundary still reads naturally because the selection noise makes it a wandering contour rather than a straight edge.

Snow is a **height rule per biome**, not a biome of its own, so a mountain reads as a mountain rather than as tall grass.

> The seven current biomes prove the machinery and give the existing blocks a natural home. Which regions the finished game has is a conversation with the user, and adding one is a row in the table.

### Caves

Carved where a 3D noise field passes close to a chosen value, which gives connected winding systems. Thresholding the field directly gives disconnected blobs that read as holes rather than caves.

Carving fades in with depth below the surface and never touches the world floor, so the ground is not left rotten and there is always something to stand on.

**Cost tracks cave surface area, not hollow volume.** Narrow tunnels have far more surface per unit volume than open caverns, so making caves *thinner* barely helps; making them *fewer and larger* does. See `CLAUDE.md`.

### Structures

`game/src/world/Structures.cpp`. Anything spanning more than one block — currently trees.

**Nothing is ever written into a neighbouring chunk.** That would break generation purity, and with it thread safety. Instead **every chunk rebuilds each structure that could reach it and discards the blocks that fall outside its own bounds.** The same tree is therefore built several times over, independently, by each chunk it touches — and because it is derived entirely from the seed, every one of them agrees. This is the whole trick; do not "optimise" it into writing across chunk boundaries.

Candidates sit on a fixed **8-block grid**, at most one per cell, jittered inside it and kept `margin = 2` blocks from the cell edge so neighbours cannot touch. `kReach` is the furthest a structure may extend horizontally and sets how many cells outside the chunk are considered — **a structure wider than `kReach` gets silently clipped at borders**, so raise it when adding anything larger.

**Presence is decided by an integer hash before any noise runs.** Most cells are empty, and finding that out costs one hash; sampling biome and surface height first meant three noise evaluations to answer a question already settled. `maxTreeDensity()` is derived from the table rather than written down, so a leafier biome cannot invalidate the early rejection.

Placement gates on `Biome::treeDensity`, on the biome's surface block being grass, and on being above sea level and below the snow line.

### Block shapes

Everything before M17b was a unit cube, and both meshing and collision assumed it. `BlockShape` is where that assumption is now written down: `Empty`, `Full`, `Cross` (plants), `Slab`, `Stairs` and `Fence`.

Dimensions match the reference models exactly, verified at M19b against `models/block/*.json`: slabs at `0–8` and `8–16` sixteenths, a stair's step at `[8,8,0]→[16,16,16]`, the fence post at `[6,0,6]→[10,16,10]`, its rails at `6–9` and `12–15`, and a plant's blades spanning `0.8–15.2`.

**`collisionBoxes` is the single source of truth for a block's extent**, read by the mesher, by physics — the player and creatures share `world/Collision.hpp` for exactly this reason — and by the targeting raycast. Keep it that way: the moment two of them compute a shape independently, they will disagree.

**Two deliberate exceptions**, both of which earned their place by having a real second case rather than an anticipated one:

- **`selectionBoxes`** is what the crosshair picks. A plant is walked straight through and still has to be breakable, so it collides with nothing and selects as a slim column.
- **`fenceRailBoxes`** is what gets *drawn*. A fence renders as a post and two thin rails, but collides as a post and solid full-height arms — modelling the gap between the rails would let the player squeeze through a fence line.

The raycast intersects those boxes rather than treating cell entry as a hit. Testing the cell instead put a placed slab *beside* its neighbour rather than on top: a slab fills half its cell, so a ray aimed at its top from a distance crosses the empty upper half first and reported entry through the side.

**The targeting cage is sized from the live block, not from the ray hit.** The hit is resolved before the frame's edits are applied, so reading the shape from it flashed a full-size cage around a cell the moment its slab was broken. It spans the selection box's actual min and max Y, which is also what puts the cage on the upper half of a top slab rather than around the whole cell.

**Orientation lives in the block id.** Stairs occupy eight contiguous ids — two bits of facing, one of half — the same way water spends eight on its level. Orientation is part of *which block this is*, so it needs no second per-block array. See `TIMELINE.md` M17c for why a metadata nibble was rejected.

**A half block has a half.** `StoneSlab` is the lower half and `StoneSlabTop` the upper, and **two halves meeting in one cell are placed as a whole block instead**. Without both of those, stacking slabs gives slab, gap, slab — the second lands in the next cell's lower half. Which half gets placed comes from whether the underside or the top of a block was clicked.

Non-cube shapes are meshed in a **second pass** and never greedily merged. Teaching the greedy mask about partial faces would slow the path carrying the whole world for the sake of a handful of decorative blocks. Their faces are built by mixing the shared unit-cube corner tables into the box, which preserves the winding those tables established.

A face **buried inside another box of the same block** is skipped, or a stair's step and the half it stands on leave coplanar quads fighting over one depth value.

`occludesFace` replaces a bare `isOpaque` test when deciding whether a face is buried: a bottom slab only hides the face directly above it, because that is the only boundary its geometry reaches.

**Landing is the one case where a block boundary is the wrong answer.** Sides and undersides sit on integer planes, but a slab's top is halfway up its cell, so downward movement resolves against the real surface height instead. See `CLAUDE.md`.

---

## Water

**Depth lives in the block id.** `Water0` through `Water7` are contiguous ids: level 0 is a source that never drains, 7 is the thinnest film. Encoding it this way costs no per-block metadata array and needed no change to the save format. `isWater`, `waterLevel` and `waterAtLevel` are the only things that should know about the encoding.

Three predicates that are easy to confuse and must stay separate:

| | Water | Why |
|---|---|---|
| `isSolid` | no | you sink into it |
| `isOpaque` | no | you can see the seabed |
| `isLightTransparent` | yes | sunlight reaches underwater |

Conflating the first two gets you either walking on water or an invisible seabed.

### Cutout blocks

A third category beside opaque and translucent. **Cutout** geometry is drawn in the *opaque* pass, and the fragment shader `discard`s any texel below half alpha. What survives writes depth normally, so it needs no sorting — which is the whole reason it is not simply translucent. Leaves are the only one so far.

**A surviving cutout pixel is written fully opaque.** Mip levels average alpha, so distant foliage arrives with partial values that pass the test and would then blend with the sky behind, turning every distant tree pale grey. World alpha therefore comes from the *vertex*, never the texture. The texture's alpha is consumed entirely by the discard test.

**Cutout blocks keep the faces they share with their own kind**, which is the opposite of every other block. Culling them is the cheaper "fast foliage" style and leaves a canopy as a hollow shell, with every hole showing sky rather than more leaves. They are also **double-sided** — emitted with both windings — so a hole in the near face shows the inside of the far face instead of straight through the block.

Each shared boundary is emitted **once**, by whichever neighbour faces the positive direction. Both emitting it leaves two coplanar quads at identical depth; the double-sided winding means the other neighbour is still covered.

`discard` compiles to `OpDemoteToHelperInvocation` against Vulkan 1.3, so `shaderDemoteToHelperInvocation` must stay enabled on the device. Without it the shader still runs on this driver but validation rejects the module.

### Flow

Event-driven and incremental. Generated oceans are already settled, so nothing runs until an edit disturbs them; `setBlock` queues the changed cell and its six neighbours, and the queue is drained against the same per-frame budget as everything else.

A cell takes the **strongest supply reaching it** — the minimum level of any valid neighbour, plus one. That is what makes two flows meeting resolve to one answer instead of oscillating. **Falling beats spreading:** a neighbour with air beneath it is draining downward and does not feed sideways. **Two sources meeting over solid ground create a source**, which is what makes water renewable.

Sources are the fixed points of the whole system. Without something that never drains, every body of water eventually empties itself.

### Drawing it

Blending depends on draw order, so water cannot share a buffer with the terrain behind it. **Each chunk owns two meshes**, opaque and translucent, and the renderer draws every opaque mesh in the scene before any translucent one. Water faces are emitted against air, and against thinner water whose lower surface would otherwise leave a gap.

---

## World Persistence

Generation is a pure function of `(seed, chunkCoord)`, so an untouched chunk is already perfectly reproducible. A save is therefore the **difference** between the generated world and the played one, which keeps it small no matter how far the player travels.

A chunk is flagged `modified` the moment `setBlock` changes something, and only flagged chunks are ever written. The write happens **immediately before the chunk is erased** on unload — not queued — because a modified chunk erased before writing would silently revert. Loading checks disk first and falls back to the generator.

Each chunk is one file under `saves/world_<seed>/chunks/`, written to a `.tmp` name and then **renamed**. Rename is atomic on Windows and POSIX, so the file on disk is always either the complete old version or the complete new one, never a mixture.

Every file carries a header with magic, format version, seed, its own coordinates and block count, **all four checked on load**. This is the only place the game reads bytes it did not produce this run, so it validates rather than assumes. Player position and view direction live in `player.dat` beside the chunks, furnace contents in `furnaces.dat`, and the creature population in `creatures.dat`. All three side tables follow the same shape: magic, version, seed, count, then fixed-size records written as raw bytes and `static_assert`ed trivially copyable. An empty table deletes its file rather than leaving a stale one — a leftover would restore furnaces the player has broken, or repopulate a world they have cleared.

One file per chunk is deliberately crude: a bad write damages exactly one chunk and it needs no index to stay consistent. A packed region format belongs with M13 if file count ever becomes the problem.

---

## Rendering Statistics

`Renderer::stats()` reports the last completed frame's GPU time, draw calls and triangle count.

GPU time comes from **timestamp queries** written at the top and bottom of the command buffer, read back only after that frame slot's fence has been waited on — which is exactly when the results are guaranteed available. It measures real device work rather than how long the CPU waited. A `timestampPeriod` of zero means the device does not support timestamps, in which case the figure reads zero and a warning is logged.

### Diagnostics overlay

`F5` draws a compact panel in the top-left: a 160-frame frame-time graph over nine labelled rows — fps, cpu ms, gpu ms, chunks, meshes, queued, retired, draws, tris.

**Frame time is the headline, not fps.** Frames per second is an average, and an average hides the one 30 ms frame that is what actually felt bad. The graph shows every frame individually, clamped at 25 ms, with a single guide line at the 60 fps budget. Bars turn from green to orange when a frame misses that budget.

Rows are real text, not colour codes. An earlier version used colour swatches with the key logged to the console; it was unreadable in practice, which is what prompted building the font.

The overlay mesh rebuilds at **20 Hz**, not every frame. It changes constantly, and rebuilding per frame would churn GPU buffers for no readable benefit.

---

## Sun and sky

A placeholder day cycle: one sun on a fixed arc, no moon, no seasons. Real directional lighting with cast shadows is M24 and needs the renderer restructure first.

The sun is a **billboarded quad** drawn in world space at a fixed distance from the camera, using a layer of the block texture array so it needs no extra binding. It is depth-tested against terrain, so a hill hides it.

World surfaces take a **Lambert term** against the sun direction, on top of the baked sky/block light and ambient occlusion. Sun direction, ambient floor and sun strength travel in the **push constants**, with a per-draw flag so HUD and sky geometry stay unlit.

**The face normal is recovered in the fragment shader** from how world position changes across the triangle, rather than carried per vertex. Every face here is flat, so it is exact, and it kept a normal out of the vertex format — which would have cost memory on every vertex in the world. This works only because chunk geometry is drawn with an identity model matrix, so its vertex position *is* its world position.

Sky colour is blue overhead, warm near the horizon and dark at night, driven by the sun's elevation. Night keeps a usable ambient floor rather than going black; this is a placeholder, and a world nobody can see in is not worth shipping over realism.

> **There are no cast shadows.** A surface is lit by which way it faces, not by whether anything stands between it and the sun. Do not describe this as shadows.

---

## Lighting

Every block stores one byte of light: **sky in the high nibble, block in the low one**, 0-15 each. This doubles a chunk to 64 KB. It is stored rather than recomputed per mesh because light crosses chunk boundaries, so it cannot be derived from a single chunk's contents.

Sky light falls **straight down at full strength** and dims only when spreading sideways, which is what makes open ground uniformly bright. Block light dims in every direction. The two are independent: a surface takes the **brighter** of them, not the sum, which would blow out anywhere both reach.

The free fall only happens through **sky-transparent** blocks, which is a narrower set than light-transparent. Leaves let light through but are not sky-transparent, so a canopy breaks the fall and everything under it dims one level per block like any other direction. That is what shades a forest floor, and it means no per-block attenuation value is needed.

They are also kept apart **all the way into the fragment shader**, and that is load-bearing rather than tidiness. Only sky light answers to the sun's direction and the time of day; block light must not, or a glowstone underground gets multiplied by the ambient term and lights nothing. See `CLAUDE.md`.

Propagation runs on the **main thread**, budgeted per frame. Generation and meshing get self-contained snapshots and run on workers; light cannot, because it walks freely between chunks. Adding light is a flood fill. Removing it is the harder half: it walks back everything the dead source lit, and any cell found brighter than expected has its own supply and becomes a seed to re-fill the hole.

**Block light propagates before sky light**, even though sky light is what makes newly streamed terrain look right. Block light is rare and its queue is short, so going first costs sky light almost nothing — but behind sky light it can be starved outright, because streaming refills the sky queue every frame and the two share one budget.

**Seeding is the part that has to stay careful.** A column's sky light is traced top-down when the last chunk of that column arrives, and only cells that can actually spread somewhere are queued — those with a horizontal neighbour whose sky reaches less far down. Queueing every lit cell instead buried the queue under tens of millions of entries and took startup from 0.7 s to 8.5 s.

Chunks whose light changed are collected in a set and invalidated **once per frame**, not per cell.

### Smooth lighting and ambient occlusion

Each face corner averages the light of the open cells touching it, and darkens by how boxed-in it is. Two solid sides meeting seals a corner regardless of what sits diagonally behind them.

Quads **split along their darker diagonal**. Without that, occlusion on opposite corners creases flat ground the wrong way, which reads as a fold in the terrain.

**Light does not change the vertex format.** The vertex colour carries **red = sky light, green = block light, blue = face shade × ambient occlusion**, with alpha still alpha. It fits because the shading was only ever greyscale, so two of the three colour channels were redundant. The shader reads those channels as light only when `lighting.z > 0.5`; HUD and sky geometry are drawn unlit and use the colour as an actual colour.

The final term is `shading * (floor + (1 - floor) * max(block, sky * daylight))`, where `daylight` is the sun's ambient plus its directional term and the floor arrives as `lighting.w`.

**Shading has to stay a separate channel**, not be pre-multiplied into the two light values. Folding it in makes both terms exactly zero wherever no light reaches, which silently deletes ambient occlusion in caves *and* lets every unlit face merge with its neighbours — geometry dropped 47% and the underground rendered perfectly flat.

---

## Meshing

A face is emitted only where a solid block touches air. That alone is what makes voxel worlds affordable; interior faces are never created.

**Adjacent identical faces are merged into single quads** (greedy meshing). For each of the six directions the mesher walks slice by slice, builds a mask of visible faces on that slice, and grows each run as far as it can horizontally and then vertically.

**Only evenly lit faces merge.** A merged quad interpolates its four corners across the whole span, which matches the individual faces it replaces only when every one of them was uniform. Anything with a lighting or occlusion gradient is emitted on its own. Flat open terrain still merges; edges and corners no longer do, which cost 2.37× more geometry when lighting arrived.

The merge key is the **texture layer plus the corner brightness**. The layer already encodes both block type and which way the face points, so nothing else needs comparing.

**No vertex format change was needed** for either merging or lighting. The sampler repeats, so a quad covering N×M blocks takes texture coordinates of 0→N and 0→M and tiles correctly. A merged quad is built by scaling the original unit-face corners, which is deliberate: scaling by positive factors cannot flip the winding, so the culling orientation that was verified on screen still holds. **Do not rewrite this to construct corners from scratch** — see `CLAUDE.md` on backface winding.

Each face's `uAxis`/`vAxis` say which world axes the texture coordinates run along, read off the corner tables rather than derived. Getting them wrong stretches textures instead of tiling them, silently.

### What the mesher is given

`ChunkVolume` is the chunk plus **one cell of padding** — 34³ of blocks and light, about 79 KB. Ambient occlusion samples diagonally, so a face on a chunk edge needs cells from as many as three neighbouring chunks at once, which the six face borders it replaced could not supply. It also turns every neighbour lookup into a plain array index instead of a chain of bounds tests.

---

## Culling

Every mesh gets a world-space bounding box when it is uploaded. Before drawing, the box is tested against the six planes of the view-projection matrix, extracted by the Gribb-Hartmann method. Roughly 70% of draw calls disappear.

The test is conservative — it rejects only boxes provably outside — because a false rejection means geometry vanishing at the edge of the screen, which is much worse than drawing a few extra chunks.

GLM is column-major and the project builds with `GLM_FORCE_DEPTH_ZERO_TO_ONE`, so the near plane is row 2 alone rather than `w + row2`. That detail is Vulkan-specific and wrong in most OpenGL-era references.

**Verify culling by the fraction of geometry drawn**, not by looking for gaps: at a 100° horizontal field of view about 28% of a full circle is visible, and the measured figure was 29.4%.

---

## Settings

`settings.cfg` sits next to the executable. One `key=value` per line, `#` for comments, read once at startup.

| Key | Effect | Changeable while playing |
|---|---|---|
| `worker_threads` | Background threads for generation and meshing | **No** — restart |
| `render_distance` | How far the world is drawn, in chunks | Yes, `F6`/`F7` |
| `frame_cap` | Target frames per second, 0 for uncapped | Yes, `F1`/`F2` |
| `day_length_seconds` | Real seconds for a full day and night | No — restart |
| `spawn_x` / `spawn_z` | Which column a new world starts in | No — restart |
| `spawn_underground` | Start in the most open cave near that column | No — restart |
| `creative_mode` | Infinite blocks, no drops | No — restart |

`spawn_underground` **searches** the surrounding area for the roomiest cave floor rather than trusting the spawn column to contain one. It exists because testing anything underground otherwise means minutes of flying per attempt, which is enough friction that the test stops being run. All three only apply to a fresh world; a saved player position wins.

Render distance is safe to change live because nothing ends up half-migrated: the next update loads or unloads the difference. Shrinking also drops meshes outside the new radius immediately, rather than waiting for those chunks to leave the unload radius, or the change appears to do nothing.

The worker count is different and deliberately restart-only — see `CLAUDE.md`.

> `tools/benchmark.ps1` sweeps a setting and **restores the file afterwards**. Benchmarks write to the same file the game is played from, and a leftover `frame_cap=0` looks exactly like a broken frame limiter.

---

## Threading
`engine::JobSystem` is a worker pool with a single job queue. It takes a callable and runs it elsewhere; it knows nothing about chunks, and deciding what is safe to run in parallel is entirely the caller's problem.

**The pool size is fixed for the lifetime of the process.** `settings.cfg` next to the executable holds `worker_threads`, read once at startup, defaulting to half the machine's hardware threads. Zero is a supported value and means every job runs inline on the calling thread — the lowest-resource mode, and a way to reproduce a bug without threads in the picture. Changing it requires a restart; see `CLAUDE.md` for why resizing a live pool was rejected.

### What runs where

| Work | Thread |
|---|---|
| Chunk generation and disk load | Worker |
| Meshing | Worker |
| Everything else: block edits, physics, raycasting, GPU upload, saving | Main |

**The main thread remains the only thing that mutates the world.** A worker never sees `m_chunks`. Before a mesh job is submitted the main thread copies the chunk and its six neighbouring border layers into a `MeshJobInput`, so the job owns everything it reads and the question of what the main thread may do meanwhile never arises. That copy is ~38 KB against a mesh build that costs far more.

### Surviving the world's destruction

Jobs capture `shared_ptr`s to the results buffer and to `WorldStore`, never a `World*`. A job still running when the world is destroyed writes into a buffer that is still alive and is simply never read. Nothing has to be drained or waited for at shutdown.

`JobSystem` is declared **before** `World` in `main`, so it is destroyed after it. The world submits to the pool and must not outlive it.

### Stale results

Every chunk carries a `revision`, and a mesh job records the revision it started from. On collection, a result whose revision no longer matches is thrown away and the chunk re-queued. This is what catches an edit made while a mesh was being built.

Two distinct operations exist, and confusing them causes real bugs:

- `queueMesh` — this chunk has no current mesh. Does **not** bump the revision.
- `invalidateMesh` — this chunk's geometry is genuinely out of date. Bumps the revision, discarding any job in flight.

Editing a block on a chunk face calls `invalidateMesh` on **both** chunks, because the neighbour may already be meshing against the old border. Merely re-queueing it there would let a stale mesh install and never be corrected. Conversely the "never meshed" sweep uses `queueMesh`, because bumping revisions there would throw away good in-flight work every time the player crossed a chunk boundary.

---

## Chunk Streaming

Chunks live in a `std::unordered_map` keyed by world chunk coordinate. The world is **3 chunks (96 blocks) tall** and streams horizontally only.

| Radius | Chunks | Meaning |
|---|---|---|
| Load | visible + 1 | Generated and kept in memory |
| **Visible** | **`render_distance`** | Meshed and drawn |
| Unload | visible + 3 | Erased past this |

**The three radii are derived from one number.** `kDefaultVisibleRadiusChunks = 5` in `World.hpp` is only a fallback — in practice `Settings::renderDistance` always overrides it, and it **defaults to 12** (`settings.cfg` in the current debug build says 12). So the table's real content is the *relationship*, not the absolute values.

**Visible is one less than load on purpose.** A chunk is only meshed once its four horizontal neighbours exist; meshing against a missing neighbour emits a full sheet of faces at the frontier that has to be thrown away when the neighbour arrives. **Unload is larger than load** so pacing back and forth across the boundary does not thrash chunks in and out.

Generation and meshing are submitted to the job system; the **3 ms per-frame budget** now meters how much is dispatched and how many finished meshes are uploaded, because uploading is the part still on the main thread. Whatever does not fit waits for the next frame. Unloading is never metered — it frees memory and is nearly free.

Only `workers × 2 + 2` jobs of each kind are outstanding at once. A longer queue does not go faster; it just produces results for places the player has already left. Loads and meshes have separate limits so a long stream of loads cannot starve meshing and leave the player standing in an invisible world. **Startup ignores the limit entirely** and queues everything at once — batching it left eleven workers waiting on each other and was slower than single-threaded.

The load queue is sorted farthest-first and drained from the back, so the nearest chunk is always the cheapest to remove and the world fills in from the player outwards.

**Chunks that exist but were never meshed are re-queued whenever the player crosses a chunk boundary.** Without that, a chunk dropped from the queue — because a neighbour was missing, or because it was outside the visible radius at the time — would never be picked up again, leaving a permanent hole.

### Not stalling the GPU

Replacing or freeing a mesh's buffers while a submitted frame may still be reading them is a use-after-free. Waiting for the GPU to go idle each time is correct but unusable at streaming rates.

Instead, buffers are **retired**: moved to a list tagged with the current frame index and destroyed once `kFramesInFlight + 1` frames have started, at which point no in-flight frame can reference them. The release pass runs at the very top of `drawFrame`, **before any early return**, so a minimized or resizing window still frees them.

Mesh slots carry an explicit `inUse` flag. Removing the same chunk twice would otherwise push its handle onto the free list twice and hand one slot to two different chunks — corrupting geometry and leaking buffers at the same time.

### Watching for leaks

The per-second log line reports `chunks | meshes | pending | retired`. Each catches a different failure:

- **chunks** — unbounded growth means chunks are not being erased.
- **meshes** — must track chunks; divergence means slots are not reclaimed.
- **pending** — must return to zero when standing still; otherwise queues refill faster than they drain.
- **retired** — must sit at zero; sustained growth means the release pass stopped running.
- **creatures** — must settle at the cap (14) and never exceed it. This one has already earned its place twice: it read 16 against a cap of 14 after a scripted edit deleted a `return;`, and the per-species breakdown (`s` sheep, `c` cows, `b` Brambles) settled an apparent "passives are broken" in one run by showing the spawn was simply a region neither passive lives in.
- **drops** — one per block broken, so it doubles as a count of how many blocks a click actually destroyed. Added while chasing the creative instant-break bug, where a single click broke twelve.

---

### Getting geometry onto the GPU

The memory a GPU renders from fastest is not writable by the CPU, so data goes into a host-visible *staging* buffer and the GPU copies it across.

`engine::UploadContext` owns a **16 MB persistently mapped staging arena**, one command buffer and one fence. `stage()` memcpys into the arena and records a copy; `flush()` submits the whole batch once. `Renderer::drawFrame` flushes immediately before recording the frame, so the copies are submitted to the graphics queue ahead of the draws that read them.

**Visibility comes from submission order plus one barrier** at the end of the batch, from `TRANSFER_WRITE` to `VERTEX_ATTRIBUTE_READ | INDEX_READ`. There is no transfer queue and no semaphore, and there does not need to be while everything is on one queue.

This is safe against in-flight frames for a specific reason worth keeping: **`uploadInto` always retires the old buffers and allocates new ones**, so a copy only ever targets a buffer no submitted frame has ever referenced. If mesh updates are ever changed to write into an existing buffer, that guarantee disappears and this needs revisiting.

The only blocking is in `recycleArena`, which waits on the previous submission before the arena is reused. In play the fence is already signalled and the wait is free; during a bulk load it throttles to a handful of waits instead of one per buffer.

`uploadBufferData` still exists for **one-shot texture uploads only**. It allocates, submits and calls `vkQueueWaitIdle` every time, which is why the mesh path no longer uses it.

---

## GPU Memory Ownership
**Every Vulkan allocation has exactly one owning C++ object that releases it in its destructor.** Vulkan reference-counts nothing, so this is the only thing preventing VRAM leaks.

- `Buffer` owns a `VkBuffer` and its `VkDeviceMemory` together. Copy **and move** are deleted — two objects owning one allocation is how double-frees happen. If a buffer ever needs to be returned from a factory, implement move properly rather than reaching for `shared_ptr`.
- If `vkAllocateMemory` fails after `vkCreateBuffer` succeeded, the constructor destroys the buffer before throwing. Partial construction must not leak.
- `uploadBufferData()` creates its staging buffer as a scoped local and calls `vkQueueWaitIdle` before returning, because the staging buffer is destroyed the instant it does.
- Its one-time command buffer is owned by a scoped RAII wrapper, so it is freed even if the upload throws mid-way.
- One `vkAllocateMemory` per buffer is acceptable at this scale but will not survive thousands of chunks. Sub-allocation (VMA) is planned for M4+; see `TIMELINE.md`.

**How to verify no leaks:** validation layers list every undestroyed Vulkan object when the instance is destroyed. A Debug run that exits silently is the proof. For CPU-side leaks, sample `WorkingSet64`/`HandleCount` over time and confirm they are flat.

---

## Runtime Behavior

Milestone 1 program flow:

1. Create the window.
2. Initialize Vulkan (instance → surface → physical device → logical device → swapchain).
3. Loop: poll OS events → apply any frame-cap key presses → acquire a swapchain image → record a command buffer that clears it to a solid color → submit → present → wait out the remainder of the frame budget.
4. On window close: wait for the GPU to go idle, then destroy everything in reverse order.

**Waiting for GPU idle before shutdown is mandatory**, not a nicety — destroying resources the GPU is still using is undefined behavior.

---

## Current Validation Baseline

Verified 2026-07-31 on this machine.

- **Configure:** `cmake --preset debug` succeeds. GLFW 3.4 fetched, `Found Vulkan: 1.4.357` with `glslc` and `glslangValidator` components.
- **Compile:** clean build in both debug and release, **zero warnings** at `/W4 /permissive-`. Shaders compile to SPIR-V as part of the build.
- **Runtime:** window opens at 1280x720; validation layers report **no errors**, including across a continuous ~50-minute session.
- **M2 result:** an RGB-interpolated triangle renders correctly, confirmed visually. Resizing scales it correctly with no flicker, no crash, and no validation errors; the swapchain rebuilds on each resize as expected.
- **M3 result:** six shaded boxes on a ground plane, 144 vertices / 72 triangles, render solid with correct mutual occlusion from every angle. Free-fly camera confirmed smooth with no ghosting or artifacting. Resizing rebuilds both swapchain and depth image cleanly.
- **M4 result:** a single 32³ chunk emits 5098 faces where naive per-block meshing would emit 196608 — 97.4% of the work discarded before it reaches the GPU.
- **M5 result:** value-noise fBm heightmap, 5 octaves, seed 1337. Determinism check passes: regenerating a chunk reproduces it byte for byte. Measured on the pre-streaming fixed area, ~27 ms to generate and ~156 ms to mesh 72 chunks.
- **M6 result:** player spawns on the surface at the world centre and walks, jumps, sprints and sneaks across terrain without clipping into blocks or falling through the world. Steady 120–122 fps against a 120 fps cap with collision running every frame.
- **M7 result:** breaking and placing work across chunk boundaries with no holes or stale geometry, at 12 m reach with hold-to-repeat.
- **M8 result:** verified over a continuous 135-second flight. Loaded chunks oscillated in a bounded 594–663 band and mesh slots 441–555, tracking each other; pending work spiked to 78 and returned to zero every time; retired GPU buffers stayed at zero throughout. Frame rate held **119–121 fps** with no hitch at any point. Initial load is ~970 ms for 507 chunks, taken as a deliberate one-off stall at startup rather than pop-in.
- **M9 result:** ten block textures plus a white utility layer in one sRGB texture array with a full mip chain. Frame rate and streaming counters were unchanged from M8, so texturing cost nothing measurable.
- **M10 result:** a 43-minute session across hundreds of chunks wrote exactly **one** 32,796-byte chunk file — 32,768 blocks plus a 28-byte header — because only one chunk was edited. Relaunching restored both the edits and the player's position.
- **M10b result:** hotbar, isometric icons and translucent slots added with no measurable change to frame rate or streaming counters.
- **M11 result:** `F5` overlay reports fps, CPU and GPU frame time, streaming counters, draw calls and triangles. GPU time comes from timestamp queries read after the frame's fence.
- **M12 result:** generation and meshing moved to a worker pool. Release build, 507 chunks: 179 ms on 0 workers, 55 ms on 11. Identical triangle counts at every worker count, which is the determinism check.
- **M13a result:** batched mesh upload. Startup upload 186 ms → 33 ms, submissions 726 → ~4. `vkQueueWaitIdle` gone from the mesh path.
- **M13b result:** greedy meshing and frustum culling. Geometry ~6.4× smaller, draw calls ~70% lower. Default render distance 5 → 12, and 32 is usable. Culling verified by drawing 29.4% of triangles against the ~28% a 100° horizontal field of view covers.
- **M14 result:** sky and block light propagate across chunks; smooth lighting and ambient occlusion per face corner. Verified numerically at the spawn column — sky 15 above the surface, 0 at it and below. Geometry rose 2.37× because only evenly lit faces can merge.
- **M14c result:** a visible sun crosses the sky, surfaces take a directional term, and sky colour follows the sun's elevation. No cast shadows.
- **M15 result:** caves, seven biomes, oceans and flowing water. Release build at render distance 12: 2,742,884 triangles, 1.15 ms GPU, 121 fps, ~1.9 s startup, 575–630 MB.
- **M16 result:** trees. Release build at render distance 12: **2,829,100 triangles, 1.06 ms GPU, 121 fps**, generate+mesh 1275 ms on 11 workers. The determinism check is the one that matters here — 2,829,100 triangles at 0, 4 and 11 workers, so structures straddling chunk borders come out identical regardless of scheduling.
- **M17a result:** alpha-tested, double-sided, layered leaves. **3,024,166 triangles, 1.45 ms GPU, 121 fps** — +6.9% geometry over M16. Zero validation errors once `shaderDemoteToHelperInvocation` was enabled.
- **M17b result:** block shapes. Tall grass and stone slabs. **3,146,576 triangles, 1.35 ms GPU, 120 fps.** Verified numerically: a player dropped onto a slab platform rests at 40.501 where the surface is 40.5.
- **M17c result:** oriented shapes. Cobblestone stairs in eight orientations, plus top slabs and slab merging. **3,146,570 triangles, 1.48 ms GPU, 120 fps.** Slabs were rewritten onto the shared box table and reproduced a bit-identical triangle count, which is the check that the rewrite changed nothing.
- **M20b result:** thirty-six species. Release build holds **120 fps** with no measurable cost from creatures. The counter that matters is the per-second census — `creatures N (breakdown) hunting M` — which is what makes "is the behaviour actually running?" answerable without a debugger, and which has twice settled a bug that looked like something else entirely.
- **M20c result:** the three-state switch replaced by eight behaviours in a `constexpr` table. **The acceptance test was that nothing changed**, so the evidence is negative by design: both presets clean at `/W4`, a debug soak with zero validation errors, and every prior behaviour intact. The one visible addition — a creature glancing at you while it keeps walking — is the proof that disjoint control flags coexist.
- **M20d result:** explosions, jumping, the chicken's slow fall and thirty-six spawn eggs. Debug soaks of 20–45 s at **100–121 fps**, zero validation errors, population stable, and the sprite array grown from 38 to **74 layers** with a startup check that the egg run still begins where the block list ends.
- **GPU selection:** correctly picks `NVIDIA GeForce RTX 4070 Laptop GPU`, not the Intel iGPU that enumerates first.
- **Swapchain:** 3 images, `mailbox` present mode, rebuilt cleanly on every resize.
- **Frame pacing:** holds 120–121 fps against a 120 fps target while the machine is active. Extended idle sessions show stretches near 66 fps, attributed to laptop power management dropping the panel refresh rate — not an engine fault, and not investigated further per user direction.
- **GPU load:** ~9 W at ~500 MHz core clock while rendering the triangle at M2, i.e. essentially idle. Third-party overlay tools report an implausible frame rate on this machine; trust the engine's own counter (see the third-party layer note above).
- **Shutdown:** closing the window saves every modified chunk and the player's position, logs how many chunks were written, and exits through the normal path with no validation errors and no crash.

---

## Update Discipline

Keep this file factual, current, and concise. Replace superseded facts in place — no changelog, no dated entries, no "previously this was X."

Anything that is a *story* (why a choice was made, what was tried and rejected, what fooled someone) belongs in `CLAUDE.md` instead.
