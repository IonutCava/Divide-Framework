#include "stdafx.h"

#include "Headers/MotionBlurPreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

MotionBlurPreRenderOperator::MotionBlurPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, ResourceCache* cache)
    : PreRenderOperator(context, parent, FilterType::FILTER_MOTION_BLUR)
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
    shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    ResourceDescriptor motionBlur("MotionBlur");
    motionBlur.propertyDescriptor(shaderDescriptor);
    motionBlur.waitForReady(false);

    _blurApply = CreateResource<ShaderProgram>(cache, motionBlur);
    _blurApply->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _blurApply->handle();

        _blurApplyPipelineCmd._pipeline = _context.newPipeline(pipelineDescriptor);
    });

    parametersChanged();
}

bool MotionBlurPreRenderOperator::ready() const noexcept {
    if (_blurApplyPipelineCmd._pipeline != nullptr) {
        return PreRenderOperator::ready();
    }

    return false;
}

void MotionBlurPreRenderOperator::parametersChanged() noexcept {
    NOP();
}

bool MotionBlurPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
    const auto& velocityAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::VELOCITY));

    const F32 fps = _context.parent().platformContext().app().timer().getFps();
    const F32 velocityScale = _context.context().config().rendering.postFX.motionBlur.velocityScale;
    const F32 velocityFactor = fps / Config::TARGET_FRAME_RATE * velocityScale;

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._usage = DescriptorSetUsage::PER_DRAW_SET;
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TextureUsage::UNIT0);
        binding._data._combinedImageSampler._image = screenAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = screenAtt.samplerHash();
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TextureUsage::UNIT1);
        binding._data._combinedImageSampler._image = velocityAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = velocityAtt.samplerHash();
    }

    GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    beginRenderPassCmd->_name = "DO_MOTION_BLUR_PASS";
    beginRenderPassCmd->_target = output._targetID;
    beginRenderPassCmd->_descriptor = _screenOnlyDraw;

    GFX::EnqueueCommand(bufferInOut, _blurApplyPipelineCmd);

    PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants;
    constants.set(_ID("dvd_velocityScale"), GFX::PushConstantType::FLOAT, velocityFactor);
    constants.set(_ID("dvd_maxSamples"),    GFX::PushConstantType::INT,   to_I32(maxSamples()));

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    return true;
}
}
