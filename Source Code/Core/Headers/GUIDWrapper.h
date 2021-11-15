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
#ifndef _GUID_WRAPPER_H_
#define _GUID_WRAPPER_H_

namespace Divide {

/// Utility class that adds basic GUID management to objects
class GUIDWrapper {
   public:
    GUIDWrapper() noexcept : _GUID(generateGUID()) {}
    GUIDWrapper(const GUIDWrapper& old) noexcept : _GUID(generateGUID()) { (void)old; }
    GUIDWrapper(GUIDWrapper&& old) noexcept : _GUID(old._GUID) {}
    virtual ~GUIDWrapper() = default;

    [[nodiscard]] FORCE_INLINE I64 getGUID() const noexcept { return _GUID; }

    static I64 generateGUID() noexcept;

    GUIDWrapper& operator=(const GUIDWrapper& old) = delete;
    GUIDWrapper& operator=(GUIDWrapper&& other) = delete;

   protected:
    const I64 _GUID;
};

}  // namespace Divide

#endif
