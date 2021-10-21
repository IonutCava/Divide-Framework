#include "stdafx.h"

#include "Headers/RenderQueue.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

RenderQueue::RenderQueue(Kernel& parent, const RenderStage stage)
    : KernelComponent(parent),
      _stage(stage),
      _renderBins{nullptr}
{
    for (U8 i = 0u; i < to_U8(RenderBinType::COUNT); ++i) {
        const RenderBinType rbType = static_cast<RenderBinType>(i);
        if (rbType == RenderBinType::COUNT) {
            continue;
        }

        _renderBins[i] = MemoryManager_NEW RenderBin(rbType, stage);
    }
}

RenderQueue::~RenderQueue()
{
    for (RenderBin* bin : _renderBins) {
        MemoryManager::DELETE(bin);
    }
}

U16 RenderQueue::getRenderQueueStackSize() const {
    U16 temp = 0;
    for (RenderBin* bin : _renderBins) {
        if (bin != nullptr) {
            temp += bin->getBinSize();
        }
    }
    return temp;
}

RenderingOrder RenderQueue::getSortOrder(const RenderStagePass stagePass, const RenderBinType rbType) const {
    RenderingOrder sortOrder = RenderingOrder::COUNT;
    switch (rbType) {
        case RenderBinType::OPAQUE: {
            // Opaque items should be rendered front to back in depth passes for early-Z reasons
            sortOrder = stagePass.isDepthPass() ? RenderingOrder::FRONT_TO_BACK
                                                : RenderingOrder::BY_STATE;
        } break;
        case RenderBinType::SKY: {
            sortOrder = RenderingOrder::NONE;
        } break;
        case RenderBinType::IMPOSTOR:
        case RenderBinType::TERRAIN: {
            sortOrder = RenderingOrder::FRONT_TO_BACK;
        } break;
        case RenderBinType::TERRAIN_AUX: {
            // Water first, everything else after
            sortOrder = RenderingOrder::WATER_FIRST;
        } break;
        case RenderBinType::TRANSLUCENT: {
            // We are using weighted blended OIT. State is fine (and faster)
            // Use an override one level up from this if we need a regular forward-style pass
            sortOrder = RenderingOrder::BY_STATE;
        } break;
        default:
        case RenderBinType::COUNT: {
            Console::errorfn(Locale::Get(_ID("ERROR_INVALID_RENDER_BIN_CREATION")));
        } break;
    };
    
    return sortOrder;
}

RenderBin* RenderQueue::getBinForNode(const SceneGraphNode* node, const Material_ptr& matInstance) {
    switch (node->getNode().type()) {
        case SceneNodeType::TYPE_TRANSFORM:
        {
            if (BitCompare(node->componentMask(), ComponentType::SPOT_LIGHT) ||
                BitCompare(node->componentMask(), ComponentType::POINT_LIGHT) ||
                BitCompare(node->componentMask(), ComponentType::DIRECTIONAL_LIGHT) ||
                BitCompare(node->componentMask(), ComponentType::ENVIRONMENT_PROBE))
            {
                return _renderBins[to_base(RenderBinType::IMPOSTOR)];
            }
            /*if (BitCompare(node->componentMask(), ComponentType::PARTICLE_EMITTER_COMPONENT) ||
                BitCompare(node->componentMask(), ComponentType::GRASS_COMPONENT))
            {
                return _renderBins[to_base(RenderBinType::TRANSLUCENT)];
            }*/
            return nullptr;
        }

        case SceneNodeType::TYPE_VEGETATION:
        case SceneNodeType::TYPE_PARTICLE_EMITTER:
            return _renderBins[to_base(RenderBinType::TRANSLUCENT)];

        case SceneNodeType::TYPE_SKY:
            return _renderBins[to_base(RenderBinType::SKY)];

        case SceneNodeType::TYPE_WATER:
        case SceneNodeType::TYPE_INFINITEPLANE:
            return _renderBins[to_base(RenderBinType::TERRAIN_AUX)];

        // Water is also opaque as refraction and reflection are separate textures
        // We may want to break this stuff up into mesh rendering components and not care about specifics anymore (i.e. just material checks)
        //case SceneNodeType::TYPE_WATER:
        case SceneNodeType::TYPE_OBJECT3D: {
            if (node->getNode().type() == SceneNodeType::TYPE_OBJECT3D) {
                switch (node->getNode<Object3D>().getObjectType()) {
                    case ObjectType::TERRAIN:
                        return _renderBins[to_base(RenderBinType::TERRAIN)];

                    case ObjectType::DECAL:
                        return _renderBins[to_base(RenderBinType::TRANSLUCENT)];
                    default: break;
                }
            }
            // Check if the object has a material with transparency/translucency
            if (matInstance != nullptr && matInstance->hasTransparency()) {
                // Add it to the appropriate bin if so ...
                return _renderBins[to_base(RenderBinType::TRANSLUCENT)];
            }

            //... else add it to the general geometry bin
            return _renderBins[to_base(RenderBinType::OPAQUE)];
        }
        default:
        case SceneNodeType::COUNT:
        case SceneNodeType::TYPE_TRIGGER: break;
    }
    return nullptr;
}

void RenderQueue::addNodeToQueue(const SceneGraphNode* sgn,
                                 const RenderStagePass stagePass,
                                 const F32 minDistToCameraSq,
                                 const RenderBinType targetBinType)
{
    RenderingComponent* const renderingCmp = sgn->get<RenderingComponent>();
    // We need a rendering component to render the node
    assert(renderingCmp != nullptr);
    if (!renderingCmp->getDrawPackage(stagePass).empty()) {
        RenderBin* rb = getBinForNode(sgn, renderingCmp->getMaterialInstance());
        assert(rb != nullptr);

        if (targetBinType == RenderBinType::COUNT || rb->getType() == targetBinType) {
            rb->addNodeToBin(sgn, stagePass, minDistToCameraSq);
        }
    }
}

void RenderQueue::populateRenderQueues(const RenderStagePass stagePass, const std::pair<RenderBinType, bool> binAndFlag, RenderQueuePackages& queueInOut) {
    OPTICK_EVENT();

    auto [binType, includeBin] = binAndFlag;

    if (binType == RenderBinType::COUNT) {
        if (!includeBin) {
            // Why are we allowed to exclude everything? idk.
            return;
        }

        for (RenderBin* renderBin : _renderBins) {
            renderBin->populateRenderQueue(stagePass, queueInOut);
        }
    } else {
        // Everything except the specified type or just the specified type
        for (RenderBin* renderBin : _renderBins) {
            if ((renderBin->getType() == binType) == includeBin) {
                renderBin->populateRenderQueue(stagePass, queueInOut);
            }
        }
    }
}

void RenderQueue::postRender(const SceneRenderState& renderState, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut) {
    for (RenderBin* renderBin : _renderBins) {
        renderBin->postRender(renderState, stagePass, bufferInOut);
    }
}

void RenderQueue::sort(const RenderStagePass& stagePass, const RenderBinType targetBinType, const RenderingOrder renderOrder) {
    OPTICK_EVENT();

    // How many elements should a render bin contain before we decide that sorting should happen on a separate thread
    constexpr U16 threadBias = 64;
    
    if (targetBinType != RenderBinType::COUNT)
    {
        const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder(stagePass, targetBinType) : renderOrder;
        _renderBins[to_base(targetBinType)]->sort(sortOrder);
    }
    else
    {
        TaskPool& pool = parent().platformContext().taskPool(TaskPoolType::HIGH_PRIORITY);
        Task* sortTask = CreateTask(TASK_NOP);
        for (RenderBin* renderBin : _renderBins) {
            if (renderBin->getBinSize() > threadBias) {
                const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder(stagePass, renderBin->getType()) : renderOrder;
                Start(*CreateTask(sortTask,
                                    [renderBin, sortOrder](const Task& /*parentTask*/) {
                                        renderBin->sort(sortOrder);
                                    }),
                      pool);
            }
        }

        Start(*sortTask, pool);

        for (RenderBin* renderBin : _renderBins) {
            if (renderBin->getBinSize() <= threadBias) {
                const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder(stagePass, renderBin->getType()) : renderOrder;
                renderBin->sort(sortOrder);
            }
        }

        Wait(*sortTask, pool);
    }
}

void RenderQueue::refresh(const RenderBinType targetBinType) {
    if (targetBinType == RenderBinType::COUNT) {
        for (RenderBin* renderBin : _renderBins) {
            renderBin->refresh();
        }
    } else {
        for (RenderBin* renderBin : _renderBins) {
            if (renderBin->getType() == targetBinType) {
                renderBin->refresh();
            }
        }
    }
}

U16 RenderQueue::getSortedQueues(const vector<RenderBinType>& binTypes, RenderBin::SortedQueues& queuesOut) const {
    OPTICK_EVENT();

    U16 countOut = 0u;

    if (binTypes.empty()) {
        for (const RenderBin* renderBin : _renderBins) {
            RenderBin::SortedQueue& nodes = queuesOut[to_base(renderBin->getType())];
            countOut += renderBin->getSortedNodes(nodes);
        }
    } else {
        for (const RenderBinType type : binTypes) {
            const RenderBin* renderBin = _renderBins[to_base(type)];
            RenderBin::SortedQueue& nodes = queuesOut[to_base(type)];
            countOut += renderBin->getSortedNodes(nodes);
        }
    }
    return countOut;
}

};