// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "Platform/Headers/PlatformDataTypes.h"

namespace Divide
{
    inline constexpr U32 OCCLUSION_CULL_GROUP_SIZE = 64u;

    struct OcclusionCullDispatch
    {
        U32 _groupsX = 0u;
        U32 _groupsY = 0u;
    };

    [[nodiscard]] inline bool TryPlanOcclusionCullDispatch( const U32 commandCount,
                                                            const U32 maxGroupsX,
                                                            const U32 maxGroupsY,
                                                            OcclusionCullDispatch& dispatchOut ) noexcept
    {
        dispatchOut = {};
        if ( commandCount == 0u )
        {
            return true;
        }
        if ( maxGroupsX == 0u || maxGroupsY == 0u )
        {
            return false;
        }

        const U32 groups = commandCount / OCCLUSION_CULL_GROUP_SIZE +
                           (commandCount % OCCLUSION_CULL_GROUP_SIZE != 0u ? 1u : 0u);
        const U32 groupsX = groups < maxGroupsX ? groups : maxGroupsX;
        const U32 groupsY = groups / groupsX + (groups % groupsX != 0u ? 1u : 0u);
        if ( groupsY > maxGroupsY )
        {
            return false;
        }

        dispatchOut = { groupsX, groupsY };
        return true;
    }
}
