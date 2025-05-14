#include "Headers/Client.h"
#include "Headers/Connection.h"

#include "Utility/Headers/Localization.h"
#include "ECS/Components/Headers/NetworkingComponent.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide::Networking
{
    Client::Client()
        : _heartbeatTimer(_context)
    {
    }

    Client::~Client()
    {
        // If the client is destroyed, always try and disconnect from server
        disconnect();
    }

    bool Client::connect(const std::string_view host, const U16 port)
    {
        try
        {
            // Resolve hostname/ip-address into tangiable physical address
            boost::asio::ip::tcp::resolver resolver(_context);
            boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

            // Create connection
            _connection = std::make_unique<Connection>(Connection::Owner::CLIENT, _context, boost::asio::ip::tcp::socket(_context), _messagesIn);

            // Tell the connection object to connect to server
            _connection->connectToServer(endpoints);

            // Start Context Thread
            _contextThread = std::thread
            (
                [&]()
                {
                    SetThreadName("ASIO_CLIENT_THREAD");
                    _context.run();
                }
            );
        }
        catch (std::exception& e)
        {
            Console::errorfn(LOCALE_STR("CLIENT_EXCEPTION"), e.what());
            return false;
        }

        return true;
    }

    void Client::disconnect()
    {
        _heartbeatTimer.cancel();

        // If connection exists, and it's connected then...
        if (isConnected())
        {
            // ...disconnect from server gracefully
            _connection->disconnect();
        }

        // Either way, we're also done with the asio context...
        _context.stop();

        // ...and its thread
        if (_contextThread.joinable())
        {
            _contextThread.join();
        }

        // Destroy the connection object
        _connection.release();
    }


    bool Client::isConnected() const
    {
        if (_connection != nullptr)
        {
            return _connection->isConnected();
        }

        return false;
    }

    
    void Client::send(const NetworkPacket& p)
    {
        if (isConnected())
        {
            _heartbeatTimer.expires_at(boost::posix_time::neg_infin);
            sendMessage(p);
            heartbeatWait();
        }
    }

    void Client::update()
    {
        if ( !isConnected() )
        {
            return;
        }

        while (!_messagesIn.empty())
        {
            NetworkPacket msg = _messagesIn.pop_front()._msg;
            receiveMessage(msg);
        }

        static bool init = false;
        if ( !init)
        {
            init = true;
            heartbeatSend();
        }
    }

    void Client::heartbeatWait()
    {
        _heartbeatTimer.expires_from_now(boost::posix_time::seconds(2));
        _heartbeatTimer.async_wait
        (
            [&]([[maybe_unused]] const boost::system::error_code ec)
            {
                heartbeatSend();
            }
        );
    }

    void Client::heartbeatSend()
    {
        if ( ++_heartbeatCounter == HeartbeastPerPingRequest)
        {
            NetworkPacket msg{ OPCodes::CMSG_PING };
            msg << Time::App::ElapsedMilliseconds();
            _heartbeatCounter = 0u;
        }
        else
        {
            NetworkPacket msg{ OPCodes::CMSG_HEARTBEAT };
            msg << U8_ZERO;
            sendMessage(msg);
        }
        heartbeatWait();
    }

    void Client::receiveMessage(NetworkPacket& msg)
    {
        Console::printfn(LOCALE_STR("CLIENT_MSG_RECEIVE"), to_base(msg.header()._opCode));
        switch (msg.header()._opCode)
        {
            default:
            case OPCodes::MSG_NOP:
            case OPCodes::SMSG_ACCEPT:
            case OPCodes::SMSG_DENY:   break;

            case OPCodes::SMSG_PONG: 
            {
                D64 timeClient = 0., timeServer = 0.;
                msg >> timeClient;
                msg >> timeServer;
                Console::printfn(LOCALE_STR("CLIENT_ON_RECEIVE_PONG"), timeClient, timeServer);
            } break;
            case OPCodes::SMSG_MSG:
            {
                U32 srcID{0u};
                msg >> srcID;
                Console::printfn(LOCALE_STR("CLIENT_ON_RECEIVE_MSG"), srcID);
            } break;
            case OPCodes::SMSG_ENTITY_UPDATE:
            {
                U32 srcID{ 0u };
                I64 targetGUID{-1};
                U32 frameCount{0u};
                msg >> srcID;
                msg >> targetGUID;
                msg >> frameCount;
                Console::printfn(LOCALE_STR("CLIENT_ON_RECEIVE_ENTITY_UPDATE"), targetGUID, srcID, frameCount);
                NetworkingComponent* comp = NetworkingComponent::GetReceiver(targetGUID);
                if ( comp != nullptr )
                {
                    comp->flagDirty(srcID, frameCount);
                }

            } break;
            case OPCodes::SMSG_SEND_FILE:
            {
                ResourcePath filePath;
                string fileName;
                vector<Byte> fileData;
                bool flag{false};

                msg >> flag;
                msg >> filePath;
                msg >> fileName;
                if ( flag )
                {
                    msg >> fileData;
                    Console::printfn(LOCALE_STR("CLIENT_ON_RECEIVE_FILE_DATA"), filePath.string(), fileName, fileData.size() );
                    if (writeFile(filePath, fileName, fileData.data(), fileData.size(), FileType::BINARY) != FileError::NONE)
                    {
                        Console::errorfn(LOCALE_STR("CLIENT_FAIL_SAVE_FILE"), filePath.string(), fileName, fileData.size());
                    }
                }
                else
                {
                    Console::printfn(LOCALE_STR("CLIENT_ON_RECEIVE_FILE_DATA_ERROR"), filePath.string(), fileName);
                }

            } break;
        };
    }

    void Client::sendMessage(const NetworkPacket& msg)
    {
        Console::printfn(LOCALE_STR("CLIENT_MSG_SEND"), to_base(msg.header()._opCode));
        _connection->send(msg);
    }

    void Client::requestFile(const ResourcePath& path, const string& name)
    {
        NetworkPacket msg{OPCodes::CMSG_REQUEST_FILE};
        msg << path;
        msg << name;
        sendMessage(msg);
    }
} //namespace Divide::Networking
