#include "stdafx.h"

#include "Headers/glBufferLockManager.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    // Auto-delete locks older than this number of frames
    constexpr U32 g_LockFrameLifetime = 6u; //(APP->Driver->GPU x2)
};

std::atomic_bool glBufferLockManager::s_driverBusy;

void glBufferLockManager::OnStartup() noexcept {
    std::atomic_init(&s_driverBusy, false);
}

bool glBufferLockManager::DriverBusy() noexcept {
    return s_driverBusy.load();
}

void glBufferLockManager::DriverBusy(const bool state) noexcept {
    s_driverBusy.store(state);
}

glBufferLockManager::~glBufferLockManager()
{
    const ScopedLock<Mutex> w_lock(_lock);
    for (const BufferLock& lock : _bufferLocks) {
        GL_API::RegisterSyncDelete(lock._syncObj);
    }
}

bool glBufferLockManager::waitForLockedRange(size_t lockBeginBytes,
                                             size_t lockLength,
                                             const bool blockClient,
                                             const bool quickCheck) {
    OPTICK_EVENT();
    OPTICK_TAG("BlockClient", blockClient);
    OPTICK_TAG("QuickCheck", quickCheck);

    while (blockClient && DriverBusy()) {
        // Random busy work
        GL_API::TryDeleteExpiredSync();
    }

    const BufferRange testRange{lockBeginBytes, lockLength};

    bool error = false;
    ScopedLock<Mutex> w_lock(_lock);
    _swapLocks.resize(0);
    for (const BufferLock& lock : _bufferLocks) {
        switch (lock._state) {
            case BufferLockState::ACTIVE: {
                if (!Overlaps(testRange, lock._range)) {
                    _swapLocks.push_back(lock);
                } else {
                    U8 retryCount = 0u;
                    if (Wait(lock._syncObj, blockClient, quickCheck, retryCount)) {
                        GL_API::RegisterSyncDelete(lock._syncObj);
                        if (retryCount > g_MaxLockWaitRetries - 1) {
                            Console::errorfn("glBufferLockManager: Wait (%p) [%d - %d] %s - %d retries", this, lockBeginBytes, lockLength, blockClient ? "true" : "false", retryCount);
                        }
                    } else if (!quickCheck) {
                        error = true;
                        _swapLocks.push_back(lock);
                        _swapLocks.back()._state = BufferLockState::ERROR;
                    }
                }
            } break;
            case BufferLockState::EXPIRED: {
                GL_API::RegisterSyncDelete(lock._syncObj);
            } break;
            case BufferLockState::ERROR: {
                //DIVIDE_UNEXPECTED_CALL();
                // Something is holding OpenGL in a busy state. Trace down the offending calls and surround them
                // by a DriverBusy(true/false) pair
            } break;
        };
    }

    _bufferLocks.swap(_swapLocks);

    return !error;
}

bool glBufferLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, const U32 frameID) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(lockLength > 0u, "glBufferLockManager::lockRange error: Invalid lock range!");

    const BufferRange testRange{ lockBeginBytes, lockLength };

    ScopedLock<Mutex> w_lock(_lock);

    // This should avoid any lock leaks, since any fences we haven't waited on will be considered "signaled" eventually
    for (BufferLock& lock : _bufferLocks) {
        if (lock._frameID < frameID && frameID - lock._frameID >= g_LockFrameLifetime) {
            lock._state = BufferLockState::EXPIRED;
        }
    }

    // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
    for (BufferLock& lock : _bufferLocks) {
        if (Overlaps(testRange, lock._range) && 
            (lock._state == BufferLockState::ACTIVE ||
             lock._state == BufferLockState::ERROR))
        
        {
            GL_API::RegisterSyncDelete(lock._syncObj);

            lock._range._startOffset = std::min(testRange._startOffset, lock._range._startOffset);
            lock._range._length = std::max(testRange._length, lock._range._length);
            lock._frameID = frameID;
            lock._syncObj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            return true;
        }
    }

    // No luck with our reuse search. Add a new lock.
    _bufferLocks.emplace_back(
        testRange,
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0),
        frameID
    );

    return true;
}

};
