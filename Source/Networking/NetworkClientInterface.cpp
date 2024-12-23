

#ifndef OPCODE_ENUM
#define OPCODE_ENUM OPcodes
#endif

#include "Headers/OPCodesTpl.h"
#include "Headers/NetworkClientInterface.h"
#include "Headers/NetworkClientImpl.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    NetworkClientInterface::~NetworkClientInterface()
    {
        _ioService.stop();
        _work.reset();
        if ( _thread != nullptr )
        {
            _thread->join();
        }
        if ( _localClient != nullptr )
        {
            _localClient->stop();
        }
    }

    void NetworkClientInterface::disconnect()
    {
        if ( !_connected )
        {
            return;
        }
        WorldPacket p( OPCodes::CMSG_REQUEST_DISCONNECT );
        p << _localClient->socket().local_endpoint().address().to_string();
        sendPacket( p );
    }

    bool NetworkClientInterface::init( const string& address, const U16 port )
    {
        try
        {
            tcp_resolver res( _ioService );
            _localClient = std::make_unique<NetworkClientImpl>( this, _ioService );
            _work.reset( new boost::asio::io_context::work( _ioService ) );
            _localClient->start( res.resolve( address, Util::to_string( port ) ) );
            _thread = std::make_unique<std::thread>( [&]
                                                     {
                                                        SetThreadName("ASIO_THREAD");
                                                         _ioService.run();
                                                     } );
            _ioService.poll();
            _connected = true;
        }
        catch ( const std::exception& e )
        {
            Console::errorfn( LOCALE_STR("ASIO_EXCEPTION"), e.what() );
            _connected = false;
        }

        return _connected;
    }

    bool NetworkClientInterface::connect( const string& address, const U16 port )
    {
        if ( _connected )
        {
            close();
        }

        return init( address, port );
    }

    bool NetworkClientInterface::isConnected() const noexcept
    {
        return _connected;
    }

    void NetworkClientInterface::close()
    {
        _localClient->stop();
        _connected = false;
    }

    bool NetworkClientInterface::sendPacket( WorldPacket& p ) const
    {
        if ( !_connected )
        {
            return false;
        }
        if ( _localClient->sendPacket( p ) )
        {

            Console::printfn( LOCALE_STR("ASIO_OPCODE"), p.opcode() );
            return true;
        }

        return false;
    }

};  // namespace Divide
