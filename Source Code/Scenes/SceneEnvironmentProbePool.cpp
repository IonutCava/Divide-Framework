#include "stdafx.h"

#include "Headers/SceneEnvironmentProbePool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Scenes/Headers/Scene.h"

#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "Headers/SceneShaderData.h"

namespace Divide {

vector<DebugView_ptr> SceneEnvironmentProbePool::s_debugViews;
vector<Camera*> SceneEnvironmentProbePool::s_probeCameras;
bool SceneEnvironmentProbePool::s_probesDirty = true;

std::array<std::pair<bool, bool>, Config::MAX_REFLECTIVE_PROBES_PER_PASS> SceneEnvironmentProbePool::s_availableSlices;
RenderTargetHandle SceneEnvironmentProbePool::s_reflection;
RenderTargetHandle SceneEnvironmentProbePool::s_IBL;
RenderTargetHandle SceneEnvironmentProbePool::s_skyLight;
ShaderProgram_ptr SceneEnvironmentProbePool::s_previewShader;
bool SceneEnvironmentProbePool::s_debuggingSkyLight = false;
bool SceneEnvironmentProbePool::s_skyLightNeedsRefresh = true;

SceneEnvironmentProbePool::SceneEnvironmentProbePool(Scene& parentScene) noexcept
    : SceneComponent(parentScene)
{

}

SceneEnvironmentProbePool::~SceneEnvironmentProbePool() 
{
}

I16 SceneEnvironmentProbePool::AllocateSlice(const bool lock) {
    static_assert(Config::MAX_REFLECTIVE_PROBES_PER_PASS < I16_MAX);

    for (U32 i = 0; i < Config::MAX_REFLECTIVE_PROBES_PER_PASS; ++i) {
        if (s_availableSlices[i].first) {
            s_availableSlices[i] = { false, lock };
            return to_I16(i);
        }
    }

    if_constexpr (Config::Build::IS_DEBUG_BUILD) {
        DIVIDE_UNEXPECTED_CALL();
    }

    return -1;
}

void SceneEnvironmentProbePool::UnlockSlice(const I16 slice) noexcept {
    s_availableSlices[slice] = { true, false };
}

void SceneEnvironmentProbePool::OnStartup(GFXDevice& context) {
    SkyLightNeedsRefresh(true);

    for (U32 i = 0; i < 6; ++i) {
        s_probeCameras.emplace_back(Camera::createCamera<FreeFlyCamera>(Util::StringFormat("ProbeCamera_%d", i)));
    }

    s_availableSlices.fill({ true, false });

    // Reflection Targets
    SamplerDescriptor reflectionSampler = {};
    reflectionSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    reflectionSampler.magFilter(TextureFilter::LINEAR);
    reflectionSampler.minFilter(TextureFilter::LINEAR_MIPMAP_LINEAR);
    reflectionSampler.anisotropyLevel(context.context().config().rendering.maxAnisotropicFilteringLevel);
    const size_t samplerHash = reflectionSampler.getHash();


    const U32 reflectRes = to_U32(context.context().config().rendering.reflectionProbeResolution);
    RenderTargetDescriptor desc = {};
    desc._resolution.set(reflectRes, reflectRes);
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
        environmentDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);

        TextureDescriptor depthDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);
        depthDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS);
        depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors att = {
            { environmentDescriptor, samplerHash, RTAttachmentType::Colour },
            { depthDescriptor, samplerHash, RTAttachmentType::Depth },
        };

        desc._name = "EnvironmentProbe";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();

        s_reflection = context.renderTargetPool().allocateRT(RenderTargetUsage::ENVIRONMENT, desc, 0u);
    }
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
        environmentDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        RTAttachmentDescriptors att = {
          { environmentDescriptor, samplerHash, RTAttachmentType::Colour },
        };
        desc._name = "IBLProbe";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();

        s_IBL = context.renderTargetPool().allocateRT(RenderTargetUsage::IBL, desc, 0u);
    }
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
        environmentDescriptor.layerCount(1u);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);

        RTAttachmentDescriptors att = {
           { environmentDescriptor, samplerHash, RTAttachmentType::Colour },
        };
        desc._name = "SkyLight";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();
        s_skyLight = context.renderTargetPool().allocateRT(RenderTargetUsage::ENVIRONMENT, desc, 1u);
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "fbPreview.glsl";
        fragModule._variant = "Cube";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        ResourceDescriptor shadowPreviewShader("fbPreview.Cube");
        shadowPreviewShader.propertyDescriptor(shaderDescriptor);
        shadowPreviewShader.threaded(false);
        s_previewShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), shadowPreviewShader);
    }
}

void SceneEnvironmentProbePool::OnShutdown(GFXDevice& context) {
    s_previewShader.reset();
    context.renderTargetPool().deallocateRT(s_reflection);
    context.renderTargetPool().deallocateRT(s_IBL);
    context.renderTargetPool().deallocateRT(s_skyLight);
    for (U8 i = 0u; i < 6u; ++i) {
        Camera::destroyCamera(s_probeCameras[i]);
    }
    s_probeCameras.clear();
    // Remove old views
    if (!s_debugViews.empty()) {
        for (const DebugView_ptr& view : s_debugViews) {
            context.removeDebugView(view.get());
        }
        s_debugViews.clear();
    }
}

RenderTargetHandle SceneEnvironmentProbePool::ReflectionTarget() noexcept {
    return s_reflection;
}

RenderTargetHandle SceneEnvironmentProbePool::IBLTarget() noexcept {
    return s_IBL;
}

RenderTargetHandle SceneEnvironmentProbePool::SkyLightTarget() noexcept {
    return s_skyLight;
}

const EnvironmentProbeList& SceneEnvironmentProbePool::sortAndGetLocked(const vec3<F32>& position) {
    eastl::sort(begin(_envProbes),
                end(_envProbes),
                [&position](const auto& a, const auto& b) noexcept -> bool {
                    return a->distanceSqTo(position) < b->distanceSqTo(position);
                });

    return _envProbes;
}

void SceneEnvironmentProbePool::Prepare(GFX::CommandBuffer& bufferInOut) {
    for (U16 i = 0u; i < Config::MAX_REFLECTIVE_PROBES_PER_PASS; ++i) {
        if (!s_availableSlices[i].second) {
            s_availableSlices[i].first = true;
        }
    }

    if (ProbesDirty()) {
        GFX::ClearRenderTargetCommand clearMainTarget = {};
        clearMainTarget._target = s_reflection._targetID;
        clearMainTarget._descriptor.clearDepth(true);
        clearMainTarget._descriptor.clearColours(false);
        clearMainTarget._descriptor.resetToDefault(true);
        EnqueueCommand(bufferInOut, clearMainTarget);

        ProbesDirty(false);
    }
}

void SceneEnvironmentProbePool::UpdateSkyLight(GFXDevice& context, GFX::CommandBuffer& bufferInOut) {
    if (!SkyLightNeedsRefresh()) {
        return;
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand("SkyLight Pass"));

    vector<Camera*>& probeCameras = ProbeCameras();
    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(probeCameras), std::min(cameras.size(), probeCameras.size()), std::begin(cameras));

    RenderPassParams params = {};
    params._target = SceneEnvironmentProbePool::SkyLightTarget()._targetID;
    params._stagePass = RenderStagePass(RenderStage::REFLECTION, RenderPassType::COUNT, to_U8(ReflectorType::CUBE), Config::MAX_REFLECTIVE_NODES_IN_VIEW + 0u);

    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));
    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES)));
    SetBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_SKY_NODES)));

    context.generateCubeMap(params,
                            0,
                            VECTOR3_ZERO,
                            vec2<F32>(0.1f, 10000.f),
                            bufferInOut,
                            cameras);

    //ToDo: Blur, convolute, etc -Ionut
    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._texture = SceneEnvironmentProbePool::SkyLightTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).texture().get();
    computeMipMapsCommand._layerRange = { 0, 1 };
    EnqueueCommand(bufferInOut, computeMipMapsCommand);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    SkyLightNeedsRefresh(false);
}

void SceneEnvironmentProbePool::lockProbeList() const noexcept {
    _probeLock.lock();
}

void SceneEnvironmentProbePool::unlockProbeList() const noexcept {
    _probeLock.unlock();
}

const EnvironmentProbeList& SceneEnvironmentProbePool::getLocked() const noexcept {
    return _envProbes;
}

void SceneEnvironmentProbePool::registerProbe(EnvironmentProbeComponent* probe) {
    DebuggingSkyLight(false);

    ScopedLock<SharedMutex> w_lock(_probeLock);

    assert(_envProbes.size() < U16_MAX - 2u);
    _envProbes.emplace_back(probe);
    probe->poolIndex(to_U16(_envProbes.size() - 1u));
}

void SceneEnvironmentProbePool::unregisterProbe(const EnvironmentProbeComponent* probe) {
    if (probe != nullptr) {
        ScopedLock<SharedMutex> w_lock(_probeLock);
        auto it = _envProbes.erase(eastl::remove_if(begin(_envProbes), end(_envProbes),
                                                    [probeGUID = probe->getGUID()](const auto& p) noexcept { return p->getGUID() == probeGUID; }),
                                   end(_envProbes));
        while (it != cend(_envProbes)) {
            (*it)->poolIndex((*it)->poolIndex() - 1);
            it++;
        }
    }

    if (probe == _debugProbe) {
        debugProbe(nullptr);
    }
}

void SceneEnvironmentProbePool::prepareDebugData() {
    // Remove old views
    if (!s_debugViews.empty()) {
        for (const DebugView_ptr& view : s_debugViews) {
            parentScene().context().gfx().removeDebugView(view.get());
        }
        s_debugViews.clear();
    }

    debugSkyLight();
    debugProbe(_debugProbe);

    for (const DebugView_ptr& view : s_debugViews) {
        parentScene().context().gfx().addDebugView(view);
    }
}

void SceneEnvironmentProbePool::debugProbe(EnvironmentProbeComponent* probe) {
    _debugProbe = probe;
    // Add new views if needed
    if (probe == nullptr) {
        return;
    }

    constexpr I32 Base = 10;
    for (U32 i = 0u; i < 6u; ++i) {
        DebugView_ptr probeView = std::make_shared<DebugView>(to_I16(I16_MAX - 1 - 6 + i));
        if (_debugProbe->debugIBL()) {
            probeView->_texture = IBLTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
            probeView->_samplerHash = IBLTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
        } else {
            probeView->_texture = ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
            probeView->_samplerHash = ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
        }
        probeView->_shader = s_previewShader;
        probeView->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, probe->rtLayerIndex());
        probeView->_shaderData.set(_ID("face"), GFX::PushConstantType::INT, i);
        probeView->_name = Util::StringFormat("CubeProbe_%d_face_%d", probe->rtLayerIndex(), i);
        probeView->_groupID = Base + probe->rtLayerIndex();
        probeView->_enabled = true;
        s_debugViews.push_back(probeView);
    }
}

void SceneEnvironmentProbePool::debugSkyLight() {
    if (!DebuggingSkyLight()) {
        return;
    }
    // Add new views if needed
    constexpr I32 Base = 4220;
    for (U32 i = 0u; i < 6u; ++i) {
        DebugView_ptr probeView = std::make_shared<DebugView>(to_I16(I16_MAX - 1 - 6 + i));
        probeView->_texture = SkyLightTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
        probeView->_samplerHash = SkyLightTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
        probeView->_shader = s_previewShader;
        probeView->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, 0u);
        probeView->_shaderData.set(_ID("face"), GFX::PushConstantType::INT, i);
        probeView->_name = Util::StringFormat("CubeSkyLight_face_%d", i);
        probeView->_groupID = Base + 255;
        probeView->_enabled = true;
        s_debugViews.push_back(probeView);
    }
}

void SceneEnvironmentProbePool::OnNodeUpdated(const SceneEnvironmentProbePool& probePool, const SceneGraphNode& node) noexcept {
    const BoundingSphere& bSphere = node.get<BoundsComponent>()->getBoundingSphere();
    probePool.lockProbeList();
    const EnvironmentProbeList& probes = probePool.getLocked();
    for (const auto& probe : probes) {
        if (probe->checkCollisionAndQueueUpdate(bSphere)) {
            NOP();
        }
    }
    probePool.unlockProbeList();
    if (node.getNode().type() == SceneNodeType::TYPE_SKY) {
        SkyLightNeedsRefresh(true);
    }
}

void SceneEnvironmentProbePool::OnTimeOfDayChange(const SceneEnvironmentProbePool& probePool) noexcept {
    probePool.lockProbeList();
    const EnvironmentProbeList& probes = probePool.getLocked();
    for (const auto& probe : probes) {
        if (probe->updateType() != EnvironmentProbeComponent::UpdateType::ONCE) {
            probe->dirty(true);
        }
    }
    probePool.unlockProbeList();
    SkyLightNeedsRefresh(true);
}

bool SceneEnvironmentProbePool::DebuggingSkyLight() {
    return s_debuggingSkyLight;
}

void SceneEnvironmentProbePool::DebuggingSkyLight(const bool state) noexcept {
    s_debuggingSkyLight = state;
}

bool SceneEnvironmentProbePool::SkyLightNeedsRefresh() {
    return s_skyLightNeedsRefresh;
}

void SceneEnvironmentProbePool::SkyLightNeedsRefresh(const bool state) noexcept {
    s_skyLightNeedsRefresh = state;
}
} //namespace Divide