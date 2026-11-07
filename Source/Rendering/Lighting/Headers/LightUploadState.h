/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

namespace Divide
{
    template<typename Count, typename Capacity>
    [[nodiscard]] constexpr bool TryReserveLightUploadSlot(Count& count, const Capacity capacity) noexcept
    {
        if (count >= capacity)
        {
            return false;
        }
        ++count;
        return true;
    }

    template<typename Count>
    [[nodiscard]] constexpr bool ShouldUploadLightData(const Count count) noexcept
    {
        return count > 0;
    }
}
