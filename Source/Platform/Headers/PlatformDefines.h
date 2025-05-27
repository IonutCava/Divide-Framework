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
#ifndef DVD_PLATFORM_DEFINES_H_
#define DVD_PLATFORM_DEFINES_H_

#include "config.h"
#include "Core/Headers/ErrorCodes.h"
#include "Core/Headers/NonMovable.h"
#include "Platform/File/Headers/ResourcePath.h"

#define EXP( x ) x

#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define GET_4TH_ARG(arg1, arg2, arg3, arg4, ...) arg4
#define GET_5TH_ARG(arg1, arg2, arg3, arg4, arg5, ...) arg5
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6

#if defined(_DEBUG)
#define STUBBED(x)                                    \
do {                                                  \
    static bool seen_this = false;                    \
    if (!seen_this) {                                 \
        seen_this = true;                             \
        Console::errorfn("[ STUBBED ] {} ({} : {})\n",\
                         x, __FILE__, __LINE__);      \
    }                                                 \
} while (0)

#else
#define STUBBED(x) static_assert(true, "")
#endif

/// Makes writing and reading smart pointer types easier and cleaner
/// Faster to write ClassName_uptr than std::unique_ptr<ClassName>
/// Also cleaner and easier to read the managed type in nested templated parameters 
/// e.g. std::pair<ClassNameA_uptr, ClassNameB_wptr> vs std::pair<std::unique_ptr<ClassNameA>, std::weak_ptr<ClassNameB>>
/// Also makes it easier to switch smart pointer implementations (i.e. eastl for unique_ptr) using a centralized location
#define TYPEDEF_SMART_POINTERS_FOR_TYPE(T)       \
    using T ## _wptr  = std::weak_ptr<T>;        \
    using T ## _ptr   = std::shared_ptr<T>;      \
    using T ## _cwptr = std::weak_ptr<const T>;  \
    using T ## _cptr  = std::shared_ptr<const T>;\
    using T ## _uptr  = std::unique_ptr<T>;


#define FWD_DECLARE_MANAGED_CLASS(T)      \
    class T;                              \
    TYPEDEF_SMART_POINTERS_FOR_TYPE(T)

#define FWD_DECLARE_MANAGED_STRUCT(T)     \
    struct T;                             \
    TYPEDEF_SMART_POINTERS_FOR_TYPE(T)


#define CONCATENATE_IMPL(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#ifdef __COUNTER__
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __COUNTER__)
#else
#define ANONYMOUSE_VARIABLE(str) CONCATENATE(str, __LINE__)
#endif

 // Multumesc Andrei A.!
#if defined(_MSC_VER)
#define _FUNCTION_NAME_AND_SIG_ __FUNCSIG__
#elif defined(__GNUC__)
#define _FUNCTION_NAME_AND_SIG_ __PRETTY_FUNCTION__
#else
#define _FUNCTION_NAME_AND_SIG_ __FUNCTION__
#endif

#ifndef NO_DESTROY
#if defined(USING_CLANG)
#define NO_DESTROY [[clang::no_destroy]]
#else //USING_CLANG
#define NO_DESTROY
#endif //USING_CLANG
#endif //NO_DESTROY

#define ALIAS_TEMPLATE_FUNCTION(highLevelF, lowLevelF)                         \
template<typename... Args>                                                     \
constexpr auto highLevelF(Args&&... args) -> decltype(lowLevelF(FWD(args)...)) \
{                                                                              \
    return lowLevelF(FWD(args)...);                                            \
}

ALIAS_TEMPLATE_FUNCTION(ArrayCount, std::size)

//ref: https://vittorioromeo.info/index/blog/passing_functions_to_functions.html
template <typename TSignature>
class function_view;

template <typename TReturn, typename... TArgs>
class function_view<TReturn(TArgs...)> final
{
    typedef TReturn signature_type(void*, TArgs...);

    void* _ptr;
    TReturn(*_erased_fn)(void*, TArgs...);

public:
    template <typename T> requires std::is_invocable_v<T&(TArgs...)> && (!std::is_same_v<std::decay_t<T>, function_view>)
    function_view(T&& x) noexcept : _ptr{ (void*)std::addressof(x) } {
        _erased_fn = [](void* ptr, TArgs... xs) -> TReturn {
            return (*reinterpret_cast<std::add_pointer_t<T>>(ptr))(
                FWD(xs)...);
        };
    }

    decltype(auto) operator()(TArgs... xs) const
        noexcept(noexcept(_erased_fn(_ptr, FWD(xs)...))) {
        return _erased_fn(_ptr, FWD(xs)...);
    }
};

namespace Divide {

using PlayerIndex = U8;

// FNV1a c++11 constexpr compile time hash functions, 32 and 64 bit
// str should be a null terminated string literal, value should be left out 
// e.g hash_32_fnv1a_const("example")
// code license: public domain or equivalent
// post: https://notes.underscorediscovery.com/constexpr-fnv1a/

template<typename T>
concept is_pod = (std::is_trivial_v<T> && std::is_standard_layout_v<T>);

constexpr U32 val_32_const = 0x811c9dc5;
constexpr U32 prime_32_const = 0x1000193;
constexpr U64 val_64_const = 0xcbf29ce484222325;
constexpr U64 prime_64_const = 0x100000001b3;

[[nodiscard]] constexpr U64 _ID_VIEW(const char* const str, const size_t len, const U64 value = val_64_const) noexcept
{
    return len == 0 ? value : _ID_VIEW(&str[1], len - 1, (value ^ to_U64(str[0])) * prime_64_const);
}

[[nodiscard]] constexpr U64 _ID(const char* const str, const U64 value = val_64_const) noexcept
{
    return str[0] == '\0' ? value : _ID(&str[1], (value ^ to_U64(str[0])) * prime_64_const);
}

[[nodiscard]] constexpr U64 _ID( const std::string_view str, const U64 value = val_64_const ) noexcept
{
    return _ID_VIEW(str.data(), str.size(), value);
}

[[nodiscard]] constexpr U64 operator ""_id(const char* str, const size_t len)
{
    return _ID_VIEW(str, len);
}

struct SysInfo
{
    size_t _availableRamInBytes{0u};
    ResourcePath _workingDirectory;
};

[[nodiscard]] SysInfo& sysInfo() noexcept;
[[nodiscard]] const SysInfo& const_sysInfo() noexcept;

void InitSysInfo(SysInfo& info, I32 argc, char** argv);

extern F32 PlatformDefaultDPI() noexcept;

struct WindowHandle;
extern void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept;

enum class ThreadPriority : U8 {
    IDLE = 0,
    BELOW_NORMAL,
    NORMAL,
    ABOVE_NORMAL,
    HIGHEST,
    TIME_CRITICAL,
    COUNT
};

extern void SetThreadPriority(ThreadPriority priority);

extern void SetThreadName(std::string_view threadName) noexcept;

extern bool CallSystemCmd(std::string_view cmd, std::string_view args);

bool DebugBreak(bool condition = true) noexcept;

[[nodiscard]] ErrorCode PlatformInit(int argc, char** argv);
[[nodiscard]] bool PlatformClose();
[[nodiscard]] bool GetAvailableMemory(SysInfo& info);

void EnforceDPIScaling() noexcept;

[[nodiscard]] string GetClipboardText() noexcept;
void SetClipboardText(const char* text) noexcept;

void ToggleCursor(bool state) noexcept;

[[nodiscard]] bool CursorState() noexcept;

[[nodiscard]] std::string CurrentDateTimeString();

/// Converts an arbitrary positive integer value to a bitwise value used for masks
template<typename T>
[[nodiscard]] constexpr T toBit(const T X) {
    return 1 << X;
}

[[nodiscard]] constexpr U32 powerOfTwo(U32 X) noexcept
{
    U32 r = 0u;
    while (X >>= 1)
    {
        r++;
    }
    return r;
}

[[nodiscard]] constexpr U32 previousPowerOfTwo(const U32 X) noexcept
{
    U32 r = 1u;
    while (r * 2 < X)
    {
        r *= 2;
    }

    return r;
}

[[nodiscard]] constexpr U32 mipLevels(U32 width, U32 height) noexcept
{
    U32 result = 1u;

    while (width > 1u || height > 1u)
    {
        ++result;
        width /= 2u;
        height /= 2u;
    }

    return result;
}

template<typename T> requires std::is_unsigned_v<T>
[[nodiscard]] constexpr bool isPowerOfTwo(const T x) noexcept {
    return !(x == 0) && !(x & (x - 1));
}

[[nodiscard]] constexpr size_t realign_offset(const size_t offset, const size_t align) noexcept {
    return (offset + align - 1) & ~(align - 1);
}

//ref: http://stackoverflow.com/questions/14226952/partitioning-batch-chunk-a-container-into-equal-sized-pieces-using-std-algorithm
template<typename Iterator, typename Pred>
void for_each_interval(Iterator from, Iterator to, std::ptrdiff_t partition_size, Pred&& operation) 
{
    if (partition_size > 0) {
        Iterator partition_end = from;
        while (partition_end != to) 
        {
            while (partition_end != to && std::distance(from, partition_end) < partition_size) 
            {
                ++partition_end;
            }
            operation(from, partition_end);
            from = partition_end;
        }
    }
}

//ref: http://stackoverflow.com/questions/9530928/checking-a-member-exists-possibly-in-a-base-class-c11-version
template<typename C>
concept has_reserve = requires(C c) {
    c.reserve( std::declval<typename C::size_type>() );
};

template<typename C>
concept has_emplace_back = requires(C c) {
    c.emplace_back( std::declval<typename C::value_type>() );
};

//ref: https://github.com/ParBLiSS/kmerind/blob/master/src/utils/container_traits.hpp
template<typename C>
concept has_assign = requires(C c) {
    c.assign( std::declval<decltype(std::declval<C>().begin())>(),
              std::declval<decltype(std::declval<C>().end())>());
};

template< typename C > requires (!has_reserve<C>)
void optional_reserve(C&, std::size_t) {}

template< typename C > requires has_reserve<C>
void optional_reserve(C& c, std::size_t n) {
    c.reserve(c.size() + n);
}


using std::make_index_sequence;
using std::index_sequence;

namespace detail
{
    template <typename T, std::size_t ... Is>
    constexpr std::array<T, sizeof...(Is)>
        create_array(T value, index_sequence<Is...>)
    {
        // cast Is to void to remove the warning: unused value
        return { {(static_cast<void>(Is), value)...} };
    }

    template <typename T, std::size_t ... Is>
    constexpr eastl::array<T, sizeof...(Is)>
        create_eastl_array(T value, index_sequence<Is...>)     {
        // cast Is to void to remove the warning: unused value
        return { {(static_cast<void>(Is), value)...} };
    }
}


template <std::size_t N, typename T>
constexpr std::array<T, N> create_array(const T& value)
{
    return detail::create_array(value, make_index_sequence<N>());
}

template <std::size_t N, typename T>
constexpr eastl::array<T, N> create_eastl_array(const T& value) {
    return detail::create_eastl_array(value, make_index_sequence<N>());
}

#define NOP() static_assert(true, "")

//Andrei Alexandrescu's ScopeGuard macros from "Declarative Control Flow" (CppCon 2015)
//ref: https://gist.github.com/mmha/6bee3983caf2eab04d80af8e0eaddfbe
namespace detail
{
    enum class ScopeGuardOnExit{};
    enum class ScopeGuardOnFail{};
    enum class ScopeGuardOnSuccess{};

    template <typename Fun>
    class ScopeGuard
    {
        public:
            ScopeGuard(Fun &&fn) noexcept
                : fn(MOV(fn))
            {
            }

            ~ScopeGuard()
            {
                fn();
            }

        private:
            Fun fn;
    };

    class UncaughtExceptionCounter
    {
        I32 exceptionCount_;

    public:
        UncaughtExceptionCounter() noexcept 
            : exceptionCount_(std::uncaught_exceptions())
        {
        }

        bool newUncaughtException() noexcept
        {
            return std::uncaught_exceptions() > exceptionCount_;
        }
    };

    template <typename FunctionType, bool executeOnException>
    class ScopeGuardForNewException
    {
        FunctionType function_;
        UncaughtExceptionCounter ec_;

    public:
        explicit ScopeGuardForNewException(const FunctionType &fn) : function_(fn)
        {
        }

        explicit ScopeGuardForNewException(FunctionType &&fn) : function_(MOV(fn))
        {
        }

        ~ScopeGuardForNewException() noexcept(executeOnException)
        {
            if (executeOnException == ec_.newUncaughtException()) 
            {
                function_();
            }
        }
    };

    template <typename Fun>
    auto operator+(ScopeGuardOnExit, Fun &&fn) noexcept
    {
        return ScopeGuard<Fun>(FWD(fn));
    }

    template <typename Fun>
    auto operator+(ScopeGuardOnFail, Fun &&fn)
    {
        return ScopeGuardForNewException<std::decay_t<Fun>, true>(FWD(fn));
    }

    template <typename Fun>
    auto operator+(ScopeGuardOnSuccess, Fun &&fn)
    {
        return ScopeGuardForNewException<std::decay_t<Fun>, false>(FWD(fn));
    }
} //namespace detail


template <typename T>
struct Handle
{
    union
    {
        struct
        {
            U32 _generation: 8;
            U32 _index : 24;
        };

        U32 _data;
    };

    FORCE_INLINE bool operator==( const Handle& rhs ) const
    {
        return _data == rhs._data;
    }
};


template<typename T>
inline constexpr Handle<T> INVALID_HANDLE{ {._data = U32_MAX} };
                          
#define SCOPE_FAIL          auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = detail::ScopeGuardOnFail() + [&]() noexcept
#define SCOPE_SUCCESS       auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = detail::ScopeGuardOnSuccess() + [&]()
#define SCOPE_EXIT          auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) = detail::ScopeGuardOnExit() + [&]() noexcept
#define SCOPE_EXIT_PARAM(X) auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) = detail::ScopeGuardOnExit() + [&, X]() noexcept

constexpr F32 EPSILON_F32 = std::numeric_limits<F32>::epsilon();
constexpr D64 EPSILON_D64 = std::numeric_limits<D64>::epsilon();


template<std::floating_point T>
[[nodiscard]] constexpr T ABS(const T input)
{
    // mostly for floats
    return input >= T{ 0 } ? input : (input < T{ 0 } ? -input : T{ 0 });
}

template<std::signed_integral T>
[[nodiscard]] constexpr T ABS(const T input)
{
    return input >= T{ 0 } ? input : -input;
}

template<std::unsigned_integral T>
[[nodiscard]] constexpr T ABS(const T input)
{
    return input;
}

template <typename T>
[[nodiscard]] constexpr bool IS_ZERO(const T X) noexcept
{
    return X == T{ 0 };
}

template <>
[[nodiscard]] constexpr bool IS_ZERO(const F32 X) noexcept
{
    return ABS(X) < EPSILON_F32;
}

template <>
[[nodiscard]] constexpr bool IS_ZERO(const D64 X) noexcept
{
    return ABS(X) < EPSILON_D64;
}

template <typename T, typename U = T>
[[nodiscard]] constexpr bool COMPARE_TOLERANCE(const T X, const U TOLERANCE) noexcept
{
    return ABS(X) <= TOLERANCE;
}

template<typename T, typename U = T, typename W = T>
[[nodiscard]] constexpr bool COMPARE_TOLERANCE(const T X, const U Y, const W TOLERANCE) noexcept
{
    if constexpr (std::is_unsigned_v<T> && std::is_unsigned_v<U>)
    {
        return COMPARE_TOLERANCE((X >= Y ? subtract(X, Y) : subtract(Y, X)), TOLERANCE);
    }

    return COMPARE_TOLERANCE(subtract(X, Y), TOLERANCE);
}

template<typename T, typename U  = T>
[[nodiscard]] constexpr bool COMPARE(const T X, const U Y) noexcept
{
    return X == Y;
}

template<is_pod T, is_pod U = T>
[[nodiscard]] constexpr bool COMPARE(const T X, const U Y) noexcept
{
    return X == static_cast<resolve_uac<T, U>::return_type>(Y);
}

template<>
[[nodiscard]] constexpr bool COMPARE(const F32 X, const F32 Y) noexcept
{
    return COMPARE_TOLERANCE(X, Y, EPSILON_F32);
}

template<>
[[nodiscard]] constexpr  bool COMPARE(const D64 X, const D64 Y) noexcept
{
    return COMPARE_TOLERANCE(X, Y, EPSILON_D64);
}

template<typename T, typename tagType>
[[nodiscard]] constexpr primitiveWrapper<T, tagType> ABS(const primitiveWrapper<T, tagType> input)
{
    return ABS<T>(input.value);
}

template<typename T, typename tagType>
[[nodiscard]] constexpr bool COMPARE(const primitiveWrapper<T, tagType> X, const primitiveWrapper<T, tagType> Y) noexcept
{
    return COMPARE(X.value, Y.value);
}

template<typename T, typename tagType, typename W>
[[nodiscard]] constexpr bool COMPARE_TOLERANCE(const primitiveWrapper<T, tagType> X, const primitiveWrapper<T, tagType> Y, const W TOLERANCE) noexcept
{
    return COMPARE_TOLERANCE(X.value, Y.value, TOLERANCE);
}

/* See
http://randomascii.wordpress.com/2012/01/11/tricks-with-the-floating-point-format/
for the potential portability problems with the union and bit-fields below.
*/
union Float_t
{
    explicit constexpr Float_t(const F32 num = 0.0f) noexcept : f(num) {}

    // Portable extraction of components.
    [[nodiscard]] constexpr bool Negative()    const noexcept { return (i >> 31) != 0; }
    [[nodiscard]] constexpr I32  RawMantissa() const noexcept { return i & ((1 << 23) - 1); }
    [[nodiscard]] constexpr I32  RawExponent() const noexcept { return (i >> 23) & 0xFF; }

    I32 i;
    F32 f;
};

union Double_t
{
    explicit constexpr Double_t(const D64 num = 0.0) noexcept : d(num) {}

    // Portable extraction of components.
    [[nodiscard]] constexpr bool Negative()    const noexcept { return (i >> 63) != 0; }
    [[nodiscard]] constexpr I64  RawMantissa() const noexcept { return i & ((1LL << 52) - 1); }
    [[nodiscard]] constexpr I64  RawExponent() const noexcept { return (i >> 52) & 0x7FF; }

    I64 i;
    D64 d;
};

[[nodiscard]]
constexpr bool ALMOST_EQUAL_ULPS_AND_ABS(const F32 A, const F32 B, const F32 maxDiff, const I32 maxUlpsDiff) noexcept
{
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const F32 absDiff = ABS(A - B);
    if (absDiff <= maxDiff)
    {
        return true;
    }

    const Float_t uA(A);
    const Float_t uB(B);

    // Different signs means they do not match.
    if (uA.Negative() != uB.Negative())
    {
        return false;
    }

    // Find the difference in ULPs.
    return ABS(uA.i - uB.i) <= maxUlpsDiff;
}

[[nodiscard]]
constexpr bool ALMOST_EQUAL_ULPS_AND_ABS(const D64 A, const D64 B, const D64 maxDiff, const I32 maxUlpsDiff) noexcept
{
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const D64 absDiff = ABS(A - B);
    if (absDiff <= maxDiff)
    {
        return true;
    }

    const Double_t uA(A);
    const Double_t uB(B);

    // Different signs means they do not match.
    if (uA.Negative() != uB.Negative())
    {
        return false;
    }

    // Find the difference in ULPs.
    return ABS(uA.i - uB.i) <= maxUlpsDiff;
}

[[nodiscard]]
constexpr bool ALMOST_EQUAL_RELATIVE_AND_ABS(const F32 A, const F32 B, const F32 maxDiff, const F32 maxRelDiff) noexcept
{
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const F32 diff = ABS(A - B);
    if (diff <= maxDiff)
    {
        return true;
    }

    const F32 largest = std::max(ABS(A), ABS(B));
    return diff <= largest * maxRelDiff;
}

[[nodiscard]]
constexpr bool ALMOST_EQUAL_RELATIVE_AND_ABS(D64 A, D64 B, const D64 maxDiff, const D64 maxRelDiff) noexcept
{
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const D64 diff = ABS(A - B);
    if (diff <= maxDiff)
    {
        return true;
    }

    A = ABS(A);
    B = ABS(B);
    const D64 largest = B > A ? B : A;

    return diff <= largest * maxRelDiff;
}

[[nodiscard]] constexpr bool COMPARE_TOLERANCE_ACCURATE(const F32 X, const F32 Y, const F32 TOLERANCE) noexcept
{
    return ALMOST_EQUAL_ULPS_AND_ABS(X, Y, TOLERANCE, 4);
}

[[nodiscard]] constexpr bool COMPARE_TOLERANCE_ACCURATE(const D64 X, const D64 Y, const D64 TOLERANCE) noexcept
{
    return ALMOST_EQUAL_ULPS_AND_ABS(X, Y, TOLERANCE, 4);
}

/// should be fast enough as the first condition is almost always true
template <typename T>
[[nodiscard]] constexpr bool IS_GEQUAL(const T X, const T Y) noexcept
{
    return X > Y || COMPARE(X, Y);
}
template <typename T>
[[nodiscard]] constexpr bool IS_LEQUAL(const T X, const T Y) noexcept
{
    return X < Y || COMPARE(X, Y);
}

template<typename T>
[[nodiscard]] constexpr T FLOOR(const T input)
{
    return input;
}

template<>
[[nodiscard]] constexpr F32 FLOOR(const F32 input)
{
    const I32 i = to_I32(input);
    const F32 f = to_F32(i);
    return (input >= 0.f ? f : (COMPARE(input, f) ? input : f - 1.f));
}

template<>
[[nodiscard]] constexpr D64 FLOOR(const D64 input)
{
    const I64 i = to_I64(input);
    const D64 f = to_D64(i);
    return (input >= 0.0 ? f : (COMPARE(input, f) ? input : f - 1.0));
}

template<>
[[nodiscard]] constexpr D128 FLOOR(const D128 input)
{
    const long long int i = static_cast<long long int>(input);
    const D128 f = to_D128(i);
    return (input >= 0.0 ? f : (COMPARE(input, f) ? input : f - 1.0));
}

template<typename T>
constexpr T CEIL(const T input)
{
    return input;
}

template<>
constexpr F32 CEIL(const F32 input)
{
    const I32 i = to_I32(input);
    return to_F32(input > i ? i + 1 : i);
}

template<>
constexpr D64 CEIL(const D64 input)
{
    const I64 i = to_I64(input);
    return to_D64(input > i ? i + 1 : i);
}

template<>
constexpr D128 CEIL(const D128 input)
{
    const long long int i = static_cast<long long int>(input);
    return to_D128(input > i ? i + 1 : i);
}

template <typename T, typename U = T>
[[nodiscard]] bool constexpr IS_IN_RANGE_INCLUSIVE(const T x, const U min, const U max) noexcept
{
    return x >= min && x <= max;
}

template <typename T, typename U = T>
[[nodiscard]] bool constexpr IS_IN_RANGE_EXCLUSIVE(const T x, const U min, const U max) noexcept
{
    return x > min && x < max;
}

///ref: http://blog.molecular-matters.com/2011/08/12/a-safer-static_cast/#more-120
// base template
template <bool IsFromSigned, bool IsToSigned>
struct safe_static_cast_helper;

// template specialization for casting from an unsigned type into an unsigned type
template <>
struct safe_static_cast_helper<false, false>
{
    template <typename TO, typename FROM>
    static TO cast(FROM from)
    {
        assert(IS_IN_RANGE_INCLUSIVE(std::is_enum_v<FROM> 
                                         ? to_U32(to_underlying_type(from))
                                         : from,
                                     std::numeric_limits<TO>::lowest(),
                                     std::numeric_limits<TO>::max()) &&
            "Number to cast exceeds numeric limits.");

        return static_cast<TO>(from);
    }
};

// template specialization for casting from an unsigned type into a signed type
template <>
struct safe_static_cast_helper<false, true>
{
    template <typename TO, typename FROM>
    static TO cast(FROM from)
    {
        assert(IS_IN_RANGE_INCLUSIVE(from,
                                     std::numeric_limits<TO>::lowest(),
                                     std::numeric_limits<TO>::max()) &&
            "Number to cast exceeds numeric limits.");

        return static_cast<TO>(from);
    }
};

// template specialization for casting from a signed type into an unsigned type
template <>
struct safe_static_cast_helper<true, false>
{
    template <typename TO, typename FROM>
    static TO cast(FROM from)
    {
        // make sure the input is not negative
        assert(from >= 0 && "Number to cast exceeds numeric limits.");

        // assuring a positive input, we can safely cast it into its unsigned type and check the numeric limits
        using UnsignedFrom = std::make_unsigned_t<FROM>;
        assert(IS_IN_RANGE_INCLUSIVE(static_cast<UnsignedFrom>(from),
                                     std::numeric_limits<TO>::lowest(),
                                     std::numeric_limits<TO>::max()) &&
            "Number to cast exceeds numeric limits.");
        return static_cast<TO>(from);
    }
};

// template specialization for casting from a signed type into a signed type
template <>
struct safe_static_cast_helper<true, true>
{
    template <typename TO, typename FROM>
    static TO cast(FROM from)
    {
        assert(IS_IN_RANGE_INCLUSIVE(std::is_enum_v<FROM> ? to_underlying_type(from) : from,
                                     std::numeric_limits<TO>::lowest(),
                                     std::numeric_limits<TO>::max()) &&
            "Number to cast exceeds numeric limits.");

        return static_cast<TO>(from);
    }
};


template <typename TO, typename FROM>
[[nodiscard]] TO safe_static_cast(FROM from)
{
#if defined(_DEBUG)
    // delegate the call to the proper helper class, depending on the signedness of both types
    return safe_static_cast_helper<std::numeric_limits<FROM>::is_signed,
                                   std::numeric_limits<TO>::is_signed>
           ::template cast<TO>(from);
#else
    return static_cast<TO>(from);
#endif
}

template <typename TO>
[[nodiscard]] TO safe_static_cast(F32 from)
{
    return static_cast<TO>(from);
}

template <typename TO>
[[nodiscard]] TO safe_static_cast(D64 from)
{
    return static_cast<TO>(from);
} 

extern void DIVIDE_ASSERT_MSG_BOX(std::string_view failMessage) noexcept;

namespace Assert
{
    /// It is safe to call evaluate expressions and call functions inside the assert check as it will compile for every build type
    bool Callback(bool expression, std::string_view expressionStr, std::string_view file, int line, std::string_view failMessage) noexcept;
}

#define DIVIDE_ASSERT_2_ARGS(expression, msg) Assert::Callback(expression, #expression, __FILE__, __LINE__, msg)
#define DIVIDE_ASSERT_1_ARGS(expression) DIVIDE_ASSERT_2_ARGS(expression, "UNEXPECTED CALL")

#define ___DETAIL_DIVIDE_ASSERT(...) EXP(GET_3RD_ARG(__VA_ARGS__, DIVIDE_ASSERT_2_ARGS, DIVIDE_ASSERT_1_ARGS, ))
#define DIVIDE_ASSERT(...) EXP(___DETAIL_DIVIDE_ASSERT(__VA_ARGS__)(__VA_ARGS__))

#define DIVIDE_UNEXPECTED_CALL_MSG(X) DIVIDE_ASSERT(false, X)
#define DIVIDE_UNEXPECTED_CALL() DIVIDE_UNEXPECTED_CALL_MSG("UNEXPECTED CALL")

#define DIVIDE_EXPECTED_CALL( X ) if ( !(X) ) [[unlikely]] { DIVIDE_UNEXPECTED_CALL_MSG("Expected call failed: " #X); }
#define DIVIDE_EXPECTED_CALL_MSG( X, MSG ) if ( !(X) ) [[unlikely]] { DIVIDE_UNEXPECTED_CALL_MSG( MSG ); }

template <typename Ret, typename... Args >
using DELEGATE_EASTL = eastl::function< Ret(Args...) >;

template <typename Ret, typename... Args >
using DELEGATE_STD = std::function< Ret(Args...) >;

template <typename Ret, typename... Args >
using DELEGATE = DELEGATE_STD<Ret, Args...>;

};  // namespace Divide

#endif //DVD_PLATFORM_DEFINES_H_
