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
#ifndef DVD_OBJECT_POOL_INL_
#define DVD_OBJECT_POOL_INL_

namespace Divide {

template<typename T, size_t N>
T* ObjectPool<T, N>::find(PoolHandle handle) const {
    SharedLock<SharedMutex> r_lock(_poolLock);
    if (_ids[handle._id - 1]._generation == handle._generation) {
        return _pool[handle._id - 1];
    }

    return nullptr;
}

template<typename T, size_t N>
template<typename... Args>
PoolHandle ObjectPool<T, N>::allocate(Args... args) {
    T* obj = new T(FWD(args)...);
    return registerExisting(*obj);
}

template<typename T, size_t N>
void ObjectPool<T, N>::deallocate(const PoolHandle handle) {
    T* obj = find(handle);
    MemoryManager::SAFE_DELETE(obj);
    unregisterExisting(handle);
}

template<typename T, size_t N>
template<typename... Args>
PoolHandle ObjectPool<T, N>::allocate(void* mem, Args... args) {
    T* obj = new(mem) T(FWD(args)...);
    return registerExisting(*obj);
}

template<typename T, size_t N>
void ObjectPool<T, N>::deallocate([[maybe_unused]] void* mem, const PoolHandle handle) {

    T* obj = find(handle);
    if (obj) {
        obj->~T();
    }
    unregisterExisting(handle);
}

template<typename T, size_t N>
PoolHandle ObjectPool<T, N>::registerExisting(T& object) {
    LockGuard<SharedMutex> w_lock(_poolLock);
    for (size_t i = 0; i < N; ++i) {
        PoolHandle& handle = _ids[i];
        if (handle._id == 0) {
            _pool[i] = &object;
            handle._id = to_U16(i) + 1;
            return handle;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return {};
}

template<typename T, size_t N>
void ObjectPool<T, N>::unregisterExisting(const PoolHandle handle) {
    LockGuard<SharedMutex> w_lock(_poolLock);
    PoolHandle& it = _ids[handle._id - 1];
    if (it._generation == handle._generation) {
        _pool[handle._id - 1] = nullptr;
        it._id = 0;
        ++it._generation;
        return;
    }

    DIVIDE_UNEXPECTED_CALL();
}

} //namespace Divide

namespace eastl {
    template <> struct hash<Divide::PoolHandle>
    {
        size_t operator()(const Divide::PoolHandle& x) const noexcept
        {
            size_t h = 17;
            Divide::Util::Hash_combine(h, x._generation, x._id);
            return h;
        }
    };
};

#endif //DVD_OBJECT_POOL_INL_
