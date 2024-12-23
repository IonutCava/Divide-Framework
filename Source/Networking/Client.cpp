

#include "Headers/Client.h"
#include "Headers/OPCodesImpl.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ApplicationTimer.h"

#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

Client::Client(Kernel& parent)
    : NetworkClientInterface()
    , KernelComponent(parent)
{
}

void Client::handlePacket(WorldPacket& p)
{
    switch (p.opcode())
    {
        case OPCodes::MSG_HEARTBEAT:
            HandleHeartBeatOpCode(p);
            break;
        case OPCodesEx::SMSG_PONG:
            HandlePongOpCode(p);
            break;
        case OPCodes::SMSG_DISCONNECT:
            HandleDisconnectOpCode(p);
            break;
        case OPCodes::SMSG_SEND_FILE :
            HandleFileOpCode(p);
            break;
        case OPCodesEx::SMSG_GEOMETRY_APPEND:
            HandleGeometryAppendOpCode(p);
            break;
        default:
            _parent.platformContext().paramHandler().setParam(_ID("serverResponse"), "Unknown OpCode: [ 0x" + Util::to_string(p.opcode()) + " ]");
            break;
    }
}

void Client::HandlePongOpCode(WorldPacket& p) const
{
    Console::printfn("Client::HandlePongOpCode");

    F32 time = 0;
    p >> time;
    const D64 result = Time::App::ElapsedMilliseconds() - time;
    _parent.platformContext().paramHandler().setParam(
        _ID("serverResponse"),
        "Server says: Pinged with : " +
        Util::to_string(floor(result + 0.5f)) +
        " ms latency");
}

void Client::HandleDisconnectOpCode(WorldPacket& p)
{
    Console::printfn("Client::HandleDisconnectOpCode");

    U8 code;
    p >> code;
    Console::printfn(LOCALE_STR("ASIO_CLOSE"));
    if (code == 0) close();
    // else handleError(code);
}

void Client::HandleGeometryAppendOpCode(WorldPacket& p)
{
    Console::printfn(LOCALE_STR("ASIO_PAK_REC_GEOM_APPEND"));
    U8 size;
    p >> size;
    /*vector<FileData> patch;
    for (U8 i = 0; i < size; i++) {
        FileData d;
        p >> d.ItemName;
        p >> d.ModelName;
        p >> d.orientation.x;
        p >> d.orientation.y;
        p >> d.orientation.z;
        p >> d.position.x;
        p >> d.position.y;
        p >> d.position.z;
        p >> d.scale.x;
        p >> d.scale.y;
        p >> d.scale.z;
        patch.push_back(d);
    }
    _parentScene.addPatch(patch);*/
}

void Client::HandleHeartBeatOpCode([[maybe_unused]] WorldPacket& p)
{
    Console::printfn("Client::HandleHeartBeatOpCode");

    /// nothing. Heartbeats keep us alive \:D/
}

void Client::HandleFileOpCode(WorldPacket& p)
{
    string filePath, fileName;
    vector<Byte> byte_data;
    
    p >> filePath;
    p >> fileName;
    p >> byte_data;

    if (writeFile(ResourcePath(filePath), fileName, byte_data.data(), byte_data.size(), FileType::BINARY) != FileError::NONE)
    {
        Console::errorfn(LOCALE_STR("ASIO_PACKET_ERROR"), filePath, fileName, byte_data.size());
    }
}

}; //namespace Divide
