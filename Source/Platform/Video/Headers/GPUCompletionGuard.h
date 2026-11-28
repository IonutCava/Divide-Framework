// Copyright (c) 2026 DIVIDE-Studio

#pragma once

namespace Divide
{
    template<typename Wait, typename Teardown>
    [[nodiscard]] bool TeardownAfterGPUCompletion( const bool completionRequired, Wait&& wait, Teardown&& teardown )
    {
        if ( completionRequired && !wait() )
        {
            return false;
        }

        teardown();
        return true;
    }
}
