#include "stdafx.h"

#include "Headers/BloomPreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

namespace {
    F32 resolutionDownscaleFactor = 2.0f;
}

BloomPreRenderOperator::BloomPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, ResourceCache* cache)
    : PreRenderOperator(context, parent, FilterType::FILTER_BLOOM),
      _bloomThreshold(0.99f)
{
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "bloom.glsl";
    fragModule._variant = "BloomCalc";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);
    shaderDescriptor._globalDefines.emplace_back( "luminanceThreshold PushData0[0].x" );

    ResourceDescriptor bloomCalc("BloomCalc");
    bloomCalc.propertyDescriptor(shaderDescriptor);
    bloomCalc.waitForReady(false);

    _bloomCalc = CreateResource<ShaderProgram>(cache, bloomCalc);
    _bloomCalc->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*)
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomCalc->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _bloomCalcPipeline = _context.newPipeline(pipelineDescriptor);
    });

    fragModule._variant = "BloomApply";
    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor bloomApply("BloomApply");
    bloomApply.propertyDescriptor(shaderDescriptor);
    bloomApply.waitForReady(false);
    _bloomApply = CreateResource<ShaderProgram>(cache, bloomApply);
    _bloomApply->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*)
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomApply->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _bloomApplyPipeline = _context.newPipeline(pipelineDescriptor);
    });

    const vec2<U16> res = parent.screenRT()._rt->getResolution();
    if (res.height > 1440)
    {
        resolutionDownscaleFactor = 4.0f;
    }

    const auto& screenAtt = parent.screenRT()._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    TextureDescriptor screenDescriptor = screenAtt->texture()->descriptor();
    screenDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    InternalRTAttachmentDescriptors att
    {
        InternalRTAttachmentDescriptor{ screenDescriptor, screenAtt->descriptor()._samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
    };

    RenderTargetDescriptor desc = {};
    desc._resolution = res;
    desc._attachmentCount = to_U8(att.size());
    desc._attachments = att.data();

    desc._name = "Bloom_Blur_0";
    _bloomBlurBuffer[0] = _context.renderTargetPool().allocateRT(desc);
    desc._name = "Bloom_Blur_1";
    _bloomBlurBuffer[1] = _context.renderTargetPool().allocateRT(desc);

    desc._name = "Bloom";
    desc._resolution = vec2<U16>(res / resolutionDownscaleFactor);
    _bloomOutput = _context.renderTargetPool().allocateRT(desc);

    luminanceThreshold(_context.context().config().rendering.postFX.bloom.threshold);
}

BloomPreRenderOperator::~BloomPreRenderOperator()
{
    if (!_context.renderTargetPool().deallocateRT(_bloomOutput) ||
        !_context.renderTargetPool().deallocateRT(_bloomBlurBuffer[0]) ||
        !_context.renderTargetPool().deallocateRT(_bloomBlurBuffer[1]))
    {
        DIVIDE_UNEXPECTED_CALL();
    }
}

bool BloomPreRenderOperator::ready() const noexcept
{
    if (_bloomCalcPipeline != nullptr && _bloomApplyPipeline != nullptr)
    {
        return PreRenderOperator::ready();
    }

    return false;
}

void BloomPreRenderOperator::reshape(const U16 width, const U16 height)
{
    PreRenderOperator::reshape(width, height);

    const U16 w = to_U16(width / resolutionDownscaleFactor);
    const U16 h = to_U16(height / resolutionDownscaleFactor);
    _bloomOutput._rt->resize(w, h);
    _bloomBlurBuffer[0]._rt->resize(width, height);
    _bloomBlurBuffer[1]._rt->resize(width, height);
}

void BloomPreRenderOperator::luminanceThreshold(const F32 val)
{
    _bloomThreshold = val;
    _context.context().config().rendering.postFX.bloom.threshold = val;
    _context.context().config().changed(true);
}

// Order: luminance calc -> bloom -> toneMap
bool BloomPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    assert(input._targetID != output._targetID);

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    const auto& screenTex = screenAtt->texture()->sampledView();
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, screenTex, screenAtt->descriptor()._samplerHash );
    }

    PushConstantsStruct params{};
    params.data[0]._vec[0].x = _bloomThreshold;
    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _bloomCalcPipeline });
    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(params);

    // Step 1: generate bloom - render all of the "bright spots"
    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = _bloomOutput._targetID;
    beginRenderPassCmd._name = "DO_BLOOM_PASS";
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    // Step 2: blur bloom
    _context.blurTarget(_bloomOutput,
                        _bloomBlurBuffer[0],
                        _bloomBlurBuffer[1],
                        RTAttachmentType::COLOUR,
                        RTColourAttachmentSlot::SLOT_0,
                        10,
                        true,
                        1,
                        bufferInOut);

    // Step 3: apply bloom
    const auto& bloomAtt = _bloomBlurBuffer[1]._rt->getAttachment(RTAttachmentType::COLOUR );
    const auto& bloomTex = bloomAtt->texture()->sampledView();

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, screenTex, screenAtt->descriptor()._samplerHash );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 1u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, bloomTex, bloomAtt->descriptor()._samplerHash );
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _bloomApplyPipeline });

    beginRenderPassCmd._target = output._targetID;
    beginRenderPassCmd._descriptor = _screenOnlyDraw;
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);


    return true;
}
}
