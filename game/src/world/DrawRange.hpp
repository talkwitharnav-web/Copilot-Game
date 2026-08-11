#pragma once

#include <glm/glm.hpp>

namespace game {

/// How far entity geometry is built, and from where.
///
/// **A rendering tier and nothing else.** Everything outside this range still
/// ticks, still paths, still spawns and despawns, and is still saved - it is
/// simply not turned into triangles this frame, and it is again the moment it
/// comes back inside. Nothing may read this to decide what a creature does.
///
/// Passed rather than stored, so a mesh builder stays a pure function of its
/// arguments in the same way the chunk mesher is.
struct DrawRange {
    glm::vec3 eye{0.0f};

    /// Metres. **Zero or less means everything**, which is the tier turned off
    /// and is what makes it something to compare against.
    float distance = 0.0f;

    bool contains(const glm::vec3& at) const {
        if (distance <= 0.0f) {
            return true;
        }
        // Horizontal only, to match the chunk tier: that boundary is a column
        // radius, and measuring this one in three dimensions would leave a
        // creature on a peak drawn while the ground beneath it was not.
        const float dx = at.x - eye.x;
        const float dz = at.z - eye.z;
        return dx * dx + dz * dz <= distance * distance;
    }
};

} // namespace game
