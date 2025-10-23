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

#ifdef USING_MSVC
#include <intrin.h>
#else // USING_MSVC
#include <x86intrin.h>
#endif // USING_MSVC

#if defined(IS_WINDOWS_BUILD)
#include "PlatformDefinesWindows.h"
#elif defined(IS_MACOS_BUILD) || defined(IS_LINUX_BUILD)
#include "PlatformDefinesUnix.h"
#else
#error "Unknow operating system!"
#endif

#define DIVIDE_USE_FALLBACK_ASSUME_MACRO 1

// Everyone agreed on this one. Yay!
#ifndef RESTRICT
#define RESTRICT __restrict
#endif //RESTRICT

#if defined(__has_cpp_attribute) and __has_cpp_attribute(assume) >= 202207L
#   define DIVIDE_ASSUME(...) [[assume(__VA_ARGS__)]]
#endif //defined(__has_cpp_attribute) and __has_cpp_attribute(assume) >= 202207L

#ifdef USING_MSVC

#ifndef FORCE_INLINE
#define FORCE_INLINE __forceinline
#endif //FORCE_INLINE

#ifndef NO_INLINE
#define NO_INLINE __declspec(noinline)
#endif //NO_INLINE

#ifndef NOINITVTABLE
#define NOINITVTABLE_CLASS(X) class __declspec(novtable) X
#define NOINITVTABLE_STRUCT(X) struct __declspec(novtable) X
#endif  //NOINITVTABLE

#ifndef DIVIDE_ASSUME
#define DIVIDE_ASSUME(...) do { __assume(__VA_ARGS__); } while(0)
#endif //DIVIDE_ASSUME

#else //USING_MSVC

#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif //FORCE_INLINE

#ifndef NO_INLINE
#define NO_INLINE __attribute__((noinline))
#endif //NO_INLINE

#ifdef USING_CLANG

#ifndef NOINITVTABLE
#define NOINITVTABLE_CLASS(X) __declspec(novtable) class X
#define NOINITVTABLE_STRUCT(X) __declspec(novtable) struct X
#endif  //NOINITVTABLE

#ifndef DIVIDE_ASSUME
#define DIVIDE_ASSUME(...) do {  __builtin_assume(__VA_ARGS__); } while(0)
#endif //DIVIDE_ASSUME

#else //USING_CLANG

// GCC does not have this attribute
#ifndef NOINITVTABLE
#define NOINITVTABLE_CLASS(X) class X
#define NOINITVTABLE_STRUCT(X) struct X
#endif  //NOINITVTABLE

#ifndef DIVIDE_ASSUME
#   if defined(__GNUC__) && __GNUC__ >= 13
#       define DIVIDE_ASSUME(...) __attribute__((__assume__(__VA_ARGS__)))
#   else
#       define DIVIDE_ASSUME(...) __builtin_assume(__VA_ARGS__)
#   endif
#endif //DIVIDE_ASSUME

#endif //USING_CLANG

#endif //USING_MSVC

#if !defined(DIVIDE_ASSUME)
#   if DIVIDE_USE_FALLBACK_ASSUME_MACRO
#       include <utility>
#       define DIVIDE_ASSUME(...) do { if (!bool(__VA_ARGS__)) { ::std::unreachable(); } } while(0)
#   else   
#       define DIVIDE_ASSUME(...)
#   endif
#endif // !defined(DIVIDE_ASSUME)

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

#define CHANGE_GCC_WARNING_PUSH(WARNING_ID)                     \
    DO_PRAGMA(GCC diagnostic push)                              \
    DO_PRAGMA(GCC diagnostic warning STR_CAT("-W", WARNING_ID))

#define CHANGE_GCC_WARNING_POP() \
    DO_PRAGMA(GCC diagnostic pop)

#else //USING_GCC

#define DISABLE_GCC_WARNING_PUSH(WARNING_ID)
#define DISABLE_GCC_WARNING_POP()

#define CHANGE_GCC_WARNING_PUSH(WARNING_ID)
#define CHANGE_GCC_WARNING_POP()

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


#if defined(IS_MACOS_BUILD)
#define UNSEQ_STD_SORT(BEGIN, END, CBK) std::sort(BEGIN, END, CBK)
#define UNSEQ_STD_FOR_EACH(BEGIN, END, CBK) std::for_each(BEGIN, END, CBK)
#else// IS_MACOS_BUILD
#define UNSEQ_STD_SORT(BEGIN, END, CBK) std::sort(std::execution::par_unseq, BEGIN, END, CBK)
#define UNSEQ_STD_FOR_EACH(BEGIN, END, CBK) std::for_each(std::execution::par_unseq, BEGIN, END, CBK)
#endif // IS_MACOS_BUILD

#ifndef SDL_WINDOW_SURFACE_VSYNC_ENABLED
#define SDL_WINDOW_SURFACE_VSYNC_ENABLED 1
#endif //SDL_WINDOW_SURFACE_VSYNC_ENABLED

//ref: https://stackoverflow.com/questions/77581378/with-c11-how-can-i-generate-a-warning-message-in-a-manner-that-works-in-gcc
#define EMIT_COMPILER_WARNING_STRINGIFY0(x) #x
#define EMIT_COMPILER_WARNING_STRINGIFY1(x) EMIT_COMPILER_WARNING_STRINGIFY0(x)
#ifdef __GNUC__
#define EMIT_COMPILER_WARNING_COMPOSE(x) GCC warning x
#else
#define EMIT_COMPILER_MESSAGE_PREFACE(type) \
    __FILE__ "(" EMIT_COMPILER_WARNING_STRINGIFY1(__LINE__) "): " type ": "
#define EMIT_COMPILER_WARNING_COMPOSE(x) message(EMIT_COMPILER_MESSAGE_PREFACE("warning C0000") x)
#endif
#define WARNING(x) _Pragma(EMIT_COMPILER_WARNING_STRINGIFY1(EMIT_COMPILER_WARNING_COMPOSE(x)))

#endif //DVD_PLATFORM_DEFINES_OS_H_