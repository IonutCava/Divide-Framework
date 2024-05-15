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

#include "PushConstant.h"

namespace Divide {
struct PushConstantsStruct
{
    static constexpr U8 PUSH_MATRIX_COUNT = 2u;

    mat4<F32> data[PUSH_MATRIX_COUNT] = {MAT4_ZERO, MAT4_ZERO};
    bool _set{false};

    [[nodiscard]] static constexpr size_t Size() noexcept { return 2 * sizeof(mat4<F32>); }
    [[nodiscard]] inline const F32* dataPtr() const { return data[0].mat; }

    bool operator==(const PushConstantsStruct& rhs) const = default;
};

struct PushConstants
{
    PushConstants() = default;
    explicit PushConstants(const PushConstantsStruct& pushConstants);
    explicit PushConstants(const GFX::PushConstant& constant);
    explicit PushConstants(GFX::PushConstant&& constant);

    void set(const GFX::PushConstant& constant);

    void set(const PushConstantsStruct& fastData);

    template<typename T> requires (!std::is_same_v<bool, T>)
    void set(U64 bindingHash, PushConstantType type, const T* values, size_t count);

    template<typename T>
    void set(U64 bindingHash, PushConstantType type, const T& value);

    template<typename T> requires (!std::is_same_v<bool, T>)
    void set(U64 bindingHash, PushConstantType type, const vector<T>& values);

    template<typename T, size_t N> requires (!std::is_same_v<bool, T>)
    void set(U64 bindingHash, PushConstantType type, const std::array<T, N>& values);

    void clear() noexcept;
    bool empty() const noexcept;
    void countHint(const size_t count);

    [[nodiscard]] const vector<GFX::PushConstant>& data() const noexcept;
    [[nodiscard]] const PushConstantsStruct& fastData() const noexcept;

private:
    friend bool Merge(PushConstants& lhs, const PushConstants& rhs, bool& partial);
    PushConstantsStruct _fastData{};
    vector<GFX::PushConstant> _data;
};

bool Merge(PushConstants& lhs, const PushConstants& rhs, bool& partial);

}; //namespace Divide

#endif //DVD_PUSH_CONSTANTS_H_

#include "PushConstants.inl"
