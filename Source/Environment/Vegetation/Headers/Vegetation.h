/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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

#ifndef DVD_VEGETATION_H_
#define DVD_VEGETATION_H_

#include "VegetationDescriptor.h"

#include "Utility/Headers/ImageTools.h"
#include "Graphs/Headers/SceneNode.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Platform/Threading/Headers/Task.h"
#include "Platform/Video/Headers/RenderStagePass.h"

namespace eastl
{
template <>
struct hash<Divide::vec2<Divide::F32>>
{
    size_t operator()( const Divide::vec2<Divide::F32>& a ) const
    {
        size_t h = 17;
        Divide::Util::Hash_combine( h, a.x, a.y );
        return h;
    }
};
} //namespace eastl

namespace Divide {

namespace GFX {
    class CommandBuffer;
};

class Mesh;
class Texture;
class Terrain;
class Pipeline;
class GFXDevice;
class SceneState;
class RenderTarget;
class TerrainChunk;
class ShaderProgram;
class SceneGraphNode;
class PlatformContext;
enum class RenderStage : U8;

FWD_DECLARE_MANAGED_CLASS(VertexBuffer);
FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

struct VegetationData
{
    float4 _positionAndScale;
    float4 _orientationQuat;
    // x - grass layer/tree species (source) or tree wind flag (draw),
    // y - grass chunk ID (source) or packed LoD/tint (draw)/tree cull radius,
    // z - visibility/fade, w - camera distance
    float4 _data = { 1.0f, 1.0f, 1.0f, 0.0f };
};
//RenderDoc: mat4 transform; vec4 posAndIndex; vec4 extentAndRender;

[[nodiscard]] inline BufferRange<> VegetationBufferRange( const U32 chunkIndex, const U32 chunkCapacity, const U32 instanceCount ) noexcept
{
    DIVIDE_ASSERT( instanceCount <= chunkCapacity );
    return { to_size( chunkIndex ) * to_size( chunkCapacity ) * sizeof( VegetationData ),
             to_size( instanceCount ) * sizeof( VegetationData ) };
}

[[nodiscard]] inline F32 VegetationTreeCullRadius( const BoundingBox& bounds ) noexcept
{
    const float3 furthest
    {
        std::max( std::abs( bounds._min.x ), std::abs( bounds._max.x ) ),
        std::max( std::abs( bounds._min.y ), std::abs( bounds._max.y ) ),
        std::max( std::abs( bounds._min.z ), std::abs( bounds._max.z ) )
    };
    return furthest.length();
}

struct VegetationChunkDrawState
{
    U32 _grassInstanceCount{ 0u };
    U32 _treeInstanceCount{ 0u };

    [[nodiscard]] constexpr bool hasInstances() const noexcept
    {
        return _grassInstanceCount > 0u || _treeInstanceCount > 0u;
    }

    [[nodiscard]] constexpr F32 requiredRenderRange( const F32 grassRange, const F32 treeRange ) const noexcept
    {
        return _treeInstanceCount > 0u ? std::max( grassRange, treeRange ) : grassRange;
    }
};

struct VegetationTreeRange
{
    U32 _offset = 0u;
    U32 _count = 0u;
    F32 _cullRadius = 0.f;
    SceneGraphNode* _parentNode = nullptr;
};

struct VegetationWindState
{
    U64 _frame = U64_MAX;
    float3 _wind{};

    [[nodiscard]] bool update(const U64 frame, const U64 deltaTimeUS, const float3& wind) noexcept
    {
        if (_frame == frame)
        {
            return false;
        }
        _frame = frame;
        const bool changed = _wind != wind;
        _wind = wind;
        return changed || (deltaTimeUS > 0u && wind.z != 0.f && (wind.x != 0.f || wind.y != 0.f));
    }
};

/// Trees sway only where their shadows are refreshed every frame: colour/pre-passes, spot and point shadows
/// and the nearest CSM cascade. Farther cascades and World AO use the rest pose so they remain cacheable.
[[nodiscard]] constexpr bool TreeWindAnimatedInPass( const RenderStagePass& stagePass ) noexcept
{
    if ( stagePass._stage != RenderStage::SHADOW ||
         stagePass._variant != static_cast<RenderStagePass::VariantType>(ShadowType::CSM) )
    {
        return true;
    }

    return stagePass._pass == RenderStagePass::PassIndex::PASS_0 &&
           stagePass._index != ShadowMap::WORLD_AO_LAYER_INDEX;
}

class VegetationInstance;

/// Generates grass on the terrain.
/// Grass VB's + all resources are stored locally in the class.
DEFINE_NODE_TYPE(Vegetation, SceneNodeType::TYPE_VEGETATION)
{
   public:
    explicit Vegetation( const ResourceDescriptor<Vegetation>& descriptor );
    ~Vegetation() override;

    using SceneNode::postLoad;
    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    bool load( PlatformContext & context ) override;
    bool unload() override;
    [[nodiscard]] bool boundsInWorldSpace() const noexcept override { return true; }

    void finalizeInstance(SceneGraphNode* sgn, const SceneState& sceneState);

  protected:
    void postLoad(SceneGraphNode* sgn) override;

    void prepareRender(SceneGraphNode* sgn,
                       RenderingComponent& rComp,
                       RenderPackage& pkg,
                       GFX::MemoryBarrierCommand& postDrawMemCmd,
                       RenderStagePass renderStagePass,
                       const CameraSnapshot& cameraSnapshot,
                       bool refreshData) override;

    void sceneUpdate(U64 deltaTimeUS,
                     SceneGraphNode* sgn,
                     SceneState& sceneState) override;
   private:
    void uploadVegetationData(vector<Byte>& grassDataOut, vector<Byte> treeDataOut);
    void prepareDraw(SceneGraphNode* sgn, VegetationInstance& instance);
    [[nodiscard]] Handle<Material> treeMaterialFor(Handle<Material> source);
    [[nodiscard]] VegetationInstance* findInstance(U32 chunkID);
    void applyWindAnimation();

   protected:
    friend class VegetationInstance;

    void registerInstance( U32 chunkID, VegetationInstance* instance);
    void unregisterInstance(U32 chunkID);

    vector<Str<256>> _treeMeshNames;

    SharedMutex _instanceLock;
    vector<std::pair<U32, VegetationInstance*>> _instances;

  private:
    const VegetationDescriptor _descriptor;

    VegetationWindState _windShadowState;
    /// Editor-controlled. Disabling it compiles wind out of the vegetation shaders and keeps every shadow static.
    bool _windAnimation = true;
    /// State currently applied to the vegetation materials. Written on the main thread by applyWindAnimation.
    std::atomic_bool _appliedWindAnimation{ true };
    F32 _windBoundsPadding = 0.f;
    float4 _grassExtents = VECTOR4_UNIT;
    Mutex _sceneUpdateLock;
    Mutex _geometryLock;
    Mutex _treeMaterialLock;

    F32 _grassDistance = -1.0f;
    F32 _treeDistance = -1.0f;
    F32 _globalWaterHeight = 0.f;
    F32 _instanceBoundsPadding = 0.f;
    U32 _maxGrassInstances = 0u;
    U32 _maxTreeInstances = 0u;

    Pipeline* _cullPipelineGrass = nullptr;
    Pipeline* _cullPipelineTrees = nullptr;
    Pipeline* _occlusionPipelineGrass = nullptr;
    Pipeline* _occlusionPipelineTrees = nullptr;

    std::array<U16, 3> _lodPartitions;
    VertexBuffer_uptr _buffer;
    ShaderBuffer_uptr _treeData;
    /// Per chunk and species, visible tree instances compacted by the cull shader (draws read this).
    ShaderBuffer_uptr _treeDrawData;
    /// One visible-instance count per chunk and species (MAX_VEGETATION_TREE_MESHES per chunk).
    ShaderBuffer_uptr _treeVisibleCounts;
    ShaderBuffer_uptr _grassData;
    ShaderBuffer_uptr _grassDrawData;
    ShaderBuffer_uptr _grassVisibleCounts;
    vector<Handle<Mesh>> _treeMeshes;
    vector<std::pair<Handle<Material>, Handle<Material>>> _treeMaterials;

    eastl::unordered_set<float2> _grassPositions;
    eastl::unordered_set<float2> _treePositions;

    Handle<ShaderProgram> _cullShaderGrass = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _cullShaderTrees = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _occlusionShaderGrass = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _occlusionShaderTrees = INVALID_HANDLE<ShaderProgram>;
    Handle<Material> _vegetationMaterial = INVALID_HANDLE<Material>;
};

FWD_DECLARE_MANAGED_CLASS(Vegetation);

class VegetationInstance : public PlatformContextComponent
{
public:
    explicit VegetationInstance( PlatformContext& context, Handle<Vegetation> parent, TerrainChunk* chunk );
    ~VegetationInstance();

    void computeTransforms();

private:
    vector<VegetationData> computeTransforms( bool treeData );

protected:
    friend class Vegetation;

    VegetationChunkDrawState _drawState;
    std::array<VegetationTreeRange, MAX_VEGETATION_TREE_MESHES> _treeRanges{};
    vector<VegetationData> _grassCandidates;
    vector<VegetationData> _treeCandidates;
    bool _initialized = false;

private:
    const Handle<Vegetation> _parent = INVALID_HANDLE<Vegetation>;
    const TerrainChunk* _chunk = nullptr;
};

FWD_DECLARE_MANAGED_CLASS( VegetationInstance );

}  // namespace Divide

#endif //DVD_VEGETATION_H_
