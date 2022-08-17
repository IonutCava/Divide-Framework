#include "stdafx.h"

#include "Headers/glLockManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

Mutex glLockManager::s_bufferLockLock;
glLockManager::BufferLockPool glLockManager::s_bufferLockPool;

constexpr GLuint64 kOneSecondInNanoSeconds = 1000000000;

SyncObject::~SyncObject()
{
    reset();
}

void SyncObject::reset() {
    GL_API::DestroyFrameFenceSync(_impl);
}

glLockManager::~glLockManager()
{
    wait(true);
}

void glLockManager::CleanExpiredSyncObjects(const U64 frameNumber) {

    ScopedLock<Mutex> r_lock(s_bufferLockLock);
    for (const SyncObject_uptr& syncObject : s_bufferLockPool) {
        ScopedLock<Mutex> w_lock(syncObject->_fenceLock);
        if (syncObject->_impl._frameNumber <= frameNumber) {
            syncObject->reset();
        }
    }
}
SyncObject* glLockManager::CreateSyncObjectLocked(const bool isRetry) {
    if (isRetry) {
        // We failed once, so create a new object
        const SyncObject_uptr& ret = s_bufferLockPool.emplace_back(MOV(eastl::make_unique<SyncObject>()));
        ret->_impl = GL_API::CreateFrameFenceSync();
        return ret.get();
    }

    // Attempt reuse
    for (const SyncObject_uptr& syncObject : s_bufferLockPool) {
        ScopedLock<Mutex> w_lock_sync(syncObject->_fenceLock);
        // Check again to avoid race conditions
        if (syncObject->_impl._syncObject == nullptr) {
            syncObject->_impl = GL_API::CreateFrameFenceSync();
            return syncObject.get();
        }
    }

    return CreateSyncObjectLocked(true);
}

SyncObject* glLockManager::CreateSyncObject() {
    ScopedLock<Mutex> w_lock(s_bufferLockLock);
    return CreateSyncObjectLocked();
}

void glLockManager::Clear() {
    ScopedLock<Mutex> r_lock(s_bufferLockLock);
    s_bufferLockPool.clear();
}

void glLockManager::wait(const bool blockClient) {
    OPTICK_EVENT();

    waitForLockedRange(0u, std::numeric_limits<size_t>::max(), blockClient);
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
    _swapLocks.resize(0);

    ScopedLock<SharedMutex> w_lock(_bufferLockslock);
    for (BufferLockInstance& lock : _bufferLocks) {
        assert(lock._syncObj != nullptr);

        if (!Overlaps(testRange, lock._range)) {
            _swapLocks.push_back(lock);
        } else {
            U8 retryCount = 0u;

            ScopedLock<Mutex> w_lock_sync(lock._syncObj->_fenceLock);
            if (lock._syncObj->_impl._syncObject == nullptr ||
                lock._syncObj->_impl._frameNumber <= GL_API::GetStateTracker()->_lastSyncedFrameNumber)
            {
                continue;
            }

            if (Wait(lock._syncObj->_impl._syncObject, blockClient, quickCheck, retryCount)) {
                lock._syncObj->reset();
                if (retryCount > g_MaxLockWaitRetries - 1) {
                    Console::errorfn("glLockManager: Wait (%p) [%d - %d] %s - %d retries", this, lockBeginBytes, lockLength, blockClient ? "true" : "false", retryCount);
                }
            } else if (!quickCheck) {
                error = true;
                _swapLocks.push_back(lock);
            }
        }
    }

    _bufferLocks.swap(_swapLocks);

    return !error;
}

bool glLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, SyncObject* syncObj) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(syncObj != nullptr && lockLength > 0u, "glLockManager::lockRange error: Invalid lock range!");

    const BufferRange testRange{ lockBeginBytes, lockLength };

    SharedLock<SharedMutex> w_lock(_bufferLockslock);
    // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
    for (BufferLockInstance& lock : _bufferLocks) {
        if (Overlaps(testRange, lock._range)) {
            Merge(lock._range, testRange);
            lock._syncObj = syncObj;
            return true;
        }
    }

    for (BufferLockInstance& lock : _bufferLocks) {
        if (lock._syncObj == nullptr) {
            lock._range = testRange;
            lock._syncObj = syncObj;
            return true;
        }
    }

    // No luck with our reuse search. Add a new lock.
    _bufferLocks.push_back(BufferLockInstance{ testRange, syncObj });
    
    return true;
}

};//namespace Divide