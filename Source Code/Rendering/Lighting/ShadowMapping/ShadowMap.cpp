#include "stdafx.h"

#include "Headers/ShadowMap.h"
#include "Headers/CascadedShadowMapsGenerator.h"
#include "Headers/CubeShadowMapGenerator.h"
#include "Headers/SingleShadowMapGenerator.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/SceneState.h"

#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"

namespace Divide {

Mutex ShadowMap::s_depthMapUsageLock;
std::array<ShadowMap::LayerUsageMask, to_base(ShadowType::COUNT)> ShadowMap::s_depthMapUsage;
std::array<ShadowMapGenerator*, to_base(ShadowType::COUNT)> ShadowMap::s_shadowMapGenerators;

vector<DebugView_ptr> ShadowMap::s_debugViews;
std::array<RenderTargetHandle, to_base(ShadowType::COUNT)> ShadowMap::s_shadowMaps;
Light* ShadowMap::s_shadowPreviewLight = nullptr;

std::array<U16, to_base(ShadowType::COUNT)> ShadowMap::s_shadowPassIndex;
std::array<ShadowMap::ShadowCameraPool, to_base(ShadowType::COUNT)> ShadowMap::s_shadowCameras;

ShadowMapGenerator::ShadowMapGenerator(GFXDevice& context, const ShadowType type) noexcept
    : _context(context),
      _type(type)
{
}

ShadowType ShadowMap::getShadowTypeForLightType(const LightType type) noexcept {
    switch (type) {
        case LightType::DIRECTIONAL: return ShadowType::LAYERED;
        case LightType::POINT: return ShadowType::CUBEMAP;
        case LightType::SPOT: return ShadowType::SINGLE;
        case LightType::COUNT: break;
    }

    return ShadowType::COUNT;
}

LightType ShadowMap::getLightTypeForShadowType(const ShadowType type) noexcept {
    switch (type) {
        case ShadowType::LAYERED: return LightType::DIRECTIONAL;
        case ShadowType::CUBEMAP: return LightType::POINT;
        case ShadowType::SINGLE: return LightType::SPOT;
        case ShadowType::COUNT: break;
    }

    return LightType::COUNT;
}

void ShadowMap::initShadowMaps(GFXDevice& context) {
    for (U8 t = 0; t < to_base(ShadowType::COUNT); ++t) {
        const ShadowType type = static_cast<ShadowType>(t);
        const U8 cameraCount = type == ShadowType::SINGLE ? 1 : type == ShadowType::CUBEMAP ? 6 : Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;

        for (U32 i = 0; i < cameraCount; ++i) {
            Camera* cam = s_shadowCameras[t].emplace_back(Camera::createCamera<FreeFlyCamera>(Util::StringFormat("ShadowCamera_%s_%d", Names::shadowType[t], i)));
            cam->flag(t);
        }
    }

    Configuration::Rendering::ShadowMapping& settings = context.context().config().rendering.shadowMapping;
    
    if (!isPowerOfTwo(settings.csm.shadowMapResolution)) {
        settings.csm.shadowMapResolution = nextPOW2(settings.csm.shadowMapResolution);
    }
    if (!isPowerOfTwo(settings.spot.shadowMapResolution)) {
        settings.spot.shadowMapResolution = nextPOW2(settings.spot.shadowMapResolution);
    }
    if (!isPowerOfTwo(settings.point.shadowMapResolution)) {
        settings.point.shadowMapResolution = nextPOW2(settings.point.shadowMapResolution);
    }

    SamplerDescriptor depthMapSampler = {};
    depthMapSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    depthMapSampler.magFilter(TextureFilter::LINEAR);
    depthMapSampler.minFilter(TextureFilter::LINEAR_MIPMAP_LINEAR);

    RenderTargetHandle crtTarget;
    for (U8 i = 0; i < to_U8(ShadowType::COUNT); ++i) {
        s_depthMapUsage[i].resize(0);

        switch (static_cast<ShadowType>(i)) {
            case ShadowType::LAYERED:
            case ShadowType::SINGLE: {
                const bool isCSM = i == to_U8(ShadowType::LAYERED);
                if (isCSM && !settings.csm.enabled) {
                    continue;
                }
                if (!isCSM && !settings.spot.enabled) {
                    continue;
                }

                depthMapSampler.anisotropyLevel(isCSM ? settings.csm.maxAnisotropicFilteringLevel : settings.spot.maxAnisotropicFilteringLevel);

                // Default filters, LINEAR is OK for this
                TextureDescriptor depthMapDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXImageFormat::RG, isCSM ? GFXDataFormat::FLOAT_32 : GFXDataFormat::FLOAT_16);
                depthMapDescriptor.layerCount(isCSM ? Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT
                                                    : Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS);
                depthMapDescriptor.autoMipMaps(false);

                RTAttachmentDescriptors att = {
                    { depthMapDescriptor, depthMapSampler.getHash(), RTAttachmentType::Colour },
                };

                RenderTargetDescriptor desc = {};
                desc._name = isCSM ? "CSM_ShadowMap" : "Single_ShadowMap";
                desc._resolution.set(to_U16(isCSM ? settings.csm.shadowMapResolution : settings.spot.shadowMapResolution));
                desc._attachmentCount = to_U8(att.size());
                desc._attachments = att.data();

                crtTarget = context.renderTargetPool().allocateRT(RenderTargetUsage::SHADOW, desc, isCSM ? to_base(ShadowType::LAYERED) : to_base(ShadowType::SINGLE));

                if (isCSM) {
                    s_shadowMapGenerators[i] = MemoryManager_NEW CascadedShadowMapsGenerator(context);
                } else {
                    s_shadowMapGenerators[i] = MemoryManager_NEW SingleShadowMapGenerator(context);
                }
            } break;
            case ShadowType::CUBEMAP: {
                if (!settings.point.enabled) {
                    continue;
                }

                TextureDescriptor colourMapDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RG, GFXDataFormat::FLOAT_16);
                colourMapDescriptor.layerCount(Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS);
                colourMapDescriptor.autoMipMaps(false);
                depthMapSampler.minFilter(TextureFilter::LINEAR);
                depthMapSampler.anisotropyLevel(0);
                const size_t samplerHash = depthMapSampler.getHash();

                TextureDescriptor depthDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);
                depthDescriptor.layerCount(Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS);
                depthDescriptor.autoMipMaps(false);

                RTAttachmentDescriptors att = {
                    { colourMapDescriptor, samplerHash, RTAttachmentType::Colour },
                    { depthDescriptor, samplerHash, RTAttachmentType::Depth },
                };

                RenderTargetDescriptor desc = {};
                desc._name = "Cube_ShadowMap";
                desc._resolution.set(to_U16(settings.point.shadowMapResolution));
                desc._attachmentCount = to_U8(att.size());
                desc._attachments = att.data();

                crtTarget = context.renderTargetPool().allocateRT(RenderTargetUsage::SHADOW, desc, to_base(ShadowType::CUBEMAP));

                s_shadowMapGenerators[i] = MemoryManager_NEW CubeShadowMapGenerator(context);
            } break;
            case ShadowType::COUNT: break;
        }
        s_shadowMaps[i] = crtTarget;
    }
}

void ShadowMap::destroyShadowMaps(GFXDevice& context) {
    for (U8 t = 0; t < to_base(ShadowType::COUNT); ++t) {
        for (auto& camera : s_shadowCameras[t]) {
            Camera::destroyCamera(camera);
        }
        s_shadowCameras[t].clear();
    }

    s_debugViews.clear();

    for (RenderTargetHandle& handle : s_shadowMaps) {
        context.renderTargetPool().deallocateRT(handle);
        handle._rt = nullptr;
    }

    for (ShadowMapGenerator* smg : s_shadowMapGenerators) {
        MemoryManager::SAFE_DELETE(smg);
    }

    s_shadowMapGenerators.fill(nullptr);
}

void ShadowMap::resetShadowMaps(GFX::CommandBuffer& bufferInOut) {
    for (U8 i = 0u; i < to_base(ShadowType::COUNT); ++i) {
        // We don't need to clear the draw buffers here as both the CSM and Single-layer targets use full-size blitting from intermidiate targets
        // so every pixel gets overwritten anyway.
        GFX::EnqueueCommand<GFX::ResetRenderTargetCommand>(bufferInOut)->_source = RenderTargetID(RenderTargetUsage::SHADOW, i);
    }

    ScopedLock<Mutex> w_lock(s_depthMapUsageLock);
    for (U32 i = 0u; i < to_base(ShadowType::COUNT); ++i) {
        s_depthMapUsage[i].resize(0);
        s_shadowPassIndex[i] = 0u;
    }
}

void ShadowMap::bindShadowMaps(GFX::CommandBuffer& bufferInOut) {
    GFX::BindDescriptorSetsCommand descriptorSetCmd{};
    for (U8 i = 0u; i < to_base(ShadowType::COUNT); ++i) {
        RenderTargetHandle& sm = s_shadowMaps[i];
        if (sm._rt == nullptr) {
            continue;
        }

        const ShadowType shadowType = static_cast<ShadowType>(i);
        const U16 useCount = lastUsedDepthMapOffset(shadowType);

        const U8 bindSlot = LightPool::GetShadowBindSlotOffset(static_cast<ShadowType>(i));
        const RTAttachment& shadowTexture = sm._rt->getAttachment(RTAttachmentType::Colour, 0);
        const TextureDescriptor& texDescriptor = shadowTexture.descriptor()._texDescriptor;

        if (IS_IN_RANGE_EXCLUSIVE(useCount, to_U16(0u), texDescriptor.layerCount())) {
            TextureViewEntry entry = {};
            entry._binding = bindSlot;
            entry._view._textureData = shadowTexture.texture().get()->data();
            entry._descriptor = shadowTexture.texture().get()->descriptor();
            entry._view._samplerHash = shadowTexture.samplerHash();
            entry._view._mipLevels.set(0, texDescriptor.mipCount());
            entry._view._layerRange.set(0, useCount);
            descriptorSetCmd._set._textureViews.add(entry);
        } else {
            descriptorSetCmd._set._textureData.add(TextureEntry{ shadowTexture.texture()->data(), shadowTexture.samplerHash(), bindSlot });
        }
    }
    EnqueueCommand(bufferInOut, descriptorSetCmd);
}

U16 ShadowMap::lastUsedDepthMapOffset(const ShadowType shadowType) {
    ScopedLock<Mutex> w_lock(s_depthMapUsageLock);
    const LayerUsageMask& usageMask = s_depthMapUsage[to_U32(shadowType)];

    for (U16 i = 0; i < to_U16(usageMask.size()); ++i) {
        if (usageMask[i] == false) {
            return i;
        }
    }

    return to_U16(usageMask.size());
}

U16 ShadowMap::findFreeDepthMapOffset(const ShadowType shadowType, const U32 layerCount) {
    ScopedLock<Mutex> w_lock(s_depthMapUsageLock);

    LayerUsageMask& usageMask = s_depthMapUsage[to_U32(shadowType)];
    U16 layer = U16_MAX;
    const U16 usageCount = to_U16(usageMask.size());
    for (U16 i = 0; i < usageCount; ++i) {
        if (usageMask[i] == false) {
            layer = i;
            break;
        }
    }

    if (layer > usageCount) {
        layer = usageCount;
        usageMask.insert(std::end(usageMask), layerCount, false);
    }

    return layer;
}

void ShadowMap::commitDepthMapOffset(const ShadowType shadowType, const U32 layerOffest, const U32 layerCount) {
    ScopedLock<Mutex> w_lock(s_depthMapUsageLock);

    LayerUsageMask& usageMask = s_depthMapUsage[to_U32(shadowType)];
    for (U32 i = layerOffest; i < layerOffest + layerCount; ++i) {
        usageMask[i] = true;
    }
}

bool ShadowMap::freeDepthMapOffset(const ShadowType shadowType, const U32 layerOffest, const U32 layerCount) {
    ScopedLock<Mutex> w_lock(s_depthMapUsageLock);

    LayerUsageMask& usageMask = s_depthMapUsage[to_U32(shadowType)];
    for (U32 i = layerOffest; i < layerOffest + layerCount; ++i) {
        usageMask[i] = false;
    }

    return true;
}

U32 ShadowMap::getLightLayerRequirements(const Light& light) {
    switch (light.getLightType()) {
        case LightType::DIRECTIONAL: return to_U32(static_cast<const DirectionalLightComponent&>(light).csmSplitCount());
        case LightType::SPOT:
        case LightType::POINT: return 1u;
        case LightType::COUNT: break;
    }

    return 0u;
}

bool ShadowMap::generateShadowMaps(const Camera& playerCamera, Light& light, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    const U32 layerRequirement = getLightLayerRequirements(light);
    const ShadowType sType = getShadowTypeForLightType(light.getLightType());

    if (layerRequirement == 0u || s_shadowMapGenerators[to_base(sType)] == nullptr) {
        return false;
    }

    const U16 offset = findFreeDepthMapOffset(sType, layerRequirement);
    light.setShadowOffset(offset);
    commitDepthMapOffset(sType, offset, layerRequirement);

    s_shadowMapGenerators[to_base(sType)]->render(playerCamera, light, s_shadowPassIndex[to_base(sType)]++, bufferInOut);

    return true;
}

const RenderTargetHandle& ShadowMap::getDepthMap(const LightType type) {
    return s_shadowMaps[to_base(getShadowTypeForLightType(type))];
}

void ShadowMap::setMSAASampleCount(const ShadowType type, const U8 sampleCount) {
    if (s_shadowMapGenerators[to_base(type)] != nullptr) {
        s_shadowMapGenerators[to_base(type)]->updateMSAASampleCount(sampleCount);
    }
}

void ShadowMap::setDebugViewLight(GFXDevice& context, Light* light) {
    if (light == s_shadowPreviewLight) {
        return;
    }

    // Remove old views
    if (!s_debugViews.empty()) {
        for (const DebugView_ptr& view : s_debugViews) {
            context.removeDebugView(view.get());
        }
        s_debugViews.clear();
    }

    s_shadowPreviewLight = light;

    // Add new views if needed
    if (light != nullptr && light->castsShadows()) {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "fbPreview.glsl";

        switch (light->getLightType()) {
            case LightType::DIRECTIONAL: {

                fragModule._variant = "Layered.LinearDepth";

                ShaderProgramDescriptor shaderDescriptor = {};
                shaderDescriptor._modules.push_back(vertModule);
                shaderDescriptor._modules.push_back(fragModule);
                 
                ResourceDescriptor shadowPreviewShader("fbPreview.Layered.LinearDepth");
                shadowPreviewShader.propertyDescriptor(shaderDescriptor);
                shadowPreviewShader.threaded(false);
                ShaderProgram_ptr previewShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), shadowPreviewShader);
                const U8 splitCount = static_cast<DirectionalLightComponent&>(*light).csmSplitCount();

                constexpr I16 Base = 2;
                for (U8 i = 0; i < splitCount; ++i) {
                    DebugView_ptr shadow = std::make_shared<DebugView>(to_I16(I16_MAX - 1 - splitCount + i));
                    shadow->_texture = getDepthMap(LightType::DIRECTIONAL)._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
                    shadow->_samplerHash = getDepthMap(LightType::DIRECTIONAL)._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
                    shadow->_shader = previewShader;
                    shadow->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, i + light->getShadowOffset());
                    shadow->_name = Util::StringFormat("CSM_%d", i + light->getShadowOffset());
                    shadow->_groupID = Base + to_I16(light->shadowIndex());
                    s_debugViews.push_back(shadow);
                }
            } break;
            case LightType::SPOT: {
                fragModule._variant = "Layered.LinearDepth";

                ShaderProgramDescriptor shaderDescriptor = {};
                shaderDescriptor._modules.push_back(vertModule);
                shaderDescriptor._modules.push_back(fragModule);

                ResourceDescriptor shadowPreviewShader("fbPreview.Layered.LinearDepth");
                shadowPreviewShader.propertyDescriptor(shaderDescriptor);
                shadowPreviewShader.threaded(false);

                DebugView_ptr shadow = std::make_shared<DebugView>(to_I16(I16_MAX - 1));
                shadow->_texture = getDepthMap(LightType::SPOT)._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
                shadow->_samplerHash = getDepthMap(LightType::SPOT)._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
                shadow->_shader = CreateResource<ShaderProgram>(context.parent().resourceCache(), shadowPreviewShader);
                shadow->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, light->getShadowOffset());
                shadow->_name = Util::StringFormat("SM_%d", light->getShadowOffset());
                s_debugViews.push_back(shadow);
            }break;
            case LightType::POINT: {
                fragModule._variant = "Cube.Shadow";

                ShaderProgramDescriptor shaderDescriptor = {};
                shaderDescriptor._modules.push_back(vertModule);
                shaderDescriptor._modules.back()._defines.emplace_back("SPLAT_R_CHANNEL", true);
                shaderDescriptor._modules.push_back(fragModule);

                ResourceDescriptor shadowPreviewShader("fbPreview.Cube.Shadow");
                shadowPreviewShader.propertyDescriptor(shaderDescriptor);
                shadowPreviewShader.threaded(false);
                ShaderProgram_ptr previewShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), shadowPreviewShader);

                const vec2<F32> zPlanes = shadowCameras(ShadowType::CUBEMAP)[0]->getZPlanes();

                constexpr I16 Base = 5;
                for (U32 i = 0; i < 6; ++i) {
                    DebugView_ptr shadow = std::make_shared<DebugView>(to_I16(I16_MAX - 1 - 6 + i));
                    shadow->_texture = getDepthMap(LightType::POINT)._rt->getAttachment(RTAttachmentType::Colour, 0).texture();
                    shadow->_samplerHash = getDepthMap(LightType::POINT)._rt->getAttachment(RTAttachmentType::Colour, 0).samplerHash();
                    shadow->_shader = previewShader;
                    shadow->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, light->getShadowOffset());
                    shadow->_shaderData.set(_ID("face"), GFX::PushConstantType::INT, i);
                    shadow->_groupID = Base + to_I16(light->shadowIndex());
                    shadow->_name = Util::StringFormat("CubeSM_%d_face_%d", light->getShadowOffset(), i);
                    s_debugViews.push_back(shadow);
                }
            } break;
            case LightType::COUNT: break;
        }

        for (const DebugView_ptr& view : s_debugViews) {
            context.addDebugView(view);
        }
    }
}
}