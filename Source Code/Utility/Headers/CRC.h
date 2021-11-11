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

/*Code references:
    http://www.cplusplus.com/forum/lounge/27570/
*/


#pragma once
#ifndef UTIL_CRC_H_
#define UTIL_CRC_H_

namespace Divide {
namespace Util {
    class CRC32 final
    {
    public:
        CRC32() = default;
        explicit CRC32(const void* buf, const size_t size)
        {
            Hash(buf, size);
        }

        //=========================================
        /// implicit cast, so that you can do something like foo = CRC(dat,siz);
        operator U32() const  noexcept { return Get(); }

        //=========================================
        /// getting the crc
        [[nodiscard]] U32 Get() const noexcept { return ~mCrc; }

        //=========================================
        // HashBase stuff
        void Reset() noexcept { mCrc = to_U32(~0); }
        void Hash(const void* buf, size_t siz) noexcept;

    private:
        U32 mCrc = to_U32(~0u);
        static U32 mTable[0x100];

    //=========================================
    // internal support
    static U32 Reflect(U32 v, I32 bits) noexcept;
};
}  // namespace Util
}  // namespace Divide

#endif
