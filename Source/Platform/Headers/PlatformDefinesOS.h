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
#ifndef DVD_PLATFORM_DEFINES_OS_H_
#define DVD_PLATFORM_DEFINES_OS_H_

#if defined(_WIN32)
#include "PlatformDefinesWindows.h"
#elif defined(__APPLE_CC__) 
#include "PlatformDefinesApple.h"
#else //defined(__linux) || defined (__unix)
#include "PlatformDefinesUnix.h"
#endif

#ifndef STR_CAT
#define STR_CAT(STR1, STR2) STR1 STR2
#endif //STR_CAT

#ifndef TO_STRING
#define TO_STRING_NAME(X) #X
#define TO_STRING(X) TO_STRING_NAME(X)
#endif //TO_STRING

#ifndef DO_PRAGMA
#define DO_PRAGMA(X) _Pragma(TO_STRING_NAME(X))
#endif //DO_PRAGMA

#ifndef DISABLE_MSVC_WARNING_PUSH

#ifdef USING_MSVC

#define DISABLE_MSVC_WARNING_PUSH(WARNING_ID) \
    DO_PRAGMA(warning(push))                  \
    DO_PRAGMA(warning(disable: WARNING_ID))

#define DISABLE_MSVC_WARNING_POP() \
    DO_PRAGMA(warning(pop))

#else //USING_MSVC

#define DISABLE_MSVC_WARNING_PUSH(WARNING_ID)
#define DISABLE_MSVC_WARNING_POP()

#endif//USING_MSVC

#endif //DISABLE_MSVC_WARNING_PUSH

#ifndef DISABLE_CLANG_WARNING_PUSH

#ifdef USING_CLANG

#define DISABLE_CLANG_WARNING_PUSH(WARNING_ID)                    \
    DO_PRAGMA(clang diagnostic push)                              \
    DO_PRAGMA(clang diagnostic ignored STR_CAT("-W", WARNING_ID))

#define DISABLE_CLANG_WARNING_POP() \
    DO_PRAGMA(clang diagnostic pop)

#else //USING_CLANG

#define DISABLE_CLANG_WARNING_PUSH(WARNING_ID)
#define DISABLE_CLANG_WARNING_POP()

#endif //USING_CLANG

#endif //DISABLE_CLANG_WARNING_PUSH

#ifndef DISABLE_GCC_WARNING_PUSH

#ifdef USING_GCC

#define DISABLE_GCC_WARNING_PUSH(WARNING_ID)                    \
    DO_PRAGMA(GCC diagnostic push)                              \
    DO_PRAGMA(GCC diagnostic ignored STR_CAT("-W", WARNING_ID))

#define DISABLE_GCC_WARNING_POP() \
    DO_PRAGMA(GCC diagnostic pop)

#else //USING_GCC

#define DISABLE_GCC_WARNING_PUSH(WARNING_ID)
#define DISABLE_GCC_WARNING_POP()

#endif //USING_GCC

#endif //DISABLE_GCC_WARNING_PUSH

#ifndef DISABLE_NON_MSVC_WARNING_PUSH

#define DISABLE_NON_MSVC_WARNING_PUSH(WARNING_ID) \
    DISABLE_GCC_WARNING_PUSH(WARNING_ID)          \
    DISABLE_CLANG_WARNING_PUSH(WARNING_ID)

#define DISABLE_NON_MSVC_WARNING_POP() \
    DISABLE_CLANG_WARNING_POP()        \
    DISABLE_GCC_WARNING_POP()

#endif //DISABLE_NON_MSVC_WARNING_PUSH

#endif //DVD_PLATFORM_DEFINES_OS_H_