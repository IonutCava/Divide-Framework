// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Platform/Headers/PlatformDataTypes.h"
#include "Platform/Threading/Headers/SharedMutex.h"

#include <algorithm>

namespace Divide
{
    class SceneNodeRemovalQueue
    {
    public:
        void push(const I64 guid)
        {
            LockGuard<Mutex> lock(_lock);
            _pending.push_back(guid);
        }

        template<typename Resolver, typename Remover>
        void drain(const Resolver& resolve, const Remover& remove)
        {
            {
                LockGuard<Mutex> lock(_lock);
                _processing.swap(_pending);
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
            LockGuard<Mutex> lock(_lock);
            _pending.clear();
        }

    private:
        Mutex _lock;
        vector<I64> _pending;
        vector<I64> _processing;
    };
}
