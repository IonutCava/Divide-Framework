#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "PushConstants.h"

namespace Divide
{
    class UniformDataSnapshots
    {
    public:
        explicit UniformDataSnapshots(const size_t snapshotCount = 8u)
            : _snapshots(snapshotCount)
        {
        }

        template<typename Callback>
        void record(const U64 frame, const UniformData& defaults, Callback&& callback)
        {
            LockGuard<Mutex> lock(_mutex);
            if (_frame != frame)
            {
                _frame = frame;
                _next = 0u;
            }

            if (_next == _snapshots.size())
            {
                // Immediate draws have no fixed view budget. Deque growth preserves recorded addresses.
                _snapshots.emplace_back();
            }
            UniformData& snapshot = _snapshots[_next++];

            snapshot = defaults;
            callback(snapshot);
        }

    private:
        Mutex _mutex;
        eastl::deque<UniformData> _snapshots;
        U64 _frame{U64_MAX};
        size_t _next{0u};
    };
}
