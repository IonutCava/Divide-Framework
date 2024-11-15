

#include "Headers/Server.h"
#include "Headers/Session.h"

#include "Networking/Headers/ASIO.h"
#include "Utility/Headers/Localization.h"

using namespace boost::asio;

namespace Divide
{
    Server::Server()
        : _channel( std::make_shared<channel>() )
    {
    }

    Server::~Server()
    {
        close();
    }

    void Server::close()
    {
        if ( _thread == nullptr )
        {
            return; // stopped
        }

        _acceptor->close();
        _ioService.stop();
        _thread->join();
        _thread.reset();
    }

    void Server::init( const U16 port, const string& broadcast_endpoint_address, const bool debugOutput )
    {
        if ( _thread == nullptr )
        {
            return;
        }

        _debugOutput = debugOutput;
        try
        {
            const boost::asio::ip::tcp::endpoint listen_endpoint( boost::asio::ip::tcp::v4(), port );
            const boost::asio::ip::udp::endpoint broadcast_endpoint( boost::asio::ip::address::from_string( broadcast_endpoint_address.c_str() ), port );

            _acceptor = std::make_unique<tcp_acceptor>( _ioService.get_executor(), listen_endpoint );
            const subscriber_ptr bc( new udp_broadcaster( _ioService, broadcast_endpoint ) );
            _channel->join( bc );
            tcp_session_ptr new_session( new Session( _ioService, *_channel ) );

            _acceptor->async_accept(
                new_session->getSocket(),
                [&]( const boost::system::error_code code )
                {
                    handle_accept( new_session, code );
                }
            );

            std::unique_ptr<io_context::work> work( new io_context::work( _ioService ) );
            _thread = std::make_unique<std::thread>( [this]()
                                                     {
                                                         _ioService.run();
                                                     } );

        }
        catch ( std::exception& e )
        {
            ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("SERVER_EXCEPTION"), e.what()).c_str(), true );
        }
    }

    void Server::handle_accept( const tcp_session_ptr& session, const boost::system::error_code& ec )
    {
        if ( !ec )
        {
            if ( _debugOutput )
            {
                ASIO::LOG_PRINT( LOCALE_STR("SERVER_ACCEPT_TCP") );
            }
            session->start();

            tcp_session_ptr new_session( new Session( _ioService, *_channel ) );

            _acceptor->async_accept(
                new_session->getSocket(),
                [&]( const boost::system::error_code code )
                {
                    handle_accept( new_session, code );
                } );
        }
        else
        {
            ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("SERVER_ACCEPT_ERROR"), ec.what()).c_str(), true );
        }
    }

};  // namespace Divide
