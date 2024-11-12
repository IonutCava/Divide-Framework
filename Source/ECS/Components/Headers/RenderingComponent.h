/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_RENDERING_COMPONENT_H_
#define DVD_RENDERING_COMPONENT_H_

#include "SGNComponent.h"

#include "Geometry/Material/Headers/MaterialEnums.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStagePass.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Video/Headers/IMPrimitiveDescriptors.h"

namespace Divide {
struct NodeMaterialData;

class Sky;
class SubMesh;
class Material;
class GFXDevice;
class RenderBin;
class RenderPass;
class WaterPlane;
class RenderQueue;
class SceneGraphNode;
class ParticleEmitter;
class RenderPassExecutor;
class SceneEnvironmentProbePool;
class EnvironmentProbeComponent;

struct RenderBinItem;
struct Configuration;

using EnvironmentProbeList = vector<EnvironmentProbeComponent*>;

TYPEDEF_SMART_POINTERS_FOR_TYPE(Material);

namespace Attorney
{
    class RenderingCompRenderPass;
    class RenderingCompGFXDevice;
    class RenderingCompRenderBin;
    class RenderingCompRenderPassExecutor;
    class RenderingComponentSGN;
}

struct RenderParams
{
    GenericDrawCommand _cmd;
    Pipeline _pipeline;
};

struct RenderCbkParams
{
    explicit RenderCbkParams(GFXDevice& context,
                             const SceneGraphNode* sgn,
                             const RenderTargetID& renderTarget,
                             const RenderTargetID& oitTarget,
                             const RenderTargetID& hizTarget,
                             const ReflectorType reflectType,
                             const RefractorType refractType,
                             const U16 passIndex,
                             Camera* camera) noexcept
        : _context(context)
        , _sgn(sgn)
        , _renderTarget(renderTarget)
        , _oitTarget(oitTarget)
        , _hizTarget(hizTarget)
        , _camera(camera)
        , _passIndex(passIndex)
        , _reflectType(reflectType)
        , _refractType(refractType)
    {
    }

    GFXDevice& _context;
    const SceneGraphNode* _sgn{ nullptr };
    const RenderTargetID& _renderTarget;
    const RenderTargetID& _oitTarget;
    const RenderTargetID& _hizTarget;

    Camera* _camera{ nullptr };
    U16 _passIndex{ 0u };

    const ReflectorType _reflectType{ReflectorType::COUNT};
    const RefractorType _refractType{RefractorType::COUNT};
};

using RenderCallback = DELEGATE<bool, RenderCbkParams&, GFX::CommandBuffer&, GFX::MemoryBarrierCommand&>;

using DrawCommandContainer = eastl::fixed_vector<IndirectIndexedDrawCommand, Config::MAX_VISIBLE_NODES, false>;

BEGIN_COMPONENT(Rendering, ComponentType::RENDERING)
    friend class Attorney::RenderingCompRenderPass;
    friend class Attorney::RenderingCompGFXDevice;
    friend class Attorney::RenderingCompRenderBin;
    friend class Attorney::RenderingCompRenderPassExecutor;
    friend class Attorney::RenderingComponentSGN;

   public:
       static constexpr U8 MAX_LOD_LEVEL = 4u;

       enum class RenderOptions : U16
       {
           RENDER_GEOMETRY = toBit(1),
           RENDER_WIREFRAME = toBit(2),
           RENDER_SKELETON = toBit(3),
           RENDER_SELECTION = toBit(4),
           RENDER_AXIS = toBit(5),
           CAST_SHADOWS = toBit(6),
           RECEIVE_SHADOWS = toBit(7),
           IS_VISIBLE = toBit(8)
       };

       struct DrawCommands
       {
           GenericDrawCommandContainer _data;
           SharedMutex _dataLock;
       };

   public:
    explicit RenderingComponent(SceneGraphNode* parentSGN, PlatformContext& context);
    ~RenderingComponent() override;

    /// Returns true if the specified render option is enabled
    [[nodiscard]] bool renderOptionEnabled(RenderOptions option) const noexcept;
                  void toggleRenderOption(RenderOptions option, bool state, bool recursive = true);

                         void             setMinRenderRange(F32 minRange)                        noexcept;
                         void             setMaxRenderRange(F32 maxRange)                        noexcept;
                  inline void             setRenderRange(const F32 minRange, const F32 maxRange) noexcept { setMinRenderRange(minRange); setMaxRenderRange(maxRange); }
    [[nodiscard]] inline vec2<F32>        renderRange()                                    const noexcept { return _renderRange; }

    [[nodiscard]] inline bool lodLocked(const RenderStage stage)   const noexcept { return _lodLockLevels[to_base(stage)].first; }
                  inline void lockLoD(const U8 level)                             { _lodLockLevels.fill({ true, level }); }
                  inline void unlockLoD()                                         { _lodLockLevels.fill({ false, U8_ZERO }); }
                  inline void lockLoD(const RenderStage stage, U8 level) noexcept { _lodLockLevels[to_base(stage)] = { true, level }; }
                  inline void unlockLoD(const RenderStage stage)         noexcept { _lodLockLevels[to_base(stage)] = { false, U8_ZERO }; }
    [[nodiscard]]          U8 getLoDLevel(RenderStage renderStage) const noexcept;
    [[nodiscard]]          U8 getLoDLevel(const F32 distSQtoCenter, RenderStage renderStage, vec4<U16> lodThresholds);

                         void setIndexBufferElementOffset(size_t indexOffset) noexcept;
                         void setLoDIndexOffset(U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept;

    void getMaterialData(NodeMaterialData& dataOut) const;
    void rebuildMaterial();

                         void             instantiateMaterial(Handle<Material> material);
    [[nodiscard]] inline Handle<Material> getMaterialInstance() const noexcept { return _materialInstance; }

    [[nodiscard]] inline DrawCommands& drawCommands() noexcept { return _drawCommands; }

    inline void setReflectionCallback(const RenderCallback& cbk) { _reflectionCallback = cbk; }
    inline void setRefractionCallback(const RenderCallback& cbk) { _refractionCallback = cbk; }

    [[nodiscard]] bool canDraw(const RenderStagePass& renderStagePass);

    void drawDebugAxis();
    void drawSelectionGizmo();
    void drawSkeleton();
    void drawBounds(bool AABB, bool OBB, bool Sphere);
    
    PROPERTY_R(bool, showAxis, false);
    PROPERTY_R(bool, receiveShadows, false);
    PROPERTY_RW(bool, primitiveRestartRequired, false);
    PROPERTY_R(bool, castsShadows, false);
    PROPERTY_RW(bool, occlusionCull, true);
    PROPERTY_RW(F32, dataFlag, 1.0f);
    PROPERTY_R_IW(bool, isInstanced, false);
    PROPERTY_R_IW(bool, rebuildDrawCommands, false);

  protected:
                  void           getCommandBuffer(RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut);
    [[nodiscard]] RenderPackage& getDrawPackage(const RenderStagePass& renderStagePass);
    [[nodiscard]] U8             getLoDLevelInternal(const F32 distSQtoCenter, RenderStage renderStage, vec4<U16> lodThresholds);

    void toggleBoundsDraw(bool showAABB, bool showBS, bool showOBB, bool recursive);
    void onRenderOptionChanged(RenderOptions option, bool state);
    void clearDrawPackages(const RenderStage stage, const RenderPassType pass);
    void clearDrawPackages();
    void updateReflectRefractDescriptors(bool reflectState, bool refractState);

    [[nodiscard]] bool hasDrawCommands() noexcept;
                  void retrieveDrawCommands(const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut);

    /// Called after the parent node was rendered
    void postRender(const SceneRenderState& sceneRenderState,
                    const RenderStagePass& renderStagePass,
                    GFX::CommandBuffer& bufferInOut);

    bool prepareDrawPackage(const CameraSnapshot& cameraSnapshot,
                            const SceneRenderState& sceneRenderState,
                            const RenderStagePass& renderStagePass,
                            GFX::MemoryBarrierCommand& postDrawMemCmd,
                            bool refreshData);

    // This returns false if the node is not reflective, otherwise it generates a new reflection cube map
    // and saves it in the appropriate material slot
    [[nodiscard]] bool updateReflection(ReflectorType reflectorType,
                                        U16 reflectionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        GFX::CommandBuffer& bufferInOut,
                                        GFX::MemoryBarrierCommand& memCmdInOut);

    [[nodiscard]] bool updateRefraction(RefractorType refractorType,
                                        U16 refractionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        GFX::CommandBuffer& bufferInOut,
                                        GFX::MemoryBarrierCommand& memCmdInOut);

    void updateNearestProbes(const vec3<F32>& position);
 
    void onParentUsageChanged(NodeUsageContext context) const;
    void OnData(const ECS::CustomEvent& data) override;

   protected:
    GFXDevice& _gfxContext;
    const Configuration& _config;

    struct PackageEntry 
    {
        RenderPackage _package;
        U16 _index{ 0u };
    };

    using PackagesPerIndex = vector<PackageEntry>;
    using PackagesPerPassIndex = std::array<PackagesPerIndex, to_base(RenderStagePass::PassIndex::COUNT)>;
    using PackagesPerVariant = std::array<PackagesPerPassIndex, to_base(RenderStagePass::VariantType::COUNT)>;
    using PackagesPerPassType = std::array<PackagesPerVariant, to_base(RenderPassType::COUNT)>;

    vector<EnvironmentProbeComponent*> _envProbes{};

    Handle<Material> _materialInstance{ INVALID_HANDLE<Material> };
    RenderCallback _reflectionCallback{};
    RenderCallback _refractionCallback{};

    IM::LineDescriptor _axisGizmoLinesDescriptor;
    IM::LineDescriptor _skeletonLinesDescriptor;
    IM::OBBDescriptor _selectionGizmoDescriptor;

    SharedMutex _renderPackagesLock;

    DrawCommands _drawCommands;
    std::pair<Handle<Texture>, SamplerDescriptor> _reflectionPlanar{INVALID_HANDLE<Texture>, {}};
    std::pair<Handle<Texture>, SamplerDescriptor> _refractionPlanar{INVALID_HANDLE<Texture>, {}};
    std::pair<Handle<Texture>, SamplerDescriptor> _reflectionCube{ INVALID_HANDLE<Texture>, {} };
    std::pair<Handle<Texture>, SamplerDescriptor> _refractionCube{ INVALID_HANDLE<Texture>, {} };

    size_t _indexBufferOffsetCount{0u}; ///< Number of indices to skip in the batched index buffer
    std::array<std::pair<size_t, size_t>, MAX_LOD_LEVEL> _lodIndexOffsets{};
    std::array<U8, to_base(RenderStage::COUNT)> _lodLevels{};
    std::array<std::pair<bool, U8>, to_base(RenderStage::COUNT)> _lodLockLevels{};
    std::array<PackagesPerPassType, to_base(RenderStage::COUNT)> _renderPackages{};

    NodeIndirectionData _indirectionData{};

    vec2<F32> _renderRange;

    U32 _indirectionBufferEntry{ NodeIndirectionData::INVALID_IDX };
    U32 _renderMask{ 0u };
    U16 _reflectionProbeIndex{ 0u };


    bool _selectionGizmoDirty{ true };
    bool _drawAABB{ false };
    bool _drawOBB{ false };
    bool _drawBS{ false };
    bool _updateReflection{ false };
    bool _updateRefraction{ false };
    U32 _materialUpdateMask{ to_base(MaterialUpdateResult::OK) };

END_COMPONENT(Rendering);

namespace Attorney
{

class RenderingCompRenderPass
{
     /// Returning true or false controls our reflection/refraction budget only. 
     /// Return true if we executed an external render pass (e.g. water planar reflection)
     /// Return false for no or non-expensive updates (e.g. selected the nearest probe)
    [[nodiscard]] static bool updateReflection(RenderingComponent& renderable,
                                               const ReflectorType reflectorType,
                                               const U16 reflectionIndex,
                                               const bool inBudget,
                                               Camera* camera,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut)
    {
        return renderable.updateReflection(reflectorType, reflectionIndex, inBudget, camera, bufferInOut, memCmdInOut);
    }

    /// Return true if we executed an external render pass (e.g. water planar refraction)
    /// Return false for no or non-expensive updates (e.g. selected the nearest probe)
    [[nodiscard]] static bool updateRefraction(RenderingComponent& renderable,
                                               const RefractorType refractorType,
                                               const U16 refractionIndex,
                                               const bool inBudget,
                                               Camera* camera,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut)
    {
        return renderable.updateRefraction(refractorType, refractionIndex, inBudget, camera, bufferInOut, memCmdInOut);
    }

    [[nodiscard]] static bool prepareDrawPackage(RenderingComponent& renderable,
                                                    const CameraSnapshot& cameraSnapshot,
                                                    const SceneRenderState& sceneRenderState,
                                                    RenderStagePass renderStagePass,
                                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                                    const bool refreshData)
    {
        return renderable.prepareDrawPackage(cameraSnapshot, sceneRenderState, renderStagePass, postDrawMemCmd, refreshData);
    }

    [[nodiscard]] static bool hasDrawCommands(RenderingComponent& renderable) noexcept
    {
        return renderable.hasDrawCommands();
    }

    static void retrieveDrawCommands(RenderingComponent& renderable, const RenderStagePass stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut)
    {
        renderable.retrieveDrawCommands(stagePass, cmdOffset, cmdsInOut);
    }

    friend class Divide::RenderPass;
    friend class Divide::RenderQueue;
    friend class Divide::RenderPassExecutor;
};

class RenderingCompRenderBin
{
    static void postRender(RenderingComponent* renderable,
                           const SceneRenderState& sceneRenderState,
                           const RenderStagePass renderStagePass,
                           GFX::CommandBuffer& bufferInOut)
    {
        renderable->postRender(sceneRenderState, renderStagePass, bufferInOut);
    }

    [[nodiscard]] static RenderPackage& getDrawPackage(RenderingComponent* renderable, const RenderStagePass renderStagePass)
    {
        return renderable->getDrawPackage(renderStagePass);
    }

    [[nodiscard]] static size_t getStateHash( RenderingComponent* renderable, const RenderStagePass renderStagePass )
    {
        const RenderPackage& pkg = getDrawPackage( renderable, renderStagePass );
        return pkg.pipelineCmd()._pipeline->stateHash();
    }

    friend class Divide::RenderBin;
    friend struct Divide::RenderBinItem;
};

class RenderingCompRenderPassExecutor
{

    static void setIndirectionBufferEntry(RenderingComponent* renderable, const U32 indirectionBufferEntry) noexcept
    {
        renderable->_indirectionBufferEntry = indirectionBufferEntry;
    }

    [[nodiscard]] static U32 getIndirectionBufferEntry(RenderingComponent* const renderable) noexcept
    {
        return renderable->_indirectionBufferEntry;
    }

    [[nodiscard]] static const NodeIndirectionData& getIndirectionNodeData(RenderingComponent* const renderable) noexcept
    {
        return renderable->_indirectionData;
    }

    static void setTransformIDX(RenderingComponent* const renderable, const U32 transformIDX) noexcept
    {
        renderable->_indirectionData._transformIDX = transformIDX;
    }

    static void setMaterialIDX(RenderingComponent* const renderable, const U32 materialIDX) noexcept
    {
        renderable->_indirectionData._materialIDX = materialIDX;
    }

    static void getCommandBuffer(RenderingComponent* renderable, RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut)
    {
        renderable->getCommandBuffer(pkg, bufferInOut);
    }

    friend class Divide::RenderPassExecutor;
};

class RenderingComponentSGN
{
    static void onParentUsageChanged(const RenderingComponent& comp, const NodeUsageContext context)
    {
        comp.onParentUsageChanged(context);
    }

    friend class Divide::SceneGraphNode;
};
}  // namespace Attorney
}  // namespace Divide

#endif //DVD_RENDERING_COMPONENT_H_
