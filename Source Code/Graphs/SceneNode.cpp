#include "stdafx.h"

#include "Headers/SceneNode.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Managers/Headers/SceneManager.h"

#include "ECS/Components/Headers/BoundsComponent.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

SceneNode::SceneNode(ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, const SceneNodeType type, const U32 requiredComponentMask)
    : CachedResource(ResourceType::DEFAULT, descriptorHash, name, resourceName, resourceLocation),
     _type(type),
     _editorComponent(Names::sceneNodeType[to_base(type)]),
     _parentCache(parentCache)
{
    std::atomic_init(&_sgnParentCount, 0);
    _requiredComponentMask |= requiredComponentMask;

    _boundingBox.setMin(-1.0f);
    _boundingBox.setMax(1.0f);

    getEditorComponent().onChangedCbk([this](const std::string_view field) {
        editorFieldChanged(field);
    });
}

stringImpl SceneNode::getTypeName() const {
    if (_type == SceneNodeType::TYPE_OBJECT3D) {
        const Object3D* obj = static_cast<const Object3D*>(this);
        if (obj->getObjectType() != ObjectType::COUNT) {
            return TypeUtil::ObjectTypeToString(obj->getObjectType());
        }
    }

    return Names::sceneNodeType[to_base(_type)];
}

void SceneNode::sceneUpdate(const U64 deltaTimeUS,
                            SceneGraphNode* sgn,
                            SceneState& sceneState) {
    ACKNOWLEDGE_UNUSED(deltaTimeUS);
    ACKNOWLEDGE_UNUSED(sgn);
    ACKNOWLEDGE_UNUSED(sceneState);
}

void SceneNode::prepareRender(SceneGraphNode* sgn,
                              RenderingComponent& rComp,
                              const RenderStagePass& renderStagePass,
                              const Camera& camera,
                              bool refreshData) {
    ACKNOWLEDGE_UNUSED(sgn);
    ACKNOWLEDGE_UNUSED(rComp);
    ACKNOWLEDGE_UNUSED(renderStagePass);
    ACKNOWLEDGE_UNUSED(camera);
    ACKNOWLEDGE_UNUSED(refreshData);

    assert(getState() == ResourceState::RES_LOADED);
}


void SceneNode::occlusionCull(const RenderStagePass& stagePass,
                              const Texture_ptr& depthBuffer,
                              const Camera& camera,
                              GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                              GFX::CommandBuffer& bufferInOut) const {
    ACKNOWLEDGE_UNUSED(stagePass);
    ACKNOWLEDGE_UNUSED(depthBuffer);
    ACKNOWLEDGE_UNUSED(camera);
    ACKNOWLEDGE_UNUSED(HIZPushConstantsCMDInOut);
    ACKNOWLEDGE_UNUSED(bufferInOut);
}

void SceneNode::postLoad(SceneGraphNode* sgn) {
    if (getEditorComponent().name().empty()) {
        getEditorComponent().name(getTypeName());
    }

    sgn->postLoad();
}

void SceneNode::setBounds(const BoundingBox& aabb) {
    _boundsChanged = true;
    _boundingBox.set(aabb);
}

const Material_ptr& SceneNode::getMaterialTpl() const {
    // UpgradableReadLock ur_lock(_materialLock);
    return _materialTemplate;
}

void SceneNode::setMaterialTpl(const Material_ptr& material) {
    if (material) {  // If we need to update the material
        // UpgradableReadLock ur_lock(_materialLock);

        // If we had an old material
        if (_materialTemplate) {  
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

void SceneNode::editorFieldChanged(std::string_view field) {
    ACKNOWLEDGE_UNUSED(field);
}

void SceneNode::buildDrawCommands(SceneGraphNode* sgn,
                                  const RenderStagePass& renderStagePass,
                                  const Camera& crtCamera,
                                  RenderPackage& pkgInOut) {
    ACKNOWLEDGE_UNUSED(sgn);
    ACKNOWLEDGE_UNUSED(renderStagePass);
    ACKNOWLEDGE_UNUSED(crtCamera);
    ACKNOWLEDGE_UNUSED(pkgInOut);
}

void SceneNode::onNetworkSend(SceneGraphNode* sgn, WorldPacket& dataOut) const {
    ACKNOWLEDGE_UNUSED(sgn);
    ACKNOWLEDGE_UNUSED(dataOut);
}

void SceneNode::onNetworkReceive(SceneGraphNode* sgn, WorldPacket& dataIn) const {
    ACKNOWLEDGE_UNUSED(sgn);
    ACKNOWLEDGE_UNUSED(dataIn);
}

bool SceneNode::saveCache(ByteBuffer& outputBuffer) const {
    outputBuffer << BYTE_BUFFER_VERSION;
    return true;
}

bool SceneNode::loadCache(ByteBuffer& inputBuffer) {
    U16 tempVer = 0u;
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