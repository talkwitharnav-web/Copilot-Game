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

Six rules drove the ordering. When a future session is tempted to reorder something, check it against these first.

1. **Get it into the user's hands as early as possible, then keep it there.** The user is the project's primary bug-finding resource and has said so explicitly (2026-07-30): _"the best thing for the game is to get playability on early so that i can 'play' the game and report findings back to you. i am the most valuable bug finding resource you can have."_ Real play finds problems in minutes that reasoning about code does not find at all. **From M3 onward, every milestone must end in something runnable that the user can react to** — not a library, not a refactor with nothing to see. If a milestone cannot produce something playable or visible, it is too big and should be split.
2. **Prove the technology before depending on it.** Every milestone that introduces a new piece of tech does so in the smallest program that can demonstrate it, so failures are attributable.
3. **Correct, then measurable, then fast.** No optimization or multithreading before something works single-threaded and its cost can actually be measured. This is an explicit user instruction, not a style preference.
4. **Make it a game before making it beautiful.** A gorgeous renderer attached to a world nobody can play in is a tech demo. Gameplay comes before the heavy graphics phases.
5. **Big architectural changes go early within their phase.** Restructuring the renderer for PBR/deferred is far cheaper before ten effects depend on the old structure.
6. **One milestone, one working build.** The repository always compiles and runs. Never leave it broken between milestones.

> **On the source of the vision above:** the long, highly detailed feature lists were drafted with the help of another AI, not written by the user directly (disclosed 2026-07-30). Treat them as a **menu of possibilities and a statement of ambition, not a specification**. Where the user's own plain-language wishes conflict with a bullet in that list, **the user wins.** Do not treat any item there as a commitment, and do not implement something merely because it appears on the list.

---

## What Must Be Right Early, and What Can Be Rewritten Later

Deferring work is only safe when the later work is *additive*. Some things are not, and those must be designed correctly from the first milestone that touches them, even though the payoff arrives much later.

### Multithreading — the constraint is data ownership, not threads

Multithreading arrives at **M12**, but it becomes *impossible* if M4–M10 are written carelessly. The threads are the easy part; untangling shared mutable state afterwards is the rewrite. Minecraft's well-known stutter is largely this problem, and it is a large part of why this engine exists at all.

**Three rules that make M12 a migration instead of a rewrite. They apply from M4 onward, with no exceptions:**

1. **Chunk generation must be a pure function of `(seed, chunkCoord)`.** It may not read the state of neighbouring chunks, global mutable data, wall-clock time, or anything else. If two different threads generate the same chunk they must produce byte-identical results. This also gives determinism for free, which M5 requires anyway.
2. **Meshing must be a pure function of `(chunk blocks, neighbouring border blocks)` returning vertex data.** It must not touch the live world, not upload to the GPU itself, and not mutate the chunk it is reading. Hand it a snapshot; take back a buffer.
3. **Only one place mutates the world.** Block edits go through a single owner on the main thread. Workers receive copies and return results; they never write into live chunk storage.

Follow these and M12 is "call these existing functions from a worker pool." Break them and M12 means rewriting the world layer.

**Before M12 lands, single-threaded streaming must still be playable** — so M8 gets a per-frame time budget for generation and meshing (do as much work as fits in a few milliseconds, finish the rest next frame). That converts a hard freeze into a slightly slower horizon, which is testable. It is not a substitute for M12.

### GPU/graphics — deliberately *not* future-proofed

**The renderer will be substantially rewritten at M23, and that is the plan, not a failure.**

Building a deferred, physically based, HDR pipeline before there is a single block on screen would mean carrying enormous complexity through every early milestone for no benefit, and doing it blind — with no real scene, no real content, and no measurements to design against.

What actually changes at M23 is the *rendering path*: passes, shaders, material handling, vertex formats. That is genuinely rewritable in isolation. What does **not** change is the world data, chunk storage, generation, meshing inputs, persistence, physics, and gameplay — the expensive parts to get wrong. Vertex formats will also change at M14 (lighting) and again at M23; that churn is localized inside meshing and is accepted.

So: **no `IRenderBackend` interface, no material abstraction layer, no "PBR-ready" hooks before M23.** Per `CLAUDE.md`, no abstraction without a second implementation actually in sight. The cheap insurance is keeping mesh *generation* separate from mesh *upload* (rule 2 above), which we need for threading regardless.

### Summary

| Area | When | Retrofit risk | Mitigation |
|---|---|---|---|
| World data ownership | M4+ | **Severe** | The three purity rules above, enforced from M4 |
| Determinism | M5 | **Severe** | Generation depends only on seed and coordinates |
| Job system | M12 | Low, if the above hold | Migration, not rewrite |
| Mesh optimization / LOD | M13 | Low | Purely additive to meshing |
| Renderer architecture | M23 | Low, and accepted | Isolated to the rendering path |
| Ray tracing | M30 | Low | Hybrid; sits on top of M23 |

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

### ✅ M2 — Shader pipeline and first triangle · **Core**

The Vulkan graphics pipeline, vertex and fragment shaders, and shader compilation (`glslc`) wired into the CMake build so shaders rebuild automatically.

**Why now:** every single later rendering feature is a shader. Proving the compile-and-load path on a triangle means a broken shader is never confused with a broken renderer.

**Done when:** *(met 2026-07-30)* a coloured triangle renders; editing a `.glsl` file and rebuilding changes what is on screen.

### ▶ M3 — 3D scene: camera, depth, cube · **Core**

**Split into four parts at user request** (2026-07-30) — the combined milestone was roughly triple the size of M2, and each part below fails in a distinct, recognisable way. Doing them separately means a bug is attributable on sight rather than by bisection.

#### ✅ M3a — Geometry from GPU buffers

Vertex and index buffers in device-local memory, uploaded through a staging buffer; vertex input layout declared in the pipeline. The shader stops inventing its own corners.

**Isolates:** GPU memory allocation, memory-type selection, and transfer/upload synchronisation.

**You can:** see the same triangle as M2 — but now editing a C++ array changes its shape.

**Done when:** *(met 2026-07-30)* the triangle is identical to M2's, driven entirely by buffer data, with zero validation errors and no leaked Vulkan objects at shutdown.

#### ▶ M3b — Matrices, perspective, and a cube

GLM arrives. Model/view/projection matrix delivered by push constants, cube geometry, backface culling enabled. The cube rotates on its own so its three-dimensionality is visible.

**Isolates:** the coordinate-convention traps. Vulkan's Y axis points down and its depth range is 0→1, while GLM defaults to OpenGL's conventions — the single most common source of an upside-down or invisible scene.

**Note:** deliberately **no depth buffer yet.** A single convex cube renders correctly on backface culling alone, so if something looks wrong here it is the matrices, not a missing depth test.

**You can:** watch a correctly-shaped cube rotate in perspective.

**Done when:** the cube is right-way-up, correctly proportioned at any window size, and no interior faces show through.

#### ⬜ M3c — Depth buffer

A depth image and depth testing, recreated alongside the swapchain on resize. Verified by adding a **second, overlapping cube** — a scene that cannot possibly render correctly without a working depth test.

**Isolates:** depth format selection, attachment setup, and resize recreation.

**Done when:** two overlapping cubes occlude each other correctly from every angle, and resizing does not break it.

#### ⬜ M3d — Free-fly camera

Camera position and orientation, mouse-look with cursor capture, WASD plus vertical movement, all scaled by delta time. `Escape` releases the mouse.

**Isolates:** input handling and frame-rate-independent movement.

**You can:** fly around the scene. First build you genuinely control.

**Done when:** movement speed is identical at 30 fps and 240 fps, and looking around has no drift or snapping.

---

# Phase 1 — The Playable Slice

*Goal: the shortest possible route to "I can walk around a world and change it." Everything non-essential to that sentence is deliberately postponed — textures, streaming, saving, lighting, optimization all come after.*

> **This phase exists because of sequencing rule 1.** An earlier draft put first playability at M9, behind texturing and chunk streaming. That was reordered on user request so real playtesting starts as early as possible. Each milestone here is deliberately small.

### ⬜ M4 — First chunk · **Core**

Block storage for a single fixed-size chunk (e.g. 32³), and meshing that emits only faces exposed to air. **Flat colours per block type — no textures yet.**

**Why textures are skipped:** solid colours are enough to see shape, spot meshing bugs, and play. Texturing is a whole milestone that would delay playability without changing whether the game *works*.

**Threading constraint (see "What Must Be Right Early"):** meshing must be a pure function taking block data plus neighbour borders and returning vertex data. It must not upload to the GPU itself and must not mutate what it reads. This costs nothing now and is what makes M12 possible.

**You can:** look at a real chunk of blocks and fly through it.

**Done when:** a hand-filled chunk renders as solid geometry with interior faces correctly absent.

### ⬜ M5 — Terrain generation · **Core**

Deterministic seeded generation. Noise-driven heightmap terrain across a modest fixed area — **not** infinite streaming yet. A generation architecture that can later accept caves, biomes, and structures.

**Threading constraint:** generation must be a pure function of `(seed, chunkCoord)` — no reads of neighbouring chunks, no global mutable state, no time. This is simultaneously what makes the world deterministic and what makes M12 a migration rather than a rewrite.

**You can:** fly over generated hills and valleys. Change the seed, get a different world.

**Done when:** the same seed always produces identical terrain.

### ⬜ M6 — Walking and collision · **Core**

AABB collision against voxels, gravity, jumping, step-up. First-person player controller. Free-fly stays available as a debug toggle.

**You can:** *walk* on the terrain instead of flying through it. This is the first build that feels like a game.

**Done when:** you can walk and jump across terrain, cannot fall through the world, and cannot clip into solid blocks.

### ⬜ M7 — Break and place blocks · **Core** · 🎮 **FIRST PLAYABLE**

Voxel raycasting, block breaking and placing, a highlight on the targeted block, and correct re-meshing of only the affected chunks.

**Why this is the milestone that matters:** it closes the loop. Walk, look, break, build. From here on, **every future milestone should be handed over as something to actually play**, and user bug reports become the main source of truth about what is broken.

**You can:** play. Genuinely — walk around, dig, build. Rough, untextured, small, but a game.

**Done when:** breaking and placing update instantly, including across chunk boundaries, with no holes or stale geometry.

---

# Phase 2 — A World Worth Staying In

*Goal: turn the playable slice into a world that is endless, looks like something, and remembers what you did.*

### ⬜ M8 — Chunk streaming · **Core**

World-space chunk coordinates, neighbour-aware meshing so chunk seams are not solid walls, and load/unload around the player. **Still single-threaded** — deliberately.

**Must include a per-frame time budget** for generation and meshing, so a burst of new chunks slows the horizon down instead of freezing the game. Without it this milestone is unpleasant to playtest, which defeats the point.

**You can:** walk in one direction forever.

**Done when:** moving in any direction loads and unloads chunks continuously, with no seams and no memory leaks, and no single frame stalls badly enough to feel like a hitch.

### ⬜ M9 — Textures and block types · **Core**

Image loading, Vulkan images and samplers, a texture atlas or array, and the first real set of block types.

**You can:** see a world that actually looks like a world.

**Done when:** blocks are textured with correct filtering and mipmaps, and adding a new block type is a small, obvious change.

### ⬜ M10 — World persistence · **Core**

Saving and loading modified chunks to disk; unmodified chunks regenerate from the seed rather than being stored.

**You can:** quit, come back, and your build is still there.

**Done when:** relaunching restores the world exactly, including edits, at reasonable file sizes.

---

# Phase 3 — It Performs

*Goal: the point where single-threaded stops being acceptable. Deliberately after correctness.*

### ⬜ M11 — Profiling and diagnostics · **Core**

Frame-time graph, CPU/GPU timing, chunk counts, draw-call and triangle counts, memory usage — as an in-game overlay. Debug UI library arrives here.

**Why now (and not later):** the next milestone is optimization, and optimizing without measurement is guessing. This must exist *before* M12, not after.

**Done when:** the real cost of generation, meshing, and rendering is visible per-frame at runtime.

### ⬜ M12 — Job system and multithreading · **Core**

A general worker-thread job system. Migrate world generation and chunk meshing onto it first, since they are the obvious wins and were written as pure parameters-in/results-out functions specifically to make this migration cheap.

**Why now:** the code was structured for this from M4, the cost is measurable from M11, and every later system builds on top of it. **If this milestone turns out to require restructuring the world layer, the purity rules were violated somewhere earlier — find and fix that rather than working around it.**

**You can:** walk fast, in any direction, without the world hitching to catch up.

**Done when:** generation and meshing no longer stall the render thread, main-thread frame time is measurably lower than the M11 baseline, and worlds remain byte-identical regardless of thread count.

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
| **Playtesting** | From M7 on, hand the user a runnable build at every milestone and ask what felt wrong. Their reports outrank reasoning about the code — if they say something is broken, it is broken and has not been found yet. |
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
| M3b | GLM | Vectors, matrices, projection |
| M4+ | Vulkan Memory Allocator | Practical GPU memory management |
| M5 | A noise library | Terrain generation |
| M9 | stb_image | Loading texture files |
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
