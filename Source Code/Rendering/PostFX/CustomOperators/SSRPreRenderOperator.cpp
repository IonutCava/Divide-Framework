#include "stdafx.h"

#include "Headers/SSRPreRenderOperator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

SSRPreRenderOperator::SSRPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, ResourceCache* cache)
    : PreRenderOperator(context, parent, FilterType::FILTER_SS_REFLECTIONS)
{
    SamplerDescriptor screenSampler = {};
    screenSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    screenSampler.mipSampling(TextureMipSampling::NONE);
    screenSampler.anisotropyLevel(0);

    ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" };
    ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "ScreenSpaceReflections.glsl" };

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
        pipelineDescriptor._shaderProgramHandle = _ssrShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

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

void SSRPreRenderOperator::prepare([[maybe_unused]] const PlayerIndex idx, GFX::CommandBuffer& bufferInOut) {
    PreRenderOperator::prepare(idx, bufferInOut);

    if (_stateChanged && !_enabled) {
        RTClearDescriptor clearDescriptor = {};
        clearDescriptor._clearDepth = true;
        clearDescriptor._clearColours = true;
        clearDescriptor._resetToDefault = true;

        GFX::ClearRenderTargetCommand clearMainTarget = {};
        clearMainTarget._target = RenderTargetNames::SSR_RESULT;
        clearMainTarget._descriptor = clearDescriptor;
        EnqueueCommand(bufferInOut, clearMainTarget);
    }

    _stateChanged = false;
}

bool SSRPreRenderOperator::execute(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, [[maybe_unused]] const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {
    assert(_enabled);

    RTAttachment* screenAtt = input._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
    RTAttachment* normalsAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::NORMALS));
    RTAttachment* depthAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Depth_Stencil, 0);

    const auto& screenTex = screenAtt->texture()->defaultView();
    const auto& normalsTex = normalsAtt->texture()->defaultView();
    const auto& depthTex = depthAtt->texture()->defaultView();
    U16 screenMipCount = screenAtt->texture()->mipCount();
    if (screenMipCount > 2u) {
        screenMipCount -= 2u;
    }

    //const CameraSnapshot& prevSnapshot = _parent.getCameraSnapshot(idx);
    const CameraSnapshot& prevSnapshot = _context.getCameraSnapshot(idx);
    _constantsCmd._constants.set(_ID("previousViewProjection"), GFX::PushConstantType::MAT4, mat4<F32>::Multiply(prevSnapshot._viewMatrix, prevSnapshot._projectionMatrix));
    _constantsCmd._constants.set(_ID("projToPixel"), GFX::PushConstantType::MAT4, cameraSnapshot._projectionMatrix * _projToPixelBasis);
    _constantsCmd._constants.set(_ID("projectionMatrix"), GFX::PushConstantType::MAT4, cameraSnapshot._projectionMatrix);
    _constantsCmd._constants.set(_ID("invProjectionMatrix"), GFX::PushConstantType::MAT4, cameraSnapshot._invProjectionMatrix);
    _constantsCmd._constants.set(_ID("invViewMatrix"), GFX::PushConstantType::MAT4, cameraSnapshot._invViewMatrix);
    _constantsCmd._constants.set(_ID("screenDimensions"), GFX::PushConstantType::VEC2, vec2<F32>(screenAtt->texture()->width(), screenAtt->texture()->height()));
    _constantsCmd._constants.set(_ID("maxScreenMips"), GFX::PushConstantType::UINT, screenMipCount);
    _constantsCmd._constants.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, cameraSnapshot._zPlanes);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            auto& binding = cmd->_bindings.emplace_back();
            binding._slot = 0;
            binding._data.As<DescriptorCombinedImageSampler>() = { screenTex, screenAtt->descriptor()._samplerHash };
        }
        {
            auto& binding = cmd->_bindings.emplace_back();
            binding._slot = 1;
            binding._data.As<DescriptorCombinedImageSampler>() = { depthTex, depthAtt->descriptor()._samplerHash };
        }
        {
            auto& binding = cmd->_bindings.emplace_back();
            binding._slot = 2;
            binding._data.As<DescriptorCombinedImageSampler>() = { normalsTex, normalsAtt->descriptor()._samplerHash };
        }

    GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    renderPassCmd->_target = RenderTargetNames::SSR_RESULT;
    renderPassCmd->_descriptor = _screenOnlyDraw;
    renderPassCmd->_name = "DO_SSR_PASS";

    GFX::EnqueueCommand(bufferInOut, _pipelineCmd);

    GFX::EnqueueCommand(bufferInOut, _constantsCmd);

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    return false;
}

} //namespace Divide
