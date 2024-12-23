

#include "Headers/ShadowMap.h"
#include "Headers/CascadedShadowMapsGenerator.h"
#include "Headers/CubeShadowMapGenerator.h"
#include "Headers/SingleShadowMapGenerator.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/SceneState.h"

#include "Managers/Headers/ProjectManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"

namespace Divide
{
    Mutex ShadowMap::s_shadowMapUsageLock;
    std::array<ShadowMap::LayerLifetimeMask, to_base( ShadowType::COUNT )> ShadowMap::s_shadowMapLifetime;
    vector<std::unique_ptr<ShadowMapGenerator>> ShadowMap::s_shadowMapGenerators;

    vector<DebugView_ptr> ShadowMap::s_debugViews;
    std::array<RenderTargetHandle, to_base( ShadowType::COUNT )> ShadowMap::s_shadowMaps;
    std::array<RenderTargetHandle, to_base( ShadowType::COUNT )> ShadowMap::s_shadowMapCaches;
    Light* ShadowMap::s_shadowPreviewLight = nullptr;

    std::array<U16, to_base( ShadowType::COUNT )> ShadowMap::s_shadowPassIndex;
    std::array<ShadowMap::ShadowCameraPool, to_base( ShadowType::COUNT )> ShadowMap::s_shadowCameras;

    ShadowMapGenerator::ShadowMapGenerator( GFXDevice& context, const ShadowType type ) noexcept
        : _context( context ),
        _type( type )
    {
    }

    ShadowType ShadowMap::getShadowTypeForLightType( const LightType type ) noexcept
    {
        switch ( type )
        {
            case LightType::DIRECTIONAL: return ShadowType::CSM;
            case LightType::POINT: return ShadowType::CUBEMAP;
            case LightType::SPOT: return ShadowType::SINGLE;

            default:
            case LightType::COUNT: break;
        }

        return ShadowType::COUNT;
    }

    LightType ShadowMap::getLightTypeForShadowType( const ShadowType type ) noexcept
    {
        switch ( type )
        {
            case ShadowType::CSM: return LightType::DIRECTIONAL;
            case ShadowType::CUBEMAP: return LightType::POINT;
            case ShadowType::SINGLE: return LightType::SPOT;

            default:
            case ShadowType::COUNT: break;
        }

        return LightType::COUNT;
    }

    void ShadowMap::initShadowMaps( GFXDevice& context )
    {
        for ( U8 t = 0; t < to_base( ShadowType::COUNT ); ++t )
        {
            const ShadowType type = static_cast<ShadowType>(t);
            const U8 cameraCount = type == ShadowType::SINGLE ? 1 : type == ShadowType::CUBEMAP ? 6 : Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;

            for ( U32 i = 0; i < cameraCount; ++i )
            {
                s_shadowCameras[t].emplace_back( Camera::CreateCamera( Util::StringFormat( "ShadowCamera_{}_{}", Names::shadowType[t], i ).c_str(), Camera::Mode::STATIC));
            }
        }

        Configuration::Rendering::ShadowMapping& settings = context.context().config().rendering.shadowMapping;

        if ( !isPowerOfTwo( settings.csm.shadowMapResolution ) )
        {
            settings.csm.shadowMapResolution = nextPOW2( settings.csm.shadowMapResolution );
        }
        if ( !isPowerOfTwo( settings.spot.shadowMapResolution ) )
        {
            settings.spot.shadowMapResolution = nextPOW2( settings.spot.shadowMapResolution );
        }
        if ( !isPowerOfTwo( settings.point.shadowMapResolution ) )
        {
            settings.point.shadowMapResolution = nextPOW2( settings.point.shadowMapResolution );
        }

        SamplerDescriptor shadowMapSampler = {};
        shadowMapSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        shadowMapSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        shadowMapSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;

        SamplerDescriptor shadowMapSamplerCache = {};
        shadowMapSamplerCache._wrapU = TextureWrap::CLAMP_TO_EDGE;
        shadowMapSamplerCache._wrapV = TextureWrap::CLAMP_TO_EDGE;
        shadowMapSamplerCache._wrapW = TextureWrap::CLAMP_TO_EDGE;
        shadowMapSamplerCache._magFilter = TextureFilter::LINEAR;
        shadowMapSamplerCache._minFilter = TextureFilter::LINEAR;
        shadowMapSamplerCache._mipSampling = TextureMipSampling::NONE;
        shadowMapSamplerCache._anisotropyLevel = 0u;

        s_shadowMapGenerators.resize(0u);

        for ( U8 i = 0; i < to_U8( ShadowType::COUNT ); ++i )
        {
            switch ( static_cast<ShadowType>(i) )
            {
                case ShadowType::CSM:
                case ShadowType::SINGLE:
                {
                    const bool isCSM = i == to_U8( ShadowType::CSM );
                    if ( isCSM && !settings.csm.enabled )
                    {
                        continue;
                    }
                    if ( !isCSM && !settings.spot.enabled )
                    {
                        continue;
                    }

                    shadowMapSampler._anisotropyLevel = isCSM ? settings.csm.maxAnisotropicFilteringLevel : settings.spot.maxAnisotropicFilteringLevel;

                    // Default filters, LINEAR is OK for this
                    TextureDescriptor shadowMapDescriptor{};
                    shadowMapDescriptor._dataType = isCSM ? GFXDataFormat::FLOAT_32 : GFXDataFormat::FLOAT_16;
                    shadowMapDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
                    shadowMapDescriptor._baseFormat = GFXImageFormat::RG;
                    shadowMapDescriptor._packing = GFXImagePacking::UNNORMALIZED;
                    shadowMapDescriptor._mipMappingState = MipMappingState::MANUAL;
                    shadowMapDescriptor._layerCount = isCSM ? (Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT) + 1u /* world AO*/
                                                            : Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS;

                    RenderTargetDescriptor desc = {};
                    desc._resolution.set( to_U16( isCSM ? settings.csm.shadowMapResolution : settings.spot.shadowMapResolution ) );
                    {
                        desc._attachments = 
                        {
                            InternalRTAttachmentDescriptor{ shadowMapDescriptor, shadowMapSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
                        };

                        desc._name = isCSM ? "CSM_ShadowMap" : "Single_ShadowMap";
                        s_shadowMaps[i] = context.renderTargetPool().allocateRT( desc );
                    }
                    {
                        TextureDescriptor shadowMapCacheDescriptor = shadowMapDescriptor;
                        shadowMapCacheDescriptor._mipMappingState = MipMappingState::OFF;

                        desc._attachments = 
                        {
                            InternalRTAttachmentDescriptor{ shadowMapCacheDescriptor, shadowMapSamplerCache, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
                        };

                        desc._name = isCSM ? "CSM_ShadowMap_StaticCache" : "Single_ShadowMap_StaticCache";
                        s_shadowMapCaches[i] = context.renderTargetPool().allocateRT( desc );
                    }
                    if ( isCSM )
                    {
                        s_shadowMapGenerators.emplace_back(std::make_unique<CascadedShadowMapsGenerator>( context ) );
                    }
                    else
                    {
                        s_shadowMapGenerators.emplace_back(std::make_unique<SingleShadowMapGenerator>( context ));
                    }

                    s_shadowMapLifetime[i].resize( shadowMapDescriptor._layerCount );
                } break;
                case ShadowType::CUBEMAP:
                {
                    if ( !settings.point.enabled )
                    {
                        continue;
                    }

                    TextureDescriptor colourMapDescriptor{};
                    colourMapDescriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
                    colourMapDescriptor._dataType = GFXDataFormat::FLOAT_16;
                    colourMapDescriptor._baseFormat = GFXImageFormat::RG;
                    colourMapDescriptor._packing = GFXImagePacking::UNNORMALIZED;
                    colourMapDescriptor._layerCount = Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS;
                    colourMapDescriptor._mipMappingState = MipMappingState::MANUAL;
                    shadowMapSampler._mipSampling = TextureMipSampling::NONE;
                    shadowMapSampler._anisotropyLevel = 0u;

                    TextureDescriptor depthDescriptor{};
                    depthDescriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
                    depthDescriptor._dataType = GFXDataFormat::UNSIGNED_INT;
                    depthDescriptor._baseFormat = GFXImageFormat::RED;
                    depthDescriptor._packing = GFXImagePacking::DEPTH;
                    depthDescriptor._layerCount = colourMapDescriptor._layerCount;
                    depthDescriptor._mipMappingState = MipMappingState::MANUAL;

                    RenderTargetDescriptor desc = {};

                    desc._resolution.set( to_U16( settings.point.shadowMapResolution ) );
                    {
                        desc._name = "Cube_ShadowMap";
                        desc._attachments = 
                        {
                            InternalRTAttachmentDescriptor{ colourMapDescriptor, shadowMapSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0},
                            InternalRTAttachmentDescriptor{ depthDescriptor, shadowMapSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
                        };
                        s_shadowMaps[i] = context.renderTargetPool().allocateRT( desc );
                    }
                    {
                        TextureDescriptor shadowMapCacheDescriptor = colourMapDescriptor;
                        shadowMapCacheDescriptor._mipMappingState = MipMappingState::OFF;

                        desc._attachments = 
                        {
                            InternalRTAttachmentDescriptor{ shadowMapCacheDescriptor, shadowMapSamplerCache, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
                            InternalRTAttachmentDescriptor{ depthDescriptor, shadowMapSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
                        };

                        desc._name = "Cube_ShadowMap_StaticCache";
                        s_shadowMapCaches[i] = context.renderTargetPool().allocateRT( desc );
                    }

                    s_shadowMapGenerators.emplace_back(std::make_unique<CubeShadowMapGenerator>( context ));
                    s_shadowMapLifetime[i].resize( colourMapDescriptor._layerCount );
                } break;
                default:
                case ShadowType::COUNT: break;
            }
        }
    }

    void ShadowMap::destroyShadowMaps( GFXDevice& context )
    {
        for ( U8 t = 0u; t < to_base( ShadowType::COUNT ); ++t )
        {
            for ( auto& camera : s_shadowCameras[t] )
            {
                Camera::DestroyCamera( camera );
            }
            s_shadowCameras[t].clear();
        }

        s_debugViews.clear();

        for ( RenderTargetHandle& handle : s_shadowMaps )
        {
            if ( !context.renderTargetPool().deallocateRT( handle ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            handle._rt = nullptr;
        }

        for ( RenderTargetHandle& handle : s_shadowMapCaches )
        {
            if ( !context.renderTargetPool().deallocateRT( handle ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            handle._rt = nullptr;
        }

        s_shadowMapGenerators.resize( 0 );
    }

    void ShadowMap::reset()
    {
        LockGuard<Mutex> w_lock( s_shadowMapUsageLock );
        for (LayerLifetimeMask& lifetimeMask : s_shadowMapLifetime)
        {
            for ( auto& mast : lifetimeMask )
            {
                mast._lifetime = MAX_SHADOW_FRAME_LIFETIME;
                mast._lightGUID = -1;
            }
        }
    }

    void ShadowMap::resetShadowMaps( )
    {
        LockGuard<Mutex> w_lock( s_shadowMapUsageLock );
        for ( U32 i = 0u; i < to_base( ShadowType::COUNT ); ++i )
        {
            s_shadowPassIndex[i] = 0u;
        }
        for ( LayerLifetimeMask& lifetimePerType : s_shadowMapLifetime )
        {
            for ( ShadowLayerData& lifetime : lifetimePerType )
            {
                if ( lifetime._lifetime < MAX_SHADOW_FRAME_LIFETIME )
                {
                    ++lifetime._lifetime;
                }
            }
        }
    }

    void ShadowMap::bindShadowMaps( GFX::CommandBuffer& bufferInOut )
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_FRAME;

        for ( U8 i = 0u; i < to_base( ShadowType::COUNT ); ++i )
        {
            RenderTargetHandle& sm = s_shadowMaps[i];
            if ( sm._rt == nullptr )
            {
                continue;
            }

            const ShadowType shadowType = static_cast<ShadowType>(i);
            const U8 bindSlot = LightPool::GetShadowBindSlotOffset( shadowType );
            RTAttachment* shadowTexture = sm._rt->getAttachment( RTAttachmentType::COLOUR );
            DescriptorSetBinding& binding = AddBinding( cmd->_set, bindSlot, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, shadowTexture->texture(), shadowTexture->_descriptor._sampler );
        }

        DescriptorSetBinding& binding = AddBinding( cmd->_set, 9u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, LightPool::ShadowBuffer(), { 0u, 1u } );
    }

    bool ShadowMap::freeShadowMapOffset( const Light& light )
    {
        LockGuard<Mutex> w_lock( s_shadowMapUsageLock );
        return freeShadowMapOffsetLocked( light );
    }

    bool ShadowMap::freeShadowMapOffsetLocked( const Light& light )
    {
        const U16 layerOffset = light.getShadowArrayOffset();
        if ( layerOffset == U16_MAX )
        {
            return true;
        }

        const U32 layerRequirement = getLightLayerRequirements( light );
        const ShadowType sType = getShadowTypeForLightType( light.getLightType() );

        LayerLifetimeMask& lifetimeMask = s_shadowMapLifetime[to_U32( sType )];
        for ( U32 i = layerOffset; i < layerOffset + layerRequirement; ++i )
        {
            lifetimeMask[i]._lifetime = MAX_SHADOW_FRAME_LIFETIME;
            lifetimeMask[i]._lightGUID = -1;
        }

        return true;
    }

    bool ShadowMap::commitLayerRange( Light& light )
    {
        const U32 layerCount = getLightLayerRequirements( light );
        if ( layerCount == 0u ) [[unlikely]]
        {
            return false;
        }

        const U16 crtArrayOffset = light.getShadowArrayOffset();

        const ShadowType shadowType = getShadowTypeForLightType( light.getLightType() );

        LockGuard<Mutex> w_lock( s_shadowMapUsageLock );
        LayerLifetimeMask& lifetimeMask = s_shadowMapLifetime[to_U32( shadowType )];
        if ( crtArrayOffset != U16_MAX ) [[likely]]
        {
            bool valid = true;
            for ( U16 i = 0u; i < layerCount; ++i )
            {
                if ( lifetimeMask[crtArrayOffset + i]._lightGUID == light.getGUID() )
                {
                    lifetimeMask[crtArrayOffset + i]._lifetime = 0u;
                }
                else
                {
                    valid = false;
                }

            }
            if ( valid ) [[likely]]
            {
                return true;
            }
            else
            {
                freeShadowMapOffsetLocked( light );
            }
        }

        // Common case. Broken out for ease of debugging
        if ( layerCount == 1u )
        {
            for ( U16 i = 0u; i < lifetimeMask.size(); ++i )
            {
                if ( lifetimeMask[i]._lifetime >= MAX_SHADOW_FRAME_LIFETIME )
                {
                    lifetimeMask[i]._lifetime = 0u;
                    lifetimeMask[i]._lightGUID = light.getGUID();
                    light.setShadowArrayOffset( i );
                    return true;
                }
            }
        }
        else
        {
            // Really only happens for directional lights due to the way CSM is laid out
            const U16 availableLayers = to_U16( lifetimeMask.size() );
            bool found = false;
            for ( U16 i = 0u; i < availableLayers - layerCount; ++i )
            {
                for ( U16 j = i; j < i + layerCount; ++j )
                {
                    if ( lifetimeMask[j]._lifetime < MAX_SHADOW_FRAME_LIFETIME )
                    {
                        found = false;
                        break;
                    }

                    found = true;
                }
                if ( found )
                {
                    for ( U16 j = i; j < i + layerCount; ++j )
                    {
                        lifetimeMask[j]._lifetime = 0u;
                        lifetimeMask[j]._lightGUID = light.getGUID();
                    }
                    light.setShadowArrayOffset( i );
                    return true;
                }
            }
        }

        return false;
    }

    U32 ShadowMap::getLightLayerRequirements( const Light& light )
    {
        switch ( light.getLightType() )
        {
            case LightType::DIRECTIONAL: return to_U32( static_cast<const DirectionalLightComponent&>(light).csmSplitCount() );
            case LightType::SPOT:
            case LightType::POINT: return 1u;

            default:
            case LightType::COUNT: break;
        }

        return 0u;
    }

    bool ShadowMap::markShadowMapsUsed( Light& light )
    {
        if ( !commitLayerRange( light ) )
        {
            // If we don't have enough resources available, something went terribly wrong as we limit shadow casting lights per-type
            // and allocate enough slices for the worst case -Ionut
            DIVIDE_UNEXPECTED_CALL();
            return false;
        }

        return true;
    }

    bool ShadowMap::generateShadowMaps( const Camera& playerCamera, Light& light, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const U8 shadowTypeIdx = to_base( getShadowTypeForLightType( light.getLightType() ) );
        if ( s_shadowMapGenerators[shadowTypeIdx] == nullptr ) [[unlikely]]
        {
            // If we don't have enough resources available, something went terribly wrong as we limit shadow casting lights per-type
            // and allocate enough slices for the worst case -Ionut
            DIVIDE_UNEXPECTED_CALL();
            return false;
        }

            if ( markShadowMapsUsed( light ) ) [[likely]]
            {
                s_shadowMapGenerators[shadowTypeIdx]->render( playerCamera, light, s_shadowPassIndex[shadowTypeIdx]++, bufferInOut, memCmdInOut );
                return true;
            }

        return false;
    }

    void ShadowMap::generateWorldAO( const Camera& playerCamera, GFX::CommandBuffer& bufferInOut,GFX::MemoryBarrierCommand& memCmdInOut )
    {
        static_cast<CascadedShadowMapsGenerator*>(s_shadowMapGenerators[to_base( ShadowType::CSM )].get())->generateWorldAO( playerCamera , bufferInOut, memCmdInOut );
    }

    const RenderTargetHandle& ShadowMap::getShadowMap( const LightType type )
    {
        return getShadowMap( getShadowTypeForLightType( type ) );
    }

    const RenderTargetHandle& ShadowMap::getShadowMapCache( const LightType type )
    {
        return getShadowMapCache( getShadowTypeForLightType( type ) );
    }
    const RenderTargetHandle& ShadowMap::getShadowMap( const ShadowType type )
    {
        return s_shadowMaps[to_base( type )];
    }

    const RenderTargetHandle& ShadowMap::getShadowMapCache( const ShadowType type )
    {
        return s_shadowMapCaches[to_base( type )];
    }

    void ShadowMap::setMSAASampleCount( const ShadowType type, const U8 sampleCount )
    {
        if ( s_shadowMapGenerators[to_base( type )] != nullptr )
        {
            s_shadowMapGenerators[to_base( type )]->updateMSAASampleCount( sampleCount );
        }
    }

    void ShadowMap::setDebugViewLight( GFXDevice& context, Light* light )
    {
        if ( light == s_shadowPreviewLight )
        {
            return;
        }

        // Remove old views
        if ( !s_debugViews.empty() )
        {
            for ( const DebugView_ptr& view : s_debugViews )
            {
                context.removeDebugView( view.get() );
            }
            s_debugViews.clear();
        }

        s_shadowPreviewLight = light;

        // Add new views if needed
        if ( light != nullptr && light->castsShadows() )
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "baseVertexShaders.glsl";
            vertModule._variant = "FullScreenQuad";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "fbPreview.glsl";

            switch ( light->getLightType() )
            {
                case LightType::DIRECTIONAL:
                {

                    fragModule._variant = "Layered.LinearDepth";

                    ShaderProgramDescriptor shaderDescriptor = {};
                    shaderDescriptor._modules.push_back( vertModule );
                    shaderDescriptor._modules.push_back( fragModule );

                    ResourceDescriptor<ShaderProgram> shadowPreviewShader( "fbPreview.Layered.LinearDepth", shaderDescriptor );
                    shadowPreviewShader.waitForReady( true );
                    Handle<ShaderProgram> previewShader = CreateResource( shadowPreviewShader );
                    const U8 splitCount = static_cast<DirectionalLightComponent&>(*light).csmSplitCount();

                    constexpr I16 Base = 2;
                    for ( U8 i = 0; i < splitCount; ++i )
                    {
                        DebugView_ptr shadow = std::make_shared<DebugView>( to_I16( I16_MAX - 1 - splitCount + i ) );
                        shadow->_texture = getShadowMap( LightType::DIRECTIONAL )._rt->getAttachment( RTAttachmentType::COLOUR )->texture();
                        shadow->_sampler = getShadowMap( LightType::DIRECTIONAL )._rt->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
                        shadow->_shader = previewShader;
                        shadow->_shaderData.set( _ID( "layer" ), PushConstantType::INT, i + light->getShadowArrayOffset() );
                        shadow->_groupID = Base + to_I16( light->shadowPropertyIndex() );
                        shadow->_enabled = true;
                        Util::StringFormatTo( shadow->_name, "CSM_{}", i + light->getShadowArrayOffset() );
                        s_debugViews.push_back( shadow );
                    }
                } break;
                case LightType::SPOT:
                {
                    constexpr I16 Base = 22;
                    fragModule._variant = "Layered.LinearDepth";

                    ShaderProgramDescriptor shaderDescriptor = {};
                    shaderDescriptor._modules.push_back( vertModule );
                    shaderDescriptor._modules.push_back( fragModule );

                    ResourceDescriptor<ShaderProgram> shadowPreviewShader( "fbPreview.Layered.LinearDepth", shaderDescriptor );
                    shadowPreviewShader.waitForReady( true );

                    DebugView_ptr shadow = std::make_shared<DebugView>( to_I16( I16_MAX - 1 ) );
                    shadow->_texture = getShadowMap( LightType::SPOT )._rt->getAttachment( RTAttachmentType::COLOUR )->texture();
                    shadow->_sampler = getShadowMap( LightType::SPOT )._rt->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
                    shadow->_shader = CreateResource( shadowPreviewShader );
                    shadow->_shaderData.set( _ID( "layer" ), PushConstantType::INT, light->getShadowArrayOffset() );
                    shadow->_enabled = true;
                    shadow->_groupID = Base + to_I16( light->shadowPropertyIndex() );
                    Util::StringFormatTo( shadow->_name, "SM_{}", light->getShadowArrayOffset() );
                    s_debugViews.push_back( shadow );
                }break;
                case LightType::POINT:
                {
                    constexpr I16 Base = 222;
                    fragModule._variant = "Cube.Shadow";

                    ShaderProgramDescriptor shaderDescriptor = {};
                    shaderDescriptor._modules.push_back( vertModule );
                    shaderDescriptor._modules.back()._defines.emplace_back( "SPLAT_R_CHANNEL" );
                    shaderDescriptor._modules.push_back( fragModule );

                    ResourceDescriptor<ShaderProgram> shadowPreviewShader( "fbPreview.Cube.Shadow", shaderDescriptor );
                    shadowPreviewShader.waitForReady( true );
                    Handle<ShaderProgram> previewShader = CreateResource( shadowPreviewShader );

                    for ( U8 i = 0u; i < 6u; ++i )
                    {
                        DebugView_ptr shadow = std::make_shared<DebugView>( to_I16( I16_MAX - 1 - 6 + i ) );
                        shadow->_texture = getShadowMap( LightType::POINT )._rt->getAttachment( RTAttachmentType::COLOUR )->texture();
                        shadow->_sampler = getShadowMap( LightType::POINT )._rt->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
                        shadow->_shader = previewShader;
                        shadow->_shaderData.set( _ID( "layer" ), PushConstantType::INT, light->getShadowArrayOffset() );
                        shadow->_shaderData.set( _ID( "face" ), PushConstantType::INT, i );
                        shadow->_groupID = Base + to_I16( light->shadowPropertyIndex() );
                        shadow->_enabled = true;
                        Util::StringFormatTo( shadow->_name, "CubeSM_{}_face_{}", light->getShadowArrayOffset(), i );
                        s_debugViews.push_back( shadow );
                    }
                } break;

                default:
                case LightType::COUNT: break;
            }

            for ( const DebugView_ptr& view : s_debugViews )
            {
                context.addDebugView( view );
            }
        }
    }
} //namespace Divide
