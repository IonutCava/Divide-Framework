/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_HARDWARE_VIDEO_GFX_DEVICE_H_
#define DVD_HARDWARE_VIDEO_GFX_DEVICE_H_

#include "config.h"

#include "ClipPlanes.h"
#include "GFXShaderData.h"
#include "Pipeline.h"
#include "CommandBuffer.h"
#include "PushConstants.h"
#include "RenderAPIWrapper.h"
#include "RenderStagePass.h"
#include "IMPrimitiveDescriptors.h"

#include "Core/Math/Headers/Line.h"
#include "Core/Headers/FrameListener.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Geometry/Material/Headers/MaterialEnums.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

#include "Rendering/Camera/Headers/Frustum.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/RenderPass.h"

namespace Divide
{

struct RenderPassParams;
struct ShaderProgramDescriptor;

enum class SceneNodeType : U16;
enum class WindowEvent : U8;

class GUI;
class GUIText;
class SceneGUIElements;

class Light;
class Camera;
class Quad3D;
class Texture;
class Object3D;
class GFXRTPool;
class ShadowMap;
class IMPrimitive;
class SceneManager;
class ResourceCache;
class SceneGraphNode;
class SceneShaderData;
class SceneRenderState;
class KernelApplication;
class ShaderComputeQueue;
class CascadedShadowMapsGenerator;

struct SizeChangeParams;
struct ShaderBufferDescriptor;

enum class ShadowType : U8;

FWD_DECLARE_MANAGED_CLASS( VertexBuffer )

namespace Time {
    class ProfileTimer;
};

namespace Attorney {
    class GFXDeviceAPI;
    class GFXDeviceKernel;
    class GFXDeviceGraphicsResource;
    class GFXDeviceGFXRTPool;
    class GFXDeviceSceneManager;
    class GFXDeviceShaderProgram;
    class GFXDeviceShadowMap;
    class KernelApplication;
};

namespace TypeUtil {
    [[nodiscard]] const char* GraphicResourceTypeToName(GraphicsResource::Type type) noexcept;

    [[nodiscard]] const char* RenderStageToString(RenderStage stage) noexcept;
    [[nodiscard]] RenderStage StringToRenderStage(const char* stage) noexcept;

    [[nodiscard]] const char* RenderPassTypeToString(RenderPassType pass) noexcept;
    [[nodiscard]] RenderPassType StringToRenderPassType(const char* pass) noexcept;
};

struct DebugView final : GUIDWrapper {
    DebugView() noexcept
        : DebugView(-1)
    {
    }

    explicit DebugView(const I16 sortIndex) noexcept 
        : GUIDWrapper()
        , _sortIndex(to_I16(sortIndex))
    {
    }

    PushConstants _shaderData;
    string _name;
    ShaderProgram_ptr _shader = nullptr;
    Texture_ptr _texture = nullptr;
    SamplerDescriptor _sampler{};
    I16 _groupID = -1;
    I16 _sortIndex = -1;
    U8 _textureBindSlot = 0u;
    bool _enabled = false;
    bool _cycleMips = false;
};

FWD_DECLARE_MANAGED_STRUCT(DebugView);

template<typename Descriptor, size_t N>
struct DebugPrimitiveHandler
{
    static constexpr U8 g_maxFrameLifetime = 6u;

    [[nodiscard]] size_t size() const noexcept { return _debugPrimitives.size(); }

    struct DataEntry {
        Descriptor _descriptor;
        I64 _id = 0u;
        U8 _frameLifeTime = 0u;
    };

    DebugPrimitiveHandler()   noexcept
    {
        for (auto& primitive : _debugPrimitives) {
            primitive = nullptr;
        }
    }

    ~DebugPrimitiveHandler()
    {
        reset();
    }

    void reset();

    void add(const I64 ID, const Descriptor& data) noexcept {
        LockGuard<Mutex> w_lock(_dataLock);
        addLocked(ID, data);
    }

    void addLocked(const I64 ID, const Descriptor& data) noexcept {
        const size_t count = _debugData.size();

        for (U32 i = 0u; i < count; ++i) {
            DataEntry& entry = _debugData[i];
            if (entry._id == ID) {
                entry._descriptor = data;
                entry._frameLifeTime = g_maxFrameLifetime;
                return;
            }
        }
        for (U32 i = 0u; i < count; ++i) {
            DataEntry& entry = _debugData[i];
            if (entry._frameLifeTime == 0u) {
                entry._id = ID;
                entry._descriptor = data;
                entry._frameLifeTime = g_maxFrameLifetime;
                return;
            }
        }

        //We need a new entry. Create one and try again
        _debugPrimitives.emplace_back(nullptr);
        _debugData.emplace_back();
        addLocked(ID, data);
    }

    Mutex _dataLock;
    eastl::fixed_vector<IMPrimitive*, N, true>  _debugPrimitives;
    eastl::fixed_vector<DataEntry, N, true> _debugData;
};

struct ImShaders
{
    explicit ImShaders( GFXDevice& context );

    PROPERTY_R_IW( ShaderProgram_ptr, imShader, nullptr );
    PROPERTY_R_IW( ShaderProgram_ptr, imShaderNoTexture, nullptr );
    PROPERTY_R_IW( ShaderProgram_ptr, imWorldShader, nullptr );
    PROPERTY_R_IW( ShaderProgram_ptr, imWorldShaderNoTexture, nullptr );
    PROPERTY_R_IW( ShaderProgram_ptr, imWorldOITShader, nullptr );
    PROPERTY_R_IW( ShaderProgram_ptr, imWorldOITShaderNoTexture, nullptr );
};

FWD_DECLARE_MANAGED_STRUCT( ImShaders );

struct RenderTargetNames {
    static RenderTargetID BACK_BUFFER;
    static RenderTargetID SCREEN;
    static RenderTargetID SCREEN_PREV;
    static RenderTargetID NORMALS_RESOLVED;
    static RenderTargetID OIT;
    static RenderTargetID OIT_REFLECT;
    static RenderTargetID SSAO_RESULT;
    static RenderTargetID SSR_RESULT;
    static RenderTargetID HI_Z;
    static RenderTargetID HI_Z_REFLECT;
    static RenderTargetID REFLECTION_PLANAR_BLUR;
    static RenderTargetID REFLECTION_CUBE;

    static std::array<RenderTargetID, Config::MAX_REFLECTIVE_NODES_IN_VIEW> REFLECTION_PLANAR;
    static std::array<RenderTargetID, Config::MAX_REFRACTIVE_NODES_IN_VIEW> REFRACTION_PLANAR;
};

/// Rough around the edges Adapter pattern abstracting the actual rendering API and access to the GPU
class GFXDevice final : public PlatformContextComponent, public FrameListener {
    friend class Attorney::GFXDeviceAPI;
    friend class Attorney::GFXDeviceKernel;
    friend class Attorney::GFXDeviceGraphicsResource;
    friend class Attorney::GFXDeviceGFXRTPool;
    friend class Attorney::GFXDeviceShaderProgram;
    friend class Attorney::GFXDeviceShadowMap;
    friend class Attorney::GFXDeviceSceneManager;

public:
    struct ScreenTargets
    {
        constexpr static RTColourAttachmentSlot ALBEDO = RTColourAttachmentSlot::SLOT_0;
        constexpr static RTColourAttachmentSlot VELOCITY = RTColourAttachmentSlot::SLOT_1;
        constexpr static RTColourAttachmentSlot NORMALS = RTColourAttachmentSlot::SLOT_2;
        constexpr static RTColourAttachmentSlot MODULATE = RTColourAttachmentSlot::SLOT_3;
        constexpr static RTColourAttachmentSlot ACCUMULATION = ALBEDO;
        constexpr static RTColourAttachmentSlot REVEALAGE = VELOCITY;
    };

    struct GFXDescriptorSet {
        PROPERTY_RW(DescriptorSet, impl);
        PROPERTY_RW(bool, dirty, true);
        void clear();
        void update(DescriptorSetUsage usage, const DescriptorSet& newBindingData);
        void update(DescriptorSetUsage usage, const DescriptorSetBinding& newBindingData);
    };

    using GFXDescriptorSets = std::array<GFXDescriptorSet, to_base(DescriptorSetUsage::COUNT)>;

public:  // GPU interface
    explicit GFXDevice( PlatformContext& context );
    ~GFXDevice() override;

    ErrorCode initRenderingAPI(I32 argc, char** argv, RenderAPI API);
    ErrorCode postInitRenderingAPI(vec2<U16> renderResolution);
    void closeRenderingAPI();

    void idle(bool fast, U64 deltaTimeUSGame, U64 deltaTimeUSApp);
    void drawToWindow(DisplayWindow& window);
    void flushWindow(DisplayWindow& window);

    void flushCommandBuffer(GFX::CommandBuffer& commandBuffer, bool batch = true);

    void debugDraw( const SceneRenderState& sceneRenderState, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
    void debugDrawLines(const I64 ID, IM::LineDescriptor descriptor) noexcept;
    void debugDrawBox(const I64 ID, IM::BoxDescriptor descriptor) noexcept;
    void debugDrawOBB(const I64 ID, IM::OBBDescriptor descriptor) noexcept;
    void debugDrawSphere(const I64 ID, IM::SphereDescriptor descriptor) noexcept;
    void debugDrawCone(const I64 ID, IM::ConeDescriptor descriptor) noexcept;
    void debugDrawFrustum(const I64 ID, IM::FrustumDescriptor descriptor) noexcept;
    void validateAndUploadDescriptorSets();
    /// Generate a cubemap from the given position
    /// It renders the entire scene graph (with culling) as default
    /// use the callback param to override the draw function
    void generateCubeMap(RenderPassParams& params,
                         U16 arrayOffset,
                         const vec3<F32>& pos,
                         vec2<F32> zPlanes,
                         GFX::CommandBuffer& commandsInOut,
                         GFX::MemoryBarrierCommand& memCmdInOut,
                         mat4<F32>* viewProjectionOut = nullptr);

    void generateDualParaboloidMap(RenderPassParams& params,
                                   U16 arrayOffset,
                                   const vec3<F32>& pos,
                                   vec2<F32> zPlanes,
                                   GFX::CommandBuffer& bufferInOut,
                                   GFX::MemoryBarrierCommand& memCmdInOut,
                                   mat4<F32>* viewProjectionOut = nullptr);

    const GFXShaderData::CamData& cameraData() const noexcept;
    const GFXShaderData::PrevFrameData& previousFrameData() const noexcept;

    /// Returns true if the viewport was changed
    bool setViewport(const Rect<I32>& viewport);
    bool setViewport( I32 x, I32 y, I32 width, I32 height );
    bool setScissor(const Rect<I32>& scissor);
    bool setScissor( I32 x, I32 y, I32 width, I32 height );
    void setDepthRange(vec2<F32> depthRange);
    void setPreviousViewProjectionMatrix(const mat4<F32>& prevViewMatrix, const mat4<F32> prevProjectionMatrix);

    void setCameraSnapshot(PlayerIndex index, const CameraSnapshot& snapshot) noexcept;
    CameraSnapshot& getCameraSnapshot(PlayerIndex index) noexcept;
    const CameraSnapshot& getCameraSnapshot(PlayerIndex index) const noexcept;

    F32 renderingAspectRatio() const noexcept;
    vec2<U16> renderingResolution() const noexcept;

    /// Switch between fullscreen rendering
    void toggleFullScreen() const;
    void increaseResolution();
    void decreaseResolution();

    void setScreenMSAASampleCount(U8 sampleCount);
    void setShadowMSAASampleCount(ShadowType type, U8 sampleCount);

    /// Save a screenshot in TGA format
    void screenshot(const ResourcePath& filename, GFX::CommandBuffer& bufferInOut ) const;

    ShaderComputeQueue& shaderComputeQueue() noexcept;
    const ShaderComputeQueue& shaderComputeQueue() const noexcept;

public:  // Accessors and Mutators

    Renderer& getRenderer() const;
    /// returns the standard state block
    const RenderStateBlock& getNoDepthTestBlock() const noexcept;
    const RenderStateBlock& get2DStateBlock() const noexcept;
    GFXRTPool& renderTargetPool() noexcept;
    const GFXRTPool& renderTargetPool() const noexcept;
    const ShaderProgram_ptr& getRTPreviewShader(bool depthOnly) const noexcept;
    void registerDrawCall() noexcept;
    void registerDrawCalls(U32 count) noexcept;

    DebugView* addDebugView(const std::shared_ptr<DebugView>& view);
    bool removeDebugView(DebugView* view);
    void toggleDebugView(I16 index, bool state);
    void toggleDebugGroup(I16 groupID, bool state);
    bool getDebugGroupState(I16 groupID) const;
    void getDebugViewNames(vector<std::tuple<string, I16, I16, bool>>& namesOut);

    [[nodiscard]] PerformanceMetrics& getPerformanceMetrics() noexcept;
    [[nodiscard]] const PerformanceMetrics& getPerformanceMetrics() const noexcept;

    void onThreadCreated(const std::thread::id& threadID, bool isMainRenderThread) const;

    static void FrameInterpolationFactor(const D64 interpolation) noexcept { s_interpolationFactor = interpolation; }
    [[nodiscard]] static D64 FrameInterpolationFactor() noexcept { return s_interpolationFactor; }
    [[nodiscard]] static U64 FrameCount() noexcept { return s_frameCount; }

    static const DeviceInformation& GetDeviceInformation() noexcept;
    static void OverrideDeviceInformation(const DeviceInformation& info) noexcept;

    static bool IsSubmitCommand(GFX::CommandType type) noexcept;

public:
    /// Create and return a new framebuffer.
    RenderTarget_uptr newRT( const RenderTargetDescriptor& descriptor );

    /// Create and return a new immediate mode emulation primitive.
    IMPrimitive*       newIMP(const Str<64>& name);
    bool               destroyIMP(IMPrimitive*& primitive);

    /// Create and return a new vertex array (VAO + VB + IB).
    VertexBuffer_ptr  newVB(bool renderIndirect, const Str<256>& name);
    /// Create and return a new generic vertex data object
    GenericVertexData_ptr newGVD(U32 ringBufferLength, bool renderIndirect, const Str<256>& name);
    /// Create and return a new texture.
    Texture_ptr        newTexture(size_t descriptorHash,
                                  const Str<256>& resourceName,
                                  const ResourcePath& assetNames,
                                  const ResourcePath& assetLocations,
                                  const TextureDescriptor& texDescriptor,
                                  ResourceCache& parentCache);
    /// Create and return a new shader program.
    ShaderProgram_ptr  newShaderProgram(size_t descriptorHash,
                                        const Str<256>& resourceName,
                                        const Str<256>& assetName,
                                        const ResourcePath& assetLocation,
                                        const ShaderProgramDescriptor& descriptor,
                                        ResourceCache& parentCache);
    /// Create and return a new shader buffer. 
    /// The OpenGL implementation creates either an 'Uniform Buffer Object' if unbound is false
    /// or a 'Shader Storage Block Object' otherwise
    ShaderBuffer_uptr  newSB(const ShaderBufferDescriptor& descriptor);
    /// Create and return a new graphics pipeline. This is only used for caching and doesn't use the object arena
    Pipeline*          newPipeline(const PipelineDescriptor& descriptor);

    // Render the texture using a custom viewport
    void drawTextureInViewport(const ImageView& texture, SamplerDescriptor sampler, const Rect<I32>& viewport, bool convertToSrgb, bool drawToDepthOnly, bool drawBlend, GFX::CommandBuffer& bufferInOut);

    void blurTarget(RenderTargetHandle& blurSource, 
                    RenderTargetHandle& blurBuffer,
                    const RenderTargetHandle& blurTarget, ///< can be the same as source
                    RTAttachmentType att,
                    RTColourAttachmentSlot slot,
                    I32 kernelSize,
                    bool gaussian,
                    U8 layerCount,
                    GFX::CommandBuffer& bufferInOut);

    void updateSceneDescriptorSet(GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const;

    [[nodiscard]] GFXDescriptorSet& descriptorSet(DescriptorSetUsage usage) noexcept;
    [[nodiscard]] const GFXDescriptorSet& descriptorSet(DescriptorSetUsage usage) const noexcept;

    PROPERTY_RW(MaterialDebugFlag, materialDebugFlag, MaterialDebugFlag::COUNT);
    PROPERTY_RW(RenderAPI, renderAPI, RenderAPI::COUNT);
    PROPERTY_RW(bool, queryPerformanceStats, false);
    PROPERTY_RW(bool, enableOcclusionCulling, true);
    PROPERTY_R_IW(U32, frameDrawCalls, 0u);
    PROPERTY_R_IW(U32, frameDrawCallsPrev, 0u);
    PROPERTY_R_IW(vec4<U32>, lastCullCount, VECTOR4_ZERO);     ///< X = culled items HiZ, Y = culled items overflow, Z = baseInstance == 0 count, W = skipped cull count
    PROPERTY_R_IW(Rect<I32>, activeViewport, UNIT_VIEWPORT);
    PROPERTY_R_IW(Rect<I32>, activeScissor, UNIT_VIEWPORT);
    POINTER_R(SceneShaderData, sceneData, nullptr);
    PROPERTY_R(ImShaders_uptr, imShaders);

   [[nodiscard]] bool framePreRender( const FrameEvent& evt ) override;
   [[nodiscard]] bool frameStarted( const FrameEvent& evt ) override;
   [[nodiscard]] bool frameEnded( const FrameEvent& evt ) noexcept override;

protected:

    void update(U64 deltaTimeUSFixed, U64 deltaTimeUSApp);

    ErrorCode initDescriptorSets();

    void setScreenMSAASampleCountInternal(U8 sampleCount);
    void setShadowMSAASampleCountInternal(ShadowType type, U8 sampleCount);

    // returns true if the window and the viewport have different aspect ratios
    bool fitViewportInWindow(U16 w, U16 h);

    void onWindowSizeChange(const SizeChangeParams& params);
    void onResolutionChange(const SizeChangeParams& params);

    void initDebugViews();
    void renderDebugViews(Rect<I32> targetViewport, I32 padding, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

    void stepResolution(bool increment);

    void flushCommandBufferInternal(GFX::CommandBuffer& commandBuffer);

    [[nodiscard]] PipelineDescriptor& getDebugPipeline(const IM::BaseDescriptor& descriptor) noexcept;
    void debugDrawLines( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    void debugDrawBoxes( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
    void debugDrawOBBs( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
    void debugDrawCones( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
    void debugDrawSpheres( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
    void debugDrawFrustums( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

protected:
    friend class RenderPassManager;
    void renderDebugUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

protected:
    friend class RenderPass;
    friend class RenderPassExecutor;

    void occlusionCull(const RenderPass::BufferData& bufferData,
                       const Texture_ptr& hizBuffer,
                       SamplerDescriptor sampler,
                       const CameraSnapshot& cameraSnapshot,
                       bool countCulledNodes,
                       GFX::CommandBuffer& bufferInOut);

    // Returns the HiZ texture that can be sent directly to occlusionCull
    std::pair<const Texture_ptr&, SamplerDescriptor> constructHIZ(RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut);

    RenderAPIWrapper& getAPIImpl() { return *_api; }
    const RenderAPIWrapper& getAPIImpl() const { return *_api; }

private:
    /// Upload draw related data to the GPU (view & projection matrices, viewport settings, etc)
    bool uploadGPUBlock();
    void resizeGPUBlocks(size_t targetSizeCam, size_t targetSizeCullCounter);
    void setClipPlanes(const FrustumClipPlanes& clipPlanes);
    void renderFromCamera(const CameraSnapshot& cameraSnapshot);
    void shadowingSettings(const F32 lightBleedBias, const F32 minShadowVariance) noexcept;
    void worldAOViewProjectionMatrix(const mat4<F32>& vpMatrix) noexcept;

    ErrorCode createAPIInstance(RenderAPI api);

private:
    RenderAPIWrapper_uptr _api = nullptr;
    Renderer_uptr _renderer = nullptr;

    ShaderComputeQueue* _shaderComputeQueue = nullptr;

    DebugPrimitiveHandler<IM::LineDescriptor, 16u> _debugLines;
    DebugPrimitiveHandler<IM::BoxDescriptor, 16u> _debugBoxes;
    DebugPrimitiveHandler<IM::OBBDescriptor, 16u> _debugOBBs;
    DebugPrimitiveHandler<IM::SphereDescriptor, 16u> _debugSpheres;
    DebugPrimitiveHandler<IM::ConeDescriptor, 16u> _debugCones;
    DebugPrimitiveHandler<IM::FrustumDescriptor, 8u> _debugFrustums;

    CameraSnapshot  _activeCameraSnapshot;

    GFXRTPool* _rtPool = nullptr;

    std::pair<vec2<U16>, bool> _resolutionChangeQueued;

    static constexpr U8 s_invalidQueueSampleCount = 255u;
    U8 _queuedScreenSampleChange = s_invalidQueueSampleCount;
    std::array<U8, to_base(ShadowType::COUNT)> _queuedShadowSampleChange;
    /// The default render state but with depth testing disabled
    RenderStateBlock _defaultStateNoDepthTest{};
    /// Special render state for 2D rendering
    RenderStateBlock _state2DRendering{};
    RenderStateBlock _stateDepthOnlyRendering{};
    /// The interpolation factor between the current and the last frame
    FrustumClipPlanes _clippingPlanes;

    /// shader used to preview the depth buffer
    ShaderProgram_ptr _previewDepthMapShader = nullptr;
    ShaderProgram_ptr _previewRenderTargetColour = nullptr;
    ShaderProgram_ptr _previewRenderTargetDepth = nullptr;
    ShaderProgram_ptr _renderTargetDraw = nullptr;
    ShaderProgram_ptr _hIZConstructProgram = nullptr;
    ShaderProgram_ptr _hIZCullProgram = nullptr;
    ShaderProgram_ptr _displayShader = nullptr;
    ShaderProgram_ptr _depthShader = nullptr;
    ShaderProgram_ptr _blurBoxShaderSingle = nullptr;
    ShaderProgram_ptr _blurBoxShaderLayered = nullptr;
    ShaderProgram_ptr _blurGaussianShaderSingle = nullptr;
    ShaderProgram_ptr _blurGaussianShaderLayered = nullptr;


    Pipeline* _hIZPipeline = nullptr;
    Pipeline* _hIZCullPipeline = nullptr;
    PipelineDescriptor _debugGizmoPipeline;
    PipelineDescriptor _debugGizmoPipelineNoDepth;
    PipelineDescriptor _debugGizmoPipelineNoCull;
    PipelineDescriptor _debugGizmoPipelineNoCullNoDepth;
    GFX::BindPipelineCommand _drawFSTexturePipelineCmd;
    GFX::BindPipelineCommand _drawFSTexturePipelineBlendCmd;
    GFX::BindPipelineCommand _drawFSDepthPipelineCmd;
    GFX::BindPipelineCommand _blurBoxPipelineSingleCmd;
    GFX::BindPipelineCommand _blurBoxPipelineLayeredCmd;
    GFX::BindPipelineCommand _blurGaussianPipelineSingleCmd;
    GFX::BindPipelineCommand _blurGaussianPipelineLayeredCmd;

    GFXDescriptorSets _descriptorSets;
    Mutex _graphicsResourceMutex;
    vector<std::tuple<GraphicsResource::Type, I64, U64>> _graphicResources;

    vec2<U16> _renderingResolution;
    GFXShaderData _gpuBlock;

    mutable Mutex _debugViewLock;
    vector<DebugView_ptr> _debugViews;

    Mutex _queuedCommandbufferLock;
    DisplayWindow* _activeWindow{nullptr};

    struct GFXBuffers {
        static constexpr U8 PER_FRAME_BUFFER_COUNT = 3u;

        struct PerFrameBuffers {
            BufferRange _camBufferWriteRange;
            ShaderBuffer_uptr _camDataBuffer = nullptr;
            ShaderBuffer_uptr _cullCounter = nullptr;
            size_t _camWritesThisFrame = 0u;
            size_t _renderWritesThisFrame = 0u;
        } _perFrameBuffers[PER_FRAME_BUFFER_COUNT];
        size_t _perFrameBufferIndex = 0u;
        bool _needsResizeCam = false;
        [[nodiscard]] inline PerFrameBuffers& crtBuffers() noexcept { return _perFrameBuffers[_perFrameBufferIndex]; }
        [[nodiscard]] inline const PerFrameBuffers& crtBuffers() const noexcept { return _perFrameBuffers[_perFrameBufferIndex]; }

        inline void reset(const bool camBuffer, const bool cullBuffer) noexcept {
            for (U8 i = 0u; i < PER_FRAME_BUFFER_COUNT; ++i) {
                if (camBuffer) {
                    _perFrameBuffers[i]._camDataBuffer.reset();
                }
                if (cullBuffer) {
                    _perFrameBuffers[i]._cullCounter.reset();
                }
            }
            crtBuffers()._camWritesThisFrame = 0u;
            crtBuffers()._renderWritesThisFrame = 0u;
        }

        inline void onEndFrame() noexcept
        {
            _perFrameBufferIndex = (_perFrameBufferIndex + 1u) % PER_FRAME_BUFFER_COUNT;
            crtBuffers()._camWritesThisFrame = 0u;
            crtBuffers()._renderWritesThisFrame = 0u;
        }

    } _gfxBuffers;

    Mutex _pipelineCacheLock;
    hashMap<size_t, Pipeline, NoHash<size_t>> _pipelineCache;

    static constexpr U8 MAX_CAMERA_SNAPSHOTS = 32u;
    std::stack<CameraSnapshot, eastl::fixed_vector<CameraSnapshot, MAX_CAMERA_SNAPSHOTS, false>> _cameraSnapshots;

    std::array<CameraSnapshot, Config::MAX_LOCAL_PLAYER_COUNT> _cameraSnapshotHistory;

    std::stack<Rect<I32>> _viewportStack;
    Mutex _imprimitiveMutex;

    PerformanceMetrics _performanceMetrics{};

    using IMPrimitivePool = MemoryPool<IMPrimitive, 1 << 15>;
    static IMPrimitivePool s_IMPrimitivePool;

    static D64 s_interpolationFactor;
    static U64 s_frameCount;

    static DeviceInformation s_deviceInformation;
};

namespace Attorney {
    class GFXDeviceKernel {
        static void onWindowSizeChange(GFXDevice& device, const SizeChangeParams& params) {
            device.onWindowSizeChange(params);
        }

        static void onResolutionChange(GFXDevice& device, const SizeChangeParams& params) {
            device.onResolutionChange(params);
        }
        
        static void update(GFXDevice& device, const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp) {
            device.update(deltaTimeUSFixed, deltaTimeUSApp);
        }

        friend class Divide::Kernel;
        friend class Divide::KernelApplication;
    };

    class GFXDeviceGraphicsResource {
       static void onResourceCreate(GFXDevice& device, GraphicsResource::Type type, I64 GUID, U64 nameHash) {
           LockGuard<Mutex> w_lock(device._graphicsResourceMutex);
           device._graphicResources.emplace_back(type, GUID, nameHash);
       }

       static void onResourceDestroy(GFXDevice& device, [[maybe_unused]] GraphicsResource::Type type, I64 GUID, [[maybe_unused]] U64 nameHash) {
           LockGuard<Mutex> w_lock(device._graphicsResourceMutex);
           const bool success = dvd_erase_if(device._graphicResources,
                                             [type, GUID, nameHash](const auto& crtEntry) noexcept -> bool {
                                                if (std::get<1>(crtEntry) == GUID) {
                                                   assert(std::get<0>(crtEntry) == type && std::get<2>(crtEntry) == nameHash);
                                                   return true;
                                                }
                                                return false;
                                             });
           DIVIDE_ASSERT(success);
   
       }
       friend class Divide::GraphicsResource;
    };

    class GFXDeviceGFXRTPool {
        static RenderTarget_uptr newRT(GFXDevice& device, const RenderTargetDescriptor& descriptor)
        {
            return device.newRT(descriptor);
        }

        friend class Divide::GFXRTPool;
    }; 
    
    class GFXDeviceSceneManager {
        static void shadowingSettings(GFXDevice& device, const F32 lightBleedBias, const F32 minShadowVariance) noexcept
        {
            device.shadowingSettings(lightBleedBias, minShadowVariance);
        }

        friend class Divide::SceneManager;
    };

    class GFXDeviceShadowMap
    {
        static void worldAOViewProjectionMatrix( GFXDevice& device, const mat4<F32>& vpMatrix ) noexcept
        {
            device.worldAOViewProjectionMatrix( vpMatrix );
        }

        friend class Divide::ShadowMap;
        friend class Divide::CascadedShadowMapsGenerator;
    };

};  // namespace Attorney
};  // namespace Divide

#include "GFXDevice.inl"

#endif //DVD_HARDWARE_VIDEO_GFX_DEVICE_H_