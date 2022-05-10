/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _VERTEX_DATA_INTERFACE_H_
#define _VERTEX_DATA_INTERFACE_H_

#include "Core/Headers/ObjectPool.h"

#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace Divide {

struct RenderStagePass;
struct GenericDrawCommand;

struct BufferParams
{
    std::pair<bufferPtr, size_t> _initialData = { nullptr, 0 };
    U32 _elementCount = 0;
    size_t _elementSize = 0;     ///< Buffer primitive size in bytes

    BufferUpdateFrequency _updateFrequency = BufferUpdateFrequency::COUNT;
    BufferUpdateUsage _updateUsage = BufferUpdateUsage::COUNT;
};

enum class BufferLockState : U8 {
    ACTIVE = 0,
    EXPIRED,
    DELETED,
    ERROR
};

struct SyncObject {
    ~SyncObject();
    void reset();

    Mutex _fenceLock;
    std::any _fenceObject;
    U32 _frameID = 0u;
};

FWD_DECLARE_MANAGED_STRUCT(SyncObject);

struct BufferRange {
    size_t _startOffset = 0u;
    size_t _length = 0u;
};

inline bool operator==(const BufferRange& lhs, const BufferRange& rhs) noexcept {
    return lhs._startOffset == rhs._startOffset &&
           lhs._length == rhs._length;
}

inline bool operator!=(const BufferRange& lhs, const BufferRange& rhs) noexcept {
    return lhs._startOffset != rhs._startOffset ||
           lhs._length == rhs._length;
}

[[nodiscard]] inline bool Overlaps(const BufferRange& lhs, const BufferRange& rhs) noexcept {
    return lhs._startOffset < (rhs._startOffset + rhs._length) && rhs._startOffset < (lhs._startOffset + lhs._length);
}

inline void Merge(BufferRange& lhs, const BufferRange& rhs) {
    lhs._startOffset = std::min(lhs._startOffset, rhs._startOffset);
    lhs._length = std::max(lhs._length, rhs._length);
}

struct BufferLockInstance {
    BufferLockInstance() = default;
    explicit BufferLockInstance(const BufferRange range, SyncObject* syncObj) noexcept
        : _range(range), _syncObj(syncObj)
    {
    }

    BufferRange _range{};
    SyncObject* _syncObj = nullptr;
    BufferLockState _state = BufferLockState::ACTIVE;
};

class LockableDataRangeBuffer;
struct BufferLock {
    const LockableDataRangeBuffer* _targetBuffer = nullptr;
    BufferRange _range{};
};

using BufferLocks = eastl::fixed_vector<BufferLock, 3, true, eastl::dvd_allocator>;

class LockableDataRangeBuffer : public GUIDWrapper {
public:
    [[nodiscard]] virtual bool lockByteRange(BufferRange range, SyncObject* sync) const = 0;
};

class NOINITVTABLE VertexDataInterface : public GUIDWrapper, public GraphicsResource {
   public:
    using Handle = PoolHandle;

    explicit VertexDataInterface(GFXDevice& context);
    virtual ~VertexDataInterface();

    virtual void draw(const GenericDrawCommand& command) = 0;

    PROPERTY_R(Handle, handle);
    PROPERTY_RW(bool, primitiveRestartEnabled, false);

    using VDIPool = ObjectPool<VertexDataInterface, 4096>;
    // We only need this pool in order to get a valid handle to pass around to command buffers instead of using raw pointers
    static VDIPool s_VDIPool;
};

};  // namespace Divide


#endif