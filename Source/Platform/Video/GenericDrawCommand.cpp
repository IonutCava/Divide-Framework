

#include "Headers/GenericDrawCommand.h"

namespace Divide {

bool Compatible(const GenericDrawCommand& lhs, const GenericDrawCommand& rhs) noexcept {
    if ( lhs._sourceBuffersCount != rhs._sourceBuffersCount )
    {
        return false;
    }

    if ( lhs._sourceBuffersCount > 0u)
    {
        if (lhs._sourceBuffers == nullptr || rhs._sourceBuffers == nullptr)
        {
            return false;
        }

        for ( size_t i = 0u; i < lhs._sourceBuffersCount; ++i )
        {
            if ( lhs._sourceBuffers[i] != rhs._sourceBuffers[i])
            {
                return false;
            }
        }
    }

    return true;
}

}; //namespace Divide
