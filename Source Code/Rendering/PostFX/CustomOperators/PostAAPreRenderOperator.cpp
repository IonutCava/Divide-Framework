#include "stdafx.h"

#include "Headers/PostAAPreRenderOperator.h"

#include "Utility/Headers/Localization.h"
#include "Core/Headers/Configuration.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Core/Headers/PlatformContext.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/PostFX/Headers/PreRenderBatch.h"

namespace Divide {

PostAAPreRenderOperator::PostAAPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, ResourceCache* cache)
    : PreRenderOperator(context, parent, FilterType::FILTER_SS_ANTIALIASING)
{
    useSMAA(cache->context().config().rendering.postFX.postAA.type == "SMAA");
    postAAQualityLevel(cache->context().config().rendering.postFX.postAA.qualityLevel);

    RenderTargetDescriptor desc = {};
    desc._resolution = parent.screenRT()._rt->getResolution();
    {
        SamplerDescriptor sampler = {};
        sampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        sampler.minFilter(TextureFilter::LINEAR);
        sampler.magFilter(TextureFilter::LINEAR);
        sampler.anisotropyLevel(0);

        TextureDescriptor weightsDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
        weightsDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors att = {
            { weightsDescriptor, sampler.getHash(), RTAttachmentType::Colour }
        };

        desc._name = "SMAAWeights";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();

        _smaaWeights = _context.renderTargetPool().allocateRT(desc);
    }
    { //FXAA Shader
        ShaderProgramDescriptor aaShaderDescriptor = {};
        aaShaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        aaShaderDescriptor._modules = { 
            ShaderModuleDescriptor{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" },
            ShaderModuleDescriptor{ ShaderType::FRAGMENT, "FXAA.glsl" } 
        };

        ResourceDescriptor fxaa("FXAA");
        fxaa.propertyDescriptor(aaShaderDescriptor);
        fxaa.waitForReady(false);
        _fxaa = CreateResource<ShaderProgram>(cache, fxaa);
        _fxaa->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateHash = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _fxaa->handle();

            _fxaaPipeline = _context.newPipeline(pipelineDescriptor);
        });
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
        weightsDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        ResourceDescriptor smaaWeights("SMAA.Weights");
        smaaWeights.propertyDescriptor(weightsDescriptor);
        smaaWeights.waitForReady(false);
        _smaaWeightComputation = CreateResource<ShaderProgram>(cache, smaaWeights);
        _smaaWeightComputation->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateHash = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _smaaWeightComputation->handle();

            _smaaWeightPipeline = _context.newPipeline(pipelineDescriptor);
        });

        vertModule._variant = "Blend";
        fragModule._variant = "Blend";
        ShaderProgramDescriptor blendDescriptor = {};
        blendDescriptor._modules = { vertModule, fragModule };
        blendDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        ResourceDescriptor smaaBlend("SMAA.Blend");
        smaaBlend.propertyDescriptor(blendDescriptor);
        smaaBlend.waitForReady(false);
        _smaaBlend = CreateResource<ShaderProgram>(cache, smaaBlend);
        _smaaBlend->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateHash = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _smaaBlend->handle();

            _smaaBlendPipeline = _context.newPipeline(pipelineDescriptor);
        });
    }
    { //SMAA Textures
        TextureDescriptor textureDescriptor(TextureType::TEXTURE_2D);
        textureDescriptor.srgb(false);
        textureDescriptor.textureOptions()._alphaChannelTransparency = false;

        ResourceDescriptor searchDescriptor("SMAA_Search");
        searchDescriptor.assetName(ResourcePath("smaa_search.png"));
        searchDescriptor.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
        searchDescriptor.propertyDescriptor(textureDescriptor);
        searchDescriptor.waitForReady(false);
        _searchTexture = CreateResource<Texture>(cache, searchDescriptor);

        ResourceDescriptor areaDescriptor("SMAA_Area");
        areaDescriptor.assetName(ResourcePath("smaa_area.png"));
        areaDescriptor.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
        areaDescriptor.propertyDescriptor(textureDescriptor);
        areaDescriptor.waitForReady(false);
        _areaTexture = CreateResource<Texture>(cache, areaDescriptor);
    }
    _pushConstantsCommand._constants.set(_ID("dvd_qualityMultiplier"), GFX::PushConstantType::INT, to_I32(postAAQualityLevel() - 1));
}

bool PostAAPreRenderOperator::ready() const noexcept {
    if (_smaaBlendPipeline != nullptr && _smaaWeightPipeline != nullptr && _fxaaPipeline != nullptr) {
        if (_searchTexture->getState() == ResourceState::RES_LOADED && _areaTexture->getState() == ResourceState::RES_LOADED) {
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
bool PostAAPreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {
    if (useSMAA() != currentUseSMAA()) {
        currentUseSMAA(useSMAA());

        _context.context().config().rendering.postFX.postAA.type = (useSMAA() ? "SMAA" : "FXAA");
        _context.context().config().changed(true);
    }

    if (postAAQualityLevel() != currentPostAAQualityLevel()) {
        currentPostAAQualityLevel(postAAQualityLevel());

        _pushConstantsCommand._constants.set(_ID("dvd_qualityMultiplier"), GFX::PushConstantType::INT, to_I32(postAAQualityLevel() - 1));

        _context.context().config().rendering.postFX.postAA.qualityLevel = postAAQualityLevel();
        _context.context().config().changed(true);

        if (currentPostAAQualityLevel() == 0) {
            _parent.parent().popFilter(_operatorType);
        } else {
            _parent.parent().pushFilter(_operatorType);
        }
    }

    const auto& screenAtt = input._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
    const TextureData screenTex = screenAtt.texture()->data();

    if (useSMAA()) {
        { //Step 1: Compute weights
            RTClearDescriptor clearTarget{};
            clearTarget._clearDepth = false;
            clearTarget._clearColours = true;

            GFX::ClearRenderTargetCommand clearRenderTargetCmd{};
            clearRenderTargetCmd._target = _smaaWeights._targetID;
            clearRenderTargetCmd._descriptor = clearTarget;
            GFX::EnqueueCommand(bufferInOut, clearRenderTargetCmd);

            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = _smaaWeights._targetID;
            beginRenderPassCmd._name = "DO_SMAA_WEIGHT_PASS";
            GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

            const auto& att = _parent.edgesRT()._rt->getAttachment(RTAttachmentType::Colour, 0);
            const TextureData edgesTex = att.texture()->data();
            const TextureData areaTex = _areaTexture->data();
            const TextureData searchTex = _searchTexture->data();

            SamplerDescriptor samplerDescriptor = {};

            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._usage = DescriptorSetUsage::PER_DRAW_SET;
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT0);
                binding._data._combinedImageSampler._image = edgesTex;
                binding._data._combinedImageSampler._samplerHash = att.samplerHash();
            }
            samplerDescriptor.minFilter(TextureFilter::LINEAR);
            samplerDescriptor.magFilter(TextureFilter::LINEAR);
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT1);
                binding._data._combinedImageSampler._image = areaTex;
                binding._data._combinedImageSampler._samplerHash = samplerDescriptor.getHash();
            }
            samplerDescriptor.minFilter(TextureFilter::NEAREST);
            samplerDescriptor.magFilter(TextureFilter::NEAREST);
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT1) + 1u;
                binding._data._combinedImageSampler._image = searchTex;
                binding._data._combinedImageSampler._samplerHash = samplerDescriptor.getHash();
            }

            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _smaaWeightPipeline });
            GFX::EnqueueCommand(bufferInOut, _pushConstantsCommand);

            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        }
        { //Step 2: Blend
            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = output._targetID;
            beginRenderPassCmd._descriptor = _screenOnlyDraw;
            beginRenderPassCmd._name = "DO_SMAA_BLEND_PASS";
            GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

            const auto& att = _smaaWeights._rt->getAttachment(RTAttachmentType::Colour, 0);
            const TextureData blendTex = att.texture()->data();

            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._usage = DescriptorSetUsage::PER_DRAW_SET;
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT0);
                binding._data._combinedImageSampler._image = screenTex;
                binding._data._combinedImageSampler._samplerHash = screenAtt.samplerHash();
            }
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT1);
                binding._data._combinedImageSampler._image = blendTex;
                binding._data._combinedImageSampler._samplerHash = att.samplerHash();
            }

            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _smaaBlendPipeline });
            GFX::EnqueueCommand(bufferInOut, _pushConstantsCommand);

            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        }
    } else {
        // Apply FXAA/SMAA to the specified render target
        GFX::BeginRenderPassCommand beginRenderPassCmd;
        beginRenderPassCmd._target = output._targetID;
        beginRenderPassCmd._descriptor = _screenOnlyDraw;
        beginRenderPassCmd._name = "DO_POSTAA_PASS";
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _fxaaPipeline });
        GFX::EnqueueCommand(bufferInOut, _pushConstantsCommand);

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._usage = DescriptorSetUsage::PER_DRAW_SET;
        {
            auto& binding = set._bindings.emplace_back();
            binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            binding._resourceSlot = to_U8(TextureUsage::UNIT0);
            binding._data._combinedImageSampler._image = screenTex;
            binding._data._combinedImageSampler._samplerHash = screenAtt.samplerHash();
        }

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }

    return true;
}
}
