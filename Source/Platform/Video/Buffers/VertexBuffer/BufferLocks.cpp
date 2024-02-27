

#include "Headers/BufferLocks.h"
#include "Platform/Video/Headers/LockManager.h"

namespace Divide {

    [[nodiscard]] bool IsEmpty(const BufferLocks& locks) noexcept {
        if (locks.empty())
        {
            return true;
        }

        for (auto& it : locks) {
            if ( it._type != BufferSyncUsage::COUNT &&
                 it._buffer != nullptr && 
                 it._range._length > 0u)
            {
                return false;
            }
        }

        return true;
    }

    bool LockableBuffer::lockRange( const BufferRange range, SyncObjectHandle& sync ) const
    {
        if ( _isLockable && range._length > 0u ) [[likely]]
        {
            DIVIDE_ASSERT( _lockManager != nullptr );

            return _lockManager->lockRange( range._startOffset, range._length, sync );
        }

        return true;
    }

    bool LockableBuffer::waitForLockedRange( const BufferRange range ) const
    {
        if ( _isLockable && range._length > 0u ) [[likely]]
        {
            DIVIDE_ASSERT( _lockManager != nullptr );

            return _lockManager->waitForLockedRange( range._startOffset, range._length );
        }

        return true;
    }
} //namespace Divide
