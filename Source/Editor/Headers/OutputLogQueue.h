// Copyright (c) 2026 DIVIDE-Studio

#pragma once


namespace Divide
{
    class OutputLogQueue
    {
    public:
        static constexpr size_t CAPACITY = 1024u;

        void push(const Console::OutputEntry& entry)
        {
            // Reserve the slot before publishing so a concurrent drain can never subtract more than was added.
            const size_t previous = _count.fetch_add(1u, std::memory_order_relaxed);
            _queue.enqueue(entry);
            if (previous >= CAPACITY)
            {
                Console::OutputEntry dropped;
                if (_queue.try_dequeue(dropped))
                {
                    _count.fetch_sub(1u, std::memory_order_relaxed);
                }
            }
        }

        template<typename Consumer>
        size_t drain(Consumer&& consume)
        {
            size_t count = 0u;
            Console::OutputEntry entry;
            while (_queue.try_dequeue(entry))
            {
                consume(entry);
                ++count;
            }
            _count.fetch_sub(count, std::memory_order_relaxed);
            return count;
        }

        void clear()
        {
            auto ret = drain([](const Console::OutputEntry&) noexcept {});
            DIVIDE_UNUSED(ret);
        }

    private:
        moodycamel::ConcurrentQueue<Console::OutputEntry> _queue;
        std::atomic<size_t> _count{ 0u };
    };
}
