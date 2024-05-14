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
#ifndef RESOURCE_INL_
#define RESOURCE_INL_

namespace Divide
{

template<typename T>
[[nodiscard]] size_t GetHash( const PropertyDescriptor<T>& descriptor ) noexcept
{
    return 1337;
}

template<typename T>
FORCE_INLINE bool operator==( const PropertyDescriptor<T>& lhs, const PropertyDescriptor<T>& rhs ) noexcept
{
    return GetHash(lhs) == GetHash(rhs);
}

template<typename T>
FORCE_INLINE bool operator!=( const PropertyDescriptor<T>& lhs, const PropertyDescriptor<T>& rhs ) noexcept
{
    return GetHash( lhs ) != GetHash( rhs );
}

template <typename T>
ResourceDescriptor<T>::ResourceDescriptor( const std::string_view resourceName )
    : ResourceDescriptorBase( resourceName )
{
}

template <typename T>
ResourceDescriptor<T>::ResourceDescriptor( const std::string_view resourceName, const PropertyDescriptor<T>& descriptor )
    : ResourceDescriptorBase( resourceName )
    , _propertyDescriptor( descriptor )
{
}

template <typename T>
size_t ResourceDescriptor<T>::getHash() const
{
    _hash = GetHash( _propertyDescriptor );
    Util::Hash_combine( _hash, ResourceDescriptorBase::getHash() );
    return _hash;
}

template<typename T>
FORCE_INLINE bool operator==( const ResourceDescriptor<T>& lhs, const ResourceDescriptor<T>& rhs ) noexcept
{
    return lhs.getHash() == rhs.getHash();
}

template<typename T>
FORCE_INLINE bool operator!=( const ResourceDescriptor<T>& lhs, const ResourceDescriptor<T>& rhs ) noexcept
{
    return lhs.getHash() != rhs.getHash();
}

} //namespace Divide

#endif //RESOURCE_INL_
