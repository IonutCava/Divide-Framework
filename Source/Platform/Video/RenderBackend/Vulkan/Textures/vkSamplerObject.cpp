

#include "Headers/vkSamplerObject.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {
    VkSampler vkSamplerObject::Construct(const SamplerDescriptor& descriptor)
    {
        VkSamplerCustomBorderColorCreateInfoEXT customBorderColour{};
        customBorderColour.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;

        const F32 maxAnisotropy = to_F32(std::min(descriptor._anisotropyLevel, to_U8(GFXDevice::GetDeviceInformation()._maxAnisotropy)));

        VkSamplerCreateInfo samplerInfo = vk::samplerCreateInfo();
        samplerInfo.flags = 0u;
        samplerInfo.minFilter = descriptor._minFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        samplerInfo.magFilter = descriptor._magFilter == TextureFilter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        if ( descriptor._mipSampling == TextureMipSampling::NONE )
        {
            // emulate GL_LINEAR/GL_NEAREST for minification (ref: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html)
            samplerInfo.minLod = 0.f;
            samplerInfo.maxLod = 0.25f;
            samplerInfo.mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 0.f;
        }
        else
        {
            samplerInfo.minLod = descriptor._minLOD;
            samplerInfo.maxLod = descriptor._maxLOD;
            samplerInfo.mipLodBias = descriptor._biasLOD;
            samplerInfo.mipmapMode = descriptor._mipSampling == TextureMipSampling::LINEAR
                                                              ? VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                              : VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.anisotropyEnable = maxAnisotropy > 0.f;
            samplerInfo.maxAnisotropy = maxAnisotropy;
        }

        samplerInfo.addressModeU = vkWrapTable[to_base(descriptor._wrapU)];
        samplerInfo.addressModeV = vkWrapTable[to_base(descriptor._wrapV)];
        samplerInfo.addressModeW = vkWrapTable[to_base(descriptor._wrapW)];

        if (descriptor._depthCompareFunc != ComparisonFunction::COUNT )
        {
            samplerInfo.compareEnable = true;
            samplerInfo.compareOp = vkCompareFuncTable[to_base(descriptor._depthCompareFunc )];
        }
        else
        {
            samplerInfo.compareEnable = false;
        }

        switch (descriptor._borderColour)
        {
            default:
            case TextureBorderColour::TRANSPARENT_BLACK_INT: samplerInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;   break;
            case TextureBorderColour::TRANSPARENT_BLACK_F32: samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; break;
            case TextureBorderColour::OPAQUE_BLACK_INT     : samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;        break;
            case TextureBorderColour::OPAQUE_BLACK_F32     : samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;      break;
            case TextureBorderColour::OPAQUE_WHITE_INT     : samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;        break;
            case TextureBorderColour::OPAQUE_WHITE_F32     : samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;      break;
        }

        if (descriptor._customBorderColour == TextureBorderColour::CUSTOM_INT)
        {
            customBorderColour.customBorderColor.uint32[0] = to_U32(descriptor._customBorderColour.r);
            customBorderColour.customBorderColor.uint32[1] = to_U32(descriptor._customBorderColour.g);
            customBorderColour.customBorderColor.uint32[2] = to_U32(descriptor._customBorderColour.b);
            customBorderColour.customBorderColor.uint32[3] = to_U32(descriptor._customBorderColour.a);
            customBorderColour.format = VK_FORMAT_R8_UINT;
            samplerInfo.pNext = &customBorderColour;
        }
        else if ( descriptor._customBorderColour == TextureBorderColour::CUSTOM_F32 )
        {
            const FColour4 fColour = Util::ToFloatColour(descriptor._customBorderColour);
            customBorderColour.customBorderColor.float32[0] = fColour.r;
            customBorderColour.customBorderColor.float32[1] = fColour.g;
            customBorderColour.customBorderColor.float32[2] = fColour.b;
            customBorderColour.customBorderColor.float32[3] = fColour.a;
            customBorderColour.format = VK_FORMAT_R32_SFLOAT;
            samplerInfo.pNext = &customBorderColour;
        }

        //samplerInfo.unnormalizedCoordinates = false;

        VkSampler ret;
        vkCreateSampler(VK_API::GetStateTracker()._device->getVKDevice(), &samplerInfo, nullptr, &ret);

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)ret, VK_OBJECT_TYPE_SAMPLER, Util::StringFormat( "SAMPLER_%zu", GetHash(descriptor) ).c_str() );
        }
        return ret;
    }

    void vkSamplerObject::Destruct(VkSampler& handle)
    {
        if (handle != VK_NULL_HANDLE)
        {
            VK_API::RegisterCustomAPIDelete([sampler = handle](VkDevice device)
            {
                vkDestroySampler(device, sampler, nullptr);
            }, true);
            handle = VK_NULL_HANDLE;
        }
    }
} //namespace Divide