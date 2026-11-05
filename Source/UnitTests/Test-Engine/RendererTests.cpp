// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/WindowManager.h"
#include "Core/Headers/ErrorCodes.h"
#include "Core/Headers/RingBuffer.h"
#include "GUI/Headers/FontVertexFormat.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkSwapChain.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkToolProperties.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkRenderTarget.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderDataUploader.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/BlurPass.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/fontstash.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSAOState.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"
#include "../../../Assets/Shaders/GLSL/Common/ssaoResampling.cmn"
#include "../../../Assets/Shaders/GLSL/Common/hizConservative.cmn"

namespace Divide
{
    TEST_CASE("Shadow invalidation selects layers without hiding full cache changes", "[renderer][shadow]")
    {
        CHECK(ShadowLayerUpdateMask(4u, 1u, true, false) == 1u);
        CHECK(ShadowLayerUpdateMask(4u, 0b1010u, true, false) == 0b1010u);
        CHECK(ShadowLayerUpdateMask(4u, 1u | 4u, true, false) == 5u);
        CHECK(ShadowLayerUpdateMask(4u, 0u, true, false) == 0u);
        CHECK(ShadowLayerUpdateMask(4u, 1u, false, false) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 1u, true, true) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 0u, false, false) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 0b10000u, true, false) == 0u);
        CHECK(ShadowLayerUpdateMask(1u, 1u, true, false) == 1u);
        CHECK(ShadowLayerUpdateMask(0u, U32_MAX, false, true) == 0u);
        CHECK(ShadowLayerMask(32u) == U32_MAX);
        CHECK(ShadowLayerSelected(U32_MAX, 31u));
        CHECK_FALSE(ShadowLayerSelected(U32_MAX, 32u));
    }

    TEST_CASE("Shadow blur and mip ranges cover only selected layers", "[renderer][shadow]")
    {
        for (U32 mask = 0u; mask < 16u; ++mask)
        {
            CAPTURE(mask);
            U32 visited = 0u;
            U8 previousEnd = 0u;
            bool firstRange = true;
            ForEachShadowLayerRange(mask, 4u, [&](const U8 first, const U8 count)
            {
                CHECK(count > 0u);
                CHECK(first + count <= 4u);
                CHECK((firstRange || first > previousEnd));
                firstRange = false;
                previousEnd = first + count;
                for (U8 layer = first; layer < first + count; ++layer)
                {
                    CHECK(ShadowLayerSelected(mask, layer));
                    CHECK_FALSE(ShadowLayerSelected(visited, layer));
                    visited |= U32{1u} << layer;
                }
            });
            CHECK(visited == mask);
        }
        for (U8 first = 0u; first < 4u; ++first)
        {
            for (U8 relative = 0u; relative < 4u - first; ++relative)
            {
                CHECK(ShadowBlurTexelScale(first) * ShadowBlurTexelScale(relative) ==
                      ShadowBlurTexelScale(to_U8(first + relative)));
            }
        }
        CHECK(ShadowBlurTexelScale(0u) == 1.f);
        CHECK(ShadowBlurTexelScale(3u) == 0.125f);
        U32 calls = 0u;
        ForEachShadowLayerRange(U32_MAX, 32u, [&](const U8 first, const U8 count)
        {
            CHECK(first == 0u);
            CHECK(count == 32u);
            ++calls;
        });
        CHECK(calls == 1u);
        ForEachShadowLayerRange(U32_MAX, 0u, [&](U8, U8) { FAIL("Empty layer sets must not schedule work"); });
    }

    TEST_CASE("Shadow invalidation filters light type and caller supplied bounds", "[renderer][shadow]")
    {
        const BoundingSphere source{float3{0.f}, 500.f};
        const BoundingSphere influence{float3{0.f}, 100.f};
        LightPool::MovingVolume request{source, false, 1u, LightType::POINT, influence};
        CHECK(request.affectsLight(LightType::POINT, BoundingSphere{float3{109.f, 0.f, 0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::POINT, BoundingSphere{float3{111.f, 0.f, 0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::SPOT, BoundingSphere{float3{0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::DIRECTIONAL, BoundingSphere{float3{0.f}, 10.f}));
        request._volume = BoundingSphere{float3{1000.f}, 10.f};
        CHECK_FALSE(request.affectsLight(LightType::POINT, BoundingSphere{float3{0.f}, 10.f}));
        request._lightType = LightType::DIRECTIONAL;
        request._influenceBounds.reset();
        CHECK(request.affectsLight(LightType::DIRECTIONAL, BoundingSphere{float3{0.f}, 10.f}));
        const LightPool::MovingVolume geometryChange{source, true};
        CHECK(geometryChange._layerMask == 0u);
        CHECK(geometryChange.affectsLight(LightType::POINT, BoundingSphere{float3{0.f}, 10.f}));
        CHECK(geometryChange.affectsLight(LightType::SPOT, BoundingSphere{float3{0.f}, 10.f}));
    }

    struct ParticleEmitterTestAccess
    {
        static auto& Upload(ParticleEmitter& emitter, const U64 frame, const float3& eye)
        {
            return emitter.getUpload(frame, eye);
        }
    };

    TEST_CASE("Particle uploads retain every view until its frame slot is recycled", "[renderer][particles]")
    {
        platformInitRunListener::PlatformInit();
        ParticleEmitter emitter{ ResourceDescriptor<ParticleEmitter>{ "particle_upload_lifetime" } };
        auto& first = ParticleEmitterTestAccess::Upload(emitter, 0u, float3{0.f});
        first._uploaded = true;
        first._handles[1] = { 7u, 1u };
        GenericDrawCommand& command = first._commands.emplace_back();
        command._sourceBuffers = first._handles.data();
        command._sourceBuffersCount = to_U16(first._handles.size());
        command._cmd.instanceCount = 17u;
        const GenericDrawCommand queued = command;

        // More views than the old three-slot ring and four-player modulo.
        for (U32 i = 1u; i < 32u; ++i)
        {
            auto& other = ParticleEmitterTestAccess::Upload(emitter, 0u, float3{to_F32(i), 0.f, 0.f});
            CHECK(&other != &first);
            other._handles[1] = { to_U16(i + 8u), 1u };
            CHECK(queued._sourceBuffers[1] == GPUBuffer::Handle{7u, 1u});
        }
        CHECK(&ParticleEmitterTestAccess::Upload(emitter, 0u, float3{0.f}) == &first);
        CHECK(first._uploaded);
        CHECK(queued._cmd.instanceCount == 17u);

        for (U64 frame = 1u; frame < Config::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            auto& inFlight = ParticleEmitterTestAccess::Upload(emitter, frame, float3{0.f});
            CHECK(&inFlight != &first);
            CHECK(first._uploaded);
        }
        auto& recycled = ParticleEmitterTestAccess::Upload(emitter, Config::MAX_FRAMES_IN_FLIGHT, float3{0.f});
        CHECK(&recycled == &first);
        CHECK_FALSE(recycled._uploaded);
    }

    TEST_CASE("Render packages retain immutable per-view particle commands", "[renderer][particles]")
    {
        GenericDrawCommandContainer first;
        first.emplace_back()._cmd.instanceCount = 2u;
        GenericDrawCommandContainer second;
        second.emplace_back()._cmd.instanceCount = 9u;
        RenderPackage firstPackage;
        RenderPackage secondPackage;
        firstPackage.drawCommandsOverride(&first);
        secondPackage.drawCommandsOverride(&second);
        GFX::DrawCommand queued;
        queued._drawCommands = *firstPackage.drawCommandsOverride();
        firstPackage.drawCommandsOverride(&second);
        CHECK(queued._drawCommands.front()._cmd.instanceCount == 2u);
        CHECK(secondPackage.drawCommandsOverride()->front()._cmd.instanceCount == 9u);
        Clear(firstPackage);
        CHECK(firstPackage.drawCommandsOverride() == nullptr);
    }

    template<typename T>
    concept UniformScalarCanBeSet = requires( UniformData& uniforms, const T& value )
    {
        uniforms.set( 1u, PushConstantType::VEC4, value );
    };

    static_assert( UniformScalarCanBeSet<float4> );
    static_assert( UniformScalarCanBeSet<std::array<float4, 2>> );
    static_assert( !UniformScalarCanBeSet<vector<float4>> );
    static_assert( !UniformScalarCanBeSet<std::vector<float4>> );
    static_assert( sizeof(float4) == 4u * sizeof(F32) );

    TEST_CASE( "SSAO preserves each configured blur kernel size", "[renderer][ssao]" )
    {
        Configuration::SSAOSettings fullRes;
        Configuration::SSAOSettings halfRes;
        fullRes.BlurKernelSize = 0;
        halfRes.BlurKernelSize = 4;

        const std::array<I32, 2> sizes = SSAO::ConfiguredBlurKernelSizes( fullRes, halfRes );
        CHECK( sizes[0] == 0 );
        CHECK( sizes[1] == 4 );
    }

    TEST_CASE( "SSAO resizes all full- and half-resolution targets together", "[renderer][ssao]" )
    {
        struct TestTarget
        {
            vec2<U16> _resolution{};
            U32 _resizeCount = 0u;

            void resize( const U16 width, const U16 height )
            {
                _resolution.set( width, height );
                ++_resizeCount;
            }
        };

        TestTarget output;
        TestTarget blur;
        TestTarget halfOutput;
        TestTarget halfDepthAndNormals;

        const auto checkTargets = [&]( const vec2<U16> resolution, const vec2<U16> halfResolution, const U32 count )
        {
            CHECK( output._resolution.width == resolution.width );
            CHECK( output._resolution.height == resolution.height );
            CHECK( blur._resolution.width == resolution.width );
            CHECK( blur._resolution.height == resolution.height );
            CHECK( halfOutput._resolution.width == halfResolution.width );
            CHECK( halfOutput._resolution.height == halfResolution.height );
            CHECK( halfDepthAndNormals._resolution.width == halfResolution.width );
            CHECK( halfDepthAndNormals._resolution.height == halfResolution.height );
            CHECK( output._resizeCount == count );
            CHECK( blur._resizeCount == count );
            CHECK( halfOutput._resizeCount == count );
            CHECK( halfDepthAndNormals._resizeCount == count );
        };

        SSAO::ResizeTargets( { 1920u, 1080u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1920u, 1080u }, { 960u, 540u }, 1u );

        SSAO::ResizeTargets( { 1279u, 719u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1279u, 719u }, { 640u, 360u }, 2u );

        SSAO::ResizeTargets( { 1u, 1u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1u, 1u }, { 1u, 1u }, 3u );
    }

    TEST_CASE( "Half-resolution SSAO downsampling covers each source texel", "[renderer][shader][ssao]" )
    {
        using std::min;

        for ( const U16 size : std::array<U16, 6>{ { 1u, 2u, 3u, 5u, 1279u, 65535u } } )
        {
            CAPTURE( size );
            const I32 sourceSize = size;
            const I32 targetSize = SSAO::HalfResolution( { size, size } ).width;
            std::vector<bool> covered( size, false );
            for ( I32 pixel = 0; pixel < targetSize; ++pixel )
            {
                for ( const I32 offset : { 0, 1 } )
                {
                    const I32 source = SSAO_DOWNSAMPLE_SOURCE_PIXEL( pixel, offset, sourceSize - 1 );
                    REQUIRE( source >= 0 );
                    REQUIRE( source < sourceSize );
                    CHECK( source == min( pixel * 2 + offset, sourceSize - 1 ) );
                    covered[to_size( source )] = true;
                }
            }
            CHECK( std::ranges::all_of( covered, []( const bool value ) { return value; } ) );
        }
    }

    TEST_CASE( "Half-resolution SSAO upsampling follows the bilinear footprint", "[renderer][shader][ssao]" )
    {
        using std::clamp;
        using std::floor;
        using std::max;

        const auto sourcePixels = []( const I32 fullPixel, const I32 fullSize, const I32 halfSize )
        {
            const F32 uv = ( to_F32( fullPixel ) + 0.5f ) / to_F32( fullSize );
            const F32 halfPosition = SSAO_HALF_GRID_POSITION( uv * to_F32( fullSize ), to_F32( fullSize ), to_F32( fullSize & 1 ) );
            const I32 base = to_I32( floor( halfPosition ) );
            return std::array<I32, 2>{ {
                SSAO_CLAMP_SOURCE_PIXEL( base, 0, 0, halfSize - 1 ),
                SSAO_CLAMP_SOURCE_PIXEL( base, 1, 0, halfSize - 1 )
            } };
        };

        CHECK( ( sourcePixels( 0, 1, 1 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 0, 4, 2 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 1, 4, 2 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 2, 4, 2 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 3, 4, 2 ) == std::array<I32, 2>{ { 1, 1 } } ) );
        CHECK( ( sourcePixels( 0, 5, 3 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 1, 5, 3 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 2, 5, 3 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 3, 5, 3 ) == std::array<I32, 2>{ { 1, 2 } } ) );
        CHECK( ( sourcePixels( 4, 5, 3 ) == std::array<I32, 2>{ { 2, 2 } } ) );
    }

    TEST_CASE( "Half-resolution SSAO reconstructs the selected source pixel without stretching odd edges", "[renderer][shader][ssao]" )
    {
        using std::floor;
        using std::max;
        using std::min;

        for ( const I32 fullSize : { 1, 2, 3, 4, 5, 719, 1279, 1920, 65535 } )
        {
            CAPTURE( fullSize );
            const I32 halfSize = (fullSize + 1) / 2;
            for ( I32 halfPixel = 0; halfPixel < halfSize; ++halfPixel )
            {
                const F32 blockCenter = min( to_F32( halfPixel * 2 + 1 ), to_F32( fullSize ) - 0.5f );
                const F32 halfPosition = SSAO_HALF_GRID_POSITION( blockCenter, to_F32( fullSize ), to_F32( fullSize & 1 ) );
                REQUIRE( halfPosition == Catch::Approx( to_F32( halfPixel ) ).margin( 0.00001f ) );

                for ( const I32 offset : { 0, 1 } )
                {
                    const I32 sourcePixel = SSAO_DOWNSAMPLE_SOURCE_PIXEL( halfPixel, offset, fullSize - 1 );
                    const F32 uv = SSAO_SOURCE_UV( to_F32( sourcePixel ), to_F32( fullSize ) );
                    const F32 reconstructedPixel = uv * to_F32( fullSize ) - 0.5f;
                    REQUIRE( reconstructedPixel == Catch::Approx( to_F32( sourcePixel ) ).margin( 0.004f ) );
                    REQUIRE( to_I32( SSAO_HALF_SAMPLE_POSITION( uv, to_F32( fullSize ) ) ) == halfPixel );
                }
            }
        }
    }

    TEST_CASE( "SSAO uploads kernel elements independently for both quality modes", "[renderer][ssao]" )
    {
        std::array<UniformData, 2> modes;
        vector<float4> fullKernel( 32u );
        vector<float4> halfKernel( 24u );
        for ( size_t i = 0u; i < fullKernel.size(); ++i )
        {
            fullKernel[i].set( to_F32(i), -to_F32(i), to_F32(i) * 0.1f, 1.f );
        }
        for ( size_t i = 0u; i < halfKernel.size(); ++i )
        {
            halfKernel[i].set( -to_F32(i), to_F32(i), to_F32(i) * 0.2f, 0.f );
        }

        const auto checkKernel = []( const UniformData& uniforms, const vector<float4>& expected )
        {
            REQUIRE( uniforms.entries().size() == 1u );
            const UniformData::Entry& entry = uniforms.entries().front();
            REQUIRE( entry._bindingHash == _ID( "sampleKernel" ) );
            REQUIRE( entry._type == PushConstantType::VEC4 );
            REQUIRE( entry._range._length == expected.size() * 4u * sizeof(F32) );
            CHECK( std::memcmp( uniforms.data( entry._range._startOffset ), expected.data(), entry._range._length ) == 0 );
        };

        SSAO::UploadSampleKernel( modes[1], halfKernel );
        SSAO::UploadSampleKernel( modes[0], fullKernel );
        checkKernel( modes[1], halfKernel );
        checkKernel( modes[0], fullKernel );

        halfKernel[0].set( 0.5f, 0.25f, 0.125f, 0.f );
        SSAO::UploadSampleKernel( modes[1], halfKernel );
        SSAO::UploadSampleKernel( modes[0], fullKernel );
        checkKernel( modes[1], halfKernel );
        checkKernel( modes[0], fullKernel );
    }

    TEST_CASE( "OpenGL object labels respect the device length limit", "[renderer][opengl]" )
    {
        std::array<char, 500> storage;
        storage.fill( 'x' );
        const std::string_view label{ storage.data(), storage.size() };
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 12u ), 256u ) == 12u );
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 255u ), 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 256u ), 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label, 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label, 1u ) == 0u );
        CHECK( GLUtil::ObjectLabelLength( label, 0u ) == 0u );

        const std::string_view substring{ storage.data() + 1u, 3u };
        CHECK( GLUtil::ObjectLabelLength( substring, 256u ) == 3u );
        CHECK( GLUtil::ObjectLabelLength( substring, 3u ) == 2u );
    }

    TEST_CASE( "Vulkan tool enumeration handles changing counts and errors", "[render_smoke][vulkan_tools]" )
    {
        std::vector<VkPhysicalDeviceToolProperties> tools( 1u );

        SECTION( "No active tools" )
        {
            const VkResult result = EnumeratePhysicalDeviceTools(
                []( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    CHECK( properties == nullptr );
                    *count = 0u;
                    return VK_SUCCESS;
                },
                tools );
            CHECK( result == VK_SUCCESS );
            CHECK( tools.empty() );
        }

        SECTION( "An incomplete result is retried using a fresh count" )
        {
            U32 countCalls = 0u;
            U32 dataCalls = 0u;
            const VkResult result = EnumeratePhysicalDeviceTools(
                [&]( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    if ( properties == nullptr )
                    {
                        *count = countCalls++ == 0u ? 1u : 2u;
                        return VK_SUCCESS;
                    }

                    for ( U32 i = 0u; i < *count; ++i )
                    {
                        CHECK( properties[i].sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES );
                        CHECK( properties[i].pNext == nullptr );
                    }
                    if ( dataCalls++ == 0u )
                    {
                        CHECK( *count == 1u );
                        return VK_INCOMPLETE;
                    }
                    CHECK( *count == 2u );
                    properties[0].name[0] = 'A';
                    properties[1].name[0] = 'B';
                    return VK_SUCCESS;
                },
                tools );
            REQUIRE( result == VK_SUCCESS );
            REQUIRE( tools.size() == 2u );
            CHECK( tools[0].name[0] == 'A' );
            CHECK( tools[1].name[0] == 'B' );
            CHECK( countCalls == 2u );
            CHECK( dataCalls == 2u );
        }

        SECTION( "A shrinking count keeps only the returned properties" )
        {
            const VkResult result = EnumeratePhysicalDeviceTools(
                []( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    if ( properties == nullptr )
                    {
                        *count = 2u;
                    }
                    else
                    {
                        *count = 1u;
                    }
                    return VK_SUCCESS;
                },
                tools );
            CHECK( result == VK_SUCCESS );
            CHECK( tools.size() == 1u );
        }

        SECTION( "Repeated incomplete results are bounded" )
        {
            U32 dataCalls = 0u;
            const VkResult result = EnumeratePhysicalDeviceTools(
                [&]( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    *count = 1u;
                    if ( properties == nullptr )
                    {
                        return VK_SUCCESS;
                    }
                    ++dataCalls;
                    return VK_INCOMPLETE;
                },
                tools );
            CHECK( result == VK_INCOMPLETE );
            CHECK( dataCalls == MAX_VULKAN_TOOL_ENUMERATION_ATTEMPTS );
            CHECK( tools.empty() );
        }

        SECTION( "Query failures and invalid returned counts do not publish partial results" )
        {
            for ( const bool invalidCount : { false, true } )
            {
                const VkResult result = EnumeratePhysicalDeviceTools(
                    [invalidCount]( U32* count, VkPhysicalDeviceToolProperties* properties )
                    {
                        if ( properties == nullptr )
                        {
                            *count = 1u;
                            return VK_SUCCESS;
                        }
                        if ( invalidCount )
                        {
                            *count = 2u;
                            return VK_SUCCESS;
                        }
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    },
                    tools );
                CHECK( result == ( invalidCount ? VK_ERROR_UNKNOWN : VK_ERROR_OUT_OF_HOST_MEMORY ) );
                CHECK( tools.empty() );
            }
        }
    }

    TEST_CASE( "Blur passes record their constants before every draw", "[render_smoke][blur]" )
    {
        for ( const bool gaussian : { false, true } )
        {
            for ( const U8 layers : { U8{ 1u }, U8{ 3u } } )
            {
                for ( const F32 direction : { 0.f, 1.f } )
                {
                    CAPTURE( gaussian, layers, direction );
                    PushConstantsStruct pushData{};
                    pushData.data[0]._vec[0].w = 7.f;
                    pushData.data[0]._vec[1].x = direction;
                    pushData.data[0]._vec[1].y = to_F32( layers );

                    GFX::CommandBuffer commands;
                    GFX::RecordBlurDrawCommands( commands, pushData, gaussian, layers );
                    const size_t drawCount = gaussian ? 1u : to_size( layers );
                    REQUIRE( commands.commands().size() == drawCount * 2u );
                    for ( size_t draw = 0u; draw < drawCount; ++draw )
                    {
                        auto* constants = commands.commands()[draw * 2u];
                        const auto* command = commands.commands()[draw * 2u + 1u];
                        REQUIRE( constants->type() == GFX::CommandType::SEND_PUSH_CONSTANTS );
                        REQUIRE( command->type() == GFX::CommandType::DRAW_COMMANDS );
                        const PushConstantsStruct& recorded = constants->As<GFX::SendPushConstantsCommand>()->_fastData;
                        CHECK( recorded.data[0]._vec[0].x == ( gaussian ? 0.f : to_F32( draw ) ) );
                        CHECK( recorded.data[0]._vec[0].w == 7.f );
                        CHECK( recorded.data[0]._vec[1].x == direction );
                        CHECK( recorded.data[0]._vec[1].y == to_F32( layers ) );
                    }
                }
            }
        }
    }

    TEST_CASE( "View depth and colour passes exclude translucent bins without changing shadow casters", "[renderer][render_queue]" )
    {
        using Params = RenderQueue::PopulateQueueParams;
        constexpr std::array<RenderBinType, 7u> bins{{
            RenderBinType::OPAQUE, RenderBinType::WATER, RenderBinType::TERRAIN, RenderBinType::TERRAIN_AUX,
            RenderBinType::SKY, RenderBinType::TRANSLUCENT, RenderBinType::IMPOSTOR
        }};
        constexpr std::array<RenderStage, 4u> stages{{
            RenderStage::DISPLAY, RenderStage::REFLECTION, RenderStage::REFRACTION, RenderStage::NODE_PREVIEW
        }};

        for ( const RenderStage stage : stages )
        {
            for ( const RenderPassType pass : { RenderPassType::PRE_PASS, RenderPassType::MAIN_PASS } )
            {
                const Params params = Params::ForPass( { stage, pass }, false );
                for ( const RenderBinType bin : bins )
                {
                    CAPTURE( stage, pass, bin );
                    CHECK( params.includes( bin ) == ( bin != RenderBinType::TRANSLUCENT ) );
                }
                CHECK_FALSE( params.includes( RenderBinType::COUNT ) );
            }

            for ( const RenderPassType pass : { RenderPassType::OIT_PASS, RenderPassType::TRANSPARENCY_PASS } )
            {
                const Params params = Params::ForPass( { stage, pass }, true );
                for ( const RenderBinType bin : bins )
                {
                    CAPTURE( stage, pass, bin );
                    CHECK( params.includes( bin ) == ( bin == RenderBinType::TRANSLUCENT ) );
                }
            }
        }

        for ( const RenderPassType pass : { RenderPassType::PRE_PASS, RenderPassType::MAIN_PASS } )
        {
            const Params params = Params::ForPass( { RenderStage::SHADOW, pass }, false );
            for ( const RenderBinType bin : bins )
            {
                CAPTURE( pass, bin );
                CHECK( params.includes( bin ) );
            }
        }

        const Params all{ {}, RenderBinType::COUNT, false };
        const Params none{ {}, RenderBinType::COUNT, true };
        for ( const RenderBinType bin : bins )
        {
            CHECK( all.includes( bin ) );
            CHECK_FALSE( none.includes( bin ) );
        }
    }

    TEST_CASE( "OIT composition selects the destination and sample-count variant", "[render_smoke][oit]" )
    {
        using Variant = RenderPassExecutor::OITCompositionVariant;
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( false, false ) == Variant::COLOUR );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( false, true ) == Variant::COLOUR_MSAA );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( true, false ) == Variant::COLOUR_NORMALS );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( true, true ) == Variant::COLOUR_NORMALS_MSAA );
    }

    TEST_CASE( "Vulkan pipeline variants retain the complete rendering signature", "[render_smoke][pipeline]" )
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

    TEST_CASE( "Vulkan rebinds retained graphics pipelines when pass compatibility changes", "[render_smoke][pipeline]" )
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

    TEST_CASE( "FontStash vertex format matches its uploaded vertices", "[render_smoke][font]" )
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

    TEST_CASE( "FontStash keeps the atlas only after successful creation", "[render_smoke][font]" )
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

    TEST_CASE( "Additional render commands retain their package's uniform storage", "[render_smoke][uniforms]" )
    {
        RenderPackage package;
        RenderPackage otherPackage;
        GFX::CommandBuffer commands;
        constexpr U64 chunkBinding = _ID( "dvd_terrainChunkOffset" );

        {
            GFX::SendPushConstantsCommand cull = package.additionalPushConstantsCmd();
            REQUIRE( cull._uniformData != nullptr );
            CHECK( cull._uniformData == package.pushConstantsCmd()._uniformData );
            CHECK( cull._uniformData != otherPackage.additionalPushConstantsCmd()._uniformData );
            cull._uniformData->set( chunkBinding, PushConstantType::UINT, 7u );
            GFX::EnqueueCommand( commands, cull );
        }

        REQUIRE( commands.commands().size() == 1u );
        const auto* queued = commands.commands().front()->As<GFX::SendPushConstantsCommand>();
        REQUIRE( queued->_uniformData != nullptr );
        REQUIRE( queued->_uniformData->entries().size() == 1u );
        const UniformData::Entry& entry = queued->_uniformData->entries().front();
        CHECK( entry._bindingHash == chunkBinding );
        U32 chunkIndex = 0u;
        std::memcpy( &chunkIndex, queued->_uniformData->data( entry._range._startOffset ), sizeof( chunkIndex ) );
        CHECK( chunkIndex == 7u );

        Clear( package );
        CHECK( package.additionalPushConstantsCmd()._uniformData == package.pushConstantsCmd()._uniformData );
    }

    TEST_CASE( "Render package preparation is copied separately from draw commands", "[render_smoke][vegetation][commands]" )
    {
        GFX::InitPools( 4u );
        SCOPE_EXIT
        {
            GFX::DestroyPools();
        };

        RenderPackage package;
        GFX::CommandBuffer pass;
        AppendPreparationCommands( package, pass );
        CHECK( pass.commands().empty() );

        const Handle<GFX::CommandBuffer> prepHandle = GetPreparationCommandBuffer( package );
        REQUIRE( prepHandle == GetPreparationCommandBuffer( package ) );
        GFX::CommandBuffer* preparation = GFX::Get( prepHandle );
        REQUIRE( preparation != nullptr );
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( *preparation )->_workGroupSize.set( 1u, 1u, 1u );
        AppendPreparationCommands( package, pass );

        GFX::CommandBuffer* draw = GFX::Get( GetCommandBuffer( package ) );
        REQUIRE( draw != nullptr );
        GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( *draw );
        preparation->clear();

        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( pass );
        pass.add( *draw );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( pass );
        REQUIRE( pass.commands().size() == 4u );
        CHECK( pass.commands()[0]->type() == GFX::CommandType::DISPATCH_SHADER_TASK );
        CHECK( pass.commands()[1]->type() == GFX::CommandType::BEGIN_RENDER_PASS );
        CHECK( pass.commands()[2]->type() == GFX::CommandType::BIND_SHADER_RESOURCES );
        CHECK( pass.commands()[3]->type() == GFX::CommandType::END_RENDER_PASS );

        Clear( package );
        GFX::CommandBuffer afterClear;
        AppendPreparationCommands( package, afterClear );
        CHECK( afterClear.commands().empty() );
    }

    TEST_CASE("Vegetation occlusion commands remain outside depth and colour passes", "[renderer][vegetation][commands]")
    {
        GFX::InitPools(4u);
        SCOPE_EXIT { GFX::DestroyPools(); };
        RenderPackage package;
        GFX::CommandBuffer view;
        AppendOcclusionCommands(package, view);
        CHECK(view.commands().empty());
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(*GFX::Get(GetPreparationCommandBuffer(package)))->_workGroupSize.x = 1u;
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(*GFX::Get(GetOcclusionCommandBuffer(package)))->_workGroupSize.x = 2u;
        AppendPreparationCommands(package, view);
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(view)->_workGroupSize.x = 3u; // Current-view Hi-Z.
        AppendOcclusionCommands(package, view);
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(view);
        GFX::Get(GetOcclusionCommandBuffer(package))->clear();
        REQUIRE(view.commands().size() == 7u);
        CHECK(view.commands()[0]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 1u);
        CHECK(view.commands()[3]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 3u);
        CHECK(view.commands()[4]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 2u);
        Clear(package);
        GFX::CommandBuffer afterClear;
        AppendOcclusionCommands(package, afterClear);
        CHECK(afterClear.commands().empty());
    }

    TEST_CASE("Hi-Z occlusion keeps near-plane intersections and equal-depth surfaces", "[renderer][vegetation][hiz]")
    {
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(-1.f, 0.5f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(0.f, 0.5f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(1.f, 0.f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(1.f, -0.1f));
        CHECK(HIZ_PROJECTABLE_CORNER(1.f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.5f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.500001f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.1f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.99f, 1.f));
        CHECK(HIZ_OCCLUDED(0.8f, 0.5f));
    }

    TEST_CASE("Hi-Z four-tap footprints cover rectangular and odd-sized pyramids", "[renderer][vegetation][hiz]")
    {
        using std::ceil;
        using std::floor;
        using std::log2;
        using std::min;
        using std::max;
        for (const U32 size : {1u, 3u, 5u, 719u, 1279u, 1920u})
        {
            for (const F32 width : {0.f, 0.00001f, 0.001f, 0.1f, 0.5f, 1.f})
            {
                const U32 mip = to_U32(HIZ_MIP_LEVEL(width * to_F32(size)));
                const F32 dimension = to_F32(max(size >> mip, 1u));
                for (const F32 start : {0.f, 0.2f, 0.51f, 0.99f})
                {
                    const F32 end = min(start + width, 1.f);
                    const F32 first = HIZ_TEXEL(start, dimension);
                    const F32 last = HIZ_TEXEL(end, dimension);
                    CHECK(first >= 0.f);
                    CHECK(last < dimension);
                    CHECK(HIZ_FOUR_TAP_COVERS(first, last));
                }
            }
        }
        CHECK_FALSE(HIZ_FOUR_TAP_COVERS(0, 2));
    }

    TEST_CASE( "Visible tree consumers schedule their preparation owner once", "[render_smoke][vegetation][commands]" )
    {
        struct Owner {};
        struct Package {};
        Owner grassAndTrees;
        Owner treesOnly;
        Package preparedGrass;
        vector<std::pair<Owner*, Package*>> owners{
            { &treesOnly, nullptr },
            { &grassAndTrees, nullptr },
            { &treesOnly, nullptr },
            { &grassAndTrees, &preparedGrass },
            { &grassAndTrees, nullptr }
        };
        vector<std::pair<Owner*, Package*>> selected;

        RenderPassExecutor::VisitPreparationOwners( owners, [&]( Owner& owner, Package* prepared )
        {
            selected.emplace_back( &owner, prepared );
        } );

        REQUIRE( selected.size() == 2u );
        CHECK( std::ranges::count_if( selected, [&]( const auto& entry )
        {
            return entry.first == &grassAndTrees && entry.second == &preparedGrass;
        } ) == 1 );
        CHECK( std::ranges::count_if( selected, [&]( const auto& entry )
        {
            return entry.first == &treesOnly && entry.second == nullptr;
        } ) == 1 );
    }

    TEST_CASE( "Vulkan attachment selections address cubemap faces and array layers", "[render_smoke][render_target]" )
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

    TEST_CASE( "Render package uniforms remain stable when their owning collection grows", "[render_smoke][uniforms]" )
    {
        STATIC_REQUIRE_FALSE( std::is_copy_constructible_v<RenderPackage> );
        STATIC_REQUIRE_FALSE( std::is_move_constructible_v<RenderPackage> );

        vector<std::unique_ptr<RenderPackage>> packages;
        packages.emplace_back( std::make_unique<RenderPackage>() );
        RenderPackage& package = *packages.front();
        GFX::CommandBuffer commands;
        GFX::SendPushConstantsCommand constants = package.additionalPushConstantsCmd();
        constexpr U64 binding = _ID( "dvd_terrainChunkOffset" );
        constants._uniformData->set( binding, PushConstantType::UINT, 7u );
        GFX::EnqueueCommand( commands, constants );

        for ( U32 i = 0u; i < 128u; ++i )
        {
            packages.emplace_back( std::make_unique<RenderPackage>() );
        }

        REQUIRE( commands.commands().size() == 1u );
        const auto* queued = commands.commands().front()->As<GFX::SendPushConstantsCommand>();
        REQUIRE( queued->_uniformData == packages.front()->pushConstantsCmd()._uniformData );
        REQUIRE( queued->_uniformData->entries().size() == 1u );
        CHECK( queued->_uniformData->entries().front()._bindingHash == binding );
    }

    TEST_CASE( "File texture arrays validate loaded layers against the descriptor", "[render_smoke][texture]" )
    {
        TextureDescriptor descriptor{};
        descriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        descriptor._layerCount = 8u;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 8u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 7u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 1u ) );

        descriptor._texType = TextureType::TEXTURE_1D_ARRAY;
        descriptor._layerCount = 2u;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 2u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 1u ) );

        descriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 6u ) );
        CHECK( Texture::HasValidFileLayerCount( descriptor, 12u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 0u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 7u ) );
        CHECK( Texture::TextureLayersForImageLayers( descriptor._texType, 6u ) == 1u );
        CHECK( Texture::TextureLayersForImageLayers( descriptor._texType, 12u ) == 2u );

        descriptor._texType = TextureType::TEXTURE_2D;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 1u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 2u ) );
        CHECK( Texture::TextureLayersForImageLayers( TextureType::TEXTURE_2D_ARRAY, 8u ) == 8u );
    }

    TEST_CASE( "Texture views preserve cube and array layer ranges", "[render_smoke][texture]" )
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

    TEST_CASE( "Vulkan view barriers use the view's layer units", "[render_smoke][texture]" )
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

    TEST_CASE( "Vulkan render passes load retained attachments before resolving", "[render_smoke][render_target]" )
    {
        for ( const bool clear : { false, true } )
        {
            for ( const bool resolve : { false, true } )
            {
                for ( const bool keep : { false, true } )
                {
                    CAPTURE( clear, resolve, keep );
                    VkRenderingAttachmentInfo info{};
                    vkRenderTarget::ConfigureAttachmentLoadStore( info, clear, resolve, keep );
                    CHECK( info.loadOp == ( clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD ) );
                    CHECK( info.storeOp == ( resolve && !keep ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                              : VK_ATTACHMENT_STORE_OP_STORE ) );
                }
            }
        }

        VkRenderingAttachmentInfo retainedPass{};
        vkRenderTarget::ConfigureAttachmentLoadStore( retainedPass, true, true, true );
        REQUIRE( retainedPass.storeOp == VK_ATTACHMENT_STORE_OP_STORE );

        VkRenderingAttachmentInfo finalPass{};
        vkRenderTarget::ConfigureAttachmentLoadStore( finalPass, false, true, false );
        CHECK( finalPass.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD );
        CHECK( finalPass.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE );
    }

    TEST_CASE( "Vulkan retained attachments transition only their rendered mip", "[render_smoke][render_target]" )
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

    TEST_CASE( "Vulkan attachment blits restore layouts and attachment load access", "[render_smoke][image_barrier]" )
    {
        using Transition = vkTexture::TransitionType;
        for ( const bool depth : { false, true } )
        {
            for ( const bool isResolve : { false, true } )
            {
                CAPTURE( depth, isResolve );
                const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageSubresourceRange range
                {
                    .aspectMask = aspect,
                    .baseMipLevel = 2u,
                    .levelCount = 1u,
                    .baseArrayLayer = 3u,
                    .layerCount = 1u
                };
                const VkAccessFlags2 attachmentAccess = depth
                    ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                    : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                const VkPipelineStageFlags2 attachmentStages = depth
                    ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                    : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

                const std::array cases
                {
                    std::pair
                    {
                        depth ? Transition::ATTACHMENT_TO_BLIT_READ_DEPTH : Transition::ATTACHMENT_TO_BLIT_READ_COLOUR,
                        depth ? Transition::BLIT_READ_TO_ATTACHMENT_DEPTH : Transition::BLIT_READ_TO_ATTACHMENT_COLOUR
                    },
                    std::pair
                    {
                        depth ? Transition::ATTACHMENT_TO_BLIT_WRITE_DEPTH : Transition::ATTACHMENT_TO_BLIT_WRITE_COLOUR,
                        depth ? Transition::BLIT_WRITE_TO_ATTACHMENT_DEPTH : Transition::BLIT_WRITE_TO_ATTACHMENT_COLOUR
                    }
                };
                for ( size_t i = 0u; i < cases.size(); ++i )
                {
                    const bool read = i == 0u;
                    const VkAccessFlags2 transferAccess = read ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    const VkImageLayout transferLayout = read ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    VkImageMemoryBarrier2 before{};
                    VkImageMemoryBarrier2 after{};
                    const vkTexture::NamedVKImage image{ ._name = "attachment", ._isResolveImage = isResolve };
                    vkTexture::TransitionTexture( cases[i].first, range, image, before );
                    vkTexture::TransitionTexture( cases[i].second, range, image, after );
                    CHECK( before.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                    CHECK( before.newLayout == transferLayout );
                    CHECK( after.oldLayout == transferLayout );
                    CHECK( after.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                    CHECK( ( before.srcAccessMask & attachmentAccess ) == attachmentAccess );
                    CHECK( ( before.srcStageMask & attachmentStages ) == attachmentStages );
                    CHECK( ( before.dstAccessMask & transferAccess ) == transferAccess );
                    CHECK( ( after.srcAccessMask & transferAccess ) == transferAccess );
                    CHECK( ( after.dstAccessMask & attachmentAccess ) == attachmentAccess );
                    CHECK( ( after.dstStageMask & attachmentStages ) == attachmentStages );
                    CHECK( before.subresourceRange.baseMipLevel == range.baseMipLevel );
                    CHECK( before.subresourceRange.baseArrayLayer == range.baseArrayLayer );
                    CHECK( after.subresourceRange.aspectMask == range.aspectMask );
                    CHECK( after.subresourceRange.levelCount == range.levelCount );
                    CHECK( after.subresourceRange.layerCount == range.layerCount );
                }
            }
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

    TEST_CASE( "Device-fault capture requires smoke mode, its extension and feature", "[render_smoke][device_fault]" )
    {
        CHECK( VKDevice::CanEnableDeviceFault( true, true, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( false, true, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( true, false, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( true, true, false ) );
    }

    TEST_CASE( "Vulkan smoke readback accepts only single-sample RGBA8 scene images", "[render_smoke][readback]" )
    {
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 2u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 2u ) );
    }

    TEST_CASE( "Vulkan editor readback transitions directly from the last screen pass", "[render_smoke][readback]" )
    {
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, false, false ) == VK_IMAGE_LAYOUT_UNDEFINED );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, false, true ) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, true, true ) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, true, false ) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        CHECK( VKSwapChain::ScreenPassInitialLayout( false, true, true ) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        CHECK( VKSwapChain::ScreenPassInitialLayout( false, true, false ) == VK_IMAGE_LAYOUT_UNDEFINED );

        for ( const bool capture : { false, true } )
        {
            const VkImageMemoryBarrier2 barrier = VKSwapChain::SmokePresentBarrier( VK_NULL_HANDLE, capture );
            CHECK( barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 );
            CHECK( barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED );
            CHECK( barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED );
            CHECK( barrier.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT );
            CHECK( barrier.srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT );
            CHECK( barrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            CHECK( barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT );
            CHECK( barrier.subresourceRange.levelCount == 1u );
            CHECK( barrier.subresourceRange.layerCount == 1u );
            CHECK( barrier.newLayout == ( capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ) );
            CHECK( barrier.dstStageMask == ( capture ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT ) );
            CHECK( barrier.dstAccessMask == ( capture ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_NONE ) );
        }
    }

    TEST_CASE( "Vulkan editor readback converts swapchain pixels and image origin", "[render_smoke][readback]" )
    {
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_SRGB ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_UNORM ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_A2B10G10R10_UNORM_PACK32 ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_A2R10G10B10_UNORM_PACK32 ) );
        CHECK_FALSE( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT ) );

        const std::array<Byte, 16> bgra
        {{
            Byte{ 3 }, Byte{ 2 }, Byte{ 1 }, Byte{ 255 }, Byte{ 6 }, Byte{ 5 }, Byte{ 4 }, Byte{ 255 },
            Byte{ 9 }, Byte{ 8 }, Byte{ 7 }, Byte{ 255 }, Byte{ 12 }, Byte{ 11 }, Byte{ 10 }, Byte{ 255 }
        }};
        const std::array<Byte, 16> expected
        {{
            Byte{ 7 }, Byte{ 8 }, Byte{ 9 }, Byte{ 255 }, Byte{ 10 }, Byte{ 11 }, Byte{ 12 }, Byte{ 255 },
            Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 255 }, Byte{ 4 }, Byte{ 5 }, Byte{ 6 }, Byte{ 255 }
        }};
        std::array<Byte, 16> converted{};
        REQUIRE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB, 2u, 2u, bgra, converted ) );
        CHECK( converted == expected );
        REQUIRE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 2u, 2u, expected, converted ) );
        CHECK( converted[0] == Byte{ 1 } );
        CHECK( converted[2] == Byte{ 3 } );
        CHECK( converted[8] == Byte{ 7 } );

        const std::array<Byte, 4> expectedPacked{{ Byte{ 255 }, Byte{ 128 }, Byte{ 0 }, Byte{ 170 } }};
        for ( const VkFormat format : { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32 } )
        {
            const U32 pixel = ( 2u << 30u ) | ( 512u << 10u ) |
                              ( format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? 1023u : 1023u << 20u );
            const std::array<U32, 1> source{ pixel };
            std::array<Byte, 4> output{};
            REQUIRE( VKSwapChain::ConvertSmokeReadback( format, 1u, 1u, std::as_bytes( std::span{ source } ), output ) );
            CHECK( output == expectedPacked );
        }

        const auto unchanged = converted;
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 0u, 2u, bgra, converted ) );
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 3u, 2u, bgra, converted ) );
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT, 2u, 2u, bgra, converted ) );
        CHECK( converted == unchanged );
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

    TEST_CASE( "Removed debug views retain queued uniform data until frame flush", "[render_smoke][debug_views]" )
    {
        DebugViewRegistry registry;
        DebugView_ptr view = std::make_shared<DebugView>();
        view->_shaderData.set( _ID( "layer" ), PushConstantType::INT, I32{ 7 } );
        std::weak_ptr<DebugView> weakView = view;
        DebugView* const raw = registry.add( view );
        registry.add( view );
        GFX::SendPushConstantsCommand command;
        command._uniformData = &view->_shaderData;
        view.reset();

        REQUIRE( registry.remove( raw ) );
        CHECK_FALSE( registry.remove( raw ) );
        CHECK_FALSE( registry.remove( nullptr ) );
        vector<std::tuple<string, I16, I16, bool>> names;
        registry.getNames( names );
        CHECK( names.empty() );
        REQUIRE_FALSE( weakView.expired() );
        REQUIRE( command._uniformData == &weakView.lock()->_shaderData );
        const auto& entries = command._uniformData->entries();
        const auto layer = std::find_if( entries.begin(), entries.end(), []( const auto& entry )
        {
            return entry._bindingHash == _ID( "layer" );
        } );
        REQUIRE( layer != entries.end() );
        I32 layerValue = -1;
        std::memcpy( &layerValue, command._uniformData->data( layer->_range._startOffset ), sizeof( layerValue ) );
        CHECK( layerValue == 7 );

        registry.releaseRetired();
        CHECK( weakView.expired() );
    }

    TEST_CASE( "Debug views sort by group and index and support concurrent removal", "[render_smoke][debug_views]" )
    {
        DebugViewRegistry registry;
        std::array<DebugView_ptr, 3> ordered = {
            std::make_shared<DebugView>( 2 ),
            std::make_shared<DebugView>( 0 ),
            std::make_shared<DebugView>( 1 )
        };
        for ( size_t i = 0u; i < ordered.size(); ++i )
        {
            ordered[i]->_groupID = to_I16( i + 1u );
            ordered[i]->_name = Util::StringFormat( "view{}", i );
            registry.add( ordered[i] );
        }
        vector<std::tuple<string, I16, I16, bool>> names;
        registry.getNames( names );
        REQUIRE( names.size() == ordered.size() );
        CHECK( std::get<0>( names[0] ) == "view0" );
        CHECK( std::get<0>( names[1] ) == "view1" );
        CHECK( std::get<0>( names[2] ) == "view2" );

        constexpr size_t viewCount = 32u;
        std::array<DebugView_ptr, viewCount> views;
        std::array<std::weak_ptr<DebugView>, viewCount> weakViews;
        for ( size_t i = 0u; i < viewCount; ++i )
        {
            views[i] = std::make_shared<DebugView>();
            weakViews[i] = views[i];
        }

        std::atomic_bool removedAll{ true };
        std::array<std::thread, 4> workers;
        for ( size_t worker = 0u; worker < workers.size(); ++worker )
        {
            workers[worker] = std::thread( [&, worker]
            {
                for ( size_t i = worker; i < viewCount; i += workers.size() )
                {
                    registry.add( views[i] );
                    if ( !registry.remove( views[i].get() ) )
                    {
                        removedAll.store( false );
                    }
                }
            } );
        }
        for ( std::thread& worker : workers )
        {
            worker.join();
        }
        REQUIRE( removedAll.load() );
        views.fill( nullptr );
        for ( const auto& weak : weakViews )
        {
            CHECK_FALSE( weak.expired() );
        }
        registry.releaseRetired();
        for ( const auto& weak : weakViews )
        {
            CHECK( weak.expired() );
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
