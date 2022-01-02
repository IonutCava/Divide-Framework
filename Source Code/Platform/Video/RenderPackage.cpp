#include "stdafx.h"

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

void Clear(RenderPackage& pkg) {
    pkg.drawCmdOffset(RenderPackage::INVALID_CMD_OFFSET);
    pkg.stagePassBaseIndex(RenderPackage::INVALID_STAGE_INDEX);
    pkg.pipelineCmd({});
    pkg.descriptorSetCmd({});
    pkg.pushConstantsCmd({});
}

bool Empty(const RenderPackage& pkg) noexcept {
    return pkg.pipelineCmd()._pipeline == nullptr;
}

}; //namespace Divide