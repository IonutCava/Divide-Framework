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
#ifndef _DESCRIPTOR_SETS_INL_
#define _DESCRIPTOR_SETS_INL_

namespace Divide {
    template<typename Item>
    [[nodiscard]] bool Condition(const Item&) noexcept { return true; }
    template<typename Item>
    [[nodiscard]] bool Matches(const Item& lhs, const Item& rhs) noexcept { return lhs == rhs; }
    template<typename Item, typename SearchType>
    [[nodiscard]] bool Matches(const Item&, const SearchType&) noexcept { return false; }

    template<>
    [[nodiscard]] inline bool Condition(const ShaderBufferBinding& entry) noexcept { return entry._elementRange.max > 0u; }
    template<>
    [[nodiscard]] inline bool Matches(const ShaderBufferBinding& lhs, const ShaderBufferBinding& rhs) noexcept { return lhs._binding == rhs._binding; }
    template<>
    [[nodiscard]] inline bool Matches(const ShaderBufferBinding& entry, const ShaderBufferLocation& binding) noexcept { return entry._binding == binding; }
    template<>
    [[nodiscard]] inline bool Matches(const TextureViewEntry& lhs, const TextureViewEntry& rhs) noexcept { return lhs._binding == rhs._binding; }
    template<>
    [[nodiscard]] inline bool Matches(const TextureViewEntry& entry, const U8& binding) noexcept { return entry._binding == binding; }
    template<>
    [[nodiscard]] inline bool Matches(const Image& lhs, const Image& rhs) noexcept { return lhs._binding == rhs._binding; }
    template<>
    [[nodiscard]] inline bool Matches(const Image& entry, const U8& binding) noexcept { return entry._binding == binding; }

    template<typename Item, size_t Count, typename SearchType, bool CanExpand>
    const Item* SetContainerStorage<Item, Count, SearchType, CanExpand>::find(const SearchType search) const {
        for (auto it = begin(_entries); it != end(_entries); ++it) {
            if (Matches(it, search)) {
                return it;
            }
        }
        return nullptr;
    }

    template<typename Item, size_t Count, typename SearchType, bool CanExpand>
    bool SetContainer<Item, Count, SearchType, CanExpand>::add(const Item& entry) {
        if (Condition(entry)) {
            for (auto& it : _entries) {
                if (Matches(it, entry)) {
                    it = entry;
                    return true;
                }
            }

            _entries.push_back(entry);
            return true;
        }

        return false;
    }

    template<typename Item, size_t Count, typename SearchType, bool CanExpand>
    bool SetContainer<Item, Count, SearchType, CanExpand>::remove(const SearchType search) {
        for (auto it = begin(_entries); it != end(_entries); ++it) {
            if (Matches(it, search)) {
                _entries.erase(it);
                return true;
            }
        }

        return false;
    }
    template<typename Item, size_t Count, typename SearchType>
    bool operator==(const SetContainer<Item, Count, SearchType> &lhs, const SetContainer<Item, Count, SearchType> &rhs) noexcept {
        return lhs._entries == rhs._entries;
    }
    template<typename Item, size_t Count, typename SearchType>
    bool operator!=(const SetContainer<Item, Count, SearchType> &lhs, const SetContainer<Item, Count, SearchType> &rhs) noexcept {
        return lhs._entries != rhs._entries;
    }


} //namespace Divide
#endif //_DESCRIPTOR_SETS_INL_
