

#include "Headers/Mesh.h"
#include "Headers/SubMesh.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/StringHelper.h"
#include "Managers/Headers/ProjectManager.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Importer/Headers/MeshImporter.h"

namespace Divide {

Mesh::Mesh( const ResourceDescriptor<Mesh>& descriptor )
    : Object3D(descriptor, GetSceneNodeType<Mesh>() )
{
    setBounds(_boundingBox);
}

void Mesh::addSubMesh(const Handle<SubMesh> subMesh, const U32 index)
{
    _subMeshList.emplace_back(MeshData{ subMesh, index });
}

void Mesh::setNodeData(const MeshNodeData& nodeStructure)
{
    _nodeStructure = nodeStructure;
}

void Mesh::setMaterialTpl( const Handle<Material> material)
{
    Object3D::setMaterialTpl(material);

    for (const Mesh::MeshData& subMesh : _subMeshList)
    {
        Get(subMesh._mesh)->setMaterialTpl(material);
    }
}

void Mesh::setAnimationCount( const size_t animationCount, const bool useDualQuaternions)
{
    _animationCount = animationCount;
    if ( _animationCount == 0 && _animator != nullptr)
    {
        _animator.reset();
    }
    else if ( _animationCount > 0u && _animator == nullptr)
    {
        _animator = std::make_unique<SceneAnimator>(useDualQuaternions);
    }
}

SceneAnimator* Mesh::getAnimator() const noexcept
{
    return _animator.get();
}

SceneGraphNode* Mesh::addSubMeshNode(SceneGraphNode* rootMeshNode, SceneGraphNode* parentNode, const U32 meshIndex)
{
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
    SubMesh* subMesh = Get( meshData._mesh );
    
    SceneGraphNodeDescriptor subMeshDescriptor;
    subMeshDescriptor._usageContext = rootMeshNode->usageContext();
    subMeshDescriptor._instanceCount = rootMeshNode->instanceCount();
    subMeshDescriptor._nodeHandle = FromHandle(meshData._mesh);
    subMeshDescriptor._componentMask = normalMask;

    if (rootMeshNode->HasComponents(ComponentType::SELECTION))
    {
        subMeshDescriptor._componentMask |= to_base(ComponentType::SELECTION);
    }

    if ( subMesh->boneCount() > 0u )
    {
        subMeshDescriptor._componentMask |= skinnedMask;
    }

    Util::StringFormat( subMeshDescriptor._name, "{}_{}", parentNode->name().c_str(), meshIndex );

    SceneGraphNode* sgn = parentNode->addChildNode(subMeshDescriptor);
    sgn->get<TransformComponent>()->setPosition(subMesh->getWorldOffset());

    return sgn;
}

void Mesh::processNode(SceneGraphNode* rootMeshNode, SceneGraphNode* parentNode, const MeshNodeData& node)
{
    for (const U32 idx : node._meshIndices)
    {
        addSubMeshNode(rootMeshNode, parentNode, idx);
    }

    if (!node._children.empty())
    {
        const Handle<TransformNode> transformHandle = CreateResource(ResourceDescriptor<TransformNode>{ node._name + "_transform" });

        SceneGraphNodeDescriptor tempNodeDescriptor
        {
            ._name = node._name.c_str(),
            ._instanceCount = parentNode->instanceCount(),
            ._nodeHandle = FromHandle(transformHandle),
            ._componentMask = to_base(ComponentType::TRANSFORM) |
                              to_base(ComponentType::NETWORKING),
            ._usageContext = parentNode->usageContext()
        };

        for (const MeshNodeData& it : node._children)
        {
            SceneGraphNode* targetSGN = parentNode;
            targetSGN = parentNode->addChildNode(tempNodeDescriptor);
            targetSGN->get<TransformComponent>()->setTransforms(it._transform);

            processNode(rootMeshNode, targetSGN, it);
        }
    }
}

/// After we loaded our mesh, we need to add submeshes as children nodes
void Mesh::postLoad(SceneGraphNode* sgn)
{
    sgn->get<TransformComponent>()->setTransforms(_nodeStructure._transform);
    processNode(sgn, sgn, _nodeStructure);

    if (_animator)
    {
        PlatformContext& pContext = sgn->context();

        Attorney::SceneAnimatorMeshImporter::buildBuffers(*_animator, pContext.gfx() );

        registerEditorComponent( pContext );
        DIVIDE_ASSERT( _editorComponent != nullptr );

        EditorComponentField playAnimationsField = {};
        playAnimationsField._name = "Toogle Animation Playback";
        playAnimationsField._data = &_playAnimationsOverride;
        playAnimationsField._type = EditorComponentFieldType::SWITCH_TYPE;
        playAnimationsField._basicType = PushConstantType::BOOL;
        playAnimationsField._readOnly = false;
        _editorComponent->registerField( MOV( playAnimationsField ) );
    }

    Object3D::postLoad(sgn);
}

bool Mesh::postLoad()
{
    return Object3D::postLoad();
}

bool Mesh::load( PlatformContext& context )
{
    if ( MeshImporter::loadMesh( context, this ) )
    {
        return Object3D::load(context);
    }

    return false;
}

bool Mesh::unload()
{
    for ( Mesh::MeshData& subMesh : _subMeshList )
    {
        DestroyResource(subMesh._mesh);
    }
    _subMeshList.clear();
    return Object3D::unload();
}

bool MeshNodeData::serialize(ByteBuffer& dataOut) const {
    dataOut << _transform;
    dataOut << _meshIndices;
    dataOut << to_U32(_children.size());
    for (const MeshNodeData& child : _children)
    {
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
    for (MeshNodeData& child : _children)
    {
        child.deserialize(dataIn);
    }
    dataIn >> _name;
    return true;
}

}; //namespace Divide
