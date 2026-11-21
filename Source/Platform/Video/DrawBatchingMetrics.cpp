// Copyright (c) 2026 DIVIDE-Studio
#include "Headers/DrawBatchingMetrics.h"

#include "Headers/Commands.h"
#include "Headers/GenericDrawCommand.h"

namespace Divide
{
    namespace
    {
        [[nodiscard]] U32 NativeCalls( const GenericDrawCommand& draw ) noexcept
        {
            return (isEnabledOption( draw, CmdRenderOptions::RENDER_GEOMETRY ) ? 1u : 0u) +
                   (isEnabledOption( draw, CmdRenderOptions::RENDER_WIREFRAME ) ? 1u : 0u);
        }
    }

    void DrawBatchingMetrics::observeDraw( const GFX::CommandBuffer::CommandList& commands,
                                           const size_t drawIndex,
                                           const GFX::DrawCommand& draw,
                                           const size_t consumed,
                                           const U32 batchDrawCount,
                                           const GFX::IndirectBatchStop stop ) noexcept
    {
        if ( !_enabled )
        {
            return;
        }

        DrawBatchingCounters& counters = _current;
        const auto& draws = draw._drawCommands;

        if ( consumed > 0u )
        {
            // The head and every absorbed draw are single-record candidates that leave as one native call
            const U64 fused = 1u + consumed / 2u;
            counters._drawCommands += fused;
            counters._genericDraws += fused;
            counters._candidates[to_base( GFX::IndirectDrawCandidate::CANDIDATE )] += fused;
            ++counters._nativeCalls;
            ++counters._indirectCalls;
            counters._indirectRecords += batchDrawCount;
            ++counters._recordsPerCall[DrawBatchingCounters::RecordBucket( batchDrawCount )];
            ++counters._collectedRuns;
            counters._collectedRecords += batchDrawCount;
        }
        else
        {
            ++counters._drawCommands;
            counters._genericDraws += draws.size();
            for ( const GenericDrawCommand& gdc : draws )
            {
                ++counters._candidates[to_base( GFX::ClassifyIndirectDraw( gdc ) )];

                const U32 calls = NativeCalls( gdc );
                counters._nativeCalls += calls;
                if ( calls > 0u && isEnabledOption( gdc, CmdRenderOptions::RENDER_INDIRECT ) )
                {
                    counters._indirectCalls += calls;
                    counters._indirectRecords += U64{ calls } * gdc._drawCount;
                    counters._recordsPerCall[DrawBatchingCounters::RecordBucket( gdc._drawCount )] += calls;
                }
            }
        }

        // Only a lone candidate that follows its push constants can start a run
        if ( drawIndex > 0u && drawIndex < commands.size() && draws.size() == 1u &&
             GFX::ClassifyIndirectDraw( draws.front() ) == GFX::IndirectDrawCandidate::CANDIDATE )
        {
            const GFX::CommandBase* previous = commands[drawIndex - 1u];
            if ( previous != nullptr && previous->type() == GFX::CommandType::SEND_PUSH_CONSTANTS )
            {
                ++counters._runs;
                ++counters._runStops[to_base( stop )];

                const size_t nextIndex = drawIndex + consumed + 1u;
                const GFX::CommandBase* next = nextIndex < commands.size() ? commands[nextIndex] : nullptr;
                ++counters._runNext[next != nullptr ? to_size( next->type() ) : to_size( GFX::CommandType::COUNT )];
            }
        }
    }
}
