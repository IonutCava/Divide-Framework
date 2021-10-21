#include "stdafx.h"

#include "config.h"

#include "Headers/GFXDevice.h"

#include "Editor/Headers/Editor.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"

#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"

#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Geometry/Material/Headers/ShaderComputeQueue.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
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
#include "Platform/Video/RenderBackend/OpenGL/Buffers/VertexBuffer/Headers/glVertexArray.h"
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
    constexpr size_t TargetBufferSize = (128 * 1024) / sizeof(GFXShaderData::GPUData);

    constexpr U32 GROUP_SIZE_AABB = 64;
    constexpr U32 MAX_INVOCATIONS_BLUR_SHADER_LAYERED = 4;
};

D64 GFXDevice::s_interpolationFactor = 1.0;
GPUVendor GFXDevice::s_GPUVendor = GPUVendor::COUNT;
GPURenderer GFXDevice::s_GPURenderer = GPURenderer::COUNT;

#pragma region Construction, destruction, initialization
GFXDevice::GFXDevice(Kernel & parent)
    : KernelComponent(parent),
      PlatformContextComponent(parent.platformContext())
{
    _viewport.set(-1);

    Line temp = {};
    temp.widthStart(3.0f);
    temp.widthEnd(3.0f);
    temp.positionStart(VECTOR3_ZERO);

    // Red X-axis
    temp.positionEnd(WORLD_X_AXIS * 2);
    temp.colourStart(UColour4(255, 0, 0, 255));
    temp.colourEnd(UColour4(255, 0, 0, 255));
    _axisLines[0] = temp;

    // Green Y-axis
    temp.positionEnd(WORLD_Y_AXIS * 2);
    temp.colourStart(UColour4(0, 255, 0, 255));
    temp.colourEnd(UColour4(0, 255, 0, 255));
    _axisLines[1] = temp;

    // Blue Z-axis
    temp.positionEnd(WORLD_Z_AXIS * 2);
    temp.colourStart(UColour4(0, 0, 255, 255));
    temp.colourEnd(UColour4(0, 0, 255, 255));
    _axisLines[2] = temp;

    AttribFlags flags{};
    flags.fill(true);
    VertexBuffer::setAttribMasks(to_size(to_base(RenderStage::COUNT) * to_base(RenderPassType::COUNT)), flags);

    // Keep color attribute in depth passes for stuff like vertex-colour based animations (e.g. trees)
    //flags[to_base(AttribLocation::COLOR)] = false; 
    flags[to_base(AttribLocation::NORMAL)] = false;
    flags[to_base(AttribLocation::TANGENT)] = false;

    for (U8 stage = 0; stage < to_base(RenderStage::COUNT); ++stage) {
        VertexBuffer::setAttribMask(RenderStagePass::baseIndex(static_cast<RenderStage>(stage), RenderPassType::PRE_PASS), flags);
    }
    for (U8 pass = 0; pass < to_base(RenderPassType::COUNT); ++pass) {
        VertexBuffer::setAttribMask(RenderStagePass::baseIndex(RenderStage::SHADOW, static_cast<RenderPassType>(pass)), flags);
    }
}

GFXDevice::~GFXDevice()
{
    closeRenderingAPI();
}

ErrorCode GFXDevice::createAPIInstance(const RenderAPI API) {
    assert(_api == nullptr && "GFXDevice error: initRenderingAPI called twice!");

    ErrorCode err = ErrorCode::NO_ERR;
    switch (API) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            _api = eastl::make_unique<GL_API>(*this, API == RenderAPI::OpenGLES);
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

    return ErrorCode::NO_ERR;
}

ErrorCode GFXDevice::postInitRenderingAPI(const vec2<U16> & renderResolution) {
    std::atomic_uint loadTasks = 0;
    ResourceCache* cache = parent().resourceCache();
    const Configuration& config = _parent.platformContext().config();

    // Initialize the shader manager
    ShaderProgram::OnStartup(cache);
    GFX::InitPools();

    // Create a shader buffer to store the GFX rendering info (matrices, options, etc)
    {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._ringBufferLength = TargetBufferSize;
        bufferDescriptor._separateReadWrite = false;
        bufferDescriptor._name = "DVD_GPU_DATA";
        bufferDescriptor._flags = to_base(ShaderBuffer::Flags::EXPLICIT_RANGE_FLUSH);
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._syncAtEndOfCmdBuffer = false;
        bufferDescriptor._bufferParams._usePersistentMapping = false;
        bufferDescriptor._bufferParams._elementSize = sizeof(GFXShaderData::GPUData);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._bufferParams._initialData = { (Byte*)&_gpuBlock._data, bufferDescriptor._bufferParams._elementSize };

        _gfxDataBuffer = newSB(bufferDescriptor);
        _gfxDataBuffer->bind(ShaderBufferLocation::GPU_BLOCK);
    }

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

    // The general purpose render state blocks are both mandatory and must
    // differ from each other at a state hash level
    assert(_stateDepthOnlyRenderingHash != _state2DRenderingHash && "GFXDevice error: Invalid default state hash detected!");
    assert(_state2DRenderingHash != _defaultStateNoDepthHash && "GFXDevice error: Invalid default state hash detected!");
    assert(_defaultStateNoDepthHash != RenderStateBlock::defaultHash() && "GFXDevice error: Invalid default state hash detected!");

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
    TextureDescriptor depthDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);
    TextureDescriptor velocityDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RG, GFXDataFormat::FLOAT_16);
    depthDescriptor.mipCount(1u);
    velocityDescriptor.mipCount(1u);

    //MainPass
    TextureDescriptor screenDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
    TextureDescriptor normalsAndMaterialDataDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
    TextureDescriptor specularDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE);
    screenDescriptor.autoMipMaps(false);
    normalsAndMaterialDataDescriptor.mipCount(1u);
    specularDescriptor.mipCount(1u);

    // Normal and MSAA
    for (U8 i = 0; i < 2; ++i) {
        const U8 sampleCount = i == 0 ? 0 : config.rendering.MSAASamples;
        const size_t screenSampler = i == 0 ? samplerHashMips : samplerHash;
        screenDescriptor.mipCount(i == 0 ? U16_MAX : 1u);

        screenDescriptor.msaaSamples(sampleCount);
        depthDescriptor.msaaSamples(sampleCount);
        normalsAndMaterialDataDescriptor.msaaSamples(sampleCount);
        velocityDescriptor.msaaSamples(sampleCount);
        specularDescriptor.msaaSamples(sampleCount);

        {

            RTAttachmentDescriptors attachments = {
                { screenDescriptor,                 screenSampler, RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO),   DefaultColours::DIVIDE_BLUE },
                { velocityDescriptor,               samplerHash,   RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY), VECTOR4_ZERO },
                { normalsAndMaterialDataDescriptor, samplerHash,   RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES), VECTOR4_ZERO },
                { specularDescriptor,               samplerHash,   RTAttachmentType::Colour, to_U8(ScreenTargets::SPECULAR), VECTOR4_ZERO },
                { depthDescriptor,                  samplerHash,   RTAttachmentType::Depth }
            };

            RenderTargetDescriptor screenDesc = {};
            screenDesc._name = "Screen";
            screenDesc._resolution = renderResolution;
            screenDesc._attachmentCount = to_U8(attachments.size());
            screenDesc._attachments = attachments.data();
            screenDesc._msaaSamples = sampleCount;

            // Our default render targets hold the screen buffer, depth buffer, and a special, on demand, down-sampled version of the depth buffer
            _rtPool->allocateRT(i == 0 ? RenderTargetUsage::SCREEN : RenderTargetUsage::SCREEN_MS, screenDesc);
        }
    }

    {
        //R = SSAO, G = Linear depth
        TextureDescriptor postFXDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RG, GFXDataFormat::FLOAT_16);
        postFXDescriptor.mipCount(1u);

        RTAttachmentDescriptors attachments = {
            { postFXDescriptor, samplerHash, RTAttachmentType::Colour, 0u, vec4<F32>{1.f, 0.f, 0.f, 0.f} }
        };

        RenderTargetDescriptor postFXDesc = {};
        postFXDesc._name = "PostFXData";
        postFXDesc._resolution = renderResolution;
        postFXDesc._attachmentCount = to_U8(attachments.size());
        postFXDesc._attachments = attachments.data();
        postFXDesc._msaaSamples = 0u;
        _rtPool->allocateRT(RenderTargetUsage::POSTFX_DATA, postFXDesc);
    }

    {
        TextureDescriptor ssrDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RGB, GFXDataFormat::FLOAT_16);
        ssrDescriptor.mipCount(1u);

        TextureDescriptor kDkSDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::RG, GFXDataFormat::UNSIGNED_BYTE);
        ssrDescriptor.mipCount(1u);
        
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
    hiZDescriptor.autoMipMaps(false);

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
        editorDescriptor.mipCount(1u);

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

        environmentDescriptorPlanar.mipCount(1u);
        depthDescriptorPlanar.mipCount(1u);

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

            refDesc._attachmentCount = 1; //skip depth
            refDesc._name = "Reflection_blur";
            _rtPool->allocateRT(RenderTargetUsage::REFLECTION_PLANAR_BLUR, refDesc);

        }
    }

    SamplerDescriptor accumulationSampler = {};
    accumulationSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    accumulationSampler.minFilter(TextureFilter::NEAREST);
    accumulationSampler.magFilter(TextureFilter::NEAREST);
    const size_t accumulationSamplerHash = accumulationSampler.getHash();

    TextureDescriptor accumulationDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RGBA, GFXDataFormat::FLOAT_16);
    accumulationDescriptor.mipCount(1u);

    //R = revealage
    TextureDescriptor revealageDescriptor(TextureType::TEXTURE_2D_MS, GFXImageFormat::RED, GFXDataFormat::UNSIGNED_BYTE);
    revealageDescriptor.mipCount(1u);

    RTAttachmentDescriptors oitAttachments = {
        { accumulationDescriptor, accumulationSamplerHash, RTAttachmentType::Colour, to_U8(ScreenTargets::ACCUMULATION), VECTOR4_ZERO },
        { revealageDescriptor,    accumulationSamplerHash, RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE), vec4<F32>{1.f, 0.f, 0.f, 0.f} },
    };

    for (U8 i = 0; i < 2; ++i) 
    {
        const U8 sampleCount = i == 0 ? 0 : config.rendering.MSAASamples;

        oitAttachments[0]._texDescriptor.msaaSamples(sampleCount);
        oitAttachments[1]._texDescriptor.msaaSamples(sampleCount);

        const RenderTarget& screenTarget = _rtPool->renderTarget(i == 0 ? RenderTargetUsage::SCREEN : RenderTargetUsage::SCREEN_MS);
        const RTAttachment_ptr& screenNormalsAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES));
        const RTAttachment_ptr& specularAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Colour, to_U8(ScreenTargets::SPECULAR));
        const RTAttachment_ptr& screenDepthAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Depth, 0);
        
        vector<ExternalRTAttachmentDescriptor> externalAttachments = {
            { screenNormalsAttachment, RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES) },
            { specularAttachment,      RTAttachmentType::Colour, to_U8(ScreenTargets::SPECULAR) },
            { screenDepthAttachment,   RTAttachmentType::Depth }
        };

        if_constexpr(Config::USE_COLOURED_WOIT) {
            const RTAttachment_ptr& screenAttachment = screenTarget.getAttachmentPtr(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO));
            externalAttachments.push_back(
                { screenAttachment,  RTAttachmentType::Colour, to_U8(ScreenTargets::MODULATE) }
            );
        }

        RenderTargetDescriptor oitDesc = {};
        oitDesc._name = "OIT_FULL_RES";
        oitDesc._resolution = renderResolution;
        oitDesc._attachmentCount = to_U8(oitAttachments.size());
        oitDesc._attachments = oitAttachments.data();
        oitDesc._externalAttachmentCount = to_U8(externalAttachments.size());
        oitDesc._externalAttachments = externalAttachments.data();
        oitDesc._msaaSamples = sampleCount;
        _rtPool->allocateRT(i == 0 ? RenderTargetUsage::OIT : RenderTargetUsage::OIT_MS, oitDesc);
    }
    {
        oitAttachments[0]._texDescriptor.msaaSamples(0);
        oitAttachments[1]._texDescriptor.msaaSamples(0);

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
    {
        TextureDescriptor environmentDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE);
        TextureDescriptor depthDescriptorCube(TextureType::TEXTURE_CUBE_ARRAY, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);

        environmentDescriptorCube.mipCount(1u);
        depthDescriptorCube.mipCount(1u);

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
            descriptor._shaderProgramHandle = res->getGUID();
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

        // Initialized our HierarchicalZ construction shader (takes a depth attachment and down-samples it for every mip level)
        ResourceDescriptor descriptor1("HiZConstruct");
        descriptor1.waitForReady(false);
        descriptor1.propertyDescriptor(shaderDescriptor);
        _HIZConstructProgram = CreateResource<ShaderProgram>(cache, descriptor1, loadTasks);
        _HIZConstructProgram->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor pipelineDesc;
            pipelineDesc._stateHash = _stateDepthOnlyRenderingHash;
            pipelineDesc._shaderProgramHandle = _HIZConstructProgram->getGUID();
            _HIZPipeline = newPipeline(pipelineDesc);
        });
    }
    {
        ShaderModuleDescriptor compModule = {};
        compModule._moduleType = ShaderType::COMPUTE;
        compModule._defines.emplace_back(Util::StringFormat("WORK_GROUP_SIZE %d", GROUP_SIZE_AABB), true);
        compModule._sourceFile = "HiZOcclusionCull.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(compModule);

        ResourceDescriptor descriptor2("HiZOcclusionCull");
        descriptor2.waitForReady(false);
        descriptor2.propertyDescriptor(shaderDescriptor);
        _HIZCullProgram = CreateResource<ShaderProgram>(cache, descriptor2, loadTasks);
        _HIZCullProgram->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._shaderProgramHandle = _HIZCullProgram->getGUID();
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
                ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->getGUID();
                _blurBoxPipelineSingleCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
        {
            ShaderProgramDescriptor shaderDescriptorLayered = {};
            shaderDescriptorLayered._modules.push_back(blurVertModule);
            shaderDescriptorLayered._modules.push_back(fragModule);
            shaderDescriptorLayered._modules.back()._variant += ".Layered";
            shaderDescriptorLayered._modules.back()._defines.emplace_back("LAYERED", true);

            ResourceDescriptor blur("BoxBlur_Layered");
            blur.propertyDescriptor(shaderDescriptorLayered);
            _blurBoxShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurBoxShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->getGUID();
                _blurBoxPipelineLayeredCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
    }
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
            shaderDescriptorSingle._modules.back()._defines.emplace_back("GS_MAX_INVOCATIONS 1", true);
            shaderDescriptorSingle._modules.push_back(fragModule);
            shaderDescriptorSingle._modules.back()._defines.emplace_back("GS_MAX_INVOCATIONS 1", true);

            ResourceDescriptor blur("GaussBlur_Single");
            blur.propertyDescriptor(shaderDescriptorSingle);
            _blurGaussianShaderSingle = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurGaussianShaderSingle->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->getGUID();
                _blurGaussianPipelineSingleCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
        {
            ShaderProgramDescriptor shaderDescriptorLayered = {};
            shaderDescriptorLayered._modules.push_back(blurVertModule);
            shaderDescriptorLayered._modules.push_back(geomModule);
            shaderDescriptorLayered._modules.back()._defines.emplace_back(Util::StringFormat("GS_MAX_INVOCATIONS %d", MAX_INVOCATIONS_BLUR_SHADER_LAYERED), true);
            shaderDescriptorLayered._modules.push_back(fragModule);
            shaderDescriptorLayered._modules.back()._defines.emplace_back(Util::StringFormat("GS_MAX_INVOCATIONS %d", MAX_INVOCATIONS_BLUR_SHADER_LAYERED), true);
            shaderDescriptorLayered._modules.back()._variant += ".Layered";
            shaderDescriptorLayered._modules.back()._defines.emplace_back("LAYERED", true);

            ResourceDescriptor blur("GaussBlur_Layered");
            blur.propertyDescriptor(shaderDescriptorLayered);
            _blurGaussianShaderLayered = CreateResource<ShaderProgram>(cache, blur, loadTasks);
            _blurGaussianShaderLayered->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateHash = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = blurShader->getGUID();
                _blurGaussianPipelineLayeredCmd._pipeline = newPipeline(pipelineDescriptor);
            });
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
                pipelineDescriptor._shaderProgramHandle = _displayShader->getGUID();
                _drawFSTexturePipelineCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
        {
            shaderDescriptor._modules.back()._defines.emplace_back("DEPTH_ONLY", true);
            descriptor3.propertyDescriptor(shaderDescriptor);
            _depthShader = CreateResource<ShaderProgram>(cache, descriptor3, loadTasks);
            _depthShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource* res) {
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateHash = _stateDepthOnlyRenderingHash;
                pipelineDescriptor._shaderProgramHandle = _depthShader->getGUID();
                _drawFSDepthPipelineCmd._pipeline = newPipeline(pipelineDescriptor);
            });
        }
    }

    context().paramHandler().setParam<bool>(_ID("rendering.previewDebugViews"), false);
    {
        PipelineDescriptor pipelineDesc;
        pipelineDesc._stateHash = getDefaultStateBlock(true);
        pipelineDesc._shaderProgramHandle = ShaderProgram::DefaultShader()->getGUID();
        _axisGizmoPipeline = newPipeline(pipelineDesc);
        pipelineDesc._stateHash = getDefaultStateBlock(false);
        _debugGizmoPipeline = newPipeline(pipelineDesc);
    }
    _renderer = eastl::make_unique<Renderer>(context(), cache);

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
    DisplayWindow* mainWindow = context().app().windowManager().mainWindow();

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

    if (_axisGizmo) {
        destroyIMP(_axisGizmo);
    }

    _debugLines.reset();
    _debugBoxes.reset();
    _debugFrustums.reset();
    _debugCones.reset();
    _debugSpheres.reset();
    _debugViews.clear();

    // Delete the renderer implementation
    Console::printfn(Locale::Get(_ID("CLOSING_RENDERER")));
    _renderer.reset(nullptr);

    RenderStateBlock::clear();

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

    // Close the shader manager
    MemoryManager::DELETE(_shaderComputeQueue);
    ShaderProgram::OnShutdown();
    _gpuObjectArena.clear();
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
void GFXDevice::idle(const bool fast) const {
    OPTICK_EVENT();

    _api->idle(fast);

    _shaderComputeQueue->idle();
    // Pass the idle call to the post processing system
    _renderer->idle();
    // And to the shader manager
    ShaderProgram::Idle();
}

void GFXDevice::update(const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp) {
    getRenderer().postFX().update(deltaTimeUSFixed, deltaTimeUSApp);
    _gpuBlock._data._renderProperties.w += Time::MicrosecondsToMilliseconds<F32>(deltaTimeUSFixed);
    _gpuBlock._needsUpload = true;
}

void GFXDevice::beginFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

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

    _api->beginFrame(window, global);

    const vec2<U16>& drawableSize = window.getDrawableSize();
    setViewport(0, 0, drawableSize.width, drawableSize.height);
}

void GFXDevice::endFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

    if (global) {
        frameCount(frameCount() + 1);
        frameDrawCallsPrev(frameDrawCalls());
        frameDrawCalls(0u);
    }

    DIVIDE_ASSERT(_cameraSnapshots.empty(), "Not all camera snapshots have been cleared properly! Check command buffers for missmatched push/pop!");
    DIVIDE_ASSERT(_viewportStack.empty(), "Not all viewports have been cleared properly! Check command buffers for missmatched push/pop!");
    // Activate the default render states
    _api->endFrame(window, global);
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
    bool isValidFB;
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
    RenderPassManager* passMgr = parent().renderPassManager();
    params._passName = "CubeMap";
    params._layerParams._type = hasColour ? RTAttachmentType::Colour : RTAttachmentType::Depth;
    params._layerParams._includeDepth = hasColour && hasDepth;
    params._layerParams._index = 0;

    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;

    for (U8 i = 0; i < 6; ++i) {
        // Draw to the current cubemap face
        params._layerParams._layer = i + arrayOffset * 6;

        Camera* camera = cameras[i];
        if (camera == nullptr) {
            camera = Camera::utilityCamera(Camera::UtilityCamera::CUBE);
        }

        // Set a 90 degree horizontal FoV perspective projection
        camera->setProjection(to_F32(aspect), Angle::to_VerticalFoV(Angle::DEGREES<F32>(90.0f), aspect), zPlanes);
        // Point our camera to the correct face
        camera->lookAt(pos, pos + CameraDirections[i].first * zPlanes.max, -CameraDirections[i].second);
        params._camera = camera;
        params._stagePass._pass = i;
        // Pass our render function to the renderer
        passMgr->doCustomPass(params, commandsInOut);
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

    bool isValidFB;
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

    RenderPassManager* passMgr = parent().renderPassManager();
    params._passName = "DualParaboloid";
    params._layerParams._type = hasColour ? RTAttachmentType::Colour : RTAttachmentType::Depth;
    params._layerParams._index = 0;

    const D64 aspect = to_D64(targetResolution.width) / targetResolution.height;
    for (U8 i = 0; i < 2; ++i) {
        Camera* camera = cameras[i];
        if (!camera) {
            camera = Camera::utilityCamera(Camera::UtilityCamera::DUAL_PARABOLOID);
        }

        params._layerParams._layer = i + arrayOffset;

        // Point our camera to the correct face
        camera->lookAt(pos, pos + (i == 0 ? WORLD_Z_NEG_AXIS : WORLD_Z_AXIS) * zPlanes.y);
        // Set a 180 degree vertical FoV perspective projection
        camera->setProjection(to_F32(aspect), Angle::to_VerticalFoV(Angle::DEGREES<F32>(180.0f), aspect), zPlanes);
        // And generated required matrices
        // Pass our render function to the renderer
        params._camera = camera;
        params._stagePass._pass = i;

        passMgr->doCustomPass(params, bufferInOut);
    }
}

void GFXDevice::blurTarget(RenderTargetHandle& blurSource,
                           RenderTargetHandle& blurBuffer,
                           RenderTargetHandle& blurTarget,
                           const RTAttachmentType att,
                           const U8 index,
                           const I32 kernelSize,
                           const bool gaussian,
                           const U8 layerCount,
                           GFX::CommandBuffer& bufferInOut)
{
    const auto& inputAttachment = blurSource._rt->getAttachment(att, index);
    const auto& bufferAttachment = blurBuffer._rt->getAttachment(att, index);

    static GFX::SendPushConstantsCommand s_pushConstantsCommand{};
    static GFX::DrawCommand s_drawCmd{ GenericDrawCommand{} };

    s_drawCmd._drawCommands.front()._primitiveType = gaussian ? PrimitiveType::API_POINTS : PrimitiveType::TRIANGLES;

    const U8 loopCount = gaussian ? 1u : layerCount;

    {// Blur horizontally
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = blurBuffer._targetID;
        renderPassCmd->_name = "BLUR_RENDER_TARGET_HORIZONTAL";

        GFX::EnqueueCommand(bufferInOut, gaussian ? (layerCount > 1 ? _blurGaussianPipelineLayeredCmd : _blurGaussianPipelineSingleCmd)
                                                  : (layerCount > 1 ? _blurBoxPipelineLayeredCmd      : _blurBoxPipelineSingleCmd));

        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ inputAttachment.texture()->data(), inputAttachment.samplerHash(), TextureUsage::UNIT0 });

        s_pushConstantsCommand._constants.set(_ID("verticalBlur"), GFX::PushConstantType::INT, false);
        if (gaussian) {
            vec2<F32> blurSize(1.0f / blurBuffer._rt->getResolution().width, 1.0f / blurBuffer._rt->getResolution().height);
            s_pushConstantsCommand._constants.set(_ID("blurSizes"), GFX::PushConstantType::VEC2, blurSize);
            s_pushConstantsCommand._constants.set(_ID("layerCount"), GFX::PushConstantType::INT, to_I32(layerCount));
            s_pushConstantsCommand._constants.set(_ID("layerOffsetRead"), GFX::PushConstantType::INT, 0);
            s_pushConstantsCommand._constants.set(_ID("layerOffsetWrite"), GFX::PushConstantType::INT, 0);
        } else {
            s_pushConstantsCommand._constants.set(_ID("kernelSize"), GFX::PushConstantType::INT, kernelSize);
            s_pushConstantsCommand._constants.set(_ID("size"), GFX::PushConstantType::VEC2, vec2<F32>(blurBuffer._rt->getResolution()));
            if (layerCount > 1) {
                s_pushConstantsCommand._constants.set(_ID("layer"), GFX::PushConstantType::INT, 0);
            }
        }
        GFX::EnqueueCommand(bufferInOut, s_pushConstantsCommand);

        for (U8 loop = 0u; loop < loopCount; ++loop) {
            if (!gaussian && loop > 0u) {
                s_pushConstantsCommand._constants.set(_ID("layer"), GFX::PushConstantType::INT, to_I32(loop));
                GFX::EnqueueCommand(bufferInOut, s_pushConstantsCommand);
            }
            GFX::EnqueueCommand(bufferInOut, s_drawCmd);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }
    {// Blur vertically
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_target = blurTarget._targetID;
        renderPassCmd->_name = "BLUR_RENDER_TARGET_VERTICAL";

        s_pushConstantsCommand._constants.set(_ID("verticalBlur"), GFX::PushConstantType::INT, true);
        if (gaussian) {
            vec2<F32> blurSize(1.0f / blurTarget._rt->getResolution().width, 1.0f / blurTarget._rt->getResolution().height);
            s_pushConstantsCommand._constants.set(_ID("blurSizes"), GFX::PushConstantType::VEC2, blurSize);
        } else {
            s_pushConstantsCommand._constants.set(_ID("size"), GFX::PushConstantType::VEC2, vec2<F32>(blurTarget._rt->getResolution()));
        }

        GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ bufferAttachment.texture()->data(), bufferAttachment.samplerHash(), TextureUsage::UNIT0 });
        GFX::EnqueueCommand(bufferInOut, s_pushConstantsCommand);

        for (U8 loop = 0u; loop < loopCount; ++loop) {
            if (!gaussian && loop > 0u) {
                s_pushConstantsCommand._constants.set(_ID("layer"), GFX::PushConstantType::INT, to_I32(loop));
                GFX::EnqueueCommand(bufferInOut, s_pushConstantsCommand);
            }
            GFX::EnqueueCommand(bufferInOut, s_drawCmd);
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
    auto compare = [](const vec2<U16>& a, const vec2<U16>& b) noexcept -> bool {
        return a.x > b.x || a.y > b.y;
    };

    WindowManager& winManager = _parent.platformContext().app().windowManager();

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
    WindowManager& winManager = _parent.platformContext().app().windowManager();

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

void GFXDevice::setScreenMSAASampleCount(U8 sampleCount) {
    CLAMP(sampleCount, to_U8(0u), gpuState().maxMSAASampleCount());
    if (_context.config().rendering.MSAASamples != sampleCount) {
        _context.config().rendering.MSAASamples = sampleCount;
        _rtPool->updateSampleCount(RenderTargetUsage::SCREEN_MS, sampleCount);
        _rtPool->updateSampleCount(RenderTargetUsage::OIT_MS, sampleCount);
    }
}

void GFXDevice::setShadowMSAASampleCount(const ShadowType type, U8 sampleCount) {
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

        // Update render targets with the new resolution
        _rtPool->resizeTargets(RenderTargetUsage::SCREEN, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SCREEN_MS, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::POSTFX_DATA, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::SSR_RESULT, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::HI_Z, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::OIT, w, h);
        _rtPool->resizeTargets(RenderTargetUsage::OIT_MS, w, h);
        if_constexpr(Config::Build::ENABLE_EDITOR) {
            _rtPool->resizeTargets(RenderTargetUsage::EDITOR, w, h);
        }

        // Update post-processing render targets and buffers
        _renderer->updateResolution(w, h);

        // Update the 2D camera so it matches our new rendering viewport
        if (Camera::utilityCamera(Camera::UtilityCamera::_2D)->setProjection(vec4<F32>(0, to_F32(w), 0, to_F32(h)), vec2<F32>(-1, 1))) {
            Camera::utilityCamera(Camera::UtilityCamera::_2D)->updateFrustum();
        }
        if (Camera::utilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->setProjection(vec4<F32>(0, to_F32(w), to_F32(h), 0), vec2<F32>(-1, 1))) {
            Camera::utilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->updateFrustum();
        }
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
void GFXDevice::uploadGPUBlock() {
    OPTICK_EVENT();

    if (_gpuBlock._needsUpload) {
        _gpuBlock._needsUpload = false;
        _gpuBlock._data._otherProperties.x = to_F32(materialDebugFlag());
        _gfxDataBuffer->writeData(&_gpuBlock._data);
        _gfxDataBuffer->bind(ShaderBufferLocation::GPU_BLOCK);
        _gfxDataBuffer->incQueue();
    }
}

/// set a new list of clipping planes. The old one is discarded
void GFXDevice::setClipPlanes(const FrustumClipPlanes& clipPlanes) {
    if (clipPlanes != _clippingPlanes){
        _clippingPlanes = clipPlanes;

        auto& planes = _clippingPlanes.planes();
        auto& states = _clippingPlanes.planeState();

        U8 count = 0;
        for (U8 i = 0; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
            if (states[i]) {
                _gpuBlock._data._clipPlanes[count++] = planes[i];
                if (count == Config::MAX_CLIP_DISTANCES) {
                    break;
                }
            }
        }

        _gpuBlock._data._otherProperties.w = to_F32(count);
        _gpuBlock._needsUpload = true;
    }
}

void GFXDevice::renderFromCamera(const CameraSnapshot& cameraSnapshot) {
    OPTICK_EVENT();
    constexpr F32 reserved = 1.f;

    GFXShaderData::GPUData& data = _gpuBlock._data;

    bool needsUpdate = false, projectionDirty = false, viewDirty = false;
    if (cameraSnapshot._projectionMatrix != data._ProjectionMatrix) {
        data._ProjectionMatrix.set(cameraSnapshot._projectionMatrix);
        data._ProjectionMatrix.getInverse(data._InvProjectionMatrix);
        projectionDirty = true;
    }

    if (cameraSnapshot._viewMatrix != data._ViewMatrix) {
        data._ViewMatrix.set(cameraSnapshot._viewMatrix);
        data._ViewMatrix.getInverse(data._InvViewMatrix);
        viewDirty = true;
    }

    if (projectionDirty || viewDirty) {
        mat4<F32>::Multiply(data._ViewMatrix, data._ProjectionMatrix, data._ViewProjectionMatrix);

        data._frustumPlanes = cameraSnapshot._frustumPlanes;
        needsUpdate = true;
    }

    const vec4<F32> cameraPos(cameraSnapshot._eye, cameraSnapshot._aspectRatio);
    if (cameraPos != data._cameraPosition) {
        data._cameraPosition.set(cameraPos);
        needsUpdate = true;
    }

    const vec4<F32> cameraProperties(cameraSnapshot._zPlanes, cameraSnapshot._FoV, data._renderProperties.w);
    if (data._renderProperties != cameraProperties) {
        data._renderProperties.set(cameraProperties);

        if (cameraSnapshot._isOrthoCamera) {
            data._lightingProperties.z = 1.f; //scale
            data._lightingProperties.w = 0.f; //bias
        } else {
            const F32 gridSizeZ = to_F32(_context.config().rendering.lightClusteredSizes.z);
            const F32 zFar = cameraSnapshot._zPlanes.max;
            const F32 zNear = cameraSnapshot._zPlanes.min;
            const F32 zLogRatio = std::log2f(zFar / zNear);

            data._lightingProperties.z = cameraSnapshot._isOrthoCamera ? 1.0f : gridSizeZ / zLogRatio; //scale
            data._lightingProperties.w = cameraSnapshot._isOrthoCamera ? 0.0f : -(gridSizeZ * std::log2f(zNear) / zLogRatio); //bias
        }
        needsUpdate = true;
    }

    if (to_U8(data._otherProperties.z) != cameraSnapshot._flag) {
        data._otherProperties.z = to_F32(cameraSnapshot._flag);
        needsUpdate = true;
    }

    if (needsUpdate) {
        _gpuBlock._needsUpload = true;
        _activeCameraSnapshot = cameraSnapshot;
    }
}

/// Update the rendering viewport
bool GFXDevice::setViewport(const Rect<I32>& viewport) {
    OPTICK_EVENT();

    // Change the viewport on the Rendering API level
    if (_api->setViewport(viewport)) {
        // Update the buffer with the new value
        _gpuBlock._data._ViewPort.set(viewport.x, viewport.y, viewport.z, viewport.w);
        const F32 viewportWidth = to_F32(viewport.z);
        const F32 viewportHeight= to_F32(viewport.w);
        const U32 gridSizeX = to_U32(context().config().rendering.lightClusteredSizes.x);
        const U32 gridSizeY = to_U32(context().config().rendering.lightClusteredSizes.y);
        const F32 clusterSizeX = std::ceil(viewportWidth  / gridSizeX);
        const F32 clusterSizeY = std::ceil(viewportHeight / gridSizeY);
        _gpuBlock._data._lightingProperties.x = clusterSizeX;
        _gpuBlock._data._lightingProperties.y = clusterSizeY;
        _gpuBlock._needsUpload = true;
        _viewport.set(viewport);

        return true;
    }

    return false;
}

void GFXDevice::setPreviousViewProjection(const mat4<F32>& view, const mat4<F32>& projection) noexcept {
    mat4<F32>::Multiply(view, projection, _gpuBlock._data._PreviousViewProjectionMatrix);
    _gpuBlock._needsUpload = true;
}

mat4<F32> GFXDevice::getPreviousViewProjection() const {
    return _gpuBlock._data._PreviousViewProjectionMatrix;
}

const GFXShaderData::GPUData& GFXDevice::renderingData() const noexcept {
    return _gpuBlock._data;
}

#pragma endregion

#pragma region Command buffers, occlusion culling, etc
void GFXDevice::flushCommandBuffer(GFX::CommandBuffer& commandBuffer, const bool batch) {
    OPTICK_EVENT("FLUSHING COMMAND BUFFER");

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        DIVIDE_ASSERT(Runtime::isMainThread(), "GFXDevice::flushCommandBuffer called from worker thread!");
    }

    if (batch) {
        commandBuffer.batch();
    }

    const GFX::ErrorType error = commandBuffer.validate();
    if (error != GFX::ErrorType::NONE) {
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_INVALID_COMMAND_BUFFER")), commandBuffer.toString().c_str());
        DIVIDE_UNEXPECTED_CALL_MSG(Util::StringFormat("GFXDevice::flushCommandBuffer error [ %s ]: Invalid command buffer. Check error log!", GFX::Names::errorType[to_base(error)]).c_str());
        return;
    }

    _api->preFlushCommandBuffer(commandBuffer);

    const GFX::CommandBuffer::CommandOrderContainer& commands = commandBuffer();
    for (const GFX::CommandBuffer::CommandEntry& cmd : commands) {
        const GFX::CommandType cmdType = static_cast<GFX::CommandType>(cmd._typeIndex);
        switch (cmdType) {
            case GFX::CommandType::BLIT_RT: {
                GFX::BlitRenderTargetCommand* crtCmd = commandBuffer.get<GFX::BlitRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd->_source);
                RenderTarget& destination = renderTargetPool().renderTarget(crtCmd->_destination);

                RenderTarget::RTBlitParams params = {};
                params._inputFB = &source;
                params._blitDepth = crtCmd->_blitDepth;
                params._blitColours = crtCmd->_blitColours;

                destination.blitFrom(params);
            } break;
            case GFX::CommandType::CLEAR_RT: {
                const GFX::ClearRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ClearRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._target);
                source.clear(crtCmd._descriptor);
            }break;
            case GFX::CommandType::RESET_RT: {
                const GFX::ResetRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ResetRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._source);
                source.setDefaultState(crtCmd._descriptor);
            } break;
            case GFX::CommandType::RESET_AND_CLEAR_RT: {
                const GFX::ResetAndClearRenderTargetCommand& crtCmd = *commandBuffer.get<GFX::ResetAndClearRenderTargetCommand>(cmd);
                RenderTarget& source = renderTargetPool().renderTarget(crtCmd._source);
                source.setDefaultState(crtCmd._drawDescriptor);
                source.clear(crtCmd._clearDescriptor);
            } break;
            case GFX::CommandType::CLEAR_TEXTURE: {
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
                const GFX::ReadBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ReadBufferDataCommand>(cmd);
                if (crtCmd._buffer != nullptr && crtCmd._target != nullptr) {
                    crtCmd._buffer->readData(crtCmd._offsetElementCount, crtCmd._elementCount, crtCmd._target);
                }
            } break;
            case GFX::CommandType::CLEAR_BUFFER_DATA: {
                const GFX::ClearBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ClearBufferDataCommand>(cmd);
                if (crtCmd._buffer != nullptr) {
                    crtCmd._buffer->clearData(crtCmd._offsetElementCount, crtCmd._elementCount);
                }
            } break;
            case GFX::CommandType::SET_VIEWPORT:
                setViewport(commandBuffer.get<GFX::SetViewportCommand>(cmd)->_viewport);
                break;
            case GFX::CommandType::PUSH_VIEWPORT: {
                GFX::PushViewportCommand* crtCmd = commandBuffer.get<GFX::PushViewportCommand>(cmd);
                _viewportStack.push(_viewport);
                setViewport(crtCmd->_viewport);
            } break;
            case GFX::CommandType::POP_VIEWPORT: {
                setViewport(_viewportStack.top());
                _viewportStack.pop();
            } break;
            case GFX::CommandType::SET_CAMERA: {
                GFX::SetCameraCommand* crtCmd = commandBuffer.get<GFX::SetCameraCommand>(cmd);
                // Tell the Rendering API to draw from our desired PoV
                renderFromCamera(crtCmd->_cameraSnapshot);
            } break;
            case GFX::CommandType::PUSH_CAMERA: {
                GFX::PushCameraCommand* crtCmd = commandBuffer.get<GFX::PushCameraCommand>(cmd);
                DIVIDE_ASSERT(_cameraSnapshots.size() < _cameraSnapshots._Get_container().max_size(), "GFXDevice::flushCommandBuffer error: PUSH_CAMERA stack too deep!");

                _cameraSnapshots.push(_activeCameraSnapshot);
                renderFromCamera(crtCmd->_cameraSnapshot);
            } break;
            case GFX::CommandType::POP_CAMERA: {
                renderFromCamera(_cameraSnapshots.top());
                _cameraSnapshots.pop();
            } break;
            case GFX::CommandType::SET_CLIP_PLANES:
                setClipPlanes(commandBuffer.get<GFX::SetClipPlanesCommand>(cmd)->_clippingPlanes);
                break;
            case GFX::CommandType::EXTERNAL:
                uploadGPUBlock();
                commandBuffer.get<GFX::ExternalCommand>(cmd)->_cbk();
                break;

            case GFX::CommandType::DRAW_TEXT:
            case GFX::CommandType::DRAW_IMGUI:
            case GFX::CommandType::DRAW_COMMANDS:
            case GFX::CommandType::DISPATCH_COMPUTE:
                uploadGPUBlock(); /*no break. fall-through*/

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
    static GFX::SendPushConstantsCommand s_pushConstantsCommand{};
    assert(depthBuffer != HiZTarget);

    GenericDrawCommand drawCmd = {};
    drawCmd._primitiveType = PrimitiveType::TRIANGLES;

    // The depth buffer's resolution should be equal to the screen's resolution
    RenderTarget& renderTarget = _rtPool->renderTarget(HiZTarget);
    const U16 width = renderTarget.getWidth();
    const U16 height = renderTarget.getHeight();
    U16 level = 0;
    U16 dim = width > height ? width : height;

    // Store the current width and height of each mip
    const Rect<I32> previousViewport(_viewport);

    GFX::EnqueueCommand(cmdBufferInOut, GFX::BeginDebugScopeCommand{ "Construct Hi-Z" });

    GFX::ClearRenderTargetCommand* clearCmd = GFX::EnqueueCommand<GFX::ClearRenderTargetCommand>(cmdBufferInOut);
    clearCmd->_target = HiZTarget;
    clearCmd->_descriptor = {};
    clearCmd->_descriptor.clearDepth(true);
    clearCmd->_descriptor.clearColours(true);

    { // Copy depth buffer to the colour target for compute shaders to use later on

        GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(cmdBufferInOut);
        beginRenderPassCmd->_target = HiZTarget;
        beginRenderPassCmd->_descriptor = {};
        beginRenderPassCmd->_name = "CONSTRUCT_HI_Z_DEPTH";
        

        const auto& att = _rtPool->renderTarget(depthBuffer).getAttachment(RTAttachmentType::Depth, 0);
        drawTextureInViewport(att.texture()->data(),
                              att.samplerHash(),
                              Rect<I32>{0, 0, renderTarget.getWidth(), renderTarget.getHeight()},
                              false,
                              true,
                              cmdBufferInOut);

        GFX::EnqueueCommand(cmdBufferInOut, GFX::EndRenderPassCommand{});
    }

    const auto& att = renderTarget.getAttachment(RTAttachmentType::Depth, 0);
    const Texture_ptr& hizDepthTex = att.texture();

    const TextureData hizData = hizDepthTex->data();
    if (!hizDepthTex->descriptor().autoMipMaps()) {
        // We use a special shader that downsamples the buffer
        // We will use a state block that disables colour writes as we will render only a depth image,
        // disables depth testing but allows depth writes
        GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(cmdBufferInOut);
        beginRenderPassCmd->_name = "CONSTRUCT_HI_Z";
        beginRenderPassCmd->_target = HiZTarget;
        beginRenderPassCmd->_descriptor = {};
        beginRenderPassCmd->_descriptor.setViewport(false);
        beginRenderPassCmd->_descriptor.drawMask().disableAll();
        beginRenderPassCmd->_descriptor.drawMask().setEnabled(RTAttachmentType::Depth, 0, true);

        GFX::EnqueueCommand(cmdBufferInOut, GFX::PushCameraCommand{ Camera::utilityCamera(Camera::UtilityCamera::_2D)->snapshot() });

        GFX::ComputeMipMapsCommand computeMipMapsCommand{};
        computeMipMapsCommand._texture   = hizDepthTex.get();
        computeMipMapsCommand._clearOnly = true;
        GFX::EnqueueCommand(cmdBufferInOut, computeMipMapsCommand);

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

                s_pushConstantsCommand._constants.set(_ID("depthInfo"),   GFX::PushConstantType::IVEC2, vec2<I32>(level - 1, wasEven ? 1 : 0));
                s_pushConstantsCommand._constants.set(_ID("LastMipSize"), GFX::PushConstantType::IVEC2, vec2<I32>(owidth, oheight));
                GFX::EnqueueCommand(cmdBufferInOut, s_pushConstantsCommand);

                // Dummy draw command as the full screen quad is generated completely in the vertex shader
                GFX::EnqueueCommand(cmdBufferInOut, GFX::DrawCommand{ drawCmd });

                GFX::EnqueueCommand(cmdBufferInOut, GFX::EndRenderSubPassCommand{});
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

        // Restore mip level
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(cmdBufferInOut)->_mipWriteLevel = 0u;

        GFX::EnqueueCommand(cmdBufferInOut, GFX::EndRenderSubPassCommand{});

        // Restore viewport
        GFX::EnqueueCommand(cmdBufferInOut, GFX::SetViewportCommand{previousViewport});

        // Restore camera
        GFX::EnqueueCommand(cmdBufferInOut, GFX::PopCameraCommand{});

        GFX::EnqueueCommand(cmdBufferInOut, GFX::EndRenderPassCommand{});
    }

    GFX::EnqueueCommand(cmdBufferInOut, GFX::EndDebugScopeCommand{});

    return { hizDepthTex, att.samplerHash() };
}

void GFXDevice::occlusionCull(const RenderStagePass& stagePass,
                              const RenderPass::BufferData& bufferData,
                              const Texture_ptr& depthBuffer,
                              const size_t samplerHash,
                              GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                              GFX::CommandBuffer& bufferInOut) const
{
    OPTICK_EVENT();

    ACKNOWLEDGE_UNUSED(stagePass);

    const U32 cmdCount = *bufferData._lastCommandCount;
    const U32 threadCount = (cmdCount + GROUP_SIZE_AABB - 1) / GROUP_SIZE_AABB;
    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Occlusion Cull" });

    // Not worth the overhead for a handful of items and the Pre-Z pass should handle overdraw just fine
    if (threadCount < 3u) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Skipped. Node count too low." });
        GFX::EnqueueCommand(bufferInOut, GFX::EndDebugScopeCommand{});
    } else {
        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _HIZCullPipeline });

        ShaderBufferBinding shaderBuffer = {};
        shaderBuffer._binding = ShaderBufferLocation::GPU_COMMANDS;
        shaderBuffer._buffer = bufferData._commandBuffer;
        shaderBuffer._elementRange = { bufferData._commandElementOffset, cmdCount };

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._textureData.add(TextureEntry{ depthBuffer->data(), samplerHash, TextureUsage::UNIT0 });
        set._buffers.add(shaderBuffer);

        if (bufferData._cullCounterBuffer != nullptr) {
            ShaderBufferBinding atomicCount = {};
            atomicCount._binding = ShaderBufferLocation::ATOMIC_COUNTER;
            atomicCount._buffer = bufferData._cullCounterBuffer;
            atomicCount._elementRange.set(0, 1);
            set._buffers.add(atomicCount); // Atomic counter should be cleared by this point
        }

        HIZPushConstantsCMDInOut._constants.set(_ID("dvd_countCulledItems"), GFX::PushConstantType::UINT, bufferData._cullCounterBuffer != nullptr ? 1u : 0u);
        HIZPushConstantsCMDInOut._constants.set(_ID("dvd_numEntities"), GFX::PushConstantType::UINT, cmdCount);
        HIZPushConstantsCMDInOut._constants.set(_ID("dvd_viewSize"), GFX::PushConstantType::VEC2, vec2<F32>(depthBuffer->width(), depthBuffer->height()));

        GFX::EnqueueCommand(bufferInOut, HIZPushConstantsCMDInOut);

        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ threadCount, 1, 1 });
    }

    GFX::EnqueueCommand(bufferInOut, GFX::EndDebugScopeCommand{});
}

void GFXDevice::updateCullCount(const RenderPass::BufferData& bufferData, GFX::CommandBuffer& cmdBufferInOut) {
    if (queryPerformanceStats() && bufferData._cullCounterBuffer != nullptr) {
          GFX::ReadBufferDataCommand readAtomicCounter;
          readAtomicCounter._buffer = bufferData._cullCounterBuffer;
          readAtomicCounter._target = &_lastCullCount;
          readAtomicCounter._offsetElementCount = 0;
          readAtomicCounter._elementCount = 1;
          EnqueueCommand(cmdBufferInOut, readAtomicCounter);
      }
}
#pragma endregion

#pragma region Drawing functions
void GFXDevice::drawText(const TextElementBatch& batch, GFX::CommandBuffer& bufferInOut) const {
    drawText(GFX::DrawTextCommand{ batch }, bufferInOut);
}

void GFXDevice::drawText(const GFX::DrawTextCommand& cmd, GFX::CommandBuffer& bufferInOut) const {
    EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::utilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _textRenderPipeline });
    EnqueueCommand(bufferInOut, GFX::SendPushConstantsCommand{ _textRenderConstants });
    EnqueueCommand(bufferInOut, cmd);
    EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
}

void GFXDevice::drawText(const TextElementBatch& batch) {
    GFX::ScopedCommandBuffer sBuffer(GFX::AllocateScopedCommandBuffer());
    GFX::CommandBuffer& buffer = sBuffer();

    // Assume full game window viewport for text
    GFX::SetViewportCommand viewportCommand;
    const RenderTarget& screenRT = _rtPool->screenTarget();
    const U16 width = screenRT.getWidth();
    const U16 height = screenRT.getHeight();
    viewportCommand._viewport.set(0, 0, width, height);
    EnqueueCommand(buffer, viewportCommand);

    drawText(batch, buffer);
    flushCommandBuffer(sBuffer());
}

void GFXDevice::drawTextureInViewport(const TextureData data, const size_t samplerHash, const Rect<I32>& viewport, const bool convertToSrgb, const bool drawToDepthOnly, GFX::CommandBuffer& bufferInOut) {
    static GFX::BeginDebugScopeCommand   s_beginDebugScopeCmd = { "Draw Texture In Viewport" };
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBTrue{ PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, true}}};
    static GFX::SendPushConstantsCommand s_pushConstantsSRGBFalse{ PushConstants{{_ID("convertToSRGB"), GFX::PushConstantType::BOOL, false}}};

    GenericDrawCommand drawCmd = {};
    drawCmd._primitiveType = PrimitiveType::TRIANGLES;

    GFX::EnqueueCommand(bufferInOut, s_beginDebugScopeCmd);
    GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ Camera::utilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    GFX::EnqueueCommand(bufferInOut, drawToDepthOnly ? _drawFSDepthPipelineCmd : _drawFSTexturePipelineCmd);
    GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ data, samplerHash, TextureUsage::UNIT0 });
    GFX::EnqueueCommand(bufferInOut, GFX::PushViewportCommand{ viewport });

    if (!drawToDepthOnly) {
        GFX::EnqueueCommand(bufferInOut, convertToSrgb ? s_pushConstantsSRGBTrue : s_pushConstantsSRGBFalse);
    }

    GFX::EnqueueCommand(bufferInOut, GFX::DrawCommand{ drawCmd });
    GFX::EnqueueCommand(bufferInOut, GFX::PopViewportCommand{});
    GFX::EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
    GFX::EnqueueCommand(bufferInOut, GFX::EndDebugScopeCommand{});
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
                targetViewport.z - padding * 2,
                targetViewport.w - padding * 2),
            padding,
            bufferInOut);

        EnqueueCommand(bufferInOut, GFX::EndDebugScopeCommand{});
    }
}

namespace {
    DebugView* HiZView = nullptr;
};

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
        HiZ->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::HI_Z)).getAttachment(RTAttachmentType::Depth, 0).texture();
        HiZ->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::HI_Z)).getAttachment(RTAttachmentType::Depth, 0).samplerHash();
        HiZ->_name = "Hierarchical-Z";
        HiZ->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, to_F32(HiZ->_texture->descriptor().mipCount() - 1));
        HiZ->_shaderData.set(_ID("zPlanes"), GFX::PushConstantType::VEC2, vec2<F32>(Camera::s_minNearZ, _context.config().runtime.cameraViewDistance));

        DebugView_ptr DepthPreview = std::make_shared<DebugView>();
        DepthPreview->_shader = _previewDepthMapShader;
        DepthPreview->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Depth, 0).texture();
        DepthPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Depth, 0).samplerHash();
        DepthPreview->_name = "Depth Buffer";
        DepthPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        DepthPreview->_shaderData.set(_ID("zPlanes"), GFX::PushConstantType::VEC2, vec2<F32>(Camera::s_minNearZ, _context.config().runtime.cameraViewDistance));

        DebugView_ptr NormalPreview = std::make_shared<DebugView>();
        NormalPreview->_shader = _renderTargetDraw;
        NormalPreview->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES)).texture();
        NormalPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES)).samplerHash();
        NormalPreview->_name = "Normals";
        NormalPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        NormalPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, true);
        NormalPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        NormalPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 2u);
        NormalPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);  
        
        DebugView_ptr SpecularPreview = std::make_shared<DebugView>();
        SpecularPreview->_shader = _renderTargetDraw;
        SpecularPreview->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::SPECULAR)).texture();
        SpecularPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::SPECULAR)).samplerHash();
        SpecularPreview->_name = "Specular";
        SpecularPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        SpecularPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        SpecularPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        SpecularPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 3u);
        SpecularPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr VelocityPreview = std::make_shared<DebugView>();
        VelocityPreview->_shader = _renderTargetDraw;
        VelocityPreview->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY)).texture();
        VelocityPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::VELOCITY)).samplerHash();
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
        SSAOPreview->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::POSTFX_DATA)).getAttachment(RTAttachmentType::Colour, 0u).texture();
        SSAOPreview->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::POSTFX_DATA)).getAttachment(RTAttachmentType::Colour, 0u).samplerHash();
        SSAOPreview->_name = "SSAO Map";
        SSAOPreview->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        SSAOPreview->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        SSAOPreview->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        SSAOPreview->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 1u);
        SSAOPreview->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaAccumulationHigh = std::make_shared<DebugView>();
        AlphaAccumulationHigh->_shader = _renderTargetDraw;
        AlphaAccumulationHigh->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::OIT)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO)).texture();
        AlphaAccumulationHigh->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::OIT)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::ALBEDO)).samplerHash();
        AlphaAccumulationHigh->_name = "Alpha Accumulation High";
        AlphaAccumulationHigh->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, 0.0f);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelsArePacked"), GFX::PushConstantType::BOOL, false);
        AlphaAccumulationHigh->_shaderData.set(_ID("startChannel"), GFX::PushConstantType::UINT, 0u);
        AlphaAccumulationHigh->_shaderData.set(_ID("channelCount"), GFX::PushConstantType::UINT, 4u);
        AlphaAccumulationHigh->_shaderData.set(_ID("multiplier"), GFX::PushConstantType::FLOAT, 1.0f);

        DebugView_ptr AlphaRevealageHigh = std::make_shared<DebugView>();
        AlphaRevealageHigh->_shader = _renderTargetDraw;
        AlphaRevealageHigh->_texture = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::OIT)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE)).texture();
        AlphaRevealageHigh->_samplerHash = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::OIT)).getAttachment(RTAttachmentType::Colour, to_U8(ScreenTargets::REVEALAGE)).samplerHash();
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

        HiZView = addDebugView(HiZ);
        addDebugView(DepthPreview);
        addDebugView(NormalPreview);
        addDebugView(SpecularPreview);
        addDebugView(VelocityPreview);
        addDebugView(SSAOPreview);
        addDebugView(AlphaAccumulationHigh);
        addDebugView(AlphaRevealageHigh);
        addDebugView(Luminance);
        addDebugView(Edges);
        WAIT_FOR_CONDITION(_previewDepthMapShader->getState() == ResourceState::RES_LOADED);
    }
}

void GFXDevice::renderDebugViews(Rect<I32> targetViewport, const I32 padding, GFX::CommandBuffer& bufferInOut) {
    static size_t labelStyleHash = TextLabelStyle(Font::DROID_SERIF_BOLD, UColour4(128), 96).getHash();

    GenericDrawCommand drawCmd = {};
    drawCmd._primitiveType = PrimitiveType::TRIANGLES;

    initDebugViews();

    if (HiZView) {
        //HiZ preview
        I32 LoDLevel = 0;
        RenderTarget& HiZRT = renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::HI_Z));
        LoDLevel = to_I32(std::ceil(Time::Game::ElapsedMilliseconds() / 750.0f)) % (HiZRT.getAttachment(RTAttachmentType::Depth, 0).texture()->descriptor().mipCount() - 1);
        HiZView->_shaderData.set(_ID("lodLevel"), GFX::PushConstantType::FLOAT, to_F32(LoDLevel));
    }

    constexpr I32 maxViewportColumnCount = 10;
    I32 viewCount = to_I32(_debugViews.size());
    for (const auto& view : _debugViews) {
        if (!view->_enabled) {
            --viewCount;
        }
    }

    if (viewCount == 0) {
        return;
    }

    const I32 columnCount = viewCount < 4 ? 4 : std::min(viewCount, maxViewportColumnCount);
    I32 rowCount = viewCount / maxViewportColumnCount;
    if (viewCount % maxViewportColumnCount > 0) {
        rowCount++;
    }

    const I32 screenWidth = targetViewport.z - targetViewport.x;
    const I32 screenHeight = targetViewport.w - targetViewport.y;
    const F32 aspectRatio = to_F32(screenWidth) / screenHeight;

    const I32 viewportWidth = screenWidth / columnCount - padding;
    const I32 viewportHeight = to_I32(viewportWidth / aspectRatio) - padding;
    Rect<I32> viewport(screenWidth - viewportWidth, targetViewport.y, viewportWidth, viewportHeight);

    PipelineDescriptor pipelineDesc = {};
    pipelineDesc._stateHash = _state2DRenderingHash;

    vector_fast <std::pair<string, Rect<I32>>> labelStack;

    GFX::SetViewportCommand setViewport = {};
    GFX::SendPushConstantsCommand pushConstants = {};
    GFX::BindPipelineCommand bindPipeline = {};

    const Rect<I32> previousViewport(_viewport);

    I16 idx = 0;
    for (I16 i = 0; i < to_I16(_debugViews.size()); ++i) {
        DebugView& view = *_debugViews[i];

        if (!view._enabled) {
            continue;
        }

        pipelineDesc._shaderProgramHandle = view._shader->getGUID();

        bindPipeline._pipeline = newPipeline(pipelineDesc);
        EnqueueCommand(bufferInOut, bindPipeline);

        pushConstants._constants = view._shaderData;
        EnqueueCommand(bufferInOut, pushConstants);

        setViewport._viewport.set(viewport);
        EnqueueCommand(bufferInOut, setViewport);

        GFX::BindDescriptorSetsCommand bindDescriptorSets = {};
        bindDescriptorSets._set._textureData.add(TextureEntry{ view._texture->data(), view._samplerHash, view._textureBindSlot });
        EnqueueCommand(bufferInOut, bindDescriptorSets);

        EnqueueCommand(bufferInOut, GFX::DrawCommand{ drawCmd });

        if (!view._name.empty()) {
            labelStack.emplace_back(view._name, viewport);
        }

        if (idx > 0 &&  idx % (columnCount - 1) == 0) {
            viewport.y += viewportHeight + targetViewport.y;
            viewport.x += viewportWidth * columnCount + targetViewport.x * columnCount;
        }
             
        viewport.x -= viewportWidth + targetViewport.x;
        ++idx;
    }

    TextElement text(labelStyleHash, RelativePosition2D(RelativeValue(0.1f, 0.0f), RelativeValue(0.1f, 0.0f)));
    for (const auto& [labelText, targetViewRect] : labelStack) {
        // Draw labels at the end to reduce number of state changes
        setViewport._viewport.set(targetViewRect);
        EnqueueCommand(bufferInOut, setViewport);

        text.position().d_y.d_offset = targetViewRect.sizeY - 10.0f;
        text.text(labelText.c_str(), false);

        TextElementBatch batch{ text };
        drawText(GFX::DrawTextCommand{ batch }, bufferInOut);
    }

    setViewport._viewport.set(previousViewport);
    EnqueueCommand(bufferInOut, setViewport);
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

void GFXDevice::debugDrawLines(const Line* lines, const size_t count) {
    _debugLines.add({ lines, count});
}

void GFXDevice::debugDrawLines(GFX::CommandBuffer& bufferInOut) {
    const U32 lineCount = _debugLines._Id.load();
    for (U32 f = 0; f < lineCount; ++f) {
        IMPrimitive*& linePrimitive = _debugLines._debugPrimitives[f];
        if (linePrimitive == nullptr) {
            linePrimitive = newIMP();
            linePrimitive->name(Util::StringFormat("DebugLine_%d", f));
            linePrimitive->pipeline(*_debugGizmoPipeline);
            linePrimitive->skipPostFX(true);
        }

        const auto&[lines, count] = _debugLines._debugData[f];

        linePrimitive->fromLines(lines, count);
        bufferInOut.add(linePrimitive->toCommandBuffer());
    }

    _debugLines._Id.store(0u);
}

void GFXDevice::debugDrawBox(const vec3<F32>& min, const vec3<F32>& max, const FColour3& colour) {
    _debugBoxes.add({ min, max, colour });
}

void GFXDevice::debugDrawBoxes(GFX::CommandBuffer& bufferInOut) {
    const U32 boxesCount = _debugBoxes._Id.load();
    for (U32 f = 0; f < boxesCount; ++f) {
        IMPrimitive*& boxPrimitive = _debugBoxes._debugPrimitives[f];
        if (boxPrimitive == nullptr) {
            boxPrimitive = newIMP();
            boxPrimitive->name(Util::StringFormat("DebugBox_%d", f));
            boxPrimitive->pipeline(*_debugGizmoPipeline);
            boxPrimitive->skipPostFX(true);
        }

        const auto&[min, max, colour3] = _debugBoxes._debugData[f];
        const FColour4 colour = FColour4(colour3, 1.0f);
        boxPrimitive->fromBox(min, max, Util::ToByteColour(colour));
        bufferInOut.add(boxPrimitive->toCommandBuffer());
    }

    _debugBoxes._Id.store(0u);
}

void GFXDevice::debugDrawSphere(const vec3<F32>& center, F32 radius, const FColour3& colour) {
    _debugSpheres.add({ center, radius, colour });
}

void GFXDevice::debugDrawSpheres(GFX::CommandBuffer& bufferInOut) {
    const U32 spheresCount = _debugSpheres._Id.load();
    for (U32 f = 0; f < spheresCount; ++f) {
        IMPrimitive*& spherePrimitive = _debugSpheres._debugPrimitives[f];
        if (spherePrimitive == nullptr) {
            spherePrimitive = newIMP();
            spherePrimitive->name(Util::StringFormat("DebugSphere_%d", f));
            spherePrimitive->pipeline(*_debugGizmoPipeline);
            spherePrimitive->skipPostFX(true);
        }

        const auto&[center, radius, colour3] = _debugSpheres._debugData[f];
        const FColour4 colour = FColour4(colour3, 1.0f);
        spherePrimitive->fromSphere(center, radius, Util::ToByteColour(colour));
        bufferInOut.add(spherePrimitive->toCommandBuffer());
    }

    _debugSpheres._Id.store(0u);
}

void GFXDevice::debugDrawCone(const vec3<F32>& root, const vec3<F32>& direction, F32 length, F32 radius, const FColour3& colour) {
    _debugCones.add({root, direction, length, radius, colour});
}

void GFXDevice::debugDrawCones(GFX::CommandBuffer& bufferInOut) {
    const U32 conesCount = _debugCones._Id.load();
    for (U32 f = 0; f < conesCount; ++f) {
        IMPrimitive*& conePrimitive = _debugCones._debugPrimitives[f];
        if (conePrimitive == nullptr) {
            conePrimitive = newIMP();
            conePrimitive->name(Util::StringFormat("DebugCone_%d", f));
            conePrimitive->pipeline(*_debugGizmoPipeline);
            conePrimitive->skipPostFX(true);
        }

        const auto&[root, dir, length, radius, colour3] = _debugCones._debugData[f];
        const FColour4 colour = FColour4(colour3, 1.0f);
        conePrimitive->fromCone(root, dir, length, radius, Util::ToByteColour(colour));
        bufferInOut.add(conePrimitive->toCommandBuffer());
    }

    _debugCones._Id.store(0u);
}

void GFXDevice::debugDrawFrustum(const Frustum& frustum, const FColour3& colour) {
    _debugFrustums.add({ frustum, colour });
}

void GFXDevice::debugDrawFrustums(GFX::CommandBuffer& bufferInOut) {
    const U32 frustumCount = _debugFrustums._Id.load();

    Line temp = {};
    std::array<Line, to_base(FrustumPlane::COUNT) * 2> lines = {};
    std::array<vec3<F32>, to_base(FrustumPoints::COUNT)> corners = {};
    for (U32 f = 0; f < frustumCount; ++f) {
        IMPrimitive*& frustumPrimitive = _debugFrustums._debugPrimitives[f];
        if (frustumPrimitive == nullptr) {
            frustumPrimitive = newIMP();
            frustumPrimitive->name(Util::StringFormat("DebugFrustum_%d", f));
            frustumPrimitive->pipeline(*_debugGizmoPipeline);
            frustumPrimitive->skipPostFX(true);
        }

        _debugFrustums._debugData[f].first.getCornersWorldSpace(corners);
        const FColour3& endColour = _debugFrustums._debugData[f].second;
        const FColour3 startColour = endColour * 0.25f;

        UColour4 startColourU = Util::ToUIntColour(startColour);
        UColour4 endColourU = Util::ToUIntColour(endColour);

        U8 lineCount = 0;
        // Draw Near Plane
        temp.positionStart(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
        temp.colourStart(startColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
        temp.colourStart(startColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
        temp.colourStart(startColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
        temp.colourStart(startColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        // Draw Far Plane
        temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
        temp.colourStart(endColourU);
        temp.colourEnd(endColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
        temp.colourStart(endColourU);
        temp.colourEnd(endColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
        temp.colourStart(endColourU);
        temp.colourEnd(endColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
        temp.colourStart(endColourU);
        temp.colourEnd(endColourU);
        lines[lineCount++] = temp;
        
        // Connect Planes
        temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
        temp.colourStart(endColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
        temp.colourStart(endColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
        temp.colourStart(endColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
        temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
        temp.colourStart(endColourU);
        temp.colourEnd(startColourU);
        lines[lineCount++] = temp;

        frustumPrimitive->fromLines(lines.data(), lineCount);
        bufferInOut.add(frustumPrimitive->toCommandBuffer());
    }

    _debugFrustums._Id.store(0u);
}

/// Render all of our immediate mode primitives. This isn't very optimised and most are recreated per frame!
void GFXDevice::debugDraw(const SceneRenderState& sceneRenderState, const Camera* activeCamera, GFX::CommandBuffer& bufferInOut) {
    debugDrawFrustums(bufferInOut);
    debugDrawLines(bufferInOut);
    debugDrawBoxes(bufferInOut);
    debugDrawSpheres(bufferInOut);
    debugDrawCones(bufferInOut);

    if_constexpr(Config::Build::ENABLE_EDITOR)
    {
        // Debug axis form the axis arrow gizmo in the corner of the screen
        // This is toggleable, so check if it's actually requested
        if (sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::SCENE_GIZMO)) {
            if (!_axisGizmo) {
                _axisGizmo = newIMP();
                _axisGizmo->name("GFXDeviceAxisGizmo");
                _axisGizmo->pipeline(*_axisGizmoPipeline);
                _axisGizmo->skipPostFX(true);
            }

            // Apply the inverse view matrix so that it cancels out in the shader
            // Submit the draw command, rendering it in a tiny viewport in the lower
            // right corner
            const U16 windowWidth = renderTargetPool().screenTarget().getWidth();
            _axisGizmo->viewport(Rect<I32>(windowWidth - 120, 8, 128, 128));
            _axisGizmo->fromLines(_axisLines.data(), _axisLines.size());
        
            // We need to transform the gizmo so that it always remains axis aligned
            // Create a world matrix using a look at function with the eye position
            // backed up from the camera's view direction
            _axisGizmo->worldMatrix(mat4<F32>(-activeCamera->getForwardDir() * 2,
                                               VECTOR3_ZERO,
                                               activeCamera->getUpDir()) * activeCamera->worldMatrix());
            bufferInOut.add(_axisGizmo->toCommandBuffer());
        } else if (_axisGizmo) {
            destroyIMP(_axisGizmo);
        }
    }
}
#pragma endregion

#pragma region GPU Object instantiation
Mutex& GFXDevice::objectArenaMutex() noexcept {
    return _gpuObjectArenaMutex;
}

GFXDevice::ObjectArena& GFXDevice::objectArena() noexcept {
    return _gpuObjectArena;
}

RenderTarget* GFXDevice::newRT(const RenderTargetDescriptor& descriptor) {
    RenderTarget* temp = nullptr;
    {
        ScopedLock<Mutex> w_lock(objectArenaMutex());

        switch (renderAPI()) {
            case RenderAPI::OpenGL:
            case RenderAPI::OpenGLES: {
                temp =  new (objectArena()) glFramebuffer(*this, descriptor);
            } break;
            case RenderAPI::Vulkan: {
                temp = new (objectArena()) vkRenderTarget(*this, descriptor);
            } break;
            case RenderAPI::None: {
                temp = new (objectArena()) noRenderTarget(*this, descriptor);
            } break;
            default: {
                DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
            } break;
        };

        if (temp != nullptr) {
            objectArena().DTOR(temp);
        }
    }

    bool valid = false;
    if (temp != nullptr) {
        valid = temp->create();
        assert(valid);
    }

    return valid ? temp : nullptr;
}

IMPrimitive* GFXDevice::newIMP() {
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            return GL_API::NewIMP(_imprimitiveMutex , *this);
        };
        case RenderAPI::Vulkan:
        case RenderAPI::None: {
            return nullptr;
        };
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    return nullptr;
}

bool GFXDevice::destroyIMP(IMPrimitive*& primitive) {
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            return GL_API::DestroyIMP(_imprimitiveMutex , primitive);
        };
        case RenderAPI::Vulkan:
        case RenderAPI::None: {
            return false;
        };
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    return false;
}

VertexBuffer* GFXDevice::newVB() {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    VertexBuffer* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glVertexArray(*this);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkVertexBuffer(*this);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noVertexBuffer(*this);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}

PixelBuffer* GFXDevice::newPB(const PBType type, const char* name) {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    PixelBuffer* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glPixelBuffer(*this, type, name);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkPixelBuffer(*this, type, name);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noPixelBuffer(*this, type, name);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}

GenericVertexData* GFXDevice::newGVD(const U32 ringBufferLength, const char* name) {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    GenericVertexData* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glGenericVertexData(*this, ringBufferLength, name);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkGenericVertexData(*this, ringBufferLength, name);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noGenericVertexData(*this, ringBufferLength, name);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}

Texture* GFXDevice::newTexture(const size_t descriptorHash,
                               const Str256& resourceName,
                               const ResourcePath& assetNames,
                               const ResourcePath& assetLocations,
                               const bool isFlipped,
                               const bool asyncLoad,
                               const TextureDescriptor& texDescriptor) {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    // Texture is a resource! Do not use object arena!
    Texture* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glTexture(*this, descriptorHash, resourceName, assetNames, assetLocations, isFlipped, asyncLoad, texDescriptor);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkTexture(*this, descriptorHash, resourceName, assetNames, assetLocations, isFlipped, asyncLoad, texDescriptor);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noTexture(*this, descriptorHash, resourceName, assetNames, assetLocations, isFlipped, asyncLoad, texDescriptor);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}

Pipeline* GFXDevice::newPipeline(const PipelineDescriptor& descriptor) {
    // Pipeline with no shader is no pipeline at all
    DIVIDE_ASSERT(descriptor._shaderProgramHandle != 0, "Missing shader handle during pipeline creation!");

    const size_t hash = descriptor.getHash();

    ScopedLock<Mutex> lock(_pipelineCacheLock);
    const hashMap<size_t, Pipeline, NoHash<size_t>>::iterator it = _pipelineCache.find(hash);
    if (it == std::cend(_pipelineCache)) {
        return &insert(_pipelineCache, hash, Pipeline(descriptor)).first->second;
    }

    return &it->second;
}

ShaderProgram* GFXDevice::newShaderProgram(const size_t descriptorHash,
                                           const Str256& resourceName,
                                           const Str256& assetName,
                                           const ResourcePath& assetLocation,
                                           const ShaderProgramDescriptor& descriptor,
                                           const bool asyncLoad) {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    ShaderProgram* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glShaderProgram(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, asyncLoad);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkShaderProgram(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, asyncLoad);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noShaderProgram(*this, descriptorHash, resourceName, assetName, assetLocation, descriptor, asyncLoad);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}

ShaderBuffer* GFXDevice::newSB(const ShaderBufferDescriptor& descriptor) {

    ScopedLock<Mutex> w_lock(objectArenaMutex());

    ShaderBuffer* temp = nullptr;
    switch (renderAPI()) {
        case RenderAPI::OpenGL:
        case RenderAPI::OpenGLES: {
            temp = new (objectArena()) glUniformBuffer(*this, descriptor);
        } break;
        case RenderAPI::Vulkan: {
            temp = new (objectArena()) vkUniformBuffer(*this, descriptor);
        } break;
        case RenderAPI::None: {
            temp = new (objectArena()) noUniformBuffer(*this, descriptor);
        } break;
        default: {
            DIVIDE_UNEXPECTED_CALL_MSG(Locale::Get(_ID("ERROR_GFX_DEVICE_API")));
        } break;
    };

    if (temp != nullptr) {
        objectArena().DTOR(temp);
    }

    return temp;
}
#pragma endregion

ShaderComputeQueue& GFXDevice::shaderComputeQueue() {
    assert(_shaderComputeQueue != nullptr);
    return *_shaderComputeQueue;
}

const ShaderComputeQueue& GFXDevice::shaderComputeQueue() const {
    assert(_shaderComputeQueue != nullptr);
    return *_shaderComputeQueue;
}

/// Extract the pixel data from the main render target's first colour attachment and save it as a TGA image
void GFXDevice::screenshot(const string& filename) const {
    // Get the screen's resolution
    const RenderTarget& screenRT = _rtPool->screenTarget();
    const U16 width = screenRT.getWidth();
    const U16 height = screenRT.getHeight();
    // Allocate sufficiently large buffers to hold the pixel data
    const U32 bufferSize = width * height * 4;
    vector<U8> imageData(bufferSize, 0u);
    // Read the pixels from the main render target (RGBA16F)
    screenRT.readData(GFXImageFormat::RGB, GFXDataFormat::UNSIGNED_BYTE, (bufferPtr)imageData.data());
    // Save to file
    ImageTools::SaveSeries(filename,
                           vec2<U16>(width, height),
                           32,
                           imageData.data());
}

/// returns the standard state block
size_t GFXDevice::getDefaultStateBlock(const bool noDepth) const noexcept {
    return noDepth ? _defaultStateNoDepthHash : RenderStateBlock::defaultHash();
}

};
