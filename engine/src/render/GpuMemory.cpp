#include "render/GpuMemory.hpp"

#include <atomic>
#include <stdexcept>

namespace engine {
namespace {

std::atomic<std::uint32_t> g_liveAllocations{0};

} // namespace

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

VkResult allocateDeviceMemory(VkDevice device, const VkMemoryAllocateInfo& info, VkDeviceMemory* memory) {
    const VkResult result = vkAllocateMemory(device, &info, nullptr, memory);
    if (result == VK_SUCCESS) {
        g_liveAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

void freeDeviceMemory(VkDevice device, VkDeviceMemory memory) {
    if (memory == VK_NULL_HANDLE) {
        return;
    }
    vkFreeMemory(device, memory, nullptr);
    g_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
}

std::uint32_t liveDeviceAllocations() {
    return g_liveAllocations.load(std::memory_order_relaxed);
}

} // namespace engine
