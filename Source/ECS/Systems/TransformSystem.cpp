

#include "Headers/TransformSystem.h"

#include "Graphs/Headers/SceneGraphNode.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    namespace
    {
        constexpr U32 g_parallelPartitionSize = 256;
    }

    TransformSystem::TransformSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context)
        , ECSSystem(parentEngine)
    {
    }

    void TransformSystem::PreUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);

        for ( TransformComponent* comp : _componentCache )
        {
            // If we have dirty transforms, inform everybody
            const U32 updateMask = comp->_transformUpdatedMask.load();
            if (updateMask != to_base(TransformType::NONE))
            {
                Attorney::SceneGraphNodeSystem::setTransformDirty(comp->parentSGN(), updateMask);
                comp->_local._computed = comp->_world._computed = false;
            }
        }
    }

    void TransformSystem::Update(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        static vector<TransformComponent*> dirtyComponents;

        Parent::Update(dt);

        dirtyComponents.reserve(_componentCache.size());

        for (TransformComponent* comp : _componentCache)
        {
            // Cleanup our dirty transforms
            const U32 previousMask = comp->_transformUpdatedMask.exchange(to_U32(TransformType::NONE));
            if (previousMask != to_U32(TransformType::NONE))
            {
                dirtyComponents.emplace_back(comp);
                comp->_broadcastMask = previousMask;
            }
        }

        Parallel_For( _context.taskPool( TaskPoolType::HIGH_PRIORITY ),
                      ParallelForDescriptor
                      {
                          ._iterCount = to_U32(dirtyComponents.size()),
                          ._partitionSize = g_parallelPartitionSize
                      },
                      [](const Task*, const U32 start, const U32 end)
                      {
                          for (U32 i = start; i < end; ++i)
                          {
                              TransformComponent* comp = dirtyComponents[i];
                              comp->_local._previousValues = comp->_local._values;

                              {
                                  LockGuard<SharedMutex> w_lock(comp->_lock);
                                  comp->_local._values = comp->_transformInterface;
                              }

                              comp->_local._matrix = mat4<F32>
                              {
                                  comp->_local._values._translation,
                                  comp->_local._values._scale,
                                  comp->_local._values._orientation.getConjugate()
                              };

                              comp->_local._computed = true;
                          }
                      });

        dirtyComponents.clear();
    }

    void TransformSystem::PostUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PostUpdate(dt);

        for (TransformComponent* comp : _componentCache)
        {
            computeWorldMatrix(comp);
        }

        for (TransformComponent* comp : _componentCache)
        {
            if ( comp->_broadcastMask != 0u )
            {
                comp->parentSGN()->SendEvent(
                    ECS::CustomEvent
                    {
                          ._type = ECS::CustomEvent::Type::TransformUpdated,
                          ._sourceCmp = comp,
                          ._flag = comp->_broadcastMask
                    }
                );


                comp->_broadcastMask = 0u;
            }
        }
    }

    void TransformSystem::computeWorldMatrix(TransformComponent* comp) const
    {
        if ( comp->_world._computed )
        {
            return;
        }

        const bool useLocalRotations = comp->rotationMode() == TransformComponent::RotationMode::LOCAL;

        comp->_world._matrix         = comp->_local._matrix;
        comp->_world._previousValues = comp->_world._values;
        comp->_world._values         = comp->_local._values;

        SceneGraphNode* grandParent = comp->_parentSGN->parent();
        if ( grandParent != nullptr ) 
        {
            TransformComponent* tComp = grandParent->get<TransformComponent>();
            if ( tComp != nullptr )
            {
                computeWorldMatrix(tComp);

                comp->_world._values._orientation = tComp->_world._values._orientation * comp->_world._values._orientation;
                comp->_world._values._scale       = tComp->_world._values._scale       * comp->_world._values._scale;

                const float3 worldPosition = useLocalRotations ? comp->_world._values._translation
                                                               : tComp->_world._values._orientation * (tComp->_world._values._scale * comp->_world._values._translation);

                comp->_world._values._translation = tComp->_world._values._translation + worldPosition;
            }
        }

        comp->_world._matrix = mat4<F32>
        {
            comp->_world._values._translation,
            comp->_world._values._scale,
            comp->_world._values._orientation.getConjugate()
        };

        comp->_world._computed = true;
    }

    bool TransformSystem::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer)
    {
        if (Parent::saveCache(sgn, outputBuffer))
        {
            const TransformComponent* tComp = sgn->GetComponent<TransformComponent>();
            if (tComp != nullptr && !tComp->saveCache(outputBuffer))
            {
                return false;
            }

            return true;
        }

        return false;
    }

    bool TransformSystem::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer)
    {
        if (Parent::loadCache(sgn, inputBuffer))
        {
            TransformComponent* tComp = sgn->GetComponent<TransformComponent>();
            if (tComp != nullptr && !tComp->loadCache(inputBuffer))
            {
                return false;
            }

            return true;
        }

        return false;
    }
} //namespace Divide
