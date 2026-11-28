#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "ShaderDataUploader.h"
#include "Platform/Video/Headers/DescriptorSets.h"

namespace Divide
{
    using SampledImageFallback = void (*)( DescriptorSetBindingData&, const Reflection::ImageEntry& );

    // Scratch must not alias entry._set and is unchanged if no fallback is needed.
    [[nodiscard]] bool ResolveSampledImages( const DescriptorSetEntry& entry,
                                             const vector<Reflection::ImageEntry>& images,
                                             DescriptorSet& scratch,
                                             SampledImageFallback applyFallback );

    struct SampledImageResolutionCache
    {
        DescriptorSet _resolvedSet{};

        [[nodiscard]] FORCE_INLINE bool update( const bool hasFallback, const bool alreadyDirty ) noexcept
        {
            if ( !hasFallback )
            {
                _previousWasFallback = false;
                return false;
            }

            const bool changed = !_previousWasFallback || (!alreadyDirty && _previousFallback != _resolvedSet);
            if ( alreadyDirty || changed )
            {
                _previousFallback = _resolvedSet;
            }
            _previousWasFallback = true;
            return changed;
        }

    private:
        DescriptorSet _previousFallback{};
        bool _previousWasFallback{ false };
    };
} // namespace Divide
