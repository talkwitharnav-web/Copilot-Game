# START HERE

You are picking up a **custom C++20 + Vulkan voxel sandbox** built from scratch. Not a mod, not Unity — a real engine. This page exists so you are useful in ten minutes instead of an hour.

Read it all. It is short on purpose.

---

## 1. The five rules that will otherwise cost the user time

They have paid for each of these once already. Do not make them pay twice.

1. **Never run `git commit` or `git push` unless explicitly asked.** Write files, leave them uncommitted. Direct correction: *"undo those commits. who told you to commit."*
2. **Bedrock Edition is the reference, not Java.** Where they differ, Bedrock wins. `RESEARCH.md` marks Java values `[JE]`.
3. **The user is the playtester, and a good one.** Anything needing aim, a held button, or a judgement about how something *feels* goes to them. They have said "I'm a tool" three times, the last in capitals. **Two failed screenshot-harness attempts is the signal to stop** — restore `settings.cfg` and `saves/*/player.dat`, hand over the build, and say what to look at.
4. **When the user names a milestone, build it.** If it is too big, split it *yourself* and ship the first slice. Handing back a recommendation instead of work is the one thing that has genuinely angered them. Bugs are expected and forgiven; not starting is not.
5. **Launch subagents with `model: "Claude Opus 5 (copilot)"`.** Omitting the parameter silently gets a small model and the work comes back thin.

---

## 2. Read these, in this order

| File | Size | What it is | When |
|---|---|---|---|
| **`CLAUDE.md`** | 116 KB | Why every settled decision exists, and every lesson paid for in bugs. **Auto-attached, opens with a 5-item box.** | Always |
| **`SYSTEM_MEMORY.md`** | 97 KB | Current technical truth — structure, build, invariants, sizes | Before touching code |
| **`TIMELINE.md`** | 75 KB | Milestone route and what the finished game is | Before starting work |
| **`TEXTURING.md`** | 46 KB | Box nets, creature models, **§14 is a formula reference** | **Before any texture or model work** |
| **`RESEARCH.md`** | 103 KB | How the original actually works — physics constants, spawn rules, mob behaviour, AI architecture | When designing a system |
| **`ANIMATION.md`** | 66 KB | How the original rigs and animates mobs — pivots, gait maths, head turn | Before any movement or pose work |
| **`INTERFACE.md`** | 24 KB | How the inventory and crafting screens should work — the two cards, tabs, search, the craftable states, and the build order | Before any inventory or crafting UI work |
| `CRAFTABLE.md` | 8 KB | Planned recipes | Crafting work |
| `ASSETS-REFERENCE.md` | 8 KB | Map of the reference asset dump | Art work |

**If you only read two:** `CLAUDE.md`'s opening box and §3 of this file.

---

## 3. Where the project actually is

**Milestones 1–19, 20a and 20c are done. M20b is in progress.**

The game is genuinely playable. An endless seeded world streams in; you walk, sprint, sneak, jump, fly and swim; you break blocks on a timer that depends on block and tool, and place them back. There is a 36-slot inventory with full click/drag/shift-click behaviour, a crafting table, a working furnace, ten tools that wear out, torches, and saves that survive a restart.

Underneath: generation and meshing run on worker threads, geometry is greedily merged and frustum culled, and the world has sky light, block light, smooth lighting and ambient occlusion under a placeholder sun.

**Thirty-six creature species** share the world — eighteen animals, two passive folk (a **villager** and a **wandering trader**), and sixteen hostiles: the **Bramble** (our creeper analogue), **slimes** in three sizes that split when killed, the **spider** and **cave spider**, which climb walls and only hunt in the dark, the **zombie**, **skeleton**, **Blackbone**, **stray**, **bogged**, **zombie villager** and **witch**, the bipeds, the **husk**, the **silverfish**, and the **Princepin**, the only hostile that spawns in daylight. They spawn by biome and light, wander, flee, jump ledges, and two are neutral (wolf and polar bear). A share of every natural group is young, striking one rouses its neighbours, and the population survives a restart.

**They are properly animated.** Limbs hang from a joint and **rotate** about it rather than sliding, on an eased amplitude that spins up and winds down; heads turn **about the neck** and tip up and down to watch you; bipeds sway their arms when idle; and walking up a step rises to the real surface with the drawn body easing after it.

**The Bramble detonates.** A real blast: the reference's 1352-ray block algorithm, an exposure test so cover protects, and damage on the player *and* the population. It stops dead to swell, needs line of sight for the whole countdown, gives up past 7 m, runs from cats, and a hard landing shortens its fuse. **Charged ones** carry the reference's blue energy shell and twice the power.

**Thirty-six spawn eggs** sit in the creative inventory — right-click to place that creature. `F10` swaps them for the block kit, because 36 eggs fill all 36 slots.

### What is in flight right now

- **Thirty-two of the thirty-six creature skins are Mojang placeholders**, as are all 36 spawn egg sprites. This is **deliberate and sanctioned** — see §5. Do not "fix" it.
- **M20b is not finished.** Still owed: stronger pathfinding, the water and flying archetypes, and a death animation. `TIMELINE.md` has the list.
- **The inventory and crafting UI is planned but not started.** `INTERFACE.md` holds the design, the measurements and a nine-slice build order. Slice 1 is a **platform** change — there is no text input in the engine at all, which gates the search bar.
- **Nothing damages the player yet.** Blasts and blows apply knockback only, because player health is M21.

### M20c is done (2026-08-04) — the shape everything else now hangs off

The three-state switch is **gone**. Eight behaviours sit in a `constexpr` table selected by priority with `Move` / `Look` / `Jump` control flags: `Panic`, `HurtByTarget`, `NearestAttackableTarget`, `Swell`, `AvoidFeline`, `MeleeAttack`, `Wander`, `LookAtPlayer`.

Two ideas carry the whole win, and both are worth understanding before adding behaviour:

1. **Control flags.** Rows claiming *disjoint* flags run at the same time; rows claiming the *same* flag are exclusive and resolved by priority. **A row claiming nothing always runs** — that is not a degenerate case, it is how targeting works.
2. **Target production is split from consumption.** `HurtByTarget` and `NearestAttackableTarget` *write* `CreatureTarget`; `MeleeAttack` *reads* it and never asks why it is set. Retaliation, pack anger and hunting-on-sight are three rows that never mention each other.

`canStart` and `canContinue` are deliberately different questions — a hunter gives up further out than it engages, and **a sneaking player is only harder to *notice***, never harder to escape from. That asymmetry is where the reference's rules land naturally.

§8.8 steps 1–4 are done. **Step 5** (unify the two spawners) is untouched and orthogonal; **step 6** is explicitly deferred.

---

## 4. Build and run

```powershell
. .\tools\dev-env.ps1          # once per terminal — the leading dot is required
cmake --build build\release    # or build\debug
.\run.ps1 release              # or debug
```

- **PowerShell 5.1.** Chain with `;`, never `&&`. Never paste multi-line scripts into the terminal — they get mangled.
- **`Start-Process` needs an absolute path** (`"$PWD\build\release\bin\game.exe"`), or it fails with a confusing "cannot find the file".
- **Debug build with zero Vulkan validation errors is the acceptance bar.** Run it before calling anything done.
- **Never benchmark on Debug** — the debug CRT serialises the heap and the numbers are not just noisy, they are non-monotonic. Repeat any timing at least three times and report the minimum.
- **Close the game with `CloseMainWindow()`, never a force-kill** — the save path runs after the window closes.
- **Anything that writes `settings.cfg` or a save must restore it.** A benchmark once left `frame_cap=0` behind and the user reported the limiter broken. Twice.

**Keys worth knowing while testing.** `F1`/`F2` frame cap, `F3`/`F4` field of view, `F5` diagnostics overlay, `F6`/`F7` render distance, **`F8` spawns a Bramble 12 m ahead, `F9` a charged one**, **`F10` swaps the creative inventory between the spawn eggs and the block kit**. Right-click a spawn egg to place that creature.

**`creature_showcase` in `settings.cfg` freezes the spawner** and lines the roster up (value = `CreatureKind` index + 2 for one species alone). It is the wrong tool for anything about *behaviour*, because frozen creatures never act — use `F8` for that. **Restore it to `0` when you are done.**

---

## 5. The creature-skin situation — read this before you panic

You will find `creatures-reference.png` sitting next to `game.exe`, containing Mojang's art, plus `spawn-eggs/`, `hud-reference.png` and `blocks-reference/` beside it.

**This is fine. It is deliberate, it was the user's decision, and it is temporary.**

The user's own generated skins were rejected on sight, so they asked for Mojang's to go in as placeholders: *"yeah just replace all 9 textures with mojang's, i'll have my friend rework them."*

**They asked an artist friend to take the replacements on (2026-08-03).** There are two phases ahead of her and both take real time: she is **researching how UV texturing works** — how a flat image wraps onto a 3D model — and only after that comes **drawing the art itself**, which is the longer half. Expect weeks, not days. Much of `TEXTURING.md` exists to serve the first phase.

So:

- **Do not re-author the creature skins to help.** `tools/make-roster-skins.ps1` stays as a fallback and as documentation of the nets.
- **Do not lecture the user about it.** They know.
- **Do keep `TEXTURING.md` §13 (net tables) and §14 (formulas) accurate** — that is what she works from, and it is the genuinely useful contribution.
- **New species follow the same arrangement:** model it, prove it against the reference nets, leave the skin. That was confirmed as the standing choice on 2026-08-03.

**The rule that does still stand, precisely:** reference art may be **measured from**, must **never live under `assets/`**, and must **never ship in a release**. The atlas tool refuses to write under `assets/`, so it is mechanically enforced. Rows 0–191 of the sheet (sheep, cow, pig, Bramble) are ours and final; everything above row 767 has no art of ours at all.

**Three placeholder sets sit beside the exe, not under `assets/`**, and they follow one arrangement:

| Beside `game.exe` | What | Restored by |
|---|---|---|
| `creatures-reference.png` | 32 of 36 creature skins, plus the charged Bramble's energy shell | `tools/make-reference-creature-atlas.ps1` |
| `spawn-eggs/egg00–35.png` | all 36 spawn egg sprites | `tools/make-spawn-egg-sprites.ps1` |
| `hud-reference.png` | the five catalogue tabs, over a copy of our own HUD sheet | `tools/make-reference-hud.ps1` |
| `blocks-reference/` | 36 block and item textures | `tools/make-reference-blocks.ps1` |
| `assets/textures/hud.png` | the widget sheet — **the one exception that does live under `assets/`**, at the user's request | hand-authored |

`run.ps1` restores all three if they are missing, and **rebuilds each atlas whenever the sheet it was composited from is newer**. Do not remove that — a stale atlas serves old art silently, and it once shipped a mangled sheep through a rebuild, a re-run and a verification pass. A missing egg sprite falls back to blank rather than being skipped, because the list index is the layer index.

**The sequence that produced the HUD atlas is the standing rule for art, not a one-off:** stage the reference first, prove the layout against pixels known to be right, *then* author ours. Authoring art and judging the layout by it at the same time leaves two unknowns behind one symptom — the same trap as debugging a box net and a skin together.

---

## 6. Building a creature model — the procedure that stopped the failures

Three models in a row came out wrong before this was written down. Follow it in order; **each step's failure mode is invisible to the next**.

1. **Alpha runs per row, before island detection.** Touching nets merge into one island and will mislead you. The net arithmetic then falls straight out — see `TEXTURING.md` §14.1.
2. **A band narrower than predicted is information, not an error.** It means one rect is transparent, and *which one* tells you where the box is buried.
3. **Where the arithmetic ties, sample the colour.** This has settled three ambiguities that geometry could not — the cow's udder, the goat's ruff, and whether the frog's two identical nets were head-and-body (they are back-and-belly).
4. **Scan for shared extents before rendering.** Every creature quad is double-sided, so two overlapping boxes sharing any extent will z-fight, and the symptom is a flickering *triangle diagonal* that looks nothing like a modelling error. Fix by **growing the box carrying the artwork** by +0.01, never by insetting the one in front.
5. **Two renders, then your eye.** A third render means the **pose** is wrong, not the numbers. `creature_showcase` in `settings.cfg` freezes the spawner and lines the roster up (value = `CreatureKind` index + 2 for a single species).

**The trap that has cost the most time:** the reference happily butts boxes together with **coincident faces**, because Minecraft never draws the inward side of a surface. We do. So copying a model's exact offsets reproduces a flicker the original doesn't have — and **resizing the part cannot fix it** (bigger bulges, equal flickers, smaller looks recessed). Match the sizes and *offset* the box a sixteenth of a texel instead. `TEXTURING.md` §14.5b.

**The metrics are evidence, not proof.** A deliberately flattened pig once *beat* the correct one on every measure, and the user spotted four faults in seconds. The table catches gross deviation; the eye is the gate.

---

## 7. The one bug shape that keeps recurring

> **A value derived somewhere other than the one table that owns it.**

Block shapes, creature nets, sheet dimensions, knockback — each has exactly one home. Every bug in this project that "compiled and looked plausible but was subtly wrong" traces back to a second copy.

It bites hardest when you *widen* what something can be: adding slabs broke collision resolution, the fluid update, the targeting outline and the raycast, one at a time, because each had its own idea of a block's shape. **When widening an invariant, grep for the old literals** — the compiler cannot help, because nothing changed type.

---

## 8. Things that are settled — do not re-litigate

- **C++20, Vulkan, GLFW, CMake + Ninja, FetchContent.** Each was chosen deliberately; `CLAUDE.md` has the reasoning.
- **No multiplayer, ever.** It is a design freedom, not a limitation — do not add abstractions "in case".
- **`engine/` must never reference `game/`.**
- **Generation is a pure function of `(seed, chunkCoord)`; meshing a pure function of its input volume. Only the main thread mutates the world.** This is what makes the threading work and cannot be retrofitted.
- **The renderer is deliberately not future-proofed** and gets rewritten at M23. Do not add abstraction layers before then.
- **Mechanics may follow Minecraft freely; assets must be ours.** Ordinary animal names (sheep, wolf, frog) are free; only *coined* names need replacing — which is why the creeper is a **Bramble**.

---

## 9. Talking to the user

They are an experienced vibe coder but **not** a C++ or graphics programmer. Use the correct technical term, then immediately explain it in plain English — they have asked for this explicitly, as a way of learning over time. Do not substitute vague language, and do not drown them in theory either.

They think in outcomes and feel. Translate that into a technical plan yourself rather than asking them to specify one.

**When they report a bug, default to "this is real and I haven't found it yet."** Three of the four bugs at M3 were found by them playing, not by reading code. None produced a warning.
