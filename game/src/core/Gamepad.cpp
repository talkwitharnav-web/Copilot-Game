#include "core/Gamepad.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

/// How far a trigger travels before it counts as a button.
///
/// Well clear of the rest position, because a released trigger is not reliably
/// at zero on a worn pad and mining is a hold: a trigger that reads pressed at
/// rest digs a tunnel on its own.
constexpr float kTriggerThreshold = 0.35f;

/// How hard a stick has to be pushed to claim the input mode. Higher than any
/// sane dead zone on purpose - drift must never take the mode off the mouse.
constexpr float kActivationMagnitude = 0.5f;

/// Below this the sprint clutch lets go. Not zero: sprinting should end when
/// you stop pushing, not when the stick has fully centred.
constexpr float kSprintReleaseMagnitude = 0.25f;

/// Camera speed at full stick, in radians per second, at sensitivity 1.
/// About 155 degrees a second, which is a whole turn in a little over two.
constexpr float kLookRadiansPerSecond = 2.7f;

/// Cursor speed at full stick, in screen units per second, at sensitivity 1.
/// The screen is two units tall, so this crosses it in a shade over a second.
constexpr float kPointerUnitsPerSecond = 1.9f;

/// Catalogue rows per second at full stick, in mouse-wheel notches.
constexpr float kScrollNotchesPerSecond = 9.0f;

/// Sticks are curved rather than linear so that small pushes give fine control
/// and the top of the range is still fast. Squared is the usual choice and is
/// what makes a pad able to aim at a block rather than sweep past it.
constexpr float kStickExponent = 2.0f;

/// Before a held direction starts repeating, and how fast it repeats after.
constexpr float kRepeatDelaySeconds = 0.4f;
constexpr float kRepeatIntervalSeconds = 0.11f;

/// The one place pad buttons are translated to the platform's.
///
/// A table rather than a cast: this enum is not the platform's order - it drops
/// Guide and adds the two triggers - so the two only agree by being written
/// down. `LeftTrigger` and `RightTrigger` have no platform button and are
/// filled in from the axes by the caller.
constexpr bool platformButton(PadButton button, engine::GamepadButton& out) {
    switch (button) {
    case PadButton::A: out = engine::GamepadButton::A; return true;
    case PadButton::B: out = engine::GamepadButton::B; return true;
    case PadButton::X: out = engine::GamepadButton::X; return true;
    case PadButton::Y: out = engine::GamepadButton::Y; return true;
    case PadButton::LeftBumper: out = engine::GamepadButton::LeftBumper; return true;
    case PadButton::RightBumper: out = engine::GamepadButton::RightBumper; return true;
    case PadButton::Back: out = engine::GamepadButton::Back; return true;
    case PadButton::Start: out = engine::GamepadButton::Start; return true;
    case PadButton::LeftThumb: out = engine::GamepadButton::LeftThumb; return true;
    case PadButton::RightThumb: out = engine::GamepadButton::RightThumb; return true;
    case PadButton::DpadUp: out = engine::GamepadButton::DpadUp; return true;
    case PadButton::DpadRight: out = engine::GamepadButton::DpadRight; return true;
    case PadButton::DpadDown: out = engine::GamepadButton::DpadDown; return true;
    case PadButton::DpadLeft: out = engine::GamepadButton::DpadLeft; return true;
    case PadButton::LeftTrigger:
    case PadButton::RightTrigger:
    case PadButton::Count:
        return false;
    }
    return false;
}

/// Whether the table above uses each platform button exactly once and forgets
/// none of them but Guide.
///
/// **Not a restatement of the table.** It says nothing about which button goes
/// where - asserting that would only compare the switch against a copy of
/// itself. It checks the two things a hand-written switch of sixteen cases
/// actually gets wrong: the same platform button written twice, and one left
/// out entirely.
constexpr bool mappingIsSound() {
    bool seen[static_cast<std::size_t>(engine::GamepadButton::Count)]{};
    std::size_t mapped = 0;
    for (std::size_t i = 0; i < kPadButtonCount; ++i) {
        engine::GamepadButton platform{};
        if (!platformButton(static_cast<PadButton>(i), platform)) {
            continue; // The triggers, which come from the axes instead.
        }
        const auto slot = static_cast<std::size_t>(platform);
        if (seen[slot]) {
            return false;
        }
        seen[slot] = true;
        ++mapped;
    }
    return mapped == static_cast<std::size_t>(engine::GamepadButton::Count) - 1 &&
           !seen[static_cast<std::size_t>(engine::GamepadButton::Guide)];
}

static_assert(mappingIsSound(),
              "every pad button must take a different platform button, and together they must cover "
              "all of them but Guide");

/// Removes the dead zone and rescales what is left back to a full 0-to-1 range.
///
/// **Radial rather than per axis.** Testing each axis on its own leaves a
/// square hole in the middle, so a slow diagonal walk works and a slow straight
/// one does not. Rescaling is what stops the first movement past the edge being
/// a jump from nothing to a fifth of full speed.
glm::vec2 applyDeadzone(glm::vec2 raw, float deadzone) {
    const float magnitude = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (magnitude <= deadzone) {
        return glm::vec2{0.0f};
    }
    const float live = std::min((magnitude - deadzone) / std::max(1.0f - deadzone, 0.001f), 1.0f);
    return raw * (live / magnitude);
}

/// Bends a already-dead-zoned stick towards its centre, keeping its direction.
glm::vec2 applyCurve(glm::vec2 stick) {
    const float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (magnitude <= 0.0f) {
        return glm::vec2{0.0f};
    }
    return stick * (std::pow(magnitude, kStickExponent) / magnitude);
}

} // namespace

void updateGamepad(Gamepad& pad, const engine::Window& window, const Settings& settings, float deltaSeconds) {
    pad.connected = window.isGamepadConnected();
    // Read before anything overwrites it: the trigger edges below are the only
    // ones derived here rather than reported by the window, so they need the
    // previous frame's answer to "were we actually reading the pad?".
    const bool wasLive = pad.live;

    const engine::GamepadAxes axes = window.gamepadAxes();
    const float deadzone = std::clamp(settings.controllerDeadzone, 0.0f, 0.9f);

    // The device reports Y negative upward, so both sticks are flipped here to
    // the "up is positive" the rest of this file is written in.
    const glm::vec2 leftRaw{axes.leftX, -axes.leftY};
    const glm::vec2 rightRaw{axes.rightX, -axes.rightY};
    const glm::vec2 left = applyDeadzone(leftRaw, deadzone);
    const glm::vec2 right = applyDeadzone(rightRaw, deadzone);

    pad.move = left;

    const glm::vec2 lookStick = applyCurve(right);
    const float lookScale = kLookRadiansPerSecond * std::max(settings.controllerLookSensitivity, 0.0f) * deltaSeconds;
    const float pitchSign = settings.controllerInvertY ? -1.0f : 1.0f;
    pad.lookRadians = glm::vec2{lookStick.x * lookScale, lookStick.y * lookScale * pitchSign};

    const glm::vec2 pointerStick = applyCurve(left);
    const float pointerScale =
        kPointerUnitsPerSecond * std::max(settings.controllerCursorSensitivity, 0.0f) * deltaSeconds;
    // Screen space grows downward, which is the opposite of the stick.
    pad.pointerDelta = glm::vec2{pointerStick.x * pointerScale, -pointerStick.y * pointerScale};

    // **The one derivation off `lookStick` that is scaled by neither the
    // sensitivity nor the invert flag its sibling two lines up gets.** Measured
    // through this function on 2026-08-19: identical stick, identical frame, and
    // `controller_look_sensitivity` at 1 or at 4 both yield 0.15 notches, while
    // `controller_invert_y` flips the aim and leaves the scroll direction alone.
    //
    // Left alone rather than "fixed", and the earlier report of it named the
    // wrong pair - it said `pointerDelta`, which comes off the **left** stick
    // and obeys `controller_cursor_sensitivity`; these two are merely adjacent
    // lines. The real pair is `lookRadians` and this, both off the right stick.
    // Which of the two scalings a hotbar cycle should take, or whether it should
    // take one at all when the reference puts hotbar cycling on the bumpers, is
    // a judgement about feel and belongs to the playtester. One line either way
    // once it is called.
    pad.scrollNotches = lookStick.y * kScrollNotchesPerSecond * deltaSeconds;

    for (std::size_t i = 0; i < kPadButtonCount; ++i) {
        const auto button = static_cast<PadButton>(i);
        engine::GamepadButton platform{};
        bool down = false;
        bool pressed = false;
        if (platformButton(button, platform)) {
            down = window.isGamepadButtonDown(platform);
            pressed = window.wasGamepadButtonPressed(platform);
        } else {
            const float travel = button == PadButton::LeftTrigger ? axes.leftTrigger : axes.rightTrigger;
            down = travel >= kTriggerThreshold;
            // A trigger has no press event, so its edge is the one thing here
            // derived from the previous frame rather than reported. Read before
            // `heldNow` is overwritten, just below.
            //
            // **`wasLive` is the same guard the window puts on every button
            // edge**, and for the same reason: held state is cleared while
            // unfocused or unplugged, so without it a trigger held across an
            // alt-tab fires a press on the way back and the game uses whatever
            // you were pointing at.
            pressed = down && !pad.heldNow[i] && wasLive;
        }

        pad.heldNow[i] = down;
        pad.pressedNow[i] = pressed;
        const float previousHeld = pad.heldSeconds[i];
        pad.heldSeconds[i] = down ? previousHeld + deltaSeconds : 0.0f;

        // Repeat is measured in whole intervals crossed since the delay, so a
        // long frame yields one repeat rather than none.
        bool repeating = false;
        if (down && previousHeld >= kRepeatDelaySeconds) {
            const float before = std::floor((previousHeld - kRepeatDelaySeconds) / kRepeatIntervalSeconds);
            const float after = std::floor((pad.heldSeconds[i] - kRepeatDelaySeconds) / kRepeatIntervalSeconds);
            repeating = after > before;
        } else if (down && pad.heldSeconds[i] >= kRepeatDelaySeconds) {
            repeating = true;
        }
        pad.repeatedNow[i] = pressed || repeating;
    }

    // Clicking the stick starts a sprint; letting the stick go ends it.
    if (pad.pressed(PadButton::LeftThumb)) {
        pad.sprint = true;
    }
    if (std::sqrt(left.x * left.x + left.y * left.y) < kSprintReleaseMagnitude) {
        pad.sprint = false;
    }

    const bool anyButton = std::any_of(pad.heldNow.begin(), pad.heldNow.end(), [](bool held) { return held; });
    const bool anyStick = std::sqrt(leftRaw.x * leftRaw.x + leftRaw.y * leftRaw.y) >= kActivationMagnitude ||
                          std::sqrt(rightRaw.x * rightRaw.x + rightRaw.y * rightRaw.y) >= kActivationMagnitude;
    pad.active = pad.connected && (anyButton || anyStick);

    if (!pad.connected) {
        pad.sprint = false;
    }

    // Last, so everything above saw the previous frame's value.
    pad.live = window.isGamepadLive();
}

InputDevice resolveInputDevice(InputMode mode, InputDevice current, const Gamepad& pad, bool keyboardActive) {
    if (mode == InputMode::KeyboardMouse) {
        return InputDevice::KeyboardMouse;
    }
    if (mode == InputMode::Gamepad) {
        // Falls back when nothing is plugged in. A pinned gamepad mode with no
        // pad would hide the pointer and then have nothing able to move it,
        // which leaves the inventory unusable by either device.
        return pad.connected ? InputDevice::Gamepad : InputDevice::KeyboardMouse;
    }
    // Unplugging drops straight back to the keyboard, because a pinned-by-habit
    // gamepad mode with nothing plugged in has no way to reach the menu.
    if (!pad.connected) {
        return InputDevice::KeyboardMouse;
    }
    if (pad.active) {
        return InputDevice::Gamepad;
    }
    if (keyboardActive) {
        return InputDevice::KeyboardMouse;
    }
    return current;
}

const char* inputModeName(InputMode mode) {
    switch (mode) {
    case InputMode::KeyboardMouse: return "keyboard";
    case InputMode::Gamepad: return "gamepad";
    case InputMode::Auto:
    case InputMode::Count:
        break;
    }
    return "auto";
}

namespace {

/// What each cue feels like, **indexed by the event** rather than switched on.
///
/// An array cannot have a missing case the way a switch can, and the assert
/// below makes a new enumerator without a row here a compile error rather than
/// a silent nothing.
constexpr std::array<RumbleShape, kRumbleEventCount> kRumbleShapes{{
    /* HitCreature  */ {0.85f, 0.55f, 0.18f},
    /* Hurt         */ {1.00f, 0.70f, 0.38f},
    /* HeavyLanding */ {1.00f, 0.55f, 0.45f},
    /* BlockBroken  */ {0.40f, 0.80f, 0.11f},
    /* BowLoosed    */ {0.55f, 0.80f, 0.16f},
    /* Explosion    */ {1.00f, 0.90f, 0.70f},
    /* Death        */ {1.00f, 1.00f, 1.10f},
}};

/// **A missing row here is invisible: it builds clean and rumbles nothing.**
///
/// `kRumbleShapes` takes its size from `kRumbleEventCount`, which is
/// `RumbleEvent::Count` in `Gamepad.hpp`. That derivation is right - it is what
/// stops two hand-written numbers drifting apart - but it moves the *size* on
/// its own while the *rows* stay hand-written, so **adding an enumerator to
/// `RumbleEvent` silently grows this array and default-constructs the new row**.
/// Every field of `RumbleShape` defaults to `0.0f`, so the new event exists, is
/// dispatched, passes the `index >= kRumbleEventCount` guard in `rumble()`, and
/// drives both motors at zero for zero seconds.
///
/// **`std::array` will not warn.** A too-long initialiser is a hard error; a
/// too-short one is filled in for you. That asymmetry is the whole hazard, and
/// it is the same shape as this project's `default:` rule - a real value
/// returned for a case nobody wrote.
///
/// Every genuine cue fades over a positive time, so `seconds` is an exact
/// detector for a row nobody wrote rather than a heuristic.
constexpr bool everyRumbleShapeIsFilled() {
    for (const RumbleShape& shape : kRumbleShapes) {
        if (!(shape.seconds > 0.0f)) {
            return false;
        }
    }
    return true;
}

static_assert(everyRumbleShapeIsFilled(),
              "a RumbleEvent has no shape in kRumbleShapes. If you just added an "
              "enumerator to RumbleEvent in Gamepad.hpp, add its row to kRumbleShapes "
              "in the same order - the array sized itself from the enum and left your "
              "new row zeroed, which is a cue that fires and is felt as nothing");

/// The level below which a motor does not really turn.
///
/// **A fact about the hardware, not a taste.** Each motor spins an off-centre
/// weight, and below roughly a quarter power it cannot overcome its own
/// friction - so a cue asking for 0.2 is obediently sent and felt as nothing.
/// Anything above zero is lifted to at least this, which is what makes the
/// quiet cues quiet rather than absent.
constexpr float kMotorFloor = 0.34f;

/// Lifts a level clear of the floor, leaving silence silent.
///
/// Written as `level + floor * (1 - level)` rather than the equivalent
/// `floor + level * (1 - floor)` because both ends then come out exact: a full
/// cue is exactly 1 and silence is exactly 0, with no rounding to reason about.
constexpr float audible(float level) {
    return level <= 0.0f ? 0.0f : level + kMotorFloor * (1.0f - level);
}

static_assert(audible(0.0f) == 0.0f, "silence must stay silent");
static_assert(audible(1.0f) == 1.0f, "and a full cue must still reach the top of the range");
static_assert(audible(0.01f) > kMotorFloor, "anything audible at all must clear the floor");

/// Whether every row is a cue that can actually be felt.
///
/// Checks what a hand-written table of magic numbers gets wrong: a level typed
/// outside the motor's range, which would clamp and silently lose the contrast
/// the number was chosen for, and a duration of zero, which would start a cue
/// that ends before the frame it began on.
constexpr bool shapesAreSane() {
    for (const RumbleShape& shape : kRumbleShapes) {
        if (shape.heavy < 0.0f || shape.heavy > 1.0f || shape.light < 0.0f || shape.light > 1.0f) {
            return false;
        }
        if (shape.seconds <= 0.0f) {
            return false;
        }
        if (shape.heavy <= 0.0f && shape.light <= 0.0f) {
            return false;
        }
    }
    return true;
}

static_assert(shapesAreSane(),
              "every cue needs both motors within 0 to 1, a duration above zero, and at least one "
              "motor actually running");

} // namespace

void playRumble(Rumble& rumble, RumbleEvent event, float strength) {
    const std::size_t index = static_cast<std::size_t>(event);
    if (index >= kRumbleEventCount) {
        return;
    }
    const RumbleShape& shape = kRumbleShapes[index];
    const float scale = std::clamp(strength, 0.0f, 1.0f);
    if (scale <= 0.0f) {
        return;
    }

    // The slot with the least left in it, which is the free one where there is
    // one and the least missed where there is not.
    std::size_t chosen = 0;
    float weakest = std::numeric_limits<float>::max();
    for (std::size_t slot = 0; slot < Rumble::kSlots; ++slot) {
        const Rumble::Active& candidate = rumble.active[slot];
        const float left = candidate.total > 0.0f ? candidate.remaining / candidate.total : 0.0f;
        const float weight = std::max(candidate.heavy, candidate.light) * left;
        if (weight < weakest) {
            weakest = weight;
            chosen = slot;
        }
    }

    rumble.active[chosen] = Rumble::Active{shape.heavy * scale, shape.light * scale, shape.seconds,
                                           shape.seconds};
}

float rumbleStrength(float measured, float atSmallest, float atLargest) {
    const float span = atLargest - atSmallest;
    const float across = span > 0.0f ? (measured - atSmallest) / span : 1.0f;
    return kFaintestCue + (1.0f - kFaintestCue) * std::clamp(across, 0.0f, 1.0f);
}

void updateRumble(Rumble& rumble, float deltaSeconds, float masterScale) {
    float heavy = 0.0f;
    float light = 0.0f;

    for (Rumble::Active& cue : rumble.active) {
        if (cue.remaining <= 0.0f || cue.total <= 0.0f) {
            cue = Rumble::Active{};
            continue;
        }
        // Faded rather than cut, which is what makes a cue read as one event
        // ending instead of the motor being switched off.
        //
        // **Square-rooted rather than straight.** A straight ramp spends half
        // the cue below half power, so what is felt is the average and not the
        // number in the table - which is most of why every cue read as weaker
        // than it was written. This holds near the peak and then drops away.
        const float fade = std::sqrt(cue.remaining / cue.total);
        // **The loudest cue wins, rather than the sum.** Adding them saturates
        // on any busy frame, and once everything is at full both motors every
        // event feels identical - which is worse than no rumble at all.
        heavy = std::max(heavy, cue.heavy * fade);
        light = std::max(light, cue.light * fade);
        cue.remaining -= deltaSeconds;
    }

    // Floored before the player's setting, not after: the floor exists so a
    // cue is felt at all, and turning rumble down to a fifth is a choice to
    // feel less. Scaling first and flooring after would ignore that choice.
    const float scale = std::clamp(masterScale, 0.0f, 1.0f);
    rumble.heavy = audible(heavy) * scale;
    rumble.light = audible(light) * scale;
}

} // namespace game
