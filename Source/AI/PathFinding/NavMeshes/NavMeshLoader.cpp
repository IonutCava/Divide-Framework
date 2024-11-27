

#include "Headers/NavMeshLoader.h"

#include "Core/Headers/ByteBuffer.h"
#include "Utility/Headers/Localization.h"
#include "Managers/Headers/ProjectManager.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Water/Headers/Water.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/NavigationComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide::AI::Navigation::NavigationMeshLoader {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    constexpr U32 g_cubeFaces[6][4] = {{0, 4, 6, 2},
                                       {0, 2, 3, 1},
                                       {0, 1, 5, 4},
                                       {3, 2, 6, 7},
                                       {7, 6, 4, 5},
                                       {3, 7, 5, 1}};

char* ParseRow(char* buf, const char* const bufEnd, char* row, const I32 len) noexcept {
    bool start = true;
    bool done = false;
    I32 n = 0;

    while (!done && buf < bufEnd) {
        const char c = *buf;
        buf++;
        // multi row
        switch (c) {
            case '\\':
                break;  // multi row
            case '\n': {
                if (start) {
                    break;
                }
                done = true;
            } break;
            case '\r':
                break;
            case '\t':
            case ' ':
                if (start) {
                    break;
                }
                [[fallthrough]];
            default: {
                start = false;
                row[n++] = c;
                if (n >= len - 1) {
                    done = true;
                }
            } break;
        }
    }

    row[n] = '\0';
    return buf;
}

I32 ParseFace(char* row, I32* data, const I32 n, const I32 vcnt) noexcept {
    I32 j = 0;
    while (*row != '\0') {
        // Skip initial white space
        while (*row != '\0' && (*row == ' ' || *row == '\t')) {
            row++;
        }

        const char* s = row;
        // Find vertex delimiter and terminated the string there for conversion.
        while (*row != '\0' && *row != ' ' && *row != '\t') {
            if (*row == '/') {
                *row = '\0';
            }
            row++;
        }

        if (*s == '\0') {
            continue;
        }

        const I32 vi = atoi(s);
        data[j++] = vi < 0 ? vi + vcnt : vi - 1;
        if (j >= n) {
            return j;
        }
    }
    return j;
}

bool LoadMeshFile(NavModelData& outData, const ResourcePath& filePath, const char* fileName) {
    STUBBED("ToDo: Rework load/save to properly use a ByteBuffer instead of this const char* hackery. -Ionut");

    ByteBuffer tempBuffer;
    if (!tempBuffer.loadFromFile(filePath, fileName))
    {
        return false;
    }

    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    tempBuffer >> tempVer;
    if (tempVer != BYTE_BUFFER_VERSION) {
        return false;
    }

    char* buf = new char[tempBuffer.storageSize()];
    std::memcpy(buf, reinterpret_cast<const char*>(tempBuffer.contents()), tempBuffer.storageSize());
    char* srcEnd = buf + tempBuffer.storageSize();

    char* src = buf;

    char row[512] = {};
    I32 face[32] = {};
    F32 x, y, z;
    while (src < srcEnd) {
        // Parse one row
        row[0] = '\0';
        src = ParseRow(src, srcEnd, row, sizeof row / sizeof(char));
        // Skip comments
        if (row[0] == '#')
            continue;

        if (row[0] == 'v' && row[1] != 'n' && row[1] != 't') {
            // Vertex pos
            const I32 result = sscanf(row + 1, "%f %f %f", &x, &y, &z);
            if (result != 0)
                AddVertex(&outData, float3(x, y, z));
        }
        if (row[0] == 'f') {
            // Faces
            const I32 nv = ParseFace(row + 1, face, 32, outData._vertexCount);
            for (I32 i = 2; i < nv; ++i) {
                const I32 a = face[0];
                const I32 b = face[i - 1];
                const I32 c = face[i];
                if (a < 0 || a >= to_I32(outData._vertexCount) || b < 0 ||
                    b >= to_I32(outData._vertexCount) || c < 0 ||
                    c >= to_I32(outData._vertexCount)) {
                    continue;
                }

                AddTriangle(&outData, uint3(a, b, c));
            }
        }
    }

    delete[] buf;

    // Calculate normals.
    outData._normals.resize(outData._triangleCount * 3);

    for (I32 i = 0; i < to_I32(outData._triangleCount) * 3; i += 3) {
        const F32* v0 = &outData._vertices[outData._triangles[i] * 3];
        const F32* v1 = &outData._vertices[outData._triangles[i + 1] * 3];
        const F32* v2 = &outData._vertices[outData._triangles[i + 2] * 3];

        F32 e0[3] = {}, e1[3] = {};
        for (I32 j = 0; j < 3; ++j) {
            e0[j] = v1[j] - v0[j];
            e1[j] = v2[j] - v0[j];
        }

        F32* n = &outData._normals[i];
        n[0] = e0[1] * e1[2] - e0[2] * e1[1];
        n[1] = e0[2] * e1[0] - e0[0] * e1[2];
        n[2] = e0[0] * e1[1] - e0[1] * e1[0];
        F32 d = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (d > 0) {
            d = 1.0f / d;
            n[0] *= d;
            n[1] *= d;
            n[2] *= d;
        }
    }

    return true;
}

bool SaveMeshFile(const NavModelData& inData, const ResourcePath& filePath, const char* filename) {
    if (!inData.getVertCount() || !inData.getTriCount())
    {
        return false;
    }

    ByteBuffer tempBuffer;
    tempBuffer << BYTE_BUFFER_VERSION;

    const F32* vStart = inData._vertices.data();
    const I32* tStart = inData._triangles.data();
    for (U32 i = 0; i < inData.getVertCount(); i++) {
        const F32* vp = vStart + i * 3;
        tempBuffer << "v " << *vp << " " << *(vp + 1) << " " << *(vp + 2) << "\n";
    }
    for (U32 i = 0; i < inData.getTriCount(); i++) {
        const I32* tp = tStart + i * 3;
        tempBuffer << "f " << *tp + 1 << " " << *(tp + 1) + 1 << " " << *(tp + 2) + 1 << "\n";
    }

    return tempBuffer.dumpToFile(filePath, filename);
}

NavModelData MergeModels(NavModelData& a,
                         NavModelData& b,
                         const bool delOriginals /* = false*/) {
    NavModelData mergedData;
    if (a.getVerts() || b.getVerts()) {
        if (!a.getVerts()) {
            return b;
        }

        if (!b.getVerts()) {
            return a;
        }

        mergedData.clear();

        const I32 totalVertCt = a.getVertCount() + b.getVertCount();
        I32 newCap = 8;

        while (newCap < totalVertCt) {
            newCap *= 2;
        }

        mergedData._vertices.resize(newCap * 3);
        mergedData._vertexCapacity = newCap;
        mergedData._vertexCount = totalVertCt;

        memcpy(mergedData._vertices.data(), a.getVerts(), a.getVertCount() * 3 * sizeof(F32));
        memcpy(mergedData._vertices.data() + a.getVertCount() * 3, b.getVerts(), b.getVertCount() * 3 * sizeof(F32));

        const I32 totalTriCt = a.getTriCount() + b.getTriCount();
        newCap = 8;

        while (newCap < totalTriCt) {
            newCap *= 2;
        }

        mergedData._triangles.resize(newCap * 3);
        mergedData._triangleCapacity = newCap;
        mergedData._triangleCount = totalTriCt;
        const I32 aFaceSize = a.getTriCount() * 3;
        memcpy(mergedData._triangles.data(), a.getTris(), aFaceSize * sizeof(I32));

        const I32 bFaceSize = b.getTriCount() * 3;
        I32* bFacePt = mergedData._triangles.data() + a.getTriCount() * 3;  // i like pointing at faces
        memcpy(bFacePt, b.getTris(), bFaceSize * sizeof(I32));

        for (U32 i = 0; i < to_U32(bFaceSize); i++) {
            *(bFacePt + i) += a.getVertCount();
        }

        if (mergedData._vertexCount > 0) {
            if (delOriginals) {
                a.clear();
                b.clear();
            }
        } else {
            mergedData.clear();
        }
    }

    mergedData.name(Util::StringFormat("{}+{}", a.name(), b.name() ).c_str());
    return mergedData;
}

void AddVertex(NavModelData* modelData, const float3& vertex) {
    assert(modelData != nullptr);

    if (modelData->getVertCount() + 1 > modelData->_vertexCapacity)
    {
        modelData->_vertexCapacity = !modelData->_vertexCapacity ? 8 : modelData->_vertexCapacity * 2;

        modelData->_vertices.resize(modelData->_vertexCapacity * 3);
    }

    F32* dst = &modelData->_vertices[modelData->getVertCount() * 3];
    *dst++ = vertex.x;
    *dst++ = vertex.y;
    *dst++ = vertex.z;

    modelData->_vertexCount++;
}

void AddTriangle(NavModelData* modelData,
                 const uint3& triangleIndices,
                 const U32 triangleIndexOffset,
                 const SamplePolyAreas& areaType) {
    if (modelData->getTriCount() + 1 > modelData->_triangleCapacity)
    {
        modelData->_triangleCapacity = !modelData->_triangleCapacity ? 8 : modelData->_triangleCapacity * 2;

        modelData->_triangles.resize(modelData->_triangleCapacity * 3);
    }

    I32* dst = &modelData->_triangles[modelData->getTriCount() * 3];

    *dst++ = to_I32(triangleIndices.x + triangleIndexOffset);
    *dst++ = to_I32(triangleIndices.y + triangleIndexOffset);
    *dst++ = to_I32(triangleIndices.z + triangleIndexOffset);

    modelData->getAreaTypes().push_back(areaType);
    modelData->_triangleCount++;
}

const float3 g_borderOffset(BORDER_PADDING);
bool Parse(const BoundingBox& box, NavModelData& outData, SceneGraphNode* sgn)
{
    assert(sgn != nullptr);

    const NavigationComponent* navComp = sgn->get<NavigationComponent>();
    if (navComp && 
        navComp->navigationContext() != NavigationComponent::NavigationContext::NODE_IGNORE &&  // Ignore if specified
        box.getHeight() > 0.05f)  // Skip small objects
    {
        const SceneNodeType nodeType = sgn->getNode().type();
        const char* resourceName = sgn->getNode().resourceName().c_str();

        if (nodeType != SceneNodeType::TYPE_WATER && !Is3DObject(nodeType))
        {
            Console::printfn(LOCALE_STR("WARN_NAV_UNSUPPORTED"), resourceName);
            goto next;
        }

        if (nodeType == SceneNodeType::TYPE_MESH)
        {
            // Even though we allow Object3Ds, we do not parse MESH nodes, instead we grab its children so we get an accurate triangle list per node
            goto next;
        }

        MeshDetailLevel level = MeshDetailLevel::MAXIMUM;
        SamplePolyAreas areaType = SamplePolyAreas::SAMPLE_AREA_OBSTACLE;
        if ( Is3DObject(nodeType))
        {
            // Check if we need to override detail level
            if ( navComp && !navComp->navMeshDetailOverride() && sgn->usageContext() == NodeUsageContext::NODE_STATIC )
            {
                level = MeshDetailLevel::BOUNDINGBOX;
            }
            if ( nodeType == SceneNodeType::TYPE_TERRAIN )
            {
                areaType = SamplePolyAreas::SAMPLE_POLYAREA_GROUND;
            }
        }
        else if ( nodeType == SceneNodeType::TYPE_WATER)
        {
            if (navComp && !navComp->navMeshDetailOverride()) {
                level = MeshDetailLevel::BOUNDINGBOX;
            }
            areaType = SamplePolyAreas::SAMPLE_POLYAREA_WATER;
        }
        else
        {
            // we should never reach this due to the bit checks above
            DIVIDE_UNEXPECTED_CALL();
        }

        Console::d_printfn(LOCALE_STR("NAV_MESH_CURRENT_NODE"), resourceName, to_base(level));

        const U32 currentTriangleIndexOffset = outData.getVertCount();
        VertexBuffer* geometry = nullptr;
        if (level == MeshDetailLevel::MAXIMUM)
        {
            Object3D* obj = nullptr;
            if (Is3DObject(nodeType))
            {
                obj = &sgn->getNode<Object3D>();
            }
            else if (nodeType == SceneNodeType::TYPE_WATER)
            {
                obj = Get(sgn->getNode<WaterPlane>().getQuad());
            }
            assert(obj != nullptr);
            geometry = obj->geometryBuffer().get();
            assert(geometry != nullptr);

            const auto& vertices = geometry->getVertices();
            if (vertices.empty())
            {
                Console::printfn(LOCALE_STR("NAV_MESH_NODE_NO_DATA"), resourceName);
                goto next;
            }

            const auto& triangles = obj->getTriangles(obj->getGeometryPartitionID(0u));
            if (triangles.empty())
            {
                Console::printfn(LOCALE_STR("NAV_MESH_NODE_NO_DATA"), resourceName);
                goto next;
            }

            mat4<F32> nodeTransform = MAT4_IDENTITY;
            sgn->get<TransformComponent>()->getWorldMatrix(nodeTransform);

            for (const VertexBuffer::Vertex& vert : vertices)
            {
                // Apply the node's transform and add the vertex to the NavMesh
                AddVertex(&outData, nodeTransform * vert._position);
            }

            for (const uint3& triangle : triangles)
            {
                AddTriangle(&outData, triangle, currentTriangleIndexOffset, areaType);
            }
        }
        else if (level == MeshDetailLevel::BOUNDINGBOX)
        {
            std::array<float3, 8> vertices = box.getPoints();

            for (U32 i = 0; i < 8; i++)
            {
                AddVertex(&outData, vertices[i]);
            }

            for (U32 f = 0; f < 6; f++)
            {
                for (U32 v = 2; v < 4; v++)
                {
                    // Note: We reverse the normal on the polygons to prevent things from going inside out
                    AddTriangle(&outData,
                                uint3(g_cubeFaces[f][0], g_cubeFaces[f][v - 1],
                                          g_cubeFaces[f][v]),
                                currentTriangleIndexOffset, areaType);
                }
            }
        }
        else
        {
            Console::errorfn(LOCALE_STR("ERROR_RECAST_LEVEL"), to_base(level));
        }

        Console::printfn(LOCALE_STR("NAV_MESH_ADD_NODE"), resourceName);
    }

next: // although labels are bad, skipping here using them is the easiest solution to follow -Ionut
    const SceneGraphNode::ChildContainer& children = sgn->getChildren();
    SharedLock<SharedMutex> r_lock(children._lock);

    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i)
    {
        SceneGraphNode* child = children._data[i];
        if (!Parse(child->get<BoundsComponent>()->getBoundingBox(), outData, child))
        {
            return false;
        }
    }

    return true;
}

}  // namespace Divide::AI::Navigation::NavigationMeshLoader
