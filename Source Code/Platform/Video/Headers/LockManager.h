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
#ifndef _LOCK_MANAGER_H_
#define _LOCK_MANAGER_H_

#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferRange.h"


//ref: https://github.com/nvMcJohn/apitest

namespace Divide {

    enum class RenderAPI : U8;

    constexpr U8 g_MaxLockWaitRetries = 5u;

    struct SyncObject {
        inline static constexpr U64 INVALID_FRAME_NUMBER = U64_MAX;

        virtual ~SyncObject();
        virtual void reset();

        Mutex _fenceLock;
        U64 _frameNumber{ INVALID_FRAME_NUMBER };
    };

    FWD_DECLARE_MANAGED_STRUCT(SyncObject);

    struct SyncObjectHandle {
        static constexpr size_t INVALID_SYNC_ID = SIZE_MAX;

        size_t _id{ INVALID_SYNC_ID };
        size_t _generation{ 0u };
    };

    struct BufferLockInstance {
        BufferLockInstance() = default;
        BufferLockInstance(const BufferRange& range, const SyncObjectHandle& handle) noexcept;

        BufferRange _range{};
        SyncObjectHandle _syncObjHandle{};
    };

    class LockManager : public GUIDWrapper {
    public:
        static constexpr U8 DEFAULT_SYNC_FLAG_INTERNAL = 254u;
        static constexpr U8 DEFAULT_SYNC_FLAG_GVD = 255u;
        static constexpr U8 DEFAULT_SYNC_FLAG_SSBO = 252u;
        static constexpr U8 DEFAULT_SYNC_FLAG_TEXTURE = 253u;

        struct BufferLockPoolEntry {
            SyncObject_uptr _ptr{ nullptr };
            size_t _generation{ 0u };
            U8 _flag{ 0u };
        };
        using BufferLockPool = eastl::fixed_vector<BufferLockPoolEntry, 1024, true>;

        static void CleanExpiredSyncObjects( RenderAPI api, U64 frameNumber );
        static void Clear();

    public:
        virtual ~LockManager() = default;

        /// Returns false if we encountered an error
        bool waitForLockedRange(size_t lockBeginBytes, size_t lockLength);
        /// Returns false if we encountered an error
        bool lockRange(size_t lockBeginBytes, size_t lockLength, SyncObjectHandle syncObj);

        static [[nodiscard]] SyncObjectHandle CreateSyncObject(RenderAPI api, U8 flag = DEFAULT_SYNC_FLAG_INTERNAL);

    protected:
        static [[nodiscard]] bool InitLockPoolEntry( RenderAPI api, BufferLockPoolEntry& entry );
        static [[nodiscard]] SyncObjectHandle CreateSyncObjectLocked( RenderAPI api, U8 flag, bool isRetry = false);

        virtual bool waitForLockedRangeLocked(const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock) = 0;

    protected:
        mutable Mutex _bufferLockslock; // :D
        eastl::fixed_vector<BufferLockInstance, 64, true> _bufferLocks;
        eastl::fixed_vector<BufferLockInstance, 64, true> _swapLocks;

        static Mutex s_bufferLockLock; // :D
        static BufferLockPool s_bufferLockPool;
    };

}; //namespace Divide

#endif //_LOCK_MANAGER_H_
