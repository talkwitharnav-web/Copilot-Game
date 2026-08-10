#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine {

class VulkanContext;

/// The sun's view of the world, stored as distance-to-the-nearest-surface.
///
/// A "shadow map" is the scene rendered once from where the light is, keeping
/// only how far away the first thing it hit was. Shading a pixel then asks: is
/// this point further from the sun than whatever the sun saw first in that
/// direction? If it is, something is in the way, and the point is in shadow.
///
/// One map covering the whole view would have to be enormous to give crisp
/// shadows at your feet, so it is split into **cascades**: several maps, each
/// covering a slice of the distance in front of the camera, the nearest one
/// small and sharp and the furthest one large and coarse. They live as layers
/// of one image, which is what lets the lighting pass pick between them with an
/// index instead of a branch between separate textures.
class ShadowMap {
public:
    ShadowMap(const VulkanContext& context, std::uint32_t resolution, std::uint32_t cascades);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&&) = delete;
    ShadowMap& operator=(ShadowMap&&) = delete;

    VkImage image() const { return m_image; }
    /// One cascade, to render into.
    VkImageView cascadeView(std::uint32_t cascade) const { return m_cascadeViews[cascade]; }
    /// All of them at once, to sample.
    VkImageView arrayView() const { return m_arrayView; }
    /// Compares rather than reads: the hardware returns how much of the 2x2
    /// texel neighbourhood the fragment is in front of, already filtered, which
    /// is a free first step of softening.
    VkSampler sampler() const { return m_sampler; }

    VkFormat format() const { return m_format; }
    std::uint32_t resolution() const { return m_resolution; }
    std::uint32_t cascades() const { return static_cast<std::uint32_t>(m_cascadeViews.size()); }

    /// Highest number of slices anything here will build. The lighting pass
    /// declares a fixed-size array, so this is shared with `FrameUniforms`.
    static constexpr std::uint32_t kMaxCascades = 4;

    /// Prefers a full 32-bit depth format and falls back to the 16-bit one every
    /// Vulkan device is required to support.
    static VkFormat chooseFormat(VkPhysicalDevice physicalDevice);

private:
    const VulkanContext& m_context;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    std::uint32_t m_resolution = 0;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    std::vector<VkImageView> m_cascadeViews;
    VkImageView m_arrayView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace engine
