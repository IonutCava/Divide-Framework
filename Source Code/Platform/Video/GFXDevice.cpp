#include "stdafx.h"

#include "config.h"

#include "Headers/GFXDevice.h"

#include "Editor/Headers/Editor.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"

#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Material/Headers/ShaderComputeQueue.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Platform/Video/RenderBackend/None/Headers/NoneWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Platform/Video/RenderBackend/OpenGL/Buffers/PixelBuffer/Headers/glPixelBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/RenderTarget/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/VertexBuffer/Headers/glGenericVertexData.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"

#include "Headers/CommandBufferPool.h"

namespace Divide {

namespace TypeUtil {
    const char* GraphicResourceTypeToName(const GraphicsResource::Type type) noexcept {
        return Names::resourceTypes[to_base(type)];
    };

    const char* RenderStageToString(const RenderStage stage) noexcept {
        return Names::renderStage[to_base(stage)];
    }

    RenderStage StringToRenderStage(const char* stage) noexcept {
        for (U8 i = 0; i < to_U8(RenderStage::COUNT); ++i) {
            if (strcmp(stage, Names::renderStage[i]) == 0) {
                return static_cast<RenderStage>(i);
            }
        }

        return RenderStage::COUNT;
    }

    const char* RenderPassTypeToString(const RenderPassType pass) noexcept {
        return Names::renderPassType[to_base(pass)];
    }

    RenderPassType StringToRenderPassType(const char* pass) noexcept {
        for (U8 i = 0; i < to_U8(RenderPassType::COUNT); ++i) {
            if (strcmp(pass, Names::renderPassType[i]) == 0) {
                return static_cast<RenderPassType>(i);
            }
        }

        return RenderPassType::COUNT;
    }
};

namespace {
    /// How many writes we can basically issue per frame to our scratch buffers before we have to sync
    constexpr size_t TargetBufferSizeCam = 128u;
    constexpr size_t TargetBufferSizeRender = 8u;

    constexpr U32 GROUP_SIZE_AABB = 64;
    constexpr U32 MAX_INVOCATIONS_BLUR_SHADER_LAYERED = 4;
};

D64 GFXDevice::s_interpolationFactor = 1.0;
U32 GFXDevice::s_frameCount = 0u;

DeviceInformation GFXDevice::s_deviceInformation{};

#pragma region Construction, destruction, initialization
GFXDevice::GFXDevice(Kernel & parent)
    : KernelComponent(parent),
      PlatformContextComponent(parent.platformContext())
{
    _queuedShadowSampleChange.fill(s_invalidQueueSampleCount);
}

GFXDevice::~GFXDevice()
{
    closeRenderingAPI();
}

ErrorCode GFXDevice::createAPIInstance(const RenderAPI API) {
    assert(_api == nullptr && "GFXDevice error: initRenderingAPI called twice!");

    ErrorCode err = ErrorCode::NO_ERR;
    switch (API) {
        case RenderAPI::OpenGL:{
            _api = eastl::make_unique<GL_API>(*this);
        } break;
        case RenderAPI::Vulkan: {
            _api = eastl::make_unique<VK_API>(*this);
        } break;
        case RenderAPI::None: {
            _api = eastl::make_unique<NONE_API>(*this);
        } break;
        default:
            err = ErrorCode::GFX_NON_SPECIFIED;
            break;
    };

    DIVIDE_ASSERT(_api != nullptr, Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
    renderAPI(API);

    return err;
}

/// Create a display context using the selected API and create all of the needed
/// primitives needed for frame rendering
ErrorCode GFXDevice::initRenderingAPI(const I32 argc, char** argv, const RenderAPI API) {
    ErrorCode hardwareState = createAPIInstance(API);
    Configuration& config = _parent.platformContext().config();

    if (hardwareState == ErrorCode::NO_ERR) {
        // Initialize the rendering API
        hardwareState = _api->initRenderingAPI(argc, argv, config);
    }

    if (hardwareState != ErrorCode::NO_ERR) {
        // Validate initialization
        return hardwareState;
    }

    string refreshRates;
    const size_t displayCount = gpuState().getDisplayCount();
    for (size_t idx = 0; idx < displayCount; ++idx) {
        const vector<GPUState::GPUVideoMode>& registeredModes = gpuState().getDisplayModes(idx);
        if (!registeredModes.empty()) {
            Console::printfn(Locale::Get(_ID("AVAILABLE_VIDEO_MODES")), idx, registeredModes.size());

            for (const GPUState::GPUVideoMode& mode : registeredModes) {
                // Optionally, output to console/file each display mode
                refreshRates = Util::StringFormat("%d", mode._refreshRate);
                Console::printfn(Locale::Get(_ID("CURRENT_DISPLAY_MODE")),
                    mode._resolution.width,
                    mode._resolution.height,
                    mode._bitDepth,
                    mode._formatName.c_str(),
                    refreshRates.c_str());
            }
        }
    }

    _rtPool = MemoryManager_NEW GFXRTPool(*this);

    I32 numLightsPerCluster = config.rendering.numLightsPerCluster;
    if (numLightsPerCluster < 0) {
        numLightsPerCluster = to_I32(Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER);
    }
    else {
        numLightsPerCluster = std::min(numLightsPerCluster, to_I32(Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER));
    }
    if (numLightsPerCluster != config.rendering.numLightsPerCluster) {
        config.rendering.numLightsPerCluster = numLightsPerCluster;
        config.changed(true);
    }
    const U16 reflectionProbeRes = to_U16(nextPOW2(CLAMPED(to_U32(config.rendering.reflectionProbeResolution), 16u, 4096u) - 1u));
    if (reflectionProbeRes != config.rendering.reflectionProbeResolution) {
        config.rendering.reflectionProbeResolution = reflectionProbeRes;
        config.changed(true);
    }

    return ShaderProgram::OnStartup(parent().resourceCache());
}

void GFXDevice::resizeGPUBlocks(size_t targetSizeCam, size_t targetSizeRender, size_t targetSizeCullCounter) {
    if (targetSizeCam == 0u) { targetSizeCam = 1u; }
    if (targetSizeRender == 0u) { targetSizeRender = 1u; }
    if (targetSizeCullCounter == 0u) { targetSizeCullCounter = 1u; }

    const bool resizeCamBuffer = _gfxBuffers._currentSizeCam != targetSizeCam;
    const bool resizeRenderBuffer = _gfxBuffers._currentSizeRender != targetSizeRender;
    const bool resizeCullCounter = _gfxBuffers._currentSizeCullCounter != targetSizeCullCounter;

    if (!resizeCamBuffer && !resizeRenderBuffer && !resizeCullCounter) {
        return;
    }

    DIVIDE_ASSERT(ValidateGPUDataStructure());

    _gfxBuffers.reset(resizeCamBuffer, resizeRenderBuffer, resizeCullCounter);
    _gfxBuffers._currentSizeCam = targetSizeCam;
    _gfxBuffers._currentSizeRender = targetSizeRender;
    _gfxBuffers._currentSizeCullCounter = targetSizeCullCounter;

    if (resizeCamBuffer || resizeRenderBuffer) {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;

        if (resizeCamBuffer) {
            bufferDescriptor._ringBufferLength = to_U32(targetSizeCam);
            bufferDescriptor._bufferParams._elementSize = sizeof(GFXShaderData::CamData);
            bufferDescriptor._bufferParams._initialData = { (Byte*)&_gpuBlock._camData, bufferDescriptor._bufferParams._elementSize };

            for (U8 i = 0u; i < GFXBuffers::PerFrameBufferCount; ++i) {
                bufferDescriptor._name = Util::StringFormat("DVD_GPU_CAM_DATA_%d", i);
                _gfxBuffers._perFrameBuffers[i]._camDataBuffer = newSB(bufferDescriptor);
            }
        }
        if (resizeRenderBuffer) {
            bufferDescriptor._ringBufferLength = to_U32(targetSizeRender);
            bufferDescriptor._bufferParams._elementSize = sizeof(GFXShaderData::RenderData);
            bufferDescriptor._bufferParams._initialData = { (Byte*)&_gpuBlock._renderData, bufferDescriptor._bufferParams._elementSize };

            for (U8 i = 0u; i < GFXBuffers::PerFrameBufferCount; ++i) {
                bufferDescriptor._name = Util::StringFormat("DVD_GPU_RENDER_DATA_%d", i);
                _gfxBuffers._perFrameBuffers[i]._renderDataBuffer = newSB(bufferDescriptor);
            }
        }
    }

    if (resizeCullCounter) {
        // Atomic counter for occlusion culling
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._usage = ShaderBuffer::Usage::ATOMIC_COUNTER;
        bufferDescriptor._ringBufferLength = to_U32(targetSizeCullCounter);
        bufferDescriptor._name = "CULL_COUNTER";
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_W_CPU_R;
        bufferDescriptor._separateReadWrite = true;
        bufferDescriptor._bufferParams._initialData = {};
        for (U8 i = 0u; i < GFXBuffers::PerFrameBufferCount; ++i) {
            _gfxBuffers._perFrameBuffers[i]._cullCounter = newSB(bufferDescriptor);
        }
    }
}

ErrorCode GFXDevice::postInitRenderingAPI(const vec2<U16> & renderResolution) {
    std::atomic_uint loadTasks = 0;
    ResourceCache* cache = parent().resourceCache();
    const Configuration& config = _parent.platformContext().config();

    Texture::OnStartup(*this);
    RenderPassExecutor::OnStartup(*this);
    GFX::InitPools();

    resizeGPUBlocks(TargetBufferSizeCam, TargetBufferSizeRender, RenderPass::DataBufferRingSize);

    _shaderComputeQueue = MemoryManager_NEW ShaderComputeQueue(cache);

    // Create general purpose render state blocks
    RenderStateBlock defaultStateNoDepth;
    defaultStateNoDepth.depthTestEnabled(false);
    _defaultStateNoDepthHash = defaultStateNoDepth.getHash();

    RenderStateBlock state2DRendering;
    state2DRendering.setCullMode(CullMode::NONE);
    state2DRendering.depthTestEnabled(false);
    _state2DRenderingHash = state2DRendering.getHash();

    RenderStateBlock stateDepthOnlyRendering;
    stateDepthOnlyRendering.setColourWrites(false, false, false, false);
    stateDepthOnlyRendering.setZFunc(ComparisonFunction::ALWAYS);
    _stateDepthOnlyRenderingHash = stateDepthOnlyRendering.getHash();

    // The general purpose render state blocks are both mandatory and must different from each other at a state hash level
    assert(_stateDepthOnlyRenderingHash != _state2DRenderingHash && "GFXDevice error: Invalid default state hash detected!");
    assert(_state2DRenderingHash != _defaultStateNoDepthHash && "GFXDevice error: Invalid default state hash detected!");
    assert(_defaultStateNoDepthHash != RenderStateBlock::DefaultHash() && "GFXDevice error: Invalid default state hash detected!");

    // We need to create all of our attachments for the default render targets
    // Start with the screen render target: Try a half float, multisampled
    // buffer (MSAA + HDR rendering if possible)

    SamplerDescriptor defaultSampler = {};
    defaultSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    defaultSampler.minFilter(TextureFilter::NEAREST);
    defaultSampler.magFilter(TextureFilter::NEAREST);
    defaultSampler.anisotropyLevel(0);
    const size_t samplerHash = defaultSampler.getHash();

    SamplerDescriptor defaultSamplerMips = {};
    defaultSamplerMips.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    defaultSamplerMips.minFilter(TextureFilter::LINEAR_MIPMAP_LINEAR);
    defaultSamplerMips.magFilter(TextureFilter::LINEAR);
    defaultSamplerMips.anisotropyLevel(0);
    const size_t samplerHashMips = defaultSamplerMips.getHash();

    //PrePass
    TextureDescriptor depthDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::FLOAT_32);
    TextureDescriptor velocityDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
    //RG - packed normal, B - roughness
    TextureDescriptor normalsDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
    depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    velocityDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    normalsDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    //MainPass
    TextureDescriptor screenDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
    screenDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
    TextureDescriptor materialDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RG, GFXDataFormat::FLOAT_16);
    materialDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    // Normal, Previous and MSAA
    {
        const TextureDescriptor::MipMappingState mipMapState[] = {
            TextureDescriptor::MipMappingState::OFF,
            TextureDescriptor::MipMappingState::MANUAL,
            TextureDescriptor::MipMappingState::OFF
        };
        const RenderTargetUsage rtUsage[] = {
            RenderTargetUsage::SCREEN,
            RenderTargetUsage::SCREEN_PREV,
            RenderTargetUsage::SCREEN_MS
        };
        const U8 sampleCount[] = { 0u, 0u, config.rendering.MSAASamples };
        const size_t screenSampler[] = { samplerHash, samplerHashMips, samplerHash };
        const Str64 targetName[] = { "Screen", "Screen Prev", "Screen MS" };

        for (U8 i = 0; i < 3; ++i) {
            screenDescriptor.mipMappingState(mipMapState[i]);

            screenDescriptor.msaaSamples(sampleCount[i]);
            depthDescriptor.msaaSamples(sampleCount[i]);
            normalsDescriptor.msaaSamples(sampleCount[i]);
            materialDescriptor.msaaSamples(sampleCount[i]);
            velocityDescriptor.msaaSamples(sampleCount[i]);

            {

                RTAttachmentDescriptors attachments = {
                    { screenDescriptor,   screenSampler[i], RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO),   DefaultColours::DIVIDE_BLUE},
                    { velocityDescriptor, samplerHash,      RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY), VECTOR4_ZERO },
                    { normalsDescriptor,  samplerHash,      RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS),  VECTOR4_ZERO },
                    { depthDescriptor,    samplerHash,      RTAttachmentType::Depth }
                };

                RenderTargetDescriptor screenDesc = {};
                screenDesc._name = targetName[i];
                screenDesc._resolution = renderResolution;

                //Don't need depth, and everything else for copies/resolve targets 
                screenDesc._attachmentCount = rtUsage[i] == RenderTargetUsage::SCREEN_PREV ? 1u : to_U8(attachments.size());
                screenDesc._attachments = attachments.data();
                screenDesc._msaaSamples = sampleCount[i];

                // Our default render targets hold the screen buffer, depth buffer, and a special, on demand, down-sampled version of the depth buffer
                _rtPool->allocateRT(rtUsage[i], screenDesc);
            }
        }
    }
    {
        TextureDescriptor ssaoDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RED, GFXDataFormat::FLOAT_16);
        ssaoDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors attachments = {
            { ssaoDescriptor, samplerHash, RTAttachmentType::Colour, 0u, VECTOR4_UNIT }
        };

        RenderTargetDescriptor ssaoDesc = {};
        ssaoDesc._name = "SSAO Result";
        ssaoDesc._resolution = renderResolution;
        ssaoDesc._attachmentCount = to_U8(attachments.size());
        ssaoDesc._attachments = attachments.data();
        ssaoDesc._msaaSamples = 0u;
        _rtPool->allocateRT(RenderTargetUsage::SSAO_RESULT, ssaoDesc);
    }
    {
        TextureDescriptor linearDepthDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RED, GFXDataFormat::FLOAT_16);
        linearDepthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors attachments = {
            { linearDepthDescriptor, samplerHash, RTAttachmentType::Colour, 0u, VECTOR4_ZERO }
        };

        RenderTargetDescriptor linearDepthDesc = {};
        linearDepthDesc._name = "Linear Depth";
        linearDepthDesc._resolution = renderResolution;
        linearDepthDesc._attachmentCount = to_U8(attachments.size());
        linearDepthDesc._attachments = attachments.data();
        linearDepthDesc._msaaSamples = 0u;
        _rtPool->allocateRT(RenderTargetUsage::LINEAR_DEPTH, linearDepthDesc);
    }
    {
        TextureDescriptor ssrDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
        ssrDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        
        RTAttachmentDescriptors attachments = {
            { ssrDescriptor, samplerHash, RTAttachmentType::Colour, 0u, VECTOR4_ZERO }
        };

        RenderTargetDescriptor ssrResultDesc = {};
        ssrResultDesc._name = "SSR Result";
        ssrResultDesc._resolution = renderResolution;
        ssrResultDesc._attachmentCount = to_U8(attachments.size());
        ssrResultDesc._attachments = attachments.data();
        ssrResultDesc._msaaSamples = 0u;
        _rtPool->allocateRT(RenderTargetUsage::SSR_RESULT, ssrResultDesc);
        
    }
    const U32 reflectRes = nextPOW2(CLAMPED(to_U32(config.rendering.reflectionPlaneResolution), 16u, 4096u) - 1u);

    TextureDescriptor hiZDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);
    hiZDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);

    SamplerDescriptor hiZSampler = {};
    hiZSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    hiZSampler.anisotropyLevel(0u);
    hiZSampler.magFilter(TextureFilter::NEAREST);
    hiZSampler.minFilter(TextureFilter::NEAREST_MIPMAP_NEAREST);

    RTAttachmentDescriptors hiZAttachments = {
        { hiZDescriptor, hiZSampler.getHash(), RTAttachmentType::Depth, 0, VECTOR4_UNIT },
    };

    {
        RenderTargetDescriptor hizRTDesc = {};
        hizRTDesc._name = "HiZ";
        hizRTDesc._resolution = renderResolution;
        hizRTDesc._attachmentCount = to_U8(hiZAttachments.size());
        hizRTDesc._attachments = hiZAttachments.data();
        _rtPool->allocateRT(RenderTargetUsage::HI_Z, hizRTDesc);

        hizRTDesc._resolution.set(reflectRes, reflectRes);
        hizRTDesc._name = "HiZ_Reflect";
        _rtPool->allocateRT(RenderTargetUsage::HI_Z_REFLECT, hizRTDesc);
    }

    if_constexpr(Config::Build::ENABLE_EDITOR) {
        SamplerDescriptor editorSampler = {};
        editorSampler.minFilter(TextureFilter::LINEAR_MIPMAP_LINEAR);
        editorSampler.magFilter(TextureFilter::LINEAR);
        editorSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        editorSampler.anisotropyLevel(0);

        TextureDescriptor editorDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE);
        editorDescriptor.layerCount(1u);
        editorDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors attachments = {
            { editorDescriptor, editorSampler.getHash(), RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO), DefaultColours::DIVIDE_BLUE }
        };

        RenderTargetDescriptor editorDesc = {};
        editorDesc._name = "Editor";
        editorDesc._resolution = renderResolution;
        editorDesc._attachmentCount = to_U8(attachments.size());
        editorDesc._attachments = attachments.data();
        _rtPool->allocateRT(RenderTargetUsage::EDITOR, editorDesc);
    }

    // Reflection Targets
    SamplerDescriptor reflectionSampler = {};
    reflectionSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    reflectionSampler.minFilter(TextureFilter::NEAREST);
    reflectionSampler.magFilter(TextureFilter::NEAREST);
    const size_t reflectionSamplerHash = reflectionSampler.getHash();

    {
        TextureDescriptor environmentDescriptorPlanar(TextureType::TEXTURE_2D, GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE);
        TextureDescriptor depthDescriptorPlanar(TextureType::TEXTURE_2D, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);

        environmentDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        depthDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RenderTargetDescriptor hizRTDesc = {};
        hizRTDesc._resolution.set(reflectRes, reflectRes);
        hizRTDesc._attachmentCount = to_U8(hiZAttachments.size());
        hizRTDesc._attachments = hiZAttachments.data();

        {
            RTAttachmentDescriptors attachments = {
                { environmentDescriptorPlanar, reflectionSamplerHash, RTAttachmentType::Colour },
                { depthDescriptorPlanar,       reflectionSamplerHash, RTAttachmentType::Depth },
            };

            RenderTargetDescriptor refDesc = {};
            refDesc._resolution = vec2<U16>(reflectRes);
            refDesc._attachmentCount = to_U8(attachments.size());
            refDesc._attachments = attachments.data();

            for (U32 i = 0; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i) {
                refDesc._name = Util::StringFormat("Reflection_Planar_%d", i);
                _rtPool->allocateRT(RenderTargetUsage::REFLECTION_PLANAR, refDesc);
            }

            for (U32 i = 0; i < Config::MAX_REFRACTIVE_NODES_IN_VIEW; ++i) {
                refDesc._name = Util::StringFormat("Refraction_Planar_%d", i);
                _rtPool->allocateRT(RenderTargetUsage::REFRACTION_PLANAR, refDesc);
            }

            environmentDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::OFF);
            RTAttachmentDescriptors attachmentsBlur = {//skip depth
                { environmentDescriptorPlanar, reflectionSamplerHash, RTAttachmentType::Colour }
            };

            refDesc._attachmentCount = to_U8(attachmentsBlur.size()); 
            refDesc._attachments = attachmentsBlur.data();
            refDesc._name = "Reflection_blur";
            _rtPool->allocateRT(RenderTargetUsage::REFLECTION_PLANAR_BLUR, refDesc);
        }
    }
    {
        SamplerDescriptor accumulationSampler = {};
        accumulationSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        accumulationSampler.minFilter(TextureFilter::NEAREST);
        accumulationSampler.magFilter(TextureFilter::NEAREST);
        const size_t accumulationSamplerHash = accumulationSampler.getHash();

        TextureDescriptor accumulationDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
        accumulationDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        //R = revealage
        TextureDescriptor revealageDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RED, GFXDataFormat::FLOAT_16);
        revealageDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors oitAttachments = {
            { accumulationDescriptor, accumulationSamplerHash, RTAttachmentType::Colour, to_U8(ScreenTargets::ACCUMULATION), VECTOR4_ZERO },
            { revealageDescriptor,    accumulationSamplerHash, RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE), vec4<F32>{1.f, 0.f, 0.f, 0.f} },
        };

        const RenderTargetUsage rtSource[] = {
            RenderTargetUsage::SCREEN,
            RenderTargetUsage::SCREEN_MS
        };
        const RenderTargetUsage rtTarget[] = {
            RenderTargetUsage::OIT,
            RenderTargetUsage::OIT_MS
        };
        const U8 sampleCount[] = { 0u, config.rendering.MSAASamples };
        const Str64 targetName[] = { "OIT", "OIT_MS" };


        for (U8 i = 0u; i < 2; ++i)
        {
            oitAttachments[0]._texDescriptor.msaaSamples(sampleCount[i]);
            oitAttachments[1]._texDescriptor.msaaSamples(sampleCount[i]);

            const RenderTarget& screenTarget = _rtPool->renderTarget(rtSource[i]);
            const RTAttachment_ptr& screenNormalsAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS));
            const RTAttachment_ptr& screenDepthAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Depth, 0);

            vector<ExternalRTAttachmentDescriptor> externalAttachments = {
                { screenNormalsAttachment,  RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS) },
                { screenDepthAttachment,    RTAttachmentType::Depth }
            };

            if_constexpr(Config::USE_COLOURED_WOIT) {
                const RTAttachment_ptr& screenAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO));
                externalAttachments.push_back(
                    { screenAttachment,  RTAttachmentType::Colour, to_U8(ScreenTargets::MODULATE) }
                );
            }

            RenderTargetDescriptor oitDesc = {};
            oitDesc._name = targetName[i];
            oitDesc._resolution = renderResolution;
            oitDesc._attachmentCount = to_U8(oitAttachments.size());
            oitDesc._attachments = oitAttachments.data();
            oitDesc._externalAttachmentCount = to_U8(externalAttachments.size());
            oitDesc._externalAttachments = externalAttachments.data();
            oitDesc._msaaSamples = sampleCount[i];
            _rtPool->allocateRT(rtTarget[i], oitDesc);
        }
        {
            oitAttachments[0]._texDescriptor.msaaSamples(0u);
            oitAttachments[1]._texDescriptor.msaaSamples(0u);

            for (U16 i = 0; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i) {
                const RenderTarget& reflectTarget = _rtPool->renderTarget(RenderTargetID(RenderTargetUsage::REFLECTION_PLANAR, i));
                const RTAttachment_ptr& depthAttachment = reflectTarget.getAttachmentPtr(RTAttachmentType::Depth, 0);

                vector<ExternalRTAttachmentDescriptor> externalAttachments = {
                     { depthAttachment,  RTAttachmentType::Depth }
                };

                if_constexpr(Config::USE_COLOURED_WOIT) {
                    const RTAttachment_ptr& screenAttachment = reflectTarget.getAttachmentPtr(RTAttachmentType::Colour, 0);
                    externalAttachments.push_back(
                        { screenAttachment,  RTAttachmentType::Colour, to_U8(ScreenTargets::MODULATE) }
                    );
                }

                RenderTargetDescriptor oitDesc = {};
                oitDesc._name = Util::StringFormat("OIT_REFLECT_RES_%d", i);
                oitDesc._resolution = vec2<U16>(reflectRes);
                oitDesc._attachmentCount = to_U8(oitAttachments.size());
                oitDesc._attachments = oitAttachments.data();
                oitDesc._externalAttachmentCount = to_U8(externalAttachments.size());
                oitDesc._externalAttachments = externalAttachments.data();
                oitDesc._msaaSamples = 0;
                _rtPool->allocateRT(RenderTargetUsage::OIT_REFLECT, oitDesc);
            }
        }
    }
    {
        TextureDescriptor environmentDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE);
        TextureDescriptor depthDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);

        environmentDescriptorCube.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        depthDescriptorCube.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RTAttachmentDescriptors attachments = {
            { environmentDescriptorCube, reflectionSamplerHash, RTAttachmentType::Colour },
            { depthDescriptorCube,       reflectionSamplerHash, RTAttachmentType::Depth },
        };

        RenderTargetDescriptor refDesc = {};
        refDesc._resolution = vec2<U16>(reflectRes);
        refDesc._attachmentCount = to_U8(attachments.size());
        refDesc._attachments = attachments.data();

        refDesc._name = "Reflection_Cube_Array";
        _rtPool->allocateRT(RenderTargetUsage::REFLECTION_CUBE, refDesc);
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "ImmediateModeEmulation.glsl";
        vertModule._variant = "GUI";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "ImmediateModeEmulation.glsl";
        fragModule._variant = "GUI";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        ResourceDescriptor immediateModeShader("ImmediateModeEmulationGUI");
        immediateModeShader.waitForReady(true);
        immediateModeShader.propertyDescriptor(shaderDescriptor);
        _textRenderShader = CreateResource<ShaderProgram>(cache, immediateModeShader, loadTasks);
        _textRenderShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor descriptor = {};
            descriptor._shaderProgramHandle = _textRenderShader->handle();
            descriptor._stateHash = get2DStateBlock();
            _textRenderPipeline = newPipeline(descriptor);
        });
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "HiZConstruct.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        // Initialized our HierarchicalZ construction shader (takes a depth attachment and down-samples it for every mip level)
        ResourceDescriptor descriptor1("HiZConstruct");
        descriptor1.waitForReady(false);
        descriptor1.propertyDescriptor(shaderDescriptor);
        _HIZConstructProgram = CreateResource<ShaderProgram>(cache, descriptor1, loadTasks);
        _HIZConstructProgram->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor pipelineDesc;
            pipelineDesc._stateHash = _stateDepthOnlyRenderingHash;
            pipelineDesc._shaderProgramHandle = _HIZConstructProgram->handle();

            _HIZPipeline = newPipeline(pipelineDesc);
        });
    }
    {
        ShaderModuleDescriptor compModule = {};
        compModule._moduleType = ShaderType::COMPUTE;
        compModule._defines.emplace_back(Util::StringFormat("WORK_GROUP_SIZE %d", GROUP_SIZE_AABB));
        compModule._sourceFile = "HiZOcclusionCull.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(compModule);

        ResourceDescriptor descriptor2("HiZOcclusionCull");
        descriptor2.waitForReady(false);
        descriptor2.propertyDescriptor(shaderDescriptor);
        _HIZCullProgram = CreateResource<ShaderProgram>(cache, descriptor2, loadTasks);
        _HIZCullProgram->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._shaderProgramHandle = _HIZCullProgram->handle();
            _HIZCullPipeline = newPipeline(pipelineDescriptor);
        });
    }
    {
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
        shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        
        ResourceDescriptor previewRTShader("fbPreview");
        previewRTShader.waitForReady(true);
        previewRTShader.propertyDescriptor(shaderDescriptor);
        _renderTargetDraw = CreateResource<ShaderProgram>(cache, previewRTShader, loadTasks);
        _renderTargetDraw->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) noexcept {
            _previewRenderTargetColour = _renderTargetDraw;
        });
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "fbPreview.glsl";
        fragModule._variant = "LinearDepth.ScenePlanes";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        ResourceDescriptor previewReflectionRefractionDepth("fbPreviewLinearDepthScenePlanes");
        previewReflectionRefractionDepth.waitForReady(false);
        previewReflectionRefractionDepth.propertyDescriptor(shaderDescriptor);
        _previewRenderTargetDepth = CreateResource<ShaderProgram>(cache, previewReflectionRefractionDepth, loadTasks);
    }
    ShaderModuleDescriptor blurVertModule = {};
    blurVertModule._moduleType = ShaderType::VERTEX;
    blurVertModule._sourceFile = "baseVertexShaders.glsl";
    blurVertModule._variant = "FullScreenQuad";
    {
        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "blur.glsl";
        fragModule._variant = "Generic";

        {
            ShaderProgramDescriptor shaderDescriptorSingle = {};
            shaderDescriptorSingle._modules.push_back(blurVertModule);
            shaderDescriptorSingle._modules.push_back(fragModule);
            shaderDescriptorSingle._primitiveTopology = PrimitiveTopology::TRIANGLES;

            ResourceDescriptor blur("BoxBlur_Single");
            blur.propertyDescriptor(shaderDescriptorSingle);
            _blurBoxShaderSingle = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurBoxShaderSingle->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                _blurBoxPipelineSingleCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
        {
            ShaderProgramDescriptor shaderDescriptorLayered = {};
            shaderDescriptorLayered._modules.push_back(blurVertModule);
            shaderDescriptorLayered._modules.push_back(fragModule);
            shaderDescriptorLayered._modules.back()._variant += ".Layered";
            shaderDescriptorLayered._modules.back()._defines.emplace_back("LAYERED");
            shaderDescriptorLayered._primitiveTopology = PrimitiveTopology::TRIANGLES;

            ResourceDescriptor blur("BoxBlur_Layered");
            blur.propertyDescriptor(shaderDescriptorLayered);
            _blurBoxShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurBoxShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                _blurBoxPipelineLayeredCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
    }
    {
        {
            ShaderModuleDescriptor geomModule = {};
            geomModule._moduleType = ShaderType::GEOMETRY;
            geomModule._sourceFile = "blur.glsl";
            geomModule._variant = "GaussBlur";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "blur.glsl";
            fragModule._variant = "GaussBlur";

            {
                ShaderProgramDescriptor shaderDescriptorSingle = {};
                shaderDescriptorSingle._modules.push_back(blurVertModule);
                shaderDescriptorSingle._modules.push_back(geomModule);
                shaderDescriptorSingle._modules.push_back(fragModule);
                shaderDescriptorSingle._globalDefines.emplace_back("GS_MAX_INVOCATIONS 1");
                shaderDescriptorSingle._primitiveTopology = PrimitiveTopology::POINTS;

                ResourceDescriptor blur("GaussBlur_Single");
                blur.propertyDescriptor(shaderDescriptorSingle);
                _blurGaussianShaderSingle = CreateResource<ShaderProgram>(cache, blur, loadTasks);
                _blurGaussianShaderSingle->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                    const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateHash = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                    _blurGaussianPipelineSingleCmd._pipeline = newPipeline(pipelineDescriptor);
                    });
            }
            {
                ShaderProgramDescriptor shaderDescriptorLayered = {};
                shaderDescriptorLayered._modules.push_back(blurVertModule);
                shaderDescriptorLayered._modules.push_back(geomModule);
                shaderDescriptorLayered._modules.push_back(fragModule);
                shaderDescriptorLayered._modules.back()._variant += ".Layered";
                shaderDescriptorLayered._modules.back()._defines.emplace_back("LAYERED");
                shaderDescriptorLayered._globalDefines.emplace_back(Util::StringFormat("GS_MAX_INVOCATIONS %d", MAX_INVOCATIONS_BLUR_SHADER_LAYERED));
                shaderDescriptorLayered._primitiveTopology = PrimitiveTopology::POINTS;

                ResourceDescriptor blur("GaussBlur_Layered");
                blur.propertyDescriptor(shaderDescriptorLayered);
                _blurGaussianShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
                _blurGaussianShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                    const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateHash = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = blurShader->handle();

                    _blurGaussianPipelineLayeredCmd._pipeline = newPipeline(pipelineDescriptor);
                    });
            }
        }
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "ImmediateModeEmulation.glsl";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "ImmediateModeEmulation.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back(vertModule);
            shaderDescriptor._modules.push_back(fragModule);

            // Create an immediate mode rendering shader that simulates the fixed function pipeline
            {
                ResourceDescriptor immediateModeShader("ImmediateModeEmulation");
                immediateModeShader.waitForReady(true);
                immediateModeShader.propertyDescriptor(shaderDescriptor);
                _imShader = CreateResource<ShaderProgram>(cache, immediateModeShader);
                assert(_imShader != nullptr);
            }
            {
                shaderDescriptor._modules.back()._defines.emplace_back("WORLD_PASS");
                ResourceDescriptor immediateModeShader("ImmediateModeEmulation-World");
                immediateModeShader.waitForReady(true);
                immediateModeShader.propertyDescriptor(shaderDescriptor);
                _imWorldShader = CreateResource<ShaderProgram>(cache, immediateModeShader);
                assert(_imWorldShader != nullptr);
            }

            {
                shaderDescriptor._modules.back()._defines.emplace_back("OIT_PASS");
                ResourceDescriptor immediateModeShader("ImmediateModeEmulation-OIT");
                immediateModeShader.waitForReady(true);
                immediateModeShader.propertyDescriptor(shaderDescriptor);
                _imWorldOITShader = CreateResource<ShaderProgram>(cache, immediateModeShader);
                assert(_imWorldOITShader != nullptr);
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
        fragModule._sourceFile = "display.glsl";

        ResourceDescriptor descriptor3("display");
        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        descriptor3.propertyDescriptor(shaderDescriptor);
        {
            _displayShader = CreateResource<ShaderProgram>(cache, descriptor3, loadTasks);
            _displayShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = _displayShader->handle();
                _drawFSTexturePipelineCmd._pipeline = newPipeline(pipelineDescriptor);

                BlendingSettings& blendState = pipelineDescriptor._blendStates._settings[0];
                blendState.enabled(true);
                blendState.blendSrc(BlendProperty::SRC_ALPHA);
                blendState.blendDest(BlendProperty::INV_SRC_ALPHA);
                blendState.blendOp(BlendOperation::ADD);
                _drawFSTexturePipelineBlendCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
        {
            shaderDescriptor._modules.back()._defines.emplace_back("DEPTH_ONLY");
            descriptor3.propertyDescriptor(shaderDescriptor);
            _depthShader = CreateResource<ShaderProgram>(cache, descriptor3, loadTasks);
            _depthShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateHash = _stateDepthOnlyRenderingHash;
                pipelineDescriptor._shaderProgramHandle = _depthShader->handle();

                _drawFSDepthPipelineCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
    }

    context().paramHandler().setParam<bool>(_ID("rendering.previewDebugViews"), false);
    {
        // Create general purpose render state blocks
        RenderStateBlock primitiveStateBlock{};

        PipelineDescriptor pipelineDesc;
        pipelineDesc._shaderProgramHandle = _imWorldShader->handle();

        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipeline = newPipeline(pipelineDesc);

        primitiveStateBlock.depthTestEnabled(false);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoDepth = newPipeline(pipelineDesc);

        primitiveStateBlock.setCullMode(CullMode::NONE);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoCullNoDepth = newPipeline(pipelineDesc);

        primitiveStateBlock.depthTestEnabled(true);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoCull = newPipeline(pipelineDesc);

    }
    _renderer = eastl::make_unique<Renderer>(context(), cache);

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
    const DisplayWindow* mainWindow = context().app().windowManager().mainWindow();

    SizeChangeParams params = {};
    params.width = _rtPool->screenTarget().getWidth();
    params.height = _rtPool->screenTarget().getHeight();
    params.isWindowResize = false;
    params.winGUID = mainWindow->getGUID();

    if (context().app().onSizeChange(params)) {
        NOP();
    }

    // Everything is ready from the rendering point of view
    return ErrorCode::NO_ERR;
}


/// Revert everything that was set up in initRenderingAPI()
void GFXDevice::closeRenderingAPI() {
    if (_api == nullptr) {
        //closeRenderingAPI called without init!
        return;
    }

    _debugLines.reset();
    _debugBoxes.reset();
    _debugOBBs.reset();
    _debugFrustums.reset();
    _debugCones.reset();
    _debugSpheres.reset();
    _debugViews.clear();
    _IMGUIBuffers.clear();

    // Delete the renderer implementation
    Console::printfn(Locale::Get(_ID("CLOSING_RENDERER")));
    _renderer.reset(nullptr);

    RenderStateBlock::Clear();
    SamplerDescriptor::Clear();

    GFX::DestroyPools();
    MemoryManager::SAFE_DELETE(_rtPool);

    _previewDepthMapShader = nullptr;
    _previewRenderTargetColour = nullptr;
    _previewRenderTargetDepth = nullptr;
    _renderTargetDraw = nullptr;
    _HIZConstructProgram = nullptr;
    _HIZCullProgram = nullptr;
    _displayShader = nullptr;
    _depthShader = nullptr;
    _textRenderShader = nullptr;
    _blurBoxShaderSingle = nullptr;
    _blurBoxShaderLayered = nullptr;
    _blurGaussianShaderSingle = nullptr;
    _blurGaussianShaderLayered = nullptr;
    _imShader = nullptr;
    _imWorldShader = nullptr;
    _imWorldOITShader = nullptr;
    _gfxBuffers.reset(true, true, true);

    // Close the shader manager
    MemoryManager::DELETE(_shaderComputeQueue);
    if (!ShaderProgram::OnShutdown()) {
        DIVIDE_UNEXPECTED_CALL();
    }

    RenderPassExecutor::OnShutdown(*this);
    Texture::OnShutdown();
    assert(ShaderProgram::ShaderProgramCount() == 0);
    // Close the rendering API
    _api->closeRenderingAPI();
    _api.reset();

    ScopedLock<Mutex> lock(_graphicsResourceMutex);
    if (!_graphicResources.empty()) {
        string list = " [ ";
        for (const std::tuple<GraphicsResource::Type, I64, U64>& res : _graphicResources) {
            list.append(TypeUtil::GraphicResourceTypeToName(std::get<0>(res)));
            list.append(" _ ");
            list.append(Util::to_string(std::get<1>(res)));
            list.append(" _ ");
            list.append(Util::to_string(std::get<2>(res)));
            list.append(" ");
        }
        list += " ]";
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_LEAKED_RESOURCES")), _graphicResources.size());
        Console::errorfn(list.c_str());
    }
    _graphicResources.clear();
}
#pragma endregion

#pragma region Main frame loop
/// After a swap buffer call, the CPU may be idle waiting for the GPU to draw to the screen, so we try to do some processing
void GFXDevice::idle(const bool fast) {
    OPTICK_EVENT();

    _api->idle(fast);

    _shaderComputeQueue->idle();
    // Pass the idle call to the post processing system
    _renderer->idle();
    // And to the shader manager
    ShaderProgram::Idle(context());
}

void GFXDevice::update(const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp) {
    getRenderer().postFX().update(deltaTimeUSFixed, deltaTimeUSApp);
    _gpuBlock._renderData._renderProperties.x += Time::MicrosecondsToMilliseconds<F32>(deltaTimeUSFixed);
    _gpuBlock._renderNeedsUpload = true;
}

void GFXDevice::beginFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

    if (global) {
        if (_queuedScreenSampleChange != s_invalidQueueSampleCount) {
            setScreenMSAASampleCountInternal(_queuedScreenSampleChange);
            _queuedScreenSampleChange = s_invalidQueueSampleCount;
        }
        for (U8 i = 0u; i < to_base(ShadowType::COUNT); ++i) {
            if (_queuedShadowSampleChange[i] != s_invalidQueueSampleCount) {
                setShadowMSAASampleCountInternal(static_cast<ShadowType>(i), _queuedShadowSampleChange[i]);
                _queuedShadowSampleChange[i] = s_invalidQueueSampleCount;
            }
        }
    }
    if (global && _resolutionChangeQueued.second) {
        SizeChangeParams params;
        params.isWindowResize = false;
        params.isFullScreen = window.fullscreen();
        params.width = _resolutionChangeQueued.first.width;
        params.height = _resolutionChangeQueued.first.height;
        params.winGUID = context().mainWindow().getGUID();

        if (context().app().onSizeChange(params)) {
            NOP();
        }
        _resolutionChangeQueued.second = false;
    }

    if (!_api->beginFrame(window, global)) {
        NOP();
    }
}

namespace {
    template<typename Data, size_t N>
    inline void DecrementPrimitiveLifetime(DebugPrimitiveHandler<Data, N>& container) {
        ScopedLock<Mutex> w_lock(container._dataLock);
        for (auto& entry : container._debugData) {
            if (entry._frameLifeTime > 0u) {
                entry._frameLifeTime -= 1u;
            }
        }
    }
};

void GFXDevice::endFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

    if (global) {
        s_frameCount += 1u;

        frameDrawCallsPrev(frameDrawCalls());
        frameDrawCalls(0u);

        DecrementPrimitiveLifetime(_debugLines);
        DecrementPrimitiveLifetime(_debugBoxes);
        DecrementPrimitiveLifetime(_debugOBBs);
        DecrementPrimitiveLifetime(_debugFrustums);
        DecrementPrimitiveLifetime(_debugCones);
        DecrementPrimitiveLifetime(_debugSpheres);
    }

    DIVIDE_ASSERT(_cameraSnapshots.empty(), "Not all camera snapshots have been cleared properly! Check command buffers for missmatched push/pop!");
    DIVIDE_ASSERT(_viewportStack.empty(), "Not all viewports have been cleared properly! Check command buffers for missmatched push/pop!");
    // Activate the default render states
    _api->endFrame(window, global);
    if (_gfxBuffers._needsResizeCam || _gfxBuffers._needsResizeRender) {
        resizeGPUBlocks(_gfxBuffers._needsResizeCam ? _gfxBuffers._currentSizeCam + TargetBufferSizeCam : _gfxBuffers._currentSizeCam,
                        _gfxBuffers._needsResizeRender ? _gfxBuffers._currentSizeRender + TargetBufferSizeRender : _gfxBuffers._currentSizeRender,
                        _gfxBuffers._currentSizeCullCounter);
        _gfxBuffers._needsResizeCam = _gfxBuffers._needsResizeRender = false;
    }
    _gfxBuffers.onEndFrame();
}

#pragma endregion

#pragma region Utility functions
/// Generate a cube texture and store it in the provided RenderTarget
void GFXDevice::generateCubeMap(RenderPassParams& params,
                                const I16 arrayOffset,
                                const vec3<F32>& pos,
                                const vec2<F32>& zPlanes,
                                GFX::CommandBuffer& commandsInOut,
                                std::array<Camera*, 6>& cameras) {
    OPTICK_EVENT();

    if (arrayOffset < 0) {
        return;
    }

    // Only the first colour attachment or the depth attachment is used for now
    // and it must be a cube map texture
    RenderTarget& cubeMapTarget = _rtPool->renderTarget(params._target);
    // Colour attachment takes precedent over depth attachment
    const bool hasColour = cubeMapTarget.hasAttachment(RTAttachmentType::Colour, 0);
    const bool hasDepth = cubeMapTarget.hasAttachment(RTAttachmentType::Depth, 0);
    const vec2<U16> targetResolution = cubeMapTarget.getResolution();

    // Everyone's innocent until proven guilty
    bool isValidFB = false;
    if (hasColour) {
        const RTAttachment& colourAttachment = cubeMapTarget.getAttachment(RTAttachmentType::Colour, 0);
        // We only need the colour attachment
        isValidFB = IsCubeTexture(colourAttachment.texture()->descriptor().texType());
    } else {
        const RTAttachment& depthAttachment = cubeMapTarget.getAttachment(RTAttachmentType::Depth, 0);
        // We don't have a colour attachment, so we require a cube map depth attachment
        isValidFB = hasDepth && IsCubeTexture(depthAttachment.texture()->descriptor().texType());
    }
    // Make sure we have a proper render target to draw to
    if (!isValidFB) {
        // Future formats must be added later (e.g. cube map arrays)
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_DEVICE_INVALID_FB_CUBEMAP")));
        return;
    }

    // No dual-paraboloid rendering here. Just draw once for each face.
    static const std::array<std::pair<vec3<F32>, vec3<F32>>, 6> CameraDirections = {{
          // Target Dir          Up Dir
          {WORLD_X_AXIS,      WORLD_Y_AXIS    },
          {WORLD_X_NEG_AXIS,  WORLD_Y_AXIS    },
          {WORLD_Y_AXIS,      WORLD_Z_NEG_AXIS},
          {WORLD_Y_NEG_AXIS,  WORLD_Z_AXIS    },
          {WORLD_Z_AXIS,      WORLD_Y_AXIS    },
          {WORLD_Z_NEG_AXIS,  WORLD_Y_AXIS    }
    }};

    // For each of the environment's faces (TOP, DOWN, NORTH, SOUTH, EAST, WEST)
    params._passName = "CubeMap";
    params._layerParams._type = hasColour ? RTAttachmentType::Colour : RTAttachmentType::Depth;
    params._layerParams._includeDepth = hasColour && hasDepth;
    params._layerParams._index = 0;
    params._layerParams._layer = arrayOffset * 6;

    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;

    RenderPassManager* passMgr = parent().renderPassManager();

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._name = "Clear Target Layers";
    beginRenderPassCmd._target = params._target;

    GFX::ClearRenderTargetCommand clearRTCmd{};
    clearRTCmd._target = params._target;
    clearRTCmd._descriptor._resetToDefault = false;

    for (U8 i = 0u; i < 6u; ++i) {
        // Draw to the current cubemap face
        params._layerParams._layer = i + arrayOffset * 6;

        // Let's clear only our target layers
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(commandsInOut, beginRenderPassCmd);
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(commandsInOut)->_writeLayers.push_back(params._layerParams);
        GFX::EnqueueCommand(commandsInOut, clearRTCmd);
        // No need to reset back to zero. We will be drawing into it anyway.
        GFX::EnqueueCommand<GFX::EndRenderSubPassCommand>(commandsInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(commandsInOut);


        Camera* camera = cameras[i];
        if (camera == nullptr) {
            camera = Camera::GetUtilityCamera(Camera::UtilityCamera::CUBE);
        }

        // Set a 90 degree horizontal FoV perspective projection
        camera->setProjection(to_F32(aspect), Angle::to_VerticalFoV(Angle::DEGREES<F32>(90.0f), aspect), zPlanes);
        // Point our camera to the correct face
        camera->lookAt(pos, pos + CameraDirections[i].first * zPlanes.max, -CameraDirections[i].second);
        params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);
        // Pass our render function to the renderer
        passMgr->doCustomPass(camera, params, commandsInOut);
    }
}

void GFXDevice::generateDualParaboloidMap(RenderPassParams& params,
                                          const I16 arrayOffset,
                                          const vec3<F32>& pos,
                                          const vec2<F32>& zPlanes,
                                          GFX::CommandBuffer& bufferInOut,
                                          std::array<Camera*, 2>& cameras)
{
    OPTICK_EVENT();

    if (arrayOffset < 0) {
        return;
    }

    RenderTarget& paraboloidTarget = _rtPool->renderTarget(params._target);
    // Colour attachment takes precedent over depth attachment
    const bool hasColour = paraboloidTarget.hasAttachment(RTAttachmentType::Colour, 0);
    const bool hasDepth = paraboloidTarget.hasAttachment(RTAttachmentType::Depth, 0);
    const vec2<U16> targetResolution = paraboloidTarget.getResolution();

    bool isValidFB = false;
    if (hasColour) {
        const RTAttachment& colourAttachment = paraboloidTarget.getAttachment(RTAttachmentType::Colour, 0);
        // We only need the colour attachment
        isValidFB = IsArrayTexture(colourAttachment.texture()->descriptor().texType());
    } else {
        const RTAttachment& depthAttachment = paraboloidTarget.getAttachment(RTAttachmentType::Depth, 0);
        // We don't have a colour attachment, so we require a cube map depth attachment
        isValidFB = hasDepth && IsArrayTexture(depthAttachment.texture()->descriptor().texType());
    }
    // Make sure we have a proper render target to draw to
    if (!isValidFB) {
        // Future formats must be added later (e.g. cube map arrays)
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_DEVICE_INVALID_FB_DP")));
        return;
    }

    params._passName = "DualParaboloid";
    params._layerParams._type = hasColour ? RTAttachmentType::Colour : RTAttachmentType::Depth;
    params._layerParams._index = 0;

    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;

    RenderPassManager* passMgr = parent().renderPassManager();

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._name = "Clear Target Layers";
    beginRenderPassCmd._target = params._target;

    GFX::ClearRenderTargetCommand clearRTCmd{};
    clearRTCmd._target = params._target;
    clearRTCmd._descriptor._resetToDefault = false;

    for (U8 i = 0u; i < 2u; ++i) {
        Camera* camera = cameras[i];
        if (!camera) {
            camera = Camera::GetUtilityCamera(Camera::UtilityCamera::DUAL_PARABOLOID);
        }

        params._layerParams._layer = i + arrayOffset;

        // Let's clear only our target layers
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut, beginRenderPassCmd);
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
        GFX::EnqueueCommand(bufferInOut, clearRTCmd);
        // No need to reset back to zero. We will be drawing into it anyway.
        GFX::EnqueueCommand<GFX::EndRenderSubPassCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

        // Point our camera to the correct face
        camera->lookAt(pos, pos + (i == 0 ? WORLD_Z_NEG_AXIS : WORLD_Z_AXIS) * zPlanes.y);
        // Set a 180 degree vertical FoV perspective projection
        camera->setProjection(to_F32(aspect), Angle::to_VerticalFoV(Angle::DEGREES<F32>(180.0f), aspect), zPlanes);
        // And generated required matrices
        // Pass our render function to the renderer
        params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);

        passMgr->doCustomPass(camera, params, bufferInOut);
    }
}

void GFXDevice::blurTarget(RenderTargetHandle& blurSource,
                           RenderTargetHandle& blurBuffer,
                           const RenderTargetHandle& blurTarget,
                           const RTAttachmentType att,
                           const U8 index,
                           const I32 kernelSize,
                           const bool gaussian,
                           const U8 layerCount,
                           GFX::CommandBuffer& bufferInOut)
{
    const auto& inputAttachment = blurSource._rt->getAttachment(att, index);
    const auto& bufferAttachment = blurBuffer._rt->getAttachment(att, index);

    GFX::SendPushConstantsCommand pushConstantsCmd{};

    const U8 loopCount = gaussian ? 1u : layerCount;

    {// Blur horizontally
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = blurBuffer._targetID;
        renderPassCmd->_name = "BLUR_RENDER_TARGET_HORIZONTAL";

        GFX::EnqueueCommand(bufferInOut, gaussian ? (layerCount > 1 ? _blurGaussianPipelineLayeredCmd : _blurGaussianPipelineSingleCmd)
                                                  : (layerCount > 1 ? _blurBoxPipelineLayeredCmd      : _blurBoxPipelineSingleCmd));

        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ inputAttachment.texture()->data(), inputAttachment.samplerHash(), TextureUsage::UNIT0 });

        pushConstantsCmd._constants.set(_ID("verticalBlur"), GFX::PushConstantType::INT, false);
        if (gaussian) {
            const vec2<F32> blurSize(1.0f / blurBuffer._rt->getResolution().width, 1.0f / blurBuffer._rt->getResolution().height);
            pushConstantsCmd._constants.set(_ID("blurSizes"), GFX::PushConstantType::VEC2, blurSize);
            pushConstantsCmd._constants.set(_ID("layerCount"), GFX::PushConstantType::INT, to_I32(layerCount));
            pushConstantsCmd._constants.set(_ID("layerOffsetRead"), GFX::PushConstantType::INT, 0);
            pushConstantsCmd._constants.set(_ID("layerOffsetWrite"), GFX::PushConstantType::INT, 0);
        } else {
            pushConstantsCmd._constants.set(_ID("kernelSize"), GFX::PushConstantType::INT, kernelSize);
            pushConstantsCmd._constants.set(_ID("size"), GFX::PushConstantType::VEC2, vec2<F32>(blurBuffer._rt->getResolution()));
            if (layerCount > 1) {
                pushConstantsCmd._constants.set(_ID("layer"), GFX::PushConstantType::INT, 0);
            }
        }

        for (U8 loop = 0u; loop < loopCount; ++loop) {
            if (!gaussian && loop > 0u) {
                pushConstantsCmd._constants.set(_ID("layer"), GFX::PushConstantType::INT, to_I32(loop));
                GFX::EnqueueCommand(bufferInOut, pushConstantsCmd);
            }
            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }
    {// Blur vertically
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = blurTarget._targetID;
        renderPassCmd->_name = "BLUR_RENDER_TARGET_VERTICAL";

        pushConstantsCmd._constants.set(_ID("verticalBlur"), GFX::PushConstantType::INT, true);
        if (gaussian) {
            const vec2<F32> blurSize(1.0f / blurTarget._rt->getResolution().width, 1.0f / blurTarget._rt->getResolution().height);
            pushConstantsCmd._constants.set(_ID("blurSizes"), GFX::PushConstantType::VEC2, blurSize);
        } else {
            pushConstantsCmd._constants.set(_ID("size"), GFX::PushConstantType::VEC2, vec2<F32>(blurTarget._rt->getResolution()));
        }

        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ bufferAttachment.texture()->data(), bufferAttachment.samplerHash(), TextureUsage::UNIT0 });
        GFX::EnqueueCommand(bufferInOut, pushConstantsCmd);

        for (U8 loop = 0u; loop < loopCount; ++loop) {
            if (!gaussian && loop > 0u) {
                pushConstantsCmd._constants.set(_ID("layer"), GFX::PushConstantType::INT, to_I32(loop));
                GFX::EnqueueCommand(bufferInOut, pushConstantsCmd);
            }
            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }
}
#pragma endregion

#pragma region Resolution, viewport and window management
void GFXDevice::increaseResolution() {
    stepResolution(true);
}

void GFXDevice::decreaseResolution() {
    stepResolution(false);
}

void GFXDevice::stepResolution(const bool increment) {
    const auto compare = [](const vec2<U16>& a, const vec2<U16>& b) noexcept -> bool {
        return a.x > b.x || a.y > b.y;
    };

    const WindowManager& winManager = _parent.platformContext().app().windowManager();

    const vector<GPUState::GPUVideoMode>& displayModes = _state.getDisplayModes(winManager.mainWindow()->currentDisplayIndex());

    bool found = false;
    vec2<U16> foundRes;
    if (increment) {
        for (auto it = displayModes.rbegin(); it != displayModes.rend(); ++it) {
            const vec2<U16>& res = it->_resolution;
            if (compare(res, _renderingResolution)) {
                found = true;
                foundRes.set(res);
                break;
            }
        }
    } else {
        for (const GPUState::GPUVideoMode& mode : displayModes) {
            const vec2<U16>& res = mode._resolution;
            if (compare(_renderingResolution, res)) {
                found = true;
                foundRes.set(res);
                break;
            }
        }
    }
    
    if (found) {
        _resolutionChangeQueued.first.set(foundRes);
        _resolutionChangeQueued.second = true;
    }
}

bool GFXDevice::makeImagesResident(const Images& images) const {
    OPTICK_EVENT();

    for (const Image& image : images._entries) {
        if (image._texture != nullptr) {
            image._texture->bindLayer(image._binding, image._level, image._layer, image._layered, image._flag);
        }
    }

    return true;
}

void GFXDevice::toggleFullScreen() const
{
    const WindowManager& winManager = _parent.platformContext().app().windowManager();

    switch (winManager.mainWindow()->type()) {
        case WindowType::WINDOW:
            winManager.mainWindow()->changeType(WindowType::FULLSCREEN_WINDOWED);
            break;
        case WindowType::FULLSCREEN_WINDOWED:
            winManager.mainWindow()->changeType(WindowType::FULLSCREEN);
            break;
        case WindowType::FULLSCREEN:
            winManager.mainWindow()->changeType(WindowType::WINDOW);
            break;
        default: break;
    };
}

void GFXDevice::setScreenMSAASampleCount(const U8 sampleCount) {
    _queuedScreenSampleChange = sampleCount;
}

void GFXDevice::setShadowMSAASampleCount(const ShadowType type, const U8 sampleCount) {
    _queuedShadowSampleChange[to_base(type)] = sampleCount;
}

void GFXDevice::setScreenMSAASampleCountInternal(U8 sampleCount) {
    CLAMP(sampleCount, to_U8(0u), gpuState().maxMSAASampleCount());
    if (_context.config().rendering.MSAASamples != sampleCount) {
        _context.config().rendering.MSAASamples = sampleCount;
        _rtPool->updateSampleCount(RenderTargetUsage::SCREEN_MS, sampleCount);
        _rtPool->updateSampleCount(RenderTargetUsage::OIT_MS, sampleCount);
        Material::RecomputeShaders();
    }
}

void GFXDevice::setShadowMSAASampleCountInternal(const ShadowType type, U8 sampleCount) {
    CLAMP(sampleCount, to_U8(0u), gpuState().maxMSAASampleCount());
    ShadowMap::setMSAASampleCount(type, sampleCount);
}

/// The main entry point for any resolution change request
bool GFXDevice::onSizeChange(const SizeChangeParams& params) {
    if (params.winGUID != context().app().windowManager().mainWindow()->getGUID()) {
        return false;
    }

    const U16 w = params.width;
    const U16 h = params.height;

    if (!params.isWindowResize) {
        // Update resolution only if it's different from the current one.
        // Avoid resolution change on minimize so we don't thrash render targets
        if (w < 1 || h < 1 || _renderingResolution == vec2<U16>(w, h)) {
            return false;
        }

        _renderingResolution.set(w, h);

        // Update the 2D camera so it matches our new rendering viewport
        if (Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->setProjection(vec4<F32>(0, to_F32(w), 0, to_F32(h)), vec2<F32>(-1, 1))) {
            Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->updateFrustum();
        }

        if (Camera::GetUtilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->setProjection(vec4<F32>(0, to_F32(w), to_F32(h), 0), vec2<F32>(-1, 1))) {
            Camera::GetUtilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->updateFrustum();
        }

        // Update render targets with the new resolution
        _rtPool->resizeTargets(RenderTargetUsage::SCREEN, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SCREEN_PREV, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SCREEN_MS, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SSAO_RESULT, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::LINEAR_DEPTH, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SSR_RESULT, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::HI_Z, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::OIT, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::OIT_MS, w, h);
        if_constexpr(Config::Build::ENABLE_EDITOR) {
            _rtPool->resizeTargets(RenderTargetUsage::EDITOR, w, h);
        }

        // Update post-processing render targets and buffers
        _renderer->updateResolution(w, h);
    }

    return fitViewportInWindow(w, h);
}

bool GFXDevice::fitViewportInWindow(const U16 w, const U16 h) {
    const F32 currentAspectRatio = renderingAspectRatio();

    I32 left = 0, bottom = 0;
    I32 newWidth = w;
    I32 newHeight = h;

    const I32 tempWidth = to_I32(h * currentAspectRatio);
    const I32 tempHeight = to_I32(w / currentAspectRatio);

    const F32 newAspectRatio = to_F32(tempWidth) / tempHeight;

    if (newAspectRatio <= currentAspectRatio) {
        newWidth = tempWidth;
        left = to_I32((w - newWidth) * 0.5f);
    } else {
        newHeight = tempHeight;
        bottom = to_I32((h - newHeight) * 0.5f);
    }
    
    context().mainWindow().renderingViewport(Rect<I32>(left, bottom, newWidth, newHeight));

    if (!COMPARE(newAspectRatio, currentAspectRatio)) {
        context().mainWindow().clearFlags(true, false);
        return true;
    }

    // If the aspect ratios match, then we should auto-fit to the entire visible drawing space so 
    // no need to keep clearing the backbuffer. This is one of the most useless micro-optimizations possible
    // but is really easy to add -Ionut
    context().mainWindow().clearFlags(false, false);
    return false;
}
#pragma endregion

#pragma region GPU State
PerformanceMetrics GFXDevice::getPerformanceMetrics() const noexcept {
    PerformanceMetrics ret = _api->getPerformanceMetrics();
    ret._scratchBufferQueueUsage[0] = to_U32(_gfxBuffers.crtBuffers()._camWritesThisFrame);
    ret._scratchBufferQueueUsage[1] = to_U32(_gfxBuffers.crtBuffers()._renderWritesThisFrame);
    return ret;
}

const DescriptorSet& GFXDevice::uploadGPUBlock() {
    static DescriptorSet bindSet{};

    OPTICK_EVENT();

    GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();

    if (_gpuBlock._camNeedsUpload) {
        frameBuffers._camDataBuffer->incQueue();
        if (++frameBuffers._camWritesThisFrame >= _gfxBuffers._currentSizeCam) {
            //We've wrapped around this buffer inside of a single frame so sync performance will degrade
            //unless we increase our buffer size
            _gfxBuffers._needsResizeCam = true;
        }
        frameBuffers._camDataBuffer->writeData(&_gpuBlock._camData);
    }

    if (_gpuBlock._renderNeedsUpload) {
        frameBuffers._renderDataBuffer->incQueue();
        if (++frameBuffers._renderWritesThisFrame >= _gfxBuffers._currentSizeRender) {
            //We've wrapped around this buffer inside of a single frame so sync performance will degrade
            //unless we increase our buffer size
            _gfxBuffers._needsResizeRender = true;
        }
        frameBuffers._renderDataBuffer->writeData(&_gpuBlock._renderData);
    }

    ShaderBufferBinding camBufferBinding;
    camBufferBinding._binding = ShaderBufferLocation::CAM_BLOCK;
    camBufferBinding._buffer = frameBuffers._camDataBuffer.get();
    camBufferBinding._elementRange = { 0, 1 };
    camBufferBinding._lockType = _gpuBlock._camNeedsUpload ? ShaderBufferLockType::AFTER_DRAW_COMMANDS : ShaderBufferLockType::COUNT;
    bindSet._buffers.add(camBufferBinding);

    ShaderBufferBinding renderBufferBinding;
    renderBufferBinding._binding = ShaderBufferLocation::RENDER_BLOCK;
    renderBufferBinding._buffer = frameBuffers._renderDataBuffer.get();
    renderBufferBinding._elementRange = { 0, 1 };
    renderBufferBinding._lockType = _gpuBlock._renderNeedsUpload ? ShaderBufferLockType::AFTER_DRAW_COMMANDS : ShaderBufferLockType::COUNT;
    bindSet._buffers.add(renderBufferBinding);

    _gpuBlock._camNeedsUpload = false;
    _gpuBlock._renderNeedsUpload = false;

    return bindSet;
}

/// set a new list of clipping planes. The old one is discarded
void GFXDevice::setClipPlanes(const FrustumClipPlanes& clipPlanes) {
    if (clipPlanes != _clippingPlanes){
        _clippingPlanes = clipPlanes;

        auto& planes = _clippingPlanes.planes();
        auto& states = _clippingPlanes.planeState();

        U8 count = 0u;
        for (U8 i = 0u; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
            if (states[i]) {
                _gpuBlock._renderData._clipPlanes[count++].set(planes[i]._equation);
                if (count == Config::MAX_CLIP_DISTANCES) {
                    break;
                }
            }
        }

        _gpuBlock._renderData._renderProperties.w = to_F32(count);
        _gpuBlock._renderNeedsUpload = true;
    }
}

void GFXDevice::setDepthRange(const vec2<F32>& depthRange) {
    GFXShaderData::CamData& data = _gpuBlock._camData;
    if (data._renderTargetInfo.xy != depthRange) {
        data._renderTargetInfo.xy = depthRange;
        _gpuBlock._camNeedsUpload = true;
    }
}

void GFXDevice::renderFromCamera(const CameraSnapshot& cameraSnapshot) {
    OPTICK_EVENT();

    GFXShaderData::CamData& data = _gpuBlock._camData;

    bool needsUpdate = false, projectionDirty = false, viewDirty = false;
    if (cameraSnapshot._projectionMatrix != data._ProjectionMatrix) {
        data._ProjectionMatrix.set(cameraSnapshot._projectionMatrix);
        data._InvProjectionMatrix.set(cameraSnapshot._invProjectionMatrix);
        projectionDirty = true;
    }

    if (cameraSnapshot._viewMatrix != data._ViewMatrix) {
        data._ViewMatrix.set(cameraSnapshot._viewMatrix);
        data._InvViewMatrix.set(cameraSnapshot._invViewMatrix);
        viewDirty = true;
    }

    if (projectionDirty || viewDirty) {
        mat4<F32>::Multiply(data._ViewMatrix, data._ProjectionMatrix, data._ViewProjectionMatrix);

        for (U8 i = 0u; i < to_U8(FrustumPlane::COUNT); ++i) {
            data._frustumPlanes[i].set(cameraSnapshot._frustumPlanes[i]._equation);
        }
        needsUpdate = true;
    }

    const vec4<F32> cameraProperties(cameraSnapshot._zPlanes, cameraSnapshot._FoV, data._cameraProperties.w);
    if (data._cameraProperties != cameraProperties) {
        data._cameraProperties.set(cameraProperties);

        if (cameraSnapshot._isOrthoCamera) {
            data._lightingTweakValues.x = 1.f; //scale
            data._lightingTweakValues.y = 0.f; //bias
        } else {
            const F32 zFar = cameraSnapshot._zPlanes.max;
            const F32 zNear = cameraSnapshot._zPlanes.min;

            const F32 CLUSTERS_Z = to_F32(Renderer::CLUSTER_SIZE.z);
            const F32 zLogRatio = std::log(zFar / zNear);

            data._lightingTweakValues.x = CLUSTERS_Z / zLogRatio; //scale
            data._lightingTweakValues.y = -(CLUSTERS_Z * std::log(zNear) / zLogRatio); //bias
        }
        needsUpdate = true;
    }

    const U8 orthoFlag = (cameraSnapshot._isOrthoCamera ? 1u : 0u);
    if (to_U8(data._cameraProperties.w) != orthoFlag) {
        data._cameraProperties.w = to_F32(orthoFlag);
        needsUpdate = true;
    }

    if (needsUpdate) {
        _gpuBlock._camNeedsUpload = true;
        _activeCameraSnapshot = cameraSnapshot;
    }
}

void GFXDevice::shadowingSettings(const F32 lightBleedBias, const F32 minShadowVariance) noexcept {
    GFXShaderData::CamData& data = _gpuBlock._camData;

    if (!COMPARE(data._lightingTweakValues.z, lightBleedBias) ||
        !COMPARE(data._lightingTweakValues.w, minShadowVariance))
    {
        data._lightingTweakValues.zw = { lightBleedBias, minShadowVariance };
        _gpuBlock._camNeedsUpload = true;
    }
}

void GFXDevice::setPreviousViewProjectionMatrix(const mat4<F32>& prevViewMatrix, const mat4<F32> prevProjectionMatrix) {
    bool projectionDirty = false, viewDirty = false;
    if (_gpuBlock._renderData._PreviousViewMatrix != prevViewMatrix) {
        _gpuBlock._renderData._PreviousViewMatrix = prevViewMatrix;
        viewDirty = true;
    }
    if (_gpuBlock._renderData._PreviousProjectionMatrix != prevProjectionMatrix) {
        _gpuBlock._renderData._PreviousProjectionMatrix = prevProjectionMatrix;
        projectionDirty = true;
    }

    if (projectionDirty || viewDirty) {
        mat4<F32>::Multiply(_gpuBlock._renderData._PreviousViewMatrix, _gpuBlock._renderData._PreviousProjectionMatrix, _gpuBlock._renderData._PreviousViewProjectionMatrix);
        _gpuBlock._renderNeedsUpload = true;
    }
}

void GFXDevice::materialDebugFlag(const MaterialDebugFlag flag) {
    if (_materialDebugFlag != flag) {
        _materialDebugFlag = flag;
        _gpuBlock._renderData._renderProperties.z = to_F32(materialDebugFlag());
        _gpuBlock._renderNeedsUpload = true;
    }
}
/// Update the rendering viewport
bool GFXDevice::setViewport(const Rect<I32>& viewport) {
    OPTICK_EVENT();

    // Change the viewport on the Rendering API level
    if (_api->setViewport(viewport)) {
        // Update the buffer with the new value
        _gpuBlock._camData._ViewPort.set(viewport.x, viewport.y, viewport.z, viewport.w);
        _gpuBlock._camNeedsUpload = true;

        const U32 clustersX = to_U32(std::ceil(to_F32(viewport.z) / Renderer::CLUSTER_SIZE.x));
        const U32 clustersY = to_U32(std::ceil(to_F32(viewport.w) / Renderer::CLUSTER_SIZE.y));
        if (clustersX != to_U32(_gpuBlock._camData._renderTargetInfo.z) ||
            clustersY != to_U32(_gpuBlock._camData._renderTargetInfo.w))
        {
            _gpuBlock._camData._renderTargetInfo.z = to_F32(clustersX);
            _gpuBlock._camData._renderTargetInfo.w = to_F32(clustersY);
            _gpuBlock._camNeedsUpload = true;
        }

        return true;
    }

    return false;
}

void GFXDevice::setCameraSnapshot(const PlayerIndex index, const CameraSnapshot& snapshot) noexcept {
    _cameraSnapshotHistory[index] = snapshot;
}

CameraSnapshot& GFXDevice::getCameraSnapshot(const PlayerIndex index) noexcept {
    return _cameraSnapshotHistory[index];
}

const CameraSnapshot& GFXDevice::getCameraSnapshot(const PlayerIndex index) const noexcept {
    return _cameraSnapshotHistory[index];
}

const GFXShaderData::RenderData& GFXDevice::renderingData() const noexcept {
    return _gpuBlock._renderData;
}

const GFXShaderData::CamData& GFXDevice::cameraData() const noexcept {
    return _gpuBlock._camData;
}
#pragma endregion

#pragma region Command buffers, occlusion culling, etc
void GFXDevice::flushCommandBuffer(GFX::CommandBuffer& commandBuffer, const bool batch) {
    OPTICK_EVENT();

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        DIVIDE_ASSERT(Runtime::isMainThread(), "GFXDevice::flushCommandBuffer called from worker thread!");
    }

    if (batch) {
        commandBuffer.batch();
    }

    const auto[error, lastCmdIndex] = commandBuffer.validate();
    if (error != GFX::ErrorType::NONE) {
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_INVALID_COMMAND_BUFFER")), lastCmdIndex, commandBuffer.toString().c_str());
        Console::flush();
        DIVIDE_UNEXPECTED_CALL_MSG(Util::StringFormat("GFXDevice::flushCommandBuffer error [ %s ]: Invalid command buffer. Check error log!", GFX::Names::errorType[to_base(error)]).c_str());
        return;
    }

    const auto bindDescriptorSet = [&](const DescriptorSet& set) {
        for (U8 i = 0u; i < set._buffers.count(); ++i) {
            const ShaderBufferBinding& binding = set._buffers._entries[i];
            if (binding._binding == ShaderBufferLocation::COUNT) {
                // might be leftover from a batching call
                continue;
            }

            assert(binding._buffer != nullptr);
            Attorney::ShaderBufferBind::bindRange(*binding._buffer, 
                                                  to_U8(binding._binding),
                                                  binding._elementRange.min,
                                                  binding._elementRange.max);

            if (binding._lockType != ShaderBufferLockType::COUNT) {
                if (!binding._buffer->lockRange(binding._elementRange.min,
                                                binding._elementRange.max,
                                                binding._lockType))
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
        }
        if (!makeImagesResident(set._images)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    };

    _api->preFlushCommandBuffer(commandBuffer);

    const GFX::CommandBuffer::CommandOrderContainer& commands = commandBuffer();
    for (const GFX::CommandBuffer::CommandEntry& cmd : commands) {
        const GFX::CommandType cmdType = static_cast<GFX::CommandType>(cmd._typeIndex);
        switch (cmdType) {
            case GFX::CommandType::BLIT_RT: {
                OPTICK_EVENT("BLIT_RT");

                const GFX::BlitRenderTargetCommand* crtCmd = commandBuffer.get<GFX::BlitRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd->_source);
                RenderTarget& destination = renderTargetPool().renderTarget(crtCmd->_destination);

                RenderTarget::RTBlitParams params = {};
                params._inputFB = &source;
                params._blitDepth = crtCmd->_blitDepth;
                params._blitColours = crtCmd->_blitColours;

                destination.blitFrom(params);
            } break;
            case GFX::CommandType::CLEAR_RT: {
                OPTICK_EVENT("CLEAR_RT");

                const GFX::ClearRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ClearRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._target);
                source.clear(crtCmd._descriptor);
            }break;
            case GFX::CommandType::RESET_RT: {
                OPTICK_EVENT("RESET_RT");

                const GFX::ResetRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ResetRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._source);
                source.setDefaultState(crtCmd._descriptor);
            } break;
            case GFX::CommandType::RESET_AND_CLEAR_RT: {
                OPTICK_EVENT("RESET_AND_CLEAR_RT");

                const GFX::ResetAndClearRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ResetAndClearRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._source);
                source.setDefaultState(crtCmd._drawDescriptor);
                source.clear(crtCmd._clearDescriptor);
            } break;
            case GFX::CommandType::CLEAR_TEXTURE: {
                OPTICK_EVENT("CLEAR_TEXTURE");

                const GFX::ClearTextureCommand& crtCmd = *commandBuffer.get<GFX::ClearTextureCommand>(cmd);
                if (crtCmd._texture != nullptr) {
                    if (crtCmd._clearRect) {
                        crtCmd._texture->clearSubData(crtCmd._clearColour, crtCmd._level, crtCmd._reactToClear, crtCmd._depthRange);
                    } else {
                        crtCmd._texture->clearData(crtCmd._clearColour, crtCmd._level);
                    }
                }
            }break;
            case GFX::CommandType::READ_BUFFER_DATA: {
                OPTICK_EVENT("READ_BUFFER_DATA");

                const GFX::ReadBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ReadBufferDataCommand>(cmd);
                if (crtCmd._buffer != nullptr && crtCmd._target != nullptr) {
                    crtCmd._buffer->readData(crtCmd._offsetElementCount, crtCmd._elementCount, crtCmd._target);
                }
            } break;
            case GFX::CommandType::CLEAR_BUFFER_DATA: {
                OPTICK_EVENT("CLEAR_BUFFER_DATA");

                const GFX::ClearBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ClearBufferDataCommand>(cmd);
                if (crtCmd._buffer != nullptr) {
                    crtCmd._buffer->clearData(crtCmd._offsetElementCount, crtCmd._elementCount);
                }
            } break;
            case GFX::CommandType::SET_VIEWPORT: {
                OPTICK_EVENT("SET_VIEWPORT");

                setViewport(commandBuffer.get<GFX::SetViewportCommand>(cmd)->_viewport);
            } break;
            case GFX::CommandType::PUSH_VIEWPORT: {
                OPTICK_EVENT("PUSH_VIEWPORT");

                const GFX::PushViewportCommand* crtCmd = commandBuffer.get<GFX::PushViewportCommand>(cmd);
                _viewportStack.push(_gpuBlock._camData._ViewPort);
                setViewport(crtCmd->_viewport);
            } break;
            case GFX::CommandType::POP_VIEWPORT: {
                OPTICK_EVENT("POP_VIEWPORT");

                setViewport(_viewportStack.top());
                _viewportStack.pop();
            } break;
            case GFX::CommandType::SET_CAMERA: {
                OPTICK_EVENT("SET_CAMERA");

                const GFX::SetCameraCommand* crtCmd = commandBuffer.get<GFX::SetCameraCommand>(cmd);
                // Tell the Rendering API to draw from our desired PoV
                renderFromCamera(crtCmd->_cameraSnapshot);
            } break;
            case GFX::CommandType::PUSH_CAMERA: {
                OPTICK_EVENT("PUSH_CAMERA");

                const GFX::PushCameraCommand* crtCmd = commandBuffer.get<GFX::PushCameraCommand>(cmd);
                DIVIDE_ASSERT(_cameraSnapshots.size() < _cameraSnapshots._Get_container().max_size(), "GFXDevice::flushCommandBuffer error: PUSH_CAMERA stack too deep!");

                _cameraSnapshots.push(_activeCameraSnapshot);
                renderFromCamera(crtCmd->_cameraSnapshot);
            } break;
            case GFX::CommandType::POP_CAMERA: {
                OPTICK_EVENT("POP_CAMERA");

                renderFromCamera(_cameraSnapshots.top());
                _cameraSnapshots.pop();
            } break;
            case GFX::CommandType::SET_CLIP_PLANES: {
                OPTICK_EVENT("SET_CLIP_PLANES");

                setClipPlanes(commandBuffer.get<GFX::SetClipPlanesCommand>(cmd)->_clippingPlanes);
            } break;
            case GFX::CommandType::EXTERNAL: {
                OPTICK_EVENT("EXTERNAL");

                bindDescriptorSet(uploadGPUBlock());
                commandBuffer.get<GFX::ExternalCommand>(cmd)->_cbk();
            } break;

            case GFX::CommandType::BIND_DESCRIPTOR_SETS: {
                OPTICK_EVENT("BIND_DESCRIPTOR_SETS");
                bindDescriptorSet(commandBuffer.get<GFX::BindDescriptorSetsCommand>(cmd)->_set);
            } break;
            case GFX::CommandType::BEGIN_RENDER_PASS: {
                const GFX::BeginRenderPassCommand* crtCmd = commandBuffer.get<GFX::BeginRenderPassCommand>(cmd);
                const vec2<F32> depthRange = renderTargetPool().renderTarget(crtCmd->_target).getDepthRange();
                setDepthRange(depthRange);
            } [[fallthrough]];
            case GFX::CommandType::DRAW_TEXT:
            case GFX::CommandType::DRAW_IMGUI:
            case GFX::CommandType::DRAW_COMMANDS:
            case GFX::CommandType::DISPATCH_COMPUTE: {
                bindDescriptorSet(uploadGPUBlock());
            } [[fallthrough]];
            default: break;
        }

        _api->flushCommand(cmd, commandBuffer);
    }

    _api->postFlushCommandBuffer(commandBuffer);
}

/// Transform our depth buffer to a HierarchicalZ buffer (for occlusion queries and screen space reflections)
/// Based on RasterGrid implementation: http://rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/
/// Modified with nVidia sample code: https://github.com/nvpro-samples/gl_occlusion_culling
std::pair<const Texture_ptr&, size_t> GFXDevice::constructHIZ(RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut) {
    OPTICK_EVENT();

    assert(depthBuffer != HiZTarget);

    // The depth buffer's resolution should be equal to the screen's resolution
    RenderTarget& renderTarget = _rtPool->renderTarget(HiZTarget);
    const U16 width = renderTarget.getWidth();
    const U16 height = renderTarget.getHeight();
    U16 level = 0;
    U16 dim = width > height ? width : height;

    // Store the current width and height of each mip
    const Rect<I32> previousViewport(_gpuBlock._camData._ViewPort);

    GFX::EnqueueCommand(cmdBufferInOut, GFX::BeginDebugScopeCommand{ "Construct Hi-Z" });

    GFX::EnqueueCommand<GFX::ClearRenderTargetCommand>(cmdBufferInOut)->_target = HiZTarget;
    { // Copy depth buffer to the colour target for compute shaders to use later on

        GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(cmdBufferInOut);
        beginRenderPassCmd->_target = HiZTarget;
        beginRenderPassCmd->_name = "CONSTRUCT_HI_Z_DEPTH";
        

        const auto& att = _rtPool->renderTarget(depthBuffer).getAttachment(RTAttachmentType::Depth, 0);
        drawTextureInViewport(att.texture()->data(),
                              att.samplerHash(),
                              Rect<I32>{0, 0, renderTarget.getWidth(), renderTarget.getHeight()},
                              false,
                              true,
                              false,
                              cmdBufferInOut);

        GFX::EnqueueCommand(cmdBufferInOut, GFX::EndRenderPassCommand{});
    }

    const RTAttachment& att = renderTarget.getAttachment(RTAttachmentType::Depth, 0);
    const Texture_ptr& hizDepthTex = att.texture();

    const TextureData& hizData = hizDepthTex->data();
    DIVIDE_ASSERT(hizDepthTex->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::MANUAL);

    // We use a special shader that downsamples the buffer
    // We will use a state block that disables colour writes as we will render only a depth image,
    // disables depth testing but allows depth writes
    GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(cmdBufferInOut);
    beginRenderPassCmd->_name = "CONSTRUCT_HI_Z";
    beginRenderPassCmd->_target = HiZTarget;
    beginRenderPassCmd->_descriptor._setViewport = false;
    DisableAll(beginRenderPassCmd->_descriptor._drawMask);
    SetEnabled(beginRenderPassCmd->_descriptor._drawMask, RTAttachmentType::Depth, 0, true);

    GFX::EnqueueCommand(cmdBufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });

    GFX::EnqueueCommand(cmdBufferInOut, GFX::BindPipelineCommand{ _HIZPipeline });

    // for i > 0, use texture views?
    GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(cmdBufferInOut)->_set._textureData.add(TextureEntry{ hizData, att.samplerHash(), TextureUsage::DEPTH });

    // We skip the first level as that's our full resolution image
    U16 twidth = width;
    U16 theight = height;
    bool wasEven = false;
    U16 owidth = twidth;
    U16 oheight = theight;
    while (dim) {
        if (level) {
            twidth = twidth < 1 ? 1 : twidth;
            theight = theight < 1 ? 1 : theight;

            // Bind next mip level for rendering but first restrict fetches only to previous level
            GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(cmdBufferInOut)->_mipWriteLevel = level;

            // Update the viewport with the new resolution
            GFX::EnqueueCommand<GFX::SetViewportCommand>(cmdBufferInOut)->_viewport.set(0, 0, twidth, theight);
            PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(cmdBufferInOut)->_constants;
            constants.set(_ID("depthInfo"), GFX::PushConstantType::IVEC2, vec2<I32>(level - 1, wasEven ? 1 : 0));

            // Dummy draw command as the full screen quad is generated completely in the vertex shader
            GFX::EnqueueCommand<GFX::DrawCommand>(cmdBufferInOut);

            GFX::EnqueueCommand<GFX::EndRenderSubPassCommand>(cmdBufferInOut);
        }

        // Calculate next viewport size
        wasEven = twidth % 2 == 0 && theight % 2 == 0;
        dim /= 2;
        owidth = twidth;
        oheight = theight;
        twidth /= 2;
        theight /= 2;
        level++;
    }

    GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(cmdBufferInOut)->_mipWriteLevel = 0u;      // Restore mip level
    GFX::EnqueueCommand<GFX::EndRenderSubPassCommand>(cmdBufferInOut);
    GFX::EnqueueCommand<GFX::SetViewportCommand>(cmdBufferInOut)->_viewport.set(previousViewport); // Restore viewport
    GFX::EnqueueCommand<GFX::PopCameraCommand>(cmdBufferInOut);                                    // Restore camera
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(cmdBufferInOut);
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(cmdBufferInOut);

    return { hizDepthTex, att.samplerHash() };
}

void GFXDevice::occlusionCull(const RenderPass::BufferData& bufferData,
                              const Texture_ptr& depthBuffer,
                              const size_t samplerHash,
                              const CameraSnapshot& cameraSnapshot,
                              const bool countCulledNodes,
                              GFX::CommandBuffer& bufferInOut)
{
    OPTICK_EVENT();
    ShaderBuffer* cullBuffer = _gfxBuffers.crtBuffers()._cullCounter.get();

    const U32 cmdCount = *bufferData._lastCommandCount;
    const U32 threadCount = (cmdCount + GROUP_SIZE_AABB - 1) / GROUP_SIZE_AABB;
    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Occlusion Cull" });

    // Not worth the overhead for a handful of items and the Pre-Z pass should handle overdraw just fine
    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _HIZCullPipeline });

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._textureData.add(TextureEntry{ depthBuffer->data(), samplerHash, TextureUsage::UNIT0 });

    ShaderBufferBinding atomicCount = {};
    atomicCount._binding = ShaderBufferLocation::ATOMIC_COUNTER_0;
    atomicCount._buffer = cullBuffer;
    atomicCount._elementRange.set(0, 1);
    atomicCount._lockType = ShaderBufferLockType::AFTER_COMMAND_BUFFER_FLUSH;
    set._buffers.add(atomicCount); // Atomic counter should be cleared by this point

    mat4<F32> viewProjectionMatrix;
    mat4<F32>::Multiply(cameraSnapshot._viewMatrix, cameraSnapshot._projectionMatrix, viewProjectionMatrix);

    GFX::SendPushConstantsCommand HIZPushConstantsCMD = {};
    HIZPushConstantsCMD._constants.set(_ID("countCulledItems"), GFX::PushConstantType::UINT, countCulledNodes ? 1u : 0u);
    HIZPushConstantsCMD._constants.set(_ID("numEntities"), GFX::PushConstantType::UINT, cmdCount);
    HIZPushConstantsCMD._constants.set(_ID("nearPlane"), GFX::PushConstantType::FLOAT, cameraSnapshot._zPlanes.x);
    HIZPushConstantsCMD._constants.set(_ID("viewSize"), GFX::PushConstantType::VEC2, vec2<F32>(depthBuffer->width(), depthBuffer->height()));
    HIZPushConstantsCMD._constants.set(_ID("viewMatrix"), GFX::PushConstantType::MAT4, cameraSnapshot._viewMatrix);
    HIZPushConstantsCMD._constants.set(_ID("viewProjectionMatrix"), GFX::PushConstantType::MAT4, viewProjectionMatrix);
    HIZPushConstantsCMD._constants.set(_ID("frustumPlanes"), GFX::PushConstantType::VEC4, cameraSnapshot._frustumPlanes);

    GFX::EnqueueCommand(bufferInOut, HIZPushConstantsCMD);

    GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ threadCount, 1, 1 });

    // Occlusion culling barrier
    GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{
        to_base(MemoryBarrierType::COMMAND_BUFFER) | //For rendering
        to_base(MemoryBarrierType::SHADER_STORAGE) | //For updating later on
        (countCulledNodes ? to_base(MemoryBarrierType::BUFFER_UPDATE) : 0u)
        });

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    if (queryPerformanceStats() && countCulledNodes) {
        GFX::ReadBufferDataCommand readAtomicCounter;
        readAtomicCounter._buffer = cullBuffer;
        readAtomicCounter._target = &_lastCullCount;
        readAtomicCounter._offsetElementCount = 0;
        readAtomicCounter._elementCount = 1;
        EnqueueCommand(bufferInOut, readAtomicCounter);

        cullBuffer->incQueue();

        GFX::ClearBufferDataCommand clearAtomicCounter{};
        clearAtomicCounter._buffer = cullBuffer;
        clearAtomicCounter._offsetElementCount = 0;
        clearAtomicCounter._elementCount = 1;
        GFX::EnqueueCommand(bufferInOut, clearAtomicCounter);
    }
}
#pragma endregion

#pragma region Drawing functions
void GFXDevice::drawText(const TextElementBatch& batch, GFX::CommandBuffer& bufferInOut, const bool pushCamera) const {
    drawText(GFX::DrawTextCommand{ batch }, bufferInOut, pushCamera);
}

void GFXDevice::drawText(const GFX::DrawTextCommand& cmd, GFX::CommandBuffer& bufferInOut, const bool pushCamera) const {
    if (pushCamera) {
        EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    }
    EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _textRenderPipeline });
    EnqueueCommand(bufferInOut, GFX::SendPushConstantsCommand{ _textRenderConstants });
    EnqueueCommand(bufferInOut, cmd);
    if (pushCamera) {
        EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
    }
}

void GFXDevice::drawTextureInViewport(const TextureData data, const size_t samplerHash, const Rect<I32>& viewport, const bool convertToSrgb, const bool drawToDepthOnly, bool drawBlend, GFX::CommandBuffer& bufferInOut) {
    static GFX::BeginDebugScopeCommand   s_beginDebugScopeCmd    { "Draw Texture In Viewport" };
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBTrue { PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, true}}};
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBFalse{ PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, false}}};

    GFX::EnqueueCommand(bufferInOut, s_beginDebugScopeCmd);
    GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    GFX::EnqueueCommand(bufferInOut, drawToDepthOnly ? _drawFSDepthPipelineCmd : drawBlend ? _drawFSTexturePipelineBlendCmd : _drawFSTexturePipelineCmd);
    GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ data, samplerHash, TextureUsage::UNIT0 });
    GFX::EnqueueCommand(bufferInOut, GFX::PushViewportCommand{ viewport });

    if (!drawToDepthOnly) {
        GFX::EnqueueCommand(bufferInOut, convertToSrgb ? s_pushConstantsSRGBTrue : s_pushConstantsSRGBFalse);
    }

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
    GFX::EnqueueCommand(bufferInOut, GFX::PopViewportCommand{});
    GFX::EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}
#pragma endregion

#pragma region Debug utilities
void GFXDevice::renderDebugUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) {
    constexpr I32 padding = 5;

    // Early out if we didn't request the preview
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Render Debug Views" });

        renderDebugViews(
            Rect<I32>(targetViewport.x + padding,
                      targetViewport.y + padding,
                      targetViewport.z - padding,
                      targetViewport.w - padding),
            padding,
            bufferInOut);

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
}

void GFXDevice::initDebugViews() {
    // Lazy-load preview shader
    if (!_previewDepthMapShader) {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "fbPreview.glsl";
        fragModule._variant = "LinearDepth";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        // The LinearDepth variant converts the depth values to linear values between the 2 scene z-planes
        ResourceDescriptor fbPreview("fbPreviewLinearDepth");
        fbPreview.propertyDescriptor(shaderDescriptor);
        _previewDepthMapShader = CreateResource<ShaderProgram>(parent().resourceCache(), fbPreview);
        assert(_previewDepthMapShader != nullptr);

        DebugView_ptr HiZ = std::make_shared<DebugView>();
        HiZ->_shader = _previewDepthMapShader;
        HiZ->_texture = renderTargetPool().renderTarget(RenderTargetUsage::HI_Z).getAttachment(RTAttachmentType::Depth, 0).texture();
        HiZ->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::HI_Z).getAttachment(RTAttachmentType::Depth, 0).samplerHash();
        HiZ->_name = "Hierarchical-Z";
        HiZ->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.f);
        HiZ->_shaderData.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, vec2<F32>(Camera::s_minNearZ, _context.config().runtime.cameraViewDistance));
        HiZ->_cycleMips = true;

        DebugView_ptr DepthPreview = std::make_shared<DebugView>();
        DepthPreview->_shader = _previewDepthMapShader;
        DepthPreview->_texture = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Depth, 0).texture();
        DepthPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Depth, 0).samplerHash();
        DepthPreview->_name = "Depth Buffer";
        DepthPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        DepthPreview->_shaderData.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, vec2<F32>(Camera::s_minNearZ, _context.config().runtime.cameraViewDistance));

        DebugView_ptr NormalPreview = std::make_shared<DebugView>();
        NormalPreview->_shader = _renderTargetDraw;
        NormalPreview->_texture = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS)).texture();
        NormalPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS)).samplerHash();
        NormalPreview->_name = "Normals";
        NormalPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        NormalPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, true);
        NormalPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        NormalPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 2u);
        NormalPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);  
        
        DebugView_ptr VelocityPreview = std::make_shared<DebugView>();
        VelocityPreview->_shader = _renderTargetDraw;
        VelocityPreview->_texture = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY)).texture();
        VelocityPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY)).samplerHash();
        VelocityPreview->_name = "Velocity Map";
        VelocityPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        VelocityPreview->_shaderData.set(_ID("scaleAndBias"), GFX::PushConstantType::BOOL, true);
        VelocityPreview->_shaderData.set(_ID("normalizeOutput"), GFX::PushConstantType::BOOL, true);
        VelocityPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        VelocityPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        VelocityPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 2u);
        VelocityPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 5.0f);

        DebugView_ptr SSAOPreview = std::make_shared<DebugView>();
        SSAOPreview->_shader = _renderTargetDraw;
        SSAOPreview->_texture = renderTargetPool().renderTarget(RenderTargetUsage::SSAO_RESULT).getAttachment(RTAttachmentType::Colour, 0u).texture();
        SSAOPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::SSAO_RESULT).getAttachment(RTAttachmentType::Colour, 0u).samplerHash();
        SSAOPreview->_name = "SSAO Map";
        SSAOPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        SSAOPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        SSAOPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        SSAOPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        SSAOPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaAccumulationHigh = std::make_shared<DebugView>();
        AlphaAccumulationHigh->_shader = _renderTargetDraw;
        AlphaAccumulationHigh->_texture = renderTargetPool().renderTarget(RenderTargetUsage::OIT).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO)).texture();
        AlphaAccumulationHigh->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::OIT).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO)).samplerHash();
        AlphaAccumulationHigh->_name = "Alpha Accumulation High";
        AlphaAccumulationHigh->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        AlphaAccumulationHigh->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 4u);
        AlphaAccumulationHigh->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaRevealageHigh = std::make_shared<DebugView>();
        AlphaRevealageHigh->_shader = _renderTargetDraw;
        AlphaRevealageHigh->_texture = renderTargetPool().renderTarget(RenderTargetUsage::OIT).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE)).texture();
        AlphaRevealageHigh->_samplerHash = renderTargetPool().renderTarget(RenderTargetUsage::OIT).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE)).samplerHash();
        AlphaRevealageHigh->_name = "Alpha Revealage High";
        AlphaRevealageHigh->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        AlphaRevealageHigh->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        AlphaRevealageHigh->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        AlphaRevealageHigh->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        AlphaRevealageHigh->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        SamplerDescriptor lumanSampler = {};
        lumanSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        lumanSampler.minFilter(TextureFilter::NEAREST);
        lumanSampler.magFilter(TextureFilter::NEAREST);
        lumanSampler.anisotropyLevel(0);

        DebugView_ptr Luminance = std::make_shared<DebugView>();
        Luminance->_shader = _renderTargetDraw;
        Luminance->_texture = getRenderer().postFX().getFilterBatch().luminanceTex();
        Luminance->_samplerHash = lumanSampler.getHash();
        Luminance->_name = "Luminance";
        Luminance->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        Luminance->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        Luminance->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        Luminance->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        Luminance->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr Edges = std::make_shared<DebugView>();
        Edges->_shader = _renderTargetDraw;
        Edges->_texture = renderTargetPool().renderTarget(getRenderer().postFX().getFilterBatch().edgesRT()).getAttachment(RTAttachmentType::Colour, 0u).texture();
        Edges->_samplerHash = renderTargetPool().renderTarget(getRenderer().postFX().getFilterBatch().edgesRT()).getAttachment(RTAttachmentType::Colour, 0u).samplerHash();
        Edges->_name = "Edges";
        Edges->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        Edges->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        Edges->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        Edges->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 4u);
        Edges->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        addDebugView(HiZ);
        addDebugView(DepthPreview);
        addDebugView(NormalPreview);
        addDebugView(VelocityPreview);
        addDebugView(SSAOPreview);
        addDebugView(AlphaAccumulationHigh);
        addDebugView(AlphaRevealageHigh);
        addDebugView(Luminance);
        addDebugView(Edges);
        WAIT_FOR_CONDITION(_previewDepthMapShader->getState() == ResourceState::RES_LOADED);
    }
}

void GFXDevice::renderDebugViews(const Rect<I32> targetViewport, const I32 padding, GFX::CommandBuffer& bufferInOut) {
    static vector_fast<std::tuple<string, I32, Rect<I32>>> labelStack;
    static size_t labelStyleHash = TextLabelStyle(Font::DROID_SERIF_BOLD, UColour4(196), 96).getHash();

    initDebugViews();

    constexpr I32 columnCount = 6u;
    I32 viewCount = to_I32(_debugViews.size());
    for (const auto& view : _debugViews) {
        if (!view->_enabled) {
            --viewCount;
        }
    }

    if (viewCount == 0) {
        return;
    }

    labelStack.resize(0);

    I32 rowCount = viewCount / columnCount;
    if (viewCount % columnCount > 0) {
        rowCount++;
    }

    const I32 screenWidth = targetViewport.z - targetViewport.x;
    const I32 screenHeight = targetViewport.w - targetViewport.y;
    const F32 aspectRatio = to_F32(screenWidth) / screenHeight;

    const I32 viewportWidth = (screenWidth / columnCount) - (padding * (columnCount - 1u));
    const I32 viewportHeight = to_I32(viewportWidth / aspectRatio) - padding;
    Rect<I32> viewport(targetViewport.z - viewportWidth, targetViewport.y, viewportWidth, viewportHeight);

    const I32 initialOffsetX = viewport.x;

    PipelineDescriptor pipelineDesc{};
    pipelineDesc._stateHash = _state2DRenderingHash;
    pipelineDesc._shaderProgramHandle = ShaderProgram::INVALID_HANDLE;

    const Rect<I32> previousViewport{
        to_I32(_gpuBlock._camData._ViewPort.x),
        to_I32(_gpuBlock._camData._ViewPort.y),
        to_I32(_gpuBlock._camData._ViewPort.z),
        to_I32(_gpuBlock._camData._ViewPort.w)
    };

    Pipeline* crtPipeline = nullptr;
    U16 idx = 0u;
    const I32 mipTimer = to_I32(std::ceil(Time::Game::ElapsedMilliseconds() / 750.0f));
    for (U16 i = 0; i < to_U16(_debugViews.size()); ++i) {
        if (!_debugViews[i]->_enabled) {
            continue;
        }

        const DebugView_ptr& view = _debugViews[i];

        if (view->_cycleMips) {
            const F32 lodLevel = to_F32(mipTimer % view->_texture->mipCount());
            view->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, lodLevel);
            labelStack.emplace_back(Util::StringFormat("Mip level: %d", to_U8(lodLevel)), viewport.sizeY * 4, viewport);
        }
        const ShaderProgram::Handle crtShader = pipelineDesc._shaderProgramHandle;
        const ShaderProgram::Handle newShader = view->_shader->handle();
        if (crtShader != newShader) {
            pipelineDesc._shaderProgramHandle = view->_shader->handle();
            crtPipeline = newPipeline(pipelineDesc);
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = crtPipeline;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants = view->_shaderData;
        GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport.set(viewport);
        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry
        {
            view->_texture->data(),
            view->_samplerHash,
            view->_textureBindSlot
        });

        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

        if (!view->_name.empty()) {
            labelStack.emplace_back(view->_name, viewport.sizeY, viewport);
        }
        if (idx > 0 &&  idx % (columnCount - 1) == 0) {
            viewport.y += viewportHeight + targetViewport.y;
            viewport.x = initialOffsetX;
            idx = 0u;
        } else {
            viewport.x -= viewportWidth + targetViewport.x;
            ++idx;
        }
    }

    GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    // Draw labels at the end to reduce number of state changes
    TextElement text(labelStyleHash, RelativePosition2D(RelativeValue(0.1f, 0.0f), RelativeValue(0.1f, 0.0f)));
    for (const auto& [labelText, viewportOffsetY, viewportIn] : labelStack) {
        GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport.set(viewportIn);

        text.position().d_y.d_offset = to_F32(viewportOffsetY);
        text.text(labelText.c_str(), false);
        const TextElementBatch batch{ text };
        drawText(GFX::DrawTextCommand{ batch }, bufferInOut, false);
    }
    GFX::EnqueueCommand<GFX::PopCameraCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport.set(previousViewport);
}

DebugView* GFXDevice::addDebugView(const std::shared_ptr<DebugView>& view) {
    ScopedLock<Mutex> lock(_debugViewLock);

    _debugViews.push_back(view);

    if (_debugViews.back()->_sortIndex == -1) {
        _debugViews.back()->_sortIndex = to_I16(_debugViews.size());
    }

    eastl::sort(eastl::begin(_debugViews),
                eastl::end(_debugViews),
                [](const std::shared_ptr<DebugView>& a, const std::shared_ptr<DebugView>& b) noexcept -> bool {
                    if (a->_groupID == b->_groupID) {
                        return a->_sortIndex < b->_sortIndex;
                    }  
                    if (a->_sortIndex == b->_sortIndex) {
                        return a->_groupID < b->_groupID;
                    }

                    return a->_groupID < b->_groupID && a->_sortIndex < b->_sortIndex;
                });

    return view.get();
}

bool GFXDevice::removeDebugView(DebugView* view) {
    return dvd_erase_if(_debugViews,
                        [view](const std::shared_ptr<DebugView>& entry) noexcept {
                           return view != nullptr && view->getGUID() == entry->getGUID();
                        });
}

void GFXDevice::toggleDebugView(const I16 index, const bool state) {
    ScopedLock<Mutex> lock(_debugViewLock);
    for (auto& view : _debugViews) {
        if (view->_sortIndex == index) {
            view->_enabled = state;
            break;
        }
    }
}

void GFXDevice::toggleDebugGroup(I16 group, const bool state) {
    ScopedLock<Mutex> lock(_debugViewLock);
    for (auto& view : _debugViews) {
        if (view->_groupID == group) {
            view->_enabled = state;
        }
    }
}

bool GFXDevice::getDebugGroupState(I16 group) const {
    ScopedLock<Mutex> lock(_debugViewLock);
    for (const auto& view : _debugViews) {
        if (view->_groupID == group) {
            if (!view->_enabled) {
                return false;
            }
        }
    }

    return true;
}

void GFXDevice::getDebugViewNames(vector<std::tuple<string, I16, I16, bool>>& namesOut) {
    namesOut.resize(0);

    ScopedLock<Mutex> lock(_debugViewLock);
    for (auto& view : _debugViews) {
        namesOut.emplace_back(view->_name, view->_groupID, view->_sortIndex, view->_enabled);
    }
}

Pipeline* GFXDevice::getDebugPipeline(const IMPrimitive::BaseDescriptor& descriptor) const noexcept {
    if (descriptor.noDepth) {
        return (descriptor.noCull ? _debugGizmoPipelineNoCullNoDepth : _debugGizmoPipelineNoDepth);
    } else if (descriptor.noCull) {
        return _debugGizmoPipelineNoCull;
    }

    return _debugGizmoPipeline;
}

void GFXDevice::debugDrawLines(const I64 ID, const IMPrimitive::LineDescriptor descriptor) noexcept {
    _debugLines.add(ID, descriptor);
}

void GFXDevice::debugDrawLines(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugLines._dataLock);

    const size_t lineCount = _debugLines.size();
    for (size_t f = 0u; f < lineCount; ++f) {
        auto& data = _debugLines._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& linePrimitive = _debugLines._debugPrimitives[f];
        if (linePrimitive == nullptr) {
            linePrimitive = newIMP();
            linePrimitive->name(Util::StringFormat("DebugLine_%d", f));
        }

        linePrimitive->forceWireframe(data._descriptor.wireframe); //? Uhm, not gonna do much -Ionut
        linePrimitive->pipeline(*getDebugPipeline(data._descriptor));
        linePrimitive->worldMatrix(data._descriptor.worldMatrix);
        linePrimitive->fromLines(data._descriptor);
        bufferInOut.add(linePrimitive->toCommandBuffer());
    }
}

void GFXDevice::debugDrawBox(const I64 ID, const IMPrimitive::BoxDescriptor descriptor) noexcept {
    _debugBoxes.add(ID, descriptor);
}

void GFXDevice::debugDrawBoxes(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugBoxes._dataLock);
    const size_t boxesCount = _debugBoxes.size();
    for (U32 f = 0u; f < boxesCount; ++f) {
        auto& data = _debugBoxes._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& boxPrimitive = _debugBoxes._debugPrimitives[f];
        if (boxPrimitive == nullptr) {
            boxPrimitive = newIMP();
            boxPrimitive->name(Util::StringFormat("DebugBox_%d", f));
        }

        boxPrimitive->forceWireframe(data._descriptor.wireframe);
        boxPrimitive->pipeline(*getDebugPipeline(data._descriptor));
        boxPrimitive->worldMatrix(data._descriptor.worldMatrix);
        boxPrimitive->fromBox(data._descriptor);
        bufferInOut.add(boxPrimitive->toCommandBuffer());
    }
}

void GFXDevice::debugDrawOBB(const I64 ID, const IMPrimitive::OBBDescriptor descriptor) noexcept {
    _debugOBBs.add(ID, descriptor);
}

void GFXDevice::debugDrawOBBs(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugOBBs._dataLock);
    const size_t boxesCount = _debugOBBs.size();
    for (U32 f = 0u; f < boxesCount; ++f) {
        auto& data = _debugOBBs._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& boxPrimitive = _debugOBBs._debugPrimitives[f];
        if (boxPrimitive == nullptr) {
            boxPrimitive = newIMP();
            boxPrimitive->name(Util::StringFormat("DebugOBB_%d", f));
        }

        boxPrimitive->forceWireframe(data._descriptor.wireframe);
        boxPrimitive->pipeline(*getDebugPipeline(data._descriptor));
        boxPrimitive->worldMatrix(data._descriptor.worldMatrix);
        boxPrimitive->fromOBB(data._descriptor);
        bufferInOut.add(boxPrimitive->toCommandBuffer());
    }
}
void GFXDevice::debugDrawSphere(const I64 ID, const IMPrimitive::SphereDescriptor descriptor) noexcept {
    _debugSpheres.add(ID, descriptor);
}

void GFXDevice::debugDrawSpheres(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugSpheres._dataLock);
    const size_t spheresCount = _debugSpheres.size();
    for (size_t f = 0u; f < spheresCount; ++f) {
        auto& data = _debugSpheres._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& spherePrimitive = _debugSpheres._debugPrimitives[f];
        if (spherePrimitive == nullptr) {
            spherePrimitive = newIMP();
            spherePrimitive->name(Util::StringFormat("DebugSphere_%d", f));
        }

        spherePrimitive->forceWireframe(data._descriptor.wireframe);
        spherePrimitive->pipeline(*getDebugPipeline(data._descriptor));
        spherePrimitive->worldMatrix(data._descriptor.worldMatrix);
        spherePrimitive->fromSphere(data._descriptor);
        bufferInOut.add(spherePrimitive->toCommandBuffer());
    }
}

void GFXDevice::debugDrawCone(const I64 ID, const IMPrimitive::ConeDescriptor descriptor) noexcept {
    _debugCones.add(ID, descriptor);
}

void GFXDevice::debugDrawCones(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugCones._dataLock);

    const size_t conesCount = _debugCones.size();
    for (size_t f = 0u; f < conesCount; ++f) {
        auto& data = _debugCones._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& conePrimitive = _debugCones._debugPrimitives[f];
        if (conePrimitive == nullptr) {
            conePrimitive = newIMP();
            conePrimitive->name(Util::StringFormat("DebugCone_%d", f));
        }

        conePrimitive->forceWireframe(data._descriptor.wireframe);
        conePrimitive->pipeline(*getDebugPipeline(data._descriptor));
        conePrimitive->worldMatrix(data._descriptor.worldMatrix);
        conePrimitive->fromCone(data._descriptor);
        bufferInOut.add(conePrimitive->toCommandBuffer());
    }
}

void GFXDevice::debugDrawFrustum(const I64 ID, const IMPrimitive::FrustumDescriptor descriptor) noexcept {
    _debugFrustums.add(ID, descriptor);
}

void GFXDevice::debugDrawFrustums(GFX::CommandBuffer& bufferInOut) {
    ScopedLock<Mutex> r_lock(_debugFrustums._dataLock);

    const size_t frustumCount = _debugFrustums.size();
    for (size_t f = 0u; f < frustumCount; ++f) {
        auto& data = _debugFrustums._debugData[f];
        if (data._frameLifeTime == 0u) {
            continue;
        }

        IMPrimitive*& frustumPrimitive = _debugFrustums._debugPrimitives[f];
        if (frustumPrimitive == nullptr) {
            frustumPrimitive = newIMP();
            frustumPrimitive->name(Util::StringFormat("DebugFrustum_%d", f));
        }

        frustumPrimitive->forceWireframe(data._descriptor.wireframe);
        frustumPrimitive->pipeline(*getDebugPipeline(data._descriptor));
        frustumPrimitive->worldMatrix(data._descriptor.worldMatrix);
        frustumPrimitive->fromFrustum(data._descriptor);
        bufferInOut.add(frustumPrimitive->toCommandBuffer());
    }
}

/// Render all of our immediate mode primitives. This isn't very optimised and most are recreated per frame!
void GFXDevice::debugDraw(const SceneRenderState& sceneRenderState, GFX::CommandBuffer& bufferInOut) {
    debugDrawFrustums(bufferInOut);
    debugDrawLines(bufferInOut);
    debugDrawBoxes(bufferInOut);
    debugDrawOBBs(bufferInOut);
    debugDrawSpheres(bufferInOut);
    debugDrawCones(bufferInOut);
}
#pragma endregion

#pragma region GPU Object instantiation
GenericVertexData* GFXDevice::getOrCreateIMGUIBuffer(const I64 windowGUID, const I32 maxCommandCount) {
    const U32 newSize = to_U32(maxCommandCount * RenderPass::DataBufferRingSize);

    GenericVertexData* ret = nullptr;

    const auto it = _IMGUIBuffers.find(windowGUID);
    if (it != eastl::cend(_IMGUIBuffers)) {
        // If we need more space, skip this and just create a new, larger, buffer.
        if (it->second->queueLength() >= newSize) {
            return it->second.get();
        } else {
            ret = it->second.get();
            ret->reset();
            ret->resize(newSize);
        }
    }

    if (ret == nullptr) {
        GenericVertexData_ptr newBuffer = newGVD(newSize);
        _IMGUIBuffers[windowGUID] = newBuffer;
        ret = newBuffer.get();
    }

    GenericVertexData::IndexBuffer idxBuff;
    idxBuff.smallIndices = sizeof(ImDrawIdx) == sizeof(U16);
    idxBuff.count = (1 << 16) * 3;
    idxBuff.dynamic = true;

    ret->create(1);
    ret->renderIndirect(false);

    GenericVertexData::SetBufferParams params = {};
    params._buffer = 0;
    params._useRingBuffer = true;

    params._bufferParams._elementCount = 1 << 16;
    params._bufferParams._elementSize = sizeof(ImDrawVert);
    params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
    params._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    params._bufferParams._initialData = { nullptr, 0 };

    ret->setBuffer(params); //Pos, UV and Colour
    ret->setIndexBuffer(idxBuff);

    return ret;
}

RenderTarget_ptr GFXDevice::newRTInternal(const RenderTargetDescriptor& descriptor) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return std::make_shared<glFramebuffer>(*this, descriptor);
        } break;
        case RenderAPI::Vulkan: {
            return std::make_shared<vkRenderTarget>(*this, descriptor);
        } break;
        case RenderAPI::None: {
            return std::make_shared<noRenderTarget>(*this, descriptor);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));

    return {};
}

RenderTarget_ptr GFXDevice::newRT(const RenderTargetDescriptor& descriptor) {
    RenderTarget_ptr temp = newRTInternal(descriptor);

    bool valid = false;
    if (temp != nullptr) {
        valid = temp->create();
        assert(valid);
    }

    return valid ? temp : nullptr;
}

IMPrimitive* GFXDevice::newIMP() {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return GL_API::NewIMP(_imprimitiveMutex , *this);
        };
        case RenderAPI::Vulkan: {
            ScopedLock<Mutex> w_lock(_imprimitiveMutex);
            return MemoryManager_NEW vkIMPrimitive(*this);
        }
        case RenderAPI::None: {
            ScopedLock<Mutex> w_lock(_imprimitiveMutex);
            return MemoryManager_NEW noIMPrimitive(*this);
        };
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    return nullptr;
}

bool GFXDevice::destroyIMP(IMPrimitive*& primitive) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return GL_API::DestroyIMP(_imprimitiveMutex , primitive);
        };
        case RenderAPI::Vulkan:
        case RenderAPI::None: {
            ScopedLock<Mutex> w_lock(_imprimitiveMutex);
            MemoryManager::SAFE_DELETE(primitive);
            return true;
        };
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    return false;
}

VertexBuffer_ptr GFXDevice::newVB() {
    return std::make_shared<VertexBuffer>(*this);
}

PixelBuffer_ptr GFXDevice::newPB(const PBType type, const char* name) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return std::make_shared<glPixelBuffer>(*this, type, name);
        } break;
        case RenderAPI::Vulkan: {
            return std::make_shared<vkPixelBuffer>(*this, type, name);
        } break;
        case RenderAPI::None: {
            return std::make_shared<noPixelBuffer>(*this, type, name);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));

    return {};
}

GenericVertexData_ptr GFXDevice::newGVD(const U32 ringBufferLength, const char* name) {

    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return std::make_shared<glGenericVertexData>(*this, ringBufferLength, name);
        } break;
        case RenderAPI::Vulkan: {
            return std::make_shared<vkGenericVertexData>(*this, ringBufferLength, name);
        } break;
        case RenderAPI::None: {
            return std::make_shared<noGenericVertexData>(*this, ringBufferLength, name);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));

    return {};
}

Texture_ptr GFXDevice::newTexture(const size_t descriptorHash,
                                  const Str256& resourceName,
                                  const ResourcePath& assetNames,
                                  const ResourcePath& assetLocations,
                                  const TextureDescriptor& texDescriptor,
                                  ResourceCache& parentCache) 
{
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return std::make_shared<glTexture>(*this, descriptorHash, resourceName, assetNames, assetLocations, texDescriptor, parentCache);
        } break;
        case RenderAPI::Vulkan: {
            return std::make_shared<vkTexture>(*this, descriptorHash, resourceName, assetNames, assetLocations, texDescriptor, parentCache);
        } break;
        case RenderAPI::None: {
            return std::make_shared<noTexture>(*this, descriptorHash, resourceName, assetNames, assetLocations, texDescriptor, parentCache);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
    return {};
}


Pipeline* GFXDevice::newPipeline(const PipelineDescriptor& descriptor) {
    // Pipeline with no shader is no pipeline at all
    DIVIDE_ASSERT(descriptor._shaderProgramHandle != ShaderProgram::INVALID_HANDLE, "Missing shader handle during pipeline creation!");

    const size_t hash = GetHash(descriptor);

    ScopedLock<Mutex> lock(_pipelineCacheLock);
    const hashMap<size_t, Pipeline, NoHash<size_t>>::iterator it = _pipelineCache.find(hash);
    if (it == std::cend(_pipelineCache)) {
        return &insert(_pipelineCache, hash, Pipeline(descriptor)).first->second;
    }

    return &it->second;
}

ShaderProgram_ptr GFXDevice::newShaderProgram(const size_t descriptorHash,
                                              const Str256& resourceName,
                                              const Str256& assetName,
                                              const ResourcePath& assetLocation,
                                              const ShaderProgramDescriptor& descriptor,
                                              ResourceCache& parentCache) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return std::make_shared<glShaderProgram>(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, parentCache);
        } break;
        case RenderAPI::Vulkan: {
            return std::make_shared<vkShaderProgram>(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, parentCache);
        } break;
        case RenderAPI::None: {
            return std::make_shared<noShaderProgram>(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, parentCache);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));

    return {};
}

ShaderBuffer_uptr GFXDevice::newSB(const ShaderBufferDescriptor& descriptor) {

    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return eastl::make_unique<glUniformBuffer>(*this, descriptor);
        } break;
        case RenderAPI::Vulkan: {
            return eastl::make_unique<vkUniformBuffer>(*this, descriptor);
        } break;
        case RenderAPI::None: {
            return eastl::make_unique<noUniformBuffer>(*this, descriptor);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
    return {};
}
#pragma endregion

ShaderComputeQueue& GFXDevice::shaderComputeQueue() noexcept {
    assert(_shaderComputeQueue != nullptr);
    return *_shaderComputeQueue;
}

const ShaderComputeQueue& GFXDevice::shaderComputeQueue() const noexcept {
    assert(_shaderComputeQueue != nullptr);
    return *_shaderComputeQueue;
}

/// Extract the pixel data from the main render target's first colour attachment and save it as a TGA image
void GFXDevice::screenshot(const ResourcePath& filename) const {
    // Get the screen's resolution
    STUBBED("Screenshot should save the final render target after post processing, not the current screen target!");

    const RenderTarget& screenRT = _rtPool->screenTarget();
    const U16 width = screenRT.getWidth();
    const U16 height = screenRT.getHeight();
    const U8 numChannels = 3;

    static I32 savedImages = 0;
    // compute the new filename by adding the series number and the extension
    const ResourcePath newFilename(Util::StringFormat("Screenshots/%s_%d.tga", filename.c_str(), savedImages));

    // Allocate sufficiently large buffers to hold the pixel data
    const U32 bufferSize = width * height * numChannels;
    vector<U8> imageData(bufferSize, 0u);
    // Read the pixels from the main render target (RGBA16F)
    screenRT.readData(GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE, { (bufferPtr)imageData.data(), imageData.size() });
    // Save to file
    if (ImageTools::SaveImage(filename,
                              vec2<U16>(width, height),
                              numChannels,
                              imageData.data(), 
                              ImageTools::SaveImageFormat::PNG)) {
        ++savedImages;
    }
}

/// returns the standard state block
size_t GFXDevice::getDefaultStateBlock(const bool noDepth) const noexcept {
    return noDepth ? _defaultStateNoDepthHash : RenderStateBlock::DefaultHash();
}

};
