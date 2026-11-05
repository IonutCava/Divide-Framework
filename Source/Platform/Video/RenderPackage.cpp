// Copyright (c) 2026 DIVIDE-Studio

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
    if ( _occlusionCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( _occlusionCommands );
    }
    if ( _additionalCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( _additionalCommands );
    }
    if ( _preparationCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( _preparationCommands );
    }
}

GFX::SendPushConstantsCommand RenderPackage::additionalPushConstantsCmd() noexcept
{
    GFX::SendPushConstantsCommand cmd{};
    cmd._uniformData = &_uniforms;
    return cmd;
}

void Clear(RenderPackage& pkg) noexcept
{
    if ( pkg._occlusionCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( pkg._occlusionCommands );
    }
    pkg.drawCmdOffset(RenderPackage::INVALID_CMD_OFFSET);
    pkg.stagePassBaseIndex(RenderPackage::INVALID_STAGE_INDEX);
    pkg.drawCommandsOverride(nullptr);
    pkg.pipelineCmd(GFX::BindPipelineCommand{});
    pkg.descriptorSetCmd(GFX::BindShaderResourcesCommand{});
    pkg.pushConstantsCmd(GFX::SendPushConstantsCommand{});
    pkg.pushConstantsCmd()._uniformData = &pkg._uniforms;

    if ( pkg._additionalCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( pkg._additionalCommands );
    }
    if ( pkg._preparationCommands != INVALID_HANDLE<GFX::CommandBuffer> )
    {
        GFX::DeallocateCommandBuffer( pkg._preparationCommands );
    }
}

Handle<GFX::CommandBuffer> GetCommandBuffer( RenderPackage& pkg )
{
    if ( pkg._additionalCommands == INVALID_HANDLE<GFX::CommandBuffer> )
    {
        pkg._additionalCommands = GFX::AllocateCommandBuffer("Render Package");
    }

    return pkg._additionalCommands;
}

Handle<GFX::CommandBuffer> GetPreparationCommandBuffer( RenderPackage& pkg )
{
    if ( pkg._preparationCommands == INVALID_HANDLE<GFX::CommandBuffer> )
    {
        pkg._preparationCommands = GFX::AllocateCommandBuffer( "Render Package Preparation" );
    }

    return pkg._preparationCommands;
}

void AppendPreparationCommands( const RenderPackage& pkg, GFX::CommandBuffer& buffer )
{
    buffer.add( pkg._preparationCommands );
}

Handle<GFX::CommandBuffer> GetOcclusionCommandBuffer( RenderPackage& pkg )
{
    if ( pkg._occlusionCommands == INVALID_HANDLE<GFX::CommandBuffer> )
    {
        pkg._occlusionCommands = GFX::AllocateCommandBuffer( "Render Package Occlusion" );
    }
    return pkg._occlusionCommands;
}

void AppendOcclusionCommands( const RenderPackage& pkg, GFX::CommandBuffer& buffer )
{
    buffer.add( pkg._occlusionCommands );
}

}//namespace Divide
