

#include "Headers/GUISplash.h"

#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

GUISplash::GUISplash(const std::string_view splashImageName, vec2<U16> dimensions) 
    : _dimensions(MOV(dimensions))
{
    TextureDescriptor textureDescriptor{};
    textureDescriptor._textureOptions._alphaChannelTransparency = false;

    ResourceDescriptor<Texture> splashImage("SplashScreen Texture", textureDescriptor);
    splashImage.assetName(splashImageName);
    splashImage.assetLocation(Paths::g_imagesLocation);
    _splashImage = CreateResource(splashImage);

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

    ResourceDescriptor<ShaderProgram> splashShader("fbPreview", shaderDescriptor);
    _splashShader = CreateResource(splashShader);
}

GUISplash::~GUISplash()
{
    DestroyResource(_splashImage );
}

void GUISplash::render(GFXDevice& context) const {

    SamplerDescriptor splashSampler = {};
    splashSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
    splashSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
    splashSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
    splashSampler._minFilter = TextureFilter::LINEAR;
    splashSampler._magFilter = TextureFilter::LINEAR;
    splashSampler._mipSampling = TextureMipSampling::NONE;
    splashSampler._anisotropyLevel = 0u;


    PipelineDescriptor pipelineDescriptor;
    pipelineDescriptor._stateBlock = context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _splashShader;
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    Handle<GFX::CommandBuffer> handle = GFX::AllocateCommandBuffer("Splash Screen", 16u);
    GFX::CommandBuffer& buffer = *GFX::Get(handle);

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = SCREEN_TARGET_ID;
    beginRenderPassCmd._name = "BLIT_TO_BACKBUFFER";
    beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    GFX::EnqueueCommand( buffer, beginRenderPassCmd);

    GFX::BindPipelineCommand pipelineCmd;
    pipelineCmd._pipeline = context.newPipeline(pipelineDescriptor);
    GFX::EnqueueCommand( buffer, pipelineCmd);

    GFX::SendPushConstantsCommand pushConstantsCommand = {};
    pushConstantsCommand._constants.set(_ID("lodLevel"), PushConstantType::FLOAT, 1.f);
    pushConstantsCommand._constants.set(_ID("channelsArePacked"), PushConstantType::BOOL, false);
    pushConstantsCommand._constants.set(_ID("channelCount"), PushConstantType::UINT, 4u);
    pushConstantsCommand._constants.set(_ID("startChannel"), PushConstantType::UINT, 0u);
    pushConstantsCommand._constants.set(_ID("multiplier"), PushConstantType::FLOAT, 1.f);
    GFX::EnqueueCommand( buffer, pushConstantsCommand);

    GFX::SetViewportCommand viewportCommand;
    viewportCommand._viewport.set(0, 0, _dimensions.width, _dimensions.height);
    GFX::EnqueueCommand( buffer, viewportCommand);

    WaitForReady( Get(_splashShader) );
    WaitForReady( Get(_splashImage) );

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( buffer );
    cmd->_usage = DescriptorSetUsage::PER_DRAW;

    DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
    Set( binding._data, _splashImage, splashSampler );

    GFX::EnqueueCommand<GFX::DrawCommand>( buffer )->_drawCommands.emplace_back();

    GFX::EnqueueCommand<GFX::EndRenderPassCommand>( buffer );

    context.flushCommandBuffer( MOV( handle ) );
}

}; //namespace Divide
