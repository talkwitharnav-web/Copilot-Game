# SYSTEM_MEMORY.md

Current technical truth for this voxel sandbox project: what exists, where it lives, what version it is, and how to build and run it.

Narrative history, rejected approaches, and debugging lessons live in `CLAUDE.md`. The milestone route and the long-term vision live in `TIMELINE.md`. **This file is factual and current-state only** — when something changes, replace the old fact in place rather than appending.

> **Status:** Milestones 1–9 complete. The game is playable: an endless seeded world streams in around the player, who walks, jumps, sprints, crouches, and breaks and places textured blocks. Still single-threaded and unlit, both by design. `TIMELINE.md` M10 (world persistence) is next.

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
│   └── dev-env.ps1         Loads the MSVC environment into the current shell
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
| `game` | Executable | The actual runnable program. Currently just opens a window and clears it. |

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
| `Block` | `world/Block.hpp` | The block ID enum, whether an ID is solid, and its flat colour. Colours are placeholders until textures exist. |
| `Chunk` | `world/Chunk.hpp` | A 32³ block of world as a flat array, indexed `x + z*32 + y*32*32`. Reads outside the chunk return air rather than failing, so callers do not need bounds checks everywhere. |
| `noise` | `world/Noise.hpp` | Seeded value noise and fractal Brownian motion. An integer hash, so it is reproducible on any machine without storing anything. |
| `TerrainGenerator` | `world/TerrainGenerator.hpp` | `generateChunk(seed, coord)` — **a pure function**, and required to stay one. No neighbour reads, no global state, no clock. This is what makes the world deterministic and what makes background generation a migration rather than a rewrite. |
| `ChunkMesher` | `world/ChunkMesher.hpp` | Turns a chunk plus its six neighbours into mesh data, emitting only faces that touch air. Neighbours are passed in rather than looked up, which keeps meshing pure too. |
| `World` | `world/World.hpp` | Owns every loaded chunk in a hash map keyed by chunk coordinate, and streams them in and out around the player. The single owner of block state. |
| `Raycast` | `world/Raycast.hpp` | Walks the view ray cell by cell to find the block being aimed at, and the empty cell in front of it where a new block goes. Steps block to block rather than sampling at intervals, so it cannot skip a block at any angle. |
| `BlockOutline` | `world/BlockOutline.hpp` | The wireframe cage marking the targeted block. Built from thin solid bars so it needs no second pipeline or line-width support. |
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
| `Space` | Jump (walking) / rise (flying) |
| `Left Shift` | Sneak (walking) / descend (flying) |
| `Left Ctrl` | Sprint |
| `F` | Toggle free-fly debug mode |
| Left click | Break the targeted block (hold to repeat) |
| Right click | Place against the targeted face (hold to repeat) |
| `1` `2` `3` `4` | Choose stone / dirt / grass / sand |
| `Escape` | Release the mouse cursor |
| Left click | Recapture the cursor when released |
| `F1` / `F2` | Lower / raise the frame cap |

Movement is scaled by delta time and the direction vector is normalised, so diagonal movement is not faster than straight movement. Movement direction is flattened to the horizontal plane, so looking down does not drive the player into the ground.

---

## Player Physics

The player is an axis-aligned box, **0.6 m wide, 1.8 m tall**, with eyes at **1.62 m**. `Player::position` is the centre of the feet, because that is the natural anchor for standing on a surface. One block is one cubic metre, so these are directly comparable to real human proportions.

| Quantity | Value |
|---|---|
| Walk / sprint / sneak | 4.317 / 5.612 / 1.295 m/s |
| Fly | 22 m/s |
| Gravity | 32 m/s² |
| Terminal velocity | 78.4 m/s |
| Jump apex | ~1.25 blocks |
| Automatic step-up | 0.6 m |

These are tuning numbers, not part of the game's identity, and are expected to change once there is real content to move through.

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

The PNGs are ordinary files and may be edited by hand instead; the script is a starting point, not a pipeline step. `tools/preview-textures.ps1` magnifies textures into a labelled sheet, because 16×16 cannot be judged at actual size.

Third-party textures kept for visual reference live in `reference/`, which is gitignored and deliberately outside `assets/` so the build cannot copy them into the game.

---

## Chunk Streaming

Chunks live in a `std::unordered_map` keyed by world chunk coordinate. The world is **3 chunks (96 blocks) tall** and streams horizontally only.

| Radius | Chunks | Meaning |
|---|---|---|
| Load | 6 | Generated and kept in memory |
| Visible | 5 | Meshed and drawn |
| Unload | 8 | Erased past this |

**Visible is one less than load on purpose.** A chunk is only meshed once its four horizontal neighbours exist; meshing against a missing neighbour emits a full sheet of faces at the frontier that has to be thrown away when the neighbour arrives. **Unload is larger than load** so pacing back and forth across the boundary does not thrash chunks in and out.

Generation and meshing share a **3 ms per-frame budget**. Whatever does not fit waits for the next frame, so a burst of new terrain slows the horizon down instead of freezing the game. Unloading is never metered — it frees memory and is nearly free.

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

Verified 2026-07-30 on this machine.

- **Configure:** `cmake --preset debug` succeeds. GLFW 3.4 fetched, `Found Vulkan: 1.4.357` with `glslc` and `glslangValidator` components.
- **Compile:** clean build, **zero warnings** at `/W4 /permissive-`. Shaders compile to SPIR-V as part of the build (`triangle.vert.spv` 1512 bytes, `triangle.frag.spv` 572 bytes).
- **Runtime:** window opens at 1280x720; validation layers report **no errors**, including across a continuous ~50-minute session.
- **M2 result:** an RGB-interpolated triangle renders correctly, confirmed visually. Resizing scales it correctly with no flicker, no crash, and no validation errors; the swapchain rebuilds on each resize as expected.
- **M3 result:** six shaded boxes on a ground plane, 144 vertices / 72 triangles, render solid with correct mutual occlusion from every angle. Free-fly camera confirmed smooth with no ghosting or artifacting. Resizing rebuilds both swapchain and depth image cleanly.
- **M4 result:** a single 32³ chunk emits 5098 faces where naive per-block meshing would emit 196608 — 97.4% of the work discarded before it reaches the GPU.
- **M5 result:** value-noise fBm heightmap, 5 octaves, seed 1337. Determinism check passes: regenerating a chunk reproduces it byte for byte. Measured on the pre-streaming fixed area, ~27 ms to generate and ~156 ms to mesh 72 chunks.
- **M6 result:** player spawns on the surface at the world centre and walks, jumps, sprints and sneaks across terrain without clipping into blocks or falling through the world. Steady 120–122 fps against a 120 fps cap with collision running every frame.
- **M7 result:** breaking and placing work across chunk boundaries with no holes or stale geometry, at 12 m reach with hold-to-repeat.
- **M8 result:** verified over a continuous 135-second flight. Loaded chunks oscillated in a bounded 594–663 band and mesh slots 441–555, tracking each other; pending work spiked to 78 and returned to zero every time; retired GPU buffers stayed at zero throughout. Frame rate held **119–121 fps** with no hitch at any point. Initial load is ~970 ms for 507 chunks, taken as a deliberate one-off stall at startup rather than pop-in.
- **GPU selection:** correctly picks `NVIDIA GeForce RTX 4070 Laptop GPU`, not the Intel iGPU that enumerates first.
- **Swapchain:** 3 images, `mailbox` present mode, rebuilt cleanly on every resize.
- **Frame pacing:** holds 120–121 fps against a 120 fps target while the machine is active. Extended idle sessions show stretches near 66 fps, attributed to laptop power management dropping the panel refresh rate — not an engine fault, and not investigated further per user direction.
- **GPU load:** ~9 W at ~500 MHz core clock while rendering the triangle, i.e. essentially idle. Third-party overlay tools report an implausible frame rate on this machine; trust the engine's own counter (see the third-party layer note above).
- **Shutdown:** closing the window logs `Window closed. Shutting down.` and exits through the normal path with no validation errors and no crash.

---

## Update Discipline

Keep this file factual, current, and concise. Replace superseded facts in place — no changelog, no dated entries, no "previously this was X."

Anything that is a *story* (why a choice was made, what was tried and rejected, what fooled someone) belongs in `CLAUDE.md` instead.
