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

 /*
   MMO Client/Server Framework using ASIO
   "Happy Birthday Mrs Javidx9!" - javidx9

   Videos:
   Part #1: https://youtu.be/2hNdkYInj4g
   Part #2: https://youtu.be/UbjxGvrDrbw

   License (OLC-3)
   ~~~~~~~~~~~~~~~

   Copyright 2018 - 2020 OneLoneCoder.com

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions or derivations of source code must retain the above
   copyright notice, this list of conditions and the following disclaimer.

   2. Redistributions or derivative works in binary form must reproduce
   the above copyright notice. This list of conditions and the following
   disclaimer must be reproduced in the documentation and/or other
   materials provided with the distribution.

   3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Links
   ~~~~~
   YouTube:	https://www.youtube.com/javidx9
   Discord:	https://discord.gg/WhwHUMV
   Twitter:	https://www.twitter.com/javidx9
   Twitch:		https://www.twitch.tv/javidx9
   GitHub:		https://www.github.com/onelonecoder
   Homepage:	https://www.onelonecoder.com

   Author
   ~~~~~~
   David Barr, aka javidx9, (C)OneLoneCoder 2019, 2020

*/


#pragma once
#ifndef DVD_NETWORKING_COMMON_H_
#define DVD_NETWORKING_COMMON_H_	

#include "NetworkPacket.h"

namespace Divide
{
namespace Networking
{
    template<typename T>
    class tsqueue
    {
    public:
        tsqueue() = default;
        tsqueue(const tsqueue<T>&) = delete;
        virtual ~tsqueue() { clear(); }

    public:
        // Returns and maintains item at front of Queue
        const T& front()
        {
            ScopedLock<Mutex> lock(muxQueue);
            return deqQueue.front();
        }

        // Returns and maintains item at back of Queue
        const T& back()
        {
            ScopedLock<Mutex> lock(muxQueue);
            return deqQueue.back();
        }

        // Removes and returns item from front of Queue
        T pop_front()
        {
            ScopedLock<Mutex> lock(muxQueue);
            auto t = MOV(deqQueue.front());
            deqQueue.pop_front();
            return t;
        }

        // Removes and returns item from back of Queue
        T pop_back()
        {
            ScopedLock<Mutex> lock(muxQueue);
            auto t = MOV(deqQueue.back());
            deqQueue.pop_back();
            return t;
        }

        // Adds an item to back of Queue
        void push_back(const T& item)
        {
            ScopedLock<Mutex> lock(muxQueue);
            deqQueue.emplace_back(MOV(item));

            UniqueLock<Mutex> ul(muxBlocking);
            cvBlocking.notify_one();
        }

        // Adds an item to front of Queue
        void push_front(const T& item)
        {
            ScopedLock<Mutex> lock(muxQueue);
            deqQueue.emplace_front(MOV(item));

            UniqueLock<Mutex> ul(muxBlocking);
            cvBlocking.notify_one();
        }

        // Returns true if Queue has no items
        bool empty()
        {
            ScopedLock<Mutex> lock(muxQueue);
            return deqQueue.empty();
        }

        // Returns number of items in Queue
        size_t count()
        {
            ScopedLock<Mutex> lock(muxQueue);
            return deqQueue.size();
        }

        // Clears Queue
        void clear()
        {
            ScopedLock<Mutex> lock(muxQueue);
            deqQueue.clear();
        }

        void wait()
        {
            while (empty())
            {
                UniqueLock<Mutex> ul(muxBlocking);
                cvBlocking.wait(ul);
            }
        }

    protected:
        Mutex muxQueue;
        std::deque<T> deqQueue;
        std::condition_variable cvBlocking;
        Mutex muxBlocking;
    };

    using PacketQueue = tsqueue<NetworkPacket>;
    using OwnedPacketQueue = tsqueue<OwnedNetworkPacket>;


    static constexpr char LocalHostAddress[] = "127.0.0.1";
    static constexpr U16 NetworkingPort = 3443u;

    [[nodiscard]] inline bool IsLocalHostAddress(const std::string_view address) noexcept
    {
        return address == "localhost" ||
               address == LocalHostAddress;
    }

} //namespace Networking
} //namespace Divide
#endif //DVD_NETWORKING_COMMON_H_
