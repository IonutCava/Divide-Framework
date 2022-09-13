#include "stdafx.h"

#include "Headers/LockManager.h"

#include "Headers/GFXDevice.h"

namespace Divide {
    Mutex LockManager::s_bufferLockLock;
    LockManager::BufferLockPool LockManager::s_bufferLockPool;

    SyncObject::~SyncObject() 
    {
        reset();
    }

    void SyncObject::reset() {
        _frameNumber = INVALID_FRAME_NUMBER;
    }

    BufferLockInstance::BufferLockInstance(const BufferRange& range, const SyncObjectHandle& handle) noexcept
        : _range(range),
          _syncObjHandle(handle)
    {
    }

    void LockManager::CleanExpiredSyncObjects(const U64 frameNumber) {

        ScopedLock<Mutex> r_lock(s_bufferLockLock);
        for (const BufferLockPoolEntry& syncObject : s_bufferLockPool) {
            ScopedLock<Mutex> w_lock(syncObject._ptr->_fenceLock);
            if (syncObject._ptr->_frameNumber < frameNumber) {
                syncObject._ptr->reset();
            }
        }
    }

    void LockManager::Clear() {
        ScopedLock<Mutex> r_lock(s_bufferLockLock);
        s_bufferLockPool.clear();
    }

    SyncObjectHandle LockManager::createSyncObjectLocked(const U8 flag, const bool isRetry) {
        if (isRetry) {
            // We failed once, so create a new object
            BufferLockPoolEntry newEntry{};
            if (initLockPoolEntry(newEntry)) {
                newEntry._ptr->_frameNumber = GFXDevice::FrameCount();
                newEntry._flag = flag;
                s_bufferLockPool.emplace_back(MOV(newEntry));
                return SyncObjectHandle{ s_bufferLockPool.size() - 1u, newEntry._generation };
            }
        }

        // Attempt reuse
        for (size_t i = 0u; i < s_bufferLockPool.size(); ++i) {
            BufferLockPoolEntry& syncObject = s_bufferLockPool[i];

            ScopedLock<Mutex> w_lock_sync(syncObject._ptr->_fenceLock);
            if (initLockPoolEntry(syncObject)) {
                syncObject._ptr->_frameNumber = GFXDevice::FrameCount();
                syncObject._flag = flag;
                return SyncObjectHandle{ i, ++syncObject._generation };
            }
        }

        return createSyncObjectLocked(flag, true);
    }

    bool LockManager::waitForLockedRange(const size_t lockBeginBytes, const size_t lockLength) {
        OPTICK_EVENT();

        const BufferRange testRange{ lockBeginBytes, lockLength };

        bool error = false;

        ScopedLock<Mutex> w_lock_global(_bufferLockslock);
        _swapLocks.resize(0);

        for (const BufferLockInstance& lock : _bufferLocks) {
            DIVIDE_ASSERT(lock._syncObjHandle._id != SyncObjectHandle::INVALID_SYNC_ID);

            ScopedLock<Mutex> r_lock(s_bufferLockLock);
            const BufferLockPoolEntry& syncLockInstance = s_bufferLockPool[lock._syncObjHandle._id];
            if (syncLockInstance._generation != lock._syncObjHandle._generation) {
                DIVIDE_ASSERT(syncLockInstance._generation > lock._syncObjHandle._generation);
                continue;
            }

            if (!Overlaps(testRange, lock._range)) {
                _swapLocks.push_back(lock);
            } else {
                ScopedLock<Mutex> w_lock(syncLockInstance._ptr->_fenceLock);
                if (!waitForLockedRangeLocked(syncLockInstance._ptr, testRange, lock)) {
                    _swapLocks.push_back(lock);
                }
            }
        }

        _bufferLocks.swap(_swapLocks);

        return !error;
    }

    bool LockManager::lockRange(size_t lockBeginBytes, size_t lockLength, SyncObjectHandle syncObj) {
        OPTICK_EVENT();

        DIVIDE_ASSERT(syncObj._id != SyncObjectHandle::INVALID_SYNC_ID && lockLength > 0u, "LockManager::lockRange error: Invalid lock range!");

        const BufferRange testRange{ lockBeginBytes, lockLength };

        ScopedLock<Mutex> w_lock(_bufferLockslock);
        // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
        for (BufferLockInstance& lock : _bufferLocks) {
            if (Overlaps(testRange, lock._range)) {
                Merge(lock._range, testRange);
                lock._syncObjHandle = syncObj;
                return true;
            }
        }

        // No luck with our reuse search. Add a new lock.
        _bufferLocks.emplace_back(testRange, syncObj);

        return true;
    }
    SyncObjectHandle LockManager::createSyncObject(const U8 flag) {
        ScopedLock<Mutex> w_lock(s_bufferLockLock);
        return createSyncObjectLocked(flag);
    }


}; //namespace Divide
