// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Console.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/File/Headers/FileManagement.h"

#include <charconv>
#include <iostream>

namespace Divide
{
    namespace
    {
        struct ConsoleTestScope
        {
            ConsoleTestScope()
                : _out(std::cout.rdbuf())
                , _err(std::cerr.rdbuf())
            {
                for (size_t i = 0u; i < FLAGS.size(); ++i)
                {
                    _previousFlags[i] = Console::IsFlagSet(FLAGS[i]);
                }
#if defined(IS_WINDOWS_BUILD)
                constexpr std::string_view nullDevice = "NUL";
#else
                constexpr std::string_view nullDevice = "/dev/null";
#endif
                Console::Start(ResourcePath{}, nullDevice, nullDevice, false);
                std::cout.rdbuf(_out);
                std::cerr.rdbuf(_err);
                Console::ToggleFlag(Console::Flags::PRINT_TO_STD_CONSOLE, false);
                Console::ToggleFlag(Console::Flags::DECORATE_TIMESTAMP, false);
                Console::ToggleFlag(Console::Flags::DECORATE_THREAD_ID, false);
                Console::ToggleFlag(Console::Flags::DECORATE_FRAME, false);
                Console::ToggleFlag(Console::Flags::DECORATE_SEVERITY, false);
            }

            ~ConsoleTestScope()
            {
                Console::Flush();
                Console::Stop();
                std::cout.rdbuf(_out);
                std::cerr.rdbuf(_err);
                for (size_t i = 0u; i < FLAGS.size(); ++i)
                {
                    Console::ToggleFlag(FLAGS[i], _previousFlags[i]);
                }
            }

            static constexpr std::array<Console::Flags, 9u> FLAGS{{
                Console::Flags::DECORATE_TIMESTAMP, Console::Flags::DECORATE_THREAD_ID,
                Console::Flags::DECORATE_SEVERITY, Console::Flags::DECORATE_FRAME,
                Console::Flags::ENABLE_OUTPUT, Console::Flags::ENABLE_ERROR_STREAM,
                Console::Flags::PRINT_IMMEDIATE, Console::Flags::FLUSH_ON_PRINT,
                Console::Flags::PRINT_TO_STD_CONSOLE
            }};
            std::array<bool, FLAGS.size()> _previousFlags{};
            std::streambuf* _out;
            std::streambuf* _err;
        };

        struct ConsoleCallbackScope
        {
            ~ConsoleCallbackScope()
            {
                if (_id != SIZE_MAX)
                {
                    [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput(_id);
                    assert(unbound);
                }
            }

            size_t _id = SIZE_MAX;
        };
    }

    TEST_CASE( "Concurrent console producers and flushes serialize complete output", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        constexpr U32 producerCount = 4u;
        constexpr U32 entriesPerProducer = 256u;
        constexpr U32 total = producerCount * entriesPerProducer;
        std::array<U32, total> seen{};
        std::atomic_uint activeCallbacks{0u};
        std::atomic_uint overlappingCallbacks{0u};
        std::atomic_uint invalid{0u};
        Mutex receivedLock;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry& entry)
        {
            if (activeCallbacks.fetch_add(1u) != 0u)
            {
                overlappingCallbacks.fetch_add(1u);
            }
            {
                LockGuard<Mutex> lock(receivedLock);
                const std::string_view text{entry._text.c_str(), entry._text.length()};
                if (!text.starts_with("message-") || !text.ends_with('\n'))
                {
                    invalid.fetch_add(1u);
                }
                else
                {
                    U32 id = 0u;
                    const auto result = std::from_chars(text.data() + 8u, text.data() + text.size() - 1u, id);
                    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() - 1u || id >= total ||
                        entry._type != Console::EntryType::INFO)
                    {
                        invalid.fetch_add(1u);
                    }
                    else
                    {
                        ++seen[id];
                    }
                }
            }
            activeCallbacks.fetch_sub(1u);
        });

        std::atomic_bool start{false};
        std::atomic_uint finished{0u};
        std::array<std::thread, producerCount> producers;
        for (U32 producer = 0u; producer < producerCount; ++producer)
        {
            producers[producer] = std::thread([&, producer]
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                for (U32 i = 0u; i < entriesPerProducer; ++i)
                {
                    Console::printfn("message-{}", producer * entriesPerProducer + i);
                }
                finished.fetch_add(1u, std::memory_order_release);
            });
        }
        std::thread flusher([&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            while (finished.load(std::memory_order_acquire) != producerCount)
            {
                Console::Flush();
                std::this_thread::yield();
            }
        });
        start.store(true, std::memory_order_release);
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        flusher.join();
        Console::Flush();

        CHECK(invalid.load() == 0u);
        CHECK(overlappingCallbacks.load() == 0u);
        for (const U32 count : seen)
        {
            CHECK(count == 1u);
        }
    }

    TEST_CASE( "Immediate console output preserves previously deferred entries", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        Console::ToggleFlag(Console::Flags::FLUSH_ON_PRINT, false);
        vector<string> received;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry& entry)
        {
            received.push_back(entry._text);
        });

        Console::printfn("deferred");
        CHECK(received.empty());
        Console::ToggleFlag(Console::Flags::PRINT_IMMEDIATE, true);
        Console::printfn("immediate");
        REQUIRE(received.size() == 1u);
        CHECK(received[0] == "immediate\n");
        Console::ToggleFlag(Console::Flags::PRINT_IMMEDIATE, false);
        Console::Flush();
        REQUIRE(received.size() == 2u);
        CHECK(received[1] == "deferred\n");
    }

    TEST_CASE( "Console callbacks can log recursively without losing output", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        Console::ToggleFlag(Console::Flags::FLUSH_ON_PRINT, false);
        vector<string> received;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry& entry)
        {
            received.push_back(entry._text);
            if (entry._text == "outer\n")
            {
                Console::printfn("nested");
            }
        });

        SECTION("Immediate dispatch")
        {
            Console::ToggleFlag(Console::Flags::PRINT_IMMEDIATE, true);
            Console::printfn("outer");
        }
        SECTION("Deferred dispatch")
        {
            Console::printfn("outer");
            CHECK(received.empty());
            Console::Flush();
        }
        REQUIRE(received.size() == 2u);
        CHECK(received[0] == "outer\n");
        CHECK(received[1] == "nested\n");
    }

    TEST_CASE( "Console shutdown drains buffered and reentrant output", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        Console::ToggleFlag(Console::Flags::FLUSH_ON_PRINT, false);
        vector<string> received;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry& entry)
        {
            received.push_back(entry._text);
            if (entry._text == "stop-from-callback\n")
            {
                Console::printfn("nested");
                Console::Stop();
            }
        });

        SECTION("Disabled output still drains previously accepted entries")
        {
            Console::printfn("before-stop");
            Console::ToggleFlag(Console::Flags::ENABLE_OUTPUT, false);
            Console::Stop();
            REQUIRE(received.size() == 1u);
            CHECK(received[0] == "before-stop\n");
        }
        SECTION("A callback can request shutdown")
        {
            Console::ToggleFlag(Console::Flags::PRINT_IMMEDIATE, true);
            Console::printfn("stop-from-callback");
            REQUIRE(received.size() == 2u);
            CHECK(received[0] == "stop-from-callback\n");
            CHECK(received[1] == "nested\n");
        }
    }

    TEST_CASE( "Console callbacks may unbind themselves and other callbacks", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        ConsoleCallbackScope first;
        ConsoleCallbackScope second;
        bool removedFirst = false;
        bool removedSecond = false;
        U32 firstCalls = 0u;
        U32 secondCalls = 0u;
        first._id = Console::BindConsoleOutput([&](const Console::OutputEntry&)
        {
            ++firstCalls;
            removedSecond = Console::UnbindConsoleOutput(second._id);
            removedFirst = Console::UnbindConsoleOutput(first._id);
        });
        second._id = Console::BindConsoleOutput([&](const Console::OutputEntry&) { ++secondCalls; });
        Console::printfn("remove-callbacks");
        Console::printfn("after-removal");
        CHECK(removedFirst);
        CHECK(removedSecond);
        CHECK(firstCalls == 1u);
        CHECK(secondCalls == 0u);
    }

    TEST_CASE( "Console unbind waits for an in-flight callback", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        std::atomic_bool entered{false};
        std::atomic_bool release{false};
        std::atomic_bool unbindStarted{false};
        std::atomic_bool unbindFinished{false};
        bool unbound = false;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry&)
        {
            entered.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        });
        std::thread producer([] { Console::printfn("in-flight"); });
        while (!entered.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::thread unbinder([&]
        {
            unbindStarted.store(true, std::memory_order_release);
            unbound = Console::UnbindConsoleOutput(callback._id);
            unbindFinished.store(true, std::memory_order_release);
        });
        while (!unbindStarted.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const bool returnedEarly = unbindFinished.load(std::memory_order_acquire);
        release.store(true, std::memory_order_release);
        producer.join();
        unbinder.join();
        CHECK_FALSE(returnedEarly);
        CHECK(unbound);
        CHECK(callback._id == SIZE_MAX);
    }

    TEST_CASE( "Scoped console flags are nested and local to the calling thread", "[console][threading][render_smoke]" )
    {
        const ConsoleTestScope console;
        Console::ToggleFlag(Console::Flags::DECORATE_SEVERITY, true);
        const U32 severity = to_U32(to_base(Console::Flags::DECORATE_SEVERITY));
        vector<string> received;
        ConsoleCallbackScope callback;
        callback._id = Console::BindConsoleOutput([&](const Console::OutputEntry& entry)
        {
            received.push_back(entry._text);
        });
        {
            const Console::ScopedFlags withoutSeverity{0u, severity};
            CHECK_FALSE(Console::IsFlagSet(Console::Flags::DECORATE_SEVERITY));
            {
                const Console::ScopedFlags withSeverity{severity, 0u};
                CHECK(Console::IsFlagSet(Console::Flags::DECORATE_SEVERITY));
            }
            CHECK_FALSE(Console::IsFlagSet(Console::Flags::DECORATE_SEVERITY));
            Console::printfn("local");
            std::thread other([] { Console::printfn("other"); });
            other.join();
        }
        CHECK(Console::IsFlagSet(Console::Flags::DECORATE_SEVERITY));
        REQUIRE(received.size() == 2u);
        CHECK(received[0] == "local\n");
        CHECK(received[1] == "[ INFO ] other\n");
    }

    TEST_CASE( "Application timestamp reads tolerate concurrent timer resets", "[console][threading][render_smoke]" )
    {
        Time::ApplicationTimer timer;
        std::atomic_bool invalid{false};
        std::thread reader([&]
        {
            for (U32 i = 0u; i < 10000u; ++i)
            {
                if (!std::isfinite(Time::App::ElapsedMicroseconds()))
                {
                    invalid.store(true, std::memory_order_relaxed);
                }
            }
        });
        for (U32 i = 0u; i < 10000u; ++i)
        {
            timer.reset();
        }
        reader.join();
        CHECK_FALSE(invalid.load(std::memory_order_relaxed));
    }
}
