#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine {

/// GPUs expose several memory types with different speed/visibility tradeoffs.
/// `typeFilter` is the set Vulkan says a given resource may use; `properties` is
/// what we need from it. Throws if nothing satisfies both.
std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties);

/// `vkAllocateMemory` and `vkFreeMemory`, counting what is currently live.
///
/// Vulkan caps how many allocations may exist at once and the spec floor is
/// 4096. One allocation per buffer, two buffers per mesh and up to two meshes
/// per chunk puts us near three thousand at render distance 12 - so the ceiling
/// is real, and the failure mode is `vkAllocateMemory` returning
/// `TOO_MANY_OBJECTS` with nothing having warned first.
///
/// The count is a process-wide static, which the rest of the engine avoids on
/// principle. It is the honest shape here: the limit is a property of the one
/// device, not of any object that could own the number.
VkResult allocateDeviceMemory(VkDevice device, const VkMemoryAllocateInfo& info, VkDeviceMemory* memory);
void freeDeviceMemory(VkDevice device, VkDeviceMemory memory);
std::uint32_t liveDeviceAllocations();

} // namespace engine
