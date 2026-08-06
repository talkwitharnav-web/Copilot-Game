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
| 15 | [World generation](#15-world-generation) |
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

> **▶ Where we stand.** **Taken verbatim on 2026-08-05**: 5 m blocks in every mode, 3 m entities in survival and 5 in creative. The game had shipped **12 m** since M7, which is this table's *touch creative* row — a different input mode entirely, and picking the wrong row is exactly the kind of error that survives because the number looks plausible. Entity reach is still a **distance check**, not the bounding-box expansion described above; that only starts to matter once a mob is wide enough for the difference to show.

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

> **▶ Where we stand (2026-08-06).** We have a melee swing with knockback and creatures that compute a blow and hand it back as a `CreatureAttack` rather than applying it — exactly the shape needed when player health arrives.
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

> **▶ Where we stand.** None of this exists; M21 owns it. The structural point is that **exhaustion is driven by actions we already have hooks for** — block breaking, attacking, jumping, taking damage — so hunger is one float and a handful of increments in code that already exists. It does not need a new system.

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

> **▶ Where we stand (2026-08-04).** We have dropped items with physics and pickup, but creatures drop nothing — there is no food item and no hunger, so meat has nowhere to go. M21's ordering is right.
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

## 15.1 The multi-noise biome system

Six parameters, each its own noise field:

| Parameter | Range | Decides |
|---|---|---|
| Temperature | −1..1, bands at −0.45/−0.15/0.2/0.55 | hot/cold |
| Humidity | −1..1, bands at −0.35/−0.1/0.1/0.3 | wet/dry |
| **Continentalness** | −1.2..1 | ocean → coast → inland |
| **Erosion** | −1..1, 7 bands | flat vs mountainous |
| Weirdness / ridges | −1..1, via $1-|3|w|-2|$ | variants |
| Depth | +0.0078125 per block down | surface vs cave biome |

Continentalness bands: < −0.455 deep ocean; −0.455..−0.19 ocean; −0.19..0.03 coast; > 0.03 inland.

**Each biome is a point in that 6D space and a location gets whichever is nearest.** That is the key structural idea: biomes are not regions on a map, they are **nearest neighbours in a parameter space**. Adding a biome is adding a point, not redrawing a map.

Terrain height is **not** a separate noise field — it comes from continentalness, erosion and ridges through **splines** producing a height offset and a vertical squash factor, which feed a 3D density function (positive = solid).

**Caves** are three layered systems: **cheese** (big cavities), **spaghetti** (long tunnels), **noodle** (thin branching passages that break up the cheese). Plus **aquifers**, which give underground water its own local level rather than one global sea level.

## 15.2 Ore distribution

Modern placement uses **triangular** distributions — densest at a target Y, tapering both ways:

| Ore | Range | Densest |
|---|---|---|
| Coal | 0..320 | 96 |
| Copper | −16..112 | 48 |
| Iron | −24..56 and 80..320 | 16 |
| Gold | −64..32 | −16 |
| Redstone | −64..15 | −59 |
| Lapis | −64..64 | 0 |
| Diamond | −64..16 | −59 |
| Emerald | mountains only, −16..320 | 236 |

**Large ore veins** for copper (y 0..50, granite filler) and iron (y −60..−8, tuff filler) generate as long branching spaghetti: **70% untouched filler, 30% is 10–30% ore plus 2% raw ore blocks.**

> **▶ Where we stand.** **Eight ores generate**, from a table ordered **rarest first** so a common ore cannot overwrite a scarce one where their bands overlap. Two deliberate simplifications. Ours are **thresholded 3D noise** rather than triangular-distributed vein placement — same mechanism as the caves at a much higher frequency, which gives connected blobs for free where a per-cell roll would read as speckle. And the bands are **compressed onto our world**: y 0–96 with sea level 24 against the reference's −64–320 with sea level 63, so depths below sea level scale by about 0.17 and heights above it by 0.28. Each threshold comes from the reference's share of rock via `t = 1 − sqrt(share)`, which holds because one octave of value noise is near enough triangular. `worldgen/configured_feature/ore_*.json` in the reference dump carries the exact vein sizes and counts if this is ever worth doing properly.

## 15.3 Trees

Saplings have **2 growth stages**; the block **above** needs **light ≥ 9**. Bone meal bypasses the light requirement at **45% per use**. Space requirements are checked before growth and failure means no growth: oak needs ≥5 vertical and 3×3; spruce ≥6 and 5×5; cherry ≥8 and 5×5; giant spruce ≥14 and 6×6. **If a block blocks an oak's growth space (but not directly above), the oak is forced to grow the large variant instead of failing.**

> **▶ Where we stand.** We have two noise fields (temperature and humidity) giving 7 biomes by proximity in a 2D space — the same *structure* as the reference's 6D nearest-neighbour, just smaller. The lesson in `CLAUDE.md` (two independent fields beat one "climate" value, because a single value can only order biomes along a line) is the reference's own reasoning arrived at independently.
>
> **Continentalness and erosion are the two most valuable additions, in that order.** Continentalness makes coastlines structural rather than "wherever the height noise dips below sea level". Erosion lets flat plains and jagged mountains coexist at the same temperature and humidity.
>
> Our cave system is one 3D noise field. The lesson we already paid for — **cave cost tracks surface area, not hollow volume**, so fewer bigger caves are cheaper *and* more open — is exactly why the reference separates cheese from spaghetti from noodle: three different surface-area budgets.

---

# 16. Priority list

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
| 12 | **Continentalness, then erosion** | Coastlines become structural instead of incidental | 15.1 |
| 13 | **The water weight/shortest-path-down search** | What makes water look like water rather than a spreading stain | 9.1 |
| 14 | **Flying + swimming in one milestone** | They are two-thirds the same purchase; doing them a year apart pays twice | 7.2, 7.3 |

**What is left is the hard half**, and deliberately so: 12–14 are new systems rather than new numbers, 6 waits on player health, and 11 waits on a second wood type. **The AI restructure (2 and 3) landed on 2026-08-04** and took the rest of §8.8's steps 1–4 with it; step 5, unifying the two spawners, is the only piece of that section still open.

---

## Sources

All from `minecraft.wiki` (Bedrock 26.35 / Java 26.2 era pages, fetched 2026-08-02/03), plus Microsoft Learn's Bedrock Creator Documentation for §8 — the wiki does **not** document the `minecraft:behavior.*` family, and its `Entity components` page is a red link.

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
[Creeper](https://minecraft.wiki/w/Creeper) ·
[Wolf](https://minecraft.wiki/w/Wolf) ·
[Frog](https://minecraft.wiki/w/Frog) ·
[Goat](https://minecraft.wiki/w/Goat) ·
[Camel](https://minecraft.wiki/w/Camel) ·
[Rabbit](https://minecraft.wiki/w/Rabbit) ·
[Behavior pack](https://minecraft.wiki/w/Behavior_pack)
