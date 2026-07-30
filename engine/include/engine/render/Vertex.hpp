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
    float color[3];

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions();
};

} // namespace engine
