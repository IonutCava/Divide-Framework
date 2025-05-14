#include "Headers/Connection.h"

#include "Utility/Headers/Localization.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

namespace Divide::Networking
{
    Connection::Connection(Owner parent, boost::asio::io_context& asioContext, boost::asio::ip::tcp::socket socket, OwnedPacketQueue& qIn)
        : _socket(MOV(socket))
        , _asioContext(asioContext)
        , _messagesIn(qIn)
        , _msgTemporaryIn(OPCodes::MSG_NOP)
        , _ownerType( parent )
    {
    }

    Connection::~Connection()
    {
    }

    void Connection::connectToClient(const U32 uid)
    {
        if (_ownerType == Owner::SERVER && _socket.is_open() )
        {
            _id = uid;
            readHeader();
        }
    }

    void Connection::connectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints)
    {
        // Only clients can connect to servers
        if (_ownerType == Owner::CLIENT)
        {
            // Request asio attempts to connect to an endpoint
            boost::asio::async_connect
            (
                _socket,
                endpoints,
                [this](std::error_code ec, boost::asio::ip::tcp::endpoint endpoint)
                {
                    if (!ec)
                    {
                        readHeader();
                    }
                    else
                    {
                        Console::errorfn(LOCALE_STR("NETWORK_ERROR_CODE_ERROR"), ec.message());
                    }
                });
        }
    }

    void Connection::disconnect()
    {
        if (isConnected())
        {
            boost::asio::post
            (
                _asioContext,
                [this]()
                {
                    _socket.close();
                }
            );
        }
    }

    bool Connection::isConnected() const
    {
        return _socket.is_open();
    }

    // Prime the connection to wait for incoming messages
    void Connection::startListening()
    {

    }

    void Connection::send(const NetworkPacket& p)
    {
        boost::asio::post
        (
            _asioContext,
            [this, p]()
            {
                // If the queue has a message in it, then we must 
                // assume that it is in the process of asynchronously being written.
                // Either way add the message to the queue to be output. If no messages
                // were available to be written, then start the process of writing the
                // message at the front of the queue.
                const bool writingMessage = !_messagesOut.empty();
                _messagesOut.push_back(p);

                if (!writingMessage)
                {
                    writeHeader();
                }
            }
        );
    }

    void Connection::writeHeader()
    {

        // If this function is called, we know the outgoing message queue must have 
        // at least one message to send. So allocate a transmission buffer to hold
        // the message, and issue the work - asio, send these bytes
        boost::asio::async_write
        (
            _socket,
            boost::asio::buffer(&_messagesOut.front().header(), NetworkPacket::HEADER_SIZE),
            [this](std::error_code ec, std::size_t length)
            {
                // asio has now sent the bytes - if there was a problem an error would be available...
                if (!ec)
                {
                    // ... no error, so check if the message header just sent also has a message body...
                    if (_messagesOut.front().header()._byteLength > 0u)
                    {
                        // ...it does, so issue the task to write the body bytes
                        writeBody();
                    }
                    else
                    {
                        // ...it didnt, so we are done with this message. Remove it from the outgoing message queue
                        _messagesOut.pop_front();

                        // If the queue is not empty, there are more messages to send, so make this happen by issuing the task to send the next header.
                        if (!_messagesOut.empty())
                        {
                            writeHeader();
                        }
                    }
                }
                else
                {
                    // ...asio failed to write the message, we could analyse why but 
                    // for now simply assume the connection has died by closing the
                    // socket. When a future attempt to write to this client fails due
                    // to the closed socket, it will be tidied up.
                    Console::errorfn(LOCALE_STR("NETWORK_ERROR_CODE_ERROR"), ec.message());
                    _socket.close();
                }
            }
        );
    }

    // ASYNC - Prime context to write a message body
    void Connection::writeBody()
    {
        // If this function is called, a header has just been sent, and that header
        // indicated a body existed for this message. Fill a transmission buffer
        // with the body data, and send it!
        boost::asio::async_write
        (
            _socket,
            boost::asio::buffer(_messagesOut.front().body().contents(), _messagesOut.front().body().bufferSize()),
            [this](std::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    // Sending was successful, so we are done with the message
                    // and remove it from the queue
                    _messagesOut.pop_front();

                    // If the queue still has messages in it, then issue the task to 
                    // send the next messages' header.
                    if (!_messagesOut.empty())
                    {
                        writeHeader();
                    }
                }
                else
                {
                    // Sending failed, see WriteHeader() equivalent for description :P
                    Console::errorfn(LOCALE_STR("NETWORK_ERROR_CODE_ERROR"), ec.message());
                    _socket.close();
                }
            }
        );
    }

    // ASYNC - Prime context ready to read a message header
    void Connection::readHeader()
    {
        // If this function is called, we are expecting asio to wait until it receives
        // enough bytes to form a header of a message. We know the headers are a fixed
        // size, so allocate a transmission buffer large enough to store it. In fact, 
        // we will construct the message in a "temporary" message object as it's 
        // convenient to work with.
        boost::asio::async_read
        (
            _socket,
            boost::asio::buffer(&_msgTemporaryIn._header, NetworkPacket::HEADER_SIZE),
            [this](std::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    // A complete message header has been read, check if this message has a body to follow...
                    if (_msgTemporaryIn._header._byteLength > 0u)
                    {
                        // ...it does, so allocate enough space in the messages' body
                        // vector, and issue asio with the task to read the body.
                        _msgTemporaryIn._body.resize(_msgTemporaryIn._header._byteLength);
                        readBody();
                    }
                    else
                    {
                        // it doesn't, so add this bodyless message to the connections incoming message queue
                        addToIncomingMessageQueue();
                    }
                }
                else
                {
                    // Reading form the client went wrong, most likely a disconnect
                    // has occurred. Close the socket and let the system tidy it up later.
                    Console::errorfn(LOCALE_STR("NETWORK_ERROR_CODE_ERROR"), ec.message());
                    _socket.close();
                }
            }
        );
    }

    // ASYNC - Prime context ready to read a message body
    void Connection::readBody()
    {
        // If this function is called, a header has already been read, and that header
        // request we read a body, The space for that body has already been allocated
        // in the temporary message object, so just wait for the bytes to arrive...

        vector<Byte>& storage = Attorney::ByteBufferStorageAccessor::bufferStorage(_msgTemporaryIn._body);

        boost::asio::async_read
        (
            _socket,
            boost::asio::buffer(storage.data(), storage.size()),
            [this](std::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    // ...and they have! The message is now complete, so add the whole message to incoming queue
                    addToIncomingMessageQueue();
                }
                else
                {
                    // As above!
                    Console::errorfn(LOCALE_STR("NETWORK_ERROR_CODE_ERROR"), ec.message());
                    _socket.close();
                }
            }
        );
    }

    // Once a full message is received, add it to the incoming queue
    void Connection::addToIncomingMessageQueue()
    {
        // Shove it in queue, converting it to an "owned message", by initialising
        // with the a shared pointer from this connection object
        if (_ownerType == Owner::SERVER)
        {
            _messagesIn.push_back({ shared_from_this(), _msgTemporaryIn });
        }
        else
        {
            _messagesIn.push_back({ nullptr, _msgTemporaryIn });
        }

        // We must now prime the asio context to receive the next message. It 
        // wil just sit and wait for bytes to arrive, and the message construction
        // process repeats itself. Clever huh?
       readHeader();
    }
} //namespace Divide::Networking
