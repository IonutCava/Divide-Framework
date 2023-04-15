#include "stdafx.h"

#include "Headers/TransformSystem.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    namespace {
        constexpr U32 g_parallelPartitionSize = 32;
    }

    TransformSystem::TransformSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context)
        , ECSSystem(parentEngine)
    {
    }

    TransformSystem::~TransformSystem()
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
                comp->resetCache();
            }
        }
    }

    void TransformSystem::Update(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        static vector<std::pair<TransformComponent*, U32>> events;

        Parent::Update(dt);


        for (TransformComponent* comp : _componentCache)
        {
            // Cleanup our dirty transforms
            const U32 previousMask = comp->_transformUpdatedMask.exchange(to_U32(TransformType::NONE));
            if (previousMask != to_U32(TransformType::NONE))
            {
                events.emplace_back(comp, previousMask);
            }
        }

        const D64 interpFactor = GFXDevice::FrameInterpolationFactor();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = to_U32(events.size());
        if (descriptor._iterCount > g_parallelPartitionSize * 3)
        {
            descriptor._partitionSize = g_parallelPartitionSize;
            descriptor._cbk = [this, interpFactor](const Task*, const U32 start, const U32 end)
            {
                for (U32 i = start; i < end; ++i)
                {
                    events[i].first->updateLocalMatrix( interpFactor );
                }
            };
            parallel_for(_context, descriptor);
        }
        else
        {
            for (U32 i = 0u; i < descriptor._iterCount; ++i)
            {
                events[i].first->updateLocalMatrix( interpFactor );
            }
        }

        for (const auto& [comp, mask] : events)
        {
            comp->parentSGN()->SendEvent(
                ECS::CustomEvent
                {
                      ECS::CustomEvent::Type::TransformUpdated,
                      comp,
                      mask
                }
            );
        }

        efficient_clear( events );
    }

    void TransformSystem::PostUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PostUpdate(dt);

        for (TransformComponent* comp : _componentCache)
        {
            comp->updateCachedValues();
        }
    }

    void TransformSystem::OnFrameStart()
    {
        Parent::OnFrameStart();
    }

    void TransformSystem::OnFrameEnd()
    {
        Parent::OnFrameEnd();
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
