

#include "Headers/vkResources.h"
#include "Headers/VKWrapper.h"

#include "Platform/Video/GLIM/glim.h"
#include "Platform/Video/Headers/DescriptorSets.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"

namespace Divide
{

    std::array<VkBlendFactor, to_base( BlendProperty::COUNT )> vkBlendTable;
    std::array<VkBlendOp, to_base( BlendOperation::COUNT )> vkBlendOpTable;
    std::array<VkCompareOp, to_base( ComparisonFunction::COUNT )> vkCompareFuncTable;
    std::array<VkStencilOp, to_base( StencilOperation::COUNT )> vkStencilOpTable;
    std::array<VkCullModeFlags, to_base( CullMode::COUNT )> vkCullModeTable;
    std::array<VkPolygonMode, to_base( FillMode::COUNT )> vkFillModeTable;
    std::array<VkImageType, to_base( TextureType::COUNT )> vkTextureTypeTable;
    std::array<VkImageViewType, to_base( TextureType::COUNT )> vkTextureViewTypeTable;
    std::array<VkPrimitiveTopology, to_base( PrimitiveTopology::COUNT )> vkPrimitiveTypeTable;
    std::array<VkSamplerAddressMode, to_base( TextureWrap::COUNT )> vkWrapTable;
    std::array<VkShaderStageFlagBits, to_base( ShaderType::COUNT )> vkShaderStageTable;
    std::array<VulkanQueryType, to_base( QueryType::COUNT )> vkQueryTypeTable;

    namespace Debug
    {
        PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = VK_NULL_HANDLE;
        PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = VK_NULL_HANDLE;
        PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT = VK_NULL_HANDLE;
        PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = VK_NULL_HANDLE;
        PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT = VK_NULL_HANDLE;


        void SetObjectName( const VkDevice device, const uint64_t object, const VkObjectType objectType, const char* name )
        {
            if ( VK_API::s_hasDebugMarkerSupport )
            {
                VkDebugUtilsObjectNameInfoEXT nameInfo{};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectHandle = object;
                nameInfo.objectType = objectType;
                nameInfo.pObjectName = name;
                vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
            }
        }

        void SetObjectTag( const VkDevice device, const uint64_t object, const VkObjectType objectType, const size_t tagSize, void* tagData, const uint64_t tagName )
        {
            if ( VK_API::s_hasDebugMarkerSupport )
            {
                VkDebugUtilsObjectTagInfoEXT tagInfo{};
                tagInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
                tagInfo.objectHandle = object;
                tagInfo.objectType = objectType;
                tagInfo.tagSize = tagSize;
                tagInfo.tagName = tagName;
                tagInfo.pTag = tagData;
                vkSetDebugUtilsObjectTagEXT( device, &tagInfo );
            }
        }
    }

    namespace VKUtil
    {
        void SubmitRenderCommand( const GenericDrawCommand& drawCommand,
                                  const VkCommandBuffer commandBuffer,
                                  const bool indexed )
        {
            if (drawCommand._drawCount == 0u || drawCommand._cmd.instanceCount == 0u)
            {
                // redundant command. Unexpected but not fatal. Should've been filtered out by now
                DebugBreak();
                return;
            }

            if ( VK_API::GetStateTracker()._pipeline._topology == PrimitiveTopology::MESHLET )
            {
                // We dispatch mesh shading calls sthe same as we do compute dispatches, so we should NEVER end up here.
                DIVIDE_UNEXPECTED_CALL();
                return;
            }

            const bool useIndirectBuffer = isEnabledOption(drawCommand, CmdRenderOptions::RENDER_INDIRECT);

            if ( !useIndirectBuffer && drawCommand._cmd.instanceCount > 1u && drawCommand._drawCount > 1u ) [[unlikely]]
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Multi-draw is incompatible with instancing as gl_DrawID will have the wrong value (base instance is also used for buffer indexing). Split the call into multiple draw commands with manual uniform-updates in-between!" );
            }

            if ( indexed )
            {

                if ( useIndirectBuffer )
                {
                    const size_t offset = (drawCommand._commandOffset * sizeof( IndirectIndexedDrawCommand )) + VK_API::GetStateTracker()._drawIndirectBufferOffset;
                    vkCmdDrawIndexedIndirect( commandBuffer, VK_API::GetStateTracker()._drawIndirectBuffer, offset, drawCommand._drawCount, sizeof( IndirectIndexedDrawCommand ) );
                }
                else
                {
                    vkCmdDrawIndexed( commandBuffer, drawCommand._cmd.indexCount, drawCommand._cmd.instanceCount, drawCommand._cmd.firstIndex, drawCommand._cmd.baseVertex, drawCommand._cmd.baseInstance );
                }
            }
            else
            {
                if ( useIndirectBuffer )
                {
                    const size_t offset = (drawCommand._commandOffset * sizeof( IndirectNonIndexedDrawCommand )) + VK_API::GetStateTracker()._drawIndirectBufferOffset;
                    vkCmdDrawIndirect( commandBuffer, VK_API::GetStateTracker()._drawIndirectBuffer, offset, drawCommand._drawCount, sizeof( IndirectNonIndexedDrawCommand ) );
               }
                else
                {
                    vkCmdDraw( commandBuffer, drawCommand._cmd.vertexCount, drawCommand._cmd.instanceCount, drawCommand._cmd.baseVertex, drawCommand._cmd.baseInstance );
                }
            }
        }

        void OnStartup( [[maybe_unused]] VkDevice device )
        {
            vkBlendTable[to_base( BlendProperty::ZERO )] = VK_BLEND_FACTOR_ZERO;
            vkBlendTable[to_base( BlendProperty::ONE )] = VK_BLEND_FACTOR_ONE;
            vkBlendTable[to_base( BlendProperty::SRC_COLOR )] = VK_BLEND_FACTOR_SRC_COLOR;
            vkBlendTable[to_base( BlendProperty::INV_SRC_COLOR )] = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            vkBlendTable[to_base( BlendProperty::SRC_ALPHA )] = VK_BLEND_FACTOR_SRC_ALPHA;
            vkBlendTable[to_base( BlendProperty::INV_SRC_ALPHA )] = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            vkBlendTable[to_base( BlendProperty::DEST_ALPHA )] = VK_BLEND_FACTOR_DST_ALPHA;
            vkBlendTable[to_base( BlendProperty::INV_DEST_ALPHA )] = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            vkBlendTable[to_base( BlendProperty::DEST_COLOR )] = VK_BLEND_FACTOR_DST_COLOR;
            vkBlendTable[to_base( BlendProperty::INV_DEST_COLOR )] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            vkBlendTable[to_base( BlendProperty::SRC_ALPHA_SAT )] = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;

            vkBlendOpTable[to_base( BlendOperation::ADD )] = VK_BLEND_OP_ADD;
            vkBlendOpTable[to_base( BlendOperation::SUBTRACT )] = VK_BLEND_OP_SUBTRACT;
            vkBlendOpTable[to_base( BlendOperation::REV_SUBTRACT )] = VK_BLEND_OP_REVERSE_SUBTRACT;
            vkBlendOpTable[to_base( BlendOperation::MIN )] = VK_BLEND_OP_MIN;
            vkBlendOpTable[to_base( BlendOperation::MAX )] = VK_BLEND_OP_MAX;

            vkCompareFuncTable[to_base( ComparisonFunction::NEVER )] = VK_COMPARE_OP_NEVER;
            vkCompareFuncTable[to_base( ComparisonFunction::LESS )] = VK_COMPARE_OP_LESS;
            vkCompareFuncTable[to_base( ComparisonFunction::EQUAL )] = VK_COMPARE_OP_EQUAL;
            vkCompareFuncTable[to_base( ComparisonFunction::LEQUAL )] = VK_COMPARE_OP_LESS_OR_EQUAL;
            vkCompareFuncTable[to_base( ComparisonFunction::GREATER )] = VK_COMPARE_OP_GREATER;
            vkCompareFuncTable[to_base( ComparisonFunction::NEQUAL )] = VK_COMPARE_OP_NOT_EQUAL;
            vkCompareFuncTable[to_base( ComparisonFunction::GEQUAL )] = VK_COMPARE_OP_GREATER_OR_EQUAL;
            vkCompareFuncTable[to_base( ComparisonFunction::ALWAYS )] = VK_COMPARE_OP_ALWAYS;

            vkStencilOpTable[to_base( StencilOperation::KEEP )] = VK_STENCIL_OP_KEEP;
            vkStencilOpTable[to_base( StencilOperation::ZERO )] = VK_STENCIL_OP_ZERO;
            vkStencilOpTable[to_base( StencilOperation::REPLACE )] = VK_STENCIL_OP_REPLACE;
            vkStencilOpTable[to_base( StencilOperation::INCR )] = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            vkStencilOpTable[to_base( StencilOperation::DECR )] = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            vkStencilOpTable[to_base( StencilOperation::INV )] = VK_STENCIL_OP_INVERT;
            vkStencilOpTable[to_base( StencilOperation::INCR_WRAP )] = VK_STENCIL_OP_INCREMENT_AND_WRAP;
            vkStencilOpTable[to_base( StencilOperation::DECR_WRAP )] = VK_STENCIL_OP_DECREMENT_AND_WRAP;

            vkCullModeTable[to_base( CullMode::BACK )] = VK_CULL_MODE_BACK_BIT;
            vkCullModeTable[to_base( CullMode::FRONT )] = VK_CULL_MODE_FRONT_BIT;
            vkCullModeTable[to_base( CullMode::ALL )] = VK_CULL_MODE_FRONT_AND_BACK;
            vkCullModeTable[to_base( CullMode::NONE )] = VK_CULL_MODE_NONE;

            vkFillModeTable[to_base( FillMode::POINT )] = VK_POLYGON_MODE_POINT;
            vkFillModeTable[to_base( FillMode::WIREFRAME )] = VK_POLYGON_MODE_LINE;
            vkFillModeTable[to_base( FillMode::SOLID )] = VK_POLYGON_MODE_FILL;

            vkTextureTypeTable[to_base( TextureType::TEXTURE_1D )] = VK_IMAGE_TYPE_1D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_2D )] = VK_IMAGE_TYPE_2D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_3D )] = VK_IMAGE_TYPE_3D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_CUBE_MAP )] = VK_IMAGE_TYPE_2D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_1D_ARRAY )] = VK_IMAGE_TYPE_1D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_2D_ARRAY )] = VK_IMAGE_TYPE_2D;
            vkTextureTypeTable[to_base( TextureType::TEXTURE_CUBE_ARRAY )] = VK_IMAGE_TYPE_2D;

            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_1D )] = VK_IMAGE_VIEW_TYPE_1D;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_2D )] = VK_IMAGE_VIEW_TYPE_2D;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_3D )] = VK_IMAGE_VIEW_TYPE_3D;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_CUBE_MAP )] = VK_IMAGE_VIEW_TYPE_CUBE;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_1D_ARRAY )] = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_2D_ARRAY )] = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            vkTextureViewTypeTable[to_base( TextureType::TEXTURE_CUBE_ARRAY )] = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

            vkPrimitiveTypeTable[to_base( PrimitiveTopology::POINTS )] = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::LINES )] = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::LINE_STRIP )] = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::TRIANGLES )] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::TRIANGLE_STRIP )] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::TRIANGLE_FAN )] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::LINES_ADJACENCY )] = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::LINE_STRIP_ADJACENCY )] = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::TRIANGLES_ADJACENCY )] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY )] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::PATCH )] = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::COMPUTE )] = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
            vkPrimitiveTypeTable[to_base( PrimitiveTopology::MESHLET )] = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;

            vkWrapTable[to_base( TextureWrap::MIRROR_REPEAT )] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            vkWrapTable[to_base( TextureWrap::REPEAT )] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            vkWrapTable[to_base( TextureWrap::CLAMP_TO_EDGE )] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            vkWrapTable[to_base( TextureWrap::CLAMP_TO_BORDER )] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            vkWrapTable[to_base( TextureWrap::MIRROR_CLAMP_TO_EDGE )] = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;

            vkShaderStageTable[to_base( ShaderType::VERTEX )] = VK_SHADER_STAGE_VERTEX_BIT;
            vkShaderStageTable[to_base( ShaderType::FRAGMENT )] = VK_SHADER_STAGE_FRAGMENT_BIT;
            vkShaderStageTable[to_base( ShaderType::GEOMETRY )] = VK_SHADER_STAGE_GEOMETRY_BIT;
            vkShaderStageTable[to_base( ShaderType::TESSELLATION_CTRL )] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            vkShaderStageTable[to_base( ShaderType::TESSELLATION_EVAL )] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            vkShaderStageTable[to_base( ShaderType::COMPUTE )] = VK_SHADER_STAGE_COMPUTE_BIT;
            vkShaderStageTable[to_base( ShaderType::MESH_NV )] = VK_SHADER_STAGE_MESH_BIT_NV;
            vkShaderStageTable[to_base( ShaderType::TASK_NV )] = VK_SHADER_STAGE_TASK_BIT_NV;


            vkQueryTypeTable[to_U8(log2( to_base( QueryType::VERTICES_SUBMITTED ) ) ) - 1]._statistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::PRIMITIVES_GENERATED ) ) ) - 1]._queryType = VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::TESSELLATION_PATCHES ) ) ) - 1]._statistics = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::TESSELLATION_EVAL_INVOCATIONS ) ) ) - 1]._statistics = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::GPU_TIME ) ) ) - 1]._queryType = VK_QUERY_TYPE_TIMESTAMP;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::SAMPLE_COUNT ) ) ) - 1]._queryType = VK_QUERY_TYPE_OCCLUSION;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::SAMPLE_COUNT ) ) ) - 1]._accurate = true;
            vkQueryTypeTable[to_U8(log2( to_base( QueryType::ANY_SAMPLE_RENDERED ) ) ) - 1]._queryType = VK_QUERY_TYPE_OCCLUSION;
        }

        VkFormat InternalFormat( const GFXImageFormat baseFormat, const GFXDataFormat dataType, const GFXImagePacking packing ) noexcept
        {
            const bool isDepth = IsDepthTexture( packing );
            const bool isSRGB = packing == GFXImagePacking::NORMALIZED_SRGB;
            const bool isPacked = packing == GFXImagePacking::RGB_565 || packing == GFXImagePacking::RGBA_4444;
            const bool isNormalized = packing == GFXImagePacking::NORMALIZED || isSRGB || isDepth || isPacked;

            if ( isDepth )
            {
                DIVIDE_GPU_ASSERT( baseFormat == GFXImageFormat::RED );
            }

            if ( isSRGB )
            {
                DIVIDE_GPU_ASSERT( dataType == GFXDataFormat::UNSIGNED_BYTE &&
                                   baseFormat == GFXImageFormat::RGB ||
                                   baseFormat == GFXImageFormat::BGR ||
                                   baseFormat == GFXImageFormat::RGBA ||
                                   baseFormat == GFXImageFormat::BGRA ||
                                   baseFormat == GFXImageFormat::DXT1_RGB ||
                                   baseFormat == GFXImageFormat::DXT1_RGBA ||
                                   baseFormat == GFXImageFormat::DXT3_RGBA ||
                                   baseFormat == GFXImageFormat::DXT5_RGBA ||
                                   baseFormat == GFXImageFormat::BC7,
                                   "VKUtil::InternalFormat: Vulkan supports VK_FORMAT_R8(G8)_SRGB and BC1/2/3/7, but OpenGL doesn't, so for now we completely ignore these formats!" );
            }

            if ( isNormalized && !isDepth )
            {
                DIVIDE_GPU_ASSERT( dataType == GFXDataFormat::SIGNED_BYTE ||
                                   dataType == GFXDataFormat::UNSIGNED_BYTE ||
                                   dataType == GFXDataFormat::SIGNED_SHORT ||
                                   dataType == GFXDataFormat::UNSIGNED_SHORT ||
                                   dataType == GFXDataFormat::FLOAT_16 ||
                                   dataType == GFXDataFormat::FLOAT_32);
            }

            if ( isPacked )
            {
                DIVIDE_GPU_ASSERT( baseFormat == GFXImageFormat::RGB ||
                                   baseFormat == GFXImageFormat::BGR ||
                                   baseFormat == GFXImageFormat::RGBA ||
                                   baseFormat == GFXImageFormat::BGRA );
            }

            if ( baseFormat == GFXImageFormat::BGR || baseFormat == GFXImageFormat::BGRA )
            {
                DIVIDE_GPU_ASSERT( dataType == GFXDataFormat::UNSIGNED_BYTE ||
                                   dataType == GFXDataFormat::SIGNED_BYTE,
                                   "VKUtil::InternalFormat: Vulkan only supports 8Bpp for BGR(A) formats.");
            }

            switch ( baseFormat )
            {
                case GFXImageFormat::RED:
                {
                    if ( packing == GFXImagePacking::DEPTH )
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::SIGNED_BYTE:
                            case GFXDataFormat::UNSIGNED_BYTE:
                            case GFXDataFormat::SIGNED_SHORT:
                            case GFXDataFormat::UNSIGNED_SHORT:  return VK_FORMAT_D16_UNORM;
                            case GFXDataFormat::SIGNED_INT:
                            case GFXDataFormat::UNSIGNED_INT:    return VK_API::s_depthFormatInformation._d24x8Supported ? VK_FORMAT_X8_D24_UNORM_PACK32 : VK_FORMAT_D32_SFLOAT;
                            case GFXDataFormat::FLOAT_16:        DIVIDE_UNEXPECTED_CALL_MSG( "VKUtil::InternalFormat: Vulkan does not support half-float depth buffers!" ); break;
                            case GFXDataFormat::FLOAT_32:        return VK_API::s_depthFormatInformation._d32FSupported ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_X8_D24_UNORM_PACK32;
                            default: break;
                        };
                    }
                    else if ( packing == GFXImagePacking::DEPTH_STENCIL )
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::SIGNED_BYTE:
                            case GFXDataFormat::UNSIGNED_BYTE:
                            case GFXDataFormat::SIGNED_SHORT:
                            case GFXDataFormat::UNSIGNED_SHORT:
                            case GFXDataFormat::SIGNED_INT:
                            case GFXDataFormat::UNSIGNED_INT:     return VK_API::s_depthFormatInformation._d24s8Supported ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
                            case GFXDataFormat::FLOAT_16:         DIVIDE_UNEXPECTED_CALL_MSG("VKUtil::InternalFormat: Vulkan does not support half-float depth buffers!"); break;
                            case GFXDataFormat::FLOAT_32:         return  VK_API::s_depthFormatInformation._d32s8Supported ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT;
                            default: break;
                        };
                    }
                    else
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::UNSIGNED_BYTE:  return isNormalized ? VK_FORMAT_R8_UNORM  : VK_FORMAT_R8_UINT;
                            case GFXDataFormat::UNSIGNED_SHORT: return isNormalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT;
                            case GFXDataFormat::SIGNED_BYTE:    return isNormalized ? VK_FORMAT_R8_SNORM  : VK_FORMAT_R8_SINT;
                            case GFXDataFormat::SIGNED_SHORT:   return isNormalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT;
                            case GFXDataFormat::UNSIGNED_INT:   return VK_FORMAT_R32_UINT;
                            case GFXDataFormat::SIGNED_INT:     return VK_FORMAT_R32_SINT;
                            case GFXDataFormat::FLOAT_16:       return VK_FORMAT_R16_SFLOAT;
                            case GFXDataFormat::FLOAT_32:       return VK_FORMAT_R32_SFLOAT;
                            default: break;
                        };
                    }
                }break;
                case GFXImageFormat::RG:
                {
                    switch ( dataType )
                    {
                        case GFXDataFormat::UNSIGNED_BYTE:  return isNormalized ? VK_FORMAT_R8G8_UNORM   : VK_FORMAT_R8G8_UINT;
                        case GFXDataFormat::UNSIGNED_SHORT: return isNormalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
                        case GFXDataFormat::SIGNED_BYTE:    return isNormalized ? VK_FORMAT_R8G8_SNORM   : VK_FORMAT_R8G8_SINT;
                        case GFXDataFormat::SIGNED_SHORT:   return isNormalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT;
                        case GFXDataFormat::UNSIGNED_INT:   return VK_FORMAT_R32G32_UINT;
                        case GFXDataFormat::SIGNED_INT:     return VK_FORMAT_R32G32_SINT;
                        case GFXDataFormat::FLOAT_16:       return VK_FORMAT_R16G16_SFLOAT;
                        case GFXDataFormat::FLOAT_32:       return VK_FORMAT_R32G32_SFLOAT;
                        default: break;
                    };                                      
                }break;
                case GFXImageFormat::BGR:
                {
                    if ( packing == GFXImagePacking::RGB_565 )
                    {
                        return VK_FORMAT_B5G6R5_UNORM_PACK16;
                    }
                    else
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::UNSIGNED_BYTE: return isNormalized ? (isSRGB ? VK_FORMAT_B8G8R8_SRGB : VK_FORMAT_B8G8R8_UNORM) : VK_FORMAT_B8G8R8_UINT;
                            case GFXDataFormat::SIGNED_BYTE  : return isNormalized ? VK_FORMAT_B8G8R8_SNORM                                    : VK_FORMAT_B8G8R8_SINT;
                            default: break;
                        };
                    }
                }break;
                case GFXImageFormat::RGB:
                {
                    if ( packing == GFXImagePacking::RGB_565 )
                    {
                        return VK_FORMAT_R5G6B5_UNORM_PACK16;
                    }
                    else
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::UNSIGNED_BYTE:  return isNormalized ? (isSRGB ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM) : VK_FORMAT_R8G8B8_UINT;
                            case GFXDataFormat::UNSIGNED_SHORT: return isNormalized ? VK_FORMAT_R16G16B16_UNORM                                 : VK_FORMAT_R16G16B16_UINT;
                            case GFXDataFormat::SIGNED_BYTE:    return isNormalized ? VK_FORMAT_R8G8B8_SNORM                                    : VK_FORMAT_R8G8B8_SINT;
                            case GFXDataFormat::SIGNED_SHORT:   return isNormalized ? VK_FORMAT_R16G16B16_SNORM                                 : VK_FORMAT_R16G16B16_SINT;
                            case GFXDataFormat::UNSIGNED_INT:   return VK_FORMAT_R32G32B32_UINT;
                            case GFXDataFormat::SIGNED_INT:     return VK_FORMAT_R32G32B32_SINT;
                            case GFXDataFormat::FLOAT_16:       return packing == GFXImagePacking::RGB_111110F ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16_SFLOAT;
                            case GFXDataFormat::FLOAT_32:       return VK_FORMAT_R32G32B32_SFLOAT;
                            default: break;
                        };
                    }
                }break;
                case GFXImageFormat::BGRA:
                {
                    if ( packing == GFXImagePacking::RGBA_4444 )
                    {
                        return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
                    }
                    else
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::UNSIGNED_BYTE: return isNormalized ? (isSRGB ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM) : VK_FORMAT_B8G8R8A8_UINT;
                            case GFXDataFormat::SIGNED_BYTE:   return isNormalized ? VK_FORMAT_B8G8R8A8_SNORM                                      : VK_FORMAT_B8G8R8A8_SINT;
                            default: break;
                        };
                    }
                } break;
                case GFXImageFormat::RGBA:
                {
                    if ( packing == GFXImagePacking::RGBA_4444 )
                    {
                        return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
                    }
                    else
                    {
                        switch ( dataType )
                        {
                            case GFXDataFormat::UNSIGNED_BYTE:  return isNormalized ? (isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM) : VK_FORMAT_R8G8B8A8_UINT;
                            case GFXDataFormat::UNSIGNED_SHORT: return isNormalized ? VK_FORMAT_R16G16B16A16_UNORM                                  : VK_FORMAT_R16G16B16A16_UINT;
                            case GFXDataFormat::SIGNED_BYTE:    return isNormalized ? VK_FORMAT_R8G8B8A8_SNORM                                      : VK_FORMAT_R8G8B8A8_SINT;
                            case GFXDataFormat::SIGNED_SHORT:   return isNormalized ? VK_FORMAT_R16G16B16A16_SNORM                                  : VK_FORMAT_R16G16B16A16_SINT;
                            case GFXDataFormat::UNSIGNED_INT:   return VK_FORMAT_R32G32B32A32_UINT;
                            case GFXDataFormat::SIGNED_INT:     return VK_FORMAT_R32G32B32A32_SINT;
                            case GFXDataFormat::FLOAT_16:       return VK_FORMAT_R16G16B16A16_SFLOAT;
                            case GFXDataFormat::FLOAT_32:       return VK_FORMAT_R32G32B32A32_SFLOAT;
                            default: break;
                        };
                    }
                }break;

                case GFXImageFormat::DXT1_RGB:       return isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK  : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                case GFXImageFormat::DXT1_RGBA:      return isSRGB ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                case GFXImageFormat::DXT3_RGBA:      return isSRGB ? VK_FORMAT_BC2_SRGB_BLOCK      : VK_FORMAT_BC2_UNORM_BLOCK;
                case GFXImageFormat::DXT5_RGBA:      return isSRGB ? VK_FORMAT_BC3_UNORM_BLOCK     : VK_FORMAT_BC3_UNORM_BLOCK;
                case GFXImageFormat::BC7:            return isSRGB ? VK_FORMAT_BC7_SRGB_BLOCK      : VK_FORMAT_BC7_UNORM_BLOCK;
                case GFXImageFormat::BC3n:           return VK_FORMAT_BC3_UNORM_BLOCK;
                case GFXImageFormat::BC4s:           return VK_FORMAT_BC4_SNORM_BLOCK;
                case GFXImageFormat::BC4u:           return VK_FORMAT_BC4_UNORM_BLOCK;
                case GFXImageFormat::BC5s:           return VK_FORMAT_BC5_SNORM_BLOCK;
                case GFXImageFormat::BC5u:           return VK_FORMAT_BC5_UNORM_BLOCK;
                case GFXImageFormat::BC6s:           return VK_FORMAT_BC6H_SFLOAT_BLOCK;
                case GFXImageFormat::BC6u:           return VK_FORMAT_BC6H_UFLOAT_BLOCK;
                default: break;
            }

            DIVIDE_UNEXPECTED_CALL();
            return VK_FORMAT_MAX_ENUM;
        }

        VkFormat InternalFormat( const GFXDataFormat format, const U8 componentCount, const bool normalized ) noexcept
        {
            switch ( format )
            {
                case GFXDataFormat::UNSIGNED_BYTE:
                {
                    switch ( componentCount )
                    {
                        case 1u: return normalized ? VK_FORMAT_R8_UNORM       : VK_FORMAT_R8_UINT;
                        case 2u: return normalized ? VK_FORMAT_R8G8_UNORM     : VK_FORMAT_R8G8_UINT;
                        case 3u: return normalized ? VK_FORMAT_R8G8B8_UNORM   : VK_FORMAT_R8G8B8_UINT;
                        case 4u: return normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::UNSIGNED_SHORT:
                {
                    switch ( componentCount )
                    {
                        case 1u: return normalized ? VK_FORMAT_R16_UNORM          : VK_FORMAT_R16_UINT;
                        case 2u: return normalized ? VK_FORMAT_R16G16_UNORM       : VK_FORMAT_R16G16_UINT;
                        case 3u: return normalized ? VK_FORMAT_R16G16B16_UNORM    : VK_FORMAT_R16G16B16_UINT;
                        case 4u: return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::UNSIGNED_INT:
                {
                    switch ( componentCount )
                    {
                        case 1u: return VK_FORMAT_R32_UINT;
                        case 2u: return VK_FORMAT_R32G32_UINT;
                        case 3u: return VK_FORMAT_R32G32B32_UINT;
                        case 4u: return VK_FORMAT_R32G32B32A32_UINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::SIGNED_BYTE:
                {
                    switch ( componentCount )
                    {
                        case 1u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                        case 2u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                        case 3u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                        case 4u: return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::SIGNED_SHORT:
                {
                    switch ( componentCount )
                    {
                        case 1u: return normalized ? VK_FORMAT_R16_SNORM          : VK_FORMAT_R16_SINT;
                        case 2u: return normalized ? VK_FORMAT_R16G16_SNORM       : VK_FORMAT_R16G16_SINT;
                        case 3u: return normalized ? VK_FORMAT_R16G16B16_SNORM    : VK_FORMAT_R16G16B16_SINT;
                        case 4u: return normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::SIGNED_INT:
                {
                    switch ( componentCount )
                    {
                        case 1u: return VK_FORMAT_R32_SINT;
                        case 2u: return VK_FORMAT_R32G32_SINT;
                        case 3u: return VK_FORMAT_R32G32B32_SINT;
                        case 4u: return VK_FORMAT_R32G32B32A32_SINT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::FLOAT_16:
                {
                    switch ( componentCount )
                    {
                        case 1u: return VK_FORMAT_R16_SFLOAT;
                        case 2u: return VK_FORMAT_R16G16_SFLOAT;
                        case 3u: return VK_FORMAT_R16G16B16_SFLOAT;
                        case 4u: return VK_FORMAT_R16G16B16A16_SFLOAT;
                        default: break;
                    };
                } break;
                case GFXDataFormat::FLOAT_32:
                {
                    switch ( componentCount )
                    {
                        case 1u: return VK_FORMAT_R32_SFLOAT;
                        case 2u: return VK_FORMAT_R32G32_SFLOAT;
                        case 3u: return VK_FORMAT_R32G32B32_SFLOAT;
                        case 4u: return VK_FORMAT_R32G32B32A32_SFLOAT;
                        default: break;
                    };
                } break;
                default:
                case GFXDataFormat::COUNT: break;
            }

            DIVIDE_UNEXPECTED_CALL();
            return VK_FORMAT_MAX_ENUM;
        }

        VkDescriptorType vkDescriptorType( const DescriptorSetBindingType type, const bool isPushDescriptor ) noexcept
        {
            switch ( type )
            {
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                case DescriptorSetBindingType::IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case DescriptorSetBindingType::UNIFORM_BUFFER: return isPushDescriptor ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: return isPushDescriptor ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                default:
                case DescriptorSetBindingType::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
            }

            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }; //namespace VKUtil
}; //namespace Divide
