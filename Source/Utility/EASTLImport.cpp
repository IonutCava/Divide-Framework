

#include "Platform/Headers/PlatformDefines.h"


void* operator new[](const size_t size, 
                     [[maybe_unused]] size_t alignment,
                     [[maybe_unused]] size_t alignmentOffset,
                     [[maybe_unused]] const char* pName,
                     [[maybe_unused]] int flags, 
                     [[maybe_unused]] unsigned int debugFlags,
                     [[maybe_unused]] const char* file,
                     [[maybe_unused]] int line)
{
    // this allocator doesn't support alignment
    assert(alignment == alignof(void*));

    return malloc(size);
}

void* operator new[](const size_t size,
                     [[maybe_unused]] const char* pName,
                     [[maybe_unused]] int flags,
                     [[maybe_unused]] unsigned int debugFlags,
                     [[maybe_unused]] const char* file,
                     [[maybe_unused]] int line)
{
    return malloc(size);
}

int Vsnprintf8(char* pDestination, const size_t n, const char* pFormat, va_list arguments) noexcept {
    return vsnprintf(pDestination, n, pFormat, arguments);
}

namespace eastl {
    /// gDefaultAllocator
    /// Default global allocator instance. 
    EASTL_API aligned_allocator  gDefaultAllocator;
    EASTL_API aligned_allocator* gpDefaultAllocator = &gDefaultAllocator;

    EASTL_API aligned_allocator* GetDefaultDvdAllocator() noexcept {
        return gpDefaultAllocator;
    }

    EASTL_API aligned_allocator* SetDefaultAllocator(aligned_allocator* pAllocator) noexcept {
        aligned_allocator* const pPrevAllocator = gpDefaultAllocator;
        gpDefaultAllocator = pAllocator;
        return pPrevAllocator;
    }
} //namespace eastl
