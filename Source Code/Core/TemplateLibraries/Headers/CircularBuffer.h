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
#ifndef _CIRCULAR_BUFFER_H_
#define _CIRCULAR_BUFFER_H_

//ref: https://github.com/embeddedartistry/embedded-resources/blob/master/examples/cpp/circular_buffer.cpp
namespace Divide {

template <class T>
class CircularBuffer {
public:
    explicit CircularBuffer(const size_t bufferSize) :
        _buffer(std::make_unique<T[]>(bufferSize)),
        _maxSize(bufferSize) {

    }

    void reset() {
        std::lock_guard<Mutex> lock(_lock);
        _head = _tail;
        _isFull = false;
    }

    void put(const T& item) {
        std::lock_guard<Mutex> lock(_lock);

        _buffer[_head] = item;

        if (full()) {
            _tail = (_tail + 1) % _maxSize;
        }

        _head = (_head + 1) % _maxSize;
        _isFull = _head == _tail;
    }

    [[nodiscard]] T& get(const size_t idx) {
        std::lock_guard<Mutex> lock(_lock);
        return _buffer[idx];
    }
    
    [[nodiscard]] const T& get(const size_t idx) const {
        std::lock_guard<Mutex> lock(_lock);
        return _buffer[idx];
    }

    [[nodiscard]] T get() {
        std::lock_guard<Mutex> lock(_lock);

        if (!empty()) {
            const T val = _buffer[tail_];
            _isFull = false;
            _tail = (_tail + 1) % _maxSize;

            return val;
        }

        return T();
    }

    [[nodiscard]] inline size_t size() const noexcept {
        if (full()) {
            return _maxSize;
        }

        return (_head >= _tail) 
                       ? _head - _tail
                       : _maxSize + _head - _tail;
    }

    [[nodiscard]] inline bool empty() const noexcept { return (!full() && (_head == _tail)); }
    [[nodiscard]] inline bool full() const noexcept { return _isFull; }
    [[nodiscard]] inline size_t capacity() const noexcept { return _maxSize; }

private:
    mutable Mutex _lock;
    std::unique_ptr<T[]> _buffer;
    size_t _head = 0u;
    size_t _tail = 0u;
    const size_t _maxSize;
    bool _isFull = false;
};

}; //namespace Divide

#endif //_CIRCULAR_BUFFER_H_
