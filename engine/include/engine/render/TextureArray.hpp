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
    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }

    /// Alpha of one texel, kept from the load so a caller can build geometry
    /// from a sprite's silhouette without decoding the file a second time. Out
    /// of range reads answer zero, so walking off the edge of an image needs no
    /// bounds test of its own.
    std::uint8_t alphaAt(std::uint32_t layer, std::uint32_t x, std::uint32_t y) const;

private:
    /// Everything the constructor takes, so a throw part-way through can be
    /// released by the one function that also serves the destructor.
    void createResources(VkCommandPool commandPool, const std::vector<std::filesystem::path>& files);
    void destroy() noexcept;
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

    /// One byte per texel per layer, so `width * height * layerCount`.
    ///
    /// **The number that stood here was 46 KB, and it is out by about eight
    /// times.** That figure matches 180 layers of 16x16, which is roughly where
    /// the block sheet stood several milestones ago; it is a little over 1400
    /// layers today, or about 350 KB, and the same class also holds the
    /// creature sheet at 128 x 4704 x 1 = about 590 KB. Nearly a megabyte
    /// across the four arrays rather than the 46 KB a reader budgeting from
    /// this line would have assumed. The trade is still the right one - the
    /// alternative is decoding the same PNGs a second time elsewhere - but it
    /// is being made at a price nobody had checked since it was written.
    ///
    /// Left as a formula plus today's measurements rather than one absolute
    /// number, because the sheets grow every milestone and that is exactly how
    /// this went stale.
    std::vector<std::uint8_t> m_alpha;
};

} // namespace engine
