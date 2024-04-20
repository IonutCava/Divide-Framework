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
#ifndef DVD_CIRCULAR_BUFFER_H_
#define DVD_CIRCULAR_BUFFER_H_

//ref: https://github.com/embeddedartistry/embedded-resources/blob/master/examples/cpp/circular_buffer.cpp
namespace Divide {

template <class T, size_t N, bool threadSafe = false>
class CircularBuffer {
    struct Lock
    {
        Lock()
        {
            if constexpr ( threadSafe )
            {
                _lock.lock();
            }
        }
        ~Lock()
        {
            if constexpr ( threadSafe )
            {
                _lock.unlock();
            }
        }
        mutable Mutex _lock;
    };

public:
    void reset()
    {
        Lock();

        _head = _tail;
        _isFull = false;
    }

    void put(const T& item)
    {
        Lock();

        _buffer[_head] = item;

        if ( fullLocked() )
        {
            _tail = (_tail + 1) % N;
        }

        _head = (_head + 1) % N;
        _isFull = _head == _tail;
    }

    [[nodiscard]] const T& get(const size_t idx) const
    {
        Lock();
        return _buffer[idx];
    }

    [[nodiscard]] T get()
    {
        Lock();

        if (!empty())
        {
            const T val = _buffer[_tail];
            _isFull = false;
            _tail = (_tail + 1) % N;

            return val;
        }

        return T();
    }

    [[nodiscard]] inline size_t size() const noexcept
    {
        Lock();

        if ( fullLocked() )
        {
            return N;
        }

        return (_head >= _tail) 
                       ? _head - _tail
                       : N + _head - _tail;
    }

    [[nodiscard]] inline bool empty() const noexcept
    {
        Lock();
        return (!fullLocked() && (_head == _tail));
    }

    [[nodiscard]] inline bool full() const noexcept
    {
        Lock();
        return fullLocked();
    }

    [[nodiscard]] inline static size_t capacity() noexcept
    {
        return N;
    }


private:
    [[nodiscard]] inline bool fullLocked() const noexcept
    {
        return _isFull;
    }

private:
    T _buffer[N];
    size_t _head{0u};
    size_t _tail{0u};
    bool _isFull{false};
};

}; //namespace Divide

#endif //DVD_CIRCULAR_BUFFER_H_
