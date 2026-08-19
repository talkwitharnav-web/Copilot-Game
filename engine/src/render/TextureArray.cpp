#include "engine/render/TextureArray.hpp"

#include "engine/render/Buffer.hpp"
#include "engine/render/VulkanContext.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace engine {
namespace {

/// The swapchain is sRGB, so the hardware converts on write. Sampling has to
/// convert the other way or everything comes out washed out.
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB;

constexpr int kChannels = 4;

/// One decoded image, freed by stb_image on destruction.
class StbImage {
public:
    explicit StbImage(const std::filesystem::path& path) {
        m_pixels = stbi_load(path.string().c_str(), &m_width, &m_height, &m_sourceChannels, kChannels);
        if (m_pixels == nullptr) {
            throw std::runtime_error("Failed to load texture: " + path.string());
        }
    }

    ~StbImage() {
        if (m_pixels != nullptr) {
            stbi_image_free(m_pixels);
        }
    }

    StbImage(const StbImage&) = delete;
    StbImage& operator=(const StbImage&) = delete;

    const stbi_uc* pixels() const { return m_pixels; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    stbi_uc* m_pixels = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_sourceChannels = 0;
};

/// Frees the command buffer on every exit path, including an exception mid-upload.
class ScopedCommandBuffer {
public:
    ScopedCommandBuffer(VkDevice device, VkCommandPool pool) : m_device(device), m_pool(pool) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffer), "vkAllocateCommandBuffers");
    }

    ~ScopedCommandBuffer() {
        if (m_commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_device, m_pool, 1, &m_commandBuffer);
        }
    }

    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;

    VkCommandBuffer handle() const { return m_commandBuffer; }

private:
    VkDevice m_device;
    VkCommandPool m_pool;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};

void transitionLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage, std::uint32_t baseMip, std::uint32_t mipCount,
                      std::uint32_t layerCount) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

TextureArray::TextureArray(const VulkanContext& context, VkCommandPool commandPool,
                           const std::vector<std::filesystem::path>& files)
    : m_context(context) {
    // See `DepthImage`: a constructor that throws gets no destructor. This one
    // is built once, so the leak dies with the process - but it throws in five
    // places and a missing texture file is the most ordinary failure in the
    // engine, so it should not also leave the device holding an image.
    try {
        createResources(commandPool, files);
    } catch (...) {
        destroy();
        throw;
    }
}

void TextureArray::createResources(VkCommandPool commandPool, const std::vector<std::filesystem::path>& files) {
    const VulkanContext& context = m_context;
    if (files.empty()) {
        throw std::runtime_error("TextureArray needs at least one image");
    }

    // **A tripwire, not a gate** - the same idiom, and the same reasoning, as
    // the `maxPushConstantsSize` check in `Renderer::createTimestampPool`. Both
    // GPUs on this machine report a `maxImageArrayLayers` of 2048 and the game
    // hands this array a little over 1400 layers today, so it cannot fire now.
    // It exists because the sheet grows by a handful of layers every milestone
    // and the failure without it is `vkCreateImage` returning a bare error code
    // at startup with nothing anywhere naming the cause.
    //
    // Checked **before a single file is decoded**, so the message arrives
    // immediately rather than after a thousand PNGs have been read off disk.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
    if (files.size() > static_cast<std::size_t>(properties.limits.maxImageArrayLayers)) {
        throw std::runtime_error(
            "This texture array asks for " + std::to_string(files.size()) + " layers but this GPU allows only " +
            std::to_string(properties.limits.maxImageArrayLayers) +
            " (VkPhysicalDeviceLimits::maxImageArrayLayers). Every image in the array is one layer, so the fix is "
            "to send fewer images - drop unused art from the list, or split the sheet across more than one "
            "TextureArray.");
    }

    std::vector<std::unique_ptr<StbImage>> images;
    images.reserve(files.size());
    for (const std::filesystem::path& file : files) {
        images.push_back(std::make_unique<StbImage>(file));
    }

    m_width = static_cast<std::uint32_t>(images.front()->width());
    m_height = static_cast<std::uint32_t>(images.front()->height());
    m_layerCount = static_cast<std::uint32_t>(images.size());

    for (const auto& image : images) {
        if (static_cast<std::uint32_t>(image->width()) != m_width ||
            static_cast<std::uint32_t>(image->height()) != m_height) {
            throw std::runtime_error("All textures in an array must share one size");
        }
    }

    // **The other axis of the same tripwire, and this one is already over the
    // line.** The layer check above guards how many images there are; nothing
    // guarded how big one of them is, and the sheets grow on both axes. Vulkan
    // only guarantees a `maxImageDimension2D` of 4096, and
    // `assets/textures/creatures.png` is 128 x 4704 today - it crossed 4096
    // between milestones with nothing to notice. Both GPUs in this machine
    // report far more than that, so the game runs here; on any part reporting
    // the guaranteed floor, `vkCreateImage` below fails with a bare code and
    // nothing anywhere names the sheet. That is word for word the failure the
    // layer check was written to abolish.
    //
    // Checked against the same `properties` fetched above, and after decoding
    // rather than before, because the size is not known until a file is read.
    if (m_width > properties.limits.maxImageDimension2D || m_height > properties.limits.maxImageDimension2D) {
        throw std::runtime_error(
            "This texture array's images are " + std::to_string(m_width) + "x" + std::to_string(m_height) +
            " but this GPU allows only " + std::to_string(properties.limits.maxImageDimension2D) +
            " on either side (VkPhysicalDeviceLimits::maxImageDimension2D). The first image is '" +
            files.front().string() +
            "'. A sheet that has outgrown the limit has to be split across more than one TextureArray, or laid "
            "out in more columns and fewer rows.");
    }

    // Blitting halves the image each level, so the chain ends when the larger
    // dimension reaches one pixel.
    m_mipLevels = 1 + static_cast<std::uint32_t>(std::floor(std::log2(std::max(m_width, m_height))));

    // Mip generation blits from each level into the next, so the image is both a
    // transfer source and destination.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kFormat;
    imageInfo.extent = {m_width, m_height, 1};
    imageInfo.mipLevels = m_mipLevels;
    imageInfo.arrayLayers = m_layerCount;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(context.device(), &imageInfo, nullptr, &m_image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device(), m_image, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(context.physicalDevice(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocateDeviceMemory(context.device(), allocInfo, &m_memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory failed for texture array");
    }
    vkCheck(vkBindImageMemory(context.device(), m_image, m_memory, 0), "vkBindImageMemory");

    // Every layer goes into one staging buffer, so the upload is a single copy
    // command with one region per layer.
    const VkDeviceSize layerBytes = static_cast<VkDeviceSize>(m_width) * m_height * kChannels;
    Buffer staging(context, layerBytes * m_layerCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    std::vector<unsigned char> combined(static_cast<std::size_t>(layerBytes) * m_layerCount);
    for (std::size_t layer = 0; layer < images.size(); ++layer) {
        std::copy_n(images[layer]->pixels(), layerBytes, combined.begin() + static_cast<std::ptrdiff_t>(layer * layerBytes));
    }
    staging.writeFromHost(combined.data(), combined.size());

    // Kept before the decoded images go out of scope. The pixels themselves are
    // not worth holding, but the shape they cut out is: it is what lets a
    // caller extrude a sprite into geometry rather than draw it on a card.
    m_alpha.resize(static_cast<std::size_t>(m_width) * m_height * m_layerCount);
    for (std::size_t texel = 0; texel < m_alpha.size(); ++texel) {
        m_alpha[texel] = combined[texel * kChannels + 3];
    }

    const ScopedCommandBuffer upload(context.device(), commandPool);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(upload.handle(), &beginInfo), "vkBeginCommandBuffer");

    transitionLayout(upload.handle(), m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, m_mipLevels, m_layerCount);

    std::vector<VkBufferImageCopy> regions(m_layerCount);
    for (std::uint32_t layer = 0; layer < m_layerCount; ++layer) {
        regions[layer].bufferOffset = layerBytes * layer;
        regions[layer].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[layer].imageSubresource.mipLevel = 0;
        regions[layer].imageSubresource.baseArrayLayer = layer;
        regions[layer].imageSubresource.layerCount = 1;
        regions[layer].imageExtent = {m_width, m_height, 1};
    }
    vkCmdCopyBufferToImage(upload.handle(), staging.handle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(regions.size()), regions.data());

    generateMipmaps(upload.handle());

    vkCheck(vkEndCommandBuffer(upload.handle()), "vkEndCommandBuffer");

    VkCommandBuffer commandBuffer = upload.handle();
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkCheck(vkQueueSubmit(context.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");

    // The staging buffer dies at the end of this scope, so the copy must be done.
    vkCheck(vkQueueWaitIdle(context.graphicsQueue()), "vkQueueWaitIdle");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = kFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.layerCount = m_layerCount;
    vkCheck(vkCreateImageView(context.device(), &viewInfo, nullptr, &m_view), "vkCreateImageView");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Nearest magnification is the whole blocky look: a texel stays a crisp
    // square up close instead of smearing into its neighbours.
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    // Mip levels themselves blend smoothly, which is what stops distant terrain
    // shimmering as the camera moves.
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    vkCheck(vkCreateSampler(context.device(), &samplerInfo, nullptr, &m_sampler), "vkCreateSampler");
}

void TextureArray::generateMipmaps(VkCommandBuffer commandBuffer) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice(), kFormat, &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0) {
        throw std::runtime_error("Texture format cannot be linearly filtered, so mipmaps cannot be blitted");
    }

    auto mipWidth = static_cast<std::int32_t>(m_width);
    auto mipHeight = static_cast<std::int32_t>(m_height);

    for (std::uint32_t level = 1; level < m_mipLevels; ++level) {
        // The previous level becomes readable, then is scaled into this one.
        transitionLayout(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, level - 1, 1, m_layerCount);

        const std::int32_t nextWidth = std::max(mipWidth / 2, 1);
        const std::int32_t nextHeight = std::max(mipHeight / 2, 1);

        VkImageBlit blit{};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.layerCount = m_layerCount;
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.layerCount = m_layerCount;

        vkCmdBlitImage(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        transitionLayout(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, level - 1, 1, m_layerCount);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // The last level was only ever written to, so it still needs its transition.
    transitionLayout(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_mipLevels - 1, 1, m_layerCount);
}

TextureArray::~TextureArray() { destroy(); }

void TextureArray::destroy() noexcept {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_context.device(), m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_context.device(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    freeDeviceMemory(m_context.device(), m_memory);
    m_memory = VK_NULL_HANDLE;
}

std::uint8_t TextureArray::alphaAt(std::uint32_t layer, std::uint32_t x, std::uint32_t y) const {
    if (layer >= m_layerCount || x >= m_width || y >= m_height) {
        return 0;
    }
    return m_alpha[(static_cast<std::size_t>(layer) * m_height + y) * m_width + x];
}

} // namespace engine
