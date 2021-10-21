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
#ifndef _PLATFORM_DATA_TYPES_H_
#define _PLATFORM_DATA_TYPES_H_

#include <cassert>

namespace Divide {

// "Exact" number of bits
using U8  = uint8_t;
using U16 = uint16_t;
using U32 = uint32_t;
using U64 = uint64_t;
using I8  = int8_t;
using I16 = int16_t;
using I32 = int32_t;
using I64 = int64_t;

// "At least" number of bits
using U8x  = uint_least8_t;
using U16x = uint_least16_t;
using U32x = uint_least32_t;
using U64x = uint_least64_t;
using I8x  = int_least8_t;
using I16x = int_least16_t;
using I32x = int_least32_t;
using I64x = int_least64_t;

//double is 8 bytes with Microsoft's compiler)
using F32  = float;
using D64  = double;
using D128 = long double;

// Just a name to use as a reminder that these values shoul be be in the 0.0f to 1.0f range
using F32_NORM = F32;
// Just a name to use as a reminder that these values shoul be be in the -1.0f to 1.0f range
using F32_SNORM = F32;

using bufferPtr = void*;

#define U8_MAX  std::numeric_limits<U8>::max()
#define U16_MAX std::numeric_limits<U16>::max()
#define U32_MAX std::numeric_limits<U32>::max()
#define U64_MAX std::numeric_limits<U64>::max()
#define I8_MAX  std::numeric_limits<I8>::max()
#define I16_MAX std::numeric_limits<I16>::max()
#define I32_MAX std::numeric_limits<I32>::max()
#define I64_MAX std::numeric_limits<I64>::max()

#define U8x_MAX  std::numeric_limits<U8x>::max()
#define U16x_MAX std::numeric_limits<U16x>::max()
#define U32x_MAX std::numeric_limits<U32x>::max()
#define U64x_MAX std::numeric_limits<U64x>::max()
#define I8x_MAX  std::numeric_limits<I8x>::max()
#define I16x_MAX std::numeric_limits<I16x>::max()
#define I32x_MAX std::numeric_limits<I32x>::max()
#define I64x_MAX std::numeric_limits<I64x>::max()

#if !defined(CPP_17_SUPPORT)
#warning "Native byte type not detected. Consider updating language version for current project"
enum class Byte : U8 {};
#else
using Byte = std::byte;
#endif

union P32 {
    U32 i = 0u;
    U8  b[4];

    P32() = default;
    P32(const U32 val) noexcept : i(val) {}
    P32(const U8 b1, const U8 b2, const U8 b3, const U8 b4) noexcept : b{ b1, b2, b3, b4 } {}
    P32(U8* bytes) noexcept : b{ bytes[0], bytes[1], bytes[2], bytes[3] } {}
};

static const P32 P32_FLAGS_TRUE = { 1u, 1u, 1u, 1u };
static const P32 P32_FLAGS_FALSE = { 0u, 0u, 0u, 0u };

union P64 {
    U64 i = 0u;
    U8  b[8];

    P64() = default;
    P64(const U64 val) noexcept : i(val) {}
    P64(const U8 b1, const U8 b2, const U8 b3, const U8 b4, const U8 b5, const U8 b6, const U8 b7, const U8 b8) noexcept : b{ b1, b2, b3, b4, b5, b6, b7, b8 } {}
    P64(U8* bytes) noexcept { std::memcpy(b, bytes, 8 * sizeof(U8)); }
};

static const P64 P64_FLAGS_TRUE = { 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u };
static const P64 P64_FLAGS_FALSE = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };

inline bool operator==(const P32& lhs, const P32& rhs) noexcept {
    return lhs.i == rhs.i;
}
inline bool operator!=(const P32& lhs, const P32& rhs) noexcept {
    return lhs.i != rhs.i;
}
inline bool operator==(const P64& lhs, const P64& rhs) noexcept {
    return lhs.i == rhs.i;
}
inline bool operator!=(const P64& lhs, const P64& rhs) noexcept {
    return lhs.i != rhs.i;
}

//Ref: https://stackoverflow.com/questions/7416699/how-to-define-24bit-data-type-in-c
constexpr I32 INT24_MAX = 8388607;
constexpr U32 UINT24_MAX = static_cast<U32>(INT24_MAX * 2);

#pragma pack(push, 1)
struct I24
{
    U8 value[3] = {0u, 0u, 0u};

    I24(I32 val) noexcept 
        : value{ reinterpret_cast<U8*>(&val)[0], reinterpret_cast<U8*>(&val)[1], reinterpret_cast<U8*>(&val)[2] }
    {
        assert(val < INT24_MAX && val > -INT24_MAX);
    }

    FORCE_INLINE I24& operator= (const I32 input) noexcept {
        assert(input < INT24_MAX && input > -INT24_MAX);

        value[0] = ((U8*)&input)[0];
        value[1] = ((U8*)&input)[1];
        value[2] = ((U8*)&input)[2];

        return *this;
    }

    FORCE_INLINE operator I32() const noexcept {
        /* Sign extend negative quantities */
        if (value[2] & 0x80) {
            return 0xff << 24 | value[2] << 16 | value[1] << 8 | value[0];
        }
         
        return value[2] << 16 | value[1] << 8 | value[0];
    }

    FORCE_INLINE I24 operator+   (const I32 val)  const noexcept { return I24(static_cast<I32>(*this) + val); }
    FORCE_INLINE I24 operator-   (const I32 val)  const noexcept { return I24(static_cast<I32>(*this) - val); }
    FORCE_INLINE I24 operator+   (const I24& val) const noexcept { return I24(static_cast<I32>(*this) + static_cast<I32>(val)); }
    FORCE_INLINE I24 operator-   (const I24& val) const noexcept { return I24(static_cast<I32>(*this) - static_cast<I32>(val)); }
    FORCE_INLINE I24 operator*   (const I24& val) const noexcept { return I24(static_cast<I32>(*this) * static_cast<I32>(val)); }
    FORCE_INLINE I24 operator/   (const I24& val) const noexcept { return I24(static_cast<I32>(*this) / static_cast<I32>(val)); }
    FORCE_INLINE I24& operator+= (const I24& val)       noexcept { *this = *this + val; return *this; }
    FORCE_INLINE I24& operator-= (const I24& val)       noexcept { *this = *this - val; return *this; }
    FORCE_INLINE I24& operator*= (const I24& val)       noexcept { *this = *this * val; return *this; }
    FORCE_INLINE I24& operator/= (const I24& val)       noexcept { *this = *this / val; return *this; }
    FORCE_INLINE I24 operator>>  (const I32 val) const  noexcept { return I24(static_cast<I32>(*this) >> val); }
    FORCE_INLINE I24 operator<<  (const I32 val) const  noexcept { return I24(static_cast<I32>(*this) << val); }

    FORCE_INLINE operator bool()   const noexcept { return static_cast<I32>(*this) != 0; }
    FORCE_INLINE bool operator! () const noexcept { return !static_cast<I32>(*this); }
    FORCE_INLINE I24  operator- () const noexcept { return I24(-static_cast<I32>(*this)); }
    FORCE_INLINE I24& operator++ ()      noexcept { *this = *this + 1; return *this; }
    FORCE_INLINE I24& operator-- ()      noexcept { *this = *this - 1; return *this; }

    FORCE_INLINE I24  operator++ (I32)                  noexcept { I24 ret = *this; ++*this; return ret; }
    FORCE_INLINE I24  operator-- (I32)                  noexcept { I24 ret = *this; --*this; return ret; }
    FORCE_INLINE bool operator== (const I24& val) const noexcept { return static_cast<I32>(*this) == static_cast<I32>(val); }
    FORCE_INLINE bool operator!= (const I24& val) const noexcept { return static_cast<I32>(*this) != static_cast<I32>(val); }
    FORCE_INLINE bool operator>= (const I24& val) const noexcept { return static_cast<I32>(*this) >= static_cast<I32>(val); }
    FORCE_INLINE bool operator<= (const I24& val) const noexcept { return static_cast<I32>(*this) <= static_cast<I32>(val); }
    FORCE_INLINE bool operator>  (const I24& val) const noexcept { return static_cast<I32>(*this) >  static_cast<I32>(val); }
    FORCE_INLINE bool operator<  (const I24& val) const noexcept { return static_cast<I32>(*this) <  static_cast<I32>(val); }


    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator==  (const T& val) const noexcept {
        return static_cast<I32>(*this) == val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator!=  (const T& val) const noexcept {
        return static_cast<I32>(*this) != val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator> (const T& val) const noexcept {
        return static_cast<I32>(*this) > val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator<  (const T& val) const noexcept { 
        return static_cast<I32>(*this) < val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator>=  (const T& val) const noexcept {
        return static_cast<I32>(*this) >= val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator<=  (const T& val) const noexcept {
        return static_cast<I32>(*this) <= val;
    }
};

struct U24
{
    U8 value[3] = { 0u, 0u, 0u };

    U24() noexcept : U24(0u)
    {
    }

    U24(U32 val) noexcept
        : value{ reinterpret_cast<U8*>(&val)[0], reinterpret_cast<U8*>(&val)[1], reinterpret_cast<U8*>(&val)[2] }
    {
        assert(val < UINT24_MAX);
    }

    FORCE_INLINE U24& operator= (const U32 input) noexcept {
        assert(input < UINT24_MAX);

        value[2] = input >> 16 & 0xff;
        value[1] = input >> 8 & 0xff;
        value[0] = input & 0xff;

        return *this;
    }

    FORCE_INLINE operator U32() const noexcept {
        return value[0] | value[1] << 8 | value[2] << 16;
    }

    FORCE_INLINE U24 operator+   (const U32 val)  const noexcept { return U24(static_cast<U32>(*this) + val); }
    FORCE_INLINE U24 operator-   (const U32 val)  const noexcept { return U24(static_cast<U32>(*this) - val); }
    FORCE_INLINE U24 operator+   (const U24& val) const noexcept { return U24(static_cast<U32>(*this) + static_cast<U32>(val)); }
    FORCE_INLINE U24 operator-   (const U24& val) const noexcept { return U24(static_cast<U32>(*this) - static_cast<U32>(val)); }
    FORCE_INLINE U24 operator*   (const U24& val) const noexcept { return U24(static_cast<U32>(*this) * static_cast<U32>(val)); }
    FORCE_INLINE U24 operator/   (const U24& val) const noexcept { return U24(static_cast<U32>(*this) / static_cast<U32>(val)); }
    FORCE_INLINE U24& operator+= (const U24& val)       noexcept { *this = *this + val; return *this; }
    FORCE_INLINE U24& operator-= (const U24& val)       noexcept { *this = *this - val; return *this; }
    FORCE_INLINE U24& operator*= (const U24& val)       noexcept { *this = *this * val; return *this; }
    FORCE_INLINE U24& operator/= (const U24& val)       noexcept { *this = *this / val; return *this; }
    FORCE_INLINE U24 operator>>  (const U32 val) const  noexcept { return U24(static_cast<U32>(*this) >> val); }
    FORCE_INLINE U24 operator<<  (const U32 val) const  noexcept { return U24(static_cast<U32>(*this) << val); }

    FORCE_INLINE operator bool()   const noexcept { return static_cast<U32>(*this) != 0; }
    FORCE_INLINE bool operator! () const noexcept { return !static_cast<U32>(*this); }
    FORCE_INLINE U24& operator++ ()      noexcept { *this = *this + 1u; return *this; }
    FORCE_INLINE U24& operator-- ()      noexcept { *this = *this - 1u; return *this; }

    FORCE_INLINE U24  operator++ (I32)                  noexcept { U24 ret = *this; ++*this; return ret; }
    FORCE_INLINE U24  operator-- (I32)                  noexcept { U24 ret = *this; --*this; return ret; }
    FORCE_INLINE bool operator== (const U24& val) const noexcept { return static_cast<U32>(*this) == static_cast<U32>(val); }
    FORCE_INLINE bool operator!= (const U24& val) const noexcept { return static_cast<U32>(*this) != static_cast<U32>(val); }
    FORCE_INLINE bool operator>= (const U24& val) const noexcept { return static_cast<U32>(*this) >= static_cast<U32>(val); }
    FORCE_INLINE bool operator<= (const U24& val) const noexcept { return static_cast<U32>(*this) <= static_cast<U32>(val); }
    FORCE_INLINE bool operator>  (const U24& val) const noexcept { return static_cast<U32>(*this) > static_cast<U32>(val); }
    FORCE_INLINE bool operator<  (const U24& val) const noexcept { return static_cast<U32>(*this) < static_cast<U32>(val); }

    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator==  (const T& val) const noexcept {
        return val >= 0 && static_cast<U32>(*this) == val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator!=  (const T& val) const noexcept {
        return val < 0 || static_cast<U32>(*this) != val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator> (const T& val) const noexcept {
        return static_cast<U32>(*this) > val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator<  (const T& val) const noexcept {
        return static_cast<U32>(*this) < val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator>=  (const T& val) const noexcept {
        return static_cast<U32>(*this) >= val;
    }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    FORCE_INLINE bool operator<=  (const T& val) const noexcept {
        return static_cast<U32>(*this) <= val;
    }
};
#pragma pack(pop)


template <typename From, typename To>
struct static_caster
{
    To operator()(From p) { return static_cast<To>(p); }
};

/*
template<typename Enum>
constexpr U32 operator"" _u32 ( Enum value )
{
return static_cast<U32>(value);
}*/

template<typename Type>
using BaseType = std::underlying_type_t<Type>;

template <typename Type, typename = typename std::enable_if<std::is_integral<Type>::value>::type>
constexpr auto to_base(const Type value) -> Type {
    return value;
}

template <typename Type, typename = typename std::enable_if<std::is_enum<Type>::value>::type>
constexpr auto to_base(const Type value) -> std::underlying_type_t<Type> {
    return static_cast<std::underlying_type_t<Type>>(value);
}

template <typename T>
constexpr size_t to_size(const T value) {
    return static_cast<size_t>(value);
}

template <typename T>
constexpr U64 to_U64(const T value) {
    return static_cast<U64>(value);
}

template <typename T>
constexpr U32 to_U32(const T value) {
    return static_cast<U32>(value);
}

template <typename T>
constexpr U16 to_U16(const T value) {
    return static_cast<U16>(value);
}

template <typename T>
constexpr U8 to_U8(const T value) {
    return static_cast<U8>(value);
}

template <typename T>
constexpr I64 to_I64(const T value) {
    return static_cast<I64>(value);
}

template <typename T>
constexpr I32 to_I32(const T value) {
    return static_cast<I32>(value);
}

template <typename T>
constexpr I16 to_I16(const T value) {
    return static_cast<I16>(value);
}

template <typename T>
constexpr I8 to_I8(const T value) {
    return static_cast<I8>(value);
}
template <typename T>
constexpr F32 to_F32(const T value) {
    return static_cast<F32>(value);
}

template <typename T>
constexpr D64 to_D64(const T value) {
    return static_cast<D64>(value);
}

template<typename T>
constexpr D128 to_D128(const T value) {
    return static_cast<D128>(value);
}

template<typename T>
constexpr Byte to_byte(const T value) {
    return static_cast<Byte>(value);
}

//ref: http://codereview.stackexchange.com/questions/51235/udp-network-server-client-for-gaming-using-boost-asio
class counter {
    size_t _count;
public:
    counter &operator=(const size_t val) noexcept { _count = val; return *this; }
    counter(const size_t count = 0) noexcept : _count(count) {}
    operator size_t() const noexcept { return _count; }
    counter &operator++() noexcept { ++_count; return *this; }
    counter operator++(int) noexcept { const counter ret(_count); ++_count; return ret; }
    bool operator==(counter const &other) const noexcept { return _count == other._count; }
    bool operator!=(counter const &other) const noexcept { return _count != other._count; }
};


// Type promotion
// ref: https://janmr.com/blog/2010/08/cpp-templates-usual-arithmetic-conversions/

template <typename T>
struct promote {
    typedef T type;
};

template <>
struct promote<signed short>
{
    typedef I32 type;
};

template <>
struct promote<bool>
{
    typedef I32 type;
};

template <bool C, typename T, typename F>
struct choose_type
{
    typedef F type;
};

template <typename T, typename F>
struct choose_type<true, T, F>
{
    typedef T type;
};

template <>
struct promote<unsigned short>
{
    typedef choose_type<sizeof(short) < sizeof(I32), I32, U32>::type type;
};

template <>
struct promote<signed char>
{
    typedef choose_type<sizeof(char) <= sizeof(I32), I32, U32>::type type;
};

template <>
struct promote<unsigned char>
{
    typedef choose_type<sizeof(char) < sizeof(I32), I32, U32>::type type;
};

template <>
struct promote<char>
    : public promote<choose_type<std::numeric_limits<char>::is_signed,
    signed char, unsigned char>::type>
{
};

template <>
struct promote<wchar_t>
{
    typedef choose_type<
        std::numeric_limits<wchar_t>::is_signed,
        choose_type<sizeof(wchar_t) <= sizeof(I32), I32, long>::type,
        choose_type<sizeof(wchar_t) <= sizeof(I32), U32, unsigned long>::type
    >::type type;
};

template <typename T> struct type_rank;
template <> struct type_rank<I32> { static const I32 rank = 1; };
template <> struct type_rank<U32> { static const I32 rank = 2; };
template <> struct type_rank<long> { static const I32 rank = 3; };
template <> struct type_rank<unsigned long> { static const I32 rank = 4; };
template <> struct type_rank<I64> { static const I32 rank = 5; };
template <> struct type_rank<U64> { static const I32 rank = 6; };
template <> struct type_rank<F32> { static const I32 rank = 7; };
template <> struct type_rank<D64> { static const I32 rank = 8; };
template <> struct type_rank<D128> { static const int rank = 9; };

template <typename A, typename B>
struct resolve_uac2
{
    typedef typename choose_type<
        type_rank<A>::rank >= type_rank<B>::rank, A, B
    >::type return_type;
};

template <>
struct resolve_uac2<long, U32>
{
    typedef choose_type<sizeof(long) == sizeof(U32),
        unsigned long, long>::type return_type;
};

template <>
struct resolve_uac2<U32, long> : public resolve_uac2<long, U32>
{
};

template <typename A, typename B>
struct resolve_uac : public resolve_uac2<typename promote<A>::type,
    typename promote<B>::type>
{
};

template <typename A, typename B>
constexpr typename resolve_uac<A, B>::return_type add(const A& a, const B& b) noexcept
{
    return a + b;
}


template <typename A, typename B>
constexpr typename resolve_uac<A, B>::return_type subtract(const A& a, const B& b) noexcept
{
    return a - b;
}


template <typename A, typename B>
constexpr typename resolve_uac<A, B>::return_type divide(const A& a, const B& b) noexcept
{
    return a / b;
}


template <typename A, typename B>
constexpr typename resolve_uac<A, B>::return_type multiply(const A& a, const B& b) noexcept
{
    return a * b;
}

template <typename ToCheck, std::size_t ExpectedSize, std::size_t RealSize = sizeof(ToCheck)>
constexpr void check_size() {
    static_assert(ExpectedSize == RealSize, "Wrong data size!");
}

#pragma region PROPERTY_SETTERS_GETTERS

#if !defined(EXP)
#define EXP( x ) x

#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define GET_4TH_ARG(arg1, arg2, arg3, arg4, ...) arg4
#define GET_5TH_ARG(arg1, arg2, arg3, arg4, arg5, ...) arg5
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6
#endif //EXP

#define PROPERTY_GET_SET(Type, Name) \
public: \
    FORCE_INLINE void Name(const Type& val) noexcept { _##Name = val; } \
    [[nodiscard]] FORCE_INLINE Type& Name() noexcept { return _##Name; } \
    [[nodiscard]] FORCE_INLINE const Type& Name() const noexcept { return _##Name; }

#define PROPERTY_GET(Type, Name) \
public: \
    [[nodiscard]] FORCE_INLINE const Type& Name() const noexcept { return _##Name; }

#define VIRTUAL_PROPERTY_GET_SET(Type, Name) \
public: \
    virtual void Name(const Type& val) noexcept { _##Name = val; } \
    [[nodiscard]] virtual Type& Name() noexcept { return _##Name; } \
    [[nodiscard]] virtual const Type& Name() const noexcept { return _##Name; }

#define VIRTUAL_PROPERTY_GET(Type, Name) \
public: \
    [[nodiscard]] virtual const Type& Name() const noexcept { return _##Name; }

#define POINTER_GET_SET(Type, Name) \
public: \
    FORCE_INLINE void Name(Type* const val) noexcept { _##Name = val; } \
    [[nodiscard]] FORCE_INLINE Type* Name() noexcept { return _##Name; } \
    [[nodiscard]] FORCE_INLINE Type* const Name() const noexcept { return _##Name; }

#define POINTER_GET(Type, Name) \
public: \
   [[nodiscard]] FORCE_INLINE Type* const Name() const noexcept { return _##Name; }

#define PROPERTY_GET_INTERNAL(Type, Name) \
protected: \
    [[nodiscard]] FORCE_INLINE const Type& Name() const noexcept { return _##Name; }

#define VIRTUAL_PROPERTY_GET_INTERNAL(Type, Name) \
protected: \
    [[nodiscard]] virtual const Type& Name() const noexcept { return _##Name; }

#define POINTER_GET_INTERNAL(Type, Name) \
protected: \
    [[nodiscard]] FORCE_INLINE Type* const Name() const noexcept { return _##Name; }

#define PROPERTY_SET_INTERNAL(Type, Name) \
protected: \
    FORCE_INLINE void Name(const Type& val) noexcept { _##Name = val; } \

#define VIRTUAL_PROPERTY_SET_INTERNAL(Type, Name) \
protected: \
    virtual void Name(const Type& val) noexcept { _##Name = val; } \

#define POINTER_SET_INTERNAL(Type, Name) \
protected: \
    FORCE_INLINE void Name(Type* const val) noexcept { _##Name = val; } \

#define PROPERTY_GET_SET_INTERNAL(Type, Name) \
protected: \
    PROPERTY_SET_INTERNAL(Type, Name) \
    PROPERTY_GET_INTERNAL(Type, Name)

#define VIRTUAL_PROPERTY_GET_SET_INTERNAL(Type, Name) \
protected: \
    VIRTUAL_PROPERTY_SET_INTERNAL(Type, Name) \
    VIRTUAL_PROPERTY_GET_INTERNAL(Type, Name)

#define POINTER_GET_SET_INTERNAL(Type, Name) \
protected: \
    POINTER_SET_INTERNAL(Type, Name) \
    POINTER_GET_INTERNAL(Type, Name)

#define PROPERTY_R_1_ARGS(Type)
#define PROPERTY_RW_1_ARGS(Type)
#define PROPERTY_R_IW_1_ARGS(Type)
#define VIRTUAL_PROPERTY_R_1_ARGS(Type)
#define VIRTUAL_PROPERTY_R_IW_1_ARGS(Type)
#define POINTER_R_1_ARGS(Type)
#define POINTER_RW_1_ARGS(Type)
#define POINTER_R_IW_1_ARGS(Type)
#define REFERENCE_R_1_ARGS(Type)
#define REFERENCE_RW_1_ARGS(Type)

#define PROPERTY_RW_1_ARGS_INTERNAL(Type)
#define VIRTUAL_PROPERTY_RW_1_ARGS(Type)
#define VIRTUAL_PROPERTY_RW_1_ARGS_INTERNAL(Type)
#define POINTER_RW_1_ARGS_INTERNAL(Type)
#define REFERENCE_RW_1_ARGS_INTERNAL(Type)

//------------- PROPERTY_RW
#define PROPERTY_R_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    PROPERTY_GET(Type, Name) \
public:

#define PROPERTY_R_IW_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    PROPERTY_GET(Type, Name) \
    PROPERTY_SET_INTERNAL(Type, Name) \
public:

#define PROPERTY_RW_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    PROPERTY_GET_SET(Type, Name) \
public:

#define PROPERTY_R_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    PROPERTY_GET(Type, Name) \
public:

#define PROPERTY_R_IW_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    PROPERTY_GET(Type, Name) \
    PROPERTY_SET_INTERNAL(Type, Name) \
public:

#define PROPERTY_RW_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    PROPERTY_GET_SET(Type, Name) \
public:

//------------- PROPERTY_RW_INTERNAL
#define PROPERTY_RW_3_ARGS_INTERNAL(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    PROPERTY_GET_SET_INTERNAL(Type, Name) \
public:

#define PROPERTY_RW_2_ARGS_INTERNAL(Type, Name) \
protected: \
    Type _##Name; \
    PROPERTY_GET_SET_INTERNAL(Type, Name)\
public:

//------------------- VIRTUAL_PROPERTY_RW
#define VIRTUAL_PROPERTY_R_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    VIRTUAL_PROPERTY_GET(Type, Name) \
public:

#define VIRTUAL_PROPERTY_R_IW_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    VIRTUAL_PROPERTY_GET(Type, Name) \
    VIRTUAL_PROPERTY_SET_INTERNAL(Type, Name) \
public:

#define VIRTUAL_PROPERTY_RW_3_ARGS(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    VIRTUAL_PROPERTY_GET_SET(Type, Name) \
public:

#define VIRTUAL_PROPERTY_R_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    VIRTUAL_PROPERTY_GET(Type, Name) \
public:

#define VIRTUAL_PROPERTY_R_IW_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    VIRTUAL_PROPERTY_GET(Type, Name) \
    VIRTUAL_PROPERTY_SET_INTERNAL(Type, Name) \
public:

#define VIRTUAL_PROPERTY_RW_2_ARGS(Type, Name) \
protected: \
    Type _##Name; \
    VIRTUAL_PROPERTY_GET_SET(Type, Name) \
public:

//------------------- VIRTUAL_PROPERTY_RW_INTERNAL
#define VIRTUAL_PROPERTY_RW_3_ARGS_INTERNAL(Type, Name, Val) \
protected: \
    Type _##Name = Val; \
    VIRTUAL_PROPERTY_GET_SET_INTERNAL(Type, Name) \
public:

#define VIRTUAL_PROPERTY_RW_2_ARGS_INTERNAL(Type, Name) \
protected: \
    Type _##Name; \
    VIRTUAL_PROPERTY_GET_SET_INTERNAL(Type, Name) \
public:

//-------------------- POINTER_RW
#define POINTER_R_3_ARGS(Type, Name, Val) \
protected: \
    Type* _##Name = Val; \
    POINTER_GET(Type, Name) \
public:

#define POINTER_R_IW_3_ARGS(Type, Name, Val) \
protected: \
    Type* _##Name = Val; \
    POINTER_GET(Type, Name) \
    POINTER_SET_INTERNAL(Type, Name) \
public:

#define POINTER_RW_3_ARGS(Type, Name, Val) \
protected: \
    Type* _##Name = Val; \
    POINTER_GET_SET(Type, Name) \
public:

#define POINTER_R_2_ARGS(Type, Name) \
protected: \
    Type* _##Name; \
    POINTER_GET(Type, Name) \
public:

#define POINTER_R_IW_2_ARGS(Type, Name) \
protected: \
    Type* _##Name; \
    POINTER_GET(Type, Name) \
    POINTER_SET_INTERNAL(Type, Name) \
public:

#define POINTER_RW_2_ARGS(Type, Name) \
protected: \
    Type* _##Name; \
    POINTER_GET_SET(Type, Name) \
public:

//-------------------- POINTER_RW_INTERNAL
#define POINTER_RW_3_ARGS_INTERNAL(Type, Name, Val) \
protected: \
    Type* _##Name = Val; \
    POINTER_GET_SET_INTERNAL(Type, Name) \
public:

#define POINTER_RW_2_ARGS_INTERNAL(Type, Name) \
protected: \
    Type* _##Name; \
    POINTER_GET_SET_INTERNAL(Type, Name) \
public:

//-------------------- REFERENCE_RW
#define REFERENCE_R_3_ARGS(Type, Name, Val) \
protected: \
    Type& _##Name = Val; \
    PROPERTY_GET(Type, Name) \
public:

#define REFERENCE_RW_3_ARGS(Type, Name, Val) \
protected: \
    Type& _##Name = Val; \
    PROPERTY_GET_SET(Type, Name) \
public:

#define REFERENCE_R_2_ARGS(Type, Name) \
protected: \
    Type& _##Name; \
    PROPERTY_GET(Type, Name) \
public:

#define REFERENCE_RW_2_ARGS(Type, Name) \
protected: \
    Type& _##Name; \
    PROPERTY_GET_SET(Type, Name) \
public:

//-------------------- REFERENCE_RW_INTERNAL
#define REFERENCE_RW_3_ARGS_INTERNAL(Type, Name, Val) \
protected: \
    Type& _##Name = Val; \
    PROPERTY_GET_SET_INTERNAL(Type, Name) \
public:

#define REFERENCE_RW_2_ARGS_INTERNAL(Type, Name) \
protected: \
    Type& _##Name; \
    PROPERTY_GET_SET_INTERNAL(Type, Name) \
public:

#define ___DETAIL_PROPERTY_RW_INTERNAL(...) EXP(GET_4TH_ARG(__VA_ARGS__, PROPERTY_RW_3_ARGS_INTERNAL, PROPERTY_RW_2_ARGS_INTERNAL, PROPERTY_RW_1_ARGS_INTERNAL, ))
#define ___DETAIL_VIRTUAL_PROPERTY_RW_INTERNAL(...) EXP(GET_4TH_ARG(__VA_ARGS__, VIRTUAL_PROPERTY_RW_3_ARGS_INTERNAL, VIRTUAL_PROPERTY_RW_2_ARGS_INTERNAL, VIRTUAL_PROPERTY_RW_1_ARGS_INTERNAL, ))
#define ___DETAIL_POINTER_RW_INTERNAL(...) EXP(GET_4TH_ARG(__VA_ARGS__, POINTER_RW_3_ARGS_INTERNAL, POINTER_RW_2_ARGS_INTERNAL, POINTER_RW_1_ARGS_INTERNAL, ))
#define ___DETAIL_REFERENCE_RW_INTERNAL(...) EXP(GET_4TH_ARG(__VA_ARGS__, REFERENCE_RW_3_ARGS_INTERNAL, REFERENCE_RW_2_ARGS_INTERNAL, REFERENCE_RW_1_ARGS_INTERNAL, ))

#define ___DETAIL_PROPERTY_R(...) EXP(GET_4TH_ARG(__VA_ARGS__, PROPERTY_R_3_ARGS, PROPERTY_R_2_ARGS, PROPERTY_R_1_ARGS, ))
#define ___DETAIL_PROPERTY_RW(...) EXP(GET_4TH_ARG(__VA_ARGS__, PROPERTY_RW_3_ARGS, PROPERTY_RW_2_ARGS, PROPERTY_RW_1_ARGS, ))
#define ___DETAIL_PROPERTY_R_IW(...) EXP(GET_4TH_ARG(__VA_ARGS__, PROPERTY_R_IW_3_ARGS, PROPERTY_R_IW_2_ARGS, PROPERTY_R_IW_1_ARGS, ))

#define ___DETAIL_VIRTUAL_PROPERTY_R(...) EXP(GET_4TH_ARG(__VA_ARGS__, VIRTUAL_PROPERTY_R_3_ARGS, VIRTUAL_PROPERTY_R_2_ARGS, VIRTUAL_PROPERTY_R_1_ARGS, ))
#define ___DETAIL_VIRTUAL_PROPERTY_RW(...) EXP(GET_4TH_ARG(__VA_ARGS__, VIRTUAL_PROPERTY_RW_3_ARGS, VIRTUAL_PROPERTY_RW_2_ARGS, VIRTUAL_PROPERTY_RW_1_ARGS, ))
#define ___DETAIL_VIRTUAL_PROPERTY_R_IW(...) EXP(GET_4TH_ARG(__VA_ARGS__, VIRTUAL_PROPERTY_R_IW_3_ARGS, VIRTUAL_PROPERTY_R_IW_2_ARGS, VIRTUAL_PROPERTY_R_IW_1_ARGS, ))

#define ___DETAIL_POINTER_R(...) EXP(GET_4TH_ARG(__VA_ARGS__, POINTER_R_3_ARGS, POINTER_R_2_ARGS, POINTER_R_1_ARGS, ))
#define ___DETAIL_POINTER_RW(...) EXP(GET_4TH_ARG(__VA_ARGS__, POINTER_RW_3_ARGS, POINTER_RW_2_ARGS, POINTER_RW_1_ARGS, ))
#define ___DETAIL_POINTER_R_IW(...) EXP(GET_4TH_ARG(__VA_ARGS__, POINTER_R_IW_3_ARGS, POINTER_R_IW_2_ARGS, POINTER_R_IW_1_ARGS, ))

#define ___DETAIL_REFERENCE_R(...) EXP(GET_4TH_ARG(__VA_ARGS__, REFERENCE_R_3_ARGS, REFERENCE_R_2_ARGS, REFERENCE_R_1_ARGS, ))
#define ___DETAIL_REFERENCE_RW(...) EXP(GET_4TH_ARG(__VA_ARGS__, REFERENCE_RW_3_ARGS, REFERENCE_RW_2_ARGS, REFERENCE_RW_1_ARGS, ))

/// Convenience method to add a class member with public read access but protected write access
#define PROPERTY_R(...) EXP(___DETAIL_PROPERTY_R(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access and write access
#define PROPERTY_RW(...) EXP(___DETAIL_PROPERTY_RW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access including a protected accessor
#define PROPERTY_R_IW(...) EXP(___DETAIL_PROPERTY_R_IW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access
#define VIRTUAL_PROPERTY_R(...) EXP(___DETAIL_VIRTUAL_PROPERTY_R(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access and write access
#define VIRTUAL_PROPERTY_RW(...) EXP(___DETAIL_VIRTUAL_PROPERTY_RW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access including a protected accessor
#define VIRTUAL_PROPERTY_R_IW(...) EXP(___DETAIL_VIRTUAL_PROPERTY_R_IW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access
#define POINTER_R(...) EXP(___DETAIL_POINTER_R(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access and write access
#define POINTER_RW(...) EXP(___DETAIL_POINTER_RW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access including a protected accessor
#define POINTER_R_IW(...) EXP(___DETAIL_POINTER_R_IW(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access but protected write access
#define REFERENCE_R(...) EXP(___DETAIL_REFERENCE_R(__VA_ARGS__)(__VA_ARGS__))

/// Convenience method to add a class member with public read access and write access
#define REFERENCE_RW(...) EXP(___DETAIL_REFERENCE_RW(__VA_ARGS__)(__VA_ARGS__))

/// This will only set properties internal to the actual class
#define PROPERTY_INTERNAL(...) EXP(___DETAIL_PROPERTY_RW_INTERNAL(__VA_ARGS__)(__VA_ARGS__))

/// This will only set properties internal to the actual class
#define VIRTUAL_PROPERTY_INTERNAL(...) EXP(___DETAIL_VIRTUAL_PROPERTY_RW_INTERNAL(__VA_ARGS__)(__VA_ARGS__))

/// This will only set properties internal to the actual class
#define POINTER_INTERNAL(...) EXP(___DETAIL_POINTER_RW_INTERNAL(__VA_ARGS__)(__VA_ARGS__))

/// This will only set properties internal to the actual class
#define REFERENCE_INTERNAL(...) EXP(___DETAIL_REFERENCE_RW_INTERNAL(__VA_ARGS__)(__VA_ARGS__))
#pragma endregion
}; //namespace Divide;

#endif //_PLATFORM_DATA_TYPES_H_
