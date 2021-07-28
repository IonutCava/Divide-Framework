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
#ifndef _GL_BUFFER_LOCK_MANAGER_H
#define _GL_BUFFER_LOCK_MANAGER_H

#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"

// https://github.com/nvMcJohn/apitest
// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

namespace Divide {

struct BufferRange {
    size_t _startOffset = 0u;
    size_t _length = 0u;
};

inline bool operator==(const BufferRange& lhs, const BufferRange& rhs) noexcept {
    return lhs._startOffset == rhs._startOffset &&
           lhs._length == rhs._length;
}

[[nodiscard]] inline bool Overlaps(const BufferRange& lhs, const BufferRange& rhs) noexcept {
    return lhs._startOffset < (rhs._startOffset + rhs._length) && rhs._startOffset < (lhs._startOffset + lhs._length);
}

// --------------------------------------------------------------------------------------------------------------------
struct BufferLock {
    BufferLock() = default;
    explicit BufferLock(const BufferRange range, const GLsync syncObj, const U32 frameID) noexcept
        : _range(range), _syncObj(syncObj), _frameID(frameID)
    {
    }

    BufferRange _range{};
    GLsync _syncObj = nullptr;
    U32 _frameID = 0;
    bool _valid = true;
};

// --------------------------------------------------------------------------------------------------------------------
class glBufferLockManager final : public glLockManager {
   public:
    ~glBufferLockManager();

    /// Returns false if we encountered an error
    bool waitForLockedRange(size_t lockBeginBytes, size_t lockLength, bool blockClient, bool quickCheck = false);

    /// Returns false if we encountered an error
    bool lockRange(size_t lockBeginBytes, size_t lockLength, U32 frameID);

   private:
    mutable Mutex _lock;
    vectorEASTL<BufferLock> _bufferLocks;
    vectorEASTL<BufferLock> _swapLocks;
};

};  // namespace Divide
#endif
