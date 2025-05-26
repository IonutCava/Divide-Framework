#include "Headers/Server.h"

#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide::Networking
{
    // Create a server, ready to listen on specified port
    Server::Server(const U16 port)
        : _asioAcceptor(_asioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
    }

    Server::~Server()
    {
        // May as well try and tidy up
        stop();
    }

    // Starts the server!
    bool Server::start()
    {
        try
        {
            // Issue a task to the asio context - This is important
            // as it will prime the context with "work", and stop it
            // from exiting immediately. Since this is a server, we 
            // want it primed ready to handle clients trying to
            // connect.
            waitForClientConnection();

            // Launch the asio context in its own thread
            _threadContext = std::thread
            (
                [this]()
                {
                    SetThreadName("ASIO_SERVER_THREAD");
                    _asioContext.run();
                }
            );
        }
        catch (std::exception& e)
        {
            // Something prohibited the server from listening
            Console::errorfn(LOCALE_STR("SERVER_EXCEPTION"), e.what());
            return false;
        }

        Console::printfn(LOCALE_STR("SERVER_STARTED"));
        return true;
    }

    // Stops the server!
    void Server::stop()
    {
        // Request the context to close
        _deqConnections.clear();
        _asioContext.stop();

        // Tidy up the context thread
        if (_threadContext.joinable())
        {
            _threadContext.join();
        }

        Console::printfn(LOCALE_STR("SERVER_STOPPED"));
    }

    // ASYNC - Instruct asio to wait for connection
    void Server::waitForClientConnection()
    {
        // Prime context with an instruction to wait until a socket connects. This
        // is the purpose of an "acceptor" object. It will provide a unique socket
        // for each incoming connection attempt
        _asioAcceptor.async_accept
        (
            [this](std::error_code ec, boost::asio::ip::tcp::socket socket)
            {
                // Triggered by incoming connection request
                if (!ec)
                {
                    // Display some useful(?) information
                    std::ostringstream os;
                    os << socket.remote_endpoint();
                    Console::printfn(LOCALE_STR("SERVER_NEW_CONNECTION"), os.str());
                    // Create a new connection to handle this client 
                    Connection_ptr newconn = std::make_shared<Connection>(Connection::Owner::SERVER, _asioContext, MOV(socket), _messagesIn);

                    // Give the user server a chance to deny connection
                    if (onClientConnect(newconn))
                    {
                        // Connection allowed, so add to container of new connections
                        _deqConnections.push_back(MOV(newconn));

                        // And very important! Issue a task to the connection's
                        // asio context to sit and wait for bytes to arrive!
                        _deqConnections.back()->connectToClient(_IDCounter++);

                        Console::printfn(LOCALE_STR("SERVER_NEW_CONNECTION_ACCEPTED"), _deqConnections.back()->id());
                    }
                    else
                    {
                        Console::printfn(LOCALE_STR("SERVER_NEW_CONNECTION_DENIED"));

                        // Connection will go out of scope with no pending tasks, so will
                        // get destroyed automagically due to the wonder of smart pointers
                    }
                }
                else
                {
                    // Error has occurred during acceptance
                    Console::errorfn(LOCALE_STR("SERVER_EXCEPTION"), ec.message());
                }

                // Prime the asio context with more work - again simply wait for another connection...
                waitForClientConnection();
            });
    }

    // Send a message to a specific client
    void Server::messageClient(Connection_ptr client, const NetworkPacket& msg)
    {
        // Check client is legitimate...
        if (client && client->isConnected())
        {
            // ...and post the message via the connection
            client->send(msg);
        }
        else
        {
            // If we cant communicate with client then we may as 
            // well remove the client - let the server know, it may
            // be tracking it somehow
            onClientDisconnect(client);

            // Off you go now, bye bye!
            client.reset();

            // Then physically remove it from the container
            _deqConnections.erase(std::remove(_deqConnections.begin(), _deqConnections.end(), client), _deqConnections.end());
        }
    }

    // Send message to all clients
    void Server::messageAllClients(const NetworkPacket& msg, Connection_ptr ignoreClient)
    {
        bool invalidClientExists = false;

        // Iterate through all clients in container
        for (auto& client : _deqConnections)
        {
            // Check client is connected...
            if (client && client->isConnected())
            {
                // ..it is!
                if (client != ignoreClient)
                {
                    client->send(msg);
                }
            }
            else
            {
                // The client couldnt be contacted, so assume it has
                // disconnected.
                onClientDisconnect(client);
                client.reset();

                // Set this flag to then remove dead clients from container
                invalidClientExists = true;
            }
        }

        // Remove dead clients, all in one go - this way, we dont invalidate the
        // container as we iterated through it.
        if (invalidClientExists)
        {
            _deqConnections.erase( std::remove(_deqConnections.begin(), _deqConnections.end(), nullptr), _deqConnections.end());
        }
    }

    // Force server to respond to incoming messages
    void Server::update(const size_t nMaxMessages, const bool bWait)
    {
        if (bWait)
        {
            _messagesIn.wait();
        }

        // Process as many messages as you can up to the value
        // specified
        size_t nMessageCount = 0;
        while (nMessageCount < nMaxMessages && !_messagesIn.empty())
        {
            // Grab the front message
            auto msg = _messagesIn.pop_front();

            // Pass to message handler
            receiveMessage(msg._remote, msg._msg);

            nMessageCount++;
        }
    }

    bool Server::onClientConnect(Connection_ptr client)
    {
        NetworkPacket msg{ OPCodes::SMSG_ACCEPT };
        client->send(msg);
        return true;
    }

    void Server::onClientDisconnect(Connection_ptr client)
    {
        Console::printfn(LOCALE_STR("SERVER_CLIENT_DISCONNECTED"), client->id());
    }

    void Server::receiveMessage(Connection_ptr client, NetworkPacket& msg)
    {
        switch (msg.header()._opCode)
        {
            case OPCodes::CMSG_HEARTBEAT:
            {
                U8 codeIn{U8_MAX};
                msg >> codeIn;
                Console::printfn(LOCALE_STR("SERVER_ON_RECEIVE_HEARTBEAT"), codeIn, client->id());
            } break;
            case OPCodes::CMSG_PING:
            {
                D64 timeIn = 0.;
                msg >> timeIn;
                Console::printfn(LOCALE_STR("SERVER_ON_RECEIVE_PING"), timeIn, client->id());

                NetworkPacket msgOut{ OPCodes::SMSG_PONG };
                msgOut << timeIn;
                msgOut << Time::App::ElapsedMilliseconds();

                client->send(msgOut);
            } break;
            case OPCodes::CMSG_ALL:
            {
                Console::printfn(LOCALE_STR("SERVER_ON_RECEIVE_MSG_ALL"), client->id());
                NetworkPacket msgOut{ OPCodes::SMSG_MSG };
                msgOut << client->id();
                messageAllClients(msgOut, client);
            } break;
            case OPCodes::CMSG_ENTITY_UPDATE:
            {
                I64 guid {-1};
                U32 frameCount{0u};
                msg >> guid;
                msg >> frameCount;

                Console::printfn(LOCALE_STR("SERVER_ON_RECEIVE_ENTITY_UPDATE"), client->id(), guid);

                NetworkPacket msgOut{ OPCodes::SMSG_ENTITY_UPDATE };
                msgOut << client->id();
                msgOut << guid;
                msgOut << frameCount;
                messageAllClients(msgOut, client);
            } break;
            case OPCodes::MSG_NOP:
            {
                Console::printfn(LOCALE_STR("SERVER_ON_RECEIVE_NOP"));
            } break;
            case OPCodes::CMSG_REQUEST_FILE:
            {
                ResourcePath filePath;
                string fileName;
                vector<Byte> fileData;

                msg >> filePath;
                msg >> fileName;


                bool flag = true;
                std::ifstream data;
                const FileError ret = readFile(filePath, fileName, FileType::BINARY, data);
                if (ret != FileError::NONE)
                {
                    Console::errorfn(LOCALE_STR("SERVER_FAIL_OPEN_FILE"), fileName);
                    flag = false;
                }

                NetworkPacket msgOut{ OPCodes::SMSG_SEND_FILE };
                msgOut << flag;
                msgOut << filePath;
                msgOut << fileName;
                if ( flag )
                {
                    msgOut << fileData;
                }
                client->send(msgOut);
            } break;
            default: break;
        }
    }
} //namespace Divide::Networking
