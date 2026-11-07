// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Math/BoundingVolumes/Headers/OBB.h"

#include <atomic>

namespace Divide
{
    template<typename Node, typename GetParent, typename Mark>
    void MarkAncestorBoundsChanged(Node* node, const GetParent& getParent, const Mark& mark)
    {
        while (node != nullptr)
        {
            mark(*node);
            node = getParent(*node);
        }
    }

    template<typename Sphere>
    [[nodiscard]] bool BoundingSphereChanged(const Sphere& previous, const Sphere& current) noexcept
    {
        const auto& a = previous._sphere;
        const auto& b = current._sphere;
        return a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w;
    }

    [[nodiscard]] inline OBB CalculateBoundsOBB(const BoundingBox& worldBounds, const BoundingBox& referenceBounds,
                                                 const bool containsWorldSpaceBounds, const mat4<F32>& worldMatrix)
    {
        OBB result{};
        if (containsWorldSpaceBounds)
        {
            result.fromBoundingBox(worldBounds);
        }
        else
        {
            result.fromBoundingBox(referenceBounds, worldMatrix);
        }
        return result;
    }

    class BoundsChangeCounter
    {
    public:
        void markChanged() noexcept
        {
            _revision.fetch_add(1u, std::memory_order_release);
        }

        [[nodiscard]] U64 revision() const noexcept
        {
            return _revision.load(std::memory_order_acquire);
        }

    private:
        std::atomic<U64> _revision{0u};
    };

    class BoundsChangeTracker
    {
    public:
        [[nodiscard]] bool changed(const U64 revision) const noexcept
        {
            return _applied != revision;
        }

        void capture(const U64 revision) noexcept
        {
            _captured = revision;
        }

        [[nodiscard]] bool capturedChanged() const noexcept
        {
            return changed(_captured);
        }

        void complete() noexcept
        {
            _applied = _captured;
        }

    private:
        U64 _applied = U64_MAX;
        U64 _captured = U64_MAX;
    };

    class BoundsUpdateState
    {
    public:
        void markDirty(const U32 mask) noexcept
        {
            _dirtyMask.fetch_or(mask);
        }

        [[nodiscard]] bool dirty() const noexcept
        {
            return _dirtyMask.load() != 0u;
        }

        [[nodiscard]] bool consumeDirty() noexcept
        {
            _recomputed = _dirtyMask.exchange(0u) != 0u;
            return _recomputed;
        }

        [[nodiscard]] bool recomputed() const noexcept { return _recomputed; }

        void update(BoundingBox& bounds, const BoundingBox& reference,
                    const bool worldSpace, const mat4<F32>& worldMatrix) const noexcept
        {
            if (worldSpace)
            {
                bounds.set(reference);
            }
            else
            {
                bounds.transform(reference, worldMatrix);
            }
        }

    private:
        std::atomic_uint _dirtyMask{0u};
        bool _recomputed = false;
    };
}
