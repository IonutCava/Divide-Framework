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
#ifndef DVD_GPU_BUFFER_H_
#define DVD_GPU_BUFFER_H_

#include "BufferLocks.h"
#include "Core/Headers/ObjectPool.h"
#include "Core/Headers/RingBuffer.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h"

namespace Divide {

struct RenderStagePass;
struct GenericDrawCommand;

struct BufferBindConfig
{
    static constexpr size_t INVALID_ELEMENT_STRIDE = SIZE_MAX;
    U16 _bindIdx{ 0u };
    // Settings this to INVALID_ELEMENT_STRIDE will automatically default to elementSize as the stride.
    size_t _elementStride{ INVALID_ELEMENT_STRIDE };

    inline bool operator==(const BufferBindConfig& other) const noexcept = default;
};

NOINITVTABLE_CLASS(GPUBuffer) : public GUIDWrapper, public GraphicsResource, public RingBuffer
{
   public:
      using Handle = PoolHandle;
      static constexpr Handle INVALID_HANDLE{ U16_MAX, 0u };

     static constexpr size_t INVALID_INDEX_OFFSET = SIZE_MAX;

     struct SetBufferParams : BufferParams, BufferBindConfig
     {
         std::pair<bufferPtr, size_t> _initialData{nullptr, 0};
     };

   public:
      explicit GPUBuffer(GFXDevice& context, U16 ringBufferLength, const std::string_view name);
      ~GPUBuffer() override;

      /// When reading and writing to the same buffer, we use a round-robin approach and offset the reading and writing to multiple copies of the data
      [[nodiscard]] virtual BufferLock setBuffer(const SetBufferParams& params);
      [[nodiscard]] virtual BufferLock updateBuffer(U32 elementCountOffset, U32 elementCountRange, bufferPtr data) = 0;

      PROPERTY_RW(BufferBindConfig, bindConfig, {});
      PROPERTY_R_IW(Str<256>, name, 0u);
      PROPERTY_R_IW(size_t, firstIndexOffsetCount, 0u);

      const Handle _handle{ INVALID_HANDLE };

      using GPUBufferPool = ObjectPool<GPUBuffer, 256, true>;
      static GPUBufferPool s_BufferPool;
};

FWD_DECLARE_MANAGED_CLASS(GPUBuffer);


};  // namespace Divide

#endif //DVD_GPU_BUFFER_H_

#include "GPUBuffer.inl"
