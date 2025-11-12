

#include "Headers/SubMesh.h"
#include "Headers/Mesh.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"

#include "Core/Resources/Headers/ResourceCache.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "ECS/Components/Headers/AnimationComponent.h"

namespace Divide {

SubMesh::SubMesh( const ResourceDescriptor<SubMesh>& descriptor )
    : Object3D( descriptor, GetSceneNodeType<SubMesh>() )
    , _id( descriptor.data().y )
    , _boneCount( to_U8( std::min<U32>( descriptor.data().x, U8_MAX ) ) )
{
}

/// After we loaded our mesh, we need to add submeshes as children nodes
void SubMesh::postLoad(SceneGraphNode* sgn)
{
    if ( _boneCount > 0u )
    {
        const size_t animationCount = _parentMesh->animationCount();
        _boundingBoxesState.resize(animationCount, { BoundingBoxState::COUNT });
        _boundingBoxes.resize(animationCount);

        sgn->get<AnimationComponent>()->setAnimator(_parentMesh->getAnimator());
    }

    Object3D::postLoad(sgn);
}

VertexBuffer* SubMesh::geometryBuffer()
{
    return _parentMesh->geometryBuffer();
}

void ChangeAnimationForAllChildren(SceneGraphNode* parentNode, const U32 animationIndex, const bool playInReverse)
{
    if (parentNode != nullptr)
    {
        const SceneGraphNode::ChildContainer& children = parentNode->getChildren();
        SharedLock<SharedMutex> r_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            SceneGraphNode* child = children._data[i];
            if (child->HasComponents(ComponentType::ANIMATION))
            {
                auto* aComp = child->get<AnimationComponent>();

                const bool applyToChildren = aComp->applyAnimationChangeToAllMeshes();
                aComp->applyAnimationChangeToAllMeshes(false);
                aComp->playInReverse(playInReverse);
                aComp->playAnimation(animationIndex);
                aComp->applyAnimationChangeToAllMeshes(applyToChildren);
            }

            ChangeAnimationForAllChildren(child, animationIndex, playInReverse);
        }
    }
}

void SyncAnimationForAllChildren(SceneGraphNode* parentNode, const D64 parentTimeStamp, const bool playInReverse)
{
    if (parentNode != nullptr)
    {
        const SceneGraphNode::ChildContainer& children = parentNode->getChildren();
        SharedLock<SharedMutex> r_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            SceneGraphNode* child = children._data[i];
            if (child->HasComponents(ComponentType::ANIMATION))
            {
                auto* aComp = child->get<AnimationComponent>();
                aComp->resetTimers(parentTimeStamp);
            }

            SyncAnimationForAllChildren(child, parentTimeStamp, playInReverse);
        }
    }
}

/// update possible animations
void SubMesh::onAnimationChange(SceneGraphNode* sgn, const U32 newIndex, const bool applyToAllSiblings, const bool playInReverse)
{
    computeBBForAnimation(sgn, newIndex);

    if ( applyToAllSiblings )
    {
        SceneGraphNode* parentSGN = sgn->parent();
        while (parentSGN && parentSGN->node()->type() != SceneNodeType::TYPE_MESH)
        {
            parentSGN = parentSGN->parent();
        }

        ChangeAnimationForAllChildren(parentSGN, newIndex, playInReverse);
    }
}

void SubMesh::onAnimationSync(SceneGraphNode* sgn, [[maybe_unused]] const U32 animIndex, const bool playInReverse)
{
    auto aComp = sgn->get<AnimationComponent>();

    const D64 currentTimeStamp = aComp->parentTimeStamp();

    SceneGraphNode* parentSGN = sgn->parent();
    while (parentSGN && parentSGN->node()->type() != SceneNodeType::TYPE_MESH)
    {
        parentSGN = parentSGN->parent();
    }

    SyncAnimationForAllChildren(parentSGN, currentTimeStamp, playInReverse);

    aComp->resetTimers(currentTimeStamp);
}

void SubMesh::buildBoundingBoxesForAnim([[maybe_unused]] const Task& parentTask, const U32 animationIndex, const AnimationComponent* const animComp)
{
    if (animationIndex == U32_MAX)
    {
        return;
    }

    const vector<BoneMatrices>& currentAnimation = animComp->getAnimationByIndex(animationIndex).transformMatrices();

    VertexBuffer* parentVB = _parentMesh->geometryBuffer();
    const size_t partitionOffset = parentVB->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = parentVB->getPartitionIndexCount(_geometryPartitionIDs[0]);

    LockGuard<SharedMutex> w_lock(_bbLock);
    BoundingBox& currentBB = _boundingBoxes.at(animationIndex);
    currentBB.reset();

    for (const BoneMatrices& matrices : currentAnimation)
    {
        // loop through all vertex weights of all bones
        for (U32 j = 0u; j < partitionCount; ++j)
        {
            const U32 idx = parentVB->getIndex(j + partitionOffset);
            const VertexBuffer::Vertex& vertex = parentVB->getVertices()[idx];

            const float3&  pos = vertex._position;
            const vec4<U8> ind = vertex._indices;
            const vec4<U8> wgh = vertex._weights;

            currentBB.add((UNORM_CHAR_TO_FLOAT(wgh.x) * (matrices[ind.x] * pos)) +
                          (UNORM_CHAR_TO_FLOAT(wgh.y) * (matrices[ind.y] * pos)) +
                          (UNORM_CHAR_TO_FLOAT(wgh.z) * (matrices[ind.z] * pos)) +
                          (UNORM_CHAR_TO_FLOAT(wgh.w) * (matrices[ind.w] * pos)) );
        }
    }
}

void SubMesh::updateBB(const U32 animIndex)
{
    SharedLock<SharedMutex> r_lock(_bbLock);
    setBounds(_boundingBoxes[animIndex]);
}

void SubMesh::computeBBForAnimation(SceneGraphNode* const sgn, const U32 animIndex)
{
    // Attempt to get the map of BBs for the current animation
    LockGuard<SharedMutex> w_lock(_bbStateLock);
    const BoundingBoxState state = _boundingBoxesState[animIndex];

    if (state != BoundingBoxState::COUNT)
    {
        if (state == BoundingBoxState::Computed)
        {
            updateBB(animIndex);
        }

        return;
    }

    _boundingBoxesState[animIndex] = BoundingBoxState::Computing;
    AnimationComponent* animComp = sgn->get<AnimationComponent>();

    Task* computeBBTask = CreateTask([this, animIndex, animComp](const Task& parentTask)
    {
        buildBoundingBoxesForAnim(parentTask, animIndex, animComp);
    });

    sgn->context().taskPool(TaskPoolType::HIGH_PRIORITY).enqueue(
        *computeBBTask,
        TaskPriority::DONT_CARE,
        [this, animIndex, animComp]()
        {
            LockGuard<SharedMutex> w_lock2(_bbStateLock);
            _boundingBoxesState[animIndex] = BoundingBoxState::Computed;
            // We could've changed the animation while waiting for this task to end
            if (animComp->animationIndex() == animIndex)
            {
                updateBB(animIndex);
            }
        });
}

} //namespace Divide
