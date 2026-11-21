#pragma once
// Copyright (c) 2026 DIVIDE-Studio

namespace Divide::RenderSmokeTest
{
    enum class RecoveryStage
    {
        WAITING_FOR_GPU,
        GPU_MATCHED,
        DEVICE_CREATED,
        SUBMITTED,
        RECOVERED,
        FAILED
    };

    class RecoveryProgress
    {
    public:
        [[nodiscard]] bool matchDevice( const bool matches ) noexcept
        {
            return advance( RecoveryStage::WAITING_FOR_GPU, matches, RecoveryStage::GPU_MATCHED );
        }

        [[nodiscard]] bool createDevice( const bool successful ) noexcept
        {
            return advance( RecoveryStage::GPU_MATCHED, successful, RecoveryStage::DEVICE_CREATED );
        }

        [[nodiscard]] bool submit( const bool successful ) noexcept
        {
            return advance( RecoveryStage::DEVICE_CREATED, successful, RecoveryStage::SUBMITTED );
        }

        [[nodiscard]] bool completeFence( const bool successful ) noexcept
        {
            return advance( RecoveryStage::SUBMITTED, successful, RecoveryStage::RECOVERED );
        }

        [[nodiscard]] bool recovered() const noexcept
        {
            return _stage == RecoveryStage::RECOVERED;
        }

    private:
        [[nodiscard]] bool advance( const RecoveryStage expected, const bool successful, const RecoveryStage next ) noexcept
        {
            if ( _stage != expected || !successful )
            {
                _stage = RecoveryStage::FAILED;
                return false;
            }
            _stage = next;
            return true;
        }

        RecoveryStage _stage{ RecoveryStage::WAITING_FOR_GPU };
    };
}
