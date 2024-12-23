

#include "Headers/Server.h"
#include "Headers/TCPUDPImpl.h"

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

        _work.reset();
        _acceptor->close();
        _ioService.stop();
        _thread->join();
        _thread.reset();
    }

    void Server::init( const U16 port, const string& broadcast_endpoint_address )
    {
        if ( _thread != nullptr )
        {
            close();
        }

        try
        {
            const boost::asio::ip::tcp::endpoint listen_endpoint( boost::asio::ip::tcp::v4(), port );

            const boost::asio::ip::udp::endpoint broadcast_endpoint( boost::asio::ip::address::from_string( broadcast_endpoint_address.c_str() ), port );

            
            subscriber_ptr new_subscriber = std::make_shared<UDPBroadcaster>( _ioService, broadcast_endpoint );

            _channel->join( new_subscriber );

            TCPUDPInterface_ptr new_session = std::make_shared<TCPUDPImpl>( _ioService, *_channel );

            _acceptor = std::make_unique<tcp_acceptor>( _ioService.get_executor(), listen_endpoint );
            _acceptor->async_accept
            (
                new_session->getSocket(),
                [&, new_session]( const boost::system::error_code code )
                {
                    handle_accept( new_session, code );
                }
            );

            _work = std::make_unique<io_context::work>( _ioService );
            
            _thread = std::make_unique<std::thread>( [this]()
                                                     {
                                                         _ioService.run();
                                                     } );

        }
        catch ( std::exception& e )
        {
            Console::errorfn( Util::StringFormat(LOCALE_STR("SERVER_EXCEPTION"), e.what()) );
        }
    }

    void Server::handle_accept( const TCPUDPInterface_ptr& session, const boost::system::error_code& ec )
    {
        if ( !ec )
        {
            Console::printfn( LOCALE_STR("SERVER_ACCEPT_TCP") );
            session->start();

            TCPUDPInterface_ptr new_session = std::make_shared<TCPUDPImpl>( _ioService, *_channel );

            _acceptor->async_accept
            (
                new_session->getSocket(),
                [&, new_session]( const boost::system::error_code code )
                {
                    handle_accept( new_session, code );
                }
            );
        }
        else
        {
            Console::errorfn( Util::StringFormat(LOCALE_STR("SERVER_ACCEPT_ERROR"), ec.what()).c_str() );
        }
    }

};  // namespace Divide
