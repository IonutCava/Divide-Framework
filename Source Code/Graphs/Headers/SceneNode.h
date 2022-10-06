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
#ifndef _SCENE_NODE_H_
#define _SCENE_NODE_H_

#include "SceneNodeFwd.h"
#include "SceneNodeRenderState.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Resources/Headers/Resource.h"
#include "ECS/Components/Headers/EditorComponent.h"
#include "Rendering/Camera/Headers/Frustum.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace Divide {

class Scene;
class Camera;
class Player;
class SceneGraph;
class SceneState;
class WorldPacket;
class SceneRenderState;
class BoundsSystem;
class BoundsComponent;
class RenderingComponent;
class NetworkingComponent;

class Light;
class SpotLightComponent;
class PointLightComponent;
class DirectionalLightComponent;

struct RenderPackage;
struct RenderStagePass;
struct CameraSnapshot;

namespace GFX {
    struct DrawCommand;
};

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);
FWD_DECLARE_MANAGED_CLASS(Material);

namespace Attorney {
    class SceneNodePlayer;
    class SceneNodeSceneGraph;
    class SceneNodeLightComponent;
    class SceneNodeBoundsSystem;
    class SceneNodeNetworkComponent;
};

class SceneNode : public CachedResource {
    friend class Attorney::SceneNodePlayer;
    friend class Attorney::SceneNodeSceneGraph;
    friend class Attorney::SceneNodeLightComponent;
    friend class Attorney::SceneNodeBoundsSystem;
    friend class Attorney::SceneNodeNetworkComponent;

  public:
    explicit SceneNode(ResourceCache* parentCache, size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, SceneNodeType type, U32 requiredComponentMask);
    virtual ~SceneNode();

    /// Perform any pre-draw operations POST-command build
    /// If the node isn't ready for rendering and should be skipped this frame, the return value is false
    virtual void prepareRender(SceneGraphNode* sgn,
                               RenderingComponent& rComp,
                               RenderPackage& pkg,
                               RenderStagePass renderStagePass,
                               const CameraSnapshot& cameraSnapshot,
                               bool refreshData);

    virtual void buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut);

    bool unload() override;
    virtual void setMaterialTpl(const Material_ptr& material);
    const Material_ptr& getMaterialTpl() const;

    [[nodiscard]] inline SceneNodeRenderState& renderState() noexcept { return _renderState; }
    [[nodiscard]] inline const SceneNodeRenderState& renderState() const noexcept { return _renderState; }

    [[nodiscard]] inline ResourceCache* parentResourceCache() noexcept { return _parentCache; }
    [[nodiscard]] inline const ResourceCache* parentResourceCache() const noexcept { return _parentCache; }

    [[nodiscard]] inline const BoundingBox& getBounds() const noexcept { return _boundingBox; }
    [[nodiscard]] inline const vec3<F32>& getWorldOffset() const noexcept { return _worldOffset; }

    [[nodiscard]] inline U32 requiredComponentMask() const noexcept { return _requiredComponentMask; }

    virtual bool saveCache(ByteBuffer& outputBuffer) const;
    virtual bool loadCache(ByteBuffer& inputBuffer);

    virtual void saveToXML(boost::property_tree::ptree& pt) const;
    virtual void loadFromXML(const boost::property_tree::ptree& pt);

   protected:
    /// Called from SceneGraph "sceneUpdate"
    virtual void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState);

    // Post insertion calls (Use this to setup child objects during creation)
    virtual void postLoad(SceneGraphNode* sgn);

    void setBounds(const BoundingBox& aabb, const vec3<F32>& worldOffset = {});

    [[nodiscard]] inline EditorComponent& getEditorComponent() noexcept { return _editorComponent; }
    [[nodiscard]] inline const EditorComponent& getEditorComponent() const noexcept { return _editorComponent; }

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "SceneNode"; }

    PROPERTY_RW(SceneNodeType, type, SceneNodeType::COUNT);
    PROPERTY_RW(bool, rebuildDrawCommands, false);

   protected:
     virtual void editorFieldChanged(std::string_view field);
     virtual void onNetworkSend(SceneGraphNode* sgn, WorldPacket& dataOut) const;
     virtual void onNetworkReceive(SceneGraphNode* sgn, WorldPacket& dataIn) const;

   protected:
    EditorComponent _editorComponent;
    Material_ptr _materialTemplate = nullptr;

    ResourceCache* _parentCache = nullptr;
    /// The various states needed for rendering
    SceneNodeRenderState _renderState;

    /// The initial bounding box as it was at object's creation (i.e. no transforms applied)
    BoundingBox _boundingBox{};
    vec3<F32> _worldOffset{};
    bool _boundsChanged = false;

private:
    U32 _requiredComponentMask = to_U32(ComponentType::TRANSFORM);
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(SceneNode);

namespace Attorney {
class SceneNodeSceneGraph {
    static void postLoad(SceneNode* node, SceneGraphNode* sgn) {
        node->postLoad(sgn);
    }

    static void sceneUpdate(SceneNode* node, const U64 deltaTimeUS,
                            SceneGraphNode* sgn, SceneState& sceneState) {
        PROFILE_SCOPE();
        node->sceneUpdate(deltaTimeUS, sgn, sceneState);
    }

    static EditorComponent& getEditorComponent(SceneNode* node) noexcept {
        return node->_editorComponent;
    }

    friend class Divide::SceneGraph;
    friend class Divide::SceneGraphNode;
};

class SceneNodeNetworkComponent {
    static void onNetworkSend(SceneGraphNode* sgn, const SceneNode& node, WorldPacket& dataOut) {
        node.onNetworkSend(sgn, dataOut);
    }

    static void onNetworkReceive(SceneGraphNode* sgn, const SceneNode& node, WorldPacket& dataIn) {
        node.onNetworkReceive(sgn, dataIn);
    }

    friend class Divide::NetworkingComponent;
};

class SceneNodeBoundsSystem {
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const vec3<F32>& worldOffset = {}) {
        node.setBounds(aabb, worldOffset);
    }

    static bool boundsChanged(const SceneNode& node) noexcept {
        return node._boundsChanged;
    }

    static void setBoundsChanged(SceneNode& node) noexcept {
        node._boundsChanged = true;
    }

    static bool clearBoundsChanged(SceneNode& node) noexcept {
        if (!node._boundsChanged) {
            return false;
        }

        node._boundsChanged = false;
        return true;
    }

    friend class Divide::BoundsSystem;
};

class SceneNodeLightComponent {
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const vec3<F32>& worldOffset = {}) {
        node.setBounds(aabb, worldOffset);
    }

    friend class Divide::Light;
    friend class Divide::SpotLightComponent;
    friend class Divide::PointLightComponent;
    friend class Divide::DirectionalLightComponent;
};

class SceneNodePlayer {
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const vec3<F32>& worldOffset = {}) {
        node.setBounds(aabb, worldOffset);
    }

    friend class Divide::Player;
};

};  // namespace Attorney
};  // namespace Divide
#endif
