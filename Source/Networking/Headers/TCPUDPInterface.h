#pragma once
#ifndef DVD_SESSION_TPL_H_
#define DVD_SESSION_TPL_H_

#include "WorldPacket.h"

#include <boost/asio/strand.hpp>

namespace Divide
{
    //----------------------------------------------------------------------

    //This is game specific but core functionality
    extern void UpdateEntities( WorldPacket& p );

    class subscriber
    {
        public:
        virtual ~subscriber() = default;
        virtual void sendPacket( const WorldPacket& p ) = 0;
    };

    using subscriber_ptr = std::shared_ptr<subscriber>;

    //----------------------------------------------------------------------

    class channel
    {
        public:
        void join( const subscriber_ptr& sub )
        {
            subscribers_.insert( sub );
        }

        void leave( const subscriber_ptr& sub )
        {
            subscribers_.erase( sub );
        }

        void sendPacket( const WorldPacket& p )
        {
            eastl::for_each( subscribers_.begin(), subscribers_.end(),
                            [&p]( auto& subscriber )
                            {
                                subscriber->sendPacket( p );
                            } );
        }

        private:
        eastl::set<subscriber_ptr> subscribers_;
    };

    /// This is a single session handled by the server. It is mapped to a single client
    class TCPUDPInterface : public subscriber, public std::enable_shared_from_this<TCPUDPInterface>
    {
       public:
        TCPUDPInterface( boost::asio::io_context& io_context, channel& ch );

        inline tcp_socket& getSocket() noexcept { return _socket; }

        // Called by the server object to initiate the four actors.
        virtual void start();

        // Push a new packet in the output queue
        void sendPacket( const WorldPacket& p ) override;

       private:
        virtual void stop();
        virtual bool stopped() const;

        // Read Packet;
        virtual void start_read();
        virtual void handle_read_body( const boost::system::error_code& ec, size_t bytes_transferred, WorldPacket::Header header );
        virtual void handle_read_packet( const boost::system::error_code& ec, size_t bytes_transferred, WorldPacket::Header header );

        // Write Packet
        virtual void start_write();
        virtual void handle_write( const boost::system::error_code& ec, size_t bytes_transferred );

        // Update Timers
        virtual void await_output();
        virtual void check_deadline( deadline_timer* deadline );

      protected:
        // Define this functions to implement various packet handling (a switch
        // statement for example)
        // switch(p.getOpcode()) { case SMSG_XXXXX: bla bla bla break; case
        // MSG_HEARTBEAT: break;}
        virtual void handlePacket( WorldPacket& p );

        virtual void HandleHeartBeatOpCode( WorldPacket& p );
        virtual void HandleDisconnectOpCode( WorldPacket& p );
        virtual void HandlePingOpCode( WorldPacket& p );
        virtual void HandleEntityUpdateOpCode( WorldPacket& p );
        virtual void HandleRequestFile( WorldPacket& p );
        
      private:
        channel& _channel;
        tcp_socket _socket;
        boost::asio::streambuf _inputBuffer;
        eastl::deque<WorldPacket> _outputQueue;
        deadline_timer _inputDeadline;
        deadline_timer _nonEmptyOutputQueue;
        deadline_timer _outputDeadline;
        time_t _startTime;

        std::unique_ptr<boost::asio::io_context::strand> _strand;
    };

    FWD_DECLARE_MANAGED_CLASS(TCPUDPInterface);

    //----------------------------------------------------------------------

    class UDPBroadcaster final : public subscriber
    {
        public:
        UDPBroadcaster( boost::asio::io_context& io_context,
                         const boost::asio::ip::udp::endpoint& broadcast_endpoint );

        [[nodiscard]] inline udp_socket& getSocket() noexcept
        {
            return socket_;
        }
        void sendPacket( const WorldPacket& p ) override;

        private:
        udp_socket socket_;
    };

};  // namespace Divide

#endif //DVD_SESSION_TPL_H_
