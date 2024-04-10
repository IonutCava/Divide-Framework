#include "Headers/ErrorCodes.h"

namespace Divide
{
    static_assert(std::size( Names::errorCode ) == to_base( ErrorCode::COUNT ) + 1u, "ErrorCode name array out of sync!");
} //namespace Divide
