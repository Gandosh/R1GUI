// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Pipelines.h (render passes, pipelines, sampler, descriptor pool).
// Callers: RenderDevice.cpp. Calls: the Vulkan loader; SPIR-V comes from the embedded blobs.
// Failure behavior: any creation failure throws and the destructor releases what exists so far.
#include "Pipelines.h"

#include <array>
#include <stdexcept>

#include "GpuResources.h"
#include "Shaders.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/PaintList.h"
#include "r1ui/render/Texture.h"

namespace r1ui::render::detail {

namespace {

// Owns a shader module for the duration of pipeline creation.
class ShaderModule {
 public:
  ShaderModule(VkDevice device, const ShaderBlob& blob) : device_(device) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = blob.size;
    info.pCode = reinterpret_cast<const uint32_t*>(blob.data);
    check(vkCreateShaderModule(device_, &info, nullptr, &module_), "vkCreateShaderModule");
  }
  ~ShaderModule() {
    if (module_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module_, nullptr);
  }
  ShaderModule(const ShaderModule&) = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;
  VkShaderModule handle() const { return module_; }

 private:
  VkDevice device_;
  VkShaderModule module_ = VK_NULL_HANDLE;
};

// One colour attachment, cleared at the start and stored. Passes differ only in the layout the
// image is left in; every other field, including the subpass dependencies, is identical because
// the validation layer treats differing dependencies as incompatible passes, and one pipeline
// set must draw into both. The end dependency makes the result visible to a transfer read
// (offscreen readback); presentation is ordered by the render-finished semaphore instead.
VkRenderPass createPass(VkDevice device, VkImageLayout finalLayout) {
  VkAttachmentDescription attachment{};
  attachment.format = kTargetFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = finalLayout;
  const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;

  std::array<VkSubpassDependency, 2> deps{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // wait for the acquire semaphore / previous use
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].srcAccessMask = 0;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;  // make the result visible to presentation / readback
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = core::checkedCast<uint32_t>(deps.size());
  info.pDependencies = deps.data();
  VkRenderPass pass = VK_NULL_HANDLE;
  check(vkCreateRenderPass(device, &info, nullptr, &pass), "vkCreateRenderPass");
  return pass;
}

// Builds one instanced pipeline: per-instance vertex attributes of vec4 each, triangle list of
// 6 vertices per instance generated in the vertex shader, dynamic viewport and scissor.
VkPipeline createPipeline(VkDevice device, VkRenderPass pass, VkPipelineLayout layout, const ShaderBlob& vert,
                          const ShaderBlob& frag, uint32_t attributeCount) {
  ShaderModule vertModule(device, vert);
  ShaderModule fragModule(device, frag);
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertModule.handle();
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragModule.handle();
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = attributeCount * 16;
  binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
  std::array<VkVertexInputAttributeDescription, 6> attributes{};
  for (uint32_t i = 0; i < attributeCount; ++i) {
    attributes[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
  }
  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = attributeCount;
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.blendEnable = VK_TRUE;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;

  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = core::checkedCast<uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = core::checkedCast<uint32_t>(stages.size());
  info.pStages = stages.data();
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = layout;
  info.renderPass = pass;
  VkPipeline pipeline = VK_NULL_HANDLE;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
  return pipeline;
}

}  // namespace

Pipelines::Pipelines(VkDevice device) : device_(device) {
  try {
    swapchainPass_ = createPass(device_, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    offscreenPass_ = createPass(device_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkDescriptorSetLayoutBinding setBinding{};
    setBinding.binding = 0;
    setBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setBinding.descriptorCount = 1;
    setBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &setBinding;
    check(vkCreateDescriptorSetLayout(device_, &setInfo, nullptr, &setLayout_), "vkCreateDescriptorSetLayout");

    const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float)};  // target size
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &sdfLayout_), "vkCreatePipelineLayout");
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &texturedLayout_), "vkCreatePipelineLayout");

    sdf_ = createPipeline(device_, swapchainPass_, sdfLayout_, kSdfVertSpv, kSdfFragSpv,
                          sizeof(SdfInstance) / 16);
    textured_ = createPipeline(device_, swapchainPass_, texturedLayout_, kTexturedVertSpv, kTexturedFragSpv,
                               sizeof(TexInstance) / 16);

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_), "vkCreateSampler");

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kMaxTextures;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_), "vkCreateDescriptorPool");
  } catch (...) {
    release();
    throw;
  }
}

Pipelines::~Pipelines() { release(); }

void Pipelines::release() {
  if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, pool_, nullptr);
  if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
  if (textured_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, textured_, nullptr);
  if (sdf_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, sdf_, nullptr);
  if (texturedLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, texturedLayout_, nullptr);
  if (sdfLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, sdfLayout_, nullptr);
  if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
  if (offscreenPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, offscreenPass_, nullptr);
  if (swapchainPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, swapchainPass_, nullptr);
}

VkDescriptorSet Pipelines::allocateSet(VkImageView view) {
  VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = pool_;
  alloc.descriptorSetCount = 1;
  alloc.pSetLayouts = &setLayout_;
  VkDescriptorSet set = VK_NULL_HANDLE;
  check(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
  VkDescriptorImageInfo image{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  return set;
}

void Pipelines::freeSet(VkDescriptorSet set) { vkFreeDescriptorSets(device_, pool_, 1, &set); }

}  // namespace r1ui::render::detail
