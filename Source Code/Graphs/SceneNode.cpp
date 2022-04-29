#include "stdafx.h"

#include "Headers/SceneNode.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Managers/Headers/SceneManager.h"

#include "ECS/Components/Headers/BoundsComponent.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

SceneNode::SceneNode(ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, const SceneNodeType type, const U32 requiredComponentMask)
    : CachedResource(ResourceType::DEFAULT, descriptorHash, name, resourceName, resourceLocation),
     _type(type),
     _editorComponent(nullptr, &parentCache->context().editor(), ComponentType::COUNT, Names::sceneNodeType[to_base(type)]),
     _parentCache(parentCache)
{
    _requiredComponentMask |= requiredComponentMask;

    getEditorComponent().onChangedCbk([this](const std::string_view field) {
        editorFieldChanged(field);
    });
}

SceneNode::~SceneNode()
{
    assert(_materialTemplate == nullptr);
}

string SceneNode::getTypeName() const {
    if (_type == SceneNodeType::TYPE_OBJECT3D) {
        const Object3D* obj = static_cast<const Object3D*>(this);
        if (obj->geometryType() != ObjectType::COUNT) {
            return TypeUtil::ObjectTypeToString(obj->geometryType());
        }
    }

    return Names::sceneNodeType[to_base(_type)];
}

void SceneNode::sceneUpdate([[maybe_unused]] const U64 deltaTimeUS,
                            [[maybe_unused]] SceneGraphNode* sgn,
                            [[maybe_unused]] SceneState& sceneState)
{
}

void SceneNode::prepareRender([[maybe_unused]] SceneGraphNode* sgn,
                              [[maybe_unused]] RenderingComponent& rComp,
                              [[maybe_unused]] const RenderStagePass renderStagePass,
                              [[maybe_unused]] const CameraSnapshot& cameraSnapshot,
                              [[maybe_unused]] bool refreshData)
{
    assert(getState() == ResourceState::RES_LOADED);
}

void SceneNode::postLoad(SceneGraphNode* sgn) {
    if (getEditorComponent().name().empty()) {
        getEditorComponent().name(getTypeName());
    }

    sgn->postLoad();
}

void SceneNode::setBounds(const BoundingBox& aabb, const vec3<F32>& worldOffset) {
    _boundsChanged = true;
    _boundingBox.set(aabb);
    _worldOffset.set(worldOffset);
}

const Material_ptr& SceneNode::getMaterialTpl() const {
    // UpgradableReadLock ur_lock(_materialLock);
    return _materialTemplate;
}

void SceneNode::setMaterialTpl(const Material_ptr& material) {
    if (material != nullptr) {  // If we need to update the material
        // UpgradableReadLock ur_lock(_materialLock);

        // If we had an old material
        if (_materialTemplate != nullptr) {
            // if the old material isn't the same as the new one
            if (_materialTemplate->getGUID() != material->getGUID()) {
                Console::printfn(Locale::Get(_ID("REPLACE_MATERIAL")),
                                 _materialTemplate->resourceName().c_str(),
                                 material->resourceName().c_str());
                _materialTemplate = material;  // set the new material
            }
        } else {
            _materialTemplate = material;  // set the new material
        }
    } else {  // if we receive a null material, the we need to remove this node's material
        _materialTemplate.reset();
    }
}

bool SceneNode::unload() {
    setMaterialTpl(nullptr);
    return true;
}

void SceneNode::editorFieldChanged([[maybe_unused]] std::string_view field) {
}

void SceneNode::buildDrawCommands([[maybe_unused]] SceneGraphNode* sgn,
                                  vector_fast<GFX::DrawCommand>& cmdsOut,
                                  PrimitiveTopology& topologyOut,
                                  [[maybe_unused]] AttributeMap& vertexFormatInOut)
{
    DIVIDE_ASSERT(cmdsOut.empty() || topologyOut != PrimitiveTopology::COUNT);
}

void SceneNode::onNetworkSend([[maybe_unused]] SceneGraphNode* sgn, [[maybe_unused]] WorldPacket& dataOut) const {
}

void SceneNode::onNetworkReceive([[maybe_unused]] SceneGraphNode* sgn, [[maybe_unused]] WorldPacket& dataIn) const {
}

bool SceneNode::saveCache(ByteBuffer& outputBuffer) const {
    outputBuffer << BYTE_BUFFER_VERSION;
    return true;
}

bool SceneNode::loadCache(ByteBuffer& inputBuffer) {
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;
    return tempVer == BYTE_BUFFER_VERSION;
}

void SceneNode::saveToXML(boost::property_tree::ptree& pt) const {
    Attorney::EditorComponentSceneGraphNode::saveToXML(getEditorComponent(), pt);
}

void SceneNode::loadFromXML(const boost::property_tree::ptree& pt) {
    Attorney::EditorComponentSceneGraphNode::loadFromXML(getEditorComponent(), pt);
}

};