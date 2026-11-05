/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include "Platform/Headers/PlatformDataTypes.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace Divide
{
    [[nodiscard]] constexpr U32 ShadowLayerMask(const U8 layerCount) noexcept
    {
        assert(layerCount <= 32u);
        return layerCount == 32u ? ~U32{0u} : (U32{1u} << layerCount) - 1u;
    }

    [[nodiscard]] constexpr bool ShadowLayerSelected(const U32 mask, const U8 layer) noexcept
    {
        return layer < 32u && (mask & (U32{1u} << layer)) != 0u;
    }

    [[nodiscard]] constexpr U32 ShadowLayerUpdateMask(const U8 layerCount, const U32 dirtyLayers,
                                                      const bool cacheValid, const bool fullInvalidation) noexcept
    {
        const U32 allLayers = ShadowLayerMask(layerCount);
        return !cacheValid || fullInvalidation ? allLayers : dirtyLayers & allLayers;
    }

    [[nodiscard]] constexpr F32 ShadowBlurTexelScale(const U8 firstLayer) noexcept
    {
        assert(firstLayer < 32u);
        return 1.f / static_cast<F32>(U32{1u} << firstLayer);
    }

    template<typename Callback>
    void ForEachShadowLayerRange(const U32 mask, const U8 layerCount, Callback&& callback)
    {
        assert(layerCount <= 32u);
        for (U8 layer = 0u; layer < layerCount;)
        {
            if (!ShadowLayerSelected(mask, layer))
            {
                ++layer;
                continue;
            }
            const U8 first = layer++;
            while (layer < layerCount && ShadowLayerSelected(mask, layer))
            {
                ++layer;
            }
            callback(first, static_cast<U8>(layer - first));
        }
    }

    [[nodiscard]] constexpr size_t ShadowReservationCount(const size_t contentCount,
                                                           const size_t maxCascades,
                                                           const bool directional) noexcept
    {
        if (contentCount == 0u || (directional && contentCount > maxCascades))
        {
            return 0u;
        }
        return directional ? maxCascades : contentCount;
    }

    template<typename Bounds>
    [[nodiscard]] bool ShadowVolumeAffectsLight(const bool directional, const Bounds& volume,
                                                 const Bounds& lightBounds) noexcept
    {
        return directional || volume.collision(lightBounds);
    }

    template<typename Queue, typename Bounds>
    void AppendShadowVolumeChange(Queue& pending, const Bounds& previous, const bool previousStatic,
                                  const Bounds& current, const bool currentStatic)
    {
        pending.push_back({ previous, previousStatic });
        pending.push_back({ current, currentStatic });
    }

    template<typename Layers, typename Owner>
    [[nodiscard]] bool TryReuseShadowLayers(Layers& layers, const Owner owner,
                                             const size_t offset, const size_t count,
                                             const size_t contentCount) noexcept
    {
        if (count == 0u || contentCount == 0u || offset > layers.size() || count > layers.size() - offset)
        {
            return false;
        }
        for (size_t i = offset; i < offset + count; ++i)
        {
            if (layers[i]._lightGUID != owner || layers[i]._contentLayers != contentCount)
            {
                return false;
            }
        }
        for (size_t i = offset; i < offset + count; ++i)
        {
            layers[i]._lifetime = 0u;
        }
        return true;
    }

    template<typename Layers, typename Owner, typename Lifetime>
    void ReleaseShadowLayers(Layers& layers, const Owner owner, const Lifetime expired) noexcept
    {
        for (auto& layer : layers)
        {
            if (layer._lightGUID == owner)
            {
                layer._lifetime = expired;
                layer._lightGUID = -1;
                layer._contentLayers = 0u;
            }
        }
    }

    template<typename Layers, typename Lifetime>
    [[nodiscard]] size_t FindAvailableShadowLayers(const Layers& layers, const size_t count,
                                                    const Lifetime expired) noexcept
    {
        if (count == 0u || count > layers.size())
        {
            return layers.size();
        }
        for (size_t offset = 0u; offset <= layers.size() - count; ++offset)
        {
            bool available = true;
            for (size_t i = offset; i < offset + count; ++i)
            {
                if (layers[i]._lifetime < expired)
                {
                    available = false;
                    break;
                }
            }
            if (available)
            {
                return offset;
            }
        }
        return layers.size();
    }

    template<size_t TypeCount>
    class ShadowPassSlots
    {
      public:
        explicit ShadowPassSlots(const U32 totalLimit) noexcept
            : _totalLimit(totalLimit)
        {
        }

        [[nodiscard]] bool hasCapacity(const size_t type, const U32 typeLimit) const noexcept
        {
            assert(type < TypeCount);
            return _total < _totalLimit && _perType[type] < typeLimit;
        }

        [[nodiscard]] I32 reserve(const size_t type, const U32 typeLimit) noexcept
        {
            if (!hasCapacity(type, typeLimit))
            {
                return -1;
            }

            ++_total;
            return to_I32(_perType[type]++);
        }

      private:
        std::array<U32, TypeCount> _perType{};
        U32 _total = 0u;
        U32 _totalLimit;
    };

    class ShadowMovementBatch
    {
      public:
        struct Snapshot
        {
            size_t _count;
            bool _invalidateAll;
        };

        [[nodiscard]] Snapshot begin(const size_t pendingCount) noexcept
        {
            if (!_active)
            {
                _count = pendingCount;
                _active = true;
            }
            return { _count, _invalidateAll };
        }

        [[nodiscard]] size_t finish(const size_t pendingCount) noexcept
        {
            size_t consumed = 0u;
            if (_active)
            {
                assert(_count <= pendingCount);
                consumed = _count;
                _invalidateAll = false;
            }
            else if (pendingCount > 0u)
            {
                consumed = pendingCount;
                _invalidateAll = true;
            }
            _count = 0u;
            _active = false;
            return consumed;
        }

      private:
        size_t _count = 0u;
        bool _active = false;
        bool _invalidateAll = false;
    };

    template<typename Matrix>
    class ShadowCameraState
    {
      public:
        [[nodiscard]] bool update(const Matrix& view, const Matrix& projection,
                                  const F32 nearPlane, const F32 farPlane) noexcept
        {
            const bool changed = !_valid || _nearPlane != nearPlane || _farPlane != farPlane ||
                                 _view != view || _projection != projection;
            if (changed)
            {
                _view = view;
                _projection = projection;
                _nearPlane = nearPlane;
                _farPlane = farPlane;
                _valid = true;
            }
            return changed;
        }

      private:
        Matrix _view{};
        Matrix _projection{};
        F32 _nearPlane = 0.f;
        F32 _farPlane = 0.f;
        bool _valid = false;
    };

    class ShadowRangeState
    {
      public:
        [[nodiscard]] bool update(const F32 range) noexcept
        {
            if (!_valid || _range != range)
            {
                _range = range;
                _valid = true;
                return true;
            }
            return false;
        }

      private:
        F32 _range = 0.f;
        bool _valid = false;
    };

    template<std::size_t SplitCount>
    class ShadowDirectionalSettingsState
    {
      public:
        [[nodiscard]] bool update(const F32 nearClipOffset, const std::array<bool, SplitCount>& sceneFit) noexcept
        {
            const bool changed = !_valid || _nearClipOffset != nearClipOffset || _sceneFit != sceneFit;
            if (changed)
            {
                _nearClipOffset = nearClipOffset;
                _sceneFit = sceneFit;
                _valid = true;
            }
            return changed;
        }

      private:
        std::array<bool, SplitCount> _sceneFit{};
        F32 _nearClipOffset = 0.f;
        bool _valid = false;
    };
}
