#include "stdafx.h"

#include "Headers/glBufferImpl.h"
#include "Headers/glMemoryManager.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

glBufferImpl::glBufferImpl(GFXDevice& context, const BufferImplParams& params, const std::pair<bufferPtr, size_t>& initialData, const char* name)
    : GUIDWrapper(),
      _params(params),
      _context(context)
{
    assert(_params._bufferParams._updateFrequency != BufferUpdateFrequency::COUNT);

    // Create all buffers with zero mem and then write the actual data that we have (If we want to initialise all memory)
    if (_params._bufferParams._updateUsage == BufferUpdateUsage::GPU_R_GPU_W || _params._bufferParams._updateFrequency == BufferUpdateFrequency::ONCE) {
        GLenum usage = GL_NONE;
        switch (_params._bufferParams._updateFrequency) {
            case BufferUpdateFrequency::ONCE:
                switch (_params._bufferParams._updateUsage) {
                    case BufferUpdateUsage::CPU_W_GPU_R: usage = GL_STATIC_DRAW; break;
                    case BufferUpdateUsage::CPU_R_GPU_W: usage = GL_STATIC_READ; break;
                    case BufferUpdateUsage::GPU_R_GPU_W: usage = GL_STATIC_COPY; break;
                    default: break;
                }
                break;
            case BufferUpdateFrequency::OCASSIONAL:
                switch (_params._bufferParams._updateUsage) {
                    case BufferUpdateUsage::CPU_W_GPU_R: usage = GL_DYNAMIC_DRAW; break;
                    case BufferUpdateUsage::CPU_R_GPU_W: usage = GL_DYNAMIC_READ; break;
                    case BufferUpdateUsage::GPU_R_GPU_W: usage = GL_DYNAMIC_COPY; break;
                    default: break;
                }
                break;
            case BufferUpdateFrequency::OFTEN:
                switch (_params._bufferParams._updateUsage) {
                    case BufferUpdateUsage::CPU_W_GPU_R: usage = GL_STREAM_DRAW; break;
                    case BufferUpdateUsage::CPU_R_GPU_W: usage = GL_STREAM_READ; break;
                    case BufferUpdateUsage::GPU_R_GPU_W: usage = GL_STREAM_COPY; break;
                    default: break;
                }
                break;
            default: break;
        }

        DIVIDE_ASSERT(usage != GL_NONE);

        GLUtil::createAndAllocBuffer(_params._dataSize, 
                                     usage,
                                     _memoryBlock._bufferHandle,
                                     initialData,
                                     name);

        _memoryBlock._offset = 0u;
        _memoryBlock._size = _params._dataSize;
        _memoryBlock._free = false;
    } else {
        const BufferStorageMask storageMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;
        const MapBufferAccessMask accessMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;

        const size_t alignment = params._target == GL_UNIFORM_BUFFER 
                                                ? GFXDevice::GetDeviceInformation()._UBOffsetAlignmentBytes
                                                : _params._target == GL_SHADER_STORAGE_BUFFER
                                                                  ? GFXDevice::GetDeviceInformation()._SSBOffsetAlignmentBytes
                                                                  : GFXDevice::GetDeviceInformation()._VBOffsetAlignmentBytes;

        GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator(GL_API::GetMemoryTypeForUsage(_params._target));
        _memoryBlock = allocator.allocate(_params._useChunkAllocation,
                                          _params._dataSize,
                                          alignment,
                                          storageMask,
                                          accessMask,
                                          params._target,
                                          name,
                                          initialData);
        assert(_memoryBlock._ptr != nullptr && _memoryBlock._size >= _params._dataSize && "PersistentBuffer::Create error: Can't mapped persistent buffer!");
    }

    if (!Runtime::isMainThread()) {
        _lockManager.lockRange(0u, _params._dataSize);
    }

    context.getPerformanceMetrics()._bufferVRAMUsage += params._dataSize;
}

glBufferImpl::~glBufferImpl()
{
    if (_memoryBlock._bufferHandle != GLUtil::k_invalidObjectID) {
        if (!waitByteRange(0u, _memoryBlock._size, true)) {
            DIVIDE_UNEXPECTED_CALL();
        }

        if (_memoryBlock._ptr != nullptr) {
            const GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator(GL_API::GetMemoryTypeForUsage(_params._target));
            allocator.deallocate(_memoryBlock);
        } else {
            GLUtil::freeBuffer(_memoryBlock._bufferHandle);
            GLUtil::freeBuffer(_copyBufferTarget);
        }

        _context.getPerformanceMetrics()._bufferVRAMUsage -= _params._dataSize;
        _context.getPerformanceMetrics()._bufferVRAMUsage -= _copyBufferSize;
    }
}

bool glBufferImpl::lockByteRange(const size_t offsetInBytes, const size_t rangeInBytes, SyncObjectHandle sync) {
    if (_memoryBlock._ptr != nullptr) {
        return _lockManager.lockRange(_memoryBlock._offset + offsetInBytes, rangeInBytes, sync);
    }

    return true;
}

bool glBufferImpl::waitByteRange(const size_t offsetInBytes, const size_t rangeInBytes, const bool blockClient) {
    if (_memoryBlock._ptr != nullptr) {
        return _lockManager.waitForLockedRange(_memoryBlock._offset + offsetInBytes, rangeInBytes, blockClient);
    }

    return true;
}

void glBufferImpl::writeOrClearBytes(const size_t offsetInBytes, const size_t rangeInBytes, const bufferPtr data, const bool zeroMem, const bool firstWrite) {

    assert(rangeInBytes > 0u && offsetInBytes + rangeInBytes <= _memoryBlock._size);

    OPTICK_EVENT();
    OPTICK_TAG("Mapped", static_cast<bool>(_memoryBlock._ptr != nullptr));
    OPTICK_TAG("Offset", to_U32(offsetInBytes));
    OPTICK_TAG("Range", to_U32(rangeInBytes));

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
        DIVIDE_ASSERT(zeroMem || firstWrite, "glBufferImpl: trying to write to a buffer create with BufferUpdateFrequency::ONCE");

        Byte* ptr = (Byte*)glMapNamedBufferRange(_memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (zeroMem) {
            memset(ptr, 0, rangeInBytes);
        } else {
            memcpy(ptr, data, rangeInBytes);
        }
        glUnmapNamedBuffer(_memoryBlock._bufferHandle);
    }
}

void glBufferImpl::readBytes(const size_t offsetInBytes, const size_t rangeInBytes, std::pair<bufferPtr, size_t> outData) {

    if (!waitByteRange(offsetInBytes, rangeInBytes, true)) {
        DIVIDE_UNEXPECTED_CALL();
    }

    if (_memoryBlock._ptr != nullptr) {
        memcpy(outData.first,
               _memoryBlock._ptr + offsetInBytes,
               std::min(std::min(rangeInBytes, outData.second), _memoryBlock._size));
    } else {
        if (_copyBufferTarget == GLUtil::k_invalidObjectID || _copyBufferSize < rangeInBytes) {
            GLUtil::freeBuffer(_copyBufferTarget);
            _copyBufferSize = rangeInBytes;
            GLUtil::createAndAllocBuffer(_copyBufferSize,
                                         GL_STREAM_READ,
                                         _copyBufferTarget,
                                         {nullptr, 0u},
                                         Util::StringFormat("COPY_BUFFER_%d", _memoryBlock._bufferHandle).c_str());

            _context.getPerformanceMetrics()._bufferVRAMUsage += _copyBufferSize;
        }
        glCopyNamedBufferSubData(_memoryBlock._bufferHandle, _copyBufferTarget, _memoryBlock._offset + offsetInBytes, 0u, rangeInBytes);

        const Byte* bufferData = (Byte*)glMapNamedBufferRange(_copyBufferTarget, 0u, rangeInBytes, MapBufferAccessMask::GL_MAP_READ_BIT);
        assert(bufferData != nullptr);
        memcpy(outData.first, bufferData, std::min(rangeInBytes, outData.second));
        glUnmapNamedBuffer(_copyBufferTarget);
    }
}

}; //namespace Divide