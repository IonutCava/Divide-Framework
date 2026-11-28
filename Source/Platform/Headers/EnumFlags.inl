/*
Copyright (c) 2018-2026 DIVIDE-Studio
Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_ENUM_FLAGS_H_
#define DVD_ENUM_FLAGS_H_

// Included from PlatformDataTypes.h inside namespace Divide, immediately after to_base.

template<typename E>
inline constexpr bool EnableEnumFlags = false;

template<typename E>
concept EnumFlag = std::is_enum_v<E> && EnableEnumFlags<E>;

template<EnumFlag E>
[[nodiscard]] constexpr E operator|(const E lhs, const E rhs) noexcept
{
    return static_cast<E>(to_base(lhs) | to_base(rhs));
}

template<EnumFlag E>
[[nodiscard]] constexpr E operator&(const E lhs, const E rhs) noexcept
{
    return static_cast<E>(to_base(lhs) & to_base(rhs));
}

template<EnumFlag E>
[[nodiscard]] constexpr E operator^(const E lhs, const E rhs) noexcept
{
    return static_cast<E>(to_base(lhs) ^ to_base(rhs));
}

template<EnumFlag E>
[[nodiscard]] constexpr E operator~(const E value) noexcept
{
    return static_cast<E>(~to_base(value));
}

template<EnumFlag E>
constexpr E& operator|=(E& lhs, const E rhs) noexcept
{
    return lhs = lhs | rhs;
}

template<EnumFlag E>
constexpr E& operator&=(E& lhs, const E rhs) noexcept
{
    return lhs = lhs & rhs;
}

template<EnumFlag E>
constexpr E& operator^=(E& lhs, const E rhs) noexcept
{
    return lhs = lhs ^ rhs;
}

#define DIVIDE_ENUM_FLAGS(E) \
    template<> inline constexpr bool EnableEnumFlags<E> = true

#endif //DVD_ENUM_FLAGS_H_
