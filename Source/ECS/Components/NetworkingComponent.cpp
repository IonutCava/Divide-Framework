

#include "Headers/NetworkingComponent.h"

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"

#include "Networking/Headers/Client.h"

#include "Core/Headers/PlatformContext.h"

namespace Divide {

hashMap<I64, NetworkingComponent*> NetworkingComponent::s_NetComponents;

NetworkingComponent::NetworkingComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<NetworkingComponent, ComponentType::NETWORKING>(parentSGN, context)
    , _parentClient(context.networking().client())
{
    // Register a receive callback with parent:
    // e.g.: _receive->bind(NetworkingComponent::onNetworkReceive);

    s_NetComponents[parentSGN->getGUID()] = this;
}

NetworkingComponent::~NetworkingComponent()
{
    s_NetComponents.erase(_parentSGN->getGUID());
}

void NetworkingComponent::flagDirty([[maybe_unused]] const U32 srcClientID, [[maybe_unused]] const U32 frameCount) noexcept
{

}

Networking::NetworkPacket NetworkingComponent::deltaCompress(const Networking::NetworkPacket& crt, [[maybe_unused]] const Networking::NetworkPacket& previous) const {
    return crt;
}

Networking::NetworkPacket NetworkingComponent::deltaDecompress(const Networking::NetworkPacket& crt, [[maybe_unused]] const Networking::NetworkPacket& previous) const {
    return crt;
}

void NetworkingComponent::onNetworkSend(const U32 frameCountIn)
{
    Networking::NetworkPacket dataOut(Networking::OPCodes::CMSG_ENTITY_UPDATE);
    dataOut << _parentSGN->getGUID();
    dataOut << frameCountIn;

    Attorney::SceneNodeNetworkComponent::onNetworkSend(_parentSGN, _parentSGN->getNode(), dataOut);

    const Networking::NetworkPacket p = deltaCompress(dataOut, _previousSent);
    _previousSent = p;

    _parentClient.send(dataOut);
}

void NetworkingComponent::onNetworkReceive(Networking::NetworkPacket& dataIn)
{
    const Networking::NetworkPacket p = deltaDecompress(dataIn, _previousReceived);
    _previousReceived = p;

    Attorney::SceneNodeNetworkComponent::onNetworkReceive(_parentSGN, _parentSGN->getNode(), dataIn);
}

NetworkingComponent* NetworkingComponent::GetReceiver(const I64 guid)
{
    const hashMap<I64, NetworkingComponent*>::const_iterator it = s_NetComponents.find(guid);

    if (it != std::cend(s_NetComponents))
    {
        return it->second;
    }

    return nullptr;
}

} //namespace Divide
