#include "stdafx.h"

#include "Headers/glBufferImpl.h"
#include "Headers/glMemoryManager.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    glBufferImpl::glBufferImpl( GFXDevice& context, const BufferImplParams& params, const std::pair<bufferPtr, size_t>& initialData, const char* name )
        : _params( params )
        , _context( context )
    {
        assert( params._bufferParams._flags._updateFrequency != BufferUpdateFrequency::COUNT );

        _lockManager = eastl::make_unique<glLockManager>();

        // Create all buffers with zero mem and then write the actual data that we have (If we want to initialise all memory)
        if ( params._bufferParams._flags._updateUsage == BufferUpdateUsage::GPU_TO_GPU || params._bufferParams._flags._updateFrequency == BufferUpdateFrequency::ONCE )
        {
            GLenum usage = GL_NONE;
            switch ( params._bufferParams._flags._updateFrequency )
            {
                case BufferUpdateFrequency::ONCE:
                    switch ( params._bufferParams._flags._updateUsage )
                    {
                        case BufferUpdateUsage::CPU_TO_GPU: usage = GL_STATIC_DRAW; break;
                        case BufferUpdateUsage::GPU_TO_CPU: usage = GL_STATIC_READ; break;
                        case BufferUpdateUsage::GPU_TO_GPU: usage = GL_STATIC_COPY; break;
                        default: break;
                    }
                    break;
                case BufferUpdateFrequency::OCASSIONAL:
                    switch ( params._bufferParams._flags._updateUsage )
                    {
                        case BufferUpdateUsage::CPU_TO_GPU: usage = GL_DYNAMIC_DRAW; break;
                        case BufferUpdateUsage::GPU_TO_CPU: usage = GL_DYNAMIC_READ; break;
                        case BufferUpdateUsage::GPU_TO_GPU: usage = GL_DYNAMIC_COPY; break;
                        default: break;
                    }
                    break;
                case BufferUpdateFrequency::OFTEN:
                    switch ( params._bufferParams._flags._updateUsage )
                    {
                        case BufferUpdateUsage::CPU_TO_GPU: usage = GL_STREAM_DRAW; break;
                        case BufferUpdateUsage::GPU_TO_CPU: usage = GL_STREAM_READ; break;
                        case BufferUpdateUsage::GPU_TO_GPU: usage = GL_STREAM_COPY; break;
                        default: break;
                    }
                    break;
                default: break;
            }

            DIVIDE_ASSERT( usage != GL_NONE );

            GLUtil::createAndAllocBuffer( _params._dataSize,
                                          usage,
                                          _memoryBlock._bufferHandle,
                                          initialData,
                                          name );

            _memoryBlock._offset = 0u;
            _memoryBlock._size = _params._dataSize;
            _memoryBlock._free = false;
        }
        else
        {
            const BufferStorageMask storageMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;
            const MapBufferAccessMask accessMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;

            const size_t alignment = params._target == GL_UNIFORM_BUFFER
                ? GFXDevice::GetDeviceInformation()._UBOffsetAlignmentBytes
                : _params._target == GL_SHADER_STORAGE_BUFFER
                ? GFXDevice::GetDeviceInformation()._SSBOffsetAlignmentBytes
                : GFXDevice::GetDeviceInformation()._VBOffsetAlignmentBytes;

            GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator( GL_API::GetMemoryTypeForUsage( _params._target ) );
            _memoryBlock = allocator.allocate( _params._useChunkAllocation,
                                               _params._dataSize,
                                               alignment,
                                               storageMask,
                                               accessMask,
                                               params._target,
                                               name,
                                               initialData );
            assert( _memoryBlock._ptr != nullptr && _memoryBlock._size >= _params._dataSize && "PersistentBuffer::Create error: Can't mapped persistent buffer!" );
        }

        _isLockable = _memoryBlock._ptr != nullptr;

        if ( !Runtime::isMainThread() )
        {
            auto sync = LockManager::CreateSyncObject( RenderAPI::OpenGL, LockManager::DEFAULT_SYNC_FLAG_SSBO );
            if ( !lockRange( { 0u, _params._dataSize }, sync ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        context.getPerformanceMetrics()._bufferVRAMUsage += params._dataSize;
    }

    glBufferImpl::~glBufferImpl()
    {
        if (!waitForLockedRange({0u, _memoryBlock._size}))
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        if ( _memoryBlock._bufferHandle != GL_NULL_HANDLE )
        {
            if ( _memoryBlock._ptr != nullptr )
            {
                const GLUtil::GLMemory::DeviceAllocator& allocator = GL_API::GetMemoryAllocator( GL_API::GetMemoryTypeForUsage( _params._target ) );
                allocator.deallocate( _memoryBlock );
            }
            else
            {
                GLUtil::freeBuffer( _memoryBlock._bufferHandle );
                GLUtil::freeBuffer( _copyBufferTarget );
            }

            _context.getPerformanceMetrics()._bufferVRAMUsage -= _params._dataSize;
            _context.getPerformanceMetrics()._bufferVRAMUsage -= _copyBufferSize;
        }
    }

    BufferLock glBufferImpl::writeOrClearBytes( const size_t offsetInBytes, const size_t rangeInBytes, const bufferPtr data, const bool firstWrite )
    {
        assert( rangeInBytes > 0u && offsetInBytes + rangeInBytes <= _memoryBlock._size );

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        PROFILE_TAG( "Mapped", static_cast<bool>(_memoryBlock._ptr != nullptr) );
        PROFILE_TAG( "Offset", to_U32( offsetInBytes ) );
        PROFILE_TAG( "Range", to_U32( rangeInBytes ) );

        if ( !waitForLockedRange({offsetInBytes, rangeInBytes} ) ) [[unlikely]]
        {
            Console::errorfn( Locale::Get( _ID( "ERROR_BUFFER_LOCK_MANAGER_WAIT" ) ) );
        }

        BufferLock ret = 
        {
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = this
        };

        LockGuard<Mutex> w_lock( _mapLock );
        if ( _memoryBlock._ptr != nullptr ) [[likely]]
        {
            if ( data == nullptr )
            {
                memset( &_memoryBlock._ptr[offsetInBytes], 0, rangeInBytes );
            }
            else
            {
                memcpy( &_memoryBlock._ptr[offsetInBytes], data, rangeInBytes );
            }

            ret._range = {offsetInBytes, rangeInBytes};
        }
        else
        {
            DIVIDE_ASSERT( data == nullptr || firstWrite, "glBufferImpl: trying to write to a buffer create with BufferUpdateFrequency::ONCE" );

            Byte* ptr = (Byte*)glMapNamedBufferRange( _memoryBlock._bufferHandle, _memoryBlock._offset + offsetInBytes, rangeInBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
            if ( data == nullptr )
            {
                memset( ptr, 0, rangeInBytes );
            }
            else
            {
                memcpy( ptr, data, rangeInBytes );
            }
            glUnmapNamedBuffer( _memoryBlock._bufferHandle );
        }

        return ret;
    }

    void glBufferImpl::readBytes( const size_t offsetInBytes, const size_t rangeInBytes, std::pair<bufferPtr, size_t> outData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( !waitForLockedRange( {offsetInBytes, rangeInBytes} ) ) [[unlikely]]
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        LockGuard<Mutex> w_lock( _mapLock );
        if ( _memoryBlock._ptr != nullptr ) [[likely]]
        {
            DIVIDE_ASSERT(rangeInBytes + offsetInBytes <= _memoryBlock._size );
            memcpy( outData.first, _memoryBlock._ptr + offsetInBytes, rangeInBytes);
        }
        else
        {
            if ( _copyBufferTarget == GL_NULL_HANDLE || _copyBufferSize < rangeInBytes )
            {
                GLUtil::freeBuffer( _copyBufferTarget );
                _copyBufferSize = rangeInBytes;
                GLUtil::createAndAllocBuffer( _copyBufferSize,
                                              GL_STREAM_READ,
                                              _copyBufferTarget,
                                              { nullptr, 0u },
                                              Util::StringFormat( "COPY_BUFFER_%d", _memoryBlock._bufferHandle ).c_str() );

                _context.getPerformanceMetrics()._bufferVRAMUsage += _copyBufferSize;
            }
            glCopyNamedBufferSubData( _memoryBlock._bufferHandle, _copyBufferTarget, _memoryBlock._offset + offsetInBytes, 0u, rangeInBytes );

            const Byte* bufferData = (Byte*)glMapNamedBufferRange( _copyBufferTarget, 0u, rangeInBytes, MapBufferAccessMask::GL_MAP_READ_BIT );
            assert( bufferData != nullptr );
            memcpy( outData.first, bufferData, rangeInBytes );
            glUnmapNamedBuffer( _copyBufferTarget );
        }
    }

}; //namespace Divide