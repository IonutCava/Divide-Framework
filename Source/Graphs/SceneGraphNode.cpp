// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/SceneGraphNode.h"
#include "Headers/SceneGraph.h"

#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"
#include "Geometry/Shapes/Headers/SubMesh.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Water/Headers/Water.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Scenes/Headers/SceneState.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Editor/Headers/Editor.h"

#include "ECS/Systems/Headers/ECSManager.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/NetworkingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"


namespace Divide {

namespace
{
    constexpr U16 BYTE_BUFFER_VERSION = 2u;

    FORCE_INLINE bool PropagateFlagToChildren(const SceneGraphNode::Flags flag) noexcept
    {
        return flag == SceneGraphNode::Flags::SELECTED || 
               flag == SceneGraphNode::Flags::HOVERED ||
               flag == SceneGraphNode::Flags::ACTIVE ||
               flag == SceneGraphNode::Flags::VISIBILITY_LOCKED;
    }
};

ECS::ECSEngine& SceneGraphNode::GetECSEngine() const noexcept
{
    return _sceneGraph->GetECSEngine();
}

void SceneGraphNode::AddComponents(const U32 componentMask, const bool allowDuplicates)
{
    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i)
    {
        const U32 componentBit = 1 << i;

        // Only add new components;
        if ((componentMask & componentBit) && (allowDuplicates || !(_componentMask & componentBit)))
        {
            _componentMask |= componentBit;
            SGNComponent::construct(static_cast<ComponentType>(componentBit), this);
        }
    };
}

void SceneGraphNode::AddSGNComponentInternal(SGNComponent* comp)
{
    Hacks._editorComponents.emplace_back(&comp->editorComponent());

    _componentMask |= to_U32(comp->type());

    if (comp->type() == ComponentType::TRANSFORM)
    {   //Ewww
        Hacks._transformComponentCache = (TransformComponent*)comp;
    }
    if (comp->type() == ComponentType::BOUNDS)
    {   //Ewww x2
        Hacks._boundsComponentCache = (BoundsComponent*)comp;
    }
    if (comp->type() == ComponentType::RENDERING)
    {   //Ewww x3
        Hacks._renderingComponent = (RenderingComponent*)comp;
    }
    if (comp->type() == ComponentType::ANIMATION || comp->type() == ComponentType::RENDERING)
    {
        sceneGraph()->invalidateWorldAO();
    }
}

void SceneGraphNode::RemoveSGNComponentInternal(SGNComponent* comp)
{
    if (comp == nullptr)
    {
        return;
    }

    const I64 targetGUID = comp->editorComponent().getGUID();

    Hacks._editorComponents.erase(std::remove_if(std::begin(Hacks._editorComponents),
                                                 std::end(Hacks._editorComponents),
                                                 [targetGUID](EditorComponent* editorComp) noexcept -> bool
                                                 {
                                                     return editorComp->getGUID() == targetGUID;
                                                 }),
                                  std::end(Hacks._editorComponents));

    _componentMask &= ~to_U32(comp->type());

    if (comp->type() == ComponentType::TRANSFORM)
    {
        Hacks._transformComponentCache = nullptr;
    }
    if (comp->type() == ComponentType::BOUNDS)
    {
        Hacks._boundsComponentCache = nullptr;
    }
    if (comp->type() == ComponentType::RENDERING)
    {
        Hacks._renderingComponent = nullptr;
    }
    if (comp->type() == ComponentType::ANIMATION || comp->type() == ComponentType::RENDERING)
    {
        sceneGraph()->invalidateWorldAO();
    }
}

void SceneGraphNode::RemoveComponents(const U32 componentMask)
{
    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i)
    {
        const U32 componentBit = 1 << i;
        if ((componentMask & componentBit) && (_componentMask & componentBit))
        {
            SGNComponent::destruct(static_cast<ComponentType>(componentBit), this);
        }
    }
}

bool SceneGraphNode::HasComponents(const ComponentType componentType) const
{
    return HasComponents(to_base(componentType));
}

bool SceneGraphNode::HasComponents(const U32 componentMaskIn) const
{
    return componentMask() & componentMaskIn;
}

SceneGraphNode::SceneGraphNode( PlatformContext& context, SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor )
    : PlatformContextComponent( context )
    , _relationshipCache( this )
    , _sceneGraph( sceneGraph )
    , _name( descriptor._name )
    , _instanceCount( to_U32( descriptor._instanceCount ) )
    , _serialize( descriptor._serialize )
    , _usageContext( descriptor._usageContext )
    , _descriptor( descriptor )
{
    DIVIDE_ASSERT( descriptor._nodeHandle._handle != INVALID_HANDLE<SceneNode> );
    _node = descriptor._nodeHandle._nodePtr;
    DIVIDE_ASSERT( _node != nullptr );

    if ( _name.empty() )
    {
        if ( _node->resourceName().empty() )
        {
            Util::StringFormatTo( _name, "{}_SGN", getGUID() );
        }
        else
        {
            Util::StringFormatTo( _name, "{}_SGN", _node->resourceName().c_str() );
        }
    }

    _nameHash = _ID( _name );

    setFlag( Flags::ACTIVE );
    clearFlag( Flags::VISIBILITY_LOCKED );

    const U32 dynamicNodeComponents = to_base( ComponentType::ANIMATION ) |
                                      to_base( ComponentType::INVERSE_KINEMATICS ) |
                                      to_base( ComponentType::RAGDOLL );

    if ( descriptor._componentMask & dynamicNodeComponents )
    {
        _usageContext = NodeUsageContext::NODE_DYNAMIC;
    }

    AddComponents( descriptor._componentMask, false );
    AddComponents( _node->requiredComponentMask(), false );
    if ( IsTransformNode( _descriptor._nodeHandle._type ) )
    {
        setFlag( Flags::IS_CONTAINER );
    }
}

/// If we are destroying the current graph node
SceneGraphNode::~SceneGraphNode()
{
    Console::printfn( LOCALE_STR( "REMOVE_SCENEGRAPH_NODE" ), name().c_str(), _node->resourceName().c_str() );

    Attorney::SceneGraphSGN::onNodeDestroy( _sceneGraph, this );
    // Bottom up
    {
        LockGuard<SharedMutex> w_lock( _children._lock );
        const U32 childCount = _children._count;
        for ( U32 i = 0u; i < childCount; ++i )
        {
            _sceneGraph->destroySceneGraphNode( _children._data[i] );
        }
        _children._data.clear();
        //_children._count.store(0u);
    }

    if( _descriptor._nodeHandle._deleter )
    {
        _descriptor._nodeHandle._deleter();
    }
    else
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    RemoveAllComponents();
}

void SceneGraphNode::changeUsageContext(const NodeUsageContext& newContext)
{
    if (_usageContext != newContext)
    {
        const bool previousStatic = _usageContext == NodeUsageContext::NODE_STATIC;
        _sceneGraph->invalidateStaticGeometryBounds(*this);
        _usageContext = newContext;
        _sceneGraph->invalidateStaticGeometryBounds(*this);
        const BoundsComponent* bounds = get<BoundsComponent>();
        if (bounds != nullptr)
        {
            _sceneGraph->invalidateShadowBounds(*this, bounds->getBoundingSphere(), previousStatic);
        }
    }

    TransformComponent* tComp = get<TransformComponent>();
    if (tComp)
    {
        Attorney::TransformComponentSGN::onParentUsageChanged(*tComp, _usageContext);
    }

    const RenderingComponent* rComp = get<RenderingComponent>();
    if (rComp)
    {
        Attorney::RenderingComponentSGN::onParentUsageChanged(*rComp, _usageContext);
    }
}

/// Change current SceneGraphNode's parent
void SceneGraphNode::setParent(SceneGraphNode* parent, const bool defer)
{
    if (parent == nullptr)
    {
        Console::errorfn("Cannot reparent scene node {} to a null parent", name().c_str());
        return;
    }
    _queuedNewParent = parent->getGUID();
    if (!defer)
    {
        setParentInternal();
    }
    else
    {
        Attorney::SceneGraphSGN::onNodeParentChange(sceneGraph(), this);
    }
}

void SceneGraphNode::setParentInternal()
{
    if (_queuedNewParent == -1)
    {
        return;
    }

    SceneGraphNode* newParent = sceneGraph()->findNode(_queuedNewParent);
    _queuedNewParent = -1;

    if (newParent == nullptr)
    {
        Console::warnfn("Cannot reparent scene node {}: the requested parent no longer exists", name().c_str());
        return;
    }

    for (const SceneGraphNode* ancestor = newParent; ancestor != nullptr; ancestor = ancestor->parent())
    {
        if (ancestor == this)
        {
            Console::errorfn("Cannot reparent scene node {} to itself or one of its descendants", name().c_str());
            return;
        }
    }
    if (_parent == newParent)
    {
        return;
    }

    TransformComponent* transform = get<TransformComponent>();
    TransformValues localTransform;
    const bool preserveTransform = Attorney::TransformComponentSGN::prepareParentChange(
        *transform, _parent, newParent, localTransform);
    if (!preserveTransform)
    {
        if (_parent != nullptr)
        {
            Console::errorfn("Cannot preserve the transform of scene node {} under parent {}",
                             name().c_str(), newParent->name().c_str());
            return;
        }
        Console::warnfn("Cannot preserve the initial transform of scene node {} under parent {}; keeping its local transform",
                        name().c_str(), newParent->name().c_str());
    }

    SceneGraphNode* oldParent = _parent;
    if (oldParent != nullptr && !oldParent->removeChildNode(this, false, false))
    {
        Console::errorfn("Cannot reparent scene node {}: its current parent does not contain it", name().c_str());
        return;
    }
    // Set the parent pointer to the new parent
    _parent = newParent;
    
    {// Add ourselves in the new parent's children map
        {
            LockGuard<SharedMutex> w_lock(_parent->_children._lock);
            _parent->_children._data.push_back(this);
            _parent->_children._count += 1u;
        }
        Attorney::SceneGraphSGN::onNodeAdd(_sceneGraph, this, oldParent != nullptr);
        _parent->invalidateAggregateBounds();
        // That's it. Parent Transforms will be updated in the next render pass;
        _parent->invalidateRelationshipCache();
    }
    {// Carry over new parent's flags and settings
        constexpr Flags flags[] = { Flags::SELECTED, Flags::HOVERED, Flags::ACTIVE, Flags::VISIBILITY_LOCKED };
        for (const Flags flag : flags)
        {
            if (_parent->hasFlag(flag))
            {
                setFlag(flag);
            }
            else
            {
                clearFlag(flag);
            }
        }

        // Dynamic > Static. Not the other way around (e.g. Root is static. Terrain is static, etc)
        if (_parent->usageContext() == NodeUsageContext::NODE_DYNAMIC)
        {
            changeUsageContext(_parent->usageContext());
        }
    }
    if (preserveTransform)
    {
        transform->setTransform(localTransform);
    }
}

/// Add a new SceneGraphNode to the current node's child list based on a SceneNode
SceneGraphNode* SceneGraphNode::addChildNode( SceneGraphNode* sgn)
{
    // Set the current node as the new node's parent
    sgn->setParent(this);

    if ( sgn->_node->getState() == ResourceState::RES_LOADED)
    {
        PostLoad( sgn->_node, sgn );
    }
    else if ( sgn->_node->getState() == ResourceState::RES_LOADING)
    {
        setFlag(Flags::LOADING);
    }

    // return the newly created node
    return sgn;
}

void SceneGraphNode::PostLoad(SceneNode* sceneNode, SceneGraphNode* sgn)
{
    Attorney::SceneNodeSceneGraph::postLoad(sceneNode, sgn);

    if (!sgn->_relationshipCache.isValid())
    {
        sgn->_relationshipCache.rebuild();
    }
}

bool SceneGraphNode::removeNodesByType(SceneNodeType nodeType)
{
    // Bottom-up pattern
    U32 removalCount = 0, childRemovalCount = 0;

    SharedLock<SharedMutex> r_lock(_children._lock);
    for (U32 i = 0u; i < _children._count; ++i)
    {
        if (_children._data[i]->removeNodesByType(nodeType))
        {
            ++childRemovalCount;
        }
    }

    for (U32 i = 0u; i < _children._count; ++i)
    {
        if (_children._data[i]->getNode().type() == nodeType)
        {
            _sceneGraph->addToDeleteQueue(_children._data[i]->getGUID());
            ++removalCount;
        }
    }

    if (removalCount > 0)
    {
        return true;
    }

    return childRemovalCount > 0;
}

bool SceneGraphNode::removeChildNode(const SceneGraphNode* node, const bool recursive, bool deleteNode)
{
    if (node == nullptr)
    {
        return false;
    }

    const I64 targetGUID = node->getGUID();
    if (deleteNode)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        for (SceneGraphNode* child : _children._data)
        {
            if (child->getGUID() == targetGUID)
            {
                _sceneGraph->addToDeleteQueue(targetGUID);
                return true;
            }
        }
    }
    else
    {
        bool removed = false;
        {
            LockGuard<SharedMutex> w_lock(_children._lock);
            for (auto it = _children._data.begin(); it != _children._data.end(); ++it)
            {
                if ((*it)->getGUID() == targetGUID)
                {
                    _children._data.erase(it);
                    _children._count = to_U32(_children._data.size());
                    removed = true;
                    break;
                }
            }
        }
        if (removed)
        {
            invalidateAggregateBounds();
            invalidateRelationshipCache();
            return true;
        }
    }

    if (recursive)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        for (U32 i = 0u; i < _children._count; ++i)
        {
            if (_children._data[i]->removeChildNode(node, true, deleteNode))
            {
                return false;
            }
        }

        return false;
    }

    return true;
}

void SceneGraphNode::postLoad()
{
    SendEvent(
    {
        ._type = ECS::CustomEvent::Type::EntityPostLoad
    });
}

bool SceneGraphNode::isRelated(const SceneGraphNode* target) const
{
    // We also ignore grandparents as this will usually be the root;
    if (_relationshipCache.isValid())
    {
        return _relationshipCache.classifyNode( target->getGUID() ) != SGNRelationshipCache::RelationshipType::COUNT;
    }

    return false;
}

bool SceneGraphNode::isRelated( const SceneGraphNode* target, const SGNRelationshipCache::RelationshipType relationship ) const
{
    // We also ignore grandparents as this will usually be the root;
    if ( _relationshipCache.isValid() )
    {
        return _relationshipCache.validateRelationship( target->getGUID(), relationship );
    }

    return false;
}

bool SceneGraphNode::isChild(const SceneGraphNode* target, const bool recursive) const
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    const I64 targetGUID = target->getGUID();

    const SGNRelationshipCache::RelationshipType type = _relationshipCache.classifyNode(targetGUID);
    if (type == SGNRelationshipCache::RelationshipType::GRANDCHILD && recursive)
    {
        return true;
    }

    return type == SGNRelationshipCache::RelationshipType::CHILD;
}

SceneGraphNode* SceneGraphNode::findChild(const U64 nameHash, const bool sceneNodeName, const bool recursive) const
{
    return sceneNodeName ? findChildBySceneNodeName(nameHash, recursive)
                         : findChildByGraphNodeName(nameHash, recursive);
}

SceneGraphNode* SceneGraphNode::findChild(const I64 GUID, const bool sceneNodeGuid, const bool recursive) const
{
    return sceneNodeGuid ? findChildBySceneNodeGUID(GUID, recursive)
                         : findChildByGraphNodeGUID(GUID, recursive);
}

SceneGraphNode* SceneGraphNode::findChildBySceneNodeName(const U64 nameHash, const bool recursive) const
{
    if (nameHash != 0u)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);

        for (SceneGraphNode* child : _children._data)
        {
            const U64 cmpHash = _ID(child->getNode().resourceName().c_str());
            if (cmpHash == nameHash)
            {
                return child;
            }

            if (recursive)
            {
                SceneGraphNode* recChild = child->findChildBySceneNodeName(nameHash, true);

                if (recChild != nullptr)
                {
                    return recChild;
                }
            }
        }
    }

    return nullptr;
}

SceneGraphNode* SceneGraphNode::findChildBySceneNodeGUID(const I64 GUID, const bool recursive) const
{
    if (GUID != -1)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        for (SceneGraphNode* child : _children._data)
        {
            if (child->getNode().getGUID() == GUID)
            {
                return child;
            }

            if (recursive)
            {
                SceneGraphNode* recChild = child->findChildBySceneNodeGUID(GUID, true);
                if (recChild != nullptr)
                {
                    return recChild;
                }
            }
        }
    }

    return nullptr;
}

SceneGraphNode* SceneGraphNode::findChildByGraphNodeName(const U64 nameHash, const bool recursive) const
{
    if (nameHash != 0u)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);

        for (SceneGraphNode* child : _children._data)
        {
            if (child->nameHash() == nameHash)
            {
                return child;
            }

            if (recursive)
            {
                SceneGraphNode* recChild = child->findChildByGraphNodeName(nameHash, true);

                if (recChild != nullptr)
                {
                    return recChild;
                }
            }
        }
    }

    return nullptr;
}

SceneGraphNode* SceneGraphNode::findChildByGraphNodeGUID(const I64 GUID, const bool recursive) const
{
    if (GUID != -1)
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        for (SceneGraphNode* child : _children._data)
        {
            if (child->getGUID() == GUID)
            {
                return child;
            }

            if (recursive)
            {
                SceneGraphNode* recChild = child->findChildByGraphNodeGUID(GUID, true);
                if (recChild != nullptr)
                {
                    return recChild;
                }
            }
        }
    }

    return nullptr;
}


bool SceneGraphNode::intersect(const IntersectionRay& ray, const float2 range, vector<SGNRayResult>& intersections) const
{
    vector<SGNRayResult> ret{};

    // Root has its own intersection routine, so we ignore it
    if (_sceneGraph->getRoot()->getGUID() == this->getGUID())
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            _children._data[i]->intersect(ray, range, intersections);
        }
    }
    else
    {
        // If we hit a bounding sphere, we proceed to the more expensive OBB test
        if (HasComponents(ComponentType::BOUNDS) && get<BoundsComponent>()->getBoundingSphere().intersect(ray, range.min, range.max).hit)
        {
            const RayResult result = get<BoundsComponent>()->getOBB().intersect(ray, range.min, range.max);
            if (result.hit)
            {
                intersections.push_back({ getGUID(), result.dist, result.inside, name().c_str() });

                SharedLock<SharedMutex> r_lock(_children._lock);
                const U32 childCount = _children._count;
                for (U32 i = 0u; i < childCount; ++i)
                {
                    _children._data[i]->intersect(ray, range, intersections);
                };
            }
        }
    }

    return !intersections.empty();
}

void SceneGraphNode::getAllNodes(vector<SceneGraphNode*>& nodeList)
{
    // Compute from leaf to root to ensure proper calculations
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            _children._data[i]->getAllNodes(nodeList);
        }
    }

    nodeList.push_back(this);
}

void SceneGraphNode::destroyChild(const I64 guid)
{
    bool removed = false;
    {
        LockGuard<SharedMutex> w_lock(_children._lock);
        for (auto it = _children._data.begin(); it != _children._data.end(); ++it)
        {
            if ((*it)->getGUID() == guid)
            {
                _sceneGraph->destroySceneGraphNode(*it);
                if (*it == nullptr)
                {
                    _children._data.erase(it);
                    _children._count = to_U32(_children._data.size());
                    removed = true;
                }
                break;
            }
        }
    }
    if (removed)
    {
        invalidateAggregateBounds();
        invalidateRelationshipCache();
    }
}

void SceneGraphNode::invalidateAggregateBounds()
{
    MarkAncestorBoundsChanged(this,
        [](SceneGraphNode& node) { return node.parent(); },
        [](SceneGraphNode& node) { Attorney::SceneNodeBoundsSystem::setBoundsChanged(node.getNode()); });
}

// Please call in MAIN THREAD! Nothing is thread safe here (for now) -Ionut
void SceneGraphNode::sceneUpdate(const U64 deltaTimeUS, SceneState& sceneState)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (hasFlag(Flags::ACTIVE))
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::GameLogic);
        Attorney::SceneNodeSceneGraph::sceneUpdate(_node, deltaTimeUS, this, sceneState);
    }
    else if (hasFlag(Flags::LOADING))
    {
        if (_node->getState() == ResourceState::RES_LOADED)
        {
            PostLoad( _node, this );
            clearFlag( Flags::LOADING );
        }
    }

    clearFlag(Flags::PARENT_POST_RENDERED);
}

void SceneGraphNode::processEvents()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    _events.drain([this](const ECS::CustomEvent& evt)
    {
        switch (evt._type)
        {
            case ECS::CustomEvent::Type::RelationshipCacheInvalidated:
            {
                PROFILE_SCOPE("RelationshipCacheInvalidated", Profiler::Category::Scene );
                if (!_relationshipCache.isValid()) {
                    _relationshipCache.rebuild();
                }
            } break;
            case ECS::CustomEvent::Type::EntityFlagChanged:
            {
                PROFILE_SCOPE("EntityFlagChanged", Profiler::Category::Scene );
                if ( static_cast<Flags>(evt._flag) == Flags::VEGETATION_TREE ||
                     static_cast<Flags>(evt._flag) == Flags::IS_CONTAINER )
                {
                    sceneGraph()->invalidateWorldAO();
                }
                else if ( static_cast<Flags>(evt._flag) == Flags::ACTIVE ||
                          static_cast<Flags>(evt._flag) == Flags::LOADING )
                {
                    sceneGraph()->invalidateWorldAO();
                }
                if (static_cast<Flags>(evt._flag) == Flags::SELECTED)
                {
                    RenderingComponent* rComp = get<RenderingComponent>();
                    if (rComp != nullptr)
                    {
                        bool state = false;
                        SelectionComponent* sComp = get<SelectionComponent>();
                        if ( sComp == nullptr || sComp->selectionWidgetEnabled())
                        {
                            state = evt._dataPair._first == 1u;
                        }

                        const bool recursive = evt._dataPair._second == 1u;
                        rComp->toggleRenderOption(RenderingComponent::RenderOptions::RENDER_SELECTION, state, recursive);
                    }
                }
            } break;
            case ECS::CustomEvent::Type::NewShaderReady:
            {
                PROFILE_SCOPE("NewShaderReady", Profiler::Category::Scene );
                Attorney::SceneGraphSGN::onNodeShaderReady(sceneGraph(), *this);
            } break;
            case ECS::CustomEvent::Type::TransformUpdated:
            {
                PROFILE_SCOPE("TransformUpdated", Profiler::Category::Scene );
                Attorney::SceneGraphSGN::onNodeMoved(sceneGraph(), *this);
                Attorney::SceneGraphSGN::onNodeSpatialChange(sceneGraph(), *this);
            } break;
            case ECS::CustomEvent::Type::AnimationUpdated:
            case ECS::CustomEvent::Type::BoundsUpdated:
            {
                PROFILE_SCOPE("onNodeSpatialChange", Profiler::Category::Scene );
                Attorney::SceneGraphSGN::onNodeSpatialChange(sceneGraph(), *this);
            } break;
            case ECS::CustomEvent::Type::AnimationChanged:
            case ECS::CustomEvent::Type::AnimationReSync:
            {
                if (getNode().type() == SceneNodeType::TYPE_SUBMESH)
                {
                    if ( evt._type == ECS::CustomEvent::Type::AnimationChanged)
                    {
                        Attorney::SubMeshMeshSceneGraphNode::onAnimationChange(getNode<SubMesh>(), this, evt._flag, evt._dataPair._first == 1u, evt._dataPair._second == 1u);
                    }
                    else
                    {
                        Attorney::SubMeshMeshSceneGraphNode::onAnimationSync(getNode<SubMesh>(), this, evt._flag, evt._data == 1u);
                    }
                }

                Attorney::SceneGraphSGN::onNodeSpatialChange(sceneGraph(), *this);
            } break;
            default: break;
        }
        {
            PROFILE_SCOPE("PassDataToAllComponents", Profiler::Category::Scene );
            PassDataToAllComponents(evt);
        }
    });
}

void SceneGraphNode::prepareRender( RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    const RenderStagePass& renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    const bool refreshData)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (HasComponents(ComponentType::ANIMATION))
    {
        ShaderBuffer* boneBuffer = get<AnimationComponent>()->getBoneBuffer();
        // We always bind a bone buffer if we have animation data available as the shaders will expect the data to be there
        if(boneBuffer != nullptr)
        {
            DescriptorSet& set = pkg.descriptorSetCmd()._set;

            DescriptorSetBinding *boneEntry = nullptr;
            for ( U8 i = 0u; i < set._bindingCount; ++i )
            {
                DescriptorSetBinding& entry = set._bindings[i];

                if (entry._slot == 13u )
                {
                    boneEntry = &entry;
                    break;
                }
            }

            if (!boneEntry)
            {
                boneEntry = &AddBinding( set, 13u, ShaderStageVisibility::VERTEX );
            }

            Set(boneEntry->_data, boneBuffer, {0u, boneBuffer->getPrimitiveCount()});
        }
    }

    _node->prepareRender(this, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}

void SceneGraphNode::onNetworkSend(U32 frameCount) const
{
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            _children._data[i]->onNetworkSend(frameCount);
        }
    }
    NetworkingComponent* net = get<NetworkingComponent>();
    if (net)
    {
        net->onNetworkSend(frameCount);
    }
}

bool SceneGraphNode::canDraw(const RenderStagePass& stagePass) const
{
    RenderingComponent* rComp = get<RenderingComponent>();
    return rComp != nullptr && rComp->canDraw(stagePass);
}

namespace
{
    [[nodiscard]] bool FilterCheck(const U32 renderFilter, const SceneNode& node) noexcept
    {
        if ( renderFilter == 0u )
        {
            return true;
        }

        bool ret = true;

        if ( (renderFilter & to_base(RenderPassCuller::EntityFilter::PRIMITIVES)) && IsPrimitive( node.type() ))
        {
            ret = false;
        }
        if ( (renderFilter & to_base(RenderPassCuller::EntityFilter::MESHES)) && IsMesh( node.type() ) )
        {
            ret = false;
        }

        if ( ret )
        {
            switch ( node.type() )
            {
                default: break;
                case SceneNodeType::TYPE_TERRAIN:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::TERRAIN ) )
                    {
                        ret = false;
                    }

                } break;
                case SceneNodeType::TYPE_WATER:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::WATER ) )
                    {
                        ret = false;
                    }
                } break;
                case SceneNodeType::TYPE_PARTICLE_EMITTER:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::PARTICLES ) )
                    {
                        ret = false;
                    }
                } break;
                case SceneNodeType::TYPE_SKY:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::SKY ) )
                    {
                        ret = false;
                    }
                } break;
                case SceneNodeType::TYPE_INFINITEPLANE:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::PRIMITIVES ) )
                    {
                        ret = false;
                    }
                } break;
                case SceneNodeType::TYPE_VEGETATION:
                {
                    if ( renderFilter & to_base(RenderPassCuller::EntityFilter::VEGETATION ) )
                    {
                        ret = false;
                    }
                } break;
            }
        }

        return ret;
    }
}

/// Add a new SceneGraphNode to the current node's child list based on a SceneNode
SceneGraphNode* SceneGraphNode::addChildNode( const SceneGraphNodeDescriptor& descriptor )
{
    // Create a new SceneGraphNode with the SceneNode's info
    // We need to name the new SceneGraphNode
    // If we did not supply a custom name use the SceneNode's name

    SceneGraphNode* sceneGraphNode = _sceneGraph->createSceneGraphNode( _context, _sceneGraph, descriptor );
    DIVIDE_ASSERT( sceneGraphNode != nullptr && sceneGraphNode->_node->getState() != ResourceState::RES_CREATED );
    return addChildNode( sceneGraphNode );
}


FrustumCollision SceneGraphNode::stateCullNode(const NodeCullParams& params,
                                               const U16 cullFlags,
                                               const U32 filterMask,
                                               const F32 distanceToClosestPointSQ) const
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    // Early out for inactive nodes
    if (!hasFlag(SceneGraphNode::Flags::ACTIVE))
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    // If the node is still loading, DO NOT RENDER IT. Bad things happen :D
    if (hasFlag(Flags::LOADING))
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if ( !FilterCheck( filterMask, *_node ) )
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    const RenderingComponent* rendering = get<RenderingComponent>();
    if (params._stagePass._stage == RenderStage::SHADOW &&
        (rendering == nullptr || !IsShadowCaster(_node->type(),
            rendering->renderOptionEnabled(RenderingComponent::RenderOptions::CAST_SHADOWS))))
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    const bool translucent = (cullFlags & to_base(CullOptions::CULL_TRANSLUCENT_NODES)) != 0u &&
                             rendering != nullptr && rendering->hasTransparency();
    const bool defaultDrawState = !RenderPassCuller::CullGeometryCategory( cullFlags,
        _node->type() == SceneNodeType::TYPE_VEGETATION || hasFlag(Flags::VEGETATION_TREE),
        HasComponents(ComponentType::ANIMATION), _node->type() == SceneNodeType::TYPE_TERRAIN, translucent );
    if ( rendering != nullptr ? !rendering->drawState(params._stagePass, defaultDrawState)
                              : !_node->renderState().drawState(params._stagePass, defaultDrawState) )
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if ( _node->type() == SceneNodeType::TYPE_SKY )
    {
        return FrustumCollision::FRUSTUM_IN;
    }

    if ( RenderPassCuller::CullReflectionCategory( cullFlags,
                                                    _node->type() == SceneNodeType::TYPE_VEGETATION,
                                                    hasFlag( Flags::VEGETATION_TREE ),
                                                    usageContext() == NodeUsageContext::NODE_DYNAMIC ) )
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if (usageContext() == NodeUsageContext::NODE_STATIC)
    {
        if (cullFlags & to_base(CullOptions::CULL_STATIC_NODES))
        {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }
    else
    {
        if (cullFlags & to_base(CullOptions::CULL_DYNAMIC_NODES))
        {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }

    if ((cullFlags & to_base(CullOptions::CULL_AGAINST_LOD)) && !hasFlag(Flags::IS_CONTAINER))
    {
        PROFILE_SCOPE("cullNode - LoD check", Profiler::Category::Scene );

        RenderingComponent* rComp = get<RenderingComponent>();
        const float2 renderRange = rComp->renderRange();
        const F32 minDistanceSQ = SQUARED(renderRange.min) * (renderRange.min < 0.f ? -1.f : 1.f); //Keep the sign. Might need it for rays or shadows.
        const F32 maxDistanceSQ = SQUARED(renderRange.max);

        F32 rangeDistanceSQ = distanceToClosestPointSQ;
        if ( params._rangeCameraEyePos != params._cameraEyePos && !hasFlag( Flags::VISIBILITY_LOCKED ) )
        {
            const BoundsComponent* bComp = get<BoundsComponent>();
            if ( bComp != nullptr )
            {
                rangeDistanceSQ = bComp->getBoundingBox().nearestPoint( params._rangeCameraEyePos ).distanceSquared( params._rangeCameraEyePos );
            }
        }
        if ( _node->type() == SceneNodeType::TYPE_VEGETATION )
        {
            const float3 closest = get<BoundsComponent>()->getBoundingBox().nearestPoint( params._rangeCameraEyePos );
            rangeDistanceSQ = SQUARED( closest.x - params._rangeCameraEyePos.x ) +
                              SQUARED( closest.z - params._rangeCameraEyePos.z );
        }

        if (!IS_IN_RANGE_INCLUSIVE(rangeDistanceSQ, minDistanceSQ, maxDistanceSQ))
        {
            return FrustumCollision::FRUSTUM_OUT;
        }

        // We are in range, so proceed to LoD checks
        const U8 LoDLevel = rComp->getLoDLevel(rangeDistanceSQ, params._stagePass._stage, params._lodThresholds);
        if ((params._maxLoD > -1 && LoDLevel > params._maxLoD) || 
            LoDLevel > _node->renderState().maxLodLevel())
        {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }

    return FrustumCollision::FRUSTUM_IN;
}

FrustumCollision SceneGraphNode::clippingCullNode(const NodeCullParams& params) const
{
    PROFILE_SCOPE("cullNode - Bounding Sphere - Clipping Planes Test", Profiler::Category::Scene );
    const BoundsComponent* bComp = get<BoundsComponent>();
    if (bComp)
    {
        auto& planes = params._clippingPlanes.planes();
        auto& states = params._clippingPlanes.planeState();
        const BoundingSphere& boundingSphere = bComp->getBoundingSphere();
        for (U8 i = 0u; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
            if (!states[i])
            {
                continue;
            }

            if (PlaneBoundingSphereIntersect(planes[i], boundingSphere) == FrustumCollision::FRUSTUM_OUT)
            {
                // Fails the clipping plane test
                return FrustumCollision::FRUSTUM_OUT;
            }
        }
    }

    // We got here, so we insist on this node being parsed, even if we are missing a BoundsComponent
    // It will always pass clipping plane culling as it has no volume
    return FrustumCollision::FRUSTUM_IN;
}

FrustumCollision SceneGraphNode::frustumCullNode(const NodeCullParams& params,
                                                 const U16 cullFlags,
                                                 F32& distanceToClosestPointSQ) const
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    // Early-outs must not pass a previous node's distance to the render-range cull.
    distanceToClosestPointSQ = 0.f;

    const F32 maxDistanceSQ = SQUARED(params._cullMaxDistance);
    const BoundsComponent* bComp = get<BoundsComponent>();
    // We may also not have a BoundsComponent for whatever reason
    if ( bComp == nullptr )
    {
        return FrustumCollision::FRUSTUM_IN;
    }

    // We may also wish to always render this node for whatever reason (e.g. to preload shaders)
    // We may also wish to keep the sky always visible (e.g. for dynamic cubemaps)
    if ( hasFlag( Flags::VISIBILITY_LOCKED ) || _node->type() == SceneNodeType::TYPE_SKY )
    {
        return FrustumCollision::FRUSTUM_IN;
    }

    const BoundingBox& bounds = bComp->getBoundingBox();
    distanceToClosestPointSQ = bounds.nearestPoint( params._cameraEyePos ).distanceSquared( params._cameraEyePos );
    const F32 rangeDistanceSQ = params._rangeCameraEyePos == params._cameraEyePos
                                    ? distanceToClosestPointSQ
                                    : bounds.nearestPoint( params._rangeCameraEyePos ).distanceSquared( params._rangeCameraEyePos );
    if (rangeDistanceSQ > maxDistanceSQ)
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if (bounds.getExtent().maxComponent() < std::max(params._minExtents.maxComponent(), 0.f))
    {
        // Node is too small for the current render pass
        return FrustumCollision::FRUSTUM_OUT;
    }

    FrustumCollision collisionType = FrustumCollision::FRUSTUM_IN;
    if (cullFlags & to_base(CullOptions::CULL_AGAINST_FRUSTUM))
    {
        PROFILE_SCOPE("cullNode - Bounding Sphere & Box Frustum Test", Profiler::Category::Scene );
        // Sphere is in range, so check bounds primitives against the frustum
        if (bounds.containsPoint(params._cameraEyePos))
        {
            // We are inside the AABB, so INTERSECT is correct
            collisionType = FrustumCollision::FRUSTUM_INTERSECT;
        }
        else
        {
            I8 frustPlaneCache = params._stagePass._stage == RenderStage::DISPLAY
                               ? _frustPlaneCache.load( std::memory_order_relaxed ) : -1;

            // Check if the bounding sphere is in the frustum, as Frustum <-> Sphere check is fast
            collisionType = params._frustum->ContainsSphere(bComp->getBoundingSphere(), frustPlaneCache);
            if (collisionType == FrustumCollision::FRUSTUM_INTERSECT)
            {
                // If the sphere is not completely in the frustum, check the AABB
                collisionType = params._frustum->ContainsBoundingBox(bounds, frustPlaneCache);
            }
            if (params._stagePass._stage == RenderStage::DISPLAY)
            {
                _frustPlaneCache.store( frustPlaneCache, std::memory_order_relaxed );
            }
        }
    }

    return collisionType;
}

void SceneGraphNode::invalidateRelationshipCache(SceneGraphNode* source)
{
    if (source == this || !_relationshipCache.isValid())
    {
        return;
    }

    SendEvent(
    {
        ._type = ECS::CustomEvent::Type::RelationshipCacheInvalidated
    });

    _relationshipCache.invalidate();

    if (_parent && _parent->parent())
    {
        _parent->invalidateRelationshipCache(this);

        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            if (!source || _children._data[i]->getGUID() != source->getGUID())
            {
                _children._data[i]->invalidateRelationshipCache(this);
            }
        }
    }
}

bool SceneGraphNode::saveCache(ByteBuffer& outputBuffer) const
{
    outputBuffer << BYTE_BUFFER_VERSION;

    return getNode().saveCache(outputBuffer) &&
           _sceneGraph->GetECSManager().saveCache(this, outputBuffer);
}

bool SceneGraphNode::loadCache(ByteBuffer& inputBuffer)
{
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;

    return tempVer == BYTE_BUFFER_VERSION &&
           getNode().loadCache(inputBuffer) &&
           _sceneGraph->GetECSManager().loadCache(this, inputBuffer);
}

void SceneGraphNode::saveToXML(const ResourcePath& sceneLocation, DELEGATE<void, std::string_view> msgCallback) const
{
    if (!serialize())
    {
        return;
    }

    if (msgCallback)
    {
        msgCallback(Util::StringFormat("Saving node [ {} ] ...", name().c_str()).c_str());
    }

    boost::property_tree::ptree pt;
    pt.put("static", usageContext() == NodeUsageContext::NODE_STATIC);

    getNode().saveToXML(pt);

    for (const EditorComponent* editorComponent : Hacks._editorComponents)
    {
        Attorney::EditorComponentSceneGraphNode::saveToXML(*editorComponent, pt);
    }

    const auto targetFile =  Util::MakeXMLSafe( Util::StringFormat("{}_{}.xml", parent()->name(), name()) );
    const ResourcePath savePath = sceneLocation / Paths::g_nodesSaveLocation /  targetFile;

    XML::writeXML(savePath, pt);

    SharedLock<SharedMutex> r_lock(_children._lock);
    const U32 childCount = _children._count;
    for (U32 i = 0u; i < childCount; ++i)
    {
        _children._data[i]->saveToXML(sceneLocation, msgCallback);
    }
}

void SceneGraphNode::loadFromXML(const ResourcePath& sceneLocation)
{
    boost::property_tree::ptree pt;

    const auto targetFile =  Util::MakeXMLSafe( Util::StringFormat( "{}_{}.xml", parent()->name(), name()) );
    ResourcePath savePath = sceneLocation / Paths::g_nodesSaveLocation / targetFile;

    XML::readXML(savePath, pt);

    loadFromXML(pt);
}

void SceneGraphNode::loadFromXML(const boost::property_tree::ptree& pt)
{
    if (!serialize())
    {
        return;
    }

    U32 componentsToLoad = 0;
    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i)
    {
        const U32 componentBit = 1 << i;
        const ComponentType type = static_cast<ComponentType>(componentBit);
        if (pt.count(TypeUtil::ComponentTypeToString(type)) > 0)
        {
            componentsToLoad |= componentBit;
        }
    }

    if (componentsToLoad != 0)
    {
        AddComponents(componentsToLoad, false);
    }

    for (EditorComponent* editorComponent : Hacks._editorComponents)
    {
        Attorney::EditorComponentSceneGraphNode::loadFromXML(*editorComponent, pt);
    }

    changeUsageContext(pt.get("static", false) ? NodeUsageContext::NODE_STATIC : NodeUsageContext::NODE_DYNAMIC);
}

void SceneGraphNode::setFlag(const Flags flag, const bool recursive)
{
    if (!hasFlag(flag))
    {
        _nodeFlags |= to_base(flag);
        ECS::CustomEvent evt
        {
           ._type = ECS::CustomEvent::Type::EntityFlagChanged,
           ._sourceCmp = nullptr,
           ._flag = to_U32(flag)
        };
        evt._dataPair._first = 1u;
        evt._dataPair._second = recursive ? 1u : 0u;

        SendEvent(MOV(evt));
    }

    if (recursive && PropagateFlagToChildren(flag))
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            _children._data[i]->setFlag(flag, true);
        }
    }
}

void SceneGraphNode::clearFlag(const Flags flag, const bool recursive)
{
    if (hasFlag(flag))
    {
        _nodeFlags &= ~to_U32(flag);
        ECS::CustomEvent evt
        {
            ._type = ECS::CustomEvent::Type::EntityFlagChanged,
            ._sourceCmp = nullptr,
            ._flag = to_U32(flag)
        };
        evt._dataPair._first = 0u;
        evt._dataPair._second = recursive ? 1u : 0u;

        SendEvent(MOV(evt));
    }

    if (recursive && PropagateFlagToChildren(flag))
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i)
        {
            _children._data[i]->clearFlag(flag, true);
        }
    }
}

void SceneGraphNode::SendEvent(ECS::CustomEvent&& event)
{
    _events.push(MOV(event));
    Attorney::SceneGraphSGN::onNodeEvent(sceneGraph(), this);
}

void SceneGraphNode::updateCollisions( const SceneGraphNode& parentNode, IntersectionContainer& intersections, Mutex& intersectionsLock ) const
{
    if ( parentNode.getGUID() == getGUID() )
    {
        return;
    }
    if ( usageContext() == parentNode.usageContext() && usageContext() == NodeUsageContext::NODE_STATIC )
    {
        return;
    }

    BoundsComponent* boundsA = get<BoundsComponent>();
    BoundsComponent* boundsB = parentNode.get<BoundsComponent>();

    if ( boundsB == nullptr || !boundsB->collisionsEnabled() )
    {
        return;
    }

    if ( Collision( *boundsA, *boundsB) )
    {
        LockGuard<Mutex> w_lock(intersectionsLock);
        IntersectionRecord& ir = intersections.emplace_back();
        ir._intersectedObject1 = boundsA;
        ir._intersectedObject2 = boundsB;
        ir._hasHit = true;
    }

    SharedLock<SharedMutex> r_lock( parentNode._children._lock );
    const U32 childCount = parentNode._children._count;
    for ( U32 i = 0u; i < childCount; ++i )
    {
        updateCollisions(*parentNode._children._data[i], intersections, intersectionsLock );
    }
}

} //namespace Divide
