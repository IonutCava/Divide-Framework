#include "stdafx.h"

#include "Headers/RingBuffer.h"

namespace Divide {

RingBufferSeparateWrite::RingBufferSeparateWrite(const I32 queueLength, const bool separateReadWrite, const bool writeAhead) noexcept 
    : _queueLength(std::max(queueLength, 1)),
      _writeAhead(writeAhead),
      _separateReadWrite(separateReadWrite)
{
}

void RingBufferSeparateWrite::resize(const I32 queueLength) {
    if (_queueLength != std::max(queueLength, 1)) {
        _queueLength = std::max(queueLength, 1);
        _queueIndex = std::min(_queueIndex.load(), _queueLength - 1);
    }
}

RingBuffer::RingBuffer(const I32 queueLength)  noexcept :
    _queueLength(std::max(queueLength, 1))
{
    _queueIndex = 0;
}

void RingBuffer::resize(const I32 queueLength) noexcept {
    _queueLength = std::max(queueLength, 1);
    _queueIndex = 0;
}

} //namespace Divide