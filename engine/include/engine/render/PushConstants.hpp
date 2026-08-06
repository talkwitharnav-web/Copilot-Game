#pragma once

#include <glm/glm.hpp>

namespace engine {

/// Data pushed straight into the command buffer alongside a draw.
///
/// Push constants are a small block (at least 128 bytes guaranteed) that avoids
/// the descriptor-set machinery entirely, which makes them ideal for a per-draw
/// matrix. The `layout(push_constant)` block in the shader must match this
/// exactly, field for field.
struct MeshPushConstants {
    glm::mat4 modelViewProjection;
    /// Direction *toward* the sun, normalised. `w` is unused padding: push
    /// constant members follow std140-like rules, so a vec3 would still occupy
    /// four floats and the padding may as well be explicit.
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 0.0f};
    /// x: ambient floor, y: how much the sun adds on top, z: 1 to apply
    /// directional light at all. HUD and sky geometry pass 0 and stay flat.
    glm::vec4 lighting{1.0f, 0.0f, 0.0f, 0.0f};
    /// x: the texture layer an animated surface was meshed with, y: the layer it
    /// should sample this frame. Swapping it here rather than in the mesh is
    /// what lets water animate without rebuilding a single chunk. z and w spare.
    glm::vec4 animation{-1.0f, -1.0f, 0.0f, 0.0f};
    /// rgb: what everything fades to with distance, w: how far away it is fully
    /// faded, or 0 for no fog at all. Screen-space geometry passes 0, or the
    /// HUD would fade out along with the world.
    glm::vec4 fog{0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace engine
