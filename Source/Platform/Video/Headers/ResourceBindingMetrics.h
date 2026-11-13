// Copyright (c) 2026 DIVIDE-Studio
#pragma once

#include "DescriptorSets.h"

namespace Divide
{
    struct ResourceBindingCounters
    {
        U64 _commands{ 0u }, _materialCommands{ 0u }, _overrideSlots{ 0u };
        U64 _uniformCommits{ 0u }, _uniformChanges{ 0u }, _fallbackChecks{ 0u }, _fallbackSets{ 0u };
        U64 _imageResolutions{ 0u }, _bufferResolutions{ 0u };
        U64 _compiledBindingHits{ 0u }, _compiledBindingMisses{ 0u }, _compiledBindingCacheBytes{ 0u };
        U64 _pushCalls{ 0u }, _pushWrites{ 0u }, _materialWrites{ 0u }, _viewWrites{ 0u }, _bufferWrites{ 0u };
        U64 _glTextureVisits{ 0u }, _glBufferVisits{ 0u };
        U64 _drawSets{ 0u }, _uniqueEffectiveSets{ 0u }, _effectiveOverflow{ 0u };
        U64 _uniqueMaterialLayouts{ 0u }, _materialOverflow{ 0u };
        U64 _storageGrowths{ 0u };
    };

    // Translation-owner-only, bounded diagnostics. Disabled outside explicit perf runs.
    class ResourceBindingMetrics
    {
    public:
        static constexpr size_t CAPACITY = 8192u;
        void beginFrame( bool enabled );
        void endFrame() noexcept { _previous = _current; }
        void observe( I64 program, U64 materialRevision, const DescriptorSet& bindings, U32 activeSlots = U32_MAX );

        [[nodiscard]] bool enabled() const noexcept { return _enabled; }
        [[nodiscard]] ResourceBindingCounters& current() noexcept { return _current; }
        [[nodiscard]] const ResourceBindingCounters& previous() const noexcept { return _previous; }
        [[nodiscard]] size_t storageBytes() const noexcept { return _sets.capacity() * sizeof( Sample ) + sizeof( _setIndex ) + sizeof( _materials ); }

    private:
        struct Sample
        {
            I64 _program{ 0 };
            size_t _hash{ 0u };
            DescriptorSet _bindings{};
        };
        struct MaterialLayout
        {
            I64 _program{ 0 };
            U64 _revision{ 0u };
        };
        vector<Sample> _sets;
        std::array<U16, CAPACITY * 2u> _setIndex{};
        std::array<MaterialLayout, CAPACITY * 2u> _materials{};
        ResourceBindingCounters _current{}, _previous{};
        bool _enabled{ false };
    };
}
