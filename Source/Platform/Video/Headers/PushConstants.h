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
#ifndef DVD_PUSH_CONSTANTS_H_
#define DVD_PUSH_CONSTANTS_H_

#include "Platform/Video/Buffers/Headers/BufferRange.h"

namespace Divide
{
struct PushConstantsStruct
{
    mat4<F32> data[2]{ MAT4_NEGATIVE_ONE, MAT4_NEGATIVE_ONE };

    [[nodiscard]] inline bool set() const noexcept
    { 
        return data[0] != MAT4_NEGATIVE_ONE ||
               data[1] != MAT4_NEGATIVE_ONE;
    }

    [[nodiscard]] static constexpr size_t Size() noexcept { return 2 * sizeof(mat4<F32>); }
    [[nodiscard]] inline const F32* dataPtr() const { return data[0].mat; }

    
    bool operator==(const PushConstantsStruct& rhs) const = default;
};

struct UniformData
{
    struct Entry
    {
        U64 _bindingHash{0u};
        BufferRange _range;
        PushConstantType _type{PushConstantType::COUNT};
    };

    using UniformDataContainer = vector<Entry>;

    template<typename T>
    void set(U64 bindingHash, PushConstantType type, const T& value);

    template<typename T> requires (!std::is_same_v<bool, T>)
    void set(U64 bindingHash, PushConstantType type, const T* values, size_t count);

    bool remove( U64 bindingHash );

    [[nodiscard]] const UniformDataContainer& entries() const noexcept;
    [[nodiscard]] const Byte* data( size_t offset ) const noexcept;

private:
    friend bool Merge( UniformData& lhs, UniformData& rhs, bool& partial );

    UniformDataContainer _data;
    eastl::fixed_vector<Byte, 32, true> _buffer;
};

}; //namespace Divide

#endif //DVD_PUSH_CONSTANTS_H_

#include "PushConstants.inl"
