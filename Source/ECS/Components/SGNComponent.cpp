

#include "Headers/SGNComponent.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneGraphNode.h"

#include "ECS/Components/Headers/NetworkingComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    SGNComponent::SGNComponent([[maybe_unused]] Key key, const ComponentType type, SceneGraphNode* parentSGN, PlatformContext& context)
        : PlatformContextComponent(context)
        , _parentSGN(parentSGN)
        , _type(type)
        , _editorComponent(context, type, TypeUtil::ComponentTypeToString(type))
    {
    }

    bool SGNComponent::saveCache(ByteBuffer& outputBuffer) const
    {
        outputBuffer << BYTE_BUFFER_VERSION;
        outputBuffer << uniqueID();
        return Attorney::EditorComponentSceneGraphNode::saveCache(_editorComponent, outputBuffer);
    }

    bool SGNComponent::loadCache(ByteBuffer& inputBuffer)
    {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        inputBuffer >> tempVer;
        if (tempVer != BYTE_BUFFER_VERSION)
        {
            // Older version
            return false;
        }

        U64 tempID = 0u;
        inputBuffer >> tempID;
        if (tempID != uniqueID())
        {
            // corrupt save
            return false;
        }

        return Attorney::EditorComponentSceneGraphNode::loadCache(_editorComponent, inputBuffer);
    }

    void SGNComponent::saveToXML(boost::property_tree::ptree& pt) const
    {
        Attorney::EditorComponentSceneGraphNode::saveToXML(_editorComponent, pt);
    }

    void SGNComponent::loadFromXML(const boost::property_tree::ptree& pt)
    {
        Attorney::EditorComponentSceneGraphNode::loadFromXML(_editorComponent, pt);
    }

    U64 SGNComponent::uniqueID() const
    {
        return _ID(Util::StringFormat<string>("{}_{}", _parentSGN->name().c_str(), _editorComponent.name()));
    }

    bool SGNComponent::enabled() const
    {
        return _enabled;
    }

    void SGNComponent::enabled(const bool state)
    {
        _enabled = state;
    }

    void SGNComponent::OnData([[maybe_unused]] const ECS::CustomEvent& data)
    {
       
    }

} //namespace Divide
