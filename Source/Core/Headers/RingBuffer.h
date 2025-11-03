/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_CORE_RING_BUFFER_H_
#define DVD_CORE_RING_BUFFER_H_

namespace Divide {

class RingBuffer : public NonCopyable
{
public:
    explicit RingBuffer(U16 queueLength) noexcept;
    virtual ~RingBuffer() = default;

    virtual void resize(U16 queueLength);

    [[nodiscard]] U16 queueLength() const noexcept
    {
        return _queueLength;
    }

    [[nodiscard]] U16 queueIndex() const noexcept
    {
        return _queueIndex.load();
    }

    I32 incQueue() noexcept
    {
        if (queueLength() > 1)
        {
            _queueIndex = (_queueIndex + 1) % _queueLength;
        }

        return queueIndex();
    }

    I32 decQueue() noexcept
    {
        if (queueLength() > 1)
        {
            if (_queueIndex == 0)
            {
                _queueIndex.store(_queueLength);
            }

            _queueIndex = (_queueIndex - 1) % _queueLength;
        }

        return queueIndex();
    }

protected:
    U16 _queueLength = 1u;
    std::atomic<U16> _queueIndex;
};

class RingBufferSeparateWrite : public RingBuffer {
public:
    // If separateReadWrite is true, this behaves exactly like a RingBuffer
    explicit RingBufferSeparateWrite(U16 queueLength, bool separateReadWrite, bool writeAhead = true) noexcept;
    virtual ~RingBufferSeparateWrite() = default;

    [[nodiscard]] U16 queueWriteIndex() const noexcept
    {
        const U16 ret = queueIndex();
        const U16 length = queueLength();

        // Prevent division by zero and clarify behavior for small buffer sizes
        if (length <= 1) {
            return ret;
        }
        if (_separateReadWrite)
        {
            return (ret + (_writeAhead ? 1 : (length - 1)) % length;
        }
        
        return ret;
    }

    [[nodiscard]] U16 queueReadIndex() const noexcept
    {
        return queueIndex();
    }

private:
    const bool _writeAhead = false;
    const bool _separateReadWrite = false;
};


} //namespace Divide

#endif //DVD_CORE_RING_BUFFER_H_
