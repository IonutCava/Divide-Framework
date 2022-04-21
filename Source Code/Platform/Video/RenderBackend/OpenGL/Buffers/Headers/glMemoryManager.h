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
    enum class GLMemoryType {
        SHADER_BUFFER = 0u,
        VERTEX_BUFFER,
        INDEX_BUFFER,
        OTHER,
        COUNT
    };

    struct Block
    {
        Byte* _ptr = nullptr;
        size_t _offset = 0u;
        size_t _size = 0u;
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

    [[nodiscard]] ptrdiff_t GetAlignmentCorrected(const ptrdiff_t byteOffset, const size_t alignment) noexcept;

    class Chunk final : NonCopyable, NonMovable
    {
    public:
        explicit Chunk(bool poolAllocations,
                       size_t size,
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

        FORCE_INLINE [[nodiscard]] bool poolAllocations() const noexcept { return _poolAllocations;  }

    protected:
        vector_fast<Block> _blocks;
        Byte* _memory = nullptr;
        const bool _poolAllocations = false;
    };

    class ChunkAllocator final : NonCopyable, NonMovable
    {
    public:
        explicit ChunkAllocator(size_t size) noexcept;

        // if size > mSize, allocate to the next power of 2
        [[nodiscard]] Chunk* allocate(bool poolAllocations,
                                      size_t size,
                                      size_t alignment,
                                      BufferStorageMask storageMask,
                                      MapBufferAccessMask accessMask,
                                      GLenum usage) const;

    private:
        const size_t _size = 0u;
    };

    class DeviceAllocator
    {
    public:
        explicit DeviceAllocator(GLMemoryType memoryType) noexcept;

        void init(size_t size);
        [[nodiscard]] Block allocate(bool poolAllocations,
                                     size_t size,
                                     size_t alignment,
                                     BufferStorageMask storageMask,
                                     MapBufferAccessMask accessMask,
                                     GLenum usage,
                                     const char* blockName,
                                     std::pair<bufferPtr, size_t> initialData);
        void deallocate(const Block &block) const;
        void deallocate();

        FORCE_INLINE [[nodiscard]] GLMemoryType glMemoryType() const noexcept { return _memoryType; }

    private:
        mutable Mutex _chunkAllocatorLock;
        const GLMemoryType _memoryType = GLMemoryType::COUNT;
        eastl::unique_ptr<ChunkAllocator> _chunkAllocator = nullptr;
        vector_fast<Chunk*> _chunks;
    };
} // namespace GLMemory

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