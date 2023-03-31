#include "stdafx.h"

#include "Headers/PostFX.h"
#include "Headers/PreRenderBatch.h"
#include "Headers/PreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Rendering/PostFX/CustomOperators/Headers/BloomPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/DoFPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSRPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/MotionBlurPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/PostAAPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSAOPreRenderOperator.h"

namespace Divide {

namespace {
    //ToneMap ref: https://bruop.github.io/exposure/
    constexpr U8  GROUP_X_THREADS = 16u;
    constexpr U8  GROUP_Y_THREADS = 16u;
};

namespace TypeUtil {
    const char* ToneMapFunctionsToString(const ToneMapParams::MapFunctions stop) noexcept {
        return Names::toneMapFunctions[to_base(stop)];
    }

    ToneMapParams::MapFunctions StringToToneMapFunctions(const string& name) {
        for (U8 i = 0; i < to_U8(ToneMapParams::MapFunctions::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::toneMapFunctions[i]) == 0) {
                return static_cast<ToneMapParams::MapFunctions>(i);
            }
        }

        return ToneMapParams::MapFunctions::COUNT;
    }
}

PreRenderBatch::PreRenderBatch(GFXDevice& context, PostFX& parent, ResourceCache* cache)
    : _context(context),
      _parent(parent),
      _resCache(cache)
{
    const auto& configParams = _context.context().config().rendering.postFX.toneMap;

    std::atomic_uint loadTasks = 0;
    _toneMapParams._function = TypeUtil::StringToToneMapFunctions(configParams.mappingFunction);
    _toneMapParams._manualExposureFactor = configParams.manualExposureFactor;
    _toneMapParams._maxLogLuminance = configParams.maxLogLuminance;
    _toneMapParams._minLogLuminance = configParams.minLogLuminance;
    _toneMapParams._tau = configParams.tau;

    _adaptiveExposureControl = configParams.adaptive && _toneMapParams._function != ToneMapParams::MapFunctions::COUNT;

    // We only work with the resolved screen target
    _screenRTs._hdr._screenRef._targetID = RenderTargetNames::SCREEN;
    _screenRTs._hdr._screenRef._rt = context.renderTargetPool().getRenderTarget(_screenRTs._hdr._screenRef._targetID);

    SamplerDescriptor screenSampler = {};
    screenSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    screenSampler.mipSampling(TextureMipSampling::NONE);
    screenSampler.anisotropyLevel(0);

    RenderTargetDescriptor desc = {};
    desc._resolution = _screenRTs._hdr._screenRef._rt->getResolution();

    TextureDescriptor outputDescriptor = _screenRTs._hdr._screenRef._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO)->texture()->descriptor();
    outputDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    {
        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ outputDescriptor, screenSampler.getHash(), RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0}
        };
        desc._name = "PostFX Output HDR";

        _screenRTs._hdr._screenCopy = _context.renderTargetPool().allocateRT(desc);
    }
    {
        outputDescriptor.dataType(GFXDataFormat::UNSIGNED_BYTE);
        outputDescriptor.packing(GFXImagePacking::NORMALIZED);

        //Colour0 holds the LDR screen texture
        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ outputDescriptor, screenSampler.getHash(), RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "PostFX Output LDR 0";

        _screenRTs._ldr._temp[0] = _context.renderTargetPool().allocateRT(desc);

        desc._name = "PostFX Output LDR 1";
        _screenRTs._ldr._temp[1] = _context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor edgeDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RG );
        edgeDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ edgeDescriptor, screenSampler.getHash(), RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "SceneEdges";
        _sceneEdges = _context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor lumaDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED );
        lumaDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        lumaDescriptor.addImageUsageFlag(ImageUsage::SHADER_READ);

        ResourceDescriptor texture("Luminance Texture");
        texture.propertyDescriptor(lumaDescriptor);
        texture.waitForReady(true);
        _currentLuminance = CreateResource<Texture>(cache, texture);

        F32 val = 1.f;
        _currentLuminance->createWithData((Byte*)&val, 1u * sizeof(F32), vec2<U16>(1u), {});
    }
    {
        SamplerDescriptor defaultSampler = {};
        defaultSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        defaultSampler.minFilter(TextureFilter::NEAREST);
        defaultSampler.magFilter(TextureFilter::NEAREST);
        defaultSampler.mipSampling(TextureMipSampling::NONE);
        defaultSampler.anisotropyLevel(0);
        const size_t samplerHash = defaultSampler.getHash();

        TextureDescriptor linearDepthDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED );
        linearDepthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ linearDepthDescriptor, samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "Linear Depth";
        desc._msaaSamples = 0u;
        _linearDepthRT = _context.renderTargetPool().allocateRT(desc);
    }

    // Order is very important!
    OperatorBatch& hdrBatch = _operators[to_base(FilterSpace::FILTER_SPACE_HDR)];
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i)
    {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_HDR)
        {
            switch (fType)
            {
                case FilterType::FILTER_SS_AMBIENT_OCCLUSION:
                    hdrBatch.emplace_back(eastl::make_unique<SSAOPreRenderOperator>(_context, *this, _resCache));
                    break;

                case FilterType::FILTER_SS_REFLECTIONS:
                    hdrBatch.emplace_back(eastl::make_unique<SSRPreRenderOperator>(_context, *this, _resCache));
                    break;

                default:
                    DIVIDE_UNEXPECTED_CALL();
                    break;
            }
        }
    }

    OperatorBatch& hdr2Batch = _operators[to_base(FilterSpace::FILTER_SPACE_HDR_POST_SS)];
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i)
    {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_HDR_POST_SS)
        {
            switch (fType)
            {
               case FilterType::FILTER_DEPTH_OF_FIELD:
                   hdr2Batch.emplace_back(eastl::make_unique<DoFPreRenderOperator>(_context, *this, _resCache));
                   break;

               case FilterType::FILTER_MOTION_BLUR:
                   hdr2Batch.emplace_back(eastl::make_unique<MotionBlurPreRenderOperator>(_context, *this, _resCache));
                   break;

               case FilterType::FILTER_BLOOM:
                   hdr2Batch.emplace_back(eastl::make_unique<BloomPreRenderOperator>(_context, *this, _resCache));
                   break;

               default:
                   DIVIDE_UNEXPECTED_CALL();
                   break;
            }
        }
    }

    OperatorBatch& ldrBatch = _operators[to_base(FilterSpace::FILTER_SPACE_LDR)];
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i)
    {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_LDR)
        {
            switch (fType)
            {
                case FilterType::FILTER_SS_ANTIALIASING:
                    ldrBatch.push_back(eastl::make_unique<PostAAPreRenderOperator>(_context, *this, _resCache));
                    break;

                default:
                    DIVIDE_UNEXPECTED_CALL();
                    break;
            }
        }
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        for (U8 i = 0; i < to_base(ToneMapParams::MapFunctions::COUNT) + 1; ++i) {
            fragModule._defines.emplace_back(
                Util::StringFormat("%s %d",
                                   TypeUtil::ToneMapFunctionsToString(static_cast<ToneMapParams::MapFunctions>(i)),
                                   i));
        }
        fragModule._sourceFile = "toneMap.glsl";

        ShaderProgramDescriptor mapDescriptor1 = {};
        mapDescriptor1._modules.push_back(vertModule);
        mapDescriptor1._modules.push_back(fragModule);
        mapDescriptor1._globalDefines.emplace_back( "manualExposureFactor PushData0[0].x" );
        mapDescriptor1._globalDefines.emplace_back( "mappingFunction int(PushData0[0].y)" );
        mapDescriptor1._globalDefines.emplace_back( "useAdaptiveExposure uint(PushData0[0].z)" );
        mapDescriptor1._globalDefines.emplace_back( "skipToneMapping uint(PushData0[0].w)" );

        ResourceDescriptor toneMap("toneMap");
        toneMap.waitForReady(false);
        toneMap.propertyDescriptor(mapDescriptor1);
        _toneMap = CreateResource<ShaderProgram>(_resCache, toneMap, loadTasks);

        fragModule._defines.emplace_back("USE_ADAPTIVE_LUMINANCE");

        ShaderProgramDescriptor toneMapAdaptiveDescriptor{};
        toneMapAdaptiveDescriptor._modules.push_back(vertModule);
        toneMapAdaptiveDescriptor._modules.push_back(fragModule);
        toneMapAdaptiveDescriptor._globalDefines.emplace_back( "manualExposureFactor PushData0[0].x" );
        toneMapAdaptiveDescriptor._globalDefines.emplace_back( "mappingFunction int(PushData0[0].y)" );
        toneMapAdaptiveDescriptor._globalDefines.emplace_back( "useAdaptiveExposure uint(PushData0[0].z)" );
        toneMapAdaptiveDescriptor._globalDefines.emplace_back( "skipToneMapping uint(PushData0[0].w)" );

        ResourceDescriptor toneMapAdaptive("toneMap.Adaptive");
        toneMapAdaptive.waitForReady(false);
        toneMapAdaptive.propertyDescriptor(toneMapAdaptiveDescriptor);

        _toneMapAdaptive = CreateResource<ShaderProgram>(_resCache, toneMapAdaptive, loadTasks);

    }
    {
        ShaderModuleDescriptor computeModule = {};
        computeModule._moduleType = ShaderType::COMPUTE;
        computeModule._sourceFile = "luminanceCalc.glsl";
        computeModule._defines.emplace_back(Util::StringFormat("GROUP_SIZE %d", GROUP_X_THREADS * GROUP_Y_THREADS));
        computeModule._defines.emplace_back(Util::StringFormat("THREADS_X %d", GROUP_X_THREADS));
        computeModule._defines.emplace_back(Util::StringFormat("THREADS_Y %d", GROUP_Y_THREADS));
        {
            computeModule._variant = "Create";
            ShaderProgramDescriptor calcDescriptor = {};
            calcDescriptor._modules.push_back(computeModule);
            calcDescriptor._globalDefines.emplace_back( "u_params PushData0[0]" );

            ResourceDescriptor histogramCreate("luminanceCalc.HistogramCreate");
            histogramCreate.waitForReady(false);
            histogramCreate.propertyDescriptor(calcDescriptor);
            _createHistogram = CreateResource<ShaderProgram>(_resCache, histogramCreate, loadTasks);
        }
        {
            computeModule._variant = "Average";
            ShaderProgramDescriptor calcDescriptor = {};
            calcDescriptor._modules.push_back(computeModule);
            calcDescriptor._globalDefines.emplace_back( "dvd_minLogLum PushData0[0].x" );
            calcDescriptor._globalDefines.emplace_back( "dvd_logLumRange PushData0[0].y" );
            calcDescriptor._globalDefines.emplace_back( "dvd_timeCoeff PushData0[0].z" );
            calcDescriptor._globalDefines.emplace_back( "dvd_numPixels PushData0[0].w" );

            ResourceDescriptor histogramAverage("luminanceCalc.HistogramAverage");
            histogramAverage.waitForReady(false);
            histogramAverage.propertyDescriptor(calcDescriptor);
            _averageHistogram = CreateResource<ShaderProgram>(_resCache, histogramAverage, loadTasks);
        }
    }
    {
        ShaderProgramDescriptor edgeDetectionDescriptor = {};
        edgeDetectionDescriptor._globalDefines.emplace_back( "dvd_edgeThreshold PushData0[0].x" );

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "EdgeDetection.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "EdgeDetection.glsl";

        {
            fragModule._variant = "Depth";
            edgeDetectionDescriptor._modules = { vertModule, fragModule };

            ResourceDescriptor edgeDetectionDepth("edgeDetection.Depth");
            edgeDetectionDepth.waitForReady(false);
            edgeDetectionDepth.propertyDescriptor(edgeDetectionDescriptor);

            _edgeDetection[to_base(EdgeDetectionMethod::Depth)] = CreateResource<ShaderProgram>(_resCache, edgeDetectionDepth, loadTasks);
        }
        {
            fragModule._variant = "Luma";
            edgeDetectionDescriptor._modules = { vertModule, fragModule };

            ResourceDescriptor edgeDetectionLuma("edgeDetection.Luma");
            edgeDetectionLuma.waitForReady(false);
            edgeDetectionLuma.propertyDescriptor(edgeDetectionDescriptor);
            _edgeDetection[to_base(EdgeDetectionMethod::Luma)] = CreateResource<ShaderProgram>(_resCache, edgeDetectionLuma, loadTasks);

        }
        {
            fragModule._variant = "Colour";
            edgeDetectionDescriptor._modules = { vertModule, fragModule };

            ResourceDescriptor edgeDetectionColour("edgeDetection.Colour");
            edgeDetectionColour.waitForReady(false);
            edgeDetectionColour.propertyDescriptor(edgeDetectionDescriptor);
            _edgeDetection[to_base(EdgeDetectionMethod::Colour)] = CreateResource<ShaderProgram>(_resCache, edgeDetectionColour, loadTasks);
        }
    }
    {
        ShaderProgramDescriptor lineariseDepthBufferDescriptor = {};

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "depthPass.glsl";
        fragModule._variant = "LineariseDepthBuffer";
        fragModule._defines.emplace_back( "_zPlanes PushData0[0].xy" );

        lineariseDepthBufferDescriptor._modules = { vertModule, fragModule };

        ResourceDescriptor lineariseDepthBuffer("lineariseDepthBuffer");
        lineariseDepthBuffer.waitForReady(false);
        lineariseDepthBuffer.propertyDescriptor(lineariseDepthBufferDescriptor);
        _lineariseDepthBuffer = CreateResource<ShaderProgram>(_resCache, lineariseDepthBuffer, loadTasks);
    }

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._name = "LUMINANCE_HISTOGRAM_BUFFER";
    bufferDescriptor._ringBufferLength = 0;
    bufferDescriptor._bufferParams._elementCount = 256;
    bufferDescriptor._bufferParams._elementSize = sizeof(U32);
    bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::UNBOUND_BUFFER;
    bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
    bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::GPU_TO_GPU;

    _histogramBuffer = _context.newSB(bufferDescriptor);

    WAIT_FOR_CONDITION(operatorsReady());
    WAIT_FOR_CONDITION(loadTasks.load() == 0);

    PipelineDescriptor pipelineDescriptor{};
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;

    const size_t stateHash = _context.get2DStateBlock();
    pipelineDescriptor._stateHash = stateHash;
    pipelineDescriptor._shaderProgramHandle = _createHistogram->handle();

    _pipelineLumCalcHistogram = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._shaderProgramHandle = _averageHistogram->handle();
    _pipelineLumCalcAverage = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    pipelineDescriptor._shaderProgramHandle = _toneMapAdaptive->handle();
    _pipelineToneMapAdaptive = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._shaderProgramHandle = _toneMap->handle();
    _pipelineToneMap = _context.newPipeline(pipelineDescriptor);

    for (U8 i = 0u; i < to_U8(EdgeDetectionMethod::COUNT); ++i) {
        pipelineDescriptor._shaderProgramHandle = _edgeDetection[i]->handle();
        _edgeDetectionPipelines[i] = _context.newPipeline(pipelineDescriptor);
    }

}

PreRenderBatch::~PreRenderBatch()
{
    if (!_context.renderTargetPool().deallocateRT(_screenRTs._hdr._screenCopy) ||
        !_context.renderTargetPool().deallocateRT(_screenRTs._ldr._temp[0]) ||
        !_context.renderTargetPool().deallocateRT(_screenRTs._ldr._temp[1]) ||
        !_context.renderTargetPool().deallocateRT(_sceneEdges) ||
        !_context.renderTargetPool().deallocateRT(_linearDepthRT))
    {
        DIVIDE_UNEXPECTED_CALL();
    }
}

bool PreRenderBatch::operatorsReady() const noexcept {
    for (const OperatorBatch& batch : _operators) {
        for (const auto& op : batch) {
            if (!op->ready()) {
                return false;
            }
        }
    }

    return true;
}

PreRenderOperator* PreRenderBatch::getOperator(const FilterType type) const {
    const FilterSpace fSpace = GetOperatorSpace(type);
    if (fSpace == FilterSpace::COUNT) {
        return nullptr;
    }

    const OperatorBatch& batch = _operators[to_U32(fSpace)];
    const auto* const it = std::find_if(std::cbegin(batch), 
                                        std::cend(batch),
                                        [type](const auto& op) noexcept {
                                            return op->operatorType() == type;
                                        });

    assert(it != std::cend(batch));
    return (*it).get();
}


void PreRenderBatch::adaptiveExposureControl(const bool state) noexcept {
    _adaptiveExposureControl = state;
    _context.context().config().rendering.postFX.toneMap.adaptive = state;
    _context.context().config().changed(true);
}

F32 PreRenderBatch::adaptiveExposureValue() const {
    if (adaptiveExposureControl()) {
        const PixelAlignment pixelPackAlignment{
            ._alignment = 1u
        };

        const auto[data, size] = _currentLuminance->readData(0u, pixelPackAlignment);
        if (size > 0)
        {
            return *reinterpret_cast<F32*>(data.get());
        }
    }

    return 1.0f;
}

void PreRenderBatch::toneMapParams(const ToneMapParams params) noexcept {
    _toneMapParams = params;
    auto& configParams = _context.context().config().rendering.postFX.toneMap;
    configParams.manualExposureFactor = _toneMapParams._manualExposureFactor;
    configParams.maxLogLuminance = _toneMapParams._maxLogLuminance;
    configParams.minLogLuminance = _toneMapParams._minLogLuminance;
    configParams.tau = _toneMapParams._tau;
    configParams.mappingFunction = TypeUtil::ToneMapFunctionsToString(_toneMapParams._function);
    _context.context().config().changed(true);
}

void PreRenderBatch::update(const U64 deltaTimeUS) noexcept {
    _lastDeltaTimeUS = deltaTimeUS;
}

void PreRenderBatch::onFilterToggle(const FilterType filter, const bool state) {
    for (OperatorBatch& batch : _operators) {
        for (auto& op : batch) {
            if (filter == op->operatorType()) {
                op->onToggle(state);
            }
        }
    }
}

void PreRenderBatch::prePass(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const U32 filterStack, GFX::CommandBuffer& bufferInOut) {
    for (OperatorBatch& batch : _operators) {
        for (auto& op : batch) {
            op->prepare(idx, bufferInOut);
        }
    }

    { //Linearise depth buffer
        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._name = "LINEARISE_DEPTH_BUFFER";
        beginRenderPassCmd._target = _linearDepthRT._targetID;
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        beginRenderPassCmd._descriptor._drawMask[to_base(RTColourAttachmentSlot::SLOT_0)] = true;

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _lineariseDepthBuffer->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        GFX::BindPipelineCommand bindPipelineCmd{};
        bindPipelineCmd._pipeline = _context.newPipeline(pipelineDescriptor);

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Linearise depth buffer";
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);
        GFX::EnqueueCommand(bufferInOut, bindPipelineCmd);

        RTAttachment* depthAtt = screenRT()._rt->getAttachment(RTAttachmentType::DEPTH);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, depthAtt->texture()->getView(), depthAtt->descriptor()._samplerHash );

        PushConstantsStruct pushData{};
        pushData.data[0]._vec[0].xy = cameraSnapshot._zPlanes;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(pushData);

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    const RenderTargetHandle prevScreenHandle{
        _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN_PREV),
        RenderTargetNames::SCREEN_PREV
    };

    for (auto& op : _operators[to_base(FilterSpace::FILTER_SPACE_HDR)])
    {
        if (filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ PostFX::FilterName(op->operatorType()) });
            const bool swapTargets = op->execute(idx, cameraSnapshot, prevScreenHandle, getOutput(true), bufferInOut);
            DIVIDE_ASSERT(!swapTargets, "PreRenderBatch::prePass: Swap render target request detected during prePass!");
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // Always bind these even if we haven't ran the appropriate operators!
    RTAttachment* ssrDataAtt = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SSR_RESULT)->getAttachment(RTAttachmentType::COLOUR);
    RTAttachment* ssaoDataAtt = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SSAO_RESULT)->getAttachment(RTAttachmentType::COLOUR);

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_PASS;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 3u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, ssrDataAtt->texture()->getView(), ssrDataAtt->descriptor()._samplerHash );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 4u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, ssaoDataAtt->texture()->getView(), ssaoDataAtt->descriptor()._samplerHash );
    }
}

void PreRenderBatch::execute(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, U32 filterStack, GFX::CommandBuffer& bufferInOut) {
    _screenRTs._swappedHDR = _screenRTs._swappedLDR = false;
    _toneMapParams._width = screenRT()._rt->getWidth();
    _toneMapParams._height = screenRT()._rt->getHeight();

    // We usually want accurate data when debugging material properties, so tonemapping should probably be disabled
    if (adaptiveExposureControl()) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Compute Adaptive Exposure" });

        const F32 logLumRange = _toneMapParams._maxLogLuminance - _toneMapParams._minLogLuminance;

        { // Histogram Pass
            const Texture_ptr& screenColour = screenRT()._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO)->texture();
            const ImageView screenImage = screenColour->getView();

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "CreateLuminanceHistogram" });

                        // ToDo: This can be changed to a simple sampler instead, thus avoiding this layout change
            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
            {
                ._targetView = screenImage,
                ._sourceLayout = ImageUsage::SHADER_READ,
                ._targetLayout = ImageUsage::SHADER_READ_WRITE
            });

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, screenImage, ImageUsage::SHADER_READ_WRITE);
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 1u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, _histogramBuffer.get(), {0u, _histogramBuffer->getPrimitiveCount()});
            }
            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _pipelineLumCalcHistogram });
            PushConstantsStruct params{};
            params.data[0]._vec[0].set( _toneMapParams._minLogLuminance,
                                        1.f / logLumRange,
                                        to_F32( _toneMapParams._width ),
                                        to_F32( _toneMapParams._height ) );

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set( params );

            const U32 groupsX = to_U32(std::ceil(_toneMapParams._width / to_F32(GROUP_X_THREADS)));
            const U32 groupsY = to_U32(std::ceil(_toneMapParams._height / to_F32(GROUP_Y_THREADS)));
            GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{groupsX, groupsY, 1});

            auto memCmd = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut );
            memCmd->_bufferLocks.emplace_back(BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                ._buffer = _histogramBuffer->getBufferImpl()
            });

            // ToDo: This can be changed to a simple sampler instead, thus avoiding this layout change
            memCmd->_textureLayoutChanges.emplace_back(TextureLayoutChange
            {
                ._targetView = screenImage,
                ._sourceLayout = ImageUsage::SHADER_READ_WRITE,
                ._targetLayout = ImageUsage::SHADER_READ
            });
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }

        { // Averaging pass
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "AverageLuminanceHistogram" });

            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut)->_textureLayoutChanges.emplace_back(TextureLayoutChange
            {
                ._targetView = _currentLuminance->getView(),
                ._sourceLayout = ImageUsage::SHADER_READ,
                ._targetLayout = ImageUsage::SHADER_WRITE,
            });

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 1u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, _histogramBuffer.get(), { 0u, _histogramBuffer->getPrimitiveCount() });
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, _currentLuminance->getView(), ImageUsage::SHADER_WRITE );
            }

            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _pipelineLumCalcAverage });

            PushConstantsStruct params{};
            params.data[0]._vec[0].set(_toneMapParams._minLogLuminance,
                                       logLumRange,
                                       CLAMPED_01( 1.0f - std::exp( -Time::MicrosecondsToSeconds<F32>( _lastDeltaTimeUS ) * _toneMapParams._tau ) ),
                                       to_F32( _toneMapParams._width ) * _toneMapParams._height );


            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(params);
            GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ 1, 1, 1, });
            {
                auto memCmd = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut);
                memCmd->_bufferLocks.emplace_back( BufferLock
                {
                    ._range = { 0u, U32_MAX },
                    ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                    ._buffer = _histogramBuffer->getBufferImpl()
                });

                memCmd->_textureLayoutChanges.emplace_back(TextureLayoutChange
                {
                    ._targetView = _currentLuminance->getView(),
                    ._sourceLayout = ImageUsage::SHADER_WRITE,
                    ._targetLayout = ImageUsage::SHADER_READ,
                });
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    // We handle SSR between the Pre and Main render passes
    if (filterStack & (1u << to_U32(FilterType::FILTER_SS_REFLECTIONS)))
    {
        filterStack &= ~(1u << to_U32(FilterType::FILTER_SS_REFLECTIONS));
    }
    // We handle SSAO between the Pre and Main render passes
    if (filterStack & (1u << to_U32(FilterType::FILTER_SS_AMBIENT_OCCLUSION)))
    {
        filterStack &= ~(1u << to_U32(FilterType::FILTER_SS_AMBIENT_OCCLUSION));
    }

    OperatorBatch& hdrBatch       = _operators[to_base(FilterSpace::FILTER_SPACE_HDR)];
    OperatorBatch& hdrBatchPostSS = _operators[to_base(FilterSpace::FILTER_SPACE_HDR_POST_SS)];
    OperatorBatch& ldrBatch       = _operators[to_base(FilterSpace::FILTER_SPACE_LDR)];

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: Execute HDR(1) operators"});
    for (auto& op : hdrBatch)
    {
        if (filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ PostFX::FilterName(op->operatorType()) });
            if (op->execute(idx, cameraSnapshot, getInput(true), getOutput(true), bufferInOut))
            {
                _screenRTs._swappedHDR = !_screenRTs._swappedHDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    // Execute all HDR based operators that DO NOT need to loop back to the screen target (Bloom, DoF, etc)
    for (auto& op : hdrBatchPostSS)
    {
        if (filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ PostFX::FilterName(op->operatorType()) });
            if (op->execute(idx, cameraSnapshot, getInput(true), getOutput(true), bufferInOut))
            {
                _screenRTs._swappedHDR = !_screenRTs._swappedHDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // Copy our screen target PRE tonemap to feed back to PostFX operators in the next frame
    GFX::BlitRenderTargetCommand blitScreenColourCmd = {};
    blitScreenColourCmd._source = getInput(true)._targetID;
    blitScreenColourCmd._destination = RenderTargetNames::SCREEN_PREV;
    blitScreenColourCmd._params.emplace_back(RTBlitEntry
    {
        ._input = {
            ._index = to_base( GFXDevice::ScreenTargets::ALBEDO )
        },
        ._output = {
            ._index = to_base( RTColourAttachmentSlot::SLOT_0 )
        }
    });

    GFX::EnqueueCommand(bufferInOut, blitScreenColourCmd);

    RenderTarget* prevScreenRT = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN_PREV);
    GFX::ComputeMipMapsCommand computeMipMapsCommand{};
    computeMipMapsCommand._texture = prevScreenRT->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO)->texture().get();
    computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

    { // ToneMap and generate LDR render target (Alpha channel contains pre-toneMapped luminance value)
        static size_t lumaSamplerHash = 0u;
        if (lumaSamplerHash == 0u) {
            SamplerDescriptor lumaSampler = {};
            lumaSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
            lumaSampler.minFilter(TextureFilter::NEAREST);
            lumaSampler.magFilter(TextureFilter::NEAREST);
            lumaSampler.mipSampling(TextureMipSampling::NONE);
            lumaSampler.anisotropyLevel(0);
            lumaSamplerHash = lumaSampler.getHash();
        }

        const auto& screenAtt = getInput(true)._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
        const auto& screenDepthAtt = screenRT()._rt->getAttachment(RTAttachmentType::DEPTH);

        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: tone map" });

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenAtt->texture()->getView(), screenAtt->descriptor()._samplerHash );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _currentLuminance->getView(), lumaSamplerHash );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 2u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenDepthAtt->texture()->getView(), screenDepthAtt->descriptor()._samplerHash );
        }

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_TONEMAP_PASS";
        renderPassCmd->_target = getOutput(false)._targetID;
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ adaptiveExposureControl() ? _pipelineToneMapAdaptive : _pipelineToneMap });

        const auto mappingFunction = to_base(_context.materialDebugFlag() == MaterialDebugFlag::COUNT ? _toneMapParams._function : ToneMapParams::MapFunctions::COUNT);

        PushConstantsStruct pushData{};
        pushData.data[0]._vec[0].set( _toneMapParams._manualExposureFactor,
                                     to_F32( mappingFunction ),
                                     adaptiveExposureControl() ? 1.f : 0.f,
                                     _context.materialDebugFlag() != MaterialDebugFlag::COUNT ? 1.f : 0.f);

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(pushData);

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
    }

    // Now that we have an LDR target, proceed with edge detection. This LDR target is NOT GAMMA CORRECTED!
    if (edgeDetectionMethod() != EdgeDetectionMethod::COUNT) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: edge detection" });

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        if (edgeDetectionMethod() != EdgeDetectionMethod::Depth) {
            const auto& screenAtt = getInput(false)._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);

            DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenAtt->texture()->getView(), screenAtt->descriptor()._samplerHash );

        } else {
            const auto& depthAtt = getInput(false)._rt->getAttachment(RTAttachmentType::DEPTH);

            DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, depthAtt->texture()->getView(), depthAtt->descriptor()._samplerHash );
        }

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = _sceneEdges._targetID;
        renderPassCmd->_name = "DO_EDGE_DETECT_PASS";
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _edgeDetectionPipelines[to_base(edgeDetectionMethod())] });

        PushConstantsStruct pushData{};
        pushData.data[0]._vec[0].x = edgeDetectionThreshold();
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(pushData);

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
    
    // Execute all LDR based operators
    for (auto& op : ldrBatch)
    {
        if ( filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ Util::StringFormat("PostFX: Execute LDR operator [ %s ]", PostFX::FilterName(op->operatorType())).c_str(), to_U32(op->operatorType()) });
            if (op->execute(idx, cameraSnapshot, getInput(false), getOutput(false), bufferInOut))
            {
                _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // At this point, the last output should remain the general output. So the last swap was redundant
    _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
}

void PreRenderBatch::reshape(const U16 width, const U16 height) {
    for (OperatorBatch& batch : _operators)
    {
        for (auto& op : batch)
        {
            op->reshape(width, height);
        }
    }

    _screenRTs._hdr._screenCopy._rt->resize(width, height);
    _screenRTs._ldr._temp[0]._rt->resize(width, height);
    _screenRTs._ldr._temp[1]._rt->resize(width, height);
    _sceneEdges._rt->resize(width, height);
    _linearDepthRT._rt->resize(width, height);
}
};