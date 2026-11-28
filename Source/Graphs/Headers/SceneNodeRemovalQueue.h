// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include <algorithm>

namespace Divide
{
    class SceneNodeRemovalQueue
    {
    public:
        void push(const I64 guid)
        {
            _pending.enqueue(guid);
        }

        // Requests pushed from inside the remover wait for the next batch.
        template<typename Resolver, typename Remover>
        void drain(const Resolver& resolve, const Remover& remove)
        {
            _processing.clear();
            std::array<I64, 64u> chunk;
            size_t count = 0u;
            while ((count = _pending.try_dequeue_bulk(chunk.data(), chunk.size())) > 0u)
            {
                _processing.insert(_processing.end(), chunk.data(), chunk.data() + count);
            }

            std::sort(_processing.begin(), _processing.end());
            _processing.erase(std::unique(_processing.begin(), _processing.end()), _processing.end());
            for (const I64 guid : _processing)
            {
                if (auto* node = resolve(guid))
                {
                    remove(*node);
                }
            }
            _processing.clear();
        }

        void clear()
        {
            std::array<I64, 64u> chunk;
            while (_pending.try_dequeue_bulk(chunk.begin(), chunk.size()) > 0u)
            {
            }
        }

    private:
        moodycamel::ConcurrentQueue<I64> _pending;
        vector<I64> _processing;
    };
}
