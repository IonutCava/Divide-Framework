#include "stdafx.h"

#include "Headers/glBufferImpl.h"
#include "Headers/glBufferLockManager.h"
#include "Headers/glMemoryManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

glBufferImpl::glBufferImpl(GFXDevice& context, const BufferImplParams& params)
    : glObject(glObjectType::TYPE_BUFFER, context),
      GUIDWrapper(),
      _params(params),
      _context(context)
{
    assert(_params._bufferParams._updateFrequency != BufferUpdateFrequency::RARELY ||
           _params._bufferParams._updateUsage == BufferUpdateUsage::GPU_R_GPU_W ||
           (_params._bufferParams._initialData.second > 0 && _params._bufferParams._initialData.first != nullptr));

    // We can't use persistent mapping with ONCE usage because we use block allocator for memory and it may have been mapped using write bits and we wouldn't know.
    // Since we don't need to keep writing to the buffer, we can just use a regular glBufferData call once and be done with it.
    const bool usePersistentMapping = _params._bufferParams._updateUsage != BufferUpdateUsage::GPU_R_GPU_W &&
                                      _params._bufferParams._updateFrequency != BufferUpdateFrequency::RARELY;

    // Create all buffers with zero mem and then write the actual data that we have (If we want to initialise all memory)
    if (!usePersistentMapping) {
        _params._bufferParams._sync = false;
        const GLenum usage = _params._target == GL_ATOMIC_COUNTER_BUFFER ? GL_STREAM_READ : GetBufferUsage(_params._bufferParams._updateFrequency, _params._bufferParams._updateUsage);
        GLUtil::createAndAllocBuffer(_params._dataSize, 
                                     usage,
                                     _memoryBlock._bufferHandle,
                                     _params._bufferParams._initialData,
                                     _params._name);

        _memoryBlock._offset = 0u;
        _memoryBlock._size = _params._dataSize;
        _memoryBlock._free = false;
    } else {
        const BufferStorageMask storageMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;
        const MapBufferAccessMask accessMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;

        assert(_params._bufferParams._updateFrequency != BufferUpdateFrequency::COUNT &&
               _params._bufferParams._updateFrequency != BufferUpdateFrequency::RARELY);
  
        const size_t alignment = params._target == GL_UNIFORM_BUFFER 
                                                ? GFXDevice::GetDeviceInformation()._UBOffsetAlignmentBytes
                                                : _params._target == GL_SHADER_STORAGE_BUFFER
                                                                  ? GFXDevice::GetDeviceInformation()._SSBOffsetAlignmentBytes
                                                                  : sizeof(U32);

        GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator(GL_API::GetMemoryTypeForUsage(_params._target));
        _memoryBlock = allocator.allocate(_params._useChunkAllocation,
                                          _params._dataSize,
                                          alignment,
                                          storageMask,
                                          accessMask,
                                          params._target,
                                          _params._name,
                                          _params._bufferParams._initialData);
        assert(_memoryBlock._ptr != nullptr && _memoryBlock._size >= _params._dataSize && "PersistentBuffer::Create error: Can't mapped persistent buffer!");
    }
}

glBufferImpl::~glBufferImpl()
{
    if (_memoryBlock._bufferHandle > 0) {
        if (!waitByteRange(0u, _memoryBlock._size, true)) {
            DIVIDE_UNEXPECTED_CALL();
        }

        if (_memoryBlock._ptr != nullptr) {
            const GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator(GL_API::GetMemoryTypeForUsage(_params._target));
            allocator.deallocate(_memoryBlock);
        } else {
            glInvalidateBufferData(_memoryBlock._bufferHandle);
            GLUtil::freeBuffer(_memoryBlock._bufferHandle, nullptr);
        }
    }
}


bool glBufferImpl::lockByteRange(const size_t offsetInBytes, const size_t rangeInBytes, const U32 frameID) {
    if (_params._bufferParams._sync) {
        return _lockManager.lockRange(_memoryBlock._offset + offsetInBytes, rangeInBytes, frameID);
    }

    return true;
}

bool glBufferImpl::waitByteRange(const size_t offsetInBytes, const size_t rangeInBytes, const bool blockClient) {
    if (_params._bufferParams._sync) {
        if (_params._bufferParams._syncAtEndOfCmdBuffer) {
            //DIVIDE_ASSERT(!GL_API::PendingBufferBindRange( BufferLockEntry{ this, offsetInBytes, rangeInBytes }, false));
        }

        return _lockManager.waitForLockedRange(_memoryBlock._offset + offsetInBytes, rangeInBytes, blockClient);
    }

    return true;
}

[[nodiscard]] inline bool Overlaps(const BufferMapRange& lhs, const BufferMapRange& rhs) noexcept {
    return lhs._offset < (rhs._offset + rhs._range) && rhs._offset < (lhs._offset + lhs._range);
}

void glBufferImpl::writeOrClearBytes(const size_t offsetInBytes, const size_t rangeInBytes, const bufferPtr data, const bool zeroMem) {

    OPTICK_EVENT();
    OPTICK_TAG("Mapped", static_cast<bool>(_memoryBlock._ptr != nullptr));
    OPTICK_TAG("Offset", to_U32(offsetInBytes));
    OPTICK_TAG("Range", to_U32(rangeInBytes));
    assert(rangeInBytes > 0);
    assert(offsetInBytes + rangeInBytes <= _memoryBlock._size);

    if (!waitByteRange(offsetInBytes, rangeInBytes, true)) {
        Console::errorfn(Locale::Get(_ID("ERROR_BUFFER_LOCK_MANAGER_WAIT")));
    }

    if (_memoryBlock._ptr != nullptr) {
        if (zeroMem) {
            memset(&_memoryBlock._ptr[offsetInBytes], 0, rangeInBytes);
        } else {
            memcpy(&_memoryBlock._ptr[offsetInBytes], data, rangeInBytes);
        }
    } else {
        if (zeroMem) {
            const MapBufferAccessMask accessMask = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
            Byte* ptr = (Byte*)glMapNamedBufferRange(_memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes, accessMask);
            memset(ptr, 0, rangeInBytes);
            glUnmapNamedBuffer(_memoryBlock._bufferHandle);
        } else {
            glInvalidateBufferSubData(_memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes);
            glNamedBufferSubData(_memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes, data);
        }
    }
}

void glBufferImpl::readBytes(const size_t offsetInBytes, const size_t rangeInBytes, bufferPtr data) {

    if (_params._target == GL_ATOMIC_COUNTER_BUFFER) {
        glMemoryBarrier(MemoryBarrierMask::GL_ATOMIC_COUNTER_BARRIER_BIT);
    }

    if (!waitByteRange(offsetInBytes, rangeInBytes, true)) {
        DIVIDE_UNEXPECTED_CALL();
    }

    if (_memoryBlock._ptr != nullptr) {
        if (_params._target != GL_ATOMIC_COUNTER_BUFFER) {
            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        }
        memcpy(data, _memoryBlock._ptr + offsetInBytes, rangeInBytes);
    } else {
        const Byte* bufferData = (Byte*)glMapNamedBufferRange(_memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes, MapBufferAccessMask::GL_MAP_READ_BIT);
        if (bufferData != nullptr) {
            memcpy(data, bufferData, rangeInBytes);
        }
        glUnmapNamedBuffer(_memoryBlock._bufferHandle);
    }
}

GLenum glBufferImpl::GetBufferUsage(const BufferUpdateFrequency frequency, const BufferUpdateUsage usage) noexcept {
    switch (frequency) {
        case BufferUpdateFrequency::RARELY:
            switch (usage) {
                case BufferUpdateUsage::CPU_W_GPU_R: return GL_STATIC_DRAW;
                case BufferUpdateUsage::CPU_R_GPU_W: return GL_STATIC_READ;
                case BufferUpdateUsage::GPU_R_GPU_W: return GL_STATIC_COPY;
                default: break;
            }
            break;
        case BufferUpdateFrequency::OCASSIONAL:
            switch (usage) {
                case BufferUpdateUsage::CPU_W_GPU_R: return GL_DYNAMIC_DRAW;
                case BufferUpdateUsage::CPU_R_GPU_W: return GL_DYNAMIC_READ;
                case BufferUpdateUsage::GPU_R_GPU_W: return GL_DYNAMIC_COPY;
                default: break;
            }
            break;
        case BufferUpdateFrequency::OFTEN:
            switch (usage) {
                case BufferUpdateUsage::CPU_W_GPU_R: return GL_STREAM_DRAW;
                case BufferUpdateUsage::CPU_R_GPU_W: return GL_STREAM_READ;
                case BufferUpdateUsage::GPU_R_GPU_W: return GL_STREAM_COPY;
                default: break;
            }
            break;
        default: break;
    }

    DIVIDE_UNEXPECTED_CALL();
    return GL_DYNAMIC_DRAW;
}

}; //namespace Divide