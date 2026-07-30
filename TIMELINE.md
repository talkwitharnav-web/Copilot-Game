# TIMELINE.md

The route from "a window that clears to blue" to the finished game.

**This is a sequencing document, not a promise.** It exists so that any future session — human or agent — can answer three questions without guessing: *what are we building toward*, *what is the very next thing*, and *why is it next and not something else*.

Read [`SYSTEM_MEMORY.md`](SYSTEM_MEMORY.md) for what currently exists and [`CLAUDE.md`](CLAUDE.md) for why settled decisions were made.

---

## The End Product

An **original voxel sandbox game** with the core freedoms of the genre — exploration, building, destruction, gathering, crafting, survival, discovery, progression — running on a **custom C++20/Vulkan engine** built from scratch, targeting modern high-end Windows PCs.

Someone should reasonably describe it as *a voxel sandbox*, while it is still obviously **its own game**: its own creatures, resources, biomes, structures, progression, events, mechanics, and visual identity.

The bar is deliberately higher than "Minecraft clone with shaders." The renderer should be a genuinely modern hybrid renderer — rasterization, compute, and ray tracing each doing what they are best at — not a retrofit of pretty effects onto a naive engine.

### Explicitly out of scope

- **Multiplayer. Permanently cut** (user decision, 2026-07-30). This is a single-player game.
  This is not merely "later" — it is a **design freedom**. No client/server split, no
  server-authoritative simulation, no replication, no netcode budget, no lockstep determinism
  constraints on gameplay systems. Do not add abstractions "so multiplayer is possible later."
  If that ever changes it is a deliberate re-architecture, not something to hedge against now.
- Console/mobile ports, Linux/macOS builds. Vulkan keeps the door open; nothing targets it.
- Modding APIs, user-generated content pipelines, Steam Workshop.

---

## How This Sequence Was Chosen

Five rules drove the ordering. When a future session is tempted to reorder something, check it against these first.

1. **Prove the technology before depending on it.** Every milestone that introduces a new piece of tech does so in the smallest program that can demonstrate it, so failures are attributable.
2. **Correct, then measurable, then fast.** No optimization or multithreading before something works single-threaded and its cost can actually be measured. This is an explicit user instruction, not a style preference.
3. **Make it a game before making it beautiful.** A gorgeous renderer attached to a world nobody can play in is a tech demo. Gameplay comes before the heavy graphics phases — but *after* the world exists and performs, because gameplay built on a broken foundation gets rewritten.
4. **Big architectural changes go early within their phase.** Restructuring the renderer for PBR/deferred is far cheaper before ten effects depend on the old structure.
5. **One milestone, one working build.** The repository always compiles and runs. Never leave it broken between milestones.

---

## Status Legend

| Tag | Meaning |
|---|---|
| **Core** | The game cannot ship without it. |
| **Optional** | Real value, but the game is still the game without it. Cut freely under pressure. |
| **Experimental** | Might not work out, or might be replaced. Timebox it; be willing to abandon. |
| **Long-term** | Correct destination, far away. Do not build toward it speculatively. |

Milestone states: ✅ **Done** · ▶ **Next** · ⬜ Not started

---

# Phase 0 — Foundation

*Goal: prove every layer of the technology stack, with nothing voxel-specific yet.*

### ✅ M1 — Toolchain proof · **Core**

Window, Vulkan instance/device/swapchain, render loop, clear colour, clean shutdown, adjustable frame cap.

**Done when:** *(met 2026-07-30)* zero warnings at `/W4`, zero validation errors, correct discrete-GPU selection, clean exit. See `SYSTEM_MEMORY.md`'s validation baseline.

### ▶ M2 — Shader pipeline and first triangle · **Core**

The Vulkan graphics pipeline, vertex and fragment shaders, and shader compilation (`glslc`) wired into the CMake build so shaders rebuild automatically.

**Why now:** every single later rendering feature is a shader. Proving the compile-and-load path on a triangle means a broken shader is never confused with a broken renderer.

**Done when:** a coloured triangle renders; editing a `.glsl` file and rebuilding changes what is on screen.

### ⬜ M3 — 3D scene: camera, depth, cube · **Core**

Perspective projection, depth buffering, uniform/push-constant data, a free-fly camera with mouse-look and WASD.

**Why now:** you cannot debug terrain you cannot fly around and cannot trust geometry without a depth buffer. This is the last milestone before the world exists.

**Done when:** a textured-less cube renders correctly from any angle, near/far faces occlude properly, and the camera moves smoothly at the frame cap.

### ⬜ M4 — Textures and materials (basic) · **Core**

Image loading, Vulkan images/samplers, a texture atlas or array suitable for many block types.

**Done when:** the cube has different textures per face, sampled with correct filtering and mipmaps.

---

# Phase 1 — A Voxel World Exists

*Goal: from "a cube" to "an endless streamed world you can fly through."*

### ⬜ M5 — First chunk · **Core**

Block storage for a single fixed-size chunk (e.g. 32³), and meshing that emits only faces exposed to air.

**Why now:** the smallest thing that is genuinely *voxel*. Naive meshing on purpose — it becomes the baseline that later optimization is measured against.

**Done when:** a hand-filled chunk renders as solid geometry with interior faces correctly absent.

### ⬜ M6 — Chunk streaming · **Core**

World-space chunk coordinates, neighbour-aware meshing (so chunk seams are not solid walls), and load/unload around the camera. **Still single-threaded** — deliberately.

**Done when:** flying in any direction loads and unloads chunks continuously with no seams and no leaks.

### ⬜ M7 — Procedural terrain · **Core**

Deterministic seeded generation. Noise-driven heightmap terrain to begin with; a generation architecture that can later accept caves, biomes, and structures.

**Why now:** provides effectively infinite world to stress-test streaming, and locks in determinism early — retrofitting a fixed seed into a generator is painful.

**Done when:** the same seed always produces byte-identical terrain, and the world is explorable without limit.

---

# Phase 2 — It Is Playable

*Goal: from "a world you fly through" to "a world you interact with."*

### ⬜ M8 — Player physics and collision · **Core**

AABB collision against voxels, gravity, jumping, stepping, swimming placeholder. First-person player controller replacing the free-fly camera (keep free-fly as a debug mode).

**Done when:** you can walk terrain, cannot fall through the world, and cannot clip into solid blocks.

### ⬜ M9 — Block interaction · **Core**

Voxel raycasting, block breaking and placing, targeted-block highlight, and correct incremental re-meshing of only the affected chunks.

**Why now:** this is the first moment the project is a *game* rather than a renderer. It also stresses the mesh-invalidation path, which is a common source of subtle bugs — better found now.

**Done when:** breaking and placing blocks updates instantly, including across chunk boundaries.

### ⬜ M10 — World persistence · **Core**

Saving and loading modified chunks to disk with a region-file style format; unmodified chunks regenerate from the seed rather than being stored.

**Done when:** quit and relaunch restores the world exactly, including edits, at reasonable file sizes.

---

# Phase 3 — It Performs

*Goal: the point where single-threaded stops being acceptable. Deliberately after correctness.*

### ⬜ M11 — Profiling and diagnostics · **Core**

Frame-time graph, CPU/GPU timing, chunk counts, draw-call and triangle counts, memory usage — as an in-game overlay. Debug UI library arrives here.

**Why now (and not later):** the next milestone is optimization, and optimizing without measurement is guessing. This must exist *before* M12, not after.

**Done when:** the real cost of generation, meshing, and rendering is visible per-frame at runtime.

### ⬜ M12 — Job system and multithreading · **Core**

A general worker-thread job system. Migrate world generation and chunk meshing onto it first, since they are the obvious wins and were written as parameters-in/results-out functions specifically to make this migration cheap (see `CLAUDE.md`'s architecture rules).

**Why now:** the code was structured for this from M1, the cost is measurable from M11, and every later system will be built on top of it.

**Done when:** generation and meshing no longer stall the render thread, main-thread frame time is measurably lower, and results remain deterministic regardless of thread count.

### ⬜ M13 — Mesh and render optimization · **Core**

Greedy meshing or equivalent face merging, frustum culling, level-of-detail for distant chunks, draw-call batching, indirect drawing.

**Done when:** render distance increases substantially at the same frame rate versus the M12 baseline, with numbers recorded.

---

# Phase 4 — A World Worth Exploring

*Goal: terrain stops being a heightmap and becomes a place.*

### ⬜ M14 — Voxel lighting · **Core**

Sky light and block light propagation, smooth lighting, and per-vertex ambient occlusion. Incremental updates on block changes.

**Why now:** caves are unreadable without it, and it changes the mesh vertex format — cheaper before mesh formats are depended upon by more systems.

**Done when:** caves are dark, torches illuminate believably, and lighting updates without visible lag when blocks change.

### ⬜ M15 — Caves, biomes, and 3D terrain · **Core**

3D noise for overhangs and cave systems, biome definition and blending, oceans, rivers, and underground regions.

**Done when:** exploration reveals genuinely distinct regions and a connected underground worth exploring.

### ⬜ M16 — Procedural structures · **Core**

A structure placement system — deterministic, seed-driven, biome-aware — plus the first original structures.

**Done when:** structures generate reliably without corrupting terrain or chunk borders.

### ⬜ M17 — Flexible block system · **Core**

Move beyond full cubes: slabs, stairs, cross-shaped plants, fences, models with arbitrary geometry, transparency and alpha-tested rendering.

**Why now:** this changes both block data and meshing. Doing it before the content phase means content is authored once, against the final system.

---

# Phase 5 — It Is A Game

*Goal: the original identity. This is where the project stops resembling a reference implementation.*

> **Creative direction is required before this phase begins.** Phases 0–4 are largely universal voxel-engine work. From here the specifics must be *ours* — original creatures, resources, biomes, progression, events, and art direction. Deciding this is a design conversation with the user, not an implementation detail to improvise.

### ⬜ M18 — Inventory and items · **Core**
### ⬜ M19 — Crafting and resource progression · **Core**

An original crafting and progression system. **Do not port Minecraft's recipe tree, tool tiers, or material hierarchy.**

### ⬜ M20 — Entities and creatures · **Core**

Entity system, animation, pathfinding, spawning, and original creature designs and behaviours.

### ⬜ M21 — Survival systems · **Core**

Health, damage, hazards, and whatever resource-pressure mechanics fit the game's own identity.

### ⬜ M22 — Audio · **Core**

First audio dependency. Positional sound, ambience, music. Deliberately late — it is genuinely independent of everything above.

---

# Phase 6 — It Is Beautiful

*Goal: the modern high-end renderer. Ordered so that structural changes land before the effects that depend on them.*

### ⬜ M23 — Renderer restructure: PBR and deferred/hybrid · **Core**

Physically based materials (albedo, normal, roughness, metallic, emissive), a G-buffer or visibility-buffer architecture, HDR rendering and tone mapping.

**Why first in this phase:** every subsequent effect assumes this structure. Retrofitting PBR after shadows, GI, and water exist means rewriting all of them.

### ⬜ M24 — Sun, moon, and dynamic shadows · **Core**

Day/night cycle, directional lighting, cascaded shadow maps.

### ⬜ M25 — Sky and atmosphere · **Core**

Atmospheric scattering, sunrise/sunset behaviour, fog and haze, altitude effects.

### ⬜ M26 — Water as a system · **Core**

Reflection, refraction, depth-based absorption, underwater scattering, waves, foam, shoreline effects. Water is a headline visual feature, not a transparent block.

### ⬜ M27 — Volumetric clouds and weather · **Optional**

Volumetric clouds, rain and snow rendering, wind, storms, lightning.

### ⬜ M28 — Particles and foliage · **Optional**

GPU-driven particles; dense, animated, wind-affected vegetation.

### ⬜ M29 — Post-processing and image quality · **Optional**

Anti-aliasing, bloom, motion blur, depth of field, ambient occlusion passes, resolution scaling, high-refresh-rate support.

---

# Phase 7 — Frontier

*Goal: the genuinely ambitious rendering and simulation work. Everything here is optional and may be abandoned.*

### ⬜ M30 — Hardware ray tracing · **Experimental**

Hybrid only. Candidates in rough order of value: ray-traced shadows, reflections (especially water), ambient occlusion, then global illumination.

**Deliberately not assumed:** ray tracing everything is not the goal. Each effect must earn its cost against the rasterized equivalent, measured on the RTX 4070 this project is developed on.

### ⬜ M31 — Advanced world simulation · **Experimental**

Fluid flow, fire propagation, vegetation growth, erosion. Only where it produces meaningful gameplay or visual payoff — never simulation for its own sake.

### ⬜ M32 — Global illumination · **Long-term**

Real-time GI suited to a fully destructible voxel world. This is a research-grade problem; treat it as such.

---

# Phase 8 — Ship It

### ⬜ M33 — Mature developer tooling · **Optional**

Shader and asset hot reload, world-generation debug views, chunk and collision visualization, entity inspector, teleport, time and weather control, block manipulation tools.

*(Tooling grows continuously from M11 onward — this milestone is where it is consolidated and finished, not where it starts.)*

### ⬜ M34 — Settings and accessibility · **Core**

A real settings screen — graphics presets, render distance, frame cap (retiring the temporary `F1`/`F2` binding from M1), key rebinding, audio, accessibility options.

### ⬜ M35 — Packaging and distribution · **Core**

A standalone Windows build a player can run without CMake, MSVC, or the Vulkan SDK installed. Release builds, asset packaging, no developer dependencies in the shipped product.

---

## Cross-Cutting Tracks

These are not milestones; they run continuously and are everyone's responsibility.

| Track | Rule |
|---|---|
| **Performance** | Record a measurement whenever a milestone changes cost. Never optimize without a before-number. |
| **Validation** | Debug builds must stay at zero Vulkan validation errors. This is an acceptance bar, not an aspiration. |
| **Tooling** | When something is debugged the hard way twice, build the tool the third time. |
| **Originality** | From Phase 5 on, actively check that mechanics and content are ours. "How does Minecraft do it?" is a fine engineering question and a bad design one. |
| **Documentation** | `SYSTEM_MEMORY.md` after structural change; `CLAUDE.md` when a decision was contested or a bug was misleading. |

---

## Anticipated Dependencies

Listed so future sessions know roughly when each becomes justifiable — **not** as approval to add them early. The rule stands: a dependency needs a one-sentence justification *at the moment it is added*.

| Milestone | Likely dependency | For |
|---|---|---|
| M3 | GLM (or hand-rolled math) | Vectors, matrices, projection |
| M4 | stb_image | Loading texture files |
| M5+ | Vulkan Memory Allocator | Practical GPU memory management |
| M7 | A noise library | Terrain generation |
| M11 | Dear ImGui | Debug overlay and tools |
| M13 | meshoptimizer | Mesh optimization, LOD |
| M22 | An audio library | Sound |

If the dependency count approaches ~5, revisit the vcpkg-versus-FetchContent decision recorded in `CLAUDE.md`.

---

## Open Decisions

Things deliberately not decided yet. Do not silently resolve these — raise them.

- **The project name.** `VoxelGame` is a placeholder. Needed before M35, harmless until then.
- **Creative direction.** Required before Phase 5 begins. See that phase's note.
- **Chunk dimensions.** Decided at M5; affects memory, meshing cost, and streaming granularity.
- **Art direction.** Whether blocks are stylized, realistic, or something else drives M4, M17, and all of Phase 6.

---

## Update Discipline

Update the status marker when a milestone completes, and add its acceptance evidence to `SYSTEM_MEMORY.md`'s validation baseline.

**Reordering milestones is allowed** — but check the change against the five sequencing rules above, and record the reasoning in `CLAUDE.md` if the reordering was contested. Do not quietly renumber.

Do not add speculative milestones. This file describes a route, not a wish list.
