

#include "Headers/GenericDrawCommand.h"

namespace Divide {

bool Compatible(const GenericDrawCommand& lhs, const GenericDrawCommand& rhs) noexcept {
    return lhs._sourceBuffer == rhs._sourceBuffer;
}

}; //namespace Divide
