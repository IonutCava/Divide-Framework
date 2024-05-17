

#include "Headers/SSRPreRenderOperator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

SSRPreRenderOperator::SSRPreRenderOperator(GFXDevice& context, PreRenderBatch& parent)
    : PreRenderOperator(context, parent, FilterType::FILTER_SS_REFLECTIONS)
{
    ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" };
    ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "ScreenSpaceReflections.glsl" };

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor<ShaderProgram> ssr("ScreenSpaceReflections", shaderDescriptor );
    ssr.waitForReady(false);

    _ssrShader = CreateResource( ssr);

    PipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor._stateBlock = _context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _ssrShader;
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _pipelineCmd._pipeline = _context.newPipeline( pipelineDescriptor );
    const vec2<U16> res = _parent.screenRT()._rt->getResolution();

    _uniforms.set(_ID("size"), PushConstantType::VEC2, res);

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

SSRPreRenderOperator::~SSRPreRenderOperator()
{
    WAIT_FOR_CONDITION(ready());
    DestroyResource(_ssrShader);
}
bool SSRPreRenderOperator::ready() const noexcept
{
    if (Get(_ssrShader)->getState() == ResourceState::RES_LOADED)
    {
        return PreRenderOperator::ready();
    }

    return false;
}

void SSRPreRenderOperator::parametersChanged()
{

    const auto& parameters = _context.context().config().rendering.postFX.ssr;
    _uniforms.set( _ID( "maxSteps" ), PushConstantType::FLOAT, to_F32( parameters.maxSteps ) );
    _uniforms.set( _ID( "binarySearchIterations" ), PushConstantType::FLOAT, to_F32( parameters.binarySearchIterations ) );
    _uniforms.set( _ID( "jitterAmount" ), PushConstantType::FLOAT, parameters.jitterAmount );
    _uniforms.set( _ID( "maxDistance" ), PushConstantType::FLOAT, parameters.maxDistance );
    _uniforms.set( _ID( "stride" ), PushConstantType::FLOAT, parameters.stride );
    _uniforms.set( _ID( "zThickness" ), PushConstantType::FLOAT, parameters.zThickness );
    _uniforms.set( _ID( "strideZCutoff" ), PushConstantType::FLOAT, parameters.strideZCutoff );
    _uniforms.set( _ID( "screenEdgeFadeStart" ), PushConstantType::FLOAT, parameters.screenEdgeFadeStart );
    _uniforms.set( _ID( "eyeFadeStart" ), PushConstantType::FLOAT, parameters.eyeFadeStart );
    _uniforms.set(_ID("eyeFadeEnd"), PushConstantType::FLOAT, parameters.eyeFadeEnd);
    _constantsDirty = true;
}

void SSRPreRenderOperator::reshape(const U16 width, const U16 height)
{
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

void SSRPreRenderOperator::prepare([[maybe_unused]] const PlayerIndex idx, GFX::CommandBuffer& bufferInOut)
{
    PreRenderOperator::prepare(idx, bufferInOut);

    if (_stateChanged && !_enabled)
    {
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_SSR_CLEAR_TARGET";
        renderPassCmd->_target = RenderTargetNames::SSR_RESULT;
        renderPassCmd->_descriptor = _screenOnlyDraw;
        renderPassCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }

    _stateChanged = false;
}

bool SSRPreRenderOperator::execute( const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, [[maybe_unused]] const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut)
{
    assert(_enabled);

    RTAttachment* screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
    RTAttachment* depthAtt = _parent.screenRT()._rt->getAttachment(RTAttachmentType::DEPTH);
    const auto& normalsAtt = _context.renderTargetPool().getRenderTarget( RenderTargetNames::NORMALS_RESOLVED )->getAttachment( RTAttachmentType::COLOUR );

    const Handle<Texture> screenTex = screenAtt->texture();
    const Handle<Texture> normalsTex = normalsAtt->texture();
    const Handle<Texture> depthTex = depthAtt->texture();
    U16 screenMipCount = Get(screenAtt->texture())->mipCount();
    if (screenMipCount > 2u) {
        screenMipCount -= 2u;
    }

    const GFXShaderData::PrevFrameData& prevFrameData = _context.previousFrameData( idx );

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, screenTex, screenAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, depthTex, depthAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, normalsTex, normalsAtt->_descriptor._sampler );
    }

    GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    renderPassCmd->_target = RenderTargetNames::SSR_RESULT;
    renderPassCmd->_descriptor = _screenOnlyDraw;
    renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
    renderPassCmd->_name = "DO_SSR_PASS";

    GFX::EnqueueCommand(bufferInOut, _pipelineCmd);

    _uniforms.set( _ID( "invProjectionMatrix" ), PushConstantType::MAT4, cameraSnapshot._invProjectionMatrix );
    _uniforms.set( _ID( "invViewMatrix" ), PushConstantType::MAT4, cameraSnapshot._invViewMatrix );
    _uniforms.set( _ID( "previousViewMatrix" ), PushConstantType::MAT4, prevFrameData._previousViewMatrix );
    _uniforms.set( _ID( "previousProjectionMatrix" ), PushConstantType::MAT4, prevFrameData._previousProjectionMatrix );
    _uniforms.set( _ID( "previousViewProjectionMatrix" ), PushConstantType::MAT4, prevFrameData._previousViewProjectionMatrix );
    _uniforms.set( _ID( "screenDimensions" ), PushConstantType::VEC2, vec2<F32>( Get( screenTex )->width(), Get( screenTex )->height() ) );
    _uniforms.set( _ID( "maxScreenMips" ), PushConstantType::UINT, screenMipCount );
    _uniforms.set( _ID( "_zPlanes" ), PushConstantType::VEC2, cameraSnapshot._zPlanes );

    auto sendPushConstantsCmd = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut );
    sendPushConstantsCmd->_uniformData = &_uniforms;
    sendPushConstantsCmd->_fastData.data[0] = (cameraSnapshot._projectionMatrix * _projToPixelBasis);
    sendPushConstantsCmd->_fastData.data[1] = cameraSnapshot._projectionMatrix;
    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    return false;
}

} //namespace Divide
