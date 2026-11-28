// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

namespace
{
    bool ValidComputeBarriers( const GFX::CommandBuffer& source,
                               GFX::MemoryBarrierCommand*& before,
                               GFX::MemoryBarrierCommand*& after )
    {
        const auto& commands = source.commands();
        if ( commands.size() < 3u ||
             commands.front()->type() != GFX::CommandType::MEMORY_BARRIER ||
             commands.back()->type() != GFX::CommandType::MEMORY_BARRIER )
        {
            return false;
        }

        before = commands.front()->As<GFX::MemoryBarrierCommand>();
        after = commands.back()->As<GFX::MemoryBarrierCommand>();
        if ( before->_bufferLocks.empty() || after->_bufferLocks.empty() ||
             !before->_textureLayoutChanges.empty() || !after->_textureLayoutChanges.empty() )
        {
            return false;
        }

        bool hasDispatch = false;
        for ( size_t i = 1u; i + 1u < commands.size(); ++i )
        {
            switch ( commands[i]->type() )
            {
                case GFX::CommandType::BIND_PIPELINE:
                case GFX::CommandType::BIND_SHADER_RESOURCES:
                case GFX::CommandType::SEND_PUSH_CONSTANTS:
                case GFX::CommandType::BEGIN_DEBUG_SCOPE:
                case GFX::CommandType::END_DEBUG_SCOPE:
                    break;
                case GFX::CommandType::DISPATCH_SHADER_TASK:
                    hasDispatch = true;
                    break;
                default:
                    return false;
            }
        }
        if ( !hasDispatch )
        {
            return false;
        }

        const auto sameRange = []( const BufferLock& lhs, const BufferLock& rhs )
        {
            return lhs._buffer == rhs._buffer && lhs._range == rhs._range;
        };
        for ( const BufferLock& lock : after->_bufferLocks )
        {
            if ( lock._buffer == nullptr || lock._type != BufferSyncUsage::GPU_WRITE_TO_GPU_READ ||
                 lock._range._length == 0u || lock._range._length >= U32_MAX ||
                 lock._range._startOffset > SIZE_MAX - lock._range._length )
            {
                return false;
            }
            const auto matches = [&]( const BufferSyncUsage type )
            {
                return std::ranges::any_of( before->_bufferLocks, [&]( const BufferLock& entry )
                {
                    return entry._type == type && sameRange( entry, lock );
                } );
            };
            if ( !matches( BufferSyncUsage::GPU_READ_TO_GPU_WRITE ) ||
                 !matches( BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE ) )
            {
                return false;
            }
        }
        for ( const BufferLock& lock : before->_bufferLocks )
        {
            if ( (lock._type != BufferSyncUsage::GPU_READ_TO_GPU_WRITE &&
                  lock._type != BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE) ||
                 !std::ranges::any_of( after->_bufferLocks, [&]( const BufferLock& entry )
                 {
                     return sameRange( lock, entry );
                 } ) )
            {
                return false;
            }
        }
        return true;
    }

    bool IndependentRanges( const BufferLocks& existing, const BufferLocks& candidate )
    {
        for ( const BufferLock& current : candidate )
        {
            for ( const BufferLock& previous : existing )
            {
                if ( current._buffer->getGUID() == previous._buffer->getGUID() )
                {
                    const size_t first = std::min( current._range._startOffset, previous._range._startOffset );
                    const size_t last = std::max( current._range.endOffset(), previous._range.endOffset() );
                    if ( Overlaps( current._range, previous._range ) || last - first >= U32_MAX )
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    template<typename GetHandle>
    void AppendIndependentCompute( const std::span<RenderPackage* const> packages,
                                   GFX::CommandBuffer& buffer,
                                   BufferLocks& scratch,
                                   GetHandle getHandle )
    {
        PROFILE_SCOPE( "Batch independent compute commands", Profiler::Category::Graphics );
        scratch.clear();
        GFX::MemoryBarrierCommand* batchBefore = nullptr;
        GFX::MemoryBarrierCommand batchAfter{};
        const auto flush = [&]
        {
            if ( batchBefore != nullptr )
            {
                GFX::EnqueueCommand( buffer, batchAfter );
                batchBefore = nullptr;
                batchAfter._bufferLocks.clear();
                scratch.clear();
            }
        };

        for ( const RenderPackage* package : packages )
        {
            if ( !DIVIDE_ASSERT( package != nullptr, "Null render package in compute batch" ) )
            {
                flush();
                continue;
            }
            const Handle<GFX::CommandBuffer> handle = getHandle( *package );
            if ( handle == INVALID_HANDLE<GFX::CommandBuffer> )
            {
                continue;
            }
            GFX::CommandBuffer* source = GFX::Get( handle );
            if ( !DIVIDE_ASSERT( source != nullptr, "Invalid render-package command buffer" ) )
            {
                flush();
                continue;
            }
            if ( source->commands().empty() )
            {
                continue;
            }

            GFX::MemoryBarrierCommand* before = nullptr;
            GFX::MemoryBarrierCommand* after = nullptr;
            if ( !package->independentCompute() )
            {
                flush();
                buffer.add( *source );
                continue;
            }
            if ( !ValidComputeBarriers( *source, before, after ) )
            {
                Console::errorfn( "Independent render-package compute has unsupported commands or barriers; preserving order" );
                flush();
                buffer.add( *source );
                continue;
            }
            if ( !IndependentRanges( scratch, after->_bufferLocks ) )
            {
                flush();
            }
            if ( batchBefore == nullptr )
            {
                batchBefore = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( buffer );
            }
            const bool mergedBefore = GFX::Merge( batchBefore, before );
            const bool mergedAfter = GFX::Merge( &batchAfter, after );
            DIVIDE_ASSERT( mergedBefore && mergedAfter, "Failed to merge independent compute barriers" );
            scratch.insert( scratch.end(), after->_bufferLocks.begin(), after->_bufferLocks.end() );
            buffer.add( *source, 1u, source->commands().size() - 2u );
        }
        flush();
    }
}

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
    pkg.independentCompute(false);
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

void AppendPreparationCommands( const std::span<RenderPackage* const> packages, GFX::CommandBuffer& buffer, BufferLocks& scratch )
{
    AppendIndependentCompute( packages, buffer, scratch, []( const RenderPackage& pkg ) { return pkg._preparationCommands; } );
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

void AppendOcclusionCommands( const std::span<RenderPackage* const> packages, GFX::CommandBuffer& buffer, BufferLocks& scratch )
{
    AppendIndependentCompute( packages, buffer, scratch, []( const RenderPackage& pkg ) { return pkg._occlusionCommands; } );
}

}//namespace Divide
