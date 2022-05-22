#include "stdafx.h"

#ifndef OPCODE_ENUM
#define OPCODE_ENUM OPcodes
#endif

#include "Headers/OPCodesTpl.h"
#include "Headers/ASIO.h"
#include "Headers/Client.h"

#include "Core/Headers/StringHelper.h"

#include <boost/asio/ip/tcp.hpp>

namespace Divide {

ASIO::LOG_CBK ASIO::_logCBK;

ASIO::ASIO() noexcept 
    : _localClient(nullptr),
      _thread(nullptr),
      _connected(false),
      _debugOutput(true)
{
}

ASIO::~ASIO()
{
    io_service_.stop();
    _work.reset();
    if (_thread) {
        _thread->join();
    }
    if (_localClient) {
        _localClient->stop();
        delete _localClient;
    }
}

void ASIO::disconnect() {
    if (!_connected) {
        return;
    }
    WorldPacket p(OPCodes::CMSG_REQUEST_DISCONNECT);
    p << _localClient->getSocket().local_endpoint().address().to_string();
    sendPacket(p);
}

bool ASIO::init(const string& address, const U16 port) {
    try {
        boost::asio::ip::tcp::resolver res(io_service_);
        _localClient = new Client(this, io_service_, _debugOutput);
        _work.reset(new boost::asio::io_service::work(io_service_));
        _localClient->start(
            res.resolve(boost::asio::ip::tcp::resolver::query(address, Util::to_string(port))));
        _thread = new std::thread([&] { io_service_.run(); });
        SetThreadName(_thread, "ASIO_THREAD");

        io_service_.poll();
        _connected = true;
    } catch (const std::exception& e) {
        if (_debugOutput) {
            LOG_PRINT((string("[ASIO] Exception: ") + e.what()).c_str(), true);
        }
        _connected = false;
    }

    return _connected;
}

bool ASIO::connect(const string& address, const U16 port) {
    if (_connected) {
        close();
    }

    return init(address, port);
}

bool ASIO::isConnected() const noexcept {
    return _connected;
}

void ASIO::close() {
    _localClient->stop();
    _connected = false;
}

bool ASIO::sendPacket(WorldPacket& p) const {
    if (!_connected) {
        return false;
    }
    if (_localClient->sendPacket(p)) {

        if (_debugOutput) {
            LOG_PRINT(("[ASIO]: sent opcode [ 0x" + Util::to_string(p.opcode()) + "]").c_str());
        }
        return true;
    }

    return false;
}

void ASIO::toggleDebugOutput(const bool debugOutput) noexcept {
    _debugOutput = debugOutput;
    _localClient->toggleDebugOutput(_debugOutput);
}

void ASIO::SET_LOG_FUNCTION(const LOG_CBK& cbk) {
    _logCBK = cbk;
}

void ASIO::LOG_PRINT(const char* msg, const bool error) {
    if (_logCBK) {
        _logCBK(msg, error);
    }  else {
        (error ? std::cerr : std::cout) << msg << std::endl;
    }
}

};  // namespace Divide