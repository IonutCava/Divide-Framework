#include "stdafx.h"

#include "Headers/CascadedShadowMapsGenerator.h"

#include "Rendering/Camera/Headers/Camera.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"

#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide
{

    namespace
    {
        Configuration::Rendering::ShadowMapping g_shadowSettings;
    }

    CascadedShadowMapsGenerator::CascadedShadowMapsGenerator( GFXDevice& context )
        : ShadowMapGenerator( context, ShadowType::LAYERED )
    {
        Console::printfn( Locale::Get( _ID( "LIGHT_CREATE_SHADOW_FB" ) ), "EVCSM" );

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
            shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "GS_MAX_INVOCATIONS %d", Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT ) );

            ResourceDescriptor blurDepthMapShader( Util::StringFormat( "GaussBlur_%d_invocations", Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT ) );
            blurDepthMapShader.waitForReady( true );
            blurDepthMapShader.propertyDescriptor( shaderDescriptor );

            _blurDepthMapShader = CreateResource<ShaderProgram>( context.context().kernel().resourceCache(), blurDepthMapShader );
            _blurDepthMapShader->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* )
                                                   {
                                                       PipelineDescriptor pipelineDescriptor = {};
                                                       pipelineDescriptor._stateHash = _context.get2DStateBlock();
                                                       pipelineDescriptor._shaderProgramHandle = _blurDepthMapShader->handle();
                                                       pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

                                                       _blurPipeline = _context.newPipeline( pipelineDescriptor );
                                                   } );
        }

        _shaderConstants.data[0]._vec[1].y = to_F32( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT );
        _shaderConstants.data[0]._vec[1].z = 0.f;
        _shaderConstants.data[0]._vec[1].w = 0.f;

        std::array<vec2<F32>*, 12> blurSizeConstants = {
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
        sampler.wrapUVW( TextureWrap::CLAMP_TO_EDGE );
        sampler.mipSampling( TextureMipSampling::NONE );

        sampler.anisotropyLevel( 0 );
        const size_t samplerHash = sampler.getHash();

        const TextureDescriptor& texDescriptor = rt->getAttachment( RTAttachmentType::COLOUR )->texture()->descriptor();
        // Draw FBO
        {
            // MSAA rendering is supported
            TextureDescriptor colourDescriptor( TextureType::TEXTURE_2D_ARRAY, texDescriptor.dataType(), texDescriptor.baseFormat() );
            colourDescriptor.layerCount( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT );
            colourDescriptor.msaaSamples( g_shadowSettings.csm.MSAASamples );
            colourDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            TextureDescriptor depthDescriptor( TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::RED, GFXImagePacking::DEPTH );
            depthDescriptor.layerCount( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT );
            depthDescriptor.msaaSamples( g_shadowSettings.csm.MSAASamples );
            depthDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            RenderTargetDescriptor desc = {};
            desc._attachments = 
            {
                InternalRTAttachmentDescriptor{ colourDescriptor, samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0},
                InternalRTAttachmentDescriptor{ depthDescriptor, samplerHash, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
            };

            desc._resolution = rt->getResolution();
            desc._name = "CSM_ShadowMap_Draw";
            desc._msaaSamples = g_shadowSettings.csm.MSAASamples;

            _drawBufferDepth = context.renderTargetPool().allocateRT( desc );
        }

        //Blur FBO
        {
            TextureDescriptor blurMapDescriptor( TextureType::TEXTURE_2D_ARRAY, texDescriptor.dataType(), texDescriptor.baseFormat(), texDescriptor.packing() );
            blurMapDescriptor.layerCount( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT );
            blurMapDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            RenderTargetDescriptor desc = {};
            desc._attachments = 
            {
                InternalRTAttachmentDescriptor{ blurMapDescriptor, samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
            };

            desc._name = "CSM_Blur";
            desc._resolution = rt->getResolution();

            _blurBuffer = _context.renderTargetPool().allocateRT( desc );
        }

        WAIT_FOR_CONDITION( _blurPipeline != nullptr );
    }

    CascadedShadowMapsGenerator::~CascadedShadowMapsGenerator()
    {
        if ( !_context.renderTargetPool().deallocateRT( _blurBuffer ) ||
             !_context.renderTargetPool().deallocateRT( _drawBufferDepth ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    CascadedShadowMapsGenerator::SplitDepths CascadedShadowMapsGenerator::calculateSplitDepths( DirectionalLightComponent& light, const vec2<F32> nearFarPlanes ) const noexcept
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

    void CascadedShadowMapsGenerator::applyFrustumSplits( DirectionalLightComponent& light, const Camera& shadowCamera, U8 numSplits ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const SplitDepths splitDepths = calculateSplitDepths( light, shadowCamera.snapshot()._zPlanes );

        const mat4<F32> invViewProj = GetInverse( shadowCamera.viewProjectionMatrix() );

        F32 appliedDiff = 0.0f;
        for ( U8 cascadeIterator = 0; cascadeIterator < numSplits; ++cascadeIterator )
        {
            Camera* lightCam = ShadowMap::shadowCameras( ShadowType::LAYERED )[cascadeIterator];

            const F32 prevSplitDistance = cascadeIterator == 0 ? 0.0f : splitDepths[cascadeIterator - 1];
            const F32 splitDistance = splitDepths[cascadeIterator];

            vec3<F32> frustumCornersWS[8]
            {
                {-1.0f,  1.0f, -1.0f},
                { 1.0f,  1.0f, -1.0f},
                { 1.0f, -1.0f, -1.0f},
                {-1.0f, -1.0f, -1.0f},
                {-1.0f,  1.0f,  1.0f},
                { 1.0f,  1.0f,  1.0f},
                { 1.0f, -1.0f,  1.0f},
                {-1.0f, -1.0f,  1.0f},
            };

            for ( vec3<F32>& corner : frustumCornersWS )
            {
                const vec4<F32> inversePoint = invViewProj * vec4<F32>( corner, 1.0f );
                corner.set( inversePoint / inversePoint.w );
            }

            for ( U8 i = 0; i < 4; ++i )
            {
                const vec3<F32> cornerRay = frustumCornersWS[i + 4] - frustumCornersWS[i];
                const vec3<F32> nearCornerRay = cornerRay * prevSplitDistance;
                const vec3<F32> farCornerRay = cornerRay * splitDistance;

                frustumCornersWS[i + 4] = frustumCornersWS[i] + farCornerRay;
                frustumCornersWS[i] = frustumCornersWS[i] + nearCornerRay;
            }

            vec3<F32> frustumCenter = VECTOR3_ZERO;
            for ( const vec3<F32>& corner : frustumCornersWS )
            {
                frustumCenter += corner;
            }
            frustumCenter /= 8.0f;

            F32 radius = 0.0f;
            for ( const vec3<F32>& corner : frustumCornersWS )
            {
                const F32 distance = (corner - frustumCenter).lengthSquared();
                radius = std::max( radius, distance );
            }
            radius = std::ceil( Sqrt( radius ) * 16.0f ) / 16.0f;
            radius += appliedDiff;

            vec3<F32> maxExtents( radius, radius, radius );
            vec3<F32> minExtents = -maxExtents;

            //Position the view matrix looking down the center of the frustum with an arbitrary light direction
            vec3<F32> lightPosition = frustumCenter - light.directionCache() * (light.csmNearClipOffset() - minExtents.z);
            mat4<F32> lightViewMatrix = lightCam->lookAt( lightPosition, frustumCenter, WORLD_Y_AXIS );

            if ( cascadeIterator > 0 && light.csmUseSceneAABBFit()[cascadeIterator] )
            {
                // Only meshes should be enough
                bool validResult = false;
                auto& prevPassResults = light.feedBackContainers()[cascadeIterator];
                if ( !prevPassResults.empty() )
                {
                    BoundingBox meshAABB{};
                    for ( auto& node : prevPassResults )
                    {
                        const SceneNode& sNode = node._node->getNode();
                        if ( sNode.type() == SceneNodeType::TYPE_SUBMESH)
                        {
                            meshAABB.add( node._node->get<BoundsComponent>()->getBoundingBox() );
                            validResult = true;
                        }
                    }

                    if ( validResult )
                    {
                        meshAABB.transform( lightViewMatrix );
                        appliedDiff = meshAABB.getHalfExtent().y - radius;
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
            }

            // Lets store the ortho rect in case we need it;
            const vec2<F32> clip
            {
                0.001f,
                maxExtents.z - minExtents.z
            };

            mat4<F32> lightOrthoMatrix
            {
                Rect<F32>
                {
                    minExtents.x,
                    maxExtents.x,
                    minExtents.y,
                    maxExtents.y
                },
                clip
            };

            // The rounding matrix that ensures that shadow edges do not shimmer
            // http://www.gamedev.net/topic/591684-xna-40---shimmering-shadow-maps/
            {
                const mat4<F32> shadowMatrix = mat4<F32>::Multiply( lightViewMatrix, lightOrthoMatrix );
                const vec4<F32> shadowOrigin = shadowMatrix *
                    vec4<F32>{0.0f, 0.0f, 0.0f, 1.0f } *
                    (g_shadowSettings.csm.shadowMapResolution * 0.5f);

                vec4<F32> roundedOrigin = shadowOrigin;
                roundedOrigin.round();

                lightOrthoMatrix.translate( vec3<F32>
                {
                    (roundedOrigin.xy - shadowOrigin.xy) * 2.0f / g_shadowSettings.csm.shadowMapResolution, 0.0f
                } );

                // Use our adjusted matrix for actual rendering
                lightCam->setProjection( lightOrthoMatrix, clip, true );
            }
            lightCam->updateLookAt();

            mat4<F32> lightVP = light.getShadowVPMatrix( cascadeIterator );
            mat4<F32>::Multiply( lightViewMatrix, lightOrthoMatrix, lightVP );

            light.setShadowLightPos( cascadeIterator, lightPosition );
            light.setShadowVPMatrix( cascadeIterator, mat4<F32>::Multiply( lightVP, MAT4_BIAS ) );
        }
    }

    void CascadedShadowMapsGenerator::render( const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        auto& dirLight = static_cast<DirectionalLightComponent&>(light);

        const U8 numSplits = dirLight.csmSplitCount();
        applyFrustumSplits( dirLight, playerCamera, numSplits );

        RenderPassParams params = {};
        params._sourceNode = light.getSGN();
        params._stagePass = { RenderStage::SHADOW, RenderPassType::COUNT, lightIndex, static_cast<RenderStagePass::VariantType>(light.getLightType()) };
        params._target = _drawBufferDepth._targetID;
        params._maxLoD = -1;
        params._refreshLightData = false;

        params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX]._enabled = true;
        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };

        params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand( bufferInOut, GFX::BeginDebugScopeCommand( Util::StringFormat( "Cascaded Shadow Pass Light: [ %d ]", lightIndex ).c_str(), lightIndex ) );

        RenderPassManager* rpm = _context.context().kernel().renderPassManager();

        constexpr F32 minExtentsFactors[]
        {
            0.025f,
            1.75f,
            75.0f,
            125.0f
        };

        GFX::EnqueueCommand<GFX::SetClippingStateCommand>( bufferInOut )->_negativeOneToOneDepth = true; //Ortho camera
        for ( I8 i = numSplits - 1; i >= 0 && i < numSplits; i-- )
        {
            params._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer = i;
            params._targetDescriptorMainPass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )]._layer = i;

            params._passName = Util::StringFormat( "CSM_PASS_%d", i ).c_str();
            params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);
            params._minExtents.set( minExtentsFactors[i] );
            if ( i > 0 && dirLight.csmUseSceneAABBFit()[i] )
            {
                STUBBED( "CascadedShadowMapsGenerator::render: Validate AABBFit for first cascade!" );
                params._feedBackContainer = &dirLight.feedBackContainers()[i];
                params._feedBackContainer->resize( 0 );
            }

            rpm->doCustomPass( ShadowMap::shadowCameras( ShadowType::LAYERED )[i], params, bufferInOut, memCmdInOut );
        }

        postRender( dirLight, bufferInOut );

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void CascadedShadowMapsGenerator::postRender( const DirectionalLightComponent& light, GFX::CommandBuffer& bufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const I32 layerOffset = to_I32( light.getShadowArrayOffset() );
        const I32 layerCount = to_I32( light.csmSplitCount() );

        const RenderTargetHandle& rtHandle = ShadowMap::getShadowMap( _type );

        GFX::BlitRenderTargetCommand* blitRenderTargetCommand = GFX::EnqueueCommand<GFX::BlitRenderTargetCommand>( bufferInOut );
        blitRenderTargetCommand->_source = _drawBufferDepth._targetID;
        blitRenderTargetCommand->_destination = rtHandle._targetID;
        for ( U8 i = 0u; i < light.csmSplitCount(); ++i )
        {
            blitRenderTargetCommand->_params.emplace_back(RTBlitEntry
            {
                ._input = {
                    ._layerOffset = i,
                    ._index = 0u
                },
                ._output = {
                    ._layerOffset = to_U16( layerOffset + i ),
                    ._index = 0u
                }
            });
        }

        // Now we can either blur our target or just skip to mipmap computation
        if ( g_shadowSettings.csm.enableBlurring )
        {
            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
            beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

            // Blur horizontally
            const auto& shadowAtt = rtHandle._rt->getAttachment( RTAttachmentType::COLOUR );

            beginRenderPassCmd._target = _blurBuffer._targetID;
            beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_HORIZONTAL";
            GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _blurPipeline;
            {
                auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                cmd->_usage = DescriptorSetUsage::PER_DRAW;
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, shadowAtt->texture()->getView(), shadowAtt->descriptor()._samplerHash );
            }

            _shaderConstants.data[0]._vec[1].x = 0.f;
            _shaderConstants.data[0]._vec[1].y = to_F32( layerCount );
            _shaderConstants.data[0]._vec[1].z = to_F32( layerOffset );
            _shaderConstants.data[0]._vec[1].w = 0.f;

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( _shaderConstants );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );

            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );

            // Blur vertically
            const auto& blurAtt = _blurBuffer._rt->getAttachment( RTAttachmentType::COLOUR );
            {
                auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                cmd->_usage = DescriptorSetUsage::PER_DRAW;
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, blurAtt->texture()->getView(), blurAtt->descriptor()._samplerHash );
            }

            beginRenderPassCmd._target = rtHandle._targetID;
            beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_VERTICAL";
            GFX::EnqueueCommand( bufferInOut, beginRenderPassCmd );

            _shaderConstants.data[0]._vec[1].x = 1.f;
            _shaderConstants.data[0]._vec[1].y = to_F32( layerCount );
            _shaderConstants.data[0]._vec[1].z = 0.f;
            _shaderConstants.data[0]._vec[1].w = to_F32( layerOffset );

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( _shaderConstants );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );

            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
        }
    }

    void CascadedShadowMapsGenerator::updateMSAASampleCount( const U8 sampleCount )
    {
        if ( _context.context().config().rendering.shadowMapping.csm.MSAASamples != sampleCount )
        {
            _context.context().config().rendering.shadowMapping.csm.MSAASamples = sampleCount;
            _drawBufferDepth._rt->updateSampleCount( sampleCount );
        }
    }
}
