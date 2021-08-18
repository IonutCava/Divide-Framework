#include "stdafx.h"

#include "Headers/SGNComponent.h"
#include "Graphs/Headers/SceneGraph.h"

#include "ECS/Components/Headers/NetworkingComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 17805u;

    SGNComponent::SGNComponent(Key key, const ComponentType type, SceneGraphNode* parentSGN, PlatformContext& context)
        : PlatformContextComponent(context),
          _editorComponent(*this, type, TypeUtil::ComponentTypeToString(type)),
          _parentSGN(parentSGN),
          _type(type)
    {
        ACKNOWLEDGE_UNUSED(key);
        std::atomic_init(&_enabled, true);
        std::atomic_init(&_hasChanged, false);
    }

    bool SGNComponent::saveCache(ByteBuffer& outputBuffer) const {
        if (_editorComponent.saveCache(outputBuffer)) {
            outputBuffer << BYTE_BUFFER_VERSION;
            outputBuffer << uniqueID();
            return true;
        }

        return false;
    }

    bool SGNComponent::loadCache(ByteBuffer& inputBuffer) {
        if (_editorComponent.loadCache(inputBuffer)) {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            inputBuffer >> tempVer;
            if (tempVer != BYTE_BUFFER_VERSION) {
                // Older version
                return false;
            }

            U64 tempID = 0u;
            inputBuffer >> tempID;
            if (tempID != uniqueID()) {
                // corrupt save
                return false;
            }

            return true;
        }

        return false;
    }

    void SGNComponent::saveToXML(boost::property_tree::ptree& pt) const {
        ACKNOWLEDGE_UNUSED(pt);
    }

    void SGNComponent::loadFromXML(const boost::property_tree::ptree& pt) {
        ACKNOWLEDGE_UNUSED(pt);
    }

    U64 SGNComponent::uniqueID() const {
        return _ID(Util::StringFormat("%s_%s", _parentSGN->name().c_str(), _editorComponent.name().c_str()).c_str());
    }

    bool SGNComponent::enabled() const {
        return _enabled;
    }

    void SGNComponent::enabled(const bool state) {
        _enabled = state;
    }

    void SGNComponent::OnData(const ECS::CustomEvent& data) {
        ACKNOWLEDGE_UNUSED(data);
    }

} //namespace Divide