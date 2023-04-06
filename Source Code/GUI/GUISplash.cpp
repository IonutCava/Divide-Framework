#include "stdafx.h"

#include "Headers/GUISplash.h"

#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide {

GUISplash::GUISplash(ResourceCache* cache,
                     const Str64& splashImageName,
                     vec2<U16> dimensions) 
    : _dimensions(MOV(dimensions))
{
    TextureDescriptor splashDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
    splashDescriptor.textureOptions()._alphaChannelTransparency = false;

    ResourceDescriptor splashImage("SplashScreen Texture");
    splashImage.assetName(ResourcePath(splashImageName));
    splashImage.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    splashImage.propertyDescriptor(splashDescriptor);

    _splashImage = CreateResource<Texture>(cache, splashImage);

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "fbPreview.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor splashShader("fbPreview");
    splashShader.propertyDescriptor(shaderDescriptor);
    _splashShader = CreateResource<ShaderProgram>(cache, splashShader);
}

void GUISplash::render(GFXDevice& context) const {

    SamplerDescriptor splashSampler = {};
    splashSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    splashSampler.minFilter(TextureFilter::NEAREST);
    splashSampler.magFilter(TextureFilter::NEAREST);
    splashSampler.mipSampling(TextureMipSampling::NONE);
    splashSampler.anisotropyLevel(0);

    _splashShader->waitForReady();
    PipelineDescriptor pipelineDescriptor;
    pipelineDescriptor._stateBlock = context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _splashShader->handle();
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    GFX::ScopedCommandBuffer sBuffer = GFX::AllocateScopedCommandBuffer();
    GFX::CommandBuffer& buffer = sBuffer();

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = SCREEN_TARGET_ID;
    beginRenderPassCmd._name = "BLIT_TO_BACKBUFFER";
    beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    GFX::EnqueueCommand(buffer, beginRenderPassCmd);

    GFX::BindPipelineCommand pipelineCmd;
    pipelineCmd._pipeline = context.newPipeline(pipelineDescriptor);
    GFX::EnqueueCommand(buffer, pipelineCmd);

    GFX::SendPushConstantsCommand pushConstantsCommand = {};
    pushConstantsCommand._constants.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 1.f);
    pushConstantsCommand._constants.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
    pushConstantsCommand._constants.set(_ID("channelCount"), GFX::PushConstantType::UINT, 4u);
    pushConstantsCommand._constants.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
    pushConstantsCommand._constants.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.f);
    GFX::EnqueueCommand(buffer, pushConstantsCommand);

    GFX::SetViewportCommand viewportCommand;
    viewportCommand._viewport.set(0, 0, _dimensions.width, _dimensions.height);
    GFX::EnqueueCommand(buffer, viewportCommand);

    _splashImage->waitForReady();

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(buffer);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;

    DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
    Set( binding._data, _splashImage->getView(), splashSampler.getHash() );

    GFX::EnqueueCommand<GFX::DrawCommand>(buffer);

    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(buffer);

    context.flushCommandBuffer(buffer);
}

};