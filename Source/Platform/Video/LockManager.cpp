

#include "Headers/LockManager.h"

#include "Headers/GFXDevice.h"
#include "RenderBackend/OpenGL/Headers/glLockManager.h"

namespace Divide
{
    Mutex LockManager::s_bufferLockLock;
    LockManager::BufferLockPool LockManager::s_bufferLockPool;

    SyncObject::SyncObject( const U8 flag, const U64 frameIdx )
        : _frameNumber(frameIdx)
        , _flag(flag)
    {
    }

    SyncObject::~SyncObject()
    {
        reset();
    }

    void SyncObject::reset()
    {
        _frameNumber = INVALID_FRAME_NUMBER;
    }

    BufferLockInstance::BufferLockInstance( const BufferRange& range, const SyncObjectHandle& handle ) noexcept
        : _range( range ),
          _syncObjHandle( handle )
    {
    }

    void LockManager::CleanExpiredSyncObjects( const RenderAPI api, const U64 frameNumber )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        LockGuard<Mutex> r_lock( s_bufferLockLock );
        for ( BufferLockPoolEntry& syncObject : s_bufferLockPool )
        {
            if ( syncObject._ptr->_frameNumber == SyncObject::INVALID_FRAME_NUMBER )
            {
                continue;
            }

            bool reset = false;
            switch (api)
            {
                case RenderAPI::None:
                case RenderAPI::Vulkan:
                {
                    reset = frameNumber - syncObject._ptr->_frameNumber >= Config::MAX_FRAMES_IN_FLIGHT;
                } break;
                case RenderAPI::OpenGL:
                {
                    reset = syncObject._ptr->_frameNumber < frameNumber;
                } break;

                default : DIVIDE_UNEXPECTED_CALL(); break;
            }

            if (reset)
            {
                syncObject._ptr->reset();
            }
        }
    }

    void LockManager::Clear()
    {
        LockGuard<Mutex> r_lock( s_bufferLockLock );
        s_bufferLockPool.clear();
    }

    bool LockManager::InitLockPoolEntry( const RenderAPI api, BufferLockPoolEntry& entry, const U8 flag, const U64 frameIdx )
    {
        switch ( api )
        {
            case RenderAPI::OpenGL:
            {
                return glLockManager::InitLockPoolEntry( entry, flag, frameIdx );
            } break;
            case RenderAPI::Vulkan:
            case RenderAPI::None: 
            {
                PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

                if ( entry._ptr == nullptr )
                {
                    entry._ptr = eastl::make_unique<SyncObject>( flag, frameIdx );
                    return true;
                }
                else if ( entry._ptr->_frameNumber == SyncObject::INVALID_FRAME_NUMBER )
                {
                    entry._ptr->_frameNumber = frameIdx;
                    entry._ptr->_flag = flag;
                    return true;
                }

                return false;
            } break;
        }

        return true;
    }

    SyncObjectHandle LockManager::CreateSyncObjectLocked( const RenderAPI api, const U8 flag )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Attempt reuse
        for ( size_t i = 0u; i < s_bufferLockPool.size(); ++i )
        {
            BufferLockPoolEntry& syncObject = s_bufferLockPool[i];

            if ( InitLockPoolEntry(api, syncObject, flag, GFXDevice::FrameCount() ) )
            {
                return SyncObjectHandle{ i, ++syncObject._generation };
            }
        }

        // We failed once, so create a new object
        BufferLockPoolEntry newEntry{};
        if ( !InitLockPoolEntry( api, newEntry, flag, GFXDevice::FrameCount() ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        s_bufferLockPool.emplace_back( MOV( newEntry ) );
        return SyncObjectHandle{ s_bufferLockPool.size() - 1u, newEntry._generation };
    }

    bool LockManager::waitForLockedRange( const size_t lockBeginBytes, const size_t lockLength )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const BufferRange testRange{ lockBeginBytes, lockLength };

        bool error = false;

        LockGuard<Mutex> w_lock_global( _bufferLockslock );
        efficient_clear( _swapLocks );

        for ( const BufferLockInstance& lock : _bufferLocks )
        {
            DIVIDE_ASSERT( lock._syncObjHandle._id != SyncObjectHandle::INVALID_SYNC_ID );

            LockGuard<Mutex> r_lock( s_bufferLockLock );
            const BufferLockPoolEntry& syncLockInstance = s_bufferLockPool[lock._syncObjHandle._id];
            if ( syncLockInstance._generation != lock._syncObjHandle._generation )
            {
                error = !(syncLockInstance._generation > lock._syncObjHandle._generation);
                DIVIDE_ASSERT( !error );
                continue;
            }

            if ( !Overlaps( testRange, lock._range ) )
            {
                _swapLocks.push_back( lock );
            }
            else
            {
                if ( !waitForLockedRangeLocked( syncLockInstance._ptr, testRange, lock ) )
                {
                    _swapLocks.push_back( lock );
                }
            }
        }

        _bufferLocks.swap( _swapLocks );

        return !error;
    }

    bool LockManager::waitForLockedRangeLocked( const SyncObject_uptr& sync, [[maybe_unused]] const BufferRange& testRange, [[maybe_unused]] const BufferLockInstance& lock )
    {
        sync->reset();
        return true;
    }

    bool LockManager::lockRange( size_t lockBeginBytes, size_t lockLength, SyncObjectHandle syncObj )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( syncObj._id != SyncObjectHandle::INVALID_SYNC_ID && lockLength > 0u, "LockManager::lockRange error: Invalid lock range!" );

        const BufferRange testRange{ lockBeginBytes, lockLength };

        LockGuard<Mutex> w_lock( _bufferLockslock );
        // See if we can reuse an old lock. Ignore the old fence since the new one will guard the same mem region. (Right?)
        for ( BufferLockInstance& lock : _bufferLocks )
        {
            if ( Overlaps( testRange, lock._range ) )
            {
                Merge( lock._range, testRange );
                lock._syncObjHandle = syncObj;
                return true;
            }
        }

        // No luck with our reuse search. Add a new lock.
        _bufferLocks.emplace_back( testRange, syncObj );

        return true;
    }

    SyncObjectHandle LockManager::CreateSyncObject( const RenderAPI api, const U8 flag )
    {
        LockGuard<Mutex> w_lock( s_bufferLockLock );
        return CreateSyncObjectLocked( api, flag );
    }


}; //namespace Divide
