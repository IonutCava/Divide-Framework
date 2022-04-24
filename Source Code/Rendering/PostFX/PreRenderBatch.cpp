#include "stdafx.h"

#include "Headers/PreRenderBatch.h"
#include "Headers/PreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Headers/PostFX.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

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
    _screenRTs._hdr._screenRef._targetID = RenderTargetUsage::SCREEN;
    _screenRTs._hdr._screenRef._rt = &context.renderTargetPool().renderTarget(_screenRTs._hdr._screenRef._targetID);

    SamplerDescriptor screenSampler = {};
    screenSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    screenSampler.minFilter(TextureFilter::LINEAR);
    screenSampler.magFilter(TextureFilter::LINEAR);
    screenSampler.anisotropyLevel(0);

    RenderTargetDescriptor desc = {};
    desc._resolution = _screenRTs._hdr._screenRef._rt->getResolution();
    desc._attachmentCount = 1u;

    TextureDescriptor outputDescriptor = _screenRTs._hdr._screenRef._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO)).texture()->descriptor();
    outputDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    {
        RTAttachmentDescriptors att = { { outputDescriptor, screenSampler.getHash(), RTAttachmentType::Colour } };
        desc._name = "PostFX Output HDR";
        desc._attachments = att.data();

        _screenRTs._hdr._screenCopy = _context.renderTargetPool().allocateRT(desc);
    }
    {
        outputDescriptor.dataType(GFXDataFormat::UNSIGNED_BYTE);
        //Colour0 holds the LDR screen texture
        RTAttachmentDescriptors att = { { outputDescriptor, screenSampler.getHash(), RTAttachmentType::Colour } };

        desc._name = "PostFX Output LDR 0";
        desc._attachments = att.data();

        _screenRTs._ldr._temp[0] = _context.renderTargetPool().allocateRT(desc);

        desc._name = "PostFX Output LDR 1";
        _screenRTs._ldr._temp[1] = _context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor edgeDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RG, GFXDataFormat::FLOAT_16);
        edgeDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors att = { { edgeDescriptor, screenSampler.getHash(), RTAttachmentType::Colour } };

        desc._name = "SceneEdges";
        desc._attachments = att.data();
        _sceneEdges = _context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor lumaDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RED, GFXDataFormat::FLOAT_16);
        lumaDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        lumaDescriptor.srgb(false);

        ResourceDescriptor texture("Luminance Texture");
        texture.propertyDescriptor(lumaDescriptor);
        texture.waitForReady(true);
        _currentLuminance = CreateResource<Texture>(cache, texture);

        F32 val = 1.f;
        _currentLuminance->loadData((Byte*)&val, 1u * sizeof(F32), vec2<U16>(1u));
    }

    // Order is very important!
    OperatorBatch& hdrBatch = _operators[to_base(FilterSpace::FILTER_SPACE_HDR)];
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i) {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_HDR) {
            switch (fType) {
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
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i) {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_HDR_POST_SS) {
            switch (fType) {
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
    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i) {
        const FilterType fType = static_cast<FilterType>(i);

        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_LDR) {
            switch (fType) {
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

        ResourceDescriptor toneMap("toneMap");
        toneMap.waitForReady(false);
        toneMap.propertyDescriptor(mapDescriptor1);
        _toneMap = CreateResource<ShaderProgram>(_resCache, toneMap, loadTasks);

        fragModule._defines.emplace_back("USE_ADAPTIVE_LUMINANCE");

        ShaderProgramDescriptor toneMapAdaptiveDescriptor{};
        toneMapAdaptiveDescriptor._modules.push_back(vertModule);
        toneMapAdaptiveDescriptor._modules.push_back(fragModule);

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

            ResourceDescriptor histogramCreate("luminanceCalc.HistogramCreate");
            histogramCreate.waitForReady(false);
            histogramCreate.propertyDescriptor(calcDescriptor);
            _createHistogram = CreateResource<ShaderProgram>(_resCache, histogramCreate, loadTasks);
        }
        {
            computeModule._variant = "Average";
            ShaderProgramDescriptor calcDescriptor = {};
            calcDescriptor._modules.push_back(computeModule);

            ResourceDescriptor histogramAverage("luminanceCalc.HistogramAverage");
            histogramAverage.waitForReady(false);
            histogramAverage.propertyDescriptor(calcDescriptor);
            _averageHistogram = CreateResource<ShaderProgram>(_resCache, histogramAverage, loadTasks);
        }
    }
    {
        ShaderProgramDescriptor edgeDetectionDescriptor = {};
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

        lineariseDepthBufferDescriptor._modules = { vertModule, fragModule };
        ResourceDescriptor lineariseDepthBuffer("lineariseDepthBuffer");
        lineariseDepthBuffer.waitForReady(false);
        lineariseDepthBuffer.propertyDescriptor(lineariseDepthBufferDescriptor);
        _lineariseDepthBuffer = CreateResource<ShaderProgram>(_resCache, lineariseDepthBuffer, loadTasks);
    }

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._name = "LUMINANCE_HISTOGRAM_BUFFER";
    bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
    bufferDescriptor._ringBufferLength = 0;
    bufferDescriptor._bufferParams._elementCount = 256;
    bufferDescriptor._bufferParams._elementSize = sizeof(U32);
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;

    _histogramBuffer = _context.newSB(bufferDescriptor);

    WAIT_FOR_CONDITION(operatorsReady());
    WAIT_FOR_CONDITION(loadTasks.load() == 0);

    PipelineDescriptor pipelineDescriptor{};

    const size_t stateHash = _context.get2DStateBlock();
    pipelineDescriptor._stateHash = stateHash;
    pipelineDescriptor._shaderProgramHandle = _createHistogram->handle();
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _pipelineLumCalcHistogram = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._shaderProgramHandle = _averageHistogram->handle();
    _pipelineLumCalcAverage = _context.newPipeline(pipelineDescriptor);

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
    _context.renderTargetPool().deallocateRT(_screenRTs._hdr._screenCopy);
    _context.renderTargetPool().deallocateRT(_screenRTs._ldr._temp[0]);
    _context.renderTargetPool().deallocateRT(_screenRTs._ldr._temp[1]);
    _context.renderTargetPool().deallocateRT(_sceneEdges);
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

RenderTargetHandle PreRenderBatch::getTarget(const bool hdr, const bool swapped) const noexcept {
    if (hdr) {
        return swapped ? _screenRTs._hdr._screenCopy : _screenRTs._hdr._screenRef;
    }

    return _screenRTs._ldr._temp[swapped ? 0 : 1];
}

RenderTargetHandle PreRenderBatch::getInput(const bool hdr) const {
    return getTarget(hdr, hdr ? _screenRTs._swappedHDR : _screenRTs._swappedLDR);
}

RenderTargetHandle PreRenderBatch::getOutput(const bool hdr) const {
    return getTarget(hdr, hdr ? !_screenRTs._swappedHDR : !_screenRTs._swappedLDR);
}

void PreRenderBatch::adaptiveExposureControl(const bool state) noexcept {
    _adaptiveExposureControl = state;
    _context.context().config().rendering.postFX.toneMap.adaptive = state;
    _context.context().config().changed(true);
}

F32 PreRenderBatch::adaptiveExposureValue() const {
    if (adaptiveExposureControl()) {
        const auto[data, size] = _currentLuminance->readData(0, GFXDataFormat::FLOAT_32);
        if (size > 0) {
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

RenderTargetHandle PreRenderBatch::screenRT() const noexcept {
    return _screenRTs._hdr._screenRef;
}

RenderTargetHandle PreRenderBatch::edgesRT() const noexcept {
    return _sceneEdges;
}

Texture_ptr PreRenderBatch::luminanceTex() const noexcept {
    return _currentLuminance;
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
        GFX::ClearRenderTargetCommand clearLinearDepthCmd{};
        clearLinearDepthCmd._target = { RenderTargetUsage::LINEAR_DEPTH };
        clearLinearDepthCmd._descriptor._clearDepth = false;
        clearLinearDepthCmd._descriptor._clearColours = true;
        clearLinearDepthCmd._descriptor._resetToDefault = true;

        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        beginRenderPassCmd._name = "LINEARISE_DEPTH_BUFFER";
        beginRenderPassCmd._target = { RenderTargetUsage::LINEAR_DEPTH };

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _lineariseDepthBuffer->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        GFX::BindPipelineCommand bindPipelineCmd{};
        bindPipelineCmd._pipeline = _context.newPipeline(pipelineDescriptor);

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Linearise depth buffer";
        GFX::EnqueueCommand(bufferInOut, clearLinearDepthCmd);
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);
        GFX::EnqueueCommand(bufferInOut, bindPipelineCmd);
        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;

        const RTAttachment& depthAtt = screenRT()._rt->getAttachment(RTAttachmentType::Depth, 0);
        set._textureData.add(TextureEntry{ depthAtt.texture()->data(), depthAtt.samplerHash(), TextureUsage::DEPTH });

        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, cameraSnapshot._zPlanes);

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    const RenderTargetHandle prevScreenHandle{
        RenderTargetUsage::SCREEN_PREV,
        & _context.renderTargetPool().renderTarget(RenderTargetUsage::SCREEN_PREV)
    };

    for (auto& op : _operators[to_base(FilterSpace::FILTER_SPACE_HDR)]) {
        if (BitCompare(filterStack, 1u << to_U32(op->operatorType()))) {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ PostFX::FilterName(op->operatorType()) });
            const bool swapTargets = op->execute(idx, cameraSnapshot, prevScreenHandle, getOutput(true), bufferInOut);
            DIVIDE_ASSERT(!swapTargets, "PreRenderBatch::prePass: Swap render target request detected during prePass!");
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // Always bind these even if we haven't ran the appropriate operatos!
    const RTAttachment& ssrDataAtt = _context.renderTargetPool().renderTarget(RenderTargetUsage::SSR_RESULT).getAttachment(RTAttachmentType::Colour, 0);
    const RTAttachment& ssaoDataAtt = _context.renderTargetPool().renderTarget(RenderTargetUsage::SSAO_RESULT).getAttachment(RTAttachmentType::Colour, 0u);

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._textureData.add(TextureEntry{ ssrDataAtt.texture()->data(),  ssrDataAtt.samplerHash(),  TextureUsage::SSR_SAMPLE });
    set._textureData.add(TextureEntry{ ssaoDataAtt.texture()->data(), ssaoDataAtt.samplerHash(), TextureUsage::SSAO_SAMPLE });
}

void PreRenderBatch::execute(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, U32 filterStack, GFX::CommandBuffer& bufferInOut) {
    _screenRTs._swappedHDR = _screenRTs._swappedLDR = false;
    _toneMapParams._width = screenRT()._rt->getWidth();
    _toneMapParams._height = screenRT()._rt->getHeight();

    // We usually want accurate data when debugging material properties, so tonemapping should probably be disabled
    if (adaptiveExposureControl()) {
        const F32 logLumRange = _toneMapParams._maxLogLuminance - _toneMapParams._minLogLuminance;
        const F32 histogramParams[4] = {
                _toneMapParams._minLogLuminance,
                1.0f / logLumRange,
                to_F32(_toneMapParams._width),
                to_F32(_toneMapParams._height),
        };
        const ShaderBufferBinding shaderBuffer{
            { 0u, _histogramBuffer->getPrimitiveCount() },
            _histogramBuffer.get(),
            ShaderBufferLocation::LUMINANCE_HISTOGRAM,
            ShaderBufferLockType::AFTER_DRAW_COMMANDS
        };

        { // Histogram Pass
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "CreateLuminanceHistogram" });

            const Texture_ptr& screenColour = screenRT()._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO)).texture();
            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._buffers.add(shaderBuffer);
            set._images.add(Image{
                screenColour.get(),
                Image::Flag::READ,
                false,
                0u,
                0u,
                to_base(TextureUsage::UNIT0)
            });

            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _pipelineLumCalcHistogram });
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(_ID("u_params"), GFX::PushConstantType::VEC4, histogramParams);

            const U32 groupsX = to_U32(std::ceil(_toneMapParams._width / to_F32(GROUP_X_THREADS)));
            const U32 groupsY = to_U32(std::ceil(_toneMapParams._height / to_F32(GROUP_Y_THREADS)));
            GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{groupsX, groupsY, 1});
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_U32(MemoryBarrierType::BUFFER_UPDATE) });

        { // Averaging pass
            const F32 deltaTime = Time::MicrosecondsToSeconds<F32>(_lastDeltaTimeUS);
            const F32 timeCoeff = CLAMPED_01(1.0f - std::exp(-deltaTime * _toneMapParams._tau));

            const F32 avgParams[4] = {
                _toneMapParams._minLogLuminance,
                logLumRange,
                timeCoeff,
                to_F32(_toneMapParams._width) * _toneMapParams._height,
            };

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "AverageLuminanceHistogram" });
            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._buffers.add(shaderBuffer);
            set._images.add(Image{
                _currentLuminance.get(),
                Image::Flag::READ_WRITE,
                false,
                0u,
                0u,
                to_base(TextureUsage::UNIT0)
            });
            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _pipelineLumCalcAverage });
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(_ID("u_params"), GFX::PushConstantType::VEC4, avgParams);
            GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ 1, 1, 1, });
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_IMAGE) | to_base(MemoryBarrierType::SHADER_STORAGE) });
    }

    // We handle SSR between the Pre and Main render passes
    if (BitCompare(filterStack, 1u << to_U32(FilterType::FILTER_SS_REFLECTIONS))) {
        ClearBit(filterStack, 1u << to_U32(FilterType::FILTER_SS_REFLECTIONS));
    }
    // We handle SSAO between the Pre and Main render passes
    if (BitCompare(filterStack, 1u << to_U32(FilterType::FILTER_SS_AMBIENT_OCCLUSION))) {
        ClearBit(filterStack, 1u << to_U32(FilterType::FILTER_SS_AMBIENT_OCCLUSION));
    }

    OperatorBatch& hdrBatch       = _operators[to_base(FilterSpace::FILTER_SPACE_HDR)];
    OperatorBatch& hdrBatchPostSS = _operators[to_base(FilterSpace::FILTER_SPACE_HDR_POST_SS)];
    OperatorBatch& ldrBatch       = _operators[to_base(FilterSpace::FILTER_SPACE_LDR)];

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: Execute HDR(1) operators"});
    for (auto& op : hdrBatch) {
        if (BitCompare(filterStack, 1u << to_U32(op->operatorType()))) {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ PostFX::FilterName(op->operatorType()) });
            if (op->execute(idx, cameraSnapshot, getInput(true), getOutput(true), bufferInOut)) {
                _screenRTs._swappedHDR = !_screenRTs._swappedHDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    // Execute all HDR based operators that DO NOT need to loop back to the screen target (Bloom, DoF, etc)
    for (auto& op : hdrBatchPostSS) {
        if (BitCompare(filterStack, 1u << to_U32(op->operatorType()))) {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ Util::StringFormat("PostFX: Execute HDR (2) operator [ %s ]", PostFX::FilterName(op->operatorType())).c_str() });
            if (op->execute(idx, cameraSnapshot, getInput(true), getOutput(true), bufferInOut)) {
                _screenRTs._swappedHDR = !_screenRTs._swappedHDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // Copy our screen target PRE tonemap to feed back to PostFX operators in the next frame
    GFX::BlitRenderTargetCommand blitScreenColourCmd = {};
    blitScreenColourCmd._source = getInput(true)._targetID;
    blitScreenColourCmd._destination = RenderTargetUsage::SCREEN_PREV;
    blitScreenColourCmd._blitColours[0].set(to_U16(GFXDevice::ScreenTargets::ALBEDO), 0u, 0u, 0u);
    GFX::EnqueueCommand(bufferInOut, blitScreenColourCmd);

    RenderTarget& prevScreenRT = _context.renderTargetPool().renderTarget(RenderTargetUsage::SCREEN_PREV);
    GFX::ComputeMipMapsCommand computeMipMapsCommand{};
    computeMipMapsCommand._texture = prevScreenRT.getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::ALBEDO)).texture().get();
    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

    { // ToneMap and generate LDR render target (Alpha channel contains pre-toneMapped luminance value)
        static size_t lumaSamplerHash = 0u;
        if (lumaSamplerHash == 0u) {
            SamplerDescriptor lumaSampler = {};
            lumaSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
            lumaSampler.minFilter(TextureFilter::NEAREST);
            lumaSampler.magFilter(TextureFilter::NEAREST);
            lumaSampler.anisotropyLevel(0);
            lumaSamplerHash = lumaSampler.getHash();
        }

        const auto& screenAtt = getInput(true)._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
        const auto& screenDepthAtt = screenRT()._rt->getAttachment(RTAttachmentType::Depth, 0);

        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: tone map" });

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._textureData.add(TextureEntry{ screenAtt.texture()->data(),      screenAtt.samplerHash(),      TextureUsage::UNIT0 });
        set._textureData.add(TextureEntry{ _currentLuminance->data(),        lumaSamplerHash,              TextureUsage::UNIT1 });
        set._textureData.add(TextureEntry{ screenDepthAtt.texture()->data(), screenDepthAtt.samplerHash(), TextureUsage::DEPTH });

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_TONEMAP_PASS";
        renderPassCmd->_target = getOutput(false)._targetID;

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ adaptiveExposureControl() ? _pipelineToneMapAdaptive : _pipelineToneMap });

        const auto mappingFunction = to_base(_context.materialDebugFlag() == MaterialDebugFlag::COUNT ? _toneMapParams._function : ToneMapParams::MapFunctions::COUNT);
        _toneMapConstantsCmd._constants.set(_ID("useAdaptiveExposure"),  GFX::PushConstantType::BOOL, adaptiveExposureControl());
        _toneMapConstantsCmd._constants.set(_ID("manualExposureFactor"), GFX::PushConstantType::FLOAT, _toneMapParams._manualExposureFactor);
        _toneMapConstantsCmd._constants.set(_ID("mappingFunction"),      GFX::PushConstantType::INT, mappingFunction);
        _toneMapConstantsCmd._constants.set(_ID("skipToneMapping"),      GFX::PushConstantType::BOOL, _context.materialDebugFlag() != MaterialDebugFlag::COUNT);
        GFX::EnqueueCommand(bufferInOut, _toneMapConstantsCmd);

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
    }

    // Now that we have an LDR target, proceed with edge detection. This LDR target is NOT GAMMA CORRECTED!
    if (edgeDetectionMethod() != EdgeDetectionMethod::COUNT) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "PostFX: edge detection" });

        const auto& screenAtt = getInput(false)._rt->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));

        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ screenAtt.texture()->data(), screenAtt.samplerHash(),TextureUsage::UNIT0 });

        RTClearDescriptor clearTarget = {};
        clearTarget._clearColours = true;
        
        GFX::EnqueueCommand(bufferInOut, GFX::ClearRenderTargetCommand{ _sceneEdges._targetID, clearTarget });

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = _sceneEdges._targetID;
        renderPassCmd->_name = "DO_EDGE_DETECT_PASS";

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _edgeDetectionPipelines[to_base(edgeDetectionMethod())] });
        
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(_ID("dvd_edgeThreshold"), GFX::PushConstantType::FLOAT, edgeDetectionThreshold());

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
    
    // Execute all LDR based operators
    for (auto& op : ldrBatch) {
        if (BitCompare(filterStack, 1u << to_U32(op->operatorType()))) {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ Util::StringFormat("PostFX: Execute LDR operator [ %s ]", PostFX::FilterName(op->operatorType())).c_str() });
            if (op->execute(idx, cameraSnapshot, getInput(false), getOutput(false), bufferInOut)) {
                _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    // At this point, the last output should remain the general output. So the last swap was redundant
    _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
}

void PreRenderBatch::reshape(const U16 width, const U16 height) {
    for (OperatorBatch& batch : _operators) {
        for (auto& op : batch) {
            op->reshape(width, height);
        }
    }

    _screenRTs._hdr._screenCopy._rt->resize(width, height);
    _screenRTs._ldr._temp[0]._rt->resize(width, height);
    _screenRTs._ldr._temp[1]._rt->resize(width, height);
    _sceneEdges._rt->resize(width, height);
}
};