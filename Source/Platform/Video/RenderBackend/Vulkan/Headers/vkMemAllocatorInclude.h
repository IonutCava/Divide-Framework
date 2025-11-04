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
#define VMA_HEAVY_ASSERT(expr) assert(expr)
#endif //VMA_HEAVY_ASSERT

#ifndef VMA_ASSERT
#define VMA_ASSERT(expr) DIVIDE_GPU_ASSERT(expr)
#endif //VMA_ASSERT

#define VMA_DEBUG_LOG(format, ...) do { Divide::Console::printfn(format, ##__VA_ARGS__); } while(false)

DISABLE_MSVC_WARNING_PUSH(4127) // conditional expression is constant
DISABLE_MSVC_WARNING_PUSH(4100) // unreferenced formal parameter
DISABLE_MSVC_WARNING_PUSH(4189) // local variable is initialized but not referenced
DISABLE_MSVC_WARNING_PUSH(4324) // structure was padded due to alignment specifier

DISABLE_NON_MSVC_WARNING_PUSH("tautological-compare") // comparison of unsigned expression < 0 is always fals"
DISABLE_NON_MSVC_WARNING_PUSH("unused-private-field")
DISABLE_NON_MSVC_WARNING_PUSH("unused-parameter")
DISABLE_NON_MSVC_WARNING_PUSH("missing-field-initializers")
DISABLE_NON_MSVC_WARNING_PUSH("nullability-completeness")

#include <vk_mem_alloc.h>

DISABLE_NON_MSVC_WARNING_POP()
DISABLE_NON_MSVC_WARNING_POP()
DISABLE_NON_MSVC_WARNING_POP()
DISABLE_NON_MSVC_WARNING_POP()
DISABLE_NON_MSVC_WARNING_POP()

DISABLE_MSVC_WARNING_POP()
DISABLE_MSVC_WARNING_POP()
DISABLE_MSVC_WARNING_POP()
DISABLE_MSVC_WARNING_POP()