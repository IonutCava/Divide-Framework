

#include "Headers/PostAAPreRenderOperator.h"

#include "Utility/Headers/Localization.h"
#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Core/Headers/PlatformContext.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

PostAAPreRenderOperator::PostAAPreRenderOperator(GFXDevice& context, PreRenderBatch& parent)
    : PreRenderOperator(context, parent, FilterType::FILTER_SS_ANTIALIASING)
{
    useSMAA(context.context().config().rendering.postFX.postAA.type == "SMAA");
    postAAQualityLevel(context.context().config().rendering.postFX.postAA.qualityLevel);

    RenderTargetDescriptor desc = {};
    desc._resolution = parent.screenRT()._rt->getResolution();
    {
        SamplerDescriptor sampler = {};
        sampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        sampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        sampler._mipSampling = TextureMipSampling::NONE;
        sampler._anisotropyLevel = 0u;

        TextureDescriptor weightsDescriptor{};
        weightsDescriptor._dataType = GFXDataFormat::FLOAT_16;
        weightsDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        weightsDescriptor._mipMappingState = MipMappingState::OFF;

        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ weightsDescriptor, sampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "SMAAWeights";

        _smaaWeights = _context.renderTargetPool().allocateRT(desc);
    }
    { //FXAA Shader
        ShaderProgramDescriptor aaShaderDescriptor = {};
        aaShaderDescriptor._globalDefines.emplace_back( "dvd_qualityMultiplier int(PushData0[0].x)" );
        aaShaderDescriptor._modules = { 
            ShaderModuleDescriptor{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" },
            ShaderModuleDescriptor{ ShaderType::FRAGMENT, "FXAA.glsl" } 
        };

        ResourceDescriptor<ShaderProgram> fxaa("FXAA", aaShaderDescriptor );
        fxaa.waitForReady(false);
        _fxaa = CreateResource(fxaa);

        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _fxaa;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _fxaaPipeline = _context.newPipeline( pipelineDescriptor );
    }
    { //SMAA Shaders
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "SMAA.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "SMAA.glsl";


        vertModule._variant = "Weight";
        fragModule._variant = "Weight";
        ShaderProgramDescriptor weightsDescriptor = {};
        weightsDescriptor._modules = { vertModule, fragModule };
        weightsDescriptor._globalDefines.emplace_back( "dvd_qualityMultiplier int(PushData0[0].x)" );

        ResourceDescriptor<ShaderProgram> smaaWeights("SMAA.Weights", weightsDescriptor );
        smaaWeights.waitForReady(false);
        _smaaWeightComputation = CreateResource(smaaWeights);

        {
            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateBlock = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _smaaWeightComputation;
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

            _smaaWeightPipeline = _context.newPipeline( pipelineDescriptor );
        }
        vertModule._variant = "Blend";
        fragModule._variant = "Blend";
        ShaderProgramDescriptor blendDescriptor = {};
        blendDescriptor._modules = { vertModule, fragModule };

        ResourceDescriptor<ShaderProgram> smaaBlend("SMAA.Blend", blendDescriptor );
        smaaBlend.waitForReady(false);
        _smaaBlend = CreateResource(smaaBlend);

        {
            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateBlock = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _smaaBlend;
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

            _smaaBlendPipeline = _context.newPipeline( pipelineDescriptor );
        }
    }
    { //SMAA Textures
        TextureDescriptor textureDescriptor{};
        textureDescriptor._textureOptions._alphaChannelTransparency = false;

        ResourceDescriptor<Texture> searchDescriptor("SMAA_Search", textureDescriptor );
        searchDescriptor.assetName("smaa_search.png");
        searchDescriptor.assetLocation(Paths::g_imagesLocation);
        searchDescriptor.waitForReady(false);
        _searchTexture = CreateResource(searchDescriptor);

        ResourceDescriptor<Texture> areaDescriptor("SMAA_Area", textureDescriptor );
        areaDescriptor.assetName("smaa_area.png");
        areaDescriptor.assetLocation(Paths::g_imagesLocation);
        areaDescriptor.waitForReady(false);
        _areaTexture = CreateResource(areaDescriptor);
    }
}

PostAAPreRenderOperator::~PostAAPreRenderOperator()
{
    WAIT_FOR_CONDITION(ready());
    DestroyResource( _fxaa );
    DestroyResource( _smaaWeightComputation );
    DestroyResource( _smaaBlend );
    DestroyResource( _searchTexture );
    DestroyResource( _areaTexture );
}

bool PostAAPreRenderOperator::ready() const noexcept
{
    if (_smaaBlendPipeline != nullptr && _smaaWeightPipeline != nullptr && _fxaaPipeline != nullptr) 
    {
        if (Get(_searchTexture)->getState() == ResourceState::RES_LOADED && Get(_areaTexture)->getState() == ResourceState::RES_LOADED)
        {
            return PreRenderOperator::ready();
        }
    }

    return false;
}

void PostAAPreRenderOperator::reshape(const U16 width, const U16 height) {
    PreRenderOperator::reshape(width, height);
    _smaaWeights._rt->resize(width, height);
}

/// This is tricky as we use our screen as both input and output
bool PostAAPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {
    if (useSMAA() != currentUseSMAA()) {
        currentUseSMAA(useSMAA());

        _context.context().config().rendering.postFX.postAA.type = (useSMAA() ? "SMAA" : "FXAA");
        _context.context().config().changed(true);
    }

    if (postAAQualityLevel() != currentPostAAQualityLevel()) {
        currentPostAAQualityLevel(postAAQualityLevel());

        _context.context().config().rendering.postFX.postAA.qualityLevel = postAAQualityLevel();
        _context.context().config().changed(true);

        if (currentPostAAQualityLevel() == 0) {
            _parent.parent().popFilter(_operatorType);
        } else {
            _parent.parent().pushFilter(_operatorType);
        }
    }

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);

    if (useSMAA()) {
        { //Step 1: Compute weights
            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = _smaaWeights._targetID;
            beginRenderPassCmd._name = "DO_SMAA_WEIGHT_PASS";
            beginRenderPassCmd._descriptor = _screenOnlyDraw;
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
            GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

            const auto& att = _parent.edgesRT()._rt->getAttachment(RTAttachmentType::COLOUR);

            SamplerDescriptor samplerDescriptor = {};

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;

            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, att->texture(), att->_descriptor._sampler );
            }
            samplerDescriptor._mipSampling = TextureMipSampling::NONE;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, _areaTexture, samplerDescriptor );
            }
            samplerDescriptor._minFilter = TextureFilter::NEAREST;
            samplerDescriptor._magFilter = TextureFilter::NEAREST;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, _searchTexture, samplerDescriptor );
            }
            
            GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _smaaWeightPipeline;

            PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
            pushData.data[0]._vec[0].x = to_F32( postAAQualityLevel() - 1 );

            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        }
        { //Step 2: Blend
            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = output._targetID;
            beginRenderPassCmd._descriptor = _screenOnlyDraw;
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
            beginRenderPassCmd._name = "DO_SMAA_BLEND_PASS";
            GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

            const auto& weightAtt = _smaaWeights._rt->getAttachment(RTAttachmentType::COLOUR);

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;

            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, weightAtt->texture(), screenAtt->_descriptor._sampler );
            }

            GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _smaaBlendPipeline;

            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        }
    } else {
        // Apply FXAA/SMAA to the specified render target
        GFX::BeginRenderPassCommand beginRenderPassCmd;
        beginRenderPassCmd._target = output._targetID;
        beginRenderPassCmd._descriptor = _screenOnlyDraw;
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
        beginRenderPassCmd._name = "DO_POSTAA_PASS";
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut )->_pipeline = _fxaaPipeline;

        PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        pushData.data[0]._vec[0].x = to_F32( postAAQualityLevel() - 1 );

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler );
        }

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }

    return true;
}
}
