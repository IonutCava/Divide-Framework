

#include "Headers/CommandBuffer.h"
#include "Headers/CommandBufferPool.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Utility/Headers/Localization.h"

namespace Divide::GFX
{

namespace
{
    [[nodiscard]] FORCE_INLINE bool ShouldSkipBatch( const CommandType type ) noexcept
    {
        switch ( type )
        {
            case CommandType::BEGIN_DEBUG_SCOPE:
            case CommandType::END_DEBUG_SCOPE:
            case CommandType::ADD_DEBUG_MESSAGE:
                return true;
            default: break;
        }
        return false;
    }

    [[nodiscard]] inline bool RemoveEmptyDrawCommands( DrawCommand::CommandContainer& commands )
    {
        return dvd_erase_if( commands, []( const GenericDrawCommand& cmd ) noexcept { return cmd._drawCount == 0u; } );
    }

    [[nodiscard]] inline bool EraseEmptyCommands( CommandBuffer::CommandList& commands )
    {
        return dvd_erase_if( commands, []( const CommandBase* entry ) noexcept {  return entry == nullptr; } );
    }

    [[nodiscard]] inline bool RemoveEmptyLocks( GFX::MemoryBarrierCommand* memCmd )
    {
        bool ret = false;
        if ( dvd_erase_if( memCmd->_bufferLocks, []( const BufferLock& lock )
                           {
                               return lock._buffer == nullptr || lock._range._length == 0u;
                           } ) )
        {
            ret = true;
        }

        if (dvd_erase_if( memCmd->_textureLayoutChanges, []( const TextureLayoutChange& lock )
                        {
                            return lock._sourceLayout == lock._targetLayout || lock._targetView._srcTexture == nullptr;
                        } ) )
        {
            ret = true;
        }

        return ret;
    }
}; //namespace

    void ResetCommandBufferQueue( CommandBufferQueue& queue )
    {
        for (CommandBufferQueue::Entry& entry : queue._commandBuffers)
        {
            if (entry._owning)
            {
                DeallocateCommandBuffer( entry._buffer );
            }
        }

        efficient_clear(queue._commandBuffers);
    }

    void AddCommandBufferToQueue( CommandBufferQueue& queue, const Handle<GFX::CommandBuffer>& commandBuffer )
    {
        queue._commandBuffers.emplace_back(
        CommandBufferQueue::Entry
        {
            ._buffer = commandBuffer,
            ._owning = false
        });
    }  
    
    void AddCommandBufferToQueue( CommandBufferQueue& queue, Handle<GFX::CommandBuffer>&& commandBuffer )
    {
        queue._commandBuffers.emplace_back(
        CommandBufferQueue::Entry
        {
            ._buffer = MOV(commandBuffer),
            ._owning = true
        });
    }

    CommandBuffer::CommandBuffer( const size_t reservedCmdCount )
    {
        if ( _commands.max_size() < reservedCmdCount )
        {
            _commands.reserve( reservedCmdCount );
        }
    }

    CommandBuffer::~CommandBuffer()
    {
        clear();
    }

    void CommandBuffer::clear()
    {
        for (CommandBase*& cmd : _commands)
        {
            if (cmd != nullptr)
            {
                cmd->DeleteCmd( cmd );
            }
        }

        efficient_clear( _commands );

        _batched = true;
    }

    void CommandBuffer::add( const CommandBuffer& other )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _commands.reserve( _commands.size() + other._commands.size() );

        for ( CommandBase* cmd : other._commands )
        {
            cmd->addToBuffer( this );
        }
        _batched = false;
    }

    void CommandBuffer::add( Handle<CommandBuffer> other )
    {
        DIVIDE_ASSERT(other != INVALID_HANDLE<GFX::CommandBuffer> && other._ptr != nullptr);

        add(*other._ptr);
    }

    void CommandBuffer::batch()
    {
        if ( _batched ) [[unlikely]]
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        clean();

        CommandBase* prevCommand = nullptr;
        do
        {
            PROFILE_SCOPE( "TRY_MERGE_LOOP", Profiler::Category::Graphics );

            bool tryMerge = true;
            while ( tryMerge )
            {
                tryMerge = false;
                prevCommand = nullptr;
                CommandType prevType = CommandType::COUNT;
                for ( CommandBase*& cmd : _commands )
                {
                    if ( cmd == nullptr || ShouldSkipBatch( cmd->type() ) )
                    {
                        continue;
                    }

                    PROFILE_SCOPE( "TRY_MERGE_LOOP_STEP", Profiler::Category::Graphics );

                    if ( prevCommand != nullptr &&
                         prevType == cmd->type() &&
                         TryMergeCommands( cmd->type(), prevCommand, cmd ) )
                    {
                        cmd->DeleteCmd( cmd );
                        tryMerge = true;
                    }
                    else
                    {
                        prevType = cmd->type();
                        prevCommand = cmd;
                    }
                }
            }
        }
        while ( EraseEmptyCommands( _commands ) );

        // If we don't have any actual work to do, clear everything
        bool hasWork = false;
        for ( CommandBase* cmd : _commands )
        {
            if ( hasWork )
            {
                break;
            }

            switch ( cmd->type() )
            {
                case CommandType::BEGIN_RENDER_PASS:
                case CommandType::READ_BUFFER_DATA:
                case CommandType::COMPUTE_MIPMAPS:
                case CommandType::CLEAR_BUFFER_DATA:
                case CommandType::DISPATCH_COMPUTE:
                case CommandType::MEMORY_BARRIER:
                case CommandType::DRAW_COMMANDS:
                case CommandType::BIND_SHADER_RESOURCES:
                case CommandType::BLIT_RT:
                case CommandType::SEND_PUSH_CONSTANTS:
                case CommandType::SET_CAMERA:
                case CommandType::PUSH_CAMERA:
                case CommandType::POP_CAMERA:
                case CommandType::SET_CLIP_PLANES:
                case CommandType::SET_SCISSOR:
                case CommandType::SET_VIEWPORT:
                case CommandType::PUSH_VIEWPORT:
                case CommandType::POP_VIEWPORT:
                {
                    hasWork = true;
                } break;
                case CommandType::READ_TEXTURE:
                {
                    const ReadTextureCommand* crtCmd = cmd->As<ReadTextureCommand>();
                    hasWork = crtCmd->_texture != nullptr && crtCmd->_callback;
                } break;
                case CommandType::COPY_TEXTURE:
                {
                    const CopyTextureCommand* crtCmd = cmd->As<CopyTextureCommand>();
                    hasWork = crtCmd->_source != nullptr && crtCmd->_destination != nullptr;
                }break;
                case CommandType::CLEAR_TEXTURE:
                {
                    const ClearTextureCommand* crtCmd = cmd->As<ClearTextureCommand>();
                    hasWork = crtCmd->_texture != nullptr;
                }break;
                case CommandType::BIND_PIPELINE:
                {
                    const BindPipelineCommand* crtCmd = cmd->As<BindPipelineCommand>();
                    hasWork = crtCmd->_pipeline != nullptr && crtCmd->_pipeline->stateHash() != 0u;
                }break;
                default: break;
            };
        }

        if ( hasWork )
        {
            const auto [error, lastCmdIndex] = validate();
            if ( error != GFX::ErrorType::NONE )
            {
                Console::errorfn( LOCALE_STR( "ERROR_GFX_INVALID_COMMAND_BUFFER" ), lastCmdIndex, toString().c_str() );
                DIVIDE_UNEXPECTED_CALL_MSG( Util::StringFormat( "GFX::CommandBuffer::batch error [ {} ]: Invalid command buffer. Check error log!", GFX::Names::errorType[to_base( error )] ).c_str() );
            }
        }
        else
        {
            clear();
        }

        _batched = true;
    }

    void CommandBuffer::clean()
    {
        if ( _commands.empty() ) [[unlikely]]
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        while( cleanInternal() )
        {
            PROFILE_SCOPE("Clean Loop", Profiler::Category::Graphics);
        }
    }

    bool CommandBuffer::cleanInternal()
    {
        const Pipeline* prevPipeline = nullptr;
        const Rect<I32>* prevScissorRect = nullptr;
        const Rect<I32>* prevViewportRect = nullptr;
        const DescriptorSet* prevDescriptorSet = nullptr;

        bool ret = false;

        for ( CommandBase*& cmd : _commands )
        {
            bool erase = false;

            switch ( cmd->type() )
            {
                case CommandType::DRAW_COMMANDS:
                {
                    PROFILE_SCOPE( "Clean Draw Commands", Profiler::Category::Graphics );

                    DrawCommand::CommandContainer& cmds = cmd->As<DrawCommand>()->_drawCommands;
                    if ( cmds.size() == 1 )
                    {
                        erase = cmds.begin()->_drawCount == 0u;
                    }
                    else
                    {
                        erase = RemoveEmptyDrawCommands( cmds ) && cmds.empty();
                    }
                } break;
                case CommandType::BIND_PIPELINE:
                {
                    PROFILE_SCOPE( "Clean Pipelines", Profiler::Category::Graphics );

                    const Pipeline* pipeline = cmd->As<BindPipelineCommand>()->_pipeline;
                    // If the current pipeline is identical to the previous one, remove it
                    if ( prevPipeline == nullptr || prevPipeline->stateHash() != pipeline->stateHash() )
                    {
                        prevPipeline = pipeline;
                    }
                    else
                    {
                        erase = true;
                    }
                }break;
                case CommandType::SEND_PUSH_CONSTANTS:
                {
                    PROFILE_SCOPE( "Clean Push Constants", Profiler::Category::Graphics );

                    erase = cmd->As<SendPushConstantsCommand>()->_constants.empty();
                }break;
                case CommandType::BIND_SHADER_RESOURCES:
                {
                    PROFILE_SCOPE( "Clean Descriptor Sets", Profiler::Category::Graphics );

                    auto bindCmd = cmd->As<BindShaderResourcesCommand>();
                    if ( bindCmd->_usage != DescriptorSetUsage::COUNT && (prevDescriptorSet == nullptr || *prevDescriptorSet != bindCmd->_set) )
                    {
                        prevDescriptorSet = &bindCmd->_set;
                    }
                    else
                    {
                        erase = true;
                    }
                }break;
                case CommandType::MEMORY_BARRIER:
                {
                    auto memCmd = cmd->As<MemoryBarrierCommand>();
                    if (RemoveEmptyLocks( memCmd ))
                    {
                        erase = IsEmpty( memCmd->_bufferLocks ) &&
                                IsEmpty( memCmd->_textureLayoutChanges);
                    }
                } break;
                case CommandType::SET_SCISSOR:
                {
                    PROFILE_SCOPE( "Clean Scissor", Profiler::Category::Graphics );

                    const Rect<I32>& scissorRect = cmd->As<SetScissorCommand>()->_rect;
                    if ( prevScissorRect == nullptr || *prevScissorRect != scissorRect )
                    {
                        prevScissorRect = &scissorRect;
                    }
                    else
                    {
                        erase = true;
                    }
                } break;
                case CommandType::READ_BUFFER_DATA:
                {
                    auto readCmd = cmd->As<ReadBufferDataCommand>();
                    erase = readCmd->_buffer == nullptr ||
                            readCmd->_target.first == nullptr ||
                            readCmd->_elementCount == 0u ||
                            readCmd->_target.second == 0u;
                } break;
                case CommandType::SET_VIEWPORT:
                {
                    PROFILE_SCOPE( "Clean Viewport", Profiler::Category::Graphics );

                    const Rect<I32>& viewportRect = cmd->As<SetViewportCommand>()->_viewport;

                    if ( prevViewportRect == nullptr || *prevViewportRect != viewportRect )
                    {
                        prevViewportRect = &viewportRect;
                    }
                    else
                    {
                        erase = true;
                    }
                } break;
                default: break;
            };

            if ( erase )
            {
                cmd->DeleteCmd(cmd);
                ret = true;
            }
        }

        {
            PROFILE_SCOPE( "Remove redundant Pipelines", Profiler::Category::Graphics );

            // Remove redundant pipeline changes
            CommandBase* prev = nullptr;
            for ( CommandBase* cmd : _commands )
            {
                if ( (cmd != nullptr  && cmd->type() == CommandType::BIND_PIPELINE) && // current command is a bind pipeline request 
                     (prev != nullptr && prev->type() == CommandType::BIND_PIPELINE))  // previous command was also a bind pipeline request
                {
                    prev->DeleteCmd(prev); //Remove the previous bind pipeline request as it's redundant
                    ret = true;
                }

                prev = cmd;
            }
        }

        {
            PROFILE_SCOPE( "Remove invalid commands", Profiler::Category::Graphics );
            if (EraseEmptyCommands( _commands ))
            {
                ret = true;
            }
        }

        return ret;
    }

    // New use cases that emerge from production work should be checked here.
    std::pair<ErrorType, size_t> CommandBuffer::validate() const
    {
        if constexpr( !Config::ENABLE_GPU_VALIDATION )
        {
            return { ErrorType::NONE, 0u };
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        size_t cmdIndex = 0u;
        bool pushedPass = false, pushedQuery = false, hasPipeline = false;

        I32 pushedDebugScope = 0, pushedCamera = 0, pushedViewport = 0;

        for ( CommandBase* cmd : _commands )
        {
            cmdIndex++;
            switch ( cmd->type() )
            {
                case CommandType::BEGIN_RENDER_PASS:
                {
                    if ( pushedPass )
                    {
                        return { ErrorType::MISSING_END_RENDER_PASS, cmdIndex };
                    }
                    pushedPass = true;

                    auto beginRenderPassCmd = cmd->As<GFX::BeginRenderPassCommand>();
                    for ( const auto& it : beginRenderPassCmd->_descriptor._writeLayers )
                    {
                        if ( it._layer == INVALID_INDEX )
                        {
                            return { ErrorType::INVALID_BEGIN_RENDER_PASS, cmdIndex };
                        }
                    }
                } break;
                case CommandType::END_RENDER_PASS:
                {
                    if ( !pushedPass )
                    {
                        return { ErrorType::MISSING_BEGIN_RENDER_PASS, cmdIndex };
                    }
                    pushedPass = false;
                } break;
                case CommandType::BEGIN_GPU_QUERY:
                {
                    if ( pushedQuery )
                    {
                        return { ErrorType::MISSING_END_GPU_QUERY, cmdIndex };
                    }
                    pushedQuery = true;
                } break;
                case CommandType::END_GPU_QUERY:
                {
                    if ( !pushedQuery )
                    {
                        return { ErrorType::MISSING_BEGIN_GPU_QUERY, cmdIndex };
                    }
                    pushedQuery = false;
                } break;
                case CommandType::BEGIN_DEBUG_SCOPE:
                {
                    ++pushedDebugScope;
                } break;
                case CommandType::END_DEBUG_SCOPE:
                {
                    if ( pushedDebugScope == 0 )
                    {
                        return { ErrorType::MISSING_PUSH_DEBUG_SCOPE, cmdIndex };
                    }
                    --pushedDebugScope;
                } break;
                case CommandType::PUSH_CAMERA:
                {
                    ++pushedCamera;
                }break;
                case CommandType::POP_CAMERA:
                {
                    --pushedCamera;
                }break;
                case CommandType::PUSH_VIEWPORT:
                {
                    ++pushedViewport;
                }break;
                case CommandType::POP_VIEWPORT:
                {
                    --pushedViewport;
                }break;
                case CommandType::BIND_PIPELINE:
                {
                    if ( !pushedPass )
                    {
                        if ( cmd->As<GFX::BindPipelineCommand>()->_pipeline->descriptor()._primitiveTopology != PrimitiveTopology::COMPUTE)
                        {
                            return { ErrorType::INVALID_RENDER_PASS_FOR_PIPELINE, cmdIndex };
                        }
                    }
                    hasPipeline = true;
                } break;
                case CommandType::DISPATCH_COMPUTE:
                {
                    if ( !hasPipeline )
                    {
                        return { ErrorType::MISSING_VALID_PIPELINE, cmdIndex };
                    }
                    const vec3<U32>& workGroupCount = cmd->As<GFX::DispatchComputeCommand>()->_computeGroupSize;
                    if ( !(workGroupCount.x > 0 &&
                            workGroupCount.x < GFXDevice::GetDeviceInformation()._maxWorgroupCount[0] &&
                            workGroupCount.y < GFXDevice::GetDeviceInformation()._maxWorgroupCount[1] &&
                            workGroupCount.z < GFXDevice::GetDeviceInformation()._maxWorgroupCount[2]) )
                    {
                        return { ErrorType::INVALID_DISPATCH_COUNT, cmdIndex };
                    }

                } break;
                case CommandType::DRAW_COMMANDS:
                {
                    if ( !hasPipeline )
                    {
                        return { ErrorType::MISSING_VALID_PIPELINE, cmdIndex };
                    }
                }break;
                case CommandType::BIND_SHADER_RESOURCES:
                {
                    const GFX::BindShaderResourcesCommand* resCmd = cmd->As<GFX::BindShaderResourcesCommand>();
                    if ( resCmd->_usage == DescriptorSetUsage::COUNT )
                    {
                        return { ErrorType::INVALID_DESCRIPTOR_SET, cmdIndex };
                    }
                    for ( U8 i = 0u; i < resCmd->_set._bindingCount; ++i )
                    {
                        const DescriptorSetBinding& binding = resCmd->_set._bindings[i];
                        if ( binding._shaderStageVisibility == to_base( ShaderStageVisibility::COUNT ) )
                        {
                            return { ErrorType::INVALID_DESCRIPTOR_SET, cmdIndex };
                        }
                    }
                }break;
                default:
                {
                    // no requirements yet
                }break;
            };
        }

        if ( pushedPass )
        {
            return { ErrorType::MISSING_END_RENDER_PASS, cmdIndex };
        }
        if ( pushedDebugScope != 0 )
        {
            return { ErrorType::MISSING_POP_DEBUG_SCOPE, cmdIndex };
        }
        if ( pushedCamera != 0 )
        {
            return { ErrorType::MISSING_POP_CAMERA, cmdIndex };
        }
        if ( pushedViewport != 0 )
        {
            return { ErrorType::MISSING_POP_VIEWPORT, cmdIndex };
        }

        return { ErrorType::NONE, cmdIndex };
    }

    void ToString( const CommandBase& cmd, const CommandType type, I32& crtIndent, string& out )
    {
        const auto append = []( string& target, const string& text, const I32 indent )
        {
            for ( I32 i = 0; i < indent; ++i )
            {
                target.append( "    " );
            }
            target.append( text );
        };

        switch ( type )
        {
            case CommandType::BEGIN_RENDER_PASS:
            case CommandType::BEGIN_DEBUG_SCOPE:
            {
                append( out, GFX::ToString( cmd, type, to_U16( crtIndent ) ), crtIndent );
                ++crtIndent;
            }break;
            case CommandType::END_RENDER_PASS:
            case CommandType::END_DEBUG_SCOPE:
            {
                --crtIndent;
                append( out, GFX::ToString( cmd, type, to_U16( crtIndent ) ), crtIndent );
            }break;
            default:
            {
                append( out, GFX::ToString( cmd, type, to_U16( crtIndent ) ), crtIndent );
            }break;
        }
    }

    string CommandBuffer::toString() const
    {
        I32 crtIndent = 0;
        string out = "\n\n\n\n";
        size_t idx = 0u;
        for ( CommandBase* cmd : _commands )
        {
            out.append( "[ " + std::to_string( idx++ ) + " ]: " );
            ToString( *cmd, cmd->type(), crtIndent, out );
            out.append( "\n" );
        }
        out.append( "\n\n\n\n" );

        return out;
    }

    bool BatchDrawCommands( GenericDrawCommand& previousGDC, GenericDrawCommand& currentGDC ) noexcept
    {
        // Instancing is not compatible with MDI. Well, it might be, but I can't be bothered a.t.m. to implement it -Ionut
        if ( previousGDC._cmd.instanceCount != currentGDC._cmd.instanceCount && (previousGDC._cmd.instanceCount > 1 || currentGDC._cmd.instanceCount > 1) )
        {
            return false;
        }

        // Batch-able commands must share the same buffer and other various state
        if ( !Compatible( previousGDC, currentGDC ) )
        {
            return false;
        }
        const U32 offsetCrt = to_U32( currentGDC._commandOffset );
        const U32 offsetPrev = to_U32( previousGDC._commandOffset );
        if ( offsetCrt - offsetPrev == previousGDC._drawCount )
        {
            // If the rendering commands are batch-able, increase the draw count for the previous one
            previousGDC._drawCount += currentGDC._drawCount;
            // And set the current command's draw count to zero so it gets removed from the list later on
            currentGDC._drawCount = 0;
            return true;
        }

        return false;
    }

    bool Merge( DrawCommand* prevCommand, DrawCommand* crtCommand )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        auto& commands = prevCommand->_drawCommands;
        commands.insert( cend( commands ),
                            eastl::make_move_iterator( begin( crtCommand->_drawCommands ) ),
                            eastl::make_move_iterator( end( crtCommand->_drawCommands ) ) );
        efficient_clear( crtCommand->_drawCommands );

        {
            PROFILE_SCOPE( "Merge by offset", Profiler::Category::Graphics );
            eastl::sort( begin( commands ),
                            end( commands ),
                            []( const GenericDrawCommand& a, const GenericDrawCommand& b ) noexcept -> bool
                            {
                                return a._commandOffset < b._commandOffset;
                            } );

            do
            {
                const size_t commandCount = commands.size();
                for ( size_t previousCommandIndex = 0, currentCommandIndex = 1;
                        currentCommandIndex < commandCount;
                        ++currentCommandIndex )
                {
                    if ( !BatchDrawCommands( commands[previousCommandIndex], commands[currentCommandIndex] ) )
                    {
                        previousCommandIndex = currentCommandIndex;
                    }
                }
            }
            while ( RemoveEmptyDrawCommands( commands ) );
        }

        return true;
    }

    bool Merge( GFX::MemoryBarrierCommand* lhs, GFX::MemoryBarrierCommand* rhs )
    {
        for ( const BufferLock& otherLock : rhs->_bufferLocks )
        {
            bool found = false;
            for ( BufferLock& ourLock : lhs->_bufferLocks )
            {
                if ( ourLock._buffer && 
                     otherLock._buffer &&
                     ourLock._buffer->getGUID() == otherLock._buffer->getGUID() )
                {
                    found = true;
                }

                if ( found)
                {
                    if ( ourLock._type == otherLock._type )
                    {
                        Merge( ourLock._range, otherLock._range );
                        break;
                    }
                    else
                    {
                        found = false;
                    }
                }
            }
            if ( !found )
            {
                lhs->_bufferLocks.push_back( otherLock );
            }
        }

        for ( const TextureLayoutChange& otherChange : rhs->_textureLayoutChanges )
        {
            bool found = false;
            for ( TextureLayoutChange& ourChange : lhs->_textureLayoutChanges )
            {
                if ( ourChange._targetView == otherChange._targetView)
                {
                    found = true;
                    ourChange._targetLayout = otherChange._targetLayout;
                    break;
                }
            }
            if ( !found )
            {
                lhs->_textureLayoutChanges.push_back(otherChange);
            }
        }

        return true;
    }

}; //namespace Divide::GFX
