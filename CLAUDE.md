# CLAUDE.md

Narrative decisions, rejected approaches, debugging lessons, and machine-specific guardrails for future coding sessions on this voxel sandbox project.

**New to this project? Read `START-HERE.md` first** — one page, ten minutes, and it names the current state, the traps, and which of the other documents to open and when. Then come back here.

**This file explains _why_ settled choices exist**, so a future session does not re-litigate them or repeat a mistake that has already been paid for once. Current mechanics, structure and commands are in `SYSTEM_MEMORY.md`.

---

## If you are a new session, read this box first

Seven things that will otherwise cost the user time they have already paid once.

1. **Bedrock Edition is the reference, not Java.** Direct instruction 2026-08-03. Where they differ — attack cooldown, hitboxes, creeper damage, entity cramming, mob caps, despawning — Bedrock wins. `RESEARCH.md` marks Java values `[JE]` throughout.
2. **Never commit to Git.** Not once, not "to be tidy", not while wrapping up a milestone. Write files and leave them uncommitted.
3. **The user is a testing tool and has said so three times, the last in capitals.** Anything needing aim, a held button, or a judgement about feel goes to them. **Two failed screenshot-harness attempts is the signal to stop**, restore `settings.cfg` and `saves/*/player.dat`, hand over the build, and say what to look at.
4. **Building a creature model has a written procedure and skipping it is why the last three looked wrong.** `TEXTURING.md` "Reading a net you have never seen before" — alpha runs before island detection, missing rects as placement clues, colour where arithmetic ties, a **shared-extent scan before rendering**, then two renders and the eye. Budget is two renders; a third means the *pose* is wrong, not the numbers.
5. **Subagents must be launched with `model: "Claude Opus 5 (copilot)"`.** Omitting the parameter silently gets a small default and the research comes back thin.
6. **"Both presets clean, zero validation errors" proves almost nothing.** It proves the thing does not crash. It says nothing about per-block tables, icons, screen state or item conservation — a bug sweep on 2026-08-06 found five real bugs, including two-keystroke item duplication, in code that had already passed exactly that bar. **`constexpr` per-block functions can be logged at startup by a temporary probe and checked in one run**; that is the cheapest real verification available here, and deleting the probe afterwards is part of it.
7. **A worldgen fix that tests clean can still be plainly broken on screen, because the world you are looking at was loaded rather than generated.** Water flow calls `setBlock`, which flags a chunk modified, so merely standing near an ocean writes chunks to disk. **Check `saves/`, and bump `kChunkFormatVersion` — that is what it is for.**

**The single most common shape of bug in this codebase:** a value derived somewhere other than the one table that owns it. Block shapes, creature nets, sheet dimensions, knockback — each has one home, and every bug where "it compiled and looked plausible but was subtly wrong" traces back to a second copy. When widening what something can be, grep for the old literals; the compiler cannot help because nothing changed type.

**Its close cousin, and the one that bit hardest:** widening a *predicate* silently kills every early-out already sitting behind it. `isFurnace` was widened to cover smokers — which is exactly what made the smoker free — and that turned eight `case` labels in `blockName` into dead code the compiler was perfectly happy with. **When you widen a family test, grep for everything that already asks it.**

**And the third, which no tool here can catch:** a number ported from the reference into a field measured in a *different unit*. It compiles, it validates, and the system around it works perfectly. `panicSpeedScale` multiplies our `runSpeed`; Bedrock's `panic.speed_multiplier` is measured against its single `movement` speed. Two humanoids carried the reference's value while every animal carried one authored natively — one column, two units — and a struck trader fled at a margin too small to see. **Write down what a ported number is measured against.**

**The fourth, found 2026-08-08:** a predicate written as **"equals the empty case"** where a general test already exists. A fluid drew its face where `ahead == BlockId::Air`. True while nothing but air ever sat beside water, and silently wrong for every non-cube added since — a lily pad, torch, fence or pane of glass next to water *deleted the water's surface there*. The right question, `occludesFace`, was one line away in the same expression's other branch. This shape does not announce itself when it breaks, because the code that introduces the second case is somewhere else entirely; **the symptom names the newest arrival, never the old rule.** The same session found `harvestTier` returning a real value from a `default:` for two hundred blocks, which is the same thing wearing a different hat.

**The fifth, also 2026-08-08, and the cheapest to prevent:** a derivation **applied to one of a pair and not the other**. The mesher's shape pass read `flipV` off the face tables and then wrote U as the raw cell coordinate — so the +X and −Z faces of *every shaped block in the game* were mirrored, and had been since shapes existed. Nothing showed it, because no shaped block carried an asymmetric texture until lit TNT did, and the user reported it as "the n is reversed". Ordinary TNT is a full cube and goes through the merged pass, which was always right, so the same block was correct in one state and backwards in the other. **When you derive one of a pair from a table, derive the other from the same table and write the `constexpr` assert** — `shapeUvsMatchFaceTables()` is four lines and would have caught it the day it was written.

**The sixth, which cost three separate bugs in two days:** **a blended fragment still writes depth.** The HUD shares the world pipeline, the depth test is `LESS`, and there is no alpha discard on the HUD or font layers — so anything transparent still stamps its distance and rejects whatever is drawn *later* behind it. It took the stack on the cursor punching a rectangular hole through the catalogue, a stair icon's two boxes fighting so the slab's lit top face won over the step standing on it, and every letter after the first losing its left two columns, before the pattern was named. **Ask it of any HUD geometry that overlaps**: ordering fixes the first, a per-box depth key the second, and not overlapping at all the third.

**The seventh, and the one to reach for whenever something looks choppy:** **a fixed simulation step needs render interpolation.** Projectiles tick at the reference's twenty a second because every number it publishes is per tick and four `static_assert`s depend on it — and drawing them *at* that tick showed each position six frames running on a 120 fps screen. The instinct to raise the tick rate would have quietly falsified the whole table. Keep the previous tick's state and blend by the accumulator's remainder; the fix belongs in the renderer, never in the simulation.

**And the counterweight to all of them, which is what six hundred and forty blocks in one afternoon actually bought:** when forty rows would differ only by *material*, the material is the table and everything else is one forwarding function. There was one stair type in the game for twenty milestones because a second one meant seven edits in seven files. `ShapedFamily` carries a **parent block**; `shapedParent` answers the texture, the tool, the hardness, the blast resistance and whether it burns. **Before writing the third near-identical row, ask what the rows actually differ by** — and if the answer is "one field", that field is the table.

**The eighth, and the reason a bug can be genuinely unreachable:** **a format-version migration is a promise that old data stays, which is also a promise that old *damage* stays.** Water was found hanging in mid-air; a probe put it at a sheet of water sources left in one chunk by a long-deleted test harness, and generating that chunk from the same seed produced none of it. Bumping `kChunkFormatVersion` — the mechanism that exists for exactly this — **did nothing**, because the bad file was two versions old and the loader carried an upgrade path that read and widened it rather than rejecting it. **When a version bump changes nothing, read the bytes on disk before doubting the fix.**

**The ninth, 2026-08-10, and the cheapest of all to have avoided:** **a standard formula written from memory can come out inverted, and consuming it as a *ratio* hides that.** The air-mass term in the new sky reported less air toward the horizon than straight up — the exact opposite of the truth — and divided by zero below the horizon, painting a black band precisely where the loaded chunks end, in the one place the fog exists to hide. It survived a build, a soak and a doc pass because the model is consumed as a ratio of two evaluations, and a ratio still looks plausible when both halves are wrong. **Print a curve you did not derive on the spot at three known points before trusting it** — air mass is 1 at the zenith and about 40 at the horizon, and three printed rows would have caught it in ten seconds.

---

## Critical — Read First

- **Never run `git commit`, `git push`, or any history-rewriting command unless explicitly asked.** Direct user correction, 2026-07-30: _"undo those commits. who told you to commit. keep changes that's fine, no committing."_ Three commits had been created unprompted while wrapping up Milestone 1, on the reasoning that the user had asked for "Git-friendly incremental changes" — that phrase describes how changes should be *shaped* (small, reviewable, not giant rewrites), **not** permission to run Git for them. Write files, leave them uncommitted, and let the user decide when and what to commit. `git init` and `.gitignore` were fine; the commits were not.
- **Ship something playable at every milestone, and treat the user as the primary QA resource.** Direct user instruction, 2026-07-30: _"the best thing for the game is to get playability on early so that i can 'play' the game and report findings back to you. i am the most valuable bug finding resource you can have."_ `TIMELINE.md` was reordered because of this — first playability moved from M9 to M7, textures and chunk streaming were pushed *after* walking and block-breaking. A milestone that produces nothing runnable is too big; split it. When the user reports a bug, **default to "this is real and I haven't found it yet,"** not to explaining why the code looks fine.
- **When the user names a milestone, build it. Splitting it is my job, not a question to hand back.** Direct correction, 2026-08-01: _"exactly what part of 'start m20' did you not understand? my word is absolute authority and you have wasted hours of my time... you could've autonomously seen how big m20 is and split it up into subparts on your own and then accomplished it."_ I was told to do M20, correctly worked out it needed splitting into M20a, wrote that down as a recommendation — and then did not do it. **Knowing the answer and stopping is worse than getting it wrong.** Bugs are an expected cost of development and the user says so plainly; *not starting* is not. Time spent deliberating while they are away is time taken directly from them. Being away is exactly when long autonomous work should happen, not when it should pause.
- **The long, highly detailed vision prompts were written by another AI, not by the user.** Disclosed 2026-07-30: _"the bigger and more detailed prompts i gave weren't me, they were another ai helping me get my stuff sorted, so take what it says with a grain of salt."_ Those feature lists are a **statement of ambition and a menu of options, not a specification**. Do not treat any bullet in them as a commitment, do not implement something merely because it is listed, and where they conflict with the user's own plain-language wishes, **the user wins**. Their short, direct messages are the authoritative signal.

  **This was violated for fifteen milestones.** The vision documents' "everything must be original" rule was carried into `TIMELINE.md` as a hard requirement and enforced until the user objected — costing them a push-back they should never have had to make. When something in the plan cannot be traced to the user, **say so and ask**, rather than defending it.

  Audited 2026-07-31. **Confirmed by the user directly:** the graphics ambition is real — ray tracing, volumetric clouds, the modern renderer — delivered as **settings rather than requirements** (a `Video` tab with quality toggles). The 35-milestone structure is wanted. Do not re-question either.
- **Stop at milestone boundaries.** The user explicitly asked (2026-07-30) that work stop at the end of each milestone and wait for instruction. Do not roll straight from "the window renders" into "now let's add chunks." Finishing early and asking is correct behavior here, not laziness.
- **The user is an experienced vibe coder but NOT an experienced C++/graphics-engine programmer.** They understand concepts when explained simply. Do not assume familiarity with low-level graphics or systems terminology.
- **Use the real term, then immediately explain it in plain English.** Direct user instruction, 2026-07-30: _"remember i'm just a vibe coder i don't understand all this complex terminology so use the terminology, then explain it in simple human terms so i learn over time."_ This is a learning-over-time request, not a request to dumb things down — do not silently substitute vague language for the correct word, and do not drop the plain-English half either.
- **Do not build ahead of the current milestone.** No voxel terrain, physics, ray tracing, procedural generation, PBR, weather, or job systems until explicitly asked. The long-term ambition is real, but building toward it speculatively is the fastest way to a codebase nobody can finish. `TIMELINE.md` records the intended order and why.
- **Multiplayer is permanently cut, not deferred.** User decision, 2026-07-30: _"no multiplayer btw."_ This is a **design freedom, not a limitation** — treat single-player as a licence to avoid client/server splits, server-authoritative simulation, replication hooks, and determinism constraints that exist only to serve netcode. Do not add abstractions "in case multiplayer happens later." An earlier draft of the vision listed multiplayer as long-term; that was explicitly overridden.
- **This machine's Windows account is a standard user, not an administrator.** See "The Machine" below. Never ask the user to type a password or PIN into anything the model can read.
- **Windows PowerShell 5.1 only. Never use `&&`** — chain with `;`. **Never send multi-line scripts to the terminal** (see Lessons).
- **This is not being built inside Unity, Unreal, or Godot, and that is not up for casual reconsideration.** The whole point is a custom engine. If a task feels like it "would be easier in Unreal," that is expected and not a signal to switch.
- **Do not add a dependency unless the current milestone actually needs it.** Every dependency must be justifiable in one sentence at the moment it is added, not "we'll want it eventually."
- **Threading landed at M12 and is confined to generation and meshing.** Everything else is main-thread by design, and the three purity rules in "Architecture Rules" are what keep it that way. Do not widen it without a measurement saying the serial part is the problem.

---

## The User and Working Style

The three big ones — vibe coder not a C++ programmer, term-then-plain-English, playtester — are in "Critical" above. What is not there:

- Thinks in terms of outcomes and feel ("large procedural worlds," "high-quality graphics"), not APIs. Translate that into technical plans rather than asking them to specify the technical plan. Do the low-level reasoning and stack-trace interpretation yourself.
- Wants to **understand**, not just receive working code. Before introducing a new library, tool or concept: what it is, why we need it _now_, what happens without it. A few sentences — the instruction was explicitly "do not drown me in theory."
- Prefers that missing tools be **installed**, not merely reported as missing. Only escalate to "you need to do this manually" when there is a genuine hard blocker (admin rights, a license click, a hardware limitation), and even then, automate everything on either side of the blocker.
- Wants verification, not assumption. "It should work now" is not acceptable — build it and run it.
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

### Bedrock Edition is the reference, not Java

Direct instruction, 2026-08-03: _"we are to focus on bedrock mainly."_ This is not a detail — the two editions differ in ways that change what we would build:

- **Bedrock has no attack cooldown.** Every swing does full damage; there is no charge meter, no 84.8% threshold, no sweep attacks. Following Bedrock means **we skip that entire system**, and combat stays "click to hit" rather than "time your clicks".
- **Bedrock axes are weaker than swords** and hoes match pickaxes; Java inverts both. Bedrock's tiering is the more intuitive design.
- **Bedrock has no entity cramming.**
- **Bedrock creepers hit roughly a third softer** (27.5 vs 43 point-blank on Normal).
- **Bedrock despawns almost all mobs including animals**; Java's farm animals are permanent.
- Half the roster has different hitboxes — chicken, rabbit, cow, wolf, creeper, donkey, frog and the XP orb all differ.
- **Bedrock's mob AI is data-driven and documented**, which makes it a far better architecture to copy. See `RESEARCH.md` §8.

Where a number is needed, take Bedrock's. `RESEARCH.md` marks Java values `[JE]` throughout so the divergence is always visible.


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

### Terrain is single-valued, and that is a rule rather than a tuning

Everything at or below the highest solid sample in a column is ground. **Do not reintroduce `quarter_negative` or any other mechanism that lets the density go negative and then positive again up a column** — that is the definition of a floating island, and the user found them by playing.

**The guarantee alone is not enough, and that cost a second bug report.** Single-valued terrain does not stop a would-be island being *produced*; it converts it into a column attached to the ground, and where the footprint is small that is a sheer-sided pillar with a tree on it. The underlying condition still has to hold: **the terrain ramp must beat the noise's vertical slope**. The reference does that by holding `factor` at 5.1–6.3 across ordinary land and dropping to 0.625 only inside a narrow window of high peaks-and-valleys. Ours ran 1.15 across the whole low-erosion third of the map, which is ±19 blocks of displacement, and that produced the pillars. Ruggedness belongs to the same window as jaggedness; they are one decision.

**There is no density lattice any more, and it should not come back.** With single-valued terrain it bought nothing but its own artefact: an interpolated density is piecewise-linear across a cell, so its contour lines cluster on the cell boundaries and a gentle slope reads as regular banding every four blocks. Height is computed directly per column, which is exact and eight times faster.

What this costs: no overhangs, no arches. What it buys beyond the bug: the surface rules get an exact depth counter for free, and the ore air-exposure test is a height comparison instead of six density evaluations.

### Bare stone on ordinary ground is always a bug, and `else stone` is how you cause it

The reference names what a biome *scatters* and lets everything that does not match **fall through** to the generic grass-and-dirt rule. `windswept_hills` is grass with stone patches; `plains`, `meadow` and `forest` are not named in its surface-rule tree at all. Bare stone never appears on an ordinary grassy hillside there, and the guarantee is structural rather than statistical.

Writing a biome's top block as solid stone instead produced **rings of bare stone** across the landscape, because that biome occupies a narrow band of the erosion field and a narrow interval of a smooth 2D field is an annulus. `Biome::patch` exists so the no-else pattern is the only way to express it.

**A bed material is unreachable above the waterline, and that is the whole of why a dry river is not a scar.** `river` is not named anywhere in the reference's surface rules: a river column falls through to grass, gravel is only reachable after both water tests have already failed, and **sand never appears on a riverbed at all**. The reference does not guarantee its rivers hold water either — at low erosion its river band sits well above sea level, and it ships dry grass-covered river strips through its mountains that are invisible purely because of that fallthrough. Ours painted the biome's own top block regardless of the waterline and drew ribbons of gravel and sand across dry grassland.

The one legitimate exception is `steep`, the reference's own slope test — and it belongs only on peaks and slopes. **Applying it to ordinary ground creates exactly the artefact it is there to avoid.**

### Terrain and biome are outputs of the same fields; neither decides the other

Up to 2026-08-07 a biome carried `baseHeight` and `amplitude` and terrain height was looked up from it, so the land was a **consequence of the label**. The reference abandoned that in its 1.18 rewrite — its biome files carry no `depth`, no `scale` and no `height` at all — and M20p followed it.

**Do not put a height back on a biome row.** It will look like a convenience and it reintroduces the failure that made the old world read as bands: with the biome dictating, a boundary is a step in the terrain, and blending the step is a second mechanism papering over the first.

The three pieces that carry the replacement, in order of how much they buy:

- **`factor`, not height.** How *hard* the target height is enforced, rather than what it is. It is the only reason a plain and broken ground can exist at the same temperature, humidity and altitude, and it is a multiply rather than a system.
- **Boxes, not centres.** A biome claims a range on each axis and the first containing row wins. A centre with a falloff cannot say "only ever cold"; every biome bleeds a little way into every other, which is what made seven biomes average into the same middling terrain.
- **Tags, not names.** Anything asking a question about a place asks for a property. Fifty creature spawn rules named biomes outright, and going from seven to twenty-seven would have meant editing all fifty with a silent failure for any one missed.

**Rivers are not a system and must not become one.** They fall out of `ridges` reaching −1 on the zero contour of the weirdness field. The biome and the channel are the same fact.

### A user-adjustable frame cap, not vsync, and not uncapped

Uncapped, Milestone 1 ran at 400–1600 fps — rendering hundreds of frames per second that no monitor displays, on a laptop, burning battery and spinning fans for nothing. The user asked for this directly (2026-07-30): _"i don't want it eating all my resources all the time right?"_

The cap is **120 fps by default and adjustable at runtime** (`F1`/`F2`), not a hardcoded constant, because the user explicitly wants to choose it while playing. Two alternatives were considered and rejected:

- **Vsync (`FIFO` present mode)** locks the frame rate to the monitor's refresh rate. That is a different feature from a user-chosen cap — it cannot give 60 on a 165 Hz panel, and it couples frame pacing to hardware the player did not choose.
- **A compile-time constant** would have been less code but does not satisfy the actual request.

The keyboard binding is a **temporary stand-in for a settings screen**, and is documented as such. Do not treat `F1`/`F2` as a permanent interface, and do not build a settings UI in response to this note either — wait until it is asked for.

Deliberate boundary: `FrameLimiter` knows only a target number. The list of selectable caps and the key bindings live in `game/src/Main.cpp`, because which frame rates a player may pick is a game decision, not an engine capability.

### Retired GPU buffers, not `vkDeviceWaitIdle`, once chunks stream

Up to M7, every mesh replacement called `vkDeviceWaitIdle` before freeing the old buffers. That is the simplest correct answer to "the GPU may still be reading this" and was the right call while edits were occasional.

M8 made it untenable: chunks load and unload continuously, and stalling the entire GPU several times a second is a permanent stutter. Buffers are now moved to a retirement list tagged with the frame index and destroyed once `kFramesInFlight + 1` frames have started.

**Do not simplify this back to a device wait.** It will look like a harmless cleanup and will reintroduce a stutter that only shows up while moving. The release pass must also stay at the very top of `drawFrame`, **before the minimized early-return** — otherwise a minimized window accumulates retired buffers forever.

The visible counters (`chunks | meshes | pending | retired` in the per-second log) exist specifically so this class of bug is observable rather than inferred. `retired` sitting above zero while standing still means the release pass has stopped running.

### Dear ImGui deferred from M11 to M34

`TIMELINE.md` names M11 as the milestone where a debug UI library arrives. It was not added, and that is a deliberate deviation rather than an oversight.

M11 needs read-only numbers, labels and a frame-time graph. ImGui's real value is interactive widgets — sliders, checkboxes, text input — and the milestone that genuinely needs those is **M34, settings and accessibility**.

The first attempt tried to avoid a font entirely by drawing seven-segment digits and colour-coding the rows. **That failed** (see Lessons), and a hinted bitmap font atlas turned out to be roughly eighty lines of PowerShell plus one texture binding. The standing rule that a dependency must be justifiable in one sentence *at the moment it is added* still holds, and at M11 that sentence does not exist. **Revisit the moment the overlay wants anything editable.**

### A fixed worker pool chosen at startup, not a resizable one

User decision, 2026-07-30: _"if they adjust core count, the game will need a restart for that to take effect... no removing or adding parts to a moving train buddy."_ The worker count lives in `settings.cfg`, defaults to **half** the machine's hardware threads, and is read once at startup.

Half rather than all was also the user's call, and the reasoning is worth keeping: they want to choose between "I am multitasking, use one core" and "this session is for gaming, use half". A game that seizes every core by default is a bad citizen on a laptop.

**Resizing the pool at runtime was explicitly rejected.** Growing or shrinking a pool while jobs are in flight means draining, re-queueing and reasoning about half-migrated work — a whole class of bugs bought for a feature nobody needs mid-session. A restart is free. **Do not add a `setWorkerCount`.** When the settings menu arrives at M34, it changes the file and offers a restart button.

Zero workers is a **supported mode**, not a degenerate case: `submit` runs the job inline. It is the minimum-resource setting the user asked for, and it doubles as a way to reproduce a bug with threading removed from the picture. It must keep working — which is why the per-frame budget still meters the dispatch loops, since with no workers those loops *are* the work.

### Jobs own their inputs; they never point at the world

A mesh job gets a copy of the chunk and its six border layers, and captures `shared_ptr`s to the results buffer and the store — never a `World*`. This costs ~38 KB per job and buys two things that are worth far more: the main thread may do anything it likes to the world while jobs run, and a job outliving the `World` is harmless rather than a use-after-free.

**Do not "optimise" this into borrowing a pointer to the live chunk.** It will appear to work, because the race window is small and the failure is silent corruption rather than a crash.

### A placeholder sun at M14c, and the real one at M24

User reaction to M14, 2026-07-31: _"this looks very flat... there isn't a clear light source... your idea of 'shippable' was like no shadows whatsoever."_ They were right, and the diagnosis matters: **Minecraft's lighting model has no direction at all.** Sky light answers "can this cell see the sky?", which is a visibility question, not a lighting one. Ambient occlusion darkens crevices but is subtle by nature. Neither produces the sense of a light source, so the world read as flat — correctly.

M14c added a **cheap placeholder**: a visible sun on an arc, a Lambert term, and a sky colour that follows it. It cost one shader change and no vertex format change, and it was explicitly not shadows — nothing occluded the sun.

**M24 replaced it, and the three decisions inside it should not be re-litigated:**

- **The shadow pass culls nothing.** Front-face culling is the textbook cure for shadow acne, and here it would leave flat ground casting no shadow at all: a voxel mesh is a hollow shell, so a hill has no far side to store.
- **Each cascade is fitted to a bounding sphere and snapped to whole texels**, because a box changes size as the camera turns and every shadow edge in the world crawls with it.
- **One directional light, not two.** Whichever of the sun and moon is up owns it. Moonlight therefore goes through the same cascades for free, and the handover at dawn has no step in it because both fade on the same ramp.

**Lesson for tuning any visual effect:** the first ambient occlusion strength was tuned in isolation and shipped as "subtle", which to the user was indistinguishable from broken. Show the exaggerated version alongside the tuned one and let them pick, rather than deciding what "shippable" means alone.

### The HUD reuses the world pipeline, deliberately

Screen-space geometry goes through the same pipeline, shader and vertex format as blocks, distinguished only by a negative texture layer. A separate HUD pipeline would be the textbook answer and was not taken: it would mean a second pipeline object, a second shader pair, and a second vertex path, all to avoid one branch in a fragment shader that runs on a few thousand pixels.

The costs of that choice are real and worth knowing. Alpha blending is enabled for *every* draw, not just the HUD, which is free only because world geometry is opaque. And HUD geometry is depth-tested against the world, which is why every HUD element needs a hand-assigned depth.

**If the HUD ever needs its own blend mode, or depth testing becomes a problem, split the pipeline rather than adding more special cases to the shared one.**

### Block-entity contents are derived state on the screen, never owned by it

A furnace and a smithing table both borrow the crafting grid's first two slots, which is why neither cost a single line of new click, drag or shift-click handling. That is a good trade and should be reached for again. But it has one rule attached, and breaking it produced the worst bug this project has had: **whoever borrows the shared buffer must empty it when the screen closes.**

The furnace correctly refused to hand its slots back to the player on close — they were only a view onto a block that keeps its own copy. It did not clear them either, so the copies survived into the next screen, appeared in its crafting grid, and were handed over when that one closed. Free items, in two keystrokes, with any item.

A chest could not borrow the buffer at all: twenty-seven slots against a maximum grid of nine, and its contents outlive the screen. **The test is not how many slots a thing has, but who owns what is in them.** If the answer is not the screen, the screen may display it but must not store it.

### Chests pair by derivation, not by stored state

Two chests side by side form one 54-slot container. The obvious implementation remembers which is joined to which — and then that has to go into the save file, survive a reload, and be repaired every time a neighbour is placed or broken.

Instead the pairing is recomputed from world state on every open: walk back to the start of the run of matching chests and pair off in twos. It costs a few lines, needs no saved state, and **cannot disagree with itself**, because both halves run the same calculation. The price is one named divergence — a long row re-pairs when a chest in the middle is removed, where the reference would keep its original pairs.

**Prefer this shape generally.** Derived relationships cannot go stale, cannot be corrupted by a partial save, and do not need a migration when the rule changes.

### Where water goes and what water is are two questions

The slope weights decide which **empty** cells a flow spreads into. They must never re-decide a cell that already holds water — the reference's `getNewLiquid` recomputes a level from the neighbours with no slope test in it at all.

Folding the two together let a settled cell be starved by a weight that changed *because of its own outflow*: water reaches a ledge, the column below fills, that direction stops scoring as a drop, a rival wins, the cell empties, the column drains, the weight flips back. Air, water, air, water, once per tick, for ever — and only ever within four blocks of a drop, which is why it read as "water goes mad near an edge" and why flat ground never showed it.

It cannot recur while `arriving` gates the test, because the level away from a source strictly increases and a mutual pair therefore drains. **Do not merge them back for tidiness.**

### Waterlogging is a bit beside the block array, not a second block id

A cell can be a plant *and* water. The alternative — a `KelpSubmerged` twin for every waterloggable block — doubles those rows in the id space and forces every predicate that already asks a question about a block to learn about the twin. One bit per cell costs `kBlockCount / 8` bytes, and `setBlock` is the single place that maintains it.

**The rule that makes it safe: `canWaterlog` is a shape test, not a list. Cross only — plants.** Slabs and stairs were briefly included and were taken back out: a block placed into a water source has to *cut the supply off*, which is what the player expects and what a waterlogged stair silently refused to do. A new plant is waterloggable the moment it is given the right shape, with nothing to remember.

### Chunk saves and the player file carry separate version numbers

`kChunkFormatVersion` versions chunk files; `kFormatVersion` versions `player.dat`. **Bumping the chunk version is the intended mechanism for making a worldgen fix reach a world that has already been played** — stale chunks fail their header check and regenerate — and it must not cost the player their inventory, which is the entire reason the two numbers are not one.

Do not merge them for tidiness. And do not forget the mechanism exists: water flow calls `setBlock`, which flags a chunk modified, so **merely standing near an ocean writes chunks to disk**. A correct generator fix can look completely broken because the world being tested is loaded rather than generated.

### An ore vein is a placed feature with a stated size, not a noise threshold

A threshold has no notion of size — it paints every cell above the bar, so wherever the field sits high across a region you get one connected mass, and there is no number to turn down because the shape was never asked for. A playtest found a "humongous" coal seam and the table had nothing to answer with.

`placeVein` is the reference's `OreFeature`: a spindle of `size` overlapping spheres. **The maximum is now stated rather than emergent**, and measured largest vein matches the table's own arithmetic.

The constraint this buys and must keep: a vein straddles chunk borders, so **every vein rolls its randoms in the same order regardless of which chunk is asking** — a 3×3 column neighbourhood, a `noise::Stream` per vein, and the air-discard roll taken for every sphere cell *before* any chunk-bounds test. Skipping a roll because the cell landed outside the current chunk makes two chunks disagree about one vein and leaves a seam on the border.

### A ported tuning value is only as good as the unit it was measured against

`panicSpeedScale` multiplies our `runSpeed`. Bedrock has one `movement` speed per mob and scales that, so its `panic.speed_multiplier` is a different unit. The two humanoids were given the reference's 0.6 verbatim while every animal was given a value authored in ours — one column holding two units — and a struck trader fled at a 17% margin over walking, which reads as not fleeing at all.

**When taking a number from the reference, write down what it is measured against**, and check it against a neighbouring row that was authored natively. Nothing else in the toolchain can catch this: it compiles, it validates, and the behaviour tree around it works perfectly.

**The same shape, without any porting involved:** a *comparison* can be measured against the wrong body. The blow's vertical gate read `abs(toPlayer.y) < species.height` — the player's offset judged against **the attacker's** height. For two things standing on the same ground that is roughly the box overlap, and for a half-block-tall flier it is a demand it can never meet, so a bee chased, arrived, faced the player and never once stung. The honest test is asymmetric, because the player is 1.8 m tall and the creature is not — **and it was already computed three lines above under a different name.** Before deriving a geometric test, check whether the function already contains the correct one.

### A flier owns its own velocity, so nothing may push it

`flies` is the airborne mirror of `swims`, and the thing that makes it different from every other species is that its locomotion branch writes **all three axes** every frame from its heading. Gravity must be skipped rather than applied-and-overwritten, and knockback must be **skipped rather than reduced** — an impulse assigned to something that rewrites velocity next frame is either erased or, on the vertical, reads as the animal being thrown across the sky.

Do not reach for a `knockbackResistance` float to solve this. The question is not how hard to push; it is who owns the value.

### Damage overwrites inside its window; it is not a cooldown

Half a second of invulnerability after a hit sounds like a cooldown and is not. The reference lets a **bigger** blow inside that window land for the difference between it and the one already taken, which is the entire reason a creeper is dangerous rather than absorbed by whatever punched you a moment earlier.

`damagePlayer` owns that rule and it must not be simplified. "Ignore anything inside the window" is one line shorter, looks obviously equivalent, and quietly makes every large hit survivable. RESEARCH §2.4 has a worked example precisely so the next person can check rather than reason.

The same shape applies to hunger: **it drains by exhaustion, not by time.** A bar that ticks down on a timer punishes standing still, which is not a cost anyone incurred. Every action names its own price and four points spends one leg.

### `world/Survival.hpp` is header-only on purpose

Twenty hearts, three free blocks of fall, every exhaustion rate and the whole food table live in one header with no `.cpp`. That is not laziness — it means adding it required **no edit to any CMakeLists**, and it means every constant is `constexpr` and visible to whatever wants to check one.

The rule it enforces is the one this project keeps relearning: **a value derived somewhere other than the table that owns it is the most common bug here.** Twelve foods were edible and worth nothing because edibility and value were answered in two places. They are answered in one now — `isEdible` asks `foodValue`.

### miniaudio, not SDL_mixer, OpenAL or FMOD

Sound needed the operating system's audio device opened and a callback asking for samples. Nothing more.

- **FMOD and Wwise** are what a studio uses and both carry licensing that has to be read; neither is justifiable for a project whose entire audio requirement is "play this sample over there".
- **OpenAL Soft** does positional audio properly and would mean adopting its whole listener/source/buffer model, which is more concepts than we need and less control over the mix.
- **SDL_mixer** drags in SDL, which was already rejected for windowing at M1.
- **miniaudio** is one public-domain header, opens WASAPI/CoreAudio/ALSA, and does *not* mix, position or decode anything for us. That last part is the reason it was chosen: the mixing is thirty lines, we understand all of it, and the distance rolloff and pan are ours to tune.

`stb_vorbis` decodes the `.ogg` files, and **it lives alone in `engine/src/audio/StbVorbis.cpp`**. It is C, it warns freely at `/W4`, and the first attempt to silence it with `/W0` produced `D9025` — one warning traded for another, because the global `/W4` is still on the command line. Disable by number.

> ⛔ **The audio mixer holds a mutex, and that is the one legitimate lock in this project.** `CLAUDE.md`'s "if you find yourself wanting a mutex, the design is probably wrong" is about *our* threads, where a copy is always available. The audio callback runs on a thread the operating system owns and hands us no choice. Do not generalise from it.

### A sound is named by material and by voice family, never by block or species

Ten materials cover eleven hundred blocks, and thirty-four voice families cover fifty-seven creatures. Both are the same decision and it is the one that made the milestone small: `soundMaterialFor` asks a dozen family questions and falls through to stone, and a cut shape asks `shapedParent`, so **six hundred stairs and slabs arrived with no rows at all**.

A baby is the adult's recording pitched up, which is what the reference does. Do not add a second set.

The cost, and it is real: **`kStems` in `Sounds.cpp` and the `$events` table in the staging script must agree, name for name and in order.** A mismatch does not fail — it produces an event that silently never sounds. `has()` and `hasVoice()` exist so a probe can check all 159 rows in a single run, which is the only honest verification available here.

---

## Architecture Rules

- **This is openly a game in Minecraft's tradition, and following it closely is the point.** Direct user correction, 2026-07-31: _"do you know how difficult that is? i would have to spend weeks coming up with an entire new mechanic... this game is impossible to make without referencing to the original one, and truthfully, i don't want anything different. the only difference i wanted was control, which i now have cuz i'm developing it."_ An earlier version of this file demanded original mechanics, recipe trees, creatures and biomes. **That was wrong and was actively harmful** — it put weeks of unwanted design work in front of a solo developer who wanted to build the game, not redesign the genre.

  The real boundary is narrower and legal rather than creative: **mechanics are free, assets are not.** Rules and systems — crafting grids, tool tiers, hunger, mob behaviour — are functional designs and reimplementing them is normal and lawful. Textures, models, sounds, music and names are expressive work and must be ours *in what ships*. See `TIMELINE.md` "The End Product" for the table.

  **But do not author art yourself.** Development runs on Mojang's real textures, staged into `blocks-reference/` beside each exe by `tools/make-reference-blocks.ps1` — gitignored, never under `assets/`, never shipped — and the game prefers them whenever they exist. **The user's friend is authoring the original art.** So adding anything visible means adding one row to that script; `assets/` needs a file per layer or the load fails, so give it the cheapest flat-colour placeholder and nothing more. A session was spent hand-drawing food sprites in ignorance of this. `ASSETS-REFERENCE.md` is the full map.

  **Names split in two, and the split matters.** Ordinary words for real things — *sheep*, *cow*, *pig*, *wolf*, *spider*, *stone*, *bread* — are plain English and nobody owns them; use them, and do not invent a synonym to feel safe. Only **coined** names are protectable: *Creeper*, *Enderman*, *Ghast*, *Shulker*, *Blaze*, *Wither*, *Piglin*, *Redstone*. Those need ours. Direct user correction, 2026-08-01: _"sheep will still be called sheep right? i'm not compromising there thats not copy-rightable you can't copyright a sheep."_ An earlier version of this rule said only "names must be ours", which would have forced renaming the sheep for nothing.

  So **"how does Minecraft do it?" is a good question for design as well as engineering.** Ask the user where they want something to differ; never invent divergence for its own sake. Where the project should genuinely exceed the original is *engineering* — the renderer, the threading, the absence of stutter.
- **`engine/` must not know that `game/` exists.** The engine is a library; the game is an executable that uses it. If engine code ever needs to reference a game concept (blocks, chunks, the player), that is a signal the abstraction is in the wrong place — the engine should expose a mechanism and let the game supply the policy. This one rule is what makes it possible to eventually have tools/editors/servers reusing the same engine.
- **Prefer plain data and explicit ownership over inheritance hierarchies.** No `GameObject` base class, no virtual-everything. The moment this becomes a deep class tree, both multithreading and cache performance become impossible to recover.
- **Structure for future multithreading without doing it now.** This is the _only_ concession being made to future threading, and it is non-negotiable from the first chunk milestone (`TIMELINE.md` M4) onward, because it cannot be retrofitted:
  1. **Chunk generation is a pure function of `(seed, chunkCoord)`** — no neighbour reads, no global mutable state, no wall-clock time.
  2. **Meshing is a pure function of `(blocks, neighbour borders)` returning vertex data** — it does not upload to the GPU and does not mutate what it reads.
  3. **Exactly one owner mutates the world**, on the main thread. Workers get copies and return results.

  Threads are the easy part; untangling shared mutable state afterwards is the rewrite. This is also the specific weakness the user wants this engine to beat.
- **The renderer was rewritten at M23, as planned, and is now concrete rather than throwaway.** That was never a failure of the first one — building a deferred HDR pipeline before a single block was on screen would have meant carrying huge complexity, blind, with nothing real to measure against. What survived the rewrite untouched was exactly what was meant to: world data, generation, meshing inputs, persistence, physics and gameplay. Only the vertex format churned, and that churn stayed inside meshing. **Do not now add an `IRenderBackend`, a material abstraction or a render-graph layer.** There is still one backend, the seven passes are concrete on purpose, and the same rule applies as before: no abstraction without a second implementation in sight.
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

**And the counterweight — what "thinking ahead" must NOT become:**

Do not add interfaces, virtual base classes, "manager" objects, template generality, event buses, or configuration hooks for systems that do not exist yet. Speculative structure is not foresight; it is weight that every later milestone has to carry, and it is the single most common way ambitious engine projects die. The habits above are free because they are about *shape*. Abstractions are not free, and they wait until there is a second real case.

---

## Milestone Log

> Three of the four bugs found during M3 were caught by the user playing the build, not by reasoning about the code: the cube reading as a blob, the wide-angle distortion, and inverted backface culling. None produced a validation error or a warning. **M7 repeated the pattern with worse bugs** — a latched trackpad button, and movement fast enough to tunnel through walls at low frame rates — neither of which any amount of code review had surfaced. This is the concrete evidence behind the "ship something playable at every milestone" rule above.

### Milestone 1 — Prove the toolchain (2026-07-30)

**Why this was worth a whole milestone**, since it is the pattern for any future "prove the tech first" slice: on Windows, the C++ + Vulkan toolchain has many independent pieces (compiler, SDK, build backend, driver, loader, validation layers) and any one of them being subtly wrong produces a confusing failure much later. Proving all of them at once, on an intentionally trivial program, means every future bug can be assumed to be in _our_ code.

**Added mid-milestone at user request:** a runtime-adjustable frame cap (see "A user-adjustable frame cap" above). This was scope beyond the original seven requirements and was accepted because it is small, self-contained, and prevents the loop from burning a laptop's battery at 1600 fps during every future milestone.

Results are in `SYSTEM_MEMORY.md`'s validation baseline; environment setup is in "The Machine" above.

---

## Lessons and Gotchas

**Moved to `LESSONS.md`.** There are two hundred and eighty of them, and **this file is
attached to every session while that one is not** - so keeping them here cost roughly forty
thousand tokens on every turn, whatever the task.

**Read `LESSONS.md` before debugging anything in an area you have not touched before**, and
add new entries there rather than here. The handful that have cost the most time are already
repeated in the box at the top of this file.

---

## Update Discipline

Keep this file for **non-obvious lessons, rejected approaches, unresolved problems, and the reasoning behind decisions**. Current mechanics, structure, versions, and commands belong in `SYSTEM_MEMORY.md`.

Update existing bullets in place rather than appending a chronological diary. Do not log routine work — if a change would be obvious to anyone reading the code, it does not belong here. Add an entry when a decision was contested, when something failed in a way that would fool the next person, or when a future session might reasonably undo something on purpose.
