#include "stdafx.h"

#include "Headers/vkResources.h"

#include "Platform/Video/GLIM/glim.h"

namespace Divide {
std::array<VkBlendFactor, to_base(BlendProperty::COUNT)> vkBlendTable;
std::array<VkBlendOp, to_base(BlendOperation::COUNT)> vkBlendOpTable;
std::array<VkCompareOp, to_base(ComparisonFunction::COUNT)> vkCompareFuncTable;
std::array<VkStencilOp, to_base(StencilOperation::COUNT)> vkStencilOpTable;
std::array<VkCullModeFlags, to_base(CullMode::COUNT)> vkCullModeTable;
std::array<VkPolygonMode, to_base(FillMode::COUNT)> vkFillModeTable;
std::array<VkImageType, to_base(TextureType::COUNT)> vkTextureTypeTable;
std::array<VkPrimitiveTopology, to_base(PrimitiveTopology::COUNT)> vkPrimitiveTypeTable;
std::array<VkSamplerAddressMode, to_base(TextureWrap::COUNT)> vkWrapTable;
std::array<NS_GLIM::GLIM_ENUM, to_base(PrimitiveTopology::COUNT)> glimPrimitiveType;
std::array<VkShaderStageFlagBits, to_base(ShaderType::COUNT)> vkShaderStageTable;

namespace Debug {
    PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTag = VK_NULL_HANDLE;
    PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName = VK_NULL_HANDLE;
    PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBegin = VK_NULL_HANDLE;
    PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEnd = VK_NULL_HANDLE;
    PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsert = VK_NULL_HANDLE;
}

namespace VKUtil {
    void fillEnumTables(VkDevice device) {
        // The debug marker extension is not part of the core, so function pointers need to be loaded manually
        Debug::vkDebugMarkerSetObjectTag = (PFN_vkDebugMarkerSetObjectTagEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT");
        Debug::vkDebugMarkerSetObjectName = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT");
        Debug::vkCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT");
        Debug::vkCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT");
        Debug::vkCmdDebugMarkerInsert = (PFN_vkCmdDebugMarkerInsertEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT");

        vkBlendTable[to_base(BlendProperty::ZERO)] = VK_BLEND_FACTOR_ZERO;
        vkBlendTable[to_base(BlendProperty::ONE)] = VK_BLEND_FACTOR_ONE;
        vkBlendTable[to_base(BlendProperty::SRC_COLOR)] = VK_BLEND_FACTOR_SRC_COLOR;
        vkBlendTable[to_base(BlendProperty::INV_SRC_COLOR)] = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        vkBlendTable[to_base(BlendProperty::SRC_ALPHA)] = VK_BLEND_FACTOR_SRC_ALPHA;
        vkBlendTable[to_base(BlendProperty::INV_SRC_ALPHA)] = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        vkBlendTable[to_base(BlendProperty::DEST_ALPHA)] = VK_BLEND_FACTOR_DST_ALPHA;
        vkBlendTable[to_base(BlendProperty::INV_DEST_ALPHA)] = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        vkBlendTable[to_base(BlendProperty::DEST_COLOR)] = VK_BLEND_FACTOR_DST_COLOR;
        vkBlendTable[to_base(BlendProperty::INV_DEST_COLOR)] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        vkBlendTable[to_base(BlendProperty::SRC_ALPHA_SAT)] = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;

        vkBlendOpTable[to_base(BlendOperation::ADD)] = VK_BLEND_OP_ADD;
        vkBlendOpTable[to_base(BlendOperation::SUBTRACT)] = VK_BLEND_OP_SUBTRACT;
        vkBlendOpTable[to_base(BlendOperation::REV_SUBTRACT)] = VK_BLEND_OP_REVERSE_SUBTRACT;
        vkBlendOpTable[to_base(BlendOperation::MIN)] = VK_BLEND_OP_MIN;
        vkBlendOpTable[to_base(BlendOperation::MAX)] = VK_BLEND_OP_MAX;
        
        vkCompareFuncTable[to_base(ComparisonFunction::NEVER)] = VK_COMPARE_OP_NEVER;
        vkCompareFuncTable[to_base(ComparisonFunction::LESS)] = VK_COMPARE_OP_LESS;
        vkCompareFuncTable[to_base(ComparisonFunction::EQUAL)] = VK_COMPARE_OP_EQUAL;
        vkCompareFuncTable[to_base(ComparisonFunction::LEQUAL)] = VK_COMPARE_OP_LESS_OR_EQUAL;
        vkCompareFuncTable[to_base(ComparisonFunction::GREATER)] = VK_COMPARE_OP_GREATER;
        vkCompareFuncTable[to_base(ComparisonFunction::NEQUAL)] = VK_COMPARE_OP_NOT_EQUAL;
        vkCompareFuncTable[to_base(ComparisonFunction::GEQUAL)] = VK_COMPARE_OP_GREATER_OR_EQUAL;
        vkCompareFuncTable[to_base(ComparisonFunction::ALWAYS)] = VK_COMPARE_OP_ALWAYS;
        
        vkStencilOpTable[to_base(StencilOperation::KEEP)] = VK_STENCIL_OP_KEEP;
        vkStencilOpTable[to_base(StencilOperation::ZERO)] = VK_STENCIL_OP_ZERO;
        vkStencilOpTable[to_base(StencilOperation::REPLACE)] = VK_STENCIL_OP_REPLACE;
        vkStencilOpTable[to_base(StencilOperation::INCR)] = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        vkStencilOpTable[to_base(StencilOperation::DECR)] = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        vkStencilOpTable[to_base(StencilOperation::INV)] = VK_STENCIL_OP_INVERT;
        vkStencilOpTable[to_base(StencilOperation::INCR_WRAP)] = VK_STENCIL_OP_INCREMENT_AND_WRAP;
        vkStencilOpTable[to_base(StencilOperation::DECR_WRAP)] = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        
        vkCullModeTable[to_base(CullMode::BACK)] = VK_CULL_MODE_BACK_BIT;
        vkCullModeTable[to_base(CullMode::FRONT)] = VK_CULL_MODE_FRONT_BIT;
        vkCullModeTable[to_base(CullMode::ALL)] = VK_CULL_MODE_FRONT_AND_BACK;
        vkCullModeTable[to_base(CullMode::NONE)] = VK_CULL_MODE_NONE;
        
        vkFillModeTable[to_base(FillMode::POINT)] = VK_POLYGON_MODE_POINT;
        vkFillModeTable[to_base(FillMode::WIREFRAME)] = VK_POLYGON_MODE_LINE;
        vkFillModeTable[to_base(FillMode::SOLID)] = VK_POLYGON_MODE_FILL;
        
        vkTextureTypeTable[to_base(TextureType::TEXTURE_1D)] = VK_IMAGE_TYPE_1D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_2D)] = VK_IMAGE_TYPE_2D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_3D)] = VK_IMAGE_TYPE_3D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_MAP)] = VK_IMAGE_TYPE_3D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY)] = VK_IMAGE_TYPE_2D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_ARRAY)] = VK_IMAGE_TYPE_3D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_2D_MS)] = VK_IMAGE_TYPE_2D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY_MS)] = VK_IMAGE_TYPE_2D;

        vkPrimitiveTypeTable[to_base(PrimitiveTopology::POINTS)] = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::LINES)] = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::LINE_STRIP)] = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLES)] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_STRIP)] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_FAN)] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::LINES_ADJANCENCY)] = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::LINE_STRIP_ADJACENCY)] = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLES_ADJACENCY)] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY)] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::PATCH)] = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        
        vkWrapTable[to_base(TextureWrap::MIRROR_REPEAT)] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        vkWrapTable[to_base(TextureWrap::REPEAT)] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkWrapTable[to_base(TextureWrap::CLAMP_TO_EDGE)] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkWrapTable[to_base(TextureWrap::CLAMP_TO_BORDER)] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        vkWrapTable[to_base(TextureWrap::MIRROR_CLAMP_TO_EDGE)] = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        
        glimPrimitiveType[to_base(PrimitiveTopology::POINTS)] = NS_GLIM::GLIM_ENUM::GLIM_POINTS;
        glimPrimitiveType[to_base(PrimitiveTopology::LINES)] = NS_GLIM::GLIM_ENUM::GLIM_LINES;
        glimPrimitiveType[to_base(PrimitiveTopology::LINE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_LINE_STRIP;
        glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLES)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLES;
        glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_STRIP;
        glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLE_FAN)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_FAN;

        vkShaderStageTable[to_base(ShaderType::VERTEX)] = VK_SHADER_STAGE_VERTEX_BIT;
        vkShaderStageTable[to_base(ShaderType::FRAGMENT)] = VK_SHADER_STAGE_FRAGMENT_BIT;
        vkShaderStageTable[to_base(ShaderType::GEOMETRY)] = VK_SHADER_STAGE_GEOMETRY_BIT;
        vkShaderStageTable[to_base(ShaderType::TESSELLATION_CTRL)] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        vkShaderStageTable[to_base(ShaderType::TESSELLATION_EVAL)] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        vkShaderStageTable[to_base(ShaderType::COMPUTE)] = VK_SHADER_STAGE_COMPUTE_BIT;
    };
};

}; //namespace Divide