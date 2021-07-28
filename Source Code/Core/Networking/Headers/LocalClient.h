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
#ifndef _CORE_NETWORK_LOCAL_CLIENT_H_
#define _CORE_NETWORK_LOCAL_CLIENT_H_

#include "Networking/Headers/ASIO.h"
#include "Core/Headers/KernelComponent.h"

namespace Divide {
    
class LocalClient final : public ASIO,
                          public KernelComponent {
public:
    explicit LocalClient(Kernel& parent);

private:
    /// Define this functions to implement various packet handling (a switch
    /// statement for example)
    /// switch(p.getOpcode()) { case SMSG_XXXXX: bla bla bla break; case
    /// MSG_HEARTBEAT: break;}
    void handlePacket(WorldPacket& p) override;

    void HandlePongOpCode(WorldPacket& p) const;
    void HandleHeartBeatOpCode(WorldPacket& p);
    void HandleDisconnectOpCode(WorldPacket& p);
    void HandleGeometryAppendOpCode(WorldPacket& p);
};
}; //namespace Divide

#endif //_CORE_NETWORK_LOCAL_CLIENT_H_
