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
#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "TemplateAllocator.h"

#include <EASTL/sort.h>
#include <EASTL/vector.h>

template <typename Type, typename Allocator = eastl::aligned_allocator>
using vectorEASTL = eastl::vector<Type, Allocator>;
template <typename Type>
using vectorEASTLFast = eastl::vector<Type, eastl::dvd_allocator>;

template< typename T, typename Pred, typename A>
typename vectorEASTL<T, A>::iterator insert_sorted(vectorEASTL<T, A>& vec, T const& item, Pred&& pred)
{
    return vec.insert(eastl::upper_bound(eastl::begin(vec), eastl::end(vec), item, pred), item);
}

template<typename T, typename A>
void insert_unique(vectorEASTL<T, A>& target, const T& item)
{
    if (eastl::find(eastl::cbegin(target), eastl::cend(target), item) == eastl::cend(target)) {
        target.push_back(item);
    }

}

template<typename T, typename A>
void insert_unique(vectorEASTL<T, A>& target, const vectorEASTL<T>& source)
{
    eastl::for_each(eastl::cbegin(source), eastl::cend(source),
        [&target](T const& item) {
        insert_unique(target, item);
    });
}

template <class T, class Allocator, class Predicate>
bool dvd_erase_if(eastl::vector<T, Allocator>& vec, Predicate&& pred) {
    const size_t size = vec.size();
    erase_if(vec, pred);
    return vec.size() < size;
}

template<typename T, typename A>
void pop_front(vectorEASTL<T, A>& vec)
{
    assert(!vec.empty());
    vec.erase(eastl::begin(vec));
}

template<typename T, typename A>
void unchecked_copy(vectorEASTL<T, A>& dst, const vectorEASTL<T, A>& src)
{
    dst.resize(src.size());
    memcpy(dst.data(), src.data(), src.size() * sizeof(T));
}

template<typename T, typename U, typename A>
vectorEASTL<T, A> convert(const vectorEASTL<U, A>& data) {
    return vector<T, A>(eastl::cbegin(data), eastl::cend(data));
}

template<typename Cont, typename It>
auto ToggleIndices(Cont& cont, It beg, It end) -> decltype(eastl::end(cont))
{
    int helpIndx = 0;
    return eastl::stable_partition(eastl::begin(cont), eastl::end(cont),
        [&](decltype(*eastl::begin(cont)) const & val) -> bool {
            ACKNOWLEDGE_UNUSED(val);
            return eastl::find(beg, end, helpIndx++) == end;
        });
}

template<typename Cont, typename IndCont>
void EraseIndicesSorted(Cont& cont, IndCont& indices) {
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        cont.erase(cont.begin() + *it);
    }
}

template<typename Cont, typename IndCont>
void EraseIndices(Cont& cont, IndCont& indices) {
    eastl::sort(indices.begin(), indices.end());
    EraseIndicesSorted(cont, indices);
}

//ref: https://stackoverflow.com/questions/7571937/how-to-delete-items-from-a-stdvector-given-a-list-of-indices

template<typename T, typename A>
vectorEASTL<T, A> erase_indices(const vectorEASTL<T, A>& data, vectorEASTL<size_t>& indicesToDelete/* can't assume copy elision, don't pass-by-value */) {
    eastl::sort(begin(indicesToDelete), end(indicesToDelete));
    return erase_sorted_indices(data, indicesToDelete);
}

template<typename T, typename A>
vectorEASTL<T, A> erase_sorted_indices(const vectorEASTL<T, A>& data, vectorEASTL<size_t>& indicesToDelete/* can't assume copy elision, don't pass-by-value */)
{
    if (indicesToDelete.empty()) {
        return data;
    }

    vectorEASTL<T, A> ret;
    ret.reserve(data.size() - indicesToDelete.size());

    // new we can assume there is at least 1 element to delete. copy blocks at a time.
    typename vectorEASTL<T, A>::const_iterator itBlockBegin = eastl::cbegin(data);
    for (size_t it : indicesToDelete) {
        typename vectorEASTL<T, A>::const_iterator itBlockEnd = eastl::cbegin(data) + it;
        if (itBlockBegin != itBlockEnd) {
            eastl::copy(itBlockBegin, itBlockEnd, eastl::back_inserter(ret));
        }
        itBlockBegin = itBlockEnd + 1;
    }

    // copy last block.
    if (itBlockBegin != data.end()) {
        eastl::copy(itBlockBegin, eastl::cend(data), eastl::back_inserter(ret));
    }

    return ret;
}

template<typename T, typename A1, typename A2>
vectorEASTL<T, A1> erase_indices(const vectorEASTL<T, A1>& data, vectorEASTL<size_t, A2>& indicesToDelete/* can't assume copy elision, don't pass-by-value */)
{
    eastl::sort(eastl::begin(indicesToDelete), eastl::end(indicesToDelete));
    return erase_sorted_indices(data, indicesToDelete);
}

template<typename T, typename A1, typename A2>
vectorEASTL<T, A1> erase_sorted_indices(const vectorEASTL<T, A1>& data, vectorEASTL<size_t, A2>& indicesToDelete/* can't assume copy elision, don't pass-by-value */)
{
    if (indicesToDelete.empty()) {
        return data;
    }
    assert(indicesToDelete.size() <= data.size());

    vectorEASTL<T, A1> ret;
    ret.reserve(data.size() - indicesToDelete.size());

    // new we can assume there is at least 1 element to delete. copy blocks at a time.
    typename vectorEASTL<T, A1>::const_iterator itBlockBegin = eastl::cbegin(data);
    for (size_t it : indicesToDelete) {
        typename vectorEASTL<T, A1>::const_iterator itBlockEnd = eastl::cbegin(data) + it;
        if (itBlockBegin != itBlockEnd) {
            eastl::copy(itBlockBegin, itBlockEnd, eastl::back_inserter(ret));
        }
        itBlockBegin = itBlockEnd + 1;
    }

    // copy last block.
    if (itBlockBegin != data.end()) {
        eastl::copy(itBlockBegin, eastl::cend(data), eastl::back_inserter(ret));
    }

    return ret;
}
#endif
