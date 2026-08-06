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

| File | What it is | When |
|---|---|---|
| **`CLAUDE.md`** | Why every settled decision exists, and the rules that come from them. **Auto-attached, opens with a 5-item box.** | Always |
| **`LESSONS.md`** | Every mistake already paid for once, and what it cost. Split out of `CLAUDE.md` so it is *not* auto-attached. | **Before debugging in an area you have not touched** |
| **`SYSTEM_MEMORY.md`** | Current technical truth — structure, build, invariants, sizes | Before touching code |
| **`TIMELINE.md`** | Milestone route and what the finished game is | Before starting work |
| **`TEXTURING.md`** | Box nets, creature models, **§14 is a formula reference** | **Before any texture or model work** |
| **`RESEARCH.md`** | How the original actually works — physics constants, spawn rules, mob behaviour, AI architecture | When designing a system |
| **`ANIMATION.md`** | How the original rigs and animates mobs — pivots, gait maths, head turn | Before any movement or pose work |
| **`INTERFACE.md`** | How the inventory and crafting screens should work — the two cards, tabs, search, the craftable states, and the build order | Before any inventory or crafting UI work |
| `CRAFTABLE.md` | Planned recipes | Crafting work |
| `ASSETS-REFERENCE.md` | Map of the reference asset dump | Art work |

They are large — `CLAUDE.md` and `SYSTEM_MEMORY.md` are the two biggest by a wide margin. **Read the section you need, not the whole file.**

**If you only read two:** `CLAUDE.md`'s opening box and §3 of this file.

---

## 3. Where the project actually is

**Milestones 1–19, 20a, 20c and 20f–20l are done. M20b is in progress. `INTERFACE.md` slices 1–4 are built; 5–9 are not.**

The game is genuinely playable. An endless seeded world streams in; you walk, sprint, sneak, jump, fly and swim; you break blocks on a timer that depends on block and tool, and place them back. There is a 36-slot inventory with full click/drag/shift-click behaviour, a crafting table, a working furnace, ten tools that wear out, torches, and saves that survive a restart.

Underneath: generation and meshing run on worker threads, geometry is greedily merged and frustum culled, and the world has sky light, block light, smooth lighting and ambient occlusion under a placeholder sun.

**Sixty-six block types**, including **eight ores** in depth-banded veins over a deepslate floor on bedrock, glass, flowers, and the stone family. **Eleven resource items** smelt into a metal chain. Ore bands and drop counts are the wiki's, mapped onto our shorter world; the tier they demand is one step lower than the reference's, because we have no iron tier yet.

**Fifty-six creature species** share the world — twenty-two animals, two passive folk (a **villager** and a **wandering trader**), and sixteen hostiles: the **Bramble** (our creeper analogue), **slimes** in three sizes that split when killed, the **spider** and **cave spider**, which climb walls and only hunt in the dark, the **zombie**, **skeleton**, **Blackbone**, **stray**, **bogged**, **zombie villager** and **witch**, the bipeds, the **husk**, the **silverfish**, the **Princepin**, the only hostile that spawns in daylight, and the **drowned**, which walks the seabed. They spawn by biome and light, wander, flee, jump ledges, and two are neutral (wolf and polar bear). A share of every natural group is young, and the population survives a restart.

**The sea is inhabited, as of M20i.** Nine aquatic species and a whole second movement model: **cod**, **salmon**, **pufferfish**, **tropical fish**, **squid**, **glow squid**, **turtle**, **dolphin** and **axolotl**. A swimmer has no gravity and steers in three dimensions; a **pufferfish** inflates through three genuinely different models when you get within 2.5 m; a **squid** jets — its tentacles flare slowly and snap shut, and *the snap is what pushes it*, so it must turn about before it can go the other way; a **glow squid** lights itself; a **turtle** hatches on beach sand and swims only once it reaches the water. Nothing spawns in a puddle: every aquatic needs an ocean biome and three blocks of water depth.

**Aggression is the reference's, as of M20f.** A hostile has to *see* you to notice you — that predicate was missing entirely, which is why everything used to track you through rock. It scans twice a second rather than every frame, forgets a target it has lost sight of on the species' own timer (three seconds for most, **seventeen for a zombie**), notices you at one range and gives up at another (a zombie spots you at **35 m** and gives up at 25), holds a grudge for its own length (a wolf 25 seconds, a polar bear 500), and has to be *facing* you within 90° to land a blow that reaches as far as its own body is wide. Striking one no longer summons the undead: only some species call for help, and the reference's zombies and skeletons are not among them.

**They are properly animated.** Limbs hang from a joint and **rotate** about it rather than sliding, on an eased amplitude that spins up and winds down; heads turn **about the neck** and tip up and down to watch you; bipeds sway their arms when idle. **Stepping up a block eases the drawn body after the box** — for creatures and, since 2026-08-05, for the player's camera too, so a staircase no longer reads as a series of teleports.

**And as of M20j they fight properly.** A hostile closes on you, **stops when it arrives** and swings — it used to walk straight through, overshoot and orbit you, landing a blow on every pass, because a chase had no arrival condition at all. A blow swings the arms on the reference's own curves, and the two rigs run in opposite senses: a zombie starts with its arms out and **chops down**, an armed biped starts with them hanging and **swings up**. Which mobs swing is a **species** fact rather than a rig one — the skeleton family are **archers** and deliberately do not, while the Blackbone does, because it carries a sword. A **slime rebounds off you** like a wall and comes again, which is better than the reference here: Bedrock separates the two bodies with entity pushing, which we have no equivalent of.

**The spider was rebuilt at M20j** from `geometry.spider.v1.8` and `animation.spider.walk`. Its body used to sit low with eight level bars either side of it; the reference droops each leg 45° about its own forward axis from a joint level with the body, and **that droop is the entire reason a spider stands off the ground**. The walk is the reference's exactly — a quarter turn of phase per pair, a sweep at twice the rate of the lift, absolute values on both — and what keeps adjacent leg tips from colliding is the *lift*, not the sweep.

**The Bramble detonates.** A real blast: the reference's 1352-ray block algorithm, an exposure test so cover protects, and damage on the player *and* the population. It stops dead to swell within 2.5 m, needs line of sight for the whole countdown, gives up past 6 m, runs from cats, and a hard landing shortens its fuse. **Charged ones** carry the reference's blue energy shell and twice the power.

**The inventory has a second card.** Five tabs — Construction, Equipment, Items, Nature, Search — over every one of the 126 items in the game, clickable in creative to take a stack, with the empty space around the entries acting as a bin. It **scrolls**, and the row the card cuts through is clipped by a real scissor rectangle rather than left blank. **Fifty-six spawn eggs** are reached from there rather than the hotbar; right-click to place that creature. A **bucket** made from three iron ingots picks water up and puts it back down.

**A furnace faces the way you placed it**, with the mouth on one side and `furnace_side` on the other three. That needed side faces to know which way they point, which nothing in the mesher used to expose — `FaceDirection` now does, and **the crafting table uses it too**: the tooled face on its two Z sides, the plainer one on its X sides, fixed rather than a placement state. **An item icon asks for a direction as well**, because it draws two side faces at once and handing both the same one put a furnace's mouth on the right-hand face of every icon in the inventory.

**Reach is 5 m for blocks and 3 m for entities** (5 in creative), which are Bedrock's mouse values. It was 12 m for a long time — that is Bedrock's *touch creative* number and was simply the wrong row of the table.

### What is in flight right now

- **Fifty-two of the fifty-six creature skins are Mojang placeholders**, as are all 56 spawn egg sprites, the five catalogue tabs and every block and item texture. This is **deliberate and sanctioned** — see §5. Do not "fix" it.
- **Water physics were rebuilt on 2026-08-05** and the old fudge is gone. Three halves, recorded as **M20h**, with `world/Fluid.hpp` and `SYSTEM_MEMORY.md` → "Water" now the owners — `RESEARCH.md` §9.1 was retired to a pointer once the work shipped. **Entities:** water no longer weakens gravity, it replaces the model with the reference's multiplicative drag, so sinking, swimming, sprint-swimming, the plunge after a dive and being carried by a current are all one expression. Breath and drowning exist, dropped items float, and creatures follow the reference's navigation flags — a cow bobs, a zombie walks the seabed, a Princepin drowns. **The block:** falling water is its own block and is full, a cell that can drop never runs sideways (so a waterfall is a column, not a cone), and a weight search sends a stream at the nearest hole within four blocks. Seven blocks of spread sideways, unlimited downward, paced at the reference's five ticks a block. Plants are washed away and dropped rather than damming the flow. The surface plays its thirty-two frame animation, redirected in the fragment shader so no chunk is re-meshed for it. **The look:** real distance fog, not a tint — and **its colour is measured off screenshots, not taken from Bedrock's fog JSON.** `SYSTEM_MEMORY.md` has a do-not-revert box; read it before changing that constant, because the obvious correction is wrong and has been made twice. `world/Fluid.hpp` owns every water constant. **Not done and deliberate:** the directional `water_flow` texture (32×32 against our 16×16 array, and it needs per-cell UV rotation), lava, the swimming *pose*, the reference's eyes-adjusting fog ramp (built, then removed — it restarts on every dip), and the player's bubble bar — which waits for M21 so it can be built beside the heart bar.
- **Mobs no longer spawn inside blocks**, and the loading screen no longer finishes early. Both were 2026-08-05 and both were the same shape of bug: a value derived somewhere other than the table that owns it. A spawn candidate now clears its real body box through the shared collision helpers instead of scanning whole cells, anything that ends up buried climbs out, and the loading bar streams around the player's *saved* position rather than the generator's spawn point — the first log line went from `pending 372` to `pending 0`.
- **M20b is not finished.** Still owed: the **flying** archetype and a death animation. The **water** archetype landed at M20i, the melee behaviour at M20j, and **pathfinding at M20k** — creatures now search a real route with A\* and fall back to the old steering fan when they have none, so a wall is something to walk around rather than press against. `TIMELINE.md` has the list.
- **`INTERFACE.md` slice 5 is next** on the interface side. Slice 4 landed the catalogue's scrolling and, with it, the fix for the wheel silently driving the hotbar while a screen was open.
- **Do not re-tune any water constant by eye — run `tools/simulate-swim.ps1`.** It replays the vertical motion and reads every constant out of the headers, so it cannot drift from the code. Water was corrected twice after M20h and treading took three attempts; `SYSTEM_MEMORY.md` → "Water" explains what each knob does and `LESSONS.md` has the three failed attempts.
- **Nothing damages the player yet.** Blasts and blows apply knockback only, because player health is M21.

### The shape everything else hangs off (M20c)

Creature AI is **not** a state machine. Eight behaviours sit in a `constexpr` table selected by priority with `Move` / `Look` / `Jump` control flags. Two ideas carry it, and both are worth understanding before adding any behaviour:

1. **Control flags.** Rows claiming *disjoint* flags run at the same time; rows claiming the *same* flag are exclusive and resolved by priority. **A row claiming nothing always runs** — that is not a degenerate case, it is how targeting works.
2. **Target production is split from consumption.** Some rows *write* the target; `MeleeAttack` *reads* it and never asks why it is set. Retaliation, pack anger and hunting-on-sight are three rows that never mention each other.

`canStart` and `canContinue` are deliberately different questions — a hunter gives up further out than it engages, and **a sneaking player is only harder to *notice***, never harder to escape from.

`SYSTEM_MEMORY.md` "How a creature decides what to do" has the row table. `RESEARCH.md` §8.8 step 5 (unify the two spawners) is the only part still open.

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

**Keys worth knowing while testing.** `F1`/`F2` frame cap, `F3`/`F4` field of view, `F5` diagnostics overlay, `F6`/`F7` render distance, **`F8` spawns a Bramble 12 m ahead, `F9` a charged one**. Right-click a spawn egg to place that creature; take one from the inventory's left card.

**`creature_showcase` in `settings.cfg` freezes the spawner** and lines the roster up in front of spawn: `1` is every species in a staggered grid, and **`2` and above is one species alone** in three copies — profile, facing the camera, facing away — where the value is the `CreatureKind` index + 2. It is the wrong tool for anything about *behaviour*, because frozen creatures never act; use `F8` for that. **Restore it to `0` when you are done**, like anything else that writes `settings.cfg`.

**`tools/simulate-swim.ps1` replays the vertical water physics on the CPU** and prints the bob height, the period, how far the eyes ride above the surface and how much of the body is out. It reads every constant out of `Fluid.hpp` and `Player.hpp` rather than copying them, so it cannot drift from the code. Use it instead of playtesting any water tuning — `-PoolDepth 1` checks a one-block puddle, `-Sprint` checks sprint-swimming, and `-TryFloatEye`/`-TryStroke`/`-TryTread` sweep a value without editing the header.

---

## 5. The creature-skin situation — read this before you panic

You will find `creatures-reference.png` sitting next to `game.exe`, containing Mojang's art, plus `spawn-eggs/`, `hud-reference.png` and `blocks-reference/` beside it. **Almost every texture you see in the running game is Mojang's.**

**This is fine. It is deliberate, it was the user's decision, and it is temporary.**

The user's own generated skins were rejected on sight, so they asked for Mojang's to go in as placeholders: *"yeah just replace all 9 textures with mojang's, i'll have my friend rework them."*

**They asked an artist friend to take the replacements on (2026-08-03).** There are two phases ahead of her and both take real time: she is **researching how UV texturing works** — how a flat image wraps onto a 3D model — and only after that comes **drawing the art itself**, which is the longer half. Expect weeks, not days. Much of `TEXTURING.md` exists to serve the first phase.

So:

- **Do not re-author the creature skins to help.** `tools/make-roster-skins.ps1` stays as a fallback and as documentation of the nets.
- **Do not lecture the user about it.** They know.
- **Do keep `TEXTURING.md` §13 (net tables) and §14 (formulas) accurate** — that is what she works from, and it is the genuinely useful contribution.
- **New species follow the same arrangement:** model it, prove it against the reference nets, leave the skin. That was confirmed as the standing choice on 2026-08-03.

**The rule that does still stand, precisely:** reference art may be **measured from**, must **never live under `assets/`**, and must **never ship in a release**. Every staging tool *refuses* to write under `assets/`, so it is mechanically enforced rather than remembered. (`SYSTEM_MEMORY.md` → "Creature skins" has which sheet rows are ours and which are covered.)

**Four placeholder sets sit beside the exe, not under `assets/`**, and they follow one arrangement:

| Beside `game.exe` | What | Restored by |
|---|---|---|
| `creatures-reference.png` | 52 of 56 creature skins, plus the charged Bramble's energy shell and the drowned's clothing | `tools/make-reference-creature-atlas.ps1` |
| `spawn-eggs/egg00–45.png` | all 56 spawn egg sprites | `tools/make-spawn-egg-sprites.ps1` |
| `hud-reference.png` | the five catalogue tabs, over a copy of our own HUD sheet | `tools/make-reference-hud.ps1` |
| `blocks-reference/` | 76 block and item textures | `tools/make-reference-blocks.ps1` |

`assets/textures/hud.png` is the **one exception that does live under `assets/`** — Minecraft's widget art, kept there at the user's explicit request, and also to be replaced before release.

`run.ps1` restores all four if they are missing, and **rebuilds each atlas whenever the sheet it was composited from is newer**. Do not remove that — a stale atlas serves old art silently, and it once shipped a mangled sheep through a rebuild, a re-run and a verification pass. A missing egg sprite falls back to blank rather than being skipped, because the list index is the layer index.

**None of this produces a startup warning, deliberately.** Warn about *faults* — a sheet whose dimensions disagree with the code, a missing sprite, a layer constant that has drifted. Never warn about a state the user chose.

**The sequence that produced the HUD atlas is the standing rule for art, not a one-off:** stage the reference first, prove the layout against pixels known to be right, *then* author ours. Authoring art and judging the layout by it at the same time leaves two unknowns behind one symptom — the same trap as debugging a box net and a skin together.

---

## 6. Building a creature model — the procedure that stopped the failures

Three models in a row came out wrong before this was written down. Follow it in order; **each step's failure mode is invisible to the next**.

1. **Alpha runs per row, before island detection.** Touching nets merge into one island and will mislead you. The net arithmetic then falls straight out — see `TEXTURING.md` §14.1.
2. **A band narrower than predicted is information, not an error.** It means one rect is transparent, and *which one* tells you where the box is buried.
3. **Where the arithmetic ties, sample the colour.** This has settled three ambiguities that geometry could not — the cow's udder, the goat's ruff, and whether the frog's two identical nets were head-and-body (they are back-and-belly).
4. **Scan for shared extents before rendering.** Every creature quad is double-sided, so two overlapping boxes sharing any extent will z-fight, and the symptom is a flickering *triangle diagonal* that looks nothing like a modelling error. Fix by **growing the box carrying the artwork** by +0.01, never by insetting the one in front.
5. **Two renders, then your eye.** A third render means the **pose** is wrong, not the numbers. Use `creature_showcase` (§4).

**The trap that has cost the most time:** the reference happily butts boxes together with **coincident faces**, because Minecraft never draws the inward side of a surface. We do. So copying a model's exact offsets reproduces a flicker the original doesn't have — and **resizing the part cannot fix it** (bigger bulges, equal flickers, smaller looks recessed). Match the sizes and *offset* the box a sixteenth of a texel instead. `TEXTURING.md` §14.5b. **Two boxes that merely *touch* have the same problem**, and the shared-extent scan in step 4 does not catch it: overlap them slightly with different `grow` values rather than letting their faces meet.

**And check every UV against the texture's alpha before rendering.** Our reference art is Java's while the geometry is Bedrock's, and for most mobs the two sheets agree — but not all of them. A rect that comes back partly transparent means the origin is wrong; one that comes back empty means the part will silently not exist at all. The dolphin lost its entire back half to exactly this.

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
