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
#ifndef DVD_GL_MEMORY_MANAGER_H_
#define DVD_GL_MEMORY_MANAGER_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

namespace Divide {
namespace GLUtil {
namespace GLMemory{
    enum class GLMemoryType {
        SHADER_BUFFER = 0u,
        UNIFORM_BUFFER,
        VERTEX_BUFFER,
        INDEX_BUFFER,
        OTHER,
        COUNT
    };

    Byte* GetZeroData(const size_t bufferSize);
    void OnFrameEnd(U64 frameCount);
    U32  TotalBufferCount();

    struct Block
    {
        Byte* _ptr{ nullptr };
        size_t _offset{ 0u };
        size_t _size{ 0u };
        gl46core::GLuint _bufferHandle{ GL_NULL_HANDLE };
        bool _free{ true };
        bool _pooled{ false };
        bool operator==(const Block &rhs) const = default;
    };


    class Chunk final : NonCopyable, NonMovable
    {
    public:
        explicit Chunk(std::string_view chunkName,
                       size_t alignedSize,
                       gl46core::BufferStorageMask storageMask,
                       gl46core::BufferAccessMask accessMask,
                       U32 flags);
        ~Chunk();
                      void deallocate(const Block& block);
        [[nodiscard]] bool allocate(size_t alignedSize, std::pair<bufferPtr, size_t> initialData, Block& blockOut);
        [[nodiscard]] bool containsBlock(const Block &block) const;

        PROPERTY_R_IW( gl46core::BufferStorageMask, storageMask, gl46core::BufferStorageMask::GL_NONE_BIT);
        PROPERTY_R_IW( gl46core::BufferAccessMask, accessMask, gl46core::BufferAccessMask::GL_NONE_BIT);
        PROPERTY_R_IW( U32, flags, 0u);
    protected:
        eastl::list<Block> _blocks;
        Byte* _memory{ nullptr };
    };

    class ChunkAllocator final : NonCopyable, NonMovable
    {
    public:
        explicit ChunkAllocator(size_t size) noexcept;

        // if size > mSize, allocate to the next power of 2
        [[nodiscard]] std::unique_ptr<Chunk> allocate(size_t alignedSize,
                                                      gl46core::BufferStorageMask storageMask,
                                                      gl46core::BufferAccessMask accessMask,
                                                      U32 flags) const;

    private:
        const size_t _size{ 0u };
    };

    FWD_DECLARE_MANAGED_CLASS(ChunkAllocator);

    class DeviceAllocator
    {
    public:
        explicit DeviceAllocator(GLMemoryType memoryType) noexcept;

        void init(size_t size);
        [[nodiscard]] Block allocate(bool poolAllocations,
                                     size_t alignedSize,
                                     gl46core::BufferStorageMask storageMask,
                                     gl46core::BufferAccessMask accessMask,
                                     U32 flags,
                                     std::pair<bufferPtr, size_t> initialData);
        void deallocate(Block &block);
        void deallocate();

        [[nodiscard]] FORCE_INLINE GLMemoryType glMemoryType() const noexcept { return _memoryType; }

    private:
        mutable Mutex _chunkAllocatorLock;
        mutable Mutex _blockAllocatorLock;

        const GLMemoryType _memoryType{ GLMemoryType::COUNT };
        ChunkAllocator_uptr _chunkAllocator{ nullptr };
        vector<std::unique_ptr<Chunk>> _chunks;

        vector<Block> _blocks;
    };
} // namespace GLMemory

void freeBuffer( gl46core::GLuint &bufferId);

void createBuffer( gl46core::GLuint& bufferIdOut,
                   std::string_view name);

void createAndAllocateBuffer( gl46core::GLuint& bufferIdOut, 
                              std::string_view name,
                              gl46core::BufferStorageMask storageMask,
                              size_t alignedSize,
                              std::pair<bufferPtr, size_t> initialData );

void createAndAllocateMappedBuffer( gl46core::GLuint& bufferIdOut,
                                    std::string_view name,
                                    gl46core::BufferStorageMask storageMask,
                                    size_t alignedSize,
                                    std::pair<bufferPtr, size_t> initialData,
                                    gl46core::BufferAccessMask accessMask,
                                    Byte*& ptrOut );


}; //namespace GLUtil
}; //namespace Divide

#endif //DVD_GL_MEMORY_MANAGER_H_
