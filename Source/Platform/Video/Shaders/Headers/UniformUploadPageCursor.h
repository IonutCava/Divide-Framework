// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "EngineConfig.h"

#include <array>
namespace Divide
{
    class UniformUploadPageCursor
    {
    public:
        static constexpr U16 SlotsPerPage = 64u;

        struct Allocation
        {
            size_t _page{ 0u };
            U16 _slot{ 0u };
            U64 _generation{ 0u };

            bool operator==( const Allocation& ) const = default;
        };

        struct Usage
        {
            struct Frame
            {
                U64 _number{ 0u };
                bool _valid{ false };
            };

            void markUsed( const U64 frameNumber ) noexcept
            {
                _frames[to_size( frameNumber % Config::MAX_FRAMES_IN_FLIGHT )] = { frameNumber, true };
            }

            std::array<Frame, Config::MAX_FRAMES_IN_FLIGHT> _frames{};
        };

        // Call only after every consumer of this frame slot has completed.
        [[nodiscard]] bool beginFrame( const U64 frameNumber )
        {
            if ( _hasFrame && frameNumber < _frameNumber )
            {
                return false;
            }
            _frameNumber = frameNumber;
            _hasFrame = true;

            if ( frameNumber >= Config::MAX_FRAMES_IN_FLIGHT )
            {
                const size_t frameSlot = to_size( frameNumber % Config::MAX_FRAMES_IN_FLIGHT );
                CompletedFrame& completed = _completed[frameSlot];
                completed._number = frameNumber - Config::MAX_FRAMES_IN_FLIGHT;
                completed._valid = true;

                auto& retired = _retired[frameSlot];
                for ( size_t i = 0u; i < retired.size(); )
                {
                    const size_t pendingSlot = pendingFrameSlot( retired[i]._usage );
                    if ( pendingSlot != frameSlot )
                    {
                        const Allocation allocation = retired[i]._allocation;
                        if ( pendingSlot == Config::MAX_FRAMES_IN_FLIGHT )
                        {
                            _available.push_back( allocation );
                            _states[IndexOf( allocation )]._state = SlotState::AVAILABLE;
                        }
                        else
                        {
                            _retired[pendingSlot].push_back( retired[i] );
                        }
                        retired[i] = retired.back();
                        retired.pop_back();
                    }
                    else
                    {
                        ++i;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] Allocation reserve()
        {
            DIVIDE_ASSERT( _hasFrame );
            while ( !_available.empty() )
            {
                Allocation allocation = _available.back();
                _available.pop_back();
                Slot& slot = _states[IndexOf( allocation )];
                if ( slot._generation == U64_MAX )
                {
                    slot._state = SlotState::EXHAUSTED;
                    continue;
                }
                allocation._generation = ++slot._generation;
                slot._state = SlotState::IN_USE;
                return allocation;
            }

            const size_t index = _states.size();
            _states.push_back( { SlotState::IN_USE, 1u } );
            return { index / SlotsPerPage, to_U16( index % SlotsPerPage ), 1u };
        }

        [[nodiscard]] bool release( const Allocation allocation, const Usage& usage )
        {
            if ( !_hasFrame || allocation._slot >= SlotsPerPage ||
                 allocation._page > SIZE_MAX / SlotsPerPage )
            {
                return false;
            }

            const size_t index = IndexOf( allocation );
            if ( index >= _states.size() || _states[index]._state != SlotState::IN_USE ||
                 _states[index]._generation != allocation._generation )
            {
                return false;
            }

            for ( size_t frameSlot = 0u; frameSlot < usage._frames.size(); ++frameSlot )
            {
                const Usage::Frame& frame = usage._frames[frameSlot];
                if ( frame._valid &&
                     ( frame._number > _frameNumber ||
                       frame._number % Config::MAX_FRAMES_IN_FLIGHT != frameSlot ) )
                {
                    return false;
                }
            }

            const size_t pendingSlot = pendingFrameSlot( usage );
            if ( pendingSlot == Config::MAX_FRAMES_IN_FLIGHT )
            {
                _available.push_back( allocation );
                _states[index]._state = SlotState::AVAILABLE;
            }
            else
            {
                _retired[pendingSlot].push_back( { allocation, usage } );
                _states[index]._state = SlotState::RETIRED;
            }
            return true;
        }

    private:
        static_assert(Config::MAX_FRAMES_IN_FLIGHT > 0u);

        enum class SlotState : U8
        {
            IN_USE,
            RETIRED,
            AVAILABLE,
            EXHAUSTED
        };

        struct Slot
        {
            SlotState _state;
            U64 _generation;
        };

        struct RetiredAllocation
        {
            Allocation _allocation;
            Usage _usage;
        };

        struct CompletedFrame
        {
            U64 _number{ 0u };
            bool _valid{ false };
        };

        [[nodiscard]] static constexpr size_t IndexOf( const Allocation allocation ) noexcept
        {
            return allocation._page * SlotsPerPage + allocation._slot;
        }

        [[nodiscard]] size_t pendingFrameSlot( const Usage& usage ) const noexcept
        {
            for ( size_t frameSlot = 0u; frameSlot < usage._frames.size(); ++frameSlot )
            {
                const Usage::Frame& frame = usage._frames[frameSlot];
                const CompletedFrame& completed = _completed[frameSlot];
                if ( frame._valid && ( !completed._valid || frame._number > completed._number ) )
                {
                    return frameSlot;
                }
            }
            return Config::MAX_FRAMES_IN_FLIGHT;
        }

        std::array<CompletedFrame, Config::MAX_FRAMES_IN_FLIGHT> _completed{};
        std::array<vector<RetiredAllocation>, Config::MAX_FRAMES_IN_FLIGHT> _retired;
        vector<Allocation> _available;
        vector<Slot> _states;
        U64 _frameNumber{ 0u };
        bool _hasFrame{ false };
    };
}
