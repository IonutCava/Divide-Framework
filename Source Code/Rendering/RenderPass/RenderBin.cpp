#include "stdafx.h"

#include "Headers/RenderBin.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

RenderBin::RenderBin(const RenderBinType rbType, const RenderStage stage) 
    : _rbType(rbType),
      _stage(stage)

{
    std::atomic_init(&_renderBinIndex, 0u);
}

const RenderBinItem& RenderBin::getItem(const U16 index) const {
    assert(index < _renderBinIndex.load());
    return _renderBinStack[index];
}

void RenderBin::sort(const RenderingOrder renderOrder) {
    OPTICK_EVENT();

    switch (renderOrder) {
        case RenderingOrder::BY_STATE: {
            // Sorting opaque items is a 3 step process:
            // 1: sort by shaders
            // 2: if the shader is identical, sort by state hash
            // 3: if shader is identical and state hash is identical, sort by albedo ID
            eastl::sort(begin(_renderBinStack),
                        begin(_renderBinStack) + getBinSize(),
                        [](const RenderBinItem& a, const RenderBinItem& b) -> bool {
                            // Sort by shader in all states The sort key is the shader id (for now)
                            if (a._shaderKey != b._shaderKey) return a._shaderKey < b._shaderKey;
                            // If the shader values are the same, we use the state hash for sorting
                            // The _stateHash is a CRC value created based on the RenderState.
                            if (a._stateHash != b._stateHash) return a._stateHash < b._stateHash;
                            // If both the shader are the same and the state hashes match,
                            // we sort by the secondary key (usually the texture id)
                            if (a._textureKey != b._textureKey) return a._textureKey < b._textureKey;
                            // ... and then finally fallback to front to back
                            return a._distanceToCameraSq < b._distanceToCameraSq;
                        });
        } break;
        case RenderingOrder::BACK_TO_FRONT: {
            eastl::sort(begin(_renderBinStack),
                        begin(_renderBinStack) + getBinSize(),
                        [](const RenderBinItem& a, const RenderBinItem& b) -> bool {
                            return a._distanceToCameraSq > b._distanceToCameraSq;
                        });
        } break;
        case RenderingOrder::FRONT_TO_BACK: {
            eastl::sort(begin(_renderBinStack),
                        begin(_renderBinStack) + getBinSize(),
                        [](const RenderBinItem& a, const RenderBinItem& b) -> bool {
                            return a._distanceToCameraSq < b._distanceToCameraSq;
                        });
        } break;
        case RenderingOrder::WATER_FIRST: {
            eastl::sort(begin(_renderBinStack),
                        begin(_renderBinStack) + getBinSize(),
                        [](const RenderBinItem& a, const RenderBinItem&) -> bool {
                            return a._renderable->getSGN()->getNode().type() == SceneNodeType::TYPE_WATER;
                        });
        } break;
        case RenderingOrder::NONE: {
            // no need to sort
        } break;
        case RenderingOrder::COUNT: {
            Console::errorfn(Locale::Get(_ID("ERROR_INVALID_RENDER_BIN_SORT_ORDER")), Names::renderBinType[to_base(_rbType)]);
        } break;
    }
}

U16 RenderBin::getSortedNodes(SortedQueue& nodes) const {
    OPTICK_EVENT();

    const U16 binSize = getBinSize();

    nodes.resize(binSize);
    for (U16 i = 0; i < binSize; ++i) {
        nodes[i].first = _renderBinStack[i]._renderable;
        nodes[i].second = {};
    }

    return to_U16(binSize);
}

void RenderBin::refresh() {
    _renderBinIndex.store(0u);
}

void RenderBin::addNodeToBin(const SceneGraphNode* sgn, const RenderStagePass& renderStagePass, const F32 minDistToCameraSq)
{
    RenderingComponent* const rComp = sgn->get<RenderingComponent>();

    RenderBinItem item = {};
    item._renderable = rComp;

    // Sort by state hash depending on the current rendering stage
    // Save the render state hash value for sorting
    item._stateHash = rComp->getSortKeyHash(renderStagePass);
    item._distanceToCameraSq = minDistToCameraSq;

    const Material_ptr& nodeMaterial = rComp->getMaterialInstance();
    if (nodeMaterial) {
        nodeMaterial->getSortKeys(renderStagePass, item._shaderKey, item._textureKey);
    }

    _renderBinStack[_renderBinIndex.fetch_add(1)] = item;
}

void RenderBin::populateRenderQueue(const RenderStagePass stagePass, RenderQueuePackages& queueInOut) const {
    OPTICK_EVENT();

    const U16 binSize = getBinSize();
    for (U16 i = 0; i < binSize; ++i) {
        queueInOut.push_back(&_renderBinStack[i]._renderable->getDrawPackage(stagePass));
    }
}

void RenderBin::postRender(const SceneRenderState& renderState, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut) {
    const U16 binSize = getBinSize();
    for (U16 i = 0; i < binSize; ++i) {
        Attorney::RenderingCompRenderBin::postRender(_renderBinStack[i]._renderable, renderState, stagePass, bufferInOut);
    }
}

U16 RenderBin::getBinSize() const {
    return _renderBinIndex.load();
}

} // namespace Divide