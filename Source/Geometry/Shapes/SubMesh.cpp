

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

SubMesh::SubMesh( PlatformContext& context, const ResourceDescriptor<SubMesh>& descriptor )
    : Object3D(context, descriptor, GetSceneNodeType<SubMesh>() )
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

        sgn->get<AnimationComponent>()->animator(_parentMesh->getAnimator());
    }

    Object3D::postLoad(sgn);
}

bool SubMesh::postLoad()
{
    return Object3D::postLoad();
}
/// update possible animations
void SubMesh::onAnimationChange(SceneGraphNode* sgn, const U32 newIndex)
{
    computeBBForAnimation(sgn, newIndex);

    Object3D::onAnimationChange(sgn, newIndex);
}

void SubMesh::buildBoundingBoxesForAnim([[maybe_unused]] const Task& parentTask, const U32 animationIndex, const AnimationComponent* const animComp)
{
    if (animationIndex == U32_MAX)
    {
        return;
    }

    const vector<BoneTransform>& currentAnimation = animComp->getAnimationByIndex(animationIndex).transforms();

    auto& parentVB = _parentMesh->geometryBuffer();
    const size_t partitionOffset = parentVB->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = parentVB->getPartitionIndexCount(_geometryPartitionIDs[0]);

    LockGuard<SharedMutex> w_lock(_bbLock);
    BoundingBox& currentBB = _boundingBoxes.at(animationIndex);
    currentBB.reset();

    for (const BoneTransform& transforms : currentAnimation)
    {
        const BoneTransform::Container& matrices = transforms.matrices();
        // loop through all vertex weights of all bones
        for (U32 j = 0u; j < partitionCount; ++j)
        {
            const U32 idx = parentVB->getIndex(j + partitionOffset);
            const vec4<U8> ind = parentVB->getBoneIndices(idx);
            const vec4<F32>& wgh = parentVB->getBoneWeights(idx);
            const vec3<F32>& curentVert = parentVB->getPosition(idx);

            currentBB.add((wgh.x * (matrices[ind.x] * curentVert)) +
                          (wgh.y * (matrices[ind.y] * curentVert)) +
                          (wgh.z * (matrices[ind.z] * curentVert)) +
                          (wgh.w * (matrices[ind.w] * curentVert)) );
        }
    }
}

void SubMesh::updateBB(const U32 animIndex)
{
    SharedLock<SharedMutex> r_lock(_bbLock);
    setBounds(_boundingBoxes[animIndex], _worldOffset);
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

    Start(*computeBBTask,
        _context.context().taskPool(TaskPoolType::HIGH_PRIORITY),
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
