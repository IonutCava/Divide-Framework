#include "stdafx.h"

#include "Headers/GenericDrawCommand.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"

namespace Divide {

bool Compatible(const GenericDrawCommand& lhs, const GenericDrawCommand& rhs) noexcept {
    return lhs._sourceBuffer == rhs._sourceBuffer &&
           lhs._bufferFlag == rhs._bufferFlag;
}

}; //namespace Divide
