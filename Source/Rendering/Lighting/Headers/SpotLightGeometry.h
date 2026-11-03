/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace Divide
{
    struct SpotConeAngles
    {
        float _inner = 15.f;
        float _outer = 35.f;

        void setInner(const float value) noexcept
        {
            _inner = std::clamp(value, 0.f, 180.f);
            _outer = std::max(_outer, _inner);
        }

        void setOuter(const float value) noexcept
        {
            _outer = std::clamp(value, 0.f, 180.f);
            _inner = std::min(_inner, _outer);
        }

        void setInnerClamped(const float value) noexcept
        {
            _inner = std::clamp(value, 0.f, _outer);
        }

        void setOuterClamped(const float value) noexcept
        {
            _outer = std::clamp(value, _inner, 180.f);
        }

        void normalize() noexcept
        {
            _inner = std::clamp(_inner, 0.f, 180.f);
            _outer = std::clamp(_outer, 0.f, 180.f);
            if (_inner > _outer)
            {
                std::swap(_inner, _outer);
            }
        }
    };

    struct SpotBoundingSphere
    {
        float _offset;
        float _radius;
    };

    [[nodiscard]] inline SpotBoundingSphere SpotConeBoundingSphere(const float slantHeight, const float cosOuter) noexcept
    {
        constexpr float radiusMargin = 1.f + 4.f * std::numeric_limits<float>::epsilon();
        if (cosOuter >= 0.70710678f)
        {
            const float radius = slantHeight / (2.f * cosOuter);
            return { radius, radius * radiusMargin };
        }
        if (cosOuter > 0.f)
        {
            return { slantHeight * cosOuter,
                     slantHeight * std::sqrt(1.f - cosOuter * cosOuter) * radiusMargin };
        }
        return { 0.f, slantHeight * radiusMargin };
    }
}
