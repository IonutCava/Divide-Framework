// Copyright (c) 2026 DIVIDE-Studio
#pragma once

#include "CommandBuffer.h"

namespace Divide
{
    struct DrawBatchingCounters
    {
        static constexpr size_t RECORD_BUCKETS = 8u;
        static constexpr size_t CANDIDATE_CLASSES = to_size( GFX::IndirectDrawCandidate::COUNT );
        static constexpr size_t STOP_REASONS = to_size( GFX::IndirectBatchStop::COUNT );
        // One slot per command type, plus one for "no command follows the run"
        static constexpr size_t NEXT_COMMANDS = to_size( GFX::CommandType::COUNT ) + 1u;

        // Buckets: 1, 2-3, 4-7, 8-15, 16-31, 32-63, 64-127, 128+
        [[nodiscard]] static constexpr size_t RecordBucket( const U32 recordCount ) noexcept
        {
            size_t bucket = 0u;
            for ( U32 limit = 2u; bucket + 1u < RECORD_BUCKETS && recordCount >= limit; limit <<= 1u )
            {
                ++bucket;
            }
            return bucket;
        }

        U64 _drawCommands{ 0u };     // DRAW_COMMANDS entering submission
        U64 _genericDraws{ 0u };     // Generic draws inside those commands
        U64 _nativeCalls{ 0u };      // Geometry + wireframe calls after submission-time batching
        U64 _indirectCalls{ 0u };    // ... of which use indirect records
        U64 _indirectRecords{ 0u };  // Indirect records issued by those calls
        U64 _collectedRuns{ 0u };    // Adjacent draws fused at submission
        U64 _collectedRecords{ 0u }; // Records inside the fused runs
        U64 _runs{ 0u };             // Lone candidate draws that could start a run
        std::array<U64, RECORD_BUCKETS> _recordsPerCall{};
        std::array<U64, CANDIDATE_CLASSES> _candidates{};
        std::array<U64, STOP_REASONS> _runStops{};
        std::array<U64, NEXT_COMMANDS> _runNext{};
    };

    // Translation-owner-only (no atomics) diagnostics for submission-time multi-draw batching.
    // Disabled outside explicit perf runs; the disabled path is a single flag check.
    class DrawBatchingMetrics
    {
    public:
        void beginFrame( const bool enabled ) noexcept
        {
            _enabled = enabled;
            _current = {};
        }

        void endFrame() noexcept { _previous = _current; }

        // 'consumed' and 'batchDrawCount' come from GFX::CollectIndirectDrawBatch for the draw at 'drawIndex'
        // (zero when nothing was fused); 'stop' is why the run beginning at that draw ended.
        void observeDraw( const GFX::CommandBuffer::CommandList& commands,
                          size_t drawIndex,
                          const GFX::DrawCommand& draw,
                          size_t consumed,
                          U32 batchDrawCount,
                          GFX::IndirectBatchStop stop ) noexcept;

        [[nodiscard]] bool enabled() const noexcept { return _enabled; }
        [[nodiscard]] const DrawBatchingCounters& current() const noexcept { return _current; }
        [[nodiscard]] const DrawBatchingCounters& previous() const noexcept { return _previous; }

    private:
        DrawBatchingCounters _current{}, _previous{};
        bool _enabled{ false };
    };
}
