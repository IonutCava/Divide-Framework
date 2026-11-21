// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include <atomic>

namespace Divide
{
    class GizmoSelectionRefresh
    {
    public:
        void request() noexcept
        {
            _pending.store(true, std::memory_order_release);
        }

        [[nodiscard]] bool pending() const noexcept
        {
            return _pending.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool consumeIfIdle(const bool manipulating) noexcept
        {
            return !manipulating && _pending.exchange(false, std::memory_order_acq_rel);
        }

    private:
        std::atomic_bool _pending{false};
    };
}
