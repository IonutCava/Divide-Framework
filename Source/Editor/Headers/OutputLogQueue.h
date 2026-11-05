// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/Headers/Console.h"
#include "Platform/Threading/Headers/SharedMutex.h"

namespace Divide
{
    class OutputLogQueue
    {
    public:
        static constexpr size_t CAPACITY = 1024u;

        void push(const Console::OutputEntry& entry)
        {
            LockGuard<Mutex> lock(_lock);
            if (_size == CAPACITY)
            {
                _entries[_head] = entry;
                _head = (_head + 1u) % CAPACITY;
            }
            else
            {
                _entries[(_head + _size) % CAPACITY] = entry;
                ++_size;
            }
        }

        template<typename Consumer>
        size_t drain(Consumer&& consume)
        {
            LockGuard<Mutex> lock(_lock);
            const size_t count = _size;
            while (_size > 0u)
            {
                consume(_entries[_head]);
                _head = (_head + 1u) % CAPACITY;
                --_size;
            }
            return count;
        }

        void clear()
        {
            LockGuard<Mutex> lock(_lock);
            _head = 0u;
            _size = 0u;
        }

    private:
        std::array<Console::OutputEntry, CAPACITY> _entries{};
        size_t _head = 0u;
        size_t _size = 0u;
        Mutex _lock;
    };
}
