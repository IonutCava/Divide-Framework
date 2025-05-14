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
#ifndef DVD_NETWORKING_CONNECTION_H_
#define DVD_NETWORKING_CONNECTION_H_	

#include "Common.h"

namespace Divide
{
namespace Networking
{
    class Connection : public std::enable_shared_from_this<Connection>
    {
        public:
            // A connection is "owned" by either a server or a client, and its
            // behaviour is slightly different bewteen the two.
            enum class Owner : U8
            {
                SERVER,
                CLIENT
            };

        public:
            // Constructor: Specify Owner, connect to context, transfer the socket. Provide reference to incoming message queue
            Connection(Owner parent, boost::asio::io_context& asioContext, boost::asio::ip::tcp::socket socket, OwnedPacketQueue& qIn);

            virtual ~Connection();

            // This ID is used system wide - its how clients will understand other clients exist across the whole system.

            PROPERTY_R(U32, id, 0u);

            void connectToClient(U32 uid = 0u);

            void connectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints);


            void disconnect();

            bool isConnected() const;

            // Prime the connection to wait for incoming messages
            void startListening();

            // ASYNC - Send a message, connections are one-to-one so no need to specifiy the target, for a client, the target is the server and vice versa
            void send(const NetworkPacket& p);

        private:
            // ASYNC - Prime context to write a message header
            void writeHeader();

            // ASYNC - Prime context to write a message body
            void writeBody();

            // ASYNC - Prime context ready to read a message header
            void readHeader();

            // ASYNC - Prime context ready to read a message body
            void readBody();

            // Once a full message is received, add it to the incoming queue
            void addToIncomingMessageQueue();

    protected:
        // Each connection has a unique socket to a remote 
        boost::asio::ip::tcp::socket _socket;

        // This context is shared with the whole asio instance
        boost::asio::io_context& _asioContext;

        // This queue holds all messages to be sent to the remote side of this connection
        PacketQueue _messagesOut;

        // This references the incoming queue of the parent object
        OwnedPacketQueue& _messagesIn;

        // Incoming messages are constructed asynchronously, so we will
        // store the part assembled message here, until it is ready
        NetworkPacket _msgTemporaryIn;

        // The "owner" decides how some of the connection behaves
        Owner _ownerType{ Owner::SERVER };

    };

    FWD_DECLARE_MANAGED_CLASS(Connection);
} //namespace Networking
} //namespace Divide

#endif //DVD_NETWORKING_CONNECTION_H_