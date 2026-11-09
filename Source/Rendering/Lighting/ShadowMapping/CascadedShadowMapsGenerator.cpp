// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/CascadedShadowMapsGenerator.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/ShadowPassState.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Scenes/Headers/SceneState.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"

#include "Managers/Headers/ProjectManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide
{

    namespace
    {
        Configuration::Rendering::ShadowMapping g_shadowSettings;

        constexpr F32 g_minExtentsFactors[]
        {
            0.025f,
            1.75f,
            75.0f,
            125.0f
        };
    }

    CascadedShadowMapsGenerator::CascadedShadowMapsGenerator( GFXDevice& context )
        : ShadowMapGenerator( context, ShadowType::CSM )
        , _worldAOCamera( Camera::CreateCamera( "WorldAOCamera", Camera::Mode::STATIC ) )
    {
        Console::printfn( LOCALE_STR( "LIGHT_CREATE_SHADOW_FB"), "EVCSM" );

        const RenderTarget* rt = ShadowMap::getShadowMap( _type )._rt;

        g_shadowSettings = context.context().config().rendering.shadowMapping;
        {
            ShaderModuleDescriptor vertModule{ ShaderType::VERTEX,   "baseVertexShaders.glsl", "FullScreenQuad" };
            ShaderModuleDescriptor geomModule{ ShaderType::GEOMETRY, "blur.glsl",              "GaussBlur" };
            ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "blur.glsl",              "GaussBlur.Layered" };

            geomModule._defines.emplace_back( "verticalBlur uint(PushData0[1].x)" );
            geomModule._defines.emplace_back( "layerCount int(PushData0[1].y)" );
            geomModule._defines.emplace_back( "layerOffsetRead int(PushData0[1].z)" );
            geomModule._defines.emplace_back( "layerOffsetWrite int(PushData0[1].w)" );

            fragModule._defines.emplace_back( "LAYERED" );
            fragModule._defines.emplace_back( "layer uint(PushData0[0].x)" );
            fragModule._defines.emplace_back( "size PushData0[0].yz" );
            fragModule._defines.emplace_back( "kernelSize int(PushData0[0].w)" );
            fragModule._defines.emplace_back( "verticalBlur uint(PushData0[1].x)" );

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( geomModule );
            shaderDescriptor._modules.push_back( fragModule );
            shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "GS_MAX_INVOCATIONS {}", Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT ) );

            {
                ResourceDescriptor<ShaderProgram> blurDepthMapShader( Util::StringFormat( "GaussBlur_{}_invocations", Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT ), shaderDescriptor );
                blurDepthMapShader.waitForReady( true );
                _blurDepthMapShader = CreateResource( blurDepthMapShader );
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateBlock = _context.get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = _blurDepthMapShader;
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

                _blurPipelineCSM = _context.newPipeline( pipelineDescriptor );
            }
            shaderDescriptor._globalDefines[0]._define = "GS_MAX_INVOCATIONS 1u";
            {
                ResourceDescriptor<ShaderProgram> blurDepthMapShader( "GaussBlur_1_invocations", shaderDescriptor );
                blurDepthMapShader.waitForReady( true );
                _blurAOMapShader = CreateResource( blurDepthMapShader );
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateBlock = _context.get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = _blurAOMapShader;
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

                _blurPipelineAO = _context.newPipeline( pipelineDescriptor );
            }
        }

        _shaderConstants.data[0]._vec[1].y = to_F32( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT );
        _shaderConstants.data[0]._vec[1].z = 0.f;
        _shaderConstants.data[0]._vec[1].w = 0.f;

        std::array<float2*, 12> blurSizeConstants = {
                &_shaderConstants.data[0]._vec[2].xy,
                &_shaderConstants.data[0]._vec[2].zw,
                &_shaderConstants.data[0]._vec[3].xy,
                &_shaderConstants.data[0]._vec[3].zw,
                &_shaderConstants.data[1]._vec[0].xy,
                &_shaderConstants.data[1]._vec[0].zw,
                &_shaderConstants.data[1]._vec[1].xy,
                &_shaderConstants.data[1]._vec[1].zw,
                &_shaderConstants.data[1]._vec[2].xy,
                &_shaderConstants.data[1]._vec[2].zw,
                &_shaderConstants.data[1]._vec[3].xy,
                &_shaderConstants.data[1]._vec[3].zw
        };

        blurSizeConstants[0]->set( 1.f / g_shadowSettings.csm.shadowMapResolution );
        for ( size_t i = 1u; i < blurSizeConstants.size(); ++i )
        {
            blurSizeConstants[i]->set((*blurSizeConstants[i - 1]) * 0.5f);
        }

        SamplerDescriptor sampler{};
        sampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        sampler._mipSampling = TextureMipSampling::NONE;
        sampler._anisotropyLevel = 0u;

        const auto& texDescriptor = Get(rt->getAttachment( RTAttachmentType::COLOUR )->texture())->descriptor();
        // Draw FBO
        {
            // MSAA rendering is supported
            TextureDescriptor colourDescriptor{};
            colourDescriptor._texType = TextureType::TEXTURE_2D_ARRAY; 
            colourDescriptor._dataType = texDescriptor._dataType;
            colourDescriptor._baseFormat = texDescriptor._baseFormat;
            colourDescriptor._layerCount = 1u;
            colourDescriptor._mipMappingState = MipMappingState::OFF;

            TextureDescriptor depthDescriptor{};
            depthDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
            depthDescriptor._dataType = GFXDataFormat::UNSIGNED_INT;
            depthDescriptor._baseFormat = GFXImageFormat::RED;
            depthDescriptor._packing = GFXImagePacking::DEPTH;
            depthDescriptor._layerCount = 1u;
            depthDescriptor._mipMappingState = MipMappingState::OFF;

            RenderTargetDescriptor desc = {};
            desc._attachments = 
            {
                InternalRTAttachmentDescriptor{ colourDescriptor, sampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0},
                InternalRTAttachmentDescriptor{ depthDescriptor, sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
            };

            desc._resolution = rt->getResolution();
            desc._name = "CSM_ShadowMap_Draw";
            desc._msaaSamples = g_shadowSettings.csm.MSAASamples;

            _drawBufferDepth = context.renderTargetPool().allocateRT( desc );
        }

        //Blur FBO
        {
            TextureDescriptor blurMapDescriptor{};
            blurMapDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
            blurMapDescriptor._dataType = texDescriptor._dataType;
            blurMapDescriptor._baseFormat = texDescriptor._baseFormat;
            blurMapDescriptor._packing = texDescriptor._packing;
            blurMapDescriptor._layerCount = Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;
            blurMapDescriptor._mipMappingState = MipMappingState::OFF;

            RenderTargetDescriptor desc = {};
            desc._attachments = 
            {
                InternalRTAttachmentDescriptor{ blurMapDescriptor, sampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
            };

            desc._name = "CSM_Blur";
            desc._resolution = rt->getResolution();

            _blurBuffer = _context.renderTargetPool().allocateRT( desc );
        }

        WAIT_FOR_CONDITION( _blurPipelineAO != nullptr );
    }

    CascadedShadowMapsGenerator::~CascadedShadowMapsGenerator()
    {
        if ( !_context.renderTargetPool().deallocateRT( _blurBuffer ) ||
             !_context.renderTargetPool().deallocateRT( _drawBufferDepth ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        DestroyResource( _blurDepthMapShader );
        DestroyResource( _blurAOMapShader );
        Camera::DestroyCamera( _worldAOCamera );
    }

    CascadedShadowMapsGenerator::SplitDepths CascadedShadowMapsGenerator::calculateSplitDepths( DirectionalLightComponent& light, const float2 nearFarPlanes ) const noexcept
    {
        //Between 0 and 1, change these to check the results
        constexpr F32 minDistance = 0.0f;
        constexpr F32 maxDistance = 1.0f;

        SplitDepths depths = {};

        const U8 numSplits = light.csmSplitCount();
        const F32 nearClip = nearFarPlanes.min;
        const F32 farClip = nearFarPlanes.max;
        const F32 clipRange = farClip - nearClip;

        DIVIDE_ASSERT( clipRange > 0.f, "CascadedShadowMapsGenerator::calculateSplitDepths error: invalid clip range specified!" );

        const F32 minZ = nearClip + minDistance * clipRange;
        const F32 maxZ = nearClip + maxDistance * clipRange;

        const F32 range = maxZ - minZ;
        const F32 ratio = maxZ / minZ;

        U8 i = 0;
        for ( ; i < numSplits; ++i )
        {
            const F32 p = to_F32( i + 1 ) / numSplits;
            const F32 log = minZ * std::pow( ratio, p );
            const F32 uniform = minZ + range * p;
            const F32 d = g_shadowSettings.csm.splitLambda * (log - uniform) + uniform;
            depths[i] = (d - nearClip) / clipRange;
            light.setShadowFloatValue( i, d );
        }

        for ( ; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i )
        {
            depths[i] = F32_MAX;
            light.setShadowFloatValue( i, -depths[i] );
        }

        return depths;
    }

    void CascadedShadowMapsGenerator::applyFrustumSplits( DirectionalLightComponent& light, const Camera& shadowCamera, U8 numSplits, const U32 layerMask ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const float2 cameraPlanes = shadowCamera.snapshot()._zPlanes;
        const SplitDepths splitDepths = calculateSplitDepths( light, cameraPlanes );
        const F32 clipRange = cameraPlanes.max - cameraPlanes.min;

        const mat4<F32> invViewProj = GetInverse( shadowCamera.viewProjectionMatrix() );

        F32 appliedDiff = 0.0f;
        for ( U8 cascadeIterator = 0; cascadeIterator < numSplits; ++cascadeIterator )
        {
            Camera* lightCam = ShadowMap::shadowCameras( ShadowType::CSM )[cascadeIterator];

            F32 prevSplitDistance = 0.f;
            if ( cascadeIterator > 0u )
            {
                // Include the preceding split's transition region in this cascade.
                const F32 previousStart = cascadeIterator > 1u
                                        ? cameraPlanes.min + clipRange * splitDepths[cascadeIterator - 2u]
                                        : 0.f;
                const F32 previousEnd = cameraPlanes.min + clipRange * splitDepths[cascadeIterator - 1u];
                prevSplitDistance = CSMOverlapNearFraction( cameraPlanes.min, clipRange, previousStart, previousEnd );
            }
            const F32 splitDistance = splitDepths[cascadeIterator];

            float3 frustumCornersWS[8]
            {
                {-1.0f,  1.0f,  0.0f},
                { 1.0f,  1.0f,  0.0f},
                { 1.0f, -1.0f,  0.0f},
                {-1.0f, -1.0f,  0.0f},
                {-1.0f,  1.0f,  1.0f},
                { 1.0f,  1.0f,  1.0f},
                { 1.0f, -1.0f,  1.0f},
                {-1.0f, -1.0f,  1.0f},
            };

            for ( float3& corner : frustumCornersWS )
            {
                const float4 inversePoint = invViewProj * float4( corner, 1.0f );
                corner.set( inversePoint / inversePoint.w );
            }

            for ( U8 i = 0; i < 4; ++i )
            {
                const float3 cornerRay = frustumCornersWS[i + 4] - frustumCornersWS[i];
                const float3 nearCornerRay = cornerRay * prevSplitDistance;
                const float3 farCornerRay = cornerRay * splitDistance;

                frustumCornersWS[i + 4] = frustumCornersWS[i] + farCornerRay;
                frustumCornersWS[i] = frustumCornersWS[i] + nearCornerRay;
            }

            float3 frustumCenter = VECTOR3_ZERO;
            for ( const float3& corner : frustumCornersWS )
            {
                frustumCenter += corner;
            }
            frustumCenter /= 8.0f;

            F32 radius = 0.0f;
            for ( const float3& corner : frustumCornersWS )
            {
                const F32 distance = (corner - frustumCenter).lengthSquared();
                radius = std::max( radius, distance );
            }
            radius = std::ceil( Sqrt<F32>( radius ) * 16.0f ) / 16.0f;
            radius += appliedDiff;

            float3 maxExtents( radius, radius, radius );
            float3 minExtents = -maxExtents;

            //Position the view matrix looking down the center of the frustum with an arbitrary light direction
            float3 lightPosition = frustumCenter - light.directionCache() * (light.csmNearClipOffset() - minExtents.z);
            mat4<F32> lightViewMatrix = lightCam->lookAt( lightPosition, frustumCenter, WORLD_Y_AXIS );

            if ( cascadeIterator > 0 && light.csmUseSceneAABBFit()[cascadeIterator] )
            {
                const ShadowCasterBounds& previous = light.cascadeBoundsFeedback()[cascadeIterator];
                if ( previous._valid )
                {
                    BoundingBox meshAABB{ previous._bounds };
                    meshAABB.transform( lightViewMatrix );
                    appliedDiff = std::max( 0.f, meshAABB.getHalfExtent().y - radius );
                    if ( appliedDiff > 0.5f )
                    {
                        radius += appliedDiff * 0.75f;

                        maxExtents.set( radius, radius, radius );
                        minExtents = -maxExtents;

                        //Position the view matrix looking down the center of the frustum with an arbitrary light direction
                        lightPosition = frustumCenter - light.directionCache() * (light.csmNearClipOffset() - minExtents.z);
                        lightViewMatrix = lightCam->lookAt( lightPosition, frustumCenter, WORLD_Y_AXIS );
                    }
                }
            }

            const float2 clip
            {
                0.0001f,
                maxExtents.z - minExtents.z + std::max( 0.f, light.csmNearClipOffset() )
            };

            mat4<F32> lightOrthoMatrix = Camera::Ortho( minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, clip.min, clip.max );

            // The rounding matrix that ensures that shadow edges do not shimmer
            // http://www.gamedev.net/topic/591684-xna-40---shimmering-shadow-maps/
            {
                const mat4<F32> shadowMatrix = mat4<F32>::Multiply( lightOrthoMatrix, lightViewMatrix );
                const float4 shadowOrigin = shadowMatrix * float4{VECTOR3_ZERO, 1.f } * (g_shadowSettings.csm.shadowMapResolution * 0.5f);

                float4 roundedOrigin = shadowOrigin;
                roundedOrigin.round();

                lightOrthoMatrix.translate( float3
                {
                    (roundedOrigin.xy - shadowOrigin.xy) * 2.0f / g_shadowSettings.csm.shadowMapResolution, 0.0f
                } );

                // Use our adjusted matrix for actual rendering
                lightCam->setProjection( lightOrthoMatrix, clip, true );
            }
            lightCam->updateLookAt();

            const mat4<F32> lightVP = mat4<F32>::Multiply( lightOrthoMatrix, lightViewMatrix );

            if (ShadowLayerSelected(layerMask, cascadeIterator))
            {
                light.setShadowLightPos( cascadeIterator, lightPosition );
                light.setShadowVPMatrix( cascadeIterator, ShadowMap::BiasedViewProjection( lightVP ) );
            }
        }
    }

    void CascadedShadowMapsGenerator::render( const Camera& playerCamera, Light& light, U16 lightIndex, bool& pendingCastersOut, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut, const ShadowRefreshPlan& refreshPlan )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        auto& dirLight = static_cast<DirectionalLightComponent&>(light);

        const U32 layerMask = refreshPlan.updateMask();
        const U8 numSplits = dirLight.csmSplitCount();
        dirLight.cascadeBoundsFeedback().resize( numSplits );
        applyFrustumSplits( dirLight, playerCamera, numSplits, layerMask );

        RenderPassParams params = {};
        params._sourceNode = light.sgn();
        params._pendingShaderOut = &pendingCastersOut;
        params._stagePass = { RenderStage::SHADOW, RenderPassType::COUNT, lightIndex, static_cast<RenderStagePass::VariantType>(ShadowType::CSM) };
        params._target = _drawBufferDepth._targetID;
        params._maxLoD = -1;
        params._refreshLightData = false;
        params._useMSAA = _context.context().config().rendering.shadowMapping.csm.MSAASamples;

        params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;

        params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        auto cmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut);
        Util::StringFormatTo( cmd->_scopeName, "Cascaded Shadow Pass Light: [ {} ]", lightIndex );
        cmd->_scopeId = lightIndex;

        RenderPassManager* rpm = _context.context().kernel().renderPassManager().get();
        const U16 layerOffset = dirLight.getShadowArrayOffset();
        const RenderTargetHandle& rtHandle = ShadowMap::getShadowMap( _type );
        const RenderTargetHandle& staticTarget = ShadowMap::getShadowMapCache(_type);
        const auto& config = _context.context().config();
        const auto& filter = config.debug.renderFilter;
        const auto& scene = *light.sgn()->sceneGraph()->parentScene().state();
        const U32 renderMask = (filter.primitives ? toBit(0) : 0u) |
                               (filter.meshes ? toBit(1) : 0u) |
                               (filter.terrain ? toBit(2) : 0u) |
                               (filter.vegetation ? toBit(3) : 0u) |
                               (filter.treeInstances ? toBit(4) : 0u) |
                               (filter.particles ? toBit(5) : 0u) |
                               (scene.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY) ? toBit(6) : 0u) |
                               (scene.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME) ? toBit(7) : 0u);
        U32 staticRebuildMask = 0u;
        U32 staticReuseMask = 0u;

        for ( I8 i = numSplits - 1; i >= 0 && i < numSplits; i-- )
        {
            if (!ShadowLayerSelected(layerMask, to_U8(i)))
            {
                continue;
            }
            Util::StringFormatTo( params._passName, "CSM_PASS_{}", i );
            params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);
            params._enableDeformation = i == 0;
            params._minExtents.set( g_minExtentsFactors[i] );
            params._shadowBoundsFeedback = SelectCascadeBoundsFeedback(
                dirLight.cascadeBoundsFeedback()[i], to_U8( i ), dirLight.csmUseSceneAABBFit()[i] );
            if ( params._shadowBoundsFeedback != nullptr )
            {
                STUBBED( "CascadedShadowMapsGenerator::render: Validate AABBFit for first cascade!" );
            }

            const U16 cacheIndex = to_U16(layerOffset + i);
            StaticCacheLayer& cache = _staticLayers[cacheIndex];
            if (config.rendering.shadowMapping.csm.staticCaching)
            {
                CSMStaticCacheKey key
                {
                    ._owner = light.getGUID(),
                    ._coverageRevision = light.sgn()->sceneGraph()->worldAORevision(),
                    ._renderMask = renderMask,
                    ._resolution = {_drawBufferDepth._rt->getWidth(), _drawBufferDepth._rt->getHeight()},
                    ._visibilityDistances = {scene.renderState().generalVisibility(), scene.renderState().treeVisibility()},
                    ._samples = _drawBufferDepth._rt->getSampleCount(),
                    ._splitCount = numSplits
                };
                const auto& viewProjection = ShadowMap::shadowCameras(ShadowType::CSM)[i]->viewProjectionMatrix();
                std::copy_n(viewProjection.mat, key._viewProjection.size(), key._viewProjection.begin());
                const bool rebuild = cache._state.needsRefresh(
                    key, ShadowLayerSelected(refreshPlan._staticMask, to_U8(i)));
                cache._combinedBounds = params._shadowBoundsFeedback;
                if (rebuild)
                {
                    cache._pendingKey = key;
                    cache._rebuilt = true;
                    cache._pendingCasters = false;
                    cache._pendingOut = &pendingCastersOut;
                    cache._staticBounds.reset();
                    RenderPassParams staticParams = params;
                    staticParams._target = staticTarget._targetID;
                    staticParams._pendingShaderOut = &cache._pendingCasters;
                    staticParams._shadowBoundsFeedback = &cache._staticBounds;
                    staticParams._drawMask = to_U8(1u << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES));
                    staticParams._targetDescriptorMainPass._autoResolveMSAA = false;
                    staticParams._targetDescriptorMainPass._keepMSAADataAfterResolve = true;
                    staticParams._targetDescriptorMainPass._writeLayers[0]._layer = {cacheIndex, 1u};
                    staticParams._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer = {cacheIndex, 1u};
                    Util::StringFormatTo(staticParams._passName, "CSM_STATIC_{}", i);
                    rpm->doCustomPass(ShadowMap::shadowCameras(ShadowType::CSM)[i], staticParams, bufferInOut, memCmdInOut);
                    staticRebuildMask |= 1u << i;
                }
                else
                {
                    staticReuseMask |= 1u << i;
                }

                auto* copy = GFX::EnqueueCommand<GFX::BlitRenderTargetCommand>(bufferInOut);
                copy->_source = staticTarget._targetID;
                copy->_destination = _drawBufferDepth._targetID;
                for (const U8 attachment : {U8{0u}, to_U8(RT_DEPTH_ATTACHMENT_IDX)})
                {
                    copy->_params.push_back(
                    {
                        ._input = {._layerOffset = cacheIndex, ._index = attachment},
                        ._output = {._layerOffset = 0u, ._index = attachment},
                        ._copySamples = true
                    });
                }
                RenderPassParams dynamicParams = params;
                dynamicParams._drawMask = to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES));
                dynamicParams._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX]._enabled = false;
                dynamicParams._clearDescriptorMainPass[0]._enabled = false;
                rpm->doCustomPass(ShadowMap::shadowCameras(ShadowType::CSM)[i], dynamicParams, bufferInOut, memCmdInOut);
            }
            else
            {
                cache._state._valid = false;
                rpm->doCustomPass( ShadowMap::shadowCameras( ShadowType::CSM )[i], params, bufferInOut, memCmdInOut );
            }
            GFX::BlitRenderTargetCommand* blitRenderTargetCommand = GFX::EnqueueCommand<GFX::BlitRenderTargetCommand>( bufferInOut );
            blitRenderTargetCommand->_source = _drawBufferDepth._targetID;
            blitRenderTargetCommand->_destination = rtHandle._targetID;
            blitRenderTargetCommand->_params.emplace_back( ShadowMap::ScratchBlit( to_U16( layerOffset + i ) ) );
        }

        if (RenderSmokeTest::PerfModeRequested() && GFXDevice::FrameCount() % 64u == 0u)
        {
            Console::printfn("[CSMCacheMetrics] frame={} light={} rebuilt={} reused={} samples={}",
                GFXDevice::FrameCount(), light.getGUID(), staticRebuildMask, staticReuseMask,
                _drawBufferDepth._rt->getSampleCount());
        }

        // Now we can either blur our target or just skip to mipmap computation
        if ( g_shadowSettings.csm.enableBlurring )
        {
            ForEachShadowLayerRange(layerMask, numSplits, [&](const U8 first, const U8 count)
            {
                blurTarget(to_U16(layerOffset + first), count, bufferInOut, first);
            });
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    mat4<F32> CascadedShadowMapsGenerator::generateWorldAO( const WorldAOCacheKey& key, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        const bool refresh = _worldAOCache.needsRefresh(key, _worldAOPendingCasters);
        if ( RenderSmokeTest::PerfModeRequested() && GFXDevice::FrameCount() % 64u == 0u )
        {
            Console::printfn("[WorldAOMetrics] frame={} revision={} refresh={} pendingCasters={}",
                             GFXDevice::FrameCount(), key._revision, refresh, _worldAOPendingCasters);
        }
        if ( !refresh )
        {
            return _worldAOVP;
        }
        _worldAOCache._key = key;
        _worldAOCache._valid = true;
        _worldAOPendingCasters = false;

        constexpr F32 horizontalExtent = WorldAOCacheState::HORIZONTAL_EXTENT;
        constexpr F32 verticalMargin = 50.f;
        const F32 top = key._height[1] + verticalMargin;
        const F32 bottom = key._height[0] - verticalMargin;
        const float3 eye{ key._center[0], top, key._center[1] };
        const float3 target{ key._center[0], top - 1.f, key._center[1] };

        const float2 maxExtents( horizontalExtent, horizontalExtent );
        const float2 minExtents = -maxExtents;

        const Rect<I32> orthoRect = { minExtents.x, maxExtents.x, minExtents.y, maxExtents.y };
        const float2 zPlanes = { 1.f, top - bottom };

        const mat4<F32> viewMatrix = _worldAOCamera->lookAt( eye, target, WORLD_Z_NEG_AXIS );
        const mat4<F32> projMatrix = _worldAOCamera->setProjection( orthoRect, zPlanes );
        _worldAOCamera->updateLookAt();

        // Render large(ish) objects into shadow map
        RenderPassParams params = {};
        params._sourceNode = nullptr;
        params._stagePass = ShadowMap::WorldAOStagePass();
        params._target = _drawBufferDepth._targetID;
        params._maxLoD = -1;
        params._refreshLightData = false;
        params._passName = "WorldAOPass";
        params._enableDeformation = false;
        params._pendingShaderOut = &_worldAOPendingCasters;
        params._cullFlags = ShadowMap::WorldAOCullFlags();
        params._minExtents.set( g_minExtentsFactors[1] );
        params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
        params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "World AO Render Pass";
        _context.context().kernel().renderPassManager()->doCustomPass( _worldAOCamera, params, bufferInOut, memCmdInOut );

        const RenderTargetHandle& handle = ShadowMap::getShadowMap( _type );

        GFX::BlitRenderTargetCommand blitRenderTargetCommand = {};
        blitRenderTargetCommand._source = _drawBufferDepth._targetID;
        blitRenderTargetCommand._destination = handle._targetID;
        blitRenderTargetCommand._params.emplace_back( ShadowMap::ScratchBlit( ShadowMap::WORLD_AO_LAYER_INDEX ) );

        GFX::EnqueueCommand( bufferInOut, blitRenderTargetCommand );

        // Blur the top-down coverage map.
        blurTarget( ShadowMap::WORLD_AO_LAYER_INDEX, 1u, bufferInOut );

        GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
        computeMipMapsCommand._texture = handle._rt->getAttachment( RTAttachmentType::COLOUR )->texture();
        computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
        SubRange& layerRange = computeMipMapsCommand._layerRange;
        layerRange._offset = ShadowMap::WORLD_AO_LAYER_INDEX;
        layerRange._count = 1u;
        GFX::EnqueueCommand( bufferInOut, computeMipMapsCommand );

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

        const mat4<F32> worldAOVP = mat4<F32>::Multiply( projMatrix, viewMatrix );
        _worldAOVP = ShadowMap::BiasedViewProjection( worldAOVP );
        return _worldAOVP;
    }

    void CascadedShadowMapsGenerator::blurTarget( const U16 layerOffset, const U16 layerCount, GFX::CommandBuffer & bufferInOut, const U8 firstLayer )
    {
        const RenderTargetHandle& rtHandle = ShadowMap::getShadowMap( _type );
        const auto& shadowAtt = rtHandle._rt->getAttachment( RTAttachmentType::COLOUR );

        auto constants = _shaderConstants;
        const F32 blurScale = ShadowBlurTexelScale(firstLayer);
        constants.data[0]._vec[2] *= blurScale;
        constants.data[0]._vec[3] *= blurScale;
        for (auto& offsets : constants.data[1]._vec)
        {
            offsets *= blurScale;
        }

        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
        beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
        beginRenderPassCmd._descriptor._writeLayers[to_base(RTColourAttachmentSlot::SLOT_0)]._layer._count = ALL_LAYERS;

        // Blur horizontally
        beginRenderPassCmd._target = _blurBuffer._targetID;
        beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_HORIZONTAL";
        GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );

        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = layerCount == 1u ? _blurPipelineAO : _blurPipelineCSM;
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, shadowAtt->texture(), shadowAtt->_descriptor._sampler );
        }

        constants.data[0]._vec[1].x = 0.f;
        constants.data[0]._vec[1].y = to_F32( layerCount );
        constants.data[0]._vec[1].z = to_F32( layerOffset );
        constants.data[0]._vec[1].w = 0.f;

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData = constants;

        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );

        // Blur vertically
        beginRenderPassCmd._target = rtHandle._targetID;
        beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_VERTICAL";
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, false };

        GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );
        const auto& blurAtt = _blurBuffer._rt->getAttachment( RTAttachmentType::COLOUR );
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, blurAtt->texture(), blurAtt->_descriptor._sampler );
        }

        constants.data[0]._vec[1].x = 1.f;
        constants.data[0]._vec[1].y = to_F32( layerCount );
        constants.data[0]._vec[1].z = 0.f;
        constants.data[0]._vec[1].w = to_F32( layerOffset );

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData = constants;

        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
    }

    void CascadedShadowMapsGenerator::finishRender()
    {
        for (auto& cache : _staticLayers)
        {
            if (cache._rebuilt)
            {
                cache._state.complete(cache._pendingKey, cache._pendingCasters);
                *cache._pendingOut |= cache._pendingCasters;
                cache._pendingOut = nullptr;
                cache._rebuilt = false;
            }
            if (cache._combinedBounds != nullptr)
            {
                cache._combinedBounds->add(cache._staticBounds);
            }
            cache._combinedBounds = nullptr;
        }
    }

    void CascadedShadowMapsGenerator::updateMSAASampleCount( const U8 sampleCount )
    {
        if ( _context.context().config().rendering.shadowMapping.csm.MSAASamples != sampleCount )
        {
            _context.context().config().rendering.shadowMapping.csm.MSAASamples = sampleCount;
            _drawBufferDepth._rt->updateSampleCount( sampleCount );
            ShadowMap::getShadowMapCache(_type)._rt->updateSampleCount(sampleCount);
            for (auto& cache : _staticLayers)
            {
                cache._state._valid = false;
            }
            _worldAOCache._valid = false;
        }
    }
}
