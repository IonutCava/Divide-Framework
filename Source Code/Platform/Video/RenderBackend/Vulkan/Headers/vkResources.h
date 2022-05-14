/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef VK_RESOURCES_H
#define VK_RESOURCES_H

#include "Platform/Headers/PlatformDefines.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"

#include <vk-bootstrap/src/VkBootstrap.h>

namespace NS_GLIM {
    enum class GLIM_ENUM : int;
}; //namespace NS_GLIM

namespace Divide {

namespace Debug {
    extern PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTag;
    extern PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName;
    extern PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBegin;
    extern PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEnd;
    extern PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsert;
};

//ref:  SaschaWillems / Vulkan / VulkanTools
inline std::string VKErrorString(VkResult errorCode)
{
    switch (errorCode)
    {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}
#ifndef VK_CHECK
#define VK_CHECK(x)                                                                       \
    do                                                                                    \
    {                                                                                     \
        VkResult err = x;                                                                 \
        if (err)                                                                          \
        {                                                                                 \
            Console::errorfn("Detected Vulkan error: %s\n", VKErrorString(err).c_str());  \
            DIVIDE_UNEXPECTED_CALL();                                                     \
        }                                                                                 \
    } while (0)
#endif //VK_CHECK

    extern std::array<VkBlendFactor, to_base(BlendProperty::COUNT)> vkBlendTable;
    extern std::array<VkBlendOp, to_base(BlendOperation::COUNT)> vkBlendOpTable;
    extern std::array<VkCompareOp, to_base(ComparisonFunction::COUNT)> vkCompareFuncTable;
    extern std::array<VkStencilOp, to_base(StencilOperation::COUNT)> vkStencilOpTable;
    extern std::array<VkCullModeFlags, to_base(CullMode::COUNT)> vkCullModeTable;
    extern std::array<VkPolygonMode, to_base(FillMode::COUNT)> vkFillModeTable;
    extern std::array<VkImageType, to_base(TextureType::COUNT)> vkTextureTypeTable;
    extern std::array<VkPrimitiveTopology, to_base(PrimitiveTopology::COUNT)> vkPrimitiveTypeTable;
    extern std::array<VkSamplerAddressMode, to_base(TextureWrap::COUNT)> vkWrapTable;
    extern std::array<NS_GLIM::GLIM_ENUM, to_base(PrimitiveTopology::COUNT)> glimPrimitiveType;
    extern std::array<VkShaderStageFlagBits, to_base(ShaderType::COUNT)> vkShaderStageTable;

namespace VKUtil {
    void fillEnumTables(VkDevice device);

}; //namespace VKUtil
}; //namespace Divide

#endif //VK_RESOURCES_H