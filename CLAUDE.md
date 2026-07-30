# CLAUDE.md

Narrative decisions, rejected approaches, debugging lessons, and machine-specific guardrails for future coding sessions on this voxel sandbox project.

Read `SYSTEM_MEMORY.md` first for the current project structure, build commands, and technical truth. **This file explains _why_ settled choices exist** so a future session does not re-litigate them or repeat a mistake that has already been paid for once. `TIMELINE.md` holds the milestone route and what the finished game is meant to be.

---

## Critical — Read First

- **Never run `git commit`, `git push`, or any history-rewriting command unless explicitly asked.** Direct user correction, 2026-07-30: _"undo those commits. who told you to commit. keep changes that's fine, no committing."_ Three commits had been created unprompted while wrapping up Milestone 1, on the reasoning that the user had asked for "Git-friendly incremental changes" — that phrase describes how changes should be *shaped* (small, reviewable, not giant rewrites), **not** permission to run Git for them. Write files, leave them uncommitted, and let the user decide when and what to commit. `git init` and `.gitignore` were fine; the commits were not.
- **Ship something playable at every milestone, and treat the user as the primary QA resource.** Direct user instruction, 2026-07-30: _"the best thing for the game is to get playability on early so that i can 'play' the game and report findings back to you. i am the most valuable bug finding resource you can have."_ `TIMELINE.md` was reordered because of this — first playability moved from M9 to M7, textures and chunk streaming were pushed *after* walking and block-breaking. A milestone that produces nothing runnable is too big; split it. When the user reports a bug, **default to "this is real and I haven't found it yet,"** not to explaining why the code looks fine.
- **The long, highly detailed vision prompts were written by another AI, not by the user.** Disclosed 2026-07-30: _"the bigger and more detailed prompts i gave weren't me, they were another ai helping me get my stuff sorted, so take what it says with a grain of salt."_ Those feature lists are a **statement of ambition and a menu of options, not a specification**. Do not treat any bullet in them as a commitment, do not implement something merely because it is listed, and where they conflict with the user's own plain-language wishes, **the user wins**. Their short, direct messages are the authoritative signal.
- **Stop at milestone boundaries.** The user explicitly asked (2026-07-30) that work stop at the end of each milestone and wait for instruction. Do not roll straight from "the window renders" into "now let's add chunks." Finishing early and asking is correct behavior here, not laziness.
- **The user is an experienced vibe coder but NOT an experienced C++/graphics-engine programmer.** They understand concepts when explained simply. Do not assume familiarity with low-level graphics or systems terminology.
- **Use the real term, then immediately explain it in plain English.** Direct user instruction, 2026-07-30: _"remember i'm just a vibe coder i don't understand all this complex terminology so use the terminology, then explain it in simple human terms so i learn over time."_ This is a learning-over-time request, not a request to dumb things down — do not silently substitute vague language for the correct word, and do not drop the plain-English half either.
- **Do not build ahead of the current milestone.** No voxel terrain, physics, ray tracing, procedural generation, PBR, weather, or job systems until explicitly asked. The long-term ambition is real, but building toward it speculatively is the fastest way to a codebase nobody can finish. `TIMELINE.md` records the intended order and why.
- **Multiplayer is permanently cut, not deferred.** User decision, 2026-07-30: _"no multiplayer btw."_ This is a **design freedom, not a limitation** — treat single-player as a licence to avoid client/server splits, server-authoritative simulation, replication hooks, and determinism constraints that exist only to serve netcode. Do not add abstractions "in case multiplayer happens later." An earlier draft of the vision listed multiplayer as long-term; that was explicitly overridden.
- **This machine's Windows account is a standard user, not an administrator.** See "The `.141`-Equivalent Machine Facts" below. Never ask the user to type a password or PIN into anything the model can read.
- **Windows PowerShell 5.1 only. Never use `&&`** — chain with `;`. **Never send multi-line scripts to the terminal** (see Lessons).
- **This is not being built inside Unity, Unreal, or Godot, and that is not up for casual reconsideration.** The whole point is a custom engine. If a task feels like it "would be easier in Unreal," that is expected and not a signal to switch.
- **Do not add a dependency unless the current milestone actually needs it.** Every dependency must be justifiable in one sentence at the moment it is added, not "we'll want it eventually."
- **Do not multithread yet.** But do not paint the code into a corner where multithreading later requires a rewrite — see "Architecture Rules."

---

## The User and Working Style

- Comfortable directing software and reading code; not comfortable with C++ memory model, GPU pipelines, or build-system internals. Do the low-level reasoning and stack-trace interpretation.
- Thinks in terms of outcomes and feel ("large procedural worlds," "high-quality graphics"), not APIs. Translate that into technical plans rather than asking them to specify the technical plan.
- Wants to **understand** what is happening, not just receive working code. Before introducing a new library, tool, architectural concept, or unfamiliar term: say what it is, why we need it _now_, and what happens without it. Keep it to a few sentences — the instruction was explicitly "do not drown me in theory."
- Prefers that missing tools be **installed**, not merely reported as missing. Only escalate to "you need to do this manually" when there is a genuine hard blocker (admin rights, a license click, a hardware limitation), and even then, automate everything on either side of the blocker.
- Wants verification, not assumption. "It should work now" is not acceptable — build it and run it.
- **Is the project's playtester, and a good one.** Give them builds early and often, describe what to try, and ask what felt wrong. Do not save up several milestones' work for one big reveal.
- Expects Git-friendly incremental change. No giant speculative rewrites.

---

## The Machine (2026-07-30)

Facts about this specific development machine that have already cost time to discover:

- **The Windows user account `arnav` is a standard (non-administrator) user.** The local Administrators group contains `Administrator`, `haris`, and `sneha`. The user knows the `haris` PIN.
- **Consequence:** anything that installs into `C:\Program Files` or writes machine-wide registry keys cannot be installed silently by the agent. The working pattern that succeeded: the agent downloads the official installer, then launches it with `Start-Process -Verb RunAs`, and **the user completes Windows' own credential prompt themselves.** The PIN is never typed into, read by, or routed through the model. Reuse this pattern; do not invent alternatives.
- **User-scope installs are strongly preferred where they exist.** CMake and Ninja were installed as plain extracted archives under `%LOCALAPPDATA%\Programs\` with the user `Path` environment variable updated — no admin needed, trivially upgradable, and nothing to uninstall. Prefer this for any future standalone tool.
- **Hardware is not the bottleneck.** Intel Core Ultra 7 155H (16 cores / 22 threads), NVIDIA RTX 4070 Laptop GPU plus an Intel Arc integrated GPU, Windows 11. The RTX 4070 supports hardware ray tracing, so the long-term ray-tracing ambition is realistic on this machine — but see "Two GPUs" in `SYSTEM_MEMORY.md`, because *which* GPU Vulkan picks is a real decision, not an accident.
- **The Vulkan runtime was already present before any SDK was installed** (`C:\Windows\System32\vulkan-1.dll`, shipped by the GPU drivers). Do not confuse "Vulkan runs on this machine" with "the Vulkan SDK is installed" — they are separate things and only the second one gives us validation layers and a shader compiler.

---

## Settled Technology Decisions (Not to Re-Litigate Casually)

Each of these was a real fork. The mechanics live in `SYSTEM_MEMORY.md`; this is the reasoning.

### C++20 from day one, no prototype-in-another-language phase

Explicit user decision. Prototyping in C#/Rust/Python and porting later means writing the engine twice and learning the wrong performance intuitions in between. C++20 specifically (not 17) for `concepts`, `<span>`, designated initializers, and `constexpr` improvements that make Vulkan struct setup dramatically less painful. C++23 was not chosen because MSVC's support is still uneven and nothing in the near-term plan needs it.

### Vulkan, not DirectX 12 or OpenGL

- **OpenGL** is easier but is effectively frozen, has no real path to modern ray tracing, and gives poor control over CPU/GPU parallelism — which is a core long-term goal here.
- **DirectX 12** is a genuinely good Windows choice and comparable in difficulty, but locks the project to Windows/Xbox forever.
- **Vulkan** costs more upfront code but keeps Linux/Steam Deck open, has first-class ray-tracing extensions, and its explicit design is exactly what a "strong CPU utilization across many cores" engine wants. Windows is only the _initial_ target, not the permanent one.

The upfront cost is real and known: Vulkan needs roughly 800–1000 lines before it draws a single triangle. That was accepted deliberately.

### GLFW for windowing, not raw Win32 and not SDL

We need a window and keyboard/mouse input. Writing raw Win32 window handling is several hundred lines of boilerplate that teaches nothing about voxels or rendering.

- **SDL3** was considered and rejected for now: it is much larger, bundles audio/gamepad/threading/filesystem subsystems we do not need yet, and its extra surface area is not free.
- **GLFW** is small, zlib-licensed, has native Vulkan surface creation built in, and does exactly one job.

If gamepad support or audio becomes a real requirement later, revisiting SDL is legitimate — but "GLFW is missing feature X" must be a real blocker first, not a hypothetical.

### CMake + Ninja, built through CMake Presets

- **CMake** is the de-facto standard for C++ and is what every third-party library expects. Nothing else is realistic.
- **Ninja** is the thing that actually runs the compiler commands. MSBuild (Visual Studio's own) works too but is meaningfully slower for incremental builds, and on a 22-thread machine that difference compounds badly once the codebase is large.
- **CMake Presets** (`CMakePresets.json`) exist so the build configuration is a committed file rather than a per-machine ritual. Anyone (including a future session) runs one named preset instead of remembering flags.

### Dependencies fetched by CMake, not vcpkg and not committed binaries

`FetchContent` makes CMake download pinned dependency sources at configure time.

- **vcpkg** was rejected for now: it is a whole extra package-manager layer to learn and maintain for what is currently a single dependency.
- **Committing prebuilt `.lib`/`.dll` files** was rejected: it bloats Git history, hides which version we are on, and breaks the moment we target a second platform.

`FetchContent` pins an exact Git tag, so builds are reproducible and upgrades are a one-line, reviewable diff. **If the dependency count ever passes ~5, revisit vcpkg** — that is the point where FetchContent's build-times start to hurt.

### Vulkan SDK installed properly, not headers-only

Vulkan can technically be used with just the Khronos headers plus the driver-provided runtime, with no SDK. That was offered and rejected in favor of the full LunarG SDK, because the SDK provides **validation layers** — a debug mode that watches every Vulkan call and reports exactly what you did wrong. Without them, a mistake in Vulkan does not produce an error; it produces a black screen, a driver crash, or nothing at all. For someone learning Vulkan this is the single highest-value tool available. The SDK also provides `glslc`, the shader compiler, which is needed from Milestone 2 onward anyway.

### Placeholder project name

The CMake project is currently named `VoxelGame` with targets `engine` and `game`. **This is a deliberate placeholder, not a decision.** It was chosen to avoid blocking Milestone 1 on a naming conversation. Renaming later costs one line in the root `CMakeLists.txt` and a folder rename — do not treat it as load-bearing, and do not scatter the name through source files.

### A user-adjustable frame cap, not vsync, and not uncapped

Uncapped, Milestone 1 ran at 400–1600 fps — rendering hundreds of frames per second that no monitor displays, on a laptop, burning battery and spinning fans for nothing. The user asked for this directly (2026-07-30): _"i don't want it eating all my resources all the time right?"_

The cap is **120 fps by default and adjustable at runtime** (`F1`/`F2`), not a hardcoded constant, because the user explicitly wants to choose it while playing. Two alternatives were considered and rejected:

- **Vsync (`FIFO` present mode)** locks the frame rate to the monitor's refresh rate. That is a different feature from a user-chosen cap — it cannot give 60 on a 165 Hz panel, and it couples frame pacing to hardware the player did not choose.
- **A compile-time constant** would have been less code but does not satisfy the actual request.

The keyboard binding is a **temporary stand-in for a settings screen**, and is documented as such. Do not treat `F1`/`F2` as a permanent interface, and do not build a settings UI in response to this note either — wait until it is asked for.

Deliberate boundary: `FrameLimiter` knows only a target number. The list of selectable caps and the key bindings live in `game/src/Main.cpp`, because which frame rates a player may pick is a game decision, not an engine capability.

---

## Architecture Rules

- **The goal is an original game, not a Minecraft clone.** The genre conventions (exploration, building, crafting, survival) are the target; Minecraft's specific mechanics, content, recipe trees, tool tiers, creatures, and biomes are not. From Phase 5 of `TIMELINE.md` onward, "how does Minecraft do it?" is a reasonable *engineering* question and a bad *design* one — the finished game must have its own identity.
- **`engine/` must not know that `game/` exists.** The engine is a library; the game is an executable that uses it. If engine code ever needs to reference a game concept (blocks, chunks, the player), that is a signal the abstraction is in the wrong place — the engine should expose a mechanism and let the game supply the policy. This one rule is what makes it possible to eventually have tools/editors/servers reusing the same engine.
- **Prefer plain data and explicit ownership over inheritance hierarchies.** No `GameObject` base class, no virtual-everything. The moment this becomes a deep class tree, both multithreading and cache performance become impossible to recover.
- **Structure for future multithreading without doing it now.** This is the _only_ concession being made to future threading, and it is non-negotiable from the first chunk milestone (`TIMELINE.md` M4) onward, because it cannot be retrofitted:
  1. **Chunk generation is a pure function of `(seed, chunkCoord)`** — no neighbour reads, no global mutable state, no wall-clock time.
  2. **Meshing is a pure function of `(blocks, neighbour borders)` returning vertex data** — it does not upload to the GPU and does not mutate what it reads.
  3. **Exactly one owner mutates the world**, on the main thread. Workers get copies and return results.

  Threads are the easy part; untangling shared mutable state afterwards is the rewrite. This is also the specific weakness the user wants this engine to beat.
- **The renderer is deliberately NOT future-proofed, and will be rewritten at `TIMELINE.md` M23.** That is planned, not a failure. Do not add an `IRenderBackend`, a material abstraction, or "PBR-ready" hooks before then — building a deferred HDR pipeline before a single block is on screen means carrying huge complexity, blind, with nothing real to measure against. What must survive untouched is the world data, generation, meshing inputs, persistence, physics, and gameplay. Vertex formats will churn at M14 and M23; that churn is localized inside meshing and is accepted.
- **Rendering resources are owned by RAII types.** Vulkan requires explicit destruction of nearly everything, in the right order. Tying each Vulkan handle to a C++ object that destroys it in its destructor is what prevents this from becoming a leak-hunting nightmare at scale.
- **No abstraction without a second implementation in sight.** Do not write a `IRenderBackend` interface "in case we add DirectX." One backend, concrete, until a second one is actually being written.
- **One milestone, one working build.** Never leave the repository in a state that does not compile and run.

### Coding Habits That Make Later Migration Cheap

User instruction, 2026-07-30: _"coding with multi-threading and graphics and stuff in mind make it easier in the future to migrate to the big guns and you have to think less and i have to worry less about screw ups. time is also saved."_ This is correct, and it is about **how code is shaped**, not about building the future systems early. Apply these by default, in every milestone, without being asked.

**Data and ownership — these make M12 (threading) nearly free:**

- **No global mutable state, no singletons.** Anything a worker might touch should be passed in, not reached for. This one habit prevents most threading rewrites.
- **Separate "compute the result" from "apply the result."** A function that calculates a mesh, a generation result, or a physics resolution should return it, not install it. The caller applies it. This is what makes work movable to another thread later.
- **Explicit single ownership.** Prefer a clear owner plus plain references over `shared_ptr` for hot data. Shared ownership makes it genuinely unclear who may mutate what, which is exactly the ambiguity that becomes a data race.
- **Plain data in contiguous arrays**, not graphs of small heap objects pointing at each other. Good for the CPU cache now, and a prerequisite for handing slices of work to threads later.
- **Keep allocation out of hot loops.** Reuse buffers. This matters for frame pacing long before it matters for threads.
- **If you find yourself wanting a mutex, the design is probably wrong.** Prefer giving a worker its own copy and taking a result back.

**Rendering — these make M23 (PBR/deferred) a contained rewrite instead of a sprawling one:**

- **Mesh generation never touches the GPU.** Meshing produces vertex data; a separate step uploads it. Needed for threading anyway, and it means changing the vertex format later touches two places, not twenty.
- **Vertex layouts live in one place**, not duplicated between shader, mesher, and pipeline setup. They *will* change at M14 and M23.
- **No Vulkan types in gameplay code.** Game logic says "draw this chunk," never `VkCommandBuffer`.
- **Think in batches, not individual draws.** Even while drawing one thing, do not bake in assumptions like one-texture-per-draw or one-draw-per-chunk that a batched or indirect renderer would have to unpick.
- **RAII for every GPU resource**, destroyed in reverse creation order.

**And the counterweight \u2014 what "thinking ahead" must NOT become:**

Do not add interfaces, virtual base classes, "manager" objects, template generality, event buses, or configuration hooks for systems that do not exist yet. Speculative structure is not foresight; it is weight that every later milestone has to carry, and it is the single most common way ambitious engine projects die. The habits above are free because they are about *shape*. Abstractions are not free, and they wait until there is a second real case.

---

## Milestone Log

> Three of the four bugs found during M3 were caught by the user playing the build, not by reasoning about the code: the cube reading as a blob, the wide-angle distortion, and inverted backface culling. None produced a validation error or a warning. This is the concrete evidence behind the "ship something playable at every milestone" rule above.

### Milestone 1 — Prove the toolchain (2026-07-30)

**Goal:** the smallest clean C++20/CMake/MSVC/Vulkan project that opens a window, clears it to a color, runs a loop, and closes cleanly. Explicitly _not_ a game.

**Why this is worth a whole milestone:** on Windows, the C++ + Vulkan toolchain has many independent pieces (compiler, SDK, build backend, driver, loader, validation layers) and any one of them being subtly wrong produces a confusing failure much later. Proving all of them at once, on an intentionally trivial program, means every future bug can be assumed to be in _our_ code.

**Outcome: complete and verified.** Zero warnings at `/W4`, zero validation errors, correct discrete-GPU selection, clean shutdown. Measurements are in `SYSTEM_MEMORY.md`'s Current Validation Baseline.

**Added mid-milestone at user request:** a runtime-adjustable frame cap (see "A user-adjustable frame cap" above). This was scope beyond the original seven requirements and was accepted because it is small, self-contained, and prevents the loop from burning a laptop's battery at 1600 fps during every future milestone.

Environment setup notes for this milestone are in the "The Machine" section above; verified versions are in `SYSTEM_MEMORY.md`.

---

## Lessons and Gotchas

- **Backface culling errors are completely silent, and a hand derivation of the winding was wrong.** With `VK_CULL_MODE_BACK_BIT`, getting `frontFace` backwards keeps the far half of every closed object and discards the near half — boxes render as hollow shells you are looking *into*. Validation layers say nothing, the frame rate is unchanged, and every individual triangle is drawn correctly. Only a human looking at a closed shape catches it. On 2026-07-30 the value was derived by hand — twice, through the Vulkan signed-area formula — reasoning that flipping Y in the projection reverses screen winding, concluding `VK_FRONT_FACE_CLOCKWISE`. **That was wrong; the correct value with this codebase's CCW-from-outside geometry and Y-flipped projection is `VK_FRONT_FACE_COUNTER_CLOCKWISE`,** established by looking at the screen. The derivation's flaw was never found. **Do not re-derive this and "fix" it — verify against a closed box viewed from outside.**
- **Do not infer world orientation from a rotating object's face colours.** While diagnosing the above, the cube was spinning on an arbitrary axis, so face colours (painted in model space) said nothing about which way was up. A confident claim that "left and right faces are both visible, culling is broken" was made from exactly this reasoning and was simply false — both screenshots showed three mutually adjacent faces, entirely valid. **The user proposed the fix: freeze the object in a known orientation, put it on a ground plane, and move the camera instead.** That turns "the camera is above, so I must see the bright top face" into a falsifiable test. Adopt this shape of experiment for any future "is the geometry right?" question.
- **Sharing vertices between cube faces destroys the edges that make it read as a cube.** The first cube used 8 shared corners; where three faces meet they were forced to share one colour, so the GPU blended smoothly across every edge and the result looked like a soft blob. Geometrically perfect, visually unreadable. The fix is **4 vertices per face, 24 per box** — which is what voxel meshing needs regardless, since normals and texture coordinates also differ per face at a shared corner. A vertex is position *plus everything that varies*; only the position is actually shared.
- **A wide field of view close up looks like a bug and is not.** At 60° with the camera near the subject, perspective foreshortening made a cube look like a warped trapezoid. The projection maths was verified correct (horizontal and vertical scale factors produce identical on-screen size for a square). Fixed by dropping to 45° and moving the camera back. Check the maths before changing it, but treat "looks wrong" as worth investigating either way.
- **A Windows console window freezes the program the moment you click inside it.** Cost real confusion on 2026-07-30: the Vulkan SDK installer was reported as "a blank TUI" doing nothing. It was not broken — the window title had become `Select C:\...\vulkansdk-...exe`, and that `Select ` prefix is Windows' QuickEdit mode, entered by a single click in the console. In that mode the process blocks the next time it writes to stdout. **Diagnosis:** check `(Get-Process <name>).MainWindowTitle` for a `Select ` prefix and compare `.CPU` seconds against elapsed wall time — a process using 0.08 CPU-seconds over 90 seconds is parked, not working. **Fix:** press `Esc` or `Enter` in that window. Applies to any long-running console tool, not just installers.
- **A plain shell cannot build this project, and the failure looks like a broken install.** CMake's Ninja generator locates MSVC through `PATH`/`INCLUDE`/`LIB`, which only exist inside a Developer environment. Verified directly by configuring from a scrubbed shell: `No CMAKE_CXX_COMPILER could be found`. An earlier version of `SYSTEM_MEMORY.md` confidently claimed CMake would find MSVC on its own via the Visual Studio registry — that is true only for the `Visual Studio` generators, not for Ninja. **The claim was written from plausible reasoning and was simply wrong; it was caught only because it was actually tested before being trusted.** `tools/dev-env.ps1` now exists so this is one command instead of a rediscovery.
- **VS Code's CMake Tools needs `architecture`/`toolset` with `"strategy": "external"` in the preset**, or it cannot build a Ninja + MSVC project either — same root cause as above. Those fields are ignored by CMake itself for Ninja, so they are free on the command line and load-bearing in the editor. Separately, `Cannot configure: No configure preset is active` just means no preset has been selected yet (`Ctrl+Shift+P` → `CMake: Select Configure Preset`); it is not an error in the preset file.
- **`std::this_thread::sleep_for` is far too coarse for frame pacing on Windows.** The default system timer resolution is about 15.6 ms, so asking to sleep 8.3 ms for a 120 fps cap actually sleeps ~15.6 ms and lands near 64 fps. The naive implementation would have looked like a mysterious "my cap doesn't work" bug. `FrameLimiter` instead uses a Win32 **high-resolution waitable timer** (`CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) and spins the final ~500 µs. `timeBeginPeriod(1)` was rejected because it changes timer resolution process-wide as a side effect. Measured result: a steady 120–121 fps against a 120 target. Reuse this pattern for any future precise wait; do not reach for `sleep_for`.
- **The VS Code integrated terminal mangles multi-line PowerShell input.** Observed 2026-07-30: a multi-line script with a `function` definition and blank lines came back as the single garbage token `inue'` — the tail of `SilentlyContinue'`, meaning the input was fragmented mid-paste. Nothing was wrong with the script itself. **Always send one line, chaining with `;`.** For anything genuinely long, write a `.ps1` file and invoke it, or base64-encode it and use `powershell -EncodedCommand`.
- **`Start-Process -UseNewEnvironment` is broken in PowerShell 5.1** — it strips variables PowerShell itself needs and dies with `Internal Windows PowerShell error ... 8009001d`. To test something in a clean environment, launch a child shell and reset `Path` from the Machine/User registry values by hand instead.
- **`cl.exe` not being on `PATH` does not mean MSVC is missing.** MSVC deliberately only appears on `PATH` inside a "Developer Command Prompt." To check whether MSVC exists at all, look for `vswhere.exe` at `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` instead. (On this machine it genuinely was missing — but the check matters, because the naive `Get-Command cl` test reports "missing" even on a correctly configured machine.)
- **`Get-LocalGroupMember` can silently return nothing for a non-admin caller**, which makes an "is this user an admin?" check produce a false negative for the wrong reason. `net localgroup Administrators` works from a standard account and gives the real answer.
- **PowerShell prints a native program's stderr as a red `NativeCommandError`, which is not a failure.** `cl.exe`, `code --install-extension`, and `vulkaninfo` all write informational banners and warnings to stderr and still succeed. Check the exit code and the actual text before reporting a problem.
- **Say what success is supposed to look like before showing it.** The user reasonably asked whether "it just shows a shade of blue, that's it" was a problem — it is precisely the milestone's goal, but that had not been stated plainly enough up front. When a deliberately minimal result could be mistaken for a broken one, name it in advance.

---

## Update Discipline

Keep this file for **non-obvious lessons, rejected approaches, unresolved problems, and the reasoning behind decisions**. Current mechanics, structure, versions, and commands belong in `SYSTEM_MEMORY.md`.

Update existing bullets in place rather than appending a chronological diary. Do not log routine work — if a change would be obvious to anyone reading the code, it does not belong here. Add an entry when a decision was contested, when something failed in a way that would fool the next person, or when a future session might reasonably undo something on purpose.
