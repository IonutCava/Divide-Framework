#include "stdafx.h"

#include "Headers/Mesh.h"
#include "Headers/SubMesh.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/StringHelper.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "Managers/Headers/SceneManager.h"

namespace Divide {

Mesh::Mesh(GFXDevice& context,
           ResourceCache* parentCache,
           const size_t descriptorHash,
           const Str256& name,
           const ResourcePath& resourceName,
           const ResourcePath& resourceLocation)
    : Object3D(context, parentCache, descriptorHash, name, resourceName, resourceLocation, ObjectType::MESH, Object3D::ObjectFlag::OBJECT_FLAG_NONE),
      _animator(nullptr)
{
    setBounds(_boundingBox);
}

void Mesh::addSubMesh(const SubMesh_ptr& subMesh) {
    // Hold a reference to the SubMesh by ID
    _subMeshList.emplace_back(MeshData{subMesh, subMesh->id() });
    Attorney::SubMeshMesh::setParentMesh(*subMesh, this);
}

void Mesh::setNodeData(const MeshNodeData& nodeStructure) {
    _nodeStructure = nodeStructure;
}

void Mesh::setMaterialTpl(const Material_ptr& material) {
    Object3D::setMaterialTpl(material);

    for (const Mesh::MeshData& subMesh : _subMeshList) {
        if (material != nullptr) {
            subMesh._mesh->setMaterialTpl(material->clone(subMesh._mesh->getMaterialTpl()->resourceName()));
        }
    }
}

SceneGraphNode* Mesh::addSubMeshNode(SceneGraphNode* parentNode, const U32 meshIndex) {
    constexpr U32 normalMask = to_base(ComponentType::NAVIGATION) |
                               to_base(ComponentType::TRANSFORM) |
                               to_base(ComponentType::BOUNDS) |
                               to_base(ComponentType::RENDERING) |
                               to_base(ComponentType::RIGID_BODY) |
                               to_base(ComponentType::NAVIGATION);

    constexpr U32 skinnedMask = to_base(ComponentType::ANIMATION) |
                                to_base(ComponentType::INVERSE_KINEMATICS) |
                                to_base(ComponentType::RAGDOLL);

    DIVIDE_ASSERT(meshIndex < _subMeshList.size());

    const MeshData& meshData = _subMeshList[meshIndex];
    DIVIDE_ASSERT(meshData._index == meshIndex);
    const SubMesh_ptr& subMesh = meshData._mesh;
    
    SceneGraphNodeDescriptor subMeshDescriptor;
    subMeshDescriptor._usageContext = parentNode->usageContext();
    subMeshDescriptor._instanceCount = parentNode->instanceCount();
    subMeshDescriptor._node = subMesh;
    subMeshDescriptor._componentMask = normalMask;

    if (subMesh->getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED)) {
        assert(getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED));
        subMeshDescriptor._componentMask |= skinnedMask;
    }

    subMeshDescriptor._name = Util::StringFormat("%s_%d", parentNode->name().c_str(), meshIndex);

    SceneGraphNode* sgn = parentNode->addChildNode(subMeshDescriptor);
    sgn->get<TransformComponent>()->setPosition(subMesh->getWorldOffset());

    return sgn;
}

void Mesh::processNode(SceneGraphNode* parentNode, const MeshNodeData& node) {
    for (const U32 idx : node._meshIndices) {
        addSubMeshNode(parentNode, idx);
    }

    if (!node._children.empty()) {
        SceneGraphNodeDescriptor tempNodeDescriptor;
        tempNodeDescriptor._usageContext = parentNode->usageContext();
        tempNodeDescriptor._instanceCount = parentNode->instanceCount();
        tempNodeDescriptor._name = node._name;
        tempNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                            to_base(ComponentType::NETWORKING);
        for (const MeshNodeData& it : node._children) {
            SceneGraphNode* targetSGN = parentNode;
            if (!it._transform.isIdentity()) {
                targetSGN = parentNode->addChildNode(tempNodeDescriptor);
                targetSGN->get<TransformComponent>()->setTransforms(it._transform);
            }
            processNode(targetSGN, it);
        }
    }
}

/// After we loaded our mesh, we need to add submeshes as children nodes
void Mesh::postLoad(SceneGraphNode* sgn) {
    sgn->get<TransformComponent>()->setTransforms(_nodeStructure._transform);
    processNode(sgn, _nodeStructure);
    if (_animator) {
        Attorney::SceneAnimatorMeshImporter::buildBuffers(*_animator, _context);
    }
    Object3D::postLoad(sgn);
}

bool MeshNodeData::serialize(ByteBuffer& dataOut) const {
    dataOut << _transform;
    dataOut << _meshIndices;
    dataOut << to_U32(_children.size());
    for (const MeshNodeData& child : _children) {
        child.serialize(dataOut);
    }
    dataOut << _name;
    return true;
}

bool MeshNodeData::deserialize(ByteBuffer& dataIn) {
    dataIn >> _transform;
    dataIn >> _meshIndices;
    U32 childCount = 0u;
    dataIn >> childCount;
    _children.resize(childCount);
    for (MeshNodeData& child : _children) {
        child.deserialize(dataIn);
    }
    dataIn >> _name;
    return true;
}

}; //namespace Divide