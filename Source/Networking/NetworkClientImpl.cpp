

#ifndef OPCODE_ENUM
#define OPCODE_ENUM OPcodes
#endif

#include "Headers/Client.h"
#include "Headers/NetworkClientImpl.h"
#include "Headers/OPCodesTpl.h"
#include "Utility/Headers/Localization.h"

#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>

namespace Divide
{
    NetworkClientImpl::NetworkClientImpl( NetworkClientInterface* parent, boost::asio::io_context& io_context )
        : _socket( io_context.get_executor() )
        , _parent( parent )
        , _deadline( io_context.get_executor() )
        , _heartbeatTimer( io_context.get_executor() )
        , _strand( std::make_unique<boost::asio::io_context::strand>( io_context ) )
    {
    }

    bool NetworkClientImpl::sendPacket( const WorldPacket& p )
    {
        _packetQueue.push_back(p);
        _heartbeatTimer.expires_at( boost::posix_time::neg_infin );

        return true;
    }

    void NetworkClientImpl::start( const boost::asio::ip::tcp::resolver::iterator endpoint_iter )
    {
        start_connect( MOV( endpoint_iter ) );
        _deadline.async_wait
        (
            _strand->wrap
            (
                [&]( [[maybe_unused]] const boost::system::error_code ec)
                {
                    check_deadline();
                }
            )
        );
    }

    void NetworkClientImpl::stop()
    {
        _stopped = true;
        _socket.close();
        _deadline.cancel();
        _heartbeatTimer.cancel();
        _packetQueue.clear();
    }

    void NetworkClientImpl::start_read()
    {
        _deadline.expires_from_now( boost::posix_time::seconds( 30 ) );
        _inputBuffer.consume( _inputBuffer.size() );

        WorldPacket::Header header = {};

        async_read
        (
            _socket,
            boost::asio::buffer( &header, WorldPacket::HEADER_SIZE ),
            _strand->wrap
            (
                [&]( const boost::system::error_code ec, const std::size_t bytes_transferred )
                {
                    handle_read_body( ec, bytes_transferred, header );
                }
            )
        );
    }

    void NetworkClientImpl::handle_read_body( const boost::system::error_code& ec, [[maybe_unused]] size_t bytes_transferred, const WorldPacket::Header header )
    {
        if ( ec )
        {
            Console::errorfn( LOCALE_STR("ASIO_PACKET_ERROR"), ec.message() );
            stop();
        
        }
        if ( _stopped )
        {
            return;
        }

        _deadline.expires_from_now( boost::posix_time::seconds( 30 ) );
        
        async_read
        (
            _socket,
            _inputBuffer.prepare( header._byteLength ),
            _strand->wrap
            (
                [&]( const boost::system::error_code code, const std::size_t bytes_transferred )
                {
                    handle_read_packet( code, bytes_transferred, header );
                }
            )
        );
    }

    void NetworkClientImpl::handle_read_packet( const boost::system::error_code& ec, [[maybe_unused]] size_t bytes_transferred, const WorldPacket::Header header)
    {
        if ( ec )
        {
            Console::errorfn( LOCALE_STR("ASIO_PACKET_ERROR"), ec.message() );
            stop();
        
        }

        if ( _stopped )
        {
            return;
        }

        _inputBuffer.commit( header._byteLength );

        WorldPacket packet;
        if (packet.loadFromBuffer(_inputBuffer)) [[likely]]
        {
            _parent->handlePacket( packet );
        }
        else
        {
            Console::errorfn( LOCALE_STR( "ASIO_EXCEPTION" ), "WorldPacket::loadFromBuffer");
        }

        start_read();
    }

    void NetworkClientImpl::start_write()
    {
        if ( _stopped )
        {
            return;
        }
        
        if ( _packetQueue.empty() )
        {
            WorldPacket heart( OPCodes::MSG_HEARTBEAT );
            heart << I8_ZERO;
            sendPacket(heart);
        }

        boost::asio::streambuf buf;
        if ( !_packetQueue.front().saveToBuffer(buf))
        {
            Console::errorfn( LOCALE_STR( "ASIO_EXCEPTION" ), "WorldPacket::saveToBuffer" );
            _packetQueue.pop_front();
            start_write();
            return;
        }

        WorldPacket::Header header
        {
            ._byteLength = to_U32(buf.size()),
            ._opCode = _packetQueue.front().opcode()
        };

        async_write
        (
            _socket,
            std::array<boost::asio::const_buffer, 2>
            {{
                boost::asio::buffer( &header, WorldPacket::HEADER_SIZE ),
                buf.data()
            }},
            _strand->wrap
            (
                [&]( const boost::system::error_code ec, const size_t bytes_transferred)
                {
                    handle_write( ec, bytes_transferred );
                }
            )
        );
    }

    void NetworkClientImpl::handle_write( const boost::system::error_code& ec, [[maybe_unused]] const size_t bytes_transferred)
    {
        if ( _stopped )
        {
            return;
        }

        SCOPE_EXIT
        {
            _packetQueue.pop_front();
        };

        if ( ec )
        {
            Console::errorfn( LOCALE_STR("ASIO_PACKET_ERROR"), ec.message() );
            stop();
        }
        else
        {
            _heartbeatTimer.expires_from_now( boost::posix_time::seconds( 2 ) );
            _heartbeatTimer.async_wait
            (
                 _strand->wrap
                 (
                    [&]( [[maybe_unused]] const boost::system::error_code ec)
                    {
                        start_write();
                    }
                )
            );
        } 
    }

    void NetworkClientImpl::check_deadline()
    {
        if ( _stopped )
        {
            return;
        }
        // Check whether the deadline has passed. We compare the deadline against
        // the current time since a new asynchronous operation may have moved the
        // deadline before this actor had a chance to run.
        if ( _deadline.expires_at() <= boost::asio::deadline_timer::traits_type::now() )
        {
            // The deadline has passed. The socket is closed so that any outstanding
            // asynchronous operations are cancelled.
            _socket.close();

            // There is no longer an active deadline. The expiry is set to positive
            // infinity so that the actor takes no action until a new deadline is
            // set.
            _deadline.expires_at( boost::posix_time::pos_infin );
        }

        // Put the actor back to sleep.
        _deadline.async_wait
        (
            _strand->wrap
            (
                [&]( [[maybe_unused]] const boost::system::error_code ec )
                {
                    check_deadline();
                }
            )
        );
    }

    void NetworkClientImpl::start_connect( boost::asio::ip::tcp::resolver::iterator endpoint_iter )
    {
        if ( endpoint_iter != boost::asio::ip::tcp::resolver::iterator() )
        {
            {
                std::stringstream ss;
                ss << endpoint_iter->endpoint();
                Console::printfn( LOCALE_STR("ASIO_CONNECTING_TO_IP"), ss.str() );
            }

            // Set a deadline for the connect operation.
            _deadline.expires_from_now( boost::posix_time::seconds( 60 ) );

            // Start the asynchronous connect operation.
            _socket.async_connect
            (
                endpoint_iter->endpoint(),
                _strand->wrap
                (
                    [&, endpoint_iter]( const boost::system::error_code ec )
                    {
                        handle_connect( ec, endpoint_iter );
                    }
                )
            );
        }
        else
        {
            // There are no more endpoints to try. Shut down the client.
            stop();
        }
    }

    void NetworkClientImpl::handle_connect( const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpoint_iter )
    {
        if ( _stopped )
        {
            return;
        }
        // The async_connect() function automatically opens the socket at the start
        // of the asynchronous operation. If the socket is closed at this time then
        // the timeout handler must have run first.
        if ( !_socket.is_open() )
        {
            Console::errorfn( LOCALE_STR("ASIO_CONNECT_TIME_OUT") );
            
            // Try the next available endpoint.
            start_connect( ++endpoint_iter );
        }
        else if ( ec ) // Check if the connect operation failed before the deadline expired.
        {
            Console::errorfn( LOCALE_STR("ASIO_EXCEPTION"), ec.message() );

            // We need to close the socket used in the previous connection attempt before starting a new one.
            _socket.close();

            // Try the next available endpoint.
            start_connect( ++endpoint_iter );
        }
        else // Otherwise we have successfully established a connection.
        {
            {
                std::stringstream ss;
                ss << endpoint_iter->endpoint();
                Console::printfn( LOCALE_STR("ASIO_CONNECTED_TO_IP"), ss.str() );
            }

            // Start the input actor.
            start_read();

            // Start the heartbeat actor.
            start_write();
        }
    }

};  // namespace Divide
