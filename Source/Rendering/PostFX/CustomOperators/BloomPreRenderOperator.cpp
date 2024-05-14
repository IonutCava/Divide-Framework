

#include "Headers/BloomPreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
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

BloomPreRenderOperator::BloomPreRenderOperator(GFXDevice& context, PreRenderBatch& parent)
    : PreRenderOperator(context, parent, FilterType::FILTER_BLOOM)
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
    shaderDescriptor._globalDefines.emplace_back( "luminanceBias PushData0[0].x" );

    ResourceDescriptor<ShaderProgram> bloomCalc("BloomCalc", shaderDescriptor );
    bloomCalc.waitForReady(false);

    _bloomCalc = CreateResource(bloomCalc);
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomCalc;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _bloomCalcPipeline = _context.newPipeline( pipelineDescriptor );
    }

    fragModule._variant = "BloomApply";
    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor<ShaderProgram> bloomApply("BloomApply", shaderDescriptor );
    bloomApply.waitForReady(false);
    _bloomApply = CreateResource(bloomApply);
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomApply;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _bloomApplyPipeline = _context.newPipeline( pipelineDescriptor );
    }

    const vec2<U16> res = parent.screenRT()._rt->getResolution();
    if (res.height > 1440)
    {
        resolutionDownscaleFactor = 4.0f;
    }

    const auto& screenAtt = parent.screenRT()._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    TextureDescriptor screenDescriptor = Get(screenAtt->texture())->descriptor();
    screenDescriptor._mipMappingState = MipMappingState::OFF;

    RenderTargetDescriptor desc = {};
    desc._attachments =
    {
        InternalRTAttachmentDescriptor{ screenDescriptor, screenAtt->_descriptor._sampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
    };

    desc._resolution = res;
    desc._name = "Bloom_Blur_0";
    _bloomBlurBuffer[0] = _context.renderTargetPool().allocateRT(desc);
    desc._name = "Bloom_Blur_1";
    _bloomBlurBuffer[1] = _context.renderTargetPool().allocateRT(desc);

    desc._name = "Bloom";
    desc._resolution = vec2<U16>(res / resolutionDownscaleFactor);
    _bloomOutput = _context.renderTargetPool().allocateRT(desc);

    luminanceBias(_context.context().config().rendering.postFX.bloom.luminanceBias);
}

BloomPreRenderOperator::~BloomPreRenderOperator()
{
    if (!_context.renderTargetPool().deallocateRT(_bloomOutput) ||
        !_context.renderTargetPool().deallocateRT(_bloomBlurBuffer[0]) ||
        !_context.renderTargetPool().deallocateRT(_bloomBlurBuffer[1]))
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    DestroyResource( _bloomCalc );
    DestroyResource( _bloomApply );
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

void BloomPreRenderOperator::luminanceBias(const F32 val)
{
    _luminanceBias = val;
    _context.context().config().rendering.postFX.bloom.luminanceBias = val;
    _context.context().config().changed(true);
}

// Order: luminance calc -> bloom -> toneMap
bool BloomPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    assert(input._targetID != output._targetID);

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    const auto& screenTex = Get(screenAtt->texture())->getView();

    { // Step 1: generate bloom - render all of the "bright spots"
        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._target = _bloomOutput._targetID;
        beginRenderPassCmd._name = "DO_BLOOM_PASS";
        beginRenderPassCmd._descriptor = _screenOnlyDraw;
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        PushConstantsStruct params{};
        params.data[0]._vec[0].x = luminanceBias();
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _bloomCalcPipeline;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( params );
    
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenTex, screenAtt->_descriptor._sampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _parent.luminanceTex(), _parent.lumaSampler());
        }


        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }
    {// Step 2: blur bloom
        _context.blurTarget(_bloomOutput,
                            _bloomBlurBuffer[0],
                            _bloomBlurBuffer[1],
                            RTAttachmentType::COLOUR,
                            RTColourAttachmentSlot::SLOT_0,
                            10,
                            true,
                            1,
                            bufferInOut);
    }
    {// Step 3: apply bloom
        const auto& bloomAtt = _bloomBlurBuffer[1]._rt->getAttachment(RTAttachmentType::COLOUR );
        const auto& bloomTex = Get(bloomAtt->texture())->getView();

        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._target = output._targetID;
        beginRenderPassCmd._descriptor = _screenOnlyDraw;
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenTex, screenAtt->_descriptor._sampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, bloomTex, bloomAtt->_descriptor._sampler );
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _bloomApplyPipeline;

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }

    return true;
}
}
