

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

RenderPackage::RenderPackage()
{
    pushConstantsCmd()._uniformData = &_uniforms;
}

RenderPackage::~RenderPackage()
{
    GFX::DeallocateCommandBuffer( _additionalCommands );
}

void Clear(RenderPackage& pkg) noexcept
{
    pkg.drawCmdOffset(RenderPackage::INVALID_CMD_OFFSET);
    pkg.stagePassBaseIndex(RenderPackage::INVALID_STAGE_INDEX);
    pkg.pipelineCmd(GFX::BindPipelineCommand{});
    pkg.descriptorSetCmd(GFX::BindShaderResourcesCommand{});
    pkg.pushConstantsCmd(GFX::SendPushConstantsCommand{});
    pkg.pushConstantsCmd()._uniformData = &pkg._uniforms;

    GFX::DeallocateCommandBuffer( pkg._additionalCommands );
}

Handle<GFX::CommandBuffer> GetCommandBuffer( RenderPackage& pkg )
{
    if ( pkg._additionalCommands == INVALID_HANDLE<GFX::CommandBuffer> )
    {
        pkg._additionalCommands = GFX::AllocateCommandBuffer("Render Package");
    }

    return pkg._additionalCommands;
}

}//namespace Divide
