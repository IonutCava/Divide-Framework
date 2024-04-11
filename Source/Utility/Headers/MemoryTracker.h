/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_MEMORY_TRACKER_H_
#define DVD_MEMORY_TRACKER_H_

#include "Core/Headers/StringHelper.h"

namespace Divide {
// not totally compatible with smart pointers. Oh well ... -Ionut
namespace MemoryManager {
/// From https://www.relisoft.com/book/tech/9new.html
class MemoryTracker {
    class Entry {
       public:
        Entry() noexcept : Entry(nullptr, 0, 0)
        {
        }

        Entry(const char* file, const size_t line, const size_t size) noexcept
            : _file(file),
              _line(line),
              _size(size)
        {
        }

        [[nodiscard]] char const* File() const noexcept {
            return _file;
        }

        [[nodiscard]] size_t Line() const noexcept {
            return _line;
        }

        [[nodiscard]] size_t Size() const noexcept {
            return _size;
        }

       private:
        char const* _file;
        size_t _line;
        size_t _size;
    };

    class Lock {
       public:
        Lock(MemoryTracker& tracer) noexcept
            : _tracer(tracer)
        {
            _tracer.lock();
        }
        ~Lock()
        {
            _tracer.unlock();
        }

       private:
        MemoryTracker& _tracer;
    };

    friend class Lock;

   public:
    MemoryTracker() noexcept
    {
        Ready = true;
    }

    ~MemoryTracker()
    {
        Ready = false;
        _allocations.clear();
    }

    void Add(void* p, size_t size, char const* file, size_t line) {
        if (!_locked) {
            LockGuard<Mutex> w_lock(_mutex);
            Lock lock(*this);
            emplace(_allocations, p, file, line, size);
        }
    }

    void Remove(void* p) {
        if (!_locked) {
            if (!LogAllAllocations) {
                LockGuard<Mutex> w_lock(_mutex);
                Lock lock(*this);
                const hashMap<void*, Entry>::iterator it = _allocations.find(p);
                if (it != std::cend(_allocations)) {
                    _allocations.erase(it);
                } else {
                    //assert(false && "Called custom delete on non-custom allocated pointer!");
                }
            }
        }
    }

    string Dump(bool& leakDetected, size_t& sizeLeaked) {
        string output;
        sizeLeaked = 0;
        leakDetected = false;

        Lock lock(*this);
        LockGuard<Mutex> w_lock(_mutex);
        if (!_allocations.empty()) {
            string msg;
            if (LogAllAllocations) {
                msg = "memory allocations detected";
            } else {
                msg = "memory leaks detected";
            }
            output.append(Util::StringFormat("{} {}\n", _allocations.size(), msg.c_str()));
            size_t totalUsage = 0;
            for (hashMap<void*, Entry>::const_iterator it = std::cbegin(_allocations);
                           it != std::cend(_allocations);
                           ++it)
            {
                const Entry& entry = it->second;
                size_t crtSize = entry.Size();
                totalUsage += crtSize;

                output.append(entry.File());
                output.append(", ");
                output.append(Util::to_string(entry.Line()));
                output.append(Util::StringFormat("( {} bytes / {} bytes ({:5.2f} Mb)) \n", crtSize, totalUsage, (totalUsage / 1024.0f) / 1024));
                sizeLeaked += entry.Size();
            }
            leakDetected = true;
            _allocations.clear();
        }
        if (!LogAllAllocations && sizeLeaked > 0) {
            output.append("Total leaked bytes: ");
            output.append(Util::to_string(sizeLeaked));
            output.append(" / MB: ");
            output.append(Util::to_string((sizeLeaked / 1024.0f) / 1024));
            output.append("\n");
        } 
        return output;
    }

    static bool Ready;
    static bool LogAllAllocations;

   private:
    void lock() noexcept {
        _locked = true;
    }

    void unlock() noexcept {
        _locked = false;
    }

   private:
    mutable Mutex _mutex;
    hashMap<void*, Entry> _allocations;
    std::atomic_bool _locked = false;
};

extern MemoryTracker AllocTracer;
}  // namespace MemoryManager
}  // namespace Divide

#endif  //DVD_MEMORY_TRACKER_H_
