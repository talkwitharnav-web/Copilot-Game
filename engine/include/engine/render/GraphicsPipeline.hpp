#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace engine {

/// One complete configuration for drawing: both shader stages plus all the
/// fixed-function state (topology, rasterizer, blending) baked into a single
/// object the GPU validates once instead of per draw call.
///
/// Viewport and scissor are deliberately left as dynamic state so resizing the
/// window does not require rebuilding the pipeline.
class GraphicsPipeline {
public:
    /// `colorFormat` and `depthFormat` must match the attachments this pipeline
    /// renders into; dynamic rendering validates them against each other.
    /// `descriptorSetLayout` describes the resources the shaders read, and may
    /// be null for a pipeline that samples nothing.
    GraphicsPipeline(VkDevice device, const std::filesystem::path& vertexSpirv,
                     const std::filesystem::path& fragmentSpirv, VkFormat colorFormat, VkFormat depthFormat,
                     VkDescriptorSetLayout descriptorSetLayout);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine
