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
#ifndef _HARDWARE_VIDEO_GFX_DEVICE_H_
#define _HARDWARE_VIDEO_GFX_DEVICE_H_

#include "config.h"

#include "ClipPlanes.h"
#include "GFXRTPool.h"
#include "GFXShaderData.h"
#include "GFXState.h"
#include "IMPrimitive.h"

#include "Core/Math/Headers/Line.h"
#include "Core/Headers/KernelComponent.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Geometry/Material/Headers/MaterialEnums.h"

#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Headers/RenderStagePass.h"

#include "Rendering/Camera/Headers/Frustum.h"
#include "Rendering/PostFX/CustomOperators/Headers/BloomPreRenderOperator.h"
#include "Rendering/RenderPass/Headers/RenderPass.h"

namespace Divide {
class ShaderProgramDescriptor;
struct RenderPassParams;

enum class SceneNodeType : U16;
enum class WindowEvent : U8;

class GUI;
class GUIText;
class SceneGUIElements;

class GL_API;
class DX_API;

class Light;
class Camera;
class Quad3D;
class Texture;
class Object3D;
class Renderer;
class IMPrimitive;
class PixelBuffer;
class VertexBuffer;
class SceneGraphNode;
class SceneRenderState;
class GenericVertexData;
class ShaderComputeQueue;

struct SizeChangeParams;
struct SizeChangeParams;
struct ShaderBufferDescriptor;

enum class ShadowType : U8;

FWD_DECLARE_MANAGED_CLASS(Texture);

namespace Time {
    class ProfileTimer;
};

namespace Attorney {
    class GFXDeviceAPI;
    class GFXDeviceGUI;
    class GFXDeviceKernel;
    class GFXDeviceGraphicsResource;
    class GFXDeviceGFXRTPool;
    class KernelApplication;
};

namespace TypeUtil {
    const char* GraphicResourceTypeToName(GraphicsResource::Type type) noexcept;

    const char* RenderStageToString(RenderStage stage) noexcept;
    RenderStage StringToRenderStage(const char* stage) noexcept;

    const char* RenderPassTypeToString(RenderPassType pass) noexcept;
    RenderPassType StringToRenderPassType(const char* pass) noexcept;
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
    size_t _samplerHash = 0;
    I16 _groupID = -1;
    I16 _sortIndex = -1;
    U8 _textureBindSlot = 0u;
    bool _enabled = false;
};

FWD_DECLARE_MANAGED_STRUCT(DebugView);

template<typename Data, size_t N>
struct DebugPrimitiveHandler
{
    DebugPrimitiveHandler()   noexcept
    {
        std::atomic_init(&_Id, 0u);
        _debugPrimitives.fill(nullptr);
    }
    ~DebugPrimitiveHandler()
    {
        reset();
    }

    void reset();

    void add(Data&& data) noexcept {
        if (_Id.load() == N) {
            return;
        }

        _debugData[_Id.fetch_add(1)] = MOV(data);
    }

    std::array<IMPrimitive*, N>  _debugPrimitives;
    std::array<Data, N> _debugData;
    std::atomic_uint _Id;
};

/// Rough around the edges Adapter pattern abstracting the actual rendering API and access to the GPU
class GFXDevice final : public KernelComponent, public PlatformContextComponent {
    friend class Attorney::GFXDeviceAPI;
    friend class Attorney::GFXDeviceGUI;
    friend class Attorney::GFXDeviceKernel;
    friend class Attorney::GFXDeviceGraphicsResource;
    friend class Attorney::GFXDeviceGFXRTPool;

public:
    enum class ScreenTargets : U8 {
        ALBEDO = 0,
        VELOCITY,
        NORMALS_AND_MATERIAL_PROPERTIES,
        MODULATE,
        COUNT,
        ACCUMULATION = ALBEDO,
        REVEALAGE = VELOCITY,
    };

    using ObjectArena = MyArena<Config::REQUIRED_RAM_SIZE_IN_BYTES / 4>;

public:  // GPU interface
    explicit GFXDevice(Kernel& parent);
    ~GFXDevice();

    static constexpr U32 MaxFrameQueueSize = 2;
    static_assert(MaxFrameQueueSize > 0, "FrameQueueSize is invalid!");

    ErrorCode initRenderingAPI(I32 argc, char** argv, RenderAPI API);
    ErrorCode postInitRenderingAPI(const vec2<U16>& renderResolution);
    void closeRenderingAPI();

    void idle(bool fast) const;
    void beginFrame(DisplayWindow& window, bool global);
    void endFrame(DisplayWindow& window, bool global);

    void debugDraw(const SceneRenderState& sceneRenderState, const Camera* activeCamera, GFX::CommandBuffer& bufferInOut);
    void debugDrawLines(const Line* lines, size_t count) noexcept;
    void debugDrawBox(const vec3<F32>& min, const vec3<F32>& max, const FColour3& colour) noexcept;
    void debugDrawSphere(const vec3<F32>& center, F32 radius, const FColour3& colour) noexcept;
    void debugDrawCone(const vec3<F32>& root, const vec3<F32>& direction, F32 length, F32 radius, const FColour3& colour) noexcept;
    void debugDrawFrustum(const Frustum& frustum, const FColour3& colour) noexcept;
    void flushCommandBuffer(GFX::CommandBuffer& commandBuffer, bool batch = true);
    
    /// Generate a cubemap from the given position
    /// It renders the entire scene graph (with culling) as default
    /// use the callback param to override the draw function
    void generateCubeMap(RenderPassParams& params,
                         I16 arrayOffset,
                         const vec3<F32>& pos,
                         const vec2<F32>& zPlanes,
                         GFX::CommandBuffer& commandsInOut,
                         std::array<Camera*, 6>& cameras);

    void generateDualParaboloidMap(RenderPassParams& params,
                                   I16 arrayOffset,
                                   const vec3<F32>& pos,
                                   const vec2<F32>& zPlanes,
                                   GFX::CommandBuffer& bufferInOut,
                                   std::array<Camera*, 2>& cameras);

    /// Access (Read Only) rendering data used by the GFX
    inline const GFXShaderData::GPUData& renderingData() const noexcept;

    /// Returns true if the viewport was changed
           bool setViewport(const Rect<I32>& viewport);
    inline bool setViewport(I32 x, I32 y, I32 width, I32 height);
    inline const Rect<I32>& getViewport() const noexcept;

    void setPreviousViewProjection(const mat4<F32>& view, const mat4<F32>& projection) noexcept;
    mat4<F32> getPreviousViewProjection() const noexcept;

    inline F32 renderingAspectRatio() const noexcept;
    inline const vec2<U16>& renderingResolution() const noexcept;

    /// Switch between fullscreen rendering
    void toggleFullScreen() const;
    void increaseResolution();
    void decreaseResolution();

    void setScreenMSAASampleCount(U8 sampleCount);
    void setShadowMSAASampleCount(ShadowType type, U8 sampleCount);

    /// Save a screenshot in TGA format
    void screenshot(const ResourcePath& filename) const;

    ShaderComputeQueue& shaderComputeQueue() noexcept;
    const ShaderComputeQueue& shaderComputeQueue() const noexcept;

public:  // Accessors and Mutators
    inline Renderer& getRenderer() const;
    inline const GPUState& gpuState() const noexcept;
    inline GPUState& gpuState() noexcept;
    /// returns the standard state block
    size_t getDefaultStateBlock(bool noDepth) const noexcept;
    inline size_t get2DStateBlock() const noexcept;
    inline GFXRTPool& renderTargetPool() noexcept;
    inline const GFXRTPool& renderTargetPool() const noexcept;
    inline const ShaderProgram_ptr& getRTPreviewShader(bool depthOnly) const noexcept;
    inline Arena::Statistics getObjectAllocStats() const noexcept;
    inline void registerDrawCall() noexcept;
    inline void registerDrawCalls(U32 count) noexcept;
    inline const Rect<I32>& getCurrentViewport() const noexcept;

    DebugView* addDebugView(const std::shared_ptr<DebugView>& view);
    bool removeDebugView(DebugView* view);
    void toggleDebugView(I16 index, bool state);
    void toggleDebugGroup(I16 groupID, bool state);
    bool getDebugGroupState(I16 groupID) const;
    void getDebugViewNames(vector<std::tuple<string, I16, I16, bool>>& namesOut);

    [[nodiscard]] inline PerformanceMetrics getPerformanceMetrics() const noexcept;

    inline vec2<U16> getDrawableSize(const DisplayWindow& window) const;
    inline U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const;
    inline void onThreadCreated(const std::thread::id& threadID) const;

    static void FrameInterpolationFactor(const D64 interpolation) noexcept { s_interpolationFactor = interpolation; }
    static void setGPUVendor(const GPUVendor gpuVendor) noexcept { s_GPUVendor = gpuVendor; }
    static void setGPURenderer(const GPURenderer gpuRenderer) noexcept { s_GPURenderer = gpuRenderer; }

    [[nodiscard]] static D64 FrameInterpolationFactor() noexcept { return s_interpolationFactor; }
    [[nodiscard]] static GPUVendor getGPUVendor() noexcept { return s_GPUVendor; }
    [[nodiscard]] static GPURenderer getGPURenderer() noexcept { return s_GPURenderer; }

public:
    Mutex&       objectArenaMutex() noexcept;
    ObjectArena& objectArena() noexcept;

    /// Create and return a new immediate mode emulation primitive.
    IMPrimitive*       newIMP();
    bool               destroyIMP(IMPrimitive*& primitive);

    /// Create and return a new vertex array (VAO + VB + IB).
    VertexBuffer*      newVB();
    /// Create and return a new pixel buffer using the requested format.
    PixelBuffer*       newPB(PBType type = PBType::PB_TEXTURE_2D, const char* name = nullptr);
    /// Create and return a new generic vertex data object
    GenericVertexData* newGVD(U32 ringBufferLength, const char* name = nullptr);
    /// Create and return a new texture.
    Texture*           newTexture(size_t descriptorHash,
                                  const Str256& resourceName,
                                  const ResourcePath& assetNames,
                                  const ResourcePath& assetLocations,
                                  bool asyncLoad,
                                  const TextureDescriptor& texDescriptor);

    /// Create and return a new shader program.
    ShaderProgram*     newShaderProgram(size_t descriptorHash,
                                        const Str256& resourceName,
                                        const Str256& assetName,
                                        const ResourcePath& assetLocation,
                                        const ShaderProgramDescriptor& descriptor,
                                        bool asyncLoad);
    /// Create and return a new shader buffer. 
    /// The OpenGL implementation creates either an 'Uniform Buffer Object' if unbound is false
    /// or a 'Shader Storage Block Object' otherwise
    /// The shader buffer can also be persistently mapped, if requested
    ShaderBuffer*      newSB(const ShaderBufferDescriptor& descriptor);
    /// Create and return a new graphics pipeline. This is only used for caching and doesn't use the object arena
    Pipeline*          newPipeline(const PipelineDescriptor& descriptor);

    // Shortcuts
    void drawText(const GFX::DrawTextCommand& cmd, GFX::CommandBuffer& bufferInOut) const;
    void drawText(const TextElementBatch& batch, GFX::CommandBuffer& bufferInOut) const;

    // Render the texture using a custom viewport
    void drawTextureInViewport(TextureData data, size_t samplerHash, const Rect<I32>& viewport, bool convertToSrgb, bool drawToDepthOnly, GFX::CommandBuffer& bufferInOut);

    void blurTarget(RenderTargetHandle& blurSource, 
                    RenderTargetHandle& blurBuffer,
                    const RenderTargetHandle& blurTarget, ///< can be the same as source
                    RTAttachmentType att,
                    U8 index,
                    I32 kernelSize,
                    bool gaussian,
                    U8 layerCount,
                    GFX::CommandBuffer& bufferInOut);

    PROPERTY_RW(MaterialDebugFlag, materialDebugFlag, MaterialDebugFlag::COUNT);
    PROPERTY_RW(RenderAPI, renderAPI, RenderAPI::COUNT);
    PROPERTY_RW(bool, queryPerformanceStats, false);
    PROPERTY_R_IW(U32, frameDrawCalls, 0u);
    PROPERTY_R_IW(U32, frameDrawCallsPrev, 0u);
    PROPERTY_R_IW(U32, frameCount, 0u);
    PROPERTY_R_IW(U32, lastCullCount, 0u);

protected:
    void update(U64 deltaTimeUSFixed, U64 deltaTimeUSApp);

    /// Create and return a new framebuffer.
    RenderTarget* newRT(const RenderTargetDescriptor& descriptor);

    void drawText(const TextElementBatch& batch);

    // returns true if the window and the viewport have different aspect ratios
    bool fitViewportInWindow(U16 w, U16 h);

    bool onSizeChange(const SizeChangeParams& params);

    void initDebugViews();
    void renderDebugViews(Rect<I32> targetViewport, I32 padding, GFX::CommandBuffer& bufferInOut);
    
    void stepResolution(bool increment);
    void debugDrawLines(GFX::CommandBuffer& bufferInOut);
    void debugDrawBoxes(GFX::CommandBuffer& bufferInOut);
    void debugDrawCones(GFX::CommandBuffer& bufferInOut);
    void debugDrawSpheres(GFX::CommandBuffer& bufferInOut);
    void debugDrawFrustums(GFX::CommandBuffer& bufferInOut);

protected:
    friend class RenderPassManager;
    void renderDebugUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut);

protected:
    friend class SceneManager;
    friend class RenderPass;
    friend class RenderPassExecutor;

    void occlusionCull(const RenderStagePass& stagePass,
                       const RenderPass::BufferData& bufferData,
                       const Texture_ptr& depthBuffer,
                       size_t samplerHash,
                       GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                       GFX::CommandBuffer& bufferInOut) const;

    // Returns the HiZ texture that can be sent directly to occlusionCull
    std::pair<const Texture_ptr&, size_t> constructHIZ(RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut);

    void updateCullCount(const RenderPass::BufferData& bufferData, GFX::CommandBuffer& cmdBufferInOut);

    RenderAPIWrapper& getAPIImpl() { return *_api; }
    const RenderAPIWrapper& getAPIImpl() const { return *_api; }

private:
    /// Upload draw related data to the GPU (view & projection matrices, viewport settings, etc)
    void uploadGPUBlock();
    void setClipPlanes(const FrustumClipPlanes& clipPlanes);
    void renderFromCamera(const CameraSnapshot& cameraSnapshot);

    ErrorCode createAPIInstance(RenderAPI api);

private:
    eastl::unique_ptr<RenderAPIWrapper> _api = nullptr;
    eastl::unique_ptr<Renderer> _renderer = nullptr;

    ShaderComputeQueue* _shaderComputeQueue = nullptr;

    std::array<Line, 3> _axisLines;
    IMPrimitive* _axisGizmo = nullptr;

    DebugPrimitiveHandler<std::tuple<const Line* /*lines*/, size_t/*count*/>, 16u> _debugLines;
    DebugPrimitiveHandler<std::tuple<vec3<F32> /*min*/, vec3<F32> /*max*/, FColour3>, 16u> _debugBoxes;
    DebugPrimitiveHandler<std::tuple<vec3<F32> /*center*/, F32 /*radius*/, FColour3>, 16u> _debugSpheres;
    DebugPrimitiveHandler<std::tuple<vec3<F32> /*root*/, vec3<F32> /*dir*/, F32 /*length*/, F32 /*radius*/, FColour3>, 16u> _debugCones;
    DebugPrimitiveHandler<std::pair<Frustum, FColour3>, 8u> _debugFrustums;

    CameraSnapshot  _activeCameraSnapshot;

    GPUState _state;
    GFXRTPool* _rtPool = nullptr;

    std::pair<vec2<U16>, bool> _resolutionChangeQueued;

    /// The default render state but with depth testing disabled
    size_t _defaultStateNoDepthHash = 0;
    /// Special render state for 2D rendering
    size_t _state2DRenderingHash = 0;
    size_t _stateDepthOnlyRenderingHash = 0;
    /// The interpolation factor between the current and the last frame
    FrustumClipPlanes _clippingPlanes{};

    /// shader used to preview the depth buffer
    ShaderProgram_ptr _previewDepthMapShader = nullptr;
    ShaderProgram_ptr _previewRenderTargetColour = nullptr;
    ShaderProgram_ptr _previewRenderTargetDepth = nullptr;
    ShaderProgram_ptr _renderTargetDraw = nullptr;
    ShaderProgram_ptr _HIZConstructProgram = nullptr;
    ShaderProgram_ptr _HIZCullProgram = nullptr;
    ShaderProgram_ptr _displayShader = nullptr;
    ShaderProgram_ptr _depthShader = nullptr;
    ShaderProgram_ptr _textRenderShader = nullptr;
    ShaderProgram_ptr _blurBoxShaderSingle = nullptr;
    ShaderProgram_ptr _blurBoxShaderLayered = nullptr;
    ShaderProgram_ptr _blurGaussianShaderSingle = nullptr;
    ShaderProgram_ptr _blurGaussianShaderLayered = nullptr;
    
    Pipeline* _HIZPipeline = nullptr;
    Pipeline* _HIZCullPipeline = nullptr;
    Pipeline* _axisGizmoPipeline = nullptr;
    Pipeline* _debugGizmoPipeline = nullptr;
    GFX::BindPipelineCommand _drawFSTexturePipelineCmd;
    GFX::BindPipelineCommand _drawFSDepthPipelineCmd;
    GFX::BindPipelineCommand _blurBoxPipelineSingleCmd;
    GFX::BindPipelineCommand _blurBoxPipelineLayeredCmd;
    GFX::BindPipelineCommand _blurGaussianPipelineSingleCmd;
    GFX::BindPipelineCommand _blurGaussianPipelineLayeredCmd;

    PushConstants _textRenderConstants;
    Pipeline*     _textRenderPipeline = nullptr;
        
    Mutex _graphicsResourceMutex;
    vector<std::tuple<GraphicsResource::Type, I64, U64>> _graphicResources;

    Rect<I32> _viewport;
    vec2<U16> _renderingResolution;

    GFXShaderData _gpuBlock;

    mutable Mutex _debugViewLock;
    vector<DebugView_ptr> _debugViews;
    
    ShaderBuffer* _gfxDataBuffer = nullptr;
   
    Mutex _pipelineCacheLock;
    hashMap<size_t, Pipeline, NoHash<size_t>> _pipelineCache;

    std::stack<CameraSnapshot, eastl::fixed_vector<CameraSnapshot, 32, false>> _cameraSnapshots;
    std::stack<Rect<I32>> _viewportStack;
    Mutex _gpuObjectArenaMutex;
    Mutex _imprimitiveMutex;

    ObjectArena _gpuObjectArena;

    static D64 s_interpolationFactor;
    static GPUVendor s_GPUVendor;
    static GPURenderer s_GPURenderer;
};

namespace Attorney {
    class GFXDeviceGUI {
        static void drawText(const GFXDevice& device, const GFX::DrawTextCommand& cmd, GFX::CommandBuffer& bufferInOut) {
            return device.drawText(cmd, bufferInOut);
        }

        static void drawText(const GFXDevice& device, const TextElementBatch& batch, GFX::CommandBuffer& bufferInOut) {
            return device.drawText(batch, bufferInOut);
        }
        friend class GUI;
        friend class GUIText;
        friend class SceneGUIElements;
    };

    class GFXDeviceKernel {
        static bool onSizeChange(GFXDevice& device, const SizeChangeParams& params) {
            return device.onSizeChange(params);
        }
        
        static void update(GFXDevice& device, const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp) {
            device.update(deltaTimeUSFixed, deltaTimeUSApp);
        }

        friend class Kernel;
        friend class KernelApplication;
    };

    class GFXDeviceGraphicsResource {
       static void onResourceCreate(GFXDevice& device, GraphicsResource::Type type, I64 GUID, U64 nameHash) {
           ScopedLock<Mutex> w_lock(device._graphicsResourceMutex);
           device._graphicResources.emplace_back(type, GUID, nameHash);
       }

       static void onResourceDestroy(GFXDevice& device, [[maybe_unused]] GraphicsResource::Type type, I64 GUID, [[maybe_unused]] U64 nameHash) {
           ScopedLock<Mutex> w_lock(device._graphicsResourceMutex);
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
       friend class GraphicsResource;
    };

    class GFXDeviceGFXRTPool {
        static RenderTarget* newRT(GFXDevice& device, const RenderTargetDescriptor& descriptor) {
            return device.newRT(descriptor);
        };

        friend class GFXRTPool;
    };
};  // namespace Attorney
};  // namespace Divide

#include "GFXDevice.inl"

#endif
