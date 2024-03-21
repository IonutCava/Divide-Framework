

#include "Headers/RingBuffer.h"

namespace Divide {

RingBufferSeparateWrite::RingBufferSeparateWrite(const U16 queueLength, const bool separateReadWrite, const bool writeAhead) noexcept 
    : _queueLength(std::max(queueLength, U16_ONE)),
      _writeAhead(writeAhead),
      _separateReadWrite(separateReadWrite)
{
}

void RingBufferSeparateWrite::resize(const U16 queueLength) {
    if (_queueLength != std::max(queueLength, U16_ONE)) {
        _queueLength = std::max(queueLength, U16_ONE);
        _queueIndex = to_I32(std::min(to_U32(_queueIndex.load()), _queueLength - 1u));
    }
}

RingBuffer::RingBuffer(const U16 queueLength)  noexcept :
    _queueLength(std::max(queueLength, U16_ONE))
{
    _queueIndex = 0;
}

void RingBuffer::resize(const U16 queueLength) noexcept {
    _queueLength = std::max(queueLength, U16_ONE);
    _queueIndex = 0;
}

} //namespace Divide
