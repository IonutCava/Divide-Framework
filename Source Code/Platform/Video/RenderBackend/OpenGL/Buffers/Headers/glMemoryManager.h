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
#ifndef _GL_MEMORY_MANAGER_H_
#define _GL_MEMORY_MANAGER_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

namespace Divide {
namespace GLUtil {
namespace GLMemory{
    struct Block
    {
        size_t _offset = 0u;
        size_t _size = 0u;
        Byte* _ptr = nullptr;
        GLuint _bufferHandle = 0u;
        bool _free = true;
    };

    inline bool operator==(const Block &lhs, const Block &rhs) noexcept {
        return lhs._bufferHandle == rhs._bufferHandle &&
               lhs._offset == rhs._offset &&
               lhs._size == rhs._size &&
               lhs._free == rhs._free &&
               lhs._ptr == rhs._ptr;
    }

    class Chunk final : NonCopyable, NonMovable
    {
    public:
        explicit Chunk(size_t size, 
                       size_t alignment,
                       BufferStorageMask storageMask,
                       MapBufferAccessMask accessMask,
                       GLenum usage);
        ~Chunk();
                      void deallocate(const Block& block);
        [[nodiscard]] bool allocate(size_t size, const char* name, std::pair<bufferPtr, size_t> initialData, Block& blockOut);
        [[nodiscard]] bool containsBlock(const Block &block) const;

        PROPERTY_RW(BufferStorageMask, storageMask, BufferStorageMask::GL_NONE_BIT);
        PROPERTY_RW(MapBufferAccessMask, accessMask, MapBufferAccessMask::GL_NONE_BIT);
        PROPERTY_RW(GLenum, usage, GL_NONE);
        PROPERTY_RW(size_t, alignment, 0u);

    protected:
        vector_fast<Block> _blocks;
    };

    class ChunkAllocator final : NonCopyable, NonMovable
    {
    public:
        explicit ChunkAllocator(size_t size) noexcept;

        // if size > mSize, allocate to the next power of 2
        [[nodiscard]] Chunk* allocate(size_t size, 
                                      size_t alignment,
                                      BufferStorageMask storageMask,
                                      MapBufferAccessMask accessMask,
                                      GLenum usage) const;

    private:
        size_t _size = 0u;
    };

    class DeviceAllocator
    {
    public:
        void init(size_t size);
        [[nodiscard]] Block allocate(size_t size,
                                     size_t alignment,
                                     BufferStorageMask storageMask,
                                     MapBufferAccessMask accessMask,
                                     GLenum usage,
                                     const char* blockName,
                                     std::pair<bufferPtr, size_t> initialData);
        void deallocate(const Block &block) const;
        void deallocate();

    private:
        mutable Mutex _chunkAllocatorLock;
        eastl::unique_ptr<ChunkAllocator> _chunkAllocator = nullptr;
        vector_fast<Chunk*> _chunks;
    };
} // namespace GLMemory

class VBO final {
public:
    // Allocate VBOs in 16K chunks. This will HIGHLY depend on actual data usage and requires testing.
    static constexpr U32 MAX_VBO_CHUNK_SIZE_BYTES = 16 * 1024;
    // nVidia recommended (years ago) to use up to 4 megs per VBO. Use 16 MEG VBOs :D
    static constexpr U32  MAX_VBO_SIZE_BYTES = 16 * 1024 * 1024;
    // The total number of available chunks per VBO is easy to figure out
    static constexpr U32 MAX_VBO_CHUNK_COUNT = MAX_VBO_SIZE_BYTES / MAX_VBO_CHUNK_SIZE_BYTES;

    static U32 getChunkCountForSize(size_t sizeInBytes) noexcept;

    VBO() noexcept;

    void freeAll();
    [[nodiscard]] U32 handle() const noexcept;
    bool checkChunksAvailability(size_t offset, U32 count, U32& chunksUsedTotal) noexcept;

    bool allocateChunks(U32 count, GLenum usage, size_t& offsetOut);

    bool allocateWhole(U32 count, GLenum usage);

    void releaseChunks(size_t offset);

    size_t getMemUsage() noexcept;

    //keep track of what chunks we are using
    //for each chunk, keep track how many next chunks are also part of the same allocation
    std::array<std::pair<bool, U32>, MAX_VBO_CHUNK_COUNT> _chunkUsageState = create_array<MAX_VBO_CHUNK_COUNT, std::pair<bool, U32>>(std::make_pair(false, 0));

private:
    GLuint _handle;
    GLenum _usage;
    bool   _filledManually;
};

struct AllocationHandle {
    explicit AllocationHandle() noexcept
        : _id(0),
          _offset(0)
    {
    }

    GLuint _id;
    size_t _offset;
};

bool commitVBO(U32 chunkCount, GLenum usage, GLuint& handleOut, size_t& offsetOut);
bool releaseVBO(GLuint& handle, size_t& offset);
size_t getVBOMemUsage(GLuint handle) noexcept;
U32 getVBOCount() noexcept;

void clearVBOs() noexcept;

void createAndAllocBuffer(size_t bufferSize,
                          GLenum usageMask,
                          GLuint& bufferIdOut,
                          std::pair<bufferPtr, size_t> initialData,
                          const char* name = nullptr);

Byte* createAndAllocPersistentBuffer(size_t bufferSize,
                                     BufferStorageMask storageMask,
                                     MapBufferAccessMask accessMask,
                                     GLuint& bufferIdOut,
                                     std::pair<bufferPtr, size_t> initialData,
                                     const char* name = nullptr);

void freeBuffer(GLuint &bufferId, bufferPtr mappedPtr = nullptr);

}; //namespace GLUtil
}; //namespace Divide

#endif //_GL_MEMORY_MANAGER_H_