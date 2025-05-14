/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_NETWORK_PACKET_H_
#define DVD_NETWORK_PACKET_H_

#include "Core/Headers/ByteBuffer.h"

namespace Divide
{
namespace Networking
{

FWD_DECLARE_MANAGED_CLASS(Connection);

enum class OPCodes : U16
{
    SMSG_ACCEPT = 0,
    SMSG_DENY,
    CMSG_ALL,
    MSG_NOP,
    CMSG_HEARTBEAT,
    CMSG_PING,
    SMSG_PONG,
    SMSG_MSG,
    CMSG_ENTITY_UPDATE,
    SMSG_ENTITY_UPDATE,
    CMSG_REQUEST_FILE,
    SMSG_SEND_FILE,
    COUNT
};

struct NetworkPacket
{
    struct Header
    {
        size_t _byteLength{0u};
        OPCodes _opCode{OPCodes::MSG_NOP};
    };

    static constexpr size_t HEADER_SIZE = sizeof(Header);

    explicit NetworkPacket(const OPCodes opCode)
    {
        _header._opCode = opCode;
    }

    template<typename DataType>
    friend NetworkPacket& operator << (NetworkPacket& msg, const DataType& data)
    {
        msg._body << data;
        msg._header._byteLength = msg._body.bufferSize();
        return msg;
    }

    template<typename DataType>
    friend NetworkPacket& operator >> (NetworkPacket& msg, DataType& data)
    {
        msg._body >> data;
        msg._header._byteLength = msg._body.bufferSize();
        return msg;
    }

    friend std::ostream& operator << (std::ostream& os, const NetworkPacket& msg)
    {
        os << "ID:" << to_base(msg._header._opCode) << " Size:" << msg._header._byteLength;
        return os;
    }

    friend class Connection;
    PROPERTY_R(Header, header);
    PROPERTY_R(ByteBuffer, body);
};


struct OwnedNetworkPacket
{
    Connection_ptr _remote{ nullptr };
    NetworkPacket  _msg;

    friend std::ostream& operator<<(std::ostream& os, const OwnedNetworkPacket& msg)
    {
        os << msg._msg;
        return os;
    }
};


} // namespace Networking
} // namespace Divide

#endif //DVD_NETWORK_PACKET_H_
