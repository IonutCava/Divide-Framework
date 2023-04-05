#include "stdafx.h"

#include "Headers/Terrain.h"

#include "Core/Headers/Configuration.h"
#include "Headers/TerrainChunk.h"
#include "Headers/TerrainDescriptor.h"
#include "Headers/TileRing.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/SceneManager.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

#include "Geometry/Material/Headers/Material.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"

namespace Divide {

namespace {
    vector_fast<U16> CreateTileQuadListIB()
    {
        vector_fast<U16> indices(TessellationParams::QUAD_LIST_INDEX_COUNT, 0u);

        U16 index = 0u;

        // The IB describes one tile of NxN patches.
        // Four vertices per quad, with VTX_PER_TILE_EDGE-1 quads per tile edge.
        for (U8 y = 0u; y < TessellationParams::VTX_PER_TILE_EDGE - 1; ++y)
        {
            const U16 rowStart = y * TessellationParams::VTX_PER_TILE_EDGE;

            for (U8 x = 0u; x < TessellationParams::VTX_PER_TILE_EDGE - 1; ++x) {
                indices[index++] = rowStart + x;
                indices[index++] = rowStart + x + TessellationParams::VTX_PER_TILE_EDGE;
                indices[index++] = rowStart + x + TessellationParams::VTX_PER_TILE_EDGE + 1;
                indices[index++] = rowStart + x + 1;
            }
        }
        assert(index == TessellationParams::QUAD_LIST_INDEX_COUNT);

        return indices;
    }
}

void TessellationParams::fromDescriptor(const std::shared_ptr<TerrainDescriptor>& descriptor) noexcept {
    WorldScale(descriptor->dimensions() * 0.5f / to_F32(PATCHES_PER_TILE_EDGE));
}

Terrain::Terrain(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : Object3D(context, parentCache, descriptorHash, name, ResourcePath{ name }, {}, SceneNodeType::TYPE_TERRAIN, ObjectFlag::OBJECT_FLAG_NO_VB),
      _terrainQuadtree()
{
    _renderState.addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(LightType::SPOT));
    _renderState.addToDrawExclusionMask(RenderStage::SHADOW, RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(LightType::POINT));
}

void Terrain::postLoad(SceneGraphNode* sgn) {

    if (!_initialSetupDone) {
        _editorComponent.onChangedCbk([this](const std::string_view field) {onEditorChange(field); });

        EditorComponentField tessTriangleWidthField = {};
        tessTriangleWidthField._name = "Tessellated Triangle Width";
        tessTriangleWidthField._dataGetter = [&](void* dataOut) noexcept {
            *static_cast<U32*>(dataOut) = tessParams().tessellatedTriangleWidth();
        };
        tessTriangleWidthField._dataSetter = [&](const void* data) noexcept {
            _tessParams.tessellatedTriangleWidth(*static_cast<const U32*>(data));
        };
        tessTriangleWidthField._type = EditorComponentFieldType::SLIDER_TYPE;
        tessTriangleWidthField._readOnly = false;
        tessTriangleWidthField._serialise = true;
        tessTriangleWidthField._basicType = GFX::PushConstantType::UINT;
        tessTriangleWidthField._range = { 1.0f, 50.0f };
        tessTriangleWidthField._step = 1.0f;

        _editorComponent.registerField(MOV(tessTriangleWidthField));

        EditorComponentField toggleBoundsField = {};
        toggleBoundsField._name = "Toggle Quadtree Bounds";
        toggleBoundsField._range = { toggleBoundsField._name.length() * 10.0f, 20.0f };//dimensions
        toggleBoundsField._type = EditorComponentFieldType::BUTTON;
        toggleBoundsField._readOnly = false; //disabled/enabled
        _editorComponent.registerField(MOV(toggleBoundsField));

        PlatformContext& pContext = sgn->context();
        SceneManager* sMgr = pContext.kernel().sceneManager();

        EditorComponentField grassVisibilityDistanceField = {};
        grassVisibilityDistanceField._name = "Grass visibility distance";
        grassVisibilityDistanceField._range = { 0.01f, 10000.0f };
        grassVisibilityDistanceField._serialise = false;
        grassVisibilityDistanceField._dataGetter = [sMgr](void* dataOut) noexcept {
            const SceneRenderState& rState = sMgr->getActiveScene().state()->renderState();
            *static_cast<F32*>(dataOut) = rState.grassVisibility();
        };
        grassVisibilityDistanceField._dataSetter = [sMgr](const void* data) noexcept {
            SceneRenderState& rState = sMgr->getActiveScene().state()->renderState();
            rState.grassVisibility(*static_cast<const F32*>(data)); 
        };
        grassVisibilityDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
        grassVisibilityDistanceField._basicType = GFX::PushConstantType::FLOAT;
        grassVisibilityDistanceField._readOnly = false;
        _editorComponent.registerField(MOV(grassVisibilityDistanceField));

        EditorComponentField treeVisibilityDistanceField = {};
        treeVisibilityDistanceField._name = "Tree visibility distance";
        treeVisibilityDistanceField._range = { 0.01f, 10000.0f };
        treeVisibilityDistanceField._serialise = false;
        treeVisibilityDistanceField._dataGetter = [sMgr](void* dataOut) noexcept {
            const SceneRenderState& rState = sMgr->getActiveScene().state()->renderState();
            *static_cast<F32*>(dataOut) = rState.treeVisibility();
        };
        treeVisibilityDistanceField._dataSetter = [sMgr](const void* data) noexcept {
            SceneRenderState& rState = sMgr->getActiveScene().state()->renderState();
            rState.treeVisibility(*static_cast<const F32*>(data));
        };
        treeVisibilityDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
        treeVisibilityDistanceField._basicType = GFX::PushConstantType::FLOAT;
        treeVisibilityDistanceField._readOnly = false;
        _editorComponent.registerField(MOV(treeVisibilityDistanceField));
        _initialSetupDone = true;
    }

    SceneGraphNodeDescriptor vegetationParentNode;
    vegetationParentNode._serialize = false;
    vegetationParentNode._name = "Vegetation";
    vegetationParentNode._usageContext = NodeUsageContext::NODE_STATIC;
    vegetationParentNode._componentMask = to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS);
    SceneGraphNode* vegParent = sgn->addChildNode(vegetationParentNode);
    assert(vegParent != nullptr);

    SceneGraphNodeDescriptor vegetationNodeDescriptor;
    vegetationNodeDescriptor._serialize = false;
    vegetationNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
    vegetationNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                              to_base(ComponentType::BOUNDS) |
                                              to_base(ComponentType::RENDERING);

    for (const TerrainChunk* chunk : _terrainChunks) {
        vegetationNodeDescriptor._node = Attorney::TerrainChunkTerrain::getVegetation(*chunk);
        vegetationNodeDescriptor._name = Util::StringFormat("Vegetation_chunk_%d", chunk->ID());
        vegParent->addChildNode(vegetationNodeDescriptor);
    }

    sgn->get<RenderingComponent>()->lockLoD(0u);

    SceneNode::postLoad(sgn);
}

void Terrain::onEditorChange(const std::string_view field) {
    if (field == "Toggle Quadtree Bounds") {
        toggleBoundingBoxes();
    }
}

void Terrain::postBuild() {
    const U16 terrainWidth = _descriptor->dimensions().width;
    const U16 terrainHeight = _descriptor->dimensions().height;
    {
        vector<vec3<U32>> triangles;
        // Generate index buffer
        triangles.reserve((terrainWidth - 1) * (terrainHeight - 1) * 2);

        //Ref : https://www.3dgep.com/multi-textured-terrain-in-opengl/
        for (U32 height = 0; height < to_U32(terrainHeight - 1); ++height) {
            for (U32 width = 0; width < to_U32(terrainWidth - 1); ++width) {
                const U32 vertexIndex = TER_COORD(width, height, to_U32(terrainWidth));
                // Top triangle (T0)
                triangles.emplace_back(vertexIndex, vertexIndex + terrainWidth + 1u, vertexIndex + 1u);
                // Bottom triangle (T1)
                triangles.emplace_back(vertexIndex, vertexIndex + terrainWidth, vertexIndex + terrainWidth + 1u);
            }
        }

        addTriangles(0, triangles);
    }

    // Approximate bounding box
    const F32 halfWidth = terrainWidth * 0.5f;
    _boundingBox.setMin(-halfWidth, _descriptor->altitudeRange().min, -halfWidth);
    _boundingBox.setMax(halfWidth, _descriptor->altitudeRange().max, halfWidth);

    _terrainQuadtree.build(_boundingBox, _descriptor->dimensions(), this);

    // The terrain's final bounding box is the QuadTree's root bounding box
    _boundingBox.set(_terrainQuadtree.computeBoundingBox());

    {
        // widths[0] doesn't define a ring hence -1
        const U8 ringCount = _descriptor->ringCount() - 1;
        _tileRings.reserve(ringCount);
        F32 tileWidth = _descriptor->startWidth();
        for (U8 i = 0; i < ringCount ; ++i) {
            const U8 count0 = _descriptor->tileRingCount(i + 0);
            const U8 count1 = _descriptor->tileRingCount(i + 1);
            _tileRings.emplace_back(eastl::make_unique<TileRing>(count0 / 2, count1, tileWidth));
            tileWidth *= 2.0f;
        }

        // This is a whole fraction of the max tessellation, i.e., 64/N.  The intent is that 
        // the height field scrolls through the terrain mesh in multiples of the polygon spacing.
        // So polygon vertices mostly stay fixed relative to the displacement map and this reduces
        // scintillation.  Without snapping, it scintillates badly.  Additionally, we make the
        // snap size equal to one patch width, purely to stop the patches dancing around like crazy.
        // The non-debug rendering works fine either way, but crazy flickering of the debug patches 
        // makes understanding much harder.
        _tessParams.SnapGridSize(tessParams().WorldScale() * _tileRings[ringCount - 1]->tileSize() / TessellationParams::PATCHES_PER_TILE_EDGE);
        vector_fast<U16> indices = CreateTileQuadListIB();

        { // Create a single buffer to hold the data for all of our tile rings
            GenericVertexData::IndexBuffer idxBuff{};
            idxBuff.smallIndices = true;
            idxBuff.count = indices.size();
            idxBuff.data = indices.data();
            idxBuff.dynamic = false;

            _terrainBuffer = _context.newGVD(1, true, _descriptor->_name.c_str());
            {
                const BufferLock lock = _terrainBuffer->setIndexBuffer(idxBuff);
                DIVIDE_UNUSED(lock);
            }
            vector<TileRing::InstanceData> vbData;
            vbData.reserve(TessellationParams::QUAD_LIST_INDEX_COUNT * ringCount);

            GenericVertexData::SetBufferParams params = {};
            params._bindConfig = { 0u, 0u };
            params._bufferParams._elementSize = sizeof(TileRing::InstanceData);
            params._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
            params._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;

            for (size_t i = 0u; i < ringCount; ++i) {
                vector<TileRing::InstanceData> ringData = _tileRings[i]->createInstanceDataVB(to_I32(i));
                vbData.insert(cend(vbData), cbegin(ringData), cend(ringData));
                params._bufferParams._elementCount += to_U32(ringData.size());
            }
            params._initialData = { (Byte*)vbData.data(), vbData.size() * sizeof(TileRing::InstanceData) };
            {
                const BufferLock lock = _terrainBuffer->setBuffer(params);
                DIVIDE_UNUSED(lock);
            }
        }
    }
}

void Terrain::toggleBoundingBoxes() {
    _terrainQuadtree.toggleBoundingBoxes();
    rebuildDrawCommands(true);
}

void Terrain::prepareRender(SceneGraphNode* sgn,
                            RenderingComponent& rComp,
                            RenderPackage& pkg,
                            GFX::MemoryBarrierCommand& postDrawMemCmd,
                            const RenderStagePass renderStagePass,
                            const CameraSnapshot& cameraSnapshot,
                            const bool refreshData)
{
    if (renderStagePass._stage == RenderStage::DISPLAY && renderStagePass._passType == RenderPassType::MAIN_PASS) {
        _terrainQuadtree.drawBBox(sgn->context().gfx());
    }

    const F32 triangleWidth = to_F32(tessParams().tessellatedTriangleWidth());
    if (renderStagePass._stage == RenderStage::REFLECTION ||
        renderStagePass._stage == RenderStage::REFRACTION)                 
    {
        // Lower the level of detail in reflections and refractions
        //triangleWidth *= 1.5f;
    } else if (renderStagePass._stage == RenderStage::SHADOW) {
        //triangleWidth *= 2.0f;
    }

    const vec2<F32> SNAP_GRID_SIZE = tessParams().SnapGridSize();
    vec3<F32> cullingEye = cameraSnapshot._eye;
    const vec2<F32> eyeXZ = cullingEye.xz();

    vec2<F32> snappedXZ = eyeXZ;
    for (U8 i = 0; i < 2; ++i) {
        if (SNAP_GRID_SIZE[i] > 0.f) {
            snappedXZ[i] = std::floorf(snappedXZ[i] / SNAP_GRID_SIZE[i]) * SNAP_GRID_SIZE[i];
        }
    }

    vec2<F32> uvEye = snappedXZ;
    uvEye /= tessParams().WorldScale();
    uvEye *= -1;
    uvEye /= (TessellationParams::PATCHES_PER_TILE_EDGE * 2);

    const vec2<F32> dXZ = eyeXZ - snappedXZ;
    snappedXZ = eyeXZ - dXZ;
        
    cullingEye.x += snappedXZ[0];
    cullingEye.z += snappedXZ[1];

    const mat4<F32> terrainWorldMat(vec3<F32>(snappedXZ[0], 0.f, snappedXZ[1]),
                                    vec3<F32>(tessParams().WorldScale()[0], 1.f, tessParams().WorldScale()[1]),
                                    mat3<F32>());

    PushConstants& constants = pkg.pushConstantsCmd()._constants;
    constants.set(_ID("dvd_terrainWorld"), GFX::PushConstantType::MAT4, terrainWorldMat);
    constants.set(_ID("dvd_uvEyeOffset"), GFX::PushConstantType::VEC2, uvEye);
    constants.set(_ID("dvd_tessTriangleWidth"),  GFX::PushConstantType::FLOAT, triangleWidth);
    

    Object3D::prepareRender(sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}

void Terrain::buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut)
{
    GenericDrawCommand cmd = {};
    cmd._sourceBuffer = _terrainBuffer->handle();
    cmd._cmd.indexCount = to_U32(TessellationParams::QUAD_LIST_INDEX_COUNT);
    for (const auto& tileRing : _tileRings) {
        cmd._cmd.instanceCount += tileRing->tileCount();
    }

    cmdsOut.emplace_back(GFX::DrawCommand{ cmd });

    Object3D::buildDrawCommands(sgn, cmdsOut);
}

const vector<VertexBuffer::Vertex>& Terrain::getVerts() const noexcept {
    return _physicsVerts;
}

Terrain::Vert Terrain::getVertFromGlobal(F32 x, F32 z, const bool smooth) const {
    x -= _boundingBox.getCenter().x;
    z -= _boundingBox.getCenter().z;
    const vec2<U16>& dim = _descriptor->dimensions();
    const F32 xClamp = (0.5f * dim.width + x) / dim.width;
    const F32 zClamp = (0.5f * dim.height - z) / dim.height;
    return getVert(xClamp, 1 - zClamp, smooth);
}

Terrain::Vert Terrain::getVert(const F32 x_clampf, const F32 z_clampf, const bool smooth) const {
    return smooth ? getSmoothVert(x_clampf, z_clampf)
                  : getVert(x_clampf, z_clampf);
}

Terrain::Vert Terrain::getSmoothVert(const F32 x_clampf, const F32 z_clampf) const {
    assert(!(x_clampf < 0.0f || z_clampf < 0.0f || 
             x_clampf > 1.0f || z_clampf > 1.0f));

    const vec2<U16>& dim   = _descriptor->dimensions();
    const vec3<F32>& bbMin = _boundingBox.getMin();
    const vec3<F32>& bbMax = _boundingBox.getMax();

    const vec2<F32> posF(x_clampf * dim.width,    z_clampf * dim.height);
          vec2<I32> posI(to_I32(posF.width),      to_I32(posF.height));
    const vec2<F32> posD(posF.width - posI.width, posF.height - posI.height);

    if (posI.width >= to_I32(dim.width) - 1) {
        posI.width = dim.width - 2;
    }

    if (posI.height >= to_I32(dim.height) - 1) {
        posI.height = dim.height - 2;
    }

    assert(posI.width  >= 0 && posI.width  < to_I32(dim.width)  - 1 &&
           posI.height >= 0 && posI.height < to_I32(dim.height) - 1);

    const VertexBuffer::Vertex& tempVert1 = _physicsVerts[TER_COORD(posI.width,     posI.height,     to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert2 = _physicsVerts[TER_COORD(posI.width + 1, posI.height,     to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert3 = _physicsVerts[TER_COORD(posI.width,     posI.height + 1, to_I32(dim.width))];
    const VertexBuffer::Vertex& tempVert4 = _physicsVerts[TER_COORD(posI.width + 1, posI.height + 1, to_I32(dim.width))];

    const vec3<F32> normals[4]{
        Util::UNPACK_VEC3(tempVert1._normal),
        Util::UNPACK_VEC3(tempVert2._normal),
        Util::UNPACK_VEC3(tempVert3._normal),
        Util::UNPACK_VEC3(tempVert4._normal)
    };

    const vec3<F32> tangents[4]{
        Util::UNPACK_VEC3(tempVert1._tangent),
        Util::UNPACK_VEC3(tempVert2._tangent),
        Util::UNPACK_VEC3(tempVert3._tangent),
        Util::UNPACK_VEC3(tempVert4._tangent)
    };

    Vert ret = {};
    ret._position.set(
        // X
        bbMin.x + x_clampf * (bbMax.x - bbMin.x),

        //Y
        tempVert1._position.y * (1.0f - posD.width) * (1.0f - posD.height) +
        tempVert2._position.y *         posD.width  * (1.0f - posD.height) +
        tempVert3._position.y * (1.0f - posD.width) *         posD.height +
        tempVert4._position.y *         posD.width  *         posD.height,

        //Z
        bbMin.z + z_clampf * (bbMax.z - bbMin.z));

    ret._normal.set(normals[0] * (1.0f - posD.width) * (1.0f - posD.height) +
                    normals[1] *         posD.width  * (1.0f - posD.height) +
                    normals[2] * (1.0f - posD.width) *         posD.height +
                    normals[3] *         posD.width  *         posD.height);

    ret._tangent.set(tangents[0] * (1.0f - posD.width) * (1.0f - posD.height) +
                     tangents[1] *         posD.width  * (1.0f - posD.height) +
                     tangents[2] * (1.0f - posD.width) *         posD.height +
                     tangents[3] *         posD.width  *         posD.height);

    ret._normal.normalize();
    ret._tangent.normalize();
    return ret;

}

Terrain::Vert Terrain::getVert(const F32 x_clampf, const F32 z_clampf) const {
    assert(!(x_clampf < 0.0f || z_clampf < 0.0f ||
             x_clampf > 1.0f || z_clampf > 1.0f));

    const vec2<U16>& dim = _descriptor->dimensions();
    
    vec2<I32> posI(to_I32(x_clampf * dim.width), 
                   to_I32(z_clampf * dim.height));

    if (posI.width >= to_I32(dim.width) - 1) {
        posI.width = dim.width - 2;
    }

    if (posI.height >= to_I32(dim.height) - 1) {
        posI.height = dim.height - 2;
    }

    assert(posI.width  >= 0 && posI.width  < to_I32(dim.width)  - 1 &&
           posI.height >= 0 && posI.height < to_I32(dim.height) - 1);

    const VertexBuffer::Vertex& tempVert1 = _physicsVerts[TER_COORD(posI.width, posI.height, to_I32(dim.width))];

    Vert ret = {};
    ret._position.set(tempVert1._position);
    ret._normal.set(Util::UNPACK_VEC3(tempVert1._normal));
    ret._tangent.set(Util::UNPACK_VEC3(tempVert1._tangent));

    return ret;

}

vec2<U16> Terrain::getDimensions() const noexcept {
    return _descriptor->dimensions();
}

vec2<F32> Terrain::getAltitudeRange() const noexcept {
    return _descriptor->altitudeRange();
}

void Terrain::getVegetationStats(U32& maxGrassInstances, U32& maxTreeInstances) const {
    U32 grassInstanceCount = 0u, treeInstanceCount = 0u;
    for (const TerrainChunk* chunk : _terrainChunks) {
        Attorney::TerrainChunkTerrain::getVegetation(*chunk)->getStats(grassInstanceCount, treeInstanceCount);
        maxGrassInstances = std::max(maxGrassInstances, grassInstanceCount);
        maxTreeInstances = std::max(maxTreeInstances, treeInstanceCount);
    }
}

void Terrain::saveToXML(boost::property_tree::ptree& pt) const {
    pt.put("descriptor", _descriptor->getVariable("descriptor"));
    pt.put("waterCaustics", _descriptor->getVariable("waterCaustics"));
    pt.put("underwaterAlbedoTexture", _descriptor->getVariable("underwaterAlbedoTexture"));
    pt.put("underwaterDetailTexture", _descriptor->getVariable("underwaterDetailTexture"));
    pt.put("underwaterTileScale", _descriptor->getVariablef("underwaterTileScale"));
    pt.put("tileNoiseTexture", _descriptor->getVariable("tileNoiseTexture"));
    Object3D::saveToXML(pt);
}

void Terrain::loadFromXML(const boost::property_tree::ptree& pt) {

    Object3D::loadFromXML(pt);
}

}