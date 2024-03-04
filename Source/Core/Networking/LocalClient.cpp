

#include "Headers/LocalClient.h"
#include "Headers/OPCodesImpl.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide {

LocalClient::LocalClient(Kernel& parent) : ASIO(), KernelComponent(parent)
{
}

void LocalClient::handlePacket(WorldPacket& p) {
    switch (p.opcode()) {
    case OPCodes::MSG_HEARTBEAT:
        HandleHeartBeatOpCode(p);
        break;
    case OPCodesEx::SMSG_PONG:
        HandlePongOpCode(p);
        break;
    case OPCodes::SMSG_DISCONNECT:
        HandleDisconnectOpCode(p);
        break;
    case OPCodesEx::SMSG_GEOMETRY_APPEND:
        HandleGeometryAppendOpCode(p);
        break;
    default:
        _parent.platformContext().paramHandler().setParam(_ID("serverResponse"), "Unknown OpCode: [ 0x" + Util::to_string(p.opcode()) + " ]");
        break;
    }
}

void LocalClient::HandlePongOpCode(WorldPacket& p) const
{
    F32 time = 0;
    p >> time;
    const D64 result = Time::App::ElapsedMilliseconds() - time;
    _parent.platformContext().paramHandler().setParam(
        _ID("serverResponse"),
        "Server says: Pinged with : " +
        Util::to_string(floor(result + 0.5f)) +
        " ms latency");
}

void LocalClient::HandleDisconnectOpCode(WorldPacket& p) {
    U8 code;
    p >> code;
    Console::printfn(LOCALE_STR("ASIO_CLOSE"));
    if (code == 0) close();
    // else handleError(code);
}

void LocalClient::HandleGeometryAppendOpCode(WorldPacket& p) {
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

void LocalClient::HandleHeartBeatOpCode([[maybe_unused]] WorldPacket& p) {
    /// nothing. Heartbeats keep us alive \:D/
}

}; //namespace Divide