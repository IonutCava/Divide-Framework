// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/WindowManager.h"
#include "Core/Headers/ErrorCodes.h"
#include "Core/Headers/RingBuffer.h"
#include "GUI/Headers/FontVertexFormat.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkSwapChain.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkRenderTarget.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderDataUploader.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/fontstash.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"

namespace Divide
{
    TEST_CASE( "Vulkan pipeline variants retain the complete rendering signature", "[vulkan][pipeline]" )
    {
        std::array<VkFormat, 2> colourFormats{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM };
        VkPipelineRenderingCreateInfo info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        info.colorAttachmentCount = to_U32( colourFormats.size() );
        info.pColorAttachmentFormats = colourFormats.data();

        const VKRenderingSignature base = VKRenderingSignature::From( info, 1u );
        CHECK( base._colourAttachmentCount == 2u );
        CHECK( base._colourFormats[0] == colourFormats[0] );
        CHECK( base._colourFormats[1] == colourFormats[1] );
        CHECK( base._depthFormat == VK_FORMAT_UNDEFINED );
        CHECK( base._stencilFormat == VK_FORMAT_UNDEFINED );

        info.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        const VKRenderingSignature depth = VKRenderingSignature::From( info, 1u );
        CHECK( depth != base );
        info.stencilAttachmentFormat = VK_FORMAT_S8_UINT;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        info.viewMask = 3u;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.viewMask = 0u;
        CHECK( VKRenderingSignature::From( info, 4u ) != depth );
        info.colorAttachmentCount = 1u;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.colorAttachmentCount = 2u;
        colourFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        CHECK( depth._colourFormats[0] == VK_FORMAT_R16G16B16A16_SFLOAT );

        struct CollidingHash
        {
            size_t operator()( const VKPipelineCacheKey& ) const noexcept { return 0u; }
        };
        hashMap<VKPipelineCacheKey, U32, CollidingHash> variants;
        const VKPipelineCacheKey colourOnly{ ._pipelineStateHash = 42u, ._rendering = base };
        const VKPipelineCacheKey withDepth{ ._pipelineStateHash = 42u, ._rendering = depth };
        variants[colourOnly] = 1u;
        variants[withDepth] = 2u;
        REQUIRE( variants.size() == 2u );
        CHECK( variants[colourOnly] == 1u );
        CHECK( variants[withDepth] == 2u );
        CHECK( VKPipelineCacheKeyHash{}( colourOnly ) == VKPipelineCacheKeyHash{}( colourOnly ) );
    }

    TEST_CASE( "Vulkan rebinds retained graphics pipelines when pass compatibility changes", "[vulkan][pipeline]" )
    {
        PipelineDescriptor descriptor{};
        descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        const Pipeline logicalPipeline{ descriptor };
        VKStateTracker tracker{};
        tracker._logicalPipeline = &logicalPipeline;
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        tracker._renderingSignature._sampleCount = 1u;
        tracker._boundRenderingSignature = tracker._renderingSignature;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );

        tracker._renderingSignature._depthFormat = VK_FORMAT_D32_SFLOAT;
        CHECK( tracker.needsGraphicsPipelineRebind() );
        tracker._boundRenderingSignature = tracker._renderingSignature;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
        tracker._renderingSignature._sampleCount = 4u;
        CHECK( tracker.needsGraphicsPipelineRebind() );
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        tracker._logicalPipeline = nullptr;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
    }

    TEST_CASE( "FontStash vertex format matches its uploaded vertices", "[vulkan][font]" )
    {
        const AttributeMap format = CreateFontVertexFormat();
        REQUIRE( format._vertexBindings.size() == 1u );
        CHECK( format._vertexBindings[0]._bufferBindIndex == 0u );
        CHECK( format._vertexBindings[0]._strideInBytes == sizeof( FONSvert ) );

        AttributeFlags shaderInputs{};
        shaderInputs[to_base( AttribLocation::POSITION )] = true;
        shaderInputs[to_base( AttribLocation::TEXCOORD )] = true;
        shaderInputs[to_base( AttribLocation::COLOR )] = true;
        const VertexInputDescription input = getVertexDescription( format, shaderInputs );
        REQUIRE( input.bindings.size() == 1u );
        CHECK( input.bindings[0].binding == 0u );
        CHECK( input.bindings[0].stride == sizeof( FONSvert ) );
        CHECK( input.bindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX );
        REQUIRE( input.attributes.size() == 3u );

        const auto checkAttribute = []( const VkVertexInputAttributeDescription& attribute,
                                        const AttribLocation location, const VkFormat type, const size_t offset )
        {
            CHECK( attribute.binding == 0u );
            CHECK( attribute.location == to_base( location ) );
            CHECK( attribute.format == type );
            CHECK( attribute.offset == offset );
        };
        checkAttribute( input.attributes[0], AttribLocation::POSITION, VK_FORMAT_R32G32_SFLOAT, offsetof( FONSvert, pos ) );
        checkAttribute( input.attributes[1], AttribLocation::TEXCOORD, VK_FORMAT_R32G32_SFLOAT, offsetof( FONSvert, tcoords ) );
        checkAttribute( input.attributes[2], AttribLocation::COLOR, VK_FORMAT_R8G8B8A8_UNORM, offsetof( FONSvert, colours ) );
    }

    TEST_CASE( "FontStash keeps the atlas only after successful creation", "[vulkan][font]" )
    {
        struct AtlasState
        {
            I32 _createResult = 1;
            I32 _resizeResult = 1;
            U32 _creates = 0u;
            U32 _resizes = 0u;
            U32 _deletes = 0u;
        } state;

        FONSparams params{};
        params.width = 32;
        params.height = 32;
        params.userPtr = &state;
        params.renderCreate = []( void* userPtr, int, int )
        {
            AtlasState& atlas = *static_cast<AtlasState*>( userPtr );
            ++atlas._creates;
            return atlas._createResult;
        };
        params.renderResize = []( void* userPtr, int, int )
        {
            AtlasState& atlas = *static_cast<AtlasState*>( userPtr );
            ++atlas._resizes;
            return atlas._resizeResult;
        };
        params.renderDelete = []( void* userPtr )
        {
            ++static_cast<AtlasState*>( userPtr )->_deletes;
        };

        SECTION( "A successfully created atlas remains usable until deletion" )
        {
            FONScontext* context = fonsCreateInternal( &params );
            REQUIRE( context != nullptr );
            CHECK( state._creates == 1u );
            CHECK( state._deletes == 0u );
            CHECK( fonsExpandAtlas( context, 64, 64 ) == 1 );
            CHECK( state._resizes == 1u );

            state._resizeResult = 0;
            CHECK( fonsExpandAtlas( context, 128, 128 ) == 0 );
            int width = 0;
            int height = 0;
            fonsGetAtlasSize( context, &width, &height );
            CHECK( width == 64 );
            CHECK( height == 64 );
            fonsDeleteInternal( context );
            CHECK( state._deletes == 1u );
        }

        SECTION( "A failed atlas creation returns no context and releases its resources" )
        {
            state._createResult = 0;
            CHECK( fonsCreateInternal( &params ) == nullptr );
            CHECK( state._creates == 1u );
            CHECK( state._deletes == 1u );
        }
    }

    TEST_CASE( "Vulkan attachment selections address cubemap faces and array layers", "[vulkan][render_target]" )
    {
        SubRange range{};
        for ( U16 cube = 0u; cube < 7u; ++cube )
        {
            for ( U8 face = 0u; face < 6u; ++face )
            {
                const DrawLayerEntry entry{ ._layer = { cube, 1u }, ._cubeFace = face };
                REQUIRE( vkRenderTarget::GetAttachmentLayerRange( entry, 7u, true, range ) );
                CHECK( range._offset == cube * 6u + face );
                CHECK( range._count == 1u );
            }
        }

        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, ALL_LAYERS } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 0u, 42u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, ALL_LAYERS } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 12u, 30u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, 3u } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 12u, 18u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, ALL_LAYERS } }, 7u, false, range ) );
        CHECK( (range == SubRange{ 2u, 5u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 6u, 1u } }, 7u, false, range ) );
        CHECK( (range == SubRange{ 6u, 1u }) );

        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 7u, 1u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 6u, 2u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 0u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 1u }, ._cubeFace = 6u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 2u }, ._cubeFace = 1u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, ALL_LAYERS }, ._cubeFace = 1u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 1u }, ._cubeFace = 1u }, 7u, false, range ) );
    }

    TEST_CASE( "Texture views preserve cube and array layer ranges", "[vulkan][texture]" )
    {
        SubRange range{};
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 7u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                          { 2u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 2u, 5u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_MAP, 7u,
                                          { 6u, 1u }, range ) );
        CHECK( (range == SubRange{ 6u, 1u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D_ARRAY, 7u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 42u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D, 7u,
                                          { 37u, 1u }, range ) );
        CHECK( (range == SubRange{ 37u, 1u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_2D_ARRAY, 8u,
                                          { 3u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 3u, 5u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_3D, TextureType::TEXTURE_3D, 1u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 1u }) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                                { 7u, 1u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D_ARRAY, 7u,
                                                { 41u, 2u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D, 7u,
                                                { 0u, 2u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_2D_ARRAY, 8u,
                                                { 0u, 0u }, range ) );
    }

    TEST_CASE( "Vulkan view barriers use the view's layer units", "[vulkan][texture]" )
    {
        ImageSubRange input{ ._mipLevels = { 1u, 2u }, ._layerRange = { 2u, 3u } };
        VkImageSubresourceRange range = vkTexture::GetViewSubresourceRange(
            TextureType::TEXTURE_CUBE_ARRAY, input, VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.baseMipLevel == 1u );
        CHECK( range.levelCount == 2u );
        CHECK( range.baseArrayLayer == 12u );
        CHECK( range.layerCount == 18u );

        input._mipLevels = { 0u, ALL_MIPS };
        input._layerRange = { 37u, 1u };
        range = vkTexture::GetViewSubresourceRange( TextureType::TEXTURE_2D_ARRAY, input, VK_IMAGE_ASPECT_DEPTH_BIT );
        CHECK( range.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT );
        CHECK( range.levelCount == VK_REMAINING_MIP_LEVELS );
        CHECK( range.baseArrayLayer == 37u );
        CHECK( range.layerCount == 1u );

        input._layerRange = { 2u, ALL_LAYERS };
        range = vkTexture::GetViewSubresourceRange( TextureType::TEXTURE_CUBE_ARRAY, input, VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.baseArrayLayer == 12u );
        CHECK( range.layerCount == VK_REMAINING_ARRAY_LAYERS );
    }

    TEST_CASE( "Vulkan retained attachments transition only their rendered mip", "[vulkan][render_target]" )
    {
        using Transition = vkTexture::TransitionType;
        for ( const U16 mip : { U16{ 0u }, U16{ 1u }, U16{ 5u } } )
        {
            for ( const bool depth : { false, true } )
            {
                CAPTURE( mip, depth );
                const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageSubresourceRange range = vkRenderTarget::GetAttachmentTransitionRange( aspect, mip );
                CHECK( range.aspectMask == aspect );
                CHECK( range.baseMipLevel == mip );
                CHECK( range.levelCount == 1u );
                CHECK( range.baseArrayLayer == 0u );
                CHECK( range.layerCount == VK_REMAINING_ARRAY_LAYERS );

                VkImageMemoryBarrier2 toAttachment{};
                VkImageMemoryBarrier2 toRead{};
                vkTexture::TransitionTexture( depth ? Transition::SHADER_READ_TO_DEPTH_ATTACHMENT : Transition::SHADER_READ_TO_COLOUR_ATTACHMENT,
                                              range, {}, toAttachment );
                vkTexture::TransitionTexture( depth ? Transition::DEPTH_ATTACHMENT_TO_SHADER_READ : Transition::COLOUR_ATTACHMENT_TO_SHADER_READ,
                                              range, {}, toRead );
                CHECK( toAttachment.oldLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( toAttachment.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                CHECK( toRead.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                CHECK( toRead.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( toRead.subresourceRange.baseMipLevel == mip );
                CHECK( toRead.subresourceRange.levelCount == 1u );
                CHECK( toRead.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS );
            }
        }
    }

    TEST_CASE( "Immediate submission failures stop later work until reinitialization", "[render_smoke][submission]" )
    {
        VKImmediateSubmitStatus status{};
        CHECK_FALSE( status.failed() );
        CHECK( status.record( VK_SUCCESS ) );
        CHECK_FALSE( status.failed() );
        CHECK_FALSE( status.record( VK_ERROR_OUT_OF_HOST_MEMORY ) );
        CHECK( status.failed() );
        CHECK( status.record( VK_SUCCESS ) );
        CHECK( status.failed() );
        status.reset();
        CHECK_FALSE( status.failed() );
        CHECK_FALSE( status.record( VK_ERROR_DEVICE_LOST ) );
        CHECK( status.failed() );
    }

    TEST_CASE( "Per-node dirty ranges remain pending independently in every ring slot", "[render_smoke][per_node]" )
    {
        using Range = RenderPassExecutor::BufferUpdateRange;
        std::array<Range, 4> slots{};
        Range pending{ 0u, 9u };

        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 0u, 9u }) );
        CHECK( pending.range() == 0u );
        CHECK( slots[1].range() == 0u );
        CHECK( (slots[0] == Range{ 0u, 9u }) );
        CHECK( (slots[2] == Range{ 0u, 9u }) );
        CHECK( (slots[3] == Range{ 0u, 9u }) );

        pending = { 20u, 23u };
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 20u, 23u }) );
        CHECK( (slots[0] == Range{ 0u, 23u }) );
        CHECK( (slots[2] == Range{ 0u, 23u }) );
        CHECK( (slots[3] == Range{ 0u, 23u }) );
        CHECK( RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ).range() == 0u );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 2u ) == Range{ 0u, 23u }) );

        pending = { 4u, 5u };
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 2u ) == Range{ 4u, 5u }) );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 0u ) == Range{ 0u, 23u }) );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 4u, 5u }) );
    }

    TEST_CASE( "Per-node upload starts align without dropping the dirty range", "[render_smoke][per_node]" )
    {
        using Range = RenderPassExecutor::BufferUpdateRange;
        Range aligned{ 5u, 7u };
        aligned.alignStart( 12u, 16u );
        CHECK( (aligned == Range{ 4u, 7u }) );
        CHECK( aligned.range() == 4u );

        aligned = { 7u, 8u };
        aligned.alignStart( 128u, 256u );
        CHECK( (aligned == Range{ 6u, 8u }) );
        aligned = { 3u, 3u };
        aligned.alignStart( 4u, 256u );
        CHECK( (aligned == Range{ 0u, 3u }) );
        aligned = { 0u, 3u };
        aligned.alignStart( 4u, 256u );
        CHECK( (aligned == Range{ 0u, 3u }) );
    }

    TEST_CASE( "Per-node player slots outlast Vulkan's bounded in-flight frames", "[render_smoke][per_node]" )
    {
        CHECK( VKSwapChain::FrameSlotCount() == Config::MAX_FRAMES_IN_FLIGHT );
        for ( U64 frame = 0u; frame < 20u; ++frame )
        {
            CHECK( VKSwapChain::FrameSlotIndex( frame ) == frame % Config::MAX_FRAMES_IN_FLIGHT );
        }
        CHECK( RenderPassExecutor::BufferQueueLengthForPlayers( 0u ) ==
               RenderPassExecutor::BufferQueueLengthForPlayers( 1u ) );

        for ( U8 players = 1u; players <= Config::MAX_LOCAL_PLAYER_COUNT; ++players )
        {
            const U16 length = RenderPassExecutor::BufferQueueLengthForPlayers( players );
            RingBuffer ring{ length };
            vector<I32> lastUsedFrame( length, -1 );
            for ( I32 frame = 0; frame < 20; ++frame )
            {
                for ( U8 player = 0u; player < players; ++player )
                {
                    ring.incQueue();
                    const U16 slot = ring.queueIndex();
                    if ( lastUsedFrame[slot] >= 0 )
                    {
                        CHECK( frame - lastUsedFrame[slot] >= Config::MAX_FRAMES_IN_FLIGHT );
                    }
                    lastUsedFrame[slot] = frame;
                }
            }
        }
    }

    TEST_CASE( "Vulkan rendering attachments discard resolve state between passes", "[render_smoke][render_target]" )
    {
        VkRenderingAttachmentInfo info{};
        vkRenderTarget::ResetAttachmentInfo( info );
        info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        info.resolveImageView = reinterpret_cast<VkImageView>( &info );
        info.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        vkRenderTarget::ResetAttachmentInfo( info );
        CHECK( info.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO );
        CHECK( info.imageLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
        CHECK( info.resolveMode == VK_RESOLVE_MODE_NONE );
        CHECK( info.resolveImageView == VK_NULL_HANDLE );
        CHECK( info.resolveImageLayout == VK_IMAGE_LAYOUT_UNDEFINED );
        CHECK( info.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR );
    }

    TEST_CASE( "Vulkan resolve targets return to shader-read after a pass", "[render_smoke][render_target]" )
    {
        CHECK( vkRenderTarget::ResolveTargetUsage( true ) == RTUsageTracker::Layout::ATTACHMENT );
        CHECK( vkRenderTarget::ResolveTargetUsage( false ) == RTUsageTracker::Layout::SHADER_READ );

        for ( const bool depth : { false, true } )
        {
            VkImageSubresourceRange range
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0u,
                .levelCount = 1u,
                .baseArrayLayer = 0u,
                .layerCount = 1u
            };
            if ( depth )
            {
                range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            VkImageMemoryBarrier2 endBarrier{};
            vkTexture::TransitionTexture( depth ? vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ
                                                : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ,
                                          range, { ._name = "resolve", ._isResolveImage = true }, endBarrier );
            CHECK( endBarrier.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
            CHECK( endBarrier.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );

            VkImageMemoryBarrier2 beginBarrier{};
            vkTexture::TransitionTexture( depth ? vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT
                                                : vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT,
                                          range, { ._name = "resolve", ._isResolveImage = true }, beginBarrier );
            CHECK( beginBarrier.oldLayout == endBarrier.newLayout );
            CHECK( beginBarrier.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
        }
    }

    TEST_CASE( "Vulkan shader-read transitions do not infer layout from resolve usage", "[render_smoke][image_barrier]" )
    {
        using Transition = vkTexture::TransitionType;
        struct TransitionCase
        {
            Transition _type;
            VkImageLayout _oldLayout;
            VkImageLayout _newLayout;
        };
        constexpr std::array cases
        {
            TransitionCase{ Transition::UNDEFINED_TO_SHADER_READ, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_SHADER_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL },
            TransitionCase{ Transition::SHADER_READ_TO_SHADER_READ_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL },
            TransitionCase{ Transition::SHADER_READ_TO_BLIT_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_BLIT_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
            TransitionCase{ Transition::BLIT_READ_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::BLIT_WRITE_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_COPY_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_COPY_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
            TransitionCase{ Transition::COPY_READ_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::COPY_WRITE_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }
        };
        constexpr VkImageSubresourceRange range
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 2u,
            .levelCount = 1u,
            .baseArrayLayer = 3u,
            .layerCount = 1u
        };
        for ( const TransitionCase& test : cases )
        {
            INFO( vkTexture::Names::transitionType[to_base( test._type )] );
            for ( const bool isResolve : { false, true } )
            {
                INFO( isResolve );
                VkImageMemoryBarrier2 barrier{};
                vkTexture::TransitionTexture( test._type, range, { ._name = "image", ._isResolveImage = isResolve }, barrier );
                CHECK( barrier.oldLayout == test._oldLayout );
                CHECK( barrier.newLayout == test._newLayout );
                CHECK( barrier.subresourceRange.baseMipLevel == range.baseMipLevel );
                CHECK( barrier.subresourceRange.baseArrayLayer == range.baseArrayLayer );
                if ( test._oldLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL )
                {
                    CHECK( ( barrier.srcAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
                }
                if ( test._newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL )
                {
                    CHECK( ( barrier.dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
                }
            }
        }
    }

    TEST_CASE( "Vulkan initializes sampleable attachments in shader-read layout", "[render_smoke][image_barrier]" )
    {
        TextureDescriptor descriptor{};
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );

        AddImageUsageFlag( descriptor, ImageUsage::RT_COLOUR_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        AddImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_RESOLVE_TARGET );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_COLOUR_ATTACHMENT );

        RemoveImageUsageFlag( descriptor, ImageUsage::RT_COLOUR_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_DEPTH_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_RESOLVE_TARGET );
        AddImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
    }

    TEST_CASE( "Vulkan initializes every sampled attachment subresource before shader reads", "[render_smoke][image_barrier]" )
    {
        TextureDescriptor descriptor{};
        for ( const GFXImagePacking packing : { GFXImagePacking::NORMALIZED, GFXImagePacking::DEPTH, GFXImagePacking::DEPTH_STENCIL } )
        {
            descriptor._packing = packing;
            const vkTexture::InitialClearPlan plan = vkTexture::MakeInitialClearPlan( descriptor );
            CHECK( plan._range.aspectMask == vkTexture::GetAspectFlags( descriptor ) );
            CHECK( plan._range.baseMipLevel == 0u );
            CHECK( plan._range.levelCount == VK_REMAINING_MIP_LEVELS );
            CHECK( plan._range.baseArrayLayer == 0u );
            CHECK( plan._range.layerCount == VK_REMAINING_ARRAY_LAYERS );
            CHECK( plan._isDepthStencil == ( packing != GFXImagePacking::NORMALIZED ) );
            CHECK( plan._depthStencil.depth == 1.f );
            CHECK( plan._depthStencil.stencil == 0u );
            CHECK( plan._colour.float32[0] == 0.f );
            CHECK( plan._colour.float32[1] == 0.f );
            CHECK( plan._colour.float32[2] == 0.f );
            CHECK( plan._colour.float32[3] == 0.f );

            for ( const bool isResolve : { false, true } )
            {
                VkImageMemoryBarrier2 before{};
                VkImageMemoryBarrier2 after{};
                vkTexture::TransitionTexture( vkTexture::TransitionType::UNDEFINED_TO_COPY_WRITE, plan._range,
                                              { ._name = "initial clear", ._isResolveImage = isResolve }, before );
                vkTexture::TransitionTexture( vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ, plan._range,
                                              { ._name = "initial clear", ._isResolveImage = isResolve }, after );
                CHECK( before.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED );
                CHECK( before.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
                CHECK( ( before.dstAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT ) != 0u );
                CHECK( after.oldLayout == before.newLayout );
                CHECK( after.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( ( after.srcAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT ) != 0u );
                CHECK( ( after.dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
            }
        }
    }

    TEST_CASE( "Render debug scopes clear the active entry without exceeding the stack", "[render_smoke][debug_scope]" )
    {
        GFXDevice::ClearDebugMessages();
        SCOPE_EXIT
        {
            GFXDevice::ClearDebugMessages();
        };

        for ( U32 i = 0u; i < Config::MAX_DEBUG_SCOPE_DEPTH; ++i )
        {
            GFXDevice::PushDebugMessage( "scope", i );
        }
        REQUIRE( GFXDevice::s_debugScopeDepth == Config::MAX_DEBUG_SCOPE_DEPTH );

        for ( U32 i = Config::MAX_DEBUG_SCOPE_DEPTH; i > 0u; --i )
        {
            CHECK( GFXDevice::s_debugScope[i - 1u]._id == i - 1u );
            GFXDevice::PopDebugMessage();
            CHECK( GFXDevice::s_debugScopeDepth == i - 1u );
            CHECK( GFXDevice::s_debugScope[i - 1u]._name == nullptr );
            CHECK( GFXDevice::s_debugScope[i - 1u]._id == U32_MAX );
        }
    }

    TEST_CASE( "Vulkan images allow VMA to choose dedicated or pooled memory", "[render_smoke][allocation]" )
    {
        const VmaAllocationCreateInfo info = vkTexture::ImageAllocationInfo();
        CHECK( info.usage == VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE );
        CHECK( ( info.flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT ) == 0u );
        CHECK( info.priority == 1.f );
    }

    TEST_CASE( "Compute workgroup support treats device limits as inclusive maxima", "[compute][device_limits]" )
    {
        DeviceInformation info{};
        info._maxWorkgroupSize[0] = 256u;
        info._maxWorkgroupSize[1] = 128u;
        info._maxWorkgroupSize[2] = 64u;
        info._maxWorkgroupInvocations = 256u;

        CHECK( SupportsComputeWorkgroup( info, 1u, 1u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 256u, 1u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 16u, 16u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 2u, 2u, 64u ) );

        CHECK_FALSE( SupportsComputeWorkgroup( info, 0u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 0u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 1u, 0u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 257u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 129u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 1u, 65u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 32u, 32u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 16u, 8u, 4u ) );

        // The product is computed in 64 bits so huge per-axis limits cannot wrap the invocation check.
        info._maxWorkgroupSize[0] = info._maxWorkgroupSize[1] = info._maxWorkgroupSize[2] = U32_MAX;
        info._maxWorkgroupInvocations = 1024u;
        CHECK_FALSE( SupportsComputeWorkgroup( info, 65536u, 65536u, 1u ) );

        // Vulkan's guaranteed minimum limits.
        DeviceInformation vkMinimum{};
        vkMinimum._maxWorkgroupSize[0] = 128u;
        vkMinimum._maxWorkgroupSize[1] = 128u;
        vkMinimum._maxWorkgroupSize[2] = 64u;
        vkMinimum._maxWorkgroupInvocations = 128u;
        CHECK( SupportsComputeWorkgroup( vkMinimum, 8u, 8u, 1u ) );
        CHECK( SupportsComputeWorkgroup( vkMinimum, 64u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( vkMinimum, 16u, 16u, 1u ) );
    }

    TEST_CASE( "Instanced draws agree with material indirection requirements", "[instancing][indirection]" )
    {
        for ( const U32 instanceCount : { 0u, 1u, 2u, 64u } )
        {
            const bool drawRequiresInstancing = instanceCount > 1u;
            const bool nonInstancedMaterial = RequiresInstancedIndirection( false, false, instanceCount );
            CHECK( nonInstancedMaterial == drawRequiresInstancing );
            CHECK( IndirectionBaseInstance( nonInstancedMaterial, 37u ) == (drawRequiresInstancing ? 0u : 38u) );

            const bool instancedMaterial = RequiresInstancedIndirection( false, true, instanceCount );
            CHECK( instancedMaterial );
            CHECK( IndirectionBaseInstance( instancedMaterial, 37u ) == 0u );
            CHECK( RequiresInstancedIndirection( true, false, instanceCount ) );
        }

        CHECK( IndirectionBaseInstance( false, 0u ) == 1u );
    }

    TEST_CASE( "Light descriptor visibility names match their shader stages", "[descriptors][lighting]" )
    {
        constexpr auto vertex = to_base( ShaderStageVisibility::VERTEX );
        constexpr auto fragment = to_base( ShaderStageVisibility::FRAGMENT );
        constexpr auto compute = to_base( ShaderStageVisibility::COMPUTE );
        constexpr auto fragmentCompute = to_base( ShaderStageVisibility::COMPUTE_AND_FRAGMENT );
        constexpr auto lightBuffer = to_base( ShaderStageVisibility::VERTEX_FRAGMENT_COMPUTE );

        CHECK( fragmentCompute == (fragment | compute) );
        CHECK( (fragmentCompute & vertex) == 0u );
        CHECK( lightBuffer == (vertex | fragment | compute) );
        CHECK( (lightBuffer & to_base( ShaderStageVisibility::GEOMETRY )) == 0u );
        CHECK( to_base( ShaderStageVisibility::COMPUTE_AND_DRAW ) == fragmentCompute );
    }

    struct WindowManagerTestAccess
    {
        static ErrorCode ApplyContextSettings(WindowManager& manager, const bool debug)
        {
            manager._apiSettings._createDebugContext = debug;
            return manager.applyAPISettingsPreCreate(0u, RenderAPI::OpenGL);
        }
    };

    TEST_CASE( "OpenGL debug retries clear no-error context attributes", "[window_context]" )
    {
        platformInitRunListener::PlatformInit();
        const char* previousDriverHint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
        const std::string previousDriver = previousDriverHint == nullptr ? "" : previousDriverHint;
        const bool hadDriverHint = previousDriverHint != nullptr;
        REQUIRE( SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy") );
        SCOPE_EXIT
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            if (hadDriverHint)
            {
                SDL_SetHint(SDL_HINT_VIDEO_DRIVER, previousDriver.c_str());
            }
            else
            {
                SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
            }
        };
        REQUIRE( SDL_InitSubSystem(SDL_INIT_VIDEO) );
        WindowManager manager;
        for (const bool debug : { false, true, false, true })
        {
            REQUIRE( WindowManagerTestAccess::ApplyContextSettings(manager, debug) == ErrorCode::NO_ERR );
            int noError = -1;
            int flags = 0;
            REQUIRE( SDL_GL_GetAttribute(SDL_GL_CONTEXT_NO_ERROR, &noError) );
            REQUIRE( SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags) );
            CHECK( noError == (debug ? 0 : 1) );
            CHECK( ((flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0) == debug );
        }
    }

    TEST_CASE( "Debug previews explicitly reset splash linearisation state", "[texture_fallbacks]" )
    {
        DebugView view;
        const auto& entries = view._shaderData.entries();
        const auto entry = std::find_if( entries.begin(), entries.end(), [](const auto& value)
        {
            return value._bindingHash == _ID( "skipLinearisation" );
        } );
        REQUIRE( entry != entries.end() );
        CHECK( entry->_type == PushConstantType::UINT );
        U32 value = 1u;
        std::memcpy( &value, view._shaderData.data( entry->_range._startOffset ), sizeof(value) );
        CHECK( value == 0u );
    }

    TEST_CASE( "Debug previews reset numerical display options", "[texture_fallbacks]" )
    {
        DebugView view;
        const auto& entries = view._shaderData.entries();
        for (const U64 hash : { _ID( "channelsArePacked" ), _ID( "scaleAndBias" ), _ID( "normalizeOutput" ) })
        {
            const auto entry = std::find_if( entries.begin(), entries.end(), [hash](const auto& value)
            {
                return value._bindingHash == hash;
            } );
            REQUIRE( entry != entries.end() );
            CHECK( entry->_type == PushConstantType::UINT );
            U32 value = 1u;
            std::memcpy( &value, view._shaderData.data( entry->_range._startOffset ), sizeof(value) );
            CHECK( value == 0u );
        }
    }

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
        const std::array<F32, 2> incompatible{{oldValue, newValue}};
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
