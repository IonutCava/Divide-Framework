// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/GodRaysPreRenderOperator.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Camera/Headers/CameraSnapshot.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"
#include "Scenes/Headers/Scene.h"

namespace Divide {

namespace
{
    [[nodiscard]] bool ProjectLightToScreen( const float3& worldDirection,
                                             const CameraSnapshot& cameraSnapshot,
                                             float2& uvOut ) noexcept
    {
        // Direction toward the light source (opposite the light's travel direction).
        const float4 viewDir = cameraSnapshot._viewMatrix * float4( -worldDirection, 0.f );
        // Camera looks down -Z; skip lights behind the camera.
        if ( viewDir.z >= -EPSILON_F32 )
        {
            return false;
        }

        const float4 clip = cameraSnapshot._projectionMatrix * viewDir;
        if ( std::abs( clip.w ) <= EPSILON_F32 )
        {
            return false;
        }

        const float2 ndc = clip.xy / clip.w;
        uvOut = ndc * 0.5f + float2( 0.5f );
        // Allow a little overscan so shafts still fade in near the screen edge.
        return uvOut.x > -0.25f && uvOut.x < 1.25f &&
               uvOut.y > -0.25f && uvOut.y < 1.25f;
    }
}

GodRaysPreRenderOperator::GodRaysPreRenderOperator( GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter )
    : PreRenderOperator( context, parent, FilterType::FILTER_GOD_RAYS, taskCounter )
{
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    {
        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "godRays.glsl";
        fragModule._variant = "Occlusion";
        fragModule._defines.emplace_back( "dvd_lightColour PushData0[0].xyzw" );
        fragModule._defines.emplace_back( "dvd_depthThreshold PushData0[1].x" );

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back( vertModule );
        shaderDescriptor._modules.push_back( fragModule );

        ResourceDescriptor<ShaderProgram> occlusion( "GodRays.Occlusion", shaderDescriptor );
        occlusion.waitForReady( false );
        _occlusionShader = CreateResource( occlusion, taskCounter );

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _occlusionShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        _occlusionPipeline = _context.newPipeline( pipelineDescriptor );
    }
    {
        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "godRays.glsl";
        fragModule._variant = "RadialBlur";
        fragModule._defines.emplace_back( "dvd_lightScreenPos PushData0[0].xy" );
        fragModule._defines.emplace_back( "dvd_density PushData0[0].z" );
        fragModule._defines.emplace_back( "dvd_decay PushData0[0].w" );
        fragModule._defines.emplace_back( "dvd_weight PushData0[1].x" );
        fragModule._defines.emplace_back( "dvd_exposure PushData0[1].y" );
        fragModule._defines.emplace_back( "dvd_sampleCount uint(PushData0[1].z)" );

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back( vertModule );
        shaderDescriptor._modules.push_back( fragModule );

        ResourceDescriptor<ShaderProgram> blur( "GodRays.RadialBlur", shaderDescriptor );
        blur.waitForReady( false );
        _radialBlurShader = CreateResource( blur, taskCounter );

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _radialBlurShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        BlendingSettings& blend = pipelineDescriptor._blendStates._settings[to_U8( RTColourAttachmentSlot::SLOT_0 )];
        blend.enabled( true );
        blend.blendSrc( BlendProperty::ONE );
        blend.blendDest( BlendProperty::ONE );
        blend.blendOp( BlendOperation::ADD );

        _radialBlurPipeline = _context.newPipeline( pipelineDescriptor );
    }

    {
        SamplerDescriptor sampler{};
        sampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        sampler._minFilter = TextureFilter::LINEAR;
        sampler._magFilter = TextureFilter::LINEAR;
        sampler._mipSampling = TextureMipSampling::NONE;

        TextureDescriptor descriptor{};
        descriptor._dataType = GFXDataFormat::FLOAT_16;
        descriptor._packing = GFXImagePacking::RGB_111110F;
        descriptor._baseFormat = GFXImageFormat::RGB;
        descriptor._mipMappingState = MipMappingState::OFF;

        RenderTargetDescriptor rtDesc{};
        rtDesc._attachments = {
            InternalRTAttachmentDescriptor{ descriptor, sampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };
        rtDesc._name = "GodRays Occlusion";
        rtDesc._resolution = _parent.screenRT()._rt->getResolution() * 0.5f;
        rtDesc._msaaSamples = 0u;
        _occlusionRT = _context.renderTargetPool().allocateRT( rtDesc );
    }

    const auto& cfg = _context.context().config().rendering.postFX.godRays;
    _density = cfg.density;
    _decay = cfg.decay;
    _weight = cfg.weight;
    _exposure = cfg.exposure;
    _sampleCount = cfg.sampleCount;
}

GodRaysPreRenderOperator::~GodRaysPreRenderOperator()
{
    DestroyResource( _occlusionShader );
    DestroyResource( _radialBlurShader );
    if ( _occlusionRT._targetID != INVALID_RENDER_TARGET_ID )
    {
        DIVIDE_EXPECTED_CALL( _context.renderTargetPool().deallocateRT( _occlusionRT ) );
    }
}

bool GodRaysPreRenderOperator::ready() const noexcept
{
    if ( _occlusionPipeline != nullptr && _radialBlurPipeline != nullptr )
    {
        return PreRenderOperator::ready();
    }

    return false;
}

void GodRaysPreRenderOperator::reshape( const U16 width, const U16 height )
{
    PreRenderOperator::reshape( width, height );
    if ( _occlusionRT._rt != nullptr )
    {
        _occlusionRT._rt->resize( width / 2u, height / 2u );
    }
}

void GodRaysPreRenderOperator::density( const F32 val )
{
    _density = val;
    _context.context().config().rendering.postFX.godRays.density = val;
    _context.context().config().changed( true );
}

void GodRaysPreRenderOperator::decay( const F32 val )
{
    _decay = val;
    _context.context().config().rendering.postFX.godRays.decay = val;
    _context.context().config().changed( true );
}

void GodRaysPreRenderOperator::weight( const F32 val )
{
    _weight = val;
    _context.context().config().rendering.postFX.godRays.weight = val;
    _context.context().config().changed( true );
}

void GodRaysPreRenderOperator::exposure( const F32 val )
{
    _exposure = val;
    _context.context().config().rendering.postFX.godRays.exposure = val;
    _context.context().config().changed( true );
}

void GodRaysPreRenderOperator::sampleCount( const U8 val )
{
    _sampleCount = std::max<U8>( 1u, val );
    _context.context().config().rendering.postFX.godRays.sampleCount = _sampleCount;
    _context.context().config().changed( true );
}

bool GodRaysPreRenderOperator::execute( [[maybe_unused]] const PlayerIndex idx,
                                        const CameraSnapshot& cameraSnapshot,
                                        [[maybe_unused]] const RenderTargetHandle& input,
                                        [[maybe_unused]] const RenderTargetHandle& output,
                                        GFX::CommandBuffer& bufferInOut )
{
    DIVIDE_ASSERT( _enabled );

    ProjectManager* mgr = _context.context().kernel().projectManager().get();
    if ( mgr == nullptr || mgr->activeProject() == nullptr || mgr->activeProject()->getActiveScene() == nullptr )
    {
        return false;
    }

    LightPool& lightPool = *mgr->activeProject()->getActiveScene()->lightPool();
    const auto& lights = lightPool.getLights( LightType::DIRECTIONAL );

    struct Contributor
    {
        float2 uv{};
        float3 colour{};
    };

    vector<Contributor> contributors;
    contributors.reserve( lights.size() );
    for ( Light* light : lights )
    {
        if ( light == nullptr || !light->enabled() || light->intensity() <= EPSILON_F32 )
        {
            continue;
        }

        auto* dirLight = static_cast<DirectionalLightComponent*>( light );
        if ( !dirLight->enableGodRays() )
        {
            continue;
        }

        float2 uv{};
        if ( !ProjectLightToScreen( dirLight->directionCache(), cameraSnapshot, uv ) )
        {
            continue;
        }

        Contributor& entry = contributors.emplace_back();
        entry.uv = uv;
        const FColour3 diffuse = dirLight->getDiffuseColour();
        const F32 intensity = dirLight->intensity();
        entry.colour = float3( diffuse.r * intensity, diffuse.g * intensity, diffuse.b * intensity );
    }

    RTAttachment* depthAtt = _parent.screenRT()._rt->getAttachment( RTAttachmentType::DEPTH );
    const Handle<Texture> depthTex = depthAtt->texture();
    const auto& occlusionAtt = _occlusionRT._rt->getAttachment( RTAttachmentType::COLOUR );

    // Always clear so stale shafts disappear when every contributor is culled.
    {
        GFX::BeginRenderPassCommand* clearCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
        clearCmd->_name = "DO_GOD_RAYS_CLEAR";
        clearCmd->_target = RenderTargetNames::GOD_RAYS_RESULT;
        clearCmd->_descriptor = _screenOnlyDraw;
        clearCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
    }

    if ( contributors.empty() )
    {
        return false;
    }

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "PostFX: God Rays";

    for ( const Contributor& contributor : contributors )
    {
        {
            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            renderPassCmd->_name = "DO_GOD_RAYS_OCCLUSION";
            renderPassCmd->_target = _occlusionRT._targetID;
            renderPassCmd->_descriptor = _screenOnlyDraw;
            renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _occlusionPipeline;

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, depthTex, depthAtt->_descriptor._sampler );
            }

            PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
            pushData.data[0]._vec[0].set( contributor.colour.r, contributor.colour.g, contributor.colour.b, 1.f );
            pushData.data[0]._vec[1].x = 0.9999f;

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
        }
        {
            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            renderPassCmd->_name = "DO_GOD_RAYS_BLUR";
            renderPassCmd->_target = RenderTargetNames::GOD_RAYS_RESULT;
            renderPassCmd->_descriptor = _screenOnlyDraw;
            renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )]._enabled = false;

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _radialBlurPipeline;

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, occlusionAtt->texture(), occlusionAtt->_descriptor._sampler );
            }

            PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
            pushData.data[0]._vec[0].set( contributor.uv.x, contributor.uv.y, _density, _decay );
            pushData.data[0]._vec[1].set( _weight, _exposure, to_F32( _sampleCount ), 0.f );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
        }
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

    return false;
}

} //namespace Divide
