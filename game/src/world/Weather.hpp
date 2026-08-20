#pragma once

#include "world/Biome.hpp"
#include "world/Tick.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace engine {
struct MeshData;
}

namespace game {

class World;

/// Weather, as the reference models it.
///
/// **Two independent boolean flags with their own countdowns, not a three-way
/// state.** Rain on, thunder on, and a thunderstorm is simply both at once. The
/// thunder flag keeps cycling while it is dry, which is exactly why storms are
/// rare and why one can start partway through a rainstorm or end before it
/// does. Writing it as an enum with transitions would need a probability table
/// that does not exist in the reference and would get the frequencies wrong.
namespace weather {

/// Seconds in one simulation tick, from the one file that owns it. Every timer
/// below is quoted in ticks, so this is the only thing that turns them into
/// wall time - it read `1.0f / 20.0f` here and `0.05f` in `Fluid.hpp`, the same
/// number twice with nothing to notice if one of them moved.
constexpr float kTickSeconds = tick::kSeconds;

/// The reference's timers, in ticks, drawn uniformly.
constexpr int kRainOnMinTicks = 12000;
constexpr int kRainOnMaxTicks = 24000;
constexpr int kRainOffMinTicks = 12000;
constexpr int kRainOffMaxTicks = 180000;
constexpr int kThunderOnMinTicks = 3600;
constexpr int kThunderOnMaxTicks = 15600;
constexpr int kThunderOffMinTicks = 12000;
constexpr int kThunderOffMaxTicks = 180000;

/// One in a hundred thousand per loaded chunk per tick, against the reference's
/// own **128-block simulation radius** rather than our render distance.
///
/// Porting the per-chunk probability straight across would have been wrong
/// twice over: our chunks are 32 blocks where the reference's columns are 16,
/// and our render distance reaches three times further. Either mistake alone
/// multiplies the strike rate by an order of magnitude. This is the reference's
/// resulting **rate**, which is the number that was actually measured.
constexpr float kStrikeChancePerSecond = 201.0f * tick::kPerSecond / 100000.0f;
constexpr float kSimulationRadius = 128.0f;

/// Below this the column freezes and snow falls instead of rain. The reference's
/// own threshold, against a biome's fixed temperature.
constexpr float kFreezingWarmth = 0.15f;

/// **The altitude temperature lapse, in our blocks - and the one owner of it.**
/// `Climate.cpp`'s `freezesAt` reads these two rather than carrying its own
/// pair, because the snow the generator lays down and the weather that falls on
/// it are the same rule and a second copy is how they came to disagree.
///
/// The units, written down, because a number ported into a field measured in a
/// different unit is exactly the bug this replaced:
///   - the reference loses **0.00125 of `warmth` per *reference* block** above
///     its y 80, which is its sea level 63 plus 17, in a 384-block world;
///   - one of our blocks stands in for `1 / 0.28 = 3.57` of its own, so the
///     same gradient is `0.00125 / 0.28 = 0.004464` **per our block**;
///   - and its y 80 lands at our `24 (sea level) + 17 * 0.28 = 28.76`, taken as
///     29 - which is the figure `freezesAt` has always used.
///
/// The reference's raw 0.00125 above y 81 stood here until 2026-08-17 and had
/// never been mapped onto this world. Across all 96 blocks it is worth 0.011 of
/// warmth where the converted figure is worth 0.27, so Taiga, Windswept Hills,
/// Gravelly Hills and Stony Shore generated snow-capped - `freezesAt` already
/// had the converted one - and were then told it was raining, and lightning
/// struck the ice.
constexpr float kReferenceWarmthPerBlock = 0.00125f;
/// Our blocks per reference block. `freezesAt`'s snow-line jitter is quoted in
/// the reference's blocks as well and is scaled by this same number.
constexpr float kWorldScale = 0.28f;
constexpr float kWarmthPerBlock = kReferenceWarmthPerBlock / kWorldScale;
constexpr float kWarmthBaseHeight = 29.0f;

/// The height at which a biome of this fixed `warmth` first freezes, in our
/// blocks. Here so the assert below can state what the lapse is *for* rather
/// than compare one side of its own derivation against itself.
///
/// **No run-time caller, and it must not become one by substitution.**
/// `precipitationFor` clamps the drop at `kWarmthBaseHeight` - nothing below
/// the base cools - and this inverse has no clamp to give it, so the two agree
/// above the base and disagree beneath it. Swapping the arithmetic there for a
/// "snow above this line" test would hand a cold valley floor rain, silently.
/// The assert below the next one holds that difference still.
constexpr float freezingHeight(float warmth) {
    return kWarmthBaseHeight + (warmth - kFreezingWarmth) / kWarmthPerBlock;
}

// Taiga's row in `Biome.cpp` carries 0.25 and the biome generates snow-capped:
// its snow line is y 51 of a world whose tallest surface is `kMaxSurface = 90`.
// The reference's raw pair - 0.00125 above its y 81 - puts the same biome at
// y 161, and the rate alone at y 109; neither is reachable here, so pasting
// either back in fails the build rather than the mountain.
static_assert(freezingHeight(0.25f) > 45.0f && freezingHeight(0.25f) < 55.0f,
              "a 0.25-warmth biome must cross freezing inside a 96-block world; if this fails, "
              "the lapse is the reference's raw per-reference-block figure rather than ours");

// A biome already below freezing solves to a line *under* the base height,
// where `precipitationFor`'s clamp means no cooling happens at all - so the
// inverse and the rule genuinely part company down there, and the rule is the
// one that is right. The single edit that fails this: giving `freezingHeight`
// its own `std::max` and calling the two interchangeable.
static_assert(freezingHeight(kFreezingWarmth - 0.05f) < kWarmthBaseHeight,
              "the freezing line for a cold biome sits below the height where cooling starts, "
              "which is why precipitationFor clamps rather than comparing against this");

/// How fast the levels move toward their targets - the reference's 0.01 a tick.
constexpr float kLevelRampPerSecond = 0.2f;

/// The level at which precipitation counts as **actually falling on the world**,
/// as opposed to a sky that is merely darkening or clearing.
///
/// `m_rainOn` alone is the wrong test for anything that touches blocks: it flips
/// the instant the cycle decides to rain, while `m_rainLevel` still needs
/// `1 / kLevelRampPerSecond = 5` seconds to climb, so a farmland hydrating or a
/// snow layer settling off the raw flag would happen under a clear sky. Above
/// this level there are visible drops on screen and the two agree.
///
/// **`Main.cpp` holds an unlinked copy of this same 0.2**, in the expression
/// that computes `world.setPrecipitating(...)`. That is a duplicated constant
/// across a file boundary, which is bug shape #1 in miniature; it is filed
/// rather than fixed because `Main.cpp` belongs to another owner. When it is
/// reconciled, this is the one that should survive, because it sits beside the
/// ramp rate that gives it its meaning.
constexpr float kFallingLevel = 0.2f;

// Not vacuous on either side: a level below the ramp's per-second step would be
// reached within a single frame of the flag turning on, which is the same as not
// having the gate at all, and one at or above 1.0 could never be reached.
static_assert(kFallingLevel > kLevelRampPerSecond * kTickSeconds && kFallingLevel < 1.0f,
              "the falling gate must be far enough above zero that the level ramp takes real time "
              "to cross it, and low enough to be crossed at all");

/// **Coverage leads the rain.** Clouds gather, then it rains; a deck that
/// thickens at the same moment the first drop falls reads as a slider being
/// dragged rather than as weather having a cause.
///
/// The two numbers are one mechanism and neither does the job alone. The rate
/// is the slow half: at 0.03 a second the deck takes `1 / 0.03 = 33` seconds to
/// go from clear to overcast against the levels' 5. The **lead** is the early
/// half - the cover starts climbing `kCoverageLeadSeconds` before the rain flag
/// turns on, which has it `30 * 0.03 = 0.9` of the way in when the first drop
/// falls and full a moment later.
///
/// **The rate on its own was worse than nothing**, and that is what shipped:
/// `update` floors the cover at the rain level so rain cannot fall out of a
/// clear sky, and with no lead the rain is always the one in front, so the
/// floor dragged the deck up in exact lockstep with it - the one effect this
/// pair exists to prevent, using the constant that was supposed to prevent it.
constexpr float kCoverageRampPerSecond = 0.03f;
constexpr float kCoverageLeadSeconds = 30.0f;

/// How fast the wind chases its target, in **units a second**, where a unit is
/// one of `update`'s wind numbers - calm is 1 and a full storm is 12.
///
/// Hoisted out of `update`, where it was a bare `0.35f`, so that the note on
/// `restore` can name it rather than restate the number. It is the slowest ramp
/// in this file by a wide margin - a load in mid-storm takes `(12 - 1) /
/// kWindRampPerSecond`, about thirty-one seconds, to reach full.
///
/// **That used to be harmless and is not any more.** This comment previously
/// ended "survivable only because `windSpeed()` currently has no reader at
/// all", which was true when written on 2026-08-19 and false by the end of the
/// same day - `Main.cpp` now reads it twice, for `weatherWind` and for the
/// cloud deck's drift. Thirty-one seconds of
/// ramp is now thirty-one seconds of visibly still foliage, so `restore` snaps
/// past it rather than living with it. **The ramp itself is deliberately left
/// slow**: it is right for weather that turns while you watch, and wrong only
/// for a load, which is the one case `restore` now handles.
constexpr float kWindRampPerSecond = 0.35f;

// The single edit that fails this: shortening the lead, or slowing the ramp,
// until the deck is still thin when the rain arrives - at which point the floor
// in `update` takes over and the two move together again.
static_assert(kCoverageLeadSeconds * kCoverageRampPerSecond > 0.75f &&
                  kCoverageRampPerSecond < kLevelRampPerSecond,
              "the deck must be most of the way in before the first drop, and must still take "
              "longer to gather than the rain takes to reach full");

/// **`Rain` and `Snow` are not interchangeable, and the difference is a
/// gameplay rule rather than a choice of particle.** Verified 2026-08-19 by
/// cross-reading two pages, because one of them does not settle it:
/// [[Rain]] says "Rain extinguishes fires" and [[Farmland]] says "Farmland can
/// also be hydrated by rain", while [[Snowfall]] draws the contrast explicitly -
/// **"Unlike with rain, any entities that are on fire are not extinguished on
/// contact with snow."** So the wet effects - hydrating farmland, putting out an
/// open fire, quenching a burning mob - belong to `Rain` **only**.
///
/// A consumer that asks `!= None` has therefore silently opted every snowy
/// biome into the wet rules. That is not hypothetical: it is exactly what
/// `Main.cpp` does today when it sets `World::setPrecipitating`, filed as a
/// finding against that file. The distinction exists here so that it can be
/// read; collapsing it at the call site is what makes it useless.
/// **CROSS-FILE CONSTRAINT, ENFORCED BY THE ASSERT BELOW RATHER THAN BY THIS
/// PARAGRAPH.** Every consumer tests this type by **equality against one
/// enumerator**, not by a `switch`. Equality tests never warn, and `/W4` has
/// C4062 off, so a fourth enumerator would otherwise produce **no diagnostic
/// anywhere** - it would simply take the `else` branch of every test and be
/// silently absent. The `Count` sentinel plus the `static_assert` turns that
/// silent failure into a build failure at the moment of the breaking edit,
/// which is the only reason it exists.
///
/// The consumers, named symbolically so this survives edits to them. **Four
/// sites, and one function appears twice because its two halves want opposite
/// things** - which is the actual trap here:
///
///   - `Main.cpp`, where `World::setPrecipitating` is set, tests
///     `== Precipitation::Rain`. A new fall type gets no farmland hydration and
///     does not extinguish fire.
///   - `World::weatherTickColumn` tests `!= Precipitation::Snow` and returns.
///     A new fall type accumulates nothing and forms no ice.
///   - `Weather::strike`, **lightning half**, tests `!= Precipitation::Rain`
///     and returns, which is what keeps bolts off frozen peaks. A new fall type
///     gets no lightning.
///   - `Weather::strike`, **`setWeatherFalling` half at the top of the
///     function**, is deliberately precipitation-**agnostic**: it reads
///     `m_rainOn` and `m_rainLevel` only. **Do not "tidy" this by hoisting the
///     lightning half's `kind` test to the top of the function** - that single
///     edit disables snow accumulation and ice, because snow columns would stop
///     reporting that anything is falling. The two halves are in one function
///     and must stay on opposite sides of that test. See the note above
///     `strike`.
///
/// The seeded `precipitationFor` overload also tests `== Precipitation::None`,
/// but only to forward its dry answer, so it needs nothing.
///
/// **Three more sites in `Main.cpp` that this list did not name, added
/// 2026-08-19 after sweeping for them.** None of them changes the assert, and
/// two are harmless, but the middle one decides a *sound*:
///
///   - the `falls` local tests `!= Precipitation::None`, which is generic and
///     wants nothing;
///   - the rain **sound** is gated on `kind == Precipitation::Snow` being
///     false, so a fourth fall type would be given the rain recording rather
///     than silence - which is the one of the three worth visiting;
///   - the splash particles test `== Precipitation::Rain`, so a fourth type
///     gets none, matching the farmland rule above.
///
/// **Do not trust that list to stay complete - re-run the search.** The set is
/// "every comparison against a `Precipitation::` enumerator"; a bare-name sweep
/// for `Precipitation` over comment-stripped source finds it in seconds, and a
/// list written here rots while a search does not. **This paragraph was itself
/// wrong within an hour of being written** - it claimed `strike` was wholly
/// agnostic, and the sweep is what caught it.
///
/// **And the obvious sweep has a measured blind spot: a single-line regex for
/// `(==|!=)\s*Precipitation::` misses `Weather::strike`'s own lightning test**,
/// because clang-format puts the `!=` at the end of one line and
/// `Precipitation::Rain` at the start of the next. Run it over the file as one
/// string, or sweep the bare word `Precipitation` and read the hits - the
/// stricter pattern is the one that looks more careful and quietly drops the
/// site this list exists to protect. Measured here on 2026-08-19: the
/// single-line form returned every site above except that one.
enum class Precipitation : std::uint8_t {
    None,
    Rain,
    Snow,
    /// Not a fall type. Exists only so the assert below can count.
    ///
    /// **`Count` is unreachable as a value, and that is a property of the
    /// producers rather than of this enum.** Both `precipitationFor` overloads
    /// leave only through named enumerators: each has one `return
    /// Precipitation::None` and one ternary yielding `Snow` or `Rain`, four
    /// exits between them, with no arithmetic, no `static_cast` and no variable
    /// anywhere in the return path. Nothing else produces a `Precipitation`.
    ///
    /// **The constraint on a future writer is therefore: produce a
    /// `Precipitation` by naming an enumerator, never by casting an integer,
    /// indexing a table or doing arithmetic.** Break that and `Count` reaches
    /// the four consumers listed above - every one of which compares against a
    /// single enumerator with `==` or `!=`, so every one falls into its *else*
    /// branch. No farmland hydration, no snow accumulation, no ice, no
    /// lightning, all at once, silently, with a green build. **No assert can
    /// catch it**: a cast is legal C++ and the value is in range. This comment
    /// is the only instrument that exists for it.
    ///
    /// Falsified by any producer of a `Precipitation` that is not a named
    /// enumerator; search `static_cast<Precipitation>` and read the return
    /// paths of both overloads. **Read them rather than grepping**: the
    /// enumerator does not follow the word `return` in the ternary exits, so a
    /// sweep for `return Precipitation::` reports the two `None` exits and
    /// misses `Rain` and `Snow` entirely. That exact sweep was run here on the
    /// day this was written and returned "1 distinct enumerator"; the claim
    /// survived only because the bodies were then read.
    Count,
};

// **Negate this to check it earns its place**: change the 3 and the build must
// red. It fires on exactly the edit that breaks the four consumers above -
// adding an enumerator before `Count` - and on nothing else.
//
// **AUDITING NOTE, because this file has the shape that cost the fleet an hour
// (finding 9753, 2026-08-19).** A bare search for `static_assert` over this
// file over-counts: one hit is *quoted inside the doc comment above*, prose
// describing this assert rather than an assert. Counting it shifts every index
// by one and lands **this** assert - the newest and the only one coupled to
// another file's behaviour - at the position where a self-contained one is
// expected. That is precisely how `Loot.hpp`'s `Block.hpp` coupling was
// declared absent and then restored. **Search `^\s*static_assert` instead**;
// the anchor selects statements and rejects prose. Re-run the search rather
// than trusting a count written here, which is why no count is written here.
static_assert(static_cast<int>(Precipitation::Count) == 3,
              "a fall type was added: visit Main.cpp's setPrecipitating call (tests == Rain), "
              "World::weatherTickColumn (tests != Snow), and BOTH halves of Weather::strike - its "
              "lightning half tests != Rain while its setWeatherFalling half must stay agnostic - "
              "then update this count");

/// What falls in a column, or nothing.
///
/// **The dry test is a tag, not a list of biome names.** The reference asks
/// whether the biome's downfall is zero; ours asks whether it is tagged dry or
/// badlands, which is the same set and survives a new biome being added.
///
/// This form has no column to ask about, so it runs the lapse without
/// `freezesAt`'s +/-2.24-block jitter and its snow line is a clean contour. Where
/// the caller knows the seed and the column - which is everywhere the answer
/// has to agree with the ground - use the five-argument form below.
Precipitation precipitationFor(BiomeId biome, int surfaceY);

/// The same question asked of a real column, which answers it with `freezesAt`
/// itself: one rule, jitter and all, so what falls on a column can never
/// disagree with what was generated there.
Precipitation precipitationFor(BiomeId biome, int surfaceY, std::uint32_t seed, int worldX,
                               int worldZ);

/// A strike, while it is on screen.
struct Strike {
    glm::vec3 position{0.0f};
    /// Counts up. The bolt and the flash both read it.
    float age = 0.0f;
    float duration = 0.0f;
    /// One to three return strokes, which is what stops a flash reading as a
    /// camera going off.
    int pulses = 1;
    std::uint32_t seed = 0;
    /// Damage is dealt once, on the frame it appears.
    bool resolved = false;
};

/// The whole weather state, ticked by the game loop.
class Weather {
public:
    explicit Weather(std::uint64_t seed);

    /// `cycle` false freezes the flags where they are, which is the reference's
    /// `doWeatherCycle` game rule and how the debug key holds a storm open.
    void update(float deltaSeconds, bool cycle);

    /// Clear, rain, storm - what the debug key steps through. **Rain first**,
    /// because the whole point of the key is seeing weather on demand and a
    /// first press that produces clear skies is a wasted one.
    ///
    /// **It also outranks `restore`**, which skips its two flag assignments
    /// while `m_forced` is non-zero - see there for why that had to be spelled
    /// out in code rather than left to call order.
    void force(int state);
    int forced() const { return m_forced; }

    /// Puts the cycle back where a save left it.
    ///
    /// **Four fields, and they are the whole of the weather's save state.** The
    /// two flags and the two countdowns are what `update` reads; the five ramps
    /// below are computed from them and are deliberately not part of this.
    /// The reference stores the same four - active flags plus timers - so this
    /// is not a divergence.
    ///
    /// **`thundering` is `m_thunderOn` on its own, NOT `storming()`, and
    /// getting that wrong silently inverts the storm phase.** The two
    /// countdowns in `update` never consult each other, so thunder running
    /// while rain is off is an ordinary, reachable state that simply reports
    /// `storming() == false`. A caller that saves `storming()` and restores it
    /// here loses the thunder flag; the thunder timer then expires and flips it
    /// *on*, so the world comes back in the opposite phase to the one it was
    /// saved in. Save `thundering()`, not `storming()`.
    ///
    /// **The ramps are snapped to their settled values, not faded in.** A load
    /// is a resumption rather than a transition - the storm was already at full
    /// when the player saved - so fading in from calm animates an event that
    /// never happened.
    ///
    /// **This reversed an earlier decision on 2026-08-19, and the reversal is
    /// recorded because the original reasoning was sound when it was written.**
    /// The first version left them to fade, on two supports: that snapping
    /// "needed a second copy of `update`'s target expressions", and that
    /// nothing read the wind. The first is answered - the expressions were
    /// hoisted into `rainLevelTarget`, `thunderLevelTarget` and
    /// `windSpeedTarget`, so there is one copy and `restore` uses it. The
    /// second **stopped being true within hours**: `Main.cpp`'s `weatherWind` and
    /// `:11388` now read `windSpeed()`.
    ///
    /// The consequence was visible rather than theoretical. Only the wind is
    /// genuinely slow - see `kWindRampPerSecond`, six times slower than the
    /// levels - so loading a saved thunderstorm gave heavy rain over still
    /// trees for about thirty seconds, made exact by `Main.cpp` subtracting a
    /// calm floor of 1.0 that is precisely `m_windSpeed`'s default.
    ///
    /// Defensive about the two timers, because they come off disk: negative,
    /// infinite and NaN all become zero, which makes `update` roll a fresh
    /// countdown on the next tick. NaN is the one that matters - `m_rainSeconds
    /// <= 0.0f` is *false* for NaN, so an unguarded NaN never expires and the
    /// weather freezes for the rest of the session.
    ///
    /// The flags are taken as given. There is no invalid pair: all four
    /// combinations are states `update` can reach on its own.
    ///
    /// **A `start_weather` override outranks the save, and the two flags are
    /// the only things it outranks.** `m_forced` is not weather, it is an
    /// instruction from the settings file, and `restore` skips the two flag
    /// assignments while one is in effect. Three files already promised this -
    /// `Settings.hpp`'s "anything but 0 holds until V is pressed",
    /// `WorldStore.hpp` naming `m_forced` the `settings.startWeather` override,
    /// and `Main.cpp`'s own call site - and none of them described the code,
    /// because "`restore` does not touch `m_forced`" was never sufficient:
    /// `Main.cpp` forces first and restores second, so the flags were
    /// overwritten while `m_forced` went on freezing the cycle. **The override
    /// did not merely fail, it froze the state it had failed to change** - a
    /// `start_weather=1` over a clear save gave clear skies that could never
    /// change, and `start_weather=3` over a stormy one gave a storm that could
    /// never end. The countdowns are still restored, because they are simulated
    /// state rather than an instruction and `update` ignores them for as long
    /// as anything is forced; that is what lets the cycle resume from the save
    /// the moment V returns `m_forced` to 0.
    ///
    /// **`m_flash` is cleared with `m_strikes`, not merely alongside them.**
    /// The list is what produces the flash and the flash is what is seen, so
    /// clearing one without the other carried the previous world's last bolt
    /// into the sky of the one being loaded for a frame. It heals itself on the
    /// next `update`, which is why it survived being read.
    void restore(bool raining, bool thundering, float rainSeconds, float thunderSeconds);

    /// The thunder flag on its own. **This is the half of the pair `storming()`
    /// cannot give you back**, because `storming()` is the conjunction and a
    /// false answer does not say which side was false. `storming() == raining()
    /// && thundering()` holds by construction.
    bool thundering() const { return m_thunderOn; }

    /// The two countdowns. **Seconds remaining, not seconds elapsed, and not
    /// ticks** - `update` subtracts a delta in seconds from these and converts
    /// from ticks only at the roll. All three of those distinctions compile and
    /// validate if you get them backwards, which is why they are stated here.
    ///
    /// **The save path landed on 2026-08-19 and these are wired.**
    /// `Main.cpp`'s world-save path fills `WorldStore`'s `weatherRainSeconds` and
    /// `weatherThunderSeconds` from them, and `restore` above takes them back.
    /// An earlier version of this comment said they "read as dead until
    /// `Main.cpp` wires the save" and told a future sweep to conclude the save
    /// path was unfinished - **that instruction is now the wrong one and would
    /// send someone to write a second writer for a field that already has
    /// one.** Corrected rather than deleted because the wrong version is the
    /// interesting part: a negative claim in a header outlives every ledger.
    float rainSeconds() const { return m_rainSeconds; }
    float thunderSeconds() const { return m_thunderSeconds; }

    /// The flag rather than the ramp. Correctly not used for anything visual,
    /// which reads `rainLevel` and keeps fading after the flag drops. It is the
    /// right question for gameplay: whether a crop is watered or a mob may
    /// spawn is a yes or a no, not a fraction.
    ///
    /// **It has one caller, and this comment previously claimed zero.**
    /// `Main.cpp`'s world-save path writes `saved.weatherRaining = weather.raining() ? 1 :
    /// 0` - the save path, which landed hours after the zero was measured. The
    /// old text set an explicit falsification condition, "any `.raining()`
    /// outside this header", and that is exactly what caught it. Measured
    /// 2026-08-19 11:02.
    ///
    /// **The substantive point the old text made is unaffected and still
    /// matters: a low caller count here never meant rain had no gameplay
    /// effect.** Two gameplay paths reach the world without going through this
    /// accessor. `Main.cpp` calls `precipitationFor` for the player's column,
    /// combines it with `rainLevel() > kFallingLevel` and pushes one bit into
    /// `World::setPrecipitating`, which farmland hydration and the fire update
    /// read; and `strike` pushes `m_rainOn && m_rainLevel > kFallingLevel` into
    /// `World::setWeatherFalling`, which gates freezing water and settling
    /// snow, reading the members directly because it is inside the class.
    /// **Never conclude from an accessor's caller count that its feature is
    /// absent** - that inference has been wrong twice on this one accessor.
    bool raining() const { return m_rainOn; }
    /// Two uses, both inside `Weather.cpp` - the thunder ramp and the strike
    /// gate. Nothing outside this file asks whether it is storming, and those
    /// two are what prove the reachability probe below can still find a hit.
    bool storming() const { return m_rainOn && m_thunderOn; }

    /// 0 to 1, ramped. Everything visual reads these rather than the flags.
    float rainLevel() const { return m_rainLevel; }
    float thunderLevel() const { return m_thunderLevel; }
    /// Runs ahead of the rain and settles far more slowly, so the sky thickens
    /// before the first drop and stays heavy after the last.
    float cloudLevel() const { return m_cloudLevel; }

    /// Blocks per second, at cloud height. One number drives the deck's drift,
    /// the rain's slant and anything else that should agree with them.
    ///
    /// **It now has two readers, and this comment previously said it had
    /// none.** `Main.cpp`'s `weatherWind` takes `std::max(0.0f, windSpeed() -
    /// kCalmWindSpeed)` as its `weatherWind`, and `:11388` clamps it with
    /// `std::min(windSpeed(), kMaxDeckWindFactor)` for the deck. Measured
    /// 2026-08-19 11:02.
    ///
    /// **The falsification condition the old text set was met, which is the
    /// only reason this was caught.** It ended: "What would falsify this:
    /// `setWind` being fed from a `Weather` member." `weatherWind` is exactly
    /// the value that becomes `windStrength` and is handed to
    /// `Renderer::setWind`, so a `Weather` member now feeds it. **Leave that
    /// sentence pattern on any negative claim written here** - it is what
    /// turned a stale comment into a five-minute check instead of a wrong
    /// belief someone acts on.
    ///
    /// **So the two-wind-models defect is closed, and finding 1160's theory was
    /// right rather than wrong.** That finding proposed that one dead wind
    /// accessor explained both the rain slant and the `shadow.vert` foliage
    /// sway. It was rejected on 2026-08-19 05:22 by tracing `weatherWind` back
    /// to `rainAmount` and `thunderAmount` with `m_windSpeed` nowhere in the
    /// chain. **That trace was accurate at the time and the rejection is now
    /// obsolete**: `Main.cpp` has since been rewired to source `weatherWind`
    /// from here, which is what 1160 asked for. The rejection was a correct
    /// reading of a tree that then moved, not a mistake in the reading.
    ///
    /// **The live consequence, and why `restore` changed.** Being read makes
    /// `kWindRampPerSecond`'s thirty-one seconds visible: `m_windSpeed`
    /// defaults to 1.0 and `Main.cpp`'s `kCalmWindSpeed` is also `1.0f`, so a
    /// freshly loaded world computes a sway of exactly zero and climbs for half
    /// a minute while the rain is at full in five seconds. `restore` now snaps
    /// the ramps rather than fading them, which is that bug's fix.
    ///
    /// **Do not read any of this as "precipitation falls vertical" - it does
    /// slant.** `Main.cpp` derives a slant and hands it to `setPrecipitation`,
    /// and `buildPrecipitationMesh` below takes a wind direction.
    ///
    /// Method for the original zero, kept because it is the strongest
    /// reachability instrument this project has and it was *correct* - what
    /// expired was the tree, not the technique. Mark these accessors
    /// `[[deprecated]]` in a shadow copy of this header, put it first on the
    /// include path, and compile all 39 translation units in `game/src`.
    /// `storming` came back with its two known internal uses - a control that
    /// returns the number ground truth says it should, which is the test,
    /// rather than merely returning something - while `raining` and this one
    /// came back with none. Every TU was proven compiled by exit code, a
    /// freshly written `.obj` and an `error|fatal` scan. **The lesson is that a
    /// reachability result is a measurement with a timestamp, not a property of
    /// the code**: two of the three answers it gave that morning had changed by
    /// midday, this one and `raining`, which `Main.cpp`'s save path now writes.
    float windSpeed() const { return m_windSpeed; }

    /// How much of the sky a strike is lighting, 0 when none is. Already
    /// includes the multi-pulse envelope.
    float flash() const { return m_flash; }
    const std::vector<Strike>& strikes() const { return m_strikes; }
    std::vector<Strike>& strikes() { return m_strikes; }

    /// Rolls strikes and places them on real columns, **and hands the world the
    /// one bit of weather state that gameplay needs.** Separate from `update`
    /// because it needs the world, and `update` deliberately does not.
    ///
    /// **The name is now half a lie and cannot be fixed from this side.** It is
    /// the only member that takes a `World&`, and `Main.cpp` calls it
    /// unconditionally every frame - not behind `storming()`, not behind any
    /// weather test - so it is the single place a weather fact can reach the
    /// world without another owner's file changing. The push therefore happens
    /// at the very top, *before* the storm early-out, because snow settles in
    /// ordinary snowfall and would never see a thunderstorm gate.
    ///
    /// **A better shape exists. DO NOT PART-BUILD IT - it is one edit in two
    /// files and it is unsafe as either half.** The shape is `Main.cpp` calling
    /// `world.setWeatherFalling(...)` itself, and `strike` going back to being
    /// only about lightning. An earlier version of this paragraph described
    /// that in the imperative and called it "one line", which is the shape of
    /// comment that gets honoured by a reader who cannot see the other half.
    ///
    /// **Honouring it partially produces two writers for one field**, since
    /// nothing removes the push below; they run in an order neither file
    /// controls and the last one wins.
    ///
    /// **And the obvious way to write it is wrong.** The invitation was to put
    /// it "beside the existing `setPrecipitating` call" - but that call is
    /// deliberately `kind == Precipitation::Rain`, and copying its predicate
    /// would make `m_weatherFalling` false during snowfall, **silently
    /// disabling snow accumulation and ice formation**, which are the only
    /// things that read it. The predicate must stay precipitation-agnostic:
    /// `m_rainOn && m_rainLevel > kFallingLevel`, no `kind` test.
    ///
    /// So if you take it: move the push and delete it from `strike` in the same
    /// commit, and keep the predicate above. Otherwise leave both alone.
    void strike(World& world, const glm::vec3& around, float deltaSeconds);

private:
    float roll();
    int rollTicks(int minTicks, int maxTicks);

    /// The three steady-state values the two flags imply, with no ramp between.
    ///
    /// **One owner for `update`'s target expressions, and that is the whole
    /// point of them existing.** `update` ramps towards these; `restore`
    /// assigns them outright. Before they were hoisted, `restore` could only
    /// snap by writing a second copy of the same arithmetic - bug shape #1, a
    /// value derived somewhere other than the one place that owns it - and that
    /// cost was the stated reason `restore` did not snap at all.
    ///
    /// Named `...Target` rather than reusing the accessor names because
    /// `update` holds locals called `rainTarget`, `thunderTarget` and
    /// `windTarget`, and a member function a local shadows is a trap.
    ///
    /// **`windSpeedTarget` reads the two levels, not the two flags**, so it is
    /// only meaningful after they hold their settled values. `update` satisfies
    /// that by ramping them first; `restore` satisfies it by assigning them
    /// first. Call it last, either way.
    float rainLevelTarget() const { return m_rainOn ? 1.0f : 0.0f; }
    float thunderLevelTarget() const { return storming() ? 1.0f : 0.0f; }
    float windSpeedTarget() const { return 1.0f + m_rainLevel * 5.0f + m_thunderLevel * 6.0f; }

    std::uint64_t m_random;
    bool m_rainOn = false;
    bool m_thunderOn = false;
    float m_rainSeconds = 0.0f;
    float m_thunderSeconds = 0.0f;
    float m_rainLevel = 0.0f;
    float m_thunderLevel = 0.0f;
    float m_cloudLevel = 0.0f;
    float m_windSpeed = 1.0f;
    float m_flash = 0.0f;
    /// 0 follows the cycle, 1 forces rain, 2 forces a storm, 3 forces clear.
    ///
    /// **Read by `restore` as well as by `update`**, because it is an
    /// instruction from the settings file rather than weather, and an
    /// instruction has to survive a world being loaded under it.
    int m_forced = 0;
    std::vector<Strike> m_strikes;
};

/// The falling curtain, as one mesh of camera-following quads.
///
/// **One vertical quad per column, exactly as the reference does it**, because
/// that single choice answers the hardest question for free: the quad starts at
/// the top of whatever blocks the rain in that column, so a roof, a cave and an
/// overhang all work with no shelter test anywhere. A screen-space version
/// would have needed the whole heightmap on the GPU to answer the same thing.
///
/// The streaks themselves are procedural in the fragment shader rather than a
/// texture, so they snap to the same pixel grid the blocks do.
///
/// Vertex channels, which `precipitation.frag` must agree with:
/// - colour r: sky light at the column top. g: a per-column phase. **b: the
///   wind's component along this quad**, rescaled into 0-1. a: the radial fade.
/// - uv x: **the world-space coordinate along the quad**, not a 0-1 span - the
///   slant has to be a direction in the world, and a quad-local one rotates
///   with each quad's own facing, which reads as a spiral when you look up.
///   uv y: the world height of that corner.
/// - layer: unused; rain and snow are told apart by a uniform.
engine::MeshData buildPrecipitationMesh(const World& world, const glm::vec3& eye, int radius,
                                        float level, const glm::vec2& windDirection);

/// Where a bolt starts, which is the base of the cloud deck.
///
/// **A second copy of `clouds.glsl`'s `kCloudBottom`, and there is no way to
/// assert it** - the deck is a field evaluated in GLSL, so neither side can see
/// the other. Change one and change the other, or bolts start in clear air.
///
/// Deliberately **not** mapped through `kWorldScale` like every other height in
/// this file, and this is the derivation the absence of one used to look like.
/// The reference puts its deck at y 191 (`minecraft.wiki`, "Altitude"), which
/// maps to our `24 + (191 - 63) * 0.28 = 60` - inside a 96-block world and
/// below `kMaxSurface = 90`, so peaks would stand through it and the visible
/// sky would span less than one cloud. The deck is set dressing rather than
/// terrain and sits above the build ceiling on purpose; 140 keeps the
/// reference's *proportion* - a player on the ground about 100 blocks under it
/// - rather than its altitude. `clouds.glsl` carries the same reasoning.
constexpr float kBoltTop = 140.0f;

/// The agreed signal for "no artwork, just white". Must stay below every other
/// negative sentinel `triangle.frag` tests for.
constexpr float kBoltLayer = -6.0f;

/// The bolts currently in the sky, as one mesh.
///
/// Random midpoint displacement: a line from the deck to the ground, split at
/// its midpoint, the midpoint pushed sideways, and the push halved each
/// generation. Five generations, which is where it stops reading as a polyline
/// and before the extra detail goes sub-pixel.
///
/// **Each segment is a cross of two vertical quads, not a camera-facing one.**
/// The mesh is built without knowing where the camera is, and a cross reads the
/// same from every angle - the same trick a plant blade already uses here.
engine::MeshData buildBoltMesh(const std::vector<Strike>& strikes);

} // namespace weather
} // namespace game
