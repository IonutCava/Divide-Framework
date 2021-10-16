#include "stdafx.h"

#include "Headers/SceneGraph.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/Kernel.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/FrameListenerManager.h"
#include "Managers/Headers/SceneManager.h"
#include "Utility/Headers/Localization.h"

#include "ECS/Systems/Headers/ECSManager.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Physics/Headers/PXDevice.h"

namespace Divide {

namespace {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;
    constexpr std::array<U32, 2> g_cacheMarkerByteValue = { 0xDEADBEEF, 0xBADDCAFE };
    constexpr U32 g_nodesPerPartition = 32u;

    [[nodiscard]] bool IsTransformNode(const SceneNodeType nodeType, const ObjectType objType) noexcept {
        return nodeType == SceneNodeType::TYPE_TRANSFORM ||
               nodeType == SceneNodeType::TYPE_TRIGGER ||
               objType  == ObjectType::MESH;
    }
};

SceneGraph::SceneGraph(Scene& parentScene)
    : FrameListener("SceneGraph", parentScene.context().kernel().frameListenerMgr(), 1),
      SceneComponent(parentScene)
{
    _ecsManager = eastl::make_unique<ECSManager>(parentScene.context(), GetECSEngine());

    SceneGraphNodeDescriptor rootDescriptor = {};
    rootDescriptor._name = "ROOT";
    rootDescriptor._node = std::make_shared<SceneNode>(parentScene.resourceCache(), generateGUID(), "ROOT", ResourcePath{ "ROOT" }, ResourcePath{ "" }, SceneNodeType::TYPE_TRANSFORM, to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS));
    rootDescriptor._componentMask = to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS);
    rootDescriptor._usageContext = NodeUsageContext::NODE_STATIC;

    _root = createSceneGraphNode(this, rootDescriptor);
    _root->postLoad();
    onNodeAdd(_root);

    constexpr U16 octreeExclusionMask = 1 << to_base(SceneNodeType::TYPE_TRANSFORM) |
                                        1 << to_base(SceneNodeType::TYPE_SKY) |
                                        1 << to_base(SceneNodeType::TYPE_INFINITEPLANE) |
                                        1 << to_base(SceneNodeType::TYPE_VEGETATION);

    _octree.reset(new Octree(nullptr, octreeExclusionMask));
    _octreeUpdating = false;
}

SceneGraph::~SceneGraph()
{ 
    _octree.reset();
    Console::d_printfn(Locale::Get(_ID("DELETE_SCENEGRAPH")));
    // Should recursively delete the entire scene graph
    unload();
}

void SceneGraph::unload()
{
    destroySceneGraphNode(_root);
    assert(_root == nullptr);
}

void SceneGraph::addToDeleteQueue(SceneGraphNode* node, const size_t childIdx) {
    ScopedLock<SharedMutex> w_lock(_pendingDeletionLock);
    vector<size_t>& list = _pendingDeletion[node];
    if (eastl::find(cbegin(list), cend(list), childIdx) == cend(list))
    {
        list.push_back(childIdx);
    }
}

void SceneGraph::onNodeUpdated(const SceneGraphNode& node) {

    //ToDo: Maybe add particles too? -Ionut
    if (node.getNode<>().type() == SceneNodeType::TYPE_OBJECT3D) {
        SceneEnvironmentProbePool* probes = Attorney::SceneGraph::getEnvProbes(parentScene());
        SceneEnvironmentProbePool::OnNodeUpdated(*probes, node);
    }
}

void SceneGraph::onNodeMoved(const SceneGraphNode& node) {
    getOctree().onNodeMoved(node);
    onNodeUpdated(node);
}

void SceneGraph::onNodeDestroy(SceneGraphNode* oldNode) {
    I64 guid = oldNode->getGUID();

    if (guid == _root->getGUID()) {
        return;
    }

    erase_if(_nodesByType[to_base(oldNode->getNode().type())],
             [guid](SceneGraphNode* node)-> bool
             {
                 return node && node->getGUID() == guid;
             });

    Attorney::SceneGraph::onNodeDestroy(_parentScene, oldNode);

    _nodeListChanged = true;
}

void SceneGraph::onNodeAdd(SceneGraphNode* newNode) {
    _nodesByType[to_base(newNode->getNode().type())].push_back(newNode);

    _nodeListChanged = true;

    if (_loadComplete) {
        WAIT_FOR_CONDITION(!_octreeUpdating);
        _octreeChanged = _octree->addNode(newNode) || _octreeChanged;
    }
}

bool SceneGraph::removeNodesByType(const SceneNodeType nodeType) {
    return _root != nullptr && getRoot()->removeNodesByType(nodeType);
}

bool SceneGraph::removeNode(const I64 guid) {
    return removeNode(findNode(guid));
}

bool SceneGraph::removeNode(SceneGraphNode* node) {
    if (node) {
        SceneGraphNode* parent = node->parent();
        if (parent) {
            if (!parent->removeChildNode(node, true)) {
                return false;
            }
        }

        return true;
    }

    return false;
}

bool SceneGraph::frameStarted(const FrameEvent& evt) {
    {
        ScopedLock<SharedMutex> lock(_pendingDeletionLock);
        if (!_pendingDeletion.empty()) {
            for (auto entry : _pendingDeletion) {
                if (entry.first != nullptr) {
                    Attorney::SceneGraphNodeSceneGraph::processDeleteQueue(entry.first, entry.second);
                }
            }
            _pendingDeletion.clear();
        }
    }

    // Gather all nodes at the start of the frame only if we added/removed any of them
    if (_nodeListChanged) {
        // Very rarely called
        _nodeList.resize(0);
        Attorney::SceneGraphNodeSceneGraph::getAllNodes(_root, _nodeList);
        _nodeListChanged = false;
    }

    {
        OPTICK_EVENT("ECS::OnFrameStart");
        GetECSEngine().OnFrameStart();
    }
    return true;
}

bool SceneGraph::frameEnded(const FrameEvent& evt) {
    {
        OPTICK_EVENT("ECS::OnFrameEnd");
        GetECSEngine().OnFrameEnd();
    }

    return true;
}

void SceneGraph::sceneUpdate(const U64 deltaTimeUS, SceneState& sceneState) {
    OPTICK_EVENT();

    const F32 msTime = Time::MicrosecondsToMilliseconds<F32>(deltaTimeUS);
    {
        OPTICK_EVENT("ECS::PreUpdate");
        GetECSEngine().PreUpdate(msTime);
    }
    {
        OPTICK_EVENT("ECS::Update");
        GetECSEngine().Update(msTime);
    }
    {
        OPTICK_EVENT("ECS::PostUpdate");
        GetECSEngine().PostUpdate(msTime);
    }

    PlatformContext& context = parentScene().context();

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = to_U32(_nodeList.size());
    descriptor._partitionSize = g_nodesPerPartition;
    descriptor._cbk = [this](const Task* /*parentTask*/, const U32 start, const U32 end) {
        for (U32 i = start; i < end; ++i) {
            Attorney::SceneGraphNodeSceneGraph::processEvents(_nodeList[i]);
        }
    };

    parallel_for(context, descriptor);

    for (SceneGraphNode* node : _nodeList) {
        node->sceneUpdate(deltaTimeUS, sceneState);
    }

    if (_loadComplete) {
        Task* task = CreateTask(
            [this, deltaTimeUS](const Task& /*parentTask*/) mutable
            {
                OPTICK_EVENT("Octree Update");
                _octreeUpdating = true;
                if (_octreeChanged) {
                    _octree->updateTree();
                }
                _octree->update(deltaTimeUS);
        });

        Start(*task,
              context.taskPool(TaskPoolType::HIGH_PRIORITY),
              //TaskPriority::DONT_CARE,
              TaskPriority::REALTIME,
              [this]() { _octreeUpdating = false; });
    }
}

void SceneGraph::onStartUpdateLoop(const U8 loopNumber) {
    ACKNOWLEDGE_UNUSED(loopNumber);
}

void SceneGraph::onNetworkSend(const U32 frameCount) {
    Attorney::SceneGraphNodeSceneGraph::onNetworkSend(_root, frameCount);
}

bool SceneGraph::intersect(const SGNIntersectionParams& params, vector<SGNRayResult>& intersectionsOut) const {
    intersectionsOut.resize(0);

    // Try to leverage our physics system as it will always be way more faster and accurate
    if (!parentScene().context().pfx().intersect(params._ray, params._range, intersectionsOut)) {
        // Fallback to Sphere/AABB/OBB intersections
        if (!_root->intersect(params._ray, params._range, intersectionsOut)) {
            return false;
        }
    }


    DIVIDE_ASSERT(!intersectionsOut.empty());

    const auto isIgnored = [&params](const SceneNodeType type) {
        for (size_t i = 0; i < params._ignoredTypesCount; ++i) {
            if (type == params._ignoredTypes[i]) {
                return true;
            }
        }
        return false;
    };

    for (SGNRayResult& result : intersectionsOut) {
        SceneGraphNode* node = findNode(result.sgnGUID);
        const SceneNodeType snType =  node->getNode().type();
        ObjectType objectType = ObjectType::COUNT;
        if (snType == SceneNodeType::TYPE_OBJECT3D) {
            objectType = static_cast<const Object3D&>(node->getNode()).getObjectType();
        }

        if (isIgnored(snType) || (!params._includeTransformNodes && IsTransformNode(snType, objectType))) {
            result.sgnGUID = -1;
        }
    }

    erase_if(intersectionsOut,
             [](const SGNRayResult& res) {
                 return res.dist < 0.f || res.sgnGUID == -1;
             });

    return !intersectionsOut.empty();
}

void SceneGraph::postLoad() {
    for (const auto& nodes : _nodesByType) {
        if (!_octree->addNodes(nodes)) {
            NOP();
        }
    }
    _octreeChanged = true;
    _loadComplete = true;
}

SceneGraphNode* SceneGraph::createSceneGraphNode(SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor) {
    ScopedLock<Mutex> u_lock(_nodeCreateMutex);

    const ECS::EntityId nodeID = GetEntityManager()->CreateEntity<SceneGraphNode>(sceneGraph, descriptor);
    return static_cast<SceneGraphNode*>(GetEntityManager()->GetEntity(nodeID));
}

void SceneGraph::destroySceneGraphNode(SceneGraphNode*& node, const bool inPlace) {
    if (node) {
        if (inPlace) {
            GetEntityManager()->DestroyAndRemoveEntity(node->GetEntityID());
        } else {
            GetEntityManager()->DestroyEntity(node->GetEntityID());
        }
        node = nullptr;
    }
}

size_t SceneGraph::getTotalNodeCount() const noexcept {
    size_t ret = 0;
    for (const auto& nodes : _nodesByType) {
        ret += nodes.size();
    }

    return ret;
}
const vector<SceneGraphNode*>& SceneGraph::getNodesByType(const SceneNodeType type) const {
    return _nodesByType[to_base(type)];
}

ECS::EntityManager* SceneGraph::GetEntityManager() {
    return GetECSEngine().GetEntityManager();
}

ECS::EntityManager* SceneGraph::GetEntityManager() const {
    return GetECSEngine().GetEntityManager();
}

ECS::ComponentManager* SceneGraph::GetComponentManager() {
    return GetECSEngine().GetComponentManager();
}

ECS::ComponentManager* SceneGraph::GetComponentManager() const {
    return GetECSEngine().GetComponentManager();
}

SceneGraphNode* SceneGraph::findNode(const Str128& name, const bool sceneNodeName) const {
    return findNode(_ID(name.c_str()), sceneNodeName);
}

SceneGraphNode* SceneGraph::findNode(const U64 nameHash, const bool sceneNodeName) const {
    const U64 cmpHash = sceneNodeName ? _ID(_root->getNode().resourceName().c_str()) : _ID(_root->name().c_str());

    if (cmpHash == nameHash) {
        return _root;
    }

    return _root->findChild(nameHash, sceneNodeName, true);
}

SceneGraphNode* SceneGraph::findNode(const I64 guid) const {
    if (_root->getGUID() == guid) {
        return _root;
    }

    return _root->findChild(guid, false, true);
}

bool SceneGraph::saveCache(ByteBuffer& outputBuffer) const {
    const std::function<bool(SceneGraphNode*, ByteBuffer&)> saveNodes = [&](SceneGraphNode* sgn, ByteBuffer& outputBuffer) {
        // Because loading is async, nodes will not be necessarily in the same order. We need a way to find
        // the node using some sort of ID. Name based ID is bad, but is the only system available at the time of writing -Ionut
        outputBuffer << _ID(sgn->name().c_str());
        if (!Attorney::SceneGraphNodeSceneGraph::saveCache(sgn, outputBuffer)) {
            NOP();
        }

        // Data may be bad, so add markers to be able to just jump over the entire node data instead of attempting partial loads
        outputBuffer.addMarker(g_cacheMarkerByteValue);

        sgn->forEachChild([this, &saveNodes, &outputBuffer](SceneGraphNode* child, I32 /*idx*/) {
            return saveNodes(child, outputBuffer);
        });

        return true;
    };

    outputBuffer << BYTE_BUFFER_VERSION;

    if (saveNodes(_root, outputBuffer)) {
        outputBuffer << _ID(_root->name().c_str());
        return true;
    }

    return false;
}

bool SceneGraph::loadCache(ByteBuffer& inputBuffer) {
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION) {
        const U64 rootID = _ID(_root->name().c_str());

        U64 nodeID = 0u;

        bool skipRoot = true;
        bool missingData = false;
        do {
            inputBuffer >> nodeID;
            if (nodeID == rootID && !skipRoot) {
                break;
            }

            SceneGraphNode* node = findNode(nodeID, false);

            if (node == nullptr || !Attorney::SceneGraphNodeSceneGraph::loadCache(node, inputBuffer)) {
                missingData = true;
            }

            inputBuffer.readSkipToMarker(g_cacheMarkerByteValue);

            if (nodeID == rootID && skipRoot) {
                skipRoot = false;
                nodeID = 0u;
            }
        } while (nodeID != rootID);

        return !missingData;
    }

    return false;
}

namespace {
    constexpr size_t g_sceneGraphVersion = 1;

    boost::property_tree::ptree dumpSGNtoAssets(const SceneGraphNode* node) {
        boost::property_tree::ptree entry;
        entry.put("<xmlattr>.name", node->name().c_str());
        entry.put("<xmlattr>.type", node->getNode().getTypeName().c_str());

        node->forEachChild([&entry](const SceneGraphNode* child, I32 /*childIdx*/) {
            if (child->serialize()) {
                entry.add_child("node", dumpSGNtoAssets(child));
            }
            return true;
        });

        return entry;
    }
};

void SceneGraph::saveToXML(const char* assetsFile, DELEGATE<void, std::string_view> msgCallback) const {
    const ResourcePath scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation;
    ResourcePath sceneLocation(scenePath + "/" + parentScene().resourceName());

    {
        boost::property_tree::ptree pt;
        pt.put("version", g_sceneGraphVersion);
        pt.add_child("entities.node", dumpSGNtoAssets(getRoot()));

        if (copyFile(sceneLocation + "/", ResourcePath(assetsFile), sceneLocation + "/", ResourcePath("assets.xml.bak"), true) == FileError::NONE) {
            XML::writeXML((sceneLocation + "/" + assetsFile).str(), pt);
        } else {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    getRoot()->forEachChild([&sceneLocation, &msgCallback](const SceneGraphNode* child, I32 /*childIdx*/) {
        child->saveToXML(sceneLocation.str(), msgCallback);
        return true;
    });
}

namespace {
    boost::property_tree::ptree g_emptyPtree;
}

void SceneGraph::loadFromXML(const char* assetsFile) {
    using boost::property_tree::ptree;
    static const auto& scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation;

    const ResourcePath file = scenePath + "/" + parentScene().resourceName() + "/" + assetsFile;

    if (!fileExists(file)) {
        return;
    }

    Console::printfn(Locale::Get(_ID("XML_LOAD_GEOMETRY")), file.c_str());

    ptree pt = {};
    XML::readXML(file.str(), pt);
    if (pt.get("version", g_sceneGraphVersion) != g_sceneGraphVersion) {
        // ToDo: Scene graph version mismatch. Handle condition - Ionut
        NOP();
    }

    const auto readNode = [](const ptree& rootNode, XML::SceneNode& graphOut, auto& readNodeRef) -> void {
        for (const auto&[name, value] : rootNode.get_child("<xmlattr>", g_emptyPtree)) {
            if (name == "name") {
                graphOut.name = value.data();
            } else if (name == "type") {
                graphOut.typeHash = _ID(value.data().c_str());
            } else {
                //ToDo: Error handling -Ionut
                NOP();
            }
        }

        for (const auto&[name, ptree] : rootNode.get_child("")) {
            if (name == "node") {
                graphOut.children.emplace_back();
                readNodeRef(ptree, graphOut.children.back(), readNodeRef);
            }
        }
    };



    XML::SceneNode rootNode = {};
    const auto& [name, node_pt] = pt.get_child("entities", g_emptyPtree).front();
    // This is way faster than pre-declaring a std::function and capturing that or by using 2 separate
    // lambdas and capturing one.
    readNode(node_pt, rootNode, readNode);
    // This may not be needed;
    assert(rootNode.typeHash == _ID("TRANSFORM"));
    Attorney::SceneGraph::addSceneGraphToLoad(parentScene(), MOV(rootNode));
}

bool SceneGraph::saveNodeToXML(const SceneGraphNode* node) const {
    const ResourcePath scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation;
    const ResourcePath sceneLocation(scenePath + "/" + parentScene().resourceName());
    node->saveToXML(sceneLocation.str());
    return true;
}

bool SceneGraph::loadNodeFromXML(const char* assetsFile, SceneGraphNode* node) const {
    ACKNOWLEDGE_UNUSED(assetsFile);

    const ResourcePath scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation;
    const ResourcePath sceneLocation(scenePath + "/" + parentScene().resourceName());
    node->loadFromXML(sceneLocation.str());
    return true;
}

};
