#include "stdafx.h"

#include "Headers/TransformSystem.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {
    namespace {
        constexpr U32 g_parallelPartitionSize = 32;
    }

    TransformSystem::TransformSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    TransformSystem::~TransformSystem()
    {
    }

    void TransformSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);
        for (TransformComponent* comp : _componentCache) {
            // If we have dirty transforms, inform everybody
            const U32 updateMask = comp->_transformUpdatedMask.load();
            if (updateMask != to_base(TransformType::NONE)) {
                Attorney::SceneGraphNodeSystem::setTransformDirty(comp->getSGN(), updateMask);
                comp->resetInterpolation();
            }
        }
    }

    void TransformSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        static vector<std::pair<TransformComponent*, U32>> events;

        Parent::Update(dt);

        for (TransformComponent* comp : _componentCache) {
            // Cleanup our dirty transforms
            const U32 previousMask = comp->_transformUpdatedMask.exchange(to_U32(TransformType::NONE));
            if (previousMask != to_U32(TransformType::NONE)) {
                events.emplace_back(comp, previousMask);
            }
        }

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = to_U32(events.size());
        descriptor._partitionSize = g_parallelPartitionSize;
        descriptor._cbk = [this](const Task*, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                TransformComponent* tComp = events[i].first;
                tComp->updateWorldMatrix();
            }
        };

        parallel_for(_context, descriptor);


        for (const auto& [comp, mask] : events) {
            comp->getSGN()->SendEvent(
                ECS::CustomEvent{
                      ECS::CustomEvent::Type::TransformUpdated,
                      comp,
                      mask
                });
        }
        events.resize(0);
    }

    void TransformSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void TransformSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void TransformSystem::OnFrameEnd() {
        OPTICK_EVENT();

        Parent::OnFrameEnd();
        for (TransformComponent* comp : _componentCache) {
            if (comp->_prevWorldMatrixDirty) {
                // Just memcpy the 2
                mat4<F32>& dest = comp->_worldMatrix[to_base(TransformComponent::WorldMatrixType::PREVIOUS)];
                const mat4<F32>& src = comp->_worldMatrix[to_base(TransformComponent::WorldMatrixType::CURRENT)];
                dest.set(src);
                comp->_prevWorldMatrixDirty = false;
            }
        }
    }

    bool TransformSystem::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) {
        if (Parent::saveCache(sgn, outputBuffer)) {
            TransformComponent* tComp = sgn->GetComponent<TransformComponent>();
            if (tComp != nullptr && !tComp->saveCache(outputBuffer)) {
                return false;
            }

            return true;
        }

        return false;
    }

    bool TransformSystem::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) {
        if (Parent::loadCache(sgn, inputBuffer)) {
            TransformComponent* tComp = sgn->GetComponent<TransformComponent>();
            if (tComp != nullptr && !tComp->loadCache(inputBuffer)) {
                return false;
            }

            return true;
        }

        return false;
    }
} //namespace Divide