#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "PushConstants.h"

namespace Divide
{
    // Hands out per-frame UniformData slots whose addresses stay valid until the same slot is reused in a later frame.
    // Slots are claimed with a single CAS on a frame-tagged cursor and live in lazily allocated pages, so recording takes no lock.
    // Only a frame that needs more than PAGE_SIZE * MAX_PAGES snapshots touches the (mutex guarded) overflow deque.
    class UniformDataSnapshots
    {
    public:
        static constexpr size_t PAGE_SIZE = 16u;
        static constexpr size_t MAX_PAGES = 64u;
        static constexpr size_t LOCK_FREE_CAPACITY = PAGE_SIZE * MAX_PAGES;

        explicit UniformDataSnapshots(const size_t snapshotCount = 8u)
        {
            const size_t pageCount = std::min((snapshotCount + PAGE_SIZE - 1u) / PAGE_SIZE, MAX_PAGES);
            for (size_t page = 0u; page < pageCount; ++page)
            {
                _pages[page].store(new UniformData[PAGE_SIZE], std::memory_order_relaxed);
            }
        }

        ~UniformDataSnapshots()
        {
            for (std::atomic<UniformData*>& page : _pages)
            {
                delete[] page.load(std::memory_order_relaxed);
            }
        }

        UniformDataSnapshots(const UniformDataSnapshots&) = delete;
        UniformDataSnapshots& operator=(const UniformDataSnapshots&) = delete;

        template<typename Callback>
        void record(const U64 frame, const UniformData& defaults, Callback&& callback)
        {
            UniformData& snapshot = slotAt(claimSlot(frame));
            snapshot = defaults;
            callback(snapshot);
        }

    private:
        // The cursor packs the low 32 bits of the frame with the next free slot, so a new frame restarts at slot 0 in the same CAS.
        [[nodiscard]] size_t claimSlot(const U64 frame) noexcept
        {
            const U64 tag = frame & U32_MAX;
            U64 current = _cursor.load(std::memory_order_relaxed);
            for (;;)
            {
                const U64 index = (current >> 32u) == tag ? (current & U32_MAX) : 0u;
                if (_cursor.compare_exchange_weak(current, (tag << 32u) | (index + 1u), std::memory_order_relaxed))
                {
                    return to_size(index);
                }
            }
        }

        [[nodiscard]] UniformData& slotAt(const size_t slot)
        {
            const size_t page = slot / PAGE_SIZE;
            if (page >= MAX_PAGES) [[unlikely]]
            {
                return overflowAt(slot - LOCK_FREE_CAPACITY);
            }

            UniformData* data = _pages[page].load(std::memory_order_acquire);
            if (data == nullptr)
            {
                UniformData* fresh = new UniformData[PAGE_SIZE];
                if (_pages[page].compare_exchange_strong(data, fresh, std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    data = fresh;
                }
                else
                {
                    delete[] fresh;
                }
            }
            return data[slot % PAGE_SIZE];
        }

        [[nodiscard]] UniformData& overflowAt(const size_t index)
        {
            LockGuard<Mutex> lock(_overflowLock);
            while (_overflow.size() <= index)
            {
                // Immediate draws have no fixed view budget. Deque growth preserves recorded addresses.
                _overflow.emplace_back();
            }
            return _overflow[index];
        }

    private:
        std::array<std::atomic<UniformData*>, MAX_PAGES> _pages{};
        std::atomic<U64> _cursor{0u};
        Mutex _overflowLock;
        eastl::deque<UniformData> _overflow;
    };
}
