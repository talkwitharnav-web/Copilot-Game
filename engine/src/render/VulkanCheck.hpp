#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace engine {

const char* vulkanResultName(VkResult result);

/// Most Vulkan calls report failure through a return code rather than an
/// exception. Wrapping them means a failure surfaces at the call that caused
/// it instead of as mysterious corruption several frames later.
inline void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " + vulkanResultName(result));
    }
}

} // namespace engine
