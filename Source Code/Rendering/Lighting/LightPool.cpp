#include "stdafx.h"

#include "Headers/LightPool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"

#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"

namespace Divide {

std::array<U8, to_base(ShadowType::COUNT)> LightPool::_shadowLocation = { {
    3, //TextureUsage::SHADOW_SINGLE,
    4, //TextureUsage::SHADOW_LAYERED,
    5  //TextureUsage::SHADOW_CUBE
}};

namespace {
    constexpr U8 DataBufferRingSize = 4u;

    static size_t s_debugSamplerHash = 0u;

    FORCE_INLINE I32 GetMaxLights(const LightType type) noexcept {
        switch (type) {
            case LightType::DIRECTIONAL: return to_I32(Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS);
            case LightType::POINT      : return to_I32(Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS);
            case LightType::SPOT       : return to_I32(Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS);
            case LightType::COUNT      : break;
        }

        return 0;
    }

    U32 LightBufferIndex(const RenderStage stage) noexcept{
        assert(stage != RenderStage::SHADOW);
        return to_base(stage) - 1;
    }
}

bool LightPool::IsLightInViewFrustum(const Frustum& frustum, const Light* const light) noexcept {
    I8 frustumPlaneCache = -1;
    return frustum.ContainsSphere(light->boundingVolume(), frustumPlaneCache) != FrustumCollision::FRUSTUM_OUT;
}

LightPool::LightPool(Scene& parentScene, PlatformContext& context)
    : FrameListener("LightPool", context.kernel().frameListenerMgr(), 231),
      SceneComponent(parentScene),
      PlatformContextComponent(context),
     _shadowPassTimer(Time::ADD_TIMER("Shadow Pass Timer"))
{
    for (U8 i = 0; i < to_U8(RenderStage::COUNT); ++i) {
        _activeLightCount[i].fill(0);
        _sortedLights[i].reserve(Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME);
    }

    _lightTypeState.fill(true);

    init();
}

LightPool::~LightPool()
{
    s_debugSamplerHash = 0u;

    const SharedLock<SharedMutex> r_lock(_lightLock);
    for (const LightList& lightList : _lights) {
        if (!lightList.empty()) {
            Console::errorfn(Locale::Get(_ID("ERROR_LIGHT_POOL_LIGHT_LEAKED")));
        }
    }
}

void LightPool::init() {
    if (_init) {
        return;
    }

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
    bufferDescriptor._ringBufferLength = DataBufferRingSize;
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    
    {
        bufferDescriptor._name = "LIGHT_DATA_BUFFER";
        bufferDescriptor._bufferParams._elementCount = Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME * (to_base(RenderStage::COUNT) - 1); ///< no shadows
        bufferDescriptor._bufferParams._elementSize = sizeof(LightProperties);
        // Holds general info about the currently active lights: position, colour, etc.
        _lightBuffer = _context.gfx().newSB(bufferDescriptor);
    } 
    {
        // Holds info about the currently active shadow casting lights:
        // ViewProjection Matrices, View Space Position, etc
        bufferDescriptor._name = "LIGHT_SHADOW_BUFFER"; 
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._elementSize = sizeof(ShadowProperties);
        _shadowBuffer = _context.gfx().newSB(bufferDescriptor);
    }
    {
        bufferDescriptor._name = "LIGHT_SCENE_BUFFER";
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._elementCount = to_base(RenderStage::COUNT) - 1; ///< no shadows
        bufferDescriptor._bufferParams._elementSize = sizeof(SceneData);
        // Holds general info about the currently active scene: light count, ambient colour, etc.
        _sceneBuffer = _context.gfx().newSB(bufferDescriptor);
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
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(geomModule);
    shaderDescriptor._modules.push_back(fragModule);

    std::atomic_uint loadingTasks = 0u;
    ResourceDescriptor lightImpostorShader("lightImpostorShader");
    lightImpostorShader.propertyDescriptor(shaderDescriptor);
    lightImpostorShader.waitForReady(false);
    _lightImpostorShader = CreateResource<ShaderProgram>(_parentScene.resourceCache(), lightImpostorShader, loadingTasks);

    TextureDescriptor iconDescriptor(TextureType::TEXTURE_2D_ARRAY);
    iconDescriptor.layerCount(1u);
    iconDescriptor.srgb(true);

    ResourceDescriptor iconImage("LightIconTexture");
    iconImage.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    iconImage.assetName(ResourcePath("lightIcons.png"));
    iconImage.propertyDescriptor(iconDescriptor);
    iconImage.waitForReady(false);

    _lightIconsTexture = CreateResource<Texture>(_parentScene.resourceCache(), iconImage, loadingTasks);

    WAIT_FOR_CONDITION(loadingTasks.load() == 0u);

    _init = true;
}

bool LightPool::clear() noexcept {
    if (!_init) {
        return true;
    }
    _lightBuffer.reset();
    _shadowBuffer.reset();
    _sceneBuffer.reset();
    return _lights.empty();
}

bool LightPool::frameStarted([[maybe_unused]] const FrameEvent& evt) {
    OPTICK_EVENT();

    return true;
}

bool LightPool::frameEnded(const FrameEvent& evt) {
    OPTICK_EVENT();

    ScopedLock<SharedMutex> w_lock(_movedSceneVolumesLock);
    _movedSceneVolumes.resize(0);
    return true;
}

bool LightPool::addLight(Light& light) {
    const LightType type = light.getLightType();
    const U32 lightTypeIdx = to_base(type);

    ScopedLock<SharedMutex> r_lock(_lightLock);
    if (findLightLocked(light.getGUID(), type) != end(_lights[lightTypeIdx])) {

        Console::errorfn(Locale::Get(_ID("ERROR_LIGHT_POOL_DUPLICATE")),
                         light.getGUID());
        return false;
    }

    _lights[lightTypeIdx].emplace_back(&light);
    _totalLightCount += 1u;

    return true;
}

// try to remove any leftover lights
bool LightPool::removeLight(const Light& light) {
    ScopedLock<SharedMutex> lock(_lightLock);
    const LightList::const_iterator it = findLightLocked(light.getGUID(), light.getLightType());

    if (it == end(_lights[to_U32(light.getLightType())])) {
        Console::errorfn(Locale::Get(_ID("ERROR_LIGHT_POOL_REMOVE_LIGHT")),
                         light.getGUID());
        return false;
    }

    _lights[to_U32(light.getLightType())].erase(it);  // remove it from the map
    _totalLightCount -= 1u;
    return true;
}

void LightPool::onVolumeMoved(const BoundingSphere& volume, const bool staticSource) {
    OPTICK_EVENT();

    ScopedLock<SharedMutex> w_lock(_movedSceneVolumesLock);
    _movedSceneVolumes.push_back({ volume , staticSource });
}

//ToDo: Generate shadow maps in parallel - Ionut
void LightPool::generateShadowMaps(const Camera& playerCamera, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();

    Time::ScopedTimer timer(_shadowPassTimer);

    std::array<I32, to_base(LightType::COUNT)> indexCounter{};
    std::array<bool, to_base(LightType::COUNT)> shadowsGenerated{};

    ShadowMap::resetShadowMaps(bufferInOut);

    const Frustum& camFrustum = playerCamera.getFrustum();

    U32 totalShadowLightCount = 0u;

    constexpr U8 stageIndex = to_U8(RenderStage::SHADOW);
    LightList& sortedLights = _sortedLights[stageIndex];

    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    for (Light* light : sortedLights) {
        const LightType lType = light->getLightType();
        computeMipMapsCommand._texture = ShadowMap::getShadowMap(lType)._rt->getAttachment(RTAttachmentType::Colour, 0)->texture().get();

        // Skip non-shadow casting lights (and free up resources if any are used by it)
        if (!light->enabled() || !light->castsShadows()) {
            const U16 crtShadowIOffset = light->getShadowArrayOffset();
            if (crtShadowIOffset != U16_MAX) {
                if (!ShadowMap::freeShadowMapOffset(*light)) {
                    DIVIDE_UNEXPECTED_CALL();
                }
                light->setShadowArrayOffset(U16_MAX);
            }
            continue;
        }

        // We have a global shadow casting budget that we need to consider
        if (++totalShadowLightCount >= Config::Lighting::MAX_SHADOW_CASTING_LIGHTS) {
            break;
        }

        // Make sure we do not go over our shadow casting budget and only consider visible and cache invalidated lights
        I32& counter = indexCounter[to_base(lType)];
        if (counter == GetMaxLights(lType) || !IsLightInViewFrustum(camFrustum, light))
        {
            continue;
        }

        if (!isShadowCacheInvalidated(playerCamera.getEye(), light) && ShadowMap::markShadowMapsUsed(*light)) {
            continue;
        }

        // We have a valid shadow map update request at this point. Register our properties slot ...
        const I32 shadowIndex = counter++;
        light->shadowPropertyIndex(shadowIndex);

        // ... and update the shadow map
        if (!ShadowMap::generateShadowMaps(playerCamera, *light, bufferInOut, memCmdInOut)) {
            continue;
        }

        const Light::ShadowProperties& propsSource = light->getShadowProperties();
        vec2<U16>& layerRange = computeMipMapsCommand._layerRange;
        layerRange.min = std::min(layerRange.min, light->getShadowArrayOffset());

        switch (lType) {
            case LightType::POINT: {
                PointShadowProperties& propsTarget = _shadowBufferData._pointLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                propsTarget._position = propsSource._lightPosition[0];
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowArrayOffset() + 1u));
            }break;
            case LightType::SPOT: {
                SpotShadowProperties& propsTarget = _shadowBufferData._spotLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                propsTarget._vpMatrix = propsSource._lightVP[0];
                propsTarget._position = propsSource._lightPosition[0];
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowArrayOffset() + 1u));
            }break;
            case LightType::DIRECTIONAL: {
                CSMShadowProperties& propsTarget = _shadowBufferData._dirLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                std::memcpy(propsTarget._position.data(), propsSource._lightPosition.data(), sizeof(vec4<F32>) * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT);
                std::memcpy(propsTarget._vpMatrix.data(), propsSource._lightVP.data(), sizeof(mat4<F32>) * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT);
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowArrayOffset() + static_cast<DirectionalLightComponent*>(light)->csmSplitCount()));
            }break;
            case LightType::COUNT:
                DIVIDE_UNEXPECTED_CALL();
                break;
        }
        light->cleanShadowProperties();

        shadowsGenerated[to_base(lType)] = true;

        EnqueueCommand(bufferInOut, computeMipMapsCommand);
    }

    memCmdInOut._bufferLocks.push_back(_shadowBuffer->writeData(_shadowBufferData.data()));
    memCmdInOut._syncFlag = 1u;
    _shadowBufferDirty = true;

    ShadowMap::bindShadowMaps(bufferInOut);
}

void LightPool::debugLight(Light* light) {
    _debugLight = light;
    ShadowMap::setDebugViewLight(context().gfx(), _debugLight);
}

Light* LightPool::getLight(const I64 lightGUID, const LightType type) const {
    SharedLock<SharedMutex> r_lock(_lightLock);

    const LightList::const_iterator it = findLight(lightGUID, type);
    if (it != eastl::end(_lights[to_U32(type)])) {
        return *it;
    }

    DIVIDE_UNEXPECTED_CALL();
    return nullptr;
}

U32 LightPool::uploadLightList(const RenderStage stage, const LightList& lights, const mat4<F32>& viewMatrix) {
    const U8 stageIndex = to_U8(stage);
    U32 ret = 0u;

    auto& lightCount = _activeLightCount[stageIndex];
    LightData& crtData = _sortedLightProperties[stageIndex];

    SpotLightComponent* spot = nullptr;

    lightCount.fill(0);
    vec3<F32> tempColour;
    for (Light* light : lights) {
        const LightType type = light->getLightType();
        const U32 typeIndex = to_U32(type);

        if (_lightTypeState[typeIndex] && light->enabled() && light->range() > 0.f) {
            if (++ret > Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME) {
                break;
            }

            const bool isDir = type == LightType::DIRECTIONAL;
            const bool isOmni = type == LightType::POINT;
            const bool isSpot = type == LightType::SPOT;
            if (isSpot) {
                spot = static_cast<SpotLightComponent*>(light);
            }

            LightProperties& temp = crtData[ret - 1];
            light->getDiffuseColour(tempColour);
            temp._diffuse.set(tempColour * light->intensity(), isSpot ? std::cos(Angle::DegreesToRadians(spot->outerConeCutoffAngle())) : 0.f);
            // Omni and spot lights have a position. Directional lights have this set to (0,0,0)
            temp._position.set( isDir  ? VECTOR3_ZERO : (viewMatrix * vec4<F32>(light->positionCache(),  1.0f)).xyz, light->range());
            temp._direction.set(isOmni ? VECTOR3_ZERO : (viewMatrix * vec4<F32>(light->directionCache(), 0.0f)).xyz, isSpot ? std::cos(Angle::DegreesToRadians(spot->coneCutoffAngle())) : 0.f);
            temp._options.xyz = {typeIndex, light->shadowPropertyIndex(), isSpot ? to_I32(spot->coneSlantHeight()) : 0};

            ++lightCount[typeIndex];
        }
    }

    return ret;
}

// This should be called in a separate thread for each RenderStage
void LightPool::sortLightData(const RenderStage stage, const CameraSnapshot& cameraSnapshot) {
    OPTICK_EVENT();

    const U8 stageIndex = to_U8(stage);

    LightList& sortedLights = _sortedLights[stageIndex];
    sortedLights.resize(0);
    {
        SharedLock<SharedMutex> r_lock(_lightLock);
        sortedLights.reserve(_totalLightCount);
        for (U8 i = 1; i < to_base(LightType::COUNT); ++i) {
            sortedLights.insert(cend(sortedLights), cbegin(_lights[i]), cend(_lights[i]));
        }
    }
    
    const vec3<F32>& eyePos = cameraSnapshot._eye;
    const auto lightSortCbk = [&eyePos](Light* a, Light* b) noexcept {
        return  a->getLightType() < b->getLightType() ||
                    (a->getLightType() == b->getLightType() && 
                    a->distanceSquared(eyePos) < b->distanceSquared(eyePos));
    };

    OPTICK_EVENT("LightPool::SortLights");
    if (sortedLights.size() > 32) {
        std::sort(std::execution::par_unseq, begin(sortedLights), end(sortedLights), lightSortCbk);
    } else {
        eastl::sort(begin(sortedLights), end(sortedLights), lightSortCbk);
    }
    {
        SharedLock<SharedMutex> r_lock(_lightLock);
        const LightList& dirLights = _lights[to_base(LightType::DIRECTIONAL)];
        sortedLights.insert(begin(sortedLights), cbegin(dirLights), cend(dirLights));
    }

}

void LightPool::uploadLightData(const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();

    const U8 stageIndex = to_U8(stage);
    const U32 bufferOffset = LightBufferIndex(stage);
    LightList& sortedLights = _sortedLights[stageIndex];

    U32& totalLightCount = _sortedLightPropertiesCount[stageIndex];
    totalLightCount = uploadLightList(stage, sortedLights, cameraSnapshot._viewMatrix);

    SceneData& crtData = _sortedSceneProperties[stageIndex];
    crtData._globalData.set(
        _activeLightCount[stageIndex][to_base(LightType::DIRECTIONAL)],
        _activeLightCount[stageIndex][to_base(LightType::POINT)],
        _activeLightCount[stageIndex][to_base(LightType::SPOT)],
        0u);

    if (!sortedLights.empty()) {
        crtData._ambientColour.rgb = { 0.05f * sortedLights.front()->getDiffuseColour() };
    } else {
        crtData._ambientColour.set(DefaultColours::BLACK);
    }

    {
        OPTICK_EVENT("LightPool::UploadLightDataToGPU");
        memCmdInOut._bufferLocks.push_back(
            _lightBuffer->writeData({ bufferOffset * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, totalLightCount },
                                    &_sortedLightProperties[stageIndex]));
    }

    {
        OPTICK_EVENT("LightPool::UploadSceneDataToGPU");
        memCmdInOut._bufferLocks.push_back(_sceneBuffer->writeData({ bufferOffset, 1 }, &_sortedSceneProperties[stageIndex]));
    }
    memCmdInOut._syncFlag = 2u;
}

void LightPool::uploadLightData(const RenderStage stage, GFX::CommandBuffer& bufferInOut) {
    const U8 stageIndex = to_U8(stage);
    const U32 lightCount = _sortedLightPropertiesCount[stageIndex];
    const size_t bufferOffset = to_size(LightBufferIndex(stage));

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_FRAME;
    {
        auto& binding = cmd->_bindings.emplace_back();
        binding._slot = 9;
        binding._data.As<ShaderBufferEntry>() = { *_lightBuffer, { bufferOffset * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, lightCount } };
    }
    {
        auto& binding = cmd->_bindings.emplace_back();
        binding._slot = 8;
        binding._data.As<ShaderBufferEntry>() = { *_sceneBuffer, { bufferOffset, 1u } };
    }
    {
        auto& binding = cmd->_bindings.emplace_back();
        binding._slot = 13;
        binding._data.As<ShaderBufferEntry>() = { *_shadowBuffer, { 0u, 1u } };
    }
}

[[nodiscard]] bool LightPool::isShadowCacheInvalidated(const vec3<F32>& cameraPosition, Light* const light) {
    {
        SharedLock<SharedMutex> r_lock(_movedSceneVolumesLock);
        if (_movedSceneVolumes.empty()) {
            return light->staticShadowsDirty() || light->dynamicShadowsDirty();
        }
    }

    const BoundingSphere& lightBounds = light->boundingVolume();
    {
        SharedLock<SharedMutex> r_lock(_movedSceneVolumesLock);
        for (const MovingVolume& volume : _movedSceneVolumes) {
            if (volume._volume.collision(lightBounds)) {
                if (volume._staticSource) {
                    light->staticShadowsDirty(true);
                } else {
                    light->dynamicShadowsDirty(true);
                }
                if (light->staticShadowsDirty() && light->dynamicShadowsDirty()) {
                    return true;
                }
            }
        }
    }
    return light->staticShadowsDirty() || light->dynamicShadowsDirty();
}

void LightPool::preRenderAllPasses(const Camera* playerCamera) {
    OPTICK_EVENT();

    SharedLock<SharedMutex> r_lock(_lightLock);
    std::for_each(std::execution::par_unseq, std::cbegin(_lights), std::cend(_lights),
    [playerCamera](const LightList& lightList) {
        for (Light* light : lightList) {
            light->updateBoundingVolume(playerCamera);
        }
    });
}

void LightPool::postRenderAllPasses() noexcept {
    // Move backwards so that we don't step on our own toes with the lockmanager
    if (_shadowBufferDirty) {
        _shadowBuffer->decQueue();
        _shadowBufferDirty = false;
    }

    _lightBuffer->decQueue();
    _sceneBuffer->decQueue();
}

void LightPool::drawLightImpostors(GFX::CommandBuffer& bufferInOut) const {
    if (!lightImpostorsEnabled()) {
        return;
    }
    if (s_debugSamplerHash == 0u) {
        SamplerDescriptor iconSampler = {};
        iconSampler.wrapUVW(TextureWrap::REPEAT);
        iconSampler.mipSampling(TextureMipSampling::NONE);
        iconSampler.anisotropyLevel(0);
        s_debugSamplerHash = iconSampler.getHash();
    }

    const U32 totalLightCount = _sortedLightPropertiesCount[to_U8(RenderStage::DISPLAY)];
    if (totalLightCount > 0u) {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateHash = _context.gfx().getDefaultStateBlock(false);
        pipelineDescriptor._shaderProgramHandle = _lightImpostorShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _context.gfx().newPipeline(pipelineDescriptor) });
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;

            auto& binding = cmd->_bindings.emplace_back();
            binding._slot = 0;
            binding._data.As<DescriptorCombinedImageSampler>() = { _lightIconsTexture->defaultView(), s_debugSamplerHash };
        }

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.back()._drawCount = to_U16(totalLightCount);
    }
}

}
