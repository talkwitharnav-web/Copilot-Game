# TIMELINE.md

The route from "a window that clears to blue" to the finished game.

**This is a sequencing document, not a promise.** It exists so that any future session — human or agent — can answer three questions without guessing: *what are we building toward*, *what is the very next thing*, and *why is it next and not something else*.

Read [`SYSTEM_MEMORY.md`](SYSTEM_MEMORY.md) for what currently exists and [`CLAUDE.md`](CLAUDE.md) for why settled decisions were made.

---

## The End Product

A **voxel sandbox game** with the core freedoms of the genre — exploration, building, destruction, gathering, crafting, survival, discovery, progression — running on a **custom C++20/Vulkan engine** built from scratch, targeting Windows.

**The graphics ambition is real and confirmed** (user, 2026-07-31) — ray tracing, volumetric clouds, the modern hybrid renderer, all of it. But it arrives as **settings, not as a hardware requirement**. A `Video` tab lets the player turn clouds off, ray tracing off, resolution down, frame cap up, render distance in or out.

That is a design constraint on every visual feature from M23 onward, not a footnote: **anything expensive must be switchable at runtime.** A feature baked unconditionally into pipeline state cannot be turned off, and the machine this is developed on is a laptop whose owner sometimes wants to play on one core while doing something else.

**This is openly a game in Minecraft's tradition, and that is the point.** User position, 2026-07-31: _"this game is impossible to make without referencing to the original one, and truthfully, i don't want anything different. the only difference i wanted was control, which i now have cuz i'm developing it."_ Reinventing the genre from first principles was never the goal and is not a requirement. Following Minecraft's design closely is a legitimate, deliberate choice.

### The one boundary that is real

**Mechanics are free. Assets are not.**

Game rules, systems and mechanics — crafting grids, tool tiers, hunger, mob behaviour, redstone-like logic — are functional designs, and reimplementing them is both legal and normal. A great many voxel games do exactly this.

What cannot be copied is the **expressive** work: textures, models, sounds, music, code, and trademarked names. Those must be ours. This is not a purity test, it is the line between a game that can be shared and one that cannot.

| | Copy freely | Must be original |
|---|---|---|
| Mechanics, rules, systems | ✅ | |
| Recipe structure, progression shape | ✅ | |
| Textures, models, sounds, music | | ✅ |
| Names, lore, characters | | ✅ |

Where something feels genuinely better done differently, do it differently — because it is better, not because it must differ.

The bar on *engineering* is still deliberately higher than "Minecraft clone with shaders." The renderer should be a genuinely modern hybrid renderer — rasterization, compute, and ray tracing each doing what they are best at — not a retrofit of pretty effects onto a naive engine. **That** is where this project earns its keep.

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

### ✅ M16 — Procedural structures · **Core**

A structure placement system — deterministic, seed-driven, biome-aware — plus the first structures.

**Done when:** structures generate reliably without corrupting terrain or chunk borders.

**Result:** forests. Trees stand in plains, sparsely on rocky ground, and never in desert, ocean, beach or above the snow line.

**The hard part is not the tree, it is the chunk border.** Generation must stay a pure function of `(seed, chunkCoord)` — that rule is what makes threading safe — so a chunk may not push blocks into its neighbour, and may not ask a neighbour what it decided. Instead **every chunk rebuilds each structure that could reach it and keeps only the blocks landing inside its own bounds.** Two chunks generating the same tree independently reach the same answer, so it comes out whole. Verified: 2,829,100 triangles at **0, 4 and 11 workers** — bit-identical regardless of scheduling.

**Candidates live on an 8-block grid**, one structure per cell, jittered within it and held two blocks off the cell edge so neighbouring trees cannot touch. A cheap integer hash decides presence *before* any noise runs — sampling biome and height first meant three noise evaluations per candidate to answer a question already settled, and skipping that cut generation from 1912 ms to **1275 ms**.

**Biome density is a table column, not a branch.** `Biome::treeDensity` means adding a leafier region is a row edit.

Release build, render distance 12: **2,829,100 triangles, 1.06 ms GPU, 121 fps.** A whole forest cost 3.1% more geometry.

**Leaves are opaque.** They read correctly at distance, but the dark pixels standing in for gaps are a placeholder until M17 brings alpha-tested rendering. Trees are also the only structure so far — villages, dungeons and ruins are Phase 5 content.

### ✅ M17a — Cutout transparency · **Core**

Alpha-tested rendering, so a texture can have genuine holes in it.

**Done when:** leaves have real gaps you can see through, and a canopy shades the ground beneath it.

**Why it is separate:** *cutout* is not blending. The fragment shader throws a pixel away entirely with `discard`, so what survives still writes depth and needs no sorting — which means cutout geometry rides in the ordinary opaque pass. Water's blending, by contrast, forced a whole second pass at M15c. Getting that distinction wrong is the expensive mistake here, so it ships on its own.

**Result:** leaves are see-through and layered — **3,024,166 triangles, 1.45 ms GPU, 121 fps**, which is +6.9% geometry over M16.

**A cutout pixel must be written fully opaque.** The first attempt kept the texture's alpha, and every distant tree turned pale grey — mip levels average alpha, so distant foliage arrived at ~0.6, passed the `< 0.5` test, and then blended with the sky behind it. Sampled pixels sat exactly halfway between leaf green and sky blue, which is what identified it. World transparency now comes from the vertex alpha, which is where water already kept its.

**Leaves keep the faces they share with other leaves**, unlike every other block. Culling those is the cheap "fast foliage" style and it makes a canopy a hollow shell, where every hole shows sky instead of more leaves. They are also **double-sided**, so looking through a hole in the near side of a block shows the inside of its far side rather than straight through it. Each shared boundary is emitted once, by the positive-facing neighbour, or the two would be coplanar quads fighting over the same depth — that dedup alone saved 4%.

**Canopies shade the ground** via a new distinction: sky light falls at full strength only through *sky-transparent* blocks. Leaves are light-transparent but not sky-transparent, so they break the free fall and everything below dims one level per block. No per-block attenuation value was needed.

**Not yet:** mip levels still erode alpha coverage, so canopies thin slightly with distance. The standard fix is rescaling alpha per mip to preserve coverage, and it belongs with the renderer work at M23 rather than here.

### ✅ M17b — Block shapes · **Core**

Blocks that are not full cubes: cross-shaped plants, slabs, stairs.

**Done when:** a slab is half-height to stand on and to look at, and plants render as crossed quads visible from both sides.

**Result:** tall grass scatters across grassland, and stone slabs are placeable and standable. **3,146,576 triangles, 1.35 ms GPU, 120 fps.**

**One shape, two consumers.** `BlockShape` and `shapeHeight` are read by both the mesher and the collision code, so geometry and the box you bump into cannot drift apart.

**Non-cubes are meshed in a second pass, not through the greedy mask.** Teaching the mask about partial faces would slow the path that carries the entire world for the sake of a few decorative blocks. Slabs reuse the same unit-cube corner tables scaled in Y, which keeps the winding the face tables established rather than restating it.

**Landing is the only place a block boundary is the wrong answer.** A slab's sides and underside sit on integer planes; only its top is halfway up the cell. Resolving downward movement to the boundary dropped the player onto thin air above a slab, the ground probe found nothing, and they fell again — an endless bounce. Vertical descent now resolves against the real surface height.

**Stairs are deferred to M17c**, because they need per-block *orientation* and the block id carries no such state. Water encodes its level in the id; stairs would need eight variants per material, which is a data-model question rather than a geometry one.

### ✅ M17c — Oriented shapes · **Core**

Stairs, and the block-state decision they force.

**Done when:** stairs can be placed facing any direction and either way up, and you can walk up them.

**Result:** cobblestone stairs, eight orientations, oriented from the camera when placed and flipped when placed against the underside of a block.

**State lives in contiguous block ids**, exactly as water's level already does — two bits of facing and one of half. The alternative, a 4-bit metadata array per chunk, was rejected because **memory is this project's measured binding constraint** (1.66 GB at render distance 24, while frame rate never was) and a nibble array costs about +25% of chunk storage to buy headroom that 233 unused ids already provide. Revisit when ids genuinely run short, or when a block needs state that is *not* part of its identity — that is a block entity, a different problem.

**One table, three consumers.** `collisionBoxes` is read by meshing, by physics and by the targeting raycast, so what you see, what you bump into and what you aim at cannot disagree. Slabs were rewritten onto it and produced a bit-identical 3,146,574 triangles, which is the check that the rewrite changed nothing.

A face buried inside another box of the same block is skipped, or a stair's step and the half it stands on leave coplanar quads fighting over one depth value.

**Slabs gained a top half and a merge rule**, because stacking them otherwise gives slab, gap, slab — the second lands in the next cell's lower half. Two halves meeting in one cell are placed as a whole block instead.

**Selection and collision had to split.** A plant is walked through and still has to be breakable, so it collides with nothing and selects as a slim column. That is the only block where the two differ, and it is the one place the "single source of truth" rule bends.

> **The cost of widening the cube assumption was five bugs, every one found by playing.** Collision resolution snapped to the block boundary; the fluid update deleted plants; the outline drew a full cage; the raycast treated cell entry as a hit; and the outline flashed on the frame a block was broken. All five were the same fault — a shape derived somewhere other than the shape table — and none of them was anything the compiler could catch.

### ✅ M17d — Connected shapes · **Optional**

Fences, walls and panes, whose geometry depends on their neighbours rather than on stored state.

**Done when:** a line of fence posts joins into a rail and blocks the player.

**Result:** plank fences that grow arms toward any solid block or other fence.

**Connections are derived at mesh time, not stored**, which is why this needed no block state at all and could be separated from M17c.

**Collision assumes every arm**, because `collisionBoxes` is given only an id and cannot see neighbours. A post on its own leaves a 0.75-wide gap between two fences, which the 0.6-wide player walks straight through — so the barrier has to be there even when the arm is not drawn. That is the one place the shape table and the drawn geometry deliberately disagree.

---

# Phase 5 — It Is A Game

*Goal: the systems that turn a world into something you play rather than something you look at.*

> **Reference the genre freely.** "How does Minecraft do it?" is a perfectly good question here, for design as well as engineering — it is a well-tested set of answers and there is no prize for ignoring them. Ask the user where they want something to differ; do not invent divergence for its own sake.
>
> **Assets are the exception.** Textures, models, sounds, music and names have to be ours. That is a legal line, not a creative one — see "The End Product".

### ✅ M18a — Items and drops · **Core**

Blocks stop being infinite: breaking one yields a thing you have to pick up.

**Done when:** breaking a block drops an item, walking over it collects it, and placing consumes one from the stack.

**Result:** a nine-slot inventory with stack counts drawn on the hotbar, dropped items that fall and are drawn to the player, and placement that runs out.

**`ItemId` is deliberately not `BlockId`.** Every placeable block has an item form; the reverse will not hold, because a pickaxe is an item that is never a block. Block items *share* the block numbering so there is one list to maintain rather than two that drift, and anything that is not a block starts above `kFirstToolItem`.

**Drops are the smallest possible entity**, not the start of an entity system. They have a position, a velocity and a stack, and resolve only against the ground. The general system is M20, and building it now would be guessing at what creatures need before any exist.

**What a block drops is a table, not an identity.** Stone yields cobblestone, grass yields dirt, and the eight stair orientations all collapse to one item so an inventory does not fill with rotations of the same thing.

**Creative mode survives**, as `creative_mode` in `settings.cfg`. Infinite blocks are genuinely useful for testing everything that is not the inventory, and losing them would make every later milestone harder to work on.

### ✅ M18b — Inventory screen · **Core**

The full inventory UI, against the concept art in `reference/`: armour slots, offhand, 2×2 crafting, and the wider grid.

**Done when:** `E` opens a screen showing the whole inventory, and stacks can be moved between slots.

**Result:** the artist's panel drawn as a single sprite, with 36 slots wired up — 27 storage plus the hotbar — and click-to-swap between them. `Q` throws, and clicking outside the panel drops one from the cursor.

**The panel is the artwork, not a rebuild of it.** It is one textured quad from a sheet, with item icons composited on top. Redrawing her frames and bevels out of primitives would have been hand-copying something that already exists as a picture, and it would drift the moment she revised it.

**Slot positions are measured off the art in its own pixels** and scaled through a single constant, so the layout cannot disagree with the image it came from.

**No UI library.** ImGui was deferred at M11 and is still deferred: this screen needs click-to-swap and nothing else, and hand-rolling that was smaller than the dependency. Revisit when something wants text entry or dragging.

**Not wired:** armour, offhand and crafting slots are drawn and hit-tested but do nothing — crafting is M19 and equipment needs armour to exist. The character panel is empty until there is a player model at M20.

### ✅ M19a — Texture fidelity · **Core**

Every block texture measured against the reference dump and regenerated to match.

**Done when:** each texture's measured statistics land near the reference and the world looks better, not merely more similar.

**Measurement replaced opinion, up to a point.** `tools/compare-texture.ps1` reports palette size, luminance spread, saturation and run lengths side by side, and it caught things the eye had not: our snow spanned 25 luminance levels against the reference's 5, cobblestone was half as noisy as it should be, gravel was three times too saturated, and planks ran flat colour the full 16-pixel width where the reference's longest run is 8.

**Biome-tinted foliage was built and reverted.** Greyscale grass and leaves multiplied by a per-column climate colour — every statistic agreed with the reference, and the result was washed-out sage-grey plants. A mid-grey times a mid-green is duller than either. Green is baked into the textures instead. See `CLAUDE.md` for why the number moving was not enough.

**What survived:** thirteen retuned textures. **What did not:** the vertex tint attribute, per-chunk climate storage, and the tint comparison in the greedy merge — all removed rather than left dead.

### ✅ M19b — Geometry fidelity · **Core**

Every non-cube shape checked against the reference model JSON, which records exact extents in sixteenths of a block.

**Done when:** slabs, stairs, fences and the plant cross all match the reference's dimensions, or differ deliberately with the reason written down.

**Already exact:** slabs (`0–8` and `8–16`), both stair boxes (`[0,0,0]→[16,8,16]` plus `[8,8,0]→[16,16,16]`), and the fence post (`[6,0,6]→[10,16,10]`). All four had been arrived at by guessing and all four were right, which is worth knowing — the guesses were not lucky so much as constrained, since a half block has few plausible sizes.

**Two real errors found.**

Fences were *drawn* with solid full-height arms, so a fence line read as a thin wall rather than as a rail you can see through — and the code carried a comment claiming it drew two rails, which it had never done. Rendering now uses `fenceRailBoxes` (rails at `6–9` and `12–15`, matching the reference) while collision keeps `fenceBoxes` and its full-height arms. **This is the second place where drawn geometry and collision deliberately disagree**, and like the first it is written down at the function rather than left to be rediscovered.

Plants were held 0.15 off each cell wall where the reference spans `0.8–15.2`, very nearly corner to corner, so every plant rendered a size too small. Their selection box also now matches the reference hitbox — slimmer and shorter than the blades, so aiming at a plant is not the same as aiming at its whole cell.

**Dimensions are not colours.** M19a's lesson was that a matching statistic does not make something look right. A matching *hitbox* genuinely is right, because it is a fact about geometry rather than a judgement about appearance — which is why this milestone could follow the reference exactly where the last one could not.

### 🔶 M19c — Crafting and resource progression · **Core**

A crafting and progression system: a grid, material tiers, tools gating access to better materials.

Recipes are settled facts — see `CRAFTABLE.md`, verified against the reference recipe JSON. What still needs deciding is **scope**: which recipes ship, and in what order.

**Landed:**

- The recipe matcher (`item/Recipe.hpp`), shaped and shapeless in one struct.
- The 2×2 grid wired into the inventory screen, with slot positions measured off the artwork.
- Two recipes: `Log → 4 Planks` (shapeless) and `2 Planks → 4 Sticks` (shaped).
- **Stick**, the first non-block item. Its sprite is a layer in the block texture array rather than a second binding, because that array is really "every 16×16 sprite we own".
- Full slot interaction (`item/SlotOps.hpp`): left click takes or merges a whole stack, right click takes half and places one, and a click-drag distributes a stack across every slot it crosses — evenly with the left button, one at a time with the right.

**Patterns are stored at their own size, not padded.** The matcher finds the bounding box of what is in the grid and compares against that, so a 1×2 recipe works in either column of a 2×2 and will work anywhere in a 3×3 without the matcher changing. `craftResult` takes the grid size as an argument for the same reason.

**The result is a preview until it is taken**, which is why `craftResult` and `consumeIngredients` are separate calls.

**Shapeless matching counts leftovers**, or a grid holding an extra item would still craft and quietly eat it.

**Dragging applies live and is replayed from scratch each frame**, not committed on release. See `CLAUDE.md`: a UI action that commits only on release looks broken while it is working.

**Four bugs fell out of playtesting**, all recorded in `CLAUDE.md`: horizontal collision resolution ignoring the shape table (stairs threw the player backwards), creative mode skipping item pickup (drops bounced off the player forever), drag committing on release only, and a drag armed by the click that filled the cursor.

**Still to do — this is where a fresh session should pick up:**

1. **The crafting table.** A new block with three face textures (reference: `crafting_table_{top,side,front}`), right-click to open, and a 3×3 layout. The matcher needs no changes; the screen does. `4 Planks → 1 Crafting Table` is already in `CRAFTABLE.md`.
2. **Shift-click quick-move** between hotbar and storage, and double-click to gather a stack.
3. **The rest of Tier 1** from `CRAFTABLE.md`: furnace and smelting, then torches.
4. **Tools and mining tiers**, which is the part that turns crafting into progression.

### ⬜ M20 — Entities and creatures · **Core**

Entity system, animation, pathfinding, spawning, and creature behaviours. Behaviours can follow the genre; the **models, textures and names must be ours**.

### ⬜ M21 — Survival systems · **Core**

Health, damage, hazards, and resource pressure.

### ⬜ M22 — Audio · **Core**

First audio dependency. Positional sound, ambience, music. Deliberately late — it is genuinely independent of everything above.

---

# Phase 6 — It Is Beautiful

*Goal: the modern high-end renderer. Ordered so that structural changes land before the effects that depend on them.*

> **Every effect in this phase and the next needs an off switch.** The target is a game that scales from "one core while multitasking" to "everything on", which means quality levels are part of each feature's definition of done, not a pass at M34. See "The End Product".

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

A real settings screen. **This is where the whole graphics-as-settings promise is delivered**, so it is not cosmetic: a `Video` tab exposing quality levels, clouds, ray tracing, resolution, render distance and frame cap (retiring the temporary `F1`–`F7` bindings), plus key rebinding, audio and accessibility options.

The backing file already exists — `settings.cfg` has carried worker count, render distance, frame cap and day length since M12–M14c. This milestone builds the screen, not the system.

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
| **Assets** | Every texture, model, sound and name that ships must be ours. Third-party material may be used as reference and lives in `reference/`, never in `assets/`. Mechanics carry no such restriction. |
| **Scalability** | Any feature with a real cost ships with a way to turn it down or off. The game must stay playable on minimal settings, not merely on the development machine. |
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
- **Creative direction.** Not a blocker. The game follows the genre by default; ask the user where they want it to differ rather than assuming it must.
- **Art direction.** Whether blocks are stylized, realistic, or something else drives M9, M17, and all of Phase 6. Assets must be original regardless of which way this goes.
- **Minimum hardware.** The game must scale down, but how far has not been decided. Settle it when there is something worth benchmarking on a weaker machine.
- **The biome roster.** M15b built the machinery and shipped seven placeholders. Which regions the finished game actually has is a conversation with the user, not a blocker.
- **Rivers.** M15c delivered oceans, shorelines and inland water where terrain dips below sea level. Winding rivers cutting through highlands need a separate carving pass and were not done.

**Resolved:** chunk dimensions are 32³, settled at M4 and confirmed by M8's streaming behaviour. Worker count is a restart-only setting, settled at M12. Render distance is adjustable while playing, settled at M13b.

---

## Update Discipline

Update the status marker when a milestone completes, and add its acceptance evidence to `SYSTEM_MEMORY.md`'s validation baseline.

**Reordering milestones is allowed** — but check the change against the five sequencing rules above, and record the reasoning in `CLAUDE.md` if the reordering was contested. Do not quietly renumber.

Do not add speculative milestones. This file describes a route, not a wish list.
