/*Copyright (c) 2018 DIVIDE-Studio
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
#ifndef _GL_LOCK_MANAGER_H_
#define _GL_LOCK_MANAGER_H_

#include "glResources.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferRange.h"

// https://github.com/nvMcJohn/apitest
// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------


namespace Divide {

constexpr U8 g_MaxLockWaitRetries = 5u;

struct SyncObject {
    ~SyncObject();
    void reset();

    Mutex _fenceLock;
    FrameDependendSync _impl;
};

FWD_DECLARE_MANAGED_STRUCT(SyncObject);

struct SyncObjectHandle {
    static constexpr size_t INVALID_ID = std::numeric_limits<size_t>::max();

    size_t _id{ INVALID_ID };
    size_t _generation{ 0u };
};

struct BufferLockInstance {
    BufferRange _range{};
    SyncObjectHandle _syncObjHandle{};
};

// --------------------------------------------------------------------------------------------------------------------
class glLockManager : public GUIDWrapper {
   public:
    static constexpr U8 DEFAULT_SYNC_FLAG_INTERNAL = 254u;
    static constexpr U8 DEFAULT_SYNC_FLAG_GVD = 255u;
    struct BufferLockPoolEntry {
        SyncObject_uptr _ptr{ nullptr };
        size_t _generation{ 0u };
        U8 _flag{ 0u };
    };

    using BufferLockPool = eastl::fixed_vector<BufferLockPoolEntry, 1024, true>;

    static void CleanExpiredSyncObjects(U64 frameNumber);
    static [[nodiscard]] SyncObjectHandle CreateSyncObject(U8 flag);
    static void Clear();

   public:
    virtual ~glLockManager();

    bool lock(SyncObjectHandle syncObj = CreateSyncObject(DEFAULT_SYNC_FLAG_INTERNAL));
    void wait(bool blockClient);

    /// Returns false if we encountered an error
    bool waitForLockedRange(size_t lockBeginBytes, size_t lockLength, bool blockClient, bool quickCheck = false);
    /// Returns false if we encountered an error
    bool lockRange(size_t lockBeginBytes, size_t lockLength, SyncObjectHandle syncObj = CreateSyncObject(DEFAULT_SYNC_FLAG_INTERNAL));

  protected:
    /// Returns true if the sync object was signaled. retryCount is the number of retries it took to wait for the object
    /// if quickCheck is true, we don't retry if the initial check fails 
    static bool Wait(GLsync sync, bool blockClient, bool quickCheck, U8& retryCount);
    static [[nodiscard]] SyncObjectHandle CreateSyncObjectLocked(U8 flag, bool isRetry = false);
   protected:
     mutable Mutex _bufferLockslock; // :D
     eastl::fixed_vector<BufferLockInstance, 64, true> _bufferLocks;
     eastl::fixed_vector<BufferLockInstance, 64, true> _swapLocks;

     static Mutex s_bufferLockLock; // :D
     static BufferLockPool s_bufferLockPool;
};

};  // namespace Divide

#endif  //_GL_LOCK_MANAGER_H_