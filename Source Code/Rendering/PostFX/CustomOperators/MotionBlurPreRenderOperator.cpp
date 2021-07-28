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

    ResourceDescriptor motionBlur("MotionBlur");
    motionBlur.propertyDescriptor(shaderDescriptor);
    motionBlur.waitForReady(false);

    _blurApply = CreateResource<ShaderProgram>(cache, motionBlur);
    _blurApply->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _blurApply->getGUID();

        _blurApplyPipeline = _context.newPipeline(pipelineDescriptor);
    });

    parametersChanged();
}

bool MotionBlurPreRenderOperator::ready() const {
    if (_blurApplyPipeline != nullptr) {
        return PreRenderOperator::ready();
    }

    return false;
}

void MotionBlurPreRenderOperator::parametersChanged() {
    NOP();
}

bool MotionBlurPreRenderOperator::execute(const Camera* camera, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {

    const F32 fps = _context.parent().platformContext().app().timer().getFps();
    const F32 velocityScale = _context.context().config().rendering.postFX.motionBlur.velocityScale;
    const F32 velocityFactor = fps / Config::TARGET_FRAME_RATE * velocityScale;
    _blurApplyConstants.set(_ID("dvd_velocityScale"), GFX::PushConstantType::FLOAT, velocityFactor);
    _blurApplyConstants.set(_ID("dvd_maxSamples"), GFX::PushConstantType::INT, to_I32(maxSamples()));

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
    const auto& velocityAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::VELOCITY));

    const TextureData screenTex = screenAtt.texture()->data();
    const TextureData velocityTex = velocityAtt.texture()->data();

    GFX::BindDescriptorSetsCommand descriptorSetCmd = {};
    descriptorSetCmd._set._textureData.add(TextureEntry{ screenTex, screenAtt.samplerHash(), TextureUsage::UNIT0 });
    descriptorSetCmd._set._textureData.add(TextureEntry{ velocityTex, velocityAtt.samplerHash(),TextureUsage::UNIT1 });
    EnqueueCommand(bufferInOut, descriptorSetCmd);

    GFX::BeginRenderPassCommand beginRenderPassCmd = {};
    beginRenderPassCmd._target = output._targetID;
    beginRenderPassCmd._descriptor = _screenOnlyDraw;
    beginRenderPassCmd._name = "DO_MOTION_BLUR_PASS";
    EnqueueCommand(bufferInOut, beginRenderPassCmd);

    EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _blurApplyPipeline });
    EnqueueCommand(bufferInOut, GFX::SendPushConstantsCommand{ _blurApplyConstants });

    EnqueueCommand(bufferInOut, _triangleDrawCmd);

    EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{ });

    return true;
}

}