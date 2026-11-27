/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_OBJECT_POOL_H_
#define DVD_OBJECT_POOL_H_

#include "PoolHandle.h"

namespace Divide {

template<typename T, size_t N, bool allowResize>
class ObjectPool {
public:
    /// Holds the pool's exclusive lock so a batch of *Unlocked calls is atomic.
    class ScopedLocker
    {
    public:
        explicit ScopedLocker( ObjectPool& pool ) : _pool( pool ) { _pool.lock(); }
        ~ScopedLocker() { _pool.unlock(); }

        ScopedLocker( const ScopedLocker& ) = delete;
        ScopedLocker& operator=( const ScopedLocker& ) = delete;

    private:
        ObjectPool& _pool;
    };

    /// Holds the pool's shared lock: enough for findUnlocked.
    class ScopedSharedLocker
    {
    public:
        explicit ScopedSharedLocker( const ObjectPool& pool ) : _pool( pool ) { _pool.lockShared(); }
        ~ScopedSharedLocker() { _pool.unlockShared(); }

        ScopedSharedLocker( const ScopedSharedLocker& ) = delete;
        ScopedSharedLocker& operator=( const ScopedSharedLocker& ) = delete;

    private:
        const ObjectPool& _pool;
    };

    ObjectPool();

    template<typename... Args>
    [[nodiscard]] PoolHandle allocate(Args... args);
                        void deallocate(PoolHandle handle);

    [[nodiscard]] PoolHandle registerExisting(T& object);
                        void unregisterExisting(PoolHandle handle);

    [[nodiscard]] T* find(PoolHandle handle) const;

    /// The *Unlocked variants expect the caller to hold the lock (see ScopedLocker / ScopedSharedLocker).
    /// Constructors and destructors of pooled objects run under that lock, so they must not call back into the pool.
    template<typename... Args>
    [[nodiscard]] PoolHandle allocateUnlocked(Args... args);
                        void deallocateUnlocked(PoolHandle handle);

    [[nodiscard]] PoolHandle registerExistingUnlocked(T& object);
    /// Detaches the object from its slot and returns it (nullptr for an invalid handle); the caller owns it from here.
    [[nodiscard]] T* unregisterExistingUnlocked(PoolHandle handle);

    [[nodiscard]] T* findUnlocked(PoolHandle handle) const;

    void lock();
    void unlock();
    void lockShared() const;
    void unlockShared() const;

protected:
    [[nodiscard]] PoolHandle registerExistingInternal( T& object, bool retry );

protected:
    mutable SharedMutex _poolLock;

    fixed_vector<PoolHandle, N, allowResize> _ids{};
    fixed_vector<T*, N, allowResize> _pool{};
};

} //namespace Divide

#endif //DVD_OBJECT_POOL_H_

#include "ObjectPool.inl"
