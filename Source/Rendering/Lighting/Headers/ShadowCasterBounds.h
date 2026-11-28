// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

namespace Divide
{
    struct ShadowCasterBounds
    {
        void reset() noexcept
        {
            _valid = false;
        }

        void add(const BoundingBox& bounds) noexcept
        {
            if (_valid)
            {
                _bounds.add(bounds);
            }
            else
            {
                _bounds.set(bounds);
                _valid = true;
            }
        }

        void add(const ShadowCasterBounds& other) noexcept
        {
            if (other._valid)
            {
                add(other._bounds);
            }
        }

        BoundingBox _bounds{};
        bool _valid = false;
    };

    [[nodiscard]] inline ShadowCasterBounds* SelectCascadeBoundsFeedback(
        ShadowCasterBounds& feedback, [[maybe_unused]] const U8 cascade, const bool fitToScene) noexcept
    {
        feedback.reset();
        if ( !fitToScene )
        {
            return nullptr;
        }

        return &feedback;
    }
}
