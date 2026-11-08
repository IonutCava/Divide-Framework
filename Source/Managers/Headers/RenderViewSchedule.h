// Copyright (c) 2026 DIVIDE-Studio

#pragma once
#ifndef DVD_RENDER_VIEW_SCHEDULE_H_
#define DVD_RENDER_VIEW_SCHEDULE_H_

#include "Core/Headers/TaskPool.h"

namespace Divide
{
    class RenderViewSchedule
    {
    public:
        static constexpr size_t LANE_COUNT = 2u;

        void reserve( const size_t viewCount )
        {
            for ( auto& lane : _lanes )
            {
                lane.reserve( viewCount );
            }
        }

        void reset()
        {
            for ( auto& lane : _lanes )
            {
                lane.clear();
            }
            _viewCount = 0u;
        }

        void add( const U16 passIndex )
        {
            // Repeated package identities must never record concurrently.
            _lanes[passIndex % LANE_COUNT].push_back( _viewCount++ );
        }

        [[nodiscard]] bool usesLane( const size_t lane ) const noexcept
        {
            return !_lanes[lane].empty();
        }

        template<typename RecordView>
        void record( TaskPool& pool, RecordView&& recordView, const bool parallel = true ) const
        {
            if ( !parallel )
            {
                for ( size_t view = 0u; view < _viewCount; ++view )
                {
                    for ( size_t lane = 0u; lane < LANE_COUNT; ++lane )
                    {
                        if ( eastl::find( _lanes[lane].begin(), _lanes[lane].end(), view ) != _lanes[lane].end() )
                        {
                            recordView( lane, view );
                            break;
                        }
                    }
                }
                return;
            }
            TaskGroup tasks( pool );
            size_t callerLane = LANE_COUNT;
            for ( size_t lane = 0u; lane < LANE_COUNT; ++lane )
            {
                if ( usesLane( lane ) )
                {
                    if ( callerLane == LANE_COUNT )
                    {
                        callerLane = lane;
                        continue;
                    }
                    tasks.enqueue( [this, lane, &recordView]( const Task& )
                    {
                        for ( const size_t view : _lanes[lane] )
                        {
                            recordView( lane, view );
                        }
                    } );
                }
            }
            if ( callerLane != LANE_COUNT )
            {
                for ( const size_t view : _lanes[callerLane] )
                {
                    recordView( callerLane, view );
                }
            }
            tasks.wait();
        }

    private:
        std::array<vector<size_t>, LANE_COUNT> _lanes;
        size_t _viewCount = 0u;
    };
}

#endif //DVD_RENDER_VIEW_SCHEDULE_H_
