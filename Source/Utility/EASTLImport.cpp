

#include "Platform/Headers/PlatformDefines.h"

void* operator new[](const size_t size, 
                     size_t alignment,
                     [[maybe_unused]] size_t alignmentOffset,
                     [[maybe_unused]] const char* pName,
                     [[maybe_unused]] int flags, 
                     [[maybe_unused]] unsigned int debugFlags,
                     [[maybe_unused]] const char* file,
                     [[maybe_unused]] int line)
{
#if defined(ENABLE_MIMALLOC)
    return mi_new_aligned_nothrow(size, alignment);
#else//ENABLE_MIMALLOC
    return new std::byte[size];
#endif//ENABLE_MIMALLOC
}

void* operator new[](const size_t size,
                     [[maybe_unused]] const char* pName,
                     [[maybe_unused]] int flags,
                     [[maybe_unused]] unsigned int debugFlags,
                     [[maybe_unused]] const char* file,
                     [[maybe_unused]] int line)
{
#if defined(ENABLE_MIMALLOC)
    return mi_new_nothrow(size);
#else//ENABLE_MIMALLOC
    return new std::byte[size];
#endif//ENABLE_MIMALLOC
}

int Vsnprintf8( char* p, size_t n, const char* pFormat, va_list arguments )
{
#ifdef _MSC_VER
    return vsnprintf_s( p, n, _TRUNCATE, pFormat, arguments );
#else
    return vsnprintf( p, n, pFormat, arguments );
#endif
}
