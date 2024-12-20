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
#ifndef DVD_TEMPLATE_ALLOCATOR_H_
#define DVD_TEMPLATE_ALLOCATOR_H_

#include <EASTL/internal/config.h>

#if !defined(ENABLE_MIMALLOC)

template<typename T>
class dvd_allocator : public std::allocator<T>
{
};

#else //ENABLE_MIMALLOC

template<typename T>
using dvd_allocator = mi_stl_allocator<T>;

#if defined(EASTL_USER_DEFINED_ALLOCATOR)
namespace eastl
{
	inline allocator::allocator( const char* EASTL_NAME( pName ) )
	{
#if EASTL_NAME_ENABLED
		mpName = pName ? pName : EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline allocator::allocator( const allocator& EASTL_NAME( alloc ) )
	{
#if EASTL_NAME_ENABLED
		mpName = alloc.mpName;
#endif
	}


	inline allocator::allocator( const allocator&, const char* EASTL_NAME( pName ) )
	{
#if EASTL_NAME_ENABLED
		mpName = pName ? pName : EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline allocator& allocator::operator=( const allocator& EASTL_NAME( alloc ) )
	{
#if EASTL_NAME_ENABLED
		mpName = alloc.mpName;
#endif
		return *this;
	}


	inline const char* allocator::get_name() const
	{
#if EASTL_NAME_ENABLED
		return mpName;
#else
		return EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline void allocator::set_name( const char* EASTL_NAME( pName ) )
	{
#if EASTL_NAME_ENABLED
		mpName = pName;
#endif
	}


	inline void* allocator::allocate( size_t n, [[maybe_unused]] int flags )
	{
#if EASTL_NAME_ENABLED
#define pName mpName
#else
#define pName EASTL_ALLOCATOR_DEFAULT_NAME
#endif
		return mi_new( n );
	}


	inline void* allocator::allocate( size_t n, size_t alignment, [[maybe_unused]] size_t offset, [[maybe_unused]] int flags )
	{
		return mi_new_aligned(n, alignment);
#undef pName  // See above for the definition of this.
	}


	inline void allocator::deallocate( void* p, size_t )
	{
		mi_free(p);
	}


	inline bool operator==( const allocator&, const allocator& )
	{
		return true; // All allocators are considered equal, as they merely use global new/delete.
	}

#if !defined(EA_COMPILER_HAS_THREE_WAY_COMPARISON)
	inline bool operator!=( const allocator&, const allocator& )
	{
		return false; // All allocators are considered equal, as they merely use global new/delete.
	}
#endif

} // namespace eastl
#endif //EASTL_USER_DEFINED_ALLOCATOR
#endif //ENABLE_MIMALLOC

#endif //DVD_TEMPLATE_ALLOCATOR_H_
