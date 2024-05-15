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
#ifndef DVD_PUSH_CONSTANTS_INL_
#define DVD_PUSH_CONSTANTS_INL_

namespace Divide
{
    template<typename T> requires (!std::is_same_v<bool, T>)
    void PushConstants::set(U64 bindingHash, PushConstantType type, const T* values, size_t count)
    {
        for (GFX::PushConstant& constant : _data)
        {
            if (constant.bindingHash() == bindingHash)
            {
                constant = GFX::PushConstant(bindingHash, type, values, count);
                return;
            }
        }

        _data.emplace_back(bindingHash, type, values, count);
    }

    template<typename T>
    void PushConstants::set(U64 bindingHash, PushConstantType type, const T& value)
    {
        set(bindingHash, type, &value, 1);
    }

    template<>
    inline void PushConstants::set( U64 bindingHash, PushConstantType type, const bool& value )
    {
        const U32 newValue = value ? 1u : 0u;
        set(bindingHash, type, &newValue, 1);
    }

    template<typename T> requires (!std::is_same_v<bool, T>)
    void PushConstants::set(U64 bindingHash, PushConstantType type, const vector<T>& values)
    {
        set(bindingHash, type, values.data(), values.size());
    }

    template<typename T, size_t N> requires (!std::is_same_v<bool, T>)
    void PushConstants::set(U64 bindingHash, PushConstantType type, const std::array<T, N>& values)
    {
        set(bindingHash, type, values.data(), N);
    }

} //namespace Divide

#endif //DVD_PUSH_CONSTANTS_INL_
