// Probe for finding 991 - a thrown potion leaves 20 degrees above the aim.
//
// Source is Mojang/bedrock-samples behavior_pack/entities/*.json, fetched and
// read rather than remembered:
//     splash_potion.json     angle_offset -20.0   power 0.5   gravity 0.05
//     lingering_potion.json  angle_offset -20.0   power 0.5   gravity 0.05
//     egg.json               angle_offset   0.0   power 1.5   gravity 0.03
//     ender_pearl.json       angle_offset   0.0   power 1.5   gravity 0.025
// So the lift belongs to the two potions and to nothing else my throw branch
// handles.
//
// The flight integration below is `Projectiles::update`'s own order, copied
// from it: position, then drag, then gravity. Getting that backwards would
// change every range printed here.
//
// CARRIES A CONTROL. A negative result with no control is indistinguishable
// from a broken test, so the sweep is also run against three deliberately wrong
// rules and has to reject all three.

#include "world/Projectile.hpp"
#include "world/Tick.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(const char* what, bool ok) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

constexpr float kDegrees = 3.14159265358979323846f / 180.0f;

// Minecraft's convention: pitch positive looks DOWN, so forward.y = -sin(pitch).
glm::vec3 forwardFrom(float pitchDegrees, float yawDegrees) {
    const float p = pitchDegrees * kDegrees;
    const float y = yawDegrees * kDegrees;
    return glm::vec3{-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}

// EXACTLY what Main.cpp now does. `offsetDegrees` is the behaviour pack's
// `angle_offset`, applied to the pitch used for the vertical component only -
// the horizontal keeps the unmodified aim's cos(pitch), which is what stops an
// upward throw tipping over the top and going backwards.
glm::vec3 liftedAim(const glm::vec3& forward, float offsetDegrees) {
    const float cosPitch = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    const float pitch = std::atan2(-forward.y, cosPitch);
    const glm::vec3 lifted{forward.x, -std::sin(pitch + offsetDegrees * kDegrees), forward.z};
    return glm::normalize(lifted);
}

// A whole flight, in `Projectiles::update`'s order, until it comes back to the
// ground it was thrown over. Returns horizontal distance travelled in blocks.
float rangeOnTheLevel(const glm::vec3& launchVelocity, float eyeHeight,
                      const game::ProjectileSpecies& species) {
    glm::vec3 position{0.0f, eyeHeight, 0.0f};
    glm::vec3 velocity = launchVelocity;
    for (int t = 0; t < 20000; ++t) {
        const glm::vec3 previous = position;
        position += velocity;
        if (position.y <= 0.0f) {
            // Land on the plane rather than at the end of the tick, so the
            // answer does not quantise to a whole tick of travel.
            const float span = previous.y - position.y;
            const float part = span > 0.0f ? previous.y / span : 0.0f;
            const glm::vec3 hit = previous + (position - previous) * part;
            return std::sqrt(hit.x * hit.x + hit.z * hit.z);
        }
        velocity *= species.inertia;
        velocity.y -= species.gravity;
    }
    return -1.0f;
}

void howFarAPotionGoes() {
    std::printf("How far a thrown potion lands, from the eye, on the level\n");
    const game::ProjectileSpecies& potion =
        game::projectileInfo(game::ProjectileKind::SplashPotion);
    // reachFrom is the eye (feet + 1.62) and the throw drops 0.1 off it.
    constexpr float kThrowHeight = 1.62f - 0.1f;

    std::printf("  power %.2f blocks/tick, gravity %.3f, inertia %.2f  (splash_potion.json)\n",
                static_cast<double>(potion.power), static_cast<double>(potion.gravity),
                static_cast<double>(potion.inertia));

    struct Row {
        float pitch;
        const char* what;
    };
    const Row rows[] = {{0.0f, "level"}, {-30.0f, "30 deg up"}, {30.0f, "30 deg down"}};
    for (const Row& row : rows) {
        const glm::vec3 aim = forwardFrom(row.pitch, 0.0f);
        const float before = rangeOnTheLevel(aim * potion.power, kThrowHeight, potion);
        const float after = rangeOnTheLevel(liftedAim(aim, -20.0f) * potion.power, kThrowHeight,
                                            potion);
        std::printf("  aiming %-12s BEFORE %5.2f blocks   AFTER %5.2f blocks   (+%.0f%%)\n",
                    row.what, static_cast<double>(before), static_cast<double>(after),
                    static_cast<double>(100.0f * (after - before) / before));
    }

    const glm::vec3 level = forwardFrom(0.0f, 0.0f);
    const float before = rangeOnTheLevel(level * potion.power, kThrowHeight, potion);
    const float after = rangeOnTheLevel(liftedAim(level, -20.0f) * potion.power, kThrowHeight,
                                        potion);
    // My own number, measured here, not carried across from the report. The
    // finding said "about 5.2 blocks"; this integration - which is
    // `Projectiles::update`'s order, position then drag then gravity - gives
    // 4.04 from an eye at 1.52 down to y = 0. Reporting theirs would have been
    // the same mistake as citing a measurement nobody can reproduce.
    check("a level throw lands close under four blocks away", before > 3.9f && before < 4.2f);
    check("and now lands meaningfully further, because it arcs", after > before * 1.3f);
}

void theLiftIsSaneAtEveryAim() {
    std::printf("The lifted aim, swept over every pitch a player can hold\n");
    int bad = 0;
    int flipped = 0;
    int sank = 0;
    float worstLoss = 0.0f;
    int worstAt = 0;
    float leastNearLevel = 1000.0f;
    for (int pitch = -90; pitch <= 90; ++pitch) {
        for (int yaw = 0; yaw < 360; yaw += 15) {
            const glm::vec3 aim = forwardFrom(static_cast<float>(pitch), static_cast<float>(yaw));
            const glm::vec3 lift = liftedAim(aim, -20.0f);
            const float length = glm::length(lift);
            if (!std::isfinite(length) || length < 0.999f || length > 1.001f) {
                if (bad == 0) {
                    std::printf("  first non-unit result: pitch %d yaw %d -> (%.4f, %.4f, %.4f) "
                                "length %.6f\n",
                                pitch, yaw, static_cast<double>(lift.x), static_cast<double>(lift.y),
                                static_cast<double>(lift.z), static_cast<double>(length));
                }
                ++bad;
                continue;
            }
            // Never over the top: the horizontal part must keep pointing the
            // way the player is looking, never turn round behind them.
            if (aim.x * lift.x + aim.z * lift.z < -1e-5f) {
                ++flipped;
            }
            // Elevation, in degrees, of the aim and of the lifted result.
            const float aimUp = glm::degrees(std::asin(glm::clamp(aim.y, -1.0f, 1.0f)));
            const float liftUp = glm::degrees(std::asin(glm::clamp(lift.y, -1.0f, 1.0f)));
            const float gained = liftUp - aimUp;
            if (gained < worstLoss) {
                worstLoss = gained;
                worstAt = pitch;
            }
            // Below the fold-back the lift must be a real lift. It cannot be
            // the full twenty degrees - renormalising a vertical that grew
            // against a horizontal that did not shortens the angle - and it
            // tapers to nothing at both poles, where there is no horizontal
            // left to tilt against. So the band that matters is the one a
            // player actually throws in.
            if (pitch >= -45 && pitch <= 45) {
                if (gained < leastNearLevel) {
                    leastNearLevel = gained;
                }
                if (gained < 5.0f) {
                    if (sank == 0) {
                        std::printf("  first aim near level that failed to gain: pitch %d yaw %d, "
                                    "%.2f -> %.2f degrees\n",
                                    pitch, yaw, static_cast<double>(aimUp),
                                    static_cast<double>(liftUp));
                    }
                    ++sank;
                }
            }
        }
    }
    std::printf("  swept %d aims\n", 181 * 24);
    check("every lifted aim is a finite unit vector", bad == 0);
    std::printf("  least gained within 45 degrees of level: %.2f degrees\n",
                static_cast<double>(leastNearLevel));
    check("every aim within 45 degrees of level gains at least five", sank == 0);
    // Past about 70 degrees up the lifted pitch crosses vertical and the
    // vertical component starts *shrinking* again while the horizontal is held
    // at the unmodified aim's cos(pitch). That fold-back is the reference's,
    // not ours - Java's `shootFromRotation` builds x and z from cos(xRot) and y
    // from sin(xRot + offset), so it folds identically. It is worth a fraction
    // of a degree and nothing else, so the test bounds it rather than banning
    // it: a real sign or formula error would be worth tens of degrees.
    std::printf("  worst elevation lost anywhere: %.3f degrees, at pitch %d\n",
                static_cast<double>(worstLoss), worstAt);
    check("the fold-back past vertical costs under a quarter of a degree",
          worstLoss > -0.25f);
    check("aiming straight up throws straight up, never back over the shoulder",
          flipped == 0);

    const glm::vec3 up = forwardFrom(-90.0f, 0.0f);
    const glm::vec3 liftedUp = liftedAim(up, -20.0f);
    std::printf("  straight up  -> (%.3f, %.3f, %.3f)\n", static_cast<double>(liftedUp.x),
                static_cast<double>(liftedUp.y), static_cast<double>(liftedUp.z));
    check("straight up stays exactly straight up", liftedUp.y > 0.999f);

    const glm::vec3 down = forwardFrom(90.0f, 0.0f);
    const glm::vec3 liftedDown = liftedAim(down, -20.0f);
    std::printf("  straight down-> (%.3f, %.3f, %.3f)\n", static_cast<double>(liftedDown.x),
                static_cast<double>(liftedDown.y), static_cast<double>(liftedDown.z));
    check("straight down stays exactly straight down, because there is no "
          "horizontal part left to tilt",
          liftedDown.y < -0.999f);

    const glm::vec3 level = forwardFrom(0.0f, 0.0f);
    const glm::vec3 liftedLevel = liftedAim(level, -20.0f);
    const float elevation = std::asin(liftedLevel.y) / kDegrees;
    std::printf("  level aim leaves at %.2f degrees above the horizon\n",
                static_cast<double>(elevation));
    check("a level aim leaves upward", elevation > 15.0f && elevation < 20.0f);
}

// ---------------------------------------------------------------------------
// THE CONTROL. If the sweep above cannot fail, its passing means nothing. Three
// rules that are wrong in three different ways, each of which must be caught.
void theControl() {
    std::printf("Control: three deliberately wrong rules, all of which must be rejected\n");

    const game::ProjectileSpecies& potion =
        game::projectileInfo(game::ProjectileKind::SplashPotion);
    constexpr float kThrowHeight = 1.62f - 0.1f;
    const glm::vec3 level = forwardFrom(0.0f, 0.0f);

    // 1. The sign flipped - reading angle_offset as "20 degrees DOWN".
    const glm::vec3 wrongSign = liftedAim(level, 20.0f);
    const float wrongSignRange = rangeOnTheLevel(wrongSign * potion.power, kThrowHeight, potion);
    const float rightRange =
        rangeOnTheLevel(liftedAim(level, -20.0f) * potion.power, kThrowHeight, potion);
    std::printf("  sign flipped (+20): lands %.2f blocks vs the correct %.2f\n",
                static_cast<double>(wrongSignRange), static_cast<double>(rightRange));
    check("CONTROL - a flipped sign is caught by the range test",
          !(wrongSignRange > rightRange * 0.9f));

    // 2. No offset at all - the bug as it stands today.
    const float noOffset = rangeOnTheLevel(level * potion.power, kThrowHeight, potion);
    check("CONTROL - no offset at all is caught by the range test",
          !(noOffset > rightRange * 0.9f));

    // 3. A pure rotation of the aim vector, which looks right and tips over the
    //    top: at 80 degrees up it throws the potion BEHIND the player.
    const glm::vec3 steep = forwardFrom(-80.0f, 0.0f);
    const float c = std::cos(-20.0f * kDegrees);
    const float s = std::sin(-20.0f * kDegrees);
    // Rotate about the right vector (yaw 0, so right is +X, rotation in the ZY plane).
    const glm::vec3 rotated{steep.x, steep.y * c - steep.z * s, steep.y * s + steep.z * c};
    std::printf("  pure rotation at 80 deg up -> z %.3f (a negative z is behind you)\n",
                static_cast<double>(rotated.z));
    check("CONTROL - a pure rotation goes over the top, and the flip test catches it",
          rotated.z < 0.0f);
    const glm::vec3 decomposed = liftedAim(steep, -20.0f);
    check("the rule actually used does not, at the same aim", decomposed.z >= 0.0f);
}

} // namespace

int main() {
    std::printf("=== 991: a thrown potion leaves 20 degrees above the aim ===\n\n");
    howFarAPotionGoes();
    std::printf("\n");
    theLiftIsSaneAtEveryAim();
    std::printf("\n");
    theControl();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
