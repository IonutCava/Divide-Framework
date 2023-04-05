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
#ifndef VK_GENERIC_VERTEX_DATA_H
#define VK_GENERIC_VERTEX_DATA_H

#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "vkLockManager.h"

namespace Divide {

class vkGenericVertexData final : public GenericVertexData {
    public:
        vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, bool renderIndirect, const Str256& name);
        ~vkGenericVertexData() = default;

        void reset() override;

        void draw(const GenericDrawCommand& command, VDIUserData* data) noexcept override;

        [[nodiscard]] BufferLock setIndexBuffer( const IndexBuffer& indices ) override;
        [[nodiscard]] BufferLock setBuffer(const SetBufferParams& params) noexcept override;
        [[nodiscard]] BufferLock updateBuffer(U32 buffer, U32 elementCountOffset, U32 elementCountRange, bufferPtr data) noexcept override;

    private:
        void bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig, VkCommandBuffer& cmdBuffer);

    private:
        struct GenericBufferImpl
        {
            vkBufferImpl_uptr _buffer{ nullptr };
            SetBufferParams::BufferBindConfig _bindConfig{};
            size_t _ringSizeFactor{ 1u };
            size_t _elementStride{ 0u };
        };

        struct IndexBufferEntry
        {
            vkBufferImpl_uptr _buffer{ nullptr };
            IndexBuffer _data{};
            size_t _ringSizeFactor{ 1u };
            size_t _bufferSize{ 0u };
        };

        vector<IndexBufferEntry> _idxBuffers;
        vector<GenericBufferImpl> _bufferObjects;
        SharedMutex _idxBufferLock;
    };

} //namespace Divide

#endif //VK_GENERIC_VERTEX_DATA_H