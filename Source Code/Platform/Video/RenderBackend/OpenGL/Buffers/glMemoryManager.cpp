#include "stdafx.h"

#include "Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {
namespace GLUtil {

namespace GLMemory {
namespace {
    constexpr bool g_useBlockAllocator = false;

    [[nodiscard]] ptrdiff_t GetAlignmentCorrected(const ptrdiff_t byteOffset) noexcept {
        return byteOffset % GL_API::s_UBOffsetAlignment == 0u 
                    ? byteOffset
                    : ((byteOffset + GL_API::s_UBOffsetAlignment - 1u) / GL_API::s_UBOffsetAlignment) * GL_API::s_UBOffsetAlignment;
    }
}

Chunk::Chunk(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, Byte* initialData) 
    : _storageMask(storageMask),
      _accessMask(accessMask),
      _size(GetAlignmentCorrected(size))  // Code for the worst case?
{
    if_constexpr (g_useBlockAllocator) {
        static U32 g_bufferIndex = 0u;
        _ptr = createAndAllocPersistentBuffer(_size, storageMask, accessMask, _bufferHandle, nullptr, Util::StringFormat("DVD_BUFFER_CHUNK_%d", g_bufferIndex++).c_str());
    }

    Block block;
    block._size = _size;
    block._ptr = _ptr;
    block._bufferHandle = _bufferHandle;
    
    _blocks.emplace_back(block);
}

Chunk::~Chunk()
{
    if_constexpr(g_useBlockAllocator) {
        if (_bufferHandle > 0u) {
            glDeleteBuffers(1, &_bufferHandle);
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

    if_constexpr(!g_useBlockAllocator) {
        if (blockIt->_bufferHandle > 0u) {
            glDeleteBuffers(1, &blockIt->_bufferHandle);
            blockIt->_bufferHandle = 0u;
        }
    }
}

bool Chunk::allocate(const size_t size, const char* name, Byte* initialData, Block &blockOut) {
    const size_t requestedSize = GetAlignmentCorrected(size);

    if (requestedSize > _size) {
        return false;
    }

    const size_t count = _blocks.size();
    for (size_t i = 0u; i < count; ++i) {
        Block& block = _blocks[i];
        if (!block._free || block._size < requestedSize) {
            continue;
        }

        if_constexpr(g_useBlockAllocator) {
            if (initialData == nullptr) {
                memset(block._ptr, 0, requestedSize);
            } else {
                memcpy(block._ptr, initialData, size);
                memset(&block._ptr[size], 0, requestedSize - size);
            }
        } else {
            block._ptr = createAndAllocPersistentBuffer(requestedSize, storageMask(), accessMask(), block._bufferHandle, initialData, name);
        }

        block._free = false;
        if (block._size > requestedSize) {
            Block nextBlock;
            nextBlock._free = true;
            nextBlock._offset = g_useBlockAllocator ? block._offset + requestedSize : 0u;
            nextBlock._bufferHandle = _bufferHandle;
            nextBlock._size = block._size - requestedSize;
            nextBlock._ptr = &_ptr[nextBlock._offset];

            block._size = requestedSize;
            blockOut = block;
            _blocks.emplace_back(nextBlock);
        } else {
            blockOut = block;
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
    assert(isPowerOfTwo(size));
}

Chunk* ChunkAllocator::allocate(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, Byte* initialData) const {
    const size_t overflowSize = to_size(1) << to_size(std::log2(size) + 1);
    return MemoryManager_NEW Chunk((size > _size ? overflowSize : _size), storageMask, accessMask, initialData);
}

void DeviceAllocator::init(size_t size) {
    deallocate();

    assert(size > 0u);
    ScopedLock<Mutex> w_lock(_chunkAllocatorLock);
    _chunkAllocator = eastl::make_unique<ChunkAllocator>(size);
}

Block DeviceAllocator::allocate(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, const char* blockName, Byte* initialData) {
    ScopedLock<Mutex> w_lock(_chunkAllocatorLock);

    Block block;
    for (Chunk* chunk : _chunks) {
        if (chunk->storageMask() == storageMask && chunk->accessMask() == accessMask) {
            if (chunk->allocate(size, blockName, initialData, block)) {
                return block;
            }
        }
    }

    _chunks.emplace_back(_chunkAllocator->allocate(size, storageMask, accessMask, initialData));
    if(!_chunks.back()->allocate(size, blockName, initialData, block)) {
        DIVIDE_UNEXPECTED_CALL();
    }
    return block;
}

void DeviceAllocator::deallocate(Block &block) const {
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

static vector<VBO> g_globalVBOs;

U32 VBO::getChunkCountForSize(const size_t sizeInBytes) noexcept {
    return to_U32(std::ceil(to_F32(sizeInBytes) / MAX_VBO_CHUNK_SIZE_BYTES));
}

VBO::VBO()  noexcept
    : _handle(0),
      _usage(GL_NONE),
      _filledManually(false)
{
}

void VBO::freeAll() {
    freeBuffer(_handle);
    _usage = GL_NONE;
}

U32 VBO::handle() const noexcept {
    return _handle;
}

bool VBO::checkChunksAvailability(const size_t offset, const U32 count, U32& chunksUsedTotal) noexcept {
    assert(MAX_VBO_CHUNK_COUNT > offset);

    U32 freeChunkCount = 0;
    chunksUsedTotal = _chunkUsageState[offset].second;
    if (!_chunkUsageState[offset].first) {
        ++freeChunkCount;
        for (U32 j = 1; j < count; ++j) {
            if (_chunkUsageState[offset + j].first) {
                break;
            }

            ++freeChunkCount;
        }
    }
    
    return freeChunkCount >= count;
}

bool VBO::allocateChunks(const U32 count, const GLenum usage, size_t& offsetOut) {
    if (_usage == GL_NONE || _usage == usage) {
        U32 crtOffset = 0u;
        for (U32 i = 0; i < MAX_VBO_CHUNK_COUNT; ++i) {
            if (checkChunksAvailability(i, count, crtOffset)) {
                if (_handle == 0) {
                    createAndAllocBuffer(MAX_VBO_SIZE_BYTES, usage, _handle, nullptr, Util::StringFormat("VBO_CHUNK_%d", i).c_str());
                    _usage = usage;
                }
                offsetOut = i;
                _chunkUsageState[i] = { true, count };
                for (U32 j = 1; j < count; ++j) {
                    _chunkUsageState[to_size(j) + i].first = true;
                    _chunkUsageState[to_size(j) + i].second = count - j;
                }
                return true;
            }
            if (crtOffset > 1) {
                i += crtOffset - 1;
            }
        }
    }

    return false;
}

bool VBO::allocateWhole(const U32 count, const GLenum usage) {
    static U32 idx = 0;

    assert(_handle == 0);
    createAndAllocBuffer(static_cast<size_t>(count) * MAX_VBO_CHUNK_SIZE_BYTES, usage, _handle, nullptr, Util::StringFormat("VBO_WHOLE_CHUNK_%d", idx++).c_str());
    _usage = usage;
    _chunkUsageState.fill(std::make_pair(true, 0));
    _chunkUsageState[0].second = count;
    _filledManually = true;
    return true;
}

void VBO::releaseChunks(const size_t offset) {
    if (_filledManually) {
        _chunkUsageState.fill(std::make_pair(false, 0));
        return;
    }

    assert(offset < MAX_VBO_CHUNK_COUNT);
    assert(_chunkUsageState[offset].second != 0);

    const U32 childCount = _chunkUsageState[offset].second;
    for (size_t i = 0; i < childCount; ++i) {
        auto& [used, count] = _chunkUsageState[i + offset];
        assert(used);
        used = false;
        count = 0;
    }
}

size_t VBO::getMemUsage() noexcept {
    return MAX_VBO_CHUNK_SIZE_BYTES * 
           std::count_if(std::begin(_chunkUsageState),
                         std::end(_chunkUsageState),
                         [](const auto& it) noexcept { return it.first; });
}

bool allocateVBOChunks(const U32 chunkCount, const GLenum usage, GLuint& handleOut, size_t& offsetOut) {
    VBO vbo;
    if (vbo.allocateChunks(chunkCount, usage, offsetOut)) {
        handleOut = vbo.handle();
        g_globalVBOs.push_back(vbo);
        return true;
    }
    return false;
}

bool allocateVBONew(const U32 chunkCount, const GLenum usage, GLuint& handleOut, size_t& offsetOut) {
    VBO vbo;
    if (vbo.allocateWhole(chunkCount, usage)) {
        offsetOut = 0;
        handleOut = vbo.handle();
        g_globalVBOs.push_back(vbo);
        return true;
    }
    return false;
}

bool commitVBO(const U32 chunkCount, const GLenum usage, GLuint& handleOut, size_t& offsetOut) {
    if (chunkCount > VBO::MAX_VBO_CHUNK_COUNT) {
        return allocateVBONew(chunkCount, usage, handleOut, offsetOut);
    }

    for (VBO& vbo : g_globalVBOs) {
        if (vbo.allocateChunks(chunkCount, usage, offsetOut)) {
            handleOut = vbo.handle();
            return true;
        }
    }

    return allocateVBOChunks(chunkCount, usage, handleOut, offsetOut);
}

bool releaseVBO(GLuint& handle, size_t& offset) {
    for (VBO& vbo : g_globalVBOs) {
        if (vbo.handle() == handle) {
            vbo.releaseChunks(offset);
            handle = 0;
            offset = 0;
            return true;
        }
    }

    return false;
}

size_t getVBOMemUsage(const GLuint handle) noexcept {
    for (VBO& vbo : g_globalVBOs) {
        if (vbo.handle() == handle) {
            return vbo.getMemUsage();
        }
    }

    return 0u;
}

U32 getVBOCount() noexcept {
    return to_U32(g_globalVBOs.size());
}

void clearVBOs() noexcept {
    g_globalVBOs.clear();
}

Byte* createAndAllocPersistentBuffer(const size_t bufferSize,
                                     const BufferStorageMask storageMask,
                                     const MapBufferAccessMask accessMask,
                                     GLuint& bufferIdOut,
                                     bufferPtr const data,
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

    glNamedBufferStorage(bufferIdOut, bufferSize, data, storageMask);
    Byte* ptr = (Byte*)glMapNamedBufferRange(bufferIdOut, 0, bufferSize, accessMask);
    assert(ptr != nullptr);
    return ptr;
}

void createAndAllocBuffer(const size_t bufferSize,
                          const GLenum usageMask,
                          GLuint& bufferIdOut,
                          const bufferPtr data,
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
    glNamedBufferData(bufferIdOut, bufferSize, data, usageMask);
}

void freeBuffer(GLuint& bufferId, bufferPtr mappedPtr) {
    if (bufferId > 0) {
        if (mappedPtr != nullptr) {
            [[maybe_unused]] const GLboolean result = glUnmapNamedBuffer(bufferId);
            assert(result != GL_FALSE && "GLUtil::freeBuffer error: buffer unmaping failed");
            mappedPtr = nullptr;
        }
        GL_API::DeleteBuffers(1, &bufferId);
        bufferId = 0;
    }
}

};  // namespace GLUtil
};  // namespace Divide