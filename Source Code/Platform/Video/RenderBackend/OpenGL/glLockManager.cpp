#include "stdafx.h"

#include "Headers/glLockManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

constexpr GLuint64 kOneSecondInNanoSeconds = 1000000000;

void glSyncObject::reset() {
    GL_API::DestroyFenceSync(_syncObject);
    SyncObject::reset();
}

glLockManager::~glLockManager()
{
    waitForLockedRange(0u, SIZE_MAX);
}


bool glLockManager::initLockPoolEntry(BufferLockPoolEntry& entry) {
    if (entry._ptr == nullptr) {
        entry._ptr = eastl::make_unique<glSyncObject>();
        static_cast<glSyncObject*>(entry._ptr.get())->_syncObject = GL_API::CreateFenceSync();
        return true;
    } else {
        glSyncObject* glSync = static_cast<glSyncObject*>(entry._ptr.get());
        if (glSync->_syncObject == nullptr) {
            glSync->_syncObject = GL_API::CreateFenceSync();
            return true;
        }
    }

    return false;
}

bool Wait(GLsync sync, U8& retryCount) {
    PROFILE_SCOPE();

    GLuint64 waitTimeout = 0u;
    SyncObjectMask waitFlags = SyncObjectMask::GL_NONE_BIT;
    while (true) {
        PROFILE_SCOPE("Wait - OnLoop");
        PROFILE_TAG("RetryCount", retryCount);

        const GLenum waitRet = glClientWaitSync(sync, waitFlags, waitTimeout);
        if (waitRet == GL_ALREADY_SIGNALED || waitRet == GL_CONDITION_SATISFIED) {
            return true;
        }
        DIVIDE_ASSERT(waitRet != GL_WAIT_FAILED, "glLockManager::wait error: Not sure what to do here. Probably raise an exception or something.");

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

bool glLockManager::waitForLockedRangeLocked(const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock) {
    PROFILE_SCOPE();

    glSyncObject* glSync = static_cast<glSyncObject*>(sync.get());

    if (glSync->_syncObject == nullptr ||
        glSync->_frameNumber < GL_API::GetStateTracker()._lastSyncedFrameNumber)
    {
        // Lock expired from underneath us
        return true;
    }

    U8 retryCount = 0u;
    if (Wait(glSync->_syncObject, retryCount)) {
        glSync->reset();

        if (retryCount > g_MaxLockWaitRetries - 1) {
            Console::errorfn("glLockManager: Wait [%d - %d] %d retries", testRange._startOffset, testRange._length, retryCount);
        }
        return true;
    }

    return false;
}


};//namespace Divide