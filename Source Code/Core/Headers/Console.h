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
#ifndef _CORE_CONSOLE_H_
#define _CORE_CONSOLE_H_

namespace Divide {

constexpr int CONSOLE_OUTPUT_BUFFER_SIZE = 4096 * 16 * 2;
constexpr int MAX_CONSOLE_ENTRIES = 128;

struct Console : NonCopyable
{
    template <typename... T>
    NO_INLINE static void printfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void printf(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnf(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorf(const char* format, T&&... args);

    template <typename... T>
    NO_INLINE static void d_printfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printf(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnf(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorfn(const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorf(const char* format, T&&... args);

    template <typename... T>
    NO_INLINE static void printfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void printf(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void warnf(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void errorf(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_printf(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_warnf(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorfn(std::ofstream& outStream, const char* format, T&&... args);
    template <typename... T>
    NO_INLINE static void d_errorf(std::ofstream& outStream, const char* format, T&&... args);


    enum class EntryType : U8
    {
        INFO = 0,
        WARNING,
        ERR,
        COMMAND
    };

    enum class Flags : U8
    {
        DECORATE_TIMESTAMP = toBit( 1 ),
        DECORATE_THREAD_ID = toBit( 2 ),
        DECORATE_SEVERITY = toBit( 3 ),
        ENABLE_OUTPUT = toBit( 4 ),
        ENABLE_ERROR_STREAM = toBit( 5 ),
        PRINT_IMMEDIATE = toBit(6),
        COUNT = 6
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
    static void Start() noexcept;
    static void Stop();

                  static void  ToggleFlag( const Flags flag, const bool state ) { ToggleBit(s_flags, flag, state); }
    [[nodiscard]] static bool  IsFlagSet( const Flags flag ) { return TestBit(s_flags, flag); }

    [[nodiscard]] static size_t BindConsoleOutput(const ConsolePrintCallback& guiConsoleCallback);
    [[nodiscard]] static bool   UnbindConsoleOutput(size_t& index);

    static void PrintCopyrightNotice();

    protected:
        static void Output(const char* text, bool newline, EntryType type);
        static void Output(std::ostream& outStream, const char* text, bool newline, EntryType type);
        static void DecorateAndPrint(std::ostream& outStream, const char* text, bool newline, EntryType type);
        static void PrintToFile(const OutputEntry& entry);

        [[nodiscard]] static const char* FormatText( const char* format, ... ) noexcept;

    private:
        static SharedMutex                       s_callbackLock;
        static vector<ConsolePrintCallbackEntry> s_guiConsoleCallbacks;
        static U32                               s_flags;
        static std::                             atomic_bool s_running;
};

}  // namespace Divide

#endif  //_CORE_CONSOLE_H_

#include "Console.inl"
