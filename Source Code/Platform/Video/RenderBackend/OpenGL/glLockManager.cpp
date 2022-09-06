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
    for (const BufferLockPoolEntry& syncObject : s_bufferLockPool) {
        ScopedLock<Mutex> w_lock(syncObject._ptr->_fenceLock);
        if (syncObject._ptr->_impl._frameNumber < frameNumber) {
            syncObject._ptr->reset();
        }
    }
}

SyncObjectHandle glLockManager::CreateSyncObjectLocked(const U8 flag, const bool isRetry) {
    if (isRetry) {
        // We failed once, so create a new object
        BufferLockPoolEntry newEntry{};
        newEntry._ptr = eastl::make_unique<SyncObject>();
        newEntry._ptr->_impl = GL_API::CreateFrameFenceSync();
        newEntry._flag = flag;
        s_bufferLockPool.emplace_back(MOV(newEntry));
        return SyncObjectHandle{ s_bufferLockPool.size() - 1u, newEntry._generation };
    }

    // Attempt reuse
    for (size_t i = 0u; i < s_bufferLockPool.size(); ++i) {
        BufferLockPoolEntry& syncObject = s_bufferLockPool[i];

        ScopedLock<Mutex> w_lock_sync(syncObject._ptr->_fenceLock);
        if (syncObject._ptr->_impl._syncObject == nullptr) {
            syncObject._ptr->_impl = GL_API::CreateFrameFenceSync();
            syncObject._flag = flag;
            return SyncObjectHandle{ i, ++syncObject._generation };
        }
    }

    return CreateSyncObjectLocked(flag, true);
}

SyncObjectHandle glLockManager::CreateSyncObject(const U8 flag) {
    ScopedLock<Mutex> w_lock(s_bufferLockLock);
    return CreateSyncObjectLocked(flag);
}

void glLockManager::Clear() {
    ScopedLock<Mutex> r_lock(s_bufferLockLock);
    s_bufferLockPool.clear();
}

void glLockManager::wait(const bool blockClient) {
    waitForLockedRange(0u, std::numeric_limits<size_t>::max(), blockClient);
}

bool glLockManager::lock(SyncObjectHandle syncObj) {
    return lockRange(0u, std::numeric_limits<size_t>::max(), syncObj);
}

bool glLockManager::Wait(GLsync sync, const bool blockClient, const bool quickCheck, U8& retryCount) {
    OPTICK_EVENT();
    OPTICK_TAG("Blocking", blockClient);
    OPTICK_TAG("QuickCheck", quickCheck);

    if (!blockClient) {
        glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
        GL_API::QueueFlush();

        return true;
    }

    GLuint64 waitTimeout = 0u;
    SyncObjectMask waitFlags = SyncObjectMask::GL_NONE_BIT;
    while (true) {
        OPTICK_EVENT("Wait - OnLoop");
        OPTICK_TAG("RetryCount", retryCount);

        const GLenum waitRet = glClientWaitSync(sync, waitFlags, waitTimeout);
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

    ScopedLock<Mutex> w_lock_global(_bufferLockslock);
    _swapLocks.resize(0);
    for (BufferLockInstance& lock : _bufferLocks) {
        DIVIDE_ASSERT(lock._syncObjHandle._id != GLUtil::k_invalidSyncID);

        ScopedLock<Mutex> r_lock(s_bufferLockLock);
        BufferLockPoolEntry& syncLockInstance = s_bufferLockPool[lock._syncObjHandle._id];
        if (syncLockInstance._generation != lock._syncObjHandle._generation) {
            DIVIDE_ASSERT(syncLockInstance._generation > lock._syncObjHandle._generation);
            continue;
        }

        ScopedLock<Mutex> w_lock(syncLockInstance._ptr->_fenceLock);
        FrameDependendSync& syncInstance = syncLockInstance._ptr->_impl;

        if (syncInstance._syncObject == nullptr) {
            // Lock expired from underneath us
            continue;
        }

        if (!Overlaps(testRange, lock._range)) {
            _swapLocks.push_back(lock);
        } else {
            U8 retryCount = 0u;

            if (syncInstance._syncObject == nullptr || syncInstance._frameNumber < GL_API::GetStateTracker()->_lastSyncedFrameNumber) {
                continue;
            }

            if (Wait(syncInstance._syncObject, blockClient, quickCheck, retryCount)) {
                syncLockInstance._ptr->reset();

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

bool glLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, SyncObjectHandle syncObj) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(syncObj._id != GLUtil::k_invalidSyncID && lockLength > 0u, "glLockManager::lockRange error: Invalid lock range!");

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

};//namespace Divide