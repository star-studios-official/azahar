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

#include <algorithm>
#include <array>
#include <string.h>

#include "NDS.h"
#include "GPU_Vulkan.h"
#include "GPU_Vulkan_shaders.h"
#include "Platform.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;


VulkanRenderer::VulkanRenderer(melonDS::NDS& nds)
    : Renderer(nds.GPU)
{
    AuxInputBuffer[0] = new u16[256 * 256];
    AuxInputBuffer[1] = new u16[256 * 192];
    memset(AuxInputBuffer[0], 0, 256 * 256 * sizeof(u16));
    memset(AuxInputBuffer[1], 0, 256 * 192 * sizeof(u16));
    memset(AuxInputDirty, 1, sizeof(AuxInputDirty));

    Ctx = std::make_unique<VK::Context>();
    Rend3D = std::make_unique<ComputeRenderer3D_Vulkan>(GPU.GPU3D, *Ctx);

    Rend2D_A = std::make_unique<VulkanRenderer2D>(GPU.GPU2D_A, *this, *Ctx);
    Rend2D_B = std::make_unique<VulkanRenderer2D>(GPU.GPU2D_B, *this, *Ctx);

    ScaleFactor = 0;
}

bool VulkanRenderer::Init()
{
    if (!Ctx->Init())
    {
        Log(LogLevel::Error, "Vulkan renderer: no usable Vulkan implementation\n");
        return false;
    }

    // The 3D child creates resources used by both 2D units and the parent,
    // so initialise it first now that the shared context is live.
    if (!Rend3D->Init())
        return false;

    // ---- compile the parent's own shaders ----

    std::string fpVS = "#version 460\n" + GPUShadersVulkan::FinalPassVS;
    std::string fpFS = "#version 460\n" + GPUShadersVulkan::FinalPassFS;
    std::string capVS = "#version 460\n" + GPUShadersVulkan::CaptureVS;
    std::string capFS = "#version 460\n" + GPUShadersVulkan::CaptureFS;
    std::string capDownVS = "#version 460\n" + GPUShadersVulkan::CaptureDownscaleVS;
    std::string capDownFS = "#version 460\n" + GPUShadersVulkan::CaptureDownscaleFS;

    VkShaderModule fpVSMod = VK_NULL_HANDLE, fpFSMod = VK_NULL_HANDLE;
    VkShaderModule capVSMod = VK_NULL_HANDLE, capFSMod = VK_NULL_HANDLE;
    VkShaderModule capDownVSMod = VK_NULL_HANDLE, capDownFSMod = VK_NULL_HANDLE;

    bool ok = true;
    ok = ok && Ctx->CompileShader(fpVSMod, VK::Context::ShaderStage::Vertex, fpVS, "FinalPassVS");
    ok = ok && Ctx->CompileShader(fpFSMod, VK::Context::ShaderStage::Fragment, fpFS, "FinalPassFS");
    ok = ok && Ctx->CompileShader(capVSMod, VK::Context::ShaderStage::Vertex, capVS, "CaptureVS");
    ok = ok && Ctx->CompileShader(capFSMod, VK::Context::ShaderStage::Fragment, capFS, "CaptureFS");
    ok = ok && Ctx->CompileShader(capDownVSMod, VK::Context::ShaderStage::Vertex, capDownVS, "CaptureDownscaleVS");
    ok = ok && Ctx->CompileShader(capDownFSMod, VK::Context::ShaderStage::Fragment, capDownFS, "CaptureDownscaleFS");

    if (!ok)
    {
        VkShaderModule mods[6] = {fpVSMod, fpFSMod, capVSMod, capFSMod, capDownVSMod, capDownFSMod};
        for (VkShaderModule m : mods)
            if (m) VK::vkDestroyShaderModule(Ctx->Device, m, nullptr);
        return false;
    }

    // ---- FinalPass: descriptor set layout, pipeline layout, render pass, pipeline ----
    {
        VkDescriptorSetLayoutBinding bindings[4] = {};
        auto setBinding = [&](u32 idx, VkDescriptorType type, VkShaderStageFlags stages)
        {
            bindings[idx].binding = idx;
            bindings[idx].descriptorType = type;
            bindings[idx].descriptorCount = 1;
            bindings[idx].stageFlags = stages;
        };
        setBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        setBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        setBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        setBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;
        if (VK::vkCreateDescriptorSetLayout(Ctx->Device, &layoutInfo, nullptr, &FPSetLayout) != VK_SUCCESS)
            return false;

        VkPipelineLayoutCreateInfo plInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &FPSetLayout;
        if (VK::vkCreatePipelineLayout(Ctx->Device, &plInfo, nullptr, &FPPipelineLayout) != VK_SUCCESS)
            return false;
    }

    if (!Ctx->CreateRenderPass(FPRenderPass, VK_FORMAT_R8G8B8A8_UNORM, false, 2, false))
        return false;

    {
        VK::Context::GraphicsPipelineConfig cfg = {};
        cfg.VertexShader = fpVSMod;
        cfg.FragmentShader = fpFSMod;
        cfg.Layout = FPPipelineLayout;
        cfg.RenderPass = FPRenderPass;
        cfg.VertexStride = 2 * sizeof(float);
        cfg.VertexAttributes = { {0, 0, VK_FORMAT_R32G32_SFLOAT, 0} };
        cfg.ColorAttachmentCount = 2;
        if (!Ctx->CreateGraphicsPipeline(FPPipeline, cfg))
            return false;
    }

    // ---- Capture: descriptor set layout, pipeline layout, render pass, pipeline ----
    {
        VkDescriptorSetLayoutBinding bindings[3] = {};
        auto setBinding = [&](u32 idx, VkDescriptorType type, VkShaderStageFlags stages)
        {
            bindings[idx].binding = idx;
            bindings[idx].descriptorType = type;
            bindings[idx].descriptorCount = 1;
            bindings[idx].stageFlags = stages;
        };
        setBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        setBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        setBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        if (VK::vkCreateDescriptorSetLayout(Ctx->Device, &layoutInfo, nullptr, &CaptureSetLayout) != VK_SUCCESS)
            return false;

        VkPipelineLayoutCreateInfo plInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &CaptureSetLayout;
        if (VK::vkCreatePipelineLayout(Ctx->Device, &plInfo, nullptr, &CapturePipelineLayout) != VK_SUCCESS)
            return false;
    }

    if (!Ctx->CreateRenderPass(CaptureRenderPass, VK_FORMAT_R8G8B8A8_UNORM, false, 1, false))
        return false;

    {
        VK::Context::GraphicsPipelineConfig cfg = {};
        cfg.VertexShader = capVSMod;
        cfg.FragmentShader = capFSMod;
        cfg.Layout = CapturePipelineLayout;
        cfg.RenderPass = CaptureRenderPass;
        cfg.VertexStride = 4 * sizeof(s16);
        cfg.VertexAttributes = {
            {0, 0, VK_FORMAT_R16G16_SINT, 0},
            {1, 0, VK_FORMAT_R16G16_SINT, 2 * (u32)sizeof(s16)},
        };
        cfg.ColorAttachmentCount = 1;
        if (!Ctx->CreateGraphicsPipeline(CapturePipeline, cfg))
            return false;
    }

    // ---- CaptureDownscale: descriptor set layout, pipeline layout, render pass, pipeline ----
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (VK::vkCreateDescriptorSetLayout(Ctx->Device, &layoutInfo, nullptr, &CapDownSetLayout) != VK_SUCCESS)
            return false;

        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(s32);

        VkPipelineLayoutCreateInfo plInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &CapDownSetLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pushRange;
        if (VK::vkCreatePipelineLayout(Ctx->Device, &plInfo, nullptr, &CapDownPipelineLayout) != VK_SUCCESS)
            return false;
    }

    // CaptureSync is a self-contained one-shot target each time it's used
    // (see SyncVRAMCapture), so a clearing render pass is always safely
    // reenterable regardless of the image's actual previous layout
    if (!Ctx->CreateRenderPass(CapDownRenderPass, VK_FORMAT_R8G8B8A8_UNORM, true, 1, false))
        return false;

    {
        VK::Context::GraphicsPipelineConfig cfg = {};
        cfg.VertexShader = capDownVSMod;
        cfg.FragmentShader = capDownFSMod;
        cfg.Layout = CapDownPipelineLayout;
        cfg.RenderPass = CapDownRenderPass;
        cfg.VertexStride = 2 * sizeof(float);
        cfg.VertexAttributes = { {0, 0, VK_FORMAT_R32G32_SFLOAT, 0} };
        cfg.ColorAttachmentCount = 1;
        if (!Ctx->CreateGraphicsPipeline(CapDownPipeline, cfg))
            return false;
    }

    VkShaderModule allMods[6] = {fpVSMod, fpFSMod, capVSMod, capFSMod, capDownVSMod, capDownFSMod};
    for (VkShaderModule m : allMods)
        if (m) VK::vkDestroyShaderModule(Ctx->Device, m, nullptr);

    // ---- samplers ----

    auto createSampler = [&](VkSampler& out, VkSamplerAddressMode mode) -> bool
    {
        VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = mode;
        samplerInfo.addressModeV = mode;
        samplerInfo.addressModeW = mode;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        return VK::vkCreateSampler(Ctx->Device, &samplerInfo, nullptr, &out) == VK_SUCCESS;
    };
    if (!createSampler(SamplerNearestClamp, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) return false;
    if (!createSampler(SamplerNearestRepeat, VK_SAMPLER_ADDRESS_MODE_REPEAT)) return false;
    if (!createSampler(SamplerNearestBorder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)) return false;

    // ---- vertex buffers ----

    // FinalPass fullscreen quad: position only, NDC (-1..1) directly
    // (GL: FPVertexArrayID; FinalPassVS derives fTexcoord from vPosition)
    {
        const float verts[6][2] = {
            {-1, 1}, {1, -1}, {1, 1},
            {-1, 1}, {-1, -1}, {1, -1},
        };
        if (!Ctx->CreateBuffer(FPVertexBuffer, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
            return false;
        memcpy(FPVertexBuffer.Map, verts, sizeof(verts));
        if (!Ctx->FlushBuffer(FPVertexBuffer, 0, sizeof(verts)))
            return false;
    }

    // shared fullscreen unit rect [0,1] (GL: RectVtxBuffer), used by CaptureDownscale
    {
        const float rectverts[6][2] = {
            {0, 1}, {1, 0}, {1, 1},
            {0, 1}, {0, 0}, {1, 0},
        };
        if (!Ctx->CreateBuffer(RectVtxBuffer, sizeof(rectverts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
            return false;
        memcpy(RectVtxBuffer.Map, rectverts, sizeof(rectverts));
        if (!Ctx->FlushBuffer(RectVtxBuffer, 0, sizeof(rectverts)))
            return false;
    }

    if (!Ctx->CreateBuffer(CaptureVertexRing.Buf, 256 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
        return false;
    CaptureVertexRing.Host.resize(CaptureVertexRing.Buf.Size);
    if (!Ctx->CreateBuffer(AuxStagingRing.Buf, 512 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
        return false;
    AuxStagingRing.Host.resize(AuxStagingRing.Buf.Size);

    // ---- config rings ----

    static_assert((sizeof(sFinalPassConfig) & 15) == 0);
    static_assert((sizeof(sCaptureConfig) & 15) == 0);

    if (!InitConfigRing(FPConfigRing, sizeof(sFinalPassConfig), 256))
        return false;
    if (!InitConfigRing(CaptureConfigRing, sizeof(sCaptureConfig), 256))
        return false;

    // ---- AuxInput texture (VRAM-display / mainmem DISP FIFO); fixed size ----

    if (!Ctx->CreateImage(AuxInputImg, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 256, 256, 2,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true))
        return false;
    {
        // r<->b swizzle recovers the DS channel order for this raw-uploaded
        // packed format (see VulkanRenderer2D::CreatePalView for the same trick)
        VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = AuxInputImg.Img;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = AuxInputImg.Format;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G,
                               VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A};
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
        if (VK::vkCreateImageView(Ctx->Device, &viewInfo, nullptr, &AuxInputView) != VK_SUCCESS)
            return false;
    }
    {
        VkCommandBuffer cmd = Ctx->BeginOneShot();
        if (cmd == VK_NULL_HANDLE)
            return false;
        Ctx->TransitionImage(cmd, AuxInputImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        if (!Ctx->EndOneShot(cmd))
            return false;
    }

    // ---- CaptureSync (1x IR capture readback target); fixed size ----

    if (!Ctx->CreateImage(CaptureSyncImg, VK_FORMAT_R8G8B8A8_UNORM, 256, 256, 1,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT, false))
        return false;
    if (!Ctx->CreateFramebuffer(CaptureSyncFB, CapDownRenderPass, CaptureSyncImg, 0))
        return false;
    if (!Ctx->CreateBuffer(CaptureSyncReadback, 256 * 256 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return false;

    // ---- descriptor pools ----

    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
        };
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        if (VK::vkCreateDescriptorPool(Ctx->Device, &poolInfo, nullptr, &FPDescPool) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayout layouts[2] = {FPSetLayout, FPSetLayout};
        VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = FPDescPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        if (VK::vkAllocateDescriptorSets(Ctx->Device, &allocInfo, FPDescSet) != VK_SUCCESS)
            return false;
    }

    {
        const u32 maxSets = 32;
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * maxSets},
        };
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = maxSets;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        if (VK::vkCreateDescriptorPool(Ctx->Device, &poolInfo, nullptr, &CaptureDescPool) != VK_SUCCESS)
            return false;
    }

    {
        VkDescriptorPoolSize sizes[] = { {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2} };
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = sizes;
        if (VK::vkCreateDescriptorPool(Ctx->Device, &poolInfo, nullptr, &CapDownDescPool) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayout layouts[2] = {CapDownSetLayout, CapDownSetLayout};
        VkDescriptorSet sets[2] = {};
        VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = CapDownDescPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        if (VK::vkAllocateDescriptorSets(Ctx->Device, &allocInfo, sets) != VK_SUCCESS)
            return false;
        CapDown128Set = sets[0];
        CapDown256Set = sets[1];
    }

    // ---- command buffer + fence (own submission, separate from Rend3D's) ----

    {
        VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = Ctx->CmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 2;
        if (VK::vkAllocateCommandBuffers(Ctx->Device, &allocInfo, FrameCmd) != VK_SUCCESS)
            return false;

        VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        for (int i = 0; i < 2; i++)
            if (VK::vkCreateFence(Ctx->Device, &fenceInfo, nullptr, &FrameFence[i]) != VK_SUCCESS)
                return false;
    }

    // ---- GL interop presentation textures (mirrors GLRenderer::FPOutputTex) ----

    glGenTextures(2, FPOutputTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    // ---- 2D units ----

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    if (!rend2dA->InitShaders()) return false;
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    if (!rend2dB->InitShaders(*rend2dA)) return false;

    if (!Rend2D_A->Init()) return false;
    if (!Rend2D_B->Init()) return false;

    return true;
}

VulkanRenderer::~VulkanRenderer()
{
    if (Ctx && Ctx->Valid)
        VK::vkDeviceWaitIdle(Ctx->Device);

    // The children borrow Ctx. Destroy B before A because it shares A's
    // shader resources, then destroy 3D before releasing the context.
    Rend2D_B.reset();
    Rend2D_A.reset();

    if (Ctx && Ctx->Valid)
    {
        DestroyScaleDependentResources();

        for (int i = 0; i < 2; i++)
            if (FrameFence[i]) VK::vkDestroyFence(Ctx->Device, FrameFence[i], nullptr);
        if (FrameCmd[0]) VK::vkFreeCommandBuffers(Ctx->Device, Ctx->CmdPool, 2, FrameCmd);

        if (FPDescPool) VK::vkDestroyDescriptorPool(Ctx->Device, FPDescPool, nullptr);
        if (CaptureDescPool) VK::vkDestroyDescriptorPool(Ctx->Device, CaptureDescPool, nullptr);
        if (CapDownDescPool) VK::vkDestroyDescriptorPool(Ctx->Device, CapDownDescPool, nullptr);

        if (FPPipeline) VK::vkDestroyPipeline(Ctx->Device, FPPipeline, nullptr);
        if (CapturePipeline) VK::vkDestroyPipeline(Ctx->Device, CapturePipeline, nullptr);
        if (CapDownPipeline) VK::vkDestroyPipeline(Ctx->Device, CapDownPipeline, nullptr);

        if (FPRenderPass) VK::vkDestroyRenderPass(Ctx->Device, FPRenderPass, nullptr);
        if (CaptureRenderPass) VK::vkDestroyRenderPass(Ctx->Device, CaptureRenderPass, nullptr);
        if (CapDownRenderPass) VK::vkDestroyRenderPass(Ctx->Device, CapDownRenderPass, nullptr);

        if (FPPipelineLayout) VK::vkDestroyPipelineLayout(Ctx->Device, FPPipelineLayout, nullptr);
        if (CapturePipelineLayout) VK::vkDestroyPipelineLayout(Ctx->Device, CapturePipelineLayout, nullptr);
        if (CapDownPipelineLayout) VK::vkDestroyPipelineLayout(Ctx->Device, CapDownPipelineLayout, nullptr);

        if (FPSetLayout) VK::vkDestroyDescriptorSetLayout(Ctx->Device, FPSetLayout, nullptr);
        if (CaptureSetLayout) VK::vkDestroyDescriptorSetLayout(Ctx->Device, CaptureSetLayout, nullptr);
        if (CapDownSetLayout) VK::vkDestroyDescriptorSetLayout(Ctx->Device, CapDownSetLayout, nullptr);

        if (SamplerNearestClamp) VK::vkDestroySampler(Ctx->Device, SamplerNearestClamp, nullptr);
        if (SamplerNearestRepeat) VK::vkDestroySampler(Ctx->Device, SamplerNearestRepeat, nullptr);
        if (SamplerNearestBorder) VK::vkDestroySampler(Ctx->Device, SamplerNearestBorder, nullptr);

        if (AuxInputView) VK::vkDestroyImageView(Ctx->Device, AuxInputView, nullptr);
        Ctx->DestroyImage(AuxInputImg);

        if (CaptureSyncFB) VK::vkDestroyFramebuffer(Ctx->Device, CaptureSyncFB, nullptr);
        Ctx->DestroyImage(CaptureSyncImg);
        Ctx->DestroyBuffer(CaptureSyncReadback);

        Ctx->DestroyBuffer(FPVertexBuffer);
        Ctx->DestroyBuffer(RectVtxBuffer);
        Ctx->DestroyBuffer(CaptureVertexRing.Buf);
        Ctx->DestroyBuffer(AuxStagingRing.Buf);
        Ctx->DestroyBuffer(FPConfigRing.Buf);
        Ctx->DestroyBuffer(CaptureConfigRing.Buf);
    }

    for (GLuint tex : FPOutputTex)
        if (tex) glDeleteTextures(1, &tex);

    delete[] AuxInputBuffer[0];
    delete[] AuxInputBuffer[1];

    Rend3D.reset();
    Ctx.reset();
}

void VulkanRenderer::DestroyScaleDependentResources()
{
    // The 3D static descriptor set may still reference the parent's capture
    // views. Point it back at its dummy images before those views are
    // destroyed, including on a failed high-resolution allocation followed
    // by a 1x retry.
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D_Vulkan*>(Rend3D.get()))
        rend3d->SetCaptureImages(VK_NULL_HANDLE, VK_NULL_HANDLE);

    if (FPFramebuffer) { VK::vkDestroyFramebuffer(Ctx->Device, FPFramebuffer, nullptr); FPFramebuffer = VK_NULL_HANDLE; }
    for (int i = 0; i < 2; i++)
    {
        if (FPOutputView[i]) { VK::vkDestroyImageView(Ctx->Device, FPOutputView[i], nullptr); FPOutputView[i] = VK_NULL_HANDLE; }
    }
    Ctx->DestroyImage(FPOutputImg);
    Ctx->DestroyBuffer(FPReadbackBuffer[0]);
    Ctx->DestroyBuffer(FPReadbackBuffer[1]);

    for (int i = 0; i < 4; i++)
    {
        if (CaptureOutput256FB[i]) { VK::vkDestroyFramebuffer(Ctx->Device, CaptureOutput256FB[i], nullptr); CaptureOutput256FB[i] = VK_NULL_HANDLE; }
        if (CaptureOutput256View[i]) { VK::vkDestroyImageView(Ctx->Device, CaptureOutput256View[i], nullptr); CaptureOutput256View[i] = VK_NULL_HANDLE; }
    }
    for (int i = 0; i < 16; i++)
    {
        if (CaptureOutput128FB[i]) { VK::vkDestroyFramebuffer(Ctx->Device, CaptureOutput128FB[i], nullptr); CaptureOutput128FB[i] = VK_NULL_HANDLE; }
        if (CaptureOutput128View[i]) { VK::vkDestroyImageView(Ctx->Device, CaptureOutput128View[i], nullptr); CaptureOutput128View[i] = VK_NULL_HANDLE; }
    }
    Ctx->DestroyImage(CaptureOutput256Img);
    Ctx->DestroyImage(CaptureOutput128Img);
    Ctx->DestroyImage(CaptureVRAMImg);
}

void VulkanRenderer::Reset()
{
    memset(&FinalPassConfig, 0, sizeof(FinalPassConfig));
    memset(&CaptureConfig, 0, sizeof(CaptureConfig));
    memset(CaptureLineValid, 0, sizeof(CaptureLineValid));

    AuxUsageMask = 0;
    memset(AuxInputBuffer[0], 0, 256 * 256 * sizeof(u16));
    memset(AuxInputBuffer[1], 0, 256 * 192 * sizeof(u16));
    memset(AuxInputDirty, 1, sizeof(AuxInputDirty));
    FrameReady = false;
    FrameDirty = false;

    DispCntA = 0;
    DispCntB = 0;
    MasterBrightnessA = 0;
    MasterBrightnessB = 0;
    CaptureCnt = 0;

    NeedPartialRender = false;
    LastLine = 0;
    LastCapLine = 0;
    Aux0VRAMCap = -1;

    if (FrameStarted)
    {
        // Nothing has been submitted yet; discard the actual active slot
        // before FrameSlot is reset below.
        VkResult result = VK::vkResetCommandBuffer(FrameCmd[FrameSlot], 0);
        if (result != VK_SUCCESS)
        {
            Log(LogLevel::Error, "GPU_Vulkan: reset command buffer failed (%d)\n", result);
            RenderResourcesValid = false;
            return;
        }
        FrameStarted = false;
        CurCmd = VK_NULL_HANDLE;
    }

    // drain any pipelined-but-unwaited frames before discarding state
    for (int i = 0; i < 2; i++)
        if (!ReclaimFrameSlot(i))
            return;
    HavePrevFrame = false;
    FrameSlot = 0;

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
}

void VulkanRenderer::Stop()
{
    // TODO clear buffers
    // TODO: do we even need this anymore?
}

void VulkanRenderer::PostSavestate()
{
    Reset();

    auto* rend2D = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    rend2D->PostSavestate();
    rend2D = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2D->PostSavestate();
}


void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    // NOTE: order deviates from GLRenderer::SetRenderSettings (which
    // resizes the parent first, then the 2D units, then the 3D renderer
    // last). Here, both 2D units' SetSharedResources() needs Rend3D's
    // freshly-resized output view, and this renderer's own SetScaleFactor()
    // needs both 2D units' freshly-resized output views to (re)build the
    // FinalPass/Capture descriptor sets -- so 3D must resize first, then
    // the 2D units, then this renderer last.
    EnableDither = settings.Dither;

    auto resetFrameState = [&]() -> bool
    {
        // A settings update can arrive after VCOUNT 262 has opened the next
        // frame's command buffer. Release every command-buffer reference
        // before any child destroys a scale-dependent image or buffer.
        VkResult result = VK::vkDeviceWaitIdle(Ctx->Device);
        if (result != VK_SUCCESS)
        {
            Log(LogLevel::Error, "GPU_Vulkan: device wait before resize failed (%d)\n", result);
            RenderResourcesValid = false;
            return false;
        }
        for (int i = 0; i < 2; i++)
        {
            result = VK::vkResetCommandBuffer(FrameCmd[i], 0);
            if (result != VK_SUCCESS)
            {
                Log(LogLevel::Error, "GPU_Vulkan: command buffer reset before resize failed (%d)\n", result);
                RenderResourcesValid = false;
                return false;
            }
            result = VK::vkResetFences(Ctx->Device, 1, &FrameFence[i]);
            if (result != VK_SUCCESS)
            {
                Log(LogLevel::Error, "GPU_Vulkan: fence reset before resize failed (%d)\n", result);
                RenderResourcesValid = false;
                return false;
            }
            SlotPending[i] = false;
        }
        FrameStarted = false;
        FrameReady = false;
        FrameDirty = true;
        HavePrevFrame = false;
        FrameSlot = 0;
        CurCmd = VK_NULL_HANDLE;
        return true;
    };

    if (settings.ScaleFactor != ScaleFactor)
    {
        GPU.SyncRendererCaptureState();
        if (!resetFrameState())
            return;
    }

    auto* rend3d = dynamic_cast<ComputeRenderer3D_Vulkan*>(Rend3D.get());
    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());

    auto applyScale = [&](int scale) -> bool
    {
        rend3d->SetTextureFilter(settings.TexFilter);
        if (!rend3d->SetRenderSettings(scale, settings.HiresCoordinates))
            return false;
        if (!rend2dA->SetScaleFactor(scale))
            return false;
        if (!rend2dB->SetScaleFactor(scale))
            return false;
        return SetScaleFactor(scale);
    };

    const int requestedScale = settings.ScaleFactor;
    RenderResourcesValid = applyScale(requestedScale);
    if (!RenderResourcesValid && requestedScale != 1 && Ctx->Valid)
    {
        Log(LogLevel::Warn,
            "GPU_Vulkan: falling back from %dx to 1x after allocation failure\n",
            requestedScale);
        if (resetFrameState())
        {
            RenderResourcesValid = applyScale(1);
            if (RenderResourcesValid)
                settings.ScaleFactor = 1;
        }
    }
}

bool VulkanRenderer::SetScaleFactor(int scale)
{
    if (scale == ScaleFactor && FPOutputImg.Img != VK_NULL_HANDLE)
        return true;

    DestroyScaleDependentResources();

    ScaleFactor = scale;
    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    auto fail = [&](const char* resource)
    {
        Log(LogLevel::Error,
            "GPU_Vulkan: failed to allocate scale-dependent %s at %dx\n",
            resource, scale);
        DestroyScaleDependentResources();
        ScaleFactor = -1;
        ScreenW = 0;
        ScreenH = 0;
        return false;
    };

    // ---- FinalPass MRT output: layer0=top, layer1=bottom ----

    if (!Ctx->CreateImage(FPOutputImg, VK_FORMAT_R8G8B8A8_UNORM, ScreenW, ScreenH, 2,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, true))
        return fail("FinalPass output");
    if (!Ctx->CreateLayerView(FPOutputView[0], FPOutputImg, 0) ||
        !Ctx->CreateLayerView(FPOutputView[1], FPOutputImg, 1))
        return fail("FinalPass layer views");
    if (!Ctx->CreateFramebufferMulti(FPFramebuffer, FPRenderPass,
                                     {FPOutputView[0], FPOutputView[1]},
                                     (u32)ScreenW, (u32)ScreenH))
        return fail("FinalPass framebuffer");

    for (int i = 0; i < 2; i++)
        if (!Ctx->CreateBuffer(FPReadbackBuffer[i], (VkDeviceSize)ScreenW * ScreenH * 2 * 4,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
            return fail("FinalPass readback buffer");
    HavePrevFrame = false;

    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, ScreenW, ScreenH, 2, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // ---- Capture destination images ----

    if (!Ctx->CreateImage(CaptureOutput256Img, VK_FORMAT_R8G8B8A8_UNORM, 256 * scale, 256 * scale, 4,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT, true))
        return fail("256-wide capture output");
    for (int i = 0; i < 4; i++)
    {
        if (!Ctx->CreateLayerView(CaptureOutput256View[i], CaptureOutput256Img, i) ||
            !Ctx->CreateFramebufferMulti(CaptureOutput256FB[i], CaptureRenderPass,
                                         {CaptureOutput256View[i]}, 256 * scale, 256 * scale))
            return fail("256-wide capture framebuffer");
    }

    if (!Ctx->CreateImage(CaptureOutput128Img, VK_FORMAT_R8G8B8A8_UNORM, 128 * scale, 128 * scale, 16,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT, true))
        return fail("128-wide capture output");
    for (int i = 0; i < 16; i++)
    {
        if (!Ctx->CreateLayerView(CaptureOutput128View[i], CaptureOutput128Img, i) ||
            !Ctx->CreateFramebufferMulti(CaptureOutput128FB[i], CaptureRenderPass,
                                         {CaptureOutput128View[i]}, 128 * scale, 128 * scale))
            return fail("128-wide capture framebuffer");
    }

    if (!Ctx->CreateImage(CaptureVRAMImg, VK_FORMAT_R8G8B8A8_UNORM, 256 * scale, 256 * scale, 1,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true))
        return fail("capture feedback image");

    // ---- initial layouts ----
    {
        VkCommandBuffer cmd = Ctx->BeginOneShot();
        if (cmd == VK_NULL_HANDLE)
            return fail("layout command buffer");

        Ctx->TransitionImage(cmd, FPOutputImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        Ctx->TransitionImage(cmd, CaptureOutput256Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        Ctx->TransitionImage(cmd, CaptureOutput128Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        Ctx->TransitionImage(cmd, CaptureVRAMImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

        if (!Ctx->EndOneShot(cmd))
            return fail("layout submission");
    }

    // ---- push the freshly (re)created views to the 2D units, and refresh
    // this renderer's own descriptor sets that reference them ----

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    auto* rend3dVk = dynamic_cast<ComputeRenderer3D_Vulkan*>(Rend3D.get());

    VulkanRenderer2D::SharedResources shared;
    shared.OutputTex3D = rend3dVk->GetOutputImage().View;
    shared.Capture128 = CaptureOutput128Img.View;
    shared.Capture256 = CaptureOutput256Img.View;
    rend2dA->SetSharedResources(shared);
    rend2dB->SetSharedResources(shared);

    // also feed the capture output to the 3D rasteriser, so polygons that
    // use a display capture as their texture sample the real thing (closes
    // the capture feedback loop, previously a transparent dummy in Vulkan)
    rend3dVk->SetCaptureImages(CaptureOutput128Img.View, CaptureOutput256Img.View);

    {
        VkDescriptorBufferInfo bufInfo = {FPConfigRing.Buf.Buf, 0, sizeof(sFinalPassConfig)};
        VkDescriptorImageInfo mainInputs[2] = {
            {SamplerNearestClamp, rend2dA->GetOutput().View,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {SamplerNearestClamp, rend2dB->GetOutput().View,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        VkDescriptorImageInfo auxInputs[2] = {
            {SamplerNearestRepeat, AuxInputView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {SamplerNearestRepeat, CaptureOutput256Img.View,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };

        for (u32 variant = 0; variant < 2; variant++)
        {
            VkWriteDescriptorSet writes[4] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = FPDescSet[variant];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[0].pBufferInfo = &bufInfo;
            for (u32 i = 0; i < 2; i++)
            {
                writes[1 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1 + i].dstSet = FPDescSet[variant];
                writes[1 + i].dstBinding = 1 + i;
                writes[1 + i].descriptorCount = 1;
                writes[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1 + i].pImageInfo = &mainInputs[i];
            }
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = FPDescSet[variant];
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].pImageInfo = &auxInputs[variant];
            VK::vkUpdateDescriptorSets(Ctx->Device, 4, writes, 0, nullptr);
        }
    }

    {
        VkDescriptorImageInfo imgInfo128 = {SamplerNearestRepeat, CaptureOutput128Img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo imgInfo256 = {SamplerNearestRepeat, CaptureOutput256Img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = CapDown128Set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &imgInfo128;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = CapDown256Set;
        writes[1].dstBinding = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imgInfo256;

        VK::vkUpdateDescriptorSets(Ctx->Device, 2, writes, 0, nullptr);
    }

    InvalidateCaptureDescCache();
    return true;
}


// ---- per-frame command buffer plumbing --------------------------------

bool VulkanRenderer::ReclaimFrameSlot(int slot)
{
    if (!SlotPending[slot])
        return true;

    VkResult result = VK::vkWaitForFences(
        Ctx->Device, 1, &FrameFence[slot], VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: frame fence wait failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }
    result = VK::vkResetFences(Ctx->Device, 1, &FrameFence[slot]);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: frame fence reset failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }

    SlotPending[slot] = false;
    return true;
}

bool VulkanRenderer::EnsureFrameStarted()
{
    if (FrameStarted)
        return true;
    if (!Ctx->Valid || !RenderResourcesValid)
        return false;

    int s = FrameSlot;

    // reclaim this slot if a frame from 2 frames ago is still marked pending
    // (normally already waited at that frame's present; this is the safety net)
    if (!ReclaimFrameSlot(s))
        return false;

    VkResult result = VK::vkResetCommandBuffer(FrameCmd[s], 0);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: command buffer reset failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    if (!rend2dA->BeginFrame(s) || !rend2dB->BeginFrame(s))
    {
        Log(LogLevel::Error, "GPU_Vulkan: failed to reclaim 2D descriptor arena\n");
        RenderResourcesValid = false;
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = VK::vkBeginCommandBuffer(FrameCmd[s], &beginInfo);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: command buffer begin failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }

    CurCmd = FrameCmd[s];
    FrameStarted = true;
    return true;
}

bool VulkanRenderer::SubmitAndWaitFrame()
{
    if (!FrameStarted)
        return true;

    if (!PrepareMappedBuffersForSubmit())
        return false;

    int s = FrameSlot;
    VkResult result = VK::vkEndCommandBuffer(FrameCmd[s]);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: command buffer end failed (%d)\n", result);
        RenderResourcesValid = false;
        FrameStarted = false;
        CurCmd = VK_NULL_HANDLE;
        return false;
    }

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &FrameCmd[s];
    result = VK::vkQueueSubmit(Ctx->Queue, 1, &submitInfo, FrameFence[s]);
    FrameStarted = false;
    CurCmd = VK_NULL_HANDLE;
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: frame submit failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }

    SlotPending[s] = true;
    if (!ReclaimFrameSlot(s))
        return false;

    return true;
}

// Submit without blocking; the fence is reclaimed at the next VBlank present
// (or the safety net in EnsureFrameStarted) so CPU frame N+1 overlaps GPU N.
bool VulkanRenderer::SubmitFramePipelined()
{
    if (!FrameStarted)
        return true;

    if (!PrepareMappedBuffersForSubmit())
        return false;

    int s = FrameSlot;
    VkResult result = VK::vkEndCommandBuffer(FrameCmd[s]);
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: command buffer end failed (%d)\n", result);
        RenderResourcesValid = false;
        FrameStarted = false;
        CurCmd = VK_NULL_HANDLE;
        return false;
    }

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &FrameCmd[s];
    result = VK::vkQueueSubmit(Ctx->Queue, 1, &submitInfo, FrameFence[s]);
    FrameStarted = false;
    CurCmd = VK_NULL_HANDLE;
    if (result != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: frame submit failed (%d)\n", result);
        RenderResourcesValid = false;
        return false;
    }

    SlotPending[s] = true;
    return true;
}

bool VulkanRenderer::PrepareMappedBuffersForSubmit()
{
    // Command buffers are double-buffered, but the mapped upload/config
    // buffers are shared. Let the CPU build the next frame in private mirrors,
    // then publish those mirrors only after the previous GPU consumer exits.
    const int prev = FrameSlot ^ 1;
    if (!ReclaimFrameSlot(prev))
        return false;

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    if (!rend2dA->FlushMappedBuffers() ||
        !rend2dB->FlushMappedBuffers() ||
        !FlushMappedBuffers())
    {
        Log(LogLevel::Error, "GPU_Vulkan: failed to publish mapped buffers\n");
        RenderResourcesValid = false;
        return false;
    }
    return true;
}


// ---- Vulkan plumbing helpers -------------------------------------------

u32 VulkanRenderer::RingAlloc(sRingBuffer& ring, u32 size)
{
    size = (size + 255) & ~255u;
    if (size > ring.Buf.Size || ring.Offset > ring.Buf.Size - size)
    {
        if (!ring.Overflowed)
        {
            Log(LogLevel::Error,
                "GPU_Vulkan: transient buffer exhausted (used=%u, request=%u, capacity=%llu)\n",
                ring.Offset, size, (unsigned long long)ring.Buf.Size);
            ring.Overflowed = true;
        }
        return ~0u;
    }

    u32 offset = ring.Offset;
    ring.Offset += size;
    return offset;
}

bool VulkanRenderer::InitConfigRing(sConfigRing& ring, u32 size, u32 slots)
{
    u32 uboAlign = (u32)Ctx->Props.limits.minUniformBufferOffsetAlignment;
    if (uboAlign < 16) uboAlign = 16;

    ring.Stride = (size + uboAlign - 1) & ~(uboAlign - 1);
    ring.Slots = slots;
    ring.Next = 0;
    ring.CurOffset = 0;
    if (!Ctx->CreateBuffer(ring.Buf, (VkDeviceSize)ring.Stride * slots,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
        return false;
    ring.Host.resize(ring.Buf.Size);
    memset(ring.Host.data(), 0, ring.Host.size());
    memset(ring.Buf.Map, 0, ring.Buf.Size);
    return true;
}

bool VulkanRenderer::PushConfig(sConfigRing& ring, const void* data, u32 size)
{
    if (ring.Next >= ring.Slots)
    {
        if (!ring.Overflowed)
        {
            Log(LogLevel::Error,
                "GPU_Vulkan: per-band config buffer exhausted (%u slots)\n",
                ring.Slots);
            ring.Overflowed = true;
        }
        return false;
    }

    ring.CurOffset = ring.Next * ring.Stride;
    memcpy(ring.Host.data() + ring.CurOffset, data, size);
    ring.Next++;
    return true;
}

bool VulkanRenderer::FlushMappedBuffers()
{
    bool success = true;
    auto flushRing = [&](sRingBuffer& ring)
    {
        if (ring.Offset)
        {
            memcpy(ring.Buf.Map, ring.Host.data(), ring.Offset);
            success &= Ctx->FlushBuffer(ring.Buf, 0, ring.Offset);
        }
        ring.Offset = 0;
        ring.Overflowed = false;
    };
    auto flushConfig = [&](sConfigRing& ring)
    {
        memcpy(ring.Buf.Map, ring.Host.data(), ring.Host.size());
        success &= Ctx->FlushBuffer(ring.Buf, 0, ring.Host.size());
        ring.Next = 0;
        ring.CurOffset = 0;
        ring.Overflowed = false;
    };

    flushRing(AuxStagingRing);
    flushRing(CaptureVertexRing);
    flushConfig(FPConfigRing);
    flushConfig(CaptureConfigRing);
    return success;
}

void VulkanRenderer::BeginColorTarget(VK::Context::Image& img)
{
    Ctx->TransitionImage(CurCmd, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void VulkanRenderer::EndColorTarget(VK::Context::Image& img)
{
    Ctx->TransitionImage(CurCmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
}

void VulkanRenderer::BeginTexUpload(VK::Context::Image& img)
{
    Ctx->TransitionImage(CurCmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

void VulkanRenderer::EndTexUpload(VK::Context::Image& img)
{
    Ctx->TransitionImage(CurCmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

bool VulkanRenderer::UploadTexRows(VK::Context::Image& img, const void* data,
                                   u32 rowStart, u32 rowCount, u32 bytesPerRow, u32 layer)
{
    u32 size = rowCount * bytesPerRow;
    u32 offset = RingAlloc(AuxStagingRing, size);
    if (offset == ~0u)
        return false;
    memcpy(AuxStagingRing.Host.data() + offset, data, size);

    VkBufferImageCopy region = {};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
    region.imageOffset = {0, (s32)rowStart, 0};
    region.imageExtent = {img.Width, rowCount, 1};
    VK::vkCmdCopyBufferToImage(CurCmd, AuxStagingRing.Buf.Buf, img.Img,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    return true;
}

void VulkanRenderer::InvalidateCaptureDescCache()
{
    if (CaptureDescPool != VK_NULL_HANDLE)
        VK::vkResetDescriptorPool(Ctx->Device, CaptureDescPool, 0);
    CaptureDescCache.clear();
}

VkDescriptorSet VulkanRenderer::GetCaptureDescriptorSet(VkImageView viewA, VkImageView viewB)
{
    std::array<uintptr_t, 2> key = {(uintptr_t)viewA, (uintptr_t)viewB};
    auto it = CaptureDescCache.find(key);
    if (it != CaptureDescCache.end())
        return it->second;

    VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = CaptureDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &CaptureSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VK::vkAllocateDescriptorSets(Ctx->Device, &allocInfo, &set) != VK_SUCCESS)
    {
        Log(LogLevel::Error, "GPU_Vulkan: capture descriptor pool exhausted\n");
        return VK_NULL_HANDLE;
    }

    VkDescriptorBufferInfo bufInfo = {CaptureConfigRing.Buf.Buf, 0, sizeof(sCaptureConfig)};
    VkDescriptorImageInfo imgInfoA = {SamplerNearestBorder, viewA, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo imgInfoB = {SamplerNearestRepeat, viewB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &bufInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imgInfoA;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &imgInfoB;

    VK::vkUpdateDescriptorSets(Ctx->Device, 3, writes, 0, nullptr);

    CaptureDescCache.emplace(key, set);
    return set;
}


// ---- mirrors of GLRenderer's per-frame driver logic --------------------

void VulkanRenderer::DrawScanline(u32 line)
{
    if (!Ctx->Valid || !RenderResourcesValid)
        return;

    FrameDirty = true;

    if (!EnsureFrameStarted())
        return;

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2dA->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dB->SetCommandBuffer(FrameCmd[FrameSlot]);

    u32 dispcnt_a_diff = DispCntA ^ GPU.GPU2D_A.DispCnt;
    u32 dispcnt_b_diff = DispCntB ^ GPU.GPU2D_B.DispCnt;
    u32 capturecnt_diff = CaptureCnt ^ GPU.CaptureCnt;

    bool need_render = false;
    bool need_capture = false;

    if (dispcnt_a_diff & 0xF0000)
        need_render = true;
    else if (dispcnt_b_diff & 0x10000)
        need_render = true;
    else if (MasterBrightnessA != GPU.MasterBrightnessA ||
             MasterBrightnessB != GPU.MasterBrightnessB)
        need_render = true;

    if (GPU.CaptureEnable && (capturecnt_diff & 0x7FFFFFFF))
    {
        need_render = true;
        need_capture = true;
    }

    NeedPartialRender = need_render;
    rend2dA->SetNeedPartialRender(NeedPartialRender);
    rend2dB->SetNeedPartialRender(NeedPartialRender);
    rend2dA->DrawScanline(line);
    rend2dB->DrawScanline(line);

    if (need_render && (line > 0))
    {
        RenderScreen(LastLine, line);
        LastLine = line;
    }

    if (need_capture && (line > 0))
    {
        DoCapture(LastCapLine, line);
        LastCapLine = line;
    }

    DispCntA = GPU.GPU2D_A.DispCnt;
    DispCntB = GPU.GPU2D_B.DispCnt;
    MasterBrightnessA = GPU.MasterBrightnessA;
    MasterBrightnessB = GPU.MasterBrightnessB;
    CaptureCnt = GPU.CaptureCnt;

    FinalPassConfig.uScreenSwap[line] = GPU.ScreenSwap;
    FinalPassConfig.uVCount[line] = GPU.VCount;
    CaptureConfig.uVCount[line] = GPU.VCount;
    CaptureLineValid[line] = true;

    u32 dispcnt = GPU.GPU2D_A.DispCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 capcnt = GPU.CaptureCnt;
    u32 capsel = (capcnt >> 29) & 0x3;
    u32 capA = (capcnt >> 24) & 0x1;
    u32 capB = (capcnt >> 25) & 0x1;
    bool checkcap = GPU.CaptureEnable && (capsel != 0);

    if (GPU.CaptureEnable && (capsel != 1))
    {
        if (capA == 0)
            CaptureConfig.uSrcAOffset[line] = 0;
        else
        {
            int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
            xpos -= ((xpos & 0x100) << 1);
            CaptureConfig.uSrcAOffset[line] = (float)xpos / 256.f;
        }
    }

    if ((dispmode == 2) || (checkcap && (capB == 0)))
    {
        AuxUsageMask |= (1<<0);

        u32 vrambank = (dispcnt >> 18) & 0x3;
        u32 vramoffset = GPU.VCount * 256;
        u32 outoffset = line * 256;
        if (dispmode != 2)
        {
            u32 yoff = ((capcnt >> 26) & 0x3) << 14;
            vramoffset += yoff;
            outoffset += yoff;
        }

        vramoffset &= 0xFFFF;
        outoffset &= 0xFFFF;

        u16* adst = &AuxInputBuffer[0][outoffset];

        if (GPU.VRAMMap_LCDC & (1<<vrambank))
        {
            u16* vram = (u16*)GPU.VRAM[vrambank];

            for (int i = 0; i < 256; i++)
            {
                adst[i] = vram[vramoffset];
                vramoffset++;
            }
        }
        else
        {
            for (int i = 0; i < 256; i++)
                adst[i] = 0;
        }

        AuxInputDirty[0][outoffset >> 8] = true;
    }

    if ((dispmode == 3) || (checkcap && (capB == 1)))
    {
        AuxUsageMask |= (1<<1);

        u16* adst = &AuxInputBuffer[1][line * 256];
        for (int i = 0; i < 256; i++)
            adst[i] = GPU.DispFIFOBuffer[i];
        AuxInputDirty[1][line] = true;
    }
}

void VulkanRenderer::DrawSprites(u32 line)
{
    if (!Ctx->Valid || !RenderResourcesValid)
        return;

    // this can run standalone (VCount 262 pre-renders the next frame's
    // sprite line 0 after this frame's VBlank() has already submitted and
    // ended the command buffer), so a fresh one may need to be opened here
    if (!EnsureFrameStarted())
        return;

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2dA->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dB->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dA->DrawSprites(line);
    rend2dB->DrawSprites(line);
}


void VulkanRenderer::RenderScreen(int ystart, int yend)
{
    if (ystart >= yend)
        return;

    if (!EnsureFrameStarted())
        return;

    int vramcap = -1;
    if (AuxUsageMask & (1<<0))
    {
        u32 vrambank = (DispCntA >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<vrambank))
        {
            u32 vramoffset = vrambank << 17;
            if (((DispCntA >> 16) & 0x3) != 2)
                vramoffset |= ((CaptureCnt >> 26) & 0x3) << 15;
            vramcap = GPU.GetCaptureBlock_LCDC(vramoffset);
        }
    }
    Aux0VRAMCap = vramcap;

    // Transfers and pipeline barriers are not valid inside a render pass.
    // Publish only auxiliary rows changed since the preceding band.
    FlushAuxInput(vramcap);

    // Each band uses LOAD to preserve rows written by earlier bands. The
    // layout stays unchanged, but those color writes still need an explicit
    // memory dependency before the next render pass loads the attachment.
    Ctx->TransitionImage(CurCmd, FPOutputImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderPassBeginInfo rpInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = FPRenderPass;
    rpInfo.framebuffer = FPFramebuffer;
    rpInfo.renderArea = {{0, 0}, {(u32)ScreenW, (u32)ScreenH}};
    VK::vkCmdBeginRenderPass(CurCmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0.f, 0.f, (float)ScreenW, (float)ScreenH, 0.f, 1.f};
    VK::vkCmdSetViewport(CurCmd, 0, 1, &viewport);
    VkRect2D scissor = {{0, ystart * ScaleFactor}, {(u32)ScreenW, (u32)((yend - ystart) * ScaleFactor)}};
    VK::vkCmdSetScissor(CurCmd, 0, 1, &scissor);

    if (!GPU.ScreensEnabled)
    {
        VkClearAttachment clearAtt[2] = {};
        for (int i = 0; i < 2; i++)
        {
            clearAtt[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearAtt[i].colorAttachment = i;
            clearAtt[i].clearValue.color = {{0.f, 0.f, 0.f, 1.f}};
        }
        VkClearRect rect = {};
        rect.rect = scissor;
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        VK::vkCmdClearAttachments(CurCmd, 2, clearAtt, 1, &rect);
    }
    else
    {
        FinalPassConfig.uScaleFactor = ScaleFactor;
        FinalPassConfig.uDither = EnableDither ? 1 : 0;
        FinalPassConfig.uDispModeA = (DispCntA >> 16) & 0x3;
        FinalPassConfig.uDispModeB = (DispCntB >> 16) & 0x1;
        FinalPassConfig.uBrightModeA = (MasterBrightnessA >> 14) & 0x3;
        FinalPassConfig.uBrightModeB = (MasterBrightnessB >> 14) & 0x3;
        FinalPassConfig.uBrightFactorA = std::min(MasterBrightnessA & 0x1F, 16);
        FinalPassConfig.uBrightFactorB = std::min(MasterBrightnessB & 0x1F, 16);
        FinalPassConfig.uAuxUseVCount = 0;

        u32 modeA = (DispCntA >> 16) & 0x3;
        VkDescriptorSet fpDescSet = FPDescSet[0];
        if ((modeA == 2) && (vramcap != -1))
        {
            FinalPassConfig.uAuxLayer = vramcap >> 2;
            FinalPassConfig.uAuxColorFactor = 63.75f;
            FinalPassConfig.uAuxUseVCount = 1;
            fpDescSet = FPDescSet[1];
        }
        else if (modeA >= 2)
        {
            FinalPassConfig.uAuxLayer = (modeA - 2);
            FinalPassConfig.uAuxColorFactor = 62.f;
        }

        if (!PushConfig(FPConfigRing, &FinalPassConfig, sizeof(FinalPassConfig)))
        {
            // Never leave CurCmd inside a render pass on a recoverable ring
            // allocation failure. The renderer will fall back at the
            // frontend boundary after this command buffer is discarded.
            VK::vkCmdEndRenderPass(CurCmd);
            RenderResourcesValid = false;
            return;
        }

        VK::vkCmdBindPipeline(CurCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FPPipeline);
        u32 dynOffset = FPConfigRing.CurOffset;
        VK::vkCmdBindDescriptorSets(CurCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FPPipelineLayout,
                                    0, 1, &fpDescSet, 1, &dynOffset);

        VkDeviceSize bindOffset = 0;
        VK::vkCmdBindVertexBuffers(CurCmd, 0, 1, &FPVertexBuffer.Buf, &bindOffset);
        VK::vkCmdDraw(CurCmd, 2 * 3, 1, 0, 0);
    }

    VK::vkCmdEndRenderPass(CurCmd);
}

void VulkanRenderer::FlushAuxInput(int vramcap)
{
    bool uploading = false;

    for (int layer = 0; layer < 2; layer++)
    {
        if (!(AuxUsageMask & (1 << layer)))
            continue;
        if (layer == 0 && vramcap != -1)
            continue;

        const int rows = layer == 0 ? 256 : 192;
        for (int start = 0; start < rows;)
        {
            while (start < rows && !AuxInputDirty[layer][start])
                start++;
            if (start >= rows)
                break;

            int end = start + 1;
            while (end < rows && AuxInputDirty[layer][end])
                end++;

            if (!uploading)
            {
                BeginTexUpload(AuxInputImg);
                uploading = true;
            }

            if (UploadTexRows(AuxInputImg, &AuxInputBuffer[layer][start * 256],
                              start, end - start, 256 * sizeof(u16), layer))
            {
                memset(&AuxInputDirty[layer][start], 0,
                       (end - start) * sizeof(AuxInputDirty[layer][0]));
            }
            start = end;
        }
    }

    if (uploading)
        EndTexUpload(AuxInputImg);
}

void VulkanRenderer::VBlank(u32 endLine)
{
    if (!Ctx->Valid || !RenderResourcesValid || !FrameDirty)
        return;

    endLine = std::min(endLine, 192u);
    if (!EnsureFrameStarted())
        return;

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2dA->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dB->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dA->Flush(endLine);
    rend2dB->Flush(endLine);

    if (LastLine < (int)endLine)
        RenderScreen(LastLine, endLine);

    if (GPU.CaptureEnable && LastCapLine < (int)endLine)
        DoCapture(LastCapLine, endLine);

    LastLine = endLine;
    if (GPU.CaptureEnable)
        LastCapLine = endLine;
    FrameDirty = false;
}

void VulkanRenderer::FinishFrame(u32 endLine)
{
    if (!Ctx->Valid || !RenderResourcesValid || FrameReady)
        return;

    endLine = std::min(endLine, 192u);
    if (!EnsureFrameStarted())
        return;

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    auto* rend2dB = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2dA->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dB->SetCommandBuffer(FrameCmd[FrameSlot]);
    rend2dA->FinishFrame(endLine);
    rend2dB->FinishFrame(endLine);

    if (FrameDirty)
    {
        if (LastLine < (int)endLine)
            RenderScreen(LastLine, endLine);
        if (GPU.CaptureEnable && LastCapLine < (int)endLine)
            DoCapture(LastCapLine, endLine);
    }

    LastLine = 0;
    LastCapLine = 0;
    FrameDirty = false;

    // read back the finished FinalPass output and hand it to the GL
    // compositor as the BACK buffer (mirrors GLRenderer::RenderScreen
    // writing into FPOutputFB[BackBuffer]; the front/back flip happens in
    // Renderer::SwapBuffers(), called by GPU.cpp at frame end)
    int backbuf = BackBuffer;

    // copy this frame's FinalPass output into this slot's readback buffer
    int cur = FrameSlot;
    if (FPReadbackBuffer[cur].Buf != VK_NULL_HANDLE)
    {
        Ctx->TransitionImage(FrameCmd[cur], FPOutputImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkBufferImageCopy region = {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
        region.imageExtent = {(u32)ScreenW, (u32)ScreenH, 1};
        VK::vkCmdCopyImageToBuffer(FrameCmd[cur], FPOutputImg.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   FPReadbackBuffer[cur].Buf, 1, &region);

        VkMemoryBarrier hostBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        VK::vkCmdPipelineBarrier(FrameCmd[cur], VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

        Ctx->TransitionImage(FrameCmd[cur], FPOutputImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }

    auto uploadToGL = [&](VK::Context::Buffer& rb)
    {
        if (!rb.Map || !Ctx->InvalidateBuffer(rb))
            return false;

        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[backbuf]);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, ScreenW, ScreenH, 2,
                        GL_RGBA, GL_UNSIGNED_BYTE, rb.Map);
        return true;
    };

    if (!HavePrevFrame)
    {
        // first frame: no completed frame to show yet, so present this one
        // synchronously to avoid flashing an uninitialised back buffer
        if (!SubmitAndWaitFrame())
            return;
        FrameReady = uploadToGL(FPReadbackBuffer[cur]);
        HavePrevFrame = FrameReady;
    }
    else
    {
        // steady state: hand this frame to the GPU without blocking, then
        // present the PREVIOUS slot (finished during this frame's emulation)
        // with 1 frame of latency. Waiting it here is where CPU/GPU overlap.
        if (!SubmitFramePipelined())
            return;
        int prev = cur ^ 1;
        if (!ReclaimFrameSlot(prev))
            return;
        FrameReady = uploadToGL(FPReadbackBuffer[prev]);
    }

    FrameSlot ^= 1;
}

void VulkanRenderer::VBlankEnd()
{
    AuxUsageMask = 0;
    memset(CaptureLineValid, 0, sizeof(CaptureLineValid));
    FrameDirty = true;
}

void VulkanRenderer::SwapBuffers()
{
    if (!FrameReady)
        return;

    Renderer::SwapBuffers();
    FrameReady = false;
}


void VulkanRenderer::DoCapture(int ystart, int yend)
{
    u32 dispcnt = DispCntA;
    u32 capcnt = CaptureCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 srcA = (capcnt >> 24) & 0x1;
    u32 srcB = (capcnt >> 25) & 0x1;
    u32 srcBblock = (dispcnt >> 18) & 0x3;
    u32 srcBoffset = (dispmode == 2) ? 0 : ((capcnt >> 26) & 0x3);
    u32 dstblock = (capcnt >> 16) & 0x3;
    u32 dstoffset = (capcnt >> 18) & 0x3;
    u32 capsize = (capcnt >> 20) & 0x3;
    u32 dstmode = (capcnt >> 29) & 0x3;
    u32 eva = std::min(capcnt & 0x1F, 16u);
    u32 evb = std::min((capcnt >> 8) & 0x1F, 16u);

    // determine the region we're going to capture to

    int dstwidth, dstheight;

    if (capsize == 0)
    {
        dstwidth = 128;
        dstheight = 128;
    }
    else
    {
        dstwidth = 256;
        dstheight = 64 * capsize;
    }

    if (!EnsureFrameStarted())
        return;

    FlushAuxInput(Aux0VRAMCap);

    auto* rend3dVk = dynamic_cast<ComputeRenderer3D_Vulkan*>(Rend3D.get());

    auto* rend2dA = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    VkImageView viewA = srcA ? rend3dVk->GetOutputImage().View : rend2dA->GetOutput().View;

    bool useSrcB = (dstmode == 1) || (dstmode >= 2 && evb > 0);

    VkImageView viewB = AuxInputView;
    u32 layerB = srcB;
    CaptureConfig.uSrcBColorFactor = 248.f;

    const bool useTrackedSrcB = useSrcB && !srcB && (Aux0VRAMCap != -1);
    if (useTrackedSrcB && dstblock == srcBblock && yend - ystart > 1)
    {
        // Same-bank capture is read-before-write for each scanline. Split the
        // band so every call snapshots the bank after the previous line's
        // write, preserving repeated or reversed VCOUNT dependencies.
        for (int line = ystart; line < yend; line++)
            if (CaptureLineValid[line])
                DoCapture(line, line + 1);
        return;
    }
    if (useTrackedSrcB)
    {
        // hi-res VRAM
        if (capsize != 0)
        {
            // CaptureOutput256Img contains all four banks. Rendering any one
            // bank transitions the whole image to attachment layout, so a
            // different bank cannot remain bound as shader-read input. A
            // full scratch snapshot also handles non-linear VCOUNT rows and
            // same-bank read-before-write feedback without mixed layouts.
            if (dstblock == srcBblock && dstoffset != srcBoffset)
                Log(LogLevel::Error, "GPU_Vulkan: MISMATCHED VRAM OFFSETS ON SAME BANK!!! bank=%d src=%d dst=%d\n",
                    dstblock, srcBoffset, dstoffset);

            Ctx->TransitionImage(CurCmd, CaptureOutput256Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            Ctx->TransitionImage(CurCmd, CaptureVRAMImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

            bool copiedRows[256] = {};
            std::vector<VkImageCopy> regions;
            regions.reserve(yend - ystart);
            for (int line = ystart; line < yend; line++)
            {
                if (!CaptureLineValid[line])
                    continue;
                const u32 vcount = CaptureConfig.uVCount[line];
                if (vcount >= (u32)dstheight)
                    continue;

                const int sourceRow = (srcBoffset * 64 + vcount) & 0xFF;
                if (copiedRows[sourceRow])
                    continue;
                copiedRows[sourceRow] = true;

                const int sourceY = sourceRow * ScaleFactor;
                VkImageCopy region = {};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, srcBblock, 1};
                region.srcOffset = {0, sourceY, 0};
                region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.dstOffset = {0, sourceY, 0};
                region.extent = {(u32)(256 * ScaleFactor), (u32)ScaleFactor, 1};
                regions.push_back(region);
            }

            if (!regions.empty())
                VK::vkCmdCopyImage(CurCmd, CaptureOutput256Img.Img,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   CaptureVRAMImg.Img,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   (u32)regions.size(), regions.data());

            Ctx->TransitionImage(CurCmd, CaptureOutput256Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
            Ctx->TransitionImage(CurCmd, CaptureVRAMImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

            viewB = CaptureVRAMImg.View;
            layerB = 0;
        }
        else
        {
            // A 128-wide capture writes a separate image, so the tracked
            // 256-wide source can stay bound directly.
            viewB = CaptureOutput256Img.View;
            layerB = srcBblock;
        }

        CaptureConfig.uSrcBColorFactor = 255.f;
    }

    VkDescriptorSet set = GetCaptureDescriptorSet(viewA, viewB);
    if (set == VK_NULL_HANDLE)
        return;

    VK::Context::Image& dstImg = (capsize == 0) ? CaptureOutput128Img : CaptureOutput256Img;
    VkFramebuffer dstFB;
    u32 fbSize;
    if (capsize == 0)
    {
        dstFB = CaptureOutput128FB[(dstblock << 2) | dstoffset];
        fbSize = (u32)(128 * ScaleFactor);
    }
    else
    {
        dstFB = CaptureOutput256FB[dstblock];
        fbSize = (u32)(256 * ScaleFactor);
    }

    CaptureConfig.uInvCaptureSize[0] = 1.f / (float)dstwidth;
    CaptureConfig.uInvCaptureSize[1] = 1.f / (float)dstheight;

    CaptureConfig.uSrcALayer = srcA;

    if (srcB == 0)
        CaptureConfig.uSrcBOffset = 64 * srcBoffset;
    else
        CaptureConfig.uSrcBOffset = 0;

    CaptureConfig.uSrcBLayer = layerB;
    CaptureConfig.uSrcBUseVCount = useTrackedSrcB;

    CaptureConfig.uDstMode = dstmode;
    CaptureConfig.uBlendFactors[0] = eva;
    CaptureConfig.uBlendFactors[1] = evb;

    if (!PushConfig(CaptureConfigRing, &CaptureConfig, sizeof(CaptureConfig)))
        return;

    s16 vtxbuf[192 * 6 * 4];
    s16* vptr = vtxbuf;
    int numvtx = 0;

    // VCOUNT can be rewritten during active display. Capture writes use
    // the emulated row, while 2D/FIFO inputs remain scheduled snapshots;
    // the fragment shader remaps only direct-3D and tracked VRAM inputs.
    for (int line = ystart; line < yend; line++)
    {
        if (!CaptureLineValid[line])
            continue;

        const int vcount = CaptureConfig.uVCount[line];
        if (vcount >= dstheight)
            continue;

        const int y0 = capsize == 0 ? vcount : ((dstoffset * 64 + vcount) & 0xFF);
        const int y1 = y0 + 1;
        const int t0 = line;
        const int t1 = line + 1;

        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y1; *vptr++ = dstwidth;  *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y0; *vptr++ = 0;         *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        numvtx += 6;
    }

    if (numvtx == 0)
        return;

    u32 vtxbytes = (u32)numvtx * 4 * sizeof(s16);
    u32 vtxoffset = RingAlloc(CaptureVertexRing, vtxbytes);
    if (vtxoffset == ~0u)
        return;
    memcpy(CaptureVertexRing.Host.data() + vtxoffset, vtxbuf, vtxbytes);

    BeginColorTarget(dstImg);

    VkRenderPassBeginInfo rpInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = CaptureRenderPass;
    rpInfo.framebuffer = dstFB;
    rpInfo.renderArea = {{0, 0}, {fbSize, fbSize}};
    VK::vkCmdBeginRenderPass(CurCmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0.f, 0.f, (float)fbSize, (float)fbSize, 0.f, 1.f};
    VK::vkCmdSetViewport(CurCmd, 0, 1, &viewport);
    VkRect2D scissor = {{0, 0}, {fbSize, fbSize}};
    VK::vkCmdSetScissor(CurCmd, 0, 1, &scissor);

    VK::vkCmdBindPipeline(CurCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CapturePipeline);
    u32 dynOffset = CaptureConfigRing.CurOffset;
    VK::vkCmdBindDescriptorSets(CurCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CapturePipelineLayout,
                               0, 1, &set, 1, &dynOffset);

    VkDeviceSize bindOffset = vtxoffset;
    VK::vkCmdBindVertexBuffers(CurCmd, 0, 1, &CaptureVertexRing.Buf.Buf, &bindOffset);
    VK::vkCmdDraw(CurCmd, numvtx, 1, 0, 0);

    VK::vkCmdEndRenderPass(CurCmd);
    EndColorTarget(dstImg);

    // A 32-KiB VRAM block has both 128x128 and 256x64 interpretations.
    // Mirror the rows written above so either representation can be sampled
    // by later display capture or 3D texture reads.
    std::vector<VkImageCopy> mirrorRegions;
    if (capsize == 0)
    {
        bool mirroredRows[128] = {};
        for (int line = ystart; line < yend; line++)
        {
            if (!CaptureLineValid[line])
                continue;
            const u32 row128 = CaptureConfig.uVCount[line];
            if (row128 >= 128 || mirroredRows[row128])
                continue;
            mirroredRows[row128] = true;

            const u32 row256 = dstoffset * 64 + (row128 >> 1);
            const u32 half = row128 & 1;
            VkImageCopy region = {};
            region.srcSubresource = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, (dstblock << 2) | dstoffset, 1};
            region.srcOffset = {0, (s32)(row128 * ScaleFactor), 0};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dstblock, 1};
            region.dstOffset = {(s32)(half * 128 * ScaleFactor),
                                (s32)(row256 * ScaleFactor), 0};
            region.extent = {(u32)(128 * ScaleFactor), (u32)ScaleFactor, 1};
            mirrorRegions.push_back(region);
        }

        Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        if (!mirrorRegions.empty())
            VK::vkCmdCopyImage(CurCmd,
                CaptureOutput128Img.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                CaptureOutput256Img.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                (u32)mirrorRegions.size(), mirrorRegions.data());
        Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
    }
    else
    {
        bool mirroredRows[256] = {};
        for (int line = ystart; line < yend; line++)
        {
            if (!CaptureLineValid[line])
                continue;
            const u32 vcount = CaptureConfig.uVCount[line];
            if (vcount >= (u32)dstheight)
                continue;
            const u32 row256 = (dstoffset * 64 + vcount) & 0xFF;
            if (mirroredRows[row256])
                continue;
            mirroredRows[row256] = true;

            const u32 block = row256 >> 6;
            const u32 compactRow = (row256 & 0x3F) * 2;
            for (u32 half = 0; half < 2; half++)
            {
                VkImageCopy region = {};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dstblock, 1};
                region.srcOffset = {(s32)(half * 128 * ScaleFactor),
                                    (s32)(row256 * ScaleFactor), 0};
                region.dstSubresource = {
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, (dstblock << 2) | block, 1};
                region.dstOffset = {0,
                                    (s32)((compactRow + half) * ScaleFactor), 0};
                region.extent = {
                    (u32)(128 * ScaleFactor), (u32)ScaleFactor, 1};
                mirrorRegions.push_back(region);
            }
        }

        Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        if (!mirrorRegions.empty())
            VK::vkCmdCopyImage(CurCmd,
                CaptureOutput256Img.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                CaptureOutput128Img.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                (u32)mirrorRegions.size(), mirrorRegions.data());
        Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
    }
}


void VulkanRenderer::AllocCapture(u32 bank, u32 start, u32 len,
                                  bool preserveContents)
{
    auto* rend2D = dynamic_cast<VulkanRenderer2D*>(Rend2D_A.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;
    rend2D = dynamic_cast<VulkanRenderer2D*>(Rend2D_B.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;

    if (preserveContents)
        return;
    if (!EnsureFrameStarted())
        return;

    struct SeedBlock
    {
        u32 Block;
        u32 StagingOffset;
    };
    SeedBlock seeds[3];
    const u32 blockCount = len == 0 ? 1 : len;
    u32 pixels[128 * 128];
    for (u32 i = 0; i < blockCount; i++)
    {
        seeds[i].Block = (start + i) & 0x3;
        seeds[i].StagingOffset = RingAlloc(AuxStagingRing, sizeof(pixels));
        if (seeds[i].StagingOffset == ~0u)
        {
            RenderResourcesValid = false;
            return;
        }

        const u16* source = reinterpret_cast<const u16*>(
            &GPU.VRAM[bank][seeds[i].Block * 0x8000]);
        for (u32 pixel = 0; pixel < 128 * 128; pixel++)
        {
            const u16 color = source[pixel];
            pixels[pixel] = ((color & 0x001F) << 3) |
                            ((color & 0x03E0) << 6) |
                            ((color & 0x7C00) << 9) |
                            ((color & 0x8000) ? 0xFF000000 : 0);
        }
        memcpy(AuxStagingRing.Host.data() + seeds[i].StagingOffset,
               pixels, sizeof(pixels));
    }

    const bool seedUndefined = CaptureSyncImg.Layout == VK_IMAGE_LAYOUT_UNDEFINED;
    Ctx->TransitionImage(CurCmd, CaptureSyncImg,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        seedUndefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        seedUndefined ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    for (u32 i = 0; i < blockCount; i++)
    {
        VkBufferImageCopy upload = {};
        upload.bufferOffset = seeds[i].StagingOffset;
        upload.bufferRowLength = 256;
        upload.bufferImageHeight = 64;
        upload.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        upload.imageExtent = {256, 64, 1};
        VK::vkCmdCopyBufferToImage(CurCmd, AuxStagingRing.Buf.Buf,
            CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);

        Ctx->TransitionImage(CurCmd, CaptureSyncImg,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkImageBlit blit = {};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {256, 64, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, bank, 1};
        blit.dstOffsets[0] = {0, (s32)(seeds[i].Block * 64 * ScaleFactor), 0};
        blit.dstOffsets[1] = {256 * ScaleFactor,
                              (s32)((seeds[i].Block + 1) * 64 * ScaleFactor), 1};
        VK::vkCmdBlitImage(CurCmd,
            CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            CaptureOutput256Img.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_NEAREST);

        Ctx->TransitionImage(CurCmd, CaptureSyncImg,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        upload.bufferRowLength = 128;
        upload.bufferImageHeight = 128;
        upload.imageExtent = {128, 128, 1};
        VK::vkCmdCopyBufferToImage(CurCmd, AuxStagingRing.Buf.Buf,
            CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);

        Ctx->TransitionImage(CurCmd, CaptureSyncImg,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        blit.srcOffsets[1] = {128, 128, 1};
        blit.dstSubresource.baseArrayLayer = (bank << 2) | seeds[i].Block;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {128 * ScaleFactor, 128 * ScaleFactor, 1};
        VK::vkCmdBlitImage(CurCmd,
            CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            CaptureOutput128Img.Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_NEAREST);

        if (i + 1 < blockCount)
            Ctx->TransitionImage(CurCmd, CaptureSyncImg,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    Ctx->TransitionImage(CurCmd, CaptureSyncImg,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    Ctx->TransitionImage(CurCmd, CaptureOutput256Img,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    Ctx->TransitionImage(CurCmd, CaptureOutput128Img,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
}

void VulkanRenderer::DownscaleCapture(VkCommandBuffer cmd, int width, int height, int layer)
{
    // downscale a hi-res capture buffer to 1x IR, ready for CPU readback;
    // like the GL renderer, this uses a shader pass (not a blit) so color
    // components get accurately averaged rather than point-sampled
    VkRenderPassBeginInfo rpInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = CapDownRenderPass;
    rpInfo.framebuffer = CaptureSyncFB;
    rpInfo.renderArea = {{0, 0}, {256, 256}};
    VK::vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0.f, 0.f, (float)width, (float)height, 0.f, 1.f};
    VK::vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = {{0, 0}, {(u32)width, (u32)height}};
    VK::vkCmdSetScissor(cmd, 0, 1, &scissor);

    VK::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CapDownPipeline);
    VkDescriptorSet set = (width == 128) ? CapDown128Set : CapDown256Set;
    VK::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CapDownPipelineLayout,
                               0, 1, &set, 0, nullptr);

    s32 layerVal = layer;
    VK::vkCmdPushConstants(cmd, CapDownPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(s32), &layerVal);

    VkDeviceSize bindOffset = 0;
    VK::vkCmdBindVertexBuffers(cmd, 0, 1, &RectVtxBuffer.Buf, &bindOffset);
    VK::vkCmdDraw(cmd, 2 * 3, 1, 0, 0);

    VK::vkCmdEndRenderPass(cmd);

    // the render pass's finalLayout is COLOR_ATTACHMENT_OPTIMAL; correct
    // our bookkeeping to match before the manual transition below (mirrors
    // the OBJDepthImg.Layout correction in VulkanRenderer2D::SetScaleFactor)
    CaptureSyncImg.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    Ctx->TransitionImage(cmd, CaptureSyncImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
}

void VulkanRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete)
{
    if (!Ctx->Valid || !RenderResourcesValid)
        return;

    if (!complete)
        Log(LogLevel::Error, "GPU_Vulkan: !!! READING VRAM AS IT IS BEING CAPTURED TO\n");

    u8* vram = GPU.VRAM[bank];

    // this can run mid-frame: SyncVRAMCaptureBlock is called from GPU.cpp
    // while frame emulation is in progress, not just at frame boundaries.
    // Flush whatever capture work is already recorded so the downscale
    // below observes it, mirroring the implicit GPU sync glReadPixels
    // performs in GLRenderer::SyncVRAMCapture. The next DrawScanline/
    // DrawSprites call will lazily reopen a fresh command buffer for the
    // remainder of the frame.
    if (FrameStarted && !SubmitAndWaitFrame())
        return;

    VkCommandBuffer cmd = Ctx->BeginOneShot();
    if (cmd == VK_NULL_HANDLE)
        return;
    CurCmd = cmd;

    if (len == 0) // 128x128
    {
        DownscaleCapture(cmd, 128, 128, (int)((bank << 2) | start));

        VkBufferImageCopy region = {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {128, 128, 1};
        VK::vkCmdCopyImageToBuffer(cmd, CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   CaptureSyncReadback.Buf, 1, &region);
    }
    else
    {
        DownscaleCapture(cmd, 256, 256, (int)bank);

        u32 pos = start;
        for (u32 i = 0; i < len;)
        {
            u32 end = pos + (len - i);
            if (end > 4)
                end = 4;

            VkBufferImageCopy region = {};
            region.bufferOffset = (VkDeviceSize)(pos * 64) * 256 * 4;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {0, (s32)(pos * 64), 0};
            region.imageExtent = {256, (end - pos) * 64, 1};
            VK::vkCmdCopyImageToBuffer(cmd, CaptureSyncImg.Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       CaptureSyncReadback.Buf, 1, &region);

            i += (end - pos);
            pos += (end - pos);
            pos &= 3;
        }
    }

    VkMemoryBarrier hostBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VK::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    // return CaptureSyncImg to a rest layout; the next DownscaleCapture
    // call always uses a clearing render pass so any layout is fine here,
    // but keeping the bookkeeping consistent costs nothing
    Ctx->TransitionImage(cmd, CaptureSyncImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    if (!Ctx->EndOneShot(cmd)) // submits + fence-waits
    {
        CurCmd = VK_NULL_HANDLE;
        return;
    }

    CurCmd = VK_NULL_HANDLE;

    if (!Ctx->InvalidateBuffer(CaptureSyncReadback))
        return;

    // unpack RGBA8 (already snapped to 5-bit granularity by the downscale
    // shader: oColor.rgb = (col.rgb>>3)/31, oColor.a = col.a>0?1:0) into the
    // DS's native RGBA5551 VRAM representation, matching what
    // glReadPixels(..., GL_UNSIGNED_SHORT_1_5_5_5_REV, ...) did for GL:
    // bit15=A, bits14-10=B, bits9-5=G, bits4-0=R
    auto unpackRegion = [&](u32 bufByteOffset, u32 rowCount, u32 strideWidth, u32 vramByteOffset)
    {
        const u8* src = (const u8*)CaptureSyncReadback.Map + bufByteOffset;
        u16* dst = (u16*)&vram[vramByteOffset];
        u32 count = rowCount * strideWidth;
        for (u32 i = 0; i < count; i++)
        {
            u8 r = src[i*4+0], g = src[i*4+1], b = src[i*4+2], a = src[i*4+3];
            dst[i] = (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (a ? (1 << 15) : 0));
        }
    };

    if (len == 0)
    {
        unpackRegion(0, 128, 128, start * 64 * 512);

        for (u32 j = start * 64; j < (start+1) * 64; j++)
            GPU.VRAMDirty[bank][j] = true;
    }
    else
    {
        u32 pos = start;
        for (u32 i = 0; i < len;)
        {
            u32 end = pos + (len - i);
            if (end > 4)
                end = 4;

            unpackRegion(pos * 64 * 256 * 4, (end - pos) * 64, 256, pos * 64 * 512);

            for (u32 j = pos * 64; j < end * 64; j++)
                GPU.VRAMDirty[bank][j] = true;

            i += (end - pos);
            pos += (end - pos);
            pos &= 3;
        }
    }
}


bool VulkanRenderer::GetFramebuffers(void** top, void** bottom)
{
    // since we use an array texture, we only need one of the pointer fields
    int frontbuf = BackBuffer ^ 1;
    *top = &FPOutputTex[frontbuf];
    *bottom = nullptr;
    return false;
}

void VulkanRenderer::Start3DRendering()
{
    if (RenderResourcesValid)
    {
        // VCOUNT writes can reach the emulated line-215 render event before
        // physical line 192 submits the current display work. The 3D output
        // is single-buffered, so make all earlier sampling complete before
        // the 3D renderer overwrites it. Later scanlines reopen the parent
        // command buffer lazily.
        if (FrameStarted && !SubmitAndWaitFrame())
            return;

        Rend3D->RenderFrame();
        auto* rend3d = dynamic_cast<ComputeRenderer3D_Vulkan*>(Rend3D.get());
        RenderResourcesValid = rend3d->IsRenderValid();
    }
}

void VulkanRenderer::Finish3DRendering()
{
    if (RenderResourcesValid)
        Rend3D->FinishRendering();
}

void VulkanRenderer::Restart3DRendering()
{
    if (RenderResourcesValid)
        Rend3D->RestartFrame();
}


bool VulkanRenderer::NeedsShaderCompile()
{
    return Rend3D->NeedsShaderCompile();
}

bool VulkanRenderer::ShaderCompileFailed() const
{
    return Rend3D->ShaderCompileFailed();
}

void VulkanRenderer::ShaderCompileStep(int& current, int& count)
{
    return Rend3D->ShaderCompileStep(current, count);
}

}
