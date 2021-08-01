#include "stdafx.h"

#include "Headers/glBufferLockManager.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    // Auto-delete locks older than this number of frames
    constexpr U32 g_LockFrameLifetime = 6u; //(APP->Driver->GPU x2)
};

glBufferLockManager::~glBufferLockManager()
{
    const ScopedLock<Mutex> w_lock(_lock);
    for (const BufferLock& lock : _bufferLocks) {
        glDeleteSync(lock._syncObj);
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
    ScopedLock<Mutex> w_lock(_lock);
    _swapLocks.resize(0);
    for (const BufferLock& lock : _bufferLocks) {
        if (lock._valid && !Overlaps(testRange, lock._range)) {
            _swapLocks.push_back(lock);
        } else {
            U8 retryCount = 0u;
            if (!lock._valid || Wait(lock._syncObj, blockClient, quickCheck, retryCount)) {
                glDeleteSync(lock._syncObj);

                if (retryCount > 4) {
                    Console::errorfn("glBufferLockManager: Wait (%p) [%d - %d] %s - %d retries", this, lockBeginBytes, lockLength, blockClient ? "true" : "false", retryCount);
                }
            } else if (!quickCheck) {
                error = true;
            }
        }
    }

    _bufferLocks.swap(_swapLocks);

    return !error;
}

bool glBufferLockManager::lockRange(const size_t lockBeginBytes, const size_t lockLength, const U32 frameID) {
    OPTICK_EVENT();

    DIVIDE_ASSERT(lockLength > 0u, "glBufferLockManager::lockRange error: Invalid lock range!");

    const BufferRange testRange{ lockBeginBytes, lockLength };

    // Verify old lock entries and merge if needed
    ScopedLock<Mutex> w_lock(_lock);
    for (BufferLock& lock : _bufferLocks) {
        // This should avoid any lock leaks, since any fences we haven't waited on will be considered "signaled" eventually
        if (lock._frameID < frameID && frameID - lock._frameID >= g_LockFrameLifetime) {
            lock._valid = false;
        }

        // See if we can reuse the old lock. Ignore the old fence since the new one will guard the same mem region. Right?
        if (Overlaps(testRange, lock._range) && lock._valid) {
            const GLsync newSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            lock._range._startOffset = std::min(testRange._startOffset, lock._range._startOffset);
            lock._range._length = std::max(testRange._length, lock._range._length);
            lock._frameID = frameID;
            glDeleteSync(lock._syncObj);
            lock._syncObj = newSync;
            return true;
        }
    }

    _bufferLocks.emplace_back(
        testRange,
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0),
        frameID
    );

    return true;
}

};
