// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/TemplateLibraries/Headers/Vector.h"
#include "ECS/Components/Headers/SGNComponent.h"
#include "Platform/Threading/Headers/SharedMutex.h"

#include <array>
#include <atomic>
#include <utility>

namespace Divide
{
    class SceneNodeEventQueue
    {
    public:
        static constexpr size_t CAPACITY = 128u;

        void push(ECS::CustomEvent event)
        {
            if (!_overflowActive.load(std::memory_order_acquire))
            {
                for (size_t idx = 0u; idx < CAPACITY; ++idx)
                {
                    State expected = State::FREE;
                    if (_states[idx].compare_exchange_strong(expected, State::WRITING,
                                                              std::memory_order_acquire,
                                                              std::memory_order_relaxed))
                    {
                        _events[idx] = std::move(event);
                        _states[idx].store(State::READY, std::memory_order_release);
                        return;
                    }
                }
            }

            LockGuard<Mutex> lock(_overflowLock);
            _overflowActive.store(true, std::memory_order_release);
            _overflow.emplace_back(std::move(event));
        }

        template<typename Handler>
        void drain(Handler&& handler)
        {
            for (size_t idx = 0u; idx < CAPACITY; ++idx)
            {
                State expected = State::READY;
                if (_states[idx].compare_exchange_strong(expected, State::READING,
                                                         std::memory_order_acquire,
                                                         std::memory_order_relaxed))
                {
                    ECS::CustomEvent event = std::move(_events[idx]);
                    _states[idx].store(State::FREE, std::memory_order_release);
                    handler(event);
                }
            }

            {
                LockGuard<Mutex> lock(_overflowLock);
                _processingOverflow.swap(_overflow);
                _overflowActive.store(false, std::memory_order_release);
            }
            for (const ECS::CustomEvent& event : _processingOverflow)
            {
                handler(event);
            }
            _processingOverflow.clear();
        }

    private:
        enum class State : U8
        {
            FREE,
            WRITING,
            READY,
            READING
        };

        std::array<ECS::CustomEvent, CAPACITY> _events{};
        std::array<std::atomic<State>, CAPACITY> _states{};
        Mutex _overflowLock;
        vector<ECS::CustomEvent> _overflow;
        vector<ECS::CustomEvent> _processingOverflow;
        std::atomic_bool _overflowActive{false};
    };
}
