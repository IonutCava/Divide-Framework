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
#ifndef _PLATFORM_DEFINES_H_
#define _PLATFORM_DEFINES_H_

#include "config.h"
#include "PlatformMemoryDefines.h"
#include "Core/Headers/ErrorCodes.h"

#define EXP( x ) x

#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define GET_4TH_ARG(arg1, arg2, arg3, arg4, ...) arg4
#define GET_5TH_ARG(arg1, arg2, arg3, arg4, arg5, ...) arg5
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6

#if defined(_DEBUG)
#define STUBBED(x)                                  \
do {                                                \
    static bool seen_this = false;                  \
    if (!seen_this) {                               \
        seen_this = true;                           \
        Console::errorfn("STUBBED: %s (%s : %d)\n", \
                         x, __FILE__, __LINE__);    \
    }                                               \
} while (0);

#else
#define STUBBED(x)
#endif

#ifndef TO_STRING
#define TO_STRING_NAME(X) #X
#define TO_STRING(X) TO_STRING_NAME(X)
#endif //TO_STRING

/// Makes writing and reading smart pointer types easier and cleaner
/// Faster to write ClassName_uptr than eastl::unique_ptr<ClassName>
/// Also cleaner and easier to read the managed type in nested templated parameters 
/// e.g. std::pair<ClassNameA_uptr, ClassNameB_wptr> vs std::pair<eastl::unique_ptr<ClassNameA>, std::weak_ptr<ClassNameB>>
/// Also makes it easier to switch smart pointer implementations (i.e. eastl for unique_ptr) using a centralized location
#define TYPEDEF_SMART_POINTERS_FOR_TYPE(T)       \
    using T ## _wptr  = std::weak_ptr<T>;        \
    using T ## _ptr   = std::shared_ptr<T>;      \
    using T ## _cwptr = std::weak_ptr<const T>;  \
    using T ## _cptr  = std::shared_ptr<const T>;\
    using T ## _uptr  = eastl::unique_ptr<T>;


#define FWD_DECLARE_MANAGED_CLASS(T)      \
    class T;                              \
    TYPEDEF_SMART_POINTERS_FOR_TYPE(T);

#define FWD_DECLARE_MANAGED_STRUCT(T)     \
    struct T;                             \
    TYPEDEF_SMART_POINTERS_FOR_TYPE(T);


#if !defined(if_constexpr)
#define if_constexpr if constexpr
#endif

#define CONCATENATE_IMPL(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#ifdef __COUNTER__
#define ANONYMOUS_VARIABLE(str) \
    CONCATENATE(str, __COUNTER__)
#else
#define ANONYMOUSE_VARIABLE(str) \
    CONCATENATE(str, __LINE__)
#endif

 // Multumesc Andrei A.!
#if defined(_MSC_VER)
#define _FUNCTION_NAME_AND_SIG_ __FUNCSIG__
#elif defined(__GNUC__)
#define _FUNCTION_NAME_AND_SIG_ __PRETTY_FUNCTION__
#else
#define _FUNCTION_NAME_AND_SIG_ __FUNCTION__
#endif

 //ref: https://foonathan.net/2020/09/move-forward/
 // static_cast to rvalue reference
#define MOV(...) \
static_cast<std::remove_reference_t<decltype(__VA_ARGS__)>&&>(__VA_ARGS__)

// static_cast to identity
// The extra && aren't necessary as discussed above, but make it more robust in case it's used with a non-reference.
#define FWD(...) \
  static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

#define ALIAS_TEMPLATE_FUNCTION(highLevelF, lowLevelF) \
template<typename... Args> \
constexpr auto highLevelF(Args&&... args) -> decltype(lowLevelF(FWD(args)...)) \
{ \
    return lowLevelF(FWD(args)...); \
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
    template <typename T, typename = std::enable_if_t <
        std::is_invocable<T&(TArgs...)>{} &&
        !std::is_same<std::decay_t<T>, function_view>{} >>
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

constexpr U32 val_32_const = 0x811c9dc5;
constexpr U32 prime_32_const = 0x1000193;
constexpr U64 val_64_const = 0xcbf29ce484222325;
constexpr U64 prime_64_const = 0x100000001b3;

[[nodiscard]] constexpr U64 _ID(const char* const str, const U64 value = val_64_const) noexcept {
    return str[0] == '\0' ? value : _ID(&str[1], (value ^ to_U64(str[0])) * prime_64_const);
}

[[nodiscard]] constexpr U64 _ID_VIEW(const char* const str, const size_t len, const U64 value = val_64_const) noexcept {
    return len == 0 ? value : _ID_VIEW(&str[1], len - 1, (value ^ to_U64(str[0])) * prime_64_const);
}

[[nodiscard]] constexpr U64 operator ""_id(const char* str, const size_t len) {
    return _ID_VIEW(str, len);
}

struct SysInfo {
    SysInfo() noexcept;

    size_t _availableRamInBytes;
    int _systemResolutionWidth;
    int _systemResolutionHeight;
    string _workingDirectory;
};

[[nodiscard]] SysInfo& sysInfo() noexcept;
[[nodiscard]] const SysInfo& const_sysInfo() noexcept;

void InitSysInfo(SysInfo& info, I32 argc, char** argv);

extern [[nodiscard]] F32 PlatformDefaultDPI() noexcept;

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

extern void SetThreadPriority(std::thread* thread, ThreadPriority priority);
extern void SetThreadPriority(ThreadPriority priority);

extern void SetThreadName(std::thread* thread, const char* threadName);
extern void SetThreadName(const char* threadName) noexcept;

extern bool CallSystemCmd(const char* cmd, const char* args);

[[nodiscard]] bool CreateDirectories(const char* path);
[[nodiscard]] bool CreateDirectories(const ResourcePath& path);

bool DebugBreak(bool condition = true) noexcept;

[[nodiscard]] ErrorCode PlatformInit(int argc, char** argv);
[[nodiscard]] bool PlatformClose();
[[nodiscard]] bool GetAvailableMemory(SysInfo& info);

[[nodiscard]] ErrorCode PlatformPreInit(int argc, char** argv);
[[nodiscard]] ErrorCode PlatformPostInit(int argc, char** argv);

[[nodiscard]] ErrorCode PlatformInitImpl(int argc, char** argv) noexcept;
[[nodiscard]] bool PlatformCloseImpl() noexcept;

[[nodiscard]] const char* GetClipboardText(void* user_data) noexcept;
void SetClipboardText(void* user_data, const char* text) noexcept;

void ToggleCursor(bool state) noexcept;

[[nodiscard]] bool CursorState() noexcept;

/// Converts an arbitrary positive integer value to a bitwise value used for masks
template<typename T>
[[nodiscard]] constexpr T toBit(const T X) {
    return 1 << X;
}

[[nodiscard]] constexpr U32 powerOfTwo(U32 X) noexcept {
    U32 r = 0u;
    while (X >>= 1) {
        r++;
    }
    return r;
}

template<typename T,
typename = typename std::enable_if<std::is_integral<T>::value>::type,
typename = typename std::enable_if<std::is_unsigned<T>::value>::type>
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
template< typename C, typename = void >
struct has_reserve
    : std::false_type
{};

template< typename C >
struct has_reserve< C, typename std::enable_if<
                                    std::is_same<
                                        decltype(std::declval<C>().reserve(std::declval<typename C::size_type>())),
                                        void
                                    >::value
                                >::type >
    : std::true_type
{};

template< typename C, typename = void >
struct has_emplace_back
    : std::false_type
{};

template< typename C >
struct has_emplace_back< C, typename std::enable_if<
                                        std::is_same<
                                            decltype(std::declval<C>().emplace_back(std::declval<typename C::value_type>())),
                                            void
                                        >::value
                                    >::type >
    : std::true_type
{};


template<typename>
static constexpr std::false_type has_assign(...) { 
    return std::false_type();
};

//ref: https://github.com/ParBLiSS/kmerind/blob/master/src/utils/container_traits.hpp
template<typename T>
static constexpr auto has_assign(T*) ->
decltype(std::declval<T>().assign(std::declval<decltype(std::declval<T>().begin())>(),
                                  std::declval<decltype(std::declval<T>().end())>()), std::true_type()) {
    return std::true_type();
};

template< typename C >
std::enable_if_t< !has_reserve< C >::value > optional_reserve(C&, std::size_t) {}

template< typename C >
std::enable_if_t< has_reserve< C >::value > optional_reserve(C& c, std::size_t n) {
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

/* See
http://randomascii.wordpress.com/2012/01/11/tricks-with-the-floating-point-format/
for the potential portability problems with the union and bit-fields below.
*/
union Float_t {
    explicit Float_t(const F32 num = 0.0f) noexcept : f(num) {}

    // Portable extraction of components.
    [[nodiscard]] bool Negative() const noexcept { return (i >> 31) != 0; }
    [[nodiscard]] I32 RawMantissa() const noexcept { return i & ((1 << 23) - 1); }
    [[nodiscard]] I32 RawExponent() const noexcept { return (i >> 23) & 0xFF; }

    I32 i;
    F32 f;
};

union Double_t {
    explicit Double_t(const D64 num = 0.0) noexcept : d(num) {}

    // Portable extraction of components.
    [[nodiscard]] bool Negative() const noexcept { return (i >> 63) != 0; }
    [[nodiscard]] I64 RawMantissa() const noexcept { return i & ((1LL << 52) - 1); }
    [[nodiscard]] I64 RawExponent() const noexcept { return (i >> 52) & 0x7FF; }

    I64 i;
    D64 d;
};

[[nodiscard]]
inline bool AlmostEqualUlpsAndAbs(const F32 A, const F32 B, const F32 maxDiff, const I32 maxUlpsDiff) noexcept {
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const F32 absDiff = std::abs(A - B);
    if (absDiff <= maxDiff) {
        return true;
    }

    const Float_t uA(A);
    const Float_t uB(B);

    // Different signs means they do not match.
    if (uA.Negative() != uB.Negative()) {
        return false;
    }

    // Find the difference in ULPs.
    return std::abs(uA.i - uB.i) <= maxUlpsDiff;
}

[[nodiscard]]
inline bool AlmostEqualUlpsAndAbs(const D64 A, const D64 B, const D64 maxDiff, const I32 maxUlpsDiff) noexcept {
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const D64 absDiff = std::abs(A - B);
    if (absDiff <= maxDiff) {
        return true;
    }

    const Double_t uA(A);
    const Double_t uB(B);

    // Different signs means they do not match.
    if (uA.Negative() != uB.Negative()) {
        return false;
    }

    // Find the difference in ULPs.
    return std::abs(uA.i - uB.i) <= maxUlpsDiff;
}

[[nodiscard]]
inline bool AlmostEqualRelativeAndAbs(const F32 A, const F32 B, const F32 maxDiff, const F32 maxRelDiff)  noexcept {
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const F32 diff = std::abs(A - B);
    if (diff <= maxDiff) {
        return true;
    }

    const F32 largest = std::max(std::abs(A), std::abs(B));
    return diff <= largest * maxRelDiff;
}

[[nodiscard]]
inline bool AlmostEqualRelativeAndAbs(D64 A, D64 B, const D64 maxDiff, const D64 maxRelDiff) noexcept {
    // Check if the numbers are really close -- needed when comparing numbers near zero.
    const D64 diff = std::abs(A - B);
    if (diff <= maxDiff) {
        return true;
    }

    A = std::abs(A);
    B = std::abs(B);
    const D64 largest = B > A ? B : A;

    return diff <= largest * maxRelDiff;
}

constexpr void NOP() noexcept {}

//Andrei Alexandrescu's ScopeGuard macros from "Declarative Control Flow" (CppCon 2015)
//ref: https://gist.github.com/mmha/6bee3983caf2eab04d80af8e0eaddfbe
namespace detail {
    enum class ScopeGuardOnExit{};
    enum class ScopeGuardOnFail{};
    enum class ScopeGuardOnSuccess{};

    template <typename Fun>
    class ScopeGuard
    {
        public:
            ScopeGuard(Fun &&fn) noexcept : fn(MOV(fn)) {}
            ~ScopeGuard() { fn(); }
        private:
            Fun fn;
    };

    class UncaughtExceptionCounter
    {
        int exceptionCount_;
    public:
        UncaughtExceptionCounter() noexcept : exceptionCount_(std::uncaught_exceptions()) {}
        bool newUncaughtException() noexcept { return std::uncaught_exceptions() > exceptionCount_; }
    };

    template <typename FunctionType, bool executeOnException>
    class ScopeGuardForNewException
    {
        FunctionType function_;
        UncaughtExceptionCounter ec_;
    public:
        explicit ScopeGuardForNewException(const FunctionType &fn) : function_(fn) {}
        explicit ScopeGuardForNewException(FunctionType &&fn) : function_(std::move(fn)) {}
        ~ScopeGuardForNewException() noexcept(executeOnException) {
            if (executeOnException == ec_.newUncaughtException()) {
                function_();
            }
        }
    };

    template <typename Fun>
    auto operator+(ScopeGuardOnExit, Fun &&fn) noexcept {
        return ScopeGuard<Fun>(FWD(fn));
    }

    template <typename Fun>
    auto operator+(ScopeGuardOnFail, Fun &&fn) 	{
        return ScopeGuardForNewException<std::decay_t<Fun>, true>(FWD(fn));
    }

    template <typename Fun>
    auto operator+(ScopeGuardOnSuccess, Fun &&fn) 	{
        return ScopeGuardForNewException<std::decay_t<Fun>, false>(FWD(fn));
    }
} //namespace detail

#define SCOPE_FAIL auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = detail::ScopeGuardOnFail() + [&]() noexcept
#define SCOPE_SUCCESS auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = detail::ScopeGuardOnSuccess() + [&]()
#define SCOPE_EXIT auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) = detail::ScopeGuardOnExit() + [&]() noexcept

constexpr F32 EPSILON_F32 = std::numeric_limits<F32>::epsilon();
constexpr D64 EPSILON_D64 = std::numeric_limits<D64>::epsilon();

template <typename T, typename U = T>
[[nodiscard]] bool IS_IN_RANGE_INCLUSIVE(const T x, const U min, const U max) noexcept {
    return x >= min && x <= max;
}
template <typename T, typename U = T>
[[nodiscard]] bool IS_IN_RANGE_EXCLUSIVE(const T x, const U min, const U max) noexcept {
    return x > min && x < max;
}

template <typename T>
[[nodiscard]] bool IS_ZERO(const T X) noexcept {
    return X == 0;
}

template <>
[[nodiscard]] inline bool IS_ZERO(const F32 X) noexcept {
    return abs(X) < EPSILON_F32;
}
template <>
[[nodiscard]] inline bool IS_ZERO(const D64 X) noexcept {
    return abs(X) < EPSILON_D64;
}

template <typename T>
[[nodiscard]] bool IS_TOLERANCE(const T X, const T TOLERANCE) noexcept {
    return abs(X) <= TOLERANCE;
}

template<typename T, typename U = T>
[[nodiscard]] bool COMPARE_TOLERANCE(const T X, const U Y, const T TOLERANCE) noexcept {
    return abs(X - static_cast<T>(Y)) <= TOLERANCE;
}

template<typename T, typename U = T>
[[nodiscard]] bool COMPARE_TOLERANCE_ACCURATE(const T X, const T Y, const T TOLERANCE) noexcept {
    return COMPARE_TOLERANCE(X, Y, TOLERANCE);
}

template<>
[[nodiscard]] inline bool COMPARE_TOLERANCE_ACCURATE(const F32 X, const F32 Y, const F32 TOLERANCE) noexcept {
    return AlmostEqualUlpsAndAbs(X, Y, TOLERANCE, 4);
}

template<>
[[nodiscard]] inline bool COMPARE_TOLERANCE_ACCURATE(const D64 X, const D64 Y, const D64 TOLERANCE) noexcept {
    return AlmostEqualUlpsAndAbs(X, Y, TOLERANCE, 4);
}

template<typename T, typename U = T>
[[nodiscard]] bool COMPARE(T X, U Y) noexcept {
    return X == static_cast<T>(Y);
}

template<>
[[nodiscard]] inline bool COMPARE(const F32 X, const F32 Y) noexcept {
    return COMPARE_TOLERANCE(X, Y, EPSILON_F32);
}

template<>
[[nodiscard]] inline bool COMPARE(const D64 X, const D64 Y) noexcept {
    return COMPARE_TOLERANCE(X, Y, EPSILON_D64);
}

/// should be fast enough as the first condition is almost always true
template <typename T>
[[nodiscard]] bool IS_GEQUAL(T X, T Y) noexcept {
    return X > Y || COMPARE(X, Y);
}
template <typename T>
[[nodiscard]] bool IS_LEQUAL(T X, T Y) noexcept {
    return X < Y || COMPARE(X, Y);
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
        assert(IS_IN_RANGE_INCLUSIVE(std::is_enum<FROM>::value 
                                         ? static_cast<U32>(to_underlying_type(from))
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
        using UnsignedFrom = typename std::make_unsigned<FROM>::type;
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
        assert(IS_IN_RANGE_INCLUSIVE(std::is_enum<FROM>::value ? to_underlying_type(from) : from,
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

extern void DIVIDE_ASSERT_MSG_BOX(const char* failMessage) noexcept;

namespace Assert {
    constexpr size_t ASSERT_MSG_BUFFER_SIZE = 512;
    thread_local static char ASSERT_TEXT_BUFFER[ASSERT_MSG_BUFFER_SIZE + 1];

    inline const char* FormatText(const char* format, ...) noexcept {
        va_list args;
        va_start(args, format);
        SCOPE_EXIT{
            va_end(args);
        };
        if (to_size(_vscprintf(format, args)) + 1 < ASSERT_MSG_BUFFER_SIZE) {
            vsprintf(ASSERT_TEXT_BUFFER, format, args);
            return ASSERT_TEXT_BUFFER;
        }

        return "";
    }

    /// It is safe to call evaluate expressions and call functions inside the assert check as it will compile for every build type
    bool DIVIDE_ASSERT_FUNC(bool expression, const char* file, int line, const char* failMessage) noexcept;
}

#define DIVIDE_ASSERT_2_ARGS(expression, msg) Assert::DIVIDE_ASSERT_FUNC(expression, __FILE__, __LINE__, msg)
#define DIVIDE_ASSERT_1_ARGS(expression) DIVIDE_ASSERT_2_ARGS(expression, "UNEXPECTED CALL")

#define ___DETAIL_DIVIDE_ASSERT(...) EXP(GET_3RD_ARG(__VA_ARGS__, DIVIDE_ASSERT_2_ARGS, DIVIDE_ASSERT_1_ARGS, ))
#define DIVIDE_ASSERT(...) EXP(___DETAIL_DIVIDE_ASSERT(__VA_ARGS__)(__VA_ARGS__))

#define DIVIDE_UNEXPECTED_CALL_MSG(X) DIVIDE_ASSERT(false, X)
#define DIVIDE_UNEXPECTED_CALL() DIVIDE_UNEXPECTED_CALL_MSG("UNEXPECTED CALL")

                                    
template <typename Ret, typename... Args >
using DELEGATE_EASTL = eastl::function< Ret(Args...) >;

template <typename Ret, typename... Args >
using DELEGATE_STD = std::function< Ret(Args...) >;

template <typename Ret, typename... Args >
using DELEGATE = DELEGATE_STD<Ret, Args...>;

[[nodiscard]] U32 HardwareThreadCount() noexcept;

template<typename T, typename U>
constexpr void assert_type(const U& ) {
    static_assert(std::is_same<U, T>::value, "value type not satisfied");
}

/// Wrapper that allows usage of atomic variables in containers
/// Copy is not atomic! (e.g. push/pop from containers is not threadsafe!)
/// ref: http://stackoverflow.com/questions/13193484/how-to-declare-a-vector-of-atomic-in-c
template <typename T>
struct AtomicWrapper : private NonMovable
{
    std::atomic<T> _a;

    AtomicWrapper() : _a() {}
    explicit AtomicWrapper(const std::atomic<T> &a) :_a(a.load()) {}
    AtomicWrapper(const AtomicWrapper &other) : _a(other._a.load()) { }
    ~AtomicWrapper() = default;

    AtomicWrapper &operator=(const AtomicWrapper &other) {
        _a.store(other._a.load());
        return *this;
    }

    AtomicWrapper &operator=(const T &value) {
        _a.store(value);
        return *this;
    }

    bool operator==(const T &value) const {
        return _a == value;
    }
};

};  // namespace Divide

#endif //_PLATFORM_DEFINES_H_