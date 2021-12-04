#include "stdafx.h"

#include "Headers/SSRPreRenderOperator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

SSRPreRenderOperator::SSRPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, ResourceCache* cache)
    : PreRenderOperator(context, parent, FilterType::FILTER_SS_REFLECTIONS)
{
    SamplerDescriptor screenSampler = {};
    screenSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    screenSampler.minFilter(TextureFilter::LINEAR);
    screenSampler.magFilter(TextureFilter::LINEAR);
    screenSampler.anisotropyLevel(0);

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "ScreenSpaceReflections.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor ssr("ScreenSpaceReflections");
    ssr.waitForReady(false);
    ssr.propertyDescriptor(shaderDescriptor);
    _ssrShader = CreateResource<ShaderProgram>(cache, ssr);
    _ssrShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _ssrShader->getGUID();
        _pipelineCmd._pipeline = _context.newPipeline(pipelineDescriptor);
    });

    const vec2<U16>& res = _parent.screenRT()._rt->getResolution();

    _constantsCmd._constants.set(_ID("size"), GFX::PushConstantType::VEC2, res);

    const vec2<F32> s = res * 0.5f;
    _projToPixelBasis = mat4<F32>
    {
       s.x, 0.f, 0.f, 0.f,
       0.f, s.y, 0.f, 0.f,
       0.f, 0.f, 1.f, 0.f,
       s.x, s.y, 0.f, 1.f
    };
    parametersChanged();
}

bool SSRPreRenderOperator::ready() const noexcept {
    if (_ssrShader->getState() == ResourceState::RES_LOADED) {
        return PreRenderOperator::ready();
    }

    return false;
}

void SSRPreRenderOperator::parametersChanged() {

    const auto& parameters = _context.context().config().rendering.postFX.ssr;
    _constantsCmd._constants.set(_ID("maxSteps"), GFX::PushConstantType::FLOAT, to_F32(parameters.maxSteps));
    _constantsCmd._constants.set(_ID("binarySearchIterations"), GFX::PushConstantType::FLOAT, to_F32(parameters.binarySearchIterations));
    _constantsCmd._constants.set(_ID("jitterAmount"), GFX::PushConstantType::FLOAT, parameters.jitterAmount);
    _constantsCmd._constants.set(_ID("maxDistance"), GFX::PushConstantType::FLOAT, parameters.maxDistance);
    _constantsCmd._constants.set(_ID("stride"), GFX::PushConstantType::FLOAT, parameters.stride);
    _constantsCmd._constants.set(_ID("zThickness"), GFX::PushConstantType::FLOAT, parameters.zThickness);
    _constantsCmd._constants.set(_ID("strideZCutoff"), GFX::PushConstantType::FLOAT, parameters.strideZCutoff);
    _constantsCmd._constants.set(_ID("screenEdgeFadeStart"), GFX::PushConstantType::FLOAT, parameters.screenEdgeFadeStart);
    _constantsCmd._constants.set(_ID("eyeFadeStart"), GFX::PushConstantType::FLOAT, parameters.eyeFadeStart);
    _constantsCmd._constants.set(_ID("eyeFadeEnd"), GFX::PushConstantType::FLOAT, parameters.eyeFadeEnd);
    _constantsDirty = true;
}

void SSRPreRenderOperator::reshape(const U16 width, const U16 height) {
    PreRenderOperator::reshape(width, height);
    const vec2<F32> s{ width * 0.5f, height * 0.5f };
    _projToPixelBasis = mat4<F32>
    {
       s.x, 0.f, 0.f, 0.f,
       0.f, s.y, 0.f, 0.f,
       0.f, 0.f, 1.f, 0.f,
       s.x, s.y, 0.f, 1.f
    };
}

bool SSRPreRenderOperator::execute(const Camera* camera, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {
    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));

    // ToDo: Cache these textures and their mipcount somehow -Ionut
    const auto[skyTexture, skySampler] = Attorney::SceneManagerSSRAccessor::getSkyTexture(_context.parent().sceneManager());
    const auto& environmentProbeAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
    const Texture_ptr& reflectionTexture = environmentProbeAtt.texture();
    if (!skyTexture && !reflectionTexture) {
        // We need some sort of environment mapping here (at least for now)
        return false;
    }

    const vec3<U32> mipCounts {
        screenAtt.texture()->mipCount(),
        reflectionTexture->mipCount(),
        skyTexture == nullptr
                    ? reflectionTexture->mipCount()
                    : skyTexture->mipCount()
    };

    _constantsCmd._constants.set(_ID("projToPixel"), GFX::PushConstantType::MAT4, camera->projectionMatrix() * _projToPixelBasis);
    _constantsCmd._constants.set(_ID("projectionMatrix"), GFX::PushConstantType::MAT4, camera->projectionMatrix());
    _constantsCmd._constants.set(_ID("invProjectionMatrix"), GFX::PushConstantType::MAT4, GetInverse(camera->projectionMatrix()));
    _constantsCmd._constants.set(_ID("invViewMatrix"), GFX::PushConstantType::MAT4, camera->worldMatrix());
    _constantsCmd._constants.set(_ID("mipCounts"), GFX::PushConstantType::UVEC3, mipCounts);
    _constantsCmd._constants.set(_ID("zPlanes"), GFX::PushConstantType::VEC2, camera->getZPlanes());
    _constantsCmd._constants.set(_ID("skyLayer"), GFX::PushConstantType::UINT, _context.getRenderer().postFX().isDayTime() ?  0u : 1u);
    _constantsCmd._constants.set(_ID("ssrEnabled"), GFX::PushConstantType::BOOL, _enabled);

    const auto& normalsAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES));
    const auto& depthAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Depth, 0);

    const TextureData screenTex = screenAtt.texture()->data();
    const TextureData normalsTex = normalsAtt.texture()->data();
    const TextureData depthTex = depthAtt.texture()->data();

    /// We need mipmaps for roughness based LoD lookup
    GFX::ComputeMipMapsCommand* mipCmd = GFX::EnqueueCommand<GFX::ComputeMipMapsCommand>(bufferInOut);
    mipCmd->_texture = screenAtt.texture().get();

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._textureData.add(TextureEntry{ screenTex, screenAtt.samplerHash(),TextureUsage::UNIT0 });
    set._textureData.add(TextureEntry{ depthTex, depthAtt.samplerHash(),TextureUsage::UNIT1 });
    set._textureData.add(TextureEntry{ normalsTex, normalsAtt.samplerHash(), TextureUsage::SCENE_NORMALS });
    if (skyTexture == nullptr) {
        set._textureData.add(TextureEntry{ reflectionTexture->data(), environmentProbeAtt.samplerHash(), TextureUsage::REFLECTION_SKY });
    } else {
        set._textureData.add(TextureEntry{ skyTexture->data(), skySampler, TextureUsage::REFLECTION_SKY });
    }
    set._textureData.add(TextureEntry{ reflectionTexture->data(), environmentProbeAtt.samplerHash(), TextureUsage::REFLECTION_ENV });

    GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    renderPassCmd->_target = { RenderTargetUsage::SSR_RESULT };
    renderPassCmd->_descriptor = _screenOnlyDraw;
    renderPassCmd->_name = "DO_SSR_PASS";

    GFX::EnqueueCommand(bufferInOut, _pipelineCmd);

    GFX::EnqueueCommand(bufferInOut, _constantsCmd);

    GFX::EnqueueCommand(bufferInOut, _triangleDrawCmd);

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    return false;
}

} //namespace Divide
