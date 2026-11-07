// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava
// SPDX-License-Identifier: MIT

#pragma once

#include "Platform/Headers/PlatformDataTypes.h"
#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Core/Math/Headers/MathVectors.h"

namespace Divide
{

class CullCounterReadbackState
{
public:
    enum class SlotState : U8
    {
        FREE,
        RESERVED,
        IN_FLIGHT
    };

    void reset( const U16 slotCount )
    {
        _slotStates.assign( slotCount, SlotState::FREE );
        _readbackData.assign( slotCount, uint4{} );
        _sequences.assign( slotCount, 0u );
    }

    [[nodiscard]] U16 reserve( const U16 startIndex, const U64 sequence ) noexcept
    {
        const size_t slotCount = _slotStates.size();
        if ( slotCount == 0u )
        {
            return U16_MAX;
        }

        for ( size_t offset = 0u; offset < slotCount; ++offset )
        {
            const size_t candidate = ( startIndex + offset ) % slotCount;
            if ( _slotStates[candidate] == SlotState::FREE )
            {
                _slotStates[candidate] = SlotState::RESERVED;
                _readbackData[candidate] = {};
                _sequences[candidate] = sequence;
                return static_cast<U16>( candidate );
            }
        }

        return U16_MAX;
    }

    [[nodiscard]] bool markInFlight( const U16 slot ) noexcept
    {
        if ( slot >= _slotStates.size() || _slotStates[slot] != SlotState::RESERVED )
        {
            return false;
        }

        _slotStates[slot] = SlotState::IN_FLIGHT;
        return true;
    }

    void releaseReserved() noexcept
    {
        for ( SlotState& state : _slotStates )
        {
            if ( state == SlotState::RESERVED )
            {
                state = SlotState::FREE;
            }
        }
    }

    [[nodiscard]] uint4* readbackData( const U16 slot ) noexcept
    {
        return slot < _readbackData.size() ? &_readbackData[slot] : nullptr;
    }

    [[nodiscard]] bool complete( const U16 slot,
                                 const bool readSucceeded,
                                 uint4& latestResult,
                                 U64& latestSequence ) noexcept
    {
        if ( slot >= _slotStates.size() || _slotStates[slot] != SlotState::IN_FLIGHT )
        {
            return false;
        }

        const bool publishResult = readSucceeded && _sequences[slot] >= latestSequence;
        if ( publishResult )
        {
            latestResult = _readbackData[slot];
            latestSequence = _sequences[slot];
        }

        _slotStates[slot] = SlotState::FREE;
        return publishResult;
    }

    [[nodiscard]] SlotState state( const U16 slot ) const noexcept
    {
        return slot < _slotStates.size() ? _slotStates[slot] : SlotState::FREE;
    }

private:
    vector<SlotState> _slotStates;
    vector<uint4> _readbackData;
    vector<U64> _sequences;
};

} // namespace Divide
