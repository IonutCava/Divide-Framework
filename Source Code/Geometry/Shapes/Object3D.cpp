#include "stdafx.h"

#include "Headers/Object3D.h"

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
    _geometryType(type),
    _rigidBodyShape(RigidBodyShape::SHAPE_COUNT)
{
    _editorComponent.name(getTypeName().c_str());
    _geometryPartitionIDs.fill(VertexBuffer::INVALID_PARTITION_ID);
    _geometryPartitionIDs[0] = 0u;

    if (!getObjectFlag(ObjectFlag::OBJECT_FLAG_NO_VB)) {
        _buffer = context.newVB();
    }

    switch (type) {
        case ObjectType::BOX_3D :
            _rigidBodyShape = RigidBodyShape::SHAPE_BOX;
            break;
        case ObjectType::QUAD_3D :
            _rigidBodyShape = RigidBodyShape::SHAPE_PLANE;
            break;
        case ObjectType::SPHERE_3D :
            _rigidBodyShape = RigidBodyShape::SHAPE_SPHERE;
            break;
        case ObjectType::TERRAIN :
            _rigidBodyShape = RigidBodyShape::SHAPE_HEIGHTFIELD;
            break;
        case ObjectType::MESH: {
            STUBBED("ToDo: Add capsule and convex mesh support for 3D Objects! -Ionut");
            if_constexpr (true) { // general meshes? Maybe have a concave flag?
                _rigidBodyShape = RigidBodyShape::SHAPE_TRIANGLEMESH;
            } else { 
                if_constexpr(true) { // skinned characters?
                    _rigidBodyShape = RigidBodyShape::SHAPE_CAPSULE;
                } else { // have a convex flag for imported meshes?
                    _rigidBodyShape = RigidBodyShape::SHAPE_CONVEXMESH;
                }
            }
            } break;
        default:
            _rigidBodyShape = RigidBodyShape::SHAPE_COUNT;
            break;
    };
}

PrimitiveTopology Object3D::getGeometryBufferType() const noexcept {
    switch (_geometryType) {
        case ObjectType::BOX_3D:
        case ObjectType::MESH:
        case ObjectType::SUBMESH: return PrimitiveTopology::TRIANGLES;
    }

    return PrimitiveTopology::TRIANGLE_STRIP;
}

bool Object3D::isPrimitive() const noexcept {
    return _geometryType == ObjectType::BOX_3D ||
           _geometryType == ObjectType::QUAD_3D ||
           _geometryType == ObjectType::PATCH_3D ||
           _geometryType == ObjectType::SPHERE_3D;
}

void Object3D::postLoad(SceneGraphNode* sgn) {
    if (geometryDirty()) {
        rebuildInternal();
        geometryDirty(false);
    }

    SceneNode::postLoad(sgn);
}

void Object3D::setGeometryVB(VertexBuffer* const vb) {
    DIVIDE_ASSERT(_buffer == nullptr,
                  "Object3D error: Please remove the previous vertex buffer of "
                  "this Object3D before specifying a new one!");
    _buffer = vb;
}

VertexBuffer* Object3D::getGeometryVB() const noexcept {
    return _buffer;
}

void Object3D::rebuildInternal() {
    if (!computeTriangleList(0)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void Object3D::prepareRender(SceneGraphNode* sgn,
                             RenderingComponent& rComp,
                             const RenderStagePass renderStagePass,
                             const CameraSnapshot& cameraSnapshot,
                             const bool refreshData) {
    if (refreshData && geometryDirty()) {
        OPTICK_EVENT();

        rebuildInternal();
        geometryDirty(false);
    }

    SceneNode::prepareRender(sgn, rComp, renderStagePass, cameraSnapshot, refreshData);
}

void Object3D::buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut, PrimitiveTopology& topologyOut, AttributeMap& vertexFormatInOut) {
    VertexBuffer* vb = getGeometryVB();
    if (vb != nullptr) {
        if (cmdsOut.size() == 0u) {
            topologyOut = getGeometryBufferType();
            vb->populateAttributeMap(vertexFormatInOut);

            const U16 partitionID = _geometryPartitionIDs[0];
            GenericDrawCommand cmd;
            cmd._sourceBuffer = vb->handle();
            cmd._bufferIndex = GenericDrawCommand::INVALID_BUFFER_INDEX;
            cmd._cmd.indexCount = to_U32(vb->getPartitionIndexCount(partitionID));
            cmd._cmd.firstIndex = to_U32(vb->getPartitionOffset(partitionID));
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
            rComp->setLoDIndexOffset(i, vb->getPartitionOffset(id), vb->getPartitionIndexCount(id));
            prevID = id;
        }
    }

    SceneNode::buildDrawCommands(sgn, cmdsOut, topologyOut, vertexFormatInOut);
}

// Create a list of triangles from the vertices + indices lists based on primitive type
bool Object3D::computeTriangleList(const U16 partitionID, const bool force) {
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


    VertexBuffer* geometry = getGeometryVB();

    DIVIDE_ASSERT(geometry != nullptr,
                  "Object3D error: Please specify a valid VertexBuffer before "
                  "calculating the triangle list!");
    // We can't have a VB without vertex positions
    DIVIDE_ASSERT(!geometry->getVertices().empty(),
                  "Object3D error: computeTriangleList called with no position "
                  "data available!");

    const size_t partitionOffset = geometry->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = geometry->getPartitionIndexCount(_geometryPartitionIDs[0]);
    const PrimitiveTopology type = getGeometryBufferType();

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
        if (ptr && ptr->getNode<Object3D>().getObjectType() == filter) {
            result.push_back(ptr);
        }
    };

    return result;
}

void Object3D::playAnimations(SceneGraphNode* sgn, const bool state) {
    if (getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED)) {
        AnimationComponent* animComp = sgn->get<AnimationComponent>();
        if (animComp != nullptr) {
            animComp->playAnimations(state);
        }
        const SceneGraphNode::ChildContainer& children = sgn->getChildren();
        SharedLock<SharedMutex> w_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            AnimationComponent* animCompInner = children._data[i]->get<AnimationComponent>();
            // Not all submeshes are necessarily animated. (e.g. flag on the back of a character)
            if (animCompInner != nullptr) {
                animCompInner->playAnimations(state && animCompInner->playAnimations());
            }
        }
    }
}

bool Object3D::saveCache(ByteBuffer& outputBuffer) const {
    if (SceneNode::saveCache(outputBuffer)) {
        if (isPrimitive()) {
            outputBuffer << to_U8(getObjectType());
        } else {
            outputBuffer << string(resourceName().c_str());
        }
        return true;
    }

    return false;
};

bool Object3D::loadCache(ByteBuffer& inputBuffer) {
    if (SceneNode::loadCache(inputBuffer)) {
        if (isPrimitive()) {
            U8 index = 0u;
            inputBuffer >> index;
            if (index != to_U8(getObjectType())) {
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
    if (isPrimitive()) {
        pt.put("model", TypeUtil::ObjectTypeToString(static_cast<const Object3D*>(this)->getObjectType()));
    } else {
        pt.put("model", resourceName().c_str());
    }

    SceneNode::saveToXML(pt);
}

void Object3D::loadFromXML(const boost::property_tree::ptree& pt) {
    string temp;
    if (isPrimitive()) {
        temp = pt.get("model", TypeUtil::ObjectTypeToString(getObjectType()));
        assert(temp == TypeUtil::ObjectTypeToString(getObjectType()));
    } else {
        temp = pt.get("model", resourceName().c_str());
        assert(temp == resourceName().c_str());
    }

    SceneNode::loadFromXML(pt);
}

};
