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
#ifndef _GENERIC_VERTEX_DATA_H
#define _GENERIC_VERTEX_DATA_H

#include "Core/Headers/RingBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"

/// This class is used to upload generic VB data to the GPU that can be rendered directly or instanced.
/// Use this class to create precise VB data with specific usage (such as particle systems)
/// Use IMPrimitive for on-the-fly geometry creation

namespace Divide {

class NOINITVTABLE GenericVertexData : public VertexDataInterface,
                                       public RingBuffer {
   public:
     struct IndexBuffer {
         bufferPtr data{ nullptr };
         size_t count{ 0u };
         size_t offsetCount{ 0u };
         U8 id{ 0u };
         bool smallIndices{ false };
         bool indicesNeedCast{ false };
         bool dynamic{ false };
     };


     struct SetBufferParams {
         static constexpr size_t INVALID_ELEMENT_STRIDE = std::numeric_limits<size_t>::max();

         struct BufferBindConfig {
             U32 _bufferIdx{ 0u };
             U32 _bindIdx{ 0u };
         };

         BufferParams _bufferParams{};
         BufferBindConfig _bindConfig{};
         size_t _elementStride{ INVALID_ELEMENT_STRIDE };
         std::pair<bufferPtr, size_t> _initialData{nullptr, 0};
         bool _useRingBuffer{ false };
         bool _useChunkAllocation{ true };
         bool _useAutoSyncObjects{ true };
     };

   public:
    GenericVertexData(GFXDevice& context, U32 ringBufferLength, const char* name = nullptr);
    virtual ~GenericVertexData() = default;

    virtual void setIndexBuffer(const IndexBuffer& indices) = 0;

    virtual void reset() = 0; //< Also clears GPU memory

    /// When reading and writing to the same buffer, we use a round-robin approach and
    /// offset the reading and writing to multiple copies of the data
    virtual void setBuffer(const SetBufferParams& params) = 0;
    /// Will manually insert a fence at call site (for buffers that were written to only!) 
    /// and clear any dirty flags associated with that write
    virtual void insertFencesIfNeeded() = 0u;

    virtual void updateBuffer(U32 buffer,
                              U32 elementCountOffset,
                              U32 elementCountRange,
                              bufferPtr data) = 0;

    PROPERTY_RW(bool, renderIndirect, true);

   protected:
    string _name;
};

inline bool AreCompatible(const GenericVertexData::IndexBuffer& lhs, const GenericVertexData::IndexBuffer& rhs) noexcept {
    return lhs.count >= rhs.count &&
           lhs.indicesNeedCast == rhs.indicesNeedCast &&
           lhs.smallIndices == rhs.smallIndices &&
           lhs.dynamic == rhs.dynamic;
}

};  // namespace Divide

#endif
