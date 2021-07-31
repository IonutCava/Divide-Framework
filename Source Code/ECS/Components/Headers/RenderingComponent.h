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
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

namespace Divide {
struct NodeMaterialData;

class Sky;
class SubMesh;
class Material;
class GFXDevice;
class RenderBin;
class WaterPlane;
class SceneGraphNode;
class ParticleEmitter;
class RenderPassExecutor;
class SceneEnvironmentProbePool;
class EnvironmentProbeComponent;

struct RenderBinItem;

using EnvironmentProbeList = vectorEASTL<EnvironmentProbeComponent*>;

TYPEDEF_SMART_POINTERS_FOR_TYPE(Material);

namespace Attorney {
    class RenderingCompRenderPass;
    class RenderingCompGFXDevice;
    class RenderingCompRenderBin;
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

using RenderCallback = DELEGATE<void, RenderPassManager*, RenderCbkParams&, GFX::CommandBuffer&>;

BEGIN_COMPONENT(Rendering, ComponentType::RENDERING)
    friend class Attorney::RenderingCompRenderPass;
    friend class Attorney::RenderingCompGFXDevice;
    friend class Attorney::RenderingCompRenderBin;
    friend class Attorney::RenderingComponentSGN;

   public:
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

   public:
    explicit RenderingComponent(SceneGraphNode* parentSGN, PlatformContext& context);
    ~RenderingComponent();


    void toggleRenderOption(RenderOptions option, bool state, bool recursive = true);
    [[nodiscard]] bool renderOptionEnabled(RenderOptions option) const;
    [[nodiscard]] bool renderOptionsEnabled(U32 mask) const;

    void setMinRenderRange(F32 minRange) noexcept;
    void setMaxRenderRange(F32 maxRange) noexcept;
    void setRenderRange(const F32 minRange, const F32 maxRange) noexcept { setMinRenderRange(minRange); setMaxRenderRange(maxRange); }
    [[nodiscard]] const vec2<F32>& renderRange() const noexcept { return _renderRange; }

    void lockLoD(U8 level) { _lodLockLevels.fill({ true, level }); }
    void unlockLoD() { _lodLockLevels.fill({ false, to_U8(0u) }); }
    void lockLoD(const RenderStage stage, U8 level) noexcept { _lodLockLevels[to_base(stage)] = { true, level }; }
    void unlockLoD(const RenderStage stage) noexcept { _lodLockLevels[to_base(stage)] = { false, to_U8(0u) }; }
    void instantiateMaterial(const Material_ptr& material);

    [[nodiscard]] bool lodLocked(const RenderStage stage) const noexcept { return _lodLockLevels[to_base(stage)].first; }

    void getMaterialData(NodeMaterialData& dataOut, NodeMaterialTextures& texturesOut) const;

    [[nodiscard]] RenderPackage& getDrawPackage(const RenderStagePass& renderStagePass);
    [[nodiscard]] const RenderPackage& getDrawPackage(const RenderStagePass& renderStagePass) const;

                  void setRebuildFlag(const RenderStagePass& renderStagePass, bool state);
    [[nodiscard]] bool getRebuildFlag(const RenderStagePass& renderStagePass) const;

    [[nodiscard]] size_t getSortKeyHash(const RenderStagePass& renderStagePass) const;

    [[nodiscard]] const Material_ptr& getMaterialInstance() const noexcept { return _materialInstance; }

    void rebuildMaterial();

    void setReflectionAndRefractionType(const ReflectorType reflectType, const RefractorType refractType) noexcept;
    void setReflectionCallback(const RenderCallback& cbk) { _reflectionCallback = cbk; }
    void setRefractionCallback(const RenderCallback& cbk) { _refractionCallback = cbk; }

    void drawDebugAxis(GFX::CommandBuffer& bufferInOut);
    void drawSelectionGizmo(GFX::CommandBuffer& bufferInOut);
    void drawSkeleton(GFX::CommandBuffer& bufferInOut);
    void drawBounds(bool AABB, bool OBB, bool Sphere, GFX::CommandBuffer& bufferInOut);

    [[nodiscard]] U8 getLodLevel(RenderStage stage) const;
    [[nodiscard]] U8 getLoDLevel(const F32 distSQtoCenter, RenderStage renderStage, const vec4<U16>& lodThresholds);

    void addShaderBuffer(const ShaderBufferBinding& binding) { _externalBufferBindings.push_back(binding); }
    [[nodiscard]] const auto& getShaderBuffers() const noexcept { return _externalBufferBindings; }

    [[nodiscard]] bool canDraw(const RenderStagePass& renderStagePass);

  protected:
    void toggleBoundsDraw(bool showAABB, bool showBS, bool recursive);

    void retrieveDrawCommands(const RenderStagePass& stagePass, U32 cmdOffset, NodeDataIdx dataIdx, DrawCommandContainer& cmdsInOut);
    [[nodiscard]] bool hasDrawCommands(const RenderStagePass& stagePass);
                  void onRenderOptionChanged(RenderOptions option, bool state);

    /// Called after the parent node was rendered
    void postRender(const SceneRenderState& sceneRenderState,
                    const RenderStagePass& renderStagePass,
                    GFX::CommandBuffer& bufferInOut);

    void rebuildDrawCommands(const RenderStagePass& stagePass, const Camera& crtCamera, RenderPackage& pkg) const;

    void prepareDrawPackage(const Camera& camera, const SceneRenderState& sceneRenderState, const RenderStagePass& renderStagePass, bool refreshData);

    // This returns false if the node is not reflective, otherwise it generates a new reflection cube map
    // and saves it in the appropriate material slot
    [[nodiscard]] bool updateReflection(U16 reflectionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        const SceneRenderState& renderState,
                                        GFX::CommandBuffer& bufferInOut);

    [[nodiscard]] bool updateRefraction(U16 refractionIndex,
                                        bool inBudget,
                                        Camera* camera,
                                        const SceneRenderState& renderState,
                                        GFX::CommandBuffer& bufferInOut);

    void updateNearestProbes(const vec3<F32>& position);
 
    PROPERTY_R(bool, showAxis, false);
    PROPERTY_R(bool, receiveShadows, false);
    PROPERTY_R(bool, castsShadows, false);
    PROPERTY_RW(bool, occlusionCull, true);
    PROPERTY_RW(F32, dataFlag, 1.0f);

   protected:

    void onMaterialChanged();
    void onParentUsageChanged(NodeUsageContext context) const;

    void OnData(const ECS::CustomEvent& data) override;

   protected:
    using PackagesPerIndex = vectorEASTLFast<RenderPackage>;
    using PackagesPerPassType = std::array<PackagesPerIndex, to_base(RenderPassType::COUNT)>;
    using PackagesPerStage = std::array<PackagesPerPassType, to_base(RenderStage::COUNT)>;
    PackagesPerStage _renderPackages{};

    using FlagsPerIndex = std::array<bool, 255>;
    using FlagsPerPassType = std::array<FlagsPerIndex, to_base(RenderPassType::COUNT)>;
    using FlagsPerStage = std::array<FlagsPerPassType, to_base(RenderStage::COUNT)>;
    FlagsPerStage _rebuildDrawCommandsFlags{};

    using OffsetsPerPassType = std::array<U32, to_base(RenderPassType::COUNT)>;

    RenderCallback _reflectionCallback{};
    RenderCallback _refractionCallback{};

    enum class DataType : U8 {
        REFLECT = 0,
        REFRACT,
        COUNT
    };
    struct ReflectRefractData {
        mutable SharedMutex _lock{};
        TextureData _texture;
        size_t _samplerHash = 0u;
        SamplerAddress _gpuAddress = 0u;
    };

    U16 _reflectionProbeIndex = 0u; //<Offset by 2 (and dec by 2 in shader) as 0 & 1 are the sky cubemaps (day & night respectively)
    
    // One for the reflection pass and one for the other passes
    std::array<ReflectRefractData, to_base(DataType::COUNT)> _reflectRefractData{};

    vectorEASTL<EnvironmentProbeComponent*> _envProbes{};
    vectorEASTL<ShaderBufferBinding> _externalBufferBindings{};

    Material_ptr _materialInstance = nullptr;
    GFXDevice& _context;
    const Configuration& _config;

    mat4<F32> _worldMatrixCache;
    mat4<F32> _worldOffsetMatrixCache;
    vec2<F32> _renderRange;
    bool _selectionGizmoDirty = true;

    Pipeline*    _primitivePipeline[3] = {nullptr, nullptr, nullptr};
    IMPrimitive* _boundingBoxPrimitive = nullptr;
    IMPrimitive* _orientedBoundingBoxPrimitive = nullptr;
    IMPrimitive* _boundingSpherePrimitive = nullptr;
    IMPrimitive* _skeletonPrimitive = nullptr;
    IMPrimitive* _axisGizmo = nullptr;
    IMPrimitive* _selectionGizmo = nullptr;

    U32 _renderMask = 0u;

    bool _drawAABB = false;
    bool _drawOBB = false;
    bool _drawBS = false;

    std::array<U8, to_base(RenderStage::COUNT)> _lodLevels{};
    ReflectorType _reflectorType = ReflectorType::CUBE;
    RefractorType _refractorType = RefractorType::COUNT;

    std::array<std::pair<bool, U8>, to_base(RenderStage::COUNT)> _lodLockLevels{};

    static hashMap<U32, DebugView*> s_debugViews[2];
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
                                               GFX::CommandBuffer& bufferInOut)
        {
            return renderable.updateReflection(reflectionIndex, inBudget, camera, renderState, bufferInOut);
        }

        /// Return true if we executed an external render pass (e.g. water planar refraction)
        /// Return false for no or non-expensive updates (e.g. selected the nearest probe)
        [[nodiscard]] static bool updateRefraction(RenderingComponent& renderable,
                                                   const U16 refractionIndex,
                                                   const bool inBudget,
                                                   Camera* camera,
                                                   const SceneRenderState& renderState,
                                                   GFX::CommandBuffer& bufferInOut)
        {
            return renderable.updateRefraction(refractionIndex, inBudget, camera, renderState, bufferInOut);
        }

        static void prepareDrawPackage(RenderingComponent& renderable,
                                       const Camera& camera,
                                       const SceneRenderState& sceneRenderState,
                                       const RenderStagePass& renderStagePass,
                                       const bool refreshData) {
            renderable.prepareDrawPackage(camera, sceneRenderState, renderStagePass, refreshData);
        }

        [[nodiscard]] static bool hasDrawCommands(RenderingComponent& renderable, const RenderStagePass& stagePass) {
            return renderable.hasDrawCommands(stagePass);
        }

        static void retrieveDrawCommands(RenderingComponent& renderable, const RenderStagePass& stagePass, const U32 cmdOffset, const NodeDataIdx dataIdx, DrawCommandContainer& cmdsInOut) {
            renderable.retrieveDrawCommands(stagePass, cmdOffset, dataIdx, cmdsInOut);
        }

          friend class Divide::RenderPass;
        friend class Divide::RenderPassExecutor;
};

class RenderingCompRenderBin {
    static void postRender(RenderingComponent* renderable,
                           const SceneRenderState& sceneRenderState,
                           const RenderStagePass& renderStagePass,
                           GFX::CommandBuffer& bufferInOut) {
        renderable->postRender(sceneRenderState, renderStagePass, bufferInOut);
    }

    friend class Divide::RenderBin;
    friend struct Divide::RenderBinItem;
};

class RenderingComponentSGN {
    static void onParentUsageChanged(RenderingComponent& comp, const NodeUsageContext context) {
        comp.onParentUsageChanged(context);
    }
    friend class Divide::SceneGraphNode;
};
}  // namespace Attorney
}  // namespace Divide
#endif