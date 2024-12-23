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
#ifndef DVD_CLIENT_H_
#define DVD_CLIENT_H_

#include "WorldPacket.h"

#include <boost/asio/strand.hpp>

namespace Divide
{
    struct OPCodes;

    class NetworkClientInterface;

    class NetworkClientImpl
    {
      public:
        NetworkClientImpl( NetworkClientInterface* parent, boost::asio::io_context& service );

        /// Called by the user of the client class to initiate the connection process.
        /// The endpoint iterator will have been obtained using a tcp::resolver.
        void start( boost::asio::ip::tcp::resolver::iterator endpoint_iter );

        // This function terminates all the actors to shut down the connection. It
        // may be called by the user of the client class, or by the class itself in
        // response to graceful termination or an unrecoverable error.
        void stop();

        // Packet I/O
        bool sendPacket( const WorldPacket& p );
        
        PROPERTY_R(tcp_socket, socket);

      private:
        // Connection
        void start_connect( boost::asio::ip::tcp::resolver::iterator endpoint_iter );
        void handle_connect( const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpoint_iter );

        // Read
        void start_read();
        void handle_read_body( const boost::system::error_code& ec, size_t bytes_transferred, WorldPacket::Header header );
        void handle_read_packet( const boost::system::error_code& ec, size_t bytes_transferred, WorldPacket::Header header );

        // File Input
        void receiveFile();
 
        // Write
        void start_write();
        void handle_write( const boost::system::error_code& ec, size_t bytes_transferred );

        // Timers
        void check_deadline();

      private:

        NetworkClientInterface* _parent{nullptr};
        boost::asio::streambuf _inputBuffer;
        deadline_timer _deadline;
        deadline_timer _heartbeatTimer;
        eastl::deque<WorldPacket> _packetQueue;
        std::unique_ptr<boost::asio::io_context::strand> _strand;
        bool _stopped = false;
    };

};  // namespace Divide

#endif //DVD_CLIENT_H_

