

#ifndef OPCODE_ENUM
#define OPCODE_ENUM OPcodes
#endif

#include "Headers/Client.h"
#include "Headers/ASIO.h"
#include "Headers/OPCodesTpl.h"
#include "Utility/Headers/Localization.h"

#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

namespace Divide
{
    Client::Client( ASIO* asioPointer, boost::asio::io_context& io_context, const bool debugOutput )
        : _debugOutput( debugOutput ),
        _socket( io_context.get_executor() ),
        _deadline( io_context.get_executor() ),
        _heartbeatTimer( io_context.get_executor() ),
        _asioPointer( asioPointer )
    {
    }

    bool Client::sendPacket( const WorldPacket& p )
    {
        _packetQueue.push_back( p );
        _heartbeatTimer.expires_at( boost::posix_time::neg_infin );

        return true;
    }

    void Client::receivePacket( WorldPacket& p ) const
    {
        _asioPointer->handlePacket( p );
    }

    void Client::start( const boost::asio::ip::tcp::resolver::iterator endpoint_iter )
    {
        start_connect( MOV( endpoint_iter ) );
        _deadline.async_wait( [&]( const boost::system::error_code )
                              {
                                  check_deadline();
                              } );
    }

    void Client::stop()
    {
        _stopped = true;
        _socket.close();
        _deadline.cancel();
        _heartbeatTimer.cancel();
    }

    void Client::start_read()
    {
        // Set a deadline for the read operation.
        _deadline.expires_from_now( boost::posix_time::seconds( 30 ) );
        _header = 0;
        _inputBuffer.consume( _inputBuffer.size() );
        // Start an asynchronous operation to read a newline-delimited message.
        async_read(
            _socket, boost::asio::buffer( &_header, sizeof _header ),
            [&]( const boost::system::error_code ec, const std::size_t N )
            {
                handle_read_body( ec, N );
            } );
    }

    void Client::handle_read_body( const boost::system::error_code& ec,
                                   [[maybe_unused]] size_t bytes_transferred )
    {
        if ( _stopped )
        {
            return;
        }

        if ( !ec )
        {
            _deadline.expires_from_now( boost::posix_time::seconds( 30 ) );
            async_read(
                _socket, _inputBuffer.prepare( _header ),
                [&]( const boost::system::error_code code, const std::size_t N )
                {
                    handle_read_packet( code, N );
                } );
        }
        else
        {
            stop();
        }
    }

    void Client::handle_read_packet( const boost::system::error_code& ec,
                                     [[maybe_unused]] size_t bytes_transferred )
    {

        if ( _stopped )
        {
            return;
        }
        if ( !ec )
        {
            _inputBuffer.commit( _header );
            std::istream is( &_inputBuffer );
            WorldPacket packet;
            try
            {
                boost::archive::text_iarchive ar( is );
                ar& packet;
            }
            catch ( const std::exception& e )
            {
                if ( _debugOutput )
                {
                    ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_PACKET_ERROR"), e.what()).c_str(), true );
                }
            }

            if ( packet.opcode() == OPCodes::SMSG_SEND_FILE )
            {
                async_read_until(
                    _socket, _requestBuf, "\n\n",
                    [&]( const boost::system::error_code code, const std::size_t N )
                    {
                        handle_read_file( code, N );
                    } );
            }
            else
            {
                receivePacket( packet );
                start_read();
            }
        }
        else
        {
            stop();
        }
    }

    void Client::handle_read_file( [[maybe_unused]] const boost::system::error_code& ec, const size_t bytes_transferred )
    {

        ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_READ_FILE"), __FUNCTION__, bytes_transferred, _requestBuf.in_avail(), _requestBuf.size(), _requestBuf.max_size()).c_str() );

        std::istream request_stream( &_requestBuf );
        string file_path;
        request_stream >> file_path;
        request_stream >> _fileSize;
        request_stream.read( _buf.data(), 2 );  // eat the "\n\n"

        {
            stringstream ss;
            ss << request_stream.tellg();
            ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_FILE_INFO"), __FUNCTION__, file_path.c_str(), _fileSize, ss.str().c_str()).c_str() );
        }

        const size_t pos = file_path.find_last_of( '\\' );
        if ( pos != string::npos ) file_path = file_path.substr( pos + 1 );
        _outputFile.open( file_path.c_str(), std::ios_base::binary );
        if ( !_outputFile )
        {
            ASIO::LOG_PRINT( Util::StringFormat( LOCALE_STR( "ASIO_FAIL_OPEN_FILE" ), file_path.c_str() ).c_str(), true );
            return;
        }
        // write extra bytes to file
        do
        {
            request_stream.read( _buf.data(), (std::streamsize)_buf.size() );
            ASIO::LOG_PRINT( Util::StringFormat( LOCALE_STR( "ASIO_WRITE_BYTES" ), __FUNCTION__, request_stream.gcount() ).c_str() );

            _outputFile.write( _buf.data(), request_stream.gcount() );
        }
        while ( request_stream.gcount() > 0 );

        async_read( _socket, boost::asio::buffer( _buf.data(), _buf.size() ),
                    [&]( const boost::system::error_code code, const std::size_t N )
                    {
                        handle_read_file_content( code, N );
                    } );
    }

    void Client::handle_read_file_content( const boost::system::error_code& err, std::size_t bytes_transferred )
    {
        if ( bytes_transferred > 0 )
        {
            _outputFile.write( _buf.data(), (std::streamsize)bytes_transferred );
            stringstream ss;
            ss << _outputFile.tellp();
            ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_READ_BYTES"), __FUNCTION__ , ss.str().c_str() ).c_str() );
            if ( _outputFile.tellp() >= (std::streamsize)_fileSize )
            {
                return;
            }
        }
        if ( err ) stop();
        start_read();
    }

    void Client::start_write()
    {
        if ( _stopped )
        {
            return;
        }

        if ( _packetQueue.empty() )
        {
            WorldPacket heart( OPCodes::MSG_HEARTBEAT );
            heart << to_I8( 0 );
            _packetQueue.push_back( heart );
        }

        const WorldPacket& p = _packetQueue.front();
        boost::asio::streambuf buf;
        std::ostream os( &buf );
        boost::archive::text_oarchive ar( os );
        ar& p;

        size_t header = buf.size();
        vector<boost::asio::const_buffer> buffers;
        buffers.push_back( boost::asio::buffer( &header, sizeof header ) );
        buffers.push_back( buf.data() );
        async_write( _socket, buffers, [&]( const boost::system::error_code ec, const size_t )
                     {
                         handle_write( ec );
                     } );
    }

    void Client::handle_write( const boost::system::error_code& ec )
    {
        if ( _stopped )
        {
            return;
        }

        if ( !ec )
        {
            _packetQueue.pop_front();
            _heartbeatTimer.expires_from_now( boost::posix_time::seconds( 2 ) );
            _heartbeatTimer.async_wait( [&]( const boost::system::error_code )
                                        {
                                            start_write();
                                        } );
        }
        else
        {
            if ( _debugOutput )
            {
                ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_PACKET_ERROR"), ec.message().c_str()).c_str(), true );
                stop();
            }
        }
    }

    void Client::check_deadline()
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
        _deadline.async_wait( [&]( const boost::system::error_code )
                              {
                                  check_deadline();
                              } );
    }

    void Client::start_connect( boost::asio::ip::tcp::resolver::iterator endpoint_iter )
    {
        if ( endpoint_iter != boost::asio::ip::tcp::resolver::iterator() )
        {
            if ( _debugOutput )
            {
                std::stringstream ss;
                ss << endpoint_iter->endpoint();
                ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_CONNECTING_TO_IP"), ss.str().c_str()).c_str() );
            }
            // Set a deadline for the connect operation.
            _deadline.expires_from_now( boost::posix_time::seconds( 60 ) );

            // Start the asynchronous connect operation.
            _socket.async_connect(
                endpoint_iter->endpoint(),
                [&, endpoint_iter]( const boost::system::error_code ec )
                {
                    handle_connect( ec, endpoint_iter );
                } );
        }
        else
        {
            // There are no more endpoints to try. Shut down the client.
            stop();
        }
    }

    void Client::handle_connect( const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpoint_iter )
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
            if ( _debugOutput )
            {
                ASIO::LOG_PRINT( LOCALE_STR("ASIO_CONNECT_TIME_OUT"), true );
            }
            // Try the next available endpoint.
            start_connect( ++endpoint_iter );
        }

        // Check if the connect operation failed before the deadline expired.
        else if ( ec )
        {
            if ( _debugOutput )
            {
                ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_EXCEPTION"),ec.message().c_str()).c_str(), true );
            }
            // We need to close the socket used in the previous connection attempt
            // before starting a new one.
            _socket.close();

            // Try the next available endpoint.
            start_connect( ++endpoint_iter );
        }

        // Otherwise we have successfully established a connection.
        else
        {
            if ( _debugOutput )
            {
                std::stringstream ss;
                ss << endpoint_iter->endpoint();
                ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_CONNECTED_TO_IP"), ss.str().c_str()).c_str() );
            }
            // Start the input actor.
            start_read();

            // Start the heartbeat actor.
            start_write();
        }
    }

};  // namespace Divide
