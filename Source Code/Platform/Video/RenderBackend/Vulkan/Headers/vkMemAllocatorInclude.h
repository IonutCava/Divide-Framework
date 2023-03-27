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

#ifndef VMA_HEAVY_ASSERT
#define VMA_HEAVY_ASSERT(expr) Divide::DIVIDE_ASSERT(expr)

#define VMA_DEBUG_LOG(format, ...) do { \
        Divide::Console::printfn(format, ##__VA_ARGS__); \
    } while(false)

#endif //VMA_HEAVY_ASSERT

#ifdef _MSVC_LANG

#pragma warning(push, 4)
#pragma warning(disable: 4127) // conditional expression is constant
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4189) // local variable is initialized but not referenced
#pragma warning(disable: 4324) // structure was padded due to alignment specifier

#endif  // #ifdef _MSVC_LANG

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare" // comparison of unsigned expression < 0 is always false
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#include <vma/vk_mem_alloc.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef _MSVC_LANG
#pragma warning(pop)
#endif
