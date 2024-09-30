

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

BloomPreRenderOperator::BloomPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter)
    : PreRenderOperator(context, parent, FilterType::FILTER_BLOOM, taskCounter)
{
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "bloom.glsl";
    fragModule._variant = "BloomDownscale";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);
    shaderDescriptor._globalDefines.emplace_back( "invSrcResolution PushData0[0].xy" );
    shaderDescriptor._globalDefines.emplace_back( "performKarisAverage (uint(PushData0[0].z) == 1)" );

    ResourceDescriptor<ShaderProgram> bloomDownscale("BloomDownscale", shaderDescriptor );
    bloomDownscale.waitForReady(false);

    _bloomDownscale = CreateResource(bloomDownscale, taskCounter);
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomDownscale;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _bloomDownscalePipeline = _context.newPipeline( pipelineDescriptor );
    }

    fragModule._variant = "BloomUpscale";
    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);
    shaderDescriptor._globalDefines.emplace_back("filterRadius PushData0[0].x");

    ResourceDescriptor<ShaderProgram> bloomUpscale("BloomUpscale", shaderDescriptor );
    bloomUpscale.waitForReady(false);
    _bloomUpscale = CreateResource(bloomUpscale, taskCounter);
    {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _bloomUpscale;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8(RTColourAttachmentSlot::SLOT_0)];
        state0.enabled(true);
        state0.blendSrc(BlendProperty::ONE);
        state0.blendDest(BlendProperty::ONE);
        state0.blendOp(BlendOperation::ADD);

        _bloomUpscalePipeline = _context.newPipeline( pipelineDescriptor );
    }

    const vec2<U16> resolution = parent.screenRT()._rt->getResolution();
    
    mipCount(std::min(mipCount(), MipCount(resolution.width, resolution.height)));

    U16 twidth = resolution.width;
    U16 theight = resolution.height;
    _mipSizes.resize(mipCount());
    for (U16 i = 0u; i < mipCount(); ++i)
    {
        twidth = twidth < 1u ? 1u : twidth;
        theight = theight < 1u ? 1u : theight;

        _mipSizes[i].set(twidth, theight);

        twidth /= 2u;
        theight /= 2u;
    }

    filterRadius(_context.context().config().rendering.postFX.bloom.filterRadius);
    strength(_context.context().config().rendering.postFX.bloom.strength);
}

BloomPreRenderOperator::~BloomPreRenderOperator()
{
    DestroyResource(_bloomDownscale);
    DestroyResource(_bloomUpscale);
}

bool BloomPreRenderOperator::ready() const noexcept
{
    if (_bloomUpscalePipeline != nullptr && _bloomDownscalePipeline != nullptr)
    {
        return PreRenderOperator::ready();
    }

    return false;
}

void BloomPreRenderOperator::reshape(const U16 width, const U16 height)
{
    PreRenderOperator::reshape(width, height);

    mipCount(std::min(mipCount(), MipCount(width, height)));

    U16 twidth = width;
    U16 theight = height;
    _mipSizes.resize(mipCount());
    for (U16 i = 0u; i < mipCount(); ++i)
    {
        twidth = twidth < 1u ? 1u : twidth;
        theight = theight < 1u ? 1u : theight;

        _mipSizes[i].set(twidth, theight);

        twidth /= 2u;
        theight /= 2u;
    }
}

void BloomPreRenderOperator::filterRadius(const F32 val)
{
    _filterRadius = val;
    _context.context().config().rendering.postFX.bloom.filterRadius = val;
    _context.context().config().changed(true);
}

void BloomPreRenderOperator::strength(const F32 val)
{
    _strength = val;
    _context.context().config().rendering.postFX.bloom.strength = val;
    _context.context().config().changed(true);
}

bool BloomPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    assert(input._targetID != output._targetID);

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    const auto& screenTex = Get(screenAtt->texture());

    const auto& bloomAtt = _context.renderTargetPool().getRenderTarget( RenderTargetNames::BLOOM_RESULT )->getAttachment( RTAttachmentType::COLOUR );
    const auto& bloomTex = Get( bloomAtt->texture() );

    const Rect<I32> activeViewport = _context.activeViewport();

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Construct Bloom Chain";
    //ref: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
    {
        ImageView inputView = screenTex->getView();

        // Progressively downsample through the mip chain
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Downsample Bloom Chain";


        for (U16 i = 0u; i < mipCount(); i++)
        {
            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
            renderPassCmd->_name = Util::StringFormat("Downsample {}", i);
            renderPassCmd->_descriptor = _screenOnlyDraw;
            renderPassCmd->_target = RenderTargetNames::BLOOM_RESULT;
            renderPassCmd->_clearDescriptor[to_base(RTColourAttachmentSlot::SLOT_0)] = DEFAULT_CLEAR_ENTRY;
            renderPassCmd->_descriptor._mipWriteLevel = i;

            GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _bloomDownscalePipeline;

            PushConstantsStruct& pushConstants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_fastData;
            pushConstants.data[0]._vec[0].set( 1.f / _mipSizes[i].width, 1.f / _mipSizes[i].height, i == 0u ? 1.f : 0.f, 0.f);

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding(cmd->_set, 0u, ShaderStageVisibility::FRAGMENT);
                Set( binding._data, inputView, bloomAtt->_descriptor._sampler );
            }

            GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport = Rect<I32>(activeViewport.offsetX, activeViewport.offsetY, _mipSizes[i].width, _mipSizes[i].height);
            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

            inputView = bloomTex->getView({._offset = i, ._count = 1u});
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
    {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Upsample Bloom Chain";

        for (U16 i = mipCount() - 1u; i > 0u; i--)
        {
            ImageView inputView = bloomTex->getView({ ._offset = i, ._count = 1u });

            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
            renderPassCmd->_name = Util::StringFormat("Upsample {}", i);
            renderPassCmd->_descriptor = _screenOnlyDraw;
            renderPassCmd->_target = RenderTargetNames::BLOOM_RESULT;
            renderPassCmd->_clearDescriptor[to_base(RTColourAttachmentSlot::SLOT_0)]._enabled = false;
            renderPassCmd->_descriptor._mipWriteLevel = i - 1;

            GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _bloomUpscalePipeline;
            PushConstantsStruct& pushConstants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_fastData;
            pushConstants.data[0]._vec[0].x = to_F32(_filterRadius);

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding(cmd->_set, 0u, ShaderStageVisibility::FRAGMENT);
                Set( binding._data, inputView, bloomAtt->_descriptor._sampler );
            }

            GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport = Rect<I32>(activeViewport.offsetX, activeViewport.offsetY, _mipSizes[i-1].width, _mipSizes[i-1].height);
            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        }
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
    GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport = activeViewport;
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    return false;
}
}
