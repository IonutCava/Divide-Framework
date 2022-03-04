#include "stdafx.h"

#include "Headers/glLockManager.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

constexpr GLuint64 kOneSecondInNanoSeconds = 1000000000;

glLockManager::~glLockManager()
{
    wait(true);
}

void glLockManager::wait(const bool blockClient) {
    OPTICK_EVENT();

    {
        SharedLock<SharedMutex> r_lock(_syncMutex);
        if (_defaultSync == nullptr) {
            return;
        }
    }

    ScopedLock<SharedMutex> w_lock(_syncMutex);
    if (_defaultSync != nullptr) {
        U8 retryCount = 0u;
        Wait(_defaultSync, blockClient, false, retryCount);
        OPTICK_EVENT("Delete Sync");
        glDeleteSync(_defaultSync);
        GL_API::s_fenceSyncCounter -= 1u;
        _defaultSync = nullptr;
    }
}
 
void glLockManager::lock() {
    OPTICK_EVENT();

    ScopedLock<SharedMutex> lock(_syncMutex);
    assert(_defaultSync == nullptr);
    _defaultSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    GL_API::s_fenceSyncCounter += 1u;
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

};//namespace Divide