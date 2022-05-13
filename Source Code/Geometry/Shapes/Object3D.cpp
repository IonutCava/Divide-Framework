#include "stdafx.h"

#include "Headers/Object3D.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Physics/Headers/PXDevice.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {

namespace TypeUtil {
    const char* ObjectTypeToString(const ObjectType objectType) noexcept {
        return Names::objectType[to_base(objectType)];
    }

    ObjectType StringToObjectType(const string& name) {
        for (U8 i = 0u; i < to_U8(ObjectType::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::objectType[i]) == 0) {
                return static_cast<ObjectType>(i);
            }
        }

        return ObjectType::COUNT;
    }
}; //namespace TypeUtil

Object3D::Object3D(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, const ObjectType type, const U32 flagMask)
    : SceneNode(parentCache,
                descriptorHash,
                name,
                resourceName,
                resourceLocation,
                SceneNodeType::TYPE_OBJECT3D,
                to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS) | to_base(ComponentType::RENDERING)),
    _context(context),
    _geometryPartitionIDs{},
    _geometryFlagMask(flagMask),
    _geometryType(type)
{
    _editorComponent.name(getTypeName().c_str());

    _geometryPartitionIDs.fill(VertexBuffer::INVALID_PARTITION_ID);
    _geometryPartitionIDs[0] = 0u;

    if (!getObjectFlag(ObjectFlag::OBJECT_FLAG_NO_VB)) {
        _geometryBuffer = context.newVB();
    }

    if (getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED)) {
        EditorComponentField playAnimationsField = {};
        playAnimationsField._name = "Toogle Animation Playback";
        playAnimationsField._data = &_playAnimationsOverride;
        playAnimationsField._type = EditorComponentFieldType::SWITCH_TYPE;
        playAnimationsField._basicType = GFX::PushConstantType::BOOL;
        playAnimationsField._readOnly = false;
        _editorComponent.registerField(MOV(playAnimationsField));
    }
}

Object3D::Object3D(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, const ObjectType type, const ObjectFlag flag)
    : Object3D(context, parentCache, descriptorHash, name, resourceName, resourceLocation, type, to_U32(flag))
{
}

void Object3D::editorFieldChanged(const std::string_view field) {
    if (field == "Toogle Animation Playback") {
        DebugBreak();
    }
}

void Object3D::rebuildInternal() {
    NOP();
}

const VertexBuffer_ptr& Object3D::geometryBuffer() {
    if (geometryDirty()) {
        geometryDirty(false);
        rebuildInternal();
    }

    return _geometryBuffer;
}

void Object3D::setMaterialTpl(const Material_ptr& material) {
    SceneNode::setMaterialTpl(material);

    if (_materialTemplate != nullptr && geometryBuffer() != nullptr) {
        _materialTemplate->setPipelineLayout(GetGeometryBufferType(geometryType()), geometryBuffer()->generateAttributeMap());
    }
}

void Object3D::buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut) {
    OPTICK_EVENT();

    if (geometryBuffer() != nullptr) {
        if (cmdsOut.size() == 0u) {
            const U16 partitionID = _geometryPartitionIDs[0];
            GenericDrawCommand cmd;
            cmd._sourceBuffer = geometryBuffer()->handle();
            cmd._cmd.indexCount = to_U32(geometryBuffer()->getPartitionIndexCount(partitionID));
            cmd._cmd.firstIndex = to_U32(geometryBuffer()->getPartitionOffset(partitionID));
            cmd._cmd.primCount = sgn->instanceCount();
            
            cmdsOut.emplace_back(GFX::DrawCommand{ cmd });
        }
        
        U16 prevID = 0;
        RenderingComponent* rComp = sgn->get<RenderingComponent>();
        assert(rComp != nullptr);

        for (U8 i = 0; i < to_U8(_geometryPartitionIDs.size()); ++i) {
            U16 id = _geometryPartitionIDs[i];
            if (id == U16_MAX) {
                assert(i > 0);
                id = prevID;
            }
            rComp->setLoDIndexOffset(i, geometryBuffer()->getPartitionOffset(id), geometryBuffer()->getPartitionIndexCount(id));
            prevID = id;
        }
    }

    SceneNode::buildDrawCommands(sgn, cmdsOut);
}

// Create a list of triangles from the vertices + indices lists based on primitive type
bool Object3D::computeTriangleList(const U16 partitionID, const bool force) {
    if (getObjectFlag(ObjectFlag::OBJECT_FLAG_NO_VB)) {
        return true;
    }

    auto& geometry = geometryBuffer();

    DIVIDE_ASSERT(geometry != nullptr,
        "Object3D error: Please specify a valid VertexBuffer before "
        "calculating the triangle list!");
    // We can't have a VB without vertex positions
    DIVIDE_ASSERT(!geometry->getVertices().empty(),
        "Object3D error: computeTriangleList called with no position "
        "data available!");

    if(partitionID >= _geometryTriangles.size()) {
        _geometryTriangles.resize(partitionID + 1);
    }

    vector<vec3<U32>>& triangles = _geometryTriangles[partitionID];
    if (!force && !triangles.empty()) {
        return true;
    }

    if (!triangles.empty()) {
        triangles.resize(0);
    }

    const size_t partitionOffset = geometry->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = geometry->getPartitionIndexCount(_geometryPartitionIDs[0]);
    const PrimitiveTopology type = GetGeometryBufferType(geometryType());

    if (geometry->getIndexCount() == 0) {
        return false;
    }

    size_t indiceCount = partitionCount;
    if (type == PrimitiveTopology::TRIANGLE_STRIP) {
        const size_t indiceStart = 2 + partitionOffset;
        const size_t indiceEnd = indiceCount + partitionOffset;
        vec3<U32> curTriangle;
        triangles.reserve(indiceCount / 2);
        const vector<U32>& indices = geometry->getIndices();
        for (size_t i = indiceStart; i < indiceEnd; i++) {
            curTriangle.set(indices[i - 2], indices[i - 1], indices[i]);
            // Check for correct winding
            if (i % 2 != 0) {
                std::swap(curTriangle.y, curTriangle.z);
            }
            triangles.push_back(curTriangle);
        }
    } else if (type == PrimitiveTopology::TRIANGLES) {
        indiceCount /= 3;
        triangles.reserve(indiceCount);
        const vector<U32>& indices = geometry->getIndices();
        for (size_t i = 0; i < indiceCount; i += 3) {
            triangles.push_back(vec3<U32>(indices[i + 0],
                                          indices[i + 1],
                                          indices[i + 2]));
        }
    }

    // Check for degenerate triangles
    triangles.erase(
        eastl::partition(
            begin(triangles), end(triangles),
            [](const vec3<U32>& triangle) -> bool {
                return triangle.x != triangle.y && triangle.x != triangle.z &&
                    triangle.y != triangle.z;
            }),
        end(triangles));

    DIVIDE_ASSERT(!triangles.empty(), "Object3D error: computeTriangleList() failed to generate any triangles!");
    return true;
}

vector<SceneGraphNode*> Object3D::filterByType(const vector<SceneGraphNode*>& nodes, const ObjectType filter) {
    vector<SceneGraphNode*> result;
    result.reserve(nodes.size());

    for (SceneGraphNode* ptr : nodes) {
        if (ptr && ptr->getNode<Object3D>().geometryType() == filter) {
            result.push_back(ptr);
        }
    };

    return result;
}

bool Object3D::saveCache(ByteBuffer& outputBuffer) const {
    if (SceneNode::saveCache(outputBuffer)) {
        if (IsPrimitive(geometryType())) {
            outputBuffer << to_U8(geometryType());
        } else {
            outputBuffer << string(resourceName().c_str());
        }
        return true;
    }

    return false;
};

bool Object3D::loadCache(ByteBuffer& inputBuffer) {
    if (SceneNode::loadCache(inputBuffer)) {
        if (IsPrimitive(geometryType())) {
            U8 index = 0u;
            inputBuffer >> index;
            if (index != to_U8(geometryType())) {
                return false;
            }
        } else {
            string tempName = {};
            inputBuffer >> tempName;
            assert(tempName == resourceName().c_str());
        }

        return true;
    }

    return false;
}

void Object3D::saveToXML(boost::property_tree::ptree& pt) const {
    if (IsPrimitive(geometryType())) {
        pt.put("model", TypeUtil::ObjectTypeToString(geometryType()));
    } else {
        pt.put("model", resourceName().c_str());
    }

    SceneNode::saveToXML(pt);
}

void Object3D::loadFromXML(const boost::property_tree::ptree& pt) {
    string temp;
    if (IsPrimitive(geometryType())) {
        temp = pt.get("model", TypeUtil::ObjectTypeToString(geometryType()));
        assert(temp == TypeUtil::ObjectTypeToString(geometryType()));
    } else {
        temp = pt.get("model", resourceName().c_str());
        assert(temp == resourceName().c_str());
    }

    SceneNode::loadFromXML(pt);
}

};
