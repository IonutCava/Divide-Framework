// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/LightPool.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/ProjectManager.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
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
    LightPool::ViewLightData::ViewLightData() = default;
    LightPool::ViewLightData::~ViewLightData() = default;
    LightPool::ViewLightData::ViewLightData(ViewLightData&&) noexcept = default;
    LightPool::ViewLightData& LightPool::ViewLightData::operator=(ViewLightData&&) noexcept = default;

    Handle<Texture>       LightPool::s_lightIconsTexture = INVALID_HANDLE<Texture>;
    Handle<ShaderProgram> LightPool::s_lightImpostorShader = INVALID_HANDLE<ShaderProgram>;
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
            // Holds info about the currently active shadow casting lights:
            // ViewProjection Matrices, View Space Position, etc
            bufferDescriptor._name = "LIGHT_SHADOW";
            bufferDescriptor._elementCount = 1;
            bufferDescriptor._elementSize = sizeof( ShadowProperties );
            s_shadowBuffer = context.gfx().newShaderBuffer( bufferDescriptor );
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

        s_shadowBuffer.reset();
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
            ViewLightDataList& views = _viewLightData[i];
            views.resize( TotalPassCountForStage( static_cast<RenderStage>( i ) ) );
            for ( ViewLightData& view : views )
            {
                view._properties.reserve( 32u );
                view._lights.reserve( 32u );
            }
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
        const auto& filter = _context.config().debug.renderFilter;
        const auto& renderState = parentScene().state()->renderState();
        const U32 renderMask = (filter.primitives ? toBit(0) : 0u) |
                               (filter.meshes ? toBit(1) : 0u) |
                               (filter.terrain ? toBit(2) : 0u) |
                               (renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY) ? toBit(3) : 0u) |
                               (renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME) ? toBit(4) : 0u) |
                               (filter.vegetation ? toBit(5) : 0u) |
                               (filter.water ? toBit(6) : 0u) |
                               (filter.sky ? toBit(7) : 0u) |
                               (filter.particles ? toBit(8) : 0u) |
                               (filter.grassInstances ? toBit(9) : 0u) |
                               (filter.treeInstances ? toBit(10) : 0u);
        const std::array<F32, 2> visibilityDistances{renderState.generalVisibility(), renderState.treeVisibility()};
        const std::array<U8, 3> sampleCounts{shadowSettings.csm.MSAASamples, shadowSettings.point.MSAASamples,
                                           shadowSettings.spot.MSAASamples};
        const bool renderPolicyChanged = _shadowRenderMask != renderMask || _shadowVisibilityDistances != visibilityDistances ||
                                         _shadowSampleCounts != sampleCounts;
        _shadowRenderMask = renderMask;
        _shadowVisibilityDistances = visibilityDistances;
        _shadowSampleCounts = sampleCounts;
        const std::array<F32, 3> rangeCameraEye{camera._eye.x, camera._eye.y, camera._eye.z};
        const bool rangeCameraMoved = _shadowRangeCameraEye != rangeCameraEye;
        _shadowRangeCameraEye = rangeCameraEye;
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
            if ( movementSnapshot._invalidateAll || renderPolicyChanged ||
                 ( rangeCameraMoved && light->getLightType() == LightType::SPOT ) ||
                 ( cameraChanged && light->getLightType() == LightType::DIRECTIONAL ) )
            {
                light->staticShadowsDirty( true );
                light->dynamicShadowsDirty( true );
            }
        }

        struct ShadowRefresh
        {
            Light* _light = nullptr;
            bool _pendingCasters = false;
        };
        std::array<ShadowRefresh, Config::Lighting::MAX_SHADOW_CASTING_LIGHTS> refreshes{};
        size_t refreshCount = 0u;
        RenderPassManager& passManager = *_context.kernel().renderPassManager();
        passManager.beginViewBatch( RenderStage::SHADOW, _context.kernel().projectManager()->playerPass(), bufferInOut );

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
            const ShadowRefreshPlan refreshPlan = BuildShadowRefreshPlan(
                layerCount, light->shadowLayersDirty(), cacheValid, light->staticShadowsDirty(), light->dynamicShadowsDirty());
            const U32 updateMask = refreshPlan.updateMask();
            const bool reuseCached = updateMask == 0u;
            if ( RenderSmokeTest::PerfModeRequested() && GFXDevice::FrameCount() % 64u == 0u )
            {
                Console::printfn( "[ShadowMetrics] frame={} type={} light={} name={} layers={} mask={} cacheValid={} fullInvalidation={} cameraChanged={} movedVolumes={} staticMask={} dynamicMask={}",
                                  GFXDevice::FrameCount(), TypeUtil::LightTypeToString( lType ), light->getShadowArrayOffset(),
                                  light->sgn()->name().c_str(), layerCount, updateMask, cacheValid, cacheInvalidated,
                                  cameraChanged, movementSnapshot._count, refreshPlan._staticMask, refreshPlan._dynamicMask );
            }
            ShadowRefresh& refresh = refreshes[refreshCount];
            if ( !reuseCached && !ShadowMap::generateShadowMaps( playerCamera, *light, refresh._pendingCasters, bufferInOut, memCmdInOut, refreshPlan ) )
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
                refresh._light = light;
                ++refreshCount;

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
        const WorldAOCacheKey worldAOKey
        {
            ._sceneGUID = parentScene().getGUID(),
            ._revision = parentScene().sceneGraph()->worldAORevision(),
            ._center = {camera._eye.x, camera._eye.z},
            ._height = {staticHeightRange.x, staticHeightRange.y},
            ._renderMask = renderMask
        };
        _shadowBufferData._worldAOVPMatrix = ShadowMap::generateWorldAO( worldAOKey, bufferInOut, memCmdInOut );
        passManager.finishViewBatch( RenderStage::SHADOW, memCmdInOut );
        ShadowMap::finishShadowMaps();
        for ( size_t i = 0u; i < refreshCount; ++i )
        {
            ShadowRefresh& refresh = refreshes[i];
            refresh._light->cleanShadowProperties();
            if ( refresh._pendingCasters )
            {
                refresh._light->dynamicShadowsDirty( true );
            }
        }

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

    LightPool::ViewLightData& LightPool::AcquireViewLightData( ViewLightDataList& views, size_t& usedCount )
    {
        if ( usedCount >= views.size() )
        {
            DIVIDE_UNEXPECTED_CALL_MSG( "View light snapshots exceed their configured stage capacity" );
        }
        return views[usedCount++];
    }

    U32 LightPool::LightBufferCapacity( const size_t lightCount ) noexcept
    {
        const U32 required = to_U32( std::min( lightCount, size_t{ Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME } ) );
        return nextPOW2( std::max( 32u, required ) );
    }

    U32 LightPool::uploadLightList( const RenderStage stage, const LightList& lights, const mat4<F32>& viewMatrix, ViewLightData& view )
    {
        const U8 stageIndex = to_U8( stage );
        U32 ret = 0u;

        auto& lightCount = _activeLightCount[stageIndex];
        view._properties.clear();
        view._lights.clear();
        const size_t capacity = std::min( lights.size(), size_t{ Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME } );
        view._properties.reserve( capacity );
        view._lights.reserve( capacity );

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

                LightProperties& temp = view._properties.emplace_back();
                view._lights.push_back( light );
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

    LightDataBinding LightPool::uploadLightData( const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const size_t stageIndex = to_size( stage );
        LightList& sortedLights = _sortedLights[stageIndex];

        ViewLightData& view = AcquireViewLightData( _viewLightData[stageIndex], _usedViewLightData[stageIndex] );
        const U32 lightCapacity = LightBufferCapacity( sortedLights.size() );
        if ( view._lightBuffer == nullptr || view._lightBuffer->getPrimitiveCount() < lightCapacity )
        {
            ShaderBufferDescriptor descriptor{};
            descriptor._ringBufferLength = (Config::MAX_FRAMES_IN_FLIGHT + 1u) * Config::MAX_LOCAL_PLAYER_COUNT;
            descriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
            descriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;
            descriptor._elementCount = lightCapacity;
            descriptor._elementSize = sizeof( LightProperties );
            Util::StringFormatTo( descriptor._name, "LIGHT_DATA_{}_VIEW_{}", stageIndex, _usedViewLightData[stageIndex] - 1u );
            view._lightBuffer = _context.gfx().newShaderBuffer( descriptor );
            if ( view._lightBuffer == nullptr )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Failed to allocate per-view light buffer" );
            }
        }
        if ( view._sceneBuffer == nullptr )
        {
            ShaderBufferDescriptor descriptor{};
            descriptor._ringBufferLength = (Config::MAX_FRAMES_IN_FLIGHT + 1u) * Config::MAX_LOCAL_PLAYER_COUNT;
            descriptor._usageType = BufferUsageType::CONSTANT_BUFFER;
            descriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;
            descriptor._elementCount = 1u;
            descriptor._elementSize = sizeof( SceneData );
            Util::StringFormatTo( descriptor._name, "LIGHT_SCENE_{}_VIEW_{}", stageIndex, _usedViewLightData[stageIndex] - 1u );
            view._sceneBuffer = _context.gfx().newShaderBuffer( descriptor );
            if ( view._sceneBuffer == nullptr )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Failed to allocate per-view scene buffer" );
            }
        }
        const U32 totalLightCount = uploadLightList( stage, sortedLights, cameraSnapshot._viewMatrix, view );

        SceneData& crtData = view._scene;
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
                memCmdInOut._bufferLocks.push_back( view._lightBuffer->writeData( { 0u, totalLightCount }, view._properties.data() ) );
            }
        }

        {
            PROFILE_SCOPE( "LightPool::UploadSceneDataToGPU", Profiler::Category::Graphics );
            memCmdInOut._bufferLocks.push_back( view._sceneBuffer->writeData( { 0u, 1u }, &crtData ) );
        }
        return { view._lightBuffer.get(), view._sceneBuffer.get() };
    }

    void LightPool::patchShadowIndices( GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( size_t stageIndex = 0u; stageIndex < to_size( RenderStage::COUNT ); ++stageIndex )
        {
            for ( size_t viewIndex = 0u; viewIndex < _usedViewLightData[stageIndex]; ++viewIndex )
            {
                ViewLightData& view = _viewLightData[stageIndex][viewIndex];
                const U32 lightCount = to_U32( view._properties.size() );
                if ( PatchLightShadowIndices( view._properties, view._lights ) )
                {
                    memCmdInOut._bufferLocks.push_back( view._lightBuffer->writeData( { 0u, lightCount }, view._properties.data() ) );
                }
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

        _usedViewLightData.fill( 0u );
        for ( ViewLightDataList& views : _viewLightData )
        {
            for ( ViewLightData& view : views )
            {
                if ( view._lightBuffer != nullptr )
                {
                    view._lightBuffer->incQueue();
                }
                if ( view._sceneBuffer != nullptr )
                {
                    view._sceneBuffer->incQueue();
                }
            }
        }
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

        const size_t viewCount = _usedViewLightData[to_base( RenderStage::DISPLAY )];
        const U32 totalLightCount = viewCount == 0u ? 0u :
            to_U32( _viewLightData[to_base( RenderStage::DISPLAY )][viewCount - 1u]._properties.size() );
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
