

#include "Headers/glLockManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Utility/Headers/Localization.h"

using namespace gl;

namespace Divide
{

    constexpr GLuint64 kOneSecondInNanoSeconds = 1000000000;

    glSyncObject::glSyncObject( const U8 flag, const U64 frameIdx )
        : SyncObject(flag, frameIdx)
    {
    }

    glSyncObject::~glSyncObject()
    {
        DIVIDE_ASSERT( _syncObject == nullptr);
    }

    void glSyncObject::reset()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GL_API::DestroyFenceSync( _syncObject );
        SyncObject::reset();
    }

    glLockManager::~glLockManager()
    {
        waitForLockedRange( 0u, SIZE_MAX );
    }


    bool glLockManager::InitLockPoolEntry( BufferLockPoolEntry& entry, const U8 flag, const U64 frameIdx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool ret = false;
        if ( entry._ptr == nullptr )
        {
            entry._ptr = eastl::make_unique<glSyncObject>(flag, frameIdx);
            ret = true;
        }
        else if ( entry._ptr->_frameNumber == SyncObject::INVALID_FRAME_NUMBER )
        {
            glSyncObject* glSync = static_cast<glSyncObject*>(entry._ptr.get());
            DIVIDE_ASSERT( glSync->_syncObject == nullptr );
            glSync->_frameNumber = frameIdx;
            glSync->_flag = flag;
            ret = true;
        }

        if ( ret )
        {
            static_cast<glSyncObject*>(entry._ptr.get())->_syncObject = GL_API::CreateFenceSync();
        }

        return ret;
    }

    bool Wait( GLsync sync, U8& retryCount )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GLuint64 waitTimeout = 0u;
        SyncObjectMask waitFlags = SyncObjectMask::GL_NONE_BIT;
        while ( true )
        {
            PROFILE_SCOPE( "Wait - OnLoop", Profiler::Category::Graphics );
            PROFILE_TAG( "RetryCount", retryCount );

            const GLenum waitRet = glClientWaitSync( sync, waitFlags, waitTimeout );
            if ( waitRet == GL_ALREADY_SIGNALED || waitRet == GL_CONDITION_SATISFIED )
            {
                return true;
            }
            DIVIDE_ASSERT( waitRet != GL_WAIT_FAILED, "glLockManager::wait error: Not sure what to do here. Probably raise an exception or something." );

            if ( retryCount == 1 )
            {
                //ToDo: Do I need this here? -Ionut
                GL_API::QueueFlush();
            }

            // After the first time, need to start flushing, and wait for a looong time.
            waitFlags = SyncObjectMask::GL_SYNC_FLUSH_COMMANDS_BIT;
            waitTimeout = kOneSecondInNanoSeconds;

            if ( ++retryCount > g_MaxLockWaitRetries )
            {
                if ( waitRet != GL_TIMEOUT_EXPIRED ) [[unlikely]]
                {
                    Console::errorfn(LOCALE_STR("ERROR_GL_LOCK_WAIT_TIMEOUT"));
                }

                break;
            }
        }

        return false;
    }

    bool glLockManager::waitForLockedRangeLocked( const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        glSyncObject* glSync = static_cast<glSyncObject*>(sync.get());

        if ( glSync->_syncObject == nullptr ||
             glSync->_frameNumber + Config::MAX_FRAMES_IN_FLIGHT < GL_API::GetStateTracker()._lastSyncedFrameNumber )
        {
            // Lock expired from underneath us
            return true;
        }

        U8 retryCount = 0u;
        if ( Wait( glSync->_syncObject, retryCount ) ) [[likely]]
        {
            if ( retryCount > g_MaxLockWaitRetries - 1 )
            {
                Console::errorfn(LOCALE_STR("ERROR_GL_LOCK_WAIT_RETRY"), testRange._startOffset, testRange._length, retryCount);
            }

            return LockManager::waitForLockedRangeLocked(sync, testRange, lock);
        }

        return false;
    }


};//namespace Divide