#include "stdafx.h"

#include "Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {
namespace GLUtil {

namespace GLMemory {
namespace {
    constexpr bool g_useBlockAllocator = false;
}

Chunk::Chunk(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, Byte* initialData) 
    : _storageMask(storageMask),
      _accessMask(accessMask),
      _size(size)
{
    if_constexpr (g_useBlockAllocator) {
        static U32 g_bufferIndex = 0u;

        glCreateBuffers(1, &_bufferHandle);
        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            glObjectLabel(GL_BUFFER,
                _bufferHandle,
                -1,
                Util::StringFormat("DVD_BUFFER_CHUNK_%d", g_bufferIndex++).c_str());
        }
        assert(_bufferHandle != 0 && "GLMemory chunk allocation error: buffer creation failed");
        glNamedBufferStorage(_bufferHandle, size, initialData, storageMask);
        _ptr = (Byte*)glMapNamedBufferRange(_bufferHandle, 0, _size, accessMask);
    } else {
        _ptr = nullptr;
    }

    Block block;
    block._free = true;
    block._offset = 0;
    block._size = size;
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
        const size_t count = _blocks.size();
        for (size_t i = 0; i < count; ++i) {
            if (_blocks[i]._bufferHandle > 0u) {
                glDeleteBuffers(1, &_blocks[i]._bufferHandle);
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
    if (size > _size) {
        return false;
    }

    const size_t count = _blocks.size();
    for (size_t i = 0; i < count; ++i) {
        Block& block = _blocks[i];
        if (block._free) {
            const size_t newSize = block._size;

            if (newSize >= size) {
                block._size = newSize;

                if_constexpr(g_useBlockAllocator) {
                    block._ptr = _ptr + block._offset;
                    std::memcpy(block._ptr, initialData, size);
                } else {
                    block._ptr = createAndAllocPersistentBuffer(size, storageMask(), accessMask(), block._bufferHandle, initialData, name);
                }

                if (block._size == size) {
                    block._free = false;
                    blockOut = block;
                    return true;
                }

                Block nextBlock;
                nextBlock._free = true;
                nextBlock._offset = g_useBlockAllocator ? block._offset + size : 0u;
                nextBlock._bufferHandle = _bufferHandle;
                nextBlock._size = block._size - size;

                _blocks.emplace_back(nextBlock);

                _blocks[i]._size = size;
                _blocks[i]._free = false;
                blockOut = _blocks[i];

                return true;
            }
        }
    }

    return false;
}

bool Chunk::isIn(const Block &block) const {
    return eastl::find(begin(_blocks), end(_blocks), block) != cend(_blocks);
}

bool IsPowerOfTwo(const size_t size) {
    size_t mask = 0u;
    const size_t power = to_size(std::log2(size));

    for (size_t i = 0; i < power; ++i) {
        mask += to_size(1 << i);
    }

    return !(size & mask);
}

ChunkAllocator::ChunkAllocator(const size_t size)
    : _size(size) 
{
    assert(isPowerOfTwo(size));
}

std::unique_ptr<Chunk> ChunkAllocator::allocate(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, Byte* initialData) const {
    const size_t power = to_size(std::log2(size) + 1);
    return std::make_unique<Chunk>((size > _size) ? to_size(1 << power) : _size, storageMask, accessMask, initialData);
}

void DeviceAllocator::init(size_t size) {
    deallocate();

    assert(size > 0u);
    _chunkAllocator = std::make_unique<ChunkAllocator>(size);
}

Block DeviceAllocator::allocate(const size_t size, const BufferStorageMask storageMask, const MapBufferAccessMask accessMask, const char* blockName, Byte* initialData) {
    Block block;
    for (auto &chunk : _chunks) {
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

void DeviceAllocator::deallocate(Block &block) {
    for (auto &chunk : _chunks) {
        if (chunk->isIn(block)) {
            chunk->deallocate(block);
            return;
        }
    }

    DIVIDE_UNEXPECTED_CALL_MSG("DeviceAllocator::deallocate error: unable to deallocate the block");
}

void DeviceAllocator::deallocate() {
    _chunks.clear();
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
    _chunkUsageState.fill(std::make_pair(false, 0));
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
                    _chunkUsageState[j + i].first = true;
                    _chunkUsageState[j + i].second = count - j;
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
                         [](const auto& it) { return it.first; });
}

bool commitVBO(const U32 chunkCount, const GLenum usage, GLuint& handleOut, size_t& offsetOut) {
    if (chunkCount < VBO::MAX_VBO_CHUNK_COUNT) {
        for (VBO& vbo : g_globalVBOs) {
            if (vbo.allocateChunks(chunkCount, usage, offsetOut)) {
                handleOut = vbo.handle();
                return true;
            }
        }

        VBO vbo;
        if (vbo.allocateChunks(chunkCount, usage, offsetOut)) {
            handleOut = vbo.handle();
            g_globalVBOs.push_back(vbo);
            return true;
        }
    } else {
        VBO vbo;
        if (vbo.allocateWhole(chunkCount, usage)) {
            offsetOut = 0;
            handleOut = vbo.handle();
            g_globalVBOs.push_back(vbo);
            return true;
        }
    }

    return false;
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

size_t getVBOMemUsage(const GLuint handle) {
    for (VBO& vbo : g_globalVBOs) {
        if (vbo.handle() == handle) {
            return vbo.getMemUsage();
        }
    }

    return 0;
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
            const GLboolean result = glUnmapNamedBuffer(bufferId);
            assert(result != GL_FALSE && "GLUtil::freeBuffer error: buffer unmaping failed");
            ACKNOWLEDGE_UNUSED(result);
            mappedPtr = nullptr;
        }
        GL_API::DeleteBuffers(1, &bufferId);
        bufferId = 0;
    }
}

};  // namespace GLUtil
};  // namespace Divide