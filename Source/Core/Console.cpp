

#include "Headers/Console.h"

#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include <iostream>

namespace Divide {

SharedMutex Console::s_callbackLock;
NO_DESTROY  vector<Console::ConsolePrintCallbackEntry> Console::s_guiConsoleCallbacks;
std::ofstream Console::s_logStream;
std::ofstream Console::s_errorStream;

constexpr U32 DEFAULT_FLAGS = to_base( Console::Flags::DECORATE_TIMESTAMP ) |
                              to_base( Console::Flags::DECORATE_THREAD_ID ) |
                              to_base( Console::Flags::DECORATE_SEVERITY ) |
                              to_base( Console::Flags::DECORATE_FRAME ) |
                              to_base( Console::Flags::ENABLE_OUTPUT ) |
                              to_base( Console::Flags::ENABLE_ERROR_STREAM );

U32 Console::s_flags = DEFAULT_FLAGS;
std::atomic_bool Console::s_running = false;

//Use moodycamel's implementation of a concurrent queue due to its "Knock-your-socks-off blazing fast performance."
//https://github.com/cameron314/concurrentqueue
namespace
{
    NO_DESTROY std::array<Console::OutputEntry, 16> g_outputCache;

    moodycamel::BlockingConcurrentQueue<Console::OutputEntry>& OutBuffer()
    {
        NO_DESTROY static moodycamel::BlockingConcurrentQueue<Console::OutputEntry> s_OutputBuffer;
        return s_OutputBuffer;
    }
}

void Console::DecorateAndPrint(std::ostream& outStream, const std::string_view text, const bool newline, const EntryType type) {
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
    if ( s_flags & to_base( Flags::DECORATE_FRAME ) ) [[likely]]
    {
        outStream << "[ " << GFXDevice::FrameCount() << " ] ";
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

void Console::Output(std::ostream& outStream, const std::string_view text, const bool newline, const EntryType type)
{
    if (s_flags & to_base(Flags::ENABLE_OUTPUT) ) [[likely]]
    {
        DecorateAndPrint(outStream, text, newline, type);
    }
}

void Console::Output(const std::string_view text, const bool newline, const EntryType type)
{
    if ( s_flags & to_base( Flags::ENABLE_OUTPUT ) )
    {
        stringstream outStream;
        DecorateAndPrint(outStream, text, newline, type);

        const OutputEntry entry
        {
            ._text = outStream.str().c_str(),
            ._type = type
        };

        if ( IsFlagSet( Flags::PRINT_IMMEDIATE ) )
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
        std::ofstream& outStream = (entry._type == EntryType::ERR && s_flags & to_base( Flags::ENABLE_ERROR_STREAM ) ? s_errorStream : s_logStream );
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

void Console::FlushOutputStreams()
{
    s_logStream.flush();
    s_errorStream.flush();
    std::cerr << std::flush;
    std::cout << std::flush;
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

        FlushOutputStreams();
    }
}

void Console::Start( const std::string_view logFilePath, const std::string_view erroFilePath, const bool printCopyright ) noexcept
{
    s_flags = DEFAULT_FLAGS;
    s_running.store(true);

    s_logStream   = std::ofstream{ (Paths::g_logPath / logFilePath).fileSystemPath(),  std::ofstream::out | std::ofstream::trunc };
    s_errorStream = std::ofstream{ (Paths::g_logPath / erroFilePath).fileSystemPath(), std::ofstream::out | std::ofstream::trunc };

    std::cout.rdbuf( s_logStream.rdbuf() );
    std::cerr.rdbuf( s_errorStream.rdbuf() );

    //! Do not remove the following license without express permission granted by Divide-Studio or Ionut Cava
    if ( printCopyright )
    {
        s_logStream << "------------------------------------------------------------------------------\n"
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
}

void Console::Stop() 
{
    bool expected = true;
    if ( s_running.compare_exchange_strong(expected, false) )
    {
        Flush();
        s_flags = DEFAULT_FLAGS;
        s_logStream << "------------------------------------------\n\n\n\n";
        FlushOutputStreams();
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

