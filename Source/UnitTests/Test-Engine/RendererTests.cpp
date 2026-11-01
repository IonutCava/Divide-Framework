// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderDataUploader.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"

namespace Divide
{
    TEST_CASE( "Push command merging preserves shared uniform storage", "[push_constants]" )
    {
        UniformData uniforms;
        const F32 value = 3.f;
        uniforms.set( 1u, PushConstantType::FLOAT, value );
        GFX::SendPushConstantsCommand lhs, rhs;
        lhs._uniformData = rhs._uniformData = &uniforms;
        lhs._fastData.data[0] = MAT4_IDENTITY;
        lhs._fastData.computeFlags();
        rhs._fastData.data[1] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        REQUIRE( GFX::Merge( &lhs, &rhs ) );
        REQUIRE( uniforms.entries().size() == 1u );
        F32 stored = 0.f;
        std::memcpy( &stored, uniforms.data( uniforms.entries()[0]._range._startOffset ), sizeof(stored) );
        CHECK( stored == value );
    }

    TEST_CASE( "Uniform merges are transactional and later values win", "[push_constants]" )
    {
        UniformData lhs, rhs;
        const F32 oldValue = 1.f, newValue = 2.f;
        lhs.set( 1u, PushConstantType::FLOAT, oldValue );
        rhs.set( 2u, PushConstantType::FLOAT, newValue );
        rhs.set( 1u, PushConstantType::FLOAT, newValue );
        REQUIRE( Merge( lhs, rhs ) );
        CHECK( rhs.entries().empty() );
        REQUIRE( lhs.entries().size() == 2u );
        for ( const auto& entry : lhs.entries() )
        {
            F32 stored = 0.f;
            std::memcpy( &stored, lhs.data( entry._range._startOffset ), sizeof(stored) );
            CHECK( stored == newValue );
        }

        rhs.set( 3u, PushConstantType::FLOAT, oldValue );
        const std::array<F32, 2> incompatible{oldValue, newValue};
        rhs.set( 1u, PushConstantType::FLOAT, incompatible.data(), incompatible.size() );
        CHECK_FALSE( Merge( lhs, rhs ) );
        CHECK( lhs.entries().size() == 2u );
        CHECK( rhs.entries().size() == 2u );
        F32 stored = 0.f;
        std::memcpy( &stored, lhs.data( lhs.entries()[0]._range._startOffset ), sizeof(stored) );
        CHECK( stored == newValue );

        UniformData empty;
        REQUIRE( Merge( empty, rhs ) );
        CHECK( empty.entries().size() == 2u );
        CHECK( rhs.entries().empty() );
    }

    TEST_CASE( "Push cache replays all known slots after shader changes", "[push_constants]" )
    {
        PushConstantsStruct cache, firstShader, secondShader, update;
        update.data[0] = MAT4_IDENTITY;
        update.computeFlags();
        cache.merge( update );
        firstShader.merge( cache );

        secondShader.merge( cache );
        CHECK( secondShader.data[0] == MAT4_IDENTITY );
        CHECK( secondShader.dataFlags() == PushConstantsStruct::FIRST );

        update = {};
        update.data[1] = MAT4_ZERO;
        update.computeFlags();
        cache.merge( update );
        secondShader.merge( cache );
        CHECK( secondShader.data[0] == MAT4_IDENTITY );
        CHECK( secondShader.data[1] == MAT4_ZERO );

        firstShader.merge( cache );
        CHECK( firstShader == secondShader );
        CHECK( firstShader.dataFlags() == PushConstantsStruct::BOTH );
    }

    TEST_CASE( "Push commands coalesce compatible slots without changing conflicts", "[push_constants]" )
    {
        GFX::SendPushConstantsCommand lhs, rhs;
        lhs._fastData.data[0] = MAT4_IDENTITY;
        lhs._fastData.computeFlags();
        rhs._fastData.data[1] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        REQUIRE( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData.data[0] == MAT4_IDENTITY );
        CHECK( lhs._fastData.data[1] == MAT4_ZERO );
        CHECK( lhs._fastData.dataFlags() == PushConstantsStruct::BOTH );

        rhs = {};
        rhs._fastData.data[0] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        const PushConstantsStruct before = lhs._fastData;
        CHECK_FALSE( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData == before );

        rhs._fastData.data[0] = MAT4_IDENTITY;
        rhs._fastData.computeFlags();
        CHECK( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData == before );
    }

    TEST_CASE( "Optional Vulkan dynamic state requires every consumed feature", "[vulkan_features]" )
    {
        for ( U8 mask = 0; mask < 8; ++mask )
        {
            VkPhysicalDeviceExtendedDynamicState3FeaturesEXT features{};
            features.extendedDynamicState3ColorBlendEnable = (mask & 1u) != 0u;
            features.extendedDynamicState3ColorBlendEquation = (mask & 2u) != 0u;
            features.extendedDynamicState3ColorWriteMask = (mask & 4u) != 0u;
            CHECK( VKDevice::HasRequiredDynamicState3Features( features ) == (mask == 7u) );
        }
    }

    TEST_CASE( "Partial push constants preserve previously supplied slots", "[push_constants]" )
    {
        PushConstantsStruct cached, update;
        update.data[0] = MAT4_IDENTITY;
        REQUIRE( update.computeFlags() == PushConstantsStruct::FIRST );
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_NEGATIVE_ONE );

        update = {};
        update.data[1] = MAT4_ZERO;
        REQUIRE( update.computeFlags() == PushConstantsStruct::SECOND );
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_ZERO );
        CHECK( cached.dataFlags() == PushConstantsStruct::BOTH );

        update = {};
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_ZERO );
        update.data[0] = MAT4_ZERO;
        update.computeFlags();
        cached.merge( update );
        CHECK( cached.data[1] == MAT4_ZERO );
        CHECK( cached.data[0] == MAT4_ZERO );
    }

    TEST_CASE( "Vulkan texture aspects follow packing, not attachment usage", "[texture_fallbacks]" )
    {
        TextureDescriptor descriptor;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == VK_IMAGE_ASPECT_COLOR_BIT );
        descriptor._packing = GFXImagePacking::DEPTH;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == VK_IMAGE_ASPECT_DEPTH_BIT );
        descriptor._packing = GFXImagePacking::DEPTH_STENCIL;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) );
    }

    TEST_CASE( "Sampler reflection retains fallback compatibility traits", "[texture_fallbacks]" )
    {
        struct CompilerScope
        {
            bool _owned{ !SpirvHelper::s_isInit };
            CompilerScope() { if ( _owned ) SpirvHelper::Init(); }
            ~CompilerScope() { if ( _owned ) SpirvHelper::Finalize(); }
        } compiler;
        const char* source = R"(#version 450
layout(set = 3, binding = 0) uniform sampler2DArray sArray;
layout(set = 3, binding = 1) uniform samplerCube sCube;
layout(set = 3, binding = 2) uniform samplerCubeArray sCubeArray;
layout(set = 3, binding = 3) uniform isampler3D sInteger;
layout(set = 3, binding = 4) uniform usampler1D sUnsigned;
layout(set = 3, binding = 5) uniform sampler2DShadow sDepth;
layout(set = 3, binding = 6) uniform sampler2DMS sMSAA;
layout(location = 0) out vec4 colour;
void main() {
    colour = texture(sArray, vec3(0.5, 0.5, 0.0))
           + texture(sCube, vec3(1.0, 0.0, 0.0))
           + texture(sCubeArray, vec4(1.0, 0.0, 0.0, 0.0))
           + vec4(texture(sInteger, vec3(0.5)))
           + vec4(texture(sUnsigned, 0.5))
           + vec4(texture(sDepth, vec3(0.5)))
           + texelFetch(sMSAA, ivec2(0), 0);
}
)";
        std::vector<unsigned int> spirv;
        REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, source, spirv, RenderAPI::Vulkan ) );
        Reflection::Data data;
        REQUIRE( SpirvHelper::BuildReflectionData( ShaderType::FRAGMENT, spirv, RenderAPI::Vulkan, data ) );
        const std::array expectedTypes{
            TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_CUBE_MAP, TextureType::TEXTURE_CUBE_ARRAY,
            TextureType::TEXTURE_3D, TextureType::TEXTURE_1D, TextureType::TEXTURE_2D, TextureType::TEXTURE_2D
        };
        REQUIRE( data._images.size() == expectedTypes.size() );
        for ( U8 slot = 0u; slot < expectedTypes.size(); ++slot )
        {
            const auto image = std::find_if( data._images.begin(), data._images.end(), [slot](const Reflection::ImageEntry& it)
            {
                return it._bindingSlot == slot;
            } );
            REQUIRE( image != data._images.end() );
            CHECK( image->_textureType == expectedTypes[slot] );
            CHECK( image->_combinedImageSampler );
            CHECK( image->_isMultiSampled == (slot == 6u) );
            CHECK( image->_isDepth == (slot == 5u) );
            CHECK( image->_sampledDataType == (slot == 3u ? GFXDataFormat::SIGNED_INT :
                                             slot == 4u ? GFXDataFormat::UNSIGNED_INT : GFXDataFormat::UNSIGNED_BYTE) );
        }
    }

    TEST_CASE( "Default sampler textures preserve dimensionality and sample format", "[texture_fallbacks]" )
    {
        for ( U8 type = 0u; type < to_base(TextureType::COUNT); ++type )
        {
            const TextureType textureType = static_cast<TextureType>(type);
            const TextureDescriptor colour = Texture::DefaultTextureDescriptor( textureType, GFXDataFormat::UNSIGNED_BYTE, false );
            CHECK( colour._texType == textureType );
            CHECK( colour._layerCount == 1u );
            CHECK( colour._msaaSamples == 0u );
            CHECK( colour._mipMappingState == MipMappingState::OFF );
            CHECK( colour._baseFormat == GFXImageFormat::RGBA );
            CHECK( colour._packing == GFXImagePacking::NORMALIZED );
            for ( const GFXDataFormat dataType : {GFXDataFormat::SIGNED_INT, GFXDataFormat::UNSIGNED_INT} )
            {
                const TextureDescriptor integer = Texture::DefaultTextureDescriptor( textureType, dataType, false );
                CHECK( integer._texType == textureType );
                CHECK( integer._dataType == dataType );
                CHECK( integer._packing == GFXImagePacking::UNNORMALIZED );
            }
            if ( textureType != TextureType::TEXTURE_3D )
            {
                const TextureDescriptor depth = Texture::DefaultTextureDescriptor( textureType, GFXDataFormat::UNSIGNED_BYTE, true );
                CHECK( depth._texType == textureType );
                CHECK( depth._baseFormat == GFXImageFormat::RED );
                CHECK( depth._dataType == GFXDataFormat::FLOAT_32 );
                CHECK( depth._packing == GFXImagePacking::DEPTH );
            }
        }
    }

    TEST_CASE( "Texture update source layouts", "[texture_updates]" )
    {
        TextureDescriptor descriptor;
        TextureUpdateLayout layout;

        SECTION( "compact RGBA rectangle uses region size, not texture size" )
        {
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 12u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._sourceOffset == 0u );
            CHECK( layout._stagingSize == 24u );
            CHECK( layout._requiredSize == 24u );
        }
        SECTION( "full atlas buffer uses texel row length and source skips" )
        {
            const PixelAlignment alignment{ ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._rowStride == 256u );
            CHECK( layout._sourceOffset == 1812u );
            CHECK( layout._requiredSize == 2080u );
            CHECK( layout._stagingSize == 24u );
        }
        SECTION( "RGB row alignment rounds up instead of multiplying" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 9u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._requiredSize == 21u );
            CHECK( layout._stagingSize == 18u );
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._requiredSize == 18u );
        }
        SECTION( "single-channel font atlas" )
        {
            descriptor._baseFormat = GFXImageFormat::RED;
            const PixelAlignment alignment{ ._alignment = 1u, ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._sourceOffset == 453u );
            CHECK( layout._requiredSize == 520u );
            CHECK( layout._stagingSize == 6u );
        }
        SECTION( "multiple layers or depth slices retain source pitch" )
        {
            const PixelAlignment alignment{ ._rowLength = 8u, ._skipPixels = 1u, ._skipRows = 1u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {2u, 3u, 2u}, alignment, layout ) );
            CHECK( layout._sliceStride == 96u );
            CHECK( layout._sourceOffset == 36u );
            CHECK( layout._requiredSize == 204u );
            CHECK( layout._stagingSize == 48u );
        }
        SECTION( "packed pixels respect requested row alignment" )
        {
            descriptor._packing = GFXImagePacking::RGBA_4444;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._rowBytes == 6u );
            CHECK( layout._rowStride == 6u );
            CHECK( layout._requiredSize == 12u );
        }
        SECTION( "R11G11B10 uploads describe the shared half-float source" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            descriptor._dataType = GFXDataFormat::FLOAT_16;
            descriptor._packing = GFXImagePacking::RGB_111110F;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 18u );
            CHECK( layout._rowStride == 20u );
            CHECK( layout._requiredSize == 38u );
            CHECK( layout._stagingSize == 36u );
        }
        SECTION( "1D uploads ignore row skips like OpenGL" )
        {
            descriptor._texType = TextureType::TEXTURE_1D;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 1u, 1u}, { ._rowLength = 8u, ._skipPixels = 2u, ._skipRows = 7u }, layout ) );
            CHECK( layout._sourceOffset == 8u );
            CHECK( layout._requiredSize == 20u );
        }
        SECTION( "BC blocks include incomplete edge blocks" )
        {
            descriptor._baseFormat = GFXImageFormat::BC1;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._rowBytes == 16u );
            CHECK( layout._rowCount == 2u );
            CHECK( layout._requiredSize == 64u );
            descriptor._baseFormat = GFXImageFormat::BC3;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._requiredSize == 128u );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {4u, 4u, 1u}, { ._rowLength = 8u }, layout ) );
        }
        SECTION( "invalid and overflowing pixel-store inputs are rejected" )
        {
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 3u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 4u, ._skipPixels = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._skipRows = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {0u, 2u, 1u}, {}, layout ) );
        }
    }

    TEST_CASE( "Texture update mip and layer bounds", "[texture_updates]" )
    {
        SECTION( "nonzero mip coordinates are already mip-local" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {13u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {14u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, ALL_MIPS, {}, {1u, 1u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 7u, {}, {1u, 1u, 1u} ) );
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 6u, {}, {1u, 1u, 1u} ) );
        }
        SECTION( "array layers do not shrink with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "cube z selects individual faces" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 11u}, {4u, 4u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 12u}, {4u, 4u, 1u} ) );
        }
        SECTION( "3D depth shrinks with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "1D arrays use z for layers" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 0u, 2u}, {4u, 1u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 1u, 2u}, {4u, 1u, 2u} ) );
        }
        SECTION( "zero extents and wrapped coordinates are invalid" )
        {
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {}, {1u, 0u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {65535u, 0u, 0u}, {2u, 1u, 1u} ) );
        }
    }

    struct VKAPITestAccessor
    {
        static VKTransferQueue& queue() noexcept { return VK_API::s_transferQueue; }
    };

    TEST_CASE("VKTransferQueue producer non-blocking and Flush drains", "[vk_transfer_queue]") {
        // Drain any leftover state
        VKTransferQueue::TransferRequest tmp{};

        while (VKAPITestAccessor::queue()._requests.try_dequeue(tmp))
        {
        }

        VKAPITestAccessor::queue()._dirty.store(false, std::memory_order_release);

        SECTION("single enqueue is non-blocking and visible")
        {
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE; // valid request form for this test

            VK_API::RegisterTransferRequest(req);

            // Immediately visible on the lock-free queue
            REQUIRE(VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }

        SECTION("Flush drains all queued requests and clears dirty")
        {
            // Enqueue multiple requests
            constexpr size_t N = 128;
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE;

            for (size_t i = 0; i < N; ++i)
            {
                VK_API::RegisterTransferRequest(req);
            }

            // Sanity: producers published work
            REQUIRE(VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));

            // Call the consumer (simulating the render thread). VK_NULL_HANDLE is acceptable for this unit test.
            VK_API::FlushBufferTransferRequests(VK_NULL_HANDLE);

            // After Flush, queue should be drained and dirty cleared.
            REQUIRE(!VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));
            REQUIRE(!VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }
    }
} //namespace Divide