

#include "Headers/PostFX.h"
#include "Headers/PreRenderBatch.h"
#include "Headers/PreRenderOperator.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "Rendering/PostFX/CustomOperators/Headers/BloomPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/DoFPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSRPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/MotionBlurPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/PostAAPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSAOPreRenderOperator.h"

namespace Divide {

namespace
{
    //ToneMap ref: https://bruop.github.io/exposure/
    constexpr U8  GROUP_X_THREADS = 16u;
    constexpr U8  GROUP_Y_THREADS = 16u;
};

namespace TypeUtil
{
    const char* ToneMapFunctionsToString(const ToneMapParams::MapFunctions stop) noexcept
    {
        return Names::toneMapFunctions[to_base(stop)];
    }

    ToneMapParams::MapFunctions StringToToneMapFunctions(const string& name)
    {
        for (U8 i = 0; i < to_U8(ToneMapParams::MapFunctions::COUNT); ++i)
        {
            if (strcmp(name.c_str(), Names::toneMapFunctions[i]) == 0)
            {
                return static_cast<ToneMapParams::MapFunctions>(i);
            }
        }

        return ToneMapParams::MapFunctions::COUNT;
    }
}

PreRenderBatch::PreRenderBatch(GFXDevice& context, PostFX& parent)
    : _context(context)
    , _parent(parent)
{
    const auto& configParams = _context.context().config().rendering.postFX.toneMap;

    _lumaSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
    _lumaSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
    _lumaSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
    _lumaSampler._minFilter = TextureFilter::NEAREST;
    _lumaSampler._magFilter = TextureFilter::NEAREST;
    _lumaSampler._mipSampling = TextureMipSampling::NONE;
    _lumaSampler._anisotropyLevel = 0u;

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

    const SamplerDescriptor screenSampler
    {
        ._mipSampling = TextureMipSampling::NONE,
        ._wrapU = TextureWrap::CLAMP_TO_EDGE,
        ._wrapV = TextureWrap::CLAMP_TO_EDGE,
        ._wrapW = TextureWrap::CLAMP_TO_EDGE,
        ._anisotropyLevel = 0u,
    };

    RenderTargetDescriptor desc = {};
    desc._resolution = _screenRTs._hdr._screenRef._rt->getResolution();

    TextureDescriptor outputDescriptor = Get(_screenRTs._hdr._screenRef._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO)->texture())->descriptor();

    outputDescriptor._mipMappingState = MipMappingState::OFF;
    {
        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ outputDescriptor, screenSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0}
        };
        desc._name = "PostFX Output HDR";

        _screenRTs._hdr._screenCopy = _context.renderTargetPool().allocateRT(desc);
    }
    {
        //Colour0 holds the LDR screen texture
        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ outputDescriptor, screenSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "PostFX Output LDR 0";

        _screenRTs._ldr._temp[0] = _context.renderTargetPool().allocateRT(desc);

        desc._name = "PostFX Output LDR 1";
        _screenRTs._ldr._temp[1] = _context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor edgeDescriptor{};
        edgeDescriptor._dataType = GFXDataFormat::FLOAT_16;
        edgeDescriptor._baseFormat = GFXImageFormat::RG;
        edgeDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        edgeDescriptor._mipMappingState = MipMappingState::OFF;

        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ edgeDescriptor, screenSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "SceneEdges";
        _sceneEdges = _context.renderTargetPool().allocateRT(desc);
    }
    {
        // Could be FLOAT_16 but due to host-readback, keeping it as 32 makes it a bit easier to manage
        ResourceDescriptor<Texture> texture("Luminance Texture");
        texture.waitForReady(true);

        TextureDescriptor& lumaDescriptor = texture._propertyDescriptor;
        lumaDescriptor._dataType = GFXDataFormat::FLOAT_32;
        lumaDescriptor._baseFormat = GFXImageFormat::RED;
        lumaDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        lumaDescriptor._mipMappingState = MipMappingState::OFF;
        AddImageUsageFlag( lumaDescriptor, ImageUsage::SHADER_READ);

        _currentLuminance = CreateResource(texture);

        F32 val = 1.f;
        Get(_currentLuminance)->createWithData((Byte*)&val, 1u * sizeof(F32), vec2<U16>(1u), {});
    }
    {
        const SamplerDescriptor defaultSampler
        {
            ._minFilter = TextureFilter::NEAREST,
            ._magFilter = TextureFilter::NEAREST,
            ._mipSampling = TextureMipSampling::NONE,
            ._wrapU = TextureWrap::CLAMP_TO_EDGE,
            ._wrapV = TextureWrap::CLAMP_TO_EDGE,
            ._wrapW = TextureWrap::CLAMP_TO_EDGE,
            ._anisotropyLevel = 0u
        };

        TextureDescriptor linearDepthDescriptor{};
        linearDepthDescriptor._dataType = GFXDataFormat::FLOAT_16;
        linearDepthDescriptor._baseFormat = GFXImageFormat::RED;
        linearDepthDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        linearDepthDescriptor._mipMappingState = MipMappingState::OFF;

        desc._attachments =
        {
            InternalRTAttachmentDescriptor{ linearDepthDescriptor, defaultSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
        };

        desc._name = "Linear Depth";
        desc._msaaSamples = 0u;
        _linearDepthRT = _context.renderTargetPool().allocateRT(desc);
    }

    for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i)
    {
        const FilterType fType = static_cast<FilterType>(i);
        if (GetOperatorSpace(fType) == FilterSpace::FILTER_SPACE_POST_FX)
        {
            // These should be handled in PostFX code
            continue;
        }

        OperatorBatch& batch = _operators[to_base(GetOperatorSpace(fType))];

        switch (fType)
        {
            // PrePass
            case FilterType::FILTER_SS_AMBIENT_OCCLUSION:
                batch.emplace_back(std::make_unique<SSAOPreRenderOperator>(_context, *this, loadTasks));
                break;
            case FilterType::FILTER_SS_REFLECTIONS:
                batch.emplace_back(std::make_unique<SSRPreRenderOperator>(_context, *this, loadTasks));
                break;

            // HDR
            case FilterType::FILTER_DEPTH_OF_FIELD:
                batch.emplace_back(std::make_unique<DoFPreRenderOperator>(_context, *this, loadTasks));
                break;
            case FilterType::FILTER_MOTION_BLUR:
                batch.emplace_back(std::make_unique<MotionBlurPreRenderOperator>(_context, *this, loadTasks));
                break;
            case FilterType::FILTER_BLOOM:
                batch.emplace_back(std::make_unique<BloomPreRenderOperator>(_context, *this, loadTasks));
                break;

            // LDR
            case FilterType::FILTER_SS_ANTIALIASING:
                batch.emplace_back(std::make_unique<PostAAPreRenderOperator>(_context, *this, loadTasks));
                break;

            default:
                DIVIDE_UNEXPECTED_CALL();
                break;
        }
    }

    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        for (U8 i = 0; i < to_base(ToneMapParams::MapFunctions::COUNT) + 1; ++i)
        {
            fragModule._defines.emplace_back(
                Util::StringFormat("{} {}",
                                   TypeUtil::ToneMapFunctionsToString(static_cast<ToneMapParams::MapFunctions>(i)),
                                   i));
        }
        fragModule._sourceFile = "toneMap.glsl";

        ResourceDescriptor<ShaderProgram> toneMap("toneMap");
        toneMap._propertyDescriptor._modules.push_back(vertModule);
        toneMap._propertyDescriptor._modules.push_back(fragModule);
        toneMap._propertyDescriptor._globalDefines.emplace_back( "manualExposureFactor PushData0[0].x" );
        toneMap._propertyDescriptor._globalDefines.emplace_back( "mappingFunction int(PushData0[0].y)" );
        toneMap._propertyDescriptor._globalDefines.emplace_back( "useAdaptiveExposure uint(PushData0[0].z)" );
        toneMap._propertyDescriptor._globalDefines.emplace_back( "skipToneMapping uint(PushData0[0].w)" );
        toneMap._propertyDescriptor._globalDefines.emplace_back( "bloomStrength PushData0[1].x" );
        toneMap.waitForReady(false);
        _toneMap = CreateResource( toneMap, loadTasks);
    }
    {
        ShaderModuleDescriptor computeModule = {};
        computeModule._moduleType = ShaderType::COMPUTE;
        computeModule._sourceFile = "luminanceCalc.glsl";
        computeModule._defines.emplace_back(Util::StringFormat("GROUP_SIZE {}", GROUP_X_THREADS * GROUP_Y_THREADS));
        computeModule._defines.emplace_back(Util::StringFormat("THREADS_X {}", GROUP_X_THREADS));
        computeModule._defines.emplace_back(Util::StringFormat("THREADS_Y {}", GROUP_Y_THREADS));
        {
            computeModule._variant = "Create";


            ResourceDescriptor<ShaderProgram> histogramCreate("luminanceCalc.HistogramCreate");
            histogramCreate.waitForReady(false);
            histogramCreate._propertyDescriptor._modules.push_back( computeModule );
            histogramCreate._propertyDescriptor._globalDefines.emplace_back( "u_params PushData0[0]" );
            _createHistogram = CreateResource(histogramCreate, loadTasks);
        }
        {
            computeModule._variant = "Average";

            ResourceDescriptor<ShaderProgram> histogramAverage("luminanceCalc.HistogramAverage");
            histogramAverage.waitForReady(false);
            histogramAverage._propertyDescriptor._modules.push_back( computeModule );
            histogramAverage._propertyDescriptor._globalDefines.emplace_back( "dvd_minLogLum PushData0[0].x" );
            histogramAverage._propertyDescriptor._globalDefines.emplace_back( "dvd_logLumRange PushData0[0].y" );
            histogramAverage._propertyDescriptor._globalDefines.emplace_back( "dvd_timeCoeff PushData0[0].z" );
            histogramAverage._propertyDescriptor._globalDefines.emplace_back( "dvd_numPixels PushData0[0].w" );
            _averageHistogram = CreateResource(histogramAverage, loadTasks);
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

            ResourceDescriptor<ShaderProgram> edgeDetectionDepth("edgeDetection.Depth", edgeDetectionDescriptor );
            edgeDetectionDepth.waitForReady(false);

            _edgeDetection[to_base(EdgeDetectionMethod::Depth)] = CreateResource(edgeDetectionDepth, loadTasks);
        }
        {
            fragModule._variant = "Luma";
            edgeDetectionDescriptor._modules = { vertModule, fragModule };

            ResourceDescriptor<ShaderProgram> edgeDetectionLuma("edgeDetection.Luma", edgeDetectionDescriptor );
            edgeDetectionLuma.waitForReady(false);
            _edgeDetection[to_base(EdgeDetectionMethod::Luma)] = CreateResource(edgeDetectionLuma, loadTasks);
        }
        {
            fragModule._variant = "Colour";
            edgeDetectionDescriptor._modules = { vertModule, fragModule };

            ResourceDescriptor<ShaderProgram> edgeDetectionColour("edgeDetection.Colour", edgeDetectionDescriptor );
            edgeDetectionColour.waitForReady(false);
            _edgeDetection[to_base(EdgeDetectionMethod::Colour)] = CreateResource(edgeDetectionColour, loadTasks);
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

        ResourceDescriptor<ShaderProgram> lineariseDepthBuffer("lineariseDepthBuffer", lineariseDepthBufferDescriptor );
        lineariseDepthBuffer.waitForReady(false);
        _lineariseDepthBuffer = CreateResource(lineariseDepthBuffer, loadTasks);
    }

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._name = "LUMINANCE_HISTOGRAM_BUFFER";
    bufferDescriptor._ringBufferLength = 0;
    bufferDescriptor._elementCount = 256;
    bufferDescriptor._elementSize = sizeof(U32);
    bufferDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
    bufferDescriptor._updateFrequency = BufferUpdateFrequency::ONCE;

    _histogramBuffer = _context.newShaderBuffer(bufferDescriptor);

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
    DIVIDE_ASSERT(operatorsReady()); // The previous loadTasks atomic should account for all threaded loads. If not, fix it.

    PipelineDescriptor pipelineDescriptor{};
    pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;

    pipelineDescriptor._stateBlock = _context.get2DStateBlock();
    pipelineDescriptor._shaderProgramHandle = _createHistogram;

    _pipelineLumCalcHistogram = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._shaderProgramHandle = _averageHistogram;
    _pipelineLumCalcAverage = _context.newPipeline(pipelineDescriptor);

    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    pipelineDescriptor._shaderProgramHandle = _toneMap;
    _pipelineToneMap = _context.newPipeline(pipelineDescriptor);

    for (U8 i = 0u; i < to_U8(EdgeDetectionMethod::COUNT); ++i)
    {
        pipelineDescriptor._shaderProgramHandle = _edgeDetection[i];
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

    DestroyResource(_currentLuminance);
    DestroyResource(_toneMap);
    DestroyResource(_createHistogram);
    DestroyResource(_averageHistogram);
    DestroyResource(_lineariseDepthBuffer);

    for (auto& shader : _edgeDetection)
    {
        DestroyResource(shader);
    }
}

bool PreRenderBatch::operatorsReady() const noexcept
{
    for (const OperatorBatch& batch : _operators)
    {
        for (const auto& op : batch)
        {
            if (!op->ready())
            {
                return false;
            }
        }
    }

    return true;
}

PreRenderOperator* PreRenderBatch::getOperator(const FilterType type) const
{
    const FilterSpace fSpace = GetOperatorSpace(type);
    if (fSpace == FilterSpace::COUNT)
    {
        return nullptr;
    }

    const OperatorBatch& batch = _operators[to_U32(fSpace)];
    const auto* const it = std::find_if(std::cbegin(batch), 
                                        std::cend(batch),
                                        [type](const auto& op) noexcept 
                                        {
                                            return op->operatorType() == type;
                                        });

    assert(it != std::cend(batch));
    return (*it).get();
}


void PreRenderBatch::adaptiveExposureControl(const bool state) noexcept
{
    _adaptiveExposureControl = state;
    _context.context().config().rendering.postFX.toneMap.adaptive = state;
    _context.context().config().changed(true);
}

F32 PreRenderBatch::adaptiveExposureValue() const noexcept
{
    _adaptiveExposureValueNeedsUpdate = adaptiveExposureControl();
    return _adaptiveExposureValue;
}

void PreRenderBatch::toneMapParams(const ToneMapParams params) noexcept
{
    _toneMapParams = params;

    auto& configParams = _context.context().config().rendering.postFX.toneMap;
    configParams.manualExposureFactor = _toneMapParams._manualExposureFactor;
    configParams.maxLogLuminance = _toneMapParams._maxLogLuminance;
    configParams.minLogLuminance = _toneMapParams._minLogLuminance;
    configParams.tau = _toneMapParams._tau;
    configParams.mappingFunction = TypeUtil::ToneMapFunctionsToString(_toneMapParams._function);
    _context.context().config().changed(true);
}

void PreRenderBatch::update(const U64 deltaTimeUS) noexcept
{
    _lastDeltaTimeUS = deltaTimeUS;
}

void PreRenderBatch::onFilterToggle(const FilterType filter, const bool state)
{
    for (OperatorBatch& batch : _operators)
    {
        for (auto& op : batch)
        {
            if (filter == op->operatorType())
            {
                op->onToggle(state);
            }
        }
    }
}

void PreRenderBatch::prePass(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const U32 filterStack, GFX::CommandBuffer& bufferInOut)
{
    for (OperatorBatch& batch : _operators)
    {
        for (auto& op : batch)
        {
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
        pipelineDescriptor._stateBlock = _context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _lineariseDepthBuffer;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        GFX::BindPipelineCommand bindPipelineCmd{};
        bindPipelineCmd._pipeline = _context.newPipeline(pipelineDescriptor);

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Linearise depth buffer";
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);
        GFX::EnqueueCommand(bufferInOut, bindPipelineCmd);

        RTAttachment* depthAtt = screenRT()._rt->getAttachment(RTAttachmentType::DEPTH);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, depthAtt->texture(), depthAtt->_descriptor._sampler );

        PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        pushData.data[0]._vec[0].xy = cameraSnapshot._zPlanes;

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    const RenderTargetHandle prevScreenHandle
    {
        _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN_PREV),
        RenderTargetNames::SCREEN_PREV
    };

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Execute PrePass operators";

    for (auto& op : _operators[to_base(FilterSpace::FILTER_SPACE_PRE_PASS)])
    {
        if (filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = PostFX::FilterName(op->operatorType());
            {
                const bool swapTargets = op->execute(idx, cameraSnapshot, prevScreenHandle, getOutput(true), bufferInOut);

                DIVIDE_ASSERT(!swapTargets, "PreRenderBatch::prePass: Swap render target request detected during prePass!");
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    // Always bind these even if we haven't ran the appropriate operators!
    RTAttachment* ssrDataAtt = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SSR_RESULT)->getAttachment(RTAttachmentType::COLOUR);
    RTAttachment* ssaoDataAtt = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SSAO_RESULT)->getAttachment(RTAttachmentType::COLOUR);

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_PASS;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 3u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, ssrDataAtt->texture(), ssrDataAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 4u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, ssaoDataAtt->texture(), ssaoDataAtt->_descriptor._sampler );
    } 
}

void PreRenderBatch::execute(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, U32 filterStack, GFX::CommandBuffer& bufferInOut)
{
    _screenRTs._swappedHDR = _screenRTs._swappedLDR = false;
    _toneMapParams._width = screenRT()._rt->getWidth();
    _toneMapParams._height = screenRT()._rt->getHeight();
    const F32 logLumRange = _toneMapParams._maxLogLuminance - _toneMapParams._minLogLuminance;
    const Handle<Texture> screenColour = screenRT()._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO)->texture();

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Compute Adaptive Exposure";
    { // Histogram Pass
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Create Luminance Histogram";

        const ImageView screenImage = Get(screenColour)->getView();

        // ToDo: This can be changed to a simple sampler instead, thus avoiding this layout change
        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView = screenImage,
            ._sourceLayout = ImageUsage::SHADER_READ,
            ._targetLayout = ImageUsage::SHADER_READ_WRITE
        });

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _pipelineLumCalcHistogram;

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
            Set(binding._data, screenImage, ImageUsage::SHADER_READ_WRITE);
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 13u, ShaderStageVisibility::COMPUTE );
            Set(binding._data, _histogramBuffer.get());
        }

        PushConstantsStruct& params = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        params.data[0]._vec[0].set( _toneMapParams._minLogLuminance,
                                    1.f / logLumRange,
                                    to_F32( _toneMapParams._width ),
                                    to_F32( _toneMapParams._height ) );

        const U32 groupsX = to_U32(std::ceil(_toneMapParams._width / to_F32(GROUP_X_THREADS)));
        const U32 groupsY = to_U32(std::ceil(_toneMapParams._height / to_F32(GROUP_Y_THREADS)));
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(bufferInOut)->_workGroupSize = {groupsX, groupsY, 1};

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

    const ImageView luminanceView = Get( _currentLuminance )->getView();

    { // Averaging pass

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Average Luminance Histogram";

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut)->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView   = luminanceView,
            ._sourceLayout = ImageUsage::SHADER_READ,
            ._targetLayout = ImageUsage::SHADER_WRITE,
        });

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _pipelineLumCalcAverage;

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::COMPUTE );
            Set(binding._data, _histogramBuffer.get(), { 0u, _histogramBuffer->getPrimitiveCount() });
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
            Set(binding._data, luminanceView, ImageUsage::SHADER_WRITE );
        }


        PushConstantsStruct& params = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        params.data[0]._vec[0].set(_toneMapParams._minLogLuminance,
                                    logLumRange,
                                    CLAMPED_01( 1.0f - std::exp( -Time::MicrosecondsToSeconds<F32>( _lastDeltaTimeUS ) * _toneMapParams._tau ) ),
                                    to_F32( _toneMapParams._width ) * _toneMapParams._height );


        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(bufferInOut)->_workGroupSize = { 1, 1, 1, };
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
                ._targetView   = luminanceView,
                ._sourceLayout = ImageUsage::SHADER_WRITE,
                ._targetLayout = ImageUsage::SHADER_READ,
            });
        }

        if ( _adaptiveExposureValueNeedsUpdate )
        {
            _adaptiveExposureValueNeedsUpdate = false;

            auto readTextureCmd = GFX::EnqueueCommand<GFX::ReadTextureCommand>(bufferInOut);
            readTextureCmd->_texture = _currentLuminance;
            readTextureCmd->_pixelPackAlignment._alignment = 1u;
            readTextureCmd->_callback = [&](const ImageReadbackData& data)
            {
                if ( !data._data.empty() )
                {
                    const Byte* imageData = data._data.data();
                    _adaptiveExposureValue = *reinterpret_cast<const F32*>(imageData);
                }
            };

        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Execute HDR operators";
    for (auto& op : _operators[to_base(FilterSpace::FILTER_SPACE_HDR)])
    {
        if (filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = PostFX::FilterName(op->operatorType());
            if (op->execute(idx, cameraSnapshot, getInput(true), getOutput(true), bufferInOut))
            {
                _screenRTs._swappedHDR = !_screenRTs._swappedHDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    RenderTarget* prevScreenRT = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN_PREV);
    Handle<Texture> prevScreenTex = prevScreenRT->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO )->texture();

    // Copy our screen target PRE tonemap to feed back to PostFX operators in the next frame
    GFX::BlitRenderTargetCommand blitScreenColourCmd = {};
    blitScreenColourCmd._source = getInput(true)._targetID;
    blitScreenColourCmd._destination = RenderTargetNames::SCREEN_PREV;
    blitScreenColourCmd._params.emplace_back(RTBlitEntry
    {
        ._input = 
        {
            ._index = to_base( GFXDevice::ScreenTargets::ALBEDO )
        },
        ._output =
        {
            ._index = to_base( RTColourAttachmentSlot::SLOT_0 )
        }
    });

    GFX::EnqueueCommand(bufferInOut, blitScreenColourCmd);

    GFX::ComputeMipMapsCommand computeMipMapsCommand{};
    computeMipMapsCommand._texture = prevScreenTex;
    computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

    const bool bloomEnabled = _context.context().config().rendering.postFX.bloom.enabled && _parent.getFilterState(FilterType::FILTER_BLOOM);

    { // ToneMap and generate LDR render target (Alpha channel contains pre-toneMapped luminance value)
        const auto& screenAtt = getInput(true)._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
        const auto& bloomDataAtt = _context.renderTargetPool().getRenderTarget(RenderTargetNames::BLOOM_RESULT)->getAttachment(RTAttachmentType::COLOUR);

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: tone map";

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_TONEMAP_PASS";
        renderPassCmd->_target = getOutput(false)._targetID;
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _pipelineToneMap;

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, luminanceView, lumaSampler() );
        }
        {
            DescriptorSetBinding& binding = AddBinding(cmd->_set, 2u, ShaderStageVisibility::FRAGMENT);
            if (bloomEnabled)
            {
                Set(binding._data, bloomDataAtt->texture(), bloomDataAtt->_descriptor._sampler);
            }
            else
            {
                Set(binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler);
            }
        }

        const auto mappingFunction = to_base(_context.materialDebugFlag() == MaterialDebugFlag::COUNT ? _toneMapParams._function : ToneMapParams::MapFunctions::COUNT);

        PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        pushData.data[0]._vec[0].set( _toneMapParams._manualExposureFactor,
                                     to_F32( mappingFunction ),
                                     adaptiveExposureControl() ? 1.f : 0.f,
                                     _context.materialDebugFlag() != MaterialDebugFlag::COUNT ? 1.f : 0.f);

        if (_context.context().config().rendering.postFX.bloom.enabled)
        {
            pushData.data[0]._vec[1].x = _context.context().config().rendering.postFX.bloom.strength;
        }
        else
        {
            pushData.data[0]._vec[1].x = 0.f;
        }
        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
    }

    // Now that we have an LDR target, proceed with edge detection. This LDR target is NOT GAMMA CORRECTED!
    if (edgeDetectionMethod() != EdgeDetectionMethod::COUNT)
    {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: edge detection";

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        if (edgeDetectionMethod() != EdgeDetectionMethod::Depth)
        {
            const auto& screenAtt = getInput(false)._rt->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);

            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, screenAtt->texture(), screenAtt->_descriptor._sampler );

        }
        else
        {
            const auto& depthAtt = getInput(false)._rt->getAttachment(RTAttachmentType::DEPTH);

            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, depthAtt->texture(), depthAtt->_descriptor._sampler );
        }

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = _sceneEdges._targetID;
        renderPassCmd->_name = "DO_EDGE_DETECT_PASS";
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true };
        renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = _edgeDetectionPipelines[to_base(edgeDetectionMethod())];

        PushConstantsStruct& pushData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
        pushData.data[0]._vec[0].x = edgeDetectionThreshold();

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut)->_drawCommands.emplace_back();

        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
    
    // Execute all LDR based operators
    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "PostFX: Execute LDR operators";
    for (auto& op : _operators[to_base(FilterSpace::FILTER_SPACE_LDR)])
    {
        if ( filterStack & 1u << to_U32(op->operatorType()))
        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = PostFX::FilterName(op->operatorType());

            if (op->execute(idx, cameraSnapshot, getInput(false), getOutput(false), bufferInOut))
            {
                _screenRTs._swappedLDR = !_screenRTs._swappedLDR;
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

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

} //namespace Divide
