#include "stdafx.h"

#include "Headers/SingleShadowMapGenerator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/SceneState.h"
#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Rendering/Camera/Headers/Camera.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "ECS/Components/Headers/SpotLightComponent.h"

namespace Divide {

namespace
{
    Configuration::Rendering::ShadowMapping g_shadowSettings;
};

SingleShadowMapGenerator::SingleShadowMapGenerator(GFXDevice& context)
    : ShadowMapGenerator(context, ShadowType::SINGLE)
{

    Console::printfn(Locale::Get(_ID("LIGHT_CREATE_SHADOW_FB")), "Single Shadow Map");

    g_shadowSettings = context.context().config().rendering.shadowMapping;
    {
        ShaderModuleDescriptor vertModule{ ShaderType::VERTEX,   "baseVertexShaders.glsl", "FullScreenQuad" };
        ShaderModuleDescriptor geomModule{ ShaderType::GEOMETRY, "blur.glsl",              "GaussBlur" };
        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "blur.glsl",              "GaussBlur.Layered" };

        geomModule._defines.emplace_back( "verticalBlur uint(PushData0[1].x)" );
        geomModule._defines.emplace_back( "layerCount int(PushData0[1].y)" );
        geomModule._defines.emplace_back( "layerOffsetRead int(PushData0[1].z)" );
        geomModule._defines.emplace_back( "layerOffsetWrite int(PushData0[1].w)" );

        geomModule._defines.emplace_back( "layerOffsetWrite int(PushData0[1].w)" );

        fragModule._defines.emplace_back( "LAYERED" );
        fragModule._defines.emplace_back( "layer uint(PushData0[0].x)" );
        fragModule._defines.emplace_back( "size PushData0[0].yz" );
        fragModule._defines.emplace_back( "kernelSize int(PushData0[0].w)" );
        fragModule._defines.emplace_back( "verticalBlur uint(PushData0[1].x)" );

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(geomModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._globalDefines.emplace_back(Util::StringFormat("GS_MAX_INVOCATIONS %d", Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS));

        ResourceDescriptor blurDepthMapShader(Util::StringFormat("GaussBlur_%d_invocations", Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS).c_str());
        blurDepthMapShader.waitForReady(true);
        blurDepthMapShader.propertyDescriptor(shaderDescriptor);

        _blurDepthMapShader = CreateResource<ShaderProgram>(context.context().kernel().resourceCache(), blurDepthMapShader);
        _blurDepthMapShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*)
        {
            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._stateBlock = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _blurDepthMapShader->handle();
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

            _blurPipeline = _context.newPipeline(pipelineDescriptor);
        });
    }

    _shaderConstants.data[0]._vec[1].y = 1.f;
    _shaderConstants.data[0]._vec[1].z = 0.f;
    _shaderConstants.data[0]._vec[1].w = 0.f;

    std::array<vec2<F32>*, 12> blurSizeConstants = {
            &_shaderConstants.data[0]._vec[2].xy,
            &_shaderConstants.data[0]._vec[2].zw,
            &_shaderConstants.data[0]._vec[3].xy,
            &_shaderConstants.data[0]._vec[3].zw,
            &_shaderConstants.data[1]._vec[0].xy,
            &_shaderConstants.data[1]._vec[0].zw,
            &_shaderConstants.data[1]._vec[1].xy,
            &_shaderConstants.data[1]._vec[1].zw,
            &_shaderConstants.data[1]._vec[2].xy,
            &_shaderConstants.data[1]._vec[2].zw,
            &_shaderConstants.data[1]._vec[3].xy,
            &_shaderConstants.data[1]._vec[3].zw
    };

    blurSizeConstants[0]->set( 1.f / g_shadowSettings.spot.shadowMapResolution );
    for ( size_t i = 1u; i < blurSizeConstants.size(); ++i )
    {
        blurSizeConstants[i]->set((*blurSizeConstants[i - 1]) * 0.5f);
    }

    SamplerDescriptor sampler = {};
    sampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    sampler.mipSampling(TextureMipSampling::NONE);
    sampler.anisotropyLevel(0);
    const size_t samplerHash = sampler.getHash();

    const RenderTarget* rt = ShadowMap::getShadowMap(_type)._rt;
    const TextureDescriptor& texDescriptor = rt->getAttachment(RTAttachmentType::COLOUR)->texture()->descriptor();

    //Draw FBO
    {
        RenderTargetDescriptor desc = {};
        desc._resolution = rt->getResolution();

        TextureDescriptor colourDescriptor(TextureType::TEXTURE_2D_ARRAY, texDescriptor.dataType(), texDescriptor.baseFormat() );
        colourDescriptor.layerCount(1u);
        colourDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        TextureDescriptor depthDescriptor(TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::RED, GFXImagePacking::DEPTH);
        depthDescriptor.layerCount( 1u );
        depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        desc._attachments = 
        {
            InternalRTAttachmentDescriptor{ colourDescriptor, samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
            InternalRTAttachmentDescriptor{ depthDescriptor, samplerHash, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "Single_ShadowMap_Draw";
        desc._msaaSamples = g_shadowSettings.spot.MSAASamples;

        _drawBufferDepth = context.renderTargetPool().allocateRT(desc);
    }

    //Blur FBO
    {
        TextureDescriptor blurMapDescriptor(TextureType::TEXTURE_2D_ARRAY, texDescriptor.dataType(), texDescriptor.baseFormat() );
        blurMapDescriptor.layerCount(1u);
        blurMapDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RenderTargetDescriptor desc = {};
        desc._attachments = 
        {
            InternalRTAttachmentDescriptor{ blurMapDescriptor, samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "Single_Blur";
        desc._resolution = rt->getResolution();

        _blurBuffer = _context.renderTargetPool().allocateRT(desc);
    }

    WAIT_FOR_CONDITION(_blurPipeline != nullptr);
}

SingleShadowMapGenerator::~SingleShadowMapGenerator()
{
    if (!_context.renderTargetPool().deallocateRT(_drawBufferDepth))
    {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void SingleShadowMapGenerator::render([[maybe_unused]] const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    const vec3<F32> lightPos = light.positionCache();
    const F32 farPlane = light.range() * 1.2f;

    auto& shadowCameras = ShadowMap::shadowCameras(ShadowType::SINGLE);
    const mat4<F32> viewMatrix = shadowCameras[0]->lookAt(lightPos, lightPos + light.directionCache() * farPlane);
    const mat4<F32> projectionMatrix = shadowCameras[0]->setProjection(1.0f, 90.0f, vec2<F32>(0.01f, farPlane));
    shadowCameras[0]->updateLookAt();

    mat4<F32> lightVP = light.getShadowVPMatrix(0);
    mat4<F32>::Multiply(projectionMatrix, viewMatrix, lightVP);
    light.setShadowLightPos(0, lightPos);
    light.setShadowFloatValue(0, shadowCameras[0]->snapshot()._zPlanes.max);
    light.setShadowVPMatrix(0, mat4<F32>::Multiply( MAT4_BIAS_ZERO_ONE_Z, lightVP ));

    RenderPassParams params = {};
    params._sourceNode = light.sgn();
    params._stagePass = { RenderStage::SHADOW, RenderPassType::COUNT, lightIndex, static_cast<RenderStagePass::VariantType>(ShadowType::SINGLE) };
    params._target = _drawBufferDepth._targetID;
    params._passName = "SingleShadowMap";
    params._maxLoD = -1;
    params._refreshLightData = false;
    params._useMSAA = _context.context().config().rendering.shadowMapping.spot.MSAASamples;
    params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
    params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
    params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Single Shadow Pass Light: [ %d ]", lightIndex).c_str(), lightIndex));

    _context.context().kernel().renderPassManager()->doCustomPass(shadowCameras[0], params, bufferInOut, memCmdInOut);

    GFX::BlitRenderTargetCommand blitRenderTargetCommand = {};
    blitRenderTargetCommand._source = _drawBufferDepth._targetID;
    blitRenderTargetCommand._destination = ShadowMap::getShadowMap( _type )._targetID;
    blitRenderTargetCommand._params.emplace_back( RTBlitEntry{
        ._input = {
            ._layerOffset = 0u,
            ._index = 0u
        },
        ._output = {
            ._layerOffset = light.getShadowArrayOffset(),
            ._index = 0u
        }
    } );

    GFX::EnqueueCommand(bufferInOut, blitRenderTargetCommand);

    if ( g_shadowSettings.spot.enableBlurring )
    {
        blurTarget( light.getShadowArrayOffset(), bufferInOut );
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SingleShadowMapGenerator::blurTarget( const U16 layerOffset, GFX::CommandBuffer& bufferInOut )
{
    const RenderTargetHandle& handle = ShadowMap::getShadowMap( _type );
    const RenderTarget* shadowMapRT = handle._rt;
    const auto& shadowAtt = shadowMapRT->getAttachment( RTAttachmentType::COLOUR );

    // Now we can either blur our target or just skip to mipmap computation
    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._descriptor._layeredRendering = true;

    beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
    beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

    // Blur horizontally
    beginRenderPassCmd._target = _blurBuffer._targetID;
    beginRenderPassCmd._name = "DO_SM_BLUR_PASS_HORIZONTAL";
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _blurPipeline });
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, shadowAtt->texture()->getView(), shadowAtt->_descriptor._samplerHash );
    }

    _shaderConstants.data[0]._vec[1].x = 0.f;
    _shaderConstants.data[0]._vec[1].y = 1.f;
    _shaderConstants.data[0]._vec[1].z = to_F32( layerOffset );
    _shaderConstants.data[0]._vec[1].w = 0.f;

    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set( _shaderConstants );

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

    // Blur vertically
    beginRenderPassCmd._target = handle._targetID;
    beginRenderPassCmd._name = "DO_SM_BLUR_PASS_VERTICAL";
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

    const auto& blurAtt = _blurBuffer._rt->getAttachment(RTAttachmentType::COLOUR);
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, blurAtt->texture()->getView(), blurAtt->_descriptor._samplerHash );
    }


    _shaderConstants.data[0]._vec[1].x = 1.f;
    _shaderConstants.data[0]._vec[1].y = 1.f;
    _shaderConstants.data[0]._vec[1].z = 0.f;
    _shaderConstants.data[0]._vec[1].w = to_F32( layerOffset );

    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( _shaderConstants );

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

}

void SingleShadowMapGenerator::updateMSAASampleCount(const U8 sampleCount)
{
    if (_context.context().config().rendering.shadowMapping.spot.MSAASamples != sampleCount)
    {
        _context.context().config().rendering.shadowMapping.spot.MSAASamples = sampleCount;
        _drawBufferDepth._rt->updateSampleCount(sampleCount);
    }
}
};
