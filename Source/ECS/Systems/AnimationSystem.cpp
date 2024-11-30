

#include "Headers/AnimationSystem.h"

#include "Geometry/Animations/Headers/SceneAnimator.h"

namespace Divide {
    AnimationSystem::AnimationSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    void AnimationSystem::PreUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);

        for (AnimationComponent* comp : _componentCache)
        {
            if (comp->animationIndex() == U32_MAX)
            {
                comp->playAnimation(0);
            }

            comp->_parentTimeStamp += comp->playAnimations() ? dt * comp->animationSpeed() : 0.0;
        }
    }

    void AnimationSystem::Update(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::Update(dt);

        for (AnimationComponent* comp : _componentCache)
        {
            const SceneAnimator* animator = comp->animator();

            if (!animator || COMPARE(comp->_parentTimeStamp, comp->_currentTimeStamp))
            {
                return;
            }

            comp->_currentTimeStamp = comp->_parentTimeStamp;
            const D64 timeStampSec = Time::MillisecondsToSeconds<D64>(comp->_currentTimeStamp);

            if (comp->playAnimations())
            {
                // Update Animations
                comp->_frameIndex = animator->frameIndexForTimeStamp(comp->animationIndex(), timeStampSec, !comp->playInReverse());

                if (comp->animationIndex() != comp->previousAnimationIndex() && comp->animationIndex() != U32_MAX)
                {
                    comp->previousAnimationIndex(comp->animationIndex());
                }
            }

            // Resolve IK
            //if (comp->_resolveIK)
            //{
                /// Use CCD to move target joints to target positions
            //}

            // Resolve ragdoll
            // if (comp->_resolveRagdoll)
            //{
                /// Use a physics actor from RigidBodyComponent to feed new bone positions/orientation
                /// And read back ragdoll results to update transforms accordingly
            //}
        }
    }

    void AnimationSystem::PostUpdate(const F32 dt)
    {
        Parent::PostUpdate(dt);
        for (AnimationComponent* const comp : _componentCache)
        {
            if (comp->frameTicked())
            {
                comp->parentSGN()->SendEvent(
                    ECS::CustomEvent
                    {
                        ._type = ECS::CustomEvent::Type::AnimationUpdated,
                        ._sourceCmp = comp
                    }
                );
            }

            if ( comp->_animationStateChanged )
            {
                comp->parentSGN()->SendEvent(
                    ECS::CustomEvent
                    {
                        ._type = ECS::CustomEvent::Type::AnimationChanged,
                        ._sourceCmp = comp,
                        ._flag = comp->_animationIndex,
                        ._dataPair = 
                        {
                            ._first  = to_U16(comp->_applyAnimationChangeToAllMeshes ? 1u : 0u),
                            ._second = to_U16(comp->_playInReverse ? 1u : 0u)
                        }
                    }
                );
                comp->_animationStateChanged = false;
            }
            else if ( comp->_resyncAllSiblings )
            {
                comp->parentSGN()->SendEvent(
                    ECS::CustomEvent
                    {
                        ._type = ECS::CustomEvent::Type::AnimationReSync,
                        ._sourceCmp = comp,
                        ._flag = comp->_animationIndex,
                        ._data = comp->_playInReverse ? 1u : 0u
                    }
                );
                comp->_resyncAllSiblings = false;
            }
        }
    }

    bool AnimationSystem::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer)
    {
        if (Parent::saveCache(sgn, outputBuffer))
        {
            const AnimationComponent* aComp = sgn->GetComponent<AnimationComponent>();
            if (aComp != nullptr && !aComp->saveCache(outputBuffer))
            {
                return false;
            }

            return true;
        }

        return false;
    }

    bool AnimationSystem::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer)
    {
        if (Parent::loadCache(sgn, inputBuffer))
        {
            AnimationComponent* aComp = sgn->GetComponent<AnimationComponent>();
            if (aComp != nullptr && !aComp->loadCache(inputBuffer))
            {
                return false;
            }
            return true;
        }

        return false;
    }

    void AnimationSystem::toggleAnimationState(const bool state) noexcept
    {
        AnimationComponent::GlobalAnimationState(state);
    }

    bool AnimationSystem::getAnimationState() const noexcept
    {
        return AnimationComponent::GlobalAnimationState();
    }

}//namespace Divide
