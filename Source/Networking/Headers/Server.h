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
#ifndef DVD_NETWORKING_SERVER_H_
#define DVD_NETWORKING_SERVER_H_	

#include "Connection.h"

namespace Divide
{
namespace Networking
{
    class Server
    {
    public:
        // Create a server, ready to listen on specified port
        Server(U16 port);
        virtual ~Server();

        [[nodiscard]] bool start();
        void stop();

        // ASYNC - Instruct asio to wait for connection
        void waitForClientConnection();

        // Send a message to a specific client
        void messageClient(Connection_ptr client, const NetworkPacket& msg);
        // Send message to all clients
        void messageAllClients(const NetworkPacket& msg, Connection_ptr ignoreClient = nullptr);

        // Force server to respond to incoming messages
        void update(size_t nMaxMessages = -1, bool bWait = false);
        

    protected:
        // This server class should override thse functions to implement
        // customised functionality

        // Called when a client connects, you can veto the connection by returning false
        virtual bool onClientConnect(Connection_ptr client);

        // Called when a client appears to have disconnected
        virtual void onClientDisconnect(Connection_ptr client);

        // Called when a message arrives
        virtual void receiveMessage(Connection_ptr client, NetworkPacket& msg);


    protected:
        // Thread Safe Queue for incoming message packets
        OwnedPacketQueue _messagesIn;

        // Container of active validated connections
        std::deque<Connection_ptr> _deqConnections;

        // Order of declaration is important - it is also the order of initialisation
        boost::asio::io_context _asioContext;
        std::thread _threadContext;

        // These things need an asio context
        boost::asio::ip::tcp::acceptor _asioAcceptor; // Handles new incoming connection attempts...

        // Clients will be identified in the "wider system" via an ID
        U32 _IDCounter = 123000;
    };

} //namespace Networking
} //namespace Divide

#endif //DVD_NETWORKING_SERVER_H_