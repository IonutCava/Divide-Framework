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
#ifndef DVD_SERVER_H_
#define DVD_SERVER_H_

#include "Networking/Headers/WorldPacket.h"

//----------------------------------------------------------------------
namespace Divide {

FWD_DECLARE_MANAGED_CLASS(tcp_session_tpl);
FWD_DECLARE_MANAGED_CLASS(channel);

class Server {
  public:
    Server();
    ~Server();

    void init(U16 port,
              const string& broadcast_endpoint_address,
              bool debugOutput);

    void close();

  private:
    void handle_accept(const tcp_session_tpl_ptr& session, const boost::system::error_code& ec);

  private:
    boost::asio::io_context _ioService;
    eastl::unique_ptr<std::thread> _thread;
    tcp_acceptor* _acceptor{nullptr};
    channel_ptr _channel;
    bool _debugOutput{false};

};

};  // namespace Divide

#endif //DVD_SERVER_H_
