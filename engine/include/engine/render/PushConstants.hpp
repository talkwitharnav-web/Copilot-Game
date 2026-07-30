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
};

} // namespace engine
