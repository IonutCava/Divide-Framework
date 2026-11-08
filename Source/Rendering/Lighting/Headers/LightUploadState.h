/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

namespace Divide
{
    class ShaderBuffer;

    struct LightDataBinding
    {
        ShaderBuffer* _lights = nullptr;
        ShaderBuffer* _scene = nullptr;
    };

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

    template<typename Properties, typename Lights>
    [[nodiscard]] bool PatchLightShadowIndices(Properties& properties, const Lights& lights) noexcept
    {
        assert(properties.size() == lights.size());
        bool anyShadowCaster = false;
        for (size_t i = 0u; i < properties.size(); ++i)
        {
            const auto* light = lights[i];
            const auto shadowIndex = light->castsShadows() ? light->shadowPropertyIndex() : -1;
            properties[i]._options.y = shadowIndex;
            anyShadowCaster = anyShadowCaster || shadowIndex != -1;
        }
        return anyShadowCaster;
    }
}
