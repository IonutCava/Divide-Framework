#include "stdafx.h"

#include "Headers/BufferLocks.h"

namespace Divide {

    [[nodiscard]] bool IsEmpty(const BufferLocks& locks) noexcept {
        if (locks.empty())
        {
            return true;
        }

        for (auto& it : locks) {
            if ( it._type != BufferSyncUsage::COUNT )
            {
                if (it._buffer != nullptr)
                {
                    return false;
                }
            }
        }

        return true;
    }

} //namespace Divide
