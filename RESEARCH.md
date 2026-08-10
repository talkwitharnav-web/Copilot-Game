# RESEARCH.md

How Minecraft actually works, measured and written down, organised around **what this project has already built or is about to build**. This is a lookup table for engineering decisions — `TIMELINE.md` decides what we do and when.

> **Bedrock Edition is the primary reference.** Where the two editions differ, the Bedrock value is given first and Java is marked `[JE]`. This was a direct instruction (2026-08-03) and it matters more than it looks: Bedrock has no attack cooldown, no entity cramming, softer creeper explosions, different hitboxes on half the roster, and a **data-driven AI architecture** that is a far better model to copy than Java's hardcoded goal classes.

**Why this file exists.** `CLAUDE.md` settles the licensing boundary: *mechanics are free, assets are not.* Rules and systems are functional designs; reimplementing them is normal and lawful. So "how does the original do it?" is a legitimate engineering question, and the answer is worth writing down once rather than re-derived under pressure at 2am.

**How to read it.** Every section that maps onto something we have ends with a **▶ Where we stand** note comparing our implementation to the reference. Those notes are the useful part; the numbers are the evidence.

**The standing caution, which has already cost us once.** Copying a *mechanism* needs its own justification, not just fidelity. M19a shipped biome-tinted foliage purely because the reference does it, and the result was pale sage-grey plants the user rejected on sight. Match the reference where it makes the game better; diverge deliberately where it does not, and write down which.

---

## Table of contents

| § | Topic |
|---|---|
| 1 | [Entity physics — the tick model and every constant](#1-entity-physics) |
| 2 | [Combat — damage, knockback, invulnerability, targeting](#2-combat) |
| 3 | [Health, hunger and food](#3-health-hunger-and-food) |
| 4 | [Mob spawning and despawning](#4-mob-spawning-and-despawning) |
| 5 | [Mob drops, breeding and babies](#5-mob-drops-breeding-and-babies) |
| 6 | [Mob behaviour — every species we have](#6-mob-behaviour--the-species-we-have) |
| 7 | [Mob behaviour — every species we could add](#7-mob-behaviour--what-we-could-add) |
| 8 | [The Bedrock AI architecture, and how to steal it](#8-the-bedrock-ai-architecture) |
| 9 | [Blocks, fluids and plants](#9-blocks-fluids-and-plants) |
| 10 | [Mining, tools, smelting, XP](#10-mining-tools-smelting-xp) |
| 11 | [Crafting and recipes](#11-crafting-and-recipes) |
| 12 | [Light](#12-light) |
| 13 | [Time, weather and sky](#13-time-weather-and-sky) |
| 14 | [Ticking, chunks and persistence](#14-ticking-chunks-and-persistence) |
| 15 | [World generation — climate, terrain, surface, caves, features](#15-world-generation) |
| 16 | [Priority list — what to take first](#16-priority-list) |

---

# 1. Entity physics

## 1.1 The tick model

Minecraft integrates at a **fixed 20 ticks/second**; one tick = 0.05 s = 50 ms. Almost everything is timed in tick counts, not wall clock, so a slow server makes the *game* slower rather than choppier. If the loop falls more than **2 seconds** behind, the accumulated debt is discarded rather than repaid.

Every entity performs exactly **three operations per tick**, and **the order differs per entity type and is behaviourally significant**:

```
1. Apply acceleration   (gravity)
2. Apply drag           (multiply velocity by a coefficient)
3. Update position      (add velocity to position)
```

For a living entity in air the order is **Position, Acceleration, Drag**:

```java
position += velocity;
velocity.y -= 0.08;
velocity.x *= 0.91;  velocity.y *= 0.98;  velocity.z *= 0.91;
```

Collision resolution is folded into "update position": the movement vector is line-clipped against block collision boxes, stopping at the first box rather than crossing it. The source clips **Y first, then X, then Z**, and if horizontal movement was blocked it retries the whole move raised by `step_height`, keeping whichever produced more horizontal distance.

**Closed-form velocity** after `t` ticks, initial velocity $v_0$, acceleration $a$, drag $d$:

$$v(t) = d^{t}\left(v_0 - \frac{a}{1-d}\right) + \frac{a}{1-d} \quad\text{(drag before acceleration)}$$

$$v(t) = d^{t}\left(v_0 - \frac{d\,a}{1-d}\right) + \frac{d\,a}{1-d} \quad\text{(drag after acceleration)}$$

**Terminal velocity:** $v_{\text{term}} = \dfrac{-a}{1-d}$ (drag first) or $\dfrac{-d\,a}{1-d}$ (drag last).

## 1.2 Converting tick-space to delta time — the formulas we actually need

**This is the single most useful table in the document for us,** because our engine integrates on delta time and every published Minecraft constant is per-tick.

| Quantity | Conversion | Worked example |
|---|---|---|
| Velocity | $v_{\text{m/s}} = v_{\text{b/t}} \times 20$ | 3.92 b/t → 78.4 m/s |
| Acceleration | $a_{\text{m/s}^2} = a_{\text{b/t}^2} \times 400$ | 0.08 b/t² → **32 m/s²** |
| **Drag over Δt** | $v \mathrel{*}= d^{\,\Delta t / 0.05}$ | exact at Δt = 0.05, stable at any frame rate |
| Drag per second | $d^{20}$ | 0.98 → 0.6676/s; 0.91 → 0.1517/s |
| Continuous damping (match decay) | $k = -20\ln d$ | 0.98 → 0.40405 s⁻¹ |
| Continuous damping (match terminal) | $k = (1-d)/(0.05\,d)$ | 0.98 → 0.40816 s⁻¹ |
| Time constant | $\tau = -0.05/\ln d$ | — |

**Recommended integrator for us:** keep the *multiplicative* drag and raise it to $d^{\Delta t/0.05}$. It is exact at 50 ms and frame-rate independent everywhere else. The two continuous $k$ candidates differ by under 1%; pick the terminal-matching one if terminal velocity matters more than the transient.

**Response times** — how fast the exponential settles, useful for tuning feel:

| Motion | Effective drag/tick | τ | 95% of top speed |
|---|---|---|---|
| Horizontal, ground (friction 0.6) | 0.546 | 0.083 s | ~0.25 s |
| Horizontal, ice (friction 0.98) | 0.892 | 0.435 s | ~1.30 s |
| Horizontal, air | 0.91 | 0.530 s | ~1.59 s |
| Vertical, any living entity | 0.98 | 2.475 s | ~7.4 s |

**Negligible-speed cutoff:** velocity components below **0.003 b/t (0.06 m/s)** are zeroed. This is not cosmetic — raising the cutoff from 0.005 to 0.003 in 15w45a changed the player's jump height from 1.2492 to 1.2522 blocks. Port the physics without it and your apex differs in the third decimal.

## 1.3 The per-entity constant table

| Entity class | Tick order | Gravity (b/t²) | Vert. drag | Horiz. drag | Terminal (m/s) |
|---|---|---|---|---|---|
| **Players, mobs, armour stands** | Pos, Acc, Drag | **0.08** | **0.98** | **0.91** † | **78.4** |
| …with Slow Falling | " | 0.01 | 0.98 | 0.91 | 9.8 |
| **Falling blocks, primed TNT** | Acc, Pos, Drag | **0.04** | 0.98 | — | 39.2 |
| **Dropped items** | Acc, Pos, Drag | **0.04** | 0.98 | **0.98** ‡ | **39.2** |
| **Experience orbs** | Acc, Pos, Drag | **0.03** | 0.98 | 0.98 | **29.4** |
| Minecarts | Acc, Pos, Drag | 0.04 | 0.95 | 0.95 § | 15.2 |
| Boats | Acc, Drag, Pos | 0.04 | none | 0.90 | ∞ |
| Eggs, snowballs, ender pearls | Acc, Drag, Pos | 0.03 | 0.99 | 0.99 | 59.4 |
| Thrown potions | " | 0.05 | 0.99 | 0.99 | 99.0 |
| Fireballs, wither skulls | Acc, Drag, Pos | 0.10 | 0.95 | 0.95 | 38.0 |
| **Arrows, thrown tridents** | Pos, Drag, Acc | **0.05** | **0.99** | 0.99 | **100.0** |
| Llama spit | Pos, Drag, Acc | 0.06 | 0.99 | 0.99 | 120.0 |

† On the ground, horizontal drag is **0.91 × block friction**.
‡ Items use a **constant 0.98 horizontal drag whether airborne or grounded** — not friction-modified. A dropped item slides much further than a mob would, and has no "rest" state; it simply asymptotes to zero.
§ Minecarts also clamp horizontal velocity to 0.4 b/t before moving.

**Block friction:** default **0.6**, slime **0.8**, ice/packed/frosted **0.98**, blue ice **0.989**.

## 1.4 Player movement — derivation of 4.317 m/s

Ground acceleration per tick is

$$A = S \cdot \frac{0.216}{f^{3}} \cdot |\mathbf{i}|$$

where $S$ is the final `movement_speed` attribute (player base **0.1**), $f$ is block friction, and $\mathbf{i}$ is the input vector (player impulses are pre-scaled by **0.98**, and normalised to length 1 only if the magnitude exceeds 1). The $0.216 = 0.6^3$ normalises so that $f = 0.6$ gives $A = S$.

Steady state, with drag applied to position first:

$$v_\infty = \frac{A}{1 - 0.91 f}$$

Forward walk on normal ground: $A = 0.1 \times 0.98 = 0.098$, so $v_\infty = 0.098 / 0.454 = 0.21586$ b/t → **4.3172 m/s**. ✓

This reproduces every published figure exactly:

| Case | S | \|i\| | f | m/s |
|---|---|---|---|---|
| Walk forward | 0.1 | 0.98 | 0.6 | **4.317** |
| Walk diagonal | 0.1 | 1.0 (normalised) | 0.6 | **4.405** |
| Sprint (S ×1.3) | 0.13 | 0.98 | 0.6 | **5.612** |
| Walk on ice | 0.1 | 0.98 | 0.98 | **4.157** |
| Walk on slime | 0.1 | 0.98 | 0.8 | **3.040** |
| Sneak forward | 0.1 | 0.294 | 0.6 | **1.295** |
| Sneak diagonal | 0.1 | 0.4158 | 0.6 | **1.832** |

Note **sneaking diagonally is 41% faster than sneaking forward**, because the sneak-scaled diagonal magnitude (0.4158) does not exceed 1 and so is never normalised. That is a genuine bug-shaped behaviour, not a design.

**Air acceleration** is a flat constant instead: **0.02** walking, **0.026** sprinting. With 0.91 air drag that gives 4.356 / 5.662 m/s terminal air speed.

The wiki's rule of thumb "blocks/s ≈ attribute × 43" is exactly 43.17 for a forward-walking player on friction 0.6. **Do not apply it to mobs** — mob AI applies its own speed modifier on top, so a 0.23-attribute zombie is slightly *slower* than a walking player, implying ~×17–19.

## 1.5 Jumping

`jump_strength` = **0.42 b/t** (horse 0.4–1.0, donkey/mule/llama 0.5). The recurrence:

$$v(t) = 0.98\big(v(t-1) - 0.08\big), \qquad v(1) = 0.42 + 0.1\,E_{\text{JumpBoost}}$$

Closed forms (blocks and ticks, downward negative):

$$v(t) = 0.98^{\,t-1}\big(v(1) + 3.92\big) - 3.92$$
$$d(t) = d(0) + 50\big(v(1)+3.92\big)\big(1 - 0.98^{\,t}\big) - 3.92\,t$$

| | v(1) | Apex tick | Apex height |
|---|---|---|---|
| No effect | 0.42 | 6 | **1.2522** |
| Jump Boost I | 0.52 | 7 | 1.8361 |
| Jump Boost II | 0.62 | 8 | 2.5168 |

Total airtime for a base jump ≈ **12 ticks (0.6 s)**.

**Sprint-jump** adds a horizontal impulse of **0.2 b/t** in the facing direction on the jump tick. That is what makes sprint-jumping average 7.127 m/s and clear 4 blocks horizontally (5 under a 2-block ceiling).

Mobs that jump as part of normal movement: **rabbits, frogs, slimes, magma cubes**, plus foxes and spiders when attacking. Mobs with step height ≥ 1 that walk up full blocks without jumping: horses and variants, iron golems, endermen, llamas, ravagers, turtles.

## 1.6 Step height and collision

`step_height` default **0.6**. Camel **1.5**; creaking 1.0625; **1.0** for axolotl, donkey, drowned, enderman, frog, horse, iron golem, llama, mule, ravager, turtle, skeleton/zombie horse, copper golem. Armour stand 0.

**Sneaking prevents drops higher than `step_height`** — it uses the same attribute, not a separate constant.

> **▶ Where we stand (2026-08-04).** Both halves are built and they are **per species**, not global. `stepHeight` defaults to 0.6 and is **1.0** for horse, mule, donkey, llama and frog and **1.5** for the camel, exactly as above — those five never jump, because their step already clears a full block, which is §1.5's own split.
>
> Everything else jumps `1.2522` m, derived from `jump_strength` 0.42, and it is real physics: the launch speed is whatever reaches that height under creature gravity, so the arc, the airtime and the landing all fall out. **The steering fan had to change with it** — it rejected any heading blocked at foot height, so creatures routed *around* every hill and the jump could never fire. It now accepts anything clearable by a step or a jump.
>
> Hoppers are the exception that proves it: a rabbit's ordinary hop reaches 0.34 m, so it nudged into every ledge forever. A hop that meets something it cannot clear is boosted to the same 1.2522 m and is otherwise untouched.
>
> Still missing: the horse family's per-individual randomised jump strength, and the goat's 5-block ram leap.

**Bedrock has an explicit push-out procedure** that Java lacks:
1. Move along a cardinal axis by the shortest amount that resolves the collision with the current block only.
2. Check the new position is free of additional solid boxes.
3. If free, move there (a falling block *always* moves); otherwise abort.
4. If occupied, **the entity is permanently exempted from this mechanic for the rest of its lifetime.**

## 1.7 Entity-versus-entity collision — the architectural fact

> When two bounding boxes overlap, **a small force proportional to the distance between them pushes each entity away horizontally. There is no vertical collision at all.**

Entity–entity collision in Minecraft is a **soft horizontal separation impulse, not a hard constraint**. That is why mobs can be stacked in one block by dropping them in from above, and why entity cramming had to be added as a separate damage mechanic. Items and XP orbs are exempt entirely — they collide only with blocks (and with boats/shulkers).

**Entity cramming is Java-only.** `maxEntityCramming` default 24; entities beyond the limit in one 1 m³ grid-aligned cube take **6 HP with a 25% chance per tick**.

## 1.8 Item entity physics

| Property | Value |
|---|---|
| Gravity | 0.04 b/t² (16 m/s²) |
| Drag | 0.98 both axes |
| Hitbox | 0.25 × 0.25, eye 0.2125 |
| **Pickup box** | player hitbox **+1 block horizontally** (≤1, inclusive) and **±0.5 vertically** (<0.5, exclusive) |
| Pickup delay | **10 ticks (0.5 s)** normally; **40 ticks (2 s)** if thrown by a player |
| Despawn | **6000 ticks (5 min)** of being in an entity-ticking chunk; **the timer pauses when the chunk stops ticking** |
| Merge radius | 0.5 × 0.25 × 0.5 box; the larger stack absorbs |
| Merge interval | every **40 ticks**, or every **2 ticks** while crossing a block boundary |

Two facts worth having:

- **Dropped items do not bounce.** They come to rest by drag alone. (XP orbs became bouncy in 1.21.4; items did not.)
- **The rotation and bobbing are purely cosmetic — "the item itself does not actually rotate or bob up and down."** Multiple items can be picked up in the same tick, and the item never physically moves toward the player; the fly-in is animation only, which is why items can be collected through thin blocks.

**Experience orbs** attract from **7.25 blocks**, speeding up as they approach, and are collected **one at a time at 10 orbs/second** regardless of how many are in range. Values are drawn from {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477} by greedy change-making.

## 1.9 Hitbox table (Bedrock first)

| Entity | H × W | Baby H × W |
|---|---|---|
| Player standing / sneaking / swimming / sleeping | 1.8×0.6 / 1.5×0.6 / 0.6×0.6 / 0.2×0.2 | — |
| Chicken | **0.8 × 0.6** `[JE 0.7×0.4]` | 0.4 × 0.3 |
| Rabbit | **0.6 × 0.49** `[JE 0.5×0.4]` | 0.4 × 0.24 |
| Frog | **0.5 × 0.55** `[JE 0.5×0.5]` | tadpole is a separate mob |
| Cat / ocelot | 0.7 × 0.6 | 0.35 × 0.3 |
| Wolf | **0.8 × 0.6** `[JE 0.85]` | 0.4 × 0.3 |
| Pig | 0.9 × 0.9 | 0.45 × 0.45 |
| Sheep / goat | 1.3 × 0.9 | 0.65 × 0.45 |
| Cow | **1.3 × 0.9** `[JE 1.4]` | 0.65 × 0.45 |
| Llama | 1.87 × 0.9 | 0.935 × 0.45 |
| Horse / mule | 1.6 × **1.4** `[JE 1.3965]` | 0.8 × 0.7 |
| Donkey | **1.6** × 1.4 `[JE 1.5×1.3965]` | 0.8 × 0.7 |
| Camel | **2.375 × 1.7** (sitting 0.945 × 1.7) | 1.1875 × 0.85 |
| Creeper | **1.8 × 0.6** `[JE 1.7]` | — |
| Spider | 0.9 × **1.4** | — |
| Enderman | **2.9** × 0.6 | — |
| Iron golem | 2.7 × 1.4 | — |
| Ghast | 4.0 × 4.02 | 0.95 × 0.95 |
| Item | 0.25 × 0.25 | — |
| XP orb | **0.25 × 0.25** `[JE 0.5]` | — |

Bounding boxes are axis-aligned and **do not rotate with the entity**. No mob has a depth distinct from its width.

## 1.10 Fall damage

$$\text{damage} = \max\!\Big(0,\ \big\lfloor (\text{fallDistance} - \texttt{safeFallDistance}) \times \texttt{fallDamageMultiplier} \big\rfloor\Big)$$

Fall damage is based on **change in Y, not on speed**. Defaults: `safe_fall_distance` **3**, `fall_damage_multiplier` **1**.

| Entity | Safe distance | Multiplier |
|---|---|---|
| Player, most mobs | 3 | 1 |
| Camel, donkey, horse, llama, mule, skeleton/zombie horse | **6** | **0.5** |
| Fox | 5 | 1 |
| **Goat** | — | takes **10 HP less**, always |
| **Frog** | — | takes **5 HP less**, always |

**The 23 vs 23.5 discrepancy is an ordering artefact worth understanding:** a 23-block fall *should* kill a full-health player, but 23.5 is actually required, because **the game checks whether the player is on the ground and computes damage before updating fall distance.** In a delta-time port, the order of "am I grounded?" → "compute damage" → "accumulate distance" is directly observable.

**Fully resets fall distance:** cobweb, water (if contacted before the ground), powder snow, ladder/vine area, slime block, sweet berry bush, scaffolding while sneaking, wind charge hit, mounting mid-fall, ender pearl teleport (which itself deals 5 HP), chorus fruit, minecart landing on rails, mace smash.

**Reduces it:** lava (−50% of accumulated distance per tick), hay bale and honey block (→20% damage), bed (→50% distance), Resistance (−20%/level), Feather Falling.

**Traps:** falling onto the *top* of a ladder does not reset; horses are unaffected by ladders and vines; **Levitation sets ΔY to 0 while active**, so distance is measured from where it expires; pointed dripstone **doubles** fall damage; a rider accumulates no ΔY of its own but takes the mount's fall damage.

**Fully immune:** blaze, breeze, creaking, ender dragon, ghast, magma cube, phantom, shulker, vex, wither, allay, bat, **bee, cat, chicken**, copper golem, iron golem, **ocelot, parrot**, snow golem.

## 1.11 Water and lava

| Mode | m/s |
|---|---|
| Swimming at surface | 2.20 |
| Swimming partially submerged | 1.97 |
| Sprint-swimming | 3.918 |
| Sprint-swimming + Depth Strider III | 5.305 |
| Walking on the bottom, head submerged | 1.960 |
| Walking in lava | 0.784 |
| Sinking rate | 0.39 |

Internal constants (approximate, and they do **not** exactly reproduce the measured figures — use the m/s targets and tune): in-water drag ≈ **0.8** (0.9 sprint-swimming), in-water acceleration ≈ **0.02**, downward acceleration while submerged ≈ **0.005 b/t²** (gravity/16). In lava, drag ≈ 0.5, downward acceleration ≈ 0.02.

**Current:** flowing water pushes at **≈1.39 m/s**. The horizontal current is the **vector sum of flows in and out across the four horizontal neighbours**, giving **16 possible directions** (multiples of 22.5°). Lava pushes too (since 1.16), including entities immune to it, and reduces horizontal speed 50% / vertical 20%.

**Drowning:** breath meter **15 s (300 ticks)**; damage begins at −20 at **2 HP/s**; air regenerates at 1 bubble per 4 ticks. Mobs get 15 s underwater, 16 s before damage.

> **▶ Where we stand.** We integrate on delta time with a constant gravity, resolve collision one axis at a time (vertical first) exactly as the reference does, and have a step height of 0.6 that matches the default. Our `kMaxDropHeight` of 3.0 matches the reference's safe fall distance — arrived at independently.
>
> Three things to take, in order of value:
> 1. **The $d^{\Delta t/0.05}$ drag conversion** (§1.2). We currently have no drag at all; velocity is set rather than accumulated. Adding multiplicative drag would give us ice, slime blocks and water for free later.
> 2. **The soft horizontal separation impulse** (§1.7). Our creatures pass through each other. One impulse proportional to overlap, horizontal only, is a dozen lines and it is what makes a herd look like a herd rather than a stack.
> 3. **The item pickup box and 5-minute despawn with a paused timer** (§1.8). We have pickup; we do not have a despawn timer, so a long session accumulates drops forever.
>
> One thing we already got right the hard way: the reference confirms **item bobbing and rotation are cosmetic only**. `CLAUDE.md` records us learning this by shipping a physical hover that made drops bounce forever.

---

# 2. Combat

## 2.1 Attack cooldown — Java only

**Bedrock has no attack cooldown.** Every swing does full damage; there is no charge meter. This is the single biggest combat divergence between editions and it means **if we follow Bedrock, we skip this entire system.**

`[JE]` For completeness: cooldown $T = 20/s$ ticks where $s$ is `attack_speed` (default 4). With $t$ ticks since the last attack, $p = \operatorname{clamp}((t+0.5)/T, 0, 1)$, and the base damage multiplier is $0.2 + 0.8p^2$ (range 0.2–1.0). Enchantment damage scales linearly by $p$ instead. Critical hits, sweep attacks and sprint-knockback all require ≥ **84.8%** charge.

## 2.2 Attack damage — Bedrock

| Item | Wood | Gold | Stone | Copper | Iron | Diamond | Netherite |
|---|---|---|---|---|---|---|---|
| **Sword** | 5 | 5 | 6 | 6 | 7 | 8 | 9 |
| **Axe** | 4 | 4 | 5 | 5 | 6 | 7 | 8 |
| **Pickaxe** | 3 | 3 | 4 | 4 | 5 | 6 | 7 |
| **Shovel** | 2 | 2 | 3 | 3 | 4 | 5 | 6 |
| **Hoe** | 3 | 3 | 4 | 4 | 5 | 6 | 7 |
| Fist / other | 1 | | | | | | |

`[JE]` differs materially: swords are 1 lower across the board, **axes are stronger than swords** (7–10) but much slower, and hoes always deal 1. Bedrock axes are *weaker* than swords and hoes match pickaxes. Since we follow Bedrock, **swords are the best weapon and the tiering is clean** — which is also the more intuitive design.

**Critical hits: ×1.5.** Conditions (Bedrock): the player must be falling, not on the ground, not in water, not on a ladder/vine/cobweb, not riding, not flying, and without Slow Falling/Blindness/Levitation. `[JE]` additionally requires ≥84.8% charge and not sprinting.

**Damage calculation order** (Bedrock): base + effects + enchantments + armour → **then ×1.5 if critical**. `[JE]` applies the crit before enchantments, which is why Bedrock crits are dramatically stronger on enchanted gear.

## 2.3 Knockback

The code-level algorithm:

1. Knockback strength starts at **0**; +1 per Knockback enchantment level; sprint-knockback adds its own.
2. **Knockback speed = strength ÷ 2.**
3. Horizontal direction follows the **attacker's viewpoint**, not the geometric vector between hitboxes.
4. Living target: speed is scaled by `knockback_resistance`.
5. **The target's horizontal speed is halved, then knockback is applied.**
6. If the target is on the ground, its **vertical speed is halved** and upward knockback applied, **capped at 0.4 b/t (8 m/s)**.
7. After resolving, **the attacker stops sprinting and its horizontal speed drops to 60%**.

Measured: a base melee hit moves the target **1.552 blocks horizontally and 0.8125 vertically**. Each Knockback level adds **2.586 blocks**; the vertical becomes a flat **1 block** at any level.

**Bedrock: airborne knockback is identical to grounded.** `[JE]` applies horizontal only to airborne targets. Entities in vehicles or riding mounts receive **no knockback at all**.

`knockback_resistance` is a pure scale, horizontal only, and does not affect explosions. Iron golem/shulker/warden/ender dragon **100%**; ravager **75%** (`[JE]` 70%); hoglin/zoglin **60%**; squid 85%; zombie family **0–5% rolled at spawn**; each netherite armour piece +10%.

## 2.4 Invulnerability — the rule that makes melee work

**After any damage, an entity is invulnerable for 10 game ticks (0.5 s).** During that window:

> Any damage **less than or equal to** the original is **ignored**. Any **higher** damage deals only **the difference**.

Worked example: hit for 7, then hit for 12 while invulnerable → the second deals **5**, total 12. **The timer is not reset.** The comparison is made *before* armour, enchantments and effects.

An attack that fails this test does **not**: reduce weapon durability, apply knockback, apply Fire Aspect or sweep, anger neutral mobs, produce crit particles, or add exhaustion.

**Consequence: single-target DPS is capped at 2 hits/second regardless of attack speed.** This is the number that makes melee survivable and it is the first thing to implement when player health lands.

## 2.5 Targeting and aggro

`follow_range` is the radius in which a mob acquires and keeps a target:

| Range | Mobs |
|---|---|
| 100 | Ghast |
| 64 | Enderman |
| 48 | Allay, bee, blaze |
| 40 | Llama, wither |
| 35 | Zombie family |
| 32 | Fox, pillager, ravager, creaking |
| 24 | Breeze, warden |
| 20 | Polar bear |
| **16** | **everything else** — creeper, skeleton, spider, slime, silverfish, witch |
| 12 | Evoker, vindicator, piglin brute |

**The detection-range modifier** is multiplicative and cumulative:

| Condition | Multiplier |
|---|---|
| Target is a **sneaking player** | **× 0.8** |
| Invisible, no armour | × 0.07 |
| Invisible, *p* armour pieces | × 0.175 × *p* |
| Wearing the matching mob head | × 0.5 |

The effective range is $\max(\text{range} \times \text{modifier},\ 2.0)$ — so **a mob always detects anything within 2 blocks** regardless of sneaking or invisibility. And crucially, the modifier is **not applied once a mob already has the target in memory**: sneaking helps you avoid being noticed, not to escape.

`[JE]` sensors run **once per second (20 ticks)**, with the first execution offset by a random amount so they do not all fire on the same tick.

**Line of sight** is required for most acquisition, and mobs cannot see through **ice, glass, tall grass or glass panes** — semi-transparent blocks block sight. Exceptions: zombies see villagers through walls; spiders and silverfish track through blocks; endermen track through walls once angered.

## 2.6 Reach

| Type | Survival | Creative |
|---|---|---|
| Bedrock block | 5 | 5 |
| Bedrock entity | 3 | 5 |
| Bedrock touch input | 6 | 12 |
| `[JE]` block / entity | 4.5 / 3 | 5 / 5 |

**For entities, reach is bounding-box intersection, not a distance check.** The attacker's collision box is expanded by its reach value; a melee attack occurs when that expanded box intersects the target's box, with clear line of sight required. **Hitbox size therefore matters directly** — a wide mob effectively reaches further.

**Most mobs perform one melee attack per second.** Players have unlimited attack frequency and greater reach than most mobs, "allowing them to continuously attack targets without being reached."

> **▶ Where we stand.** **Taken verbatim on 2026-08-05**: 5 m blocks in every mode, 3 m entities in survival and 5 in creative. The game had shipped **12 m** since M7, which is this table's *touch creative* row — a different input mode entirely, and picking the wrong row is exactly the kind of error that survives because the number looks plausible. **What the aim ray hits is now the creature's own box** (2026-08-06): it used to be a 0.6 m sphere at the body's middle, which is most of a chicken and a fraction of a camel — so a camel's legs could not be struck at all, only a band around its belly. Padded by 0.15 m, because `halfWidth` here is deliberately narrower than the model. Reach itself is still a distance along that ray rather than the attacker's box expanded.

## 2.7 Difficulty scaling

If a mob deals **D** on Normal:

| Difficulty | Player takes |
|---|---|
| Peaceful | 0 |
| Easy | $\min(D,\ 0.5D + 1)$ |
| Normal | $D$ |
| Hard | $1.5D$ |

**A mob attacking another mob always deals its Normal damage regardless of world difficulty.** That is a clean separation worth copying: scale only in the player-damage path.

**Regional difficulty** (0.00–6.75) is a per-chunk scalar driving *quality* scaling — gear, enchantment level, door-breaking chance — not raw damage:

```
if Peaceful: RD = 0
DaytimeFactor = 0                        if totalDaytime < 3 days
              = (ticks - 72000)/5760000  if between
              = 0.25                     if > 63 days
ChunkFactor   = inhabitedTicks / 3600000, capped at 1.0
if Easy or Normal: ChunkFactor *= 0.75
MoonFactor    = min(moonPhase * 0.25, DaytimeFactor)
ChunkFactor  += MoonFactor
if Easy: ChunkFactor *= 0.5
RD = 0.75 + DaytimeFactor + ChunkFactor
if Normal: RD *= 2
if Hard:   RD *= 3

CRD = 0                if RD < 2
    = (RD - 2)/2       if 2 ≤ RD ≤ 4
    = 1                if RD > 4
```

Chunk inhabited time is **cumulative across players** and capped at 50 hours. CRD is **always 0 on Easy** and always ≥ 0.125 on Hard.

> **▶ Where we stand (2026-08-06, extended 2026-08-08).** We have a melee swing with knockback and creatures that compute a blow and hand it back as a `CreatureAttack` rather than applying it — exactly the shape needed when player health arrives. **M21 supplied it**, so those blows now land: the same `CreatureAttack` goes straight into `damagePlayer` and through §2.4's overwrite rule, and blast damage does the same. Creative immunity rides in on `PlayerInput::invulnerable` rather than by the survival code knowing what mode the game is in.
>
> **M20j added the half that was missing, and it was not a number — it was a destination.** A chase had no arrival condition at all: `MeleeAttack` steered at the player for as long as it had a target, nothing here collides with the player, so a zombie walked *through*, overshot and came about at a limited turn rate. That is an orbit, and reach being a distance put the player inside it on every pass. **The reference does not fix this with a turn rate or a reach; `melee_box_attack` paths to a node *beside* its target and the path ends there.** Anything that closes on something has to say where it stops, and **"it stops when it can hit" is not the same distance as "it stops when it arrives"**.
>
> Two exemptions fall out of the reference rather than being invented. A **hopper** never stops, because a slime has no melee goal at all — `attack.melee` fires on contact, so arriving *is* the attack. And **"arrived" means on the same footing**, tested against the species' own `stepHeight`, or a zombie at the foot of a one-block ledge stands there forever: the step-up and the jump both read `walking`, which the arrival had just cleared.
>
> **A slime rebounding off the player is ours, and it is better than the reference here.** Bedrock separates the two bodies with `minecraft:pushable`, which we have no equivalent of, so a slime passed through and re-entered forever. The player's body is a wall: the inward part of the slime's velocity comes back out at a restitution, and it is lifted clear of the floor. It needs **no cooldown**, because after it fires the slime is heading away and the test cannot pass again — and it must fire on **contact rather than on the blow**, since a small slime does no damage in the reference and a wall does not care.
>
> Take, in order:
> 1. **The 0.5 s invulnerability window with the overwrite rule** (§2.4). Without it, a creature standing inside you kills instantly.
> 2. **The "halve existing velocity, then apply, cap vertical at 0.4 b/t" knockback recipe** (§2.3). We already learned the hard way that knockback must be *assigned, never accumulated* — the reference agrees, and adds the halving step that makes repeated hits feel controlled rather than launching.
> 3. ~~**`follow_range` as a per-species number with a ×0.8 sneak modifier and a 2-block floor** (§2.5).~~ **Done at M20f**, along with `must_see`, a scan interval, a forget timer and a separate leash range. See §6 and `SYSTEM_MEMORY.md` "What it takes to be noticed".
> 4. **Bounding-box-intersection reach rather than a distance check** (§2.6), the moment we have a mob wider than one block. The camel is already 1.7 wide.
>
> Skip: the attack cooldown (Bedrock has none), and regional difficulty (it is quality scaling for gear we do not have).

---

# 3. Health, hunger and food

## 3.1 Health and damage

Max health **20 HP** (10 hearts). Natural regeneration: **0.5 HP every 4 s** when food ≥ 18, costing **6 exhaustion per HP healed**. **Saturation healing:** at food = 20 with saturation remaining, heal 1 HP every 0.5 s at a cost of 1.5 saturation.

| Source | Damage | Reduced by armour? |
|---|---|---|
| Fall | 1 HP per block past 3 | no |
| Drowning | 2 HP/s | no |
| Fire (in a fire block) | 1 HP per 0.5 s | no |
| Soul fire | 2 HP per 0.5 s | no |
| Burning outside fire | 1 HP/s | no |
| Lava | 4 HP per 0.5 s | no |
| Cactus | 1 HP per contact tick | yes |
| Suffocation | 1 HP per 0.5 s | no |
| Starvation | 1 HP per 4 s | no |
| Void | instant | no |

Starvation stops at **10 HP on Easy, 1 HP on Normal, and kills on Hard**.

## 3.2 Hunger — three values, one visible

- **`foodLevel`** — the 20-point bar.
- **`foodSaturationLevel`** — hidden, capped at the current food level, spent *before* food.
- **`foodExhaustionLevel`** — hidden accumulator. Each time it reaches **4.0** it resets and removes 1 saturation, or 1 food if saturation is already 0.

| Action | Exhaustion |
|---|---|
| Walking | **0** (removed in 1.11) |
| Sprinting | 0.1 per metre |
| Swimming | 0.01 per metre |
| Jumping | 0.05 |
| Sprint-jumping | 0.2 |
| Breaking a block | 0.005 |
| Landing an attack | 0.1 |
| Taking damage | 0.1 |
| **Natural regeneration** | **6.0 per HP** |

Gates: **sprinting requires food > 6**; **regeneration requires food ≥ 18**; **food = 0 starves**.

**The design insight worth internalising: hunger is not a food timer, it is a tax on healing and hurrying.** Walking is free. Sprinting 100 blocks costs 30 exhaustion = 7.5 food. Healing ten hearts costs 60 exhaustion = 15 food — by far the most expensive thing a player can do.

## 3.3 Food

| Food | Hunger | Saturation |
|---|---|---|
| Cooked porkchop, steak | 8 | 12.8 |
| Golden carrot | 6 | 14.4 |
| Cooked chicken / mutton / salmon | 6 | 9.6 |
| Bread, baked potato, cooked cod/rabbit | 5 | 6.0 |
| Golden apple | 4 | 9.6 |
| Apple | 4 | 2.4 |
| Rotten flesh | 4 | 0.8 (80% Hunger effect) |
| Carrot | 3 | 3.6 |
| Raw beef / porkchop / rabbit | 3 | 1.8 |
| Melon slice | 2 | 1.2 |
| Raw chicken | 2 | 1.2 (30% Hunger) |
| Potato | 1 | 0.6 |

Saturation is the number that decides how long a food lasts, and it is invisible — which is exactly why cooked meat feels so much better than its hunger number suggests.

## 3.4 Armour

$$\text{reduction}\% = \min\!\left(80,\ \max\!\left(\frac{\text{armour}}{5},\ \text{armour} - \frac{4 \times \text{damage}}{\text{toughness} + 8}\right)\right)$$

The second term is what makes armour weaker against big hits.

| Material | Helm | Chest | Legs | Boots | Total | Toughness |
|---|---|---|---|---|---|---|
| Leather | 1 | 3 | 2 | 1 | 7 | 0 |
| Golden | 2 | 5 | 3 | 1 | 11 | 0 |
| Chainmail | 2 | 5 | 4 | 1 | 12 | 0 |
| Copper | 2 | 4 | 2 | 1 | 9 | 0 |
| Iron | 2 | 6 | 5 | 2 | 15 | 0 |
| Diamond | 3 | 8 | 6 | 3 | 20 | 2 |
| Netherite | 3 | 8 | 6 | 3 | 20 | 3 |

Armour loses **1 durability per 4 HP absorbed**, minimum 1.

> **▶ Where we stand (2026-08-08).** **Built as M21.** The prediction above held: exhaustion rode in on hooks that already existed — block breaking, attacking, jumping, sprinting, taking damage — and hunger really is one float and a handful of increments. `world/Survival.hpp` is the single owner of every number in this section, header-only so nothing had to be registered to add it.
>
> Taken verbatim: twenty hearts, twenty hunger, the half-second invulnerability window **with the overwrite rule** (§2.4's worked example reproduces exactly), the exhaustion cost of each action, four exhaustion to one leg of the bar, regeneration at 18 food, the saturated fast-regeneration path at half-second intervals, sprinting refused below 6, starvation stopping at half a heart on Normal, three free blocks of fall at one point a block, and the whole food table — thirty-three items with a hunger and a saturation value each.
>
> **Not taken:** armour, potions, difficulty levels other than Normal, and experience — each is its own system and none is a survival number. **Divergence:** eating is 1.6 seconds held, which is the reference's own figure, but there is no eating *animation* yet because the first-person arm does not exist.
>
> **What it caught:** twelve foods were edible and worth nothing, three of them (rotten flesh, cooked rabbit, cooked salmon) listed in §3.3 above.

---

# 4. Mob spawning and despawning

## 4.1 Bedrock spawning

- **Spawn shell:** 24–44 blocks spherical at simulation distance 4; **24–128 blocks** at sim distance ≥6, additionally restricted to ~96 blocks horizontally.
- **Attempt rate:** a **1.5/2000 chance per chunk per tick** of the algorithm running.
- **Light:** most Overworld monsters cannot spawn if sky light ≥ 7 **or** block light > 0. Most animals need combined light ≥ **7** (`[JE]` requires block light ≥ 9).
- **Global mob cap 200** (ticking mobs only).
- **Population caps per 9×9-chunk region**, Overworld: Monster **8 surface / 16 cave**; Pillager 8/8; **Animal 4 surface**; Ambient 2 cave.
- **Per-mob density caps:** creeper 5 surface, phantom 5, drowned 5 ocean / 2 river, cod 20, dolphin 5.
- Spawn probability once caps are known: `(densityCap − currentCount) / densityCap`.
- Requirements: a full solid top surface below; feet in a non-blocking block; bounding box clear of solids.

**Cluster spawning** runs in two stages — surface mobs, then cave mobs — picking a random X/Z in the chunk and searching downward for the first solid-top block with a spawnable block above.

`[JE]` differs structurally: a spawn cycle every tick per eligible chunk, packs of up to 4 (8 for wolves, 6 for horses/donkeys) with members offset ±5 by a **triangular distribution**, no player or world spawn within a **24-block sphere**, within **128 blocks** of a player, and a **Monster cap of 70** scaled as `70 × chunks ÷ 289` over a 17×17 chunk square, plus a per-player cap.

## 4.2 Spawning at chunk generation — `[JE]` only, and the biggest gap for us

When a new chunk is generated it may generate an initial set of "creature"-category mobs **ignoring the mob cap entirely**. All randomness derives from the world seed plus chunk position, so it is fully reproducible. The biome sampled is the one at the chunk's north-west corner.

Each biome carries a **creature spawn probability**: 0.03 badlands, 0.04 wooded badlands, 0.07 ice spikes and snowy plains, **0.10 everything else**. The generator loops — roll against that probability, and if it passes, pick a weighted entry, roll a pack size, and place it; each subsequent iteration is less likely.

**This is why the reference world feels inhabited the moment you generate it**, and why animals appear in places the player has never been.

Group sizes for species we have: sheep 4 (weight 12), cow 4 (8), pig 4 (10), chicken 4 (10), rabbit 2–3 (4–12 by biome), horse 2–6, donkey 1–3, llama 4–6, goat 1–3, camel 1, **frog 2–5 (weight 10)**, **wolf 1–8 by biome**.

## 4.3 Despawning — Bedrock

- **>128 blocks** from the nearest player (or in a simulation-distance edge chunk) → **immediate**. At sim distance 4 the threshold is 44 blocks.
- **>32 blocks** away with no damage taken for **30 seconds** → **1/800 chance per tick** (2.47%/s). Half the population is gone after ~28 s.
- Everything, persistent or not, despawns at **Y ≤ −128**.
- **Bedrock despawns almost all mobs, including animals.** `[JE]` most passive mobs never despawn. This is a real design difference — it is why Bedrock worlds feel emptier of livestock over time.

**Persistence** is granted by: being named, ridden, tamed, bred, leashed, summoned, picking up an item, or spawning as part of a structure or raid.

> **▶ Where we stand (2026-08-04).** We now run **both** of the reference's spawn systems. The continuous cycle fires every 2.5 s in a **24–44 m** ring around the player — the reference's own shell at its lowest simulation distance — held to a cap of 14, and retires past 90 m. The **chunk-generation pass** places a chunk's own group the first time the player comes within two chunks of it, ignoring the cap — a pure function of `(seed, chunkCoord)`, so a chunk always produces the same herd, and it fits our generation purity rule with no threading consequences. Which species and how many come from `groupSize` and `weight` in the species table; ten percent of chunks get anything at all, matching the reference's default probability.
>
> **A candidate must clear its own body box** (`overlapsSolid` on the real `bodyBox`), not a column of whole cells — the old test was blind to slabs, fences and anything wider than one cell, and dropped bulky species straight into the terrain beside them. Both spawners also refuse a column that has not finished loading, since absent ground reads as air.
>
> **Persistence has landed too** — `creatures.dat`, written on save.
>
> What is still missing: **separate caps per category.** We have one number for everything, so a night of hostiles can crowd out the animals. Bedrock's split (Monster 8 surface / 16 cave, Animal 4 surface, per 9×9 chunks) is the model. And our 90 m retirement is abrupt where the reference thins a population out — "30 s undisturbed, then 1/800 per tick" — so ours pop out rather than drifting away.

---

# 5. Mob drops, breeding and babies

## 5.1 Drop rules

- **Common drops** appear at the moment of fatal damage, each with a *drop range* — a uniform distribution. A cow's `0–2 leather` therefore yields nothing 1/3 of the time.
- **Baby animals drop no common drops at all.**
- **Meat is dropped cooked if the animal died on fire.**
- **Rare drops** normally require a player kill (or a tamed wolf kill). Base **2.5%**, +1 pp per Looting level. The rabbit's foot is the exception at **10%**, +3 pp per level.
- **Looting** adds `round(uniform(0, level))` extra items, which makes middle values twice as likely.
- **XP orbs only drop if the mob dies within 5 seconds (100 ticks)** of being hit by a player or a player's wolf.

## 5.2 The table for our roster

| Species | XP | Common drops |
|---|---|---|
| Sheep | 1–3 | 1 wool if unsheared; 1–2 raw mutton (shearing gives 1–3 wool) |
| Cow | 1–3 | 0–2 leather; 1–3 raw beef |
| Pig | 1–3 | 1–3 raw porkchop |
| Chicken | 1–3 | 0–2 feather; 1 raw chicken; also lays an egg every 5–10 min |
| Rabbit | 1–3 | 1 raw rabbit; 0–1 rabbit hide; rabbit's foot at 10% |
| Cat | 1–3 | 0–2 string |
| Horse / donkey / mule / llama | 1–3 | 0–2 leather |
| Camel | 1–3 | **nothing** |
| Goat | 1–3 | **nothing** (goat horn when it rams a solid block) |
| Frog | 1–3 | **nothing** |
| Wolf | 1–3 | **nothing** |
| Creeper (our Bramble) | 5 | 0–2 gunpowder |

## 5.3 Breeding and growth

| Mechanic | Value |
|---|---|
| Food required | **1 item per parent** |
| Pathfind-to-partner range | **8 blocks** |
| "Kissing" duration | ~2.5 s |
| XP dropped | 1–7 (llama 1–8) |
| Parent cooldown | **5 minutes** |
| Love-mode timeout with no partner | 30 s, then immediately re-feedable |
| Baby growth | **24000 ticks = 20 minutes** |
| Feeding a baby | **−10% of *remaining* time** per feed |
| Any two adults | Can breed, **including parent × child** |

**Naturally spawned groups include babies:** 5% for most farm animals, **10% wolf and llama**, **20% horse/donkey/mule**, **25% cat (Bedrock)**. Babies have a smaller body, an oversized head, a **faster walk speed**, and sounds pitched **50% faster and +6 semitones**.

Breeding foods: wheat (cow, sheep, goat), carrot/potato/beetroot (pig), seeds (chicken), carrot/golden carrot/dandelion (rabbit), golden carrot or golden apple (horse family), hay bale (llama), **slimeball (frog)**, any meat (wolf, cat), cactus (camel).

**Tempt ranges** are per species and worth having: sheep/cow/pig 6 blocks, chicken a **6×4×6 box**, **rabbit 8 blocks (approaches slowly)**, **goat 10**, frog 6, stray cat 10, wolf 8. Break-off distance is **≥16 blocks in Bedrock**, ≥10 in `[JE]`.

> **▶ Where we stand (2026-08-04, updated 2026-08-08).** Creatures drop loot, and since **M21** the meat has somewhere to go: thirty-three foods carry a hunger and a saturation value, and eating is a held 1.6-second action. The drop range including zero was taken as recommended below.
>
> The detail worth stealing early is the **drop range including zero**. `0–2 leather` feels like a roll; a flat "always 1" feels like a vending machine. It costs one line.
>
> **Babies have landed.** `babyChance` per species carries the reference's rates — 5% most farm animals, 10% wolf and llama, 20% the horse family, 25% cat — and `Creature::scale` multiplies the model and the collision box together, with a slightly quicker walk. Breeding, growth and the 20-minute timer are not built; a baby stays a baby.

---

# 6. Mob behaviour — the species we have

Bedrock values. `[JE]` marks Java differences.

## 6.1 Master table

| Species | HP | Speed attr | Behaviour | Baby % |
|---|---|---|---|---|
| Sheep | 8 | 0.23 | Passive | 5% |
| Cow | 10 | 0.2 | Passive | 5% |
| Pig | 10 | 0.25 | Passive | 5% |
| Chicken | 4 | 0.25 | Passive | 5% |
| Rabbit | 3 | 0.3 | Passive | — |
| Goat | 10 | 0.2 | **Neutral** | 5% |
| Camel | **32** | — | Passive, always tame | — |
| Frog | 10 | **1.0** | Passive | — |
| Cat | wild 10 / tamed 20 | 0.3 | Passive | **25%** |
| Wolf | wild 8 / **tamed 40** | 0.3 | **Neutral** | 10% |
| Horse | 15–30 | 0.1125–0.3375 | Passive | 20% |
| Donkey | 15–30 | **0.175 fixed** | Passive | 20% |
| Mule | 15–30 | 0.175 | Passive | 20% |
| Llama | 15–30 | 0.175 | **Neutral** | 10% |
| Creeper | 20 | 0.25 | Hostile | — |

**The speed attribute is not m/s.** Treat it as a relative ranking and calibrate against the player's 4.317 m/s walk. A 0.23 sheep is not 9.9 m/s.

## 6.2 Per-species behaviour worth implementing

**Sheep.** Grazes: on a random tick it eats a grass block (turning it to dirt) or eats tall grass, and this is what **regrows sheared wool**. The eat animation takes **1.8 s**. Colour is decided by biome temperature with weights; breeding mixes parent colours as if they were dyes. `[JE]` a sheep named `jeb_` cycles through colours.

**Cow.** The only notable quirk is in follow-parent: **a calf switches to a different adult cow if it gets more than 24 blocks from its parent**, and prefers adults over a wheat-holding player.

**Pig.** Rideable with a saddle and steerable with a carrot on a stick. Struck by lightning it becomes a zombified piglin.

**Chicken.** Lays an egg every **5–10 minutes**. **Falls slowly — it flaps and never takes fall damage.** In Bedrock, 15% of nearby baby zombies/husks/drowned will ride a chicken as a jockey.

> **▶ Built 2026-08-04.** The slow fall and the flapping are in, and they are one mechanism rather than two: `fallDrag` scales a descent by the reference's 0.6 a tick, and the wing beat is driven by the same airborne state. Terminal descent is **1.95 m/s** against the 60 everything else reaches, which is why no fall-damage exemption is needed — it never lands hard. **The factor is re-solved for our timestep**, because `v = (v - g·dt)·k` settles somewhere different when a frame is not a tick; the form used comes out at exactly 0.6 when it is. Chicks are covered for free, since `scale` never enters into it.
>
> The wings needed a **roll axis on `uprightBox`** — the first rotation in the model system about the forward axis rather than the side one — and they hinge at the shoulder, because rotating a box about its own centre swings the tip out and the root *into* the body. Still missing: laying eggs, and the jockey.

**Rabbit.** Hops rather than walks. Variants are biome-coloured. **Avoidance ranges are precise:** flees wolves within 8 blocks (`[JE]` 10) and players within 4 blocks (`[JE]` 8). Eats mature carrot crops. `[JE]` has the Killer Bunny.

**Goat — neutral, not passive.** **Rams**: picks a target, backs off, charges, and knocks it back hard. Cooldown is **30–300 s** for adults (**2–7 minutes** in the wild). It **jumps up to 10 blocks horizontally and 5 vertically**, ignores 10 HP of fall damage, and **drops a goat horn when it rams a solid block** (there are 8 horn variants). It explicitly refuses to target Creative players.

**Camel.** **Sits and stands** — standing takes ~2.8 s during which it cannot move. Seats **two riders**. Has a **dash**: a burst forward with a cooldown. Its **height is a defence** — most melee mobs cannot reach a player mounted on a camel, which is a genuinely novel mechanic. Idle animation is purely visual.

**Frog.** **Jumps far and high, preferring lily pads and big dripleaf, and generally jumping to places higher up.** Takes **5 HP less** fall damage. Its attack is a **tongue** that pulls small slimes and magma cubes into its mouth, killing them instantly — a magma cube produces a **froglight whose colour depends on the frog variant**. Breeding is turtle-like: a pregnant frog seeks water and lays **frogspawn**, which hatches after 3–10 minutes into **2–5 tadpoles**, which take 20 minutes to become frogs — and **the variant is decided by the biome the tadpole grows up in, not by its parents.**

**Cat.** Tamed with raw cod/salmon. Sits autonomously on chests, beds and furnaces. **Sleeps on the player and leaves a gift in the morning.** **Creepers and phantoms flee cats within 6 blocks / 16 blocks respectively** — the cat is a defensive structure, not a pet.

**Wolf — the full neutral model.** Nine colour variants by biome, seven sound variants. Three states: **untamed** (neutral to the player, **hostile on sight to sheep, rabbits, foxes, baby turtles and skeletons**), **angry**, and **tamed**. Attacking one angers **every wild wolf in a 33×21×33 box** — but **not if the blow kills it in one hit**, a deliberate rule so clean kills don't start a war. Untamed max health 8; tamed 40. Deals **4 HP on Normal** to players (difficulty-scaled) and a flat 4 to other mobs. **Its tail height encodes its health**, and an angry wolf holds its tail straight out. It attacks by **running at the target and leaping, dealing no damage while airborne**. Tamed wolves teleport to their owner beyond 12 blocks.

**Horse family.** Taming is by repeated mounting until temper is reached. Speed, jump strength and health are **randomised per individual and inherited by breeding** — offspring stats are drawn from both parents plus a random third value. Donkeys and mules carry chests.

**Llama — neutral.** **Spits** at attackers from range. Forms **caravans** following a leader. Strength (1–5) decides inventory size and is inherited. Wolves avoid llamas — and **a llama that spits on a wolf is always attacked in return**.

## 6.3 Creeper — our Bramble's source

| Stat | Bedrock |
|---|---|
| Health | 20 |
| Hitbox | **1.8 × 0.6** (`[JE]` 1.7) |
| Speed | 0.25 |
| Follow range | 16 |
| XP | 5 |

1. **Chases any player within 16 blocks**, reduced to **14 if sneaking**.
2. Within **3 blocks**: stops, hisses, flashes, expands.
3. **Fuse = 30 ticks = 1.5 s.**
4. **It only explodes with uninterrupted line of sight for the whole countdown.** With no LOS it will not even start hissing, at point-blank range, while being hit.
5. Cancels if the player leaves the **7-block blast radius** (regardless of difficulty), LOS breaks, or it is knocked out of range. The fuse resets.
6. **Fall-triggered detonation:** `swell += fallDistance × 1.5`, capped at `fuse − 5` — so a creeper falling more than 16 blocks explodes ~5 ticks after landing. Creepers deliberately jump toward a player if the fall is survivable.
7. **Flees cats and ocelots within 6 blocks, and moves faster fleeing than pursuing** — but will not flee once the countdown has begun.

**Explosion maths:**

```
impact = (1 − distance / (2·power)) · exposure
damage = 7 · power · (impact² + impact) + 1
```

`exposure` is the fraction of rays from the centre to a grid on the entity's bounding box not blocked by blocks. **The trailing +1 means every entity within 2·power takes at least 1 damage even if fully shielded.** Creeper power 3 (max entity radius 6, block radius 5.1); charged 6.

Point-blank damage, **Bedrock**: 14.75 / 27.5 / 41.25 by difficulty. `[JE]` is much harsher: 22.5 / 43 / 64.5.

> **▶ Where we stand.** **Built 2026-08-04.** The Bramble detonates: `world/Explosion.hpp` carries the 1352-ray block algorithm, the exposure test and the damage formula above, and `blastResistance` is kept strictly apart from mining hardness. Damage is scaled by `27.5 / 43` to land on Bedrock's point-blank figure rather than Java's.
>
> **Its stats were wrong in four places until they were checked against this section**, all of them defaults or guesses that looked plausible: health 10 against 20, follow range 14 against 16, and the two swell distances. **Run speed** went 2.4 → 4.25, converting the 0.25 attribute at the low end of §1.4's documented ×17-19 band, which finally makes the "can be strolled away from" note below untrue.
>
> **The two swell distances then went wrong a second time, in the opposite direction.** They were "corrected" from 2.5 / 6 to the 3 / 7 this section quotes, on the reasoning that this section is the Bedrock record. It is not, on this point: the numbers above came from the wiki, and `Mojang/bedrock-samples` ships the actual behaviour pack. `behavior_pack/entities/creeper.json` reads `behavior.swell { start_distance: 2.5, stop_distance: 6 }`, and its `target_nearby_sensor` says `inside_range: 2.5, outside_range: 6` — two independent statements of the same pair. **2.5 and 6 are Bedrock's; 3 and 7 are the wiki's, and almost certainly Java's.** Restored 2026-08-05. Do not flip them again without opening that file.
>
> Also built: the **line-of-sight requirement for the whole countdown**, **fall-triggered ignition** (§6.3 point 6 - 1.5 ticks of fuse per block, stopping five ticks short), and **fleeing cats and ocelots** within 6 m at ×1.2 speed, sitting *below* Swell in priority so a lit fuse is not called off by a passing cat. §2.5's detection modifier is in too: ×0.8 for a sneaking target with a 2 m floor, applied **only on acquisition**, which falls out of the behaviour table's `canStart`/`canContinue` split for free.
>
> **Entity damage was wired a step late**: the maths existed and only the player was ever run through it, so the first playtest had creepers levelling terrain around a completely unharmed goat. `Creatures::applyExplosion` now damages and throws the population too.
>
> The player takes **knockback but no damage**, because there is no player health until M21. Still missing: jumping toward a player when the fall is survivable, and charged creepers dropping mob heads.
>
> **Built since:** slimes in three sizes with real splitting, and the spider with wall-climbing and `huntsBelowLight`. Night is no longer one creature. The slime's straight-line no-pathfinding behaviour is the reference's own and we inherit it for free, since our steering fan is only applied to walkers.
>
> Our wolf already has neutrality, the tail-as-mood signal, and the leap-shaped attack posture. What it does not have is **pack anger propagation** and **hunting other species** — and pack anger is the same machinery as the herding M20b already owes. **One mechanism, two owed features.** The one-hit-kill exemption is three lines and prevents a clean kill starting a war.
>
> The frog's real behaviour — **preferring to jump onto higher ground and onto lily pads** — is a target-selection rule, not a movement one, and would sit naturally on top of the ballistic hop we already built.

---

# 7. Mob behaviour — what we could add

## 7.1 Archetype A — land quadrupeds *(reuses everything we have)*

> **Built since this was written:** fox, ocelot, polar bear and panda are in the game, and the verdict held — zero new movement code, zero new physics. What each cost was a measured net and a placement pass. Turtle, armadillo, panda variants, sniffer and hoglin remain.

| Species | HP | W × H | Speed | Attack | Group | Breed item |
|---|---|---|---|---|---|---|
| Fox | 10 | 0.6 × 0.7 | 0.30 | 2 | 2–4 | Sweet berries |
| Panda | 20 (weak 10) | 1.3 × 1.25 | 0.15 (lazy 0.07) | 2 (aggressive 6) | 1–2 | Bamboo |
| Polar bear | 30 | 1.4 × 1.4 | 0.25 | 6 | 1–2 | **cannot breed** |
| Armadillo | 12 | 0.7 × 0.65 | 0.14 | — | 4 | Spider eye |
| Turtle | 30 | 1.2 × 0.4 | 0.25 | — | 2–6 | Seagrass |
| Hoglin | 40 | 1.4 × 1.4 | 0.30 | 3–8 | 3–4 | Crimson fungus |
| Sniffer | 14 | 1.9 × 1.75 | 0.10 | — | egg only | Torchflower seeds |
| Ocelot | 10 | 0.6 × 0.7 | 0.30 | 3 (chickens only) | 1–2 | Raw cod |

**What the engine would need: nothing new in movement. Everything new is state.**

1. **Per-entity persistent state that survives save/load** — home position (turtle), trusted-player set (fox, ocelot), gene pair (panda), scute timer (armadillo). Our creatures need only position/velocity/health today; this is the real cost.
2. **A behaviour-state enum per species**, not one shared AI: the handful of rows we have becomes …/Sleep/Sit/Roll/Dig/Stalk/Pounce/LayEgg/PlayDead.
3. **Per-state shapes.** A rolled armadillo, a sleeping fox and a pregnant turtle (+0.2 height) all change the model and sometimes the hitbox. We learned at M20 that overlap and resolution must read **one shape table** — that table now needs a *per-state row*.
4. **AABB region queries** — "all adults in 41×21×41", "spiders within 6". A broadphase we can query by box.
5. **An arcing jump impulse** (fox pounce ~4 blocks) — exactly the rabbit hop we already built.

**Verdict: cheapest by far. Zero new movement code, zero new physics.**

## 7.2 Archetype B — flying

Bat, parrot, allay, phantom, ghast, bee, breeze. Speeds 0.6–0.7 attribute, but **the phantom reaches 20 blocks/s** — flying code multiplies far harder than walking code.

**What the engine would need:**

1. **A second movement controller** — a 3D target point, steering toward it, **no gravity and no ground-contact requirement**. This is the single biggest unavoidable cost.
2. **3D pathfinding or 3D steering.** Bat/parrot/bee/allay are all "hover near a point and wander" and are covered by **steering plus a box clearance probe**. Only ghast and phantom need real clearance-aware 3D pathing.
3. **A landing/perching state** — the *transition* between controllers is the awkward part, not the flying.
4. **Large-hitbox handling.** The **ghast is 4×4×4**, and its tentacles are deliberately outside the hitbox — render and collider decoupled.
5. **Sky spawn placement** — phantoms spawn in a box **28 blocks above the player's head**, not at a surface cell.

**Note: bees don't actually fly by Minecraft's own definition — they hover, like bats and parrots. Only the ghast and ender dragon truly fly.**

## 7.3 Archetype C — swimming ✅ **built, M20i (2026-08-05)**

Squid, glow squid, dolphin (speed 1.2), cod/salmon/tropical fish/pufferfish, axolotl, tadpole, guardian.

> **▶ Where we stand.** Nine of these shipped at M20i, plus the drowned. What was
> actually needed against the list below: **(1)** yes, and `world/Fluid.hpp` from
> M20h already had it. **(2)** no buoyancy at all — a swimmer simply has
> `has_gravity: false`, which is what the reference does; see §9.1's navigation flag table. **(3)** the
> cheat, exactly as recommended — a heading in three dimensions with no pathing.
> **(4)** yes, and it is *three* clocks rather than two: `breathesAir` runs the
> ordinary air counter backwards so a fish suffocates in air, and `dryOutSeconds`
> is separate again, so a dolphin drowns if held under **and** dries if kept out.
> **(5)** `groupSize`, already in the species table, was enough — no boids.
> **(6)** taken as advised: five axolotl liveries and twelve tropical fish as real
> nets, no runtime tinting.
>
> **Not built:** the tadpole (needs frog breeding) and the guardian (needs
> projectiles). Turtle home beaches and egg laying are also outstanding.
>
> **The one thing this section got wrong:** it does not mention that Minecraft has
> **no water-body size test**. I wrote a flood fill to keep fish out of puddles
> before checking. The reference does it with a **biome tag** — `spawns_underwater`
> plus an ocean/river filter — which costs nothing and is why a village well never
> fills with cod. The spawn rule files carry `height_filter`, `density_limit`,
> `herd`, `distance_filter` and `brightness_filter`, and every one is a number
> rather than a search. Ours adds a three-block depth requirement on top, because
> our biome map is coarser than theirs.

**What the engine would need:**

1. **A fluid volume the physics can query** — `isInWater(aabb)` and a **submersion fraction**, not a boolean.
2. **Buoyancy + drag replacing gravity while submerged.**
3. **Water-constrained 3D pathfinding — or the cheat.** **Guardians pick a target in a 9×9×9 box and swim straight at it ignoring blocks**, crossing air and taking fall damage. Mojang chose not to write water A* for their flagship aquatic mob. That is a legitimate cheap implementation and worth copying.
4. **A breath timer in both directions.** Dolphins need `Moistness` (die **out** of water after 2 min) *and* air (drown after 4 min submerged).
5. **Schooling** — cod ≤9, tropical fish ≤7, salmon ≤5. Boids, or a leader plus followers. **Pufferfish deliberately opt out, so the flag is per species.**
6. **Variant encoding** — tropical fish pack shape/pattern/2 colours into one int. Cheap visual variety. But **do not** build the 3072-combination runtime tinting: that is exactly the mechanism that produced the washed-out grass at M19a. Author the 22 presets as real textures.

**Flying and swimming are the same purchase made twice — roughly two-thirds shared. Doing them in one milestone is far cheaper than a year apart.**

## 7.4 Archetype D — bipedal

**Movement is nearly free; everything else is not.** A biped needs 2 legs + 2 counter-phased arms plus an item-in-hand attachment — the smallest cost in the archetype. Then:

a trading GUI with a scrollable offer list; a trade/offer data model; a **reputation graph** with five gossip types decaying every 20 minutes; a **daily schedule** driven by world time; **claimable blocks with ownership** (beds, workstations) plus a village structure to hold them; cost-weighted pathfinding (path 0, dirt 3, workstation 50, jump 20); door interaction; a **village generator**; equipment slots with visible rendering; multi-tier visual damage; a **build-pattern recognizer** (golems are made by placing a block in a shape); and biome-driven layered outfits.

**Verdict: by far the most expensive — and the reason to add bipeds is the villager, which is an economy, a reputation system and a settlement simulation.** If we want *one* bipedal, **the iron golem or snow golem is an order of magnitude cheaper** (a rig, a projectile, a build-pattern check), and **the piglin is the middle option** — a rig, an equipment slot, and a genuinely novel player-state-driven aggression machine, with none of the economy.

## 7.5 Naming

Ordinary English costs nothing — *wolf, fox, panda, bear, frog, turtle, spider, bat, bee, parrot, squid, dolphin, axolotl, silverfish, witch, zombie, skeleton, slime* are plain words nobody owns. Only **coined** names need ours, the way Creeper became **Bramble**: Enderman, Endermite, Ghast, Shulker, Blaze, Piglin, Hoglin, Strider, Warden, Allay, Breeze, Creaking, Sniffer, and the illager family.

---

# 8. The Bedrock AI architecture

**This is the most transferable section in the document.** Bedrock's entity AI is data-driven and documented, which makes it a far better model to copy than Java's hardcoded goal classes.

## 8.1 The shape

Every AI goal is a component named `minecraft:behavior.*`. **Every one of them accepts two universal parameters:**

| Parameter | Default | Meaning |
|---|---|---|
| `priority` | 0 | **Lower number = higher priority.** 0 is both highest and default. |
| `control_flags` | `[]` | Array of `jump` / `look` / `move` — the controllers this goal claims while running. |

Most movement goals also take `speed_multiplier`, applied only while that goal runs.

> "If the entity is busy doing a low priority behavior and a high priority behavior comes up, the entity will **immediately switch**."

**`control_flags` is the whole trick.** Three bits. Goals claiming **disjoint** flags coexist; goals claiming the **same** flag are mutually exclusive and resolved by priority. That is what lets `look_at_player` (look) run simultaneously with `random_stroll` (move) — the reason both appear on every passive mob at different priorities and visibly run at once.

**Targeting goals and attack goals do not compete at all** — targeting goals *write* a target slot, attack goals *read* it. That is why a zombie has `hurt_by_target` at 1 and `melee_attack` at 1 in the same list with no conflict.

## 8.2 A real vanilla ordering — the chicken

| Priority | Behaviour |
|---|---|
| 0 | `float` |
| 1 | `panic` (speed ×1.5) |
| 2 | `mount_pathing` |
| 3 | `breed` |
| 4 | `tempt` (seeds) |
| 5 | `follow_parent` (speed ×1.1) |
| 6 | `random_stroll` |
| 7 | `look_at_player` |
| 8 | `random_look_around` |

Cow and goat are the same skeleton with different numbers. Sheep adds `eat_block` at 6. Hostiles follow the same shape: **survival reflexes (float/panic) → revenge → target acquisition → attack → wander → cosmetic look.**

Tameables show one more pattern: **`stay_while_sitting` sits above `follow_owner`**, so sitting suppresses following without either goal knowing about the other.

## 8.3 Key component parameters worth knowing

| Component | Notable parameters |
|---|---|
| `random_stroll` | `interval` (120 — a **1/interval chance per tick**), `xz_dist` 10, `y_dist` 7 |
| `look_at_player` | `look_distance` 8, `probability` **0.02**, `look_time` {2,4} s, `angle_of_view_*` 360 |
| `random_look_around` | `look_time` {20,40} s, `probability` 0.02 |
| `panic` | a long `damage_sources` list, `force`, `prefer_water` |
| `avoid_mob_type` | `max_dist` 3, `max_flee` 10, `sprint_distance` 7, `walk`/`sprint_speed_multiplier` |
| `nearest_attackable_target` | `scan_interval` **10 ticks** ("values under 10 can affect performance"), `must_see`, `must_reach`, `persist_time`, **`target_sneak_visibility_multiplier` 0.8**, `target_invisible_multiplier` 0.7 |
| `hurt_by_target` | **`alert_same_type`** — this one flag is pack anger |
| `melee_attack` | `cooldown_time` 1 s, **`reach_multiplier` 2** (× entity base size), `melee_fov` 90°, `track_target` |
| `delayed_attack` | adds `attack_duration` 0.75 s and **`hit_delay_pct` 0.5** — damage lands halfway through the animation |
| `tempt` | `items[]`, `within_radius`, `stop_distance` 1.5, `can_tempt_vertically` |
| `follow_owner` | `start_distance` 10, `stop_distance` 2, `can_teleport` |
| `eat_block` | `eat_and_replace_block_pairs`, `success_chance` (sheep uses a Molang expression: `query.is_baby ? 0.02 : 0.001`), `time_until_eat` 1.8 s |

## 8.4 Navigation versus movement — the idea most worth stealing

Bedrock splits these into **two orthogonal components**:

- `minecraft:navigation.*` answers **"what counts as a traversable node?"** — it configures the path *search*.
- `minecraft:movement.*` answers **"how do I physically travel along the path?"** — it configures *locomotion*.

All seven navigation types (`walk`, `fly`, `swim`, `generic`, `hover`, `climb`, `float`) take **the same field set**; the component name only selects the search implementation. That tells you the parameters were always the real content and the type name is mostly a dispatch tag.

Fields: `can_walk`, `can_swim`, `can_float`, `can_sink`, `can_jump`, `can_breach`, `can_path_over_water`, `can_path_over_lava`, `can_walk_in_lava`, `can_path_from_air`, `is_amphibious`, `can_pass_doors`, `can_open_doors`, `can_break_doors`, `avoid_water`, `avoid_damage_blocks`, `avoid_portals`, **`avoid_sun`**, `blocks_to_avoid`.

Real configurations:

```
Cow:    navigation.walk    { can_path_over_water, avoid_water, avoid_damage_blocks }
Bogged: navigation.walk    { avoid_sun, is_amphibious, avoid_water }
Parrot: navigation.fly     { can_path_from_air, can_path_over_water }
Allay:  navigation.hover   { avoid_damage_blocks, can_pass_doors: false, can_sink: false }
Frog:   navigation.generic { can_swim, can_walk, is_amphibious, can_sink: false }
```

## 8.5 Pathfinding node costs

| Penalty | Value | Blocks |
|---|---|---|
| blocked | **−1** | most full solid blocks |
| fence / wall / closed gate | −1 | *(this is why mobs can't jump fences)* |
| lava, powder snow, rails, closed doors | −1 | |
| **cactus, sweet berry bush** | −1 | |
| open | 0 | air, trapdoors, lily pad, big dripleaf, open doors |
| damage - cautious | 0 | wither rose, pointed dripstone |
| breach water | 4 | air above water |
| water / water border | 8 | |
| **danger - fire** | **8** | ***neighbouring*** fire, lava, magma, lit campfire |
| **danger - other** | **8** | ***neighbouring*** cactus, sweet berry bush |
| honey | 8 | |
| damage - fire | 16 | fire, lava, magma block itself |

**Three structural points worth stealing:**

1. **−1 means impassable, not expensive.** One sentinel does both jobs.
2. **"danger" penalties apply to the *neighbours* of the hazard, not the hazard itself.** This is what makes mobs give a lava lake a berth rather than skimming its edge. One extra lookup in the cost function.
3. Each navigation type uses its own subset, and individual mobs override values.

Wandering, verbatim: *"random targets are generated every tick… For every target, a path is generated; with the entity preferring the path with the lowest score… If an entity's target is located inside a block (including water), the target is moved to the nearest air block above it."*

> **▶ Where we stand (2026-08-06).** Built as **M20k**. `world/Pathfinder.hpp` is A\* over foot cells with the penalty model above; only the two water entries can arise, since we have no lava, fire, cactus, honey or doors. The **"danger applies to the neighbours"** rule from point 2 is implemented and is what makes `avoidsWater` real routing rather than a fan rejection. The **−1 sentinel** is expressed as a node simply not being generated, which is the same thing with less arithmetic.
>
> Two of the quoted wandering rules are in: a goal inside a block is **lifted to the first cell a body fits in**, and a search that cannot reach its goal returns the route to the **closest cell it reached** rather than nothing. The "generate several targets and take the lowest-scoring path" part is deliberately not — it is one search per goal here, because scoring several candidate routes multiplies the only genuinely expensive thing in the system.
>
> The cost bounds are ours, not the reference's: 24 m of range, 384 expansions, 32 waypoints, two searches a frame across the population, and a repath every half second or whenever the target has moved a block.

## 8.6 Spawn rules — the cleanest thing in the whole design

A separate JSON per entity. `population_control` selects one of four capped pools: `animal`, `water_animal`, `monster`, `cat`. `conditions` is an **array**, each element an independent condition set — that is how one mob gets several spawning modes.

| Component | Parameters |
|---|---|
| `spawns_on_surface` / `spawns_underground` / `spawns_underwater` / `spawns_lava` | flags |
| `brightness_filter` | `min` 0, `max` 15, **`adjust_for_weather`** |
| `biome_filter` | a filter test, usually `has_biome_tag` |
| `density_limit` | `surface`, `underground` |
| `herd` | `min_size`, `max_size`, `event`, `event_skip_count` |
| `weight` | `default` — priority out of 100 |
| `difficulty_filter` | `min`, `max` |
| `distance_filter` | `min` **24**, `max` **128** |
| `height_filter` | `min`, `max` |
| `spawns_on_block_filter` | block ID or array |
| `permute_type` | `[{weight, entity_type}]` — variant roll at spawn |

The canonical zombie file reads as an algorithm: **filter → weight → herd → permute.** That is exactly the shape a spawn system wants.

## 8.7 What to take, and what to skip

**Copy these four:**

1. **Split "select a behaviour" from "run a behaviour", and make selection a priority sort.** This is the entire win over a switch and it is about 80 lines. A behaviour becomes a struct with `canStart()`, `tick()`, `canContinue()` plus a priority and a flag mask. At 15 species we want **8–10 behaviours, not 190.**
2. **Control flags. Do not skip this.** Three bits — `Move`, `Look`, `Jump` — buys "a chicken can walk and stare at the player at once" with no special-casing. It is the difference between an FSM and a behaviour system. **Highest value-per-line item on the list.**
3. **Target goals separate from attack goals.** Put `EntityId target` on the creature; one or two behaviours *write* it, attack behaviours *read* it. This makes "retaliates when hit", "hostile hunts the player" and "wolf defends its owner" three table rows rather than three branches. **Our existing Chase conflates the two — splitting it is the single most useful refactor available.**
4. **Spawn rules as a declarative condition list.** A `SpawnRule` struct plus one evaluator: filter, weighted roll, herd spawn. Steal the pool caps and the surface/underground density split.

**Skip these — over-engineering at 15 species:**

- **JSON at runtime.** The value is the *shape*, not the format. A `constexpr` array of structs gives compile-time checking, zero parse cost and no schema versioning.
- **Component groups and entity events as a general mechanism.** It carries a documented "removal resets to default, not to the previous value" landmine that Mojang's own docs tell authors to work around by hand-maintaining complement groups. The cheap version is a **`Mode` bitmask** on each behaviour that the selector tests — one compare gets wolf-becomes-tamed with none of the reset semantics.
- **A filter expression language.** `is_family` plus a small `enum class Family : uint16_t` bitmask covers everything.
- **Navigation/movement as class hierarchies.** Take the *idea* — one flags struct for the pathfinder, another for locomotion — as two PODs in the species row.

## 8.8 The migration path — every step compiles and runs

**Step 1.** Introduce the struct, keep the switch's logic:

```cpp
enum class ControlFlag : uint8_t { Move = 1, Look = 2, Jump = 4 };

struct Behaviour {
    uint8_t priority;   // 0 = highest, matching Bedrock
    uint8_t flags;      // ControlFlag mask
    bool (*canStart)(Creature&, const World&);
    void (*tick)(Creature&, World&, float dt);
    bool (*canContinue)(Creature&, const World&);  // nullptr => same as canStart
};
```

Write exactly three — `Wander`, `Flee`, `Chase` — by lifting the existing switch bodies verbatim. The species table gains a `std::span<const Behaviour>`. Behaviour is identical; only the shape changed.

**Step 2.** Write the selector, delete the switch. Per tick, walk the array in priority order; start a behaviour if it can start and no running higher-priority behaviour holds an overlapping flag. Track running behaviours in three slots, one per flag.

**Step 3.** Split `Chase` into `NearestAttackableTarget` (priority 2, **no flags** — it only writes the target slot) and `MeleeAttack` (priority 2, `Move|Look`). Add `HurtByTarget` at priority 1 while you are there; six lines, and retaliation comes free.

**Step 4.** Add `LookAtPlayer` at priority 7 with **`Look` only**. Do this deliberately as a test: if chickens now walk and watch simultaneously, the flag arbitration is correct. If they stop walking, it is not. Cheapest possible validation of step 2, and it is also the single change that most makes creatures look alive.

**Step 5.** Port the spawn rules table, independently. It touches only the spawner.

**Step 6.** Add the `Mode` bitmask when taming or babies genuinely need it. **Not before.**

> **▶ Where we stand (2026-08-04).** **Steps 1–4 are built.** The switch is gone; six behaviours sit in a `constexpr` table selected by priority with `Move`/`Look`/`Jump` control flags. `CreatureState` was deleted in favour of a `CreatureTarget` slot that producers write and `MeleeAttack` reads, and `strike`/`alertNeighbours` now set a `provokedTimer` rather than choosing a mood. `LookAtPlayer` claims `Look` alone and creatures turn their heads independently of their bodies, which is the arbitration test passing visibly. `SYSTEM_MEMORY.md` "How a creature decides what to do" is the current description.
>
> **Step 5 remains**, and it is now "unify the two spawners" rather than "write one": chunk-generation spawning is in, driven by `groupSize` and `weight`, while the continuous cycle still reads its filters inline. **Step 6 stays deferred** until taming or babies genuinely need a `Mode` bitmask.
>
> What the migration cost less than expected, and why: retaliation, the six-second grudge, `huntsBelowLight` and herd alerting all already existed as branches, so steps 1–3 *moved* them rather than adding them — which made "identical behaviour before and after" the acceptance test.

---

# 9. Blocks, fluids and plants

## 9.1 Fluid mechanics - BUILT; this is now a pointer

> **This section ran to nearly four thousand words and the work is done.** Water shipped at
> M20h and was corrected twice at M20j. A description of how the reference behaves, sitting
> beside an implementation that already behaves that way, is a second copy of the same facts -
> and a second copy is this project's single most reliable source of bugs.

| Question | Where the answer lives now |
|---|---|
| Every constant of how an entity moves through water | `game/src/world/Fluid.hpp`, the single owner, read by the player, creatures and dropped items |
| How the block spreads, and why a waterfall is a column | `SYSTEM_MEMORY.md` -> "Water" |
| What we deliberately diverge on | `SYSTEM_MEMORY.md` -> "Water", "Named divergences" |
| Why treading took three attempts | `LESSONS.md`; `tools/simulate-swim.ps1` replays it |

**Four findings kept because they are not recoverable from the code:**

1. **The wiki's 0.39 sink rate is a copy error** - it is the upstream *horizontal* speed. The
   real figure is 0.50. Every vertical terminal is `(impulse * drag - water gravity)/(1 - drag)`,
   and dropping the water-gravity term made all four about a quarter too fast, which shipped.
2. **The wiki's "2.23 m/s ascending by holding jump" is measured in a waterfall**, so it is not
   the still-water figure. The number to check a sprint-swim-up against is **6.98 m/s**.
3. **`Mojang/bedrock-samples` carries no fluid data at all** - it is a behaviour pack, and the
   player's water physics is closed C++. It *is* authoritative for `breathes_water`, the seven
   `navigation.*` flags and `behavior.float`.
4. **`can_sink` and `behavior.float` are separate mechanisms, and that is the crux.** Squid,
   frog, turtle, axolotl and dolphin set `can_sink: false` with *no* float goal, so they hang
   mid-water and never surface. Every land mob that bobs leaves `can_sink` at its default and
   gets the bob from `behavior.float` instead. The undead pair `is_amphibious` with
   `breathes_water`, so a lake is a road to a zombie. **Piglins have no float goal and drown.**

| Navigation flag | Default | What it changes |
|---|---|---|
| `can_swim` | false | Path nodes may sit **inside** the water volume, in 3D |
| `is_amphibious` | false | Nodes on the **seabed** are walkable |
| `can_sink` | **true** | Gravity applies in water. False is neutral buoyancy, **not** lift |
| `can_path_over_water` | false | The surface is a walkable floor |
| `avoid_water` | false | **Pathing only.** Routes around water; does not refuse entry |


## 9.2 Gravity blocks

Sand, gravel, concrete powder, dragon egg, anvils, pointed dripstone, scaffolding (at `distance` 7). They start falling when the block below is **replaceable**.

**The landing rule that matters:**

> A falling block continues until it lands on a block with a **solid top surface**. If it lands with the **bottom centre of its hitbox in a replaceable block**, and the block below can support it, it returns to its block state. **Otherwise it breaks and drops as an item.**

So landing on anything shorter than a full block — torch, rail, slab, soul sand, button — turns it into a dropped item. Gravel that does this **never drops flint**.

Falling block entity: hitbox 0.98², terminal ≈1.96 b/t, **deletes itself and drops as an item after 600 ticks (30 s)**. Anvils deal **2 HP per block after the first, capped at 40**; helmets give 25% reduction and take double durability.

## 9.3 Crop growth — the exact formula

Per random tick, if **light ≥ 9 at the crop block**:

$$P(\text{advance}) = \frac{1}{\left\lfloor \dfrac{25}{\text{speed}} \right\rfloor + 1}$$

| Contribution | Value |
|---|---|
| Farmland under the crop, **dry** | base **2** |
| Farmland under the crop, **hydrated** | base **4** |
| Each of the 8 surrounding blocks that is dry farmland | **+0.25** |
| Each that is hydrated farmland | **+0.75** |
| **Crowding penalty** — same crop diagonally, or on both N–S and E–W axes | **speed ÷ 2** |

Best realistic case (hydrated field, alternating rows) → **1/3 per random tick**, 80% mature in ~31 min. Worst realistic (no rows, dry) → 1/13. **Absolute worst: two crops diagonally adjacent on dry farmland = 1/23.**

**Farmland hydration** needs water within **4 blocks horizontally including diagonals**, at the same Y or one above. Blocks between are irrelevant. `moisture` jumps **0 → 7** directly when hydrated and counts down when not. Farmland reverts to dirt when jumped on with probability **(fall distance − 0.5)**, and mobs smaller than 0.512 m³ are exempt.

## 9.4 Random ticks — the clock everything runs on

Each chunk tick, for **each 16×16×16 subchunk**, `randomTickSpeed` blocks are chosen at random. **Subchunks with no tick-reactive blocks are skipped entirely** — that is the primary optimisation. Air is a valid target and a block can be selected twice.

| | Bedrock | `[JE]` |
|---|---|---|
| Default | **1** | **3** |
| Semantics | relative speed only | **exact count per subchunk** |
| Mean interval per block | 204.8 s | **68.27 s** |
| Median | — | **47.30 s** |

$$P(\text{a specific block is ticked}) = 1 - \left(\frac{4095}{4096}\right)^{n}$$

## 9.5 Leaf decay

`distance` 1–7 (taxicab to the nearest log, default 7) and `persistent` (true for player-placed). Leaves decay on a random tick if not connected to a log within **4 blocks (Bedrock)** / 6 (`[JE]`), searched as a **BFS through leaf blocks, allowed to cross corners**.

Drops: sapling **5%** (jungle 2.5%), 1–2 sticks **2%**, apple from oak/dark oak **0.5%**. Rolls are independent. **Burned leaves drop nothing.**

## 9.6 Grass spread

A dirt block gets grass when **all four** hold:

1. It is within a **3×5×3** volume with the source grass at the centre of the second-topmost layer — so targets may be **1 above and 3 below**.
2. The source has **light ≥ 9 directly above it**.
3. The block above the target lets light through. **The actual light level at the target does not matter.**
4. The block above the target is not lava, water or waterlogged.

On a random tick a grass block **checks 4 random positions**. Because it reaches 3 down but only 1 up, **grass spreads downhill far faster than uphill**. It reverts to dirt when covered by an opaque block.

## 9.7 Solid vs opaque vs full block — six independent properties

Minecraft keeps **at least six** separate notions, and conflating them is a classic source of bugs:

| Property | Answers | Used for |
|---|---|---|
| **Collision shape** | Can an entity move through it? | movement, physics |
| **Interaction / outline shape** | What does the crosshair select? | ray targeting, the outline |
| **Occlusion shape** | Does this face hide another, and stop light? | face culling, lighting |
| **Support shape** | Can something attach to this face? | placement |
| **"Solid"** | Is it a solid attachment target? | placement, mob spawn surface |
| **Full block** | Is the collision box a full cube solid on all sides? | enderman placement, hopper blocking |

The divergences are deliberate and instructive:

- **Leaves:** full-block collision, but an **empty support shape** — nothing attaches to leaves even though you stand on them.
- **Soul sand:** **full-block support shape**, but a **7/8 collision shape** — everything can be placed on it, but you sink in.
- **Slime block:** light-transparent with a see-through texture, yet conducts redstone and allows mob spawning on top.

`[JE]`'s formal "solid for placement" test: a block qualifies if it is on a forcibly-solid list, **or** its collision box extents sum X+Y+Z ≥ 35 pixels, **or** its vertical extent ≥ 16 pixels.

**Only a select list of blocks uses its shape for light occlusion** — slabs, stairs, snow, farmland, dirt path, daylight detector, lectern, pistons, sculk sensors, stonecutter, enchanting table, end portal frame. Everything else is treated as having an **empty** occlusion shape. This exemption exists so that **light emitters work at all** — if glowstone considered its own full cube, every face would form a full square with a neighbour and it could never emit.

## 9.8 Stairs and slabs

Stair states: `facing` (the direction the **full-block side** faces, set from the player's facing at placement), `half`, `shape` (straight / inner_left / inner_right / outer_left / outer_right), `waterlogged`.

- **Inner corner** when the stair's **half-block back** is adjacent to another stair's side; **outer corner** when its **full-block side** is.
- **Right-side-up stairs never join upside-down ones.** Corner shapes join stairs of any material.
- Bedrock added a real `minecraft:corner` block state in 26.40/26.50; before that it was derived client-side.

Slabs: `type` bottom/top/**double**. Placing a matching slab into the other half makes a **double slab** (one block). **Mobs spawn on top slabs and double slabs but not bottom slabs** — the standard spawn-proofing trick. **Falling blocks turn into items when they land on a bottom slab.**

## 9.9 Fire

Driven by **scheduled ticks**, not random ticks (`[JE]`); Bedrock also uses random ticks. Interval **30–40 ticks**. `age` 0–15 with a **1/3 chance to increment** per block tick.

Fire tries to ignite air in **3×3 horizontally, from 1 below to 4 above**:

$$\text{degree} = \left\lfloor \frac{i + 7d + 40}{a + 30} \right\rfloor$$

where *i* = the max ignite-odds among the candidate's neighbours, *d* = difficulty 0–3, *a* = the source fire's age. Spread probability ≈ degree ÷ base, where base is **100** in the 3×3×3 core, **200/300/400** at +2/+3/+4 vertically. Halved in humid biomes. Blocked entirely by rain on the target or any of its four horizontal neighbours.

**Eternal fire** on netherrack, magma block, soul sand, soul soil.

> **▶ Where we stand.** We have water with flow levels and sources, slabs and stairs with real collision shapes, alpha-tested leaves, a targeting outline that reads the shape table, and block hardness driving timed mining.
>
> Take:
> 1. **The weight/shortest-path-down search** (§9.1). Our water spreads evenly; the reference's seeks holes. This is what makes water look like water.
> 2. **The "danger applies to neighbours" idea** from §8.5 and the **−1 sentinel**, if we ever add creature hazard avoidance.
> 3. **Random ticks as the growth clock** (§9.4), when plants arrive. The subchunk-skip optimisation is what makes it affordable.
>
> Know: we already learned that **overlap and resolution must read one shape table on every axis** — the reference's six separate shape notions is the same lesson at full scale, and if we ever add a block whose support shape differs from its collision shape (leaves, soul sand), we will need the split.

---

# 10. Mining, tools, smelting, XP

## 10.1 The mining formula

$$\text{seconds} = \frac{\text{hardness} \times (\text{canHarvest} \;?\; 1.5 : 5.0)}{\text{toolMultiplier}}$$

**Hardness × 1.5 with the correct tool, hardness × 5 without.**

| Material | Multiplier |
|---|---|
| Wooden | 2 |
| Stone | 4 |
| Copper | 5 |
| Iron | 6 |
| Diamond | 8 |
| Netherite | 9 |
| **Golden** | **12** |

Modifiers, all multiplicative: Efficiency adds `level² + 1`; Haste ×(1 + 0.2·level); **underwater without Aqua Affinity ÷5**; **not on the ground ÷5**. A block breaks instantly when speed exceeds **30 × hardness**.

**Harvest level** decides whether you get a drop at all, separately from speed.

| Block | Hardness | Tool |
|---|---|---|
| Grass, dirt, sand, gravel | 0.5–0.6 | shovel |
| Glass | 0.3 | any |
| Wool | 0.8 | shears |
| Stone | 1.5 | pickaxe |
| Log, planks, cobblestone | 2.0 | axe / pickaxe |
| Deepslate, all ores | 3.0 | pickaxe |
| Deepslate ore variants | 4.5 | pickaxe |
| Ladder | 0.4 | axe |
| Obsidian | 50 | diamond pickaxe |
| Bedrock | **−1 (unbreakable)** | — |

Hardness **−1** is a sentinel, not a small number.

## 10.2 Smelting

**200 ticks (10 s)** per item. Fuel values in items smelted:

| Fuel | Items |
|---|---|
| Lava bucket | 100 |
| Block of coal | 80 |
| Blaze rod | 12 |
| Coal, charcoal | 8 |
| Any log / planks / wooden item | 1.5 |
| Stick, sapling | 0.5 |

**XP is granted when the output is collected, not when it is smelted**, and accumulates in the furnace until then. Iron and gold 0.7/item, coal 0.1, food 0.35.

## 10.3 Ore drops

| Ore | Drops | XP |
|---|---|---|
| Coal | 1 coal | 0–2 |
| Iron / gold / copper | raw ore (copper 2–5) | 0 |
| Diamond / emerald | 1 gem | 3–7 |
| Lapis | 4–9 | 2–5 |
| Redstone | 4–5 | 1–5 |

> **▶ Where we stand.** **All eight ores exist and drop by this table**, with the ranges flattened to their midpoints — copper 3, redstone 4, lapis 6, everything else 1 — because there is no randomness in `dropCountForBlock` and a fixed number is honest rather than pretending. **Tiers are one step lower than the reference's**: gold, redstone, diamond and emerald demand an iron pickaxe there and a stone one here, since we have only wood and stone. XP does not exist yet.

## 10.4 Experience

Passive animals give **1–3**; most hostiles **5**; blaze and guardian 10; wither 50. On death a player drops **7 XP per level, capped at 100** — the rest is lost.

> **▶ Where we stand.** We have timed digging with per-tool speeds and a furnace with fuel, and the formula shape matches. Two things to take: **the ×5 wrong-tool penalty** (without it, tool choice is a nicety rather than a decision — and we already have the formula it slots into), and **XP held in the furnace until collection**, which is a genuinely good feel detail.
>
> Note the reference's **instant-break rule is "speed exceeds 30 × hardness"** — a per-block gate. We fixed our creative-mode instant break with a *timer* after the bug where a 60 ms click stripped twelve blocks in a line. The reference's gate is the more principled version of the same fix.

---

# 11. Crafting and recipes

## 11.1 Matching rules

**Shaped** recipes match a pattern that may sit anywhere in the grid (the game trims empty rows and columns before matching) and may be mirrored. **Shapeless** cares only about the multiset. Ingredients can be **tags** — `#planks`, `#logs`, `#wool` — which is what stops the recipe list exploding across wood types.

## 11.2 Core recipes

| Output | Grid |
|---|---|
| 4 planks | 1 log (shapeless) |
| 4 sticks | 2 planks stacked vertically |
| 1 crafting table | 2×2 planks |
| 1 furnace | 8 cobblestone in a ring |
| 1 chest | 8 planks in a ring |
| 4 torches | 1 coal over 1 stick |
| 3 slabs | 3 of a block in a row |
| 4 stairs | 6 of a block in a descending staircase |
| 1 pickaxe | 3 material across the top, 2 sticks down the middle |
| 1 axe | 2+1 material in an L, 2 sticks down |
| 1 shovel | 1 material over 2 sticks |
| 1 sword | 2 material over 1 stick |
| 1 hoe | 2 material across, 2 sticks down |
| 1 shears | 2 iron diagonally |
| 1 bucket | 3 iron in a V |
| 4 bowls | 3 planks in a V |
| 3 ladders | 7 sticks in an H |
| 1 bed | 3 wool over 3 planks |
| 6 doors | 6 planks in a 2×3 |
| 2 fences | 4 planks + 2 sticks |
| 3 signs | 6 planks over 1 stick |
| 9-block ↔ ingot | shapeless both ways |

**Stonecutter:** one input, pick an output. Strictly better than crafting for stone stairs (1→1 vs 6→4). **Smithing table:** template + item + material, preserving durability and enchantments at no XP cost.

**Stack sizes** 64, except **16** for ender pearls, snowballs and potions, and **1** for tools, armour and filled buckets. Damaged and enchanted items do not stack.

> **▶ Where we stand.** We have a 3×3 grid with shaped and shapeless recipes and a crafting table screen derived from the inventory panel. The missing piece is **ingredient tags**, which we will want the moment a second wood type exists — and adding them retroactively means touching every recipe.

---

# 12. Light

Two independent values, 0–15, and there are actually **four** distinct quantities:

| Quantity | Definition | Changes with time? |
|---|---|---|
| **Block light** | From emitters; −1 per **taxicab** step (so diagonals lose 2) | No |
| **Sky light** (stored) | 15 if vertically exposed; flood-fills outward. **Not reduced at night.** | **No** |
| **Client light** | `max(sky, block)` — the F3 number | No |
| **Internal sky light** | Derived from stored sky light + time + weather. **May go negative.** | **Yes** |

For stored sky light below 15: $L' = L - (15 - s)$, which is why internal sky light can go negative.

| Time / weather | Internal sky light |
|---|---|
| Noon, clear | **15** |
| Noon, rain or snow | **12** |
| Noon, thunderstorm | **10** (but **5 effective for hostile spawning** — a −10 offset) |
| Midnight, any weather | **4** |

**Moonlight is not reduced by weather** — the night floor stays at 4 in clear, rain and snow alike.

**Emitters:** 15 glowstone, sea lantern, lava, beacon, froglight, jack o'lantern, campfire, shroomlight; **14 torch**, end rod, lit furnace; 13 lit redstone lamp/ore; 11 nether portal, crying obsidian; 10 soul fire; 7 enchanting table, ender chest, glow lichen; 3 magma block.

**Filtering:** glass and carpets cost nothing extra. **Bedrock water and ice cost an additional −1 per block.** Leaves diffuse sky light, attenuating further with each vertical layer. **Lava completely blocks light propagation** (rarely visible, since it emits 15).

> **▶ Where we stand.** We have both channels packed into the vertex colour's red and green, with face shade × ambient occlusion in blue. The sky-light free-fall rule is implemented, including `isSkyTransparent` split from `isLightTransparent` so leaves let light through but break the free fall — which is exactly the reference's "leaves diffuse sky light" behaviour arrived at from a different direction.
>
> What we lack is **internal sky light**: our hostiles use `sky >= 12` as a daylight test rather than a time-modulated value. That works, but it couples spawn suppression to geometry only, and it means a thunderstorm could never let hostiles spawn by day.

---

# 13. Time, weather and sky

## 13.1 The day

24,000 ticks = 20 real minutes. **Tick 0 is 06:00 (dawn)**, not midnight.

| Phase | Start | Duration | Clock |
|---|---|---|---|
| Daytime | 0 (noon at 6,000) | 12,000 ticks / 10 min | 06:00 → 18:00 |
| Sunset | 12,000 | 1,000 / 50 s | 18:00 → 19:00 |
| Night | 13,000 (midnight at 18,000) | 10,000 / 8 min 20 s | 19:00 → 05:00 |
| Sunrise | 23,000 | 1,000 / 50 s | 05:00 → 06:00 |

**Outdoor hostile spawn windows:** clear **13,188 → 22,812** (≈8 min 1 s); rain **12,969 → 23,031**; **thunderstorm: the entire day.**

## 13.2 The sky angle formula

The sun moves **faster near noon and midnight, slower near sunrise and sunset** — it is not linear:

$$\alpha = \left(1 - \cos\!\left(\pi \cdot \operatorname{mod}_1\!\left(\frac{t - 6000}{24000}\right)\right) + \operatorname{mod}_4\!\left(\frac{t - 6000}{6000}\right)\right) \cdot 60°$$

with 0° = noon. The sun's trajectory is a **perfect vertical circle through the zenith** — no axial tilt, no Z-rotation, so there is no southern bias. The moon is always **exactly opposite** the sun, which makes solar eclipses impossible. Both are drawn as **square** quads, are **not faded by fog**, and the sky box fades to full transparency **7° above the horizon** so fog colour takes over near the horizon.

**Moon phases:** 8, cycling once per 8 days (192,000 ticks = 2 h 40 min), changing at the **end of sunrise**. `phase = (day mod 8) + 1`. Mechanically it affects only swamp slime spawning (proportional to fullness, **zero at new moon**), regional difficulty, and the 1/11 black cat chance.

## 13.3 Weather — two flags, two timers

| Rain | Thunder | Result |
|---|---|---|
| Off | Off | Clear |
| Off | **On** | **Clear** (thunder does nothing without rain) |
| On | Off | Rain / snow |
| On | On | Thunderstorm |

| Timer | Range (ticks) | Real time |
|---|---|---|
| Rain **on** | 12,000 – 24,000 | 10 – 20 min |
| Rain **off** | 12,000 – 180,000 | 10 min – 2.5 h |
| Thunder **on** | 3,600 – 15,600 | 3 – 13 min |
| Thunder **off** | 12,000 – 180,000 | 10 min – 2.5 h |

All uniformly distributed. Because the flags are independent, **thunderstorm occupies only ≈1.44% of world time** — roughly 9 real hours between storms.

Effects: rain **extinguishes fire** (except on netherrack, magma, soul sand, and lit campfires); thunderstorms let hostiles spawn **at any hour**; lightning strikes at **1/100,000 per chunk tick** and needs a player within 8 chunks.

## 13.4 Burning in sunlight

Applies to zombie, skeleton, stray, drowned, phantom, zombie villager, bogged. **The husk is explicitly exempt.**

| Rule | Value |
|---|---|
| Zombie start | **27 s (540 ticks) before** the start of a new day |
| Skeleton condition | Moon no longer visible and sun above 15° |
| Suppressed by | sky light ≤14 (zombie) / ≤11 (skeleton), water, **head armour**, Fire Resistance, cobwebs |
| **Glass and ice do NOT stop it** | `[JE]` |
| Helmet cost | 50% chance to lose 1 durability per tick it would have ignited |
| Behaviour | Skeletons actively seek shade and keep shooting from it; zombies seek water or shelter unless actively pursuing |

> **▶ Where we stand.** We have a configurable day length and a placeholder sun on an arc with a Lambert term and a sky colour that follows it. **`burnsInDay` now follows this list exactly**: the zombie, zombie villager, skeleton, stray and bogged burn off; the Bramble, Blackbone, husk, witch, spiders, slimes and silverfish do not. The Bramble and the Blackbone were wrong until 2026-08-04 — both defaulted to burning, because the *default* is `true` and neither row restated it.
>
> Take **the non-linear sky angle formula** (§13.2) when M24 lands — it is one line and it is why Minecraft's sunsets linger. And take **the two-flag weather state machine** (§13.3) when weather arrives: two booleans and two countdowns is dramatically simpler than a weather-intensity float, and it produces the right statistics for free.

---

# 14. Ticking, chunks and persistence

## 14.1 Per-tick order (Java, and the ordering is the point)

```
per dimension:
  1. world border
  2. weather cycle
  3. daylight cycle
  4. player sleeping
  5. scheduled commands
  6. scheduled BLOCK ticks
  7. scheduled FLUID ticks
  8. raids
  9. update chunk load levels
 10. for all chunks in RANDOM order: spawn mobs → ice/snow → random ticks
 11. send block changes
 12. update points of interest
 13. unload chunks
 14. block events
 16. for all non-passenger entities: check despawn → tick → tick passengers
 17. tick block entities
autosave every 6000 ticks
```

Facts worth extracting: **scheduled ticks run before chunk ticks, and both before entities. Block ticks precede fluid ticks. Block entities tick after regular entities. Chunks are iterated in random order.** Weather and daylight advance before anything reads them.

## 14.2 Scheduled versus random ticks

| | Random | Scheduled |
|---|---|---|
| Trigger | stochastic subchunk sampling | explicit request by a block |
| Timing | geometric distribution | exact future tick |
| Persistence | nothing — state is in the block | **saved with the chunk** |
| Ordering | random | priority, then scheduling order |

Block ticks are sorted by priority then order; fluid ticks by order only. Java caps at **65,536 scheduled ticks per game tick**; Bedrock at **100 per chunk per tick**.

Saved as `block_ticks` / `fluid_ticks`, each entry `{i: blockID, p: priority, t: ticks remaining (may be negative when overdue), x, y, z}`.

## 14.3 Block updates — depth-first, not queued

Two types with **different neighbour orders**:

| | Post Placement | Neighbor Changed |
|---|---|---|
| Order | **west, east, north, south, down, up** | **west, east, down, up, north, south** |
| Use | connected shapes, attached blocks dropping | redstone |

> "Block updates follow the principle of depth-first search. When Block A changes, it sends updates to B and C one by one. Receiving the update, B changes and sends to D and E. D changes and sends onward. Then E receives from B. Then C receives from A."

**Updates recurse on the call stack rather than being appended to a work queue.** That is why stack-overflow update suppression is possible at all.

## 14.4 Chunk load levels

**A lower level = a higher load type. Only the lowest level from any ticket matters.**

| Load type | Level | Properties |
|---|---|---|
| **Entity Ticking** | ≤ 31 | everything works |
| **Block Ticking** ("lazy") | 32 | everything **except** mob spawning, entity processing, and chunk ticks |
| **Border** | 33 | nothing runs, but blocks and entities are accessible |
| **Inaccessible** | ≥ 34 | nothing — **but world generation happens here** |

Levels propagate outward from a ticketed chunk, **+1 per step, capped at 44**. So a single level-31 ticket produces a two-chunk skirt: one ring of lazy chunks, one ring of border chunks, then inaccessible.

**Ticking regions:** entity ticking covers $\min(2s+1, 63)$ chunks square; **block entities, scheduled ticks and chunk ticks cover that plus a one-chunk frame**, $\min(2s+3, 65)$. Chunk ticks additionally require the chunk centre within **128 blocks** of a player.

**Cross-boundary rules** worth knowing: flowing fluid spreads to the **first** block outside a block-ticking chunk then suspends; fire the same; **an entity moving out of the entity-ticking region is suspended immediately** and resumes when the level improves.

## 14.5 Region files

`r.<x>.<z>.mca`, **32×32 = 1024 chunks**, 512×512 blocks. `regionX = chunkX >> 5` using an **arithmetic** shift — integer division floors only for positives, and coordinates are negative half the time.

```
Sector size: 4096 bytes (matches the OS page size)
Sector 0: location table  (1024 × 4-byte big-endian)
Sector 1: timestamp table (1024 × 4-byte)
Sector 2+: payload
```

Location entry: 3-byte sector offset + 1-byte sector count → **max 256 sectors = 1 MiB per chunk**. Payload: 4-byte length, 1-byte compression type, data. Compression 1 = GZip (unused), **2 = Zlib (what the client writes)**, 3 = uncompressed, 4 = LZ4, 127 = custom; **+128 means the payload lives in an external `c.<x>.<z>.mcc`**, which is how chunks over 1 MiB are handled.

**Entities were split out in 1.17** into a parallel `entities/` region tree (still `.mca`, different NBT). A third tree, `poi/`, holds points of interest.

> **▶ Where we stand.** We have multithreaded generation and meshing, chunk streaming, a save format for block edits and player position, and a per-frame budget on light propagation.
>
> The most useful idea here is **load levels as a single integer that propagates outward with a +1 per step**. We currently have a binary loaded/not-loaded, which is why we needed `World::columnResident` to stop creatures falling through absent terrain. A level would encode "generated but not simulated" directly, and the two-chunk skirt is exactly the buffer that makes cross-chunk operations safe.
>
> Also worth knowing: **the reference iterates chunks in random order**, which prevents systematic bias in per-chunk work. And **the arithmetic-shift-not-division warning** is a real bug we would otherwise hit at negative coordinates.

---

# 15. World generation

> **The primary source for this section is the local data dump, not the wiki:** `reference/minecraft-assets-26.2/minecraft-assets-26.2/data/minecraft/worldgen/` (Java 26.2, released 2026-06-16). Paths below are relative to that folder. Three tags are used where it matters: **[D]** read directly out of the JSON; **[code]** Java engine behaviour that is *not* in the data files and was quoted from decompiled source; **[wiki]** taken from minecraft.wiki.
>
> **Edition warning, and it is a real one.** The whole `noise_settings` / `density_function` / `placed_feature` format is Java-only — Bedrock ships nothing equivalent, so **nothing in this section was measured in Bedrock.** The one strong parity signal is minecraft.wiki's *World seed* page: a seed "generates the same terrain and biomes in both Java and Bedrock Edition. However, structures, features (i.e. decorators), carver caves, and mob spawns will generate differently." Read that literally: **terrain and biome numbers are Bedrock-parity by strong implication; feature, carver and spawn numbers are explicitly not.** `[JE]` marks values that are Java-only outright.

## 15.1 The shape of the system

Five stages run in this order. Each is a pure function of position and seed except the last two.

| # | Stage | Input | Output | Lives in |
|---|---|---|---|---|
| 1 | **Climate sample → biome** | 6 climate values at a 4×4×4 cell | a biome id per cell | `OverworldBiomeBuilder` **[code]**, fed by `noise_settings/overworld.json` `noise_router` |
| 2 | **Climate → terrain density** | continentalness, erosion, weirdness, y | one number per 4×8×4 cell corner; `> 0` = stone | `density_function/overworld/*` + `final_density`. **Noise caves are part of this expression**, not a later pass |
| 3 | **Surface rules** | the finished stone column, biome, y, noise | replaces stone with grass/sand/gravel/bedrock/deepslate | `surface_rule` in `noise_settings/overworld.json` |
| 4 | **Carvers** | per-chunk RNG, biome's carver list | tunnels and ravines cut out of the finished column | `configured_carver/*.json` + `ChunkGenerator.applyCarvers` **[code]** |
| 5 | **Features** | per-chunk RNG, biome's 11-step feature array | ores, trees, grass, lakes, springs | `placed_feature/` + `configured_feature/` |

**The reframing that matters most: in the 1.18 rewrite the biome stopped deciding the landscape.** Pre-1.18, a biome carried `depth` and `scale` fields and terrain height was literally looked up from the biome map, which is why old worlds had hills that stopped dead at a biome border. Now causality runs the other way — **terrain and biome are both outputs of the same three noises**, so they agree without either driving the other.

Confirmed structurally **[D]**: the top-level keys of every one of the 65 biome files in 26.2 are exactly `attributes, carvers, downfall, effects, features, has_precipitation, spawn_costs, spawners, temperature`. There is no `depth`, no `scale`, no `height`.

| Question | Does the biome decide it? | Where it lives instead |
|---|---|---|
| Terrain height, hilliness, cliffs | **No** | `offset` / `factor` / `jaggedness` splines → `sloped_cheese` (§15.4) |
| Where the oceans are | **No** | continentalness spline (§15.4) |
| Noise caves — cheese, spaghetti, noodle | **No.** Entirely global | `final_density` (§15.6) |
| Aquifer water levels | **No** | `aquifer_*` noise router slots (§15.6) |
| Surface block | **No — but biome is an input.** The rule *tests* biome identity | `surface_rule` (§15.5) |
| Carver caves and ravines | **Yes**, by listing carvers — but all 55 overworld biomes list the identical three, so in practice no | `carvers` (§15.6) |
| Ores, trees, grass, lakes, springs | **Yes** | `features` (§15.7) |
| Mob spawning | **Yes** | `spawners`, `spawn_costs` (§15.8) |
| Colour, precipitation, temperature | **Yes** | `effects`, `temperature`, `downfall` (§15.8) |

The only feedback edge is `depth`: terrain shape flows *into* biome selection so that cave biomes can exist underneath surface ones. Nothing flows the other way.

## 15.2 Climate parameters and how a biome is chosen

### The six parameters

All six are `shifted_noise` reads of a double-Perlin field at `xz_scale: 0.25` (one sample per 4-block cell) and `y_scale: 0.0` (height ignored), with the read position jittered horizontally by `shift_x`/`shift_z` — which are `shift_a`/`shift_b` of the `minecraft:offset` noise. **[D]**

| Parameter | Range | Controls | Noise file (`noise/`) | `firstOctave` | `amplitudes` |
|---|---|---|---|---|---|
| temperature | −1…1 | climate row of the biome grid; **no terrain effect** | `temperature.json` | −10 | `[1.5, 0, 1, 0, 0, 0]` |
| humidity (= vegetation) | −1…1 | vegetation column; **no terrain effect** | `vegetation.json` | −8 | `[1, 1, 0, 0, 0, 0]` |
| continentalness | −1.2…1 | ocean/coast/inland **and** the terrain offset spline | `continentalness.json` | −9 | `[1, 1, 2, 2, 2, 1, 1, 1, 1]` |
| erosion | −1…1 | flat vs mountainous; feeds offset **and** factor splines | `erosion.json` | −9 | `[1, 1, 0, 1, 1]` |
| weirdness (= ridges) | −1…1 | biome variant; source of PV | `ridge.json` | −7 | `[1, 2, 1, 0, 0, 0]` |
| depth | ≈ −1.5…1.5 | surface vs cave biome | derived, below | — | — |
| *(sample shift)* | — | jitters the XZ read position | `offset.json` | −3 | `[1, 1, 1, 0]` |

**Depth is not a noise.** It is a vertical ramp plus the terrain offset **[D]** (`density_function/overworld/depth.json`):

$$\text{depth}(x,y,z) = 1.5 - \frac{y + 64}{128} + \text{offset}_{xz}$$

which is exactly **+1/128 = 0.0078125 per block downward**, with `overworld/offset` (a 60 KB spline tree ending in a `-0.5037500262260437` bias) pulling depth to ≈ 0 at the local ground surface.

**Peaks-and-valleys (PV)** is a fold of weirdness **[D]** (`overworld/ridges_folded.json` = `mul(-3, add(-1/3, abs(add(-2/3, abs(ridges)))))`):

$$\text{PV} = 1 - \bigl|\,3|w| - 2\,\bigr|$$

PV drives terrain *and* indexes the biome grid, but **it is not one of the seven stored parameters** — the stored axis is raw weirdness, and the same PV band therefore appears twice, once each side of zero. That doubling is what "W<0 / W>0" means below.

**Large Biomes `[JE]`** swaps in `_large` noises at a lower `firstOctave` (temperature −12, vegetation −10, continentalness −11, erosion −11 — i.e. 4× the wavelength). There is deliberately **no `ridge_large`**.

### Band cutoffs **[wiki]**

| T | range | | H | range | | E | range |
|---|---|---|---|---|---|---|---|
| T0 | −1.0 … −0.45 | | H0 | −1.0 … −0.35 | | E0 | −1.0 … −0.78 |
| T1 | −0.45 … −0.15 | | H1 | −0.35 … −0.1 | | E1 | −0.78 … −0.375 |
| T2 | −0.15 … 0.2 | | H2 | −0.1 … 0.1 | | E2 | −0.375 … −0.2225 |
| T3 | 0.2 … 0.55 | | H3 | 0.1 … 0.3 | | E3 | −0.2225 … 0.05 |
| T4 | 0.55 … 1.0 | | H4 | 0.3 … 1.0 | | E4 | 0.05 … 0.45 |
| | | | | | | E5 | 0.45 … 0.55 |
| | | | | | | E6 | 0.55 … 1.0 |

| Continentalness band | range | | PV band | PV range | weirdness slices producing it |
|---|---|---|---|---|---|
| Mushroom fields | −1.2 … −1.05 | | Valleys | −1.0 … −0.85 | −0.05 … 0.05 |
| Deep ocean | −1.05 … −0.455 | | Low | −0.85 … −0.2 | ±(0.05 … 0.26666668) |
| Ocean | −0.455 … −0.19 | | Mid | −0.2 … 0.2 | ±(0.26666668 … 0.4), ±(0.93333334 … 1.0) |
| Coast | −0.19 … −0.11 | | High | 0.2 … 0.7 | ±(0.4 … 0.56666666), ±(0.7666667 … 0.93333334) |
| Near-inland | −0.11 … 0.03 | | Peaks | 0.7 … 1.0 | ±(0.56666666 … 0.7666667) |
| Mid-inland | 0.03 … 0.3 | | | | |
| Far-inland | 0.3 … 1.0 | | | | |

The weirdness slices are a **derivation, not a measurement** — the algebraic preimage of the published PV cutoffs. Check: $w=0.05 \Rightarrow PV=-0.85$; $w=0.26666668 \Rightarrow PV=-0.2$; $w=0.4 \Rightarrow PV=0.2$; $w=0.7666667 \Rightarrow PV=0.7$. All land exactly on published cutoffs.

### The selection algorithm

**A biome is not a point.** Each entry is a 7-tuple of closed intervals — an axis-aligned box in 7D. The axes, in engine order and visible in the `spawn_target` block of `noise_settings/overworld.json` **[D]**, are `temperature, humidity, continentalness, erosion, depth, weirdness, offset`. There is no PV axis.

| Mechanism | Detail |
|---|---|
| **Fixed point** | Every value, sampled and stored, is a 64-bit integer equal to `round(value × 10000)` **[code]** |
| **Distance** | Per axis: 0 inside the interval, otherwise the gap to the nearest edge. Total = **squared Euclidean sum** of those gaps: $D^2=\sum_{i=0}^{6}\max(v_i-\max_i,\;\min_i-v_i,\;0)^2$ |
| **Consequence** | Unnormalised and unweighted — **one unit of temperature costs exactly what one unit of continentalness costs.** A point inside a box scores 0 and wins outright; ties break on traversal order |
| **Coverage** | Nearest-box, not containment, so **the space is fully covered**: a point outside every box still resolves |
| **Search** | An R-tree — interior nodes hold the union box of their children. Best-first with pruning, seeded with the *previous query's* result, which is the single biggest optimisation because consecutive samples in a chunk are almost always the same biome |
| **The `offset` axis** | The sampled point's 7th coordinate is **always 0**; a biome stores `[offset, offset]`, so it contributes a constant `offset²` penalty forever. It is a **handicap, not a coordinate**. **Every Overworld entry uses offset 0** — the mechanism only does work in the Nether, where biomes are single points with offsets 0 / 0.175 / 0.375 |

### The overworld surface biome table **[wiki]**

Every surface entry is emitted **twice**, once at `depth = [0.0, 0.0]` and once at `[1.0, 1.0]`, always with **`offset = 0`** — so those two columns are constant for all 55 rows and are stated here once rather than repeated.

Cells resolve first to a **group**, which is then expanded by T, H and the sign of W. `grp` below names the group(s) a biome is reached through, and the C/E/PV columns are the **union envelope** of the cells that group occupies — the engine builds a per-biome union of boxes, so a single numeric range per biome is a summary, not the exact box set.

| grp | C | E | PV |
|---|---|---|---|
| **M** Middle | Coast … Far-inland | E0–E6 | Valleys … Peaks |
| **P** Plateau | Near … Far-inland | E0–E3 | Mid … Peaks |
| **S** Shattered | Coast … Far-inland | E5 only | Mid … Peaks |
| **D** Badland | Near … Far-inland | E0–E3 | Valleys … Peaks |
| **B** Beach | Coast only | E3–E6 | Low … Mid |

| Biome | T | H | C | E | PV | W | grp |
|---|---|---|---|---|---|---|---|
| mushroom_fields | all | all | −1.2 … −1.05 | all | all | ± | — |
| deep_frozen_ocean | T0 | all | −1.05 … −0.455 | all | all | ± | — |
| deep_cold_ocean | T1 | all | −1.05 … −0.455 | all | all | ± | — |
| deep_ocean | T2 | all | −1.05 … −0.455 | all | all | ± | — |
| deep_lukewarm_ocean | T3 | all | −1.05 … −0.455 | all | all | ± | — |
| frozen_ocean | T0 | all | −0.455 … −0.19 | all | all | ± | — |
| cold_ocean | T1 | all | −0.455 … −0.19 | all | all | ± | — |
| ocean | T2 | all | −0.455 … −0.19 | all | all | ± | — |
| lukewarm_ocean | T3 | all | −0.455 … −0.19 | all | all | ± | — |
| warm_ocean | T4 | all | −1.05 … −0.19 | all | all | ± | — |
| frozen_river | T0 | all | Coast … Far | E0–E6 | Valleys | ± | — |
| river | T1–T4 | all | Coast … Far | E0–E5 (+E6 Coast) | Valleys | ± | — |
| swamp | T1–T2 | all | Near … Far | E6 | Valleys–Mid | ± | — |
| mangrove_swamp | T3–T4 | all | Near … Far | E6 | Valleys–Mid | ± | — |
| stony_shore | all | all | Coast | E0–E2 | Low–Mid | ± | — |
| beach | T1–T3 | all | Coast | E3–E6 | Low–Mid | ± | B |
| snowy_beach | T0 | all | Coast | E3–E6 | Low–Mid | ± | B |
| snowy_slopes | T0–T2 | H0–H1 | Near … Far | E0–E1 | Low–Peaks | ± | — |
| grove | T0–T2 | H2–H4 | Near … Far | E0–E1 | Low–Peaks | ± | — |
| jagged_peaks | T0–T2 | all | Coast … Far | E0–E1 | High–Peaks | − | — |
| frozen_peaks | T0–T2 | all | Coast … Far | E0–E1 | High–Peaks | + | — |
| stony_peaks | T3 | all | Coast … Far | E0–E1 | High–Peaks | ± | — |
| snowy_plains | T0 | H0–H2 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P |
| ice_spikes | T0 | H0 | Coast … Far | E0–E6 | Valleys–Peaks | + | M, P |
| snowy_taiga | T0 | H2–H4 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P |
| plains | T1–T3 | H0–H2 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, S |
| flower_forest | T2 | H0 | Coast … Far | E0–E6 | Valleys–Peaks | − | M |
| sunflower_plains | T2 | H0 | Coast … Far | E0–E6 | Valleys–Peaks | + | M |
| forest | T1–T3 | H2 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P, S |
| taiga | T0–T1 | H3–H4 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P |
| birch_forest | T2 | H3 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P |
| old_growth_birch_forest | T2 | H3 | Coast … Far | E0–E6 | Valleys–Peaks | + | M |
| old_growth_spruce_taiga | T1 | H4 | Coast … Far | E0–E6 | Valleys–Peaks | − | M, P |
| old_growth_pine_taiga | T1 | H4 | Coast … Far | E0–E6 | Valleys–Peaks | + | M, P |
| dark_forest | T2 | H4 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M |
| jungle | T3 | H3–H4 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, P |
| sparse_jungle | T3 | H3 | Coast … Far | E0–E6 | Valleys–Peaks | + | M, S |
| bamboo_jungle | T3 | H4 | Coast … Far | E0–E6 | Valleys–Peaks | + | M, S |
| savanna | T3 | H0–H1 | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, S |
| desert | T4 | all | Coast … Far | E0–E6 | Valleys–Peaks | ± | M, S, B |
| meadow | T1–T2 | H0–H3 | Near … Far | E0–E3 | Mid–Peaks | ± | P |
| cherry_grove | T1–T2 | H0–H1 | Near … Far | E0–E3 | Mid–Peaks | + | P |
| savanna_plateau | T3 | H0–H1 | Near … Far | E0–E3 | Mid–Peaks | ± | P |
| pale_garden | T2 | H4 | Near … Far | E0–E3 | Mid–Peaks | ± | P |
| windswept_gravelly_hills | T0–T1 | H0–H1 | Coast … Far | E5 | Mid–Peaks | ± | S |
| windswept_hills | T0–T2 | H0–H2 | Coast … Far | E5 | Mid–Peaks | ± | S |
| windswept_forest | T0–T2 | H3–H4 | Coast … Far | E5 | Mid–Peaks | ± | S |
| windswept_savanna | T2–T4 | H0–H3 | Coast, Near | E5 | Low–Peaks | **+** | — |
| badlands | T4 | H0–H2 | Near … Far | E0–E3 | Valleys–Peaks | ± | D, P |
| eroded_badlands | T4 | H0–H1 | Near … Far | E0–E3 | Valleys–Peaks | + | D, P |
| wooded_badlands | T4 | H3–H4 | Near … Far | E0–E3 | Valleys–Peaks | ± | D, P |

Two structural notes. `desert` is reached three ways — it is the T4 column of *both* Middle and Shattered **and** the T4 entry of the Beach group, which is why deserts run straight into the sea with no beach. And `dappled_forest` (Middle H0/T1, W>0) is **26.3+** and is not in this 26.2 dump; the roster here is 65 biome files including `sulfur_caves` and `pale_garden`.

### Cave biomes, and how `depth` separates them **[wiki]**

Surface entries pin depth to exactly 0.0 or 1.0. Cave entries claim a *span* between and beyond those points, so anywhere with depth strictly between them the cave boxes are the nearest ones. That is the whole mechanism — cave biomes are a **layer underneath** the surface biome at the same XZ, not a replacement for it.

| Biome | T | H | C | E | D | W |
|---|---|---|---|---|---|---|
| dripstone_caves | all | all | **0.8 … 1.0** | all | **0.2 … 0.9** | all |
| lush_caves | all | **0.7 … 1.0** | all | all | **0.2 … 0.9** | all |
| deep_dark | all | all | all | **−1.0 … −0.375** (E0–E1) | **1.1** (a point) | all |
| sulfur_caves *(26.2+)* | all | all | **−0.19 … 0.55** | **0.45 … 1.0** (E5–E6) | **0.2 … 0.9** | **−1.1 … −0.85** |

Dripstone and lush **overlap** where C ≥ 0.8 and H ≥ 0.7 — a documented ambiguity (MC-262252). Deep dark is a point at depth 1.1, so it only wins *below* the depth-1.0 surface entries and only under low-erosion (mountainous) terrain. Sulfur caves' weirdness lower bound of −1.1 sits outside the noise's own −1…1 range — the same deliberate over-extension mushroom fields uses on continentalness.

## 15.3 Seed, noise and sampling resolution

### Seed → RNG

`legacy_random_source` picks the generator, and it is a per-preset field **[D]**:

| Preset | `legacy_random_source` | height | min_y | size_h | size_v | sea level |
|---|---|---|---|---|---|---|
| overworld / amplified / large_biomes | **false** → Xoroshiro128++ | 384 | −64 | 1 | 2 | 63 |
| caves | true → legacy LCG | 192 | −64 | 1 | 2 | 32 |
| nether | true | 128 | 0 | 1 | 2 | 32 |
| end | true | 128 | 0 | 2 | 1 | 0 |
| floating_islands | true | 256 | 0 | 2 | 1 | −64 |

The **legacy** source is `java.util.Random`: $s_{n+1}=(25214903917\,s_n+11)\ \&\ (2^{48}-1)$. Only 48 seed bits are reachable, which is why the first 1.18 experimental snapshot temporarily cut the seed space to 48-bit and 21w41a fixed it by swapping in xoroshiro **[wiki]**.

The **modern** path upgrades the 64-bit seed to 128 bits first **[code]**: $\ell=\text{seed}\oplus\texttt{0x6A09E667F3BCC909}$, $h=\ell+\texttt{0x9E3779B97F4A7C15}$, then both halves through `mixStafford13`. `Xoroshiro128PlusPlus.nextLong()` outputs $\text{rotl}(\ell+h,17)+\ell$.

### Per-noise seeding, and why it is the good idea

**Every noise is instantiated as `factory.fromHashOf(resourceLocation.toString())`** — the MD5 of the literal string `"minecraft:continentalness"`, split into two big-endian longs and XORed into the world seed halves. **[code]**

**The consequence is the whole point: adding a 68th noise to the registry cannot shift any existing one.** Under the legacy scheme noises were seeded by consuming longs in registration order, so inserting one anywhere reshuffled every noise after it and changed the terrain of every existing world. The same trick nests one level deeper — inside `PerlinNoise` each octave is seeded `fromHashOf("octave_" + n)`, so a zero amplitude skips constructing that octave without disturbing its neighbours.

The positional variant is `at(x,y,z)`, which XORs in `Mth.getSeed(x,y,z)`: $L=(x\cdot3129871)\oplus(z\cdot116129781)\oplus y$; $L=L\cdot L\cdot42317861+L\cdot11$; return $L\gg16$.

### `NormalNoise` construction

A `NormalNoise` is **two** `PerlinNoise` instances built back to back **[code]**:

$$N(x,y,z)=\bigl(P_1(x,y,z)+P_2(\kappa x,\kappa y,\kappa z)\bigr)\cdot V,\qquad \kappa=1.0181268882175227$$

$\kappa$ is deliberately just above 1 and irrational-ish, so the second copy drifts out of phase with the first and kills the grid-aligned artefacts a single Perlin field shows. The normalisation factor is

$$V=\frac{0.16666666666666666}{0.1\left(1+\frac{1}{k-j+1}\right)}$$

where $j$ and $k$ are the indices of the **first and last non-zero** amplitude. The $0.1(1+\tfrac{1}{n+1})$ term is the empirically measured standard deviation of a sum of $n{+}1$ octaves; dividing $1/6$ by it maps the output to roughly $[-1,1]$. Within one `PerlinNoise` with `firstOctave` $F$:

$$P(x,y,z)=\sum_{k=0}^{n-1} a_k\, I_k\!\left(2^{F+k}x,\,2^{F+k}y,\,2^{F+k}z\right)\cdot\frac{2^{n-1}}{2^{n}-1}\cdot 2^{-k}$$

Worked example, `continentalness` (`firstOctave −9`, `amplitudes [1,1,2,2,2,1,1,1,1]`): $j=0$, $k=8$, so $V = 0.1\overline{6}/(0.1\times\tfrac{10}{9}) = \mathbf{1.5}$, and $2^{n-1}/(2^n-1)=256/511=0.500978$. Because `overworld/continents` samples at `xz_scale: 0.25`, one noise unit is 4 blocks, so octave 0 has a **2048-block wavelength** (continent scale ≈ 2 km) and octave 8 an 8-block one. `y_scale: 0.0` — continentalness is strictly 2D.

### Resolution — the crux

| Field | Grid | Interpolated? | Cost per chunk |
|---|---|---|---|
| **Terrain density** | cells of **4 × 8 × 4** blocks (`size_horizontal: 1`, `size_vertical: 2`, cell = 4 × the value) | **Yes**, trilinear | $(4{+}1)^2\times(48{+}1)=\mathbf{1225}$ corner samples for 98,304 blocks — **≈ 80 blocks per density sample** |
| **2D fields** (`continents`, `erosion`, `ridges`, `offset`, `factor`, `jaggedness`) | one sample per **4×4 column** via `flat_cache` | **No** — nearest-lower quart wins | 25 evaluations instead of 256 |
| **Biomes** | one sample per **4×4×4 quart**, stored as a palette per subchunk | **No** | 4×4×4 quarts per subchunk |

Independently confirmed in the data: `preliminary_surface_level` carries `"cell_height": 8` **[D]**. Interpolation is trilinear, done as three nested passes (Y, then X, then Z) with fractions $\tfrac{y\&7}{8}$, $\tfrac{x\&3}{4}$, $\tfrac{z\&3}{4}$. The visible cost is mild terracing on very steep cliffs.

**The 4-block staircase on the 2D fields is invisible** because they feed `sloped_cheese`, which *is* interpolated downstream. And **only functions wrapped in `minecraft:interpolated` get the cell treatment** — in `final_density`, `noodle` sits *outside* the wrapper and is evaluated per block **[D]**, which is exactly why noodle caves are 1-block-wide worms while cheese caves are smooth blobs.

Biome storage is per quart, but **queries** (grass colour, mob spawning) add a per-block fuzz: subtract 2 from x/y/z, then for each of the 8 surrounding quart corners compute a "fiddled" squared distance offset by $\pm0.45$ quarts $=\pm1.8$ blocks from a chained LCG salted by the coordinates, and return the nearest corner's biome **[code]**. The salt is `SHA-256(worldSeed)`, deliberately decorrelated from the terrain noise. Net effect: biome borders wobble ±1.8 blocks instead of showing a clean 4-block staircase.

**`blend_alpha` / `blend_offset` / `blend_density` are inert in a fresh world.** They exist only to stitch 1.18+ terrain against chunks generated by an older format, using `BlendingData` stored in neighbouring chunk NBT. With no such chunk, `Blender.EMPTY` gives `blend_alpha = 1.0`, `blend_offset = 0.0`, `blend_density` = identity — **hard-code all three and lose nothing.** Every expression in §15.4 and §15.6 below is quoted with that collapse already applied.

## 15.4 Terrain shaping: splines, density, and the Y geometry

### World geometry **[D]**, `noise_settings/overworld.json`

| Field | Value | Line |
|---|---|---|
| `noise.min_y` | **−64** | 16 |
| `noise.height` | **384** → Y = −64 … 319 | 15 |
| `sea_level` | **63** | 401 |
| `noise.size_horizontal` | **1** → cell width 4 | 17 |
| `noise.size_vertical` | **2** → cell height 8 | 18 |
| `default_block` / `default_fluid` | `stone` / `water` | 3–11 |
| `aquifers_enabled` / `ore_veins_enabled` | `true` / `true` | 2, 400 |
| `legacy_random_source` `[JE]` | `false` | 13 |

`amplified.json` and `large_biomes.json` carry **identical** values for every field in that table.

### The three shaping outputs

| Output | File | What it physically does |
|---|---|---|
| `offset` | `density_function/overworld/offset.json` | **Base height of the column** — shifts the vertical density gradient up or down |
| `factor` | `.../factor.json` | **How strongly that height is enforced.** It multiplies depth, so a *large* factor makes density change fast with Y (flat, hard-to-disturb terrain) and a *small* factor lets the 3D noise wander far above and below (dramatic, chaotic terrain) |
| `jaggedness` | `.../jaggedness.json` | A **high-frequency spike term** added to depth, scaled by the `jagged` noise at `xz_scale: 1500.0` |

All three are `flat_cache(cache_2d(…))` — evaluated **once per XZ column** and reused for the whole vertical stack — and all three nest the same way: **continentalness → erosion → ridges**. With blending collapsed **[D]**:

```
offset     = -0.5037500262260437 + spline(continents…)
factor     = 10.0 + (-10.0 + spline(continents…))
jaggedness =  0.0 + ( -0.0  + spline(continents…))
```

**`offset`, level 1 (keyed on `continents`)** — the complete top level, all derivatives 0.0 **[D]**:

| location | value |
|---|---|
| −1.1 | 0.044 |
| −1.02 | −0.2222 |
| −0.51 | −0.2222 |
| −0.44 | −0.12 |
| −0.18 | −0.12 |
| −0.16 / −0.15 / −0.1 | → erosion spline **A** (7 points) |
| 0.25 / 1.0 | → erosion spline **B** (11 points) |

Two level-3 branches in full, to show the shape (`ridges_folded` → value):

| erosion | continents = −0.16 (ocean/coast) | continents = 1.0 (deep inland) |
|---|---|---|
| −0.85 | (−1.0, −0.08880186, d 0.38940096) · (1.0, 0.69000006, d 0.38940096) | (−1.0, 0.34792626) · (0.0, 0.9239631, d 0.5760369) · (1.0, **1.5**, d 0.5760369) |
| −0.35 | (−1.0, −0.3, d 0.5) · (−0.4, 0.05) · (0.0, 0.05) · (0.4, 0.05) · (1.0, 0.06) | (−1.0, −0.2, d 0.5) · (−0.4, 0.5) · (0.0, 0.5) · (0.4, 0.5) · (1.0, 0.6) |
| 0.2 | (−1.0, −0.15, d 0.5) · (−0.4, 0.0) · (0.0, 0.0) · (0.4, 0.0) · (1.0, 0.0) | (−1.0, −0.05, d 0.5) · (−0.4, 0.01) · (0.0, 0.01) · (0.4, 0.03) · (1.0, 0.1) |
| 0.7 | (−1.0, −0.02) · (−0.4, −0.03) · (0.0, −0.03) · (0.4, 0.0, d 0.06) · (1.0, 0.0) | (−1.0, −0.02) · (−0.4, 0.01) · (0.0, 0.01) · (0.4, 0.03) · (1.0, 0.1) |

**`factor` leaf values**, which is where the design is legible **[D]**. Top level keys on `continents` with a leaf `3.95` at −0.19 and erosion splines at −0.15 / −0.1 / 0.03 / 0.06; the ridges level is a lo/hi pair:

| erosion | ridges pair | c=−0.15 | c=−0.1 | c=0.03 | c=0.06 |
|---|---|---|---|---|---|
| −0.6 / −0.35 / −0.25 / 0.03 | (−0.2, 0.2) | 6.3 / 6.25 | 6.3 / 5.47 | 6.3 / 5.08 | 6.3 / 4.69 |
| −0.5 / −0.1 | (−0.05, 0.05) | 6.3 / 2.67 | ← | ← | ← |
| 0.45 / 0.55 | ridges_folded | 6.25 · 6.25/**0.625** | 5.47 · 5.47/0.625 | 5.08 · 5.08/0.625 | 6.3 / 4.69 · **1.37** |
| 0.58 / 0.62 | leaf | 6.25 | 5.47 | 5.08 | 4.69 |

**Low erosion → factor ≈ 6.3 (mountains held firmly); high erosion on a peak ridge → factor falls to 0.625–1.56**, which is where the 3D noise is allowed to run wild and produce shattered terrain.

**`jaggedness` is complete in ten rows** — it is zero everywhere except continentalness ≳ 0.03, erosion ≲ −0.375, and `ridges_folded` near 1.0. **Everything else in the world is smooth by construction.** Peak values are 0.63 (ridges −0.01) / 0.3 (ridges +0.01) at continentalness 0.65 across erosion −1.0 … −0.5775, halving to 0.315 / 0.15 at continentalness 0.03 for erosion −0.78 and −0.5775.

### The density chain, in order **[D]**

| Step | Definition |
|---|---|
| 1. `depth` | `y_clamped_gradient(-64 → 1.5, 320 → -1.5)` **+** `offset` |
| 2. `base_3d_noise` | `old_blended_noise` — `xz_scale 0.25, y_scale 0.125, xz_factor 80.0, y_factor 160.0, smear_scale_multiplier 8.0` |
| 3. `sloped_cheese` | `4.0 × quarter_negative((depth + flat_cache(jaggedness × half_negative(noise jagged @ xz 1500, y 0))) × factor)` **+** `base_3d_noise` |
| 4. cave carve | `range_choice` on `sloped_cheese` at **1.5625** — see §15.6 |
| 5. slides | bottom and top clamps, below |
| 6. `final_density` | `min(squeeze(interpolated(0.64 × <slides>)), overworld/caves/noodle)` |
| 7. solid test | `final_density > 0` → stone; otherwise air, or fluid supplied by the aquifer **[code]** |

The three shaping operations, all **[code]** for their semantics but present by name in the data:

| Op | Definition | Why |
|---|---|---|
| `quarter_negative(x)` | `x < 0 ? x/4 : x` | Compresses negative density so **air is easier to reach than stone** — terrain sits lower and caves open more readily than a symmetric version would give |
| `half_negative(x)` | `x < 0 ? x/2 : x` | Applied to the `jagged` noise, so spikes go **up twice as far as they go down** — peaks, not pits |
| `squeeze(x)` | `c = clamp(x,-1,1); c/2 - c³/24` | Outermost op of `final_density`; soft-limits into roughly ±0.4583 so extreme values do not produce razor cliffs |

**The slides**, literally as written at `noise_settings/overworld.json` lines 44–86:

```
0.1171875 + ycg(-64:0 → -40:1) * ( -0.1171875 + ( -0.078125 + ycg(240:1 → 256:0) * ( 0.078125 + <caves> ) ) )
```

| Slide | Target | Band | Effect |
|---|---|---|---|
| bottom | **+0.1171875** (solid) | Y −64 → −40 | Density forced solid at the floor, the world's own density fading in linearly over 3 cells |
| top | **−0.078125** (air) | Y 240 → 256 | Density forced to air at the ceiling, fading out over 2 cells. Amplified `[JE]` moves this to **304 → 320** |

**`preliminary_surface_level`** is new in 26.2 — it replaces the old `initial_density_without_jaggedness` router key with a data type `minecraft:find_top_surface` (`cell_height: 8`, `lower_bound: -64`, `upper_bound = clamp(128 - 128·(0.2734375·invert(factor) - offset), -40, 320)`). Its density argument is the same slide sandwich **with jaggedness and caves removed**, so the surface and aquifer systems get a stable "where is the ground" answer that caves cannot punch holes in.

### Y cutoffs, explicitly

| Thing | Y | Hard or probabilistic |
|---|---|---|
| World bottom / build limit | **−64** / **319** | hard |
| Terrain forced solid | ≤ **−64**, fading out to −40 | hard clamp |
| Terrain forced air | ≥ **256**, fading in from 240 (Amplified `[JE]`: 320 / 304) | hard clamp |
| **Bedrock floor** | **−64 … −60** | **probabilistic per block**, P = 1.0 / 0.8 / 0.6 / 0.4 / 0.2, zero at −59 |
| Bedrock roof | **none in the Overworld** | only `nether.json` has `bedrock_roof` |
| **Deepslate** | ≤ **8**, guaranteed at ≤ **0** | **probabilistic per block**, P = (8−Y)/8 |
| Sea level (water fill) | **63** | hard |
| Aquifer lava level | **−54** | hard — **[code]**, *not* in the data files |
| Ore-vein band | **−60 … 50** (`min_inclusive -60.0, max_exclusive 51.0`) | hard Y gate |

### The three overworld variants

Both variants are `[JE]` — Bedrock's world types are Infinite / Flat / Old — and both use the same biome-source preset; only the noise settings change.

**Amplified**, measured by flattening both spline trees and comparing every leaf: `offset` has **126 positive leaves doubled and 56 negative leaves unchanged** (land rises twice as far, ocean depths stay put); `jaggedness` has **every leaf exactly doubled**; `factor` leaves the ocean branches alone and replaces all 65 land leaves with `f' = f / (4 + 0.8·f)` — 6.3 → 0.697, 0.625 → 0.139. **That factor collapse, roughly 9× on land, is the real mechanism**: with the vertical gradient nearly flattened the 3D noise dominates and terrain shoots to the raised ceiling. Doubled offset and jaggedness are secondary.

**Large Biomes** changes **nothing numeric at all** — all 201 offset leaves, 86 factor leaves and 28 jaggedness leaves are bit-identical to the default. The splines simply key on noises at `firstOctave −11` instead of `−9`, i.e. **4× the horizontal wavelength**. Same terrain, drawn at four times the scale.

## 15.5 Surface rules — which block goes on top

`surface_rule` in `noise_settings/overworld.json`, lines 452–2748. **[D]**

### Semantics

`minecraft:sequence` is **first match wins** — evaluation stops at the first branch that yields a block, and a `condition` whose `then_run` produces nothing falls through to the next sibling. Rules run **top-down per column, after the density pass**; they only *replace* the default block and never create geometry.

The top level is four entries:

| # | Condition | Effect |
|---|---|---|
| 1 | `vertical_gradient` `bedrock_floor` | bedrock |
| 2 | `above_preliminary_surface` | the entire surface / beach / ocean-floor tree (lines 475–2724) |
| 3 | `biome = sulfur_caves` + 3D `sulfur_cave_gradient` bands | cinnabar / sulfur / cinnabar |
| 4 | `vertical_gradient` `deepslate` | deepslate |

### The conditions worth knowing

| Condition | Meaning |
|---|---|
| `stone_depth {surface_type, offset, add_surface_depth, secondary_depth_range}` | `floor` counts solid blocks **downwards from the top** of the current run of stone (block 0 = the topmost solid block); `ceiling` counts **upwards from the bottom** of the run, which is how `sandstone`/`stone` get placed on the underside of `sand`/`gravel`. `offset` is how deep the test reaches |
| **surface depth** | A per-column thickness sampled from the `minecraft:surface` noise (firstOctave −6, amplitudes `[1,1,1]`). Java computes `surfaceNoise × 2.75 + 3.0 + random × 0.25` **[code]** — roughly 0–6 blocks, and it is what makes the dirt layer under grass vary in thickness |
| `secondary_depth_range` | Extends the allowance by a second noise (`surface_secondary`, firstOctave −6, `[1,1,0,1]`) scaled by this range — **6** for beach sandstone, **30** for desert sandstone |
| `above_preliminary_surface` | Y ≥ the cave-free preliminary surface level (§15.4). **This is the gate that stops cave ceilings growing grass** |
| `water {offset, surface_depth_multiplier, add_stone_depth}` | True when `blockY (+stoneDepth) ≥ fluidLevel + offset + surfaceDepth × multiplier`, or there is no fluid. `offset 0, mult 0` = "not underwater"; `offset −1` = "at most 1 below water"; `offset −6, mult −1, add_stone_depth` = the wide shallow-seabed band |
| `y_above`, `not`, `hole`, `steep`, `temperature` | absolute Y test (optionally shifted by surface depth); negation, used to build Y *ranges* out of two `y_above` tests; surface depth ≤ 0; slope test (bare stone on cliffs); cold enough to freeze water |

### The three passes inside branch 2

| Pass | Gate | Produces |
|---|---|---|
| **top block** | `stone_depth(floor, 0)` + `water(offset −1)` | biome-specific top block; **fallback** `water(0,0)` → `grass_block{snowy:false}`, else `dirt` |
| **under-surface** | `water(offset −6, mult −1, add_stone_depth)` | snow_block / powder_snow / mud under the top block; **fallback** `dirt`; then beach and desert sandstone |
| **ocean floor** | `stone_depth(floor, 0)` | frozen/jagged peaks → `stone`; warm/lukewarm/deep-lukewarm ocean → `sand` (+`sandstone` ceiling); **everything else → `gravel`** (+`stone` ceiling) |

Biome-gated placements are a fixed deterministic list: beaches and desert → sand + sandstone; mushroom_fields → mycelium; mangrove_swamp → mud; ice_spikes → snow_block; frozen oceans → ice/air/water via `hole` + `temperature`; dripstone_caves → stone; the snow family for frozen_peaks / snowy_slopes / jagged_peaks / grove; calcite/gravel/stone for stony_peaks and stony_shore.

### Noise-threshold rules **[D]**

| Rule | Noise | min | max |
|---|---|---|---|
| windswept_hills → stone | `surface` | 0.12121212 (4/33) | +MAX |
| windswept_savanna → stone | `surface` | 0.21212121 (7/33) | +MAX |
| windswept_savanna → coarse_dirt | `surface` | −0.06060606 (−2/33) | +MAX |
| windswept_gravelly_hills → gravel | `surface` | 0.24242424 (8/33) | +MAX |
| windswept_gravelly_hills → stone | `surface` | 0.12121212 | +MAX |
| windswept_gravelly_hills → grass/dirt | `surface` | −0.12121212 | +MAX |
| old_growth_*_taiga → coarse_dirt | `surface` | 0.21212121 | +MAX |
| old_growth_*_taiga → podzol | `surface` | −0.11515151 | +MAX |
| swamp → water at Y 62 only | `surface_swamp` | 0.0 | +MAX |
| mangrove_swamp → water at Y 60–62 | `surface_swamp` | 0.0 | +MAX |
| stony_shore → gravel | `gravel` | −0.05 | 0.05 |
| stony_peaks → calcite | `calcite` | −0.0125 | 0.0125 |
| frozen_peaks → ice (exposed / submerged) | `ice` | 0.0 / −0.0625 | 0.025 |
| frozen_peaks → packed_ice (exposed / submerged) | `packed_ice` | 0.0 / −0.5 | 0.2 |
| snowy_slopes, grove → powder_snow (exposed) | `powder_snow` | 0.35 | 0.6 |
| snowy_slopes, grove → powder_snow (submerged) | `powder_snow` | 0.45 | 0.58 |
| sulfur_caves → cinnabar / sulfur / cinnabar | `sulfur_cave_gradient` (3D) | −0.4 / 0.0 / 0.4 | −0.1 / 0.4 / +MAX |

Backing noise parameters **[D]**: `surface` −6 `[1,1,1]` · `surface_secondary` −6 `[1,1,0,1]` · `surface_swamp` −2 `[1]` · `gravel` −8 `[1,1,1,1]` · `powder_snow` −6 `[1,1,1,1]` · `calcite` −9 `[1,1,1,1]` · `ice` −4 `[1,1,1,1]` · `packed_ice` −7 `[1,1,1,1]` · `jagged` −16 `[1×16]` · `clay_bands_offset` −8 `[1.0]`.

Two details worth stealing. **The `surface` thresholds are all n/33** — 4/33, 7/33, 8/33, −2/33, and the badlands set 6/33, 18/33, 30/33. And **the powder-snow band is narrower one layer down (0.45…0.58) than at the surface (0.35…0.6)**, so a pocket is wider at the top than at the bottom — a bowl, which is exactly what makes it behave as a trap.

### The two `vertical_gradient` bands

```jsonc
{ "type":"minecraft:vertical_gradient", "random_name":"minecraft:bedrock_floor",
  "true_at_and_below": {"above_bottom": 0}, "false_at_and_above": {"above_bottom": 5} }
{ "type":"minecraft:vertical_gradient", "random_name":"minecraft:deepslate",
  "true_at_and_below": {"absolute": 0},    "false_at_and_above": {"absolute": 8} }
```

**This is a genuinely random per-block coin flip, not a smooth blend.** Inside the band the gradient value *is* the probability; a positional RNG seeded from `random_name` plus the block coordinate is rolled independently for every single block **[code]**. There is no interpolation of the *result*, only of the odds — which is why the bedrock floor is a speckled five-layer mess and the stone↔deepslate boundary is a dithered scatter rather than a plane.

| Band | Y | P(rule fires) |
|---|---|---|
| bedrock | −64 / −63 / −62 / −61 / −60 / −59 | 1.0 / 0.8 / 0.6 / 0.4 / 0.2 / 0.0 |
| deepslate | ≤0 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / ≥8 | 1.0 / 0.875 / 0.75 / 0.625 / 0.5 / 0.375 / 0.25 / 0.125 / 0.0 |

### Badlands banding

Lines 706–950, gated on `biome ∈ {badlands, eroded_badlands, wooded_badlands}` **[D]**:

```
IF stone_depth(floor, 0):
   IF y_above absolute 256                    → orange_terracotta
   IF y_above absolute 74 (+stone_depth):
        surface ∈ [-0.909,-0.5454] | [-0.1818,0.1818] | [0.5454,0.909]  → terracotta
        else                                                            → minecraft:bandlands
   IF water(offset -1)                        → red_sand (+ red_sandstone on ceiling)
   IF not hole                                → orange_terracotta
   IF water(-6, mult -1, +depth)              → white_terracotta
   else                                       → stone / gravel
IF y_above absolute 63 (+stone_depth, mult -1):
   IF y_above 63 AND NOT y_above 74(+depth)   → orange_terracotta
   else                                       → minecraft:bandlands
```

`minecraft:bandlands` is a **rule type, not a condition** — an opaque generator holding a **192-entry array of terracotta colours built once per world seed**, indexed by absolute Y, with the index shifted by the `clay_bands_offset` noise so the bands wobble ±4 blocks horizontally **[code]**. Only the rule's presence and its surrounding gates are in the data.

### Random versus smooth

| Decision | Mechanism | Continuous or per-block random |
|---|---|---|
| Terrain height | splines + `old_blended_noise`, interpolated over 4×8 cells | **continuous** |
| Sea level fill | constant `sea_level: 63` + aquifer fluid picker | **deterministic**, no randomness |
| Grass vs dirt | position in the column; thickness from `surface` noise ×2.75 + 3.0 **+ random × 0.25** | **continuous** field, small per-column random jitter on thickness only |
| Coarse dirt / podzol patches | `noise_threshold` on `surface` | **continuous** — smooth blobs, not speckle |
| Sand on beaches | **biome** membership | **deterministic** given the biome map |
| Sandstone depth under sand | `secondary_depth_range` 6 / 30 on `surface_secondary` | **continuous** |
| Gravel on the ocean floor | **fallback** — last sibling of the pass | **deterministic** |
| Gravel on stony_shore | `noise_threshold` `gravel` ∈ [−0.05, 0.05] | **continuous** thin bands |
| Calcite / ice / packed ice / powder snow | `noise_threshold` on the matching noise | **continuous** pockets |
| **Bedrock layer, Y −64…−59** | `vertical_gradient` `bedrock_floor` | **per-block random roll**, probability interpolated |
| **Deepslate transition, Y 0…8** | `vertical_gradient` `deepslate` | **per-block random roll**, probability interpolated |
| Badlands bands | 192-entry seeded colour array indexed by Y | **per-seed random once**, then a deterministic function of Y; horizontal wobble continuous |
| Cave carving (noise caves) | `range_choice` + cave density functions | **continuous** |
| Ore veins (the big ones) | `vein_toggle` / `vein_ridged` / `vein_gap` noise | **continuous** |
| Ore blobs, trees, grass, lakes | per-chunk sequential RNG draws | **genuinely random**, seeded per chunk (§15.7) |
| Carver caves and ravines | per-chunk RNG, `probability` roll | **genuinely random**, seeded per chunk |

**Only two decisions in the entire overworld surface system are true per-block coin flips: bedrock and deepslate.** Everything else on the terrain side is a continuous noise field or a hard biome/geometry test; everything genuinely random happens later, in carving and decoration.

## 15.6 Caves and aquifers

Three noise systems exist because one noise field cannot produce three topologies at once. A single 3D noise thresholded against a constant gives blobs and only blobs — it has no mechanism for long thin tubes and none for flat winding sheets. **Each system is a different way of *reading* noise, not a different noise.**

| System | Read | Shape | Why it is needed |
|---|---|---|---|
| **Cheese** | plain 3D noise below a threshold | large irregular caverns | volume — the big open spaces |
| **Spaghetti** | the *zero-crossing* of a noise (`abs(n)` near 0) | long thin winding tubes | connectivity — links the rooms |
| **Noodle** | intersection of two ridged noises, gated by a third | 1–2 block-wide worms | detail and dead ends; carved last, over everything |

### The density expressions **[D]**

| System | Expression (blending collapsed) | The part that matters |
|---|---|---|
| **Cheese** — inlined in `final_density`, no file of its own | `4.0×square(cave_layer @xz1,y8)` + `clamp(0.27 + cave_cheese @xz1,y0.667, -1, 1)` + `clamp(1.5 - 0.64×sloped_cheese, 0, 0.5)` | Air needs `cave_cheese < −0.27`; that offset is what makes cheese a minority of volume rather than half of it. `4×cave_layer²` is squared so it only ever **adds solid**, and `y_scale 8` makes it band horizontally — which is what gives caves layered floors instead of one vertical smear. The third term suppresses cheese near the surface and contributes nothing in deep rock |
| **Spaghetti 2D** | `clamp(max(A,B), -1, 1)` where `A = abs(interval_select(5 scales of spaghetti_2d)) + 0.083×thick` and `B = cube(abs(8×elevation + ycg(-64:8 → 320:-40)) + thick)` | `thick = cache_once(-0.95 - 0.35000000000000003×spaghetti_2d_thickness)` ∈ **[−1.30, −0.60]** — the negative number that sets tube width. Zeroing `B`'s inner sum gives **y ≈ 64 × elevation**, so the sheet is warped into roughly y −64…+64 and `cube()` makes its edges sharp. The `interval_select` is a **rarity/scale switch**: one modulator noise picks which of five scales of the *same* noise is read, so width and spacing vary regionally instead of uniformly |
| **Spaghetti 3D**, inside `caves/entrances` = `min(opening, 3d)` | opening = `(0.37 + cave_entrance @xz0.75,y0.5) + ycg(-10:0.3 → 30:0.0)`; 3d = `roughness + clamp(max(abs A, abs B) - 0.0765 - 0.0115×thickness, -1, 1)` | The opening term adds +0.3 solid at y ≤ −10 falling to 0 at y ≥ 30, so **cave mouths exist near sea level and are suppressed deep down**. `max(abs A, abs B)` of two independent noises is the **ridged-intersection** trick: near zero only where *both* are, which in 3D is a curve rather than a surface — tubes, not sheets |
| **Noodle** | thickness ∈ **[−0.10, −0.05]** plus `1.5 × max(|ridge_a|, |ridge_b|)` | Negative only where that max is under ~0.033–0.067 — an extremely narrow band, hence 1–2 block bores. The `noodle` noise itself is a **binary regional gate**: `< 0` disables noodles entirely in that region. Y hard-limited to −60…320 |
| **Pillars**, which *add stone back* | `cache_once((2×pillar @xz25,y0.3 - 1 - pillar_rareness) × cube(0.55 + 0.55×pillar_thickness))` | The `xz_scale 25.0 / y_scale 0.3` ratio is the entire reason they come out as vertical columns |

Backing noises **[D]** — `firstOctave`, amplitudes `[1.0]` unless shown: `cave_cheese` −8 `[0.5,1,2,1,2,1,0,2,0]` · `cave_layer` −8 · `cave_entrance` −7 `[0.4,0.5,1]` · `spaghetti_2d`, `spaghetti_3d_1`, `spaghetti_3d_2`, `noodle_ridge_a`, `noodle_ridge_b`, `pillar` (`[1,1]`) −7 · `spaghetti_2d_modulator`, `spaghetti_2d_thickness`, `spaghetti_3d_rarity` −11 · `spaghetti_2d_elevation`, `spaghetti_3d_thickness`, `noodle`, `noodle_thickness`, `pillar_rareness`, `pillar_thickness` −8 · `spaghetti_roughness` −5 · `aquifer_barrier` −3 · `aquifer_lava` −1.

### How caves fold into terrain

**Caves are not subtracted from finished terrain.** `sloped_cheese` is an *input* to the cave expression, and the caves are combined with `min`/`max` inside the same function that decides stone-vs-air. A block is air when `final_density ≤ 0`.

| Step | Operation | Meaning |
|---|---|---|
| 1 | `range_choice` on `sloped_cheese` at **1.5625** | Where terrain density is low (thin rock, near surface) **only cave entrances apply, at 5× strength**. The full cave machinery runs only in solid rock — this is optimisation and design at once |
| 2 | `min(CHEESE, entrances)` | either can open air |
| 3 | `min(…, spaghetti_2d + roughness)` | spaghetti-2D can open air anywhere the above did not |
| 4 | `max(…, pillars if ≥ 0.03)` | pillars **put stone back**; below 0.03 the branch yields −1000000 so `max` is a no-op |
| 5 | slides, `×0.64`, `interpolated`, `squeeze` | world floor/ceiling forcing, cell interpolation, final curve |
| 6 | `min(…, noodle)` | **noodles apply last and outside everything**, including the surface `range_choice` and the slides — a noodle can cut through anything within y −60…320 |

`amplified` and `large_biomes` reference the **same five** `overworld/caves/*` functions; only the shaping functions are overridden. The `caves` world-type preset references none of them — it is a legacy generator (`legacy_random_source: true`, `aquifers_enabled: false`, sea level 32, height 192).

### Carvers — every file in `configured_carver/` **[D]**

| Field | `cave` | `cave_extra_underground` | `canyon` | `nether_cave` |
|---|---|---|---|---|
| **`probability`** | **0.15** | **0.07** | **0.01** | **0.2** |
| `y.min_inclusive` | `above_bottom 8` = −56 | `above_bottom 8` = −56 | `absolute 10` | `absolute 0` |
| `y.max_inclusive` | `absolute 180` | `absolute 47` | `absolute 67` | `below_top 1` |
| `y` distribution | uniform | uniform | uniform | uniform |
| `yScale` | uniform [0.1, 0.9) | uniform [0.1, 0.9) | 3.0 | 0.5 |
| `lava_level` | `above_bottom 8` = −56 | `above_bottom 8` | `above_bottom 8` | `above_bottom 10` |
| `horizontal_radius_multiplier` | uniform [0.7, 1.4) | same | via `shape` | 1.0 |
| `vertical_radius_multiplier` | uniform [0.8, 1.3) | same | via `shape` | 1.0 |
| `floor_level` | uniform [−1.0, −0.4) | same | — | −0.7 |
| `replaceable` | `#overworld_carver_replaceables` | same | same | `#nether_carver_replaceables` |

`canyon.shape`: `distance_factor` and `horizontal_radius_factor` uniform [0.75, 1.0); `thickness` **trapezoid** 0.0/6.0 plateau 2.0; `width_smoothness` 3; `vertical_rotation` uniform [−0.125, 0.125).

`#overworld_carver_replaceables` **[D]** covers the stone, substrate, sand and terracotta tags plus `water`, `gravel`, `sandstone`, `calcite`, `packed_ice` and the iron and copper ores. Two consequences worth knowing: **water is replaceable**, which is why ravines and caves cut open under oceans; and gold, diamond, redstone, lapis and coal ores are **not** in the tag, so carvers leave them floating in mid-air.

**Which biomes carry which carvers [D]** — and the answer is genuinely boring, which is itself the finding:

| Carver set | Count | Biomes |
|---|---|---|
| `["cave", "cave_extra_underground", "canyon"]` | **55** | **every overworld biome without exception** — all oceans, all rivers, mushroom_fields, deep_dark, dripstone_caves, lush_caves, sulfur_caves |
| `"nether_cave"` | 5 | the nether biomes |
| `[]` | 6 | the end biomes and `the_void` |

**Biome does not modulate carving at all in 26.2.** It modulates decoration only.

`probability` is drawn **once per candidate chunk per carver** — `nextFloat() <= probability` **[code]**. A chunk that passes becomes a *start chunk* and then spawns a random number of tunnel systems.

### Cave biomes change decoration, nothing else **[D]**

All four use the identical carver list and identical noise settings.

| Biome | Additions vs `plains` | Removals |
|---|---|---|
| `lush_caves` | `lush_caves_ceiling_vegetation`, `cave_vines`, `lush_caves_clay`, `lush_caves_vegetation`, `rooted_azalea_tree`, `spore_blossom`, `classic_vines_cave_feature`, `ore_clay` | surface plants and trees |
| `dripstone_caves` | `large_dripstone` (step 2), `dripstone_cluster` + `pointed_dripstone` (step 7), `ore_copper_large` replaces `ore_copper` | — |
| `deep_dark` | `sculk_vein`, `sculk_patch_deep_dark` (step 7) | **no `lake_lava_underground`, no `lake_lava_surface`, no `spring_water`, no `spring_lava`** — the biome is dry |
| `sulfur_caves` | `rooted_sulfur_spring`, `sulfur_pool` (step 1), `sulfur_spike_cluster`, `sulfur_spike` (step 7) | most surface plants |

### Aquifers

An aquifer is a **local water table**. Instead of "everything below y 63 that is open is water", the world is divided into aquifer cells, each independently deciding its own fluid surface height and whether it holds water, lava or air. That is what produces an underwater cave lake at y −40 and a dry cave at y 20 in the same world.

From data **[D]**, the `noise_router` slots: `barrier` = `aquifer_barrier @ xz 1.0, y 0.5`; `fluid_level_floodedness` = `aquifer_fluid_level_floodedness @ y 0.67`; `fluid_level_spread` = `aquifer_fluid_level_spread @ y 0.714…`; `lava` = `aquifer_lava @ 1.0, 1.0`; plus `preliminary_surface_level` (§15.4), which is a **column-only, cave-free** estimate of ground height so aquifers know how deep they are.

From engine code **[code]**, so verify before relying on any of it:

| Constant | Value | Confidence |
|---|---|---|
| Aquifer cell size | 16 × 12 × 16 blocks | high |
| Global fluid picker | `y < min(-54, seaLevel)` → **lava at level −54**; otherwise water at **63** | high — matches observable lava-lake depth in both editions |
| Aquifer skipped entirely | if `y − 12 > preliminarySurfaceLevel + 8` → use the global picker | medium |
| Floodedness thresholds | −0.3…0.8 (fully flooded) and −0.8…0.4 (fully dry) against surface distance over a 64-block window | medium |
| Fluid-level Y band | `floorDiv(y, 40) * 40 + 20`, then spread quantised into 3-block steps | **low — unverified** |
| Deep dark forces the aquifer below world bottom ⇒ **deep dark caves are always dry** | — | high; corroborated by the data, which gives deep_dark no springs and no lava lakes |
| `aquifer_barrier` decides whether the boundary between two cells at different levels is sealed stone or open | — | medium |

### Every cave-related chance in one table

| Thing | Value | Per what | Exactness |
|---|---|---|---|
| `cave` carver start | **0.15** | per chunk, per carver draw | **[D]** |
| `cave_extra_underground` carver start | **0.07** | per chunk (y −56…47 only) | **[D]** |
| `canyon` (ravine) carver start | **0.01** | per chunk — 1 in 100 | **[D]** |
| `nether_cave` carver start | **0.2** | per chunk (Nether) | **[D]** |
| Cheese threshold | `cave_cheese < −0.27` before the two modifier terms | per sample point | **[D]** |
| Pillar gate | `pillars ≥ 0.03` places stone | per sample point | **[D]** |
| Noodle regional gate | `noodle < 0` ⇒ noodles disabled | per region | **[D]** |
| Carver neighbourhood scanned per generated chunk | ±8 chunks → **17 × 17 = 289** | per chunk generated | **[code]** |
| Expected `cave` start chunks influencing one chunk | 289 × 0.15 ≈ **43** | per chunk | derived |
| Tunnel clusters per successful start | `nextInt(nextInt(nextInt(15)+1)+1)` — **mean 1.75**, range 0–14 | per start chunk | **[code]** |
| Chance a cluster also makes a wide "room" | `nextInt(4)==0` → **25%**, then +`nextInt(4)` extra tunnels | per cluster | **[code]** |
| Cave tunnel max reach | ~**112 blocks** (range 4 chunks) | per tunnel | **[code]** |
| `amethyst_geode` | rarity 1/24, y `above_bottom 6` … 30 | per chunk | **[D]** |
| `lake_lava_underground` | rarity **1/9** | per chunk | **[D]** |
| `fossil_lower` / `fossil_upper` | rarity 1/64 each | per chunk | **[D]** |
| `monster_room` (dungeon) | `count: 10` attempts, y 0…top | per chunk | **[D]** |
| `monster_room_deep` | `count: 4` attempts, y `above_bottom 6` … −1 | per chunk | **[D]** |
| `spring_water` | `count: 25`, y `above_bottom 0` … 192 | per chunk | **[D]** |
| `spring_lava` | `count: 20`, `very_biased_to_bottom` inner 8 | per chunk | **[D]** |

## 15.7 Ores, features and the placement RNG

### The placement-modifier vocabulary

Every modifier is a **position transformer**: it takes a stream of positions and emits zero or more. They run in array order, and **the array order is not canonical** — `flower_plains` counts before it filters, `patch_grass_plain` filters before it counts. Census of all 264 files in `placed_feature/` **[D]**:

| Modifier | Uses | What it does |
|---|---|---|
| `biome` | 208 | Drops the position if the biome **at that position** does not list this feature. This is the guard against a feature bleeding across a biome edge |
| `in_square` | 195 | Adds `nextInt(16)` to X and to Z — spreads a chunk-origin position over the chunk |
| `count` | 193 | Duplicates each position N times; N may be an int provider |
| `block_predicate_filter` | 121 | Drops unless a block predicate passes (`would_survive`, `replaceable`, `solid`, `matching_blocks`, …) |
| `heightmap` | 108 | Snaps Y to a heightmap column value |
| `height_range` | 86 | Chooses Y from a vertical distribution |
| `random_offset` | 75 | Jitters X/Z and Y independently |
| `rarity_filter` | 54 | Keeps each position with probability **1/N** |
| `surface_water_depth_filter` | 25 | Drops if the water column above the ocean floor exceeds N |
| `environment_scan` | 11 | Walks up or down until a target predicate matches; drops if not found in `max_steps` |
| `count_on_every_layer` | 8 | Nether-only — finds every open floor layer in the column and places N per layer |
| `noise_threshold_count` | 6 | Count is a two-way switch on a low-frequency noise |
| `noise_based_count` | 4 | Count scales continuously with noise |
| `surface_relative_threshold_filter` | 3 | Drops unless `y − heightmap(x,z)` is within bounds |
| `fixed_placement` | 1 | Ignores the incoming position; emits literal coordinates |

Heightmap semantics **[code]**: `WORLD_SURFACE*` = highest non-air; `OCEAN_FLOOR*` = highest motion-blocking block, so water does **not** count → the sea bed; `MOTION_BLOCKING` = highest block that blocks motion **or is a fluid** → the water surface. `_WG` variants are the worldgen-time snapshot. The stored value is *one above* the top block, i.e. the first free cell.

| Vertical distribution | Uses | Shape | Algorithm **[code]** |
|---|---|---|---|
| `uniform` | 92 | flat slab | `randomBetweenInclusive(min, max)` |
| `trapezoid` (`plateau` absent ⇒ 0) | 137 | **triangle** peaking at the band midpoint, tapering to zero at both edges | `k = max−min; l = (k−plateau)/2; m = k−l; y = min + rand(0..m) + rand(0..l)` — a sum of two uniforms |
| `very_biased_to_bottom` | 2 | strongly bottom-weighted | three nested draws |
| `clamped_normal` | 4 | Gaussian, clipped | `mean`, `deviation`, `min`, `max` |
| `weighted_list` | 23 (all trees) | discrete weighted pick | `{data:N, weight:9},{data:N+1, weight:1}` ≡ the old `count_extra(N, 0.1, 1)` |

**The trapezoid is the single most important one for ores.** A trapezoid over `[−64, 320]` does not mean "uniform in that band" — density rises linearly to a peak at the midpoint and falls linearly away. Anchors: `absolute: N` → `y = N`; `above_bottom: N` → `y = minY + N`; `below_top: N` → `y = maxY − N`.

### The complete ore table **[D]**

Placement from `placed_feature/`, vein size and air-exposure discard from the matching `configured_feature/`.

| Placed feature | Count / rarity | Height distribution | `size` | `discard_chance_on_air_exposure` |
|---|---|---|---|---|
| `ore_coal_upper` | 30 | uniform 136 → 319 | 17 | 0.0 |
| `ore_coal_lower` | 20 | **trapezoid** 0 → 192 | 17 | **0.5** |
| `ore_iron_upper` | 90 | **trapezoid** 80 → 384 | 9 | 0.0 |
| `ore_iron_middle` | 10 | **trapezoid** −24 → 56 | 9 | 0.0 |
| `ore_iron_small` | 10 | uniform −64 → 72 | 4 | 0.0 |
| `ore_copper` | 16 | **trapezoid** −16 → 112 | 10 | 0.0 |
| `ore_copper_large` *(dripstone_caves)* | 16 | **trapezoid** −16 → 112 | 20 | 0.0 |
| `ore_gold` | 4 | **trapezoid** −64 → 32 | 9 | **0.5** |
| `ore_gold_lower` | uniform 0..1 | uniform −64 → −48 | 9 | **0.5** |
| `ore_gold_extra` *(3 badlands)* | 50 | uniform 32 → 256 | 9 | 0.0 |
| `ore_redstone` | 4 | uniform −64 → 15 | 8 | 0.0 |
| `ore_redstone_lower` | 8 | **trapezoid** −96 → −32 | 8 | 0.0 |
| `ore_diamond` | 7 | **trapezoid** −144 → −16 | 4 | **0.5** |
| `ore_diamond_medium` | 2 | uniform −64 → −4 | 8 | **0.5** |
| `ore_diamond_large` | **rarity 1/9** | **trapezoid** −144 → −16 | 12 | **0.7** |
| `ore_diamond_buried` | 4 | **trapezoid** −144 → −16 | 8 | **1.0** |
| `ore_lapis` | 2 | **trapezoid** −32 → 32 | 7 | 0.0 |
| `ore_lapis_buried` | 4 | uniform −64 → 64 | 7 | **1.0** |
| `ore_emerald` *(10 mountain biomes)* | 100 | **trapezoid** −16 → 480 | **3** | 0.0 |
| `ore_infested` *(same 10)* | 14 | uniform −64 → 63 | 9 | 0.0 |
| `ore_dirt` | 7 | uniform 0 → 160 | 33 | 0.0 |
| `ore_gravel` | 14 | uniform −64 → 319 | 33 | 0.0 |
| `ore_granite/diorite/andesite_upper` | **rarity 1/6** | uniform 64 → 128 | 64 | 0.0 |
| `ore_granite/diorite/andesite_lower` | 2 | uniform 0 → 60 | 64 | 0.0 |
| `ore_tuff` | 2 | uniform −64 → 0 | 64 | 0.0 |
| `ore_clay` *(lush_caves)* | 46 | uniform −64 → 256 | 33 | 0.0 |

`discard_chance_on_air_exposure` **[code]**: for each block the vein would place, if any of the six neighbours is air, discard that block with this probability. **1.0 means the vein is only visible where fully buried** — which is exactly why `ore_diamond_buried` and `ore_lapis_buried` never appear in a cave wall, and it is a deliberate anti-strip-mining lever, not a cosmetic one.

**Biome scoping is almost nil.** The standard 25-entry overworld ore list appears identically in **55 biomes**; only five ores are scoped at all — `ore_copper_large` → dripstone_caves; `ore_gold_extra` → the three badlands; `ore_emerald` and `ore_infested` → the ten mountain biomes; `ore_clay` → lush_caves.

### Ore veins — a separate, noise-driven system

The large copper and iron veins are **not** placed features. They are part of the noise router and run **during terrain shaping, before decoration** **[D]**:

| Router slot | Definition |
|---|---|
| `vein_toggle` | `interpolated(range_choice(y, −60 ≤ y < 51, noise ore_veininess @ 1.5/1.5, else 0.0))` |
| `vein_ridged` | `−0.07999999821186066 + max(abs(ore_vein_a @ 4.0/4.0), abs(ore_vein_b @ 4.0/4.0))` |
| `vein_gap` | `noise ore_gap @ 1.0, 1.0` |

Noises **[D]**: `ore_veininess` −8, `ore_vein_a`/`_b` −7, `ore_gap` −5, all amplitude `[1.0]`.

Consumption **[code]** (`OreVeinifier`): the **sign** of `vein_toggle` picks the type — positive → **copper** in `granite` filler, y 0…50; negative → **iron** (`deepslate_iron_ore`) in `tuff` filler, y −60…−8. An edge roll-off maps distance-to-band-edge over 20 blocks from −0.2 to 0.0 and is added to `|toggle|`; reject below the **0.4** veininess threshold; reject **30%** of what remains; reject if `vein_gap ≥ −0.3`; otherwise ore probability is `clampedMap(|toggle|, 0.4→0.6, 0.1→0.3)` and additionally requires `vein_ridged > −0.3`; a **2%** sub-roll upgrades the ore to the raw-metal block. **Cells that pass the vein test but fail the ore roll become the filler stone** — that is why veins read as granite or tuff blobs streaked with metal rather than as solid ore.

### Trees and vegetation **[D]**

`w-list N/N+1` = `weighted_list [{N,9},{N+1,1}]`, mean `N + 0.1`.

| Biome | Placed feature | Count | Mean attempts/chunk | Extra filters |
|---|---|---|---|---|
| plains | `trees_plains` | w-list 0/1 | **0.05** | water depth 0, `OCEAN_FLOOR`, `would_survive(oak_sapling)` |
| forest | `trees_birch_and_oak_leaf_litter` | w-list 10/11 | **10.1** | water depth 0, `OCEAN_FLOOR` |
| birch_forest | `trees_birch` | w-list 10/11 | **10.1** | + `would_survive(birch_sapling)` |
| dark_forest | `dark_forest_vegetation` | `count 16` | **16** | water depth 0, `OCEAN_FLOOR` |
| taiga | `trees_taiga` | w-list 10/11 | **10.1** | — |
| savanna | `trees_savanna` | w-list 1/2 | **1.1** | — |
| jungle | `trees_jungle` | w-list 50/51 | **50.1** | — |
| sparse_jungle | `trees_sparse_jungle` | w-list 2/3 | **2.1** | — |
| bamboo_jungle | `bamboo_vegetation` | w-list 30/31 | **30.1** | — |
| swamp | `trees_swamp` | w-list 2/3 | **2.1** | water depth **2** |
| mangrove_swamp | `trees_mangrove` | `count 25` | 25 | water depth **5** |
| meadow | `trees_meadow` | rarity 1/100 | 0.01 | — |

**"How many trees per chunk in plains" is 0.05 — one attempt per twenty chunks**, and that attempt can still fail the sapling-survival check. Jungle is a thousand times denser. This spread is the single biggest lever on how a biome reads.

Species mix is a `random_selector`: entries are tried in order, first hit wins, otherwise the default. `trees_savanna` = `acacia` at **0.8** else `oak`; `trees_taiga` = `pine` 0.333 else `spruce`; `trees_plains` = `fancy_oak_bees_005` 0.333 else `oak_bees_005`; `trees_jungle` = `fancy_oak` 0.1, `jungle_bush` 0.5, `mega_jungle_tree` 0.333, else `jungle_tree`. Every list also carries a `fallen_*_tree` at **0.0125**.

Ground cover: `origins/chunk` survive the outer count, then each origin spawns `inner` scatter attempts through `random_offset` plus a ground predicate. Worked example for plains — grass ≈ 10 origins × 32 = **320 short-grass attempts per chunk**; flowers ≈ 4 × 1/32 × 64 ≈ **8 per chunk**; tall grass ≈ 7/32 × 96 ≈ **21**; pumpkin 96/300 = **0.32**. Rarities worth knowing: `patch_pumpkin` 1/300, `brown_mushroom_normal` 1/256, `red_mushroom_normal` 1/512, `patch_sugar_cane` 1/6, `flower_default` 1/32, `bamboo_light` 1/4.

**Sapling growth, which is a different system from tree *placement*.** Saplings have **2 growth stages**; the block **above** needs **light ≥ 9**. Bone meal bypasses the light requirement at **45% per use**. Space is checked before growth and failure means no growth: oak needs ≥5 vertical and 3×3; spruce ≥6 and 5×5; cherry ≥8 and 5×5; giant spruce ≥14 and 6×6. **If a block blocks an oak's growth space but is not directly above it, the oak is forced to grow the large variant instead of failing.**

### Decoration step order **[D]**

The biome `features` field is a fixed-length array of arrays; the index **is** the decoration step. 57 of 68 biomes carry exactly 11 slots.

| # | Step | What actually occupies it |
|---|---|---|
| 0 | `RAW_GENERATION` | `end_island_decorated` |
| 1 | `LAKES` | `lake_lava_surface`, `lake_lava_underground`, `sulfur_pool`, `rooted_sulfur_spring` |
| 2 | `LOCAL_MODIFICATIONS` | `amethyst_geode`, `forest_rock`, `iceberg_*`, `large_dripstone`, `basalt_pillar` |
| 3 | `UNDERGROUND_STRUCTURES` | `monster_room`, `monster_room_deep`, `fossil_upper`, `fossil_lower` |
| 4 | `SURFACE_STRUCTURES` | `desert_well`, `ice_spike`, `ice_patch`, `blue_ice`, `delta`, basalt columns, end spikes |
| 5 | `STRONGHOLDS` | **empty in every biome** — strongholds are a structure, not a feature |
| 6 | `UNDERGROUND_ORES` | 34 entries — every `ore_*` plus `disk_sand`/`disk_clay`/`disk_gravel`/`disk_grass` |
| 7 | `UNDERGROUND_DECORATION` | 30 entries — nether ores, `dripstone_cluster`, `glowstone`, `sculk_*` |
| 8 | `FLUID_SPRINGS` | `spring_water`, `spring_lava`, `spring_lava_frozen` |
| 9 | `VEGETAL_DECORATION` | 115 entries — trees, grass, flowers, mushrooms, kelp, vines |
| 10 | `TOP_LAYER_MODIFICATION` | `freeze_top_layer`, `end_platform`, `void_start_platform` |

### The placement RNG, and the trap in it **[code]**

```
setDecorationSeed(levelSeed, blockX, blockZ):
    setSeed(levelSeed)
    a = nextLong() | 1
    b = nextLong() | 1
    populationSeed = (blockX * a + blockZ * b) XOR levelSeed
    setSeed(populationSeed)

setFeatureSeed(populationSeed, featureIndex, decorationStep):
    setSeed(populationSeed + featureIndex + 10000 * decorationStep)
```

`blockX`/`blockZ` are the chunk's origin block coordinates. The `| 1` forces both multipliers odd so the chunk → population-seed mapping is a bijection. The `10000 × step` gap is what stops feature indices colliding across decoration stages. **`featureIndex` is a global running counter across the whole decoration pass for that chunk, not an index within the step.**

**The consequence is a hard design constraint.** Because the index is a plain running counter added to the population seed, **inserting a feature, deleting one, or reordering two within a step shifts the index of every feature after it in that chunk's pass.** Every downstream feature then draws from a different seed — the trees move, the flowers move, the ore veins move. So a biome's feature list is effectively a **versioned, append-mostly schema**: adding a decoration at the end of `VEGETAL_DECORATION` is cheap; inserting one in the middle silently invalidates every saved world.

## 15.8 What else a biome carries

**26.2 has changed the schema versus 1.21, and it matters if this dump is used as the source of truth.** A census of all 68 biome files returns **zero hits** for `fog_color`, `sky_color`, `water_fog_color`, `mood_sound`, `music`, `ambient_sound` and `additions_sound` — they are gone from every file. `random_patch` is also gone: grass and flower patches are now a bare `simple_block` configured feature with the scatter expressed entirely as placement modifiers. **If per-biome sky and fog colours are ever wanted, they must come from a 1.21 dump; they are not here.**

| Biome | `temperature` | `downfall` | `has_precipitation` | notes |
|---|---|---|---|---|
| plains | 0.8 | 0.4 | true | water `#3f76e4` |
| forest | 0.7 | 0.8 | true | |
| birch_forest | 0.6 | 0.6 | true | |
| dark_forest | 0.7 | 0.8 | true | `grass_color_modifier: dark_forest`, `dry_foliage_color #7b5334` |
| taiga | 0.25 | 0.8 | true | |
| snowy_taiga | **−0.5** | 0.4 | true | water `#3d57d6` |
| snowy_plains | 0.0 | 0.5 | true | `creature_spawn_probability 0.07` |
| jungle | 0.95 | 0.9 | true | |
| swamp | 0.8 | 0.9 | true | water `#617b64`, foliage `#6a7039`, `grass_color_modifier: swamp` |
| cherry_grove | 0.5 | 0.8 | true | foliage/grass `#b6db61`, water `#5db7ef` |
| pale_garden | 0.7 | 0.8 | true | water `#76889d`, foliage `#878d76`, grass `#778272` |
| mushroom_fields | 0.9 | 1.0 | true | |
| jagged_peaks | **−0.7** | 0.9 | true | |
| frozen_ocean | 0.0 | 0.5 | true | `temperature_modifier: frozen`, water `#3938c9` |
| warm_ocean | 0.5 | 0.5 | true | water `#43d5ee` |
| desert | 2.0 | 0.0 | **false** | |
| savanna | 2.0 | 0.0 | **false** | |
| badlands | 2.0 | 0.0 | **false** | `creature_spawn_probability 0.03`, foliage `#9e814d`, grass `#90814d` |

Full census **[D]**: `creature_spawn_probability` appears in only **5** biomes (badlands 0.03, eroded_badlands 0.03, wooded_badlands 0.04, snowy_plains 0.07, ice_spikes 0.07; the default is 0.1 **[code]**). `temperature_modifier` appears in only **2**, both `frozen`. `grass_color_modifier` in 4; `foliage_color` in 8; `grass_color` in 7; `dry_foliage_color` in 5.

**Snow vs rain vs nothing [code]:**

```
if !has_precipitation                          -> no weather at all
else if heightAdjustedTemperature(pos) >= 0.15 -> rain
else                                           -> snow
```

`0.15` is also the ice and snow-layer threshold, and `freeze_top_layer` (step 10, present in every overworld biome) is what actually lays snow and surface ice at generation time using the same test.

**Altitude temperature falloff [code]:**

```
threshold = seaLevel + 17            // 63 + 17 = 80
if y > threshold:
    n = TEMPERATURE_NOISE(x/8, z/8) * 8.0
    T = baseTemperature - (n + y - threshold) * 0.05 / 40.0
```

That is **0.00125 per block above y = 80**, plus a ±0.01 wobble from the noise term. A plains biome at 0.8 does not drop below 0.15 until roughly **y = 600** — so the falloff matters only for biomes that already start near freezing, never for lowland ones. `temperature_modifier: frozen` overrides all of it with a two-noise test that snaps temperature to 0.2 in patches, which is what gives frozen oceans their irregular unfrozen holes.

**Spawners.** Eight categories **[D]**: `monster`, `creature`, `ambient`, `axolotls`, `underground_water_creature`, `water_creature`, `water_ambient`, `misc`. Each entry is `{type, weight, minCount, maxCount}` and **weight is relative within its category** — the category is picked first by the spawn tick's per-category cap, then one entry is drawn weighted, then `minCount..maxCount` mobs are packed at the site **[code]**. §4 covers the spawning mechanics themselves; what the biome adds is only the table.

The common overworld `monster` set is spider w100, zombie w95, zombie_villager w5 1–1, skeleton w100, creeper w100, slime w100, enderman w10 1–4, witch w5 1–1, and biomes override it rather than replace it — desert swaps in husk w80 and parched w50 while dropping zombie to w19 and skeleton to w50; swamp adds bogged w30 and drops skeleton to w70; ocean adds drowned w5. `creature` is the four farm animals (sheep w12, pig w10, chicken w10, cow w8, all 4–4) plus biome specials: plains horse w5 2–6 and donkey w1 1–3; taiga wolf w8, rabbit w4, fox w8; jungle parrot w40 and panda w1; swamp frog w10 2–5; desert replaces the lot with rabbit w12 and camel w1. `ambient` is bat w10 8–8 everywhere and `underground_water_creature` glow_squid w10 4–6. **Two data quirks worth copying deliberately or not at all:** jungle lists **chicken twice** (effective weight 20) and swamp lists **slime twice**.

**`spawn_costs`** is used by exactly two biomes in the whole dump **[D]**: `soul_sand_valley` (enderman, ghast, skeleton, strider — each `energy_budget 0.15`, `charge 0.7`) and `warped_forest` (enderman, `energy_budget 0.12`, `charge 1.0`). The mechanic **[code]** is a *potential-field density limiter* separate from the mob cap: every spawned mob deposits a point charge, and before a new spawn the game sums the field from nearby charges, multiplies by the candidate's own `charge`, and refuses if the result exceeds `energy_budget`. Net effect: fewer but evenly spread mobs instead of dense clumps. **Bedrock does not have this system** — it uses per-entity `spawn_rules` with `minecraft:density_limit` as an integer per-chunk cap, which is considerably simpler and is the correct model if we follow Bedrock.

**Colour, in resolution order [code]:**

1. **Explicit override** — if the biome sets `grass_color` or `foliage_color`, that literal RGB wins and the colormap is skipped entirely. Only 7 and 8 biomes respectively do this **[D]**.
2. **Colormap lookup** — otherwise sample a 256×256 texture. All three exist in this dump **[D]**: `assets/minecraft/textures/colormap/grass.png` (5,930 bytes), `foliage.png` (14,154), `dry_foliage.png` (8,979). The lookup is `t = clamp(temperature,0,1)`, `h = clamp(downfall,0,1)`, `x = (1−t)×255`, `y = (1−h·t)×255`. **Because `h` is multiplied by `t`, a cold biome is pulled toward the dry corner regardless of its rainfall** — which is why taiga (0.25 / 0.8) reads olive rather than lush green.
3. **`grass_color_modifier`**, applied after the lookup. `dark_forest` → `((colour & 0xFEFEFE) + 0x28340A) >> 1`, i.e. average with a fixed dark olive; `swamp` → discard the lookup and return the constant `0x6A7039`.
4. `dry_foliage_color` is a separate literal for leaf litter and dried vegetation, independent of the colormap. Water is never colormapped — `water_color` is always a literal.

**Stated as fact, with no recommendation attached:** this project shipped biome-tinted foliage at M19a and reverted it — a mid-grey source texture multiplied by a mid-green tint read as pale sage. Nothing in this data changes that, because **the colormap assumes source textures authored as near-white greyscale masks**, not as finished mid-tone art. That is a fact about our textures, not about the mechanism.

## 15.9 ▶ Where we stand

> ⛔ **Superseded on 2026-08-07, the same day it was written.** The generator was
> then rebuilt on this section's architecture, so everything below describes the
> world **as it was before that**. It is kept because the comparison is the
> argument for the rebuild, not because it is current.
>
> **What the world does now**, in one paragraph: five climate fields on the
> reference's octave weights at our own wavelengths; `offset` / `factor` /
> `jaggedness` splines with linear extrapolation; **height computed directly per
> column**, since terrain is single-valued and an interpolation lattice would buy
> nothing but its own banding; surface rules as a top-down
> walk over the **uncarved** column with a noise-driven depth, with bed materials
> gated on the waterline rather than on the biome; three cave systems
> carved at full resolution with 2D cave mouths; ores on trapezoid bands with
> air-exposure discard on the deepest three; and **twenty-seven** biomes each
> claiming a box in the five-dimensional space, resolved first-match-then-nearest,
> with a `BiomeTag` bitmask so the creature rules ask for a property rather than
> naming biomes. Rivers fall out of the ridge field's zero contour. Water fills
> only what the uncarved terrain left empty, so caves under the waterline stay
> dry — which is what aquifers buy the reference and costs us nothing.
>
> Every field gain and every ore threshold in that list was **measured by a
> temporary startup probe**, not derived. `SYSTEM_MEMORY.md` → "World Generation"
> is the current truth and **M20p–M20r** in `TIMELINE.md` are the milestones.

> **▶ Where we stand.** Every claim here was checked against the source, not the docs.
>
> **Biomes.** Seven, in [game/src/world/Biome.cpp](game/src/world/Biome.cpp), chosen by proximity in a **2D** temperature/humidity space against a fixed table of centres. Both axes are `fbm2D` value noise, 2 octaves, frequency 0.0022, each stretched by `spread()` because value noise clusters around the middle and the hot-dry and cold-wet corners would otherwise never be reached. Weight is `max(0, 1 − distance/0.55)³`, cubed so influence dies sharply — a linear falloff leaves every biome faintly present everywhere and averages all seven into the same middling terrain. Same *structure* as the reference's 7D nearest-box search, at a third of the dimensionality and with soft weights instead of a hard winner.
>
> **Height** is a weighted blend of each biome's `baseHeight` and `amplitude`, shaped by **one global noise field** (`fbm2D`, frequency 0.010, 5 octaves) raised to the power 1.6 — so biomes scale a shared landscape rather than each having their own. Blending is not optional: unblended, neighbouring regions meet at a cliff. **Surface blocks come from the single strongest biome** instead, because a blend of two block types is not a thing; the boundary still reads naturally because the selection noise makes it a wandering contour rather than a straight line. Altitude and the waterline override the biome's own top block afterwards.
>
> **World geometry.** `kWorldHeightChunks = 3` × `Chunk::kSize = 32` → **y 0–95**, sea level **24**, bedrock a hard slab at y ≤ 2, deepslate a **hard** cutoff at y ≤ 12. Against the reference's −64…319 with sea level 63, depths below sea level compress by about **0.17** and heights above it by **0.28**.
>
> **Caves** are one 3D field, `|fbm3D − 0.5| < 0.038 × fade` at frequency 0.018 with Y stretched 1.6× — and it is worth naming what that is: **thresholding a band around the mid-value is the spaghetti trick, not the cheese trick.** Taking the zero-crossing shell gives connected winding tunnels; thresholding the field directly would give disconnected blobs that read as holes. `fade` ramps in over 10 blocks below a 6-block surface margin, so the surface stays intact and deep rock opens up. The lesson we already paid for — **cave cost tracks surface area, not hollow volume**, so halving the width barely moved the triangle count while halving the frequency did — is exactly why the reference separates cheese from spaghetti from noodle: three different surface-area budgets.
>
> **Ores.** **Nine** generate (the eight metals plus ancient debris), from `kOreVeins` in [game/src/world/TerrainGenerator.cpp](game/src/world/TerrainGenerator.cpp) ordered **rarest first**, so the first match wins and a common ore can never overwrite a scarce one where their bands overlap. *(§15.2 of this file previously said eight; the code says nine. The code wins.)* Two deliberate simplifications. Ours are **thresholded 3D value noise** rather than trapezoid-distributed vein placement — same mechanism as the caves at a much higher frequency, which gives connected blobs for free where a per-cell roll would read as speckle. And the bands are compressed onto our world by the 0.17/0.28 factors above. Each threshold comes from the reference's share of rock via `t = 1 − sqrt(share)`, which holds because one octave of value noise is near enough triangular. Ancient debris is **not** kept away from air the way the reference's buried ores are — that rule exists to stop it being spotted from a lava lake, and we have no lava.
>
> **Trees** are rebuilt per chunk on an **8-block candidate grid** ([game/src/world/Structures.cpp](game/src/world/Structures.cpp)): each chunk works out every cell within `kReach = 3` that could reach it, regenerates each tree from the cell coordinates alone, and keeps only the blocks landing inside its own bounds. Two chunks building the same tree independently reach the same answer. One hash rejects most cells before any biome or height sampling happens.

**What is worth taking, in priority order.**

| # | Take | Why, and what it costs |
|---|---|---|
| 1 | **Continentalness** as a third selection axis | The largest single upgrade available. Coastlines stop being "wherever the height noise dips below sea level" and become structural — oceans, coast and inland become a decision rather than an accident. It is one more noise field and one more column in the biome table |
| 2 | **Erosion**, as a *factor* multiplying the height response | The reference's real insight is not that erosion picks biomes but that it controls **how hard the target height is enforced** (§15.4). Low erosion ⇒ factor ≈ 6.3, terrain pinned to the spline; high erosion ⇒ factor ≈ 0.6, the 3D noise runs wild. That is what lets flat plains and jagged mountains coexist at the same temperature and humidity, and it is a multiply, not a new system |
| 3 | **Split the top block from the filler depth using a noise-driven `surface depth`** | Ours is a constant `fillerDepth` per biome. The reference varies it 0–6 blocks from one noise, which is most of why its ground does not look extruded |
| 4 | **A second cave read** — cheese blobs on top of the spaghetti we already have | Two thresholded fields, different frequencies, `min`ned together. Cheap, and it is what makes caves feel like a system rather than a tunnel |
| 5 | **The trapezoid ore distribution** | Replaces a hard band with a triangle peaking at a target depth. Genuinely better than what we have and costs one function |
| 6 | **`discard_chance_on_air_exposure`** | A one-line anti-strip-mining lever with a real effect on how mining feels |

**What is not worth taking.**

- **The 7D box search with an R-tree.** At seven biomes, a linear scan of seven centres is already the right algorithm. Revisit at ~40 biomes, not before.
- **Amplified and Large Biomes.** Both are `[JE]` world types and Large Biomes changes nothing but a wavelength.
- **The badlands 192-entry clay-band table.** A lot of machinery for one biome we do not have.
- **Aquifers.** They exist to give underground water a *local* level. We have one sea level, no lava, and caves that flood correctly for free by filling after the solid pass.
- **`preliminary_surface_level`.** A cave-free second density evaluation of the whole column, purely so the surface system cannot be fooled by a cave. Our surface comes from a heightmap function directly, so the problem does not exist.

**Where the reference would break our purity rule.** Our hard rule is that generation is a pure function of `(seed, chunkCoord)` — no neighbour reads, no global mutable state, no clock. Most of the reference already satisfies it; two stages do not, and for different reasons.

| Stage | Neighbour radius | Pure for us? |
|---|---|---|
| Biome selection, terrain density, surface rules | 0 | **Yes**, exactly as implemented. `flat_cache`, `cache_2d`, `cache_once` and `interpolated` are **memoisation, not state** — removing them changes speed, not output |
| `preliminary_surface_level` | 0 chunks, but samples noise at other columns | **Yes** — a read of noise at another *position* is not a read of another chunk's *data*. Costs extra noise evaluations |
| **Carvers** | **8 chunks** | **No, as Mojang implements it** |
| **Features** | **1 chunk, and it writes** | **No** |

**Carvers.** `ChunkGenerator.applyCarvers` loops `(dx, dz)` over −8…+8 and calls `region.getChunk(...)` on all **289** neighbours — purely to ask *which biome that chunk is*, so it knows which carver list to run **[code]**. That single line is the violation. Everything else is already pure: the seed is `setLargeFeatureSeed(worldSeed + carverIndex, chunkX, chunkZ)`, which depends only on the seed, the carver index and the chunk's own coordinates, and `carve()` clips every block write to the centre chunk. **The fix is one substitution** — replace the neighbour fetch with a biome computed from the climate noise at that chunk's centre, which we can do because our biome lookup is already a pure function of position. The real cost is not purity but work: 289 chunks × 3 carvers = **867 seeded draws per chunk**, of which ~43 pass and then walk full tunnel paths whose blocks are ~99% discarded. Shrinking the neighbourhood to the actual tunnel reach (~112 blocks = 7 chunks) removes about a quarter of the draws for no visible change. **Or skip carvers entirely** and ship only noise caves — which is what we already do, and it costs us ravines and the characteristic wide winding tunnels, nothing else.

## 15.10 The rules that keep a generated world sane

Researched 2026-08-07 after a playtest found floating land, rings of bare stone and peaks that were solid white. Four agents against the same local dump. **All three symptoms were port bugs with the same shape: a mechanism the reference has and we had dropped.**

### 15.10a Why the reference's terrain does not detach

A floating island needs the density above the surface to go negative and then positive again, which is purely **noise vertical slope against ramp vertical slope**.

| | Value | Source |
|---|---|---|
| Ramp slope below the target height | `factor / 32` per block | derived from `depth`'s `y_clamped_gradient(-64:1.5 → 320:-1.5)` = 1/128, times the `4.0 ×` in `sloped_cheese` |
| Ramp slope **above** the target height | `factor / 128` | `quarter_negative` — air fills in **four times more reluctantly** than rock builds up |
| `base_3d_noise` per-octave vertical slope | **0.00261 / block, identical for every octave** | amplitude ∝ wavelength, i.e. red noise |
| 16 octaves, incoherent | RMS ≈ **0.010 / block** | |
| **Crossover** | **factor ≈ 1.33** | |

Vanilla's `factor` spline has exactly two leaves below that — 0.625 and 1.37 — and both are in the mountain-peak branch. **Everywhere else `factor ≥ 1.56` and the ramp wins by 1.5×–5×, so detachment is arithmetically impossible rather than merely rare.**

Three more things bound it: `smear_scale_multiplier: 8.0` quantises the noise's y coordinate to 8-block steps, the 4×8×4 `interpolated` lattice means nothing thinner than a cell can exist, and `jaggedness` is **2D** (`flat_cache`, `y_scale 0`) so it moves the target height rather than adding 3D wobble.

**`base_3d_noise`'s practical range is ±0.7, not ±1** — σ ≈ 0.15. Mojang's own evidence: `preliminary_surface_level` substitutes the constant **−0.703125** in its place.

> **▶ This is exactly what we got wrong.** Our first cut used amplitudes `{1, 1, 0.5, 0.25}` at a 52-block vertical wavelength — a measured slope near **0.10 per block** against ramps of 0.02–0.08. The noise beat the ramp everywhere, so islands were not an edge case, they were the norm. Fixed three ways: halving amplitudes per octave (the reference's red-noise shape), a 210-block vertical wavelength, and dropping `quarter_negative` — see §15.10e for why the last one goes further here than there.

**Mojang does produce floating islands, in three bounded places:** vanilla peaks at `factor 0.625`; the Amplified preset, whose `factor` bottoms at 0.1389 with double jaggedness; and `noise_settings/floating_islands.json`, which **removes `depth`, `factor` and `sloped_cheese` entirely** and doubles the vertical noise rate. Deleting the ramp is literally how they generate floating terrain.

### 15.10b The "no else" pattern, and where bare stone is deliberate

`windswept_hills` is **grass with stone patches, not a slab of stone.** The branch places stone only where the `surface` noise is above 4/33 ≈ 0.1212 and then simply **ends** — control leaves the biome branch and falls through to the generic grass/dirt rule.

| Biome | Noise | Threshold | Places | Else |
|---|---|---|---|---|
| `windswept_hills` | `surface` | ≥ 0.1212 | stone | **falls through to grass** |
| `windswept_gravelly_hills` | `surface` | ≥ 0.2424 / ≥ 0.1212 / ≥ −0.1212 | gravel / stone / grass | gravel |
| `windswept_savanna` | `surface` | ≥ 0.2121 / ≥ −0.0606 | stone / coarse dirt | falls through to grass |
| `stony_shore` | `gravel` | ∈ [−0.05, 0.05] | gravel | stone |
| `stony_peaks` | `calcite` | ∈ [−0.0125, 0.0125] | calcite | stone |

Every `surface` threshold is *n*/33.

**Bare stone never appears on an ordinary grassy hillside in vanilla.** The guarantee is structural, not statistical: `stoneDepthAbove ≤ 1` is true for the topmost block of every run, full stop. There is no noise gate and no slope gate on the generic path. `plains`, `meadow`, `snowy_plains`, `forest` are not named anywhere in the rule tree at all — they are pure fallback.

> **▶ Ours was `top = Stone` on a biome occupying the reference's own narrow erosion band `[0.45, 0.55]`.** A narrow interval of a smooth 2D field is an annulus, so it came out as contour rings. `else stone` is how you manufacture that bug.

### 15.10c `steep`, and why a peak is not a white blob

`minecraft:steep` compares the `WORLD_SURFACE_WG` heights of the z−1 and z+1 neighbours (then x±1) and is true when they differ by **≥ 4**. It is vanilla's **only** slope-driven bare stone.

| Biome | Top block rule | Bare rock? |
|---|---|---|
| `jagged_peaks` | `steep` → **stone**; else `snow_block` | **yes, every steep face** |
| `snowy_slopes` | `steep` → **stone**; `powder_snow` ∈ [0.35, 0.6] → powder snow; else `snow_block` | yes |
| `frozen_peaks` | `steep` → packed ice; `packed_ice` ∈ [0, 0.2]; `ice` ∈ [0, 0.025]; else `snow_block` | no, but ice blotches break the white |
| `grove` | no `steep` clause at all | no |
| `stony_peaks` | `calcite` blotches; else stone | entirely rock |

**A port that omits `steep` gets a peak that is a solid white blob.** Do not apply it to ordinary ground — it is the artefact, there.

### 15.10d Support, gravity, and snow

**Snow block does not fall, in either edition.** What falls: sand, red sand, gravel, suspicious sand/gravel, concrete powder, dragon egg, anvils, pointed dripstone, scaffolding (at distance ≥ 7). The trigger is the block below being **replaceable**, not merely non-solid.

`minecraft:snow` the *layer* is a different block from `minecraft:snow_block`: 1–8 layers, needs support, is itself replaceable, and falls **in Bedrock only**. Its `canSurvive` is a three-step test and the order matters — a deny list (`ice`, `packed_ice`, `barrier`) is checked **before** the geometric full-face test, which is the only reason frozen peaks read as ice rather than white.

**Two independent mechanisms put white on a mountain.** Surface rules place `snow_block` as *terrain material* during generation, with no support test. `minecraft:freeze_top_layer` then runs as the **last** decoration step in all 55 overworld biomes and adds one snow *layer* plus ice on water, wherever the local temperature is < 0.15 and block light < 10.

**Temperature falls with altitude**: above y 80 it drops **0.00125 per block**, jittered by a noise worth ±8 blocks of height, which is why a snow line is ragged rather than a contour. `y ≈ 81 + (T − 0.15)/0.00125` reproduces the wiki's published snow lines exactly.

Peak base temperatures: jagged −0.7, frozen −0.7, snowy slopes −0.3, grove −0.2, **stony peaks +1.0** — so stony peaks never whiten at any altitude.

**Worldgen expresses support constraints through `block_predicate_filter`**, and the important form is `would_survive`, which reuses the block's own `canSurvive` so the rule lives in one place. 56 placed features use it — the whole `*_checked` tree family.

### 15.10e Is there any post-generation validation? No.

Stated plainly, because it is worth knowing before designing one: **Minecraft never goes back.** Surface rules do not re-run after features. Nothing removes unsupported blocks or deletes floating geometry. Floating trees, ore and gravel left over a carver cut simply stay; sand falls only when a block update happens to reach it, which is why fresh worlds contain suspended gravel until a player disturbs it. The only thing resembling a fix-up is `freeze_top_layer`, and it strictly *adds*.

> **▶ Where we stand (2026-08-07, second pass).** All three reported symptoms are closed, and two of the fixes go further than the reference rather than matching it.
>
> - **Floating land: impossible by construction.** Terrain is **single-valued** — everything at or below the highest solid sample in a column is ground. The reference does not need this because its ramp beats its noise by 1.5×–5×; our world is a quarter as tall, so that margin is not available and an island is a much larger fraction of our sky. It costs overhangs and arches; it buys an exact depth counter for the surface rules and an ore air-exposure test that is a height comparison. `quarter_negative` came out with it: with single-valued terrain it produces towers rather than overhangs. **⚠ The guarantee reshapes a would-be island into a sheer pillar rather than preventing one — see §15.10f, where that came back as the next bug report.**
> - **Stone rings: gone.** `Biome::patch` + `patchThreshold` implement the no-else pattern, so windswept hills is grass with 40% stone patches instead of solid stone. Measured top blocks: plains 99% grass, savanna 100%, meadow 97%.
> - **White peaks: gone.** `steep` is implemented at a 2-block threshold (the reference's 4, scaled to our quarter-height world) off a per-chunk height array with a one-column border, and jaggedness moved to a 16-block wavelength because at 44 it left summits too smooth for the rule to ever fire. Jagged peaks measure **38% stone, 60% snow**.
> - **The snow line is now one rule**, `freezesAt`, with the reference's altitude lapse rescaled — not a number on 27 biome rows.
> - **Not taken:** falling blocks. Sand and gravel over a carved cave stay put, which is the reference's behaviour too until a block update reaches them.
>
> All of it is measured by `worldgen_probe=1` in `settings.cfg`, which censuses the world and exits.

### 15.10f Rivers, and why a dry one is invisible

Researched 2026-08-07 after a playtest found **long curved ribbons of gravel and sand across dry grassland** — river biomes whose channel never reached the waterline.

**`river` and `frozen_river` are not named anywhere in the reference's surface rules.** `Select-String 'river'` over `noise_settings/overworld.json` returns zero matches. A river column therefore falls through the whole tree to the generic terminal rule and comes out **`grass_block`**.

The three water bands, in order, and what a river column hits:

| Band | Condition | Places | Dry river column |
|---|---|---|---|
| Top | `stone_depth(floor)` + `water(offset -1)` | per-biome tops, else grass/dirt | **grass_block** |
| Subsoil | `water(offset -6, mult -1, add_stone_depth)` | per-biome unders, else dirt | — |
| Sea floor | `stone_depth(floor)` | sand for warm oceans, else **gravel** | — |

`minecraft:water` **[engine]** is `waterHeight == MIN_VALUE || blockY + … >= waterHeight + offset + surfaceDepth × multiplier` — it means "at or above the local waterline" and is **true by default on dry land**. So gravel is reachable *only* after both water tests have failed, which cannot happen with no water overhead. **Sand never appears on a riverbed at all** — the only sand branches are `warm_ocean`/`beach`/`snowy_beach` and `desert`.

**Sand and gravel near water are a decoration pass, not a surface rule.** `disk_sand` (radius 2–6, half-height 2, count 3), `disk_gravel` (2–5, 2) and `disk_clay` (2–3, 1) all target `dirt`/`grass_block`, and every one carries the identical placement: `heightmap OCEAN_FLOOR_WG` → `block_predicate_filter { matching_fluids: water }`. The heightmap puts the position on the first block **above** the ground and the filter then demands that block be water. **That is the waterline test a port is likely to be missing.**

**Terrain is not guaranteed below sea level inside the band.** There is no river branch in `offset.json`; every erosion leaf simply has its lowest control point at `ridges_folded = -1`. At low erosion inland that point is y 89–108 — far *above* sea level 63. **The reference ships dry, grass-covered river strips through its mountains**, and they are invisible precisely because of the fallthrough.

What makes it reliable where it does work is `factor`: **5.1–6.3 across the river band**, so the 3D noise cannot lift the bed back above the waterline. The band edge is engineered too — the PV = −1 control points carry derivative 0.5 toward PV = −0.4, which regains sea level almost exactly where the biome band ends, so the dry edge strip is one or two blocks wide.

**Rivers are emergent, not routed.** A river is the |weirdness| ≤ 0.05 level set of a continuous 2D noise, so it never terminates in a stub — level sets are closed loops or run to infinity — but nothing routes it anywhere. It becomes ocean only where continentalness independently drops below −0.19. There is no flow, no downhill constraint and no connectivity pass anywhere in the data.

> **▶ Where we stand (2026-08-07, third pass).** All fixed, all measured.
>
> - **Bed materials are gated on the waterline.** Above it a bed material is unreachable, exactly as in the reference; the ocean and river rows now carry **grass and dirt** as their top block, which is only ever consulted on dry ground. Probe: `DRY-BED 0`.
> - **`factor` was the spire cause.** Ours ran down to 1.15 across the entire low-erosion third of the erosion axis — ±19 blocks of noise displacement — where the reference holds 5.1–6.3 and only drops to 0.625 inside a narrow high-PV window. Raised to 4.6–6.2 with `kRuggedFactor` 1.4 confined to that window. Probe: `PILLARS 0`.
> - **The density lattice is gone.** With single-valued terrain it was buying nothing but its own artefact: an interpolated density is piecewise-linear across a cell, so its contours cluster on cell boundaries and a gentle slope came out banded every four blocks. Height is now computed directly per column, which is *also* 8x faster.
> - **Not taken:** the disk features. They are the reference's legitimate source of patchy sand near water, but the report was that there was already too much random patchiness, so adding more would be answering the wrong complaint.

---

Ordered by value per unit of work against what we already have. Input to `TIMELINE.md`, not a replacement for it.

| # | Item | Why it is first | § |
|---|---|---|---|
| 1 | ~~Spawn animals at chunk generation~~ | **Done.** A pure function of `(seed, chunkCoord)`, populated the first time the player comes within two chunks | 4.2 |
| 2 | ~~Split Chase into target-producer + target-consumer~~ | **Done (M20c).** `CreatureTarget` is written by `HurtByTarget` and `NearestAttackableTarget` and read by `MeleeAttack`; retaliation, pack anger and hunting-on-sight are three rows that never mention each other | 8.8 |
| 3 | ~~Control flags (Move/Look/Jump) + a priority selector~~ | **Done (M20c).** Eight behaviours in a `constexpr` table; validated exactly as predicted, by adding `LookAtPlayer` and watching chickens walk and watch at once | 8.7 |
| 4 | ~~Creature persistence~~ | **Done.** `creatures.dat` beside `player.dat`, as an explicit save record | 4.3 |
| 5 | ~~5–10% babies in a spawned group~~ | **Done.** `babyChance` per species; `Creature::scale` drives model and hitbox together | 5.3 |
| 6 | **The 0.5 s invulnerability window with the overwrite rule** | The moment player health exists. Without it a creature standing inside you kills instantly | 2.4 |
| 7 | ~~Pack anger propagation~~ | **Done**, and it delivered herd flight from the same mechanism | 6.2 |
| 8 | ~~The ×5 wrong-tool mining penalty~~ | **Done.** ×1.5 with the right kit, ×5 without, against a hardness table that already matched | 10.1 |
| 9 | ~~Soft horizontal entity separation~~ | **Done.** Runs after world collision, horizontal only | 1.7 |
| 10 | ~~Item despawn timer~~ | **Done**, at five minutes on wall time | 1.8 |
| 11 | **Ingredient tags in recipes** | Cheap now, expensive after a second wood type exists | 11.1 |
| 12 | **Continentalness, then erosion** | Coastlines become structural instead of incidental | 15.2, 15.4, 15.9 |
| 13 | **The water weight/shortest-path-down search** | What makes water look like water rather than a spreading stain | 9.1 |
| 14 | **Flying + swimming in one milestone** | They are two-thirds the same purchase; doing them a year apart pays twice | 7.2, 7.3 |

**What is left is the hard half**, and deliberately so: 12–14 are new systems rather than new numbers, 6 waits on player health, and 11 waits on a second wood type. **The AI restructure (2 and 3) landed on 2026-08-04** and took the rest of §8.8's steps 1–4 with it; step 5, unifying the two spawners, is the only piece of that section still open.

---

# 17 · Projectiles, the bow and the arrow

Researched 2026-08-08 by two agents against `Mojang/bedrock-samples`, learn.microsoft's entity/item component reference and the wiki. **The arrow is fully data-driven and those numbers are exact; there is no `behavior_pack/items/bow.json`** — the bow is still hardcoded in the engine, so every bow figure comes from the wiki or from Microsoft's own documented reconstruction of it. Built the same day; `SYSTEM_MEMORY.md` → "Projectiles" is the implementation.

## 17.1 The numbers, and their units

**Everything below is per *tick*, at twenty ticks a second.** Mixing that with a per-second step is the single easiest way to get this wrong: `velocity *= 0.99` once a frame at 120 fps is six times the intended drag, and it silently breaks the closed form the whole thing can be checked against.

| | Value | Where from |
|---|---|---|
| `power`, player bow at full draw | **3.0 blocks/tick** = 60 m/s | `arrow.json`, group `minecraft:player_arrow` |
| `power`, player crossbow | 3.15 | same file |
| `power`, **mob** bow | **1.6** | group `minecraft:mob_arrow` |
| `power`, dispenser | 1.1 | base component |
| `gravity` | **0.05 blocks/tick²** = 20 m/s² | `arrow.json` |
| `inertia` (air drag) | **×0.99 per tick** | `minecraft:projectile` default |
| `liquid_inertia` | **×0.6 per tick** | same |
| Terminal speed | **5.0 b/t** in air, **0.125 b/t** in water | `gravity / (1 − inertia)` |
| Collision box | **0.25 × 0.25** | `arrow.json`. The wiki's 0.5 is Java-checked and says so |
| Spawn point | **eye height − 0.1** | `anchor: 1`, `offset: [0, −0.1, 0]` |
| Owner immunity | **5 ticks**, then it *can* hit you | `owner_launch_immunity_ticks` |
| Landing wobble | 0.35 s | `on_hit.stick_in_ground.shake_time` |
| Despawn once landed | 1200 ticks = 60 s | wiki |
| Bow durability | **385** (Java 384), one per arrow **fired** | wiki |
| Draw to full | **20 ticks = 1 s** | wiki |
| Spread, player | `uncertainty_base` 1, multiplier **0** — difficulty-independent | `arrow.json` |
| Spread, mob | base 16, multiplier 4 → Easy 12, Normal 8, **Hard 4** | same |

**A skeleton gets more accurate as difficulty rises.** That is the opposite of the obvious guess and is easy to implement backwards.

## 17.2 The five things that are not obvious

1. **The charge curve is quadratic.** `p = clamp((f² + 2f)/3, 0, 1)` where `f = ticks/20`. Held half a second it is at **0.42**, not 0.5. Below `p = 0.1` (about 3 ticks) nothing is fired at all — no arrow spent, no durability taken. The reference's own published damage table falls straight out of this: `ceil(2 × 3p)` gives 1, 5, 6, 6 at 0.1 s, 0.8 s, 0.9 s and 1.0 s, and all four rows match.

2. **The visual pull and the physics charge finish at different times.** Bedrock's `bow_pulling_2` is showing by about tick 10; critical charge is tick 20. Drive the launch speed off the texture stage and you ship a bow that fires full power at half draw.

3. **Damage is a function of the speed it is doing now.** `impact_damage.damage` is **0** and `power_multiplier` is 2.0, so there is no stored damage anywhere: `D = ceil(2 × ‖v‖)`, and an arrow that has slowed does less. In water it drops to nearly nothing within a few blocks. Storing "6 damage" at launch is the most common wrong implementation.

4. **Round up before the critical roll.** `ceil_pre_critical_damage: true`, added in 1.26.40 specifically to make the order explicit. `ceil(2.61) + crit ≠ ceil(2.61 + crit)`. A fully charged bow always crits; the bonus is `randInt[0, D/2 + 1]`.

5. **The update order is position, drag, gravity** — and it is *provable* from the published terminal speeds. Drag before acceleration gives `g/(1−k)` = 5.00; the other order gives `k·g/(1−k)` = 4.95. The wiki lists arrows at 5.00 and thrown potions at 4.95, and the maximum-travel column says 100 v₀ against 99 v₀. Two independent fingerprints.

## 17.3 Collision

Both blocks and entities are answered by a **swept segment**, never by the endpoint cell — which is why nothing tunnels at three blocks a tick and why no substepping or speed cap is needed anywhere. The order is: sweep blocks first, clamp the segment to the hit, *then* look for entities inside what is left. The other order lets an arrow hit something standing behind a wall.

Blocks are tested against their **collision** geometry, with one documented oddity: the part of a fence or wall that pokes above its own cell is invisible to a projectile, so an arrow flies through the top of a fence.

Entity collision splits by family. **Throwables** (snowball, egg, pearl, potion) inflate each candidate by **0.3** before testing, which is what makes them forgiving. **Arrows do not inflate anything** — the wiki marks that `[verify]`, and no Bedrock-specific figure exists.

## 17.4 The model

`arrow.geo.json` is three cubes, and two of them have a zero dimension — they are **flat quads**, not boxes:

| | origin | size | rotation | uv |
|---|---|---|---|---|
| shaft A | `[0, −2.5, −3]` | `[0, 5, 16]` | roll +45° | `(0, 0)` |
| shaft B | `[0, −2.5, −3]` | `[0, 5, 16]` | roll −45° | `(0, 0)` |
| nock | `[−2.5, −2.5, 12]` | `[5, 5, 0]` | roll 45° | `(0, 5)` |

**The fletching is painted into the texture, not modelled** — both shaft quads sample the same 16×5 strip, and the crossed pair *is* the whole arrow. Texture is 32×32, but every rect the model uses sits in its top-left 16×16, so a crop keeps it at native size. `animation.arrow.move` scales it `[0.7, 0.7, 0.9]` and hard-codes **roll to zero**: an arrow points along its velocity and does not spin. The landing wobble is `−sin(shake × 200) × shake`, on pitch alone, decaying to nothing over 7 ticks — cheap, and most of what makes a hit feel like an impact.

## 17.5 What we deliberately do not have

Enchantments (Power, Punch, Flame, Infinity), tipped arrows, the crossbow, the bounce on a zero-damage hit (`should_bounce: "if_no_damage_dealt"` — that is what a shield does, and there are no shields), arrows lighting TNT or powering wooden buttons and the target block (no redstone), and the first-person bow pose. The arrow entity also has health in the reference and can be destroyed by lava; ours cannot.

---

# 18. Thrown items: the pearl and the egg

Read from Mojang's `behavior_pack/entities/ender_pearl.json`, `snowball.json` and `egg.json`, plus the wiki's Projectile page.

## 18.1 The table, and what is shared

| `minecraft:projectile` | ender_pearl | snowball | egg |
|---|---|---|---|
| `power` | 1.5 | 1.5 | 1.5 |
| `gravity` | **0.025** | 0.03 | 0.03 |
| `inertia` | **1** | absent → 0.99 | absent → 0.99 |
| `liquid_inertia` | **1** | absent → 0.6 | absent → 0.6 |
| `on_hit.impact_damage` | `null` | `{damage: 3, filter: "blaze"}` | `{damage: 0}` |
| `on_hit.teleport_owner` | `{}` | — | — |
| `on_hit.spawn_chance` | 5 % endermite | — | chicken, `first_spawn_chance: 8`, `second_spawn_chance: 32`, `second_spawn_count: 4` |
| collision box | 0.25 × 0.25 | same | same |

Everything else — `uncertainty_base`, `should_bounce`, `reflect_on_hurt`, `is_dangerous` — is **absent from all three files**, so the component defaults apply, including `owner_launch_immunity_ticks: 5`.

> ⛔ **The pearl overrides both inertias to exactly 1, so it has no drag and therefore no terminal speed.** That is the arithmetic behind the wiki's "about 45 blocks straight up [BE only]" against Java's 27.7 — `v²/2g = 1.5² / 0.05 = 45`. Taking the thrown-item family's 0.99 for it, which is the obvious thing to do, gets the flight visibly wrong. A `static_assert` on that height is what pins it.

## 18.2 What is genuinely undocumented

- **No in-air lifetime exists in the data.** `ender_pearl.json` carries no `minecraft:despawn`, no timer and no lifetime field, and the component has no such property. It ends on impact, in the void, or with its owner. The wiki's "despawns after some time" is unquantified — treat it as absent rather than inventing a number.
- **No placement rule for the teleport.** `teleport_owner` takes no fields, and no push-out or nudge behaviour is published. The one hard data point is the wiki's note that a pearl moves you through non-solid blocks *without* suffocating, which implies the landing point is used more or less as-is.

## 18.3 The rest

Fall damage on arrival is **5** in both editions, cooldown **20 ticks** in both, stack of 16, consumed on use. Entities are inflated by 0.3 blocks for the hit test, and the thrower cannot be hit for the first 5 ticks. Bedrock's throw sound is `random.bow` at volume 0.5 and pitch 0.33–0.5, which is engine-side rather than in the JSON. There is no `enderPearlsVanishOnDeath` gamerule in Bedrock.

**Ours diverges in three named places**: no fall damage (there is no health yet), no endermite or its 5 % roll (no such creature and the name is coined anyway), and **thrown items pass through creatures** rather than stopping on them — `damagePerSpeed` of zero skips the sweep entirely, which is a simplification and not the reference's behaviour.

---

## Sources

**§15 is the exception to everything below.** Its primary source is the **local data dump** at `reference/minecraft-assets-26.2/minecraft-assets-26.2/data/minecraft/worldgen/` (Java 26.2, `version.json` release 2026-06-16) — read directly out of the JSON, not from the wiki. Within §15, values tagged **[D]** came from those files; **[code]** from decompiled `net.minecraft.world.level.levelgen` and is *not* verifiable from the dump; **[wiki]** from the pages below. The biome→climate-parameter table in particular is **hardcoded in the engine and not shipped as data** — `multi_noise_biome_source_parameter_list/overworld.json` contains only `{"preset": "minecraft:overworld"}`, 37 bytes — so §15.2's cutoffs and grid come from the wiki and from `Cubitect/cubiomes`, a reimplementation that matches Java bit-for-bit.

Everything else is from `minecraft.wiki` (Bedrock 26.35 / Java 26.2 era pages, fetched 2026-08-02/03), plus Microsoft Learn's Bedrock Creator Documentation for §8 — the wiki does **not** document the `minecraft:behavior.*` family, and its `Entity components` page is a red link.

[Tick](https://minecraft.wiki/w/Tick) ·
[Entity](https://minecraft.wiki/w/Entity) ·
[Hitbox](https://minecraft.wiki/w/Hitbox) ·
[Attribute](https://minecraft.wiki/w/Attribute) ·
[Damage](https://minecraft.wiki/w/Damage) ·
[Melee attack](https://minecraft.wiki/w/Melee_attack) ·
[Knockback](https://minecraft.wiki/w/Knockback_(mechanic)) ·
[Mob AI](https://minecraft.wiki/w/Mob_AI) ·
[Mob spawning](https://minecraft.wiki/w/Mob_spawning) ·
[Drops](https://minecraft.wiki/w/Drops) ·
[Breeding](https://minecraft.wiki/w/Breeding) ·
[Hunger](https://minecraft.wiki/w/Hunger) ·
[Difficulty](https://minecraft.wiki/w/Difficulty) ·
[Fluid](https://minecraft.wiki/w/Fluid) ·
[Water](https://minecraft.wiki/w/Water) ·
[Solid block](https://minecraft.wiki/w/Solid_block) ·
[Opacity](https://minecraft.wiki/w/Opacity) ·
[Light](https://minecraft.wiki/w/Light) ·
[Daylight cycle](https://minecraft.wiki/w/Daylight_cycle) ·
[Weather](https://minecraft.wiki/w/Weather) ·
[Chunk](https://minecraft.wiki/w/Chunk) ·
[Chunk format](https://minecraft.wiki/w/Chunk_format) ·
[Region file format](https://minecraft.wiki/w/Region_file_format) ·
[Block update](https://minecraft.wiki/w/Block_update) ·
[Random Tick](https://minecraft.wiki/w/Random_Tick) ·
[Breaking](https://minecraft.wiki/w/Breaking) ·
[Crafting](https://minecraft.wiki/w/Crafting) ·
[Biome](https://minecraft.wiki/w/Biome) ·
[World generation](https://minecraft.wiki/w/World_generation) ·
[World seed](https://minecraft.wiki/w/World_seed) ·
[Custom world generation](https://minecraft.wiki/w/Custom_world_generation) ·
[cubiomes (`biomenoise.c`)](https://github.com/Cubitect/cubiomes) ·
[Creeper](https://minecraft.wiki/w/Creeper) ·
[Wolf](https://minecraft.wiki/w/Wolf) ·
[Frog](https://minecraft.wiki/w/Frog) ·
[Goat](https://minecraft.wiki/w/Goat) ·
[Camel](https://minecraft.wiki/w/Camel) ·
[Rabbit](https://minecraft.wiki/w/Rabbit) ·
[Behavior pack](https://minecraft.wiki/w/Behavior_pack)
