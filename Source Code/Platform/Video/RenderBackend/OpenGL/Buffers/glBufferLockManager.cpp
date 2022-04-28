#include "stdafx.h"

#include "Headers/glBufferLockManager.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    void DeleteSyncLocked(BufferLock & lock) {
        OPTICK_EVENT("Delete Sync Expired");

        if (lock._state == BufferLockState::DELETED) {
            DIVIDE_ASSERT(lock._syncObj->_fence == nullptr);
            return;
        }

        lock._syncObj->reset();
        lock._state = BufferLockState::DELETED;
    }

    void DeleteSync(BufferLock& lock) {
        ScopedLock<Mutex> w_lock(lock._syncObj->_fenceLock);
        DeleteSyncLocked(lock);
    }
};

SyncObject::~SyncObject()
{
    reset();
}

void SyncObject::reset() {
    if (_fence != nullptr) {
        glDeleteSync(_fence);
        GL_API::s_fenceSyncCounter[GL_API::g_LockFrameLifetime - 1u] -= 1u;
        _fence = nullptr;
    }
}

glBufferLockManager::~glBufferLockManager()
{
    const SharedLock<SharedMutex> r_lock(_lock);
    for (BufferLock& lock : _bufferLocks) {
        DeleteSync(lock);
    }
}

bool glBufferLockManager::waitForLockedRange(size_t lockBeginBytes,
                                             size_t lockLength,
                                             const bool blockClient,
                                             const bool quickCheck) {
    OPTICK_EVENT();
    OPTICK_TAG("BlockClient", blockClient);
    OPTICK_TAG("QuickCheck", quickCheck);

    const BufferRange testRange{lockBeginBytes, lockLength};

    bool error = false;
    ScopedLock<SharedMutex> w_lock(_lock);
    _swapLocks.resize(0);
    for (BufferLock& lock : _bufferLocks) {
        switch (lock._state) {
            case BufferLockState::ACTIVE: {
                if (!Overlaps(testRange, lock._range)) {
                    _swapLocks.push_back(lock);
                } else {
                    U8 retryCount = 0u;

                    ScopedLock<Mutex> w_lock_sync(lock._syncObj->_fenceLock);
                    if (lock._syncObj->_fence != nullptr) {
                        if (Wait(lock._syncObj->_fence, blockClient, quickCheck, retryCount)) {
                            DeleteSyncLocked(lock);

                            if (retryCount > g_MaxLockWaitRetries - 1) {
                                Console::errorfn("glBufferLockManager: Wait (%p) [%d - %d] %s - %d retries", this, lockBeginBytes, lockLength, blockClient ? "true" : "false", retryCount);
                            }
                        } else if (!quickCheck) {
                            error = true;
                            _swapLocks.push_back(lock);
                            _swapLocks.back()._state = BufferLockState::ERROR;
                        }
                    }
                }
            } break;
            case BufferLockState::EXPIRED: {
                DeleteSync(lock);
            } break;
            case BufferLockState::DELETED: {
                // Nothing. This lock was already signaled and we removed it
                NOP();
            } break;
            case BufferLockState::ERROR: {
                //DIVIDE_UNEXPECTED_CALL();
                // Something is holding OpenGL in a busy state.
            } break;
        };
    }

    _bufferLocks.swap(_swapLocks);

    return !error;
}

bool glBufferLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, SyncObject_uptr& syncObj) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(lockLength > 0u, "glBufferLockManager::lockRange error: Invalid lock range!");

    const BufferRange testRange{ lockBeginBytes, lockLength };

    SharedLock<SharedMutex> w_lock(_lock);
    {
        OPTICK_EVENT("Attempt reuse");
        // This should avoid any lock leaks, since any fences we haven't waited on will be considered "signaled" eventually
        for (BufferLock& lock : _bufferLocks) {
            ScopedLock<Mutex> w_lock_sync(lock._syncObj->_fenceLock);
            if (lock._syncObj->_frameID < syncObj->_frameID && syncObj->_frameID - lock._syncObj->_frameID >= GL_API::g_LockFrameLifetime) {
                lock._state = BufferLockState::EXPIRED;
            }
        }

        // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
        for (BufferLock& lock : _bufferLocks) {
            if (Overlaps(testRange, lock._range)) {
                if (lock._state == BufferLockState::EXPIRED) {
                    DeleteSync(lock);
                }

                lock._range._startOffset = std::min(testRange._startOffset, lock._range._startOffset);
                lock._range._length = std::max(testRange._length, lock._range._length);
                lock._syncObj = syncObj.get();
                lock._state = BufferLockState::ACTIVE;
                return true;
            }
        }

        for (BufferLock& lock : _bufferLocks) {
            if (lock._state == BufferLockState::DELETED || lock._state == BufferLockState::EXPIRED) {
                if (lock._state == BufferLockState::EXPIRED) {
                    DeleteSync(lock);
                }

                lock._range = testRange;
                lock._syncObj = syncObj.get();
                lock._state = BufferLockState::ACTIVE;
                return true;
            }
        }
    }
    {
        OPTICK_EVENT("Add Fence");
        // No luck with our reuse search. Add a new lock.
        _bufferLocks.emplace_back(testRange, syncObj.get());
    }
    return true;
}

};
