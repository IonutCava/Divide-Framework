// Copyright (c) 2026 DIVIDE-Studio

#pragma once

namespace Divide::ThreadPoolSizing
{
    constexpr U32 MIN_TOTAL_WORKERS = 16u;
    constexpr U32 RENDER_WORKERS = 6u;
    constexpr U32 LOW_PRIORITY_WORKERS = 2u;
    constexpr U32 RENDER_THREAD = 1u;
    constexpr U32 MIN_HIGH_PRIORITY_WORKERS = 4u;
    constexpr U32 MIN_ASSET_WORKERS = 4u;
    constexpr U32 MAX_ASSET_WORKERS = 8u;

    struct Counts
    {
        U32 _asset;
        U32 _render;
        U32 _lowPriority;
        U32 _highPriority;
    };

    [[nodiscard]] constexpr Counts Calculate( const U32 requestedTotal, const U32 requestedAssetWorkers = 0u ) noexcept
    {
        const U32 total = std::max( requestedTotal, MIN_TOTAL_WORKERS );
        constexpr U32 reserved = RENDER_WORKERS + LOW_PRIORITY_WORKERS + RENDER_THREAD;
        const U32 availableForAssets = std::max( MIN_ASSET_WORKERS, total - reserved - MIN_HIGH_PRIORITY_WORKERS );
        const U32 desiredAssets = requestedAssetWorkers == 0u ? MAX_ASSET_WORKERS : requestedAssetWorkers;
        const U32 assets = std::min( availableForAssets, std::clamp( desiredAssets, MIN_ASSET_WORKERS, MAX_ASSET_WORKERS ) );

        return { assets, RENDER_WORKERS, LOW_PRIORITY_WORKERS,
                 std::max( total - reserved - assets, MIN_HIGH_PRIORITY_WORKERS ) };
    }
}
