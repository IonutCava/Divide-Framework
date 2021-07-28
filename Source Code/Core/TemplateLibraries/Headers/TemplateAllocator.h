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
#ifndef _TEMPLATE_ALLOCATOR_H_
#define _TEMPLATE_ALLOCATOR_H_

#ifndef EASTL_USER_DEFINED_ALLOCATOR
#define EASTL_USER_DEFINED_ALLOCATOR
#endif //EASTL_USER_DEFINED_ALLOCATOR

#ifndef EASTLAllocatorType
#define EASTLAllocatorType eastl::aligned_allocator
#endif

#ifndef EASTLAllocatorDefault
#define EASTLAllocatorDefault eastl::GetDefaultDvdAllocator
#endif

#include <Allocator/stl_allocator.h>
#include <EASTL/internal/config.h>
#include "Platform/Headers/PlatformMemoryDefines.h"

template <typename Type>
using dvd_allocator = stl_allocator<Type>;

namespace eastl {
    class allocator;

    struct dvd_allocator
    {
        dvd_allocator() = default;
        dvd_allocator(const char* pName) noexcept { (void)pName; }
        dvd_allocator(const dvd_allocator& x, const char* pName) noexcept { (void)x;  (void)pName; }

        [[nodiscard]] void* allocate(const size_t n, int flags = 0) {
            (void)flags;
            return xmalloc(n);
        }

        [[nodiscard]] void* allocate(const size_t n, size_t alignment, size_t offset, int flags = 0) {
            (void)flags; (void)offset; (void)alignment;
            return xmalloc(n);
        }

        void deallocate(void* p, size_t n) {
            (void)n;
            //delete[](char*)p;
            xfree(p);
        }

        [[nodiscard]]
        const char* get_name()                  const noexcept { return "dvd custom eastl allocator"; }
        void  set_name(const char* pName)       noexcept { (void)pName; }
    };


    // All allocators are considered equal, as they merely use global new/delete.
    [[nodiscard]] inline bool operator==(const dvd_allocator& a, const dvd_allocator& b) noexcept {
        (void)a; (void)b;
        return true;
    }

    [[nodiscard]] inline bool operator!=(const dvd_allocator& a, const dvd_allocator& b) noexcept {
        (void)a; (void)b;
        return false;
    }

    struct aligned_allocator
    {
        aligned_allocator() = default;
        aligned_allocator(const char* pName) noexcept { (void)pName; }
        aligned_allocator(const allocator & x, const char* pName) noexcept { (void)x;  (void)pName; }
        aligned_allocator& operator=(const aligned_allocator & EASTL_NAME(x)) = default;

        [[nodiscard]] void* allocate(const size_t n, int flags = 0) {
            (void)flags;
            constexpr size_t defaultAlignment = alignof(void*);
            return malloc_aligned(n, defaultAlignment);
        }

        [[nodiscard]] void* allocate(const size_t n, const size_t alignment, const size_t offset, int flags = 0) {
            (void)flags;
            return malloc_aligned(n, alignment, offset);
        }

        void deallocate(void* p, size_t n) {
            (void)n;
            free_aligned(p);
        }

        [[nodiscard]]
        const char* get_name()                  const noexcept { return "dvd eastl allocator"; }
        void  set_name(const char* pName)       noexcept { (void)pName; }
    };

    // All allocators are considered equal, as they merely use global new/delete.
    [[nodiscard]] inline bool operator==(const aligned_allocator& a, const aligned_allocator& b) noexcept {
        (void)a; (void)b;
        return true;
    }

    [[nodiscard]] inline bool operator!=(const aligned_allocator& a, const aligned_allocator& b) noexcept {
        (void)a; (void)b;
        return false;
    }

    EASTL_API aligned_allocator* GetDefaultDvdAllocator();
    EASTL_API aligned_allocator* SetDefaultAllocator(aligned_allocator* pAllocator);
} //namespace eastl

#endif //_TEMPLATE_ALLOCATOR_H_
