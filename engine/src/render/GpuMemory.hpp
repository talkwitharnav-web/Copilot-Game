#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine {

/// GPUs expose several memory types with different speed/visibility tradeoffs.
/// `typeFilter` is the set Vulkan says a given resource may use; `properties` is
/// what we need from it. Throws if nothing satisfies both.
std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties);

} // namespace engine
