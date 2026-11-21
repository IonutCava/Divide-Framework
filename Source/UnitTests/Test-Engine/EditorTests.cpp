// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Editor/Headers/GizmoSelectionRefresh.h"
#include "Editor/Headers/OutputLogQueue.h"

#include <charconv>

namespace Divide
{
    TEST_CASE( "Spatial changes coalesce until the gizmo is idle", "[editor][threading][render_smoke]" )
    {
        GizmoSelectionRefresh refresh;
        CHECK_FALSE( refresh.pending() );
        CHECK_FALSE( refresh.consumeIfIdle( false ) );

        refresh.request();
        refresh.request();
        CHECK( refresh.pending() );
        CHECK_FALSE( refresh.consumeIfIdle( true ) );
        CHECK( refresh.pending() );
        CHECK( refresh.consumeIfIdle( false ) );
        CHECK_FALSE( refresh.pending() );
        CHECK_FALSE( refresh.consumeIfIdle( false ) );

        refresh.request();
        CHECK( refresh.consumeIfIdle( false ) );
    }

    TEST_CASE( "Concurrent spatial notifications request one gizmo refresh", "[editor][threading][render_smoke]" )
    {
        GizmoSelectionRefresh refresh;
        std::array<std::thread, 4> producers;
        for ( std::thread& producer : producers )
        {
            producer = std::thread( [&refresh]
            {
                for ( U32 i = 0u; i < 1000u; ++i )
                {
                    refresh.request();
                }
            } );
        }
        for ( std::thread& producer : producers )
        {
            producer.join();
        }

        CHECK( refresh.pending() );
        CHECK( refresh.consumeIfIdle( false ) );
        CHECK_FALSE( refresh.consumeIfIdle( false ) );
    }

    TEST_CASE( "Editor output retains the newest pending entries and clears atomically", "[editor][console][threading][render_smoke]" )
    {
        OutputLogQueue queue;
        constexpr size_t overflow = 5u;
        for (size_t i = 0u; i < OutputLogQueue::CAPACITY + overflow; ++i)
        {
            queue.push({Util::StringFormat<string>("entry-{}", i), Console::EntryType::INFO});
        }

        vector<string> received;
        CHECK(queue.drain([&](const Console::OutputEntry& entry)
        {
            received.push_back(entry._text);
        }) == OutputLogQueue::CAPACITY);
        REQUIRE(received.size() == OutputLogQueue::CAPACITY);
        for (size_t i = 0u; i < received.size(); ++i)
        {
            CHECK(received[i] == Util::StringFormat<string>("entry-{}", i + overflow));
        }

        queue.push({"discarded", Console::EntryType::INFO});
        queue.clear();
        queue.push({"retained", Console::EntryType::WARNING});
        received.clear();
        CHECK(queue.drain([&](const Console::OutputEntry& entry)
        {
            CHECK(entry._type == Console::EntryType::WARNING);
            received.push_back(entry._text);
        }) == 1u);
        REQUIRE(received.size() == 1u);
        CHECK(received.front() == "retained");
    }

    TEST_CASE( "Editor output safely publishes complete entries from concurrent producers", "[editor][console][threading][render_smoke]" )
    {
        OutputLogQueue queue;
        constexpr U32 producerCount = 4u;
        constexpr U32 entriesPerProducer = 128u;
        constexpr U32 total = producerCount * entriesPerProducer;
        std::array<U32, total> seen{};
        std::atomic_uint finished{0u};
        std::array<std::thread, producerCount> producers;
        for (U32 producer = 0u; producer < producerCount; ++producer)
        {
            producers[producer] = std::thread([&, producer]
            {
                for (U32 i = 0u; i < entriesPerProducer; ++i)
                {
                    queue.push({Util::StringFormat<string>("id-{}", producer * entriesPerProducer + i), Console::EntryType::INFO});
                }
                finished.fetch_add(1u, std::memory_order_release);
            });
        }

        U32 invalid = 0u;
        const auto record = [&](const Console::OutputEntry& entry)
        {
            const std::string_view text{entry._text.c_str(), entry._text.length()};
            if (!text.starts_with("id-"))
            {
                ++invalid;
                return;
            }
            U32 id = 0u;
            const auto result = std::from_chars(text.data() + 3u, text.data() + text.size(), id);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || id >= total ||
                entry._type != Console::EntryType::INFO)
            {
                ++invalid;
                return;
            }
            ++seen[id];
        };
        while (finished.load(std::memory_order_acquire) != producerCount)
        {
            queue.drain(record);
            std::this_thread::yield();
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        queue.drain(record);

        CHECK(invalid == 0u);
        for (const U32 count : seen)
        {
            CHECK(count == 1u);
        }
    }
}
