#include "render/GpuMemory.hpp"

#include <stdexcept>

namespace engine {

std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties available{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &available);

    for (std::uint32_t i = 0; i < available.memoryTypeCount; ++i) {
        const bool allowed = (typeFilter & (1u << i)) != 0;
        const bool suitable = (available.memoryTypes[i].propertyFlags & properties) == properties;
        if (allowed && suitable) {
            return i;
        }
    }
    throw std::runtime_error("No GPU memory type satisfies the requested properties");
}

} // namespace engine
