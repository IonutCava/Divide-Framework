/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_NETWORKING_COMPONENT_H_
#define DVD_NETWORKING_COMPONENT_H_


#include "SGNComponent.h"

#include "Networking/Headers/NetworkPacket.h"

namespace Divide {

namespace Networking
{
    class Client;
    struct NetworkPacket;
} // namespace Networking

BEGIN_COMPONENT(Networking, ComponentType::NETWORKING)
public:
    NetworkingComponent(SceneGraphNode* parentSGN, PlatformContext& context);
    ~NetworkingComponent() override;

    void onNetworkSend(U32 frameCountIn);

    void flagDirty(U32 srcClientID, U32 frameCount) noexcept;

    static NetworkingComponent* GetReceiver(I64 guid);

private:
    void onNetworkReceive(Networking::NetworkPacket& dataIn);

private:
    [[nodiscard]] Networking::NetworkPacket deltaCompress(const Networking::NetworkPacket& crt, const Networking::NetworkPacket& previous) const;
    [[nodiscard]] Networking::NetworkPacket deltaDecompress(const Networking::NetworkPacket& crt, const Networking::NetworkPacket& previous) const;

private:
    Networking::Client& _parentClient;

    Networking::NetworkPacket _previousSent{Networking::OPCodes::MSG_NOP};
    Networking::NetworkPacket _previousReceived{Networking::OPCodes::MSG_NOP};

    static hashMap<I64, NetworkingComponent*> s_NetComponents;
END_COMPONENT(Networking);

} //namespace Divide

#endif //DVD_NETWORKING_COMPONENT_H_
