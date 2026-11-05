#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "PushConstants.h"

namespace Divide
{
    class IMPrimitiveDrawSnapshots
    {
    public:
        template<typename Callback>
        void record(const U64 frame, const UniformData& defaults, Callback&& callback)
        {
            LockGuard<Mutex> lock(_mutex);
            if (_frame != frame)
            {
                _frame = frame;
                _next = 0u;
            }

            UniformData* snapshot = nullptr;
            if (_next < _inlineSnapshots.size())
            {
                snapshot = &_inlineSnapshots[_next];
            }
            else
            {
                const size_t index = _next - _inlineSnapshots.size();
                if (index == _overflowSnapshots.size())
                {
                    _overflowSnapshots.emplace_back(std::make_unique<UniformData>());
                }
                snapshot = _overflowSnapshots[index].get();
            }
            ++_next;

            *snapshot = defaults;
            callback(*snapshot);
        }

    private:
        Mutex _mutex;
        std::array<UniformData, 2u> _inlineSnapshots{};
        vector<std::unique_ptr<UniformData>> _overflowSnapshots;
        U64 _frame{U64_MAX};
        size_t _next{0u};
    };
}
