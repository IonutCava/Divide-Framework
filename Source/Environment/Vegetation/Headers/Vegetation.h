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
class GenericVertexData;
enum class RenderStage : U8;

FWD_DECLARE_MANAGED_CLASS(VertexBuffer);
FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

struct VegetationData
{
    float4 _positionAndScale;
    float4 _orientationQuat;
    //x - array index, y - chunk ID, z - render flag, w - height scale
    float4 _data = { 1.0f, 1.0f, 1.0f, 0.0f };
};
//RenderDoc: mat4 transform; vec4 posAndIndex; vec4 extentAndRender;

class VegetationInstance;

/// Generates grass on the terrain.
/// Grass VB's + all resources are stored locally in the class.
DEFINE_NODE_TYPE(Vegetation, SceneNodeType::TYPE_VEGETATION)
{
   public:
    explicit Vegetation( const ResourceDescriptor<Vegetation>& descriptor );
    ~Vegetation() override;

    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    bool load( PlatformContext & context ) override;
    bool unload() override;

  protected:
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
    void prepareDraw(SceneGraphNode* sgn);

   protected:
    friend class VegetationInstance;

    void registerInstance( U32 chunkID, VegetationInstance* instance);
    void unregisterInstance(U32 chunkID);

    vector<Str<256>> _treeMeshNames;

    SharedMutex _instanceLock;
    vector<std::pair<U32, VegetationInstance*>> _instances;

  private:
    const VegetationDescriptor _descriptor;

    F32 _windX = 0.0f, _windZ = 0.0f, _windS = 0.0f;
    U64 _stateRefreshIntervalUS = Time::SecondsToMicroseconds(1);
    U64 _stateRefreshIntervalBufferUS = 0ULL;
    float4 _grassExtents = VECTOR4_UNIT;
    float4 _treeExtents = VECTOR4_UNIT;

    SceneGraphNode* _treeParentNode = nullptr;

    F32 _grassDistance = -1.0f;
    F32 _treeDistance = -1.0f;
    U32 _maxGrassInstances = 0u;
    U32 _maxTreeInstances = 0u;

    Pipeline* _cullPipelineGrass = nullptr;
    Pipeline* _cullPipelineTrees = nullptr;
    SharedMutex _treeMeshLock;

    std::array<U16, 3> _lodPartitions;
    VertexBuffer_ptr _buffer;
    ShaderBuffer_uptr _treeData;
    ShaderBuffer_uptr _grassData;
    vector<Handle<Mesh>> _treeMeshes;

    eastl::unordered_set<float2> _grassPositions;
    eastl::unordered_set<float2> _treePositions;

    Handle<ShaderProgram> _cullShaderGrass = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _cullShaderTrees = INVALID_HANDLE<ShaderProgram>;
    Handle<Material> _treeMaterial = INVALID_HANDLE<Material>;
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

    U32 _instanceCountGrass = 0u;
    U32 _instanceCountTrees = 0u;
private:
    const Handle<Vegetation> _parent = INVALID_HANDLE<Vegetation>;
    const TerrainChunk* _chunk = nullptr;
};

FWD_DECLARE_MANAGED_CLASS( VegetationInstance );

}  // namespace Divide

#endif //DVD_VEGETATION_H_
