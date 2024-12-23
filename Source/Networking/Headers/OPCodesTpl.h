#pragma once
#ifndef DVD_OPCODES_H_INFO_
#define DVD_OPCODES_H_INFO_

#ifndef OPCODE_ENUM
#define OPCODE_ENUM
#endif

namespace Divide {

/// Packet handling requires OPCodes to be defined. Use the following num
/// structure to define them in each app:
struct OPCodes
{
    using ValueType = int32_t;

    static constexpr ValueType MSG_NOP = 0x000;
    static constexpr ValueType MSG_HEARTBEAT = 0x001;
    static constexpr ValueType SMSG_SEND_FILE = 0x002;
    static constexpr ValueType SMSG_DISCONNECT = 0x003;
    static constexpr ValueType CMSG_REQUEST_DISCONNECT = 0x004;
    static constexpr ValueType CMSG_ENTITY_UPDATE = 0x005;
    static constexpr ValueType CMSG_PING = 0x006;
    static constexpr ValueType SMSG_PONG = 0x007;
    static constexpr ValueType CMSG_REQUEST_FILE = 0x008;
    
    static const ValueType FIRST_FREE_OPCODE = CMSG_REQUEST_FILE;

    static constexpr ValueType OPCODE_ID(const ValueType index)
    {
        return FIRST_FREE_OPCODE + index;
    }
};

/*To create new OPCodes follow this convention:

struct OPCodesEx : public OPCodes
{
    static const ValueType CMSG_EXAMPLE = OPCODE_ID(1);
    static const ValueType SMSG_EXAMPLE2 = OPCODE_ID(2);
};

And use OPCodesEx for switch statements and packet handling
*/

};  // namespace Divide

#endif //DVD_OPCODES_H_INFO_
