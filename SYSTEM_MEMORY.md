# SYSTEM_MEMORY.md

Current technical truth for this voxel sandbox project: what exists, where it lives, what version it is, and how to build and run it.

Narrative history, rejected approaches, and debugging lessons live in `CLAUDE.md`. The milestone route and the long-term vision live in `TIMELINE.md`. **This file is factual and current-state only** — when something changes, replace the old fact in place rather than appending.

> **Status:** Milestones 1–16 complete plus M17a, including the inserted M10b (hotbar) and M14c (placeholder sun). The game is playable: an endless seeded world streams in around the player, who walks, jumps, sprints, crouches, flies, swims, and breaks and places textured blocks from a nine-slot hotbar. Edits and player position survive a restart, and `F5` shows per-frame diagnostics. Generation and meshing run on worker threads, mesh uploads are batched, geometry is greedily merged and frustum culled, the world is lit with sky light, block light, smooth lighting and ambient occlusion, and a placeholder sun crosses the sky. Terrain is divided into seven biomes with caves underneath, oceans that flow, and trees whose leaves are alpha-tested. `TIMELINE.md` M17b (block shapes) is next.

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
├── .vscode/                Editor config + recommended extensions
├── tools/
│   ├── dev-env.ps1         Loads the MSVC environment into the current shell
│   ├── make-block-textures.ps1  Generates the block textures
│   ├── preview-textures.ps1     Magnifies textures into a labelled sheet for review
│   ├── make-font.ps1            Regenerates the ASCII font atlas
│   ├── capture-window.ps1       Screenshots the running game, optionally after sending keys
│   ├── benchmark.ps1            Sweeps a setting and restores settings.cfg afterwards
│   ├── convert-image.ps1        Any Windows-decodable image (incl. WebP) to PNG, with crop and integer downscale
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

## Engine Architecture (current)

Milestone 1 only. Each type owns its Vulkan resources and destroys them in its destructor.

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
| `Block` | `world/Block.hpp` | The block ID enum plus the three predicates that must stay distinct: solid (blocks movement), opaque (blocks vision), light-transparent. Also which texture layer each face uses — `BlockFace` is what lets grass differ on top, sides and bottom without special cases in the mesher. Water encodes its depth in the id. |
| `Chunk` | `world/Chunk.hpp` | A 32³ block of world as a flat array, indexed `x + z*32 + y*32*32`, plus a parallel light array. Reads outside the chunk return air rather than failing, so callers do not need bounds checks everywhere. |
| `noise` | `world/Noise.hpp` | Seeded value noise and fractal Brownian motion in 2D and 3D. An integer hash, so it is reproducible on any machine without storing anything. |
| `Biome` | `world/Biome.hpp` | The table that answers "which block goes here, and why", and the temperature/humidity selection that picks between rows. |
| `TerrainGenerator` | `world/TerrainGenerator.hpp` | `generateChunk(seed, coord)` — **a pure function**, and required to stay one. No neighbour reads, no global state, no clock. This is what makes the world deterministic and what lets generation run on a worker thread. |
| `ChunkMesher` | `world/ChunkMesher.hpp` | Turns a padded chunk volume into opaque and translucent mesh data, emitting only faces that can be seen. The volume is passed in rather than looked up, which keeps meshing pure. |
| `World` | `world/World.hpp` | Owns every loaded chunk, streams them around the player, and runs light propagation and water flow. The single owner of block state, and the only thing that mutates it. |
| `WorldStore` | `world/WorldStore.hpp` | Reads and writes the save directory. Stores only modified chunks, plus the player's position and view direction. |
| `Raycast` | `world/Raycast.hpp` | Walks the view ray cell by cell to find the block being aimed at, and the empty cell in front of it where a new block goes. Steps block to block rather than sampling at intervals, so it cannot skip a block at any angle. |
| `Sky` | `world/Sky.hpp` | Placeholder day cycle: sun direction over time, sky colour, and the billboarded sun quad. |
| `BlockOutline` | `world/BlockOutline.hpp` | The wireframe cage marking the targeted block. Built from thin solid bars so it needs no second pipeline or line-width support. |
| `Settings` | `core/Settings.hpp` | `settings.cfg` next to the executable. Read once at startup. |
| `hud::HudPrimitives` | `hud/HudPrimitives.hpp` | Screen-space building blocks: quads, sprite-sheet regions, free-corner quads, text, and isometric block icons. |
| `Crosshair` | `hud/Crosshair.hpp` | The aiming reticle. |
| `Hotbar` | `hud/Hotbar.hpp` | The nine-slot bar, drawn from the HUD sprite sheet. |
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

Projection is built by the renderer from the **swapchain's** extent, not the window's, so the aspect ratio cannot disagree with what is actually drawn. Vertical FOV is 45°; wider than about 60° visibly warps nearby geometry. `projection[1][1] *= -1` converts GLM's OpenGL-style Y-up clip space to Vulkan's Y-down. `GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined **PUBLIC** on the engine target because every translation unit doing matrix maths must agree on the depth convention.

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
| Left click | Break the targeted block (hold to repeat) |
| Right click | Place against the targeted face (hold to repeat) |
| `1` – `9` / scroll | Select a hotbar slot |
| `Escape` | Release the mouse cursor |
| Left click | Recapture the cursor when released |
| `F1` / `F2` | Lower / raise the frame cap |
| `F3` / `F4` | Narrow / widen the field of view (default 70°) |
| `F5` | Toggle the diagnostics overlay |
| `F6` / `F7` | Decrease / increase render distance (saved to `settings.cfg`) |

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

---

## HUD

Screen-space geometry is a separate mesh drawn last, with no view or projection — only an aspect correction, so coordinates are relative to window **height** on both axes and a square stays square. Y is positive *downward*.

Everything is built from `hud::` primitives: axis-aligned quads, sprite-sheet regions, free-corner quads, text, and isometric block icons.

**Every HUD quad is emitted with both windings.** Backface culling is on, and screen geometry skips the projection that establishes which way is front. Deriving that has gone wrong before and fails completely silently, so two extra triangles per quad buys certainty.

### The sprite sheet

`assets/textures/hud.png` is loaded as a **second texture binding**, not a layer of the block array, because a texture array requires every layer to share one size and the sheet is 185×41 against the blocks' 16×16. A **negative vertex layer** is the agreed signal for the shader to sample it. Hearts, food and the XP bar are already in the sheet, unused.

> The sheet is recognisably Minecraft's HUD widget art and is a placeholder to replace before any release.

### Text

`assets/textures/font.png` is a **third texture binding**, selected by layer `-2.0` (the HUD sheet is `-1.0`). It is a 128×84 atlas: printable ASCII 32–126 in a 16×6 grid of 8×14 cells, rendered from Consolas with hinted 1-bit rasterisation so every glyph lands on whole pixels. `tools/make-font.ps1` regenerates it.

`hud::appendText` emits one textured quad per character at a fixed advance. Sizes should be chosen so a cell maps to whole pixels — `14.0f / 360.0f` is 1:1 at 720p — because the sampler is nearest-neighbour and any other ratio doubles some rows and not others.

> The sheet is recognisably Minecraft's HUD widget art and is a placeholder to replace before any release.

### Hotbar

Cell frames are drawn as **four edge strips** rather than one quad. The artwork's slot interiors are solid black, so a single quad would hide the world behind them; leaving the middle out is what makes the slot see-through. A dark translucent quad then tints the interior so icons stay readable against bright sky.

The bevel is **three pixels on the top and left but two on the bottom and right** — that asymmetry is what makes a cell look raised, and it means the interior is *not* centred on the tile. Everything inside a cell positions from the interior centre; using the tile centre puts icons visibly high and left.

The selected cell uses a larger sprite and a **nearer depth band than every other cell**, so its oversized frame draws over its neighbours rather than being clipped by them.

Block icons are isometric: three quads (top, front, right) at 30°, using the same face shades as the world mesher so an icon reads like the block it places.

### Transparency

`Vertex::color` carries alpha and the pipeline blends. World geometry is opaque, so blending is a no-op for it; it exists only so HUD panels can sit over the scene.

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

Every file carries a header with magic, format version, seed, its own coordinates and block count, **all four checked on load**. This is the only place the game reads bytes it did not produce this run, so it validates rather than assumes. Player position and view direction live in `player.dat` beside the chunks.

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
| Load | 6 | Generated and kept in memory |
| Visible | 5 | Meshed and drawn |
| Unload | 8 | Erased past this |

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

The window shows a solid dark blue and nothing else. That is the intended and complete result for this milestone.

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
- **GPU selection:** correctly picks `NVIDIA GeForce RTX 4070 Laptop GPU`, not the Intel iGPU that enumerates first.
- **Swapchain:** 3 images, `mailbox` present mode, rebuilt cleanly on every resize.
- **Frame pacing:** holds 120–121 fps against a 120 fps target while the machine is active. Extended idle sessions show stretches near 66 fps, attributed to laptop power management dropping the panel refresh rate — not an engine fault, and not investigated further per user direction.
- **GPU load:** ~9 W at ~500 MHz core clock while rendering the triangle at M2, i.e. essentially idle. Third-party overlay tools report an implausible frame rate on this machine; trust the engine's own counter (see the third-party layer note above).
- **Shutdown:** closing the window saves every modified chunk and the player's position, logs how many chunks were written, and exits through the normal path with no validation errors and no crash.

---

## Update Discipline

Keep this file factual, current, and concise. Replace superseded facts in place — no changelog, no dated entries, no "previously this was X."

Anything that is a *story* (why a choice was made, what was tried and rejected, what fooled someone) belongs in `CLAUDE.md` instead.
