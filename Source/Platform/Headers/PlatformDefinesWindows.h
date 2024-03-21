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

#ifndef DVD_PLATFORM_DEFINES_WINDOWS_H_
#define DVD_PLATFORM_DEFINES_WINDOWS_H_

#define NOGDI

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif //_CRT_SECURE_NO_WARNINGS

#ifndef _SCL_SECURE
#define _SCL_SECURE  0
#endif //_SCL_SECURE

/// Reduce Build time on Windows Platform
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif  // WIN32_LEAN_AND_MEAN

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif  //VC_EXTRALEAN

#ifndef NOMINMAX
#define NOMINMAX
#endif  //NOMINMAX

#ifndef _RESTRICT_
#define _RESTRICT_ __restrict
#endif

#ifndef NOINITVTABLE
#define NOINITVTABLE __declspec(novtable)
#endif  //NOINITVTABLE

#ifndef FORCE_INLINE
#define FORCE_INLINE __forceinline
#endif //FORCE_INLINE

#ifndef NO_INLINE
#define NO_INLINE __declspec(noinline)
#endif //NO_INLINE


#include <Windows.h>

#if defined(DELETE)
#undef DELETE
#endif //DELETE

#if defined(_WIN64)
#define WIN64
#else
#define WIN32
#endif

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

LRESULT DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;

namespace Divide {
    struct WindowHandle {
        HWND _handle = nullptr;
    };
}; //namespace Divide


#endif //DVD_PLATFORM_DEFINES_WINDOWS_H_
