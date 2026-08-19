#pragma once

namespace game::tick {

/// **The simulation tick, and the one place it is written down.**
///
/// Every number this project takes from the reference is published *per tick*
/// - fluid drag, weather countdowns, projectile gravity, mining speed, mob
/// timers - so this figure is the unit all of them are measured in. It is not a
/// tuning knob: raising it does not make anything smoother, it silently
/// rescales every ported constant in the game at once, and smoothness is the
/// renderer's job through interpolation between ticks (`LESSONS.md`, twice).
///
/// It exists as a file because it was written down seven times before anyone
/// looked. **All of them now derive from here**, each the identical rate
/// wearing a different name, and the local names were kept deliberately -
/// `hud::makeStatusBars` reaches for `fluid::kTickSeconds` by that name in the
/// assert that pins the air-supply bar to `fluid::kAirSeconds` - and
/// every one of them reads better in the units its own file counts in:
///
///   `world/Fluid.hpp`      `kTickSeconds`     - the water physics
///   `world/Weather.hpp`    `kTickSeconds`     - the rain and thunder clocks
///   `world/Projectile.cpp` `kTickSeconds`     - the fixed projectile step
///   `world/Effects.hpp`    `shiftedInterval`  - the published `n >> amplifier`
///                          tick counts for regeneration, poison and wither
///   `world/Campfire.hpp`   `kCampfireCookSeconds` - 600 ticks of cooking
///   `item/Mining.hpp`      `tick::kPerSecond`  - used directly, no local alias:
///                          a header-scope one collided with Creature.cpp's
///   `world/Creature.cpp`   `kTicksPerSecond`  - graze odds and creeper fuses
///   `world/Chest.hpp`      the hopper's eight-tick transfer
///   `world/World.cpp`      the day-length clock, ticks to milliseconds
///   `Main.cpp`             the break cooldown, the instant-break floor and
///                          the metres-per-second to blocks-per-tick
///                          conversion in both throw sites
///
/// **Name the function, not the line.** The entry above pointed at a line
/// number in `StatusBars.cpp`, and the assert had long since moved - and
/// `Fluid.hpp` carries the same sentence with the same wrong number, one copied
/// from the other, which is how a line-numbered citation rots twice for the
/// price of once. Nothing in this file points at a line any more.
///
/// **A constant with two owners is this project's most expensive recurring
/// bug**, and this one is the most load-bearing constant there is, so it gets
/// the treatment: one definition, and everything else derived from it.
///
/// **What actually stops a tick-rate change, and what only looks like it does.**
/// Move `kSeconds` on its own and the reciprocity assert below catches it, as
/// do the three that spell the same relation out again where they use it
/// (`Main.cpp`'s instant break, `Chest.hpp`'s hopper, `Creature.cpp`'s fuse
/// headroom). Move *both* halves coherently - the "improvement" someone would
/// actually attempt - and those all still pass, because both sides of them are
/// derived. The ones that fail are the seven that pin a tick-derived figure to
/// an **absolute** number nobody is free to change: `kPerSecond == 20.0f`
/// below, `Campfire.hpp`'s thirty-second campfire from 600 ticks,
/// `Projectile.cpp`'s three-blocks-a-tick arrow against 60 m/s,
/// `hud::makeStatusBars` pinning ten bubbles of thirty ticks to
/// `fluid::kAirSeconds`' literal fifteen, and - added 2026-08-19 -
/// `Survival.hpp`'s `freezeIntervalsAreSeconds`, which requires
/// `kFreezeOnsetSeconds` and `kFreezeInterval` to equal `fromTicks(140)` and
/// `fromTicks(40)` exactly, and which carries a negative twin fed the raw tick
/// counts so it cannot pass vacuously. **That is the shape to copy** when
/// adding another: relating two derived figures to each other proves the
/// arithmetic between them and says nothing about the rate.
///
/// **Entries six and seven were missed until 2026-08-19, and the miss is worth
/// recording because it was not a wrong number - it was the wrong population.**
/// `Player.cpp` and `Creature.cpp` each declare their own `kMaxDeltaSeconds`
/// and assert it `== tick::kSeconds && == 0.05f`. That pins a tick-derived
/// figure to an absolute, which is this list's exact stated definition, and a
/// coherent doubling fails it either way round: leave the constant at 0.05 and
/// the first clause breaks, move it to 0.025 and the second does. They were
/// missed because this list was assembled by reading **headers**, while both of
/// those sit in `.cpp` files - one of them the very file this header was being
/// edited alongside all night. **When you extend this list, search the `.cpp`
/// files too.** The population is "every assert that pins to an absolute", not
/// "every assert I happened to have open".
///
/// **A fifth tick-derived figure landed on 2026-08-19 and is deliberately NOT
/// on that list**, because putting it there would overstate its reach and this
/// file has already been bitten once by a citation that promised more than it
/// delivered. `Player.cpp`'s `kPowderSnowSinkSpeed` is `kGravity *
/// tick::kSeconds * kPowderSnowVerticalScale` - one tick of gravity and a half
/// - so its absolute value moves with the rate. It *does* carry a bounds assert
/// against two absolutes, `fluid::kSinkSpeed` below and `kTerminalVelocity`
/// above, which is the right shape; the trouble is the margins. It spans
/// roughly two-thirds of a tick a second up to ninety-six of them, so **a
/// coherent doubling to forty passes cleanly while quietly halving how fast you
/// sink into powder snow** - 2.4 m/s becomes 1.2 and no assert anywhere
/// notices.
///
/// **The constraint - and note first what it is NOT. Do not "fix" this by
/// writing a literal.** `Player.cpp` rejects that in terms: a written-down
/// `2.4f` would be a third owner of `kGravity`, and an assert tying the value
/// back to its own derivation would be the `X == X` shape this project has
/// already paid for eleven times. **The coupling is in fact dissolved in the
/// best available way** - the sink speed is *computed* from `kGravity` and
/// `kSeconds`, so it cannot disagree with them - and what a rate change costs
/// is a judgement rather than a repair. The 1.5 scale is `[JE]` and describes a
/// **per-tick** construction, so a faithful port keeps following the rate,
/// while the observable 2.4 m/s does not survive. Decide which of those two you
/// mean to preserve, and if it is the observable one, move
/// `kPowderSnowVerticalScale` so that there is still exactly one owner.
/// Falsified by `kPowderSnowSinkSpeed` becoming a literal, which is the single
/// edit this paragraph exists to prevent.
///
/// **Do not trust any list in this file for the set of things that derive from
/// here - run the search, and run it in TWO steps, because one step has a
/// false-negative class this file walked into.** Step one is `tick::kSeconds`
/// and `tick::kPerSecond`, which finds every *direct* dependent. Step two is
/// the one that matters: a file may bind one of those to a name of its own and
/// re-export it, and everything downstream then spells the alias instead.
/// `Fluid.hpp` does exactly that - `constexpr float kTickSeconds =
/// tick::kSeconds;` - so `StatusBars.cpp` depends on the tick rate through
/// `fluid::kTickSeconds` and **a search for `tick::kSeconds` never sees it**.
/// That is not hypothetical: `StatusBars.cpp` is the fourth entry on the list
/// of seven above, so this file named an assert its own stated search could not
/// have found. So: grep the two canonical names, then grep the name of every
/// alias that turns up (anything `constexpr` initialised *from* one of them),
/// and repeat until nothing new appears. The set is transitive. A list rots, a
/// one-step search under-reports, and only the transitive search is honest.
/// The seven asserts named above were verified as real code - not quoted inside
/// a doc comment, which is a distinction this project has already been bitten
/// by - on 2026-08-19, and are a starting point on any later day.
///
/// Deliberately a header with nothing in it but two numbers. Anything that
/// needs the tick rate can include it for free, which is the only way a shared
/// constant actually gets shared - the alternative was `Weather.hpp` including
/// `Fluid.hpp` and dragging the whole world, chunk and job system in with it.
/// **What does *not* derive from here, and why that is correct** (2026-08-19).
/// The list above says "all of them", which is true of every file that counts
/// in ticks - it is not true of the whole game, and a reader who takes it that
/// way will go and "fix" the exception. `World.cpp` runs seven deadline queues
/// - fluid, lava, TNT fuses, fire, falling blocks, support checks and leaf
/// decay - on `std::chrono::steady_clock` deadlines in **milliseconds**, not on
/// a tick count. That is deliberate: they are drained against a per-frame time
/// budget rather than stepped, so a wall-clock deadline is the unit they are
/// actually compared against, and converting them to ticks would mean holding a
/// tick counter they have no other use for. **The cost is that their ported
/// figures are hand-converted** - `kTntFuse{4000}` is the reference's 80-tick
/// fuse multiplied out by a person - so a tick-rate change would leave them
/// untouched while everything else moved. That is one more reason the rate is
/// not a knob, and it is why the seven absolute-anchor asserts named above
/// matter more than the derived ones. What would make this note false: any of
/// those queues changing to hold a tick count, or an eighth appearing.
constexpr float kSeconds = 0.05f;

/// The same rate the other way up, for the places that read more naturally as a
/// multiplier - a chance per tick becoming a chance per second, say.
constexpr float kPerSecond = 20.0f;

/// The two spellings must stay reciprocal, and this is exact rather than
/// approximate on purpose: `1.0f / 20.0f` and `0.05f` are the same float, which
/// is why both were able to sit in the tree looking unrelated. **The single
/// edit that fails it: changing one of the two and not the other.**
static_assert(kSeconds * kPerSecond == 1.0f && kSeconds == 1.0f / kPerSecond,
              "seconds per tick and ticks per second must describe one rate");

/// Twenty, which is the reference's rate and not a choice we get to make - the
/// ported constants are only correct against it. **Fails the moment somebody
/// "improves" the tick rate**, which is the whole point of putting a number
/// this obvious in an assert.
static_assert(kPerSecond == 20.0f,
              "every ported constant in this project is quoted per 20 Hz tick");

} // namespace game::tick
