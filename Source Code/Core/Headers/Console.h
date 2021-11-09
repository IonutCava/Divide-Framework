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

class Console : NonCopyable {
   public:
     enum class EntryType : U8 {
         INFO = 0,
         WARNING,
         ERR,
         COMMAND
    };

    struct OutputEntry {
        string _text{};
        EntryType _type = EntryType::INFO;
    };

    using ConsolePrintCallback = std::function<void(const OutputEntry&)>;

   public:
    static void flush();
    static void start() noexcept;
    static void stop();

    static void printCopyrightNotice();

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

    [[nodiscard]]
    static bool timeStampsEnabled() noexcept { return _timestamps; }
    static void toggleTimeStamps(const bool state) noexcept { _timestamps = state; }

    [[nodiscard]]
    static bool threadIDEnabled() noexcept { return _threadID; }
    static void togglethreadID(const bool state) noexcept { _threadID = state; }

    static void toggleTextDecoration(const bool state) noexcept { toggleTimeStamps(state); togglethreadID(state); }

    [[nodiscard]]
    static bool enabled() noexcept { return _enabled; }
    static void toggle(const bool state) noexcept { _enabled = state; }

    [[nodiscard]]
    static bool immediateModeEnabled() noexcept { return _immediateMode; }
    static void toggleImmediateMode(const bool state) noexcept { _immediateMode = state; }

    [[nodiscard]]
    static bool errorStreamEnabled() noexcept { return _errorStreamEnabled; }
    static void toggleErrorStream(const bool state) noexcept { _errorStreamEnabled = state; }

    [[nodiscard]]
    static size_t bindConsoleOutput(const ConsolePrintCallback& guiConsoleCallback) {
        _guiConsoleCallbacks.push_back(guiConsoleCallback);
        return _guiConsoleCallbacks.size() - 1;
    }

    [[nodiscard]]
    static bool unbindConsoleOutput(size_t& index) {
        if (index < _guiConsoleCallbacks.size()) {
            _guiConsoleCallbacks.erase(std::begin(_guiConsoleCallbacks) + index);
            index = std::numeric_limits<size_t>::max();
            return true;
        }
        return false;
    }

   protected:
    [[nodiscard]]
    static const char* formatText(const char* format, ...) noexcept;
    static void output(const char* text, bool newline, EntryType type);
    static void output(std::ostream& outStream, const char* text, bool newline, EntryType type);
    static void decorate(std::ostream& outStream, const char* text, bool newline, EntryType type);
    static void printToFile(const OutputEntry& entry);

   private:
    static vector<ConsolePrintCallback> _guiConsoleCallbacks;
    static bool _timestamps;
    static bool _threadID;
    static bool _enabled;
    static bool _immediateMode;
    static bool _errorStreamEnabled;

    static std::atomic_bool _running;

};

}  // namespace Divide

#endif  //_CORE_CONSOLE_H_

#include "Console.inl"
