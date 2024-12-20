

#include "Headers/MotionBlurPreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

MotionBlurPreRenderOperator::MotionBlurPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter)
    : PreRenderOperator(context, parent, FilterType::FILTER_MOTION_BLUR, taskCounter)
{
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "blur.glsl";
    fragModule._variant = "ObjectMotionBlur";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);
    shaderDescriptor._globalDefines.emplace_back( "dvd_velocityScale PushData0[0].x" );
    shaderDescriptor._globalDefines.emplace_back( "dvd_maxSamples int(PushData0[0].y)" );

    ResourceDescriptor<ShaderProgram> motionBlur("MotionBlur", shaderDescriptor );
    motionBlur.waitForReady(false);
    _blurApply = CreateResource(motionBlur,taskCounter);

    PipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor._stateBlock = _context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _blurApply;
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _blurApplyPipelineCmd._pipeline = _context.newPipeline( pipelineDescriptor );

    parametersChanged();
}

MotionBlurPreRenderOperator::~MotionBlurPreRenderOperator()
{
    DestroyResource( _blurApply );
}

bool MotionBlurPreRenderOperator::ready() const noexcept
{
    if (_blurApplyPipelineCmd._pipeline != nullptr)
    {
        return PreRenderOperator::ready();
    }

    return false;
}

void MotionBlurPreRenderOperator::parametersChanged() noexcept
{
    NOP();
}

bool MotionBlurPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    const auto& velocityAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::VELOCITY);

    const F32 fps = _context.context().app().timer().getFps();
    const F32 velocityScale = _context.context().config().rendering.postFX.motionBlur.velocityScale;
    const F32 velocityFactor = fps / Config::TARGET_FRAME_RATE * velocityScale;

    GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    beginRenderPassCmd->_name = "DO_MOTION_BLUR_PASS";
    beginRenderPassCmd->_target = output._targetID;
    beginRenderPassCmd->_descriptor = _screenOnlyDraw;
    beginRenderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;

    GFX::EnqueueCommand(bufferInOut, _blurApplyPipelineCmd);

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, velocityAtt->texture(), velocityAtt->_descriptor._sampler );
    }

    PushConstantsStruct& params = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
    params.data[0]._vec[0].xy.set( velocityFactor, to_F32( maxSamples() ) );

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    return true;
}
}
