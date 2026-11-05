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
#ifndef DVD_CORE_CONSOLE_H_
#define DVD_CORE_CONSOLE_H_

#include "Core/Headers/NonCopyable.h"
#include "Platform/Headers/PlatformDefines.h"

namespace Divide 
{

constexpr int MAX_CONSOLE_ENTRIES = 128;
struct Console : NonCopyable
{
    template <typename... T>
    NO_INLINE static void printfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void printf(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnf(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorf(std::string_view format, T&&... args);

    template <typename... T>
    NO_INLINE static void d_printfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printf(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnf(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorfn(std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorf(std::string_view format, T&&... args);

    template <typename... T>
    NO_INLINE static void printfn(std::ofstream& outStream, std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void printf(std::ofstream& outStream, std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnfn(std::ofstream& outStream, std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnf(std::ofstream& outStream, std::string_view format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorfn(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorf(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printfn(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printf(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnfn(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnf(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorfn(std::ofstream& outStream, std::string_view  format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorf(std::ofstream& outStream, std::string_view  format, T&&... args);


    enum class EntryType : U8
    {
        INFO = 0,
        WARNING,
        ERR,
        COMMAND,
        COUNT
    };

    enum class Flags : U16
    {
        DECORATE_TIMESTAMP = toBit( 1 ),
        DECORATE_THREAD_ID = toBit( 2 ),
        DECORATE_SEVERITY = toBit( 3 ),
        DECORATE_FRAME = toBit( 4 ),
        ENABLE_OUTPUT = toBit( 5 ),
        ENABLE_ERROR_STREAM = toBit( 6 ),
        PRINT_IMMEDIATE = toBit( 7 ),
        FLUSH_ON_PRINT = toBit( 8 ),
        PRINT_TO_STD_CONSOLE = toBit( 9 ),
        COUNT = 9
    };

    class ScopedFlags
    {
    public:
        ScopedFlags(U32 enabledMask, U32 disabledMask) noexcept;
        ~ScopedFlags() noexcept;

        ScopedFlags(const ScopedFlags&) = delete;
        ScopedFlags& operator=(const ScopedFlags&) = delete;
        ScopedFlags(ScopedFlags&&) = delete;
        ScopedFlags& operator=(ScopedFlags&&) = delete;

    private:
        U32 _previousEnabled;
        U32 _previousDisabled;
    };

    struct OutputEntry
    {
        string _text{};
        EntryType _type{ EntryType::INFO };
    };

    using ConsolePrintCallback = std::function<void( const OutputEntry& )>;
    struct ConsolePrintCallbackEntry 
    {
        ConsolePrintCallback _cbk;
        size_t _id{ SIZE_MAX };
    };

    static void Flush();
    static void Start( const ResourcePath& parentPath, std::string_view logFilePath, std::string_view errorFilePath, bool printCopyright ) noexcept;
    static void Stop();

    static void  ToggleFlag( const Flags flag, const bool state )
    {
        const U32 mask = to_U32( to_base(flag) );
        if ( state )
        {
            s_flags.fetch_or( mask, std::memory_order_relaxed );
        }
        else
        {
            s_flags.fetch_and( ~mask, std::memory_order_relaxed );
        }
    }

    [[nodiscard]] static bool IsFlagSet(Flags flag) noexcept;

    [[nodiscard]] static size_t BindConsoleOutput(const ConsolePrintCallback& guiConsoleCallback);
    [[nodiscard]] static bool   UnbindConsoleOutput(size_t& index);

    protected:
        static void Output(std::string_view text, bool newline, EntryType type);
        static void Output(std::ostream& outStream, std::string_view text, bool newline, EntryType type);
        static void DecorateAndPrint(std::ostream& outStream, std::string_view text, bool newline, EntryType type, U32 flags);
        static void PrintToFile(const OutputEntry& entry);

        static void FlushOutputStreams();
    private:
        [[nodiscard]] static U32 EffectiveFlags() noexcept;

        enum class StreamType : U8
        {
            STD_FILE = 0,
            ERR_FILE,
            COUNT
        };

        static std::ofstream                     s_logStream;
        static std::ofstream                     s_errorStream;
        static vector<ConsolePrintCallbackEntry> s_guiConsoleCallbacks;
        static std::atomic<U32>                  s_flags;
        static std::atomic_bool                  s_running;
        static std::array<bool, to_base(StreamType::COUNT)> s_streamDirty;
};

namespace Names
{
    static const char* consoleEntryType[] = {
        "INFO",
        "WARNING",
        "ERROR",
        "COMMAND",
        "UNKNOWN"
    };
} //namespace Names

static_assert(std::size(Names::consoleEntryType) == to_base(Console::EntryType::COUNT) + 1u, "EntryType name array out of sync!");

namespace TypeUtil
{
    [[nodiscard]] inline const char* ConsoleEntryTypeToString(const Console::EntryType type) noexcept
    {
        return Names::consoleEntryType[to_base(type)];
    }
}

}  // namespace Divide

#endif  //DVD_CORE_CONSOLE_H_

#include "Console.inl"
