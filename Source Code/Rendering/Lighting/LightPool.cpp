#include "stdafx.h"

#include "Headers/LightPool.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"

#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"

#include <execution>

namespace Divide {

std::array<TextureUsage, to_base(ShadowType::COUNT)> LightPool::_shadowLocation = { {
    TextureUsage::SHADOW_SINGLE,
    TextureUsage::SHADOW_LAYERED,
    TextureUsage::SHADOW_CUBE
}};

namespace {
    constexpr U8 DataBufferRingSize = 4u;

    constexpr bool g_GroupSortedLightsByType = true;

    LightPool::LightList g_sortedLightsContainer = {};

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

LightPool::LightPool(Scene& parentScene, PlatformContext& context)
    : SceneComponent(parentScene),
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
    bufferDescriptor._separateReadWrite = false;
    bufferDescriptor._flags = to_U32(ShaderBuffer::Flags::EXPLICIT_RANGE_FLUSH);
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

    TextureDescriptor iconDescriptor(TextureType::TEXTURE_2D);
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
    g_sortedLightsContainer.clear();

    return _lights.empty();
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
    return true;
}

void LightPool::idle() noexcept {
}

//ToDo: Generate shadow maps in parallel - Ionut
void LightPool::generateShadowMaps(const Camera& playerCamera, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    Time::ScopedTimer timer(_shadowPassTimer);

    std::array<I32, to_base(LightType::COUNT)> indexCounter{};
    std::array<vec2<U16>, to_base(LightType::COUNT)> shadowRanges{};
    std::array<bool, to_base(LightType::COUNT)> shadowsGenerated{};

    ShadowMap::resetShadowMaps(bufferInOut);

    for (U16 i = 0u; i < _sortedShadowLights._count; ++i) {
        Light* light = _sortedShadowLights._entries[i];

        const LightType lType = light->getLightType();
        I32& counter = indexCounter[to_base(lType)];
        if (counter == GetMaxLights(lType)) {
            continue;
        }

        if (!ShadowMap::generateShadowMaps(playerCamera, *light, bufferInOut)) {
            continue;
        }

        const Light::ShadowProperties& propsSource = light->getShadowProperties();
        const I32 shadowIndex = counter++;

        vec2<U16>& layerRange = shadowRanges[to_base(lType)];
        layerRange.min = std::min(layerRange.min, light->getShadowOffset());

        switch (lType) {
            case LightType::POINT: {
                PointShadowProperties& propsTarget = _shadowBufferData._pointLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                propsTarget._position = propsSource._lightPosition[0];
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowOffset() + 1u));
            }break;
            case LightType::SPOT: {
                SpotShadowProperties& propsTarget = _shadowBufferData._spotLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                propsTarget._vpMatrix = propsSource._lightVP[0];
                propsTarget._position = propsSource._lightPosition[0];
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowOffset() + 1u));
            }break;
            case LightType::DIRECTIONAL: {
                CSMShadowProperties& propsTarget = _shadowBufferData._dirLights[shadowIndex];
                propsTarget._details = propsSource._lightDetails;
                std::memcpy(propsTarget._position.data(), propsSource._lightPosition.data(), sizeof(vec4<F32>) * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT);
                std::memcpy(propsTarget._vpMatrix.data(), propsSource._lightVP.data(), sizeof(mat4<F32>) * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT);
                layerRange.max = std::max(layerRange.max, to_U16(light->getShadowOffset() + static_cast<DirectionalLightComponent*>(light)->csmSplitCount()));
            }break;
            case LightType::COUNT: break;
        }
        light->cleanShadowProperties(shadowIndex);

        shadowsGenerated[to_base(lType)] = true;
        GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
        computeMipMapsCommand._texture = ShadowMap::getDepthMap(lType)._rt->getAttachment(RTAttachmentType::Colour, 0).texture().get();
        computeMipMapsCommand._layerRange = layerRange;
        computeMipMapsCommand._defer = false;
        EnqueueCommand(bufferInOut, computeMipMapsCommand);
    }

    _shadowBuffer->writeData(_shadowBufferData.data());
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
    assert(it != eastl::end(_lights[to_U32(type)]));

    return *it;
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
            temp._options.xyz = {typeIndex, light->shadowIndex(), isSpot ? to_I32(spot->coneSlantHeight()) : 0};

            ++lightCount[typeIndex];
        }
    }

    return ret;
}

// This should be called in a separate thread for each RenderStage
void LightPool::prepareLightData(const RenderStage stage, const vec3<F32>& eyePos, const mat4<F32>& viewMatrix) {
    OPTICK_EVENT();

    const U8 stageIndex = to_U8(stage);

    LightList& sortedLights = _sortedLights[stageIndex];
    sortedLights.resize(0);
    {
        SharedLock<SharedMutex> r_lock(_lightLock);
        size_t totalLightCount = 0;
        for (U8 i = 0; i < to_base(LightType::COUNT); ++i) {
            totalLightCount += _lights[i].size();
        }
        sortedLights.reserve(totalLightCount);

        for (U8 i = 1; i < to_base(LightType::COUNT); ++i) {
            sortedLights.insert(cend(sortedLights), cbegin(_lights[i]), cend(_lights[i]));
        }
    }
    {
        OPTICK_EVENT("LightPool::SortLights");
        eastl::sort(begin(sortedLights),
                    end(sortedLights),
                    [&eyePos](Light* a, Light* b) noexcept {
                        if_constexpr(g_GroupSortedLightsByType) {
                            return  a->getLightType() < b->getLightType() ||
                                a->getLightType() == b->getLightType() && a->distanceSquared(eyePos) < b->distanceSquared(eyePos);
                        } else {
                            return a->distanceSquared(eyePos) < b->distanceSquared(eyePos);
                        }
                    });
    }
    {
        SharedLock<SharedMutex> r_lock(_lightLock);
        const LightList& dirLights = _lights[to_base(LightType::DIRECTIONAL)];
        sortedLights.insert(begin(sortedLights), cbegin(dirLights), cend(dirLights));
    }

    U32& totalLightCount = _sortedLightPropertiesCount[stageIndex];
    totalLightCount = uploadLightList(stage, sortedLights, viewMatrix);

    SceneData& crtData = _sortedSceneProperties[stageIndex];
    bool sceneDataDirty = false;

    const vec4<U32> tempData(
        _activeLightCount[stageIndex][to_base(LightType::DIRECTIONAL)],
        _activeLightCount[stageIndex][to_base(LightType::POINT)],
        _activeLightCount[stageIndex][to_base(LightType::SPOT)],
        to_U32(_sortedShadowLights._count));
    if (tempData != crtData._globalData) {
        crtData._globalData.set(tempData);
        sceneDataDirty = true;
    }

    FColour4 ambient = DefaultColours::BLACK;
    if (!sortedLights.empty()) {
        ambient.rgb = { 0.05f * sortedLights.front()->getDiffuseColour() };
    }
    if (ambient != crtData._ambientColour) {
        crtData._ambientColour = ambient;
        sceneDataDirty = true;
    }
    const U32 bufferOffset = LightBufferIndex(stage);
    {
        OPTICK_EVENT("LightPool::UploadLightDataToGPU");
        _lightBuffer->writeData(bufferOffset * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME,
                                totalLightCount,
                                &_sortedLightProperties[stageIndex]);
    }

    if (/*sceneDataDirty*/true) {
        OPTICK_EVENT("LightPool::UploadSceneDataToGPU");
        _sceneBuffer->writeData(bufferOffset, 1, &_sortedSceneProperties[stageIndex]);
    }
}

void LightPool::uploadLightData(const RenderStage stage, GFX::CommandBuffer& bufferInOut) {
    const U8 stageIndex = to_U8(stage);
    const U32 lightCount = _sortedLightPropertiesCount[stageIndex];
    const size_t bufferOffset = to_size(LightBufferIndex(stage));

    ShaderBufferBinding bufferLightData;
    bufferLightData._binding = ShaderBufferLocation::LIGHT_NORMAL;
    bufferLightData._buffer = _lightBuffer;
    bufferLightData._elementRange = { bufferOffset * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, lightCount };
    
    ShaderBufferBinding bufferLightScene;
    bufferLightScene._binding = ShaderBufferLocation::LIGHT_SCENE;
    bufferLightScene._buffer = _sceneBuffer;
    bufferLightScene._elementRange = { bufferOffset, 1u };

    ShaderBufferBinding bufferShadow;
    bufferShadow._binding = ShaderBufferLocation::LIGHT_SHADOW;
    bufferShadow._buffer = _shadowBuffer;
    bufferShadow._elementRange = { 0u, 1u };

    GFX::BindDescriptorSetsCommand descriptorSetCmd{};
    descriptorSetCmd._set._buffers.add(bufferLightData);
    descriptorSetCmd._set._buffers.add(bufferLightScene);
    descriptorSetCmd._set._buffers.add(bufferShadow);
    EnqueueCommand(bufferInOut, descriptorSetCmd);
}

[[nodiscard]] bool LightPool::IsLightInViewFrustum(const Frustum& frustum, Light* light) noexcept {
    switch (light->getLightType()) {
        case LightType::DIRECTIONAL:
            return true;
        case LightType::POINT:
            return frustum.ContainsSphere(light->positionCache(), light->range()) != FrustumCollision::FRUSTUM_OUT;
        case LightType::SPOT: {
            const F32 range = light->range();

            const Angle::RADIANS<F32> angle = Angle::DegreesToRadians(static_cast<SpotLightComponent*>(light)->outerConeCutoffAngle());

            const F32 radius = angle > M_PI_4 ? range * tan(angle) : range * 0.5f / pow(cos(angle), 2.0f);

            const vec3<F32> position = light->positionCache() + light->directionCache() * radius;

            return frustum.ContainsSphere(position, radius) != FrustumCollision::FRUSTUM_OUT;
        };

        default: break;
    }

    DIVIDE_UNEXPECTED_CALL();
    return false;
}

void LightPool::preRenderAllPasses(const Camera* playerCamera) {
    OPTICK_EVENT();

    g_sortedLightsContainer.resize(0);
    {
        SharedLock<SharedMutex> r_lock(_lightLock);
        for (U8 i = 0; i < to_base(LightType::COUNT); ++i) {
            if (_lightTypeState[i]) {
                g_sortedLightsContainer.insert(cend(g_sortedLightsContainer), cbegin(_lights[i]), cend(_lights[i]));
            }
        }
    }

    const vec3<F32>& eyePos = playerCamera->getEye();
    eastl::sort(begin(g_sortedLightsContainer),
                end(g_sortedLightsContainer), 
                [&eyePos](Light* a, Light* b) noexcept {
                    // directional lights first
                    if (a->getLightType() != b->getLightType()) {
                        return to_base(a->getLightType()) < to_base(b->getLightType());
                    }
                    return a->positionCache().distanceSquared(eyePos) < b->positionCache().distanceSquared(eyePos);
                });

    const Frustum& camFrustum = playerCamera->getFrustum();
    _sortedShadowLights._count = 0;
    std::array<I32, to_base(LightType::COUNT)> indexCounter = {};
    for (Light* light : g_sortedLightsContainer) {
        light->shadowIndex(-1);
        if (light->enabled() && light->castsShadows()) {
            const LightType lType = light->getLightType();
            I32& counter = indexCounter[to_base(lType)];
            if (counter == GetMaxLights(lType) || !IsLightInViewFrustum(camFrustum, light)) {
                continue;
            }

            light->shadowIndex(counter++);
            _sortedShadowLights._entries[_sortedShadowLights._count] = light;
            
            if (++_sortedShadowLights._count >= Config::Lighting::MAX_SHADOW_CASTING_LIGHTS) {
                break;
            }
        }
    }
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

    static size_t s_samplerHash = 0;
    if (s_samplerHash == 0) {
        SamplerDescriptor iconSampler = {};
        iconSampler.wrapUVW(TextureWrap::REPEAT);
        iconSampler.minFilter(TextureFilter::LINEAR);
        iconSampler.magFilter(TextureFilter::LINEAR);
        iconSampler.anisotropyLevel(0);
        s_samplerHash = iconSampler.getHash();
    }

    const U32 totalLightCount = _sortedLightPropertiesCount[to_U8(RenderStage::DISPLAY)];
    if (totalLightCount > 0) {
        ShaderBufferBinding bufferLightData;
        bufferLightData._binding = ShaderBufferLocation::LIGHT_NORMAL;
        bufferLightData._buffer = _lightBuffer;
        bufferLightData._elementRange = { to_size(LightBufferIndex(RenderStage::DISPLAY)) * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, totalLightCount };

        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateHash = _context.gfx().getDefaultStateBlock(false);
        pipelineDescriptor._shaderProgramHandle = _lightImpostorShader->getGUID();

        GenericDrawCommand pointsCmd{};
        pointsCmd._primitiveType = PrimitiveType::API_POINTS;
        pointsCmd._drawCount = to_U16(totalLightCount);

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _context.gfx().newPipeline(pipelineDescriptor) });
        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ _lightIconsTexture->data(), s_samplerHash, TextureUsage::UNIT0 });
        GFX::EnqueueCommand(bufferInOut, GFX::DrawCommand{ pointsCmd });
    }
}

}
