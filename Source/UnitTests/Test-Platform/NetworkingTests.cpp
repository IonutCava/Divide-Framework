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

            vector<OPCodes> receivedOpcodes(const size_t packetCount)
            {
                vector<OPCodes> codes;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                while (codes.size() < packetCount)
                {
                    NetworkPacket::Header header;
                    readPeer(boost::asio::buffer(&header, sizeof(header)), deadline);
                    vector<Byte> body(header._byteLength);
                    readPeer(boost::asio::buffer(body.data(), body.size()), deadline);
                    codes.push_back(header._opCode);
                }
                return codes;
            }

            void start() { _contextThread = std::thread([this]() { _context.run(); }); }

        private:
            void readPeer(const boost::asio::mutable_buffer buffer, const std::chrono::steady_clock::time_point deadline)
            {
                bool completed = false;
                boost::system::error_code error;
                boost::asio::async_read(_peer, buffer, [&](const boost::system::error_code ec, const size_t)
                {
                    error = ec;
                    completed = true;
                });
                _context.restart();
                while (!completed && _context.run_one_until(deadline) != 0u)
                {
                }
                const bool timedOut = !completed;
                if (timedOut)
                {
                    _peer.cancel();
                    _context.restart();
                    while (!completed)
                    {
                        _context.run_one();
                    }
                }
                REQUIRE_FALSE(timedOut);
                REQUIRE_FALSE(error);
            }

            boost::asio::ip::tcp::socket _peer;
        };
    }

    TEST_CASE("Heartbeat reset cancels the previous wait", "[networking_tests]")
    {
        platformInitRunListener::PlatformInit();
        platformInitRunListener::LocalizationScope localization;
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
        platformInitRunListener::PlatformInit();
        platformInitRunListener::LocalizationScope localization;
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
        platformInitRunListener::PlatformInit();
        platformInitRunListener::LocalizationScope localization;
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
        platformInitRunListener::PlatformInit();
        platformInitRunListener::LocalizationScope localization;
        HeartbeatClient client;
        for (int heartbeat = 0; heartbeat < 4; ++heartbeat)
        {
            client.sendHeartbeat();
        }
        SECTION("Receive after advancing the context")
        {
            client.runFor(std::chrono::milliseconds(20));
        }
        SECTION("Receive while sends are still queued")
        {
        }
        const auto codes = client.receivedOpcodes(4u);
        CHECK(client.heartbeatCount() == 0u);
        CHECK(client.generation() == 4u);
        REQUIRE(codes.size() == 4u);
        CHECK(codes[0] == OPCodes::CMSG_HEARTBEAT);
        CHECK(codes[1] == OPCodes::CMSG_HEARTBEAT);
        CHECK(codes[2] == OPCodes::CMSG_HEARTBEAT);
        CHECK(codes.back() == OPCodes::CMSG_PING);
    }
}
