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

class RingBufferSeparateWrite : public NonCopyable {
public:
    // If separateReadWrite is true, this behaves exactly like a RingBuffer
    explicit RingBufferSeparateWrite(U16 queueLength, bool separateReadWrite, bool writeAhead = true) noexcept;
    virtual ~RingBufferSeparateWrite() = default;

    virtual void resize(U16 queueLength);

    [[nodiscard]] U16 queueLength() const noexcept { return _queueLength; }

    [[nodiscard]] I32 queueWriteIndex() const noexcept {
        const I32 ret = _queueIndex.load();

        if (_separateReadWrite)
        {
            return (ret + (_writeAhead ? 1 : to_I32(_queueLength) - 1)) % to_I32(_queueLength);
        }
        
        return ret;
    }

    [[nodiscard]] I32 queueReadIndex() const noexcept { return _queueIndex; }

    I32 incQueue() noexcept {
        if (queueLength() > 1) {
            _queueIndex = (_queueIndex + 1) % to_I32( _queueLength );
        }

        return queueWriteIndex();
    }

    I32 decQueue() noexcept {
        if (queueLength() > 1) {
            if (_queueIndex == 0) {
                _queueIndex = to_I32( _queueLength );
            }

            _queueIndex = (_queueIndex - 1) % to_I32( _queueLength );
        }

        return queueWriteIndex();
    }

private:
    U16 _queueLength = 1u;
    const bool _writeAhead = false;
    const bool _separateReadWrite = false;
    std::atomic_int _queueIndex = 0;
};

class RingBuffer : public NonCopyable {
public:
    explicit RingBuffer(U16 queueLength) noexcept;
    virtual ~RingBuffer() = default;

    virtual void resize(U16 queueLength) noexcept;

    [[nodiscard]] U16 queueLength() const noexcept { return _queueLength; }
    [[nodiscard]] I32 queueIndex() const noexcept { return _queueIndex; }

    void incQueue() noexcept
    {
        if (queueLength() > 1)
        {
            _queueIndex = (_queueIndex + 1) % to_I32(_queueLength);
        }
    }

    void decQueue() noexcept
    {
        if (queueLength() > 1)
        {
            _queueIndex = (_queueIndex - 1) % to_I32(_queueLength);
        }
    }

private:
    U16 _queueLength = 1u;
    std::atomic_int _queueIndex;
};

} //namespace Divide

#endif //DVD_CORE_RING_BUFFER_H_
