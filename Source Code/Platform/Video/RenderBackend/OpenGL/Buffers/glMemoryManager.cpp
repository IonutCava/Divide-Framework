#include "stdafx.h"

#include "Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {
namespace GLUtil {

namespace GLMemory {
[[nodiscard]] ptrdiff_t GetAlignmentCorrected(const ptrdiff_t byteOffset, const size_t alignment) noexcept {
    return byteOffset % alignment == 0u
                ? byteOffset
                : ((byteOffset + alignment - 1u) / alignment) * alignment;
}

Chunk::Chunk(const bool poolAllocations,
             const size_t size,
             const size_t alignment,
             const BufferStorageMask storageMask,
             const MapBufferAccessMask accessMask,
             const GLenum usage)
    : _storageMask(storageMask),
      _accessMask(accessMask),
      _usage(usage),
      _alignment(alignment),
      _poolAllocations(poolAllocations)
{
    Block block;
    block._size = GetAlignmentCorrected(size, alignment);// Code for the worst case?

    if (_poolAllocations) {
        static U32 g_bufferIndex = 0u;
        _memory = createAndAllocPersistentBuffer(block._size, storageMask, accessMask, block._bufferHandle, { nullptr, 0u }, Util::StringFormat("DVD_BUFFER_CHUNK_%d", g_bufferIndex++).c_str());
        block._ptr = _memory;
    }

    _blocks.emplace_back(block);
}

Chunk::~Chunk()
{
    if (_poolAllocations) {
        if (_blocks[0]._bufferHandle > 0u) {
            glDeleteBuffers(1, &_blocks[0]._bufferHandle);
        }
    } else {
        for (Block& block : _blocks) {
            if (block._bufferHandle > 0u) {
                glDeleteBuffers(1, &block._bufferHandle);
            }
        }
    }
}

void Chunk::deallocate(const Block &block) {
    Block* const blockIt = eastl::find(begin(_blocks), end(_blocks), block);
    assert(blockIt != cend(_blocks));
    blockIt->_free = true;

    if (!_poolAllocations) {
        if (blockIt->_bufferHandle > 0u) {
            glDeleteBuffers(1, &blockIt->_bufferHandle);
            blockIt->_bufferHandle = 0u;
        }
    }
}

bool Chunk::allocate(const size_t size, const char* name, const std::pair<bufferPtr, size_t> initialData, Block &blockOut) {
    const size_t requestedSize = GetAlignmentCorrected(size, _alignment);

    if (requestedSize > _blocks.back()._size) {
        return false;
    }

    const size_t count = _blocks.size();
    for (size_t i = 0u; i < count; ++i) {
        Block& block = _blocks[i];
        const size_t remainingSize = block._size;

        if (!block._free || remainingSize < requestedSize) {
            continue;
        }

        if (_poolAllocations) {
            if (initialData.second == 0 || initialData.first == nullptr) {
                memset(block._ptr, 0, requestedSize);
            } else {
                memcpy(block._ptr, initialData.first, initialData.second);
                memset(&block._ptr[initialData.second], 0, requestedSize - size);
            }
        } else {
            _memory = createAndAllocPersistentBuffer(requestedSize, storageMask(), accessMask(), block._bufferHandle, initialData, name);
            block._ptr = _memory;
        }

        block._free = false;
        block._size = requestedSize;
        blockOut = block;

        if (remainingSize > requestedSize) {
            Block nextBlock;
            nextBlock._bufferHandle = block._bufferHandle;

            if (_poolAllocations) {
                nextBlock._offset = block._offset + requestedSize;
            }
            nextBlock._size = remainingSize - requestedSize;
            nextBlock._ptr = &_memory[nextBlock._offset];
            
            _blocks.emplace_back(nextBlock);
        }
        return true;
    }

    return false;
}

bool Chunk::containsBlock(const Block &block) const {
    return eastl::find(begin(_blocks), end(_blocks), block) != cend(_blocks);
}

ChunkAllocator::ChunkAllocator(const size_t size) noexcept
    : _size(size)
{
    assert(size > 0u && isPowerOfTwo(size));
}

Chunk* ChunkAllocator::allocate(const bool poolAllocations,
                                const size_t size,
                                const size_t alignment,
                                const BufferStorageMask storageMask,
                                const MapBufferAccessMask accessMask,
                                const GLenum usage) const
{
    const size_t overflowSize = to_size(1) << to_size(std::log2(size) + 1);
    return MemoryManager_NEW Chunk(poolAllocations, (size > _size ? overflowSize : _size), alignment, storageMask, accessMask, usage);
}

DeviceAllocator::DeviceAllocator(const GLMemoryType memoryType) noexcept
    : _memoryType(memoryType)
{
}

void DeviceAllocator::init(const size_t size) {
    deallocate();
    _chunkAllocator = eastl::make_unique<ChunkAllocator>(size);
}

Block DeviceAllocator::allocate(const bool poolAllocations,
                                const size_t size,
                                const size_t alignment,
                                const BufferStorageMask storageMask,
                                const MapBufferAccessMask accessMask,
                                const GLenum usage,
                                const char* blockName,
                                const std::pair<bufferPtr, size_t> initialData)
{
    ScopedLock<Mutex> w_lock(_chunkAllocatorLock);

    Block block;
    for (Chunk* chunk : _chunks) {
        if (chunk->storageMask() == storageMask && 
            chunk->accessMask() == accessMask &&
            chunk->usage() == usage &&
            chunk->alignment() == alignment &&
            chunk->poolAllocations() == poolAllocations)
        {
            if (chunk->allocate(size, blockName, initialData, block)) {
                return block;
            }
        }
    }

    _chunks.emplace_back(_chunkAllocator->allocate(poolAllocations, size, alignment, storageMask, accessMask, usage));
    if(!_chunks.back()->allocate(size, blockName, initialData, block)) {
        DIVIDE_UNEXPECTED_CALL();
    }
    return block;
}

void DeviceAllocator::deallocate(const Block &block) const {
    ScopedLock<Mutex> w_lock(_chunkAllocatorLock);

    for (Chunk* chunk : _chunks) {
        if (chunk->containsBlock(block)) {
            chunk->deallocate(block);
            return;
        }
    }

    DIVIDE_UNEXPECTED_CALL_MSG("DeviceAllocator::deallocate error: unable to deallocate the block");
}

void DeviceAllocator::deallocate() {
    ScopedLock<Mutex> w_lock(_chunkAllocatorLock);
    MemoryManager::DELETE_CONTAINER(_chunks);
}

} // namespace GLMemory

Byte* createAndAllocPersistentBuffer(const size_t bufferSize,
                                     const BufferStorageMask storageMask,
                                     const MapBufferAccessMask accessMask,
                                     GLuint& bufferIdOut,
                                     const std::pair<bufferPtr, size_t> initialData,
                                     const char* name)
{
    glCreateBuffers(1, &bufferIdOut);
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_BUFFER,
                      bufferIdOut,
                      -1,
                      name != nullptr
                           ? name
                           : Util::StringFormat("DVD_PERSISTENT_BUFFER_%d", bufferIdOut).c_str());
    }
    assert(bufferIdOut != 0 && "GLUtil::allocPersistentBuffer error: buffer creation failed");

    glNamedBufferStorage(bufferIdOut, bufferSize, initialData.second >= bufferSize ? initialData.first : nullptr, storageMask);
    Byte* ptr = (Byte*)glMapNamedBufferRange(bufferIdOut, 0, bufferSize, accessMask);
    assert(ptr != nullptr);
    if (initialData.second < bufferSize) {
        if (initialData.second > 0 && initialData.first != nullptr) {
            memcpy(ptr, initialData.first, initialData.second);
            memset(&ptr[initialData.second], 0, bufferSize - initialData.second);
        } else {
            memset(ptr, 0, bufferSize);
        }
    }
    return ptr;
}

void createAndAllocBuffer(const size_t bufferSize,
                          const GLenum usageMask,
                          GLuint& bufferIdOut,
                          const std::pair<bufferPtr, size_t> initialData,
                          const char* name)
{
    glCreateBuffers(1, &bufferIdOut);

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_BUFFER,
                      bufferIdOut,
                      -1,
                      name != nullptr
                           ? name
                           : Util::StringFormat("DVD_GENERAL_BUFFER_%d", bufferIdOut).c_str());
    }

    assert(bufferIdOut != 0 && "GLUtil::allocBuffer error: buffer creation failed");
    glNamedBufferData(bufferIdOut, bufferSize, initialData.second >= bufferSize ? initialData.first : nullptr, usageMask);
    if (initialData.second < bufferSize) {
        const MapBufferAccessMask accessMask = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
        // We don't want undefined, we want zero as a default. Performance considerations be damned!
        Byte* ptr = (Byte*)glMapNamedBufferRange(bufferIdOut, 0, bufferSize, accessMask);
        if (initialData.second > 0u && initialData.first != nullptr) {
            memcpy(ptr, initialData.first, initialData.second);
            memset(ptr + initialData.second, 0, bufferSize - initialData.second);
        } else {
            memset(ptr, 0, bufferSize);
        }
        glUnmapNamedBuffer(bufferIdOut);
    }
}

void freeBuffer(GLuint& bufferId, bufferPtr mappedPtr) {
    if (bufferId > 0) {
        if (mappedPtr != nullptr) {
            [[maybe_unused]] const GLboolean result = glUnmapNamedBuffer(bufferId);
            assert(result != GL_FALSE && "GLUtil::freeBuffer error: buffer unmapping failed");
            mappedPtr = nullptr;
        }
        GL_API::DeleteBuffers(1, &bufferId);
        bufferId = 0;
    }
}

};  // namespace GLUtil
};  // namespace Divide