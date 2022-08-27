#include "stdafx.h"

#include "Headers/vkResources.h"

#include "Platform/Video/GLIM/glim.h"
#include "Platform/Video/Headers/DescriptorSets.h"

namespace Divide {
std::array<VkBlendFactor, to_base(BlendProperty::COUNT)> vkBlendTable;
std::array<VkBlendOp, to_base(BlendOperation::COUNT)> vkBlendOpTable;
std::array<VkCompareOp, to_base(ComparisonFunction::COUNT)> vkCompareFuncTable;
std::array<VkStencilOp, to_base(StencilOperation::COUNT)> vkStencilOpTable;
std::array<VkCullModeFlags, to_base(CullMode::COUNT)> vkCullModeTable;
std::array<VkPolygonMode, to_base(FillMode::COUNT)> vkFillModeTable;
std::array<VkImageType, to_base(TextureType::COUNT)> vkTextureTypeTable;
std::array<VkImageViewType, to_base(TextureType::COUNT)> vkTextureViewTypeTable;
std::array<VkPrimitiveTopology, to_base(PrimitiveTopology::COUNT)> vkPrimitiveTypeTable;
std::array<VkSamplerAddressMode, to_base(TextureWrap::COUNT)> vkWrapTable;
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
        vkTextureTypeTable[to_base(TextureType::TEXTURE_1D_ARRAY)] = VK_IMAGE_TYPE_1D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY)] = VK_IMAGE_TYPE_2D;
        vkTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_ARRAY)] = VK_IMAGE_TYPE_3D;

        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_1D)] = VK_IMAGE_VIEW_TYPE_1D;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_2D)] = VK_IMAGE_VIEW_TYPE_2D;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_3D)] = VK_IMAGE_VIEW_TYPE_3D;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_CUBE_MAP)] = VK_IMAGE_VIEW_TYPE_CUBE;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_1D_ARRAY)] = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY)] = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vkTextureViewTypeTable[to_base(TextureType::TEXTURE_CUBE_ARRAY)] = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

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
        vkPrimitiveTypeTable[to_base(PrimitiveTopology::COMPUTE)] = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;

        vkWrapTable[to_base(TextureWrap::MIRROR_REPEAT)] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        vkWrapTable[to_base(TextureWrap::REPEAT)] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkWrapTable[to_base(TextureWrap::CLAMP_TO_EDGE)] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkWrapTable[to_base(TextureWrap::CLAMP_TO_BORDER)] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        vkWrapTable[to_base(TextureWrap::MIRROR_CLAMP_TO_EDGE)] = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;

        vkShaderStageTable[to_base(ShaderType::VERTEX)] = VK_SHADER_STAGE_VERTEX_BIT;
        vkShaderStageTable[to_base(ShaderType::FRAGMENT)] = VK_SHADER_STAGE_FRAGMENT_BIT;
        vkShaderStageTable[to_base(ShaderType::GEOMETRY)] = VK_SHADER_STAGE_GEOMETRY_BIT;
        vkShaderStageTable[to_base(ShaderType::TESSELLATION_CTRL)] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        vkShaderStageTable[to_base(ShaderType::TESSELLATION_EVAL)] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        vkShaderStageTable[to_base(ShaderType::COMPUTE)] = VK_SHADER_STAGE_COMPUTE_BIT;
    };

    VkFormat internalFormat(const GFXImageFormat baseFormat, const GFXDataFormat dataType, const bool srgb, const bool normalized) noexcept {
        switch (baseFormat) {
            case GFXImageFormat::RED:{
                assert(!srgb);
                switch (dataType) {
                case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM) : VK_FORMAT_R8_UINT;
                    case GFXDataFormat::UNSIGNED_SHORT: return normalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT;
                    case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32_UINT; }
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_R8_SNORM: VK_FORMAT_R8_SINT;
                    case GFXDataFormat::SIGNED_SHORT: return normalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT;
                    case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32_SINT; }
                    case GFXDataFormat::FLOAT_16: return VK_FORMAT_R16_SFLOAT;
                    case GFXDataFormat::FLOAT_32: return VK_FORMAT_R32_SFLOAT;
                };
            }break;
            case GFXImageFormat::RG: {
                assert(!srgb);
                switch (dataType) {
                    case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM) : VK_FORMAT_R8G8_UINT;
                    case GFXDataFormat::UNSIGNED_SHORT: return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
                    case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32_UINT; }
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_SINT;
                    case GFXDataFormat::SIGNED_SHORT: return normalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT;
                    case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32_SINT; }
                    case GFXDataFormat::FLOAT_16: return VK_FORMAT_R16G16_SFLOAT;
                    case GFXDataFormat::FLOAT_32: return VK_FORMAT_R32G32_SFLOAT;
                };
            }break;
            case GFXImageFormat::BGR:
            {
                assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
                switch (dataType) {
                    case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_B8G8R8_SRGB : VK_FORMAT_B8G8R8_UNORM) : VK_FORMAT_B8G8R8_UINT;
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_B8G8R8_SNORM : VK_FORMAT_B8G8R8_SINT;
                };
            }break;
            case GFXImageFormat::RGB:
            {
                assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
                switch (dataType) {
                    case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM) : VK_FORMAT_R8G8B8_UINT;
                    case GFXDataFormat::UNSIGNED_SHORT: return normalized ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R16G16B16_UINT;
                    case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32B32_UINT; }
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_R8G8B8_SNORM : VK_FORMAT_R8G8B8_SINT;
                    case GFXDataFormat::SIGNED_SHORT: return normalized ? VK_FORMAT_R16G16B16_SNORM : VK_FORMAT_R16G16B16_SINT;
                    case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32B32_SINT; }
                    case GFXDataFormat::FLOAT_16: return VK_FORMAT_R16G16B16_SFLOAT;
                    case GFXDataFormat::FLOAT_32: return VK_FORMAT_R32G32B32_SFLOAT;
                };
            }break;
            case GFXImageFormat::BGRA: {
                assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
                switch (dataType) {
                    case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM) : VK_FORMAT_B8G8R8A8_UINT;
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_B8G8R8A8_SNORM : VK_FORMAT_B8G8R8A8_SINT;
                };
            } break;
            case GFXImageFormat::RGBA:
            {
                assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
                switch (dataType) {
                    case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM) : VK_FORMAT_R8G8B8A8_UINT;
                    case GFXDataFormat::UNSIGNED_SHORT: return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT;
                    case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32B32A32_UINT; }
                    case GFXDataFormat::SIGNED_BYTE: return normalized ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_SINT;
                    case GFXDataFormat::SIGNED_SHORT: return normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT;
                    case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return VK_FORMAT_R32G32B32A32_SINT; }
                    case GFXDataFormat::FLOAT_16: return VK_FORMAT_R16G16B16A16_SFLOAT;
                    case GFXDataFormat::FLOAT_32: return VK_FORMAT_R32G32B32A32_SFLOAT;
                };
            }break;
            case GFXImageFormat::DEPTH_COMPONENT:
            {
                switch (dataType) {
                    case GFXDataFormat::SIGNED_BYTE:
                    case GFXDataFormat::UNSIGNED_BYTE:
                    case GFXDataFormat::SIGNED_SHORT:
                    case GFXDataFormat::UNSIGNED_SHORT: return VK_FORMAT_D16_UNORM;
                    case GFXDataFormat::SIGNED_INT:
                    case GFXDataFormat::UNSIGNED_INT: return VK_FORMAT_D24_UNORM_S8_UINT;
                    case GFXDataFormat::FLOAT_16:
                    case GFXDataFormat::FLOAT_32: return VK_FORMAT_D32_SFLOAT;
                };
            }break;
            // compressed formats
            case GFXImageFormat::DXT1_RGB: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            case GFXImageFormat::DXT1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;

            case GFXImageFormat::DXT1_RGBA: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            case GFXImageFormat::DXT1_RGBA_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;

            case GFXImageFormat::DXT3_RGBA: return VK_FORMAT_BC2_UNORM_BLOCK;
            case GFXImageFormat::DXT3_RGBA_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;

            case GFXImageFormat::DXT5_RGBA:
            case GFXImageFormat::DXT5_RGBA_SRGB:
            case GFXImageFormat::BC3n: return VK_FORMAT_BC3_UNORM_BLOCK;

            case GFXImageFormat::BC4s: return VK_FORMAT_BC4_SNORM_BLOCK;
            case GFXImageFormat::BC4u: return VK_FORMAT_BC4_UNORM_BLOCK;
            case GFXImageFormat::BC5s: return VK_FORMAT_BC5_SNORM_BLOCK;
            case GFXImageFormat::BC5u: return VK_FORMAT_BC5_UNORM_BLOCK;
            case GFXImageFormat::BC6s: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            case GFXImageFormat::BC6u: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            case GFXImageFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
            case GFXImageFormat::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
        }

        DIVIDE_UNEXPECTED_CALL();
        return VK_FORMAT_MAX_ENUM;
    }

    VkFormat internalFormat(const GFXDataFormat format, const U8 componentCount, const bool normalized) noexcept {
        switch (format) {
            case GFXDataFormat::UNSIGNED_BYTE: {
                switch (componentCount) {
                    case 1u: return normalized ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
                    case 2u: return normalized ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
                    case 3u: return normalized ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
                    case 4u: return normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
                };
            } break;
            case GFXDataFormat::UNSIGNED_SHORT: {
                switch (componentCount) {
                    case 1u: return normalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT;
                    case 2u: return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
                    case 3u: return normalized ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R16G16B16_UINT;
                    case 4u: return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT;
                };
            } break;
            case GFXDataFormat::UNSIGNED_INT: {
                switch (componentCount) {
                    case 1u: return VK_FORMAT_R32_UINT;
                    case 2u: return VK_FORMAT_R32G32_UINT;
                    case 3u: return VK_FORMAT_R32G32B32_UINT;
                    case 4u: return VK_FORMAT_R32G32B32A32_UINT;
                };
            } break;
            case GFXDataFormat::SIGNED_BYTE: {
                switch (componentCount) {
                    case 1u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                    case 2u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                    case 3u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                    case 4u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                };
            } break;
            case GFXDataFormat::SIGNED_SHORT: {
                switch (componentCount) {
                    case 1u: return normalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT;
                    case 2u: return normalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT;
                    case 3u: return normalized ? VK_FORMAT_R16G16B16_SNORM : VK_FORMAT_R16G16B16_SINT;
                    case 4u: return normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT;
                };
            } break;
            case GFXDataFormat::SIGNED_INT: {
                switch (componentCount) {
                    case 1u: return VK_FORMAT_R32_SINT;
                    case 2u: return VK_FORMAT_R32G32_SINT;
                    case 3u: return VK_FORMAT_R32G32B32_SINT;
                    case 4u: return VK_FORMAT_R32G32B32A32_SINT;
                };
            } break;
            case GFXDataFormat::FLOAT_16: {
                switch (componentCount) {
                    case 1u: return VK_FORMAT_R16_SFLOAT;
                    case 2u: return VK_FORMAT_R16G16_SFLOAT;
                    case 3u: return VK_FORMAT_R16G16B16_SFLOAT;
                    case 4u: return VK_FORMAT_R16G16B16A16_SFLOAT;
                };
            } break;
            case GFXDataFormat::FLOAT_32: {
                switch (componentCount) {
                    case 1u: return VK_FORMAT_R32_SFLOAT;
                    case 2u: return VK_FORMAT_R32G32_SFLOAT;
                    case 3u: return VK_FORMAT_R32G32B32_SFLOAT;
                    case 4u: return VK_FORMAT_R32G32B32A32_SFLOAT;
                };
            } break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return VK_FORMAT_MAX_ENUM;
    }

    VkDescriptorType vkDescriptorType(DescriptorSetBindingType type) noexcept {
        switch (type) {
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case DescriptorSetBindingType::IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case DescriptorSetBindingType::UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
};

}; //namespace Divide