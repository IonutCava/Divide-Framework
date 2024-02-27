

#include "Headers/RingBuffer.h"

namespace Divide {

RingBufferSeparateWrite::RingBufferSeparateWrite(const U32 queueLength, const bool separateReadWrite, const bool writeAhead) noexcept 
    : _queueLength(std::max(queueLength, 1u)),
      _writeAhead(writeAhead),
      _separateReadWrite(separateReadWrite)
{
}

void RingBufferSeparateWrite::resize(const U32 queueLength) {
    if (_queueLength != std::max(queueLength, 1u)) {
        _queueLength = std::max(queueLength, 1u);
        _queueIndex = to_I32(std::min(to_U32(_queueIndex.load()), _queueLength - 1u));
    }
}

RingBuffer::RingBuffer(const U32 queueLength)  noexcept :
    _queueLength(std::max(queueLength, 1u))
{
    _queueIndex = 0;
}

void RingBuffer::resize(const U32 queueLength) noexcept {
    _queueLength = std::max(queueLength, 1U);
    _queueIndex = 0;
}

} //namespace Divide