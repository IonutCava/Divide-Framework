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
#ifndef _PUSH_CONSTANT_INL_
#define _PUSH_CONSTANT_INL_

namespace Divide {
namespace GFX {
    template<typename T>
    PushConstant::PushConstant(const U64 bindingHash, const PushConstantType type, const T& data)
        : PushConstant(bindingHash, type, &data, 1)
    {
    }

    template<typename T>
    PushConstant::PushConstant(const U64 bindingHash, const PushConstantType type, const T* data, const size_t count)
        : _bindingHash(bindingHash),
            _type(type)
    {
        set(data, count);
    }

    template<typename T>
    void PushConstant::set(const T* data, const size_t count) {
        if (count > 0u) {
            const size_t bufferSize = count * sizeof(T);
            if (_buffer.size() < bufferSize) {
                _buffer.resize(bufferSize);
            }
            std::memcpy(_buffer.data(), data, bufferSize);
            dataSize(bufferSize);
        } else {
            _buffer.resize(0);
            dataSize(0u);
        }
    }

    [[nodiscard]] inline const Byte* PushConstant::data() const noexcept { return _buffer.data(); }

    inline bool PushConstant::operator==(const PushConstant& rhs) const {
        return type() == rhs.type() &&
            bindingHash() == rhs.bindingHash() &&
            dataSize() == rhs.dataSize() &&
            _buffer == rhs._buffer;
    }

    inline bool PushConstant::operator!=(const PushConstant& rhs) const {
        return type() != rhs.type() ||
               bindingHash() != rhs.bindingHash() ||
               dataSize() != rhs.dataSize() ||
               _buffer != rhs._buffer;
    }

    template <>
    inline void PushConstant::set<bool>(const bool* data, const size_t count) {
        assert(data != nullptr);

        if (count == 0) {
            _buffer.resize(0);
            dataSize(0u);
        } else if (count == 1) {
            //fast path
            const U32 value = *data ? 1 : 0;
            set(&value, 1);
        } else {
            //Slooow. Avoid using in the rendering loop. Try caching
            vector<U32> temp(count);
            std::transform(data, data + count, std::back_inserter(temp), [](const bool e) noexcept { return e ? 1u : 0u; });
            set(temp.data(), count);
        }
    }
} //namespace GFX
} //namespace Divide
#endif //_PUSH_CONSTANT_INL_
