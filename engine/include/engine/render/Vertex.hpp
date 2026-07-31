#pragma once

#include <vulkan/vulkan.h>

#include <array>

namespace engine {

/// The single definition of what a vertex is.
///
/// The shader's `layout(location = ...)` inputs must match `attributeDescriptions()`
/// exactly. Keeping the struct and its descriptions together means a format change
/// is one edit here plus one in the shader, never a hunt across the renderer.
struct Vertex {
    float position[3];
    /// Face shading and opacity, multiplied with the sampled texel. Opaque white
    /// leaves a texture untouched; the alpha channel is what lets HUD panels sit
    /// over the world without hiding it.
    float color[4];
    float uv[2];
    /// Which layer of the texture array to sample. A float because vertex
    /// attributes feed the shader most simply that way.
    float layer;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions();
};

} // namespace engine
