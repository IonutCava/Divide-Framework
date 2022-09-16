#include "stdafx.h"

#include "config.h"

#include "Headers/GFXDevice.h"
#include "Headers/GFXRTPool.h"
#include "Editor/Headers/Editor.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/SceneShaderData.h"

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
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Platform/Video/RenderBackend/None/Headers/NoneWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glShaderBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glGenericVertexData.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"

#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkRenderTarget.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkShaderBuffer.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkGenericVertexData.h"
#include "Platform/Video/RenderBackend/Vulkan/Shaders/Headers/vkShaderProgram.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

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
    constexpr size_t TargetBufferSizeCam = 1024u;
    constexpr size_t TargetBufferSizeRender = 64u;

    constexpr U32 GROUP_SIZE_AABB = 64u;
    constexpr U32 MAX_INVOCATIONS_BLUR_SHADER_LAYERED = 4u;
    constexpr U32 DEPTH_REDUCE_LOCAL_SIZE = 32u;

    FORCE_INLINE U32 getGroupCount(const U32 threadCount, U32 const localSize)
    {
        return (threadCount + localSize - 1u) / localSize;
    }
};

RenderTargetID RenderTargetNames::SCREEN = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::SCREEN_MS = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::SCREEN_PREV = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::OIT = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::OIT_MS = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::OIT_REFLECT = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::SSAO_RESULT = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::SSR_RESULT = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::HI_Z = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::HI_Z_REFLECT = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::REFLECTION_PLANAR_BLUR = INVALID_RENDER_TARGET_ID;
RenderTargetID RenderTargetNames::REFLECTION_CUBE = INVALID_RENDER_TARGET_ID;
std::array<RenderTargetID, Config::MAX_REFLECTIVE_NODES_IN_VIEW> RenderTargetNames::REFLECTION_PLANAR = create_array<Config::MAX_REFLECTIVE_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);
std::array<RenderTargetID, Config::MAX_REFRACTIVE_NODES_IN_VIEW> RenderTargetNames::REFRACTION_PLANAR = create_array<Config::MAX_REFRACTIVE_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);

D64 GFXDevice::s_interpolationFactor = 1.0;
U64 GFXDevice::s_frameCount = 0u;

DeviceInformation GFXDevice::s_deviceInformation{};
GFXDevice::IMPrimitivePool GFXDevice::s_IMPrimitivePool{};
#pragma region Construction, destruction, initialization

void GFXDevice::initDescriptorSets() {
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  0, DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::NONE);                 // CMD_BUFFER
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  1, DescriptorSetBindingType::UNIFORM_BUFFER,        ShaderStageVisibility::ALL);                  // CAM_BLOCK;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  2, DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::COMPUTE);              // GPU_COMMANDS;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  3, DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::ALL);                  // NODE_TRANSFORM_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  4, DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::ALL);                  // NODE_INDIRECTION_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_BATCH,  5, DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::FRAGMENT);             // NODE_MATERIAL_DATA;

    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   0, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // SCENE_NORMALS;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   1, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // DEPTH;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   2, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // TRANSMITANCE;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   3, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // SSR_SAMPLE;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   4, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // SSAO_SAMPLE;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   5, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_GEOMETRY); // TREE_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   6, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_GEOMETRY); // GRASS_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_PASS,   7, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE);              // ATOMIC_COUNTER;

    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  0, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // ENV Prefiltered
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  1, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // ENV Irradiance
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  2, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // BRDF Lut
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  3, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // Shadow Single
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  4, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // Shadow Array
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  5, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // Shadow Cube
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  6, DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::ALL_DRAW);             // SCENE_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  7, DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::FRAGMENT);             // PROBE_DATA;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  8, DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::COMPUTE_AND_DRAW);     // LIGHT_SCENE;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME,  9, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW);     // LIGHT_NORMAL;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME, 10, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW);     // LIGHT_GRID;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME, 11, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW);     // LIGHT_INDICES;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME, 12, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW);     // LIGHT_CLUSTER_AABBS;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME, 13, DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::FRAGMENT);             // LIGHT_SHADOW;
    ShaderProgram::RegisterSetLayoutBinding(DescriptorSetUsage::PER_FRAME, 14, DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT);             // Cube Reflection;
}

void GFXDevice::GFXDescriptorSet::clear() {
    _impl.resize(0);
    dirty(true);
}

void GFXDevice::GFXDescriptorSet::update(const DescriptorSetUsage usage, const DescriptorSetBinding& newBindingData) {
    for (DescriptorSetBinding& bindingEntry : _impl) {
        assert(bindingEntry._data.Type() != DescriptorSetBindingType::COUNT && 
               newBindingData._data.Type() != DescriptorSetBindingType::COUNT);

        if (bindingEntry._slot == newBindingData._slot) {
            DIVIDE_ASSERT(usage == DescriptorSetUsage::PER_DRAW || bindingEntry._data.Type() == newBindingData._data.Type());

            if (bindingEntry._data != newBindingData._data) {
                bindingEntry._data = newBindingData._data;
                dirty(true);
            }
            return;
        }
    }

    _impl.emplace_back(newBindingData);
    dirty(true);
}

GFXDevice::GFXDevice(Kernel& parent)
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

    if (s_deviceInformation._maxTextureUnits <= 16) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_TEXTURE_UNITS")));
        return ErrorCode::GFX_OLD_HARDWARE;
    }
    if (to_base(AttribLocation::COUNT) >= s_deviceInformation._maxVertAttributeBindings) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_ATTRIB_BINDS")));
        return ErrorCode::GFX_OLD_HARDWARE;
    }
    DIVIDE_ASSERT(Config::MAX_CLIP_DISTANCES <= s_deviceInformation._maxClipDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");
    DIVIDE_ASSERT(Config::MAX_CULL_DISTANCES <= s_deviceInformation._maxCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");
    DIVIDE_ASSERT(Config::MAX_CULL_DISTANCES + Config::MAX_CLIP_DISTANCES <= s_deviceInformation._maxClipAndCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");

    DIVIDE_ASSERT(Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS < s_deviceInformation._maxWorgroupSize[0] &&
                  Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS < s_deviceInformation._maxWorgroupSize[1] &&
                  Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < s_deviceInformation._maxWorgroupSize[2]);

    DIVIDE_ASSERT(to_U32(Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS) *
                  Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS *
                  Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < s_deviceInformation._maxWorgroupInvocations);
    
    const size_t displayCount = gpuState().getDisplayCount();
    string refreshRates;
    GPUState::GPUVideoMode prevMode;
    const auto printMode = [&prevMode, &refreshRates]() {
        Console::printfn(Locale::Get(_ID("CURRENT_DISPLAY_MODE")),
                                     prevMode._resolution.width,
                                     prevMode._resolution.height,
                                     prevMode._bitDepth,
                                     prevMode._formatName.c_str(),
                                     refreshRates.c_str());
    };

    for (size_t idx = 0; idx < displayCount; ++idx) {
        const vector<GPUState::GPUVideoMode>& registeredModes = gpuState().getDisplayModes(idx);
        if (!registeredModes.empty()) {
            Console::printfn(Locale::Get(_ID("AVAILABLE_VIDEO_MODES")), idx, registeredModes.size());

            prevMode = registeredModes.front();

            for (const GPUState::GPUVideoMode& it : registeredModes) {
                if (prevMode._resolution != it._resolution ||
                    prevMode._bitDepth != it._bitDepth ||
                    prevMode._formatName != it._formatName)
                {
                    printMode();
                    refreshRates = "";
                    prevMode = it;
                }

                if (refreshRates.empty()) {
                    refreshRates = Util::to_string(to_U32(it._refreshRate));
                } else {
                    refreshRates.append(Util::StringFormat(", %d", it._refreshRate));
                }
            }
        }
        if (!refreshRates.empty()) {
            printMode();
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

    initDescriptorSets();

    return ShaderProgram::OnStartup(parent().resourceCache());
}

GFX::MemoryBarrierCommand GFXDevice::updateSceneDescriptorSet(GFX::CommandBuffer& bufferInOut) const {
    return _sceneData->updateSceneDescriptorSet(bufferInOut);
}

void GFXDevice::resizeGPUBlocks(size_t targetSizeCam, size_t targetSizeCullCounter) {
    if (targetSizeCam == 0u) { targetSizeCam = 1u; }
    if (targetSizeCullCounter == 0u) { targetSizeCullCounter = 1u; }

    const bool resizeCamBuffer = _gfxBuffers.crtBuffers()._camDataBuffer == nullptr || _gfxBuffers.crtBuffers()._camDataBuffer->queueLength() != targetSizeCam;
    const bool resizeCullCounter = _gfxBuffers.crtBuffers()._cullCounter == nullptr || _gfxBuffers.crtBuffers()._cullCounter->queueLength() != targetSizeCullCounter;

    if (!resizeCamBuffer && !resizeCullCounter) {
        return;
    }

    DIVIDE_ASSERT(ValidateGPUDataStructure());

    _gfxBuffers.reset(resizeCamBuffer, resizeCullCounter);

    if (resizeCamBuffer) {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._ringBufferLength = to_U32(targetSizeCam);
        bufferDescriptor._bufferParams._elementSize = sizeof(GFXShaderData::CamData);
        bufferDescriptor._initialData = { (Byte*)&_gpuBlock._camData, bufferDescriptor._bufferParams._elementSize };

        for (U8 i = 0u; i < GFXBuffers::PerFrameBufferCount; ++i) {
            bufferDescriptor._name = Util::StringFormat("DVD_GPU_CAM_DATA_%d", i);
            _gfxBuffers._perFrameBuffers[i]._camDataBuffer = newSB(bufferDescriptor);
            _gfxBuffers._perFrameBuffers[i]._camBufferWriteRange = {};
        }
    }
    
    if (resizeCullCounter) {
        // Atomic counter for occlusion culling
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
        bufferDescriptor._ringBufferLength = to_U32(targetSizeCullCounter);
        bufferDescriptor._bufferParams._hostVisible = true;
        bufferDescriptor._bufferParams._elementSize = 4 * sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_W_CPU_R;
        bufferDescriptor._separateReadWrite = true;
        bufferDescriptor._initialData = { (bufferPtr)&VECTOR4_ZERO._v[0], 4 * sizeof(U32) };
        for (U8 i = 0u; i < GFXBuffers::PerFrameBufferCount; ++i) {
            bufferDescriptor._name = Util::StringFormat("CULL_COUNTER_%d", i);
            _gfxBuffers._perFrameBuffers[i]._cullCounter = newSB(bufferDescriptor);
        }
    }
    _gpuBlock._camNeedsUpload = true;
}

ErrorCode GFXDevice::postInitRenderingAPI(const vec2<U16> & renderResolution) {
    std::atomic_uint loadTasks = 0;
    ResourceCache* cache = parent().resourceCache();
    const Configuration& config = _parent.platformContext().config();

    IMPrimitive::InitStaticData();
    ShaderProgram::InitStaticData();
    Texture::OnStartup(*this);
    RenderPassExecutor::OnStartup(*this);
    GFX::InitPools();

    resizeGPUBlocks(TargetBufferSizeCam, RenderPass::DataBufferRingSize);

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
    defaultSampler.mipSampling(TextureMipSampling::NONE);
    defaultSampler.anisotropyLevel(0);
    const size_t samplerHash = defaultSampler.getHash();

    SamplerDescriptor defaultSamplerMips = {};
    defaultSamplerMips.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    defaultSamplerMips.anisotropyLevel(0);
    const size_t samplerHashMips = defaultSamplerMips.getHash();

    //PrePass
    TextureDescriptor depthDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_32, GFXImageFormat::DEPTH_COMPONENT);
    TextureDescriptor velocityDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
    //RG - packed normal, B - roughness
    TextureDescriptor normalsDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
    depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    velocityDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    normalsDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    //MainPass
    TextureDescriptor screenDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
    screenDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
    TextureDescriptor materialDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RG);
    materialDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

    // Normal, Previous and MSAA
    {
        InternalRTAttachmentDescriptors attachments
        {
            InternalRTAttachmentDescriptor{ screenDescriptor,   samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::ALBEDO) },
            InternalRTAttachmentDescriptor{ velocityDescriptor, samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::VELOCITY) },
            InternalRTAttachmentDescriptor{ normalsDescriptor,  samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::NORMALS) },
            InternalRTAttachmentDescriptor{ depthDescriptor,    samplerHash, RTAttachmentType::DEPTH, 0u }
        };

        RenderTargetDescriptor screenDesc = {};
        screenDesc._resolution = renderResolution;
        screenDesc._attachmentCount = to_U8(attachments.size());
        screenDesc._attachments = attachments.data();
        screenDesc._msaaSamples = 0u;
        screenDesc._name = "Screen";
        RenderTargetNames::SCREEN = _rtPool->allocateRT(screenDesc)._targetID;

        for (auto& attachment : attachments) {
            attachment._texDescriptor.msaaSamples(config.rendering.MSAASamples);
        }
        screenDesc._msaaSamples = config.rendering.MSAASamples;
        screenDesc._name = "Screen MS";
        RenderTargetNames::SCREEN_MS = _rtPool->allocateRT(screenDesc)._targetID;

        auto& screenAttachment = attachments.front();
        screenAttachment._texDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        screenAttachment._texDescriptor.msaaSamples(0);
        screenAttachment._samplerHash = samplerHashMips;
        screenDesc._attachmentCount = 1u;
        screenDesc._msaaSamples = 0u;
        screenDesc._name = "Screen Prev";
        RenderTargetNames::SCREEN_PREV = _rtPool->allocateRT(screenDesc)._targetID;
    }
    {
        TextureDescriptor ssaoDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED);
        ssaoDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        InternalRTAttachmentDescriptors attachments
        {
            InternalRTAttachmentDescriptor{ ssaoDescriptor, samplerHash, RTAttachmentType::COLOUR, 0u }
        };

        RenderTargetDescriptor ssaoDesc = {};
        ssaoDesc._name = "SSAO Result";
        ssaoDesc._resolution = renderResolution;
        ssaoDesc._attachmentCount = to_U8(attachments.size());
        ssaoDesc._attachments = attachments.data();
        ssaoDesc._msaaSamples = 0u;
        RenderTargetNames::SSAO_RESULT = _rtPool->allocateRT(ssaoDesc)._targetID;
    }
    {
        TextureDescriptor ssrDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
        ssrDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        
        InternalRTAttachmentDescriptors attachments
        {
            InternalRTAttachmentDescriptor{ ssrDescriptor, samplerHash, RTAttachmentType::COLOUR, 0u }
        };

        RenderTargetDescriptor ssrResultDesc = {};
        ssrResultDesc._name = "SSR Result";
        ssrResultDesc._resolution = renderResolution;
        ssrResultDesc._attachmentCount = to_U8(attachments.size());
        ssrResultDesc._attachments = attachments.data();
        ssrResultDesc._msaaSamples = 0u;
        RenderTargetNames::SSR_RESULT = _rtPool->allocateRT(ssrResultDesc)._targetID;
        
    }
    const U32 reflectRes = nextPOW2(CLAMPED(to_U32(config.rendering.reflectionPlaneResolution), 16u, 4096u) - 1u);

    TextureDescriptor hiZDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_32, GFXImageFormat::RED);
    hiZDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
    hiZDescriptor.addImageUsageFlag(ImageUsage::SHADER_WRITE);

    SamplerDescriptor hiZSampler = {};
    hiZSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    hiZSampler.anisotropyLevel(0u);
    hiZSampler.magFilter(TextureFilter::NEAREST);
    hiZSampler.minFilter(TextureFilter::NEAREST);
    hiZSampler.mipSampling(TextureMipSampling::NEAREST);

    InternalRTAttachmentDescriptors hiZAttachments
    {
        InternalRTAttachmentDescriptor{ hiZDescriptor, hiZSampler.getHash(), RTAttachmentType::COLOUR, 0 },
    };

    {
        RenderTargetDescriptor hizRTDesc = {};
        hizRTDesc._name = "HiZ";
        hizRTDesc._resolution = renderResolution;
        hizRTDesc._attachmentCount = to_U8(hiZAttachments.size());
        hizRTDesc._attachments = hiZAttachments.data();
        RenderTargetNames::HI_Z = _rtPool->allocateRT(hizRTDesc)._targetID;

        hizRTDesc._resolution.set(reflectRes, reflectRes);
        hizRTDesc._name = "HiZ_Reflect";
        RenderTargetNames::HI_Z_REFLECT = _rtPool->allocateRT(hizRTDesc)._targetID;
    }

    // Reflection Targets
    SamplerDescriptor reflectionSampler = {};
    reflectionSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    reflectionSampler.minFilter(TextureFilter::NEAREST);
    reflectionSampler.magFilter(TextureFilter::NEAREST);
    reflectionSampler.mipSampling(TextureMipSampling::NONE);
    const size_t reflectionSamplerHash = reflectionSampler.getHash();

    {
        TextureDescriptor environmentDescriptorPlanar(TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA);
        TextureDescriptor depthDescriptorPlanar(TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::DEPTH_COMPONENT);

        environmentDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        depthDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        RenderTargetDescriptor hizRTDesc = {};
        hizRTDesc._resolution.set(reflectRes, reflectRes);
        hizRTDesc._attachmentCount = to_U8(hiZAttachments.size());
        hizRTDesc._attachments = hiZAttachments.data();

        {
            InternalRTAttachmentDescriptors attachments
            {
                InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSamplerHash, RTAttachmentType::COLOUR, 0u },
                InternalRTAttachmentDescriptor{ depthDescriptorPlanar,       reflectionSamplerHash, RTAttachmentType::DEPTH, 0u },
            };

            RenderTargetDescriptor refDesc = {};
            refDesc._resolution = vec2<U16>(reflectRes);
            refDesc._attachmentCount = to_U8(attachments.size());
            refDesc._attachments = attachments.data();

            for (U32 i = 0; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i) {
                refDesc._name = Util::StringFormat("Reflection_Planar_%d", i);
                RenderTargetNames::REFLECTION_PLANAR[i] = _rtPool->allocateRT(refDesc)._targetID;
            }

            for (U32 i = 0; i < Config::MAX_REFRACTIVE_NODES_IN_VIEW; ++i) {
                refDesc._name = Util::StringFormat("Refraction_Planar_%d", i);
                RenderTargetNames::REFRACTION_PLANAR[i] = _rtPool->allocateRT(refDesc)._targetID;
            }

            environmentDescriptorPlanar.mipMappingState(TextureDescriptor::MipMappingState::OFF);
            InternalRTAttachmentDescriptors attachmentsBlur
            {//skip depth
                InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSamplerHash, RTAttachmentType::COLOUR, 0u }
            };

            refDesc._attachmentCount = to_U8(attachmentsBlur.size()); 
            refDesc._attachments = attachmentsBlur.data();
            refDesc._name = "Reflection_blur";
            RenderTargetNames::REFLECTION_PLANAR_BLUR = _rtPool->allocateRT(refDesc)._targetID;
        }
    }
    {
        SamplerDescriptor accumulationSampler = {};
        accumulationSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        accumulationSampler.minFilter(TextureFilter::NEAREST);
        accumulationSampler.magFilter(TextureFilter::NEAREST);
        accumulationSampler.mipSampling(TextureMipSampling::NONE);
        const size_t accumulationSamplerHash = accumulationSampler.getHash();

        TextureDescriptor accumulationDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
        accumulationDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        //R = revealage
        TextureDescriptor revealageDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED);
        revealageDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        InternalRTAttachmentDescriptors oitAttachments
        {
            InternalRTAttachmentDescriptor{ accumulationDescriptor, accumulationSamplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::ACCUMULATION) },
            InternalRTAttachmentDescriptor{ revealageDescriptor,    accumulationSamplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::REVEALAGE) },
        };

        const RenderTargetID rtSource[] = {
            RenderTargetNames::SCREEN,
            RenderTargetNames::SCREEN_MS
        };

        const U8 sampleCount[] = { 0u, config.rendering.MSAASamples };
        const Str64 targetName[] = { "OIT", "OIT_MS" };


        for (U8 i = 0u; i < 2u; ++i)
        {
            oitAttachments[0]._texDescriptor.msaaSamples(sampleCount[i]);
            oitAttachments[1]._texDescriptor.msaaSamples(sampleCount[i]);

            const RenderTarget* screenTarget = _rtPool->getRenderTarget(rtSource[i]);
            RTAttachment* screenNormalsAttachment = screenTarget->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::NORMALS));
            RTAttachment* screenDepthAttachment = screenTarget->getAttachment(RTAttachmentType::DEPTH, 0);

            ExternalRTAttachmentDescriptors externalAttachments
            {
                ExternalRTAttachmentDescriptor{ screenNormalsAttachment,  screenNormalsAttachment->descriptor()._samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::NORMALS) },
                ExternalRTAttachmentDescriptor{ screenDepthAttachment,    screenDepthAttachment->descriptor()._samplerHash, RTAttachmentType::DEPTH, 0u }
            };

            if_constexpr(Config::USE_COLOURED_WOIT) {
                RTAttachment* screenAttachment = screenTarget->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::ALBEDO));
                externalAttachments.push_back(
                    ExternalRTAttachmentDescriptor{ screenAttachment,  screenAttachment->descriptor()._samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::MODULATE) }
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
            const RenderTargetHandle handle = _rtPool->allocateRT(oitDesc);
            if (i == 0u) {
                RenderTargetNames::OIT = handle._targetID;
            } else {
                RenderTargetNames::OIT_MS = handle._targetID;
            }
        }
        {
            oitAttachments[0]._texDescriptor.msaaSamples(0u);
            oitAttachments[1]._texDescriptor.msaaSamples(0u);

            for (U16 i = 0u; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i) {
                const RenderTarget* reflectTarget = _rtPool->getRenderTarget(RenderTargetNames::REFLECTION_PLANAR[i]);
                RTAttachment* depthAttachment = reflectTarget->getAttachment(RTAttachmentType::DEPTH, 0);

                ExternalRTAttachmentDescriptors externalAttachments{
                     ExternalRTAttachmentDescriptor{ depthAttachment, depthAttachment->descriptor()._samplerHash, RTAttachmentType::DEPTH, 0u }
                };

                if_constexpr(Config::USE_COLOURED_WOIT) {
                    RTAttachment* screenAttachment = reflectTarget->getAttachment(RTAttachmentType::COLOUR, 0);
                    externalAttachments.push_back(
                        ExternalRTAttachmentDescriptor{ screenAttachment, screenAttachment->descriptor()._samplerHash, RTAttachmentType::COLOUR, to_U8(ScreenTargets::MODULATE) }
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
                RenderTargetNames::OIT_REFLECT = _rtPool->allocateRT(oitDesc)._targetID;
            }
        }
    }
    {
        TextureDescriptor environmentDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA);
        TextureDescriptor depthDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::DEPTH_COMPONENT);

        environmentDescriptorCube.mipMappingState(TextureDescriptor::MipMappingState::OFF);
        depthDescriptorCube.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        InternalRTAttachmentDescriptors attachments {
            InternalRTAttachmentDescriptor{ environmentDescriptorCube, reflectionSamplerHash, RTAttachmentType::COLOUR, 0u },
            InternalRTAttachmentDescriptor{ depthDescriptorCube,       reflectionSamplerHash, RTAttachmentType::DEPTH, 0u },
        };

        RenderTargetDescriptor refDesc = {};
        refDesc._resolution = vec2<U16>(reflectRes);
        refDesc._attachmentCount = to_U8(attachments.size());
        refDesc._attachments = attachments.data();

        refDesc._name = "Reflection_Cube_Array";
        RenderTargetNames::REFLECTION_CUBE = _rtPool->allocateRT(refDesc)._targetID;
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
            descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
            _textRenderPipeline = newPipeline(descriptor);
        });
    }
    {
        ShaderModuleDescriptor compModule = {};
        compModule._moduleType = ShaderType::COMPUTE;
        compModule._defines.emplace_back(Util::StringFormat("LOCAL_SIZE %d", DEPTH_REDUCE_LOCAL_SIZE));
        compModule._sourceFile = "HiZConstruct.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(compModule);

        // Initialized our HierarchicalZ construction shader (takes a depth attachment and down-samples it for every mip level)
        ResourceDescriptor descriptor1("HiZConstruct");
        descriptor1.waitForReady(false);
        descriptor1.propertyDescriptor(shaderDescriptor);
        _HIZConstructProgram = CreateResource<ShaderProgram>(cache, descriptor1, loadTasks);
        _HIZConstructProgram->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor pipelineDesc{};
            pipelineDesc._shaderProgramHandle = _HIZConstructProgram->handle();
            pipelineDesc._primitiveTopology = PrimitiveTopology::COMPUTE;

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
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
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

            ResourceDescriptor blur("BoxBlur_Single");
            blur.propertyDescriptor(shaderDescriptorSingle);
            _blurBoxShaderSingle = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurBoxShaderSingle->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
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

            ResourceDescriptor blur("BoxBlur_Layered");
            blur.propertyDescriptor(shaderDescriptorLayered);
            _blurBoxShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurBoxShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
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

                ResourceDescriptor blur("GaussBlur_Single");
                blur.propertyDescriptor(shaderDescriptorSingle);
                _blurGaussianShaderSingle = CreateResource<ShaderProgram>(cache, blur, loadTasks);
                _blurGaussianShaderSingle->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                    const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateHash = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;
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

                ResourceDescriptor blur("GaussBlur_Layered");
                blur.propertyDescriptor(shaderDescriptorLayered);
                _blurGaussianShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
                _blurGaussianShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                    const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateHash = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

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
        descriptor3.propertyDescriptor(shaderDescriptor);
        {
            _displayShader = CreateResource<ShaderProgram>(cache, descriptor3, loadTasks);
            _displayShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = _displayShader->handle();
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
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
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
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
        pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;
        pipelineDesc._shaderProgramHandle = _imWorldShader->handle();
        pipelineDesc._stateHash = primitiveStateBlock.getHash();

        _debugGizmoPipeline = pipelineDesc;

        primitiveStateBlock.depthTestEnabled(false);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoDepth = pipelineDesc;


        primitiveStateBlock.setCullMode(CullMode::NONE);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoCullNoDepth = pipelineDesc;


        primitiveStateBlock.depthTestEnabled(true);
        pipelineDesc._stateHash = primitiveStateBlock.getHash();
        _debugGizmoPipelineNoCull = pipelineDesc;
    }

    _renderer = eastl::make_unique<Renderer>(context(), cache);

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
    const DisplayWindow* mainWindow = context().app().windowManager().mainWindow();

    SizeChangeParams params{};
    params.width = _rtPool->getRenderTarget(RenderTargetNames::SCREEN)->getWidth();
    params.height = _rtPool->getRenderTarget(RenderTargetNames::SCREEN)->getHeight();
    params.winGUID = mainWindow->getGUID();
    params.isMainWindow = true;
    if (context().app().onResolutionChange(params)) {
        NOP();
    }

    _sceneData = MemoryManager_NEW SceneShaderData(*this);

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
    _gfxBuffers.reset(true, true);
    MemoryManager::SAFE_DELETE(_sceneData);
    // Close the shader manager
    MemoryManager::DELETE(_shaderComputeQueue);
    if (!ShaderProgram::OnShutdown()) {
        DIVIDE_UNEXPECTED_CALL();
    }

    RenderPassExecutor::OnShutdown(*this);
    Texture::OnShutdown();
    ShaderProgram::DestroyStaticData();
    assert(ShaderProgram::ShaderProgramCount() == 0);
    // Close the rendering API
    _api->closeRenderingAPI();
    _api.reset();

    ScopedLock<Mutex> lock(_graphicsResourceMutex);
    if (!_graphicResources.empty()) {
        string list = " [ ";
        for (const std::tuple<GraphicsResource::Type, I64, U64>& res : _graphicResources) {
            list.append(TypeUtil::GraphicResourceTypeToName(std::get<0>(res)));
            list.append("_");
            list.append(Util::to_string(std::get<1>(res)));
            list.append("_");
            list.append(Util::to_string(std::get<2>(res)));
            list.append(",");
        }
        list.pop_back();
        list += " ]";
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_LEAKED_RESOURCES")), _graphicResources.size());
        Console::errorfn(list.c_str());
    }
    _graphicResources.clear();
}

void GFXDevice::onThreadCreated(const std::thread::id& threadID) const {
    _api->onThreadCreated(threadID);
    if (!ShaderProgram::OnThreadCreated(*this, threadID)) {
        DIVIDE_UNEXPECTED_CALL();
    }
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
}

void GFXDevice::beginFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

    if (global) {
        ++s_frameCount;

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
        SizeChangeParams params{};
        params.isFullScreen = window.fullscreen();
        params.width = _resolutionChangeQueued.first.width;
        params.height = _resolutionChangeQueued.first.height;
        params.winGUID = context().mainWindow().getGUID();
        params.isMainWindow = global;

        if (context().app().onResolutionChange(params)) {
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
    _performanceMetrics._scratchBufferQueueUsage[0] = to_U32(_gfxBuffers.crtBuffers()._camWritesThisFrame);
    _performanceMetrics._scratchBufferQueueUsage[1] = to_U32(_gfxBuffers.crtBuffers()._renderWritesThisFrame);

    if (_gfxBuffers._needsResizeCam) {
        const GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
        const U32 currentSizeCam = frameBuffers._camDataBuffer->queueLength();
        const U32 currentSizeCullCounter = frameBuffers._cullCounter->queueLength();
        resizeGPUBlocks(_gfxBuffers._needsResizeCam ? currentSizeCam + TargetBufferSizeCam : currentSizeCam, currentSizeCullCounter);
        _gfxBuffers._needsResizeCam = false;
    }
    _gfxBuffers.onEndFrame();
    ShaderProgram::OnEndFrame(*this);
}

#pragma endregion

#pragma region Utility functions
/// Generate a cube texture and store it in the provided RenderTarget
void GFXDevice::generateCubeMap(RenderPassParams& params,
                                const I16 arrayOffset,
                                const vec3<F32>& pos,
                                const vec2<F32>& zPlanes,
                                GFX::CommandBuffer& commandsInOut,
                                GFX::MemoryBarrierCommand& memCmdInOut,
                                std::array<Camera*, 6>& cameras) {
    OPTICK_EVENT();

    if (arrayOffset < 0) {
        return;
    }

    // Only the first colour attachment or the depth attachment is used for now
    // and it must be a cube map texture
    RenderTarget* cubeMapTarget = _rtPool->getRenderTarget(params._target);
    // Colour attachment takes precedent over depth attachment
    const bool hasColour = cubeMapTarget->hasAttachment(RTAttachmentType::COLOUR, 0);
    const bool hasDepth = cubeMapTarget->hasAttachment(RTAttachmentType::DEPTH, 0);
    const vec2<U16> targetResolution = cubeMapTarget->getResolution();

    // Everyone's innocent until proven guilty
    bool isValidFB = false;
    if (hasColour) {
        RTAttachment* colourAttachment = cubeMapTarget->getAttachment(RTAttachmentType::COLOUR, 0);
        // We only need the colour attachment
        isValidFB = IsCubeTexture(colourAttachment->texture()->descriptor().texType());
    } else {
        RTAttachment* depthAttachment = cubeMapTarget->getAttachment(RTAttachmentType::DEPTH, 0);
        // We don't have a colour attachment, so we require a cube map depth attachment
        isValidFB = hasDepth && IsCubeTexture(depthAttachment->texture()->descriptor().texType());
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
    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;

    RenderPassManager* passMgr = parent().renderPassManager();

    for (U8 i = 0u; i < 6u; ++i) {
        // Draw to the current cubemap face
        if (hasColour) {
            params._layerParams._colourLayers[0] = i + (arrayOffset * 6);
        }
        if (hasDepth) {
            params._layerParams._depthLayer = i + (arrayOffset * 6);
        }

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
        passMgr->doCustomPass(camera, params, commandsInOut, memCmdInOut);
    }
}

void GFXDevice::generateDualParaboloidMap(RenderPassParams& params,
                                          const I16 arrayOffset,
                                          const vec3<F32>& pos,
                                          const vec2<F32>& zPlanes,
                                          GFX::CommandBuffer& bufferInOut,
                                          GFX::MemoryBarrierCommand& memCmdInOut,
                                          std::array<Camera*, 2>& cameras)
{
    OPTICK_EVENT();

    if (arrayOffset < 0) {
        return;
    }

    RenderTarget* paraboloidTarget = _rtPool->getRenderTarget(params._target);
    // Colour attachment takes precedent over depth attachment
    const bool hasColour = paraboloidTarget->hasAttachment(RTAttachmentType::COLOUR, 0);
    const bool hasDepth = paraboloidTarget->hasAttachment(RTAttachmentType::DEPTH, 0);
    const vec2<U16> targetResolution = paraboloidTarget->getResolution();

    bool isValidFB = false;
    if (hasColour) {
        RTAttachment* colourAttachment = paraboloidTarget->getAttachment(RTAttachmentType::COLOUR, 0);
        // We only need the colour attachment
        isValidFB = IsArrayTexture(colourAttachment->texture()->descriptor().texType());
    } else {
        RTAttachment* depthAttachment = paraboloidTarget->getAttachment(RTAttachmentType::DEPTH, 0);
        // We don't have a colour attachment, so we require a cube map depth attachment
        isValidFB = hasDepth && IsArrayTexture(depthAttachment->texture()->descriptor().texType());
    }
    // Make sure we have a proper render target to draw to
    if (!isValidFB) {
        // Future formats must be added later (e.g. cube map arrays)
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_DEVICE_INVALID_FB_DP")));
        return;
    }

    params._passName = "DualParaboloid";
    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;
    RenderPassManager* passMgr = parent().renderPassManager();

    for (U8 i = 0u; i < 2u; ++i) {
        Camera* camera = cameras[i];
        if (!camera) {
            camera = Camera::GetUtilityCamera(Camera::UtilityCamera::DUAL_PARABOLOID);
        }

        if (hasColour) {
            params._layerParams._colourLayers[0] = arrayOffset + i;
        }
        if (hasDepth) {
            params._layerParams._depthLayer = arrayOffset + i;
        }
        // Point our camera to the correct face
        camera->lookAt(pos, pos + (i == 0 ? WORLD_Z_NEG_AXIS : WORLD_Z_AXIS) * zPlanes.y);
        // Set a 180 degree vertical FoV perspective projection
        camera->setProjection(to_F32(aspect), Angle::to_VerticalFoV(Angle::DEGREES<F32>(180.0f), aspect), zPlanes);
        // And generated required matrices
        // Pass our render function to the renderer
        params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);

        passMgr->doCustomPass(camera, params, bufferInOut, memCmdInOut);
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

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::FRAGMENT);
        binding._slot = 0;
        binding._data.As<DescriptorCombinedImageSampler>() = { inputAttachment->texture()->defaultView(), inputAttachment->descriptor()._samplerHash };

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

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::FRAGMENT);
        binding._slot = 0;
        binding._data.As<DescriptorCombinedImageSampler>() = { bufferAttachment->texture()->defaultView(), bufferAttachment->descriptor()._samplerHash };

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
        _rtPool->getRenderTarget(RenderTargetNames::SCREEN_MS)->updateSampleCount(sampleCount);
        _rtPool->getRenderTarget(RenderTargetNames::OIT_MS)->updateSampleCount(sampleCount);
        Material::RecomputeShaders();
    }
}

void GFXDevice::setShadowMSAASampleCountInternal(const ShadowType type, U8 sampleCount) {
    CLAMP(sampleCount, to_U8(0u), gpuState().maxMSAASampleCount());
    ShadowMap::setMSAASampleCount(type, sampleCount);
}

/// The main entry point for any resolution change request
void GFXDevice::onWindowSizeChange(const SizeChangeParams& params) {
    if (params.isMainWindow) {
        fitViewportInWindow(params.width, params.height);
    }
}

void GFXDevice::onResolutionChange(const SizeChangeParams& params) {
    if (!params.isMainWindow) {
        return;
    }

    const U16 w = params.width;
    const U16 h = params.height;

    // Update resolution only if it's different from the current one.
    // Avoid resolution change on minimize so we don't thrash render targets
    if (w < 1 || h < 1 || _renderingResolution == vec2<U16>(w, h)) {
        return;
    }

    Configuration& config = _parent.platformContext().config();

    const F32 aspectRatio = to_F32(w) / h;
    const F32 vFoV = Angle::to_VerticalFoV(config.runtime.horizontalFOV, to_D64(aspectRatio));
    const vec2<F32> zPlanes(Camera::s_minNearZ, config.runtime.cameraViewDistance);

    // Update the 2D camera so it matches our new rendering viewport
    if (Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->setProjection(vec4<F32>(0, to_F32(w), 0, to_F32(h)), vec2<F32>(-1, 1))) {
        Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->updateFrustum();
    }
    if (Camera::GetUtilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->setProjection(vec4<F32>(0, to_F32(w), to_F32(h), 0), vec2<F32>(-1, 1))) {
        Camera::GetUtilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->updateFrustum();
    }
    if (Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT)->setProjection(aspectRatio, vFoV, zPlanes)) {
        Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT)->updateFrustum();
    }

    // Update render targets with the new resolution
    _rtPool->getRenderTarget(RenderTargetNames::SCREEN)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::SCREEN_PREV)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::SCREEN_MS)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::SSAO_RESULT)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::SSR_RESULT)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::HI_Z)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::OIT)->resize(w, h);
    _rtPool->getRenderTarget(RenderTargetNames::OIT_MS)->resize(w, h);

    // Update post-processing render targets and buffers
    _renderer->updateResolution(w, h);
    _renderingResolution.set(w, h);

    fitViewportInWindow(w, h);
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

bool GFXDevice::uploadGPUBlock() {
    OPTICK_EVENT();

    // Put the viewport update here as it is the most common source of gpu data invalidation and not always
    // needed for rendering (e.g. changed by RenderTarget::End())

    const vec4<F32> tempViewport{ activeViewport() };
    if (_gpuBlock._camData._ViewPort != tempViewport) {
        _gpuBlock._camData._ViewPort.set(tempViewport);
        const U32 clustersX = to_U32(std::ceil(to_F32(tempViewport.sizeX) / Renderer::CLUSTER_SIZE.x));
        const U32 clustersY = to_U32(std::ceil(to_F32(tempViewport.sizeY) / Renderer::CLUSTER_SIZE.y));
        if (clustersX != to_U32(_gpuBlock._camData._renderTargetInfo.z) ||
            clustersY != to_U32(_gpuBlock._camData._renderTargetInfo.w))
        {
            _gpuBlock._camData._renderTargetInfo.z = to_F32(clustersX);
            _gpuBlock._camData._renderTargetInfo.w = to_F32(clustersY);
        }
        _gpuBlock._camNeedsUpload = true;
    }

    if (_gpuBlock._camNeedsUpload) {
        GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
        _gpuBlock._camNeedsUpload = false;
        frameBuffers._camDataBuffer->incQueue();
        if (++frameBuffers._camWritesThisFrame >= frameBuffers._camDataBuffer->queueLength()) {
            //We've wrapped around this buffer inside of a single frame so sync performance will degrade unless we increase our buffer size
            _gfxBuffers._needsResizeCam = true;
            //Because we are now overwriting existing data, we need to make sure that any fences that could possibly protect us have been flushed
            DIVIDE_ASSERT(frameBuffers._camBufferWriteRange._length == 0u);
        }
        const BufferRange writtenRange = frameBuffers._camDataBuffer->writeData(&_gpuBlock._camData)._range;

        if (frameBuffers._camBufferWriteRange._length == 0u) {
            frameBuffers._camBufferWriteRange = writtenRange;
        } else {
            Merge(frameBuffers._camBufferWriteRange, writtenRange);
        }

        static DescriptorSetBinding binding{ ShaderStageVisibility::ALL };
        binding._slot = 1;
        binding._data.As<ShaderBufferEntry>() = { *_gfxBuffers.crtBuffers()._camDataBuffer, { 0, 1 } };

        _descriptorSets[to_base(DescriptorSetUsage::PER_BATCH)].update(DescriptorSetUsage::PER_BATCH, binding);
        return true;
    }

    return false;
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
                _gpuBlock._camData._clipPlanes[count++].set(planes[i]._equation);
                if (count == Config::MAX_CLIP_DISTANCES) {
                    break;
                }
            }
        }

        _gpuBlock._camData._renderProperties.x = to_F32(count);
        _gpuBlock._camNeedsUpload = true;
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
    OPTICK_EVENT();

    bool projectionDirty = false, viewDirty = false;
    if (_gpuBlock._camData._PreviousViewMatrix != prevViewMatrix) {
        _gpuBlock._camData._PreviousViewMatrix = prevViewMatrix;
        viewDirty = true;
    }
    if (_gpuBlock._camData._PreviousProjectionMatrix != prevProjectionMatrix) {
        _gpuBlock._camData._PreviousProjectionMatrix = prevProjectionMatrix;
        projectionDirty = true;
    }

    if (projectionDirty || viewDirty) {
        mat4<F32>::Multiply(_gpuBlock._camData._PreviousViewMatrix, _gpuBlock._camData._PreviousProjectionMatrix, _gpuBlock._camData._PreviousViewProjectionMatrix);
        _gpuBlock._camNeedsUpload = true;
    }
}

// Update the rendering viewport
bool GFXDevice::setViewport(const Rect<I32>& viewport) {
    OPTICK_EVENT();

    // Change the viewport on the Rendering API level
    if (_api->setViewport(viewport)) {
        _activeViewport.set(viewport);
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

const GFXShaderData::CamData& GFXDevice::cameraData() const noexcept {
    return _gpuBlock._camData;
}
#pragma endregion

#pragma region Command buffers, occlusion culling, etc
void GFXDevice::validateAndUploadDescriptorSets() {
    uploadGPUBlock();

    constexpr std::array<DescriptorSetUsage, to_base(DescriptorSetUsage::COUNT)> prioritySorted = {
        DescriptorSetUsage::PER_FRAME,
        DescriptorSetUsage::PER_PASS, 
        DescriptorSetUsage::PER_BATCH,
        DescriptorSetUsage::PER_DRAW
    };

    for (const DescriptorSetUsage usage : prioritySorted) {
        GFXDescriptorSet& set = _descriptorSets[to_base(usage)];
        if (set.dirty()) {
            _api->bindShaderResources(usage, set.impl());
            set.dirty(false);
        }
    }
}

void GFXDevice::flushCommandBuffer(GFX::CommandBuffer& commandBuffer, const bool batch) {
    OPTICK_EVENT();

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        DIVIDE_ASSERT(Runtime::isMainThread(), "GFXDevice::flushCommandBuffer called from worker thread!");
    }

    if (batch) {
        commandBuffer.batch();
    }

    static GFX::MemoryBarrierCommand gpuBlockMemCommand{};

    const Rect<I32> initialViewport = activeViewport();

    _api->preFlushCommandBuffer(commandBuffer);

    const GFX::CommandBuffer::CommandOrderContainer& commands = commandBuffer();
    for (const GFX::CommandBuffer::CommandEntry& cmd : commands) {
        const GFX::CommandType cmdType = static_cast<GFX::CommandType>(cmd._typeIndex);
        if (IsSubmitCommand(cmdType)) {
            validateAndUploadDescriptorSets();
        }

        switch (cmdType) {
            case GFX::CommandType::BLIT_RT: {
                OPTICK_EVENT("BLIT_RT");

                const GFX::BlitRenderTargetCommand* crtCmd = commandBuffer.get<GFX::BlitRenderTargetCommand>(cmd);
                RenderTarget* source = renderTargetPool().getRenderTarget(crtCmd->_source);
                RenderTarget* destination = renderTargetPool().getRenderTarget(crtCmd->_destination);
                destination->blitFrom(source, crtCmd->_params);
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
                crtCmd._buffer->readData({ crtCmd._offsetElementCount, crtCmd._elementCount }, crtCmd._target);
            } break;
            case GFX::CommandType::CLEAR_BUFFER_DATA: {
                OPTICK_EVENT("CLEAR_BUFFER_DATA");

                const GFX::ClearBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ClearBufferDataCommand>(cmd);
                if (crtCmd._buffer != nullptr) {
                    GFX::MemoryBarrierCommand memCmd{};
                    memCmd._bufferLocks.push_back(crtCmd._buffer->clearData({ crtCmd._offsetElementCount, crtCmd._elementCount }));
                    memCmd._syncFlag = 101u;
                    _api->flushCommand(&memCmd);
                }
            } break;
            case GFX::CommandType::SET_VIEWPORT: {
                OPTICK_EVENT("SET_VIEWPORT");

                setViewport(commandBuffer.get<GFX::SetViewportCommand>(cmd)->_viewport);
            } break;
            case GFX::CommandType::PUSH_VIEWPORT: {
                OPTICK_EVENT("PUSH_VIEWPORT");

                const GFX::PushViewportCommand* crtCmd = commandBuffer.get<GFX::PushViewportCommand>(cmd);
                _viewportStack.push(activeViewport());
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
                commandBuffer.get<GFX::ExternalCommand>(cmd)->_cbk();
            } break;
            case GFX::CommandType::BIND_SHADER_RESOURCES: {
                const auto resCmd = commandBuffer.get<GFX::BindShaderResourcesCommand>(cmd);
                GFXDescriptorSet& set = descriptorSet(resCmd->_usage);
                for (const DescriptorSetBinding& binding : resCmd->_bindings) {
                    set.update(resCmd->_usage, binding);
                }
            } break;
            default: break;
        }

        _api->flushCommand(commandBuffer.get<GFX::CommandBase>(cmd));
    }

    GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
    if (frameBuffers._camBufferWriteRange._length > 0u) {
        GFX::MemoryBarrierCommand writeMemCmd{};
        BufferLock& lock = writeMemCmd._bufferLocks.emplace_back();
        lock._targetBuffer = frameBuffers._camDataBuffer.get();
        lock._range = frameBuffers._camBufferWriteRange;
        _api->flushCommand(&writeMemCmd);
        frameBuffers._camBufferWriteRange = {};
    }

    _api->postFlushCommandBuffer(commandBuffer);

    setViewport(initialViewport);

    // Descriptor sets are only valid per command buffer they are submitted in. If we finish the command buffer submission,
    // we mark them as dirty so that the next command buffer can bind them again even if the data is the same
    // We always check the dirty flags before any draw/compute command by calling "validateAndUploadDescriptorSets" beforehand
    for (auto& set : _descriptorSets) {
        set.dirty(true);
    }
    descriptorSet(DescriptorSetUsage::PER_DRAW).clear();
}

/// Transform our depth buffer to a HierarchicalZ buffer (for occlusion queries and screen space reflections)
/// Based on RasterGrid implementation: http://rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/
/// Modified with nVidia sample code: https://github.com/nvpro-samples/gl_occlusion_culling
std::pair<const Texture_ptr&, size_t> GFXDevice::constructHIZ(RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut) {
    OPTICK_EVENT();

    assert(depthBuffer != HiZTarget);

    const RTAttachment* SrcAtt = _rtPool->getRenderTarget(depthBuffer)->getAttachment(RTAttachmentType::DEPTH, 0);
    const RTAttachment* HiZAtt = _rtPool->getRenderTarget(HiZTarget)->getAttachment(RTAttachmentType::COLOUR, 0);
    Texture* HiZTex = HiZAtt->texture().get();
    DIVIDE_ASSERT(HiZTex->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::MANUAL);

    GFX::EnqueueCommand(cmdBufferInOut, GFX::BeginDebugScopeCommand{ "Construct Hi-Z" });
    GFX::EnqueueCommand(cmdBufferInOut, GFX::BindPipelineCommand{ _HIZPipeline });

    U32 twidth = HiZTex->width();
    U32 theight = HiZTex->height();
    bool wasEven = false;
    U32 owidth = twidth;
    U32 oheight = theight;

    PushConstantsStruct pushConstants{};

    for (U32 i = 0u; i < HiZTex->mipCount(); ++i)
    {
        twidth = twidth < 1u ? 1u : twidth;
        theight = theight < 1u ? 1u : theight;

        ImageView outImage = HiZTex->getView({i, 1u});
        outImage._usage = ImageUsage::SHADER_WRITE;
        
        ImageView inImage =  (i == 0u ? SrcAtt->texture()->defaultView() : HiZTex->getView({i - 1u, 1u}, {0u, 1u}));
        inImage._usage = ImageUsage::SHADER_SAMPLE;

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(cmdBufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        {
            auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
            binding._slot = 0u;
            binding._data.As<ImageView>() = outImage;
        }
        {
            auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
            binding._slot = 1u;
            binding._data.As<DescriptorCombinedImageSampler>() = {inImage, HiZAtt->descriptor()._samplerHash};
        }

        pushConstants.data0._vec[0].set(owidth, oheight, twidth, theight);
        pushConstants.data0._vec[1].x = wasEven ? 1.f : 0.f;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(cmdBufferInOut)->_constants.set(pushConstants);

        // Dummy draw command as the full screen quad is generated completely in the vertex shader
        GFX::EnqueueCommand<GFX::DispatchComputeCommand>(cmdBufferInOut)->_computeGroupSize =
        {
            getGroupCount(twidth, DEPTH_REDUCE_LOCAL_SIZE),
            getGroupCount(theight, DEPTH_REDUCE_LOCAL_SIZE),
            1u
        };
        
        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(cmdBufferInOut)->_barrierMask = to_base(MemoryBarrierType::TEXTURE_FETCH);

        wasEven = twidth % 2 == 0 && theight % 2 == 0;
        owidth = twidth;
        oheight = theight;
        twidth /= 2;
        theight /= 2;
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(cmdBufferInOut);

    return { HiZAtt->texture(), HiZAtt->descriptor()._samplerHash };
}

void GFXDevice::occlusionCull(const RenderPass::BufferData& bufferData,
                              const Texture_ptr& hizBuffer,
                              const size_t samplerHash,
                              const CameraSnapshot& cameraSnapshot,
                              const bool countCulledNodes,
                              GFX::CommandBuffer& bufferInOut)
{
    OPTICK_EVENT();

    const U32 cmdCount = *bufferData._lastCommandCount;
    const U32 threadCount = getGroupCount(cmdCount, GROUP_SIZE_AABB);

    if (threadCount == 0u || !enableOcclusionCulling()) {
        GFX::EnqueueCommand(bufferInOut, GFX::AddDebugMessageCommand("Occlusion Culling Skipped"));
        return;
    }

    ShaderBuffer* cullBuffer = _gfxBuffers.crtBuffers()._cullCounter.get();
    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Occlusion Cull" });

    // Not worth the overhead for a handful of items and the Pre-Z pass should handle overdraw just fine
    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _HIZCullPipeline });
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 0;
        binding._data.As<DescriptorCombinedImageSampler>() = { hizBuffer->defaultView(), samplerHash };
    }
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_PASS;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 7;
        binding._data.As<ShaderBufferEntry>() = { *cullBuffer, { 0u, 1u } };
    }
    mat4<F32> viewProjectionMatrix;
    mat4<F32>::Multiply(cameraSnapshot._viewMatrix, cameraSnapshot._projectionMatrix, viewProjectionMatrix);

    PushConstantsStruct fastConstants{};
    fastConstants.data0 = viewProjectionMatrix;
    fastConstants.data1 = cameraSnapshot._viewMatrix;

    GFX::SendPushConstantsCommand HIZPushConstantsCMD = {};
    HIZPushConstantsCMD._constants.set(_ID("countCulledItems"), GFX::PushConstantType::UINT, countCulledNodes ? 1u : 0u);
    HIZPushConstantsCMD._constants.set(_ID("numEntities"), GFX::PushConstantType::UINT, cmdCount);
    HIZPushConstantsCMD._constants.set(_ID("nearPlane"), GFX::PushConstantType::FLOAT, cameraSnapshot._zPlanes.x);
    HIZPushConstantsCMD._constants.set(_ID("viewSize"), GFX::PushConstantType::VEC2, vec2<F32>(hizBuffer->width(), hizBuffer->height()));
    HIZPushConstantsCMD._constants.set(_ID("frustumPlanes"), GFX::PushConstantType::VEC4, cameraSnapshot._frustumPlanes);
    HIZPushConstantsCMD._constants.set(fastConstants);

    GFX::EnqueueCommand(bufferInOut, HIZPushConstantsCMD);

    GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ threadCount, 1, 1 });

    // Occlusion culling barrier
    GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand
        {
            to_base(MemoryBarrierType::COMMAND_BUFFER) | //For rendering
            to_base(MemoryBarrierType::SHADER_STORAGE) | //For updating later on
            (countCulledNodes ? to_base(MemoryBarrierType::BUFFER_UPDATE) : 0u) //For cull counter
        });

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    if (queryPerformanceStats() && countCulledNodes)
    {
        GFX::ReadBufferDataCommand readAtomicCounter;
        readAtomicCounter._buffer = cullBuffer;
        readAtomicCounter._target = { &_lastCullCount, 4 * sizeof(U32) };
        readAtomicCounter._offsetElementCount = 0;
        readAtomicCounter._elementCount = 1;
        GFX::EnqueueCommand(bufferInOut, readAtomicCounter);

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
void GFXDevice::drawText(const TextElementBatch& batch, GFX::CommandBuffer& bufferInOut, const bool pushCamera) const
{
    drawText(GFX::DrawTextCommand{ batch }, bufferInOut, pushCamera);
}

void GFXDevice::drawText(const GFX::DrawTextCommand& cmd, GFX::CommandBuffer& bufferInOut, const bool pushCamera) const
{
    if (pushCamera)
    {
        GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    }
    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _textRenderPipeline });
    GFX::EnqueueCommand(bufferInOut, GFX::SendPushConstantsCommand{ _textRenderConstants });
    GFX::EnqueueCommand(bufferInOut, cmd);
    if (pushCamera)
    {
        GFX::EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
    }
}

void GFXDevice::drawTextureInViewport(const ImageView& texture, const size_t samplerHash, const Rect<I32>& viewport, const bool convertToSrgb, const bool drawToDepthOnly, bool drawBlend, GFX::CommandBuffer& bufferInOut) {
    static GFX::BeginDebugScopeCommand   s_beginDebugScopeCmd    { "Draw Texture In Viewport" };
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBTrue { PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, true}}};
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBFalse{ PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, false}}};

    GFX::EnqueueCommand(bufferInOut, s_beginDebugScopeCmd);
    GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    GFX::EnqueueCommand(bufferInOut, drawToDepthOnly ? _drawFSDepthPipelineCmd : drawBlend ? _drawFSTexturePipelineBlendCmd : _drawFSTexturePipelineCmd);

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::FRAGMENT);
    binding._slot = 0;
    binding._data.As<DescriptorCombinedImageSampler>() = { texture, samplerHash };

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
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Render Debug Views" });

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
        HiZ->_shader = _renderTargetDraw;
        HiZ->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::HI_Z)->getAttachment(RTAttachmentType::COLOUR, 0)->texture();
        HiZ->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::HI_Z)->getAttachment(RTAttachmentType::COLOUR, 0)->descriptor()._samplerHash;
        HiZ->_name = "Hierarchical-Z";
        HiZ->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.f);
        HiZ->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        HiZ->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        HiZ->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        HiZ->_cycleMips = true;

        DebugView_ptr DepthPreview = std::make_shared<DebugView>();
        DepthPreview->_shader = _previewDepthMapShader;
        DepthPreview->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::DEPTH, 0)->texture();
        DepthPreview->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::DEPTH, 0)->descriptor()._samplerHash;
        DepthPreview->_name = "Depth Buffer";
        DepthPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        DepthPreview->_shaderData.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, vec2<F32>(Camera::s_minNearZ, _context.config().runtime.cameraViewDistance));

        DebugView_ptr NormalPreview = std::make_shared<DebugView>();
        NormalPreview->_shader = _renderTargetDraw;
        NormalPreview->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::NORMALS))->texture();
        NormalPreview->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::NORMALS))->descriptor()._samplerHash;
        NormalPreview->_name = "Normals";
        NormalPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        NormalPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, true);
        NormalPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        NormalPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 2u);
        NormalPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);  
        
        DebugView_ptr VelocityPreview = std::make_shared<DebugView>();
        VelocityPreview->_shader = _renderTargetDraw;
        VelocityPreview->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::VELOCITY))->texture();
        VelocityPreview->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::VELOCITY))->descriptor()._samplerHash;
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
        SSAOPreview->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::SSAO_RESULT)->getAttachment(RTAttachmentType::COLOUR, 0u)->texture();
        SSAOPreview->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::SSAO_RESULT)->getAttachment(RTAttachmentType::COLOUR, 0u)->descriptor()._samplerHash;
        SSAOPreview->_name = "SSAO Map";
        SSAOPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        SSAOPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        SSAOPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        SSAOPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        SSAOPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaAccumulationHigh = std::make_shared<DebugView>();
        AlphaAccumulationHigh->_shader = _renderTargetDraw;
        AlphaAccumulationHigh->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::OIT)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::ALBEDO))->texture();
        AlphaAccumulationHigh->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::OIT)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::ALBEDO))->descriptor()._samplerHash;
        AlphaAccumulationHigh->_name = "Alpha Accumulation High";
        AlphaAccumulationHigh->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        AlphaAccumulationHigh->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 4u);
        AlphaAccumulationHigh->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaRevealageHigh = std::make_shared<DebugView>();
        AlphaRevealageHigh->_shader = _renderTargetDraw;
        AlphaRevealageHigh->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::OIT)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::REVEALAGE))->texture();
        AlphaRevealageHigh->_samplerHash = renderTargetPool().getRenderTarget(RenderTargetNames::OIT)->getAttachment(RTAttachmentType::COLOUR, to_U8(ScreenTargets::REVEALAGE))->descriptor()._samplerHash;
        AlphaRevealageHigh->_name = "Alpha Revealage High";
        AlphaRevealageHigh->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        AlphaRevealageHigh->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        AlphaRevealageHigh->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        AlphaRevealageHigh->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        AlphaRevealageHigh->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        SamplerDescriptor lumaSampler = {};
        lumaSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
        lumaSampler.minFilter(TextureFilter::NEAREST);
        lumaSampler.magFilter(TextureFilter::NEAREST);
        lumaSampler.mipSampling(TextureMipSampling::NONE);
        lumaSampler.anisotropyLevel(0);

        DebugView_ptr Luminance = std::make_shared<DebugView>();
        Luminance->_shader = _renderTargetDraw;
        Luminance->_texture = getRenderer().postFX().getFilterBatch().luminanceTex();
        Luminance->_samplerHash = lumaSampler.getHash();
        Luminance->_name = "Luminance";
        Luminance->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        Luminance->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        Luminance->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        Luminance->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        Luminance->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        const RenderTargetHandle& edgeRTHandle = getRenderer().postFX().getFilterBatch().edgesRT();

        DebugView_ptr Edges = std::make_shared<DebugView>();
        Edges->_shader = _renderTargetDraw;
        Edges->_texture = renderTargetPool().getRenderTarget(edgeRTHandle._targetID)->getAttachment(RTAttachmentType::COLOUR, 0u)->texture();
        Edges->_samplerHash = renderTargetPool().getRenderTarget(edgeRTHandle._targetID)->getAttachment(RTAttachmentType::COLOUR, 0u)->descriptor()._samplerHash;
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
    pipelineDesc._shaderProgramHandle = SHADER_INVALID_HANDLE;
    pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;

    const Rect<I32> previousViewport = activeViewport();

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
        const ShaderProgramHandle crtShader = pipelineDesc._shaderProgramHandle;
        const ShaderProgramHandle newShader = view->_shader->handle();
        if (crtShader != newShader) {
            pipelineDesc._shaderProgramHandle = view->_shader->handle();
            crtPipeline = newPipeline(pipelineDesc);
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = crtPipeline;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants = view->_shaderData;
        GFX::EnqueueCommand<GFX::SetViewportCommand>(bufferInOut)->_viewport.set(viewport);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::FRAGMENT);
        binding._slot = view->_textureBindSlot;
        binding._data.As<DescriptorCombinedImageSampler>() = { view->_texture->defaultView(), view->_samplerHash };

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

PipelineDescriptor& GFXDevice::getDebugPipeline(const IM::BaseDescriptor& descriptor) noexcept {
    if (descriptor.noDepth) {
        return (descriptor.noCull ? _debugGizmoPipelineNoCullNoDepth : _debugGizmoPipelineNoDepth);
    } else if (descriptor.noCull) {
        return _debugGizmoPipelineNoCull;
    }

    return _debugGizmoPipeline;
}

void GFXDevice::debugDrawLines(const I64 ID, const IM::LineDescriptor descriptor) noexcept {
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
            linePrimitive = newIMP(Util::StringFormat("DebugLine_%d", f));
            linePrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        linePrimitive->forceWireframe(data._descriptor.wireframe); //? Uhm, not gonna do much -Ionut
        linePrimitive->fromLines(data._descriptor);
        linePrimitive->getCommandBuffer(data._descriptor.worldMatrix, bufferInOut);
    }
}

void GFXDevice::debugDrawBox(const I64 ID, const IM::BoxDescriptor descriptor) noexcept {
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
            boxPrimitive = newIMP(Util::StringFormat("DebugBox_%d", f));
            boxPrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        boxPrimitive->forceWireframe(data._descriptor.wireframe);
        boxPrimitive->fromBox(data._descriptor);
        boxPrimitive->getCommandBuffer(data._descriptor.worldMatrix, bufferInOut);
    }
}

void GFXDevice::debugDrawOBB(const I64 ID, const IM::OBBDescriptor descriptor) noexcept {
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
            boxPrimitive = newIMP(Util::StringFormat("DebugOBB_%d", f));
            boxPrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        boxPrimitive->forceWireframe(data._descriptor.wireframe);
        boxPrimitive->fromOBB(data._descriptor);
        boxPrimitive->getCommandBuffer(data._descriptor.worldMatrix, bufferInOut);
    }
}
void GFXDevice::debugDrawSphere(const I64 ID, const IM::SphereDescriptor descriptor) noexcept {
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
            spherePrimitive = newIMP(Util::StringFormat("DebugSphere_%d", f));
            spherePrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        spherePrimitive->forceWireframe(data._descriptor.wireframe);
        spherePrimitive->fromSphere(data._descriptor);
        spherePrimitive->getCommandBuffer(data._descriptor.worldMatrix, bufferInOut);
    }
}

void GFXDevice::debugDrawCone(const I64 ID, const IM::ConeDescriptor descriptor) noexcept {
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
            conePrimitive = newIMP(Util::StringFormat("DebugCone_%d", f));
            conePrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        conePrimitive->forceWireframe(data._descriptor.wireframe);
        conePrimitive->fromCone(data._descriptor);
        conePrimitive->getCommandBuffer(data._descriptor.worldMatrix,bufferInOut);
    }
}

void GFXDevice::debugDrawFrustum(const I64 ID, const IM::FrustumDescriptor descriptor) noexcept {
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
            frustumPrimitive = newIMP(Util::StringFormat("DebugFrustum_%d", f));
            frustumPrimitive->setPipelineDescriptor(getDebugPipeline(data._descriptor));
        }

        frustumPrimitive->forceWireframe(data._descriptor.wireframe);
        frustumPrimitive->fromFrustum(data._descriptor);
        frustumPrimitive->getCommandBuffer(data._descriptor.worldMatrix, bufferInOut);
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
GenericVertexData* GFXDevice::getOrCreateIMGUIBuffer(const I64 windowGUID, const I32 maxCommandCount, const U32 maxVertices) {
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
        GenericVertexData_ptr newBuffer = newGVD(newSize, "IMGUI");
        _IMGUIBuffers[windowGUID] = newBuffer;
        ret = newBuffer.get();
    }

    GenericVertexData::IndexBuffer idxBuff{};
    idxBuff.smallIndices = sizeof(ImDrawIdx) == sizeof(U16);
    idxBuff.count = maxVertices * 3;
    idxBuff.dynamic = true;

    ret->renderIndirect(false);

    GenericVertexData::SetBufferParams params = {};
    params._bindConfig = { 0u, 0u };
    params._useRingBuffer = true;
    params._useAutoSyncObjects = false; // we manually call sync after all draw commands are submitted
    params._initialData = { nullptr, 0 };

    params._bufferParams._elementCount = maxVertices;
    params._bufferParams._elementSize = sizeof(ImDrawVert);
    params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
    params._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;

    ret->setBuffer(params); //Pos, UV and Colour
    ret->setIndexBuffer(idxBuff);

    return ret;
}

RenderTarget_uptr GFXDevice::newRTInternal(const RenderTargetDescriptor& descriptor) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL: {
            return eastl::make_unique<glFramebuffer>(*this, descriptor);
        } break;
        case RenderAPI::Vulkan: {
            return eastl::make_unique<vkRenderTarget>(*this, descriptor);
        } break;
        case RenderAPI::None: {
            return eastl::make_unique<noRenderTarget>(*this, descriptor);
        } break;
    };

    DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));

    return {};
}

RenderTarget_uptr GFXDevice::newRT(const RenderTargetDescriptor& descriptor) {
    RenderTarget_uptr temp{ newRTInternal(descriptor) };

    bool valid = false;
    if (temp != nullptr) {
        valid = temp->create();
        assert(valid);
    }

    return valid ? MOV(temp) : nullptr;
}

IMPrimitive* GFXDevice::newIMP(const Str64& name) {
    ScopedLock<Mutex> w_lock(_imprimitiveMutex);
    return s_IMPrimitivePool.newElement(*this, name);
}

bool GFXDevice::destroyIMP(IMPrimitive*& primitive) {
    if (primitive != nullptr) {
        ScopedLock<Mutex> w_lock(_imprimitiveMutex);
        s_IMPrimitivePool.deleteElement(primitive);
        primitive = nullptr;
        return true;
    }

    return false;
}

VertexBuffer_ptr GFXDevice::newVB(const Str256& name) {
    return std::make_shared<VertexBuffer>(*this, name);
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
    DIVIDE_ASSERT(descriptor._shaderProgramHandle != SHADER_INVALID_HANDLE, "Missing shader handle during pipeline creation!");
    DIVIDE_ASSERT(descriptor._primitiveTopology != PrimitiveTopology::COUNT, "Missing primitive topology during pipeline creation!");

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
            return eastl::make_unique<glShaderBuffer>(*this, descriptor);
        } break;
        case RenderAPI::Vulkan: {
            return eastl::make_unique<vkShaderBuffer>(*this, descriptor);
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

    const RenderTarget* screenRT = _rtPool->getRenderTarget(RenderTargetNames::SCREEN);
    const U16 width = screenRT->getWidth();
    const U16 height = screenRT->getHeight();
    const U8 numChannels = 3;

    static I32 savedImages = 0;
    // compute the new filename by adding the series number and the extension
    const ResourcePath newFilename(Util::StringFormat("Screenshots/%s_%d.tga", filename.c_str(), savedImages));

    // Allocate sufficiently large buffers to hold the pixel data
    const U32 bufferSize = width * height * numChannels;
    vector<U8> imageData(bufferSize, 0u);
    // Read the pixels from the main render target (RGBA16F)
    screenRT->readData(GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE, { (bufferPtr)imageData.data(), imageData.size() });
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
