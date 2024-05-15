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
#ifndef DVD_PUSH_CONSTANT_INL_
#define DVD_PUSH_CONSTANT_INL_

namespace Divide::GFX
{

    template<typename T> requires (!std::is_same_v<bool, T>)
    PushConstant::PushConstant(const U64 bindingHash, const PushConstantType type, const T& data)
        : PushConstant(bindingHash, type, &data, 1)
    {
    }

    template<typename T> requires (!std::is_same_v<bool, T>)
    PushConstant::PushConstant(const U64 bindingHash, const PushConstantType type, const T* data, const size_t count)
        : _dataSize(sizeof(T) * count)
        , _bindingHash(bindingHash)
        , _type(type)
    {
        DIVIDE_ASSERT( _dataSize > 0u );
        _buffer.resize( _dataSize );
        std::memcpy( _buffer.data(), data, _dataSize );
    }

    [[nodiscard]] inline const Byte* PushConstant::data() const noexcept
    { 
        return _buffer.data();
    }

} //namespace Divide::GFX

#endif //DVD_PUSH_CONSTANT_INL_
