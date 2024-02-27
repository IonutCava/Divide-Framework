

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

void Clear(RenderPackage& pkg) noexcept {
    pkg.drawCmdOffset(RenderPackage::INVALID_CMD_OFFSET);
    pkg.stagePassBaseIndex(RenderPackage::INVALID_STAGE_INDEX);
    pkg.pipelineCmd(GFX::BindPipelineCommand{});
    pkg.descriptorSetCmd(GFX::BindShaderResourcesCommand{});
    pkg.pushConstantsCmd(GFX::SendPushConstantsCommand{});

    if ( pkg._additionalCommands != nullptr )
    {
        pkg._additionalCommands->clear( false );
    }
}

GFX::CommandBuffer* GetCommandBuffer( RenderPackage& pkg )
{
    if ( pkg._additionalCommands == nullptr )
    {
        pkg._additionalCommands = eastl::make_unique<GFX::CommandBuffer>();
    }

    return pkg._additionalCommands.get();
}
}; //namespace Divide