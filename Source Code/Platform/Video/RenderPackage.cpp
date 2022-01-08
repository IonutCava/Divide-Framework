#include "stdafx.h"

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

void Clear(RenderPackage& pkg) noexcept {
    pkg.drawCmdOffset(RenderPackage::INVALID_CMD_OFFSET);
    pkg.stagePassBaseIndex(RenderPackage::INVALID_STAGE_INDEX);
    pkg.pipelineCmd(GFX::BindPipelineCommand{});
    pkg.descriptorSetCmd(GFX::BindDescriptorSetsCommand{});
    pkg.pushConstantsCmd(GFX::SendPushConstantsCommand{});
    pkg.additionalCommands(nullptr);
}

}; //namespace Divide