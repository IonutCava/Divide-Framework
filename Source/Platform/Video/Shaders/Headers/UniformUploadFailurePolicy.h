// Copyright (c) 2026 DIVIDE-Studio
#pragma once

namespace Divide
{
    enum class UniformUploadResult : U8
    {
        OK = 0,
        // Permanent for the block: its size or the device alignment can never be satisfied.
        INVALID_BLOCK,
        // Possibly transient, e.g. a new page could not be created under VRAM pressure.
        ALLOCATION_FAILED,
        // Arena bookkeeping is inconsistent. Always a logic error.
        INTERNAL_ERROR,
        COUNT
    };

    enum class UniformCommitResult : U8
    {
        UNCHANGED = 0,
        CHANGED,
        // At least one block has no valid allocation, so draws using the program must be skipped.
        MISSING,
        COUNT
    };

    [[nodiscard]] constexpr UniformCommitResult CombineUniformCommitResults( const UniformCommitResult lhs, const UniformCommitResult rhs ) noexcept
    {
        return lhs > rhs ? lhs : rhs;
    }

    // Decides when uniform upload failures become fatal. Transient allocation failures are retried every frame
    // and only shut the renderer down after a sustained streak; internal errors are fatal immediately.
    class UniformUploadFailurePolicy
    {
    public:
        static constexpr U32 MAX_CONSECUTIVE_FAILURE_FRAMES = 120u;

        void beginFrame() noexcept
        {
            _failedThisFrame = false;
        }

        // Returns true for the first transient failure of a new failure streak, so callers can log it once.
        [[nodiscard]] bool onFailure( const UniformUploadResult result ) noexcept
        {
            switch ( result )
            {
                case UniformUploadResult::ALLOCATION_FAILED:
                {
                    const bool firstOfStreak = !_failedThisFrame && _consecutiveFailureFrames == 0u;
                    _failedThisFrame = true;
                    return firstOfStreak;
                }
                case UniformUploadResult::INTERNAL_ERROR:
                    _fatal = true;
                    return false;
                default:
                    break;
            }
            return false;
        }

        // Returns false when the renderer should stop.
        [[nodiscard]] bool endFrame() noexcept
        {
            _recovered = false;
            if ( _failedThisFrame )
            {
                if ( _consecutiveFailureFrames < U32_MAX )
                {
                    ++_consecutiveFailureFrames;
                }
            }
            else if ( _consecutiveFailureFrames > 0u )
            {
                _recovered = true;
                _consecutiveFailureFrames = 0u;
            }

            return !_fatal && _consecutiveFailureFrames < MAX_CONSECUTIVE_FAILURE_FRAMES;
        }

        [[nodiscard]] bool fatal() const noexcept { return _fatal; }
        [[nodiscard]] bool failedThisFrame() const noexcept { return _failedThisFrame; }
        [[nodiscard]] U32 consecutiveFailureFrames() const noexcept { return _consecutiveFailureFrames; }
        // True when the frame that just ended closed a failure streak.
        [[nodiscard]] bool recovered() const noexcept { return _recovered; }

    private:
        U32 _consecutiveFailureFrames{ 0u };
        bool _failedThisFrame{ false };
        bool _fatal{ false };
        bool _recovered{ false };
    };
} // namespace Divide
