// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/LightPool.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/Lighting/Headers/LightUploadState.h"
#include "Scenes/Headers/Scene.h"

#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"

#include <bit>

namespace Divide
{
    Handle<Texture>       LightPool::s_lightIconsTexture = INVALID_HANDLE<Texture>;
    Handle<ShaderProgram> LightPool::s_lightImpostorShader = INVALID_HANDLE<ShaderProgram>;
    ShaderBuffer_uptr LightPool::s_lightBuffer = nullptr;
    ShaderBuffer_uptr LightPool::s_sceneBuffer = nullptr;
    ShaderBuffer_uptr LightPool::s_shadowBuffer = nullptr;
    std::array<U8, to_base( ShadowType::COUNT )> LightPool::s_shadowLocation =
    {{
        4,  //SINGLE
        5,  //LAYERED
        6   //CUBE
    } };

    namespace
    {
        FORCE_INLINE I32 GetMaxLights( const LightType type ) noexcept
        {
            switch ( type )
            {
                case LightType::DIRECTIONAL: return to_I32( Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS );
                case LightType::POINT: return to_I32( Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS );
                case LightType::SPOT: return to_I32( Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS );
                default:
                case LightType::COUNT: break;
            }

            return 0;
        }
    }

    bool LightPool::IsLightInViewFrustum( const Frustum& frustum, const Light* const light ) noexcept
    {
        I8 frustumPlaneCache = -1;
        return frustum.ContainsSphere( light->boundingVolume(), frustumPlaneCache ) != FrustumCollision::FRUSTUM_OUT;
    }

    void LightPool::InitStaticData( PlatformContext& context )
    {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._ringBufferLength = (Config::MAX_FRAMES_IN_FLIGHT + 1u) * Config::MAX_LOCAL_PLAYER_COUNT;
        bufferDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;

        {
            bufferDescriptor._name = "LIGHT_DATA";
            bufferDescriptor._elementCount = Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME * (to_base( RenderStage::COUNT ) - 1); ///< no shadows
            bufferDescriptor._elementSize = sizeof( LightProperties );
            // Holds general info about the currently active lights: position, colour, etc.
            s_lightBuffer = context.gfx().newShaderBuffer( bufferDescriptor );
        }
        {
            // Holds info about the currently active shadow casting lights:
            // ViewProjection Matrices, View Space Position, etc
            bufferDescriptor._name = "LIGHT_SHADOW";
            bufferDescriptor._elementCount = 1;
            bufferDescriptor._elementSize = sizeof( ShadowProperties );
            s_shadowBuffer = context.gfx().newShaderBuffer( bufferDescriptor );
        }
        {
            bufferDescriptor._name = "LIGHT_SCENE";
            bufferDescriptor._usageType = BufferUsageType::CONSTANT_BUFFER;
            bufferDescriptor._elementCount = to_base( RenderStage::COUNT ) - 1; ///< no shadows
            bufferDescriptor._elementSize = sizeof( SceneData );
            // Holds general info about the currently active scene: light count, ambient colour, etc.
            s_sceneBuffer = context.gfx().newShaderBuffer( bufferDescriptor );
        }
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "lightImpostorShader.glsl";

        ShaderModuleDescriptor geomModule = {};
        geomModule._moduleType = ShaderType::GEOMETRY;
        geomModule._sourceFile = "lightImpostorShader.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "lightImpostorShader.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back( vertModule );
        shaderDescriptor._modules.push_back( geomModule );
        shaderDescriptor._modules.push_back( fragModule );

        std::atomic_uint loadingTasks = 0u;
        ResourceDescriptor<ShaderProgram> lightImpostorShader( "lightImpostorShader", shaderDescriptor );
        lightImpostorShader.waitForReady( false );
        s_lightImpostorShader = CreateResource( lightImpostorShader, loadingTasks );

        ResourceDescriptor<Texture> iconImage( "LightIconTexture" );
        iconImage.assetLocation( Paths::g_imagesLocation );
        iconImage.assetName( "lightIcons.png" );
        iconImage.waitForReady( false );
        TextureDescriptor& iconDescriptor = iconImage._propertyDescriptor;
        iconDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        iconDescriptor._packing = GFXImagePacking::NORMALIZED_SRGB;

        s_lightIconsTexture = CreateResource( iconImage, loadingTasks );

        WAIT_FOR_CONDITION( loadingTasks.load() == 0u );
    }

    void LightPool::DestroyStaticData()
    {
        DestroyResource( s_lightImpostorShader );
        DestroyResource( s_lightIconsTexture );

        s_lightBuffer.reset();
        s_shadowBuffer.reset();
        s_sceneBuffer.reset();
    }

    LightPool::LightPool( Scene& parentScene, PlatformContext& context )
        : FrameListener( "LightPool", context.kernel().frameListenerMgr(), 231 )
        , SceneComponent( parentScene )
        , PlatformContextComponent( context )
        , _shadowPassTimer( Time::ADD_TIMER( "Shadow Pass Timer" ) )
    {
        for ( U8 i = 0u; i < to_U8( RenderStage::COUNT ); ++i )
        {
            _activeLightCount[i].fill( 0 );
            _sortedLights[i].reserve( Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME );
        }

        _lightTypeState.fill( true );
    }

    LightPool::~LightPool()
    {
        ShadowMap::reset();

        LockGuard<SharedMutex> w_lock( _lightLock );
        setDebugLightLocked( nullptr );
        for ( const LightList& lightList : _lights )
        {
            if ( !lightList.empty() )
            {
                Console::errorfn( LOCALE_STR( "ERROR_LIGHT_POOL_LIGHT_LEAKED" ) );
            }
        }
    }


    bool LightPool::clear() noexcept
    {
        return _lights.empty();
    }

    bool LightPool::frameStarted( [[maybe_unused]] const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        return true;
    }

    bool LightPool::frameEnded( [[maybe_unused]] const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        LockGuard<SharedMutex> w_lock( _movedSceneVolumesLock );
        const size_t consumed = _shadowMovementBatch.finish( _movedSceneVolumes.size() );
        if ( consumed > 0u )
        {
            _movedSceneVolumes.erase( _movedSceneVolumes.begin(), _movedSceneVolumes.begin() + consumed );
        }
        return true;
    }

    bool LightPool::addLight( Light& light )
    {
        const LightType type = light.getLightType();
        const U32 lightTypeIdx = to_base( type );

        LockGuard<SharedMutex> r_lock( _lightLock );
        if ( findLightLocked( light.getGUID(), type ) != end( _lights[lightTypeIdx] ) )
        {

            Console::errorfn( LOCALE_STR( "ERROR_LIGHT_POOL_DUPLICATE" ),
                             light.getGUID() );
            return false;
        }

        _lights[lightTypeIdx].emplace_back( &light );
        _totalLightCount += 1u;

        return true;
    }

    // try to remove any leftover lights
    bool LightPool::removeLight( const Light& light )
    {
        LockGuard<SharedMutex> lock( _lightLock );
        const LightList::const_iterator it = findLightLocked( light.getGUID(), light.getLightType() );

        if ( it == end( _lights[to_U32( light.getLightType() )] ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_LIGHT_POOL_REMOVE_LIGHT" ),
                             light.getGUID() );
            return false;
        }

        if ( _debugLight == &light )
        {
            setDebugLightLocked( nullptr );
        }
        _lights[to_U32( light.getLightType() )].erase( it );  // remove it from the map
        _totalLightCount -= 1u;
        return true;
    }

    void LightPool::onVolumeMoved( const BoundingSphere& volume, const bool staticSource )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        LockGuard<SharedMutex> w_lock( _movedSceneVolumesLock );
        _movedSceneVolumes.push_back( { volume , staticSource } );
    }

    void LightPool::onVolumeChanged( const BoundingSphere& previous, const bool previousStatic,
                                     const BoundingSphere& current, const bool currentStatic )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        LockGuard<SharedMutex> w_lock( _movedSceneVolumesLock );
        AppendShadowVolumeChange( _movedSceneVolumes, previous, previousStatic, current, currentStatic );
    }

    void LightPool::invalidateShadowLayers(const BoundingSphere& volume, const U32 layerMask, const LightType lightType,
                                           const std::optional<BoundingSphere>& influenceBounds)
    {
        assert(layerMask != 0u);
        LockGuard<SharedMutex> w_lock(_movedSceneVolumesLock);
        _movedSceneVolumes.push_back({volume, false, layerMask, lightType, influenceBounds});
    }

    //ToDo: Generate shadow maps in parallel - Ionut
    void LightPool::generateShadowMaps( const Camera& playerCamera, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Time::ScopedTimer timer( _shadowPassTimer );

        ShadowPassSlots<to_base( LightType::COUNT )> slots( Config::Lighting::MAX_SHADOW_CASTING_LIGHTS );

        ShadowMap::resetShadowMaps( );

        const Frustum& camFrustum = playerCamera.getFrustum();
        const CameraSnapshot& camera = playerCamera.snapshot();
        const auto& shadowSettings = _context.config().rendering.shadowMapping;
        const bool cameraChanged = _directionalShadowCamera.update(
            camera._viewMatrix, camera._projectionMatrix, camera._zPlanes.x, camera._zPlanes.y );

        constexpr U8 stageIndex = to_U8( RenderStage::SHADOW );
        LightList& sortedLights = _sortedLights[stageIndex];

        ShadowMovementBatch::Snapshot movementSnapshot{};
        {
            LockGuard<SharedMutex> lock( _movedSceneVolumesLock );
            movementSnapshot = _shadowMovementBatch.begin( _movedSceneVolumes.size() );
        }

        for ( Light* light : sortedLights )
        {
            light->shadowPropertyIndex( -1 );
            if ( movementSnapshot._invalidateAll ||
                 ( cameraChanged && light->getLightType() == LightType::DIRECTIONAL ) )
            {
                light->staticShadowsDirty( true );
                light->dynamicShadowsDirty( true );
            }
        }

        for ( Light* light : sortedLights )
        {
            const LightType lType = light->getLightType();

            // Skip non-shadow casting lights (and free up resources if any are used by it)
            if ( !light->enabled() || !light->castsShadows() )
            {
                const U16 crtShadowIOffset = light->getShadowArrayOffset();
                if ( crtShadowIOffset != U16_MAX )
                {
                    DIVIDE_EXPECTED_CALL( ShadowMap::freeShadowMapOffset( *light ) );

                    light->setShadowArrayOffset( U16_MAX );
                }
                continue;
            }

            // Remember scene changes even for lights that are not selected this frame.
            const bool cacheInvalidated = isShadowCacheInvalidated( light, movementSnapshot._count );
            const U32 typeIndex = to_U32( lType );
            const U32 typeLimit = to_U32( GetMaxLights( lType ) );
            const bool shadowTypeEnabled = lType == LightType::DIRECTIONAL ? shadowSettings.csm.enabled :
                                           lType == LightType::POINT ? shadowSettings.point.enabled :
                                           lType == LightType::SPOT && shadowSettings.spot.enabled;
            if ( !_lightTypeState[typeIndex] || !shadowTypeEnabled || light->range() <= 0.f ||
                 !slots.hasCapacity( typeIndex, typeLimit ) || !IsLightInViewFrustum( camFrustum, light ) )
            {
                continue;
            }

            const bool cacheValid = ShadowMap::tryReuseShadowMaps( *light );
            const U8 layerCount = to_U8(ShadowMap::getLightLayerRequirements(*light));
            const U32 updateMask = ShadowLayerUpdateMask(layerCount, light->shadowLayersDirty(), cacheValid, cacheInvalidated);
            const bool reuseCached = updateMask == 0u;
            bool pendingCasters = false;
            if ( !reuseCached && !ShadowMap::generateShadowMaps( playerCamera, *light, pendingCasters, bufferInOut, memCmdInOut, updateMask ) )
            {
                continue;
            }
            const I32 shadowIndex = slots.reserve( typeIndex, typeLimit );
            assert( shadowIndex >= 0 );
            light->shadowPropertyIndex( shadowIndex );

            const Light::ShadowProperties& propsSource = light->getShadowProperties();
            switch ( lType )
            {
                case LightType::POINT:
                {
                    PointShadowProperties& propsTarget = _shadowBufferData._pointLights[shadowIndex];
                    propsTarget._details = propsSource._lightDetails;
                    propsTarget._position = propsSource._lightPosition[0];
                } break;
                case LightType::SPOT:
                {
                    SpotShadowProperties& propsTarget = _shadowBufferData._spotLights[shadowIndex];
                    propsTarget._details = propsSource._lightDetails;
                    propsTarget._vpMatrix = propsSource._lightVP[0];
                    propsTarget._position = propsSource._lightPosition[0];
                } break;
                case LightType::DIRECTIONAL:
                {
                    CSMShadowProperties& propsTarget = _shadowBufferData._dirLights[shadowIndex];
                    propsTarget._details = propsSource._lightDetails;

                    for ( U8 i = 0u; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i)
                    {
                        std::memcpy( propsTarget._position[i]._v, propsSource._lightPosition[i]._v, sizeof(F32) * 4u);
                    }

                    for ( U8 i = 0u; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i)
                    {
                        std::memcpy( propsTarget._vpMatrix[i].m, propsSource._lightVP[i].m, sizeof(F32) * 16u );
                    }
                } break;

                default:
                case LightType::COUNT:
                    DIVIDE_UNEXPECTED_CALL();
            }

            if ( !reuseCached )
            {
                light->cleanShadowProperties();
                if ( pendingCasters )
                {
                    light->dynamicShadowsDirty( true );
                }

                GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
                computeMipMapsCommand._texture = ShadowMap::getShadowMap( lType )._rt->getAttachment( RTAttachmentType::COLOUR )->texture();
                computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
                ForEachShadowLayerRange(updateMask, layerCount, [&](const U8 first, const U8 count)
                {
                    computeMipMapsCommand._layerRange = {to_U16(light->getShadowArrayOffset() + first), count};
                    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);
                });
            }
        }

        BoundingBox staticBounds{};
        float2 staticHeightRange{ 0.f, 0.f };
        if ( parentScene().sceneGraph()->staticGeometryBounds( staticBounds ) )
        {
            staticHeightRange.set( staticBounds._min.y, staticBounds._max.y );
        }
        _shadowBufferData._worldAOVPMatrix = ShadowMap::generateWorldAO( playerCamera, staticHeightRange, bufferInOut, memCmdInOut );

        _shadowBufferData._cameraViewDepthRow = camera._viewMatrix.getCol( 2 );
        memCmdInOut._bufferLocks.push_back( s_shadowBuffer->writeData( _shadowBufferData.data() ) );

        _shadowBufferDirty = true;

        ShadowMap::bindShadowMaps( bufferInOut );
    }

    void LightPool::debugLight( Light* light )
    {
        I64 lightGUID = -1;
        LightType lightType = LightType::COUNT;
        if ( light != nullptr )
        {
            SharedLock<SharedMutex> lock( _lightLock );
            for ( const LightList& lights : _lights )
            {
                const auto it = eastl::find( lights.cbegin(), lights.cend(), light );
                if ( it != lights.cend() )
                {
                    lightGUID = light->getGUID();
                    lightType = light->getLightType();
                    break;
                }
            }
            if ( lightType == LightType::COUNT )
            {
                Console::errorfn( "[Shadow Preview] Cannot select a light outside this pool." );
                return;
            }
        }

        if ( light != nullptr && !ShadowMap::prepareDebugViewShader( lightType ) )
        {
            return;
        }

        LockGuard<SharedMutex> lock( _lightLock );
        if ( light != nullptr )
        {
            const auto it = findLightLocked( lightGUID, lightType );
            if ( it == _lights[to_base( lightType )].cend() || *it != light )
            {
                Console::errorfn( "[Shadow Preview] The selected light was removed before the preview was ready." );
                return;
            }
        }
        setDebugLightLocked( light );
    }

    Light* LightPool::debugLight() const noexcept
    {
        SharedLock<SharedMutex> lock( _lightLock );
        return _debugLight;
    }

    void LightPool::setDebugLightLocked( Light* light )
    {
        if ( light == nullptr )
        {
            ShadowMap::clearDebugViewLight( context().gfx(), _debugLight );
        }
        else
        {
            ShadowMap::setDebugViewLight( context().gfx(), light );
        }
        _debugLight = light;
    }

    U32 LightPool::uploadLightList( const RenderStage stage, const LightList& lights, const mat4<F32>& viewMatrix )
    {
        const U8 stageIndex = to_U8( stage );
        U32 ret = 0u;

        auto& lightCount = _activeLightCount[stageIndex];
        LightData& crtData = _sortedLightProperties[stageIndex];

        SpotLightComponent* spot = nullptr;

        lightCount.fill( 0 );
        float3 tempColour;
        for ( Light* light : lights )
        {
            const LightType type = light->getLightType();
            const U32 typeIndex = to_U32( type );

            if ( _lightTypeState[typeIndex] && light->enabled() && light->range() > 0.f )
            {
                if ( !TryReserveLightUploadSlot( ret, Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME ) )
                {
                    break;
                }

                const bool isDir = type == LightType::DIRECTIONAL;
                const bool isOmni = type == LightType::POINT;
                const bool isSpot = type == LightType::SPOT;
                if ( isSpot )
                {
                    spot = static_cast<SpotLightComponent*>(light);
                }

                LightProperties& temp = crtData[ret - 1];
                _uploadedLights[stageIndex][ret - 1] = light;
                light->getDiffuseColour( tempColour );
                temp._diffuse.set( tempColour * light->intensity(), isSpot ? std::cos( Angle::to_RADIANS( spot->outerConeCutoffAngle() ) ) : 0.f );
                // Omni and spot lights have a position. Directional lights have this set to (0,0,0)
                temp._position.set( isDir ? VECTOR3_ZERO : (viewMatrix * float4( light->positionCache(), 1.0f )).xyz, light->range() );
                temp._direction.set( isOmni ? VECTOR3_ZERO : (viewMatrix * float4( light->directionCache(), 0.0f )).xyz, isSpot ? std::cos( Angle::to_RADIANS( spot->coneCutoffAngle() ) ) : 0.f );
                // The shadow slot (y) is assigned by the shadow pass, which may still be recording: see patchShadowIndices.
                temp._options.xyz = { typeIndex, -1,
                                      isSpot ? std::bit_cast<I32>( spot->coneSlantHeight() ) : 0 };

                ++lightCount[typeIndex];
            }
        }

        return ret;
    }

    // This should be called in a separate thread for each RenderStage
    void LightPool::sortLightData( const RenderStage stage, const CameraSnapshot& cameraSnapshot )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const U8 stageIndex = to_U8( stage );

        LightList& sortedLights = _sortedLights[stageIndex];
        sortedLights.resize( 0 );
        {
            SharedLock<SharedMutex> r_lock( _lightLock );
            sortedLights.reserve( _totalLightCount );
            for ( U8 i = 1; i < to_base( LightType::COUNT ); ++i )
            {
                sortedLights.insert( cend( sortedLights ), cbegin( _lights[i] ), cend( _lights[i] ) );
            }
        }

        const float3& eyePos = cameraSnapshot._eye;
        const auto lightSortCbk = [&eyePos]( Light* a, Light* b ) noexcept
        {
            return  a->getLightType() < b->getLightType() ||
                   (a->getLightType() == b->getLightType() &&
                    a->distanceSquared( eyePos ) < b->distanceSquared( eyePos ));
        };

        PROFILE_SCOPE( "LightPool::SortLights", Profiler::Category::Scene );
        if ( sortedLights.size() > LightList::kMaxSize )
        {
            UNSEQ_STD_SORT( begin( sortedLights ), end( sortedLights ), lightSortCbk );
        }
        else
        {
            eastl::sort( begin( sortedLights ), end( sortedLights ), lightSortCbk );
        }
        {
            SharedLock<SharedMutex> r_lock( _lightLock );
            const LightList& dirLights = _lights[to_base( LightType::DIRECTIONAL )];
            sortedLights.insert( begin( sortedLights ), cbegin( dirLights ), cend( dirLights ) );
        }

    }

    void LightPool::uploadLightData( const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const size_t stageIndex = to_size( stage );
        LightList& sortedLights = _sortedLights[stageIndex];

        U32& totalLightCount = _sortedLightPropertiesCount[stageIndex];
        totalLightCount = uploadLightList( stage, sortedLights, cameraSnapshot._viewMatrix );
        _lightDataUploaded[stageIndex] = true;

        SceneData& crtData = _sortedSceneProperties[stageIndex];
        crtData._globalData.set(
            _activeLightCount[stageIndex][to_base( LightType::DIRECTIONAL )],
            _activeLightCount[stageIndex][to_base( LightType::POINT )],
            _activeLightCount[stageIndex][to_base( LightType::SPOT )],
            0u );

        if ( !sortedLights.empty() )
        {
            crtData._ambientColour.rgb = { 0.05f * sortedLights.front()->getDiffuseColour() };
        }
        else
        {
            crtData._ambientColour.set( DefaultColours::BLACK );
        }

        {
            PROFILE_SCOPE( "LightPool::UploadLightDataToGPU", Profiler::Category::Graphics );
            if ( ShouldUploadLightData( totalLightCount ) )
            {
                memCmdInOut._bufferLocks.push_back(s_lightBuffer->writeData( { stageIndex * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, totalLightCount }, &_sortedLightProperties[stageIndex] ) );
            }
        }

        {
            PROFILE_SCOPE( "LightPool::UploadSceneDataToGPU", Profiler::Category::Graphics );
            memCmdInOut._bufferLocks.push_back(s_sceneBuffer->writeData( { stageIndex, 1 }, &crtData ) );
        }
    }

    void LightPool::patchShadowIndices( GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( size_t stageIndex = 0u; stageIndex < to_size( RenderStage::COUNT ); ++stageIndex )
        {
            if ( !_lightDataUploaded[stageIndex] )
            {
                continue;
            }
            _lightDataUploaded[stageIndex] = false;

            const U32 lightCount = _sortedLightPropertiesCount[stageIndex];
            if ( !ShouldUploadLightData( lightCount ) )
            {
                continue;
            }

            LightData& crtData = _sortedLightProperties[stageIndex];
            const UploadedLights& lights = _uploadedLights[stageIndex];
            bool anyShadowCaster = false;
            for ( U32 i = 0u; i < lightCount; ++i )
            {
                const Light* light = lights[i];
                const I32 shadowIndex = light->castsShadows() ? light->shadowPropertyIndex() : -1;
                crtData[i]._options.y = shadowIndex;
                anyShadowCaster = anyShadowCaster || shadowIndex != -1;
            }

            if ( anyShadowCaster )
            {
                memCmdInOut._bufferLocks.push_back( s_lightBuffer->writeData( { stageIndex * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, lightCount }, &crtData ) );
            }
        }
    }

    [[nodiscard]] bool LightPool::isShadowCacheInvalidated( Light* const light, const size_t movedVolumeCount )
    {
        const bool rangeChanged = light->_shadowRangeState.update( light->range() );
        bool directionalSettingsChanged = false;
        if ( light->getLightType() == LightType::DIRECTIONAL )
        {
            auto& directional = *static_cast<DirectionalLightComponent*>( light );
            directionalSettingsChanged = directional._shadowDirectionalSettings.update(
                directional.csmNearClipOffset(), directional.csmUseSceneAABBFit() );
        }
        if ( rangeChanged || directionalSettingsChanged )
        {
            light->staticShadowsDirty( true );
            light->dynamicShadowsDirty( true );
        }

        if ( movedVolumeCount > 0u )
        {
            const BoundingSphere& lightBounds = light->boundingVolume();
            SharedLock<SharedMutex> r_lock( _movedSceneVolumesLock );
            assert( movedVolumeCount <= _movedSceneVolumes.size() );
            for ( size_t i = 0u; i < movedVolumeCount; ++i )
            {
                const MovingVolume& volume = _movedSceneVolumes[i];
                if (!volume.affectsLight(light->getLightType(), lightBounds))
                {
                    continue;
                }
                if (volume._layerMask != 0u)
                {
                    light->shadowLayersDirty(light->shadowLayersDirty() | volume._layerMask);
                }
                else
                {
                    if ( volume._staticSource )
                    {
                        light->staticShadowsDirty( true );
                    }
                    else
                    {
                        light->dynamicShadowsDirty( true );
                    }
                    if ( light->staticShadowsDirty() && light->dynamicShadowsDirty() )
                    {
                        return true;
                    }
                }
            }
        }
        return light->staticShadowsDirty() || light->dynamicShadowsDirty();
    }

    void LightPool::preRenderAllPasses( const Camera* playerCamera )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        constexpr U16 k_parallelSortThreshold = 16u;

        SharedLock<SharedMutex> r_lock( _lightLock );
        if ( _lights.size() > k_parallelSortThreshold )
        {
            UNSEQ_STD_FOR_EACH( std::cbegin( _lights ),
                                std::cend( _lights ),
                                [playerCamera](const LightList& lightList)
                                {
                                    for (Light* light : lightList)
                                    {
                                        light->updateBoundingVolume(playerCamera);
                                    }
                                });
        }
        else
        {
            for ( const LightList& list : _lights )
            {
                for (Light* light : list)
                {
                    light->updateBoundingVolume(playerCamera);
                }
            }
        }

        if ( _shadowBufferDirty )
        {
            s_shadowBuffer->incQueue();
            _shadowBufferDirty = false;
        }

        s_lightBuffer->incQueue();
        s_sceneBuffer->incQueue();
    }

    void LightPool::drawLightImpostors( GFX::CommandBuffer& bufferInOut ) const
    {
        if ( !lightImpostorsEnabled() )
        {
           return;
        }

        static const SamplerDescriptor iconSampler = {
            ._mipSampling = TextureMipSampling::NONE,
            ._wrapU = TextureWrap::REPEAT,
            ._wrapV = TextureWrap::REPEAT,
            ._wrapW = TextureWrap::REPEAT,
            ._anisotropyLevel = 0u
        };

        const U32 totalLightCount = _sortedLightPropertiesCount[to_U8( RenderStage::DISPLAY )];
        if ( totalLightCount > 0u )
        {
            PipelineDescriptor pipelineDescriptor{};
            pipelineDescriptor._shaderProgramHandle = s_lightImpostorShader;
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _context.gfx().newPipeline( pipelineDescriptor );
            {
                auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
                cmd->_usage = DescriptorSetUsage::PER_DRAW;

                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, s_lightIconsTexture, iconSampler );
            }

            GenericDrawCommand& drawCmd = GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            drawCmd._drawCount = to_U16( totalLightCount );
        }
    }

}
