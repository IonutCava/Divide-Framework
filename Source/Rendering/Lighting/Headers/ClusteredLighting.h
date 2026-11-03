/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include "EngineConfig.h"

namespace Divide::ClusteredLighting
{
    [[nodiscard]] constexpr I32 ResolveLightsPerCluster( const I32 configuredLimit ) noexcept
    {
        static_assert( Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER >= 1u );
        constexpr I32 maxLimit = to_I32( Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER );
        if ( configuredLimit < 0 || configuredLimit > maxLimit )
        {
            return maxLimit;
        }
        return configuredLimit == 0 ? 1 : configuredLimit;
    }

    /// Clustered lighting uses exponential depth slices, so cameras must provide finite planes with 0 < zNear < zFar.
    [[nodiscard]] constexpr bool IsValidClusterDepthRange( const F32 zNear, const F32 zFar ) noexcept
    {
        return zNear > 0.f && zFar > zNear && zFar <= F32_MAX;
    }
}
