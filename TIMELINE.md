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

What actually changes at M23 is the *rendering path*: passes, shaders, material handling, vertex formats. That is genuinely rewritable in isolation. What does **not** change is the world data, chunk storage, generation, meshing inputs, persistence, physics, and gameplay — the expensive parts to get wrong. The vertex format was expected to churn at M14 as well; in the event it did not, because lighting folded into the existing per-vertex colour and the sun's face normal is recovered in the fragment shader.

So: **no `IRenderBackend` interface, no material abstraction layer, no "PBR-ready" hooks before M23.** Per `CLAUDE.md`, no abstraction without a second implementation actually in sight. The cheap insurance is keeping mesh *generation* separate from mesh *upload* (rule 2 above), which we need for threading regardless.

### Summary

| Area | When | Retrofit risk | Mitigation |
|---|---|---|---|
| World data ownership | M4+ | **Severe** | The three purity rules above, enforced from M4 |
| Determinism | M5 | **Severe** | Generation depends only on seed and coordinates |
| Job system | M12 | Low, if the above hold | Migration, not rewrite |
| Mesh optimization / LOD | M13b | Low | Purely additive to meshing |
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

### ✅ M3 — 3D scene: camera, depth, cube · **Core**

**Split into four parts at user request** (2026-07-30) — the combined milestone was roughly triple the size of M2, and each part below fails in a distinct, recognisable way. Doing them separately means a bug is attributable on sight rather than by bisection.

#### ✅ M3a — Geometry from GPU buffers

Vertex and index buffers in device-local memory, uploaded through a staging buffer; vertex input layout declared in the pipeline. The shader stops inventing its own corners.

**Isolates:** GPU memory allocation, memory-type selection, and transfer/upload synchronisation.

**You can:** see the same triangle as M2 — but now editing a C++ array changes its shape.

**Done when:** *(met 2026-07-30)* the triangle is identical to M2's, driven entirely by buffer data, with zero validation errors and no leaked Vulkan objects at shutdown.

#### ✅ M3b — Matrices, perspective, and a cube

GLM arrives. Model/view/projection matrix delivered by push constants, cube geometry, backface culling enabled. The cube rotates on its own so its three-dimensionality is visible.

**Isolates:** the coordinate-convention traps. Vulkan's Y axis points down and its depth range is 0→1, while GLM defaults to OpenGL's conventions — the single most common source of an upside-down or invisible scene.

**Note:** deliberately **no depth buffer yet.** A single convex cube renders correctly on backface culling alone, so if something looks wrong here it is the matrices, not a missing depth test.

**You can:** watch a correctly-shaped cube rotate in perspective.

**Done when:** *(met 2026-07-30)* the cube is right-way-up, correctly proportioned at any window size, and no interior faces show through.

#### ✅ M3c — Depth buffer

A depth image and depth testing, recreated alongside the swapchain on resize. Verified against a scene of six boxes at different depths — geometry that cannot render correctly without a working depth test.

**Isolates:** depth format selection, attachment setup, and resize recreation.

**Done when:** *(met 2026-07-30)* boxes occlude each other correctly from every angle, and resizing does not break it.

#### ✅ M3d — Free-fly camera

Camera position and orientation, mouse-look with cursor capture and raw motion, WASD plus vertical movement, all scaled by delta time. `Escape` releases the mouse; clicking recaptures it.

**Isolates:** input handling and frame-rate-independent movement.

**You can:** fly around the scene. First build genuinely under player control.

**Done when:** *(met 2026-07-30)* movement is frame-rate independent, diagonal movement is not faster than straight, and looking around has no drift or snapping.

---

# Phase 1 — The Playable Slice

*Goal: the shortest possible route to "I can walk around a world and change it." Everything non-essential to that sentence is deliberately postponed — textures, streaming, saving, lighting, optimization all come after.*

> **This phase exists because of sequencing rule 1.** An earlier draft put first playability at M9, behind texturing and chunk streaming. That was reordered on user request so real playtesting starts as early as possible. Each milestone here is deliberately small.

### ✅ M4 — First chunk · **Core**

Block storage for a single fixed-size chunk (e.g. 32³), and meshing that emits only faces exposed to air. **Flat colours per block type — no textures yet.**

**Why textures are skipped:** solid colours are enough to see shape, spot meshing bugs, and play. Texturing is a whole milestone that would delay playability without changing whether the game *works*.

**Threading constraint (see "What Must Be Right Early"):** meshing must be a pure function taking block data plus neighbour borders and returning vertex data. It must not upload to the GPU itself and must not mutate what it reads. This costs nothing now and is what makes M12 possible.

**You can:** look at a real chunk of blocks and fly through it.

**Done when:** a hand-filled chunk renders as solid geometry with interior faces correctly absent.

**Result:** *(met 2026-07-30)* a 32³ chunk emitted 5098 faces where naive per-block meshing would emit 196608 — 97.4% discarded before reaching the GPU.

### ✅ M5 — Terrain generation · **Core**

Deterministic seeded generation. Noise-driven heightmap terrain across a modest fixed area — **not** infinite streaming yet. A generation architecture that can later accept caves, biomes, and structures.

**Threading constraint:** generation must be a pure function of `(seed, chunkCoord)` — no reads of neighbouring chunks, no global mutable state, no time. This is simultaneously what makes the world deterministic and what makes M12 a migration rather than a rewrite.

**You can:** fly over generated hills and valleys. Change the seed, get a different world.

**Done when:** the same seed always produces identical terrain.

**Result:** *(met 2026-07-30)* value-noise fBm heightmap, 5 octaves. A 72-chunk test area generated in ~27 ms and meshed in ~156 ms. Determinism verified by regenerating a chunk and comparing it byte for byte.

### ✅ M6 — Walking and collision · **Core**

AABB collision against voxels, gravity, jumping, step-up. First-person player controller. Free-fly stays available as a debug toggle.

**You can:** *walk* on the terrain instead of flying through it. This is the first build that feels like a game.

**Done when:** you can walk and jump across terrain, cannot fall through the world, and cannot clip into solid blocks.

**Result:** player is a 0.6 × 1.8 × 0.6 m box with eyes at 1.62 m, spawned on the surface at the world centre. Movement resolves one axis at a time against solid blocks; ledges up to 0.6 m are stepped automatically. Gravity 32 m/s², terminal velocity 78.4 m/s, jump apex ~1.25 blocks. Walk 4.317 / sprint 5.612 / sneak 1.295 m/s. `F` toggles free-fly. Steady 120 fps, clean shutdown, no validation errors.

### ✅ M7 — Break and place blocks · **Core** · 🎮 **FIRST PLAYABLE**

Voxel raycasting, block breaking and placing, a highlight on the targeted block, and correct re-meshing of only the affected chunks.

**Why this is the milestone that matters:** it closes the loop. Walk, look, break, build. From here on, **every future milestone should be handed over as something to actually play**, and user bug reports become the main source of truth about what is broken.

**You can:** play. Genuinely — walk around, dig, build. Rough, untextured, small, but a game.

**Done when:** breaking and placing update instantly, including across chunk boundaries, with no holes or stale geometry.

**Result:** 12 m reach, hold-to-repeat breaking (0.15 s) and placing (0.18 s), a wireframe cage on the targeted block, and keys 1–4 to choose the block placed. Playtesting immediately surfaced a stuck-mouse-button bug and a movement tunnelling bug — exactly the point of shipping something playable this early.

---

# Phase 2 — A World Worth Staying In

*Goal: turn the playable slice into a world that is endless, looks like something, and remembers what you did.*

### ✅ M8 — Chunk streaming · **Core**

World-space chunk coordinates, neighbour-aware meshing so chunk seams are not solid walls, and load/unload around the player. **Still single-threaded** — deliberately.

**Must include a per-frame time budget** for generation and meshing, so a burst of new chunks slows the horizon down instead of freezing the game. Without it this milestone is unpleasant to playtest, which defeats the point.

**You can:** walk in one direction forever.

**Done when:** moving in any direction loads and unloads chunks continuously, with no seams and no memory leaks, and no single frame stalls badly enough to feel like a hitch.

**Result:** chunks live in a hash map keyed by world chunk coordinate, load radius 6, unload radius 8, world 3 chunks (96 blocks) tall. A 3 ms per-frame budget meters generation and meshing. Verified over a 135-second flight: chunk and mesh counts oscillated in a bounded band (594–663 and 441–555), pending work returned to zero every time, retired GPU buffers stayed at zero, and the frame rate held 119–121 fps throughout with no hitches.

### ✅ M9 — Textures and block types · **Core**

Image loading, Vulkan images and samplers, a texture atlas or array, and the first real set of block types.

**You can:** see a world that actually looks like a world.

**Done when:** blocks are textured with correct filtering and mipmaps, and adding a new block type is a small, obvious change.

**Result:** a 2D texture array (not an atlas) with five layers, sRGB, full mip chain, nearest magnification and linear mip blending. Textures are ordinary PNGs under `assets/textures/blocks/`, generated by `tools/make-block-textures.ps1` and editable by hand. Frame rate and streaming counters were unchanged from M8.

### ✅ M10 — World persistence · **Core**

Saving and loading modified chunks to disk; unmodified chunks regenerate from the seed rather than being stored.

**You can:** quit, come back, and your build is still there.

**Done when:** relaunching restores the world exactly, including edits, at reasonable file sizes.

**Result:** most of the world never needs saving. Generation is a pure function of `(seed, chunkCoord)`, so a save stores only the *difference* between generated and played. Chunks are marked modified on edit and written when they unload or on exit; loading checks disk before falling back to the generator. Each chunk is written to a temporary file and renamed, so a crash cannot leave a half-written chunk, and headers carry magic, version, seed and coordinates, all validated on load. Player position and view direction persist too. Verified: a 43-minute session across hundreds of chunks wrote exactly one 32,796-byte file — the single chunk that was edited.

### ✅ M10b — Hotbar and block selection · **Core**

**Inserted at user request** (2026-07-30), using the letter-suffix precedent from M3a–d. Full inventory stays at M18; this is only the visible bar and its selection.

Nine slots drawn from a HUD sprite sheet, block icons rendered as isometric cubes, selection by number keys or mouse wheel.

**You can:** see what you are holding, and switch blocks without counting keys.

**Result:** the sheet is loaded as a **second texture binding** rather than a layer of the block array, because a texture array requires every layer to share one size and the sheet is 185×41 against the blocks' 16×16. A negative vertex layer selects it. Cell frames are drawn as four edge strips so the interior stays open, and vertices gained an alpha channel so the world shows through a darkened slot. Five block types were added along the way — cobblestone, gravel, snow, planks, bricks — which was the real test of M9's claim that adding a block type is a small change.

---

# Phase 3 — It Performs

*Goal: the point where single-threaded stops being acceptable. Deliberately after correctness.*

### ✅ M11 — Profiling and diagnostics · **Core**

Frame-time graph, CPU/GPU timing, chunk counts, draw-call and triangle counts, memory usage — as an in-game overlay. Debug UI library arrives here.

**Why now (and not later):** the next milestone is optimization, and optimizing without measurement is guessing. This must exist *before* M12, not after.

**Done when:** the real cost of generation, meshing, and rendering is visible per-frame at runtime.

**Result:** `F5` shows a panel with a 160-frame frame-time graph and nine numeric rows. Bars are coloured by budget — green at 120 fps, yellow at 60, orange at 30, red below — with guide lines at the 60 and 120 fps budgets, so the graph is readable without an axis. **Frame time is the headline rather than fps**, because fps is an average that hides the single long frame that actually felt bad. GPU time comes from timestamp queries read back after the frame's fence, so it measures device work rather than CPU waiting. The overlay mesh rebuilds at 20 Hz rather than every frame, which is smooth enough to read and avoids churning GPU buffers.

**Deviation:** the plan named Dear ImGui. It was not added — see `CLAUDE.md`. This milestone needs read-only numbers and a graph, which the existing screen-space mesh system already covers; ImGui's justification is the settings UI at M34.

### ✅ M12 — Job system and multithreading · **Core**

A general worker-thread job system. Migrate world generation and chunk meshing onto it first, since they are the obvious wins and were written as pure parameters-in/results-out functions specifically to make this migration cheap.

**Why now:** the code was structured for this from M4, the cost is measurable from M11, and every later system builds on top of it. **If this milestone turns out to require restructuring the world layer, the purity rules were violated somewhere earlier — find and fix that rather than working around it.**

**You can:** walk fast, in any direction, without the world hitching to catch up.

**Done when:** generation and meshing no longer stall the render thread, main-thread frame time is measurably lower than the M11 baseline, and worlds remain byte-identical regardless of thread count.

**Result:** `engine::JobSystem` is a fixed-size worker pool with a job queue. Generation and meshing both run on it; the main thread only snapshots inputs, collects results and uploads. Release build, 507 chunks:

| Workers | Generate + mesh | Triangles |
|---|---|---|
| 0 (inline) | 179 ms | 590,492 |
| 1 | 199 ms | 590,492 |
| 4 | 77 ms | 590,492 |
| 11 (default) | 55 ms | 590,492 |

Identical triangle counts at every worker count is the determinism check. **One worker is slower than zero** and that is expected: it adds a snapshot copy and a handoff without any second thread to overlap against. Its value is that the work leaves the main thread, which a startup benchmark cannot show.

**Worker count is a setting** (`settings.cfg`, `worker_threads`), defaulting to half the machine's hardware threads, and is **fixed for the run** — see `CLAUDE.md`.

**Not achieved:** upload still stalls. Generation and meshing no longer block the main thread, but every mesh upload does two full GPU waits, so "walk fast without hitching" is only half true. That is M13a.

### ✅ M13a — Mesh upload path · **Core**

Remove the per-buffer GPU stall from mesh upload. Today `uploadBufferData` allocates a staging buffer, allocates device memory, submits a copy and calls `vkQueueWaitIdle` — **per buffer**, so a 363-chunk world costs 726 submits and 726 full queue waits, and every block break costs two.

**Why now:** it is the largest remaining main-thread cost and the reason M12's own goal is not fully met. It is also a prerequisite for M13b being measurable: batching draw calls is pointless while uploads dominate.

**Scope is deliberately narrow.** Reusable staging, one fence, uploads waited on once per frame rather than once per buffer. **No transfer queue, no allocator rewrite, no batching architecture** — those belong to M13b and M23.

**You can:** break and place blocks with no GPU stall, and load a world without a serial upload tail.

**Done when:** startup upload time and per-edit stall are both measurably lower, with numbers recorded, and `vkQueueWaitIdle` no longer appears in the per-mesh path.

**Result:** `engine::UploadContext` owns a 16 MB persistently mapped staging arena, one command buffer and one fence. Copies accumulate and are submitted once per frame from `drawFrame`, ahead of the draws that read them; queue submission order plus a transfer→vertex-input barrier is what makes them visible, with no transfer queue and no semaphore. It only ever blocks when the arena must be reused before the GPU has finished with it.

Release build, 507 chunks, same 590,492 triangles throughout:

| | Generate + mesh | Upload | Total |
|---|---|---|---|
| Pre-M12 (0 workers) | 179 ms | 180 ms | 356 ms |
| M12 (11 workers) | 55 ms | 186 ms | 245 ms |
| **M13a (11 workers)** | **51 ms** | **33 ms** | **84 ms** |

Upload is **5.6× faster** and startup overall is **4.2×** better than the pre-M12 baseline. Submissions at startup went from 726 to roughly 4. `vkQueueWaitIdle` is gone from the mesh path entirely; `uploadBufferData` survives only for one-shot texture loads and is documented as such.

**Deliberately not done:** device-local buffers are still one allocation per mesh. That is the sub-allocator question, and it belongs with M13b/VMA rather than here.

### ✅ M13b — Mesh and render optimization · **Core**

Greedy meshing or equivalent face merging, frustum culling, level-of-detail for distant chunks, draw-call batching, indirect drawing.

**Done when:** render distance increases substantially at the same frame rate versus the M12 baseline, with numbers recorded.

**Result:** two changes, both measured on the release build.

**Greedy meshing.** The mesher merged adjacent identical faces into single quads instead of emitting two triangles per block face. No vertex format change was needed: the sampler already repeats, so a merged quad takes texture coordinates of 0→N and tiles. Geometry fell **~6.4×**.

**Frustum culling.** Each mesh gets a world-space bounding box on upload, tested against the six planes of the view-projection matrix. Draw calls fell ~70%.

| Distance | Triangles | RAM | GPU | Draws | FPS uncapped |
|---|---|---|---|---|---|
| 12 before | 3,060,162 | — | 1.77 ms | 813 | 552 |
| **12 after** | **486,420** | **331 MB** | **0.04 ms** | **243** | **1903** |
| 24 before | 11,671,250 | 1663 MB | 4.89 ms | 2924 | 205 |
| **24 after** | **1,790,004** | **696 MB** | **0.53 ms** | **813** | **1460** |
| **32 after** | 3,152,346 | 1061 MB | 0.57 ms | 1416 | 1188 |

The default render distance went from **5 to 12**, and 32 (1024 blocks) is now usable. Against the M12 baseline of distance 5, that is more than six times further at better frame times.

Culling was verified by the fraction of geometry drawn: 29.4% of triangles, against the ~28% a 100° horizontal field of view should cover. A culling bug shows up as geometry vanishing at screen edges, which that ratio would expose.

**Render distance is adjustable while playing** (`F6`/`F7`), unlike the worker count. Growing streams chunks in with no frame-rate dip; shrinking immediately drops meshes outside the new radius rather than waiting for the chunks to unload, and mesh and triangle counts return exactly to their previous values.

**Cost:** greedy meshing roughly doubles per-chunk meshing CPU, which is now the dominant startup cost (3.0 s at distance 32).

**Deliberately not done:** level-of-detail and indirect drawing. GPU time is 0.57 ms against an 8.3 ms budget at distance 32, so neither has anything to fix yet. Revisit when something actually hurts.

---

# Phase 4 — A World Worth Exploring

*Goal: terrain stops being a heightmap and becomes a place.*

### ✅ M14a — Light propagation · **Core**

Sky light and block light stored per block and flood-filled across chunk boundaries, with incremental updates on block changes.

**Done when:** open ground is fully lit, anything sealed off is dark, and a light source brightens its surroundings.

**Result:** light is stored per block, one byte — sky in the high nibble, block in the low one — which doubles chunk memory to 64 KB. It has to be stored rather than derived, because light crosses chunk boundaries and so cannot be computed from one chunk's contents.

Propagation runs on the **main thread**, budgeted, for the same reason: it walks freely between chunks and so cannot be handed the self-contained snapshot that generation and meshing get. Adding light is a flood fill; removing it walks back everything the dead source lit, then re-fills from whatever still reaches.

`BlockId::Glowstone` (emission 14) exists so block light is testable at all.

Verified numerically at the spawn column: sky 15 above the surface, 0 at it and below.

**No vertex format change**, contrary to the original plan. Light is folded into the existing per-vertex colour as a brightness multiplier, which also covers M14b.

### ✅ M14b — Smooth lighting and ambient occlusion · **Core**

Per-corner light averaging and per-vertex ambient occlusion.

**Done when:** surfaces shade gradually rather than per-face, and corners darken where geometry crowds them.

**Result:** each face corner averages the light of the open cells touching it, and darkens by how boxed-in it is. Quads split along their darker diagonal, without which occlusion on opposite corners creases flat ground the wrong way.

`ChunkBorder`/`ChunkNeighbours` were replaced by **`ChunkVolume`** — the chunk plus one cell of padding, 34³ in blocks and light. Ambient occlusion samples diagonally, so a face on a chunk edge needs cells from up to three neighbouring chunks at once, which six face borders cannot supply. It also turned every neighbour lookup in the mesher into a plain array index.

| | Triangles | Startup | GPU | FPS |
|---|---|---|---|---|
| M13b (unlit) | 486,420 | 719 ms | 0.04 ms | 1903 |
| M14a (flat light) | 486,534 | 1412 ms | 0.32 ms | 120 (capped) |
| **M14b (smooth + AO)** | **1,152,956** | **1292 ms** | **0.20 ms** | **121 (capped)** |

**Greedy meshing gave ground, as expected.** A merged quad interpolates its corners across the whole span, which only matches the faces it replaces when every one of them was evenly lit — so merging is now restricted to uniform faces. Geometry rose 2.37×. Flat open terrain still merges; anything near an edge no longer does. GPU time is unaffected at 0.20 ms against an 8.3 ms budget.

### ✅ M14c — Placeholder sun and day cycle · **Inserted**

A visible sun that rises in the east and sets in the west, a directional light term, and a sky colour that follows it.

**Why inserted here:** M14's lighting model is Minecraft's, and that model has **no direction** — sky light only answers "can this cell see the sky?". The result read as flat, correctly. Real sun and cast shadows are M24 and depend on the renderer restructure at M23, which is a long way off; this is the cheap placeholder that stops the world looking wrong in the meantime.

**Result:** the sun is a billboarded quad drawn with the existing pipeline, using a texture layer in the block array so no new binding was needed. Surfaces take a Lambert term against the sun direction.

**No vertex format change.** The fragment shader recovers a face normal from how world position changes across the triangle (`dFdx`/`dFdy`), which is exact for flat faces and keeps normals out of the vertex format entirely. Sun direction and lighting parameters ride in the push constants, with a per-draw flag so HUD and sky geometry stay unlit.

`day_length_seconds` in `settings.cfg` controls the cycle; short values are useful for watching it.

**Explicitly not done:** cast shadows. Nothing occludes the sun — a wall is lit by its facing, not by whether something stands between it and the sun. That needs shadow maps and stays at M24.

### ✅ M15a — Caves and 3D terrain · **Core**

3D noise for cave systems, and the surface block decision moved out of a hardcoded height comparison.

**Done when:** the underground is worth exploring and is genuinely dark, with the surface still intact.

**Result:** caves are carved where a 3D noise field passes close to a chosen value, which gives connected winding systems. Thresholding the field directly would give disconnected blobs that read as holes rather than caves. Carving fades in with depth below the surface and never touches the bottom of the world, so the ground is not left rotten and there is always something to stand on.

Release build, render distance 12:

| | Triangles | RAM | Startup | GPU |
|---|---|---|---|---|
| M14 (no caves) | 1,152,956 | ~400 MB | 1.13 s | 0.19 ms |
| **M15a** | **2,695,614** | **629 MB** | **1.72 s** | **1.18 ms** |

About 20% of the underground is hollow. Still 120 fps.

**Cave cost tracks surface area, not volume.** Halving the tunnel width cut hollow volume from 21% to 12% and reduced geometry by only 3%, because narrow tunnels have more surface per unit volume than open caverns. Lowering the *frequency* instead — fewer, larger caves — cut geometry 25% while increasing hollow volume.

**Groundwork for M15b:** the surface block decision now lives in a `SurfaceRule` (top block, filler, depth) chosen by a single function, replacing `surface <= kSeaLevel ? Sand : Grass` inline in the fill loop. There is still only one rule, picked by height; biome selection replaces that chooser without touching the fill loop.

### ✅ M15b — Biome system · **Core**

The machinery that decides which blocks belong where: rule tables, low-frequency selection noise so regions are large and coherent, and blending so neighbours do not meet at a hard line.

**Why it matters beyond flavour:** a biome is the data structure that answers "which block goes here and why". Without it every new block type means another hardcoded threshold, and they start fighting each other.

**Done when:** regions are large, coherent and reproducible from the seed, transitions are not visible as lines, and adding a block type means adding a row rather than a branch.

**Result:** five biomes — Plains, Desert, Rocky, Mountains, Snowy Peaks — each a row in one table carrying its surface block, filler, filler depth, terrain base height, amplitude and snow line.

Selection uses **two independent low-frequency noise fields**, temperature and humidity. One field could only order biomes along a line, which is why a single "climate" value cannot separate desert from plains from tundra convincingly. Each biome sits at a point in that 2D space and claims territory by proximity.

**Blending falls out of the same weights.** Terrain height is a weighted average across every biome in range, so regions slope into each other instead of meeting at a cliff. Surface *blocks* come from the single strongest biome, because a blend of two block types is not a thing — and the boundary still reads naturally because the selection noise makes it a wandering contour rather than a straight edge.

Release build, render distance 12: **2,246,526 triangles, 575 MB, 1.27 s startup, 1.47 ms GPU, 119 fps.** Geometry actually fell from M15a's 2.70M because plains and desert are flatter than the old uniform terrain.

**The hardcoded `surface <= kSeaLevel ? Sand : Grass` is gone**, and with it the dead-flat contour line that ran across the entire world. Six of the natural block types now occur on their own — grass, dirt, sand, stone, gravel and snow. Cobblestone, planks and bricks remain manufactured-only, which is correct.

The current biome shows in the `F5` overlay.

**Placeholders, not the roster.** These five exist to prove the machinery and give the existing blocks a home. What the finished game's regions actually are is Phase 5.

### ✅ M15c — Oceans, rivers and flowing water · **Core**

Sea level, ocean basins, shorelines, and water that actually behaves like water.

**Done when:** terrain below sea level fills, water flows and recedes when disturbed, and the player can swim rather than drown in a pit.

**Result:** an Ocean biome sits well below sea level, a Beach biome rings it, and anything still empty under the waterline fills with water — including caves that breach it, which flood for free.

**Water carries its depth in the block id.** Levels run 0 (a source that never drains) to 7 (the thinnest film), which costs no per-block metadata array and works with the existing save format unchanged. Thinner flows render with a lower surface, so a stream visibly tapers.

**Flow is incremental and event-driven.** Generated oceans are already settled, so nothing runs until something disturbs them; editing a block queues that cell and its neighbours. A cell takes the **strongest supply reaching it**, which is what makes competing flows resolve rather than fight. Falling beats spreading — water only runs sideways once it has nowhere to drop. **Two sources meeting over solid ground create a source**, so water is renewable.

**Transparency needed a second render pass.** Blending depends on draw order, so water cannot sit in the same buffer as the terrain behind it. Each chunk now owns two meshes and the renderer draws every opaque one before any translucent one.

**Swimming:** buoyancy nearly cancels gravity, holding jump climbs, and letting go drifts down. Water is survivable rather than a hole you fall into.

Release build, render distance 12: **2,742,884 triangles, 1.15 ms GPU, 121 fps.**

**Still not a water *system*.** No reflection, refraction, depth absorption, waves, foam or shoreline effects — that is M26, and it is a headline visual feature rather than a block type.

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
| M3b | GLM — **added** | Vectors, matrices, projection |
| M4+ | Vulkan Memory Allocator — *not yet needed* | Practical GPU memory management. One `vkAllocateMemory` per buffer is still well inside the driver's allocation limit; revisit if that limit is ever approached. |
| M5 | A noise library — **not taken** | Value-noise fBm was ~60 lines written in-house. A dependency was not justifiable for that. |
| M9 | stb_image — **added** | Decoding PNG block textures |
| M11 | Dear ImGui | Debug overlay and tools |
| M13b | meshoptimizer | Mesh optimization, LOD |
| M22 | An audio library | Sound |

If the dependency count approaches ~5, revisit the vcpkg-versus-FetchContent decision recorded in `CLAUDE.md`.

---

## Open Decisions

Things deliberately not decided yet. Do not silently resolve these — raise them.

- **The project name.** `VoxelGame` is a placeholder. Needed before M35, harmless until then.
- **Creative direction.** Required before Phase 5 begins. See that phase's note.
- **Art direction.** Whether blocks are stylized, realistic, or something else drives M9, M17, and all of Phase 6.
- **The biome roster.** M15b built the machinery and shipped seven placeholders. Which regions the finished game actually has is Phase 5 work.
- **Rivers.** M15c delivered oceans, shorelines and inland water where terrain dips below sea level. Winding rivers cutting through highlands need a separate carving pass and were not done.

**Resolved:** chunk dimensions are 32³, settled at M4 and confirmed by M8's streaming behaviour. Worker count is a restart-only setting, settled at M12. Render distance is adjustable while playing, settled at M13b.

---

## Update Discipline

Update the status marker when a milestone completes, and add its acceptance evidence to `SYSTEM_MEMORY.md`'s validation baseline.

**Reordering milestones is allowed** — but check the change against the five sequencing rules above, and record the reasoning in `CLAUDE.md` if the reordering was contested. Do not quietly renumber.

Do not add speculative milestones. This file describes a route, not a wish list.
