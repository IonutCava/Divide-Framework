#include "stdafx.h"

#include "Headers/Console.h"

#include "Core/Time/Headers/ApplicationTimer.h"

#include <iostream>

namespace Divide {

SharedMutex Console::s_callbackLock;
vector<Console::ConsolePrintCallbackEntry> Console::s_guiConsoleCallbacks;

constexpr U32 DEFAULT_FLAGS = to_base( Console::Flags::DECORATE_TIMESTAMP ) |
                              to_base( Console::Flags::DECORATE_THREAD_ID ) |
                              to_base( Console::Flags::DECORATE_SEVERITY ) |
                              to_base( Console::Flags::ENABLE_OUTPUT ) |
                              to_base( Console::Flags::ENABLE_ERROR_STREAM );

U32 Console::s_flags = DEFAULT_FLAGS;
std::atomic_bool Console::s_running = false;

//Use moodycamel's implementation of a concurrent queue due to its "Knock-your-socks-off blazing fast performance."
//https://github.com/cameron314/concurrentqueue
namespace
{
    thread_local char textBuffer[CONSOLE_OUTPUT_BUFFER_SIZE + 1];

    std::array<Console::OutputEntry, 16> g_outputCache;

    moodycamel::BlockingConcurrentQueue<Console::OutputEntry>& OutBuffer()
    {
        static moodycamel::BlockingConcurrentQueue<Console::OutputEntry> s_OutputBuffer;
        return s_OutputBuffer;
    }
}

//! Do not remove the following license without express permission granted by DIVIDE-Studio
void Console::PrintCopyrightNotice()
{
    std::cout << "------------------------------------------------------------------------------\n"
              << "Copyright (c) 2018 DIVIDE-Studio\n"
              << "Copyright (c) 2009 Ionut Cava\n\n"
              << "This file is part of DIVIDE Framework.\n\n"
              << "Permission is hereby granted, free of charge, to any person obtaining a copy of this software\n"
              << "and associated documentation files (the 'Software'), to deal in the Software without restriction,\n"
              << "including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
              << "and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,\n"
              << "subject to the following conditions:\n\n"
              << "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n"
              << "THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,\n"
              << "INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n"
              << "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,\n"
              << "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE\n"
              << "OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n"
              << "For any problems or licensing issues I may have overlooked, please contact: \n"
              << "E-mail: ionut.cava@divide-studio.com | Website: \n http://wwww.divide-studio.com\n"
              << "-------------------------------------------------------------------------------\n\n";
}

const char* Console::FormatText( const char* format, ... ) noexcept
{
    va_list args;
    va_start( args, format );
    assert( _vscprintf( format, args ) + 1 < CONSOLE_OUTPUT_BUFFER_SIZE );
    vsprintf( textBuffer, format, args );
    va_end( args );
    return textBuffer;
}

void Console::DecorateAndPrint(std::ostream& outStream, const char* text, const bool newline, const EntryType type) {
    if (s_flags & to_base(Flags::DECORATE_TIMESTAMP)) [[likely]]
    {
        outStream << "[ " << std::internal
                          << std::setw(9)
                          << std::setprecision(3)
                          << std::setfill('0')
                          << std::fixed
                          << Time::App::ElapsedSeconds()
                  << " ] ";
    }

    if ( s_flags & to_base( Flags::DECORATE_THREAD_ID ) ) [[likely]]
    {
        outStream << "[ " << std::this_thread::get_id() << " ] ";
    }

    if ( s_flags & to_base( Flags::DECORATE_SEVERITY ) && (type == EntryType::WARNING || type == EntryType::ERR) )
    {
        outStream << (type == EntryType::ERR ? " Error: " : " Warning: ");
    }

    outStream << text;

    if (newline)
    {
        outStream << "\n";
    }
}

void Console::Output(std::ostream& outStream, const char* text, const bool newline, const EntryType type)
{
    if (s_flags & to_base(Flags::ENABLE_OUTPUT) ) [[likely]]
    {
        DecorateAndPrint(outStream, text, newline, type);
    }
}

void Console::Output(const char* text, const bool newline, const EntryType type)
{
    if ( s_flags & to_base( Flags::ENABLE_OUTPUT ) )
    {
        stringstream_fast outStream;
        DecorateAndPrint(outStream, text, newline, type);

        const OutputEntry entry
        {
            ._text = outStream.str().c_str(),
            ._type = type
        };

        if ( s_flags & to_base( Flags::PRINT_IMMEDIATE ) )
        {
            PrintToFile(entry);
        }
        else if (!OutBuffer().enqueue(entry))
        {
            PrintToFile(entry);
            DIVIDE_UNEXPECTED_CALL();
        }
    }
}

void Console::PrintToFile(const OutputEntry& entry)
{
    if ( s_running ) [[likely]]
    {
        auto& outStream = (entry._type == EntryType::ERR && s_flags & to_base( Flags::ENABLE_ERROR_STREAM ) ? std::cerr : std::cout);
        outStream << entry._text.c_str();

        SharedLock<SharedMutex> lock( s_callbackLock );
        for (const auto& it : s_guiConsoleCallbacks)
        {
            if (!s_running)
            {
                break;
            }

            it._cbk(entry);
        }
    }
}

void Console::Flush()
{
    if ( s_flags & to_base( Flags::ENABLE_OUTPUT ) && s_running) [[likely]]
    {

        size_t count{};
        do
        {
            count = OutBuffer().try_dequeue_bulk(std::begin(g_outputCache), g_outputCache.size());

            for (size_t i = 0u; i < count; ++i)
            {
                PrintToFile(g_outputCache[i]);
            }
        } while (count > 0u);
    }
}

void Console::Start() noexcept
{
    s_flags = DEFAULT_FLAGS;
    s_running.store(true);
}

void Console::Stop() 
{
    bool expected = true;
    if ( s_running.compare_exchange_strong(expected, false) )
    {
        Flush();
        s_flags = DEFAULT_FLAGS;
        std::cout << "------------------------------------------\n\n\n\n";
        std::cerr << std::flush;
        std::cout << std::flush;
    }
}

size_t Console::BindConsoleOutput( const ConsolePrintCallback& guiConsoleCallback )
{
    static size_t callbackId{ 0u };

    LockGuard<SharedMutex> lock( s_callbackLock );
    auto& entry = s_guiConsoleCallbacks.emplace_back();
    entry._cbk = guiConsoleCallback;
    entry._id = callbackId++;

    return entry._id;
}

bool Console::UnbindConsoleOutput( size_t& index )
{
    LockGuard<SharedMutex> lock( s_callbackLock );

    const size_t initialSize = s_guiConsoleCallbacks.size();
    erase_if( s_guiConsoleCallbacks, [index]( const ConsolePrintCallbackEntry& entry )
    {
        return entry._id == index;
    } );

    const bool erased = initialSize > s_guiConsoleCallbacks.size();
    if ( erased )
    {
        index = SIZE_MAX;
        return true;
    }

    return false;
}

} //namespace Divide

