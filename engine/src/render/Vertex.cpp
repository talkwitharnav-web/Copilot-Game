#include "engine/render/Vertex.hpp"

#include <cstddef>

namespace engine {

VkVertexInputBindingDescription Vertex::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 5> Vertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 5> attributes{};

    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);

    // UNORM, so the shader still receives a vec4 in 0..1 and never learns that
    // the buffer holds bytes.
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[1].offset = offsetof(Vertex, color);

    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(Vertex, uv);

    attributes[3].location = 3;
    attributes[3].binding = 0;
    attributes[3].format = VK_FORMAT_R32_SFLOAT;
    attributes[3].offset = offsetof(Vertex, layer);

    attributes[4].location = 4;
    attributes[4].binding = 0;
    attributes[4].format = VK_FORMAT_R32_UINT;
    attributes[4].offset = offsetof(Vertex, surface);

    return attributes;
}

} // namespace engine
