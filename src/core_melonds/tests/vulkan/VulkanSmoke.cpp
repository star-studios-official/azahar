/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "GPU3D_ComputeVulkan_shaders.h"
#include "GPU_Vulkan_shaders.h"
#include "Platform.h"
#include "VulkanSupport.h"

namespace melonDS
{

namespace Platform
{

void Log(LogLevel level, const char* format, ...)
{
    (void)level;

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

}

namespace
{

constexpr u32 GraphicsSamplePixel = 0xFF0000FF;

struct SmokeResources
{
    explicit SmokeResources(VK::Context& context) : Ctx(context) {}

    ~SmokeResources()
    {
        if (Ctx.Device == VK_NULL_HANDLE)
            return;

        VK::vkDeviceWaitIdle(Ctx.Device);

        if (GraphicsPipeline) VK::vkDestroyPipeline(Ctx.Device, GraphicsPipeline, nullptr);
        if (ComputePipeline) VK::vkDestroyPipeline(Ctx.Device, ComputePipeline, nullptr);
        if (Framebuffer) VK::vkDestroyFramebuffer(Ctx.Device, Framebuffer, nullptr);
        if (RenderPass) VK::vkDestroyRenderPass(Ctx.Device, RenderPass, nullptr);
        if (GraphicsPipelineLayout)
            VK::vkDestroyPipelineLayout(Ctx.Device, GraphicsPipelineLayout, nullptr);
        if (ComputePipelineLayout)
            VK::vkDestroyPipelineLayout(Ctx.Device, ComputePipelineLayout, nullptr);
        if (GraphicsDescriptorPool)
            VK::vkDestroyDescriptorPool(Ctx.Device, GraphicsDescriptorPool, nullptr);
        if (ComputeDescriptorPool)
            VK::vkDestroyDescriptorPool(Ctx.Device, ComputeDescriptorPool, nullptr);
        if (GraphicsSetLayout)
            VK::vkDestroyDescriptorSetLayout(Ctx.Device, GraphicsSetLayout, nullptr);
        if (ComputeSetLayout)
            VK::vkDestroyDescriptorSetLayout(Ctx.Device, ComputeSetLayout, nullptr);
        if (GraphicsSampler) VK::vkDestroySampler(Ctx.Device, GraphicsSampler, nullptr);
        if (VertexShader) VK::vkDestroyShaderModule(Ctx.Device, VertexShader, nullptr);
        if (FragmentShader) VK::vkDestroyShaderModule(Ctx.Device, FragmentShader, nullptr);
        if (ComputeShader) VK::vkDestroyShaderModule(Ctx.Device, ComputeShader, nullptr);

        Ctx.DestroyImage(GraphicsSample);
        Ctx.DestroyImage(ColorTarget);
        Ctx.DestroyBuffer(GraphicsVertices);
        Ctx.DestroyBuffer(GraphicsReadback);
        Ctx.DestroyBuffer(ComputeOutput);
        Ctx.DestroyBuffer(ComputeUniform);
    }

    VK::Context& Ctx;
    VK::Context::Buffer GraphicsVertices;
    VK::Context::Buffer GraphicsReadback;
    VK::Context::Buffer ComputeOutput;
    VK::Context::Buffer ComputeUniform;
    VK::Context::Image GraphicsSample;
    VK::Context::Image ColorTarget;
    VkShaderModule VertexShader = VK_NULL_HANDLE;
    VkShaderModule FragmentShader = VK_NULL_HANDLE;
    VkShaderModule ComputeShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout GraphicsSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ComputeSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout GraphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout ComputePipelineLayout = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkPipeline GraphicsPipeline = VK_NULL_HANDLE;
    VkPipeline ComputePipeline = VK_NULL_HANDLE;
    VkSampler GraphicsSampler = VK_NULL_HANDLE;
    VkDescriptorPool GraphicsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool ComputeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet GraphicsDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet ComputeDescriptorSet = VK_NULL_HANDLE;
};

bool Check(bool condition, const char* operation)
{
    if (condition)
        return true;

    fprintf(stderr, "Vulkan smoke test failed: %s\n", operation);
    return false;
}

bool CreateGraphicsPipeline(SmokeResources& resources)
{
    VK::Context& ctx = resources.Ctx;
    const std::string vertexSource =
        "#version 460\n" + GPUShadersVulkan::CaptureDownscaleVS;
    const std::string fragmentSource =
        "#version 460\n" + GPUShadersVulkan::CaptureDownscaleFS;

    if (!Check(ctx.CompileShader(resources.VertexShader, VK::Context::ShaderStage::Vertex,
                                 vertexSource, "SmokeCaptureDownscaleVS"),
               "compile the capture-downscale vertex shader") ||
        !Check(ctx.CompileShader(resources.FragmentShader, VK::Context::ShaderStage::Fragment,
                                 fragmentSource, "SmokeCaptureDownscaleFS"),
               "compile the capture-downscale fragment shader"))
        return false;

    VkDescriptorSetLayoutBinding binding = {
        0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo setLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    if (!Check(VK::vkCreateDescriptorSetLayout(ctx.Device, &setLayoutInfo, nullptr,
                                               &resources.GraphicsSetLayout) == VK_SUCCESS,
               "create the capture-downscale descriptor layout"))
        return false;

    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.size = sizeof(u32);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &resources.GraphicsSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    if (!Check(VK::vkCreatePipelineLayout(ctx.Device, &pipelineLayoutInfo, nullptr,
                                          &resources.GraphicsPipelineLayout) == VK_SUCCESS,
               "create the capture-downscale pipeline layout"))
        return false;

    if (!Check(ctx.CreateRenderPass(resources.RenderPass, VK_FORMAT_R8G8B8A8_UNORM, true),
               "create an offscreen render pass") ||
        !Check(ctx.CreateImage(resources.ColorTarget, VK_FORMAT_R8G8B8A8_UNORM, 4, 4, 1,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               false),
               "create an offscreen color image") ||
        !Check(ctx.CreateFramebuffer(resources.Framebuffer, resources.RenderPass,
                                     resources.ColorTarget, 0),
               "create an offscreen framebuffer"))
        return false;

    if (!Check(ctx.CreateImage(resources.GraphicsSample, VK_FORMAT_R8G8B8A8_UNORM,
                               1, 1, 1,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               true),
               "create the graphics sample image"))
        return false;
    if (!Check(ctx.UploadImageLayer(resources.GraphicsSample, &GraphicsSamplePixel,
                                    1, 1, 0, 4,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
               "upload the graphics sample image"))
        return false;

    VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!Check(VK::vkCreateSampler(ctx.Device, &samplerInfo, nullptr,
                                   &resources.GraphicsSampler) == VK_SUCCESS,
               "create the graphics sampler"))
        return false;

    const float vertices[] =
    {
        0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    if (!Check(ctx.CreateBuffer(resources.GraphicsVertices, sizeof(vertices),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true),
               "create the graphics vertex buffer"))
        return false;
    memcpy(resources.GraphicsVertices.Map, vertices, sizeof(vertices));
    if (!Check(ctx.FlushBuffer(resources.GraphicsVertices),
               "flush the graphics vertex buffer"))
        return false;
    if (!Check(ctx.CreateBuffer(resources.GraphicsReadback, 4 * 4 * sizeof(u32),
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT, true),
               "create the graphics readback buffer"))
        return false;
    memset(resources.GraphicsReadback.Map, 0, resources.GraphicsReadback.Size);
    if (!Check(ctx.FlushBuffer(resources.GraphicsReadback),
               "flush the graphics readback buffer"))
        return false;

    VkDescriptorPoolSize graphicsPoolSize = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo graphicsPoolInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    graphicsPoolInfo.maxSets = 1;
    graphicsPoolInfo.poolSizeCount = 1;
    graphicsPoolInfo.pPoolSizes = &graphicsPoolSize;
    if (!Check(VK::vkCreateDescriptorPool(ctx.Device, &graphicsPoolInfo, nullptr,
                                          &resources.GraphicsDescriptorPool) == VK_SUCCESS,
               "create the graphics descriptor pool"))
        return false;

    VkDescriptorSetAllocateInfo allocateInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = resources.GraphicsDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &resources.GraphicsSetLayout;
    if (!Check(VK::vkAllocateDescriptorSets(ctx.Device, &allocateInfo,
                                            &resources.GraphicsDescriptorSet) == VK_SUCCESS,
               "allocate the graphics descriptor set"))
        return false;

    VkDescriptorImageInfo imageInfo = {
        resources.GraphicsSampler, resources.GraphicsSample.View,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet imageWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    imageWrite.dstSet = resources.GraphicsDescriptorSet;
    imageWrite.dstBinding = 0;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    imageWrite.pImageInfo = &imageInfo;
    VK::vkUpdateDescriptorSets(ctx.Device, 1, &imageWrite, 0, nullptr);

    VK::Context::GraphicsPipelineConfig config;
    config.VertexShader = resources.VertexShader;
    config.FragmentShader = resources.FragmentShader;
    config.Layout = resources.GraphicsPipelineLayout;
    config.RenderPass = resources.RenderPass;
    config.VertexStride = 2 * sizeof(float);
    config.VertexAttributes.push_back(
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
    return Check(ctx.CreateGraphicsPipeline(resources.GraphicsPipeline, config),
                 "create a pipeline from the embedded capture-downscale shaders");
}

bool CreateComputePipeline(SmokeResources& resources)
{
    VK::Context& ctx = resources.Ctx;
    std::string source = R"(#version 460
#define ClearIndirectWorkCount
#define ScreenWidth 256
#define ScreenHeight 192
#define MaxWorkTiles 12288
#define TileSize 8
const int CoarseTileCountY = 4;
#define CoarseTileArea 32
#define ClearCoarseBinMaskLocalSize 64
)";
    source += ComputeRendererShadersVulkan::Common;
    source += ComputeRendererShadersVulkan::ClearIndirectWorkCount;

    if (!Check(ctx.CompileComputeShader(resources.ComputeShader, source,
                                        "SmokeClearIndirectWorkCount"),
               "compile an embedded 3D compute shader"))
        return false;

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo setLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = bindings;
    if (!Check(VK::vkCreateDescriptorSetLayout(ctx.Device, &setLayoutInfo, nullptr,
                                               &resources.ComputeSetLayout) == VK_SUCCESS,
               "create the compute descriptor layout"))
        return false;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &resources.ComputeSetLayout;
    if (!Check(VK::vkCreatePipelineLayout(ctx.Device, &pipelineLayoutInfo, nullptr,
                                          &resources.ComputePipelineLayout) == VK_SUCCESS,
               "create the compute pipeline layout"))
        return false;

    VkComputePipelineCreateInfo pipelineInfo = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = resources.ComputeShader;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = resources.ComputePipelineLayout;
    if (!Check(VK::vkCreateComputePipelines(ctx.Device, VK_NULL_HANDLE, 1,
                                            &pipelineInfo, nullptr,
                                            &resources.ComputePipeline) == VK_SUCCESS,
               "create a pipeline from the embedded 3D compute shader"))
        return false;

    if (!Check(ctx.CreateBuffer(resources.ComputeUniform, 1024,
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true),
               "create the compute uniform buffer") ||
        !Check(ctx.CreateBuffer(resources.ComputeOutput, 8192,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true),
               "create the compute output buffer"))
        return false;

    memset(resources.ComputeUniform.Map, 0, resources.ComputeUniform.Size);
    memset(resources.ComputeOutput.Map, 0, resources.ComputeOutput.Size);
    if (!Check(ctx.FlushBuffer(resources.ComputeUniform), "flush the compute uniform buffer") ||
        !Check(ctx.FlushBuffer(resources.ComputeOutput), "flush the compute output buffer"))
        return false;

    VkDescriptorPoolSize poolSizes[2] =
    {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (!Check(VK::vkCreateDescriptorPool(ctx.Device, &poolInfo, nullptr,
                                          &resources.ComputeDescriptorPool) == VK_SUCCESS,
               "create the compute descriptor pool"))
        return false;

    VkDescriptorSetAllocateInfo allocateInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = resources.ComputeDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &resources.ComputeSetLayout;
    if (!Check(VK::vkAllocateDescriptorSets(ctx.Device, &allocateInfo,
                                            &resources.ComputeDescriptorSet) == VK_SUCCESS,
               "allocate the compute descriptor set"))
        return false;

    VkDescriptorBufferInfo uniformInfo = {
        resources.ComputeUniform.Buf, 0, resources.ComputeUniform.Size};
    VkDescriptorBufferInfo outputInfo = {
        resources.ComputeOutput.Buf, 0, resources.ComputeOutput.Size};
    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = resources.ComputeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uniformInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = resources.ComputeDescriptorSet;
    writes[1].dstBinding = 4;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &outputInfo;
    VK::vkUpdateDescriptorSets(ctx.Device, 2, writes, 0, nullptr);
    return true;
}

bool ExecuteSmokeWork(SmokeResources& resources)
{
    VK::Context& ctx = resources.Ctx;
    VkCommandBuffer commandBuffer = ctx.BeginOneShot();
    if (!Check(commandBuffer != VK_NULL_HANDLE, "begin a command buffer"))
        return false;

    VkClearValue clearValue = {};
    clearValue.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = resources.RenderPass;
    renderPassInfo.framebuffer = resources.Framebuffer;
    renderPassInfo.renderArea.extent = {4, 4};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    VK::vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {4, 4}};
    VK::vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VK::vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    VK::vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          resources.GraphicsPipeline);
    VkDeviceSize vertexOffset = 0;
    VK::vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                               &resources.GraphicsVertices.Buf, &vertexOffset);
    VK::vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                resources.GraphicsPipelineLayout, 0, 1,
                                &resources.GraphicsDescriptorSet, 0, nullptr);
    const u32 inputLayer = 0;
    VK::vkCmdPushConstants(commandBuffer, resources.GraphicsPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(inputLayer), &inputLayer);
    VK::vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    VK::vkCmdEndRenderPass(commandBuffer);

    resources.ColorTarget.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ctx.TransitionImage(commandBuffer, resources.ColorTarget,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copyRegion = {};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {4, 4, 1};
    VK::vkCmdCopyImageToBuffer(commandBuffer, resources.ColorTarget.Img,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               resources.GraphicsReadback.Buf, 1, &copyRegion);
    VkBufferMemoryBarrier readbackBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    readbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readbackBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    readbackBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readbackBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readbackBarrier.buffer = resources.GraphicsReadback.Buf;
    readbackBarrier.offset = 0;
    readbackBarrier.size = VK_WHOLE_SIZE;
    VK::vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &readbackBarrier, 0, nullptr);

    VK::vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          resources.ComputePipeline);
    VK::vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                resources.ComputePipelineLayout, 0, 1,
                                &resources.ComputeDescriptorSet, 0, nullptr);
    VK::vkCmdDispatch(commandBuffer, 1, 1, 1);

    if (!Check(ctx.EndOneShot(commandBuffer), "submit graphics and compute work") ||
        !Check(ctx.InvalidateBuffer(resources.GraphicsReadback),
               "invalidate the graphics readback buffer") ||
        !Check(ctx.InvalidateBuffer(resources.ComputeOutput),
               "invalidate the compute output buffer"))
        return false;

    const u32 graphicsPixel = *static_cast<const u32*>(resources.GraphicsReadback.Map);
    if (graphicsPixel != GraphicsSamplePixel)
    {
        fprintf(stderr,
                "Vulkan smoke test failed: graphics result was %08X, expected %08X\n",
                graphicsPixel, GraphicsSamplePixel);
        return false;
    }

    const u32* output = static_cast<const u32*>(resources.ComputeOutput.Map);
    for (u32 invocation = 0; invocation < 32; invocation++)
    {
        const u32* workCount = &output[invocation * 4];
        if (workCount[0] != 1 || workCount[1] != 1 ||
            workCount[2] != 0 || workCount[3] != 0)
        {
            fprintf(stderr,
                    "Vulkan smoke test failed: compute result %u was {%u, %u, %u, %u}\n",
                    invocation, workCount[0], workCount[1], workCount[2], workCount[3]);
            return false;
        }
    }
    return true;
}

}

}

int main()
{
    melonDS::VK::Context context;
    if (!context.Init())
    {
        fprintf(stderr, "Vulkan smoke test failed: no usable Vulkan context\n");
        return 1;
    }

    bool success;
    {
        melonDS::SmokeResources resources(context);
        success = melonDS::CreateGraphicsPipeline(resources) &&
                  melonDS::CreateComputePipeline(resources) &&
                  melonDS::ExecuteSmokeWork(resources);
    }

    if (!success)
        return 1;

    printf("Vulkan smoke test passed on %s\n", context.Props.deviceName);
    return 0;
}
