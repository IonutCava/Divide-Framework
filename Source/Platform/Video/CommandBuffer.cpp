

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
    FORCE_INLINE size_t GetReserveSize( const U8 typeIndex ) noexcept
    {
        switch ( static_cast<CommandType>(typeIndex) )
        {
            case CommandType::BIND_SHADER_RESOURCES: return 2;
            case CommandType::SEND_PUSH_CONSTANTS: return 3;
            case CommandType::DRAW_COMMANDS: return 4;
            default: break;
        }

        return 1;
    }

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

    [[nodiscard]] inline bool EraseEmptyCommands( CommandBuffer::CommandOrderContainer& commandOrder )
    {
        return dvd_erase_if( commandOrder, []( const CommandEntry& entry ) noexcept { return entry._data == CommandEntry::INVALID_ENTRY_ID;} );
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

    CommandEntry::CommandEntry( const CommandType type, const U32 element ) noexcept
        : _idx{ ._element = element, ._type = to_U8( type ) }
    {
    }

    CommandBuffer::CommandBuffer( )
    {
        _commandOrder.resize(0);
        _commandCount.fill( 0u );

        for ( U8 i = 0; i < to_base( CommandType::COUNT ); ++i )
        {
            _collection[i].resize(0);

            const auto reserveSize = GetReserveSize( i );
            if ( reserveSize > 2 )
            {
                _collection[i].reserve( reserveSize );
            }
        }
    }

    CommandBuffer::~CommandBuffer()
    {
        clear(true);
    }

    CommandEntry CommandBuffer::addCommandEntry( const CommandType type )
    {
        _batched = false;
        return _commandOrder.emplace_back( type, _commandCount[to_U8( type )]++ );
    }

    void CommandBuffer::add( const CommandBuffer& other )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( U8 i = 0; i < to_base(CommandType::COUNT); ++i )
        {
            _collection[i].reserve( _collection[i].size() + other._commandCount[i] );
        }

        for ( const CommandEntry& cmd : other._commandOrder )
        {
            other.get(cmd)->addToBuffer( this );
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
                for ( CommandEntry& entry : _commandOrder )
                {
                    const CommandType cmdType = static_cast<CommandType>(entry._idx._type);
                    if ( entry._data == CommandEntry::INVALID_ENTRY_ID || ShouldSkipBatch( cmdType ) )
                    {
                        continue;
                    }

                    PROFILE_SCOPE( "TRY_MERGE_LOOP_STEP", Profiler::Category::Graphics );

                    CommandBase* crtCommand = get<CommandBase>( entry );

                    if ( prevCommand != nullptr &&
                         prevType == cmdType &&
                         TryMergeCommands( cmdType, prevCommand, crtCommand ) )
                    {
                        --_commandCount[entry._idx._type];
                        entry._data = CommandEntry::INVALID_ENTRY_ID;
                        tryMerge = true;
                    }
                    else
                    {
                        prevType = cmdType;
                        prevCommand = crtCommand;
                    }
                }
            }
        }
        while ( EraseEmptyCommands( _commandOrder ) );

        // If we don't have any actual work to do, clear everything
        bool hasWork = false;
        for ( const CommandEntry& cmd : _commandOrder )
        {
            if ( hasWork )
            {
                break;
            }

            switch ( static_cast<CommandType>(cmd._idx._type) )
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
                    const ReadTextureCommand* crtCmd = get<ReadTextureCommand>( cmd );
                    hasWork = crtCmd->_texture != nullptr && crtCmd->_callback;
                } break;
                case CommandType::COPY_TEXTURE:
                {
                    const CopyTextureCommand* crtCmd = get<CopyTextureCommand>( cmd );
                    hasWork = crtCmd->_source != nullptr && crtCmd->_destination != nullptr;
                }break;
                case CommandType::CLEAR_TEXTURE:
                {
                    const ClearTextureCommand* crtCmd = get<ClearTextureCommand>( cmd );
                    hasWork = crtCmd->_texture != nullptr;
                }break;
                case CommandType::BIND_PIPELINE:
                {
                    const BindPipelineCommand* crtCmd = get<BindPipelineCommand>( cmd );
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
            _commandOrder.clear();
            _commandCount.fill(0u);
        }

        _batched = true;
    }

    void CommandBuffer::clear( const bool clearMemory )
    {
        _commandCount.fill( 0u );

        _commandOrder.clear();

        if ( clearMemory )
        {
            for ( U8 i = 0u; i < to_base( CommandType::COUNT ); ++i )
            {
                CommandList& col = _collection[i];

                for ( CommandBase*& cmd : col )
                {
                    cmd->DeleteCmd( cmd );
                }
                col.clear();
            }
        }

        _batched = true;
    }

    void CommandBuffer::clean()
    {
        if ( _commandOrder.empty() ) [[unlikely]]
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

        for ( CommandEntry& cmd : _commandOrder )
        {
            bool erase = false;

            switch ( static_cast<CommandType>(cmd._idx._type) )
            {
                case CommandType::DRAW_COMMANDS:
                {
                    PROFILE_SCOPE( "Clean Draw Commands", Profiler::Category::Graphics );

                    DrawCommand::CommandContainer& cmds = get<DrawCommand>( cmd )->_drawCommands;
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

                    const Pipeline* pipeline = get<BindPipelineCommand>( cmd )->_pipeline;
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

                    erase = get<SendPushConstantsCommand>( cmd )->_constants.empty();
                }break;
                case CommandType::BIND_SHADER_RESOURCES:
                {
                    PROFILE_SCOPE( "Clean Descriptor Sets", Profiler::Category::Graphics );

                    auto bindCmd = get<BindShaderResourcesCommand>(cmd);
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
                    auto memCmd = get<MemoryBarrierCommand>( cmd );
                    if (RemoveEmptyLocks( memCmd ))
                    {
                        erase = IsEmpty( memCmd->_bufferLocks ) &&
                                IsEmpty( memCmd->_textureLayoutChanges);
                    }
                } break;
                case CommandType::SET_SCISSOR:
                {
                    PROFILE_SCOPE( "Clean Scissor", Profiler::Category::Graphics );

                    const Rect<I32>& scissorRect = get<SetScissorCommand>( cmd )->_rect;
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
                    auto readCmd = get<ReadBufferDataCommand>( cmd );
                    erase = readCmd->_buffer == nullptr ||
                            readCmd->_target.first == nullptr ||
                            readCmd->_elementCount == 0u ||
                            readCmd->_target.second == 0u;
                } break;
                case CommandType::SET_VIEWPORT:
                {
                    PROFILE_SCOPE( "Clean Viewport", Profiler::Category::Graphics );

                    const Rect<I32>& viewportRect = get<SetViewportCommand>( cmd )->_viewport;

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
                --_commandCount[cmd._idx._type];
                cmd._data = CommandEntry::INVALID_ENTRY_ID;

                ret = true;
            }
        }

        {
            PROFILE_SCOPE( "Remove redundant Pipelines", Profiler::Category::Graphics );

            // Remove redundant pipeline changes
            CommandEntry* entry = eastl::next( begin( _commandOrder ) );
            for ( ; entry != cend( _commandOrder ); ++entry )
            {
                const U8 typeIndex = entry->_idx._type;

                if ( static_cast<CommandType>(typeIndex) == CommandType::BIND_PIPELINE &&
                     eastl::prev( entry )->_idx._type == typeIndex )
                {
                    --_commandCount[typeIndex];
                    entry->_data = CommandEntry::INVALID_ENTRY_ID;
                    ret = true;
                }
            }
        }
        {
            PROFILE_SCOPE( "Remove invalid commands", Profiler::Category::Graphics );
            if (erase_if( _commandOrder, []( const CommandEntry& entry ) noexcept
                         {
                             return entry._data == CommandEntry::INVALID_ENTRY_ID;
                         } ) > 0u)
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

        for ( const CommandEntry& cmd : _commandOrder )
        {
            cmdIndex++;
            switch ( static_cast<CommandType>(cmd._idx._type) )
            {
                case CommandType::BEGIN_RENDER_PASS:
                {
                    if ( pushedPass )
                    {
                        return { ErrorType::MISSING_END_RENDER_PASS, cmdIndex };
                    }
                    pushedPass = true;

                    auto beginRenderPassCmd = get<GFX::BeginRenderPassCommand>( cmd );
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
                        if (get<GFX::BindPipelineCommand>( cmd )->_pipeline->descriptor()._primitiveTopology != PrimitiveTopology::COMPUTE)
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
                    const vec3<U32>& workGroupCount = get<GFX::DispatchComputeCommand>( cmd )->_computeGroupSize;
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
                    const GFX::BindShaderResourcesCommand* resCmd = get<GFX::BindShaderResourcesCommand>( cmd );
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

    void CommandBuffer::ToString( const CommandBase& cmd, const CommandType type, I32& crtIndent, string& out )
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
        for ( const CommandEntry& cmd : _commandOrder )
        {
            out.append( "[ " + std::to_string( idx++ ) + " ]: " );
            ToString( *get<CommandBase>( cmd ), static_cast<CommandType>(cmd._idx._type), crtIndent, out );
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
