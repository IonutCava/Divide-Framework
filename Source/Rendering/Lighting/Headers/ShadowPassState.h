/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace Divide
{
    [[nodiscard]] constexpr std::size_t ShadowReservationCount(const std::size_t contentCount,
                                                                const std::size_t maxCascades,
                                                                const bool directional) noexcept
    {
        if (contentCount == 0u || (directional && contentCount > maxCascades))
        {
            return 0u;
        }
        return directional ? maxCascades : contentCount;
    }

    template<typename Layers, typename Owner>
    [[nodiscard]] bool TryReuseShadowLayers(Layers& layers, const Owner owner,
                                             const std::size_t offset, const std::size_t count,
                                             const std::size_t contentCount) noexcept
    {
        if (count == 0u || contentCount == 0u || offset > layers.size() || count > layers.size() - offset)
        {
            return false;
        }
        for (std::size_t i = offset; i < offset + count; ++i)
        {
            if (layers[i]._lightGUID != owner || layers[i]._contentLayers != contentCount)
            {
                return false;
            }
        }
        for (std::size_t i = offset; i < offset + count; ++i)
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
    [[nodiscard]] std::size_t FindAvailableShadowLayers(const Layers& layers, const std::size_t count,
                                                         const Lifetime expired) noexcept
    {
        if (count == 0u || count > layers.size())
        {
            return layers.size();
        }
        for (std::size_t offset = 0u; offset <= layers.size() - count; ++offset)
        {
            bool available = true;
            for (std::size_t i = offset; i < offset + count; ++i)
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

    template<std::size_t TypeCount>
    class ShadowPassSlots
    {
      public:
        explicit ShadowPassSlots(const std::uint32_t totalLimit) noexcept
            : _totalLimit(totalLimit)
        {
        }

        [[nodiscard]] bool hasCapacity(const std::size_t type, const std::uint32_t typeLimit) const noexcept
        {
            assert(type < TypeCount);
            return _total < _totalLimit && _perType[type] < typeLimit;
        }

        [[nodiscard]] std::int32_t reserve(const std::size_t type, const std::uint32_t typeLimit) noexcept
        {
            if (!hasCapacity(type, typeLimit))
            {
                return -1;
            }

            ++_total;
            return static_cast<std::int32_t>(_perType[type]++);
        }

      private:
        std::array<std::uint32_t, TypeCount> _perType{};
        std::uint32_t _total = 0u;
        std::uint32_t _totalLimit;
    };

    template<typename Matrix>
    class ShadowCameraState
    {
      public:
        [[nodiscard]] bool update(const Matrix& view, const Matrix& projection,
                                  const float nearPlane, const float farPlane) noexcept
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
        float _nearPlane = 0.f;
        float _farPlane = 0.f;
        bool _valid = false;
    };

    class ShadowRangeState
    {
      public:
        [[nodiscard]] bool update(const float range) noexcept
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
        float _range = 0.f;
        bool _valid = false;
    };

    template<std::size_t SplitCount>
    class ShadowDirectionalSettingsState
    {
      public:
        [[nodiscard]] bool update(const float nearClipOffset, const std::array<bool, SplitCount>& sceneFit) noexcept
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
        float _nearClipOffset = 0.f;
        bool _valid = false;
    };
}
