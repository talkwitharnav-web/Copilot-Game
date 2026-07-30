#include "engine/render/VulkanContext.hpp"

#include "engine/core/Log.hpp"
#include "engine/platform/Window.hpp"
#include "render/VulkanCheck.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr const char* kRequiredDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    QueueFamilies result;
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && !result.graphics.has_value()) {
            result.graphics = i;
        }

        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported);
        if (presentSupported == VK_TRUE && !result.present.has_value()) {
            result.present = i;
        }

        if (result.complete()) {
            break;
        }
    }
    return result;
}

bool supportsRequiredExtensions(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    for (const char* required : kRequiredDeviceExtensions) {
        const bool found = std::any_of(available.begin(), available.end(), [required](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, required) == 0;
        });
        if (!found) {
            return false;
        }
    }
    return true;
}

bool hasUsableSurfaceSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    return formatCount > 0 && presentModeCount > 0;
}

bool validationLayerAvailable() {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, kValidationLayerName) == 0;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*userData*/) {
    const std::string message = std::string("[vulkan] ") + (data->pMessage != nullptr ? data->pMessage : "");
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        logError(message);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        logWarn(message);
    } else {
        logInfo(message);
    }
    return VK_FALSE; // VK_FALSE means "do not abort the offending call".
}

VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

} // namespace

VulkanContext::VulkanContext(const Window& window) {
    createInstance();
    createDebugMessenger();
    createSurface(window);
    selectPhysicalDevice();
    createLogicalDevice();
}

VulkanContext::~VulkanContext() {
    // Vulkan does not reference-count. Everything is destroyed in the exact
    // reverse of the order it was created in.
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn != nullptr) {
            destroyFn(m_instance, m_debugMessenger, nullptr);
        }
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void VulkanContext::createInstance() {
#ifdef ENGINE_ENABLE_VALIDATION
    m_validationEnabled = validationLayerAvailable();
    if (!m_validationEnabled) {
        logWarn("Validation layers requested but not available. Is the Vulkan SDK installed?");
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VoxelGame";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "VoxelGame Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // GLFW knows which instance extensions this platform needs to present to a window.
    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr) {
        throw std::runtime_error("GLFW reported no Vulkan surface extensions; Vulkan is unavailable on this system");
    }

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (m_validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Chaining the messenger info here (rather than only creating it afterwards)
    // is what makes instance creation and destruction itself validated.
    const VkDebugUtilsMessengerCreateInfoEXT debugInfo = makeDebugMessengerInfo();
    if (m_validationEnabled) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayerName;
        createInfo.pNext = &debugInfo;
    }

    vkCheck(vkCreateInstance(&createInfo, nullptr, &m_instance), "vkCreateInstance");
    logInfo(m_validationEnabled ? "Vulkan instance created (validation layers ON)"
                                : "Vulkan instance created (validation layers OFF)");
}

void VulkanContext::createDebugMessenger() {
    if (!m_validationEnabled) {
        return;
    }

    // Extension entry points are not linked in directly; they are looked up at runtime.
    auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (createFn == nullptr) {
        logWarn("vkCreateDebugUtilsMessengerEXT unavailable; continuing without a debug messenger");
        return;
    }

    const VkDebugUtilsMessengerCreateInfoEXT info = makeDebugMessengerInfo();
    vkCheck(createFn(m_instance, &info, nullptr, &m_debugMessenger), "vkCreateDebugUtilsMessengerEXT");
}

void VulkanContext::createSurface(const Window& window) {
    vkCheck(glfwCreateWindowSurface(m_instance, window.handle(), nullptr, &m_surface), "glfwCreateWindowSurface");
}

void VulkanContext::selectPhysicalDevice() {
    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;

    for (VkPhysicalDevice candidate : devices) {
        if (!supportsRequiredExtensions(candidate) || !hasUsableSurfaceSupport(candidate, m_surface)) {
            continue;
        }
        const QueueFamilies families = findQueueFamilies(candidate, m_surface);
        if (!families.complete()) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        // Dynamic rendering is a core 1.3 feature and the renderer depends on it.
        // Rejecting here gives a clear "no suitable GPU" error instead of a
        // confusing failure inside vkCreateDevice.
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        // This machine has both a discrete NVIDIA GPU and an integrated Intel one.
        // Without an explicit preference Vulkan may hand us the slower of the two.
        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }

        if (score > bestScore) {
            bestScore = score;
            best = candidate;
            m_graphicsQueueFamily = families.graphics.value();
            m_presentQueueFamily = families.present.value();
        }
    }

    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error("No GPU supports the required Vulkan features");
    }

    m_physicalDevice = best;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    logInfo(std::string("Selected GPU: ") + properties.deviceName);
}

void VulkanContext::createLogicalDevice() {
    // Graphics and presentation are often the same queue family. Requesting the
    // same family twice is invalid, so duplicates are collapsed here.
    const std::set<std::uint32_t> uniqueFamilies{m_graphicsQueueFamily, m_presentQueueFamily};

    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (std::uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &queuePriority;
        queueInfos.push_back(info);
    }

    VkPhysicalDeviceFeatures features{}; // Nothing beyond the baseline is needed yet.

    // Dynamic rendering (core in Vulkan 1.3) lets us begin rendering by naming
    // the target images directly, instead of building VkRenderPass and
    // VkFramebuffer objects up front. Fewer objects, and they no longer have to
    // be kept in sync with the swapchain.
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(kRequiredDeviceExtensions));
    createInfo.ppEnabledExtensionNames = kRequiredDeviceExtensions;

    vkCheck(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device), "vkCreateDevice");

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
}

} // namespace engine
