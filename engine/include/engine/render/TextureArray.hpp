#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace engine {

class VulkanContext;

/// A stack of same-sized images in one GPU resource, sampled by layer index.
///
/// Chosen over a texture atlas because mipmapping an atlas bleeds neighbouring
/// tiles into each other at distance — the classic voxel artifact where far-off
/// stone picks up the colour of whatever was packed next to it. Separate layers
/// share no edges, so mip generation is simply correct.
///
/// Owns its sampler as well. That is a simplification, not a rule: Vulkan keeps
/// samplers independent of images, and this should be split if a second texture
/// ever needs different filtering.
class TextureArray {
public:
    /// Every file must decode to the same dimensions. Layer indices follow the
    /// order given here.
    TextureArray(const VulkanContext& context, VkCommandPool commandPool,
                 const std::vector<std::filesystem::path>& files);
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;
    TextureArray(TextureArray&&) = delete;
    TextureArray& operator=(TextureArray&&) = delete;

    VkImageView view() const { return m_view; }
    VkSampler sampler() const { return m_sampler; }
    std::uint32_t layerCount() const { return m_layerCount; }

private:
    void generateMipmaps(VkCommandBuffer commandBuffer);

    const VulkanContext& m_context;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_layerCount = 0;
    std::uint32_t m_mipLevels = 1;
};

} // namespace engine
