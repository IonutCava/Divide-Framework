// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Networking/Headers/Client.h"
#include "Networking/Headers/Connection.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <chrono>

namespace Divide::Networking
{
    namespace
    {
        class HeartbeatClient final : public Client
        {
        public:
            HeartbeatClient()
                : _peer(_context)
            {
                platformInitRunListener::PlatformInit();
                using boost::asio::ip::tcp;
                tcp::acceptor acceptor(_context, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
                tcp::socket socket(_context);
                socket.connect(acceptor.local_endpoint());
                acceptor.accept(_peer);
                _connection = std::make_unique<Connection>(Connection::Owner::CLIENT, _context, std::move(socket), _messagesIn);
            }

            ~HeartbeatClient() { disconnect(); }

            void resetHeartbeat()
            {
                boost::asio::post(_context, [this]() { heartbeatWait(); });
            }

            void sendHeartbeat()
            {
                boost::asio::post(_context, [this]() { heartbeatSend(); });
            }

            void runFor(const std::chrono::milliseconds duration)
            {
                _context.restart();
                _context.run_for(duration);
            }

            U8 heartbeatCount() const { return _heartbeatCounter; }
            U64 generation() const { return _heartbeatGeneration; }

            vector<OPCodes> receivedOpcodes()
            {
                _peer.non_blocking(true);
                vector<OPCodes> codes;
                while (_peer.available() >= NetworkPacket::HEADER_SIZE)
                {
                    NetworkPacket::Header header;
                    boost::asio::read(_peer, boost::asio::buffer(&header, sizeof(header)));
                    vector<Byte> body(header._byteLength);
                    boost::asio::read(_peer, boost::asio::buffer(body.data(), body.size()));
                    codes.push_back(header._opCode);
                }
                return codes;
            }

            void start() { _contextThread = std::thread([this]() { _context.run(); }); }

        private:
            boost::asio::ip::tcp::socket _peer;
        };
    }

    TEST_CASE("Heartbeat reset cancels the previous wait", "[networking_tests]")
    {
        HeartbeatClient client;
        client.resetHeartbeat();
        client.runFor(std::chrono::milliseconds(20));
        client.resetHeartbeat();
        client.runFor(std::chrono::milliseconds(20));
        CHECK(client.generation() == 2u);
        CHECK(client.heartbeatCount() == 0u);
        client.runFor(std::chrono::milliseconds(2100));
        CHECK(client.heartbeatCount() == 1u);
    }

    TEST_CASE("Disconnect does not rearm a cancelled heartbeat", "[networking_tests]")
    {
        HeartbeatClient client;
        client.resetHeartbeat();
        client.runFor(std::chrono::milliseconds(20));
        client.start();
        client.disconnect();
        CHECK(client.heartbeatCount() == 0u);
        CHECK(client.generation() == 1u);
    }

    TEST_CASE("Heartbeat startup belongs to each client", "[networking_tests]")
    {
        HeartbeatClient first;
        HeartbeatClient second;
        first.update();
        second.update();
        first.runFor(std::chrono::milliseconds(20));
        second.runFor(std::chrono::milliseconds(20));
        CHECK(first.heartbeatCount() == 1u);
        CHECK(second.heartbeatCount() == 1u);
    }

    TEST_CASE("Periodic ping resets the heartbeat counter", "[networking_tests]")
    {
        HeartbeatClient client;
        for (int heartbeat = 0; heartbeat < 4; ++heartbeat)
        {
            client.sendHeartbeat();
        }
        client.runFor(std::chrono::milliseconds(20));
        CHECK(client.heartbeatCount() == 0u);
        CHECK(client.generation() == 4u);
        const auto codes = client.receivedOpcodes();
        REQUIRE(codes.size() == 4u);
        CHECK(codes.back() == OPCodes::CMSG_PING);
    }
}
