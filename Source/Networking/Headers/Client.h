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
#ifndef DVD_NETWORKING_CLIENT_H_
#define DVD_NETWORKING_CLIENT_H_	

#include "Common.h"

namespace Divide
{
namespace Networking
{
    class Client
    {
    public:
        Client();
        ~Client();

    public:
        [[nodiscard]] bool connect(std::string_view host, const U16 port);
                      void disconnect();
        [[nodiscard]] bool isConnected() const;

        /// Send a packet to the server
        void send(const NetworkPacket& p);

        // Should poll the message queue and process any received packets
        void update();

        // This is the thread safe queue of incoming messages from server
        PROPERTY_R(OwnedPacketQueue, messagesIn);

        void requestFile(const ResourcePath& path, const string& name);

    protected:
        void receiveMessage(NetworkPacket& msg);
        void sendMessage(const NetworkPacket& msg);

        void heartbeatWait();
        void heartbeatSend();

    protected:
        static constexpr U8 HeartbeastPerPingRequest = 4u;

        boost::asio::io_context _context;
        std::thread _contextThread;
        U8 _heartbeatCounter = 0u;
        boost::asio::deadline_timer _heartbeatTimer;

        // The client has a single instance of a "connection" object, which handles data transfer
        Connection_uptr _connection;
    };

} //namespace Networking
} //namespace Divide

#endif //DVD_NETWORKING_CLIENT_H_