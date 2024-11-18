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
#ifndef DVD_SCENE_NODE_H_
#define DVD_SCENE_NODE_H_

#include "SceneNodeFwd.h"
#include "SceneNodeRenderState.h"

#include "Core/Headers/Profiler.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Resources/Headers/Resource.h"
#include "ECS/Components/Headers/EditorComponent.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"

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

namespace GFX
{
    struct DrawCommand;
    struct MemoryBarrierCommand;
};

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);
FWD_DECLARE_MANAGED_CLASS(Material);

namespace Attorney
{
    class SceneNodePlayer;
    class SceneNodeSceneGraph;
    class SceneNodeLightComponent;
    class SceneNodeBoundsSystem;
    class SceneNodeNetworkComponent;
};

class SceneNode : public CachedResource 
{
    friend class Attorney::SceneNodePlayer;
    friend class Attorney::SceneNodeSceneGraph;
    friend class Attorney::SceneNodeLightComponent;
    friend class Attorney::SceneNodeBoundsSystem;
    friend class Attorney::SceneNodeNetworkComponent;

  public:
    explicit SceneNode( const ResourceDescriptorBase& descriptor, SceneNodeType type, U32 requiredComponentMask );

    virtual ~SceneNode() override;

    /// Perform any pre-draw operations POST-command build
    /// If the node isn't ready for rendering and should be skipped this frame, the return value is false
    virtual void prepareRender(SceneGraphNode* sgn,
                               RenderingComponent& rComp,
                               RenderPackage& pkg,
                               GFX::MemoryBarrierCommand& postDrawMemCmd,
                               RenderStagePass renderStagePass,
                               const CameraSnapshot& cameraSnapshot,
                               bool refreshData);

    virtual void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut);

    bool load( PlatformContext& context ) override;
    bool postLoad() override;
    bool unload() override;
    virtual void setMaterialTpl( Handle<Material> material);
    Handle<Material> getMaterialTpl() const;

    [[nodiscard]] inline SceneNodeRenderState& renderState() noexcept { return _renderState; }
    [[nodiscard]] inline const SceneNodeRenderState& renderState() const noexcept { return _renderState; }

    [[nodiscard]] inline const BoundingBox& getBounds() const noexcept { return _boundingBox; }
    [[nodiscard]] inline const float3& getWorldOffset() const noexcept { return _worldOffset; }

    [[nodiscard]] inline U32 requiredComponentMask() const noexcept { return _requiredComponentMask; }

    virtual bool saveCache(ByteBuffer& outputBuffer) const;
    virtual bool loadCache(ByteBuffer& inputBuffer);

    virtual void saveToXML(boost::property_tree::ptree& pt) const;
    virtual void loadFromXML(const boost::property_tree::ptree& pt);

    EditorComponent* editorComponent() const noexcept { return _editorComponent.get(); }

   protected:
    /// Called from SceneGraph "sceneUpdate"
    virtual void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState);

    // Post insertion calls (Use this to setup child objects during creation)
    virtual void postLoad(SceneGraphNode* sgn);

    void setBounds(const BoundingBox& aabb, const float3& worldOffset = {});

    void registerEditorComponent( PlatformContext& context );

    PROPERTY_R(SceneNodeType, type, SceneNodeType::COUNT);
    PROPERTY_RW(bool, rebuildDrawCommands, false);

   protected:
     virtual void onNetworkSend(SceneGraphNode* sgn, WorldPacket& dataOut) const;
     virtual void onNetworkReceive(SceneGraphNode* sgn, WorldPacket& dataIn) const;

   protected:
    std::unique_ptr<EditorComponent> _editorComponent;
    Handle<Material> _materialTemplate = INVALID_HANDLE<Material>;

    /// The various states needed for rendering
    SceneNodeRenderState _renderState;

    /// The initial bounding box as it was at object's creation (i.e. no transforms applied)
    BoundingBox _boundingBox{};
    float3 _worldOffset{};
    bool _boundsChanged = false;

private:
    U32 _requiredComponentMask = to_U32(ComponentType::TRANSFORM);
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(SceneNode);

DEFINE_NODE_TYPE(TransformNode, SceneNodeType::TYPE_TRANSFORM)
{
   public:
    explicit TransformNode( const ResourceDescriptor<TransformNode>& descriptor );
};

namespace Attorney {
class SceneNodeSceneGraph {
    static void postLoad(SceneNode* node, SceneGraphNode* sgn)
    {
        node->postLoad(sgn);
    }

    static void sceneUpdate(SceneNode* node, const U64 deltaTimeUS,
                            SceneGraphNode* sgn, SceneState& sceneState)
    {
        node->sceneUpdate(deltaTimeUS, sgn, sceneState);
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
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const float3& worldOffset = {}) {
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
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const float3& worldOffset = {}) {
        node.setBounds(aabb, worldOffset);
    }

    friend class Divide::Light;
    friend class Divide::SpotLightComponent;
    friend class Divide::PointLightComponent;
    friend class Divide::DirectionalLightComponent;
};

class SceneNodePlayer {
    static void setBounds(SceneNode& node, const BoundingBox& aabb, const float3& worldOffset = {}) {
        node.setBounds(aabb, worldOffset);
    }

    friend class Divide::Player;
};

};  // namespace Attorney
};  // namespace Divide

#endif //DVD_SCENE_NODE_H_

