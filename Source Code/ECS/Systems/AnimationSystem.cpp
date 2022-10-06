#include "stdafx.h"

#include "Headers/AnimationSystem.h"

#include "Geometry/Animations/Headers/SceneAnimator.h"

namespace Divide {
    AnimationSystem::AnimationSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    AnimationSystem::~AnimationSystem()
    {
    }

    void AnimationSystem::PreUpdate(const F32 dt) {
        PROFILE_SCOPE();

        Parent::PreUpdate(dt);

        for (AnimationComponent* comp : _componentCache) {
            if (comp->_currentAnimIndex == -1) {
                comp->playAnimation(0);
            }

            comp->_parentTimeStamp += comp->playAnimations() ? dt * comp->animationSpeed() : 0.0;
        }
    }

    void AnimationSystem::Update(const F32 dt) {
        PROFILE_SCOPE();

        Parent::Update(dt);

        for (AnimationComponent* comp : _componentCache) {
            const SceneAnimator_ptr animator = comp->animator();

            if (!animator || comp->_parentTimeStamp == comp->_currentTimeStamp) {
                return;
            }

            comp->_currentTimeStamp = comp->_parentTimeStamp;
            const D64 timeStampS = Time::MillisecondsToSeconds<D64>(comp->_currentTimeStamp);

            if (comp->playAnimations()) {
                // Update Animations
                comp->_frameIndex = animator->frameIndexForTimeStamp(comp->_currentAnimIndex, timeStampS);

                if (comp->_currentAnimIndex != comp->_previousAnimationIndex && comp->_currentAnimIndex >= 0) {
                    comp->_previousAnimationIndex = comp->_currentAnimIndex;
                }
            }

            // Resolve IK
            //if (comp->_resolveIK) {
                /// Use CCD to move target joints to target positions
            //}

            // Resolve ragdoll
            // if (comp->_resolveRagdoll) {
                /// Use PhysX actor from RigidBodyComponent to feed new bone positions/orientation
                /// And read back ragdoll results to update transforms accordingly
            //}
        }
    }

    void AnimationSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
        for (AnimationComponent* const comp : _componentCache) {
            if (comp->frameTicked()) {
                comp->parentSGN()->SendEvent(
                    ECS::CustomEvent{
                         ECS::CustomEvent::Type::AnimationUpdated,
                         comp
                    }
                );
            }
        }
    }

    void AnimationSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void AnimationSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool AnimationSystem::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) {
        if (Parent::saveCache(sgn, outputBuffer)) {
            const AnimationComponent* aComp = sgn->GetComponent<AnimationComponent>();
            if (aComp != nullptr && !aComp->saveCache(outputBuffer)) {
                return false;
            }
            return true;
        }

        return false;
    }

    bool AnimationSystem::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) {
        if (Parent::loadCache(sgn, inputBuffer)) {
            AnimationComponent* aComp = sgn->GetComponent<AnimationComponent>();
            if (aComp != nullptr && !aComp->loadCache(inputBuffer)) {
                return false;
            }
            return true;
        }

        return false;
    }

    void AnimationSystem::toggleAnimationState(const bool state) noexcept {
        AnimationComponent::GlobalAnimationState(state);
    }

    bool AnimationSystem::getAnimationState() const noexcept {
        return AnimationComponent::GlobalAnimationState();
    }
}//namespace Divide