#include "stdafx.h"

#include "Headers/Server.h"
#include "Headers/Session.h"

#include "Networking/Headers/ASIO.h"

using namespace boost::asio;

namespace Divide {

Server::Server()
    : acceptor_(nullptr),
      _channel(std::make_shared<channel>())
{
}

Server::~Server()
{
    close();
    delete acceptor_;
}

void Server::close() {
    if (!thread_) {
        return; // stopped
    }

    acceptor_->close();
    io_service_.stop();
    thread_->join();
    thread_.reset();
}

void Server::init(const U16 port, const string& broadcast_endpoint_address, const bool debugOutput) {
    if (thread_) {
        return;
    }

    _debugOutput = debugOutput;
    try {
        const tcp::endpoint listen_endpoint(tcp::v4(), port);
        const udp::endpoint broadcast_endpoint(
            ip::address::from_string(broadcast_endpoint_address.c_str()),
            port);
        acceptor_ = new tcp::acceptor(io_service_, listen_endpoint);
        const subscriber_ptr bc(new udp_broadcaster(io_service_, broadcast_endpoint));
        _channel->join(bc);
        tcp_session_ptr new_session(new Session(io_service_, *_channel));

        acceptor_->async_accept(
            new_session->getSocket(),
            [&](const boost::system::error_code code) {
                handle_accept(new_session, code);
            }
        );
        eastl::unique_ptr<io_service::work> work(
            new io_service::work(io_service_));
        
        thread_.reset(new std::thread([this]() {
            io_service_.run();
        }));

    } catch (std::exception& e) {
        ASIO::LOG_PRINT((string("SERVER: ") + e.what()).c_str(), true);
    }
}

void Server::handle_accept(const tcp_session_ptr& session, const boost::system::error_code& ec) {
    if (!ec) {
        if (_debugOutput) {
            ASIO::LOG_PRINT("New TCP session accepted");
        }
        session->start();

        tcp_session_ptr new_session(new Session(io_service_, *_channel));

        acceptor_->async_accept(
            new_session->getSocket(),
            [&](const boost::system::error_code code) {
            handle_accept(new_session, code);
        });
    } else {
        std::stringstream ss;
        ss << "Server::handle_accept ERROR: " << ec;
        ASIO::LOG_PRINT(ss.str().c_str());
    }
}

};  // namespace Divide
