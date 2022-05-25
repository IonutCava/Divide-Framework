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
#ifndef _RENDERING_COMPONENT_H_
#define _RENDERING_COMPONENT_H_

#include "SGNComponent.h"

#include "Geometry/Material/Headers/MaterialEnums.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderStagePass.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

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

namespace Attorney {
    class RenderingCompRenderPass;
    class RenderingCompGFXDevice;
    class RenderingCompRenderBin;
    class RenderingCompRenderPassExecutor;
    class RenderingComponentSGN;
}

struct RenderParams {
    GenericDrawCommand _cmd;
    Pipeline _pipeline;
};

struct RenderCbkParams {
    explicit RenderCbkParams(GFXDevice& context,
                             const SceneGraphNode* sgn,
                             const SceneRenderState& sceneRenderState,
                             const RenderTargetID& renderTarget,
                             const U16 passIndex,
                             const U8 passVariant,
                             Camera* camera) noexcept
        : _context(context),
          _sgn(sgn),
          _sceneRenderState(sceneRenderState),
          _renderTarget(renderTarget),
          _camera(camera),
          _passIndex(passIndex),
          _passVariant(passVariant)
    {
    }

    GFXDevice& _context;
    const SceneGraphNode* _sgn = nullptr;
    const SceneRenderState& _sceneRenderState;
    const RenderTargetID& _renderTarget;

    Camera* _camera;
    U16 _passIndex;
    U8  _passVariant;
};

using RenderCallback = DELEGATE<void, RenderPassManager*, RenderCbkParams&, GFX::CommandBuffer&, GFX::MemoryBarrierCommand&>;

using DrawCommandContainer = eastl::fixed_vector<IndirectDrawCommand, Config::MAX_VISIBLE_NODES, false>;

BEGIN_COMPONENT(Rendering, ComponentType::RENDERING)
    friend class Attorney::RenderingCompRenderPass;
    friend class Attorney::RenderingCompGFXDevice;
    friend class Attorney::RenderingCompRenderBin;
    friend class Attorney::RenderingCompRenderPassExecutor;
    friend class Attorney::RenderingComponentSGN;

   public:
       static constexpr U8 MAX_LOD_LEVEL = 4u;

       enum class RenderOptions : U16 {
           RENDER_GEOMETRY = toBit(1),
           RENDER_WIREFRAME = toBit(2),
           RENDER_SKELETON = toBit(3),
           RENDER_SELECTION = toBit(4),
           RENDER_AXIS = toBit(5),
           CAST_SHADOWS = toBit(6),
           RECEIVE_SHADOWS = toBit(7),
           IS_VISIBLE = toBit(8)
       };

       enum class PackageUpdateState : U8 {
           NeedsRefresh = 0,
           NeedsNewCull,
           Processed,
           COUNT
       };

       struct DrawCommands {
           vector_fast<GFX::DrawCommand> _data;
           SharedMutex _dataLock;
       };

   public:
    explicit RenderingComponent(SceneGraphNode* parentSGN, PlatformContext& context);
    ~RenderingComponent();

    /// Returns true if the specified render option is enabled
    [[nodiscard]] bool renderOptionEnabled(RenderOptions option) const noexcept;
    /// Returns true if ALL of the options in the mask are enabled
    [[nodiscard]] bool renderOptionsEnabledALL(U32 mask) const noexcept;
    /// Returns true if ANY of the options in the mask are enabled
    [[nodiscard]] bool renderOptionsEnabledANY(U32 mask) const noexcept;
                  void toggleRenderOption(RenderOptions option, bool state, bool recursive = true);

                         void             setMinRenderRange(F32 minRange)                        noexcept;
                         void             setMaxRenderRange(F32 maxRange)                        noexcept;
                  inline void             setRenderRange(const F32 minRange, const F32 maxRange) noexcept { setMinRenderRange(minRange); setMaxRenderRange(maxRange); }
    [[nodiscard]] inline const vec2<F32>& renderRange()                                    const noexcept { return _renderRange; }

    [[nodiscard]] inline bool lodLocked(const RenderStage stage)   const noexcept { return _lodLockLevels[to_base(stage)].first; }
                  inline void lockLoD(const U8 level)                             { _lodLockLevels.fill({ true, level }); }
                  inline void unlockLoD()                                         { _lodLockLevels.fill({ false, to_U8(0u) }); }
                  inline void lockLoD(const RenderStage stage, U8 level) noexcept { _lodLockLevels[to_base(stage)] = { true, level }; }
                  inline void unlockLoD(const RenderStage stage)         noexcept { _lodLockLevels[to_base(stage)] = { false, to_U8(0u) }; }
    [[nodiscard]]          U8 getLoDLevel(RenderStage renderStage) const noexcept;
    [[nodiscard]]          U8 getLoDLevel(const F32 distSQtoCenter, RenderStage renderStage, const vec4<U16>& lodThresholds);
                         void setLoDIndexOffset(U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept;

    void getMaterialData(NodeMaterialData& dataOut) const;
    void getMaterialTextures(NodeMaterialTextures& texturesOut, SamplerAddress defaultTexAddress) const;

    void rebuildMaterial();

                         void                instantiateMaterial(const Material_ptr& material);
    [[nodiscard]] inline const Material_ptr& getMaterialInstance() const noexcept { return _materialInstance; }

    [[nodiscard]] inline DrawCommands& drawCommands() noexcept { return _drawCommands; }

    inline void setReflectionCallback(const RenderCallback& cbk, const ReflectorType reflectType) { _reflectionCallback = cbk; _reflectorType = reflectType; }
    inline void setRefractionCallback(const RenderCallback& cbk, const RefractorType refractType) { _refractionCallback = cbk; _refractorType = refractType; }

    [[nodiscard]] bool canDraw(const RenderStagePass& renderStagePass);

    [[nodiscard]] DescriptorSet& getDescriptorSet(const RenderStagePass& renderStagePass);
    [[nodiscard]] PushConstants& getPushConstants(const RenderStagePass& renderStagePass);
    [[nodiscard]] size_t         getPipelineHash(const RenderStagePass& renderStagePass);

    void addAdditionalCommands(const RenderStagePass& renderStagePass, GFX::CommandBuffer* cmdBuffer);

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
    PROPERTY_RW(PackageUpdateState, packageUpdateState, PackageUpdateState::COUNT);
    PROPERTY_R_IW(bool, rebuildDrawCommands, false);

  protected:
                  void           getCommandBuffer(RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut);
    [[nodiscard]] RenderPackage& getDrawPackage(const RenderStagePass& renderStagePass);
    [[nodiscard]] U8             getLoDLevelInternal(const F32 distSQtoCenter, RenderStage renderStage, const vec4<U16>& lodThresholds);

    void toggleBoundsDraw(bool showAABB, bool showBS, bool showOBB, bool recursive);
    void onRenderOptionChanged(RenderOptions option, bool state);
    void clearDrawPackages();

    [[nodiscard]] bool hasDrawCommands() noexcept;
                  void retrieveDrawCommands(const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut);

    /// Called after the parent node was rendered
    void postRender(const SceneRenderState& sceneRenderState,
                    const RenderStagePass& renderStagePass,
                    GFX::CommandBuffer& bufferInOut);

    bool prepareDrawPackage(const CameraSnapshot& cameraSnapshot,
                            const SceneRenderState& sceneRenderState,
                            const RenderStagePass& renderStagePass,
                            bool refreshData);

    // This returns false if the node is not reflective, otherwise it generates a new reflection cube map
    // and saves it in the appropriate material slot
    [[nodiscard]] bool updateReflection(U16 reflectionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        const SceneRenderState& renderState,
                                        GFX::CommandBuffer& bufferInOut,
                                        GFX::MemoryBarrierCommand& memCmdInOut);

    [[nodiscard]] bool updateRefraction(U16 refractionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        const SceneRenderState& renderState,
                                        GFX::CommandBuffer& bufferInOut,
                                        GFX::MemoryBarrierCommand& memCmdInOut);

    void updateNearestProbes(const vec3<F32>& position);
 
    void onParentUsageChanged(NodeUsageContext context) const;
    void OnData(const ECS::CustomEvent& data) override;

   protected:
    GFXDevice& _context;
    const Configuration& _config;

    struct PackageEntry { RenderPackage _package; U16 _index{0u}; };
    using PackagesPerIndex = vector_fast<PackageEntry>;
    using PackagesPerPassIndex = std::array<PackagesPerIndex, to_base(RenderStagePass::PassIndex::COUNT)>;
    using PackagesPerVariant = std::array<PackagesPerPassIndex, to_base(RenderStagePass::VariantType::COUNT)>;
    using PackagesPerPassType = std::array<PackagesPerVariant, to_base(RenderPassType::COUNT)>;

    vector<EnvironmentProbeComponent*> _envProbes{};

    Material_ptr _materialInstance{ nullptr };
    RenderCallback _reflectionCallback{};
    RenderCallback _refractionCallback{};

    IMPrimitive::LineDescriptor _axisGizmoLinesDescriptor;
    IMPrimitive::LineDescriptor _skeletonLinesDescriptor;
    IMPrimitive::OBBDescriptor _selectionGizmoDescriptor;

    SharedMutex _renderPackagesLock;

    DrawCommands _drawCommands;

    std::array<U8, to_base(RenderStage::COUNT)> _lodLevels{};
    std::array<std::pair<size_t, size_t>, MAX_LOD_LEVEL> _lodIndexOffsets{};
    std::array<std::pair<bool, U8>, to_base(RenderStage::COUNT)> _lodLockLevels{};
    std::array<PackagesPerPassType, to_base(RenderStage::COUNT)> _renderPackages{};

    vec2<F32> _renderRange;

    U32 _indirectionBufferEntry{ U32_MAX };
    U32 _renderMask{ 0u };
    U16 _reflectionProbeIndex{ 0u };

    ReflectorType _reflectorType{ ReflectorType::CUBE };
    RefractorType _refractorType{ RefractorType::PLANAR };

    bool _selectionGizmoDirty{ true };
    bool _drawAABB{ false };
    bool _drawOBB{ false };
    bool _drawBS{ false };

END_COMPONENT(Rendering);

namespace Attorney {
class RenderingCompRenderPass {
     /// Returning true or false controls our reflection/refraction budget only. 
     /// Return true if we executed an external render pass (e.g. water planar reflection)
     /// Return false for no or non-expensive updates (e.g. selected the nearest probe)
    [[nodiscard]] static bool updateReflection(RenderingComponent& renderable,
                                               const U16 reflectionIndex,
                                               const bool inBudget,
                                               Camera* camera,
                                               const SceneRenderState& renderState,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut)
    {
        return renderable.updateReflection(reflectionIndex, inBudget, camera, renderState, bufferInOut, memCmdInOut);
    }

    /// Return true if we executed an external render pass (e.g. water planar refraction)
    /// Return false for no or non-expensive updates (e.g. selected the nearest probe)
    [[nodiscard]] static bool updateRefraction(RenderingComponent& renderable,
                                                const U16 refractionIndex,
                                                const bool inBudget,
                                                Camera* camera,
                                                const SceneRenderState& renderState,
                                                GFX::CommandBuffer& bufferInOut,
                                                GFX::MemoryBarrierCommand& memCmdInOut)
    {
        return renderable.updateRefraction(refractionIndex, inBudget, camera, renderState, bufferInOut, memCmdInOut);
    }

    [[nodiscard]] static bool prepareDrawPackage(RenderingComponent& renderable,
                                                    const CameraSnapshot& cameraSnapshot,
                                                    const SceneRenderState& sceneRenderState,
                                                    RenderStagePass renderStagePass,
                                                    const bool refreshData)
    {
        return renderable.prepareDrawPackage(cameraSnapshot, sceneRenderState, renderStagePass, refreshData);
    }

    [[nodiscard]] static bool hasDrawCommands(RenderingComponent& renderable) noexcept {
        return renderable.hasDrawCommands();
    }

    static void retrieveDrawCommands(RenderingComponent& renderable, const RenderStagePass stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut) {
        renderable.retrieveDrawCommands(stagePass, cmdOffset, cmdsInOut);
    }

    friend class Divide::RenderPass;
    friend class Divide::RenderQueue;
    friend class Divide::RenderPassExecutor;
};

class RenderingCompRenderBin {
    static void postRender(RenderingComponent* renderable,
                           const SceneRenderState& sceneRenderState,
                           const RenderStagePass renderStagePass,
                           GFX::CommandBuffer& bufferInOut) {
        renderable->postRender(sceneRenderState, renderStagePass, bufferInOut);
    }

    [[nodiscard]] static RenderPackage& getDrawPackage(RenderingComponent* renderable, const RenderStagePass renderStagePass) {
        return renderable->getDrawPackage(renderStagePass);
    }

    friend class Divide::RenderBin;
    friend struct Divide::RenderBinItem;
};

class RenderingCompRenderPassExecutor {

    static void setIndirectionBufferEntry(RenderingComponent* renderable, const U32 indirectionBufferEntry) noexcept {
        renderable->_indirectionBufferEntry = indirectionBufferEntry;
    }

    [[nodiscard]] static U32 getIndirectionBufferEntry(RenderingComponent* const renderable) noexcept {
        return renderable->_indirectionBufferEntry;
    }

    static void getCommandBuffer(RenderingComponent* renderable, RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut) {
        renderable->getCommandBuffer(pkg, bufferInOut);
    }

    friend class Divide::RenderPassExecutor;
};

class RenderingComponentSGN {
    static void onParentUsageChanged(const RenderingComponent& comp, const NodeUsageContext context) {
        comp.onParentUsageChanged(context);
    }

    friend class Divide::SceneGraphNode;
};
}  // namespace Attorney
}  // namespace Divide
#endif