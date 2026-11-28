// Copyright (c) 2026 DIVIDE-Studio


#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include <iostream>

namespace Divide {

NO_DESTROY  vector<Console::ConsolePrintCallbackEntry> Console::s_guiConsoleCallbacks;
std::ofstream Console::s_logStream;
std::ofstream Console::s_errorStream;

constexpr U32 DEFAULT_FLAGS = to_base( Console::Flags::DECORATE_TIMESTAMP ) |
                              to_base( Console::Flags::DECORATE_THREAD_ID ) |
                              to_base( Console::Flags::DECORATE_SEVERITY ) |
                              to_base( Console::Flags::DECORATE_FRAME ) |
                              to_base( Console::Flags::ENABLE_OUTPUT ) |
//#if defined(SHOW_CONSOLE_WINDOW)
                              to_base( Console::Flags::PRINT_TO_STD_CONSOLE ) |
                              to_base( Console::Flags::FLUSH_ON_PRINT ) |
//#endif //SHOW_CONSOLE_WINDOW
                              to_base( Console::Flags::ENABLE_ERROR_STREAM );

std::atomic<U32> Console::s_flags = DEFAULT_FLAGS;
std::atomic_bool Console::s_running = false;
std::array<bool, to_base(Console::StreamType::COUNT)> Console::s_streamDirty;

//Use moodycamel's implementation of a concurrent queue due to its "Knock-your-socks-off blazing fast performance."
//https://github.com/cameron314/concurrentqueue
namespace
{
    struct QueuedEntry
    {
        U64 _ticket{ 0u };
        Console::OutputEntry _entry;
    };

    // g_outputMutex guards flushing, the file streams, the callback list and the plain flags below.
    // They are only ever touched by the thread that holds it, which is also the only thread that can see g_dispatching set.
    NO_DESTROY std::array<QueuedEntry, 16> g_outputCache;
    NO_DESTROY vector<QueuedEntry> g_drainScratch;
    NO_DESTROY Mutex g_outputMutex;
    size_t g_nextCallbackId = 0u;
    bool g_flushRequested = false;
    bool g_stopping = false;
    bool g_stopRequested = false;
    std::atomic<U64> g_entryTicket{ 0u };
    thread_local bool t_dispatching = false;
    thread_local U32 g_enabledOverrides = 0u;
    thread_local U32 g_disabledOverrides = 0u;

    using OutputQueue = moodycamel::ConcurrentQueue<QueuedEntry>;

    OutputQueue& OutBuffer()
    {
        NO_DESTROY static OutputQueue s_OutputBuffer;
        return s_OutputBuffer;
    }

    // Per-thread order is the queue's FIFO order; the ticket restores cross-thread order when a batch is flushed.
    void EnqueueOutput(const Console::OutputEntry& entry)
    {
        OutputQueue& outputQueue = OutBuffer();
        thread_local moodycamel::ProducerToken producer{ outputQueue };
        const U64 ticket = g_entryTicket.fetch_add(1u, std::memory_order_relaxed);
        if (!outputQueue.enqueue(producer, QueuedEntry{ ticket, entry }))
        {
            ::fprintf(stderr, "Console output queue allocation failed: %s", entry._text.c_str());
            ::fflush(stderr);
            std::abort();
        }
    }

    // A thread that is already dispatching holds g_outputMutex, so re-entrant calls must not lock it again.
    [[nodiscard]] UniqueLock<Mutex> LockOutput()
    {
        UniqueLock<Mutex> lock( g_outputMutex, std::defer_lock );
        if (!t_dispatching)
        {
            lock.lock();
        }
        return lock;
    }
}

Console::ScopedFlags::ScopedFlags(const U32 enabledMask, const U32 disabledMask) noexcept
    : _previousEnabled(g_enabledOverrides)
    , _previousDisabled(g_disabledOverrides)
{
    DIVIDE_ASSERT((enabledMask & disabledMask) == 0u);
    g_enabledOverrides = (g_enabledOverrides & ~disabledMask) | enabledMask;
    g_disabledOverrides = (g_disabledOverrides & ~enabledMask) | disabledMask;
}

Console::ScopedFlags::~ScopedFlags() noexcept
{
    g_enabledOverrides = _previousEnabled;
    g_disabledOverrides = _previousDisabled;
}

U32 Console::EffectiveFlags() noexcept
{
    return (s_flags.load(std::memory_order_relaxed) | g_enabledOverrides) & ~g_disabledOverrides;
}

bool Console::IsFlagSet(const Flags flag) noexcept
{
    return (EffectiveFlags() & to_base(flag)) != 0u;
}

void Console::DecorateAndPrint(std::ostream& outStream, const std::string_view text, const bool newline, const EntryType type, const U32 flags)
{
    if (flags & to_base(Flags::DECORATE_TIMESTAMP)) [[likely]]
    {
        outStream << "[ " << std::internal
                          << std::setw(9)
                          << std::setprecision(3)
                          << std::setfill('0')
                          << std::fixed
                          << Time::App::ElapsedSeconds()
                  << " ] ";
    }
    if ( flags & to_base( Flags::DECORATE_FRAME ) ) [[likely]]
    {
        outStream << "[ " << GFXDevice::FrameCount() << " ] ";
    }
    if ( flags & to_base( Flags::DECORATE_THREAD_ID ) ) [[likely]]
    {
        outStream << "[ " << std::this_thread::get_id() << " ] ";
    }
    if ( flags & to_base( Flags::DECORATE_SEVERITY ) ) [[likely]]
    {
        outStream << "[ " << TypeUtil::ConsoleEntryTypeToString(type) << " ] ";
    }

    outStream << text;

    if (newline)
    {
        outStream << "\n";
    }
}

void Console::Output(std::ostream& outStream, const std::string_view text, const bool newline, const EntryType type)
{
    const UniqueLock<Mutex> lock = LockOutput();
    const U32 flags = EffectiveFlags();
    if (flags & to_base(Flags::ENABLE_OUTPUT)) [[likely]]
    {
        DecorateAndPrint(outStream, text, newline, type, flags);
    }
}

void Console::Output(const std::string_view text, const bool newline, const EntryType type)
{
    const U32 flags = EffectiveFlags();
    if ( !(flags & to_base( Flags::ENABLE_OUTPUT )) )
    {
        return;
    }

    stringstream outStream;
    DecorateAndPrint(outStream, text, newline, type, flags);

    const OutputEntry entry
    {
        ._text = outStream.str().c_str(),
        ._type = type
    };

    const bool immediate = (flags & to_base(Flags::PRINT_IMMEDIATE)) != 0u;
    const bool flushOnPrint = (flags & to_base(Flags::FLUSH_ON_PRINT)) != 0u &&
                              (type != EntryType::INFO || immediate);

    if (immediate && !t_dispatching && s_running.load(std::memory_order_relaxed))
    {
        const UniqueLock<Mutex> lock( g_outputMutex );
        if (s_running.load(std::memory_order_relaxed))
        {
            {
                t_dispatching = true;
                SCOPE_EXIT { t_dispatching = false; };
                PrintToFile(entry);
            }
            if (flushOnPrint || g_flushRequested)
            {
                FlushLocked();
            }
            else if (g_stopRequested && !g_stopping)
            {
                g_stopRequested = false;
                StopLocked();
            }
            return;
        }
    }

    EnqueueOutput(entry);
    if (immediate || flushOnPrint)
    {
        Flush();
    }
}

void Console::PrintToFile(const OutputEntry& entry)
{
    if ( s_running.load(std::memory_order_relaxed) ) [[likely]]
    {
        const U32 flags = EffectiveFlags();
        if ( flags & to_base( Flags::PRINT_TO_STD_CONSOLE))
        {
            ::printf("%s", entry._text.c_str());
        }
        const bool toErr = entry._type == EntryType::ERR && (flags & to_base(Flags::ENABLE_ERROR_STREAM)) != 0u;

        std::ofstream& outStream = (toErr ? s_errorStream : s_logStream );
        outStream << entry._text.c_str();

        const size_t callbackLimit = g_nextCallbackId;
        size_t callbackIndex = 0u;
        while (callbackIndex < s_guiConsoleCallbacks.size())
        {
            const ConsolePrintCallbackEntry& callbackEntry = s_guiConsoleCallbacks[callbackIndex];
            if (callbackEntry._id >= callbackLimit)
            {
                break;
            }

            const size_t callbackId = callbackEntry._id;
            const ConsolePrintCallback callback = callbackEntry._cbk;
            callback(entry);
            callbackIndex = 0u;
            while (callbackIndex < s_guiConsoleCallbacks.size() && s_guiConsoleCallbacks[callbackIndex]._id <= callbackId)
            {
                ++callbackIndex;
            }
        }

        s_streamDirty[to_base(toErr ? StreamType::ERR_FILE : StreamType::STD_FILE)] = true;
    }
}

void Console::FlushOutputStreams()
{
    for ( U8 i = 0u ; i < to_base( StreamType::COUNT ) ; ++i )
    {
        if ( s_streamDirty[i] )
        {
            switch ( static_cast<StreamType>( i ) )
            {
                case StreamType::STD_FILE:
                    s_logStream.flush();
                    break;
                case StreamType::ERR_FILE:
                    s_errorStream.flush();
                    break;
                default:
                    DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    s_streamDirty.fill(false);
}

void Console::Flush()
{
    if (t_dispatching)
    {
        if (s_running.load(std::memory_order_relaxed))
        {
            g_flushRequested = true;
        }
        return;
    }

    const UniqueLock<Mutex> lock( g_outputMutex );
    FlushLocked();
}

void Console::FlushLocked()
{
    if (!s_running.load(std::memory_order_relaxed))
    {
        return;
    }
    if (t_dispatching)
    {
        g_flushRequested = true;
        return;
    }

    {
        t_dispatching = true;
        SCOPE_EXIT { t_dispatching = false; };
        OutputQueue& queue = OutBuffer();
        for (;;)
        {
            g_drainScratch.clear();
            size_t count{};
            while ((count = queue.try_dequeue_bulk(std::begin(g_outputCache), g_outputCache.size())) > 0u)
            {
                for (size_t i = 0u; i < count; ++i)
                {
                    g_drainScratch.push_back(std::move(g_outputCache[i]));
                }
            }

            if (g_drainScratch.empty())
            {
                break;
            }

            std::ranges::sort(g_drainScratch, {}, &QueuedEntry::_ticket);
            for (const QueuedEntry& queued : g_drainScratch)
            {
                PrintToFile(queued._entry);
            }
        }
    }
    g_flushRequested = false;
    FlushOutputStreams();
    if (g_stopRequested && !g_stopping)
    {
        g_stopRequested = false;
        StopLocked();
    }
}

void Console::Start(const ResourcePath& parentPath, const std::string_view logFilePath, const std::string_view errorFilePath, const bool printCopyright ) noexcept
{
    if (t_dispatching)
    {
        ::fputs("Console::Start cannot be called from an output callback.\n", stderr);
        ::fflush(stderr);
        std::abort();
    }

    const UniqueLock<Mutex> lock( g_outputMutex );
    if (s_running.load(std::memory_order_relaxed))
    {
        FlushLocked();
    }
    s_flags.store(DEFAULT_FLAGS, std::memory_order_relaxed);

    s_logStream   = std::ofstream{ (parentPath / logFilePath).fileSystemPath(),  std::ofstream::out | std::ofstream::trunc };
    s_errorStream = std::ofstream{ (parentPath / errorFilePath).fileSystemPath(), std::ofstream::out | std::ofstream::trunc };

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

    s_streamDirty.fill( true );
    s_running.store(true, std::memory_order_relaxed);
    FlushLocked();
}

void Console::Stop()
{
    if (t_dispatching)
    {
        if (s_running.load(std::memory_order_relaxed))
        {
            g_stopRequested = true;
        }
        return;
    }

    const UniqueLock<Mutex> lock( g_outputMutex );
    StopLocked();
}

void Console::StopLocked()
{
    if (!s_running.load(std::memory_order_relaxed))
    {
        return;
    }
    if (t_dispatching)
    {
        g_stopRequested = true;
        return;
    }

    g_stopping = true;
    SCOPE_EXIT { g_stopping = false; };
    FlushLocked();
    s_running.store(false, std::memory_order_relaxed);
    s_flags.store(DEFAULT_FLAGS, std::memory_order_relaxed);
    s_logStream << "------------------------------------------\n\n\n\n";
    s_streamDirty[to_base(StreamType::STD_FILE)] = true;
    FlushOutputStreams();
    g_stopRequested = false;
}

size_t Console::BindConsoleOutput( const ConsolePrintCallback& guiConsoleCallback )
{
    const UniqueLock<Mutex> lock = LockOutput();
    auto& entry = s_guiConsoleCallbacks.emplace_back();
    entry._cbk = guiConsoleCallback;
    entry._id = g_nextCallbackId++;

    return entry._id;
}

bool Console::UnbindConsoleOutput( size_t& index )
{
    const UniqueLock<Mutex> lock = LockOutput();

    const auto it = eastl::find_if( s_guiConsoleCallbacks.begin(), s_guiConsoleCallbacks.end(), [index]( const ConsolePrintCallbackEntry& entry )
    {
        return entry._id == index;
    } );

    if ( it != s_guiConsoleCallbacks.end() )
    {
        ConsolePrintCallback removed = std::move(it->_cbk);
        s_guiConsoleCallbacks.erase(it);
        index = SIZE_MAX;
        return true;
    }

    return false;
}

} //namespace Divide
