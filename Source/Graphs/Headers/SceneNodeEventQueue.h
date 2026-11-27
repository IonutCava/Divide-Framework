// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "ECS/Components/Headers/SGNComponent.h"

namespace Divide
{
    // Every scene node owns one of these, so keep the per-queue footprint small.
    struct SceneNodeEventQueueTraits : moodycamel::ConcurrentQueueDefaultTraits
    {
        static constexpr size_t BLOCK_SIZE = 8u;
        static constexpr size_t IMPLICIT_INITIAL_INDEX_SIZE = 4u;
        static constexpr size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 4u;
    };

    class SceneNodeEventQueue
    {
    public:
        void push(ECS::CustomEvent event)
        {
            _queue.enqueue(std::move(event));
        }

        // Events pushed from inside the handler wait for the next drain.
        template<typename Handler>
        void drain(Handler&& handler)
        {
            size_t remaining = _queue.size_approx();
            ECS::CustomEvent event;
            while (remaining-- > 0u && _queue.try_dequeue(event))
            {
                handler(event);
            }
        }

    private:
        moodycamel::ConcurrentQueue<ECS::CustomEvent, SceneNodeEventQueueTraits> _queue{ 0u };
    };
}
