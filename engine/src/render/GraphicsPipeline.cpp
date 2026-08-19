#include "engine/render/GraphicsPipeline.hpp"

#include "engine/render/Vertex.hpp"
#include "render/VulkanCheck.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

std::vector<char> readBinaryFile(const std::filesystem::path& path) {
    // Opening at the end first lets us size the buffer in one step.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("Not valid SPIR-V (size must be a non-zero multiple of 4): " + path.string());
    }

    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(bytes.data(), size);
    if (!file) {
        throw std::runtime_error("Failed to read shader file: " + path.string());
    }
    return bytes;
}

/// Owns a shader module only for as long as pipeline creation needs it. Once the
/// pipeline exists the module can be destroyed; the compiled code lives on inside
/// the pipeline.
class ScopedShaderModule {
public:
    ScopedShaderModule(VkDevice device, const std::filesystem::path& path) : m_device(device) {
        const std::vector<char> code = readBinaryFile(path);

        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
        vkCheck(vkCreateShaderModule(device, &info, nullptr, &m_module), "vkCreateShaderModule");
    }

    ~ScopedShaderModule() {
        if (m_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_module, nullptr);
        }
    }

    ScopedShaderModule(const ScopedShaderModule&) = delete;
    ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

    VkShaderModule handle() const { return m_module; }

private:
    VkDevice m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

} // namespace

GraphicsPipeline::GraphicsPipeline(VkDevice device, const PipelineDesc& desc) : m_device(device) {
    const ScopedShaderModule vertexModule(device, desc.vertexSpirv);
    const ScopedShaderModule fragmentModule(device, desc.fragmentSpirv);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule.handle();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule.handle();
    stages[1].pName = "main";

    // Geometry now arrives from a vertex buffer, described by the single layout
    // definition in Vertex.hpp.
    const VkVertexInputBindingDescription binding = Vertex::bindingDescription();
    // auto, so adding an attribute is one edit in Vertex.hpp rather than two.
    const auto attributes = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertexInput) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Counts are fixed at pipeline creation; the actual values are supplied per
    // frame via vkCmdSetViewport/vkCmdSetScissor.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = desc.cullMode;
    // Geometry is wound counter-clockwise seen from outside, and this value is
    // what actually renders boxes solid rather than hollow. Verified on screen,
    // not derived: a hand derivation through the Y-flip argued for CLOCKWISE and
    // was simply wrong. If this is ever changed, check a closed box from outside
    // before trusting the reasoning.
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable =
        (desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f) ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = desc.depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = desc.depthBiasSlope;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = desc.blend == BlendMode::None ? VK_FALSE : VK_TRUE;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    if (desc.blend == BlendMode::Additive) {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    }

    // Every attachment gets the same state. `independentBlend` is the device
    // feature that would allow otherwise and it is deliberately not enabled.
    const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size(),
                                                                            blendAttachment);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    // The fragment stage needs the flags, the vertex stage needs the matrix.
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = desc.pushConstantBytes;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = desc.descriptorSetLayout != VK_NULL_HANDLE ? 1u : 0u;
    layoutInfo.pSetLayouts = desc.descriptorSetLayout != VK_NULL_HANDLE ? &desc.descriptorSetLayout : nullptr;
    layoutInfo.pushConstantRangeCount = desc.pushConstantBytes > 0 ? 1u : 0u;
    layoutInfo.pPushConstantRanges = desc.pushConstantBytes > 0 ? &pushRange : nullptr;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout), "vkCreatePipelineLayout");

    // Dynamic rendering: describes the attachment formats directly instead of
    // requiring a VkRenderPass object to have been created up front.
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(desc.colorFormats.size());
    renderingInfo.pColorAttachmentFormats = desc.colorFormats.data();
    renderingInfo.depthAttachmentFormat = desc.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = desc.depthFormat != VK_FORMAT_UNDEFINED ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_layout;

    // **Not `vkCheck`**: `m_layout` is already owned by this half-built object,
    // and C++ runs no destructor for a constructor that throws. Pipelines are
    // rebuilt whenever shadow quality changes, so this is not a once-per-run
    // leak. Three lines rather than a `destroy()` helper because the layout is
    // the only thing that can be outstanding at this point.
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }
}

GraphicsPipeline::~GraphicsPipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
}

} // namespace engine
