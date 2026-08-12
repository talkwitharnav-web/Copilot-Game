#pragma once

#include "core/Settings.hpp"

#include <engine/platform/Window.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// Which device the player is currently driving the game with.
enum class InputDevice : std::uint8_t {
    KeyboardMouse,
    Gamepad,
};

/// How that device is chosen.
///
/// `Auto` is the reference's behaviour: whichever was touched last wins, with
/// no menu visit needed. The two pinned modes exist because auto-switching is
/// exactly wrong when a stick has worn enough to drift, or when a pad is left
/// plugged in and you want the keyboard regardless.
enum class InputMode : std::uint8_t {
    Auto,
    KeyboardMouse,
    Gamepad,
    Count,
};

/// The pad's controls as the game names them.
///
/// **The triggers are in here even though the device reports them as axes**,
/// because past their threshold every reader treats one as a button, and the
/// alternative is every call site repeating the same comparison.
///
/// `Guide` is deliberately absent: Windows owns the Xbox button.
enum class PadButton : std::uint8_t {
    A,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    LeftThumb,
    RightThumb,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    LeftTrigger,
    RightTrigger,
    Count,
};

constexpr std::size_t kPadButtonCount = static_cast<std::size_t>(PadButton::Count);

/// One frame of gamepad input, with the dead zone, the response curve and the
/// player's sensitivity already applied.
///
/// Deliberately still **physical**: it names buttons, not actions. What A does
/// depends on whether a screen is open, and the two call sites decide that -
/// which is how the reference's own controller layout is written, where A is
/// jump in the world and select in a menu.
struct Gamepad {
    /// A pad is plugged in and its layout is known. Stays true while the window
    /// is in the background; everything else goes quiet.
    bool connected = false;

    /// True on any frame the pad was genuinely touched, which is what drives
    /// automatic switching. A stick resting just past its dead zone does not
    /// count, or a worn pad would hold the input mode hostage.
    bool active = false;

    /// Left stick, as a direction with magnitude 0 to 1. `+x` is right and
    /// `+y` is forward.
    glm::vec2 move{0.0f};

    /// **This frame's** yaw and pitch change, in radians, `+y` pitching up.
    ///
    /// Radians rather than a stick position, and per frame rather than per
    /// second, because the mouse's equivalent is measured per *pixel* - two
    /// quantities that add into the same camera and look identical at the call
    /// site. Naming the unit here is what stops one being used as the other.
    glm::vec2 lookRadians{0.0f};

    /// This frame's UI cursor movement, in screen units - the space the
    /// inventory is laid out in, so `+y` is **down**, not up.
    glm::vec2 pointerDelta{0.0f};

    /// This frame's scrolling, in mouse-wheel notches so it can be added
    /// straight into the wheel's own accumulator. Positive scrolls the same way
    /// as pushing the wheel away from you.
    float scrollNotches = 0.0f;

    /// Whether sprint is latched on. The stick clutch, not a held button:
    /// clicking the left stick starts it and letting the stick go ends it,
    /// because nobody can hold a stick click down for a journey.
    bool sprint = false;

    std::array<bool, kPadButtonCount> heldNow{};
    std::array<bool, kPadButtonCount> pressedNow{};
    std::array<bool, kPadButtonCount> repeatedNow{};
    /// How long each button has been down, which is all `repeatedNow` needs.
    std::array<float, kPadButtonCount> heldSeconds{};

    bool down(PadButton button) const { return heldNow[static_cast<std::size_t>(button)]; }
    bool pressed(PadButton button) const { return pressedNow[static_cast<std::size_t>(button)]; }

    /// Pressed this frame, or held long enough to be auto-repeating. For the
    /// directional pad, where holding a direction should keep going.
    bool repeated(PadButton button) const { return repeatedNow[static_cast<std::size_t>(button)]; }
};

/// Reads the pad and rewrites `pad` from it.
///
/// `move` and `pointerDelta` both come from the left stick, and `lookRadians`
/// and `scrollNotches` both from the right; a screen is open or it is not, and
/// the caller picks. Computing both costs two multiplies and means neither call
/// site has to know how the other one scales.
void updateGamepad(Gamepad& pad, const engine::Window& window, const Settings& settings, float deltaSeconds);

/// Which device to listen to this frame.
///
/// `keyboardActive` is whatever the caller counts as the mouse or keyboard
/// being used. In `Auto` the answer only changes when a device is actually
/// touched, so an idle frame never flips it.
InputDevice resolveInputDevice(InputMode mode, InputDevice current, const Gamepad& pad, bool keyboardActive);

/// Name for the log and the settings file.
const char* inputModeName(InputMode mode);

/// The moments the game rumbles for.
///
/// **This list is ours.** The reference publishes a controller *button* layout
/// and a couple of touch haptics settings, but no table of what a controller
/// vibrates for, so there was nothing to copy. What is here follows the sound
/// vocabulary instead: every one of these already had a noise, which is a good
/// sign it is a moment the game considers worth reporting.
///
/// Deliberately short. Placing a block and picking an item up were both tried
/// and cut: they repeat several times a second, and a pad that never stops
/// buzzing says nothing at all.
enum class RumbleEvent : std::uint8_t {
    /// A landed melee blow. Short and solid, so it reads as contact.
    HitCreature,
    /// Any damage taken, from any source.
    Hurt,
    /// A fall long enough to hurt. The heaviest thing short of an explosion.
    HeavyLanding,
    /// A block finished breaking. The one cue with reference precedent - the
    /// touch controls have a "vibrate when breaking blocks" setting.
    BlockBroken,
    /// An arrow leaving the bow, scaled by how far it was drawn.
    BowLoosed,
    /// Scaled by distance, so a far one is a tremor and a near one is a shove.
    Explosion,
    Death,
    Count,
};

constexpr std::size_t kRumbleEventCount = static_cast<std::size_t>(RumbleEvent::Count);

/// What one cue feels like.
struct RumbleShape {
    /// The low-frequency motor: weight and impact.
    float heavy = 0.0f;
    /// The high-frequency motor: ticks and snaps.
    float light = 0.0f;
    /// How long it takes to fade to nothing.
    float seconds = 0.0f;
};

/// Mixes the cues in flight into two motor levels.
///
/// Plain data and a fixed number of slots, so nothing allocates while playing.
struct Rumble {
    struct Active {
        float heavy = 0.0f;
        float light = 0.0f;
        float remaining = 0.0f;
        float total = 0.0f;
    };

    /// More than enough: cues are short, and the quiet ones are inaudible under
    /// the loud ones anyway.
    static constexpr std::size_t kSlots = 8;
    std::array<Active, kSlots> active{};

    /// The result, rewritten by `updateRumble` and read by the caller.
    float heavy = 0.0f;
    float light = 0.0f;
};

/// Starts a cue. `strength` scales it, for the events that come in sizes.
void playRumble(Rumble& rumble, RumbleEvent event, float strength = 1.0f);

/// The strength the smallest instance of a scaled event gets.
///
/// **Not zero.** "Scaled by size" must not become "absent" at the bottom of the
/// range, or a chicken pecking you reads as a bug rather than as a small hit.
constexpr float kFaintestCue = 0.35f;

/// Maps a measured size onto a cue strength, from `kFaintestCue` up to 1.
///
/// **One place, so that "twice as big" means the same thing at every site.**
/// Each scaled cue had started growing its own arithmetic and they had already
/// picked different floors between them, which is how two events of the same
/// size end up feeling different for no reason anybody wrote down.
///
/// `atSmallest` and `atLargest` are the real range of the thing measured, in
/// its own units - hearts of damage, blocks fallen, a block's hardness.
/// Anything outside is clamped, so one outlier cannot quietly rescale the rest.
float rumbleStrength(float measured, float atSmallest, float atLargest);

/// Ages every cue and works out this frame's two motor levels.
///
/// **Computes rather than applies**: the caller sends `rumble.heavy` and
/// `rumble.light` to the window. `masterScale` is the player's setting, and
/// zero silences everything without the call sites knowing.
void updateRumble(Rumble& rumble, float deltaSeconds, float masterScale);

} // namespace game
