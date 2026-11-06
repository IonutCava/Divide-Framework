#pragma once
// Copyright (c) 2026 DIVIDE-Studio

namespace Divide
{
    class RenderStageSchedule final
    {
    public:
        static constexpr size_t STAGE_COUNT = to_base( RenderStage::COUNT );

        void reset() noexcept
        {
            _dependents = {};
            _initialDependencies = {};
        }

        [[nodiscard]] bool addDependency( const U8 stage, const U8 dependency ) noexcept
        {
            if ( stage >= STAGE_COUNT || dependency >= STAGE_COUNT )
            {
                return false;
            }

            if ( !_dependents[dependency][stage] )
            {
                _dependents[dependency][stage] = true;
                ++_initialDependencies[stage];
            }
            return true;
        }

        [[nodiscard]] bool isAcyclic() const noexcept
        {
            std::array<U8, STAGE_COUNT> order{};
            return topologicalOrder( order );
        }

        // Fills orderOut with a dependency-respecting stage order (lowest ready stage index first).
        // Returns false if the dependencies contain a cycle.
        [[nodiscard]] bool topologicalOrder( std::array<U8, STAGE_COUNT>& orderOut ) const noexcept
        {
            auto remaining = _initialDependencies;
            std::array<bool, STAGE_COUNT> scheduled{};
            for ( size_t count = 0u; count < STAGE_COUNT; ++count )
            {
                size_t ready = STAGE_COUNT;
                for ( size_t stage = 0u; stage < STAGE_COUNT; ++stage )
                {
                    if ( !scheduled[stage] && remaining[stage] == 0u )
                    {
                        ready = stage;
                        break;
                    }
                }

                if ( ready == STAGE_COUNT )
                {
                    return false;
                }

                scheduled[ready] = true;
                orderOut[count] = to_U8( ready );
                for ( size_t stage = 0u; stage < STAGE_COUNT; ++stage )
                {
                    if ( _dependents[ready][stage] )
                    {
                        --remaining[stage];
                    }
                }
            }
            return true;
        }

        void prepare() noexcept
        {
            for ( size_t stage = 0u; stage < STAGE_COUNT; ++stage )
            {
                _remainingDependencies[stage].store( _initialDependencies[stage], std::memory_order_relaxed );
            }
        }

        template<typename Enqueue>
        void enqueueRoots( Enqueue&& enqueue ) const
        {
            // Completions can make other stages ready while roots are being enqueued.
            for ( U8 stage = 0u; stage < to_base( RenderStage::COUNT ); ++stage )
            {
                if ( _initialDependencies[stage] == 0u )
                {
                    enqueue( stage );
                }
            }
        }

        template<typename Enqueue>
        void complete( const U8 completedStage, Enqueue&& enqueue )
        {
            for ( U8 stage = 0u; stage < to_base( RenderStage::COUNT ); ++stage )
            {
                if ( _dependents[completedStage][stage] &&
                     _remainingDependencies[stage].fetch_sub( 1u, std::memory_order_acq_rel ) == 1u )
                {
                    enqueue( stage );
                }
            }
        }

    private:
        std::array<std::array<bool, STAGE_COUNT>, STAGE_COUNT> _dependents{};
        std::array<U8, STAGE_COUNT> _initialDependencies{};
        std::array<std::atomic_uint, STAGE_COUNT> _remainingDependencies{};
    };
}
