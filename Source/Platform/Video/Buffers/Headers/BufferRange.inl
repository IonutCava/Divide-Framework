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
#ifndef DVD_BUFFER_RANGE_INL_
#define DVD_BUFFER_RANGE_INL_

namespace Divide {

    template<typename T>  requires std::unsigned_integral<T>
    [[nodiscard]] inline T BufferRange<T>::endOffset() const noexcept
    {
        return _startOffset + _length;
    }

    template<typename T>
    [[nodiscard]] inline bool Overlaps(const BufferRange<T>& lhs, const BufferRange<T>& rhs) noexcept
    {
        return lhs._startOffset < rhs.endOffset() &&
               rhs._startOffset < lhs.endOffset();
    }

    template<typename T>
    inline void Merge(BufferRange<T>& lhs, const BufferRange<T>& rhs) noexcept
    {
        const size_t endOffset = std::max(lhs.endOffset(), rhs.endOffset());
        lhs._startOffset = std::min(lhs._startOffset, rhs._startOffset);
        assert(endOffset > lhs._startOffset);

        lhs._length = endOffset - lhs._startOffset;
    }

} //namespace Divide

#endif //DVD_BUFFER_RANGE_INL_
