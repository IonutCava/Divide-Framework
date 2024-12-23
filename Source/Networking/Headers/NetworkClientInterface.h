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
#ifndef DVD_NETWORK_CLIENT_INTERFACE_H_
#define DVD_NETWORK_CLIENT_INTERFACE_H_

#include "WorldPacket.h"
#include "NetworkClientImpl.h"

namespace Divide
{

#ifndef OPCODE_ENUM
#error \
    "Please include 'OPCodesTpl' and define custom OPcodes before using the networking library!"
#endif

    FWD_DECLARE_MANAGED_CLASS( NetworkClientImpl );
    struct OPCodes;

    class NetworkClientInterface
    {
      public:
        virtual ~NetworkClientInterface();

        /// Send a packet to the target server
        virtual bool sendPacket( WorldPacket& p ) const;
        /// Init a connection to the target address:port
        virtual bool init( const string& address, U16 port );
        /// Connect to target address:port only if we have a new IP:PORT combo or our connection timed out
        virtual bool connect( const string& address, U16 port );
        /// Disconnect from the server
        virtual void disconnect();
        /// Check connection state;
        virtual bool isConnected() const noexcept;

      protected:
        NetworkClientInterface() noexcept = default;

        friend class NetworkClientImpl;
        void close();

        // Define this functions to implement various packet handling (a switch
        // statement for example)
        // switch(p.getOpcode()) { case SMSG_XXXXX: bla bla bla break; case
        // MSG_HEARTBEAT: break;}
        virtual void handlePacket( WorldPacket& p ) = 0;

      protected:
        boost::asio::io_context _ioService;
        std::unique_ptr<boost::asio::io_context::work> _work;
        std::unique_ptr<std::thread> _thread;
        NetworkClientImpl_uptr _localClient;
        bool _connected{false};
        string _address, _port;
    };

};  // namespace Divide

#endif //DVD_NETWORK_CLIENT_INTERFACE_H_
