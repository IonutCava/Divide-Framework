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
    template<typename T>
    void UniformData::set( const U64 bindingHash, const PushConstantType type, const T& value )
    {
        set( bindingHash, type, &value, 1u);
    }

    template<>
    inline void UniformData::set( const U64 bindingHash, const PushConstantType type, const bool& value )
    {
        const U32 newValue = value ? 1u : 0u;
        set(bindingHash, type, &newValue, 1);
    }

    template<typename T> requires (!std::is_same_v<bool, T>)
    void UniformData::set( const U64 bindingHash, const PushConstantType type, const T* values, const size_t count )
    {
        const size_t dataSize = sizeof(T) * count;

        bool found = false;
        BufferRange range{};

        for ( Entry& entry : _data)
        {
            if ( entry._bindingHash == bindingHash)
            {
                DIVIDE_ASSERT(dataSize <= entry._range._length);
                range = entry._range;
                found = true;
                break;
            }
        }
        if ( !found )
        {
            range = 
            {
                ._startOffset = _buffer.size(),
                ._length = dataSize
            };
            _buffer.insert(_buffer.end(), dataSize, Byte_ZERO);

            _data.emplace_back(bindingHash, range, type);
        }

        std::memcpy(&_buffer[range._startOffset], values, sizeof(T) * count);
    }

} //namespace Divide

#endif //DVD_PUSH_CONSTANTS_INL_
