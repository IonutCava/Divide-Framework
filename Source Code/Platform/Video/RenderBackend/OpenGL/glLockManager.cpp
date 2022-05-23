#include "stdafx.h"

#include "Headers/glLockManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

Mutex glLockManager::s_bufferLockLock;
glLockManager::BufferLockPool glLockManager::s_bufferLockPool;

constexpr GLuint64 kOneSecondInNanoSeconds = 1000000000;

namespace {
    void DeleteSyncLocked(BufferLockInstance& lock) {
        OPTICK_EVENT("Delete Sync Expired");

        if (lock._state == BufferLockState::DELETED) {
            DIVIDE_ASSERT(std::any_cast<GLsync>(lock._syncObj->_fenceObject) == nullptr);
            return;
        }

        lock._syncObj->reset();
        lock._state = BufferLockState::DELETED;
    }

    void DeleteSync(BufferLockInstance& lock) {
        ScopedLock<Mutex> w_lock(lock._syncObj->_fenceLock);
        DeleteSyncLocked(lock);
    }
};

SyncObject::~SyncObject()
{
    reset();
}

void SyncObject::reset() {
    if (_fenceObject.has_value()) {
        GLsync sync = std::any_cast<GLsync>(_fenceObject);
        GL_API::DestroyFenceSync(sync);
        _fenceObject.reset();
    }
}

glLockManager::~glLockManager()
{
    wait(true);

    const SharedLock<SharedMutex> r_lock(_lock);
    for (BufferLockInstance& lock : _bufferLocks) {
        DeleteSync(lock);
    }
}

void glLockManager::CleanExpiredSyncObjects(const U32 frameID) {

    ScopedLock<Mutex> r_lock(s_bufferLockLock);
    for (SyncObject_uptr& syncObject : s_bufferLockPool) {
        if (syncObject->_fenceObject.has_value() && std::any_cast<GLsync>(syncObject->_fenceObject) != nullptr) {
            ScopedLock<Mutex> w_lock(syncObject->_fenceLock);
            // Check again to avoid race conditions
            if (syncObject->_fenceObject.has_value() &&
                std::any_cast<GLsync>(syncObject->_fenceObject) != nullptr &&
                syncObject->_frameID < frameID &&
                frameID - syncObject->_frameID >= GL_API::s_LockFrameLifetime)
            {
                syncObject->reset();
            }
        }
    }
}

SyncObject* glLockManager::CreateSyncObject(const bool isRetry) {
    if (isRetry) {
        CleanExpiredSyncObjects(GFXDevice::FrameCount());
    }
    {
        ScopedLock<Mutex> r_lock(s_bufferLockLock);
        for (SyncObject_uptr& syncObject : s_bufferLockPool) {
            if (!syncObject->_fenceObject.has_value() || std::any_cast<GLsync>(syncObject->_fenceObject) == nullptr) {
                ScopedLock<Mutex> w_lock(syncObject->_fenceLock);
                // Check again to avoid race conditions
                if (!syncObject->_fenceObject.has_value() || std::any_cast<GLsync>(syncObject->_fenceObject) == nullptr) {
                    syncObject->_frameID = GFXDevice::FrameCount();
                    syncObject->_fenceObject = GL_API::CreateFenceSync();
                    return syncObject.get();
                }
            }
        }
    }

    if (!isRetry) {
        return CreateSyncObject(true);
    }
    {
        ScopedLock<Mutex> r_lock(s_bufferLockLock);
        s_bufferLockPool.emplace_back(MOV(eastl::make_unique<SyncObject>()));
    }
    return CreateSyncObject(false);
}

void glLockManager::Clear() {
    ScopedLock<Mutex> r_lock(s_bufferLockLock);
    s_bufferLockPool.clear();
}

void glLockManager::wait(const bool blockClient) {
    OPTICK_EVENT();

    waitForLockedRange(0u, std::numeric_limits<size_t>::max(), blockClient);
}
 
void glLockManager::lock() {
    OPTICK_EVENT();
    lockRange(0u, std::numeric_limits<size_t>::max(), CreateSyncObject());
}

bool glLockManager::Wait(const GLsync syncObj, const bool blockClient, const bool quickCheck, U8& retryCount) {
    OPTICK_EVENT();
    OPTICK_TAG("Blocking", blockClient);
    OPTICK_TAG("QuickCheck", quickCheck);

    if (!blockClient) {
        glWaitSync(syncObj, 0, GL_TIMEOUT_IGNORED);
        GL_API::QueueFlush();

        return true;
    }

    GLuint64 waitTimeout = 0u;
    SyncObjectMask waitFlags = SyncObjectMask::GL_NONE_BIT;
    while (true) {
        OPTICK_EVENT("Wait - OnLoop");
        OPTICK_TAG("RetryCount", retryCount);
        const GLenum waitRet = glClientWaitSync(syncObj, waitFlags, waitTimeout);
        if (waitRet == GL_ALREADY_SIGNALED || waitRet == GL_CONDITION_SATISFIED) {
            return true;
        }
        DIVIDE_ASSERT(waitRet != GL_WAIT_FAILED, "glLockManager::wait error: Not sure what to do here. Probably raise an exception or something.");

        if (quickCheck) {
            return false;
        }

        if (retryCount == 1) {
            //ToDo: Do I need this here? -Ionut
            GL_API::QueueFlush();
        }

        // After the first time, need to start flushing, and wait for a looong time.
        waitFlags = SyncObjectMask::GL_SYNC_FLUSH_COMMANDS_BIT;
        waitTimeout = kOneSecondInNanoSeconds;

        if (++retryCount > g_MaxLockWaitRetries) {
            if (waitRet != GL_TIMEOUT_EXPIRED) {
                Console::errorfn("glLockManager::wait error: Lock timeout");
            }

            break;
        }
    }
    
    return false;
}

bool glLockManager::waitForLockedRange(const size_t lockBeginBytes,
                                       const size_t lockLength,
                                       const bool blockClient,
                                       const bool quickCheck) {
    OPTICK_EVENT();
    OPTICK_TAG("BlockClient", blockClient);
    OPTICK_TAG("QuickCheck", quickCheck);

    const BufferRange testRange{ lockBeginBytes, lockLength };

    bool error = false;
    ScopedLock<SharedMutex> w_lock(_lock);
    _swapLocks.resize(0);
    for (BufferLockInstance& lock : _bufferLocks) {
        switch (lock._state) {
        case BufferLockState::ACTIVE: {
            if (!Overlaps(testRange, lock._range)) {
                _swapLocks.push_back(lock);
            } else {
                U8 retryCount = 0u;

                ScopedLock<Mutex> w_lock_sync(lock._syncObj->_fenceLock);
                if (lock._syncObj->_fenceObject.has_value() && std::any_cast<GLsync>(lock._syncObj->_fenceObject) != nullptr) {
                    if (Wait(std::any_cast<GLsync>(lock._syncObj->_fenceObject), blockClient, quickCheck, retryCount)) {
                        DeleteSyncLocked(lock);

                        if (retryCount > g_MaxLockWaitRetries - 1) {
                            Console::errorfn("glLockManager: Wait (%p) [%d - %d] %s - %d retries", this, lockBeginBytes, lockLength, blockClient ? "true" : "false", retryCount);
                        }
                    }
                    else if (!quickCheck) {
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

bool glLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, SyncObject* syncObj) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(syncObj != nullptr && lockLength > 0u, "glLockManager::lockRange error: Invalid lock range!");

    const BufferRange testRange{ lockBeginBytes, lockLength };

    SharedLock<SharedMutex> w_lock(_lock);
    {
        OPTICK_EVENT("Attempt reuse");
        // This should avoid any lock leaks, since any fences we haven't waited on will be considered "signaled" eventually
        for (BufferLockInstance& lock : _bufferLocks) {
            ScopedLock<Mutex> w_lock_sync(lock._syncObj->_fenceLock);
            if (lock._syncObj->_frameID < syncObj->_frameID && syncObj->_frameID - lock._syncObj->_frameID >= GL_API::s_LockFrameLifetime) {
                lock._state = BufferLockState::EXPIRED;
            }
        }

        // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
        for (BufferLockInstance& lock : _bufferLocks) {
            if (Overlaps(testRange, lock._range)) {
                if (lock._state == BufferLockState::EXPIRED) {
                    DeleteSync(lock);
                }

                lock._range._startOffset = std::min(testRange._startOffset, lock._range._startOffset);
                lock._range._length = std::max(testRange._length, lock._range._length);
                lock._syncObj = syncObj;
                lock._state = BufferLockState::ACTIVE;
                return true;
            }
        }

        for (BufferLockInstance& lock : _bufferLocks) {
            if (lock._state == BufferLockState::DELETED || lock._state == BufferLockState::EXPIRED) {
                if (lock._state == BufferLockState::EXPIRED) {
                    DeleteSync(lock);
                }

                lock._range = testRange;
                lock._syncObj = syncObj;
                lock._state = BufferLockState::ACTIVE;
                return true;
            }
        }
    }
    {
        OPTICK_EVENT("Add Fence");
        // No luck with our reuse search. Add a new lock.
        _bufferLocks.emplace_back(testRange, syncObj);
    }
    return true;
}

};//namespace Divide