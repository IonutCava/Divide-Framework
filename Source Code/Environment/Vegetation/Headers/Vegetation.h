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

#ifndef VEGETATION_H_
#define VEGETATION_H_

#include "Utility/Headers/ImageTools.h"
#include "Graphs/Headers/SceneNode.h"
#include "Platform/Threading/Headers/Task.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Headers/RenderStagePass.h"

namespace eastl {
    template <>
    struct hash<Divide::vec2<Divide::F32>>
    {
        size_t operator()(const Divide::vec2<Divide::F32>& a) const {
            size_t h = 17;
            Divide::Util::Hash_combine(h, a.x);
            Divide::Util::Hash_combine(h, a.y);
            return h;
        }
    };
}

namespace Divide {

class SceneState;
class VertexBuffer;
class RenderTarget;
class ShaderBuffer;
class TerrainChunk;
class SceneGraphNode;
class PlatformContext;
class RenderStateBlock;
class GenericVertexData;
enum class RenderStage : U8;

FWD_DECLARE_MANAGED_CLASS(Mesh);
FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(Terrain);
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);



struct VegetationDetails {
    U16 billboardCount = 0;
    Str256 name = "";
    string billboardTextureArray = "";
    std::shared_ptr<ImageTools::ImageData> grassMap;
    std::shared_ptr<ImageTools::ImageData> treeMap;
    Terrain_wptr parentTerrain;
    vector<ResourcePath> treeMeshes;
    vec4<F32> grassScales, treeScales;
    std::array<vec3<F32>, 4> treeRotations;
};

struct VegetationData {
    vec4<F32> _positionAndScale;
    vec4<F32> _orientationQuat;
    //x - array index, y - chunk ID, z - render flag, w - height scale
    vec4<F32> _data = { 1.0f, 1.0f, 1.0f, 0.0f };
};
//RenderDoc: mat4 transform; vec4 posAndIndex; vec4 extentAndRender;

/// Generates grass on the terrain.
/// Grass VB's + all resources are stored locally in the class.
class Vegetation final : public SceneNode {
   public:
    explicit Vegetation(GFXDevice& context, TerrainChunk& parentChunk, const VegetationDetails& details);
    ~Vegetation();

    void buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut) override;

    void getStats(U32& maxGrassInstances, U32& maxTreeInstances) const;

    //ToDo: Multiple terrains will NOT support this! To fix! -Ionut
    static void destroyStaticData();
    static void precomputeStaticData(GFXDevice& gfxDevice, U32 chunkSize, U32 maxChunkCount);
    static void createAndUploadGPUData(GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails);

  protected:
    static void createVegetationMaterial(GFXDevice& gfxDevice, const Terrain_ptr& terrain, const VegetationDetails& vegDetails);

    void prepareRender(SceneGraphNode* sgn,
                       RenderingComponent& rComp,
                       RenderStagePass renderStagePass,
                       const CameraSnapshot& cameraSnapshot,
                       bool refreshData) override;

    void sceneUpdate(U64 deltaTimeUS,
                     SceneGraphNode* sgn,
                     SceneState& sceneState) override;
   private:
    void uploadVegetationData(vector<Byte>& grassDataOut, vector<Byte> treeDataOut);
    void computeVegetationTransforms(bool treeData);
    void prepareDraw(SceneGraphNode* sgn);

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Vegetation"; }

   private:
    GFXDevice& _context;
    TerrainChunk& _terrainChunk;
    // variables
    Terrain_wptr _terrain;
    F32 _windX = 0.0f, _windZ = 0.0f, _windS = 0.0f;
    U64 _stateRefreshIntervalUS = Time::SecondsToMicroseconds(1);
    U64 _stateRefreshIntervalBufferUS = 0ULL;
    vec4<F32> _grassScales = VECTOR4_UNIT;
    vec4<F32> _treeScales = VECTOR4_UNIT;
    std::array<vec3<F32>, 4> _treeRotations;
    vec4<F32> _grassExtents = VECTOR4_UNIT;
    vec4<F32> _treeExtents = VECTOR4_UNIT;
    vector<ResourcePath> _treeMeshNames;
    std::shared_ptr<ImageTools::ImageData> _grassMap = nullptr;  ///< Dispersion map for grass placement
    std::shared_ptr<ImageTools::ImageData> _treeMap = nullptr;  ///< Dispersion map for tree placement

    SceneGraphNode* _treeParentNode = nullptr;

    U32 _instanceCountGrass = 0u;
    U32 _instanceCountTrees = 0u;
    F32 _grassDistance = -1.0f;
    F32 _treeDistance = -1.0f;

    Task* _buildTask = nullptr;

    Pipeline* _cullPipelineGrass = nullptr;
    Pipeline* _cullPipelineTrees = nullptr;
    vector<VegetationData> _tempGrassData;
    vector<VegetationData> _tempTreeData;

    GFX::CommandBuffer* _cullVegetationCmds = nullptr;
    static std::array<U16, 3> s_lodPartitions;
    static U32 s_maxGrassInstances;
    static U32 s_maxTreeInstances;
    static ShaderProgram_ptr s_cullShaderGrass;
    static ShaderProgram_ptr s_cullShaderTrees;
    static std::atomic_uint s_bufferUsage;
    static VertexBuffer* s_buffer;
    static ShaderBuffer* s_treeData;
    static ShaderBuffer* s_grassData;
    static vector<Mesh_ptr> s_treeMeshes;

    static eastl::unordered_set<vec2<F32>> s_treePositions;
    static eastl::unordered_set<vec2<F32>> s_grassPositions;

    static U32 s_maxChunks;

    static Material_ptr s_treeMaterial;
    static Material_ptr s_vegetationMaterial;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Vegetation);

}  // namespace Divide

#endif