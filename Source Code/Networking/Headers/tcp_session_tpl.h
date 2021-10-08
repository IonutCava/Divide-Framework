#pragma once
#ifndef _SESSION_TPL_H_
#define _SESSION_TPL_H_

#include "Strand.h"
#include "WorldPacket.h"

#include <boost/asio/ip/udp.hpp>

namespace Divide {

//----------------------------------------------------------------------

//This is game specific but core functionality
extern void UpdateEntities(WorldPacket& p);

class subscriber {
   public:
    virtual ~subscriber() = default;
    virtual void sendPacket(const WorldPacket& p) = 0;
};

using subscriber_ptr = std::shared_ptr<subscriber>;

//----------------------------------------------------------------------

class channel {
   public:
    void join(const subscriber_ptr& sub) { subscribers_.insert(sub); }

    void leave(const subscriber_ptr& sub) { subscribers_.erase(sub); }

    void sendPacket(const WorldPacket& p) {
        std::for_each(std::begin(subscribers_), std::end(subscribers_),
                      [&p](auto& subscriber) {
                        subscriber->sendPacket(p);
                      });
    }

   private:
    std::set<subscriber_ptr> subscribers_;
};

/// This is a single session handled by the server. It is mapped to a single
/// client
class tcp_session_tpl : public subscriber,
                        public std::enable_shared_from_this<tcp_session_tpl> {
   public:
    tcp_session_tpl(boost::asio::io_service& io_service, channel& ch);

    boost::asio::ip::tcp::socket& getSocket() noexcept { return _socket; }

    // Called by the server object to initiate the four actors.
    virtual void start();

    // Push a new packet in the output queue
    void sendPacket(const WorldPacket& p) override;

    // Push a new file in the output queue
    virtual void sendFile(const string& fileName);

   private:
    virtual void stop();
    virtual bool stopped() const;

    // Read Packet;
    virtual void start_read();
    virtual void handle_read_body(const boost::system::error_code& ec,
                                  size_t bytes_transfered);
    virtual void handle_read_packet(const boost::system::error_code& ec,
                                    size_t bytes_transfered);

    // Write Packet
    virtual void start_write();
    virtual void handle_write(const boost::system::error_code& ec);

    // Write File
    virtual void handle_write_file(const boost::system::error_code& ec);

    // Update Timers
    virtual void await_output();
    virtual void check_deadline(boost::asio::deadline_timer* deadline);

   protected:
    // Define this functions to implement various packet handling (a switch
    // statement for example)
    // switch(p.getOpcode()) { case SMSG_XXXXX: bla bla bla break; case
    // MSG_HEARTBEAT: break;}
    virtual void handlePacket(WorldPacket& p);

    virtual void HandleHeartBeatOpCode(WorldPacket& p);
    virtual void HandleDisconnectOpCode(WorldPacket& p);
    virtual void HandlePingOpCode(WorldPacket& p);
    virtual void HandleEntityUpdateOpCode(WorldPacket& p);

   private:
    size_t _header;
    channel& _channel;
    boost::asio::ip::tcp::socket _socket;
    boost::asio::streambuf _inputBuffer;
    boost::asio::deadline_timer _inputDeadline;
    eastl::deque<WorldPacket> _outputQueue;
    eastl::deque<string> _outputFileQueue;
    boost::asio::deadline_timer _nonEmptyOutputQueue;
    boost::asio::deadline_timer _outputDeadline;
    time_t _startTime;

    eastl::unique_ptr<Strand> _strand;
};

using tcp_session_ptr = std::shared_ptr<tcp_session_tpl>;

//----------------------------------------------------------------------

class udp_broadcaster final : public subscriber {
   public:
    udp_broadcaster(boost::asio::io_service& io_service,
                    const boost::asio::ip::udp::endpoint& broadcast_endpoint);

    boost::asio::ip::udp::socket& getSocket() noexcept { return socket_; }
    void sendPacket(const WorldPacket& p) override;

   private:
    boost::asio::ip::udp::socket socket_;
};

};  // namespace Divide
#endif
