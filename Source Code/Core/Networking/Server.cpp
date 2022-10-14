#include "stdafx.h"

#include "Headers/Server.h"
#include "Headers/Session.h"

#include "Networking/Headers/ASIO.h"

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
        MemoryManager::SAFE_DELETE( _acceptor );
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

            _acceptor = MemoryManager_NEW tcp_acceptor( _ioService.get_executor(), listen_endpoint );
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

            eastl::unique_ptr<io_context::work> work( new io_context::work( _ioService ) );
            _thread = eastl::make_unique<std::thread>( [this]()
                                                       {
                                                           _ioService.run();
                                                       } );

        }
        catch ( std::exception& e )
        {
            ASIO::LOG_PRINT( (string( "SERVER: " ) + e.what()).c_str(), true );
        }
    }

    void Server::handle_accept( const tcp_session_ptr& session, const boost::system::error_code& ec )
    {
        if ( !ec )
        {
            if ( _debugOutput )
            {
                ASIO::LOG_PRINT( "New TCP session accepted" );
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
            std::stringstream ss;
            ss << "Server::handle_accept ERROR: " << ec;
            ASIO::LOG_PRINT( ss.str().c_str(), true );
        }
    }

};  // namespace Divide
