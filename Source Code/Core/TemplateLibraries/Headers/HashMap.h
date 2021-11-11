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
#ifndef _HASH_MAP_H_
#define _HASH_MAP_H_

#include <EASTL/unordered_map.h>
#include <EASTL/intrusive_hash_map.h>

namespace Divide {

    template<class T>
    struct EnumHash;

    template <typename Key>
    using HashType = EnumHash<Key>;
    namespace hashAlg = eastl;


    template <typename K, typename V, typename HashFun = Divide::HashType<K>, typename Predicate = eastl::equal_to<K>>
    using hashMap = hashAlg::unordered_map<K, V, HashFun, Predicate>;
    template <typename K, typename V, typename HashFun = Divide::HashType<K>, typename Predicate = eastl::equal_to<K>>
    using hashPairReturn = hashAlg::pair<typename hashMap<K, V, HashFun, Predicate>::iterator, bool>;

    template <typename K, typename V>
    using hashMapIntrusive = hashAlg::intrusive_hash_map<K, V, 37>;

    template<class T, bool>
    struct hasher {
        size_t operator() (const T& elem) {
            return hashAlg::hash<T>()(elem);
        }
    };

    template<class T>
    struct hasher<T, true> {
        size_t operator() (const T& elem) {
            using EnumType = std::underlying_type_t<T>;
            return hashAlg::hash<EnumType>()(static_cast<EnumType>(elem));
        }
    };

    template<class T>
    struct EnumHash {
        size_t operator()(const T& elem) const {
            return hasher<T, hashAlg::is_enum<T>::value>()(elem);
        }
    };

    template<class T>
    struct NoHash {
        size_t operator()(const T& elem) const noexcept {
            return static_cast<size_t>(elem);
        }
    };

    namespace MemoryManager {

        /// Deletes every element from the map and clears it at the end
        template <typename K, typename V, typename HashFun = hashAlg::hash<K> >
        void DELETE_HASHMAP(hashMap<K, V, HashFun>& map) {
            if (!map.empty()) {
                for (typename hashMap<K, V, HashFun>::value_type iter : map) {
                    log_delete(iter.second);
                    delete iter.second;
                }
                map.clear();
            }
        }

    } //namespace MemoryManager
}; //namespace Divide

namespace eastl {

template <> struct hash<std::string>
{
    size_t operator()(const std::string& x) const noexcept
    {
        const char* p = x.c_str();
        uint32_t c = 0u, result = 2166136261U;   // Intentionally uint32_t instead of size_t, so the behavior is the same regardless of size.
        while ((c = static_cast<uint8_t>(*p++)) != 0)     // cast to unsigned 8 bit.
            result = result * 16777619 ^ c;
        return static_cast<size_t>(result);
    }
};


template <typename K, typename V, typename ... Args, typename HashFun = Divide::HashType<K>, typename Predicate = equal_to<K>>
Divide::hashPairReturn<K, V, HashFun> emplace(Divide::hashMap<K, V, HashFun, Predicate>& map, K key, Args&&... args) {
    return map.try_emplace(key, eastl::forward<Args>(args)...);
}

template <typename K, typename V, typename ... Args, typename HashFun = Divide::HashType<K>, typename Predicate = equal_to<K>>
Divide::hashPairReturn<K, V, HashFun> emplace(Divide::hashMap<K, V, HashFun, Predicate>& map, Args&&... args) {
    return map.emplace(eastl::forward<Args>(args)...);
}

template <typename K, typename V, typename HashFun = Divide::HashType<K>, typename Predicate = equal_to<K>>
Divide::hashPairReturn<K, V, HashFun> insert(Divide::hashMap<K, V, HashFun, Predicate>& map, const pair<K, V>& valuePair) {
    return map.insert(valuePair);
}

template <typename K, typename V, typename HashFun = Divide::HashType<K>, typename Predicate = equal_to<K>>
Divide::hashPairReturn<K, V, HashFun> insert(Divide::hashMap<K, V, HashFun, Predicate>& map, K key, const V& value) {
    return map.emplace(key, value);
}

template <typename K, typename V, typename HashFun = Divide::HashType<K>, typename Predicate = equal_to<K>>
Divide::hashPairReturn<K, V, HashFun> insert(Divide::hashMap<K, V, HashFun, Predicate>& map, K key, V&& value) {
    return map.emplace(key, eastl::move(value));
}

} //namespace hashAlg

#endif //_HASH_MAP_H_
