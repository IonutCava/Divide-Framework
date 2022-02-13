#include "stdafx.h"

#include "Headers/SkinnedSubMesh.h"
#include "Headers/Mesh.h"

#include "Core/Headers/EngineTaskPool.h"
#include "Managers/Headers/SceneManager.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "ECS/Components/Headers/AnimationComponent.h"

namespace Divide {

SkinnedSubMesh::SkinnedSubMesh(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : SubMesh(context, parentCache, descriptorHash, name),
     _parentAnimatorPtr(nullptr)
{
    setObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED);
}

/// After we loaded our mesh, we need to add submeshes as children nodes
void SkinnedSubMesh::postLoad(SceneGraphNode* sgn) {
    if (_parentAnimatorPtr == nullptr) {
        _parentAnimatorPtr = _parentMesh->getAnimator();

        const size_t animationCount = _parentAnimatorPtr->animations().size();
        _boundingBoxesState.resize(animationCount, { BoundingBoxState::COUNT });
        _boundingBoxes.resize(animationCount);
    }

    sgn->get<AnimationComponent>()->animator(_parentAnimatorPtr);
    SubMesh::postLoad(sgn);
}

/// update possible animations
void SkinnedSubMesh::onAnimationChange(SceneGraphNode* sgn, const I32 newIndex) {
    computeBBForAnimation(sgn, newIndex);

    Object3D::onAnimationChange(sgn, newIndex);
}

void SkinnedSubMesh::buildBoundingBoxesForAnim([[maybe_unused]] const Task& parentTask,
                                               const I32 animationIndex,
                                               const AnimationComponent* const animComp) {
    if (animationIndex < 0) {
        return;
    }

    const vector<BoneTransform>& currentAnimation = animComp->getAnimationByIndex(animationIndex).transforms();

    VertexBuffer* parentVB = _parentMesh->getGeometryVB();
    const size_t partitionOffset = parentVB->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = parentVB->getPartitionIndexCount(_geometryPartitionIDs[0]);

    ScopedLock<SharedMutex> w_lock(_bbLock);
    BoundingBox& currentBB = _boundingBoxes.at(animationIndex);
    currentBB.reset();
    for (const BoneTransform& transforms : currentAnimation) {
        const BoneTransform::Container& matrices = transforms.matrices();
        // loop through all vertex weights of all bones
        for (U32 j = 0; j < partitionCount; ++j) {
            const U32 idx = parentVB->getIndex(j + partitionOffset);
            const P32 ind = parentVB->getBoneIndices(idx);
            const vec4<F32>& wgh = parentVB->getBoneWeights(idx);
            const vec3<F32>& curentVert = parentVB->getPosition(idx);

            currentBB.add(wgh.x * (matrices[ind.b[0]] * curentVert) +
                          wgh.y * (matrices[ind.b[1]] * curentVert) +
                          wgh.z * (matrices[ind.b[2]] * curentVert) +
                          wgh.w * (matrices[ind.b[3]] * curentVert));
        }
    }
}

void SkinnedSubMesh::updateBB(const I32 animIndex) {
    SharedLock<SharedMutex> r_lock(_bbLock);
    setBounds(_boundingBoxes[animIndex]);
}

void SkinnedSubMesh::computeBBForAnimation(SceneGraphNode* const sgn, const I32 animIndex) {
    // Attempt to get the map of BBs for the current animation
    ScopedLock<SharedMutex> w_lock(_bbStateLock);
    const BoundingBoxState state = _boundingBoxesState[animIndex];

    if (state != BoundingBoxState::COUNT) {
        if (state == BoundingBoxState::Computed) {
            updateBB(animIndex);
        }
        return;
    }

    _boundingBoxesState[animIndex] = BoundingBoxState::Computing;
    AnimationComponent* animComp = sgn->get<AnimationComponent>();

    Task* computeBBTask = CreateTask([this, animIndex, animComp](const Task& parentTask) {
                                         buildBoundingBoxesForAnim(parentTask, animIndex, animComp);
                                     });

    Start(*computeBBTask,
          _context.context().taskPool(TaskPoolType::HIGH_PRIORITY),
          TaskPriority::DONT_CARE,
          [this, animIndex, animComp]() {
              ScopedLock<SharedMutex> w_lock2(_bbStateLock);
              _boundingBoxesState[animIndex] = BoundingBoxState::Computed;
              // We could've changed the animation while waiting for this task to end
              if (animComp->animationIndex() == animIndex) {
                  updateBB(animIndex);
              }
          });
}

};