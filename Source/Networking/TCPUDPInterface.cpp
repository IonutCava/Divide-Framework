

#include "Headers/TCPUDPInterface.h"
#include "Headers/OPCodesTpl.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

///////////////////////////////////////////////////////////////////////////////////////
//                                     TCP                                           //
///////////////////////////////////////////////////////////////////////////////////////

namespace Divide
{

    TCPUDPInterface::TCPUDPInterface( boost::asio::io_context& io_context, channel& ch )
        : _channel( ch )
        , _socket( io_context )
        , _inputDeadline( io_context.get_executor() )
        , _nonEmptyOutputQueue( io_context.get_executor() )
        , _outputDeadline( io_context.get_executor() )
        , _startTime( time( nullptr ) )
        , _strand( std::make_unique<boost::asio::io_context::strand>( io_context ) )
    {
        _inputDeadline.expires_at( boost::posix_time::pos_infin );
        _outputDeadline.expires_at( boost::posix_time::pos_infin );
        _nonEmptyOutputQueue.expires_at( boost::posix_time::pos_infin );
    }

    void TCPUDPInterface::start()
    {
        _channel.join( shared_from_this() );

        start_read();

        _inputDeadline.async_wait( _strand->wrap( [&]( const auto )
                                                  {
                                                      check_deadline( &_inputDeadline );
                                                  } ) );

        await_output();

        _outputDeadline.async_wait( _strand->wrap( [&]( const auto )
                                                   {
                                                       check_deadline( &_outputDeadline );
                                                   } ) );
    }

    void TCPUDPInterface::stop()
    {
        _channel.leave( shared_from_this() );

        _socket.close();
        _inputDeadline.cancel();
        _nonEmptyOutputQueue.cancel();
        _outputDeadline.cancel();
        _outputQueue.clear();
    }

    bool TCPUDPInterface::stopped() const
    {
        return !_socket.is_open();
    }

    void TCPUDPInterface::sendPacket( const WorldPacket& p )
    {
        _outputQueue.push_back( p );
        _nonEmptyOutputQueue.expires_at( boost::posix_time::neg_infin );
    }

    void TCPUDPInterface::start_read()
    {
        WorldPacket::Header header{};

        _inputBuffer.consume( _inputBuffer.size() );
        _inputDeadline.expires_from_now( boost::posix_time::seconds( 30 ) );
        async_read
        (
            _socket,
            boost::asio::buffer( &header, WorldPacket::HEADER_SIZE),
            _strand->wrap
            (
                [&]( const boost::system::error_code ec, const std::size_t bytes_transferred )
                {
                    handle_read_body( ec, bytes_transferred, header );
                }
            )
        );
    }

    void TCPUDPInterface::handle_read_body( const boost::system::error_code& ec, [[maybe_unused]] size_t bytes_transferred, const WorldPacket::Header header )
    {
        if ( stopped() )
        {
            return;
        }

        if ( !ec )
        {
            _inputDeadline.expires_from_now( boost::posix_time::seconds( 30 ) );

            async_read
            (
                _socket, _inputBuffer.prepare( header._byteLength ),
                _strand->wrap
                (
                    [&]( const boost::system::error_code code, const std::size_t bytes_transferred )
                    {
                        handle_read_packet( code, bytes_transferred, header );
                    }
                )
            );
        }
        else
        {
            stop();
        }
    }

    void TCPUDPInterface::handle_read_packet( const boost::system::error_code& ec, [[maybe_unused]] const size_t bytes_transferred, const WorldPacket::Header header )
    {
        if ( ec )
        {
            stop();
        }

        if ( stopped() )
        {
            return;
        }


        _inputBuffer.commit( header._byteLength );

        Console::printfn(LOCALE_STR("ASIO_BUFFER_SIZE"), header._byteLength);

        WorldPacket packet;

        if (!packet.loadFromBuffer(_inputBuffer))
        {
            Console::errorfn( LOCALE_STR( "ASIO_EXCEPTION" ), "WorldPacket::loadFromBuffer" );
        }

        handlePacket( packet );
        start_read();        
    }

    void TCPUDPInterface::start_write()
    {
        if ( _outputQueue.empty() ) 
        {
            await_output();
        }

        // Set a deadline for the write operation.
        _outputDeadline.expires_from_now( boost::posix_time::seconds( 30 ) );


        boost::asio::streambuf buf;
        if (!_outputQueue.front().saveToBuffer(buf))
        {
            Console::errorfn( LOCALE_STR( "ASIO_EXCEPTION" ), "WorldPacket::saveToBuffer" );
            return;
        }

        WorldPacket::Header header
        {
            ._byteLength = to_U32(buf.size()),
            ._opCode = _outputQueue.front().opcode()
        };

        // Start an asynchronous operation to send a message.
        async_write
        (
            _socket,
            std::array<boost::asio::const_buffer, 2>
            {
                boost::asio::buffer( &header, WorldPacket::HEADER_SIZE ),
                buf.data()
            },
            _strand->wrap
            (
                [&]( const boost::system::error_code code, const size_t bytes_transferred)
                {
                    handle_write( code, bytes_transferred );
                }
            )
        );
    }

    void TCPUDPInterface::handle_write( const boost::system::error_code& ec, [[maybe_unused]] const size_t bytes_transferred )
    {
        if ( ec )
        {
            stop();
        }

        if ( stopped() )
        {
            return;
        }

        _outputQueue.pop_front();
        await_output();
    }

    void TCPUDPInterface::await_output()
    {
        if ( stopped() )
        {
            return;
        }

        if ( _outputQueue.empty() )
        {
            _nonEmptyOutputQueue.expires_at( boost::posix_time::pos_infin );
            _nonEmptyOutputQueue.async_wait
            (
                _strand->wrap
                (
                    [&]( [[maybe_unused]] const boost::system::error_code ec)
                    {
                        await_output();
                    }
                )
            );
        }
        else
        {
            start_write();
        }
    }

    void TCPUDPInterface::check_deadline( deadline_timer* deadline )
    {
        if ( stopped() )
        {
            return;
        }

        // Check whether the deadline has passed. We compare the deadline against
        // the current time since a new asynchronous operation may have moved the
        // deadline before this actor had a chance to run.
        if ( deadline->expires_at() <= boost::asio::deadline_timer::traits_type::now() )
        {
            // The deadline has passed. Stop the session. The other actors will
            // terminate as soon as possible.
            stop();
        }
        else
        {
            // Put the actor back to sleep.
            deadline->async_wait
            (
                _strand->wrap
                (
                    [&]( [[maybe_unused]] const boost::system::error_code ec )
                    {
                        check_deadline( deadline );
                    }
                )
            );
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //                                     UDP                                           //
    ///////////////////////////////////////////////////////////////////////////////////////

    UDPBroadcaster::UDPBroadcaster( boost::asio::io_context& io_context,
                                    const boost::asio::ip::udp::endpoint& broadcast_endpoint )
        : socket_( io_context )
    {
        socket_.connect( broadcast_endpoint );
    }

    void UDPBroadcaster::sendPacket( const WorldPacket& p )
    {
        boost::asio::streambuf buf;
        if (!p.saveToBuffer(buf))
        {
			Console::errorfn( LOCALE_STR( "ASIO_EXCEPTION" ), "WorldPacket::saveToBuffer" );
        }

        WorldPacket::Header header
        {
            ._byteLength = to_U32(buf.size()),
            ._opCode = p.opcode()
        };

        boost::system::error_code ignored_ec;
        socket_.send
        (
            std::array<boost::asio::const_buffer, 2>
            {
                boost::asio::buffer( &header, WorldPacket::HEADER_SIZE ),
                buf.data()
            },
            0,
            ignored_ec
        );
    }

    void TCPUDPInterface::handlePacket( WorldPacket& p )
    {
        switch ( p.opcode() )
        {
            case OPCodes::MSG_HEARTBEAT:           HandleHeartBeatOpCode( p );               break;
            case OPCodes::CMSG_PING:               HandlePingOpCode( p );                    break;
            case OPCodes::CMSG_REQUEST_DISCONNECT: HandleDisconnectOpCode( p );              break;
            case OPCodes::CMSG_ENTITY_UPDATE:      HandleEntityUpdateOpCode( p );            break;
            case OPCodes::CMSG_REQUEST_FILE:       HandleRequestFile( p );                   break;

            default: Console::warnf(LOCALE_STR("ASIO_RECEIVE_UNKNOWN_OPCODE"), p.opcode() ); break;
        }
    }

    void TCPUDPInterface::HandleHeartBeatOpCode( WorldPacket& p )
    {
        Console::printfn( LOCALE_STR("ASIO_RECEIVED_HEARTBEAT") );

        I8 codeIn = I8_MAX, codeOut = 1u;
        p >> codeIn;

        DIVIDE_ASSERT( p.opcode() == OPCodes::MSG_HEARTBEAT && codeIn == 0u);

        Console::printfn( LOCALE_STR("ASIO_SEND_HEARBEAT"), codeIn, codeOut);
        
        WorldPacket r( OPCodes::MSG_HEARTBEAT );
        r << codeOut;

        sendPacket( r );
    }

    void TCPUDPInterface::HandlePingOpCode( WorldPacket& p )
    {
        Console::printfn( LOCALE_STR("ASIO_RECEIVED_PING") );

        F32 time = 0.f;
        p >> time;

        Console::printfn( LOCALE_STR("ASIO_SEND_PONG"), time );
        WorldPacket r( OPCodes::SMSG_PONG );
        r << time;

        sendPacket( r );
    }

    void TCPUDPInterface::HandleDisconnectOpCode( WorldPacket& p )
    {
        string client;
        p >> client;

        Console::printfn( LOCALE_STR("ASIO_RECEIVED_REQUEST_DISCONNECT"), client );

        WorldPacket r( OPCodes::SMSG_DISCONNECT );
        r << (U8)0;  // this will be the error code returned after safely saving

        // client
        sendPacket( r );
    }

    void TCPUDPInterface::HandleEntityUpdateOpCode( WorldPacket& p )
    {
        Console::printfn( LOCALE_STR( "ASIO_RECEIVED_ENTITY_UPDATE" ) );

        UpdateEntities( p );
    }

    void TCPUDPInterface::HandleRequestFile( WorldPacket& p )
    {
        string filePath, fileName;
        p >> filePath;
        p >> fileName;

        vector<Byte> byte_data;
        std::ifstream data;
        const FileError ret = readFile(ResourcePath{filePath}, fileName, FileType::BINARY, data);
        if ( ret != FileError::NONE )
        {
            Console::errorfn( LOCALE_STR("ASIO_FAIL_OPEN_FILE"), fileName );
            return;
        }

        data.seekg(0, std::ios::end);
        const size_t fileSize = to_size(data.tellg());
        data.seekg(0);
        byte_data.resize(fileSize);
        data.read(reinterpret_cast<char*>(byte_data.data()), fileSize);

        Console::printfn( LOCALE_STR("ASIO_SEND_FILE"), fileName, fileSize );

        WorldPacket r( OPCodes::SMSG_SEND_FILE );
        r << filePath;
        r << fileName;
        r << byte_data;

        sendPacket( r );
    }
};  // namespace Divide
